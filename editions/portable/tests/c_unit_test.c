/* C core unit checks against the Rust reference vectors (lan-core/tests/data/[name].json).
 *   cc -std=gnu99 -Isrc -DLSM_APP_VERSION='"test"' -o build/c_unit_test tests/c_unit_test.c -lpthread
 *   build/c_unit_test ../../lan-core/tests/data/filename_vectors.json
 * Includes core.c directly to reach its static helpers. */
#include "../src/core.c"

static const char *js_str(const char *p, char *out, size_t n){ size_t o=0; if(*p!='"') return NULL; p++;
    while(*p && *p!='"'){ unsigned cp; if(*p=='\\'){ p++;
            switch(*p){ case 'n': cp='\n'; break; case 't': cp='\t'; break; case 'r': cp='\r'; break; case 'b': cp=8; break; case 'f': cp=12; break;
                case 'u': { char h[5]={p[1],p[2],p[3],p[4],0}; cp=(unsigned)strtoul(h,NULL,16); p+=4; break; }
                default: cp=(unsigned char)*p; }
            p++;
            if(cp<0x80){ if(o+1<n) out[o++]=(char)cp; }
            else if(cp<0x800){ if(o+2<n){ out[o++]=(char)(0xC0|(cp>>6)); out[o++]=(char)(0x80|(cp&63)); } }
            else { if(o+3<n){ out[o++]=(char)(0xE0|(cp>>12)); out[o++]=(char)(0x80|((cp>>6)&63)); out[o++]=(char)(0x80|(cp&63)); } }
        } else { if(o+1<n) out[o++]=*p; p++; } }
    out[o]=0; return *p=='"'?p+1:NULL; }

static int hist_tests(void){ lsm_msg m[4]; lsm_hq q; int bad=0; memset(m,0,sizeof m);
    m[0].kind=K_TEXT; m[0].body="\xe4\xbd\xa0\xe5\xa5\xbd World"; strcpy(m[0].conv,"dm:a"); m[0].ts_ms=100; m[0].out=1;
    m[1].kind=K_FILE; m[1].body=""; strcpy(m[1].file_name,"heart.png"); strcpy(m[1].conv,"dm:b"); m[1].ts_ms=200;
    m[2].kind=K_FILE; m[2].body=""; strcpy(m[2].file_name,"Photo.JPG"); strcpy(m[2].conv,"dm:a"); m[2].ts_ms=300;
    m[3].kind=K_SYSTEM; m[3].body="world"; strcpy(m[3].conv,"dm:a");
    memset(&q,0,sizeof q); q.query="\xe5\xa5\xbd"; bad+=!lsm_history_match(&m[0],&q);
    q.query="WORLD"; bad+=!lsm_history_match(&m[0],&q); bad+=lsm_history_match(&m[3],&q);
    memset(&q,0,sizeof q); q.types=1<<LSM_T_STICKER; bad+=!lsm_history_match(&m[1],&q); bad+=lsm_history_match(&m[2],&q);
    bad+=lsm_msg_type(&m[2])!=LSM_T_IMAGE;
    memset(&q,0,sizeof q); q.convs="dm:x,dm:a"; q.direction=2; bad+=!lsm_history_match(&m[0],&q); bad+=lsm_history_match(&m[2],&q);
    memset(&q,0,sizeof q); q.from_ms=100; q.to_ms=200; bad+=!lsm_history_match(&m[0],&q); bad+=lsm_history_match(&m[1],&q);
    bad+=!lsm_is_sticker_name("lsm-sticker-cat.gif");
    printf("history_query: %d failures\n",bad); return bad; }

static int ext_tests(void){ int bad=0; char r[400]="", e[40], v[200]; buf_t b={0};
    react_set(r,sizeof r,"AAAA","\xf0\x9f\x91\x8d"); react_set(r,sizeof r,"BBBB","x");
    bad+=!react_get(r,"AAAA",e,sizeof e)||strcmp(e,"\xf0\x9f\x91\x8d");
    react_set(r,sizeof r,"AAAA",""); bad+=react_get(r,"AAAA",e,sizeof e); bad+=!react_get(r,"BBBB",e,sizeof e)||strcmp(e,"x");
    b_u8(&b,'{'); xj_put(&b,"k"); b_u8(&b,':'); xj_put(&b,"text"); b_u8(&b,','); xj_put(&b,"body"); b_u8(&b,':'); xj_put(&b,"a\"b\n\xe4\xbd\xa0"); b_u8(&b,'}'); b_u8(&b,0);
    bad+=!xj_get((char*)b.p,"body",v,sizeof v)||strcmp(v,"a\"b\n\xe4\xbd\xa0"); bad+=!xj_get((char*)b.p,"k",v,sizeof v)||strcmp(v,"text"); bad+=xj_get((char*)b.p,"nope",v,sizeof v);
    b_free(&b); if(bad) printf("ext_tests: %d failures\n",bad); else printf("ext_tests OK\n"); return bad; }

