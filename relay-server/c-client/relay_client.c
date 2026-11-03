/* relay_client.c - minimal C test peer for the LSM relay protocol v1.
 * Depends only on Monocypher 4 (same as the lan-secure-messenger-xp core)
 * and BSD sockets. Blobs are XChaCha20-Poly1305 sealed (nonce24|ct|tag16)
 * with a test key, standing in for Noise transport messages.
 *
 *   relay_client HOST PORT PASS NAME echo SECONDS
 *   relay_client HOST PORT PASS NAME send PEERNAME COUNT SIZE
 *   relay_client --selftest
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "third_party/monocypher.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define CLOSESOCK closesocket
#else
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/time.h>
typedef int sock_t;
#define CLOSESOCK close
#endif

enum { T_HELLO = 0x00, T_AUTH = 0x01, T_AUTH_OK = 0x02, T_AUTH_FAIL = 0x03,
       T_SEND = 0x10, T_DELIVER = 0x11, T_SEND_FAIL = 0x12, T_PING = 0x20,
       T_PONG = 0x21, T_LIST = 0x30, T_PEER_LIST = 0x40, T_PEER_JOIN = 0x41,
       T_PEER_LEAVE = 0x42, T_ERROR = 0x50, T_BYE = 0x7f };

static const uint8_t E2E_KEY[32] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};

static sock_t g_s;
static uint8_t *g_buf; static uint32_t g_max = 1024 * 1024 + 64;

static void die(const char *m) { fprintf(stderr, "FAIL %s\n", m); exit(1); }
static void be32(uint8_t *p, uint32_t v) { p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
static uint32_t rd32(const uint8_t *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }

static void rnd(uint8_t *b, size_t n) {
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f || fread(b, 1, n, f) != n) die("urandom");
    fclose(f);
}
static int read_all(uint8_t *b, size_t n) {
    while (n) { long r = recv(g_s, (char *)b, (int)n, 0); if (r <= 0) return -1; b += r; n -= (size_t)r; }
    return 0;
}
static int write_all(const uint8_t *b, size_t n) {
    while (n) { long r = send(g_s, (const char *)b, (int)n, 0); if (r <= 0) return -1; b += r; n -= (size_t)r; }
    return 0;
}
/* returns body length, *ty set; -1 on EOF/error */
static long read_frame(uint8_t *ty) {
    uint8_t h[4]; uint32_t n;
    if (read_all(h, 4)) return -1;
    n = rd32(h);
    if (n == 0 || n > g_max) die("bad frame length from server");
    if (read_all(ty, 1)) return -1;
    if (n > 1 && read_all(g_buf, n - 1)) return -1;
    return (long)n - 1;
}
static void write_frame(uint8_t ty, const uint8_t *a, size_t an, const uint8_t *b, size_t bn) {
    uint8_t h[5]; be32(h, (uint32_t)(1 + an + bn)); h[4] = ty;
    if (write_all(h, 5) || (an && write_all(a, an)) || (bn && write_all(b, bn))) die("write");
}

static void derive_key(uint8_t key[32], const char *pass, const uint8_t salt[16], uint32_t m_kib, uint32_t t) {
    crypto_argon2_config cfg = { CRYPTO_ARGON2_ID, m_kib, t, 1 };
    crypto_argon2_inputs in = { (const uint8_t *)pass, salt, (uint32_t)strlen(pass), 16 };
    void *work = malloc((size_t)m_kib * 1024);
    if (!work) die("oom");
    crypto_argon2(key, 32, work, cfg, in, crypto_argon2_no_extras);
    free(work);
}
static void auth_mac(uint8_t out[32], const uint8_t key[32], const char *ctx,
                     const uint8_t n1[32], const uint8_t n2[32], const uint8_t id[32]) {
    crypto_blake2b_ctx c;
    crypto_blake2b_keyed_init(&c, 32, key, 32);
    crypto_blake2b_update(&c, (const uint8_t *)ctx, strlen(ctx));
    crypto_blake2b_update(&c, n1, 32);
    crypto_blake2b_update(&c, n2, 32);
    crypto_blake2b_update(&c, id, 32);
    crypto_blake2b_final(&c, out);
}
static void test_id(uint8_t id[32], const char *name) { crypto_blake2b(id, 32, (const uint8_t *)name, strlen(name)); }

static size_t seal(uint8_t *out, const uint8_t *pt, size_t n) {
    rnd(out, 24);
    crypto_aead_lock(out + 24, out + 24 + n, E2E_KEY, out, NULL, 0, pt, n);
    return 24 + n + 16;
}
static int open_blob(uint8_t *pt, const uint8_t *b, size_t n) {
    if (n < 40) return -1;
    return crypto_aead_unlock(pt, b + n - 16, E2E_KEY, b, NULL, 0, b + 24, n - 40);
}

