extern "C" {
#include "lsm_host.h"
}
#include "napi/native_api.h"
#include <cstdlib>
#include <string>

static std::string S(napi_env env, napi_value v) {
    napi_valuetype t = napi_undefined;
    napi_typeof(env, v, &t);
    if (t != napi_string) return std::string();
    size_t n = 0;
    napi_get_value_string_utf8(env, v, nullptr, 0, &n);
    if (n == 0) return std::string();
    std::string s(n, '\0');
    size_t wrote = 0;
    napi_get_value_string_utf8(env, v, &s[0], n + 1, &wrote);
    s.resize(wrote);
    return s;
}
static int32_t I32(napi_env env, napi_value v) { int32_t x = 0; napi_get_value_int32(env, v, &x); return x; }
static int64_t I64(napi_env env, napi_value v) { int64_t x = 0; napi_get_value_int64(env, v, &x); return x; }
static bool B(napi_env env, napi_value v) { bool x = false; napi_get_value_bool(env, v, &x); return x; }
static bool isNull(napi_env env, napi_value v) {
    napi_valuetype t = napi_undefined; napi_typeof(env, v, &t); return t == napi_null || t == napi_undefined;
}
static napi_value R(napi_env env, char *s) {
    napi_value v; napi_create_string_utf8(env, s ? s : "", NAPI_AUTO_LENGTH, &v); std::free(s); return v;
}
static napi_value Ri(napi_env env, int x) { napi_value v; napi_create_int32(env, x, &v); return v; }
static napi_value Rb(napi_env env, int x) { napi_value v; napi_get_boolean(env, x ? true : false, &v); return v; }
static napi_value Rn(napi_env env) { napi_value v; napi_get_null(env, &v); return v; }
static void take(napi_env env, napi_callback_info info, napi_value *a, size_t n) {
    size_t argc = n; napi_get_cb_info(env, info, &argc, a, nullptr, nullptr);
}

