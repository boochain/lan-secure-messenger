/* JNI bridge: Kotlin <-> lsm_host (the same JSON surface the HarmonyOS build calls). */
#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include "lsm_host.h"

static jstring J(JNIEnv *e, const char *s){ return (*e)->NewStringUTF(e,s?s:""); }
/* NewStringUTF wants modified UTF-8: 4-byte sequences (emoji) must go through a byte[] -> String(UTF-8) */
static jstring JU(JNIEnv *e, const char *s){ jsize n=(jsize)strlen(s?s:""); jbyteArray a=(*e)->NewByteArray(e,n); jclass sc=(*e)->FindClass(e,"java/lang/String");
    jmethodID ctor=(*e)->GetMethodID(e,sc,"<init>","([BLjava/lang/String;)V"); jstring r; (*e)->SetByteArrayRegion(e,a,0,n,(const jbyte*)(s?s:""));
    r=(jstring)(*e)->NewObject(e,sc,ctor,a,(*e)->NewStringUTF(e,"UTF-8")); (*e)->DeleteLocalRef(e,a); return r; }
static char *U(JNIEnv *e, jstring s){ jclass sc; jmethodID gb; jbyteArray a; jsize n; char *r; if(!s) return strdup("");
    sc=(*e)->FindClass(e,"java/lang/String"); gb=(*e)->GetMethodID(e,sc,"getBytes","(Ljava/lang/String;)[B");
    a=(jbyteArray)(*e)->CallObjectMethod(e,s,gb,(*e)->NewStringUTF(e,"UTF-8")); n=(*e)->GetArrayLength(e,a); r=malloc(n+1); (*e)->GetByteArrayRegion(e,a,0,n,(jbyte*)r); r[n]=0; (*e)->DeleteLocalRef(e,a); return r; }
static jstring JS(JNIEnv *e, char *s){ jstring r=JU(e,s?s:""); free(s); return r; }
#define FN(name) Java_io_github_frec86_lanmessenger_Native_##name

JNIEXPORT jstring JNICALL FN(start)(JNIEnv *e, jclass k, jstring dir, jstring name, jstring targets, jint port){
    char *d=U(e,dir), *n=U(e,name), *t=U(e,targets), *r; (void)k; r=lsmh_start(d,n,t,port); free(d); free(n); free(t); return JS(e,r); }
