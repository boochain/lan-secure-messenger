/* Shared host for Android JNI and the HarmonyOS NAPI module. JSON matches jni.c as of 0.7.4. */
#include "lsm_host.h"
#include "core.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lsm_core *C;
static char *keep_dir, *keep_name, *keep_targets;

typedef struct { char *p; size_t n, cap; } sb;
static void sb_put(sb *b, const char *s, size_t n){ if(b->n+n+1>b->cap){ b->cap=(b->n+n+1)*2+256; b->p=realloc(b->p,b->cap); } memcpy(b->p+b->n,s,n); b->n+=n; b->p[b->n]=0; }
static void sb_s(sb *b, const char *s){ sb_put(b,s,strlen(s)); }
static void sb_f(sb *b, const char *fmt, ...){ char t[512]; va_list ap; va_start(ap,fmt); vsnprintf(t,sizeof t,fmt,ap); va_end(ap); sb_s(b,t); }
static void sb_js(sb *b, const char *s){ sb_put(b,"\"",1); for(;s&&*s;s++){ unsigned char c=(unsigned char)*s; char e[8];
        if(c=='"'||c=='\\'){ e[0]='\\'; e[1]=c; sb_put(b,e,2); } else if(c<0x20){ snprintf(e,sizeof e,"\\u%04x",c); sb_put(b,e,6); } else sb_put(b,(char*)&c,1); } sb_put(b,"\"",1); }
static char *sb_take(sb *b){ char *p=b->p?b->p:strdup(""); b->p=NULL; return p; }
static char *sdup(const char *s){ return strdup(s?s:""); }
static char *errstr(int rc, const char *err){ return sdup(rc?(err&&err[0]?err:"failed"):""); }

static const char *TS[]={"offered","active","verifying","done","failed","rejected","cancelled"};
static const char *MS[]={"queued","sent","delivered","read","received","failed","rejected"};

char *lsmh_start(const char *dir, const char *name, const char *targets, int port){
    lsm_opts o; char err[256]="";
    if(C) return sdup("");
    keep_dir=sdup(dir); keep_name=sdup(name); keep_targets=sdup(targets);
    memset(&o,0,sizeof o); o.data_dir=keep_dir; o.name=keep_name; o.announce_targets=keep_targets[0]?keep_targets:NULL;
    o.listen_port=port; o.bind_lan_ip="auto";
    C=lsm_start(&o,err,sizeof err);
    if(!C){ free(keep_dir); free(keep_name); free(keep_targets); keep_dir=keep_name=keep_targets=NULL; }
    return sdup(C?"":(err[0]?err:"start failed"));
}
void lsmh_stop(void){ if(C){ lsm_stop(C); C=NULL; } free(keep_dir); free(keep_name); free(keep_targets); keep_dir=keep_name=keep_targets=NULL; }
int lsmh_running(void){ return C!=NULL; }
char *lsmh_poll(void){ char b[4096]; if(!C||!lsm_poll_event(C,b,sizeof b)) return NULL; return sdup(b); }

