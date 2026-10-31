/* v0.6.2 (C core, same protocol as lan-core engine/v062.rs): polls (Ext "poll"/"vote"/"poll_close"),
 * chat folders + archive (local, conv_prefs.bin), disappearing messages (Ext "ttl" syncs the per-chat timer).
 * Included by core.c after p2.c. */
#define POLL_MAX_OPTS 10
static const int TTL_CHOICES[]={0,30,300,3600,86400,604800};
static int ttl_ok(int s){ size_t i; for(i=0;i<sizeof TTL_CHOICES/sizeof TTL_CHOICES[0];i++) if(TTL_CHOICES[i]==s) return 1; return 0; }
void lsm_ttl_label(int s, char *o, size_t n){
    if(s<=0) scpy(o,"\xe5\x85\xb3\xe9\x97\xad",n);                                       /* 关闭 */
    else if(s%86400==0) snprintf(o,n,"%d \xe5\xa4\xa9",s/86400);                          /* 天 */
    else if(s%3600==0) snprintf(o,n,"%d \xe5\xb0\x8f\xe6\x97\xb6",s/3600);               /* 小时 */
    else if(s%60==0) snprintf(o,n,"%d \xe5\x88\x86\xe9\x92\x9f",s/60);                   /* 分钟 */
    else snprintf(o,n,"%d \xe7\xa7\x92",s); }                                            /* 秒 */

/* history record: V id is_poll multi closed opts votes expires */
static void hist_rec_v062(lsm_core *c, lsm_msg *m){ buf_t b={0}; bfmt(&b,"V\t%s\t%d\t%d\t%d\t",m->id,m->is_poll,m->poll_multi,m->poll_closed); esc_put(&b,m->poll_opts?m->poll_opts:""); b_u8(&b,'\t'); esc_put(&b,m->poll_votes); bfmt(&b,"\t%lld",(long long)m->expires_ms); hist_append(c,&b); b_free(&b); }
static void load_v062_rec(lsm_core *c, char **f, int k){ lsm_msg *m; if(k<8 || !(m=msg_get(c,f[1]))) return; m->is_poll=atoi(f[2]); m->poll_multi=atoi(f[3]); m->poll_closed=atoi(f[4]);
    free(m->poll_opts); m->poll_opts=f[5][0]?xstrdup(f[5]):NULL; scpy(m->poll_votes,f[6],sizeof m->poll_votes); m->expires_ms=atoll(f[7]); }

static int conv_muted(lsm_core *c, const char *conv){ lsm_contact *ct; if(strncmp(conv,"dm:",3)) return 0; ct=contact_get(c,conv+3); return ct && ct->muted; }
static void v062_on_add(lsm_core *c, lsm_msg *m){ p2conv *e; if(m->kind==K_SYSTEM) return; e=p2_get(c,m->conv,0); if(!e) return;
    if(e->ttl>0 && !m->expires_ms){ int64_t now=wall_ms(); m->expires_ms=(m->ts_ms>now?m->ts_ms:now)+(int64_t)e->ttl*1000; hist_rec_v062(c,m); }
    if(!m->out && e->archived && !conv_muted(c,m->conv)){ e->archived=0; p2_save(c); emit(c,"conv\t%s",m->conv); } }
static lsm_core *g_v062_core;
static void v062_sweep(lsm_core *c){ static int64_t last; g_v062_core=c; int64_t now=wall_ms(); int i; if(now-last<1000) return; last=now;
    for(i=c->nmsgs-1;i>=0;i--){ lsm_msg *m=&c->msgs[i]; char id[40], conv[64]; buf_t b={0}; if(!m->expires_ms || m->expires_ms>now) continue;
        scpy(id,m->id,40); scpy(conv,m->conv,64); msg_unlink(c,id); tomb_add(c,id); bfmt(&b,"D\t%s",id); hist_append(c,&b); b_free(&b); emit(c,"msg_delete\t%s\t%s",id,conv);
        if(i>c->nmsgs) i=c->nmsgs; } }

