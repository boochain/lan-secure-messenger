/* JNI bridge: Kotlin <-> portable C core (src/core.h). State is returned as JSON strings so the
 * Kotlin side needs no per-struct marshalling. All core access happens under lsm_lock. */
#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "core.h"

static lsm_core *C;
typedef struct { char *p; size_t n, cap; } sb;
static void sb_put(sb *b, const char *s, size_t n){ if(b->n+n+1>b->cap){ b->cap=(b->n+n+1)*2+256; b->p=realloc(b->p,b->cap); } memcpy(b->p+b->n,s,n); b->n+=n; b->p[b->n]=0; }
static void sb_s(sb *b, const char *s){ sb_put(b,s,strlen(s)); }
static void sb_f(sb *b, const char *fmt, ...){ char t[512]; va_list ap; va_start(ap,fmt); vsnprintf(t,sizeof t,fmt,ap); va_end(ap); sb_s(b,t); }
static void sb_js(sb *b, const char *s){ /* JSON string */ sb_put(b,"\"",1); for(;s&&*s;s++){ unsigned char c=(unsigned char)*s; char e[8];
        if(c=='"'||c=='\\'){ e[0]='\\'; e[1]=c; sb_put(b,e,2); } else if(c<0x20){ snprintf(e,sizeof e,"\\u%04x",c); sb_put(b,e,6); } else sb_put(b,(char*)&c,1); } sb_put(b,"\"",1); }
static jstring J(JNIEnv *e, const char *s){ return (*e)->NewStringUTF(e,s?s:""); }
/* NewStringUTF wants modified UTF-8: 4-byte sequences (emoji) must go through a byte[] -> String(UTF-8) */
static jstring JU(JNIEnv *e, const char *s){ jsize n=(jsize)strlen(s); jbyteArray a=(*e)->NewByteArray(e,n); jclass sc=(*e)->FindClass(e,"java/lang/String");
    jmethodID ctor=(*e)->GetMethodID(e,sc,"<init>","([BLjava/lang/String;)V"); jstring r; (*e)->SetByteArrayRegion(e,a,0,n,(const jbyte*)s);
    r=(jstring)(*e)->NewObject(e,sc,ctor,a,(*e)->NewStringUTF(e,"UTF-8")); (*e)->DeleteLocalRef(e,a); return r; }
/* Kotlin strings -> real UTF-8 (GetStringUTFChars gives modified UTF-8, wrong for emoji) */
static char *U(JNIEnv *e, jstring s){ jclass sc; jmethodID gb; jbyteArray a; jsize n; char *r; if(!s) return strdup("");
    sc=(*e)->FindClass(e,"java/lang/String"); gb=(*e)->GetMethodID(e,sc,"getBytes","(Ljava/lang/String;)[B");
    a=(jbyteArray)(*e)->CallObjectMethod(e,s,gb,(*e)->NewStringUTF(e,"UTF-8")); n=(*e)->GetArrayLength(e,a); r=malloc(n+1); (*e)->GetByteArrayRegion(e,a,0,n,(jbyte*)r); r[n]=0; (*e)->DeleteLocalRef(e,a); return r; }
#define FN(name) Java_io_github_frec86_lanmessenger_Native_##name

JNIEXPORT jstring JNICALL FN(start)(JNIEnv *e, jclass k, jstring dir, jstring name, jstring targets, jint port){
    lsm_opts o; char err[256]=""; char *d=U(e,dir), *n=U(e,name), *t=U(e,targets); (void)k;
    if(C) return J(e,""); memset(&o,0,sizeof o); o.data_dir=d; o.name=n; o.announce_targets=*t?t:NULL; o.listen_port=port;
    C=lsm_start(&o,err,sizeof err); /* the core keeps the opts pointers: intentionally not freed */ if(!C){ free(d); free(n); free(t); } return J(e,C?"":(err[0]?err:"start failed")); }