JNIEXPORT void JNICALL FN(stop)(JNIEnv *e, jclass k){ (void)e;(void)k; lsmh_stop(); }
JNIEXPORT jboolean JNICALL FN(running)(JNIEnv *e, jclass k){ (void)e;(void)k; return lsmh_running()?JNI_TRUE:JNI_FALSE; }
JNIEXPORT jstring JNICALL FN(poll)(JNIEnv *e, jclass k){ char *s; (void)k; s=lsmh_poll(); if(!s) return NULL; return JS(e,s); }
JNIEXPORT jstring JNICALL FN(state)(JNIEnv *e, jclass k){ (void)k; return JS(e,lsmh_state()); }
JNIEXPORT jstring JNICALL FN(messages)(JNIEnv *e, jclass k, jstring conv){ char *c=U(e,conv); jstring r; (void)k; r=JS(e,lsmh_messages(c)); free(c); return r; }
JNIEXPORT jstring JNICALL FN(sendText)(JNIEnv *e, jclass k, jstring conv, jstring text){ char *c=U(e,conv), *t=U(e,text); jstring r; (void)k; r=JS(e,lsmh_send_text(c,t)); free(c); free(t); return r; }
JNIEXPORT jstring JNICALL FN(sendFile)(JNIEnv *e, jclass k, jstring conv, jstring path){ char *c=U(e,conv), *p=U(e,path); jstring r; (void)k; r=JS(e,lsmh_send_file(c,p)); free(c); free(p); return r; }
JNIEXPORT jstring JNICALL FN(addAddr)(JNIEnv *e, jclass k, jstring a){ char *s=U(e,a); jstring r; (void)k; r=JS(e,lsmh_add_addr(s)); free(s); return r; }
JNIEXPORT void JNICALL FN(refreshPeers)(JNIEnv *e, jclass k){ (void)e;(void)k; lsmh_refresh_peers(); }
JNIEXPORT void JNICALL FN(wake)(JNIEnv *e, jclass k){ (void)e;(void)k; lsmh_wake(); }
JNIEXPORT void JNICALL FN(reconnectServer)(JNIEnv *e, jclass k){ (void)e;(void)k; lsmh_reconnect_server(); }
JNIEXPORT jstring JNICALL FN(setTrust)(JNIEnv *e, jclass k, jstring fp, jint t){ char *s=U(e,fp); jstring r; (void)k; r=JS(e,lsmh_set_trust(s,t)); free(s); return r; }
JNIEXPORT jstring JNICALL FN(setMuted)(JNIEnv *e, jclass k, jstring fp, jboolean m){ char *s=U(e,fp); jstring r; (void)k; r=JS(e,lsmh_set_muted(s,m?1:0)); free(s); return r; }
JNIEXPORT jboolean JNICALL FN(isMuted)(JNIEnv *e, jclass k, jstring fp){ char *s=U(e,fp); int m; (void)k; m=lsmh_is_muted(s); free(s); return m?JNI_TRUE:JNI_FALSE; }
JNIEXPORT jstring JNICALL FN(diagnostics)(JNIEnv *e, jclass k){ (void)k; return JS(e,lsmh_diagnostics()); }
JNIEXPORT jstring JNICALL FN(pinOffer)(JNIEnv *e, jclass k){ (void)k; return JS(e,lsmh_pin_offer()); }
JNIEXPORT jstring JNICALL FN(pinPair)(JNIEnv *e, jclass k, jstring fp, jstring pin){ char *f=U(e,fp), *p=U(e,pin); jstring r; (void)k; r=JS(e,lsmh_pin_pair(f,p)); free(f); free(p); return r; }
JNIEXPORT jstring JNICALL FN(groupCreate)(JNIEnv *e, jclass k, jstring name, jstring fps){ char *n=U(e,name), *f=U(e,fps); jstring r; (void)k; r=JS(e,lsmh_group_create(n,f)); free(n); free(f); return r; }
JNIEXPORT jstring JNICALL FN(groupInvite)(JNIEnv *e, jclass k, jstring gid, jstring fps){ char *g=U(e,gid), *f=U(e,fps); jstring r; (void)k; r=JS(e,lsmh_group_invite(g,f)); free(g); free(f); return r; }
JNIEXPORT jstring JNICALL FN(groupRemove)(JNIEnv *e, jclass k, jstring gid, jstring fp){ char *g=U(e,gid), *f=U(e,fp); jstring r; (void)k; r=JS(e,lsmh_group_remove(g,f)); free(g); free(f); return r; }
JNIEXPORT jstring JNICALL FN(groupLeave)(JNIEnv *e, jclass k, jstring gid){ char *g=U(e,gid); jstring r; (void)k; r=JS(e,lsmh_group_leave(g)); free(g); return r; }
JNIEXPORT jboolean JNICALL FN(deleteMessage)(JNIEnv *e, jclass k, jstring id){ char *s=U(e,id); int rc; (void)k; rc=lsmh_delete_message(s); free(s); return rc?JNI_TRUE:JNI_FALSE; }
JNIEXPORT jint JNICALL FN(clearConversation)(JNIEnv *e, jclass k, jstring conv){ char *s=U(e,conv); int n; (void)k; n=lsmh_clear_conversation(s); free(s); return n; }
JNIEXPORT void JNICALL FN(markRead)(JNIEnv *e, jclass k, jstring conv){ char *c=U(e,conv); (void)k; lsmh_mark_read(c); free(c); }
JNIEXPORT void JNICALL FN(markUnread)(JNIEnv *e, jclass k, jstring conv){ char *c=U(e,conv); (void)k; lsmh_mark_unread(c); free(c); }
JNIEXPORT void JNICALL FN(setName)(JNIEnv *e, jclass k, jstring n){ char *s=U(e,n); (void)k; lsmh_set_name(s); free(s); }
JNIEXPORT jstring JNICALL FN(relaySet)(JNIEnv *e, jclass k, jboolean on, jstring host, jint port, jstring pass){ char *h=U(e,host), *pw=pass?U(e,pass):NULL; jstring r; (void)k;
    r=JS(e,lsmh_relay_set(on?1:0,h,port,pw,pass!=NULL)); free(h); if(pw){ memset(pw,0,strlen(pw)); free(pw);} return r; }