/* ---- polls ---- */
static int opts_count(const char *o){ int n=0; if(!o||!*o) return 0; n=1; for(;*o;o++) if(*o=='\n') n++; return n; }
static void votes_set(char *list, size_t n, const char *fp, const int *ch, int k){ char out[1200]="", tmp[1200], *tok; size_t fl=strlen(fp); int i;
    scpy(tmp,list,sizeof tmp);
    for(tok=strtok(tmp,";");tok;tok=strtok(NULL,";")){ if(!strncmp(tok,fp,fl)&&tok[fl]=='=') continue; if(out[0]) strncat(out,";",sizeof out-strlen(out)-1); strncat(out,tok,sizeof out-strlen(out)-1); }
    if(k>0){ char one[200]; size_t o; o=(size_t)snprintf(one,sizeof one,"%s%s=",out[0]?";":"",fp); for(i=0;i<k&&o<sizeof one-4;i++) o+=(size_t)snprintf(one+o,sizeof one-o,"%s%d",i?",":"",ch[i]); if(strlen(out)+strlen(one)<sizeof out) strcat(out,one); }
    scpy(list,out,n); }
/* sorted, unique, in range; single choice keeps the first */
static int clean_choices(const int *in, int n, int nopts, int multi, int *out){ int k=0, i, j; for(i=0;i<n&&i<POLL_MAX_OPTS;i++){ int v=in[i], dup=0; if(v<0||v>=nopts) continue; for(j=0;j<k;j++) if(out[j]==v) dup=1; if(!dup) out[k++]=v; }
    for(i=1;i<k;i++){ int v=out[i]; j=i-1; while(j>=0&&out[j]>v){ out[j+1]=out[j]; j--; } out[j+1]=v; } if(!multi && k>1) k=1; return k; }
static int json_int_array(const char *js, const char *key, int *out, int max){ char pat[40]; const char *p; int k=0; snprintf(pat,sizeof pat,"\"%s\"",key); p=strstr(js,pat); if(!p) return 0; p=strchr(p+strlen(pat),'['); if(!p) return 0; p++;
    while(*p && *p!=']' && k<max){ char *e; long v; while(*p==' '||*p==',') p++; if(*p==']') break; v=strtol(p,&e,10); if(e==p) break; out[k++]=(int)v; p=e; } return k; }
static int json_int(const char *js, const char *key, int dflt){ char pat[40]; const char *p; snprintf(pat,sizeof pat,"\"%s\"",key); p=strstr(js,pat); if(!p) return dflt; p+=strlen(pat); while(*p==' '||*p==':') p++; return (*p=='-'||(*p>='0'&&*p<='9'))?atoi(p):dflt; }
/* "opts":["a","b"] → '\n'-joined */
static char *json_opts(const char *js){ const char *p=strstr(js,"\"opts\""); buf_t b={0}; int k=0; char *r; if(!p) return NULL; p=strchr(p,'['); if(!p) return NULL; p=xj_skipws(p+1);
    while(*p=='"' && k<POLL_MAX_OPTS){ char v[400]; p=xj_str(p,v,sizeof v); if(!p) break; { char *q; for(q=v;*q;q++) if(*q=='\n'||*q=='\r'||*q=='\t') *q=' '; } utf8_trunc_chars(v,100); if(k) b_u8(&b,'\n'); bfmt(&b,"%s",v); k++; p=xj_skipws(p); if(*p==',') p=xj_skipws(p+1); }
    if(k<2){ b_free(&b); return NULL; } b_u8(&b,0); r=xstrdup((char*)b.p); b_free(&b); return r; }

