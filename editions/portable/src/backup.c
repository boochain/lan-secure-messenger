/* backup.c - encrypted backup compatible with the Rust edition (lan-core/src/backup.rs, v2).
 * File: "LSMBAK02" | salt(16) | nonce(12) | ChaCha20-Poly1305-IETF(JSON payload) | tag(16)
 * key = Argon2id(password, salt), m=19456 KiB, t=2, p=1, v1.3 (the Rust argon2 crate defaults).
 * Payload (serde): version, identity_json, contacts_json, settings_json, history_key_hex, groups_key_hex, groups_file_hex.
 * C carries identity, contacts (name/trust/ed25519/addr/last_seen/muted), manual addresses and display name.
 * Groups / history use edition-specific formats and are not converted (null on export, ignored on import).
 * #included from core.c. */
#define BAK_MAGIC "LSMBAK02"
#define BS(b,lit) b_put((b),(lit),sizeof(lit)-1)

static int bak_kdf(const char *pw, const uint8_t salt[16], uint8_t key[32]){
    crypto_argon2_config cf; crypto_argon2_inputs in; void *wa;
    cf.algorithm=CRYPTO_ARGON2_ID; cf.nb_blocks=19456; cf.nb_passes=2; cf.nb_lanes=1;
    in.pass=(const uint8_t*)pw; in.pass_size=(uint32_t)strlen(pw); in.salt=salt; in.salt_size=16;
    wa=malloc((size_t)cf.nb_blocks*1024); if(!wa) return -1;
    crypto_argon2(key,32,wa,cf,in,crypto_argon2_no_extras); free(wa); return 0; }

/* ---- tiny JSON writer / reader (enough for the serde payload) */
static void js_str(buf_t *b, const char *s){ b_u8(b,'"'); for(;*s;s++){ unsigned char ch=(unsigned char)*s;
        if(ch=='"'||ch=='\\'){ b_u8(b,'\\'); b_u8(b,ch); } else if(ch=='\n') b_put(b,"\\n",2); else if(ch=='\r') b_put(b,"\\r",2); else if(ch=='\t') b_put(b,"\\t",2);
        else if(ch<0x20){ char t[8]; snprintf(t,sizeof t,"\\u%04x",ch); b_put(b,t,6); } else b_u8(b,ch); } b_u8(b,'"'); }
static const char *js_ws(const char *p){ while(*p==' '||*p=='\n'||*p=='\r'||*p=='\t') p++; return p; }
static const char *js_skip(const char *p){ int d=0; p=js_ws(p);
    if(*p=='"'){ for(p++;*p&&*p!='"';p++) if(*p=='\\'&&p[1]) p++; return *p?p+1:p; }
    if(*p=='{'||*p=='['){ do{ if(*p=='"'){ p=js_skip(p); continue; } if(*p=='{'||*p=='[') d++; else if(*p=='}'||*p==']') d--; p++; } while(*p&&d>0); return p; }
    while(*p&&*p!=','&&*p!='}'&&*p!=']'&&*p!=' '&&*p!='\n') p++; return p; }
