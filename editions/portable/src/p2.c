/* v0.6.1 P2 (C core, same protocol as lan-core engine/p2.rs): pinned chats + drafts (sealed conv_prefs.bin),
 * local pinned/starred message flags, typing indicator (ephemeral Ext {"k":"typing"}), forward. Included by core.c after ext.c. */
typedef struct { char conv[64]; int pinned; char *draft; int64_t last_typing; } p2conv;
struct p2st { p2conv *v; int n; int loaded; };
static p2conv *p2_get(lsm_core *c, const char *conv, int create);
static void p2_save(lsm_core *c){ buf_t b={0}; int i; size_t sn; uint8_t *s; char p[600]; FILE *f; if(!c->p2) return;
    for(i=0;i<c->p2->n;i++){ p2conv *e=&c->p2->v[i]; if(!e->pinned && !(e->draft&&*e->draft)) continue; bfmt(&b,"%s\t%d\t",e->conv,e->pinned); esc_put(&b,e->draft?e->draft:""); b_u8(&b,'\n'); }
    snprintf(p,sizeof p,"%s/conv_prefs.bin",c->data_dir); s=seal_rand(c->store_key,b.p?b.p:(const uint8_t*)"",b.n,&sn);
    f=ufopen(p,"wb"); if(f){ fwrite(s,1,sn,f); fclose(f); } free(s); b_free(&b); }
static void p2_load(lsm_core *c){ char p[600]; FILE *f; long n; uint8_t *d; size_t pn; char *t, *line, *nx;
    if(!c->p2){ c->p2=(struct p2st*)xmalloc(sizeof *c->p2); memset(c->p2,0,sizeof *c->p2); } if(c->p2->loaded) return; c->p2->loaded=1;
    snprintf(p,sizeof p,"%s/conv_prefs.bin",c->data_dir); f=ufopen(p,"rb"); if(!f) return; fseek(f,0,SEEK_END); n=ftell(f); fseek(f,0,SEEK_SET);
    if(n<=0||n>(8<<20)){ fclose(f); return; } d=(uint8_t*)xmalloc((size_t)n); if(fread(d,1,(size_t)n,f)!=(size_t)n){ fclose(f); free(d); return; } fclose(f);
    t=(char*)open_rand(c->store_key,d,(size_t)n,&pn); free(d); if(!t) return;
    { char *z=(char*)xmalloc(pn+1); memcpy(z,t,pn); z[pn]=0; free(t); t=z; }
    for(line=t; line && *line; line=nx){ char *fl[3]; int k; p2conv *e; nx=strchr(line,'\n'); if(nx) *nx++=0; k=split_tabs(line,fl,3); if(k<3) continue;
        e=p2_get(c,fl[0],1); if(!e) continue; e->pinned=atoi(fl[1]); free(e->draft); e->draft=xstrdup(fl[2]); }
    free(t); }
static p2conv *p2_get(lsm_core *c, const char *conv, int create){ int i; p2conv *e; if(!c->p2||!c->p2->loaded){ if(!c->p2||!c->p2->loaded) p2_load(c); }
    for(i=0;i<c->p2->n;i++) if(!strcmp(c->p2->v[i].conv,conv)) return &c->p2->v[i];
    if(!create || strlen(conv)>=64) return NULL;
    c->p2->v=(p2conv*)xrealloc(c->p2->v,sizeof(p2conv)*(c->p2->n+1)); e=&c->p2->v[c->p2->n++]; memset(e,0,sizeof *e); scpy(e->conv,conv,64); return e; }