static void v062_recv(lsm_core *c, const char *fp, const char *ids, const char *conv, const char *k, const char *target, const char *js, int64_t ts){
    if(!strcmp(k,"poll")){ char q[1000]; char *opts; lsm_msg *m; xj_get(js,"q",q,sizeof q); utf8_trunc_chars(q,300); opts=json_opts(js);
        if(!q[0]||!opts||msg_get(c,ids)){ free(opts); return; }
        m=add_message(c,ids,conv,0,fp,K_TEXT,q,ts,ST_RECEIVED); m->is_poll=1; m->poll_multi=strstr(js,"\"multi\":true")!=NULL; m->poll_opts=opts; hist_rec_v062(c,m); emit_msg(c,"msg_update",m); }
    else if(!strcmp(k,"vote")){ lsm_msg *m=msg_get(c,target); int in[POLL_MAX_OPTS], ch[POLL_MAX_OPTS], n, kk;
        if(!m||!m->is_poll||m->poll_closed||strcmp(m->conv,conv)) return; n=json_int_array(js,"choices",in,POLL_MAX_OPTS); kk=clean_choices(in,n,opts_count(m->poll_opts),m->poll_multi,ch);
        votes_set(m->poll_votes,sizeof m->poll_votes,fp,ch,kk); hist_rec_v062(c,m); emit_msg(c,"msg_update",m); }
    else if(!strcmp(k,"poll_close")){ lsm_msg *m=msg_get(c,target); if(!m||!m->is_poll||m->out||strcmp(m->conv,conv)||strcmp(m->sender_fp,fp)) return; m->poll_closed=1; hist_rec_v062(c,m); emit_msg(c,"msg_update",m); }
    else if(!strcmp(k,"ttl")){ int s=json_int(js,"secs",-1); p2conv *e; char lab[40], t[400]; lsm_contact *ct; uint8_t sid[16]; char sids[37];
        if(!ttl_ok(s) || !(e=p2_get(c,conv,1)) || e->ttl==s) return; e->ttl=s; p2_save(c); lsm_ttl_label(s,lab,sizeof lab); ct=contact_get(c,fp);
        if(s) snprintf(t,sizeof t,"%s \xe5\xb0\x86\xe6\xb6\x88\xe6\x81\xaf\xe5\xae\x9a\xe6\x97\xb6\xe5\x88\xa0\xe9\x99\xa4\xe8\xae\xbe\xe4\xb8\xba %s",ct?lsm_display_name(ct):fp,lab); /* X 将消息定时删除设为 … */
        else snprintf(t,sizeof t,"%s \xe5\x85\xb3\xe9\x97\xad\xe4\xba\x86\xe6\xb6\x88\xe6\x81\xaf\xe5\xae\x9a\xe6\x97\xb6\xe5\x88\xa0\xe9\x99\xa4",ct?lsm_display_name(ct):fp); /* X 关闭了消息定时删除 */
        uuid_new(sid); lsm_uuid_str(sid,sids); add_message(c,sids,conv,0,"",K_SYSTEM,t,wall_ms(),ST_RECEIVED); emit(c,"conv\t%s",conv); } }

static int conv_ok(lsm_core *c, const char *conv){ uint8_t gid[16]; if(!strncmp(conv,"dm:",3)) return contact_get(c,conv+3)!=NULL; return !strncmp(conv,"group:",6) && !lsm_uuid_parse(conv+6,gid) && group_get(c,gid); }

