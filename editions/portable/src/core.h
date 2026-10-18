/* LAN Secure Messenger — XP edition engine (wire-compatible with v0.5, PROTO 5). */
#ifndef LSM_CORE_H
#define LSM_CORE_H
#include <stdint.h>
#include <stddef.h>

#ifndef LSM_APP_VERSION
#define LSM_APP_VERSION "0.5.0-xp.2" /* Linux builds pass -DLSM_APP_VERSION=\"0.5.0-linux.1\" */
#endif
#define FP_LEN 20 /* "XXXX-XXXX-XXXX-XXXX" + NUL */

enum { TRUST_UNKNOWN = 0, TRUST_TRUSTED = 1, TRUST_BLOCKED = 2 };
enum { ST_QUEUED = 0, ST_SENT, ST_DELIVERED, ST_READ, ST_RECEIVED, ST_FAILED };
enum { K_TEXT = 0, K_FILE, K_SYSTEM };
enum { TS_OFFERED = 0, TS_ACTIVE, TS_VERIFYING, TS_DONE, TS_FAILED, TS_REJECTED, TS_CANCELLED };

typedef struct {
    char fp[FP_LEN];
    char name[300];
    uint8_t ed[32]; int has_ed;
    int trust;
    char addr[64];          /* last known dial address ip:port */
    int64_t last_seen_ms;
    int online;             /* runtime */
    int rtt_ms;             /* runtime */
    char app_version[64];   /* runtime */
    int64_t last_announce;  /* runtime mono ms, 0 = never */
    int via_relay;          /* runtime: 1 = the live link goes through the relay server (UI badge 经服务器) */
} lsm_contact;

typedef struct {
    char id[40];            /* uuid text */
    char conv[64];          /* dm:<fp> or group:<uuid> */
    int out;                /* 1 = outgoing */
    char sender_fp[FP_LEN];
    char sender_name[300];
    int kind;
    char *body;             /* UTF-8, malloc */
    int64_t ts_ms;
    int status;
    char file_tid[40];
    char file_name[260];
    uint64_t file_size;
    char file_path[520];
    int unread;
} lsm_msg;

typedef struct {
    uint8_t id[16];
    char name[300];
    char creator_fp[FP_LEN];
    char members[64][FP_LEN]; int nmembers;
    uint8_t key[32];
    int64_t created_ms;
    uint64_t key_epoch;
} lsm_group;

typedef struct {
    uint8_t tid[16];
    char msg_id[40];
    char conv[64];
    char peer_fp[FP_LEN];
    int out;
    char name[260];
    uint64_t size;
    char mime[64];
    uint8_t sha[32];
    int has_group; uint8_t group_id[16];
    char path[520];          /* source path (out) / final path (in) */
    char part[520];
    uint64_t transferred;    /* out: sent offset ; in: written */
    uint64_t credit;         /* out: peer-acked bytes */
    int state;
    char error[256];
    void *fp_handle;         /* FILE* */
    void *hash;              /* sha256_ctx* (in) */
    uint64_t last_prog;
    int64_t offer_ts;
    int64_t last_credit_mono;
    int resumable;           /* peer announced CAP_RESUME: a link loss pauses (state offered) and resumes on reconnect */
    int accepted;            /* in: accepted once, a re-offer is resumed automatically */
    uint64_t resumed_from;   /* offset the last (re)start continued from; 0 = from the beginning */
    int hashing;             /* internal: 1 sender prefix check, 2 receiver re-hash of a restored .part (chunked) */
    uint64_t hash_want, hash_done; void *hctx; uint8_t hpre[32]; int reoffer_pending;
} lsm_transfer;

typedef struct lsm_core lsm_core;

typedef struct {
    const char *data_dir;   /* required, UTF-8 */
    int discovery_port;     /* 0 = 41234 */
    int listen_port;        /* 0 = 41235 */
    int announce_ms;        /* 0 = 3000 */
    const char *announce_targets; /* comma separated host[:port] */
    const char *name;       /* initial display name if none stored */
    int no_discovery;
    int auto_trust;
} lsm_opts;

