/* v0.6.1: 个人主页 (profile sync, CAP_PROFILE / frame 17) + 消息历史查询 — same semantics as lan-core
 * (wire.rs Frame::Profile, history.rs HistoryQuery). Included from core.c. */
#define MAX_AVATAR (48*1024)
#define MAX_SIGNATURE 120

static void avatar_file(lsm_core *c, const char *fp, char *out, size_t n){ char d[600];
    if(!fp){ path_join(out,n,c->data_dir,"avatar.img"); return; }
    path_join(d,sizeof d,c->data_dir,"avatars"); umkdir(d); { char f[64]; snprintf(f,sizeof f,"%s.img",fp); path_join(out,n,d,f); } }

static void utf8_trunc_chars(char *s, int maxc){ int n=0; unsigned char *p=(unsigned char*)s; while(*p){ if((*p&0xC0)!=0x80){ if(n==maxc){ *p=0; return; } n++; } p++; } }

static void profile_frame(lsm_core *c, buf_t *f){ char ap[600]; size_t an=0; uint8_t *av; avatar_file(c,NULL,ap,sizeof ap); av=read_file(ap,&an);
    if(av && an>MAX_AVATAR){ free(av); av=NULL; an=0; }
    b_u32(f,17); b_str(f,c->signature); b_bytes(f,av?av:(const uint8_t*)"",av?an:0); free(av); }

static void profile_send(lsm_core *c, conn *k){ buf_t f={0}; if(!(k->rcaps&CAP_PROFILE)) return; profile_frame(c,&f); send_frame(k,&f); b_free(&f); }

static void profile_recv(lsm_core *c, conn *k, rd_t *r){ char *sig=r_str(r); size_t an=0; const uint8_t *av=r_bytes(r,&an); lsm_contact *ct; char ap[600];
    if(r->err){ free(sig); return; }
    ct=contact_entry(c,k->fp); sanitize_name(sig,ct->signature,sizeof ct->signature); utf8_trunc_chars(ct->signature,MAX_SIGNATURE); free(sig);
    avatar_file(c,k->fp,ap,sizeof ap);
    if(an && an<=MAX_AVATAR){ write_atomic(ap,(const char*)av,an); ct->has_avatar=1; } else { remove(ap); ct->has_avatar=0; }
    save_contacts(c); emit(c,"peer\t%s\tprofile",k->fp); }

int lsm_set_profile(lsm_core *c, const char *signature, const char *avatar_path, char *err){ conn *k; char ap[600]; int rc=0;
    mu_lock(&c->mu);
    if(signature){ char p[600]; sanitize_name(signature,c->signature,sizeof c->signature); utf8_trunc_chars(c->signature,MAX_SIGNATURE); path_join(p,sizeof p,c->data_dir,"signature.txt"); write_atomic(p,c->signature,strlen(c->signature)); }
    if(avatar_path){ avatar_file(c,NULL,ap,sizeof ap);
        if(!*avatar_path) remove(ap);
        else { size_t n=0; uint8_t *d=read_file(avatar_path,&n);
            if(!d){ if(err) strcpy(err,"\xe6\x97\xa0\xe6\xb3\x95\xe8\xaf\xbb\xe5\x8f\x96\xe5\xa4\xb4\xe5\x83\x8f"); rc=-1; } /* 无法读取头像 */
            else if(n>MAX_AVATAR){ if(err) strcpy(err,"\xe5\xa4\xb4\xe5\x83\x8f\xe9\x9c\x80\xe5\xb0\x8f\xe4\xba\x8e 48 KB"); rc=-1; } /* 头像需小于 48 KB */
            else write_atomic(ap,(const char*)d,n);
            free(d); } }
    if(!rc) for(k=c->conns;k;k=k->next) if(k->state==4) profile_send(c,k);
    if(!rc) emit(c,"me\tprofile");
    mu_unlock(&c->mu); return rc; }

const char *lsm_my_signature(lsm_core *c){ return c->signature; }
int lsm_avatar_path(lsm_core *c, const char *fp, char *out, size_t n){ FILE *f; avatar_file(c,fp,out,n); f=ufopen(out,"rb"); if(!f){ out[0]=0; return 0; } fclose(f); return 1; }
int lsm_set_remark(lsm_core *c, const char *fp, const char *remark){ lsm_contact *k; mu_lock(&c->mu); k=contact_entry(c,fp); sanitize_name(remark?remark:"",k->remark,sizeof k->remark); utf8_trunc_chars(k->remark,64); save_contacts(c); emit(c,"peer\t%s\tremark\t%s",fp,k->remark); mu_unlock(&c->mu); return 0; }
const char *lsm_display_name(const lsm_contact *k){ return k->remark[0]?k->remark:k->name; }