JNIEXPORT void JNICALL FN(stop)(JNIEnv *e, jclass k){ (void)e;(void)k; if(C){ lsm_stop(C); C=NULL; } }
JNIEXPORT jboolean JNICALL FN(running)(JNIEnv *e, jclass k){ (void)e;(void)k; return C!=NULL; }
JNIEXPORT jstring JNICALL FN(poll)(JNIEnv *e, jclass k){ char b[4096]; (void)k; if(!C||!lsm_poll_event(C,b,sizeof b)) return NULL; return JU(e,b); }

static const char *TS[]={"offered","active","verifying","done","failed","rejected","cancelled"};
static const char *MS[]={"queued","sent","delivered","read","received","failed"};
JNIEXPORT jstring JNICALL FN(state)(JNIEnv *e, jclass k){
    sb b={0}; lsm_contact *c; lsm_group *g; lsm_transfer *t; lsm_msg *m; int n,i,j,nm; jstring r; (void)k; if(!C) return J(e,"{}");
    lsm_lock(C);
    sb_s(&b,"{\"me\":{\"fp\":"); sb_js(&b,lsm_my_fp(C)); sb_s(&b,",\"name\":"); sb_js(&b,lsm_my_name(C)); sb_f(&b,",\"port\":%d,\"version\":",lsm_listen_port(C)); sb_js(&b,LSM_APP_VERSION);
    sb_s(&b,",\"downloads\":"); sb_js(&b,lsm_download_dir(C)); sb_s(&b,"},\"contacts\":[");
    n=lsm_contacts(C,&c); nm=lsm_messages(C,&m);
    for(i=0;i<n;i++){ char conv[64]; int un=0; snprintf(conv,64,"dm:%s",c[i].fp); for(j=0;j<nm;j++) if(m[j].unread&&!strcmp(m[j].conv,conv)) un++;
        if(i) sb_s(&b,","); sb_s(&b,"{\"fp\":"); sb_js(&b,c[i].fp); sb_s(&b,",\"name\":"); sb_js(&b,c[i].name); sb_f(&b,",\"trust\":%d,\"online\":%s,\"rtt\":%d,\"unread\":%d,\"addr\":",c[i].trust,c[i].online?"true":"false",c[i].rtt_ms,un);
        sb_js(&b,c[i].addr); sb_s(&b,",\"version\":"); sb_js(&b,c[i].app_version); sb_f(&b,",\"via\":%s}",c[i].via_relay?"true":"false"); }
    sb_s(&b,"],\"groups\":["); n=lsm_groups(C,&g);
    for(i=0;i<n;i++){ char u[37], conv[64]; int un=0; lsm_uuid_str(g[i].id,u); snprintf(conv,64,"group:%s",u); for(j=0;j<nm;j++) if(m[j].unread&&!strcmp(m[j].conv,conv)) un++;
        if(i) sb_s(&b,","); sb_s(&b,"{\"id\":"); sb_js(&b,u); sb_s(&b,",\"name\":"); sb_js(&b,g[i].name); sb_f(&b,",\"epoch\":%llu,\"unread\":%d,\"members\":[",(unsigned long long)g[i].key_epoch,un);
        for(j=0;j<g[i].nmembers;j++){ if(j) sb_s(&b,","); sb_js(&b,g[i].members[j]); } sb_s(&b,"]}"); }
    sb_s(&b,"],\"transfers\":["); n=lsm_transfers(C,&t);
    for(i=0;i<n;i++){ char u[37]; lsm_uuid_str(t[i].tid,u); if(i) sb_s(&b,","); sb_s(&b,"{\"id\":"); sb_js(&b,u); sb_s(&b,",\"msg\":"); sb_js(&b,t[i].msg_id); sb_s(&b,",\"name\":"); sb_js(&b,t[i].name);
        sb_f(&b,",\"out\":%s,\"size\":%llu,\"done\":%llu,\"state\":\"%s\",\"path\":",t[i].out?"true":"false",(unsigned long long)t[i].size,(unsigned long long)t[i].transferred,TS[t[i].state]); sb_js(&b,t[i].path);
        sb_s(&b,",\"error\":"); sb_js(&b,t[i].error); sb_f(&b,",\"resumed\":%llu}",(unsigned long long)t[i].resumed_from); }
    /* relay server (optional): the passphrase is write-only, only has_pass is reported */
    { int en=0, port=0, hp=0; char host[256]="", det[256]=""; const char *rs;
      lsm_unlock(C); lsm_relay_get(C,&en,host,sizeof host,&port,&hp); lsm_lock(C); rs=lsm_relay_status(C,det,sizeof det);
      sb_f(&b,"],\"relay\":{\"enabled\":%s,\"port\":%d,\"has_pass\":%s,\"peers\":%d,\"state\":",en?"true":"false",port,hp?"true":"false",lsm_relay_peers(C)); sb_js(&b,rs);
      sb_s(&b,",\"host\":"); sb_js(&b,host); sb_s(&b,",\"detail\":"); sb_js(&b,det); sb_s(&b,"}"); }
    sb_s(&b,"}"); lsm_unlock(C); r=JU(e,b.p); free(b.p); return r; }

