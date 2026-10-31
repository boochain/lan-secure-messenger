/* v0.6.1 P1: 引用回复 / @提及 / 表情回应 / 编辑 / 撤回 over Payload::Ext (index 6, CAP_EXT) — same JSON ops as lan-core engine/ext.rs.
 * Peers without CAP_EXT get a plain-text fallback (reactions are not sent to them). Included from core.c. */
#define EDIT_WINDOW_MS (24LL*3600*1000)

/* ---- tiny JSON helpers (flat objects; values: strings / string arrays) ---- */
static void xj_put(buf_t *b, const char *s){ b_u8(b,'"'); for(;s&&*s;s++){ unsigned char ch=(unsigned char)*s; if(ch=='"'||ch=='\\'){ b_u8(b,'\\'); b_u8(b,ch); } else if(ch<0x20) bfmt(b,"\\u%04x",ch); else b_u8(b,ch); } b_u8(b,'"'); }
static const char *xj_skipws(const char *p){ while(*p==' '||*p=='\t'||*p=='\n'||*p=='\r') p++; return p; }
static const char *xj_str(const char *p, char *out, size_t n){ size_t o=0; if(*p!='"') return NULL; p++;
    while(*p && *p!='"'){ unsigned cp=0; if(*p=='\\'){ p++; switch(*p){ case 'n': cp='\n'; break; case 't': cp='\t'; break; case 'r': cp='\r'; break; case 'b': cp=8; break; case 'f': cp=12; break;
            case 'u': { int i; for(i=1;i<=4;i++){ char h=p[i]; cp=cp*16+(unsigned)(h>='a'?h-'a'+10:h>='A'?h-'A'+10:h-'0'); } p+=4;
                if(cp>=0xD800&&cp<0xDC00&&p[1]=='\\'&&p[2]=='u'){ unsigned lo=0; for(i=3;i<=6;i++){ char h=p[i]; lo=lo*16+(unsigned)(h>='a'?h-'a'+10:h>='A'?h-'A'+10:h-'0'); } cp=0x10000+((cp-0xD800)<<10)+(lo-0xDC00); p+=6; } break; }
            default: cp=(unsigned char)*p; }
            if(o+4<n){ if(cp<0x80) out[o++]=(char)cp; else if(cp<0x800){ out[o++]=(char)(0xC0|(cp>>6)); out[o++]=(char)(0x80|(cp&63)); } else if(cp<0x10000){ out[o++]=(char)(0xE0|(cp>>12)); out[o++]=(char)(0x80|((cp>>6)&63)); out[o++]=(char)(0x80|(cp&63)); } else { out[o++]=(char)(0xF0|(cp>>18)); out[o++]=(char)(0x80|((cp>>12)&63)); out[o++]=(char)(0x80|((cp>>6)&63)); out[o++]=(char)(0x80|(cp&63)); } }
            p++; continue; }
        if(o+1<n) out[o++]=*p; p++; }
    out[o]=0; return *p=='"'?p+1:NULL; }
/* value of "key": string → out; array of strings → comma-joined; returns 1 if found */
static int xj_get(const char *j, const char *key, char *out, size_t n){ const char *p=j; size_t kl=strlen(key); out[0]=0;
    while((p=strchr(p,'"'))){ char k[64]; const char *q=xj_str(p,k,sizeof k); if(!q) return 0; q=xj_skipws(q);
        if(*q==':'){ q=xj_skipws(q+1); if(strlen(k)==kl && !strcmp(k,key)){
                if(*q=='"') return xj_str(q,out,n)!=NULL;
                if(*q=='['){ size_t o=0; q=xj_skipws(q+1); while(*q=='"'){ char v[256]; q=xj_str(q,v,sizeof v); if(!q) break; if(o && o+1<n) out[o++]=','; { size_t vl=strlen(v); if(o+vl<n){ memcpy(out+o,v,vl); o+=vl; } } q=xj_skipws(q); if(*q==',') q=xj_skipws(q+1); } out[o]=0; return 1; }
                return 0; }
            /* skip the value */
            if(*q=='"'){ char tmp[8]; char *big=(char*)xmalloc(strlen(q)+1); q=xj_str(q,big,strlen(q)+1); free(big); (void)tmp; if(!q) return 0; p=q; continue; }
            p=q; continue; }
        p=q; }
    return 0; }