JNIEXPORT jstring JNICALL FN(cancelTransfer)(JNIEnv *e, jclass k, jstring tid){ char *s=U(e,tid); jstring r; (void)k; r=JS(e,lsmh_cancel_transfer(s)); free(s); return r; }
JNIEXPORT jstring JNICALL FN(storageInfo)(JNIEnv *e, jclass k){ (void)k; return JS(e,lsmh_storage_info()); }
JNIEXPORT jint JNICALL FN(cleanupStorage)(JNIEnv *e, jclass k, jboolean orphans, jboolean finished){ (void)e;(void)k; return lsmh_cleanup_storage(orphans?1:0,finished?1:0); }
JNIEXPORT jstring JNICALL FN(historyQuery)(JNIEnv *e, jclass k, jstring query, jstring convs, jlong from, jlong to, jint types, jint dir, jint statuses, jint offset, jint limit){
    char *qs=U(e,query), *cv=U(e,convs); jstring r; (void)k; r=JS(e,lsmh_history_query(qs,cv,from,to,types,dir,statuses,offset,limit)); free(qs); free(cv); return r; }
JNIEXPORT jstring JNICALL FN(setProfile)(JNIEnv *e, jclass k, jstring sig, jstring avatar){ char *s=sig?U(e,sig):NULL, *a=avatar?U(e,avatar):NULL; jstring r; (void)k;
    r=JS(e,lsmh_set_profile(s,sig!=NULL,a,avatar!=NULL)); free(s); free(a); return r; }
