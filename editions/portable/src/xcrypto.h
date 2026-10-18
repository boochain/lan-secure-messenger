/* Crypto glue for the XP edition: BLAKE2s, SHA-256, ChaCha20-Poly1305 (RFC 8439),
 * Noise_XX_25519_ChaChaPoly_BLAKE2s, Ed25519 via Monocypher. No OS crypto APIs. */
#ifndef XCRYPTO_H
#define XCRYPTO_H
#include <stdint.h>
#include <stddef.h>

typedef struct { uint32_t h[8], t[2], f[2]; uint8_t buf[64]; size_t c, outlen; } blake2s_ctx;
void blake2s_init(blake2s_ctx *c, size_t outlen);
void blake2s_update(blake2s_ctx *c, const void *in, size_t n);
void blake2s_final(blake2s_ctx *c, uint8_t *out);
void blake2s(uint8_t out[32], const void *in, size_t n);

typedef struct { uint32_t s[8]; uint64_t n; uint8_t buf[64]; size_t c; } sha256_ctx;
void sha256_init(sha256_ctx *c);
void sha256_update(sha256_ctx *c, const void *in, size_t n);
void sha256_final(sha256_ctx *c, uint8_t out[32]);
void sha256(uint8_t out[32], const void *in, size_t n);

/* RFC 8439 AEAD. out = ct || tag(16). */
void aead_seal(uint8_t *out, const uint8_t key[32], const uint8_t nonce[12],
               const uint8_t *ad, size_t adn, const uint8_t *pt, size_t n);
int aead_open(uint8_t *out, const uint8_t key[32], const uint8_t nonce[12],
              const uint8_t *ad, size_t adn, const uint8_t *ct, size_t n); /* n incl tag; 0=ok */

void rand_bytes(void *p, size_t n);

/* Noise XX handshake state */
typedef struct {
    uint8_t ck[32], h[32], k[32]; int has_k; uint64_t n;
    uint8_t s_sk[32], s_pk[32], e_sk[32], e_pk[32], re[32], rs[32];
    int initiator, step;
} noise_hs;
void noise_init(noise_hs *hs, int initiator, const uint8_t s_sk[32]);
/* returns message length, or -1 */
int noise_write(noise_hs *hs, uint8_t *out);
int noise_read(noise_hs *hs, const uint8_t *in, size_t n);
/* after 3 messages: send key / recv key */
void noise_split(noise_hs *hs, uint8_t tx[32], uint8_t rx[32]);
/* transport with explicit nonce: Noise ChaChaPoly nonce = 0^4 || LE64(n), empty AD */
void noise_enc(const uint8_t k[32], uint64_t n, const uint8_t *pt, size_t len, uint8_t *out);
int noise_dec(const uint8_t k[32], uint64_t n, const uint8_t *ct, size_t len, uint8_t *out);

/* seal with random 12-byte nonce prepended (v0.5 seal_at_rest / group text) */
uint8_t *seal_rand(const uint8_t key[32], const uint8_t *pt, size_t n, size_t *outn);
uint8_t *open_rand(const uint8_t key[32], const uint8_t *in, size_t n, size_t *outn);
#endif