char *lsmh_state(void){
    sb b={0}; lsm_contact *c; lsm_group *g; lsm_transfer *t; lsm_msg *m; int n,i,j,nm;
    if(!C) return sdup("{}");
    lsm_lock(C);
    sb_s(&b,"{\"me\":{\"fp\":"); sb_js(&b,lsm_my_fp(C)); sb_s(&b,",\"name\":"); sb_js(&b,lsm_my_name(C)); sb_f(&b,",\"port\":%d,\"version\":",lsm_listen_port(C)); sb_js(&b,LSM_APP_VERSION);
    sb_s(&b,",\"downloads\":"); sb_js(&b,lsm_download_dir(C)); sb_s(&b,",\"sig\":"); sb_js(&b,lsm_my_signature(C)); { char ap[600]; lsm_avatar_path(C,NULL,ap,sizeof ap); sb_s(&b,",\"avatar\":"); sb_js(&b,ap); } sb_s(&b,"},\"contacts\":[");
    n=lsm_contacts(C,&c); nm=lsm_messages(C,&m);
    for(i=0;i<n;i++){ char conv[64]; int un=0; snprintf(conv,64,"dm:%s",c[i].fp); for(j=0;j<nm;j++) if(m[j].unread&&!strcmp(m[j].conv,conv)) un++;
        if(i) sb_s(&b,","); sb_s(&b,"{\"fp\":"); sb_js(&b,c[i].fp); sb_s(&b,",\"name\":"); sb_js(&b,lsm_display_name(&c[i])); sb_s(&b,",\"nick\":"); sb_js(&b,c[i].name); sb_s(&b,",\"remark\":"); sb_js(&b,c[i].remark); sb_s(&b,",\"sig\":"); sb_js(&b,c[i].signature); { char ap[600]; lsm_avatar_path(C,c[i].fp,ap,sizeof ap); sb_s(&b,",\"avatar\":"); sb_js(&b,ap); } sb_f(&b,",\"trust\":%d,\"online\":%s,\"rtt\":%d,\"unread\":%d,\"addr\":",c[i].trust,c[i].online?"true":"false",c[i].rtt_ms,un);
        sb_js(&b,c[i].addr); sb_s(&b,",\"version\":"); sb_js(&b,c[i].app_version); sb_f(&b,",\"via\":%s",c[i].via_relay?"true":"false"); { char d[600]; int has=lsm_get_draft(C,conv,d,sizeof d); sb_f(&b,",\"pinned\":%s,\"draft\":",lsm_conv_pinned(C,conv)?"true":"false"); sb_js(&b,has?d:""); }
        { char fo[400]; lsm_conv_folders(C,conv,fo,sizeof fo); sb_f(&b,",\"archived\":%s,\"ttl\":%d,\"folders\":",lsm_conv_archived(C,conv)?"true":"false",lsm_conv_ttl(C,conv)); sb_js(&b,fo); } sb_s(&b,"}"); }
    sb_s(&b,"],\"groups\":["); n=lsm_groups(C,&g);
    for(i=0;i<n;i++){ char u[37], conv[64]; int un=0; lsm_uuid_str(g[i].id,u); snprintf(conv,64,"group:%s",u); for(j=0;j<nm;j++) if(m[j].unread&&!strcmp(m[j].conv,conv)) un++;
        if(i) sb_s(&b,","); sb_s(&b,"{\"id\":"); sb_js(&b,u); sb_s(&b,",\"name\":"); sb_js(&b,g[i].name); { char fo[400]; lsm_conv_folders(C,conv,fo,sizeof fo); sb_f(&b,",\"archived\":%s,\"ttl\":%d,\"folders\":",lsm_conv_archived(C,conv)?"true":"false",lsm_conv_ttl(C,conv)); sb_js(&b,fo); }
        sb_f(&b,",\"epoch\":%llu,\"unread\":%d,\"pinned\":%s,\"draft\":",(unsigned long long)g[i].key_epoch,un,lsm_conv_pinned(C,conv)?"true":"false"); { char d[600]; sb_js(&b,lsm_get_draft(C,conv,d,sizeof d)?d:""); } sb_s(&b,",\"members\":[");
        for(j=0;j<g[i].nmembers;j++){ if(j) sb_s(&b,","); sb_js(&b,g[i].members[j]); } sb_s(&b,"]}"); }
    sb_s(&b,"],\"transfers\":["); n=lsm_transfers(C,&t);
    for(i=0;i<n;i++){ char u[37]; lsm_uuid_str(t[i].tid,u); if(i) sb_s(&b,","); sb_s(&b,"{\"id\":"); sb_js(&b,u); sb_s(&b,",\"msg\":"); sb_js(&b,t[i].msg_id); sb_s(&b,",\"name\":"); sb_js(&b,t[i].name);
        sb_f(&b,",\"out\":%s,\"size\":%llu,\"done\":%llu,\"state\":\"%s\",\"path\":",t[i].out?"true":"false",(unsigned long long)t[i].size,(unsigned long long)t[i].transferred,TS[t[i].state]); sb_js(&b,t[i].path);
        sb_s(&b,",\"error\":"); sb_js(&b,t[i].error); { char st[256]; lsm_transfer_status(C,&t[i],st,sizeof st); sb_s(&b,",\"status\":"); sb_js(&b,st); } sb_f(&b,",\"resumed\":%llu}",(unsigned long long)t[i].resumed_from); }
    { int en=0, port=0, hp=0; char host[256]="", det[256]=""; const char *rs;
      lsm_unlock(C); lsm_relay_get(C,&en,host,sizeof host,&port,&hp); lsm_lock(C); rs=lsm_relay_status(C,det,sizeof det);
      sb_f(&b,"],\"relay\":{\"enabled\":%s,\"port\":%d,\"has_pass\":%s,\"peers\":%d,\"state\":",en?"true":"false",port,hp?"true":"false",lsm_relay_peers(C)); sb_js(&b,rs);
      sb_s(&b,",\"host\":"); sb_js(&b,host); sb_s(&b,",\"detail\":"); sb_js(&b,det); sb_s(&b,"}"); }
    sb_s(&b,"}"); lsm_unlock(C); return sb_take(&b);
}