static int v070_unread(lsm_core *c, const char *conv){
    lsm_msg *m; int n,i,u=0;
    lsm_lock(c); n=lsm_messages(c,&m);
    for(i=0;i<n;i++) if(m[i].unread && !strcmp(m[i].conv,conv)) u++;
    lsm_unlock(c); return u;
}
static int v070_nmsg(lsm_core *c, const char *conv){
    lsm_msg *m; int n,i,u=0;
    lsm_lock(c); n=lsm_messages(c,&m);
    for(i=0;i<n;i++) if(!strcmp(m[i].conv,conv)) u++;
    lsm_unlock(c); return u;
}
static int v070_muted(lsm_core *c, const char *fp){
    lsm_contact *k; int n,i, mu=0;
    lsm_lock(c); n=lsm_contacts(c,&k);
    for(i=0;i<n;i++) if(!strcmp(k[i].fp,fp)) mu=k[i].muted;
    lsm_unlock(c); return mu;
}
static int v070_pinned(lsm_core *c, const char *conv){
    int on; lsm_lock(c); on=lsm_conv_pinned(c,conv); lsm_unlock(c); return on;
}
static void v070_add(lsm_core *c, const char *id, const char *conv, const char *sfp, const char *body){
    lsm_lock(c); add_message(c,id,conv,0,sfp,K_TEXT,body,0,ST_RECEIVED); lsm_unlock(c);
}

/* Pin & mute persist across restart; mark read/unread updates unread badge
 * counts without touching another conversation; delete removes only that conversation. */
static int v070_conv_actions(void){
    char dir[256], err[256]="", ca[80], cb[80];
    const char *fpa="AAAA-AAAA-AAAA-AAAA", *fpb="BBBB-BBBB-BBBB-BBBB";
    lsm_opts o; lsm_core *c; int bad=0, pid;
#ifdef _WIN32
    pid=(int)GetCurrentProcessId();
    snprintf(dir,sizeof dir,".\\lsm-v070-%d",pid);
#else
    pid=(int)getpid();
    snprintf(dir,sizeof dir,"/tmp/lsm-v070-%d",pid);
    mkdir(dir,0700);
#endif
    snprintf(ca,sizeof ca,"dm:%s",fpa); snprintf(cb,sizeof cb,"dm:%s",fpb);
    memset(&o,0,sizeof o); o.data_dir=dir; o.name="V070"; o.no_discovery=1;
    o.listen_port=41000+(pid%2000); o.discovery_port=o.listen_port+1;
    c=lsm_start(&o,err,sizeof err);
    if(!c){ printf("v070_conv_actions: lsm_start failed: %s\n",err); return 1; }
    v070_add(c,"id-a1",ca,fpa,"hello A1");
    v070_add(c,"id-a2",ca,fpa,"hello A2");
    v070_add(c,"id-b1",cb,fpb,"hello B1");
    if(v070_unread(c,ca)!=2){ printf("v070: unread A want 2 got %d\n",v070_unread(c,ca)); bad++; }
    if(v070_unread(c,cb)!=1){ printf("v070: unread B want 1 got %d\n",v070_unread(c,cb)); bad++; }
    lsm_mark_read(c,ca);
    if(v070_unread(c,ca)!=0){ printf("v070: mark_read A unread=%d\n",v070_unread(c,ca)); bad++; }
    if(v070_unread(c,cb)!=1){ printf("v070: mark_read leaked to B unread=%d\n",v070_unread(c,cb)); bad++; }
    if(lsm_mark_unread(c,ca)!=0){ printf("v070: mark_unread failed\n"); bad++; }
    if(v070_unread(c,ca)<1){ printf("v070: mark_unread A unread=%d\n",v070_unread(c,ca)); bad++; }
    if(v070_unread(c,cb)!=1){ printf("v070: mark_unread leaked to B unread=%d\n",v070_unread(c,cb)); bad++; }
    if(lsm_set_conv_pinned(c,ca,1)!=0){ printf("v070: pin failed\n"); bad++; }
    if(!v070_pinned(c,ca)){ printf("v070: not pinned in-session\n"); bad++; }
    if(lsm_set_muted(c,fpa,1,err)!=0){ printf("v070: mute failed %s\n",err); bad++; }
    if(!v070_muted(c,fpa)){ printf("v070: not muted in-session\n"); bad++; }
    if(v070_muted(c,fpb)){ printf("v070: mute leaked to B\n"); bad++; }
    if(lsm_clear_conversation(c,ca)<1){ printf("v070: clear A removed nothing\n"); bad++; }
    if(v070_nmsg(c,ca)!=0){ printf("v070: A still has %d msgs after delete\n",v070_nmsg(c,ca)); bad++; }
    if(v070_nmsg(c,cb)!=1){ printf("v070: delete A also removed B (now %d)\n",v070_nmsg(c,cb)); bad++; }
    lsm_stop(c);
    err[0]=0; c=lsm_start(&o,err,sizeof err);
    if(!c){ printf("v070_conv_actions: restart failed: %s\n",err); return bad+1; }
    if(!v070_pinned(c,ca)){ printf("v070: pin did not persist across restart\n"); bad++; }
    if(!v070_muted(c,fpa)){ printf("v070: mute did not persist across restart\n"); bad++; }
    if(v070_nmsg(c,ca)!=0){ printf("v070: deleted A msgs reappeared (%d)\n",v070_nmsg(c,ca)); bad++; }
    if(v070_nmsg(c,cb)!=1){ printf("v070: B msgs lost after restart (%d)\n",v070_nmsg(c,cb)); bad++; }
    lsm_stop(c);
#ifdef _WIN32
    { char cmd[300]; snprintf(cmd,sizeof cmd,"rmdir /s /q \"%s\"",dir); system(cmd); }
#else
    { char cmd[300]; snprintf(cmd,sizeof cmd,"rm -rf '%s'",dir); system(cmd); }
#endif
    if(bad) printf("v070_conv_actions: %d failures\n",bad); else printf("v070_conv_actions OK\n");
    return bad;
}