JNIEXPORT jstring JNICALL FN(messages)(JNIEnv *e, jclass k, jstring conv){
    sb b={0}; lsm_msg *m; int n,i,first=1; char *cv=U(e,conv); jstring r; (void)k; if(!C){ free(cv); return J(e,"[]"); }
    sb_s(&b,"["); lsm_lock(C); n=lsm_messages(C,&m);
    for(i=0;i<n;i++){ if(strcmp(m[i].conv,cv)) continue; if(!first) sb_s(&b,","); first=0;
        sb_s(&b,"{\"id\":"); sb_js(&b,m[i].id); sb_f(&b,",\"out\":%s,\"kind\":%d,\"status\":\"%s\",\"ts\":%lld,\"sender\":",m[i].out?"true":"false",m[i].kind,MS[m[i].status],(long long)m[i].ts_ms);
        sb_js(&b,m[i].sender_name); sb_s(&b,",\"body\":"); sb_js(&b,m[i].body); sb_s(&b,",\"file\":"); sb_js(&b,m[i].file_name); sb_f(&b,",\"size\":%llu,\"path\":",(unsigned long long)m[i].file_size); sb_js(&b,m[i].file_path); sb_s(&b,"}"); }
    lsm_unlock(C); sb_s(&b,"]"); free(cv); r=JU(e,b.p); free(b.p); return r; }

#define ERRRET(expr) { char err[256]=""; int rc=(expr); return J(e, rc?(err[0]?err:"failed"):""); }
JNIEXPORT jstring JNICALL FN(sendText)(JNIEnv *e, jclass k, jstring conv, jstring text){ char id[40], *c=U(e,conv), *t=U(e,text), err[256]=""; int rc=C?lsm_send_text(C,c,t,id,err):1; (void)k; free(c); free(t); return J(e,rc?(err[0]?err:"failed"):""); }
JNIEXPORT jstring JNICALL FN(sendFile)(JNIEnv *e, jclass k, jstring conv, jstring path){ char *c=U(e,conv), *p=U(e,path), err[256]=""; int rc=C?lsm_send_file(C,c,p,err):1; (void)k; free(c); free(p); return J(e,rc?(err[0]?err:"failed"):""); }
JNIEXPORT jstring JNICALL FN(addAddr)(JNIEnv *e, jclass k, jstring a){ char *s=U(e,a), err[256]=""; int rc=C?lsm_add_addr(C,s,err):1; (void)k; free(s); return J(e,rc?(err[0]?err:"failed"):""); }
JNIEXPORT jstring JNICALL FN(setTrust)(JNIEnv *e, jclass k, jstring fp, jint t){ char *s=U(e,fp), err[256]=""; int rc=C?lsm_set_trust(C,s,t,err):1; (void)k; free(s); return J(e,rc?(err[0]?err:"failed"):""); }
JNIEXPORT jstring JNICALL FN(pinOffer)(JNIEnv *e, jclass k){ char pin[8]=""; (void)k; if(C) lsm_pin_offer(C,pin); return J(e,pin); }
JNIEXPORT jstring JNICALL FN(pinPair)(JNIEnv *e, jclass k, jstring fp, jstring pin){ char *f=U(e,fp), *p=U(e,pin), err[256]=""; int rc=C?lsm_pin_pair(C,f,p,err):1; (void)k; free(f); free(p); return J(e,rc?(err[0]?err:"failed"):""); }
static int split_csv(char *s, const char **out){ int n=0; char *p=s; while(*p && n<64){ out[n++]=p; while(*p && *p!=',') p++; if(*p) *p++=0; } return n; }
JNIEXPORT jstring JNICALL FN(groupCreate)(JNIEnv *e, jclass k, jstring name, jstring fps){ char *nm=U(e,name), *f=U(e,fps), gid[40]="", err[256]=""; const char *a[64]; int n=split_csv(f,a), rc=C?lsm_group_create(C,nm,a,n,gid,err):1; (void)k; free(nm); free(f);
    if(rc) { char m[300]; snprintf(m,sizeof m,"!%s",err[0]?err:"failed"); return J(e,m); } return J(e,gid); }