char *lsmh_messages(const char *conv){
    sb b={0}; lsm_msg *m; int n,i,first=1;
    if(!C) return sdup("[]");
    sb_s(&b,"["); lsm_lock(C); n=lsm_messages(C,&m);
    for(i=0;i<n;i++){ if(strcmp(m[i].conv,conv?conv:"")) continue; if(!first) sb_s(&b,","); first=0;
        sb_s(&b,"{\"id\":"); sb_js(&b,m[i].id); sb_f(&b,",\"out\":%s,\"kind\":%d,\"status\":\"%s\",\"ts\":%lld,\"sender\":",m[i].out?"true":"false",m[i].kind,MS[m[i].status],(long long)m[i].ts_ms);
        sb_js(&b,m[i].sender_name); sb_s(&b,",\"body\":"); sb_js(&b,m[i].body); sb_s(&b,",\"file\":"); sb_js(&b,m[i].file_name); sb_f(&b,",\"size\":%llu,\"path\":",(unsigned long long)m[i].file_size); sb_js(&b,m[i].file_path);
        { char rs[300], mine[40]=""; lsm_reaction_summary(&m[i],rs,sizeof rs); lsm_my_reaction(C,&m[i],mine,sizeof mine);
          sb_s(&b,",\"reply_to\":"); sb_js(&b,m[i].reply_to); sb_s(&b,",\"quote\":"); sb_js(&b,m[i].quote); sb_s(&b,",\"reactions\":"); sb_js(&b,rs); sb_s(&b,",\"mine\":"); sb_js(&b,mine);
          sb_f(&b,",\"edited\":%s,\"recalled\":%s,\"can_modify\":%s,\"voice\":%d",m[i].edited?"true":"false",m[i].recalled?"true":"false",lsm_can_modify(C,&m[i])?"true":"false",lsm_voice_secs(m[i].file_name));
          sb_f(&b,",\"forwarded\":%s,\"pinned\":%s,\"starred\":%s,\"mentions_me\":%s",m[i].forwarded?"true":"false",m[i].pinned?"true":"false",m[i].starred?"true":"false",m[i].mentions_me?"true":"false");
          sb_f(&b,",\"expires\":%lld",(long long)m[i].expires_ms);
          sb_s(&b,",\"status_text\":"); sb_js(&b,lsm_status_text(C,&m[i])); sb_f(&b,",\"can_resend\":%s",lsm_can_resend(&m[i])?"true":"false");
          if(m[i].is_poll){ int cnt[10], mn[10], voters=0, k=lsm_poll_counts(C,&m[i],cnt,10,mn,&voters), q; sb_f(&b,",\"poll\":{\"multi\":%s,\"closed\":%s,\"voters\":%d,\"opts\":[",m[i].poll_multi?"true":"false",m[i].poll_closed?"true":"false",voters);
            for(q=0;q<k&&q<10;q++){ char o[400]; lsm_poll_option(&m[i],q,o,sizeof o); if(q) sb_s(&b,","); sb_s(&b,"{\"t\":"); sb_js(&b,o); sb_f(&b,",\"n\":%d,\"mine\":%s}",cnt[q],mn[q]?"true":"false"); }
            sb_s(&b,"]}"); } }
        sb_s(&b,"}"); }
    lsm_unlock(C); sb_s(&b,"]"); return sb_take(&b);
}

