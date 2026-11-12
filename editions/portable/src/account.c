/* v0.7.1 account mode: recovery key, lookup id, lsm://relay + lsm://pair. See docs/account-mode.md. */
#define LSM_RK_LEN 48
#define LSM_LOOKUP_LEN 33

static void acct_save(lsm_core *c){
    buf_t b={0};
    bfmt(&b,"%s\n%s\n%s\n%d\n", c->account_mode[0]?c->account_mode:"device",
         c->account_lookup, c->recovery_key, c->first_run_done);
    save_sealed(c,"account.dat",&b); b_free(&b);
}
static void acct_load(lsm_core *c){
    char *t=load_sealed(c,"account.dat"), *f[5]; int n=0; char *p;
    scpy(c->account_mode,"device",sizeof c->account_mode);
    c->account_lookup[0]=c->recovery_key[0]=0; c->first_run_done=0;
    if(!t) return;
    p=t; f[n++]=p; while(*p && n<5){ if(*p=='\n'){ *p=0; f[n++]=p+1; } p++; }
    if(n>=4){
        if(!strcmp(f[0],"account")||!strcmp(f[0],"device")) scpy(c->account_mode,f[0],sizeof c->account_mode);
        scpy(c->account_lookup,f[1],sizeof c->account_lookup);
        scpy(c->recovery_key,f[2],sizeof c->recovery_key);
        c->first_run_done=atoi(f[3])!=0;
    }
    crypto_wipe(t,strlen(t)); free(t);
}

int lsm_acct_normalize_rk(const char *in, char *out, size_t n){
    const char *s=in?in:""; char hex[64]; int i=0;
    if(!strncmp(s,"RK1-",4)) s+=4; else if(!strncmp(s,"RK1",3)) s+=3;
    for(; *s && i<32; s++){
        char ch=*s;
        if(ch>='a'&&ch<='f') ch=(char)(ch-'a'+'A');
        if((ch>='0'&&ch<='9')||(ch>='A'&&ch<='F')) hex[i++]=ch;
    }
    hex[i]=0;
    if(i!=32 || n<LSM_RK_LEN) return -1;
    snprintf(out,n,"RK1-%c%c%c%c-%c%c%c%c-%c%c%c%c-%c%c%c%c-%c%c%c%c-%c%c%c%c-%c%c%c%c-%c%c%c%c",
        hex[0],hex[1],hex[2],hex[3], hex[4],hex[5],hex[6],hex[7],
        hex[8],hex[9],hex[10],hex[11], hex[12],hex[13],hex[14],hex[15],
        hex[16],hex[17],hex[18],hex[19], hex[20],hex[21],hex[22],hex[23],
        hex[24],hex[25],hex[26],hex[27], hex[28],hex[29],hex[30],hex[31]);
    return 0;
}
void lsm_acct_generate_rk(char *out, size_t n){
    uint8_t raw[16]; char hex[33];
    rand_bytes(raw,16); hex_enc(raw,16,hex);
    { int i; for(i=0;i<32;i++) if(hex[i]>='a'&&hex[i]<='f') hex[i]=(char)(hex[i]-'a'+'A'); }
    lsm_acct_normalize_rk(hex,out,n);
}
int lsm_acct_lookup(const char *rk, char *out, size_t n){
    char norm[LSM_RK_LEN]; uint8_t h[32]; crypto_blake2b_ctx unused;
    uint8_t msg[80]; size_t ml;
    const char *dom="lsm-acct-lookup-v1";
    (void)unused;
    if(lsm_acct_normalize_rk(rk,norm,sizeof norm)) return -1;
    ml=strlen(dom); memcpy(msg,dom,ml); memcpy(msg+ml,norm,strlen(norm));
    sha256(h,msg,ml+strlen(norm));
    if(n<33) return -1;
    hex_enc(h,16,out);
    return 0;
}
int lsm_acct_parse_kind(const char *uri){
    if(!uri) return 0;
    while(*uri==' ') uri++;
    if(!strncmp(uri,"lsm://add?",10)) return 1;
    if(!strncmp(uri,"lsm://relay?",12)) return 2;
    if(!strncmp(uri,"lsm://pair?",11)) return 3;
    return 0;
}