int lsm_set_conv_pinned(lsm_core *c, const char *conv, int on){ p2conv *e; mu_lock(&c->mu); e=p2_get(c,conv,1); if(e){ e->pinned=on!=0; p2_save(c); } mu_unlock(&c->mu); if(e) emit(c,"conv\t%s",conv); return e?0:-1; }
int lsm_conv_pinned(lsm_core *c, const char *conv){ p2conv *e=p2_get(c,conv,0); return e?e->pinned:0; }  /* caller holds lsm_lock (GUI list refresh) */
int lsm_set_draft(lsm_core *c, const char *conv, const char *text){ p2conv *e; int ch=0; mu_lock(&c->mu); e=p2_get(c,conv,1);
    if(e){ const char *t=text?text:""; char *nd=NULL; size_t l=strlen(t); if(l>60000) l=60000; { const char *q=t; while(*q==' '||*q=='\n'||*q=='\t'||*q=='\r') q++; if(*q){ nd=(char*)xmalloc(l+1); memcpy(nd,t,l); nd[l]=0; utf8_trunc(nd,l); } }
        ch=(e->draft?1:0)!=(nd?1:0) || (nd && strcmp(nd,e->draft)); free(e->draft); e->draft=nd; if(ch) p2_save(c); }
    mu_unlock(&c->mu); return e?0:-1; }
int lsm_get_draft(lsm_core *c, const char *conv, char *out, size_t n){ p2conv *e=p2_get(c,conv,0); if(n) out[0]=0; if(!e||!e->draft||!*e->draft) return 0; scpy(out,e->draft,n); return 1; }
int lsm_set_msg_flags(lsm_core *c, const char *id, int pinned, int starred, char *err){ lsm_msg *m; mu_lock(&c->mu); m=msg_get(c,id);
    if(!m){ mu_unlock(&c->mu); E("消息不存在"); return -1; } if(pinned>=0) m->pinned=pinned!=0; if(starred>=0) m->starred=starred!=0; hist_rec_ext(c,m); emit_msg(c,"msg_update",m); mu_unlock(&c->mu); return 0; }
int lsm_send_typing(lsm_core *c, const char *conv){ p2conv *e; int64_t now=wall_ms(); buf_t j={0}, pl={0}, f={0}; uint8_t id[16]; int i;
    mu_lock(&c->mu); e=p2_get(c,conv,1); if(!e || now-e->last_typing<3000){ mu_unlock(&c->mu); return 0; } e->last_typing=now;
    bfmt(&j,"{\"k\":\"typing\"}"); uuid_new(id);
    if(!strncmp(conv,"dm:",3)){ const char *fp=conv+3; conn *k=conn_by_fp(c,fp); if(k && (k->rcaps&CAP_EXT)){ b_u32(&pl,6); b_u8(&pl,0); b_u64(&pl,0); b_bytes(&pl,j.p,j.n); b_u64(&pl,(uint64_t)now); frame_msg(&f,id,&pl); send_to_fp(c,fp,&f); } }
    else if(!strncmp(conv,"group:",6)){ uint8_t gid[16]; lsm_group *g; size_t sn; uint8_t *sealed;
        if(!lsm_uuid_parse(conv+6,gid) && (g=group_get(c,gid))){ sealed=seal_rand(g->key,j.p,j.n,&sn); b_u32(&pl,6); b_u8(&pl,1); b_uuid(&pl,gid); b_u64(&pl,g->key_epoch); b_bytes(&pl,sealed,sn); b_u64(&pl,(uint64_t)now); free(sealed); frame_msg(&f,id,&pl);
            for(i=0;i<g->nmembers;i++){ conn *k=conn_by_fp(c,g->members[i]); if(strcmp(g->members[i],c->fp) && k && (k->rcaps&CAP_EXT)) send_to_fp(c,g->members[i],&f); } } }
    b_free(&j); b_free(&pl); b_free(&f); mu_unlock(&c->mu); return 0; }