static int peer_has_ext(lsm_core *c, const char *fp){ conn *k=conn_by_fp(c,fp); lsm_contact *ct; if(k) return (k->rcaps&CAP_EXT)!=0; ct=contact_get(c,fp); return ct && (ct->caps&CAP_EXT); }

static void hist_rec_ext(lsm_core *c, lsm_msg *m){ buf_t b={0}; bfmt(&b,"X\t%s\t%s\t",m->id,m->reply_to); esc_put(&b,m->quote); b_u8(&b,'\t'); esc_put(&b,m->reactions); bfmt(&b,"\t%d\t%d\t",m->edited,m->recalled); esc_put(&b,m->body?m->body:""); b_u8(&b,'\t'); esc_put(&b,m->mentions); hist_append(c,&b); b_free(&b); }
static void load_ext_rec(lsm_core *c, char **f, int k){ lsm_msg *m; if(k<8 || !(m=msg_get(c,f[1]))) return; scpy(m->reply_to,f[2],40); scpy(m->quote,f[3],sizeof m->quote); scpy(m->reactions,f[4],sizeof m->reactions); m->edited=atoi(f[5]); m->recalled=atoi(f[6]); free(m->body); m->body=xstrdup(f[7]); if(k>=9) scpy(m->mentions,f[8],sizeof m->mentions); }

static void quote_of(lsm_core *c, const char *id, char *out, size_t n){ lsm_msg *m=msg_get(c,id); char w[300]; int i; out[0]=0; if(!m) return;
    if(m->recalled) scpy(w,"\xef\xbc\x88\xe5\xb7\xb2\xe6\x92\xa4\xe5\x9b\x9e\xef\xbc\x89",sizeof w); else if(m->kind==K_FILE) snprintf(w,sizeof w,"[\xe6\x96\x87\xe4\xbb\xb6] %s",m->file_name); else scpy(w,m->body?m->body:"",sizeof w);
    for(i=0;w[i];i++) if(w[i]=='\n'||w[i]=='\r'||w[i]=='\t') w[i]=' '; utf8_trunc_chars(w,80);
    snprintf(out,n,"%s\xef\xbc\x9a%s",m->out?"\xe6\x88\x91":m->sender_name,w); }

/* send one op; fallback NULL = skip peers without CAP_EXT. returns 1 if any recipient online */
static int send_ext(lsm_core *c, const char *conv, const buf_t *json, const char *fallback, const uint8_t id[16], const char *msg_ref){ int64_t ts=wall_ms(); int any=0;
    if(!strncmp(conv,"dm:",3)){ buf_t p={0}; const char *fp=conv+3;
        if(peer_has_ext(c,fp)){ b_u32(&p,6); b_u8(&p,0); b_u64(&p,0); b_bytes(&p,json->p,json->n); b_u64(&p,(uint64_t)ts); }
        else if(fallback){ b_u32(&p,0); b_str(&p,fallback); b_u64(&p,(uint64_t)ts); }
        else return 0;
        any=send_reliable(c,fp,&p,id,msg_ref); b_free(&p); return any; }
    if(!strncmp(conv,"group:",6)){ uint8_t gid[16]; lsm_group *g; size_t sn, fn=0; uint8_t *sealed, *fs=NULL; int i; buf_t pe={0}, pf={0};
        if(lsm_uuid_parse(conv+6,gid)||!(g=group_get(c,gid))) return 0;
        sealed=seal_rand(g->key,json->p,json->n,&sn); b_u32(&pe,6); b_u8(&pe,1); b_uuid(&pe,gid); b_u64(&pe,g->key_epoch); b_bytes(&pe,sealed,sn); b_u64(&pe,(uint64_t)ts); free(sealed);
        if(fallback){ fs=seal_rand(g->key,(const uint8_t*)fallback,strlen(fallback),&fn); b_u32(&pf,1); b_uuid(&pf,gid); b_u64(&pf,g->key_epoch); b_bytes(&pf,fs,fn); b_u64(&pf,(uint64_t)ts); free(fs); }
        for(i=0;i<g->nmembers;i++){ const char *mfp=g->members[i]; if(!strcmp(mfp,c->fp)) continue;
            if(peer_has_ext(c,mfp)) any|=send_reliable(c,mfp,&pe,id,msg_ref); else if(fallback) any|=send_reliable(c,mfp,&pf,id,msg_ref); }
        b_free(&pe); b_free(&pf); }
    return any; }

