/* Plain C surface over the portable core. Same JSON the Android JNI bridge returns.
 * Every char* result is malloc'd; the caller frees it. lsmh_poll returns NULL when idle. */
#ifndef LSM_HOST_H
#define LSM_HOST_H
#include <stdint.h>
char *lsmh_start(const char *dir, const char *name, const char *targets, int port);
void  lsmh_stop(void);
int   lsmh_running(void);
char *lsmh_poll(void);
char *lsmh_state(void);
char *lsmh_messages(const char *conv);
char *lsmh_send_text(const char *conv, const char *text);
char *lsmh_send_file(const char *conv, const char *path);
char *lsmh_add_addr(const char *addr);
void  lsmh_refresh_peers(void);
void  lsmh_wake(void);
void  lsmh_reconnect_server(void);
char *lsmh_set_trust(const char *fp, int trust);
char *lsmh_set_muted(const char *fp, int muted);
int   lsmh_is_muted(const char *fp);
char *lsmh_diagnostics(void);
char *lsmh_pin_offer(void);
char *lsmh_pin_pair(const char *fp, const char *pin);
char *lsmh_group_create(const char *name, const char *fps_csv);
char *lsmh_group_invite(const char *gid, const char *fps_csv);
char *lsmh_group_remove(const char *gid, const char *fp);
char *lsmh_group_leave(const char *gid);
int   lsmh_delete_message(const char *id);
int   lsmh_clear_conversation(const char *conv);
void  lsmh_mark_read(const char *conv);
void  lsmh_mark_unread(const char *conv);
void  lsmh_set_name(const char *name);
/* has_pass=0 keeps the stored passphrase (pass ignored). has_pass=1 and pass="" clears it. */
char *lsmh_relay_set(int on, const char *host, int port, const char *pass, int has_pass);
char *lsmh_cancel_transfer(const char *tid);
char *lsmh_storage_info(void);
int   lsmh_cleanup_storage(int orphans, int finished);
char *lsmh_history_query(const char *query, const char *convs, int64_t from_ms, int64_t to_ms,
                         int types, int dir, int statuses, int offset, int limit);
/* NULL pointer keeps that field. */
char *lsmh_set_profile(const char *sig, int has_sig, const char *avatar, int has_avatar);
void  lsmh_set_remark(const char *fp, const char *remark);
char *lsmh_send_reply(const char *conv, const char *text, const char *reply_to);
char *lsmh_react(const char *id, const char *emoji);
char *lsmh_edit_message(const char *id, const char *body);
char *lsmh_recall_message(const char *id);
char *lsmh_voice_name(int secs, const char *ext);
char *lsmh_send_poll(const char *conv, const char *question, const char *opts_nl, int multi);
char *lsmh_vote_poll(const char *id, int opt);
char *lsmh_close_poll(const char *id);
void  lsmh_set_archived(const char *conv, int on);
void  lsmh_set_folders(const char *conv, const char *csv);
char *lsmh_set_ttl(const char *conv, int secs);
char *lsmh_send_text2(const char *conv, const char *text, const char *reply_to);
void  lsmh_set_conv_pinned(const char *conv, int on);
void  lsmh_set_draft(const char *conv, const char *text);
char *lsmh_get_draft(const char *conv);
char *lsmh_set_msg_flags(const char *id, int pinned, int starred);
void  lsmh_send_typing(const char *conv);
char *lsmh_forward(const char *id, const char *conv);
char *lsmh_flagged(const char *conv, int which);
char *lsmh_mention_candidates(const char *conv);
char *lsmh_my_uri(void);
char *lsmh_qr(const char *text);
char *lsmh_add_uri(const char *uri);
char *lsmh_apply_uri(const char *uri);
int   lsmh_first_run_done(void);
char *lsmh_account_mode(void);
char *lsmh_complete_first_run(const char *name, const char *mode, const char *restore);
char *lsmh_account_setup(void);
char *lsmh_set_account_mode(const char *mode);
char *lsmh_reveal_recovery_key(void);
char *lsmh_account_devices(int unlink_others);
char *lsmh_account_pull_prev(void);
char *lsmh_relay_uri(void);
char *lsmh_account_pair_uri(void);
char *lsmh_resend(const char *id);
char *lsmh_file_preview(const char *conv, const char *path);
char *lsmh_last_editable(const char *conv);
#endif