int lsm_forward(lsm_core *c, const char *id, const char *conv, char *msgid_out, char *err){ lsm_msg *m; char path[520]="", *body=NULL; int kind, rec; uint8_t nid[16]; char ids[37], *fb; buf_t j={0}; int online;
    mu_lock(&c->mu); m=msg_get(c,id); if(!m){ mu_unlock(&c->mu); E("消息不存在"); return -1; } kind=m->kind; rec=m->recalled; if(kind==K_FILE) scpy(path,m->file_path,sizeof path); else body=xstrdup(m->body?m->body:""); mu_unlock(&c->mu);
    if(rec || kind==K_SYSTEM){ free(body); E("这条消息不能转发"); return -1; }
    if(kind==K_FILE){ { FILE *tf=path[0]?ufopen(path,"rb"):NULL; if(!tf){ E("文件不在本机，无法转发"); return -1; } fclose(tf); } return lsm_send_file(c,conv,path,err); }
    mu_lock(&c->mu);
    if(!strncmp(conv,"dm:",3)){ lsm_contact *ct=contact_get(c,conv+3); if(!ct || ct->trust==TRUST_BLOCKED || !strcmp(conv+3,c->fp)){ mu_unlock(&c->mu); free(body); E("无法转发到该联系人"); return -1; } }
    else { uint8_t gid[16]; if(strncmp(conv,"group:",6) || lsm_uuid_parse(conv+6,gid) || !group_get(c,gid)){ mu_unlock(&c->mu); free(body); E("会话不存在"); return -1; } }
    uuid_new(nid); lsm_uuid_str(nid,ids);
    bfmt(&j,"{\"k\":\"text\",\"body\":"); xj_put(&j,body); bfmt(&j,",\"fwd\":true}");
    fb=(char*)xmalloc(strlen(body)+16); sprintf(fb,"[\xe8\xbd\xac\xe5\x8f\x91] %s",body);
    m=add_message(c,ids,conv,1,c->fp,K_TEXT,body,wall_ms(),ST_QUEUED); m->forwarded=1; hist_rec_ext(c,m);
    online=send_ext(c,conv,&j,fb,nid,ids); if(online) set_status(c,msg_get(c,ids),ST_SENT);
    if(msgid_out) strcpy(msgid_out,ids); free(fb); free(body); b_free(&j); mu_unlock(&c->mu); return 0; }
/* ---- shared UI helpers (caller holds lsm_lock) ---- */
int lsm_flagged(lsm_core *c, const char *conv, int which, int *idx, int max){ int i, k=0;
    for(i=c->nmsgs-1;i>=0&&k<max;i--){ lsm_msg *m=&c->msgs[i]; if(conv&&*conv&&strcmp(m->conv,conv)) continue; if(m->recalled) continue; if((which&1)&&m->pinned) idx[k++]=i; else if((which&2)&&m->starred) idx[k++]=i; }
    return k; }
int lsm_mention_candidates(lsm_core *c, const char *conv, char names[][128], char fps[][FP_LEN], int max){ int i, k=0; uint8_t gid[16]; lsm_group *g;
    if(strncmp(conv,"group:",6)||lsm_uuid_parse(conv+6,gid)||!(g=group_get(c,gid))) return 0;
    for(i=0;i<g->nmembers&&k<max;i++){ lsm_contact *ct; if(!strcmp(g->members[i],c->fp)) continue; ct=contact_get(c,g->members[i]);
        scpy(names[k],ct?lsm_display_name(ct):g->members[i],128); scpy(fps[k],g->members[i],FP_LEN); k++; }
    return k; }
/* fingerprints of every member whose "@name" appears in text, comma separated (empty when none) */
void lsm_mentions_in(lsm_core *c, const char *conv, const char *text, char *csv, size_t n){ char nm[64][128], fp[64][FP_LEN]; int k=lsm_mention_candidates(c,conv,nm,fp,64), i; size_t o=0; if(n) csv[0]=0;
    for(i=0;i<k;i++){ char at[132]; snprintf(at,sizeof at,"@%s",nm[i]); if(!nm[i][0]||!strstr(text,at)) continue; if(o+strlen(fp[i])+2>=n) break; o+=snprintf(csv+o,n-o,"%s%s",o?",":"",fp[i]); } }