/* decode a JSON string at p into a malloc'd UTF-8 string; NULL if not a string */
static char *js_dstr(const char *p){ buf_t b={0}; p=js_ws(p); if(*p!='"') return NULL;
    for(p++;*p&&*p!='"';p++){ if(*p!='\\'){ b_u8(&b,(uint8_t)*p); continue; } p++;
        switch(*p){ case 'n': b_u8(&b,'\n'); break; case 'r': b_u8(&b,'\r'); break; case 't': b_u8(&b,'\t'); break; case 'b': b_u8(&b,8); break; case 'f': b_u8(&b,12); break;
        case 'u': { unsigned u=0,i; for(i=1;i<=4&&p[i];i++){ int v=hexv(p[i]); u=u*16+(v<0?0:(unsigned)v); } p+=4;
            if(u>=0xD800&&u<0xDC00&&p[1]=='\\'&&p[2]=='u'){ unsigned l=0; for(i=3;i<=6&&p[i];i++){ int v=hexv(p[i]); l=l*16+(v<0?0:(unsigned)v); } if(l>=0xDC00&&l<0xE000){ u=0x10000+((u-0xD800)<<10)+(l-0xDC00); p+=6; } }
            if(u<0x80) b_u8(&b,(uint8_t)u); else if(u<0x800){ b_u8(&b,(uint8_t)(0xC0|u>>6)); b_u8(&b,(uint8_t)(0x80|(u&63))); }
            else if(u<0x10000){ b_u8(&b,(uint8_t)(0xE0|u>>12)); b_u8(&b,(uint8_t)(0x80|((u>>6)&63))); b_u8(&b,(uint8_t)(0x80|(u&63))); }
            else { b_u8(&b,(uint8_t)(0xF0|u>>18)); b_u8(&b,(uint8_t)(0x80|((u>>12)&63))); b_u8(&b,(uint8_t)(0x80|((u>>6)&63))); b_u8(&b,(uint8_t)(0x80|(u&63))); } break; }
        default: if(*p) b_u8(&b,(uint8_t)*p); else p--; } }
    b_u8(&b,0); return (char*)b.p; }
/* value of key k in the object starting at p (top level only), or NULL */
static const char *js_get(const char *p, const char *k){ size_t kl=strlen(k); p=js_ws(p); if(*p!='{') return NULL; p++;
    for(;;){ const char *ks; p=js_ws(p); if(*p!='"') return NULL; ks=p+1; p=js_skip(p); p=js_ws(p); if(*p!=':') return NULL; p=js_ws(p+1);
        if((size_t)(p-ks)>=kl+1 && !strncmp(ks,k,kl) && ks[kl]=='"') return p;
        p=js_ws(js_skip(p)); if(*p!=',') return NULL; p++; } }
/* iterate array elements: returns first element / next element pointer, NULL at end */
static const char *js_first(const char *p){ p=js_ws(p); if(*p!='[') return NULL; p=js_ws(p+1); return *p==']'?NULL:p; }
static const char *js_next(const char *p){ p=js_ws(js_skip(p)); if(*p!=',') return NULL; return js_ws(p+1); }
static char *js_gstr(const char *o, const char *k){ const char *v=js_get(o,k); return v?js_dstr(v):NULL; }