int lsm_send_poll(lsm_core *c, const char *conv, const char *question, const char *opts_nl, int multi, char *msgid_out, char *err){ char q[1000]; buf_t j={0}, ob={0}, fb={0}; int n=0, online; uint8_t id[16]; char ids[37]; lsm_msg *m; const char *p;
    scpy(q,question?question:"",sizeof q); { char *s=q; while(*s==' ') s++; if(s!=q) memmove(q,s,strlen(s)+1); } utf8_trunc_chars(q,300);
    if(!q[0]){ E("\xe6\x8a\x95\xe7\xa5\xa8\xe9\x97\xae\xe9\xa2\x98\xe4\xb8\x8d\xe8\x83\xbd\xe4\xb8\xba\xe7\xa9\xba"); return -1; } /* 投票问题不能为空 */
    bfmt(&j,"{\"k\":\"poll\",\"q\":"); xj_put(&j,q); bfmt(&j,",\"opts\":["); bfmt(&fb,"[\xe6\x8a\x95\xe7\xa5\xa8] %s",q);
    for(p=opts_nl?opts_nl:"";*p && n<=POLL_MAX_OPTS;){ const char *e=strchr(p,'\n'); size_t l=e?(size_t)(e-p):strlen(p); char o[400]; if(l>=sizeof o) l=sizeof o-1; memcpy(o,p,l); o[l]=0;
        { char *s=o, *t; while(*s==' '||*s=='\r'||*s=='\t') s++; t=s+strlen(s); while(t>s&&(t[-1]==' '||t[-1]=='\r'||t[-1]=='\t')) *--t=0; if(*s){ utf8_trunc_chars(s,100); if(n<POLL_MAX_OPTS){ if(n){ b_u8(&j,','); b_u8(&ob,'\n'); } xj_put(&j,s); bfmt(&ob,"%s",s); bfmt(&fb,"\n%d. %s",n+1,s); } n++; } }
        p=e?e+1:p+l; }
    if(n<2||n>POLL_MAX_OPTS){ b_free(&j); b_free(&ob); b_free(&fb); E("\xe6\x8a\x95\xe7\xa5\xa8\xe9\x9c\x80\xe8\xa6\x81 2\xe2\x80\x93" "10 \xe4\xb8\xaa\xe9\x80\x89\xe9\xa1\xb9"); return -1; } /* 投票需要 2–10 个选项 */
    bfmt(&j,"],\"multi\":%s}",multi?"true":"false"); bfmt(&fb,"\n\xef\xbc\x88\xe5\xaf\xb9\xe6\x96\xb9\xe7\x89\x88\xe6\x9c\xac\xe8\xbe\x83\xe6\x97\xa7\xef\xbc\x8c\xe8\xaf\xb7\xe5\x8d\x87\xe7\xba\xa7\xe5\x90\x8e\xe5\x8f\x82\xe4\xb8\x8e\xe6\x8a\x95\xe7\xa5\xa8\xef\xbc\x89"); b_u8(&fb,0); b_u8(&ob,0);
    mu_lock(&c->mu); if(!conv_ok(c,conv)){ mu_unlock(&c->mu); b_free(&j); b_free(&ob); b_free(&fb); E("\xe4\xbc\x9a\xe8\xaf\x9d\xe4\xb8\x8d\xe5\xad\x98\xe5\x9c\xa8"); return -1; }
    uuid_new(id); lsm_uuid_str(id,ids); m=add_message(c,ids,conv,1,c->fp,K_TEXT,q,wall_ms(),ST_QUEUED); m->is_poll=1; m->poll_multi=multi!=0; m->poll_opts=xstrdup((char*)ob.p); hist_rec_v062(c,m); emit_msg(c,"msg_update",m);
    online=send_ext(c,conv,&j,(char*)fb.p,id,ids); if(online) set_status(c,msg_get(c,ids),ST_SENT);
    if(msgid_out) strcpy(msgid_out,ids); mu_unlock(&c->mu); b_free(&j); b_free(&ob); b_free(&fb); return 0; }
