/* relay.c — optional relay client (LSM Relay Protocol v1, docs/relay-protocol.md in lan-secure-messenger).
 * #included by core.c (uses its static helpers). The relay only carries the same length-prefixed Noise
 * bytes a LAN TCP link carries; every feature runs unchanged on top. LAN direct is preferred.
 *
 * Relay stream layer (client convention, opaque to the server) inside every SEND/DELIVER blob:
 *   u8 kind (0 DATA, 1 OPEN, 2 CLOSE) | u32 BE session id | DATA: raw TCP-equivalent stream bytes
 * The peer with the smaller relay id opens the session (sends OPEN, then Noise msg 1). */
#include "third_party/monocypher.h"

enum { RT_HELLO=0x00, RT_AUTH=0x01, RT_AUTH_OK=0x02, RT_AUTH_FAIL=0x03, RT_SEND=0x10, RT_DELIVER=0x11, RT_SEND_FAIL=0x12,
       RT_PING=0x20, RT_PONG=0x21, RT_LIST=0x30, RT_PEER_LIST=0x40, RT_PEER_JOIN=0x41, RT_PEER_LEAVE=0x42, RT_ERROR=0x50, RT_BYE=0x7f };
enum { RS_OFF=0, RS_IDLE, RS_CONNECTING, RS_HELLO, RS_AUTH, RS_UP };
enum { RK_DATA=0, RK_OPEN=1, RK_CLOSE=2 };
#define RL_OUT_HIGH (2*1024*1024)
#define RL_CHUNK 60000

typedef struct { uint8_t id[32]; char fp[FP_LEN]; char name[300]; int64_t next_try; int fails; } rl_peer;
struct relay_cl {
    int enabled; char host[256]; int port; char pass[256];
    sock_t s; int st; buf_t in, out;
    uint8_t myid[32], key[32], key_salt[16]; char key_pass[256]; int has_key;
    uint8_t sn[32], cn[32]; uint32_t max_frame; int hb;
    int64_t next_attempt, last_rx, last_ping, started; int fails;
    rl_peer *peers; int npeers;
    char status[32], detail[256];
};