int lsm_backup_export(lsm_core *c, const char *pw, const char *path, char *err){
    buf_t cj={0}, pj={0}, out={0}; char hx[65], h2[65]; int i, r; uint8_t salt[16], key[32], nonce[12], mac[16], *ct;
    if(!pw || strlen(pw)<6){ snprintf(err,256,"备份密码至少 6 个字符"); return -1; }
    mu_lock(&c->mu);
    hex_enc(c->x_sk,32,hx); hex_enc(c->ed_seed,32,h2);
    { char id[200]; snprintf(id,sizeof id,"{\n  \"x25519_secret\": \"%s\",\n  \"ed25519_secret\": \"%s\"\n}",hx,h2); BS(&pj,"{\"version\":2,\"identity_json\":"); js_str(&pj,id); }
    BS(&cj,"{\"contacts\":[");
    for(i=0;i<c->ncontacts;i++){ lsm_contact *k=&c->contacts[i]; if(i) b_u8(&cj,',');
        BS(&cj,"{\"fingerprint\":"); js_str(&cj,k->fp); BS(&cj,",\"name\":"); js_str(&cj,k->name);
        bfmt(&cj,",\"trust\":\"%s\"",k->trust==TRUST_TRUSTED?"trusted":k->trust==TRUST_BLOCKED?"blocked":"unknown");
        if(k->has_ed) hex_enc(k->ed,32,hx); else hx[0]=0; bfmt(&cj,",\"ed25519_pk\":\"%s\",\"last_addrs\":[",hx);
        if(k->addr[0]) js_str(&cj,k->addr); bfmt(&cj,"],\"last_seen_ms\":%lld,\"muted\":%s}",(long long)k->last_seen_ms,k->muted?"true":"false"); }
    BS(&cj,"],\"manual\":["); for(i=0;i<c->nmanual;i++){ if(i) b_u8(&cj,','); js_str(&cj,c->manual[i].addr); } BS(&cj,"],\"muted_groups\":[]}"); b_u8(&cj,0);
    BS(&pj,",\"contacts_json\":"); js_str(&pj,(char*)cj.p);
    { buf_t sj={0}; BS(&sj,"{\"display_name\":"); js_str(&sj,c->name); BS(&sj,"}"); BS(&pj,",\"settings_json\":"); js_str(&pj,(char*)sj.p); b_free(&sj); }
    mu_unlock(&c->mu);
    BS(&pj,",\"history_key_hex\":null,\"groups_key_hex\":null,\"groups_file_hex\":null}");
    rand_bytes(salt,16); rand_bytes(nonce,12);
    if(bak_kdf(pw,salt,key)){ b_free(&cj); b_free(&pj); snprintf(err,256,"out of memory"); return -1; }
    ct=(uint8_t*)xmalloc(pj.n+1);
    { crypto_aead_ctx ax; crypto_aead_init_ietf(&ax,key,nonce); crypto_aead_write(&ax,ct,mac,NULL,0,pj.p,pj.n); crypto_wipe(&ax,sizeof ax); }
    b_put(&out,BAK_MAGIC,8); b_put(&out,salt,16); b_put(&out,nonce,12); b_put(&out,ct,pj.n); b_put(&out,mac,16);
    r=write_atomic(path,out.p,out.n); if(r) snprintf(err,256,"无法写入 %s",path);
    crypto_wipe(key,32); crypto_wipe(pj.p,pj.n); crypto_wipe(cj.p,cj.n); free(ct); b_free(&cj); b_free(&pj); b_free(&out); return r?-1:0; }