int lsm_vote_poll(lsm_core *c, const char *id, const int *choices, int n, char *err){ lsm_msg *m; int ch[POLL_MAX_OPTS], k, i; buf_t j={0}; uint8_t nid[16];
    mu_lock(&c->mu); m=msg_get(c,id); if(!m||!m->is_poll){ mu_unlock(&c->mu); E("\xe8\xbf\x99\xe4\xb8\x8d\xe6\x98\xaf\xe6\x8a\x95\xe7\xa5\xa8"); return -1; } /* 这不是投票 */
    if(m->poll_closed){ mu_unlock(&c->mu); E("\xe6\x8a\x95\xe7\xa5\xa8\xe5\xb7\xb2\xe7\xbb\x93\xe6\x9d\x9f"); return -1; } /* 投票已结束 */
    k=clean_choices(choices,n,opts_count(m->poll_opts),m->poll_multi,ch); votes_set(m->poll_votes,sizeof m->poll_votes,c->fp,ch,k); hist_rec_v062(c,m); emit_msg(c,"msg_update",m);
    bfmt(&j,"{\"k\":\"vote\",\"target\":"); xj_put(&j,id); bfmt(&j,",\"choices\":["); for(i=0;i<k;i++) bfmt(&j,"%s%d",i?",":"",ch[i]); bfmt(&j,"]}");
    uuid_new(nid); send_ext(c,m->conv,&j,NULL,nid,NULL); b_free(&j); mu_unlock(&c->mu); return 0; }
int lsm_close_poll(lsm_core *c, const char *id, char *err){ lsm_msg *m; buf_t j={0}; uint8_t nid[16];
    mu_lock(&c->mu); m=msg_get(c,id); if(!m||!m->is_poll||!m->out){ mu_unlock(&c->mu); E("\xe5\x8f\xaa\xe8\x83\xbd\xe7\xbb\x93\xe6\x9d\x9f\xe8\x87\xaa\xe5\xb7\xb1\xe5\x8f\x91\xe8\xb5\xb7\xe7\x9a\x84\xe6\x8a\x95\xe7\xa5\xa8"); return -1; } /* 只能结束自己发起的投票 */
    m->poll_closed=1; hist_rec_v062(c,m); emit_msg(c,"msg_update",m);
    bfmt(&j,"{\"k\":\"poll_close\",\"target\":"); xj_put(&j,id); b_u8(&j,'}'); uuid_new(nid); send_ext(c,m->conv,&j,NULL,nid,NULL); b_free(&j); mu_unlock(&c->mu); return 0; }
int lsm_poll_counts(lsm_core *c, const lsm_msg *m, int *counts, int max, int *mine, int *voters){ int n=opts_count(m->poll_opts), i; const char *p=m->poll_votes; size_t fl=strlen(c->fp);
    for(i=0;i<max;i++){ counts[i]=0; if(mine) mine[i]=0; } if(voters) *voters=0;
    while(p&&*p){ const char *e=strchr(p,';'), *eq=strchr(p,'='); int me; if(!eq||(e&&eq>e)){ p=e?e+1:NULL; continue; } me=(size_t)(eq-p)==fl && !strncmp(p,c->fp,fl); if(voters) (*voters)++;
        for(eq++; *eq && (!e||eq<e);){ char *x; long v=strtol(eq,&x,10); if(x==eq) break; if(v>=0&&v<max&&v<n){ counts[v]++; if(me&&mine) mine[v]=1; } eq=x; if(*eq==',') eq++; }
        p=e?e+1:NULL; }
    return n; }

/* ---- archive + folders (this device) ---- */
int lsm_set_conv_archived(lsm_core *c, const char *conv, int on){ p2conv *e; mu_lock(&c->mu); e=p2_get(c,conv,1); if(e){ e->archived=on!=0; p2_save(c); } mu_unlock(&c->mu); if(e) emit(c,"conv\t%s",conv); return e?0:-1; }
int lsm_conv_archived(lsm_core *c, const char *conv){ p2conv *e=p2_get(c,conv,0); return e?e->archived:0; }
int lsm_set_conv_folders(lsm_core *c, const char *conv, const char *csv){ p2conv *e; char out[400]="", tmp[400], *tok; mu_lock(&c->mu); e=p2_get(c,conv,1);
    if(e){ scpy(tmp,csv?csv:"",sizeof tmp); for(tok=strtok(tmp,",");tok;tok=strtok(NULL,",")){ char *s=tok, *t; while(*s==' ') s++; t=s+strlen(s); while(t>s&&t[-1]==' ') *--t=0; if(!*s) continue; utf8_trunc_chars(s,32);
            { char pat[80]; snprintf(pat,sizeof pat,",%s,",s); { char wrap[404]; snprintf(wrap,sizeof wrap,",%s,",out); if(strstr(wrap,pat)) continue; } }
            if(strlen(out)+strlen(s)+2<sizeof out){ if(out[0]) strcat(out,","); strcat(out,s); } }
        scpy(e->folders,out,sizeof e->folders); p2_save(c); }
    mu_unlock(&c->mu); if(e) emit(c,"conv\t%s",conv); return e?0:-1; }