static void rl_be32(uint8_t *p, uint32_t v){ p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v; }
static uint32_t rl_rd32(const uint8_t *p){ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static void rl_frame(struct relay_cl *r, uint8_t ty, const void *a, size_t an, const void *b, size_t bn){
    uint8_t h[5]; rl_be32(h,(uint32_t)(1+an+bn)); h[4]=ty; b_put(&r->out,h,5); if(an) b_put(&r->out,a,an); if(bn) b_put(&r->out,b,bn); }
static void rl_set_status(lsm_core *c, const char *st, const char *detail){
    struct relay_cl *r=c->rl; if(!strcmp(r->status,st) && !strcmp(r->detail,detail?detail:"")) return;
    scpy(r->status,st,sizeof r->status); scpy(r->detail,detail?detail:"",sizeof r->detail);
    emit(c,"relay\t%s\t%s",r->status,r->detail); }
static void rl_blob(lsm_core *c, const uint8_t peer[32], uint8_t kind, uint32_t sid, const uint8_t *d, size_t n){
    struct relay_cl *r=c->rl; uint8_t h[32+5]; memcpy(h,peer,32); h[32]=kind; rl_be32(h+33,sid);
    if(r->st!=RS_UP) return; rl_frame(r,RT_SEND,h,37,d,n); }
static rl_peer *rl_peer_by_id(struct relay_cl *r, const uint8_t id[32]){ int i; for(i=0;i<r->npeers;i++) if(!memcmp(r->peers[i].id,id,32)) return &r->peers[i]; return NULL; }
static rl_peer *rl_peer_by_fp(struct relay_cl *r, const char *fp){ int i; for(i=0;i<r->npeers;i++) if(!strcmp(r->peers[i].fp,fp)) return &r->peers[i]; return NULL; }
static conn *rl_conn(lsm_core *c, const uint8_t peer[32], uint32_t sid){ conn *k; for(k=c->conns;k;k=k->next) if(k->state!=9 && k->via_relay && !memcmp(k->rpeer,peer,32) && (!sid||k->rsid==sid)) return k; return NULL; }
static conn *conn_direct_by_fp(lsm_core *c, const char *fp){ conn *k; for(k=c->conns;k;k=k->next) if(k->state==4 && !k->via_relay && !strcmp(k->fp,fp)) return k; return NULL; }

/* ---- settings (sealed relay.dat: enabled \n host \n port \n passphrase) ---- */
static void rl_save(lsm_core *c){ struct relay_cl *r=c->rl; buf_t b={0}; bfmt(&b,"%d\n%s\n%d\n",r->enabled,r->host,r->port); b_put(&b,r->pass,strlen(r->pass)); b_u8(&b,'\n'); save_sealed(c,"relay.dat",&b); crypto_wipe(b.p,b.n); b_free(&b); }
static void rl_load(lsm_core *c){ struct relay_cl *r=c->rl; char *t=load_sealed(c,"relay.dat"), *f[5]; int n=0; char *p;
    r->port=9443; if(!t) return; p=t; f[n++]=p; while(*p && n<5){ if(*p=='\n'){ *p=0; f[n++]=p+1; } p++; } { char *e=strchr(f[n-1],'\n'); if(e) *e=0; }
    if(n>=4){ r->enabled=atoi(f[0]); scpy(r->host,f[1],sizeof r->host); r->port=atoi(f[2])>0?atoi(f[2]):9443; scpy(r->pass,f[3],sizeof r->pass); }
    crypto_wipe(t,strlen(t)); free(t); }

static void rl_init(lsm_core *c){ struct relay_cl *r=(struct relay_cl*)calloc(1,sizeof *r); c->rl=r; r->s=BADSOCK;
    sha256(r->myid,c->x_pk,32); rl_load(c); r->st=r->enabled?RS_IDLE:RS_OFF; scpy(r->status,"off",sizeof r->status); }

static void rl_disconnect(lsm_core *c, const char *why, int64_t retry_ms){
    struct relay_cl *r=c->rl; conn *k;
    for(k=c->conns;k;k=k->next) if(k->via_relay && k->state!=9) conn_close(c,k,why,0);
    if(r->s!=BADSOCK) closesock(r->s); r->s=BADSOCK; r->in.n=0; r->out.n=0;
    free(r->peers); r->peers=NULL; r->npeers=0;
    r->st=r->enabled?RS_IDLE:RS_OFF; r->next_attempt=mono_ms()+retry_ms; }

static void rl_fail(lsm_core *c, const char *status, const char *why){
    struct relay_cl *r=c->rl; int64_t d; r->fails++; d=backoff(r->fails); if(d<1000) d=1000; if(!strcmp(status,"auth_failed")) d=60000;
    logf_(c,"relay: %s (%s)",status,why); rl_disconnect(c,why,d); rl_set_status(c,r->enabled?status:"off",why); }

static void rl_connect(lsm_core *c){
    struct relay_cl *r=c->rl; struct sockaddr_in a; sock_t s; int one=1, e; char hp[300];
    snprintf(hp,sizeof hp,"%s:%d",r->host,r->port);
    if(!r->host[0] || resolve(hp,r->port,&a)){ rl_fail(c,"error","\xe6\x97\xa0\xe6\xb3\x95\xe8\xa7\xa3\xe6\x9e\x90\xe6\x9c\x8d\xe5\x8a\xa1\xe5\x99\xa8\xe5\x9c\xb0\xe5\x9d\x80"); return; } /* 无法解析服务器地址 */
    s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); if(s==BADSOCK){ rl_fail(c,"error","socket"); return; }
    set_nonblock(s); setsockopt(s,IPPROTO_TCP,TCP_NODELAY,(const char*)&one,sizeof one);
    if(connect(s,(const struct sockaddr*)&a,sizeof a)!=0){ e=SOCKERR; if(!INPROGRESS(e)){ closesock(s); rl_fail(c,"error","connect"); return; } }
    r->s=s; r->st=RS_CONNECTING; r->started=mono_ms(); r->last_rx=r->started; r->in.n=r->out.n=0;
    rl_set_status(c,"connecting",hp); }

/* derive K = Argon2id(pass, salt, m, t) — cached per (pass, salt) */
static int rl_key(struct relay_cl *r, const uint8_t salt[16], uint32_t m, uint32_t t){
    void *work; crypto_argon2_config cfg; crypto_argon2_inputs in;
    if(r->has_key && !memcmp(r->key_salt,salt,16) && !strcmp(r->key_pass,r->pass)) return 0;
    if(m<8 || m>262144 || t<1 || t>16) return -1;
    work=malloc((size_t)m*1024); if(!work) return -1;
    cfg.algorithm=CRYPTO_ARGON2_ID; cfg.nb_blocks=m; cfg.nb_passes=t; cfg.nb_lanes=1;
    in.pass=(const uint8_t*)r->pass; in.pass_size=(uint32_t)strlen(r->pass); in.salt=salt; in.salt_size=16;
    crypto_argon2(r->key,32,work,cfg,in,crypto_argon2_no_extras); free(work);
    memcpy(r->key_salt,salt,16); scpy(r->key_pass,r->pass,sizeof r->key_pass); r->has_key=1; return 0; }