static int v071_account(void){
    char rk[80], look[40], n2[80]; int bad=0;
    lsm_acct_generate_rk(rk,sizeof rk);
    if(strncmp(rk,"RK1-",4)) { printf("v071 bad rk %s\n",rk); bad++; }
    if(lsm_acct_normalize_rk(rk,n2,sizeof n2) || strcmp(rk,n2)) { printf("v071 normalize fail\n"); bad++; }
    if(lsm_acct_lookup(rk,look,sizeof look) || strlen(look)!=32) { printf("v071 lookup fail\n"); bad++; }
    if(lsm_acct_parse_kind("lsm://add?a=1.2.3.4:1")!=1) bad++;
    if(lsm_acct_parse_kind("lsm://relay?h=h&p=9")!=2) bad++;
    if(lsm_acct_parse_kind("lsm://pair?r=h:1&t=aa&k=bb")!=3) bad++;
    if(bad) printf("v071_account: %d failures\n",bad); else printf("v071_account OK\n");
    return bad;
}


int main(int argc, char **argv){ FILE *f; static char buf[1<<20]; size_t len; const char *p; int n=0, bad=0;
    if(argc<2){ fprintf(stderr,"usage: %s filename_vectors.json\n",argv[0]); return 2; }
    f=fopen(argv[1],"rb"); if(!f){ perror(argv[1]); return 2; } len=fread(buf,1,sizeof buf-1,f); buf[len]=0; fclose(f);
    p=buf;
    while((p=strchr(p,'['))){ char in[4096], want[4096], got[4096]; const char *q=p+1;
        while(*q==' '||*q=='\n'||*q=='\r') q++;
        if(*q!='"'){ p=q; continue; }
        q=js_str(q,in,sizeof in); if(!q) break; while(*q==','||*q==' '||*q=='\n'||*q=='\r') q++;
        q=js_str(q,want,sizeof want); if(!q) break;
        sanitize_filename(in,got,sizeof got); n++;
        if(strcmp(got,want)){ bad++; printf("MISMATCH sanitize_filename(\"%s\")\n   C:    \"%s\"\n   Rust: \"%s\"\n",in,got,want); }
        p=q; }
    printf("sanitize_filename: %d vectors, %d mismatches\n",n,bad);
    bad+=hist_tests(); bad+=ext_tests(); bad+=v070_conv_actions(); bad+=v071_account();
    return (n<10||bad)?1:0; }