static void load_profile(lsm_core *c){ char p[600]; size_t n; uint8_t *d; path_join(p,sizeof p,c->data_dir,"signature.txt"); d=read_file(p,&n); if(d){ scpy(c->signature,(char*)d,sizeof c->signature); free(d); } }

/* ---- history query ---- */
static const char *STICKERS_[]={"thumb","heart","fire","star","party","check",NULL};
int lsm_is_sticker_name(const char *name){ const char *b=name, *s; char l[300]; size_t i; int j;
    for(s=name;*s;s++) if(*s=='/'||*s=='\\') b=s+1;
    for(i=0;b[i] && i<sizeof l-1;i++) l[i]=(char)tolower((unsigned char)b[i]); l[i]=0;
    if(!strncmp(l,"lsm-sticker-",12)) return 1;
    if(i>4 && !strcmp(l+i-4,".png")){ l[i-4]=0; for(j=0;STICKERS_[j];j++) if(!strcmp(l,STICKERS_[j])) return 1; }
    return 0; }
static int name_is_image(const char *n){ const char *e=strrchr(n,'.'); char l[8]; size_t i; if(!e) return 0; for(i=0;e[i]&&i<7;i++) l[i]=(char)tolower((unsigned char)e[i]); l[i]=0;
    return !strcmp(l,".png")||!strcmp(l,".jpg")||!strcmp(l,".jpeg")||!strcmp(l,".gif")||!strcmp(l,".bmp")||!strcmp(l,".webp"); }
int lsm_msg_type(const lsm_msg *m){ if(m->kind==K_SYSTEM) return -1; if(m->kind!=K_FILE) return LSM_T_TEXT;
    if(lsm_is_sticker_name(m->file_name)) return LSM_T_STICKER; return name_is_image(m->file_name)?LSM_T_IMAGE:LSM_T_FILE; }

/* ASCII-case-insensitive substring; non-ASCII bytes (CJK) compare exactly — same result as Rust to_lowercase for CJK */
static int ci_contains(const char *hay, const char *ndl){ size_t hn=strlen(hay), nn=strlen(ndl), i, j; if(!nn) return 1; if(nn>hn) return 0;
    for(i=0;i+nn<=hn;i++){ for(j=0;j<nn;j++){ unsigned char a=(unsigned char)hay[i+j], b=(unsigned char)ndl[j]; if(a<0x80) a=(unsigned char)tolower(a); if(b<0x80) b=(unsigned char)tolower(b); if(a!=b) break; } if(j==nn) return 1; }
    return 0; }

static int conv_in_list(const char *list, const char *conv){ const char *p=list; size_t cl=strlen(conv);
    while(p && *p){ const char *e=strchr(p,','); size_t l=e?(size_t)(e-p):strlen(p); if(l==cl && !strncmp(p,conv,l)) return 1; p=e?e+1:NULL; } return 0; }

int lsm_history_match(const lsm_msg *m, const lsm_hq *q){ int t=lsm_msg_type(m);
    if(t<0) return 0;
    if(q->convs && *q->convs && !conv_in_list(q->convs,m->conv)) return 0;
    if(q->from_ms>0 && m->ts_ms<q->from_ms) return 0;
    if(q->to_ms>0 && m->ts_ms>=q->to_ms) return 0;
    if(q->types && !(q->types&(1<<t))) return 0;
    if(q->direction==1 && m->out) return 0;
    if(q->direction==2 && !m->out) return 0;
    if(q->statuses && !(q->statuses&(1<<m->status))) return 0;
    if(q->query && *q->query){ if(!ci_contains(m->body?m->body:"",q->query) && !ci_contains(m->sender_name,q->query) && !ci_contains(m->file_name,q->query)) return 0; }
    return 1; }

/* Call with the core locked. Fills idx[] (indices into lsm_messages(), newest first) and returns how many; *total = all matches. */
int lsm_history_query(lsm_core *c, const lsm_hq *q, int *idx, int max, int *total){ int i, n=0, tot=0, lim=q->limit>0?q->limit:50;
    if(lim>max) lim=max;
    for(i=c->nmsgs-1;i>=0;i--){ if(!lsm_history_match(&c->msgs[i],q)) continue; if(tot>=q->offset && n<lim) idx[n++]=i; tot++; }
    if(total) *total=tot; return n; }

/* index of message id, or -1 (locked) */
int lsm_message_index(lsm_core *c, const char *id){ int i; for(i=c->nmsgs-1;i>=0;i--) if(!strcmp(c->msgs[i].id,id)) return i; return -1; }