char *lsmh_send_text(const char *conv, const char *text){ char id[40], err[256]=""; int rc=C?lsm_send_text(C,conv,text,id,err):1; return errstr(rc,err); }
char *lsmh_send_file(const char *conv, const char *path){ char err[256]=""; int rc=C?lsm_send_file(C,conv,path,err):1; return errstr(rc,err); }
char *lsmh_add_addr(const char *addr){ char err[256]=""; int rc=C?lsm_add_addr(C,addr,err):1; return errstr(rc,err); }
void lsmh_refresh_peers(void){ if(C) lsm_refresh_peers(C); }
void lsmh_wake(void){ if(C) lsm_wake(C); }
void lsmh_reconnect_server(void){ if(C) lsm_reconnect_server(C); }
char *lsmh_set_trust(const char *fp, int trust){ char err[256]=""; int rc=C?lsm_set_trust(C,fp,trust,err):1; return errstr(rc,err); }
char *lsmh_set_muted(const char *fp, int muted){ char err[256]=""; int rc=C?lsm_set_muted(C,fp,muted?1:0,err):1; return errstr(rc,err); }
int lsmh_is_muted(const char *fp){ int m=0,i,n; lsm_contact *ks; if(C){ lsm_lock(C); n=lsm_contacts(C,&ks); for(i=0;i<n;i++) if(!strcmp(ks[i].fp,fp?fp:"")){ m=ks[i].muted; break; } lsm_unlock(C);} return m; }
char *lsmh_diagnostics(void){ size_t n; char *b; if(!C) return sdup(""); n=lsm_diagnostics(C,NULL,0); b=(char*)malloc(n+1); if(!b) return sdup(""); lsm_diagnostics(C,b,n+1); return b; }
char *lsmh_pin_offer(void){ char pin[8]=""; if(C) lsm_pin_offer(C,pin); return sdup(pin); }
char *lsmh_pin_pair(const char *fp, const char *pin){ char err[256]=""; int rc=C?lsm_pin_pair(C,fp,pin,err):1; return errstr(rc,err); }
static int split_csv(char *s, const char **out){ int n=0; char *p=s; while(p&&*p&&n<64){ out[n++]=p; while(*p&&*p!=',') p++; if(*p) *p++=0; } return n; }
char *lsmh_group_create(const char *name, const char *fps){ char *f=sdup(fps), gid[40]="", err[256]="", m[300]; const char *a[64]; int n=split_csv(f,a), rc=C?lsm_group_create(C,name,a,n,gid,err):1; free(f);
    if(rc){ snprintf(m,sizeof m,"!%s",err[0]?err:"failed"); return sdup(m); } return sdup(gid); }