static void react_set(char *list, size_t n, const char *fp, const char *emoji){ char out[sizeof(((lsm_msg*)0)->reactions)]="", *tok, tmp[sizeof out]; size_t fl=strlen(fp);
    scpy(tmp,list,sizeof tmp); for(tok=strtok(tmp,";");tok;tok=strtok(NULL,";")){ if(!strncmp(tok,fp,fl) && tok[fl]=='=') continue; if(out[0]) strncat(out,";",sizeof out-strlen(out)-1); strncat(out,tok,sizeof out-strlen(out)-1); }
    if(emoji && *emoji){ if(out[0]) strncat(out,";",sizeof out-strlen(out)-1); strncat(out,fp,sizeof out-strlen(out)-1); strncat(out,"=",sizeof out-strlen(out)-1); strncat(out,emoji,sizeof out-strlen(out)-1); }
    scpy(list,out,n); }
static int react_get(const char *list, const char *fp, char *emoji, size_t n){ const char *p=list; size_t fl=strlen(fp); emoji[0]=0;
    while(p && *p){ const char *e=strchr(p,';'); size_t l=e?(size_t)(e-p):strlen(p); if(l>fl && !strncmp(p,fp,fl) && p[fl]=='='){ size_t vl=l-fl-1; if(vl>=n) vl=n-1; memcpy(emoji,p+fl+1,vl); emoji[vl]=0; return 1; } p=e?e+1:NULL; } return 0; }

static void ext_recv(lsm_core *c, const char *fp, const char *ids, rd_t *r){ int hasg=r_u8(r); uint8_t gid[16]; uint64_t ep; size_t dl; const uint8_t *d; int64_t ts; char conv[64], *js=NULL, k[16], target[64];
    if(hasg) r_uuid(r,gid); ep=r_u64(r); d=r_bytes(r,&dl); ts=(int64_t)r_u64(r); (void)ep; if(r->err) return;
    if(hasg){ lsm_group *g=group_get(c,gid); size_t pn; uint8_t *pl; if(!g||!group_is_member(g,fp)) return; conv_group(gid,conv); pl=open_rand(g->key,d,dl,&pn); if(!pl) return; js=(char*)xmalloc(pn+1); memcpy(js,pl,pn); js[pn]=0; free(pl); }
    else { conv_dm(fp,conv); js=(char*)xmalloc(dl+1); memcpy(js,d,dl); js[dl]=0; }
    xj_get(js,"k",k,sizeof k); xj_get(js,"target",target,sizeof target);
    if(!strcmp(k,"text")){ size_t bl=strlen(js)+1; char *body=(char*)xmalloc(bl), rt[64], q[400], mn[512]; lsm_msg *m;
        xj_get(js,"body",body,bl); xj_get(js,"reply_to",rt,sizeof rt); xj_get(js,"quote",q,sizeof q); xj_get(js,"mentions",mn,sizeof mn); utf8_trunc(body,64*1024*3);
        if(!msg_get(c,ids)){ m=add_message(c,ids,conv,0,fp,K_TEXT,body,ts,ST_RECEIVED); scpy(m->reply_to,rt,40); if(!q[0] && rt[0]) quote_of(c,rt,q,sizeof q); utf8_trunc_chars(q,200); scpy(m->quote,q,sizeof m->quote); scpy(m->mentions,mn,sizeof m->mentions); if(m->reply_to[0]||m->quote[0]||m->mentions[0]){ hist_rec_ext(c,m); emit_msg(c,"msg_update",m); } }
        free(body); }
    else if(!strcmp(k,"react")){ char em[40]; lsm_msg *m=msg_get(c,target); xj_get(js,"emoji",em,sizeof em); utf8_trunc_chars(em,8);
        if(m && !strcmp(m->conv,conv)){ react_set(m->reactions,sizeof m->reactions,fp,em); hist_rec_ext(c,m); emit_msg(c,"msg_update",m); } }
    else if(!strcmp(k,"edit")||!strcmp(k,"recall")){ lsm_msg *m=msg_get(c,target);
        if(m && !m->out && !strcmp(m->conv,conv) && !strcmp(m->sender_fp,fp)){
            if(k[0]=='r'){ m->recalled=1; free(m->body); m->body=xstrdup(""); }
            else if(!m->recalled){ size_t bl=strlen(js)+1; char *body=(char*)xmalloc(bl); xj_get(js,"body",body,bl); free(m->body); m->body=body; m->edited=1; }
            hist_rec_ext(c,m); emit_msg(c,"msg_update",m); }
        else logf_(c,"ext %s from %s for a message it did not send: ignored",k,fp); }
    free(js); }