static napi_value Start(napi_env env, napi_callback_info info) {
    napi_value a[4]; take(env, info, a, 4);
    std::string d = S(env, a[0]), n = S(env, a[1]), t = S(env, a[2]);
    return R(env, lsmh_start(d.c_str(), n.c_str(), t.c_str(), I32(env, a[3])));
}
static napi_value Stop(napi_env env, napi_callback_info info) { (void)info; lsmh_stop(); return Rn(env); }
static napi_value Running(napi_env env, napi_callback_info info) { (void)info; return Rb(env, lsmh_running()); }
static napi_value Poll(napi_env env, napi_callback_info info) {
    (void)info; char *s = lsmh_poll(); if (!s) return Rn(env); return R(env, s);
}
static napi_value State(napi_env env, napi_callback_info info) { (void)info; return R(env, lsmh_state()); }
static napi_value Messages(napi_env env, napi_callback_info info) {
    napi_value a[1]; take(env, info, a, 1); std::string c = S(env, a[0]); return R(env, lsmh_messages(c.c_str()));
}
static napi_value SendText(napi_env env, napi_callback_info info) {
    napi_value a[2]; take(env, info, a, 2); std::string c = S(env, a[0]), t = S(env, a[1]);
    return R(env, lsmh_send_text(c.c_str(), t.c_str()));
}
static napi_value SendFile(napi_env env, napi_callback_info info) {
    napi_value a[2]; take(env, info, a, 2); std::string c = S(env, a[0]), p = S(env, a[1]);
    return R(env, lsmh_send_file(c.c_str(), p.c_str()));
}
static napi_value AddAddr(napi_env env, napi_callback_info info) {
    napi_value a[1]; take(env, info, a, 1); std::string s = S(env, a[0]); return R(env, lsmh_add_addr(s.c_str()));
}
static napi_value RefreshPeers(napi_env env, napi_callback_info info) { (void)info; lsmh_refresh_peers(); return Rn(env); }
static napi_value Wake(napi_env env, napi_callback_info info) { (void)info; lsmh_wake(); return Rn(env); }
static napi_value Reconnect(napi_env env, napi_callback_info info) { (void)info; lsmh_reconnect_server(); return Rn(env); }
static napi_value SetTrust(napi_env env, napi_callback_info info) {
    napi_value a[2]; take(env, info, a, 2); std::string s = S(env, a[0]); return R(env, lsmh_set_trust(s.c_str(), I32(env, a[1])));
}
static napi_value SetMuted(napi_env env, napi_callback_info info) {
    napi_value a[2]; take(env, info, a, 2); std::string s = S(env, a[0]); return R(env, lsmh_set_muted(s.c_str(), B(env, a[1]) ? 1 : 0));
}
static napi_value IsMuted(napi_env env, napi_callback_info info) {
    napi_value a[1]; take(env, info, a, 1); std::string s = S(env, a[0]); return Rb(env, lsmh_is_muted(s.c_str()));
}
static napi_value Diagnostics(napi_env env, napi_callback_info info) { (void)info; return R(env, lsmh_diagnostics()); }
static napi_value PinOffer(napi_env env, napi_callback_info info) { (void)info; return R(env, lsmh_pin_offer()); }
static napi_value PinPair(napi_env env, napi_callback_info info) {
    napi_value a[2]; take(env, info, a, 2); std::string f = S(env, a[0]), p = S(env, a[1]);
    return R(env, lsmh_pin_pair(f.c_str(), p.c_str()));
}
static napi_value GroupCreate(napi_env env, napi_callback_info info) {
    napi_value a[2]; take(env, info, a, 2); std::string n = S(env, a[0]), f = S(env, a[1]);
    return R(env, lsmh_group_create(n.c_str(), f.c_str()));
}
static napi_value GroupInvite(napi_env env, napi_callback_info info) {
    napi_value a[2]; take(env, info, a, 2); std::string g = S(env, a[0]), f = S(env, a[1]);
    return R(env, lsmh_group_invite(g.c_str(), f.c_str()));
}
static napi_value GroupRemove(napi_env env, napi_callback_info info) {
    napi_value a[2]; take(env, info, a, 2); std::string g = S(env, a[0]), f = S(env, a[1]);
    return R(env, lsmh_group_remove(g.c_str(), f.c_str()));
}
static napi_value GroupLeave(napi_env env, napi_callback_info info) {
    napi_value a[1]; take(env, info, a, 1); std::string g = S(env, a[0]); return R(env, lsmh_group_leave(g.c_str()));
}
static napi_value DeleteMessage(napi_env env, napi_callback_info info) {
    napi_value a[1]; take(env, info, a, 1); std::string s = S(env, a[0]); return Rb(env, lsmh_delete_message(s.c_str()));
}
static napi_value ClearConversation(napi_env env, napi_callback_info info) {
    napi_value a[1]; take(env, info, a, 1); std::string s = S(env, a[0]); return Ri(env, lsmh_clear_conversation(s.c_str()));
}
static napi_value MarkRead(napi_env env, napi_callback_info info) {
    napi_value a[1]; take(env, info, a, 1); std::string s = S(env, a[0]); lsmh_mark_read(s.c_str()); return Rn(env);
}
static napi_value MarkUnread(napi_env env, napi_callback_info info) {
    napi_value a[1]; take(env, info, a, 1); std::string s = S(env, a[0]); lsmh_mark_unread(s.c_str()); return Rn(env);
}
static napi_value SetName(napi_env env, napi_callback_info info) {
    napi_value a[1]; take(env, info, a, 1); std::string s = S(env, a[0]); lsmh_set_name(s.c_str()); return Rn(env);
}
static napi_value RelaySet(napi_env env, napi_callback_info info) {
    napi_value a[4]; take(env, info, a, 4);
    std::string h = S(env, a[1]);
    int has = !isNull(env, a[3]);
    std::string p = has ? S(env, a[3]) : std::string();
    return R(env, lsmh_relay_set(B(env, a[0]) ? 1 : 0, h.c_str(), I32(env, a[2]), p.c_str(), has));
}
static napi_value CancelTransfer(napi_env env, napi_callback_info info) {
    napi_value a[1]; take(env, info, a, 1); std::string s = S(env, a[0]); return R(env, lsmh_cancel_transfer(s.c_str()));
}
static napi_value StorageInfo(napi_env env, napi_callback_info info) { (void)info; return R(env, lsmh_storage_info()); }
static napi_value CleanupStorage(napi_env env, napi_callback_info info) {
    napi_value a[2]; take(env, info, a, 2); return Ri(env, lsmh_cleanup_storage(B(env, a[0]) ? 1 : 0, B(env, a[1]) ? 1 : 0));
}
static napi_value HistoryQuery(napi_env env, napi_callback_info info) {
    napi_value a[9]; take(env, info, a, 9);
    std::string q = S(env, a[0]), c = S(env, a[1]);
    return R(env, lsmh_history_query(q.c_str(), c.c_str(), I64(env, a[2]), I64(env, a[3]), I32(env, a[4]), I32(env, a[5]), I32(env, a[6]), I32(env, a[7]), I32(env, a[8])));
}
static napi_value SetProfile(napi_env env, napi_callback_info info) {
    napi_value a[2]; take(env, info, a, 2);
    int hs = !isNull(env, a[0]), ha = !isNull(env, a[1]);
    std::string s = hs ? S(env, a[0]) : std::string(), av = ha ? S(env, a[1]) : std::string();
    return R(env, lsmh_set_profile(s.c_str(), hs, av.c_str(), ha));
}
static napi_value SetRemark(napi_env env, napi_callback_info info) {
    napi_value a[2]; take(env, info, a, 2); std::string f = S(env, a[0]), r = S(env, a[1]);
    lsmh_set_remark(f.c_str(), r.c_str()); return Rn(env);
}
static napi_value SendReply(napi_env env, napi_callback_info info) {
    napi_value a[3]; take(env, info, a, 3); std::string c = S(env, a[0]), t = S(env, a[1]), r = S(env, a[2]);
    return R(env, lsmh_send_reply(c.c_str(), t.c_str(), r.c_str()));
}
static napi_value React(napi_env env, napi_callback_info info) {
    napi_value a[2]; take(env, info, a, 2); std::string i = S(env, a[0]), m = S(env, a[1]);
    return R(env, lsmh_react(i.c_str(), m.c_str()));
}
static napi_value EditMessage(napi_env env, napi_callback_info info) {
    napi_value a[2]; take(env, info, a, 2); std::string i = S(env, a[0]), m = S(env, a[1]);
    return R(env, lsmh_edit_message(i.c_str(), m.c_str()));
}
static napi_value RecallMessage(napi_env env, napi_callback_info info) {
    napi_value a[1]; take(env, info, a, 1); std::string i = S(env, a[0]); return R(env, lsmh_recall_message(i.c_str()));
}
static napi_value VoiceName(napi_env env, napi_callback_info info) {
    napi_value a[2]; take(env, info, a, 2); std::string x = S(env, a[1]); return R(env, lsmh_voice_name(I32(env, a[0]), x.c_str()));
}
static napi_value SendPoll(napi_env env, napi_callback_info info) {
    napi_value a[4]; take(env, info, a, 4); std::string c = S(env, a[0]), q = S(env, a[1]), o = S(env, a[2]);
    return R(env, lsmh_send_poll(c.c_str(), q.c_str(), o.c_str(), B(env, a[3]) ? 1 : 0));
}
static napi_value VotePoll(napi_env env, napi_callback_info info) {
    napi_value a[2]; take(env, info, a, 2); std::string i = S(env, a[0]); return R(env, lsmh_vote_poll(i.c_str(), I32(env, a[1])));
}
static napi_value ClosePoll(napi_env env, napi_callback_info info) {
    napi_value a[1]; take(env, info, a, 1); std::string i = S(env, a[0]); return R(env, lsmh_close_poll(i.c_str()));
}
static napi_value SetArchived(napi_env env, napi_callback_info info) {
    napi_value a[2]; take(env, info, a, 2); std::string c = S(env, a[0]); lsmh_set_archived(c.c_str(), B(env, a[1]) ? 1 : 0); return Rn(env);
}
static napi_value SetFolders(napi_env env, napi_callback_info info) {
    napi_value a[2]; take(env, info, a, 2); std::string c = S(env, a[0]), f = S(env, a[1]); lsmh_set_folders(c.c_str(), f.c_str()); return Rn(env);
}
static napi_value SetTtl(napi_env env, napi_callback_info info) {
    napi_value a[2]; take(env, info, a, 2); std::string c = S(env, a[0]); return R(env, lsmh_set_ttl(c.c_str(), I32(env, a[1])));
}
static napi_value SendText2(napi_env env, napi_callback_info info) {
    napi_value a[3]; take(env, info, a, 3); std::string c = S(env, a[0]), t = S(env, a[1]), r = S(env, a[2]);
    return R(env, lsmh_send_text2(c.c_str(), t.c_str(), r.c_str()));
}
static napi_value SetConvPinned(napi_env env, napi_callback_info info) {
    napi_value a[2]; take(env, info, a, 2); std::string c = S(env, a[0]); lsmh_set_conv_pinned(c.c_str(), B(env, a[1]) ? 1 : 0); return Rn(env);
}
static napi_value SetDraft(napi_env env, napi_callback_info info) {
    napi_value a[2]; take(env, info, a, 2); std::string c = S(env, a[0]), t = S(env, a[1]); lsmh_set_draft(c.c_str(), t.c_str()); return Rn(env);
}
static napi_value GetDraft(napi_env env, napi_callback_info info) {
    napi_value a[1]; take(env, info, a, 1); std::string c = S(env, a[0]); return R(env, lsmh_get_draft(c.c_str()));
}
static napi_value SetMsgFlags(napi_env env, napi_callback_info info) {
    napi_value a[3]; take(env, info, a, 3); std::string i = S(env, a[0]);
    return R(env, lsmh_set_msg_flags(i.c_str(), I32(env, a[1]), I32(env, a[2])));
}
static napi_value SendTyping(napi_env env, napi_callback_info info) {
    napi_value a[1]; take(env, info, a, 1); std::string c = S(env, a[0]); lsmh_send_typing(c.c_str()); return Rn(env);
}
static napi_value Forward(napi_env env, napi_callback_info info) {
    napi_value a[2]; take(env, info, a, 2); std::string i = S(env, a[0]), c = S(env, a[1]);
    return R(env, lsmh_forward(i.c_str(), c.c_str()));
}
static napi_value Flagged(napi_env env, napi_callback_info info) {
    napi_value a[2]; take(env, info, a, 2); std::string c = S(env, a[0]); return R(env, lsmh_flagged(c.c_str(), I32(env, a[1])));
}
static napi_value MentionCandidates(napi_env env, napi_callback_info info) {
    napi_value a[1]; take(env, info, a, 1); std::string c = S(env, a[0]); return R(env, lsmh_mention_candidates(c.c_str()));
}
static napi_value MyUri(napi_env env, napi_callback_info info) { (void)info; return R(env, lsmh_my_uri()); }
static napi_value Qr(napi_env env, napi_callback_info info) {
    napi_value a[1]; take(env, info, a, 1); std::string t = S(env, a[0]); return R(env, lsmh_qr(t.c_str()));
}
static napi_value AddUri(napi_env env, napi_callback_info info) {
    napi_value a[1]; take(env, info, a, 1); std::string s = S(env, a[0]); return R(env, lsmh_add_uri(s.c_str()));
}
static napi_value ApplyUri(napi_env env, napi_callback_info info) {
    napi_value a[1]; take(env, info, a, 1); std::string s = S(env, a[0]); return R(env, lsmh_apply_uri(s.c_str()));
}
static napi_value FirstRunDone(napi_env env, napi_callback_info info) { (void)info; return Rb(env, lsmh_first_run_done()); }
static napi_value AccountMode(napi_env env, napi_callback_info info) { (void)info; return R(env, lsmh_account_mode()); }
static napi_value CompleteFirstRun(napi_env env, napi_callback_info info) {
    napi_value a[3]; take(env, info, a, 3); std::string n = S(env, a[0]), m = S(env, a[1]), r = S(env, a[2]);
    return R(env, lsmh_complete_first_run(n.c_str(), m.c_str(), r.c_str()));
}
static napi_value AccountSetup(napi_env env, napi_callback_info info) { (void)info; return R(env, lsmh_account_setup()); }
static napi_value SetAccountMode(napi_env env, napi_callback_info info) {
    napi_value a[1]; take(env, info, a, 1); std::string m = S(env, a[0]); return R(env, lsmh_set_account_mode(m.c_str()));
}
static napi_value RevealRecoveryKey(napi_env env, napi_callback_info info) { (void)info; return R(env, lsmh_reveal_recovery_key()); }
static napi_value AccountDevices(napi_env env, napi_callback_info info) {
    napi_value a[1]; take(env, info, a, 1); return R(env, lsmh_account_devices(B(env, a[0]) ? 1 : 0));
}
static napi_value AccountPullPrev(napi_env env, napi_callback_info info) { (void)info; return R(env, lsmh_account_pull_prev()); }
static napi_value RelayUri(napi_env env, napi_callback_info info) { (void)info; return R(env, lsmh_relay_uri()); }
static napi_value AccountPairUri(napi_env env, napi_callback_info info) { (void)info; return R(env, lsmh_account_pair_uri()); }
static napi_value Resend(napi_env env, napi_callback_info info) {
    napi_value a[1]; take(env, info, a, 1); std::string s = S(env, a[0]); return R(env, lsmh_resend(s.c_str()));
}
static napi_value FilePreview(napi_env env, napi_callback_info info) {
    napi_value a[2]; take(env, info, a, 2); std::string c = S(env, a[0]), p = S(env, a[1]);
    return R(env, lsmh_file_preview(c.c_str(), p.c_str()));
}
static napi_value LastEditable(napi_env env, napi_callback_info info) {
    napi_value a[1]; take(env, info, a, 1); std::string c = S(env, a[0]); return R(env, lsmh_last_editable(c.c_str()));
}