static void rl_mac(uint8_t out[32], const uint8_t key[32], const char *ctx, const uint8_t n1[32], const uint8_t n2[32], const uint8_t id[32]){
    crypto_blake2b_ctx b; crypto_blake2b_keyed_init(&b,32,key,32); crypto_blake2b_update(&b,(const uint8_t*)ctx,strlen(ctx));
    crypto_blake2b_update(&b,n1,32); crypto_blake2b_update(&b,n2,32); crypto_blake2b_update(&b,id,32); crypto_blake2b_final(&b,out); }

/* info = "LSM5\t<fp>\t<name>\t<version>" */
static void rl_peer_upsert(lsm_core *c, const uint8_t id[32], const uint8_t *info, size_t il){
    struct relay_cl *r=c->rl; rl_peer *p; char tmp[700], *f[4]; int n=0; char *q;
    if(!memcmp(id,r->myid,32)) return;
    if(il>=sizeof tmp) il=sizeof tmp-1; memcpy(tmp,info,il); tmp[il]=0;
    q=tmp; f[n++]=q; while(*q && n<4){ if(*q=='\t'){ *q=0; f[n++]=q+1; } q++; }
    if(n<3 || strcmp(f[0],"LSM5") || strlen(f[1])!=FP_LEN-1) return;
    p=rl_peer_by_id(r,id);
    if(!p){ r->peers=(rl_peer*)realloc(r->peers,sizeof(rl_peer)*(r->npeers+1)); p=&r->peers[r->npeers++]; memset(p,0,sizeof *p); memcpy(p->id,id,32); }
    scpy(p->fp,f[1],FP_LEN); sanitize_name(f[2],p->name,sizeof p->name); p->next_try=0; p->fails=0;
    if(!strcmp(p->fp,c->fp)){ r->npeers--; return; }
    emit(c,"relay_peer\t%s\tjoin\t%s",p->fp,p->name); }
static void rl_peer_remove(lsm_core *c, const uint8_t id[32]){ struct relay_cl *r=c->rl; int i; conn *k;
    for(i=0;i<r->npeers;i++) if(!memcmp(r->peers[i].id,id,32)){ emit(c,"relay_peer\t%s\tleave\t",r->peers[i].fp); r->peers[i]=r->peers[--r->npeers]; break; }
    while((k=rl_conn(c,id,0))!=NULL) conn_close(c,k,"peer left relay",0); }

static void conn_parse(lsm_core *c, conn *k);
static conn *rl_new_conn(lsm_core *c, const uint8_t peer[32], uint32_t sid, int initiator){
    conn *k=(conn*)calloc(1,sizeof(conn)); struct relay_cl *r=c->rl;
    k->s=BADSOCK; k->via_relay=1; k->rsid=sid; memcpy(k->rpeer,peer,32); k->initiator=initiator;
    k->started=mono_ms(); k->last_rx=k->started; k->last_ping=k->started; k->state=2;
    noise_init(&k->hs,initiator,c->x_sk); k->next=c->conns; c->conns=k; (void)r; return k; }

static void rl_on_deliver(lsm_core *c, const uint8_t *b, size_t n){
    const uint8_t *src=b; uint8_t kind; uint32_t sid; conn *k;
    if(n<37) return; kind=b[32]; sid=rl_rd32(b+33); b+=37; n-=37;
    if(kind==RK_OPEN){ conn *o; /* a new session from that peer replaces any older one (it restarted / reconnected) */
        while((o=rl_conn(c,src,0))!=NULL) conn_close(c,o,"relay session replaced",0);
        rl_new_conn(c,src,sid,0); return; }
    k=rl_conn(c,src,sid);
    if(kind==RK_CLOSE){ if(k){ k->via_relay=2; conn_close(c,k,"closed by peer (relay)",0); } return; }
    if(!k){ rl_blob(c,src,RK_CLOSE,sid,NULL,0); return; }
    b_put(&k->in,b,n); k->last_rx=mono_ms(); conn_parse(c,k); }