lsm_core *lsm_start(const lsm_opts *o, char *err, size_t errn);
void lsm_stop(lsm_core *c);

/* events: returns 1 and fills buf with a UTF-8 line "kind\tfield..." */
int lsm_poll_event(lsm_core *c, char *buf, size_t n);

/* All state accessors must be bracketed by lock/unlock. */
void lsm_lock(lsm_core *c);
void lsm_unlock(lsm_core *c);
const char *lsm_my_fp(lsm_core *c);
const char *lsm_my_name(lsm_core *c);
int lsm_listen_port(lsm_core *c);
int lsm_contacts(lsm_core *c, lsm_contact **out);
int lsm_groups(lsm_core *c, lsm_group **out);
int lsm_messages(lsm_core *c, lsm_msg **out);
int lsm_transfers(lsm_core *c, lsm_transfer **out);
const char *lsm_download_dir(lsm_core *c);

/* Commands (take the lock internally). Return 0 on success, else fill err (UTF-8). */
int lsm_send_text(lsm_core *c, const char *conv, const char *text, char *msgid_out, char *err);
int lsm_send_file(lsm_core *c, const char *conv, const char *path, char *err);
int lsm_add_addr(lsm_core *c, const char *addr, char *err);
int lsm_set_trust(lsm_core *c, const char *fp, int trust, char *err);
int lsm_pin_offer(lsm_core *c, char pin_out[8]);
int lsm_pin_pair(lsm_core *c, const char *fp, const char *pin, char *err); /* async: event pin_result */
int lsm_group_create(lsm_core *c, const char *name, const char **fps, int n, char *gid_out, char *err);
int lsm_group_invite(lsm_core *c, const char *gid, const char **fps, int n, char *err);
int lsm_group_remove(lsm_core *c, const char *gid, const char *fp, char *err);
int lsm_group_leave(lsm_core *c, const char *gid, char *err);
int lsm_mark_read(lsm_core *c, const char *conv);
/* avatar initials, shared rule (WinUI Fmt.Initials, Android initials()): CJK first -> 1 char; Latin -> up to 2 Latin
 * word initials, never mixed with CJK; else the first code point. out gets UTF-8, NUL-terminated (needs 8 bytes). */
void lsm_initials(const char *name, char out[8]);
int lsm_delete_message(lsm_core *c, const char *id);   /* this device only; tombstoned; emits "msg_delete\t<id>\t<conv>" */
int lsm_set_name(lsm_core *c, const char *name);
int lsm_transfer_cancel(lsm_core *c, const char *tid, char *err);
int lsm_drop_links(lsm_core *c);
/* optional relay server (off by default). Settings persist sealed in relay.dat. Events:
 *   "relay\t<off|connecting|online|auth_failed|error>\t<detail>"   relay connection state (UI indicator)
 *   "relay_peer\t<fp>\t<join|leave>\t<name>"                      peer seen on the relay
 *   "peer\t<fp>\tpath\t<direct|relay>"                           live link path changed (badge) */
int lsm_relay_set(lsm_core *c, int enabled, const char *host, int port, const char *pass, char *err);
/* the passphrase is write-only: lsm_relay_set takes it (NULL = keep, "" = clear); reads only report *has_pass */
int lsm_relay_get(lsm_core *c, int *enabled, char *host, size_t hn, int *port, int *has_pass);
const char *lsm_relay_status(lsm_core *c, char *detail, size_t dn); /* call under lsm_lock */
int lsm_relay_peers(lsm_core *c); /* test hook: abort all TCP links without Bye */

/* helpers */
void lsm_uuid_str(const uint8_t u[16], char out[37]);
int lsm_uuid_parse(const char *s, uint8_t u[16]);
const char *lsm_resolve_peer(lsm_core *c, const char *q); /* by fp prefix or name (locked inside) */
const char *lsm_resolve_group(lsm_core *c, const char *q, char out[37]);
int lsm_firewall_setup(const char *exe_path_utf8, char *report, size_t n);
#endif