int lsm_send_text2(lsm_core *c, const char *conv, const char *text, const char *reply_to, const char *mentions_csv, char *msgid_out, char *err){
    uint8_t id[16]; char ids[37], q[400]="", *fb; buf_t j={0}; lsm_msg *m, *t; int online;
    if((!reply_to||!*reply_to) && (!mentions_csv||!*mentions_csv)) return lsm_send_text(c,conv,text,msgid_out,err);
    if(!text[0]){ E("empty"); return -1; }
    mu_lock(&c->mu); t=reply_to&&*reply_to?msg_get(c,reply_to):NULL; if(t && strcmp(t->conv,conv)) t=NULL; if(t) quote_of(c,t->id,q,sizeof q);
    uuid_new(id); lsm_uuid_str(id,ids);
    bfmt(&j,"{\"k\":\"text\",\"body\":"); xj_put(&j,text); bfmt(&j,",\"reply_to\":"); xj_put(&j,t?t->id:""); bfmt(&j,",\"quote\":"); xj_put(&j,q); bfmt(&j,",\"mentions\":[");
    { const char *p=mentions_csv?mentions_csv:""; int first=1; while(*p){ const char *e=strchr(p,','); char one[FP_LEN]; size_t l=e?(size_t)(e-p):strlen(p); if(l && l<sizeof one){ memcpy(one,p,l); one[l]=0; if(!first) b_u8(&j,','); xj_put(&j,one); first=0; } p=e?e+1:p+l; } }
    bfmt(&j,"]}");
    fb=(char*)xmalloc(strlen(text)+strlen(q)+32); if(q[0]) sprintf(fb,"\xe3\x80\x8c%s\xe3\x80\x8d\n\xe2\x80\x94 %s",q,text); else strcpy(fb,text);
    m=add_message(c,ids,conv,1,c->fp,K_TEXT,text,wall_ms(),ST_QUEUED); scpy(m->reply_to,t?t->id:"",40); scpy(m->quote,q,sizeof m->quote); scpy(m->mentions,mentions_csv?mentions_csv:"",sizeof m->mentions); hist_rec_ext(c,m);
    online=send_ext(c,conv,&j,fb,id,ids); if(online) set_status(c,msg_get(c,ids),ST_SENT);
    if(msgid_out) strcpy(msgid_out,ids); free(fb); b_free(&j); mu_unlock(&c->mu); return 0; }

int lsm_react(lsm_core *c, const char *id, const char *emoji, char *err){ lsm_msg *m; char cur[40], em[40]; buf_t j={0}; uint8_t nid[16];
    mu_lock(&c->mu); m=msg_get(c,id); if(!m){ mu_unlock(&c->mu); E("\xe6\xb6\x88\xe6\x81\xaf\xe4\xb8\x8d\xe5\xad\x98\xe5\x9c\xa8"); return -1; }
    scpy(em,emoji?emoji:"",sizeof em); utf8_trunc_chars(em,8); if(react_get(m->reactions,c->fp,cur,sizeof cur) && !strcmp(cur,em)) em[0]=0; /* toggle */
    react_set(m->reactions,sizeof m->reactions,c->fp,em); hist_rec_ext(c,m); emit_msg(c,"msg_update",m);
    bfmt(&j,"{\"k\":\"react\",\"target\":"); xj_put(&j,id); bfmt(&j,",\"emoji\":"); xj_put(&j,em); b_u8(&j,'}'); uuid_new(nid); send_ext(c,m->conv,&j,NULL,nid,NULL); b_free(&j); mu_unlock(&c->mu); return 0; }