JNIEXPORT void JNICALL FN(setRemark)(JNIEnv *e, jclass k, jstring fp, jstring remark){ char *f=U(e,fp), *r=U(e,remark); (void)k; lsmh_set_remark(f,r); free(f); free(r); }
JNIEXPORT jstring JNICALL FN(sendReply)(JNIEnv *e, jclass k, jstring conv, jstring text, jstring replyTo){ char *c=U(e,conv), *t=U(e,text), *rto=U(e,replyTo); jstring r; (void)k; r=JS(e,lsmh_send_reply(c,t,rto)); free(c); free(t); free(rto); return r; }
JNIEXPORT jstring JNICALL FN(react)(JNIEnv *e, jclass k, jstring id, jstring emoji){ char *i=U(e,id), *m=U(e,emoji); jstring r; (void)k; r=JS(e,lsmh_react(i,m)); free(i); free(m); return r; }
JNIEXPORT jstring JNICALL FN(editMessage)(JNIEnv *e, jclass k, jstring id, jstring body){ char *i=U(e,id), *m=U(e,body); jstring r; (void)k; r=JS(e,lsmh_edit_message(i,m)); free(i); free(m); return r; }
JNIEXPORT jstring JNICALL FN(recallMessage)(JNIEnv *e, jclass k, jstring id){ char *i=U(e,id); jstring r; (void)k; r=JS(e,lsmh_recall_message(i)); free(i); return r; }
JNIEXPORT jstring JNICALL FN(voiceName)(JNIEnv *e, jclass k, jint secs, jstring ext){ char *x=U(e,ext); jstring r; (void)k; r=JS(e,lsmh_voice_name(secs,x)); free(x); return r; }
JNIEXPORT jstring JNICALL FN(sendPoll)(JNIEnv *e, jclass k, jstring conv, jstring q, jstring opts, jboolean multi){ char *c=U(e,conv), *qq=U(e,q), *o=U(e,opts); jstring r; (void)k; r=JS(e,lsmh_send_poll(c,qq,o,multi?1:0)); free(c); free(qq); free(o); return r; }
JNIEXPORT jstring JNICALL FN(votePoll)(JNIEnv *e, jclass k, jstring id, jint opt){ char *i=U(e,id); jstring r; (void)k; r=JS(e,lsmh_vote_poll(i,opt)); free(i); return r; }
JNIEXPORT jstring JNICALL FN(closePoll)(JNIEnv *e, jclass k, jstring id){ char *i=U(e,id); jstring r; (void)k; r=JS(e,lsmh_close_poll(i)); free(i); return r; }
JNIEXPORT void JNICALL FN(setArchived)(JNIEnv *e, jclass k, jstring conv, jboolean on){ char *c=U(e,conv); (void)k; lsmh_set_archived(c,on?1:0); free(c); }
JNIEXPORT void JNICALL FN(setFolders)(JNIEnv *e, jclass k, jstring conv, jstring csv){ char *c=U(e,conv), *f=U(e,csv); (void)k; lsmh_set_folders(c,f); free(c); free(f); }
JNIEXPORT jstring JNICALL FN(setTtl)(JNIEnv *e, jclass k, jstring conv, jint secs){ char *c=U(e,conv); jstring r; (void)k; r=JS(e,lsmh_set_ttl(c,secs)); free(c); return r; }
JNIEXPORT jstring JNICALL FN(sendText2)(JNIEnv *e, jclass k, jstring conv, jstring text, jstring replyTo){ char *c=U(e,conv), *t=U(e,text), *rto=U(e,replyTo); jstring r; (void)k; r=JS(e,lsmh_send_text2(c,t,rto)); free(c); free(t); free(rto); return r; }
JNIEXPORT void JNICALL FN(setConvPinned)(JNIEnv *e, jclass k, jstring conv, jboolean on){ char *c=U(e,conv); (void)k; lsmh_set_conv_pinned(c,on?1:0); free(c); }
JNIEXPORT void JNICALL FN(setDraft)(JNIEnv *e, jclass k, jstring conv, jstring text){ char *c=U(e,conv), *t=U(e,text); (void)k; lsmh_set_draft(c,t); free(c); free(t); }
JNIEXPORT jstring JNICALL FN(getDraft)(JNIEnv *e, jclass k, jstring conv){ char *c=U(e,conv); jstring r; (void)k; r=JS(e,lsmh_get_draft(c)); free(c); return r; }
JNIEXPORT jstring JNICALL FN(setMsgFlags)(JNIEnv *e, jclass k, jstring id, jint pinned, jint starred){ char *i=U(e,id); jstring r; (void)k; r=JS(e,lsmh_set_msg_flags(i,pinned,starred)); free(i); return r; }
JNIEXPORT void JNICALL FN(sendTyping)(JNIEnv *e, jclass k, jstring conv){ char *c=U(e,conv); (void)k; lsmh_send_typing(c); free(c); }
JNIEXPORT jstring JNICALL FN(forward)(JNIEnv *e, jclass k, jstring id, jstring conv){ char *i=U(e,id), *c=U(e,conv); jstring r; (void)k; r=JS(e,lsmh_forward(i,c)); free(i); free(c); return r; }
JNIEXPORT jstring JNICALL FN(flagged)(JNIEnv *e, jclass k, jstring conv, jint which){ char *c=U(e,conv); jstring r; (void)k; r=JS(e,lsmh_flagged(c,which)); free(c); return r; }
JNIEXPORT jstring JNICALL FN(mentionCandidates)(JNIEnv *e, jclass k, jstring conv){ char *c=U(e,conv); jstring r; (void)k; r=JS(e,lsmh_mention_candidates(c)); free(c); return r; }
JNIEXPORT jstring JNICALL FN(myUri)(JNIEnv *e, jclass k){ (void)k; return JS(e,lsmh_my_uri()); }
JNIEXPORT jstring JNICALL FN(qr)(JNIEnv *e, jclass k, jstring text){ char *t=U(e,text); jstring r; (void)k; r=JS(e,lsmh_qr(t)); free(t); return r; }
JNIEXPORT jstring JNICALL FN(addUri)(JNIEnv *e, jclass k, jstring uri){ char *s=U(e,uri); jstring r; (void)k; r=JS(e,lsmh_add_uri(s)); free(s); return r; }
JNIEXPORT jstring JNICALL FN(applyUri)(JNIEnv *e, jclass k, jstring uri){ char *s=U(e,uri); jstring r; (void)k; r=JS(e,lsmh_apply_uri(s)); free(s); return r; }
JNIEXPORT jboolean JNICALL FN(firstRunDone)(JNIEnv *e, jclass k){ (void)e;(void)k; return lsmh_first_run_done()?JNI_TRUE:JNI_FALSE; }
JNIEXPORT jstring JNICALL FN(accountMode)(JNIEnv *e, jclass k){ (void)k; return JS(e,lsmh_account_mode()); }
JNIEXPORT jstring JNICALL FN(completeFirstRun)(JNIEnv *e, jclass k, jstring name, jstring mode, jstring restore){
    char *n=U(e,name), *m=U(e,mode), *r=restore?U(e,restore):NULL; jstring o; (void)k; o=JS(e,lsmh_complete_first_run(n,m,r)); free(n); free(m); free(r); return o; }