JNIEXPORT jstring JNICALL FN(groupInvite)(JNIEnv *e, jclass k, jstring gid, jstring fps){ char *g=U(e,gid), *f=U(e,fps), err[256]=""; const char *a[64]; int n=split_csv(f,a), rc=C?lsm_group_invite(C,g,a,n,err):1; (void)k; free(g); free(f); return J(e,rc?(err[0]?err:"failed"):""); }
JNIEXPORT jstring JNICALL FN(groupRemove)(JNIEnv *e, jclass k, jstring gid, jstring fp){ char *g=U(e,gid), *f=U(e,fp), err[256]=""; int rc=C?lsm_group_remove(C,g,f,err):1; (void)k; free(g); free(f); return J(e,rc?(err[0]?err:"failed"):""); }
JNIEXPORT jstring JNICALL FN(groupLeave)(JNIEnv *e, jclass k, jstring gid){ char *g=U(e,gid), err[256]=""; int rc=C?lsm_group_leave(C,g,err):1; (void)k; free(g); return J(e,rc?(err[0]?err:"failed"):""); }
JNIEXPORT jboolean JNICALL FN(deleteMessage)(JNIEnv *e, jclass k, jstring id){ char *s=U(e,id); int rc=C?lsm_delete_message(C,s):-1; (void)k; free(s); return rc==0?JNI_TRUE:JNI_FALSE; }
JNIEXPORT void JNICALL FN(markRead)(JNIEnv *e, jclass k, jstring conv){ char *c=U(e,conv); (void)k; if(C) lsm_mark_read(C,c); free(c); }
JNIEXPORT void JNICALL FN(setName)(JNIEnv *e, jclass k, jstring n){ char *s=U(e,n); (void)k; if(C) lsm_set_name(C,s); free(s); }
/* pass == null keeps the stored passphrase, "" clears it (write-only: there is no getter) */
JNIEXPORT jstring JNICALL FN(relaySet)(JNIEnv *e, jclass k, jboolean on, jstring host, jint port, jstring pass){ char *h=U(e,host), *pw=pass?U(e,pass):NULL, err[256]=""; int rc=C?lsm_relay_set(C,on?1:0,h,port,pw,err):1; (void)k;
    free(h); if(pw){ memset(pw,0,strlen(pw)); free(pw); } return J(e,rc?(err[0]?err:"failed"):""); }
JNIEXPORT jstring JNICALL FN(cancelTransfer)(JNIEnv *e, jclass k, jstring tid){ char *s=U(e,tid), err[256]=""; int rc=C?lsm_transfer_cancel(C,s,err):1; (void)k; free(s); return J(e,rc?(err[0]?err:"failed"):""); }