int lsm_first_run_done(lsm_core *c){ return c->first_run_done; }
const char *lsm_account_mode(lsm_core *c){ return c->account_mode[0]?c->account_mode:"device"; }
int lsm_has_recovery_key(lsm_core *c){ return c->recovery_key[0]!=0; }
int lsm_reveal_recovery_key(lsm_core *c, char *out, size_t n){
    mu_lock(&c->mu);
    if(!c->recovery_key[0]){ mu_unlock(&c->mu); return -1; }
    scpy(out,c->recovery_key,n); mu_unlock(&c->mu); return 0;
}
static void acct_set_rk(lsm_core *c, const char *rk){
    char norm[LSM_RK_LEN], look[40];
    if(lsm_acct_normalize_rk(rk,norm,sizeof norm)) return;
    scpy(c->recovery_key,norm,sizeof c->recovery_key);
    if(!lsm_acct_lookup(norm,look,sizeof look)) scpy(c->account_lookup,look,sizeof c->account_lookup);
}

int lsm_complete_first_run(lsm_core *c, const char *name, const char *mode, const char *restore_key, char *err){
    mu_lock(&c->mu);
    if(name && name[0]){ /* name is set via lsm_set_name which takes the lock — do after */ }
    c->first_run_done=1;
    if(mode && !strcmp(mode,"account")){
        scpy(c->account_mode,"account",sizeof c->account_mode);
        if(restore_key && restore_key[0]) acct_set_rk(c, restore_key);
        else if(!c->recovery_key[0]){ char rk[LSM_RK_LEN]; lsm_acct_generate_rk(rk,sizeof rk); acct_set_rk(c,rk); }
    } else {
        scpy(c->account_mode,"device",sizeof c->account_mode);
    }
    acct_save(c);
    mu_unlock(&c->mu);
    if(name && name[0]) lsm_set_name(c,name);
    (void)err; return 0;
}
int lsm_account_setup(lsm_core *c, char *rk_out, size_t n, char *err){
    char rk[LSM_RK_LEN];
    mu_lock(&c->mu);
    if(c->recovery_key[0]) scpy(rk,c->recovery_key,sizeof rk);
    else { lsm_acct_generate_rk(rk,sizeof rk); acct_set_rk(c,rk); }
    scpy(c->account_mode,"account",sizeof c->account_mode);
    c->first_run_done=1;
    c->acct_backup_dirty=1;
    acct_save(c);
    scpy(rk_out,rk,n);
    mu_unlock(&c->mu);
    (void)err; return 0;
}
int lsm_set_account_mode(lsm_core *c, const char *mode, char *rk_out, size_t n, char *err){
    if(mode && !strcmp(mode,"account")) return lsm_account_setup(c,rk_out,n,err);
    mu_lock(&c->mu);
    scpy(c->account_mode,"device",sizeof c->account_mode);
    acct_save(c);
    if(rk_out && n) rk_out[0]=0;
    mu_unlock(&c->mu);
    (void)err; return 0;
}
int lsm_relay_uri(lsm_core *c, char *out, size_t n){
    struct relay_cl *r=c->rl;
    mu_lock(&c->mu);
    if(!r || !r->host[0]){ out[0]=0; mu_unlock(&c->mu); return -1; }
    snprintf(out,n,"lsm://relay?h=%s&p=%d",r->host,r->port>0?r->port:9443);
    mu_unlock(&c->mu); return 0;
}

int lsm_apply_uri(lsm_core *c, const char *uri, char *err){
    int k=lsm_acct_parse_kind(uri);
    if(k==1) return lsm_add_uri(c,uri,err);
    if(k==2){
        char host[256], ps[16]; int port=9443;
        ux_param(uri,"h",host,sizeof host); ux_param(uri,"p",ps,sizeof ps);
        if(ps[0]) port=atoi(ps); if(port<=0) port=9443;
        if(!host[0]){ if(err) snprintf(err,256,"链接缺少服务器地址"); return -1; }
        return lsm_relay_set(c,1,host,port,"",err);
    }
    if(k==3){
        char r[256], host[256], ts[80], ks[80]; int port=9443; char *col;
        uint8_t token[32], wrap[32];
        ux_param(uri,"r",r,sizeof r); ux_param(uri,"t",ts,sizeof ts); ux_param(uri,"k",ks,sizeof ks);
        if(!r[0] || strlen(ts)!=64 || strlen(ks)!=64){ if(err) snprintf(err,256,"配对链接无效"); return -1; }
        col=strrchr(r,':'); if(col){ *col=0; port=atoi(col+1); if(port<=0) port=9443; }
        scpy(host,r,sizeof host);
        if(hex_dec(ts,token,32) || hex_dec(ks,wrap,32)){ if(err) snprintf(err,256,"配对链接无效"); return -1; }
        if(lsm_relay_set(c,1,host,port,"",err)) return -1;
        /* Store pending pair; relay.c picks it up when online and PAIR_DATA arrives. */
        mu_lock(&c->mu);
        memcpy(c->pair_token,token,32); memcpy(c->pair_wrap,wrap,32); c->pair_pending=1;
        mu_unlock(&c->mu);
        return 0;
    }
    if(err) snprintf(err,256,"不是有效的 lsm:// 链接");
    return -1;
}