JNIEXPORT jstring JNICALL FN(accountSignIn)(JNIEnv *e, jclass k, jstring host, jint port, jstring netPass, jstring user, jstring pass, jboolean reg){
    char *h=U(e,host), *np=netPass?U(e,netPass):NULL, *u=U(e,user), *p=U(e,pass); jstring o; (void)k;
    o=JS(e,lsmh_account_sign_in(h,port,np,u,p,reg?1:0)); free(h); free(np); free(u); if(p){ memset(p,0,strlen(p)); free(p);} return o; }
JNIEXPORT jstring JNICALL FN(accountSetup)(JNIEnv *e, jclass k){ (void)k; return JS(e,lsmh_account_setup()); }
JNIEXPORT jstring JNICALL FN(setAccountMode)(JNIEnv *e, jclass k, jstring mode){ char *m=U(e,mode); jstring r; (void)k; r=JS(e,lsmh_set_account_mode(m)); free(m); return r; }
JNIEXPORT jstring JNICALL FN(revealRecoveryKey)(JNIEnv *e, jclass k){ (void)k; return JS(e,lsmh_reveal_recovery_key()); }
JNIEXPORT jstring JNICALL FN(accountDevices)(JNIEnv *e, jclass k, jboolean unlink){ (void)k; return JS(e,lsmh_account_devices(unlink?1:0)); }
JNIEXPORT jstring JNICALL FN(accountPullPrev)(JNIEnv *e, jclass k){ (void)k; return JS(e,lsmh_account_pull_prev()); }
JNIEXPORT jstring JNICALL FN(relayUri)(JNIEnv *e, jclass k){ (void)k; return JS(e,lsmh_relay_uri()); }
JNIEXPORT jstring JNICALL FN(accountPairUri)(JNIEnv *e, jclass k){ (void)k; return JS(e,lsmh_account_pair_uri()); }
JNIEXPORT jstring JNICALL FN(resend)(JNIEnv *e, jclass k, jstring id){ char *s=U(e,id); jstring r; (void)k; r=JS(e,lsmh_resend(s)); free(s); return r; }
JNIEXPORT jstring JNICALL FN(filePreview)(JNIEnv *e, jclass k, jstring conv, jstring path){ char *c=U(e,conv), *p=U(e,path); jstring r; (void)k; r=JS(e,lsmh_file_preview(c,p)); free(c); free(p); return r; }
JNIEXPORT jstring JNICALL FN(lastEditable)(JNIEnv *e, jclass k, jstring conv){ char *c=U(e,conv); jstring r; (void)k; r=JS(e,lsmh_last_editable(c)); free(c); return r; }