char *lsmh_group_invite(const char *gid, const char *fps){ char *f=sdup(fps), err[256]=""; const char *a[64]; int n=split_csv(f,a), rc=C?lsm_group_invite(C,gid,a,n,err):1; free(f); return errstr(rc,err); }
char *lsmh_group_remove(const char *gid, const char *fp){ char err[256]=""; int rc=C?lsm_group_remove(C,gid,fp,err):1; return errstr(rc,err); }
char *lsmh_group_leave(const char *gid){ char err[256]=""; int rc=C?lsm_group_leave(C,gid,err):1; return errstr(rc,err); }
int lsmh_delete_message(const char *id){ return C&&lsm_delete_message(C,id)==0; }
int lsmh_clear_conversation(const char *conv){ return C?lsm_clear_conversation(C,conv):0; }
void lsmh_mark_read(const char *conv){ if(C) lsm_mark_read(C,conv); }
void lsmh_mark_unread(const char *conv){ if(C) lsm_mark_unread(C,conv); }
void lsmh_set_name(const char *name){ if(C) lsm_set_name(C,name); }
char *lsmh_relay_set(int on, const char *host, int port, const char *pass, int has_pass){ char err[256]="", *pw=NULL, *r; if(has_pass) pw=sdup(pass?pass:""); r=errstr(C?lsm_relay_set(C,on?1:0,host,port,pw,err):1, err); if(pw){ memset(pw,0,strlen(pw)); free(pw);} return r; }
char *lsmh_cancel_transfer(const char *tid){ char err[256]=""; int rc=C?lsm_transfer_cancel(C,tid,err):1; return errstr(rc,err); }
char *lsmh_storage_info(void){ size_t n; char *b; if(!C) return sdup(""); n=lsm_storage_info(C,NULL,0); b=(char*)malloc(n+1); if(!b) return sdup(""); lsm_storage_info(C,b,n+1); return b; }
int lsmh_cleanup_storage(int orphans, int finished){ return C?lsm_cleanup_storage(C,orphans?1:0,finished?1:0):0; }

char *lsmh_history_query(const char *query, const char *convs, int64_t from_ms, int64_t to_ms, int types, int dir, int statuses, int offset, int limit){
    sb b={0}; lsm_hq q; int *idx, n, tot=0, i; lsm_msg *m;
    if(!C) return sdup("{\"total\":0,\"items\":[]}");
    if(limit<=0||limit>500) limit=50; idx=(int*)malloc(sizeof(int)*limit); memset(&q,0,sizeof q); q.query=query; q.convs=convs; q.from_ms=from_ms; q.to_ms=to_ms; q.types=types; q.direction=dir; q.statuses=statuses; q.offset=offset; q.limit=limit;
    lsm_lock(C); n=lsm_history_query(C,&q,idx,limit,&tot); lsm_messages(C,&m); sb_f(&b,"{\"total\":%d,\"items\":[",tot);
    for(i=0;i<n;i++){ lsm_msg *x=&m[idx[i]]; if(i) sb_s(&b,","); sb_s(&b,"{\"id\":"); sb_js(&b,x->id); sb_s(&b,",\"conv\":"); sb_js(&b,x->conv); sb_f(&b,",\"out\":%s,\"kind\":%d,\"type\":%d,\"status\":\"%s\",\"ts\":%lld,\"sender\":",x->out?"true":"false",x->kind,lsm_msg_type(x),MS[x->status],(long long)x->ts_ms);
        sb_js(&b,x->sender_name); sb_s(&b,",\"body\":"); sb_js(&b,x->body); sb_s(&b,",\"file\":"); sb_js(&b,x->file_name); sb_s(&b,",\"path\":"); sb_js(&b,x->file_path); sb_s(&b,"}"); }
    lsm_unlock(C); sb_s(&b,"]}"); free(idx); return sb_take(&b);
}
char *lsmh_set_profile(const char *sig, int has_sig, const char *avatar, int has_avatar){ char err[256]=""; int rc=C?lsm_set_profile(C,has_sig?sig:NULL,has_avatar?avatar:NULL,err):1; return errstr(rc,err); }
void lsmh_set_remark(const char *fp, const char *remark){ if(C) lsm_set_remark(C,fp,remark); }
char *lsmh_send_reply(const char *conv, const char *text, const char *reply_to){ char id[40], err[256]=""; int rc=C?lsm_send_text2(C,conv,text,reply_to&&reply_to[0]?reply_to:NULL,NULL,id,err):1; return errstr(rc,err); }
char *lsmh_react(const char *id, const char *emoji){ char err[256]=""; int rc=C?lsm_react(C,id,emoji,err):1; return errstr(rc,err); }
char *lsmh_edit_message(const char *id, const char *body){ char err[256]=""; int rc=C?lsm_edit_message(C,id,body,err):1; return errstr(rc,err); }
char *lsmh_recall_message(const char *id){ char err[256]=""; int rc=C?lsm_recall_message(C,id,err):1; return errstr(rc,err); }
char *lsmh_voice_name(int secs, const char *ext){ char nm[128]; lsm_voice_name(secs,ext,nm,sizeof nm); return sdup(nm); }
char *lsmh_send_poll(const char *conv, const char *question, const char *opts_nl, int multi){ char err[256]="", id[40]; int rc=C?lsm_send_poll(C,conv,question,opts_nl,multi?1:0,id,err):1; return errstr(rc,err); }
char *lsmh_vote_poll(const char *id, int opt){ char err[256]=""; int rc=C?lsm_vote_toggle(C,id,opt,err):1; return errstr(rc,err); }
char *lsmh_close_poll(const char *id){ char err[256]=""; int rc=C?lsm_close_poll(C,id,err):1; return errstr(rc,err); }
void lsmh_set_archived(const char *conv, int on){ if(C) lsm_set_conv_archived(C,conv,on?1:0); }
void lsmh_set_folders(const char *conv, const char *csv){ if(C) lsm_set_conv_folders(C,conv,csv); }
char *lsmh_set_ttl(const char *conv, int secs){ char err[256]=""; int rc=C?lsm_set_disappearing(C,conv,secs,err):1; return errstr(rc,err); }
char *lsmh_send_text2(const char *conv, const char *text, const char *reply_to){ char id[40], err[256]="", csv[64*(FP_LEN+1)]=""; int rc=1;
    if(C){ lsm_lock(C); lsm_mentions_in(C,conv,text,csv,sizeof csv); lsm_unlock(C); rc=(reply_to&&reply_to[0]||csv[0])?lsm_send_text2(C,conv,text,reply_to&&reply_to[0]?reply_to:NULL,csv[0]?csv:NULL,id,err):lsm_send_text(C,conv,text,id,err); if(!rc) lsm_set_draft(C,conv,""); }
    return errstr(rc,err); }