#define D(name, fn) { name, nullptr, fn, nullptr, nullptr, nullptr, napi_default, nullptr }

static napi_value Init(napi_env env, napi_value exports) {
    napi_property_descriptor desc[] = {
        D("start", Start), D("stop", Stop), D("running", Running), D("poll", Poll), D("state", State),
        D("messages", Messages), D("sendText", SendText), D("sendFile", SendFile), D("addAddr", AddAddr),
        D("refreshPeers", RefreshPeers), D("wake", Wake), D("reconnectServer", Reconnect),
        D("setTrust", SetTrust), D("setMuted", SetMuted), D("isMuted", IsMuted), D("diagnostics", Diagnostics),
        D("pinOffer", PinOffer), D("pinPair", PinPair), D("groupCreate", GroupCreate), D("groupInvite", GroupInvite),
        D("groupRemove", GroupRemove), D("groupLeave", GroupLeave), D("deleteMessage", DeleteMessage),
        D("clearConversation", ClearConversation), D("markRead", MarkRead), D("markUnread", MarkUnread),
        D("setName", SetName), D("relaySet", RelaySet), D("cancelTransfer", CancelTransfer),
        D("storageInfo", StorageInfo), D("cleanupStorage", CleanupStorage), D("historyQuery", HistoryQuery),
        D("setProfile", SetProfile), D("setRemark", SetRemark), D("sendReply", SendReply), D("react", React),
        D("editMessage", EditMessage), D("recallMessage", RecallMessage), D("voiceName", VoiceName),
        D("sendPoll", SendPoll), D("votePoll", VotePoll), D("closePoll", ClosePoll),
        D("setArchived", SetArchived), D("setFolders", SetFolders), D("setTtl", SetTtl),
        D("sendText2", SendText2), D("setConvPinned", SetConvPinned), D("setDraft", SetDraft), D("getDraft", GetDraft),
        D("setMsgFlags", SetMsgFlags), D("sendTyping", SendTyping), D("forward", Forward), D("flagged", Flagged),
        D("mentionCandidates", MentionCandidates), D("myUri", MyUri), D("qr", Qr), D("addUri", AddUri),
        D("applyUri", ApplyUri), D("firstRunDone", FirstRunDone), D("accountMode", AccountMode),
        D("completeFirstRun", CompleteFirstRun), D("accountSetup", AccountSetup), D("setAccountMode", SetAccountMode),
        D("revealRecoveryKey", RevealRecoveryKey), D("accountDevices", AccountDevices), D("accountPullPrev", AccountPullPrev),
        D("relayUri", RelayUri), D("accountPairUri", AccountPairUri), D("resend", Resend), D("filePreview", FilePreview),
        D("lastEditable", LastEditable)
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}

static napi_module g_mod = { 1, 0, nullptr, Init, "lsm", nullptr, { 0 } };
extern "C" __attribute__((constructor)) void RegisterLsmModule(void) { napi_module_register(&g_mod); }