int lsm_conv_folders(lsm_core *c, const char *conv, char *csv, size_t n){ p2conv *e=p2_get(c,conv,0); if(n) csv[0]=0; if(!e||!e->folders[0]) return 0; scpy(csv,e->folders,n); return 1; }
int lsm_folder_names(lsm_core *c, char names[][64], int max){ int i, k=0, a, b2; if(!c->p2||!c->p2->loaded) p2_load(c);
    for(i=0;i<c->p2->n;i++){ char tmp[400], *tok; scpy(tmp,c->p2->v[i].folders,sizeof tmp);
        for(tok=strtok(tmp,",");tok;tok=strtok(NULL,",")){ int dup=0; for(a=0;a<k;a++) if(!strcmp(names[a],tok)) dup=1; if(!dup && k<max) scpy(names[k++],tok,64); } }
    for(a=1;a<k;a++){ char t[64]; scpy(t,names[a],64); b2=a-1; while(b2>=0&&strcmp(names[b2],t)>0){ scpy(names[b2+1],names[b2],64); b2--; } scpy(names[b2+1],t,64); }
    return k; }

/* ---- disappearing messages ---- */
int lsm_set_disappearing(lsm_core *c, const char *conv, int secs, char *err){ p2conv *e; buf_t j={0}; uint8_t nid[16], sid[16]; char sids[37], lab[40], t[300], fb[300];
    if(!ttl_ok(secs)){ E("\xe4\xb8\x8d\xe6\x94\xaf\xe6\x8c\x81\xe7\x9a\x84\xe5\xae\x9a\xe6\x97\xb6"); return -1; } /* 不支持的定时 */
    mu_lock(&c->mu); if(!conv_ok(c,conv) || !(e=p2_get(c,conv,1))){ mu_unlock(&c->mu); E("\xe4\xbc\x9a\xe8\xaf\x9d\xe4\xb8\x8d\xe5\xad\x98\xe5\x9c\xa8"); return -1; }
    e->ttl=secs; p2_save(c); lsm_ttl_label(secs,lab,sizeof lab);
    if(secs){ snprintf(t,sizeof t,"\xe4\xbd\xa0\xe5\xb0\x86\xe6\xb6\x88\xe6\x81\xaf\xe5\xae\x9a\xe6\x97\xb6\xe5\x88\xa0\xe9\x99\xa4\xe8\xae\xbe\xe4\xb8\xba %s\xef\xbc\x88\xe4\xb9\x8b\xe5\x90\x8e\xe7\x9a\x84\xe6\x96\xb0\xe6\xb6\x88\xe6\x81\xaf\xe5\x88\xb0\xe6\x97\xb6\xe8\x87\xaa\xe5\x8a\xa8\xe5\x88\xa0\xe9\x99\xa4\xef\xbc\x89",lab); /* 你将消息定时删除设为 X（之后的新消息到时自动删除） */
        snprintf(fb,sizeof fb,"\xef\xbc\x88\xe6\x88\x91\xe5\xb0\x86\xe6\xb6\x88\xe6\x81\xaf\xe5\xae\x9a\xe6\x97\xb6\xe5\x88\xa0\xe9\x99\xa4\xe8\xae\xbe\xe4\xb8\xba %s\xef\xbc\x8c\xe4\xbd\xa0\xe7\x9a\x84\xe7\x89\x88\xe6\x9c\xac\xe6\x9a\x82\xe4\xb8\x8d\xe6\x94\xaf\xe6\x8c\x81\xef\xbc\x8c\xe8\xaf\xb7\xe5\x8d\x87\xe7\xba\xa7\xef\xbc\x89",lab); }
    else { scpy(t,"\xe4\xbd\xa0\xe5\x85\xb3\xe9\x97\xad\xe4\xba\x86\xe6\xb6\x88\xe6\x81\xaf\xe5\xae\x9a\xe6\x97\xb6\xe5\x88\xa0\xe9\x99\xa4",sizeof t); scpy(fb,"\xef\xbc\x88\xe6\x88\x91\xe5\x85\xb3\xe9\x97\xad\xe4\xba\x86\xe6\xb6\x88\xe6\x81\xaf\xe5\xae\x9a\xe6\x97\xb6\xe5\x88\xa0\xe9\x99\xa4\xef\xbc\x89",sizeof fb); }
    bfmt(&j,"{\"k\":\"ttl\",\"secs\":%d}",secs); uuid_new(nid); send_ext(c,conv,&j,fb,nid,NULL); b_free(&j);
    uuid_new(sid); lsm_uuid_str(sid,sids); add_message(c,sids,conv,0,"",K_SYSTEM,t,wall_ms(),ST_RECEIVED);
    mu_unlock(&c->mu); emit(c,"conv\t%s",conv); return 0; }