static void rl_on_frame(lsm_core *c, uint8_t ty, const uint8_t *b, size_t n){
    struct relay_cl *r=c->rl;
    if(r->st==RS_HELLO){
        uint8_t mac[32], hdr[99]; char info[400]; size_t il;
        if(ty==RT_ERROR||ty==RT_AUTH_FAIL){ rl_fail(c,"error",n>1?"server refused":"server refused"); return; }
        if(ty!=RT_HELLO || n<61 || memcmp(b,"LSMR",4) || b[4]!=1){ rl_fail(c,"error","\xe4\xb8\x8d\xe6\x98\xaf\xe4\xb8\xad\xe8\xbd\xac\xe6\x9c\x8d\xe5\x8a\xa1\xe5\x99\xa8"); return; } /* 不是 LSM 中转服务器 */
        if(rl_key(r,b+5,rl_rd32(b+21),rl_rd32(b+25))){ rl_fail(c,"error","bad KDF parameters"); return; }
        memcpy(r->sn,b+29,32); rand_bytes(r->cn,32);
        rl_mac(mac,r->key,"LSMR-v1-client",r->sn,r->cn,r->myid);
        il=(size_t)snprintf(info,sizeof info,"LSM5\t%s\t%s\t%s",c->fp,c->name,LSM_APP_VERSION); if(il>=sizeof info) il=sizeof info-1;
        hdr[0]=1; memcpy(hdr+1,r->cn,32); memcpy(hdr+33,r->myid,32); memcpy(hdr+65,mac,32); hdr[97]=(uint8_t)(il>>8); hdr[98]=(uint8_t)il;
        rl_frame(r,RT_AUTH,hdr,99,info,il); r->st=RS_AUTH; return; }
    if(r->st==RS_AUTH){
        uint8_t mac[32];
        if(ty==RT_AUTH_FAIL){ rl_fail(c,n&&b[0]==1?"auth_failed":"error",n&&b[0]==1?"\xe7\xbd\x91\xe7\xbb\x9c\xe5\x8f\xa3\xe4\xbb\xa4\xe9\x94\x99\xe8\xaf\xaf":"\xe6\x9c\x8d\xe5\x8a\xa1\xe5\x99\xa8\xe6\x8b\x92\xe7\xbb\x9d"); return; } /* 网络口令错误 / 服务器拒绝 */
        if(ty!=RT_AUTH_OK || n<38){ rl_fail(c,"error","unexpected reply"); return; }
        rl_mac(mac,r->key,"LSMR-v1-server",r->cn,r->sn,r->myid);
        if(crypto_verify32(mac,b)){ rl_fail(c,"auth_failed","\xe6\x9c\x8d\xe5\x8a\xa1\xe5\x99\xa8\xe6\x9c\xaa\xe9\x80\x9a\xe8\xbf\x87\xe5\x8f\xa3\xe4\xbb\xa4\xe9\xaa\x8c\xe8\xaf\x81"); return; } /* 服务器未通过口令验证 */
        r->max_frame=rl_rd32(b+32); r->hb=(b[36]<<8)|b[37]; if(r->hb<5) r->hb=5; if(r->max_frame<4096) r->max_frame=4096;
        r->st=RS_UP; r->fails=0; r->last_ping=mono_ms();
        { char d[300]; snprintf(d,sizeof d,"%s:%d",r->host,r->port); rl_set_status(c,"online",d); }
        logf_(c,"relay: online %s:%d",r->host,r->port); return; }
    switch(ty){
    case RT_PEER_LIST: { size_t p=2, cnt, i; uint8_t keep[256][32]; int nk=0, j;
        if(n<2) return; cnt=(b[0]<<8)|b[1];
        for(i=0;i<cnt && p+34<=n;i++){ size_t il=(b[p+32]<<8)|b[p+33]; if(p+34+il>n) break; rl_peer_upsert(c,b+p,b+p+34,il); if(nk<256) memcpy(keep[nk++],b+p,32); p+=34+il; }
        for(j=r->npeers-1;j>=0;j--){ int k2, f=0; for(k2=0;k2<nk;k2++) if(!memcmp(keep[k2],r->peers[j].id,32)) f=1; if(!f && nk<256) rl_peer_remove(c,r->peers[j].id); }
        break; }
    case RT_PEER_JOIN: if(n>=34){ size_t il=(b[32]<<8)|b[33]; if(34+il<=n) rl_peer_upsert(c,b,b+34,il); } break;
    case RT_PEER_LEAVE: if(n>=32) rl_peer_remove(c,b); break;
    case RT_DELIVER: rl_on_deliver(c,b,n); break;
    case RT_SEND_FAIL: if(n>=33){ conn *k; while((k=rl_conn(c,b,0))!=NULL){ k->via_relay=2; conn_close(c,k,b[32]==10?"peer offline (relay)":"peer busy (relay)",0); } } break;
    case RT_PING: rl_frame(r,RT_PONG,b,n,NULL,0); break;
    case RT_ERROR: rl_fail(c,"error",n&&b[0]==14?"\xe6\x9c\x8d\xe5\x8a\xa1\xe5\x99\xa8\xe5\x85\xb3\xe9\x97\xad":"server error"); break; /* 服务器关闭 */
    default: break; } }