void lsmh_set_conv_pinned(const char *conv, int on){ if(C) lsm_set_conv_pinned(C,conv,on?1:0); }
void lsmh_set_draft(const char *conv, const char *text){ if(C) lsm_set_draft(C,conv,text); }
char *lsmh_get_draft(const char *conv){ char *d=(char*)malloc(16384); d[0]=0; if(C){ lsm_lock(C); lsm_get_draft(C,conv,d,16384); lsm_unlock(C);} return d; }
char *lsmh_set_msg_flags(const char *id, int pinned, int starred){ char err[256]=""; int rc=C?lsm_set_msg_flags(C,id,pinned,starred,err):1; return errstr(rc,err); }
void lsmh_send_typing(const char *conv){ if(C) lsm_send_typing(C,conv); }
char *lsmh_forward(const char *id, const char *conv){ char err[256]="", nid[40]; int rc=C?lsm_forward(C,id,conv,nid,err):1; return errstr(rc,err); }
char *lsmh_flagged(const char *conv, int which){ sb b={0}; int idx[500], n, i; lsm_msg *m; sb_s(&b,"[");
    if(C){ lsm_lock(C); n=lsm_flagged(C,conv&&conv[0]?conv:NULL,which,idx,500); lsm_messages(C,&m);
        for(i=0;i<n;i++){ lsm_msg *x=&m[idx[i]]; char db[600]; if(i) sb_s(&b,","); sb_s(&b,"{\"id\":"); sb_js(&b,x->id); sb_s(&b,",\"conv\":"); sb_js(&b,x->conv); sb_s(&b,",\"sender\":"); sb_js(&b,x->out?"我":x->sender_name);
            sb_s(&b,",\"body\":"); sb_js(&b,x->kind==K_FILE?x->file_name:lsm_display_body(x,db,sizeof db)); sb_f(&b,",\"ts\":%lld}",(long long)x->ts_ms); }
        lsm_unlock(C); }
    sb_s(&b,"]"); return sb_take(&b); }