int lsm_account_pair_uri(lsm_core *c, char *uri_out, size_t n, char *err){
    uint8_t token[32], wrap[32], *sealed; size_t sn; char th[65], kh[65];
    struct relay_cl *r=c->rl;
    mu_lock(&c->mu);
    if(!c->recovery_key[0]){ mu_unlock(&c->mu); if(err) snprintf(err,256,"请先开启账号模式"); return -1; }
    if(!r || !r->enabled || !r->host[0]){ mu_unlock(&c->mu); if(err) snprintf(err,256,"请先连接中转服务器再配对新设备"); return -1; }
    rand_bytes(token,32); rand_bytes(wrap,32);
    sealed=seal_rand(wrap,(const uint8_t*)c->recovery_key,strlen(c->recovery_key),&sn);
    if(!sealed){ mu_unlock(&c->mu); if(err) snprintf(err,256,"内存不足"); return -1; }
    { uint8_t *w=(uint8_t*)malloc(8+sn); if(!w){ free(sealed); mu_unlock(&c->mu); if(err) snprintf(err,256,"内存不足"); return -1; }
      memcpy(w,"LSMPAIR1",8); memcpy(w+8,sealed,sn); free(sealed); sealed=w; sn+=8; }
    memcpy(c->pair_token,token,32); memcpy(c->pair_wrap,wrap,32);
    if(c->pair_blob) free(c->pair_blob);
    c->pair_blob=sealed; c->pair_blob_n=sn; c->pair_offer=1;
    hex_enc(token,32,th); hex_enc(wrap,32,kh);
    snprintf(uri_out,n,"lsm://pair?r=%s:%d&t=%s&k=%s",r->host,r->port>0?r->port:9443,th,kh);
    mu_unlock(&c->mu);
    (void)err; return 0;
}

int lsm_uri_from_file(const char *path, char *out, size_t n){
    size_t fn=0, i, lim; uint8_t *d; const char *s,*e; char cmd[1200], buf[800]; FILE *fp;
    if(!path||!path[0]||!out||n<8) return -1;
    out[0]=0;
    d=read_file(path,&fn);
    if(d){ lim=fn<8192?fn:8192;
        for(i=0;i+6<lim;i++) if(!memcmp(d+i,"lsm://",6)){
            s=(char*)d+i; e=s;
            /* stop at NUL, whitespace, or bytes that cannot appear in lsm:// (PNG tEXt is followed by a CRC) */
            while((size_t)(e-(char*)d)<fn){ unsigned char c=(unsigned char)*e;
                if(!c || c<=' ' || c>=0x7f) break;
                if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c==':'||c=='/'||c=='?'||c=='&'||c=='='||c=='%'||c=='.'||c=='_'||c=='-'||c=='+'||c=='~'||c=='#'||c=='@')) break;
                e++; }
            if((size_t)(e-s)<n && (size_t)(e-s)>6){ memcpy(out,s,(size_t)(e-s)); out[e-s]=0; free(d); return 0; } }
        free(d); }
    { const char *q; for(q=path;*q;q++) if(*q=='\'' || *q=='"' || *q==';' || *q=='|' || *q=='&' || *q=='$' || *q=='`' || *q=='\n') return -1; }
#ifdef _WIN32
    snprintf(cmd,sizeof cmd,"zbarimg -q --raw \"%s\" 2>NUL", path);
    fp=_popen(cmd,"r");
#else
    snprintf(cmd,sizeof cmd,"zbarimg -q --raw '%s' 2>/dev/null", path);
    fp=popen(cmd,"r");
#endif
    if(!fp) return -1;
    if(!fgets(buf,sizeof buf,fp)){
#ifdef _WIN32
        _pclose(fp);
#else
        pclose(fp);
#endif
        return -1; }
#ifdef _WIN32
    _pclose(fp);
#else
    pclose(fp);
#endif
    { char *z=buf+strlen(buf); while(z>buf && (z[-1]=='\n'||z[-1]=='\r'||z[-1]==' ')) *--z=0; }
    if(strncmp(buf,"lsm://",6)) return -1;
    scpy(out,buf,n); return 0;
}