static void rl_readable(lsm_core *c){
    struct relay_cl *r=c->rl; uint8_t tmp[65536]; int n;
    for(;;){ n=recv(r->s,(char*)tmp,sizeof tmp,0);
        if(n>0){ b_put(&r->in,tmp,(size_t)n); r->last_rx=mono_ms(); if(n<(int)sizeof tmp) break; continue; }
        if(n==0){ rl_fail(c,"error","\xe6\x9c\x8d\xe5\x8a\xa1\xe5\x99\xa8\xe6\x96\xad\xe5\xbc\x80\xe8\xbf\x9e\xe6\x8e\xa5"); return; } /* 服务器断开连接 */
        if(WOULDBLOCK(SOCKERR)) break; rl_fail(c,"error","recv"); return; }
    while(r->s!=BADSOCK && r->in.n>=4){ size_t l=rl_rd32(r->in.p); uint8_t *m;
        if(l==0 || l>16u*1024*1024){ rl_fail(c,"error","bad frame"); return; }
        if(r->in.n<4+l) break;
        m=(uint8_t*)malloc(l); memcpy(m,r->in.p+4,l); b_consume(&r->in,4+l);
        rl_on_frame(c,m[0],m+1,l-1); free(m); } }

static void rl_writable(lsm_core *c){
    struct relay_cl *r=c->rl;
    if(r->st==RS_CONNECTING){ int e=0; socklen_t el=sizeof e; getsockopt(r->s,SOL_SOCKET,SO_ERROR,(char*)&e,&el); if(e){ rl_fail(c,"error","\xe6\x97\xa0\xe6\xb3\x95\xe8\xbf\x9e\xe6\x8e\xa5\xe6\x9c\x8d\xe5\x8a\xa1\xe5\x99\xa8"); return; } r->st=RS_HELLO; } /* 无法连接服务器 */
    while(r->out.n){ int n=send(r->s,(const char*)r->out.p,(int)(r->out.n>65536?65536:r->out.n),0);
        if(n>0){ b_consume(&r->out,(size_t)n); continue; }
        if(n<0 && WOULDBLOCK(SOCKERR)) break; rl_fail(c,"error","send"); return; } }

/* move each relay link's pending stream bytes into SEND frames (bounded by the relay socket backlog) */
static void rl_pump(lsm_core *c){
    struct relay_cl *r=c->rl; conn *k; size_t chunk;
    if(r->st!=RS_UP) return;
    chunk=r->max_frame>RL_CHUNK+64?RL_CHUNK:r->max_frame-64;
    for(k=c->conns;k;k=k->next){ if(!k->via_relay || k->state==9) continue;
        while(k->out.n && r->out.n<RL_OUT_HIGH){ size_t m=k->out.n<chunk?k->out.n:chunk; rl_blob(c,k->rpeer,RK_DATA,k->rsid,k->out.p,m); b_consume(&k->out,m); } } }