int lsm_conv_ttl(lsm_core *c, const char *conv){ p2conv *e=p2_get(c,conv,0); return e?e->ttl:0; }

/* ---- shared UI helpers ---- */
static lsm_core *g_v062_core; /* GUIs run one core per process: lets lsm_display_body mark my own vote */
/* body text for polls / expiring messages: question + options with counts (√ = my vote) + timer line */
static int v062_body(const lsm_msg *m, char *buf, size_t n){ size_t o=0; int cnt[POLL_MAX_OPTS], mine[POLL_MAX_OPTS], voters=0, k=0, i; const char *p;
    if(!n) return 0; buf[0]=0;
    if(m->is_poll){ lsm_core *c=g_v062_core; if(c) k=lsm_poll_counts(c,m,cnt,POLL_MAX_OPTS,mine,&voters); else { k=opts_count(m->poll_opts); for(i=0;i<k&&i<POLL_MAX_OPTS;i++){ cnt[i]=0; mine[i]=0; } }
        o+=(size_t)snprintf(buf+o,n-o,"[%s] %s\n",m->poll_multi?"\xe5\xa4\x9a\xe9\x80\x89\xe6\x8a\x95\xe7\xa5\xa8":"\xe5\x8d\x95\xe9\x80\x89\xe6\x8a\x95\xe7\xa5\xa8",m->body?m->body:""); /* 多选投票 / 单选投票 */
        for(i=0,p=m->poll_opts?m->poll_opts:"";*p&&i<POLL_MAX_OPTS&&o<n;i++){ const char *e=strchr(p,'\n'); int l=e?(int)(e-p):(int)strlen(p);
            o+=(size_t)snprintf(buf+o,n-o," %d. %.*s \xe2\x80\x94 %d \xe7\xa5\xa8%s\n",i+1,l,p,i<k?cnt[i]:0,i<k&&mine[i]?" \xe2\x88\x9a":""); p=e?e+1:p+l; } /* — N 票 √ */
        if(o<n) o+=(size_t)snprintf(buf+o,n-o,"%d \xe4\xba\xba\xe5\x8f\x82\xe4\xb8\x8e%s",voters,m->poll_closed?" \xc2\xb7 \xe5\xb7\xb2\xe7\xbb\x93\xe6\x9d\x9f":" \xc2\xb7 \xe5\x8f\xb3\xe9\x94\xae\xe6\x8a\x95\xe7\xa5\xa8"); } /* N 人参与 · 已结束 / · 右键投票 */
    else o+=(size_t)snprintf(buf+o,n-o,"%s%s",m->body?m->body:"",m->edited?"\xef\xbc\x88\xe5\xb7\xb2\xe7\xbc\x96\xe8\xbe\x91\xef\xbc\x89":"");
    if(m->expires_ms && o<n){ time_t t=(time_t)(m->expires_ms/1000); struct tm tmv; char ts[32];
#ifdef _WIN32
        tmv=*localtime(&t);
#else
        localtime_r(&t,&tmv);
#endif
        strftime(ts,sizeof ts,"%H:%M:%S",&tmv); o+=(size_t)snprintf(buf+o,n-o,"\n[\xe5\xae\x9a\xe6\x97\xb6\xe5\x88\xa0\xe9\x99\xa4 %s]",ts); } /* [定时删除 hh:mm:ss] */
    return (int)o; }