int lsm_backup_import(const char *data_dir, const char *pw, const char *path, char *err, char *fp_out){
    size_t n; uint8_t *d=read_file(path,&n), key[32], *pl; char *idj=NULL, *cjs=NULL, *sjs=NULL, *xs=NULL, *es=NULL, p[600]; int rc=-1;
    uint8_t x_sk[32], ed_seed[32], skey[32];
    if(!d){ snprintf(err,256,"无法读取 %s",path); return -1; }
    if(n<8+16+12+16 || memcmp(d,BAK_MAGIC,8)){ snprintf(err,256,!memcmp(d,"LSMBAK01",n>=8?8:n)?"旧版 (v1) 备份请用 v0.5 导入":"不是 LAN Secure Messenger 备份文件"); free(d); return -1; }
    if(bak_kdf(pw?pw:"",d+8,key)){ free(d); snprintf(err,256,"out of memory"); return -1; }
    pl=(uint8_t*)xmalloc(n-52+1);
    { crypto_aead_ctx ax; int bad; crypto_aead_init_ietf(&ax,key,d+24); bad=crypto_aead_read(&ax,pl,d+n-16,NULL,0,d+36,n-52); crypto_wipe(&ax,sizeof ax); crypto_wipe(key,32);
      if(bad){ free(d); free(pl); snprintf(err,256,"密码错误或备份文件已损坏"); return -1; } }
    pl[n-52]=0; free(d);
    idj=js_gstr((char*)pl,"identity_json"); cjs=js_gstr((char*)pl,"contacts_json"); sjs=js_gstr((char*)pl,"settings_json");
    { const char *v=js_get((char*)pl,"version"); if(!v || atoi(v)<2){ snprintf(err,256,"旧版 (v1) 备份请用 v0.5 导入"); goto out; } }
    if(!idj || !(xs=js_gstr(idj,"x25519_secret")) || !(es=js_gstr(idj,"ed25519_secret")) || strlen(xs)!=64 || strlen(es)!=64 || hex_dec(xs,x_sk,32) || hex_dec(es,ed_seed,32)){ snprintf(err,256,"备份中的身份数据无效"); goto out; }
    umkdir(data_dir);
    { char t[131]; hex_enc(x_sk,32,t); t[64]='\n'; hex_enc(ed_seed,32,t+65); t[129]='\n'; path_join(p,sizeof p,data_dir,"identity.txt"); if(write_atomic(p,t,130)){ snprintf(err,256,"无法写入 %s",p); crypto_wipe(t,sizeof t); goto out; } crypto_wipe(t,sizeof t); }
    path_join(p,sizeof p,data_dir,"state.key"); { size_t kn; uint8_t *k=read_file(p,&kn); if(k&&kn==32) memcpy(skey,k,32); else { rand_bytes(skey,32); write_atomic(p,skey,32); } free(k); }
    { buf_t cb={0}, mb={0}; const char *a, *e; size_t sn; uint8_t *s;
      for(e=cjs?js_first(js_get(cjs,"contacts")?js_get(cjs,"contacts"):"[]"):NULL; e; e=js_next(e)){
          char *fp=js_gstr(e,"fingerprint"), *nm=js_gstr(e,"name"), *tr=js_gstr(e,"trust"), *ed=js_gstr(e,"ed25519_pk"), *ad=NULL; const char *ls=js_get(e,"last_seen_ms"), *mu=js_get(e,"muted");
          if((a=js_get(e,"last_addrs")) && (a=js_first(a))) ad=js_dstr(a);
          if(fp && strlen(fp)<FP_LEN){ bfmt(&cb,"%s\t",fp); esc_put(&cb,nm?nm:""); bfmt(&cb,"\t%s\t%d\t%s\t%lld\t%d\n",ed&&strlen(ed)==64?ed:"",tr&&!strcmp(tr,"trusted")?TRUST_TRUSTED:tr&&!strcmp(tr,"blocked")?TRUST_BLOCKED:TRUST_UNKNOWN,
              ad&&strlen(ad)<64?ad:"",ls?atoll(ls):0LL,mu&&!strncmp(js_ws(mu),"true",4)); }
          free(fp); free(nm); free(tr); free(ed); free(ad); }
      for(e=cjs&&js_get(cjs,"manual")?js_first(js_get(cjs,"manual")):NULL; e; e=js_next(e)){ char *m=js_dstr(e); if(m&&*m&&!strchr(m,'\n')) bfmt(&mb,"%s\n",m); free(m); }
      s=seal_rand(skey,cb.p?cb.p:(const uint8_t*)"",cb.n,&sn); path_join(p,sizeof p,data_dir,"contacts.dat"); write_atomic(p,s,sn); free(s);
      s=seal_rand(skey,mb.p?mb.p:(const uint8_t*)"",mb.n,&sn); path_join(p,sizeof p,data_dir,"manual.dat"); write_atomic(p,s,sn); free(s); b_free(&cb); b_free(&mb); }
    if(sjs){ char *nm=js_gstr(sjs,"display_name"); if(nm&&*nm){ char t[300]; sanitize_name(nm,t,sizeof t); path_join(p,sizeof p,data_dir,"name.txt"); write_atomic(p,t,strlen(t)); } free(nm); }
    if(fp_out){ uint8_t pk[32]; crypto_x25519_public_key(pk,x_sk); fp_from_x(pk,fp_out); }
    rc=0;
out:
    crypto_wipe(x_sk,32); crypto_wipe(ed_seed,32); crypto_wipe(skey,32);
    if(xs) crypto_wipe(xs,strlen(xs)); if(es) crypto_wipe(es,strlen(es)); if(idj) crypto_wipe(idj,strlen(idj)); crypto_wipe(pl,n-52);
    free(idj); free(cjs); free(sjs); free(xs); free(es); free(pl); return rc; }