static void do_connect(const char *host, const char *port, const char *pass, const char *name, uint8_t me[32]) {
    struct addrinfo hints, *ai; uint8_t ty; long n; int one = 1;
    uint8_t salt[16], snonce[32], cnonce[32], key[32], mac[32], hdr[99];
    uint32_t m_kib, tc;
    memset(&hints, 0, sizeof hints); hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &ai)) die("resolve");
    g_s = socket(ai->ai_family, SOCK_STREAM, 0);
    if (connect(g_s, ai->ai_addr, (int)ai->ai_addrlen)) die("connect");
    freeaddrinfo(ai);
    setsockopt(g_s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
    n = read_frame(&ty);
    if (n < 61 || ty != T_HELLO || memcmp(g_buf, "LSMR", 4) || g_buf[4] != 1) die("bad HELLO");
    memcpy(salt, g_buf + 5, 16); m_kib = rd32(g_buf + 21); tc = rd32(g_buf + 25); memcpy(snonce, g_buf + 29, 32);
    if (m_kib > 1024 * 1024 || tc > 16) die("unreasonable KDF params");
    derive_key(key, pass, salt, m_kib, tc);
    test_id(me, name); rnd(cnonce, 32);
    auth_mac(mac, key, "LSMR-v1-client", snonce, cnonce, me);
    hdr[0] = 1; memcpy(hdr + 1, cnonce, 32); memcpy(hdr + 33, me, 32); memcpy(hdr + 65, mac, 32);
    hdr[97] = (uint8_t)(strlen(name) >> 8); hdr[98] = (uint8_t)strlen(name);
    write_frame(T_AUTH, hdr, 99, (const uint8_t *)name, strlen(name));
    n = read_frame(&ty);
    if (ty == T_AUTH_FAIL) { fprintf(stderr, "AUTH_FAIL code=%d\n", n > 0 ? g_buf[0] : -1); exit(2); }
    if (ty != T_AUTH_OK || n < 38) die("no AUTH_OK");
    auth_mac(mac, key, "LSMR-v1-server", cnonce, snonce, me);
    if (crypto_verify32(mac, g_buf)) die("server failed mutual auth");
    g_max = rd32(g_buf + 32) + 64;
    crypto_wipe(key, 32);
    printf("READY name=%s (C client) max_frame=%u heartbeat=%u\n", name, rd32(g_buf + 32), (g_buf[36] << 8) | g_buf[37]);
    fflush(stdout);
}

static int selftest(void) {
    /* Same vector as the Rust test `proto::tests::c_vector` */
    uint8_t salt[16], key[32], mac[32], a[32], b[32], c[32]; int i;
    memcpy(salt, "LSMR-default-slt", 16);
    memset(a, 1, 32); memset(b, 2, 32); memset(c, 3, 32);
    derive_key(key, "test-pass", salt, 19456, 2);
    auth_mac(mac, key, "LSMR-v1-client", a, b, c);
    for (i = 0; i < 32; i++) printf("%02x", key[i]);
    printf("\n");
    for (i = 0; i < 32; i++) printf("%02x", mac[i]);
    printf("\n");
    return 0;
}

int main(int argc, char **argv) {
    uint8_t me[32], dst[32], ty; long n;
#ifdef _WIN32
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
#endif
    if (argc == 2 && !strcmp(argv[1], "--selftest")) return selftest();
    if (argc < 7) { fprintf(stderr, "usage: see source header\n"); return 64; }
    g_buf = malloc(16 * 1024 * 1024 + 64);
    do_connect(argv[1], argv[2], argv[3], argv[4], me);
    if (!strcmp(argv[5], "echo")) {
        long secs = atol(argv[6]), got = 0; uint8_t *pt = malloc(16 * 1024 * 1024), *ct = malloc(16 * 1024 * 1024 + 64);
        struct timeval tv; tv.tv_sec = secs; tv.tv_usec = 0;
        setsockopt(g_s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
        while ((n = read_frame(&ty)) >= 0) {
            if (ty == T_PEER_LIST) printf("PEER_LIST %d\n", (g_buf[0] << 8) | g_buf[1]);
            else if (ty == T_PEER_JOIN) printf("JOIN %.*s\n", (int)(n - 34), g_buf + 34);
            else if (ty == T_DELIVER && n >= 32) {
                size_t pl = (size_t)n - 32;
                if (open_blob(pt, g_buf + 32, pl)) die("decrypt");
                memcpy(dst, g_buf, 32);
                write_frame(T_SEND, dst, 32, ct, seal(ct, pt, pl - 40));
                got++;
            } else if (ty == T_PING) write_frame(T_PONG, g_buf, (size_t)n, NULL, 0);
            fflush(stdout);
        }
        printf("DONE received=%ld\n", got);
        return 0;
    }
    if (!strcmp(argv[5], "send") && argc >= 9) {
        long count = atol(argv[7]), size = atol(argv[8]), i, got = 0, online = 0;
        uint8_t *pt = calloc(1, (size_t)size + 64), *ct = malloc((size_t)size + 64);
        test_id(dst, argv[6]);
        write_frame(T_LIST, NULL, 0, NULL, 0);
        while (!online && (n = read_frame(&ty)) >= 0) {
            if (ty == T_PEER_JOIN && !memcmp(g_buf, dst, 32)) online = 1;
            if (ty == T_PEER_LIST) {
                long k = (g_buf[0] << 8) | g_buf[1], p = 2;
                while (k-- > 0 && p + 34 <= n) { if (!memcmp(g_buf + p, dst, 32)) online = 1; p += 34 + ((g_buf[p + 32] << 8) | g_buf[p + 33]); }
            }
        }
        if (!online) die("peer never came online");
        printf("PEER_ONLINE %s\n", argv[6]);
        for (i = 0; i < count; i++) {
            memset(pt, (int)(i & 0xff), (size_t)size);
            write_frame(T_SEND, dst, 32, ct, seal(ct, pt, (size_t)size));
        }
        while (got < count && (n = read_frame(&ty)) >= 0) {
            if (ty == T_DELIVER) {
                if (memcmp(g_buf, dst, 32) || open_blob(pt, g_buf + 32, (size_t)n - 32)) die("bad echo");
                if ((long)n - 32 - 40 != size) die("echo size");
                got++;
            } else if (ty == T_SEND_FAIL) die("SEND_FAIL");
        }
        if (got != count) die("missing echoes");
        write_frame(T_BYE, NULL, 0, NULL, 0);
        printf("OK roundtrip count=%ld size=%ld (C client)\n", got, size);
        return 0;
    }
    fprintf(stderr, "unknown mode\n");
    return 64;
}