char *lsmh_mention_candidates(const char *conv){ sb b={0}; static char nm[64][128], fp[64][FP_LEN]; int n=0,i; sb_s(&b,"[");
    if(C){ lsm_lock(C); n=lsm_mention_candidates(C,conv,nm,fp,64); for(i=0;i<n;i++){ if(i) sb_s(&b,","); sb_s(&b,"{\"fp\":"); sb_js(&b,fp[i]); sb_s(&b,",\"name\":"); sb_js(&b,nm[i]); sb_s(&b,"}"); } lsm_unlock(C); }
    sb_s(&b,"]"); return sb_take(&b); }
char *lsmh_my_uri(void){ char u[400]=""; if(C) lsm_my_uri(C,u,sizeof u); return sdup(u); }
char *lsmh_qr(const char *text){ static uint8_t q[177*177]; char *o; int n=lsm_qr(text?text:"",q), i;
    o=(char*)malloc((size_t)n*n+16); if(!o) return sdup(""); i=snprintf(o,16,"%d:",n); { int x; for(x=0;x<n*n;x++) o[i+x]=q[x]?'1':'0'; o[i+n*n]=0; } return o; }
char *lsmh_add_uri(const char *uri){ char err[256]=""; int rc=C?lsm_add_uri(C,uri,err):1; return errstr(rc,err); }
char *lsmh_apply_uri(const char *uri){ char err[256]=""; int rc=C?lsm_apply_uri(C,uri,err):1; return errstr(rc,err); }
int lsmh_first_run_done(void){ return C && lsm_first_run_done(C); }
char *lsmh_account_mode(void){ return sdup(C?lsm_account_mode(C):"device"); }
char *lsmh_complete_first_run(const char *name, const char *mode, const char *restore){ char err[256]=""; if(C) lsm_complete_first_run(C,name,mode,restore&&restore[0]?restore:NULL,err); return sdup(err); }
char *lsmh_account_setup(void){ char rk[80]="", err[256]=""; if(C) lsm_account_setup(C,rk,sizeof rk,err); return sdup(rk[0]?rk:err); }
char *lsmh_set_account_mode(const char *mode){ char rk[80]="", err[256]=""; if(C) lsm_set_account_mode(C,mode,rk,sizeof rk,err); return sdup(rk[0]?rk:err); }
char *lsmh_reveal_recovery_key(void){ char rk[80]=""; if(C) lsm_reveal_recovery_key(C,rk,sizeof rk); return sdup(rk); }
char *lsmh_account_devices(int unlink_others){ char b[1600]=""; if(C) lsm_account_devices(C,unlink_others?2:0,b,sizeof b); return sdup(b); }
char *lsmh_account_pull_prev(void){ char b[400]=""; if(C) lsm_account_pull_prev(C,b,sizeof b); return sdup(b); }
char *lsmh_relay_uri(void){ char u[400]=""; if(C) lsm_relay_uri(C,u,sizeof u); return sdup(u); }
char *lsmh_account_pair_uri(void){ char u[500]="", err[256]=""; if(C && lsm_account_pair_uri(C,u,sizeof u,err)) return sdup(err[0]?err:"failed"); return sdup(u); }
char *lsmh_resend(const char *id){ char err[256]="", nid[40]=""; int rc=C?lsm_resend(C,id,nid,err):1; return errstr(rc,err); }
char *lsmh_file_preview(const char *conv, const char *path){ char o[700]=""; if(C) lsm_file_preview(C,conv,path,o,sizeof o); return sdup(o); }
char *lsmh_last_editable(const char *conv){ char id[40]=""; if(C){ lsm_lock(C); if(!lsm_last_editable(C,conv,id)) id[0]=0; lsm_unlock(C);} return sdup(id); }