/* filter for chat lists: "" = main list (not archived), "\x01" = archived only, else folder name (not archived) */
int lsm_conv_visible(lsm_core *c, const char *conv, const char *filter){ p2conv *e=p2_get(c,conv,0); int arch=e?e->archived:0;
    if(!filter||!*filter) return !arch; if(!strcmp(filter,LSM_FILTER_ARCHIVED)) return arch;
    if(arch||!e) return 0; { char w[404], pat[80]; snprintf(w,sizeof w,",%s,",e->folders); snprintf(pat,sizeof pat,",%s,",filter); return strstr(w,pat)!=NULL; } }
int lsm_archived_count(lsm_core *c){ int i, k=0; if(!c->p2||!c->p2->loaded) p2_load(c); for(i=0;i<c->p2->n;i++) if(c->p2->v[i].archived) k++; return k; }
/* the poll options of a message as separate strings (caller holds lsm_lock) */
int lsm_poll_option(const lsm_msg *m, int idx, char *out, size_t n){ const char *p=m->poll_opts?m->poll_opts:""; int i=0; if(n) out[0]=0;
    while(*p){ const char *e=strchr(p,'\n'); size_t l=e?(size_t)(e-p):strlen(p); if(i==idx){ if(l>=n) l=n-1; memcpy(out,p,l); out[l]=0; return 1; } i++; p=e?e+1:p+l; } return 0; }
int lsm_my_vote(lsm_core *c, const lsm_msg *m, int idx){ int cnt[POLL_MAX_OPTS], mine[POLL_MAX_OPTS]; if(idx<0||idx>=POLL_MAX_OPTS) return 0; lsm_poll_counts(c,m,cnt,POLL_MAX_OPTS,mine,NULL); return mine[idx]; }
/* toggle option idx in my vote (single choice: replace / clear) */
int lsm_vote_toggle(lsm_core *c, const char *id, int idx, char *err){ lsm_msg *m; int cnt[POLL_MAX_OPTS], mine[POLL_MAX_OPTS], ch[POLL_MAX_OPTS], k=0, i, n, multi;
    mu_lock(&c->mu); m=msg_get(c,id); if(!m||!m->is_poll){ mu_unlock(&c->mu); E("\xe8\xbf\x99\xe4\xb8\x8d\xe6\x98\xaf\xe6\x8a\x95\xe7\xa5\xa8"); return -1; }
    n=lsm_poll_counts(c,m,cnt,POLL_MAX_OPTS,mine,NULL); multi=m->poll_multi; mu_unlock(&c->mu);
    if(idx<0||idx>=n){ E("\xe9\x80\x89\xe9\xa1\xb9\xe4\xb8\x8d\xe5\xad\x98\xe5\x9c\xa8"); return -1; }
    if(multi){ for(i=0;i<n;i++) if(i==idx?!mine[i]:mine[i]) ch[k++]=i; } else if(!mine[idx]) ch[k++]=idx;
    return lsm_vote_poll(c,id,ch,k,err); }