static int edit_or_recall(lsm_core *c, const char *id, const char *body, int recall, char *err){ lsm_msg *m; buf_t j={0}; uint8_t nid[16]; char *fb;
    mu_lock(&c->mu); m=msg_get(c,id);
    if(!m){ mu_unlock(&c->mu); E("\xe6\xb6\x88\xe6\x81\xaf\xe4\xb8\x8d\xe5\xad\x98\xe5\x9c\xa8"); return -1; }
    if(!m->out||m->kind==K_SYSTEM){ mu_unlock(&c->mu); E("\xe5\x8f\xaa\xe8\x83\xbd\xe4\xbf\xae\xe6\x94\xb9\xe8\x87\xaa\xe5\xb7\xb1\xe5\x8f\x91\xe9\x80\x81\xe7\x9a\x84\xe6\xb6\x88\xe6\x81\xaf"); return -1; }
    if(wall_ms()-m->ts_ms>EDIT_WINDOW_MS){ mu_unlock(&c->mu); E("\xe8\xb6\x85\xe8\xbf\x87 24 \xe5\xb0\x8f\xe6\x97\xb6\xe7\x9a\x84\xe6\xb6\x88\xe6\x81\xaf\xe4\xb8\x8d\xe8\x83\xbd\xe5\x86\x8d\xe7\xbc\x96\xe8\xbe\x91\xe6\x88\x96\xe6\x92\xa4\xe5\x9b\x9e"); return -1; }
    if(m->recalled){ mu_unlock(&c->mu); E("\xe6\xb6\x88\xe6\x81\xaf\xe5\xb7\xb2\xe6\x92\xa4\xe5\x9b\x9e"); return -1; }
    if(!recall && (!body||!*body||m->kind!=K_TEXT)){ mu_unlock(&c->mu); E("\xe5\x8f\xaa\xe8\x83\xbd\xe7\xbc\x96\xe8\xbe\x91\xe6\x96\x87\xe5\xad\x97\xe6\xb6\x88\xe6\x81\xaf"); return -1; }
    if(recall){ bfmt(&j,"{\"k\":\"recall\",\"target\":"); xj_put(&j,id); b_u8(&j,'}'); fb=xstrdup("\xef\xbc\x88\xe6\x92\xa4\xe5\x9b\x9e\xe4\xba\x86\xe4\xb8\x80\xe6\x9d\xa1\xe6\xb6\x88\xe6\x81\xaf\xef\xbc\x89"); m->recalled=1; free(m->body); m->body=xstrdup(""); }
    else { bfmt(&j,"{\"k\":\"edit\",\"target\":"); xj_put(&j,id); bfmt(&j,",\"body\":"); xj_put(&j,body); b_u8(&j,'}'); fb=(char*)xmalloc(strlen(body)+32); sprintf(fb,"\xef\xbc\x88\xe5\xb7\xb2\xe7\xbc\x96\xe8\xbe\x91\xef\xbc\x89%s",body); free(m->body); m->body=xstrdup(body); m->edited=1; }
    hist_rec_ext(c,m); emit_msg(c,"msg_update",m); uuid_new(nid); send_ext(c,m->conv,&j,fb,nid,NULL); free(fb); b_free(&j); mu_unlock(&c->mu); return 0; }
int lsm_edit_message(lsm_core *c, const char *id, const char *body, char *err){ return edit_or_recall(c,id,body,0,err); }
int lsm_recall_message(lsm_core *c, const char *id, char *err){ return edit_or_recall(c,id,NULL,1,err); }