static void rl_tick(lsm_core *c){
    struct relay_cl *r=c->rl; int64_t now=mono_ms(); int i;
    if(!r->enabled){ if(r->s!=BADSOCK) rl_disconnect(c,"relay disabled",0); rl_set_status(c,"off",""); return; }
    if(r->st==RS_IDLE && now>=r->next_attempt){ rl_connect(c); return; }
    if(r->st>=RS_CONNECTING && r->st<RS_UP && now-r->started>15000){ rl_fail(c,"error","\xe8\xbf\x9e\xe6\x8e\xa5\xe8\xb6\x85\xe6\x97\xb6"); return; } /* 连接超时 */
    if(r->st!=RS_UP) return;
    if(now-r->last_ping>=(int64_t)r->hb*1000){ r->last_ping=now; rl_frame(r,RT_PING,"hb",2,NULL,0); }
    if(now-r->last_rx>(int64_t)r->hb*3000){ rl_fail(c,"error","\xe5\xbf\x83\xe8\xb7\xb3\xe8\xb6\x85\xe6\x97\xb6"); return; } /* 心跳超时 */
    /* open relay sessions to relay peers that have no link at all (LAN direct preferred) */
    for(i=0;i<r->npeers;i++){ rl_peer *p=&r->peers[i]; lsm_contact *ct=contact_get(c,p->fp); conn *k;
        if(ct && ct->trust==TRUST_BLOCKED) continue;
        if(conn_by_fp(c,p->fp) || rl_conn(c,p->id,0)) continue;
        if(memcmp(r->myid,p->id,32)>0) continue;          /* the smaller id opens */
        if(p->next_try && now<p->next_try) continue;
        p->next_try=now+backoff(p->fails++); if(p->next_try<now+3000) p->next_try=now+3000;
        { uint32_t sid; rand_bytes(&sid,4); if(!sid) sid=1;
          rl_blob(c,p->id,RK_OPEN,sid,NULL,0); k=rl_new_conn(c,p->id,sid,1);
          { uint8_t m[64]; int w=noise_write(&k->hs,m); raw_send(k,m,(size_t)w); } } } }

static void rl_on_link_closed(lsm_core *c, conn *k){ if(k->via_relay==1) rl_blob(c,k->rpeer,RK_CLOSE,k->rsid,NULL,0); }

/* ---- public API ---- */
int lsm_relay_set(lsm_core *c, int enabled, const char *host, int port, const char *pass, char *err){
    struct relay_cl *r;
    if(enabled && (!host||!host[0])){ scpy(err,"\xe8\xaf\xb7\xe5\xa1\xab\xe5\x86\x99\xe6\x9c\x8d\xe5\x8a\xa1\xe5\x99\xa8\xe5\x9c\xb0\xe5\x9d\x80",200); return -1; } /* 请填写服务器地址 */
    if(port<0||port>65535){ scpy(err,"\xe7\xab\xaf\xe5\x8f\xa3\xe6\x97\xa0\xe6\x95\x88",200); return -1; } /* 端口无效 */
    mu_lock(&c->mu); r=c->rl;
    rl_disconnect(c,"relay settings changed",0);
    r->enabled=enabled?1:0; if(host) scpy(r->host,host,sizeof r->host); r->port=port?port:9443; if(pass) scpy(r->pass,pass,sizeof r->pass);
    r->has_key=0; r->fails=0; r->st=r->enabled?RS_IDLE:RS_OFF; r->next_attempt=0;
    rl_save(c); rl_set_status(c,r->enabled?"connecting":"off","");
    mu_unlock(&c->mu); return 0; }
int lsm_relay_get(lsm_core *c, int *enabled, char *host, size_t hn, int *port, int *has_pass){
    struct relay_cl *r; mu_lock(&c->mu); r=c->rl;
    if(enabled) *enabled=r->enabled; if(host) scpy(host,r->host,hn); if(port) *port=r->port; if(has_pass) *has_pass=r->pass[0]!=0;
    mu_unlock(&c->mu); return 0; }
const char *lsm_relay_status(lsm_core *c, char *detail, size_t dn){ /* locked by caller or not: plain reads */
    struct relay_cl *r=c->rl; if(detail) scpy(detail,r->detail,dn); return r->status; }
int lsm_relay_peers(lsm_core *c){ return c->rl->npeers; }
/* push a closing link's last bytes (Bye) straight into the relay socket buffer */
static void rl_flush_link(lsm_core *c, conn *k){ struct relay_cl *r=c->rl; if(r->st!=RS_UP) return;
    while(k->out.n){ size_t m=k->out.n<RL_CHUNK?k->out.n:RL_CHUNK; rl_blob(c,k->rpeer,RK_DATA,k->rsid,k->out.p,m); b_consume(&k->out,m); }
    if(r->s!=BADSOCK) send(r->s,(const char*)r->out.p,(int)r->out.n,0), r->out.n=0; }
