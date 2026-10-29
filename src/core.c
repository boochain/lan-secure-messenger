#include <signal.h>
/* LAN Secure Messenger XP edition — engine.
 * Single network thread running a select() loop; all state behind one lock.
 * XP-safe APIs only: Winsock 2.2 (select, inet_addr, inet_ntoa, gethostbyname), CreateThread,
 * CRITICAL_SECTION, GetTickCount (wrap-safe), GetAdaptersInfo, RtlGenRandom. */
#ifdef _WIN32
#define FD_SETSIZE 1024
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include <io.h>
#include <direct.h>
typedef SOCKET sock_t;
#define BADSOCK INVALID_SOCKET
#define closesock closesocket
#define SOCKERR WSAGetLastError()
#define WOULDBLOCK(e) ((e)==WSAEWOULDBLOCK)
#define INPROGRESS(e) ((e)==WSAEWOULDBLOCK||(e)==WSAEINPROGRESS)
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#if defined(__ANDROID__)
#include <sys/ioctl.h> /* getifaddrs() only exists from API 24; minSdk is 21 */
#else
#include <ifaddrs.h>
#endif
#include <net/if.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <sys/select.h>
typedef int sock_t;
#define BADSOCK (-1)
#define closesock close
#define SOCKERR errno
#define WOULDBLOCK(e) ((e)==EAGAIN||(e)==EWOULDBLOCK)
#define INPROGRESS(e) ((e)==EINPROGRESS)
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include "core.h"
#include "xcrypto.h"
#include "third_party/monocypher.h"
#include "third_party/monocypher-ed25519.h"

#define PROTO 5
#define CAP_RESUME 1u  /* byte-offset file resume (frame 16 FileResume) */
#define MY_CAPS CAP_RESUME
#define CHUNK (32*1024)
#define WINDOW (4ull*1024*1024)
#define PROGRESS_EVERY (256*1024)
#define PING_EVERY 5000
#define DEAD_AFTER 20000
#define STALE_TIEBREAK 12000
#define HS_TIMEOUT 10000
#define MAX_WIRE (65535+4)
#define MCAST_GROUP "239.255.42.99"

/* ------------------------------------------------------------ platform */
static void scpy_early(char *d, const char *s, size_t n){ size_t l=strlen(s); if(l>=n) l=n-1; memcpy(d,s,l); d[l]=0; }
#ifdef _WIN32
typedef CRITICAL_SECTION mutex_t;
static void mu_init(mutex_t *m){InitializeCriticalSection(m);} 
static void mu_lock(mutex_t *m){EnterCriticalSection(m);} 
static void mu_unlock(mutex_t *m){LeaveCriticalSection(m);} 
static int64_t mono_ms(void){ /* GetTickCount64 is Vista+: extend 32-bit tick ourselves */
    static DWORD last; static int64_t hi; static int init; DWORD t=GetTickCount();
    if(init && t<last) hi+=((int64_t)1<<32); init=1; last=t; return hi+t; }
static int64_t wall_ms(void){FILETIME f; ULARGE_INTEGER u; GetSystemTimeAsFileTime(&f); u.LowPart=f.dwLowDateTime; u.HighPart=f.dwHighDateTime; return (int64_t)((u.QuadPart-116444736000000000ULL)/10000);} 
static wchar_t *widen(const char *s){int n=MultiByteToWideChar(CP_UTF8,0,s,-1,NULL,0); wchar_t *w=(wchar_t*)malloc(sizeof(wchar_t)*(n+1)); MultiByteToWideChar(CP_UTF8,0,s,-1,w,n); return w;}
static FILE *ufopen(const char *p, const char *mode){wchar_t *w=widen(p), *m=widen(mode); FILE *f=_wfopen(w,m); free(w); free(m); return f;}
static int uremove(const char *p){wchar_t *w=widen(p); int r=_wremove(w); free(w); return r;}
static int urename(const char *a, const char *b){wchar_t *wa=widen(a),*wb=widen(b); int r=MoveFileExW(wa,wb,MOVEFILE_REPLACE_EXISTING)?0:-1; free(wa); free(wb); return r;}
static int uexists(const char *p){wchar_t *w=widen(p); DWORD a=GetFileAttributesW(w); free(w); return a!=INVALID_FILE_ATTRIBUTES;}
static void umkdir(const char *p){wchar_t *w=widen(p); size_t i, wl=wcslen(w); for(i=(wl>3?3:wl);w[i];i++) if(w[i]==L'\\'||w[i]==L'/'){ wchar_t c=w[i]; w[i]=0; CreateDirectoryW(w,NULL); w[i]=c; } CreateDirectoryW(w,NULL); free(w);} 
static void set_nonblock(sock_t s){u_long one=1; ioctlsocket(s,FIONBIO,&one);} 
#define PATHSEP "\\"
#else
typedef pthread_mutex_t mutex_t;
static void mu_init(mutex_t *m){pthread_mutexattr_t a; pthread_mutexattr_init(&a); pthread_mutexattr_settype(&a,PTHREAD_MUTEX_RECURSIVE); pthread_mutex_init(m,&a);} 
static void mu_lock(mutex_t *m){pthread_mutex_lock(m);} 
static void mu_unlock(mutex_t *m){pthread_mutex_unlock(m);} 
static int64_t mono_ms(void){struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static int64_t wall_ms(void){struct timeval t; gettimeofday(&t,NULL); return (int64_t)t.tv_sec*1000+t.tv_usec/1000;}
static FILE *ufopen(const char *p, const char *mode){return fopen(p,mode);} 
static int uremove(const char *p){return remove(p);} 
static int urename(const char *a, const char *b){return rename(a,b);} 
static int uexists(const char *p){struct stat st; return stat(p,&st)==0;}
static void umkdir(const char *p){char t[1024]; size_t i; scpy_early(t,p,sizeof t); for(i=1;t[i];i++) if(t[i]=='/'){ t[i]=0; mkdir(t,0700); t[i]='/'; } mkdir(t,0700);} 
static void set_nonblock(sock_t s){fcntl(s,F_SETFL,fcntl(s,F_GETFL)|O_NONBLOCK);} 
#define PATHSEP "/"
#endif

static char *xstrdup(const char *s){size_t n=strlen(s)+1; char *d=(char*)malloc(n); memcpy(d,s,n); return d;}
static void scpy(char *d, const char *s, size_t n){ if(!n) return; strncpy(d,s?s:"",n-1); d[n-1]=0; }
/* UTF-8 safe truncate: never cut a multibyte sequence */
static void utf8_trunc(char *s, size_t max){ size_t n=strlen(s); if(n<=max) return; while(max>0 && ((unsigned char)s[max]&0xC0)==0x80) max--; s[max]=0; }

/* ------------------------------------------------------------ growable buffer + bincode */
typedef struct { uint8_t *p; size_t n, cap; } buf_t;
static void b_res(buf_t *b, size_t add){ if(b->n+add>b->cap){ size_t c=b->cap?b->cap:256; while(c<b->n+add) c*=2; b->p=(uint8_t*)realloc(b->p,c); b->cap=c; } }
static void b_put(buf_t *b, const void *d, size_t n){ b_res(b,n); memcpy(b->p+b->n,d,n); b->n+=n; }
static void b_u8(buf_t *b, uint8_t v){ b_put(b,&v,1); }
static void b_u16(buf_t *b, uint16_t v){ uint8_t x[2]={(uint8_t)v,(uint8_t)(v>>8)}; b_put(b,x,2); }
static void b_u32(buf_t *b, uint32_t v){ uint8_t x[4]; int i; for(i=0;i<4;i++) x[i]=(uint8_t)(v>>(8*i)); b_put(b,x,4); }
static void b_u64(buf_t *b, uint64_t v){ uint8_t x[8]; int i; for(i=0;i<8;i++) x[i]=(uint8_t)(v>>(8*i)); b_put(b,x,8); }
static void b_bytes(buf_t *b, const void *d, size_t n){ b_u64(b,n); b_put(b,d,n); }
static void b_str(buf_t *b, const char *s){ b_bytes(b,s,strlen(s)); }
static void b_uuid(buf_t *b, const uint8_t u[16]){ b_bytes(b,u,16); }
static void b_free(buf_t *b){ free(b->p); b->p=NULL; b->n=b->cap=0; }
static void b_consume(buf_t *b, size_t n){ memmove(b->p,b->p+n,b->n-n); b->n-=n; }

typedef struct { const uint8_t *p, *e; int err; } rd_t;
static const uint8_t *r_take(rd_t *r, size_t n){ const uint8_t *q=r->p; if(r->err || (size_t)(r->e-r->p)<n){ r->err=1; return NULL; } r->p+=n; return q; }
static uint8_t r_u8(rd_t *r){ const uint8_t *q=r_take(r,1); return q?q[0]:0; }
static uint16_t r_u16(rd_t *r){ const uint8_t *q=r_take(r,2); return q?(uint16_t)(q[0]|(q[1]<<8)):0; }
static uint32_t r_u32(rd_t *r){ const uint8_t *q=r_take(r,4); uint32_t v=0; int i; if(q) for(i=3;i>=0;i--) v=(v<<8)|q[i]; return v; }
static uint64_t r_u64(rd_t *r){ const uint8_t *q=r_take(r,8); uint64_t v=0; int i; if(q) for(i=7;i>=0;i--) v=(v<<8)|q[i]; return v; }
static const uint8_t *r_bytes(rd_t *r, size_t *n){ uint64_t l; *n=0; l=r_u64(r); if(r->err||l>(1u<<20)){ r->err=1; return NULL; } *n=(size_t)l; return r_take(r,(size_t)l); }
/* returns malloc'd NUL-terminated string */
static char *r_str(rd_t *r){ size_t n; const uint8_t *q=r_bytes(r,&n); char *s; if(!q) return xstrdup(""); s=(char*)malloc(n+1); memcpy(s,q,n); s[n]=0; return s; }
static void r_uuid(rd_t *r, uint8_t u[16]){ size_t n; const uint8_t *q=r_bytes(r,&n); if(!q||n!=16){ r->err=1; memset(u,0,16); return; } memcpy(u,q,16); }
static void r_fix(rd_t *r, uint8_t *d, size_t n){ const uint8_t *q=r_take(r,n); if(q) memcpy(d,q,n); else memset(d,0,n); }

/* ------------------------------------------------------------ helpers */
void lsm_uuid_str(const uint8_t u[16], char out[37]){
    static const char *hx="0123456789abcdef"; int i,o=0;
    for(i=0;i<16;i++){ if(i==4||i==6||i==8||i==10) out[o++]='-'; out[o++]=hx[u[i]>>4]; out[o++]=hx[u[i]&15]; }
    out[o]=0;
}
static int hexv(int c){ if(c>='0'&&c<='9') return c-'0'; c=tolower(c); if(c>='a'&&c<='f') return c-'a'+10; return -1; }
int lsm_uuid_parse(const char *s, uint8_t u[16]){
    int i=0; while(*s && i<32){ int a,b; if(*s=='-'){s++;continue;} a=hexv(s[0]); if(a<0||!s[1]) return -1; b=hexv(s[1]); if(b<0) return -1; u[i/2]=(uint8_t)(a*16+b); i+=2; s+=2; }
    return (i==32 && *s==0)?0:-1;
}
static void uuid_new(uint8_t u[16]){ rand_bytes(u,16); u[6]=(u[6]&0x0f)|0x40; u[8]=(u[8]&0x3f)|0x80; }
static void hex_enc(const uint8_t *d, size_t n, char *o){ static const char *hx="0123456789abcdef"; size_t i; for(i=0;i<n;i++){o[2*i]=hx[d[i]>>4];o[2*i+1]=hx[d[i]&15];} o[2*n]=0; }
static int hex_dec(const char *s, uint8_t *d, size_t n){ size_t i; for(i=0;i<n;i++){ int a=hexv(s[2*i]),b; if(a<0) return -1; b=hexv(s[2*i+1]); if(b<0) return -1; d[i]=(uint8_t)(a*16+b);} return 0; }
static void fp_from_x(const uint8_t pk[32], char out[FP_LEN]){
    uint8_t h[32]; char hx[17]; int i,o=0; static const char *H="0123456789ABCDEF";
    sha256(h,pk,32); for(i=0;i<8;i++){hx[2*i]=H[h[i]>>4];hx[2*i+1]=H[h[i]&15];}
    for(i=0;i<16;i++){ if(i&&i%4==0) out[o++]='-'; out[o++]=hx[i]; } out[o]=0;
}
/* v0.5 sanitize_display_name: drop control chars, max 64 chars, trim, default 未命名 */
static void sanitize_name(const char *in, char *out, size_t outn){
    const unsigned char *p=(const unsigned char*)in; size_t o=0; int chars=0; char *s, *e;
    while(*p && chars<64 && o+5<outn){
        int len = (*p<0x80)?1:(*p>>5==6)?2:(*p>>4==14)?3:(*p>>3==30)?4:1; int k;
        if(*p<0x20 || *p==0x7f){ p++; continue; }
        if(len==2 && p[0]==0xC2 && p[1]>=0x80 && p[1]<0xA0){ p+=2; continue; } /* C1 controls */
        for(k=0;k<len && p[k];k++) out[o++]=(char)p[k];
        p+=k; chars++;
    }
    out[o]=0; s=out; while(*s==' '||*s=='\t') s++; e=s+strlen(s); while(e>s && (e[-1]==' '||e[-1]=='\t')) e--; *e=0;
    memmove(out,s,strlen(s)+1);
    if(!out[0]) scpy(out,"\xe6\x9c\xaa\xe5\x91\xbd\xe5\x90\x8d",outn); /* 未命名 */
}

/* ------------------------------------------------------------ state */
typedef struct { char fp[FP_LEN]; uint8_t id[16]; buf_t payload; char msg_ref[40]; } outgoing;

typedef struct conn {
    sock_t s; int state; /* 1 connecting, 2 handshake, 3 wait hello, 4 up, 9 dead */
    int initiator; noise_hs hs; uint8_t tx[32], rx[32]; uint64_t txn, rxn;
    buf_t in, out;
    char fp[FP_LEN]; uint8_t remote_ed[32];
    struct sockaddr_in addr;
    int64_t started, last_rx, last_ping;
    char dial_key[80];   /* "fp:<fp>" or "addr:<ip:port>" or "" */
    char rname[300]; char rver[64]; uint16_t rlisten;
    uint32_t rcaps; /* peer capabilities (Hello u32-LE trailer; 0 = old peer) */
    int via_relay; uint32_t rsid; uint8_t rpeer[32]; /* relay link: 1 = via relay (2 = closed by peer) */
    struct conn *next;
} conn;

typedef struct { char fp[FP_LEN]; struct sockaddr_in dial; int has_dial; int64_t last_announce, next_attempt; int failures, dialing; } peer_rt;
typedef struct { char addr[64]; struct sockaddr_in sa; char fp[FP_LEN]; int64_t next_attempt; int failures, dialing; } manual_rt;
typedef struct { uint32_t ip, bcast; sock_t s; } iface;
typedef struct { uint32_t ip; int64_t t; } reply_mark;

struct lsm_core {
    mutex_t mu;
    lsm_opts o; char data_dir[520], dl_dir[520], announce_targets[1024];
    uint8_t x_sk[32], x_pk[32], ed_seed[32], ed_sk[64], ed_pk[32]; char fp[FP_LEN];
    char name[300];
    uint8_t store_key[32];
    int hash_busy; /* resume.c hashing jobs pending: poll without sleeping */
    sock_t lsock, usock, ufallback; int lport;
    iface ifs[32]; int nifs; int64_t last_enum; uint32_t joined[32]; int njoined;
    reply_mark replies[64];
    conn *conns;
    lsm_contact *contacts; int ncontacts;
    peer_rt *rt; int nrt;
    manual_rt *manual; int nmanual;
    lsm_group *groups; int ngroups;
    lsm_msg *msgs; int nmsgs;
    lsm_transfer *xfers; int nxfers;
    outgoing *outbox; int noutbox;
    uint8_t (*seen)[16]; int nseen, seen_pos;
    char (*tomb)[40]; int ntomb;   /* ids deleted on this device: a resend must not resurrect them */
    char pin[8]; int64_t pin_time; int pin_tries; char pin_wait[8][FP_LEN];
    char **events; int ev_head, ev_tail, ev_cap;
    int64_t last_announce;
    struct relay_cl *rl;     /* optional relay client (relay.c) */
    volatile int stop;
#ifdef _WIN32
    HANDLE th;
#else
    pthread_t th;
#endif
};

void lsm_lock(lsm_core *c){ mu_lock(&c->mu); }
void lsm_unlock(lsm_core *c){ mu_unlock(&c->mu); }
const char *lsm_my_fp(lsm_core *c){ return c->fp; }
const char *lsm_my_name(lsm_core *c){ return c->name; }
int lsm_listen_port(lsm_core *c){ return c->lport; }
int lsm_contacts(lsm_core *c, lsm_contact **o){ *o=c->contacts; return c->ncontacts; }
int lsm_groups(lsm_core *c, lsm_group **o){ *o=c->groups; return c->ngroups; }
int lsm_messages(lsm_core *c, lsm_msg **o){ *o=c->msgs; return c->nmsgs; }
int lsm_transfers(lsm_core *c, lsm_transfer **o){ *o=c->xfers; return c->nxfers; }
const char *lsm_download_dir(lsm_core *c){ return c->dl_dir; }

static void emit(lsm_core *c, const char *fmt, ...){
    char tmp[4096]; va_list ap; int nx;
    va_start(ap,fmt); vsnprintf(tmp,sizeof tmp,fmt,ap); va_end(ap);
    nx=(c->ev_tail+1)%c->ev_cap;
    if(nx==c->ev_head){ free(c->events[c->ev_head]); c->ev_head=(c->ev_head+1)%c->ev_cap; }
    c->events[c->ev_tail]=xstrdup(tmp); c->ev_tail=nx;
}
int lsm_poll_event(lsm_core *c, char *buf, size_t n){
    int r=0; mu_lock(&c->mu);
    if(c->ev_head!=c->ev_tail){ scpy(buf,c->events[c->ev_head],n); free(c->events[c->ev_head]); c->ev_head=(c->ev_head+1)%c->ev_cap; r=1; }
    mu_unlock(&c->mu); return r;
}
static void logf_(lsm_core *c, const char *fmt, ...){ char tmp[1024]; va_list ap; va_start(ap,fmt); vsnprintf(tmp,sizeof tmp,fmt,ap); va_end(ap); emit(c,"log\t%s",tmp); }

/* ------------------------------------------------------------ persistence (sealed at rest) */
static void path_join(char *o, size_t n, const char *a, const char *b){ snprintf(o,n,"%s" PATHSEP "%s",a,b); }
static void esc_put(buf_t *b, const char *s){ for(;*s;s++){ if(*s=='\\') b_put(b,"\\\\",2); else if(*s=='\t') b_put(b,"\\t",2); else if(*s=='\n') b_put(b,"\\n",2); else if(*s=='\r') b_put(b,"\\r",2); else b_u8(b,(uint8_t)*s);} }
static void unesc(char *s){ char *o=s; for(;*s;s++){ if(*s=='\\'&&s[1]){ s++; *o++ = *s=='t'?'\t':*s=='n'?'\n':*s=='r'?'\r':*s; } else *o++=*s; } *o=0; }
/* split line in place by tabs */
static int split_tabs(char *line, char **f, int max){ int n=0; f[n++]=line; while(*line && n<max){ if(*line=='\t'){ *line=0; f[n++]=line+1; } line++; } { int i; for(i=0;i<n;i++) unesc(f[i]); } return n; }
static void bfmt(buf_t *b, const char *fmt, ...){ char t[1024]; va_list ap; va_start(ap,fmt); vsnprintf(t,sizeof t,fmt,ap); va_end(ap); b_put(b,t,strlen(t)); }

static uint8_t *read_file(const char *p, size_t *n){ FILE *f=ufopen(p,"rb"); uint8_t *d; long l; if(!f) return NULL; fseek(f,0,SEEK_END); l=ftell(f); fseek(f,0,SEEK_SET); d=(uint8_t*)malloc(l+1); if(fread(d,1,l,f)!=(size_t)l){fclose(f);free(d);return NULL;} fclose(f); d[l]=0; *n=(size_t)l; return d; }
static int write_atomic(const char *p, const void *d, size_t n){ char tmp[600]; FILE *f; snprintf(tmp,sizeof tmp,"%s.tmp",p); f=ufopen(tmp,"wb"); if(!f) return -1; if(fwrite(d,1,n,f)!=n){fclose(f);return -1;} fflush(f); fclose(f); return urename(tmp,p); }
static int save_sealed(lsm_core *c, const char *fname, buf_t *plain){ char p[600]; size_t sn; uint8_t *s=seal_rand(c->store_key,plain->p?plain->p:(const uint8_t*)"",plain->n,&sn); int r; path_join(p,sizeof p,c->data_dir,fname); r=write_atomic(p,s,sn); free(s); return r; }
static char *load_sealed(lsm_core *c, const char *fname){ char p[600]; size_t n,pn; uint8_t *d,*pl; path_join(p,sizeof p,c->data_dir,fname); d=read_file(p,&n); if(!d) return NULL; pl=open_rand(c->store_key,d,n,&pn); free(d); return (char*)pl; }

static lsm_contact *contact_get(lsm_core *c, const char *fp){ int i; for(i=0;i<c->ncontacts;i++) if(!strcmp(c->contacts[i].fp,fp)) return &c->contacts[i]; return NULL; }
static lsm_contact *contact_entry(lsm_core *c, const char *fp){ lsm_contact *k=contact_get(c,fp); if(k) return k; c->contacts=(lsm_contact*)realloc(c->contacts,sizeof(lsm_contact)*(c->ncontacts+1)); k=&c->contacts[c->ncontacts++]; memset(k,0,sizeof *k); scpy(k->fp,fp,FP_LEN); k->rtt_ms=-1; return k; }
static int trust_of(lsm_core *c, const char *fp){ lsm_contact *k=contact_get(c,fp); return k?k->trust:TRUST_UNKNOWN; }
static void save_contacts(lsm_core *c){ buf_t b={0}; int i; char ed[65];
    for(i=0;i<c->ncontacts;i++){ lsm_contact *k=&c->contacts[i]; if(k->has_ed) hex_enc(k->ed,32,ed); else ed[0]=0;
        bfmt(&b,"%s\t",k->fp); esc_put(&b,k->name); bfmt(&b,"\t%s\t%d\t%s\t%lld\n",ed,k->trust,k->addr,(long long)k->last_seen_ms); }
    save_sealed(c,"contacts.dat",&b); b_free(&b); }
static void load_contacts(lsm_core *c){ char *t=load_sealed(c,"contacts.dat"), *line, *nx; if(!t) return;
    for(line=t; line && *line; line=nx){ char *f[8]; int n; nx=strchr(line,'\n'); if(nx) *nx++=0; n=split_tabs(line,f,8); if(n>=6){ lsm_contact *k=contact_entry(c,f[0]); scpy(k->name,f[1],sizeof k->name); if(strlen(f[2])==64 && !hex_dec(f[2],k->ed,32)) k->has_ed=1; k->trust=atoi(f[3]); scpy(k->addr,f[4],sizeof k->addr); k->last_seen_ms=atoll(f[5]); } }
    free(t); }

static lsm_group *group_get(lsm_core *c, const uint8_t id[16]){ int i; for(i=0;i<c->ngroups;i++) if(!memcmp(c->groups[i].id,id,16)) return &c->groups[i]; return NULL; }
static int group_is_member(lsm_group *g, const char *fp){ int i; for(i=0;i<g->nmembers;i++) if(!strcmp(g->members[i],fp)) return 1; return 0; }
static int cmpstr(const void *a, const void *b){ return strcmp((const char*)a,(const char*)b); }
static void group_sort(lsm_group *g){ int i,o=0; qsort(g->members,g->nmembers,FP_LEN,cmpstr); for(i=0;i<g->nmembers;i++){ if(o && !strcmp(g->members[o-1],g->members[i])) continue; if(o!=i) memcpy(g->members[o],g->members[i],FP_LEN); o++; } g->nmembers=o; }
static void group_remove_member(lsm_group *g, const char *fp){ int i,o=0; for(i=0;i<g->nmembers;i++) if(strcmp(g->members[i],fp)){ if(o!=i) memcpy(g->members[o],g->members[i],FP_LEN); o++; } g->nmembers=o; }
static void group_delete(lsm_core *c, const uint8_t id[16]){ int i; for(i=0;i<c->ngroups;i++) if(!memcmp(c->groups[i].id,id,16)){ c->groups[i]=c->groups[--c->ngroups]; return; } }
static void save_groups(lsm_core *c){ buf_t b={0}; int i,j; char u[37], k[65];
    for(i=0;i<c->ngroups;i++){ lsm_group *g=&c->groups[i]; lsm_uuid_str(g->id,u); hex_enc(g->key,32,k);
        bfmt(&b,"%s\t",u); esc_put(&b,g->name); bfmt(&b,"\t%s\t%s\t%lld\t%llu\t",g->creator_fp,k,(long long)g->created_ms,(unsigned long long)g->key_epoch);
        for(j=0;j<g->nmembers;j++) bfmt(&b,"%s%s",j?",":"",g->members[j]); b_u8(&b,'\n'); }
    save_sealed(c,"groups.dat",&b); b_free(&b); }
static void load_groups(lsm_core *c){ char *t=load_sealed(c,"groups.dat"), *line, *nx; if(!t) return;
    for(line=t; line && *line; line=nx){ char *f[8]; int n; lsm_group g; nx=strchr(line,'\n'); if(nx) *nx++=0; n=split_tabs(line,f,8); memset(&g,0,sizeof g);
        if(n>=7 && !lsm_uuid_parse(f[0],g.id) && !hex_dec(f[3],g.key,32)){ char *m=f[6], *cm; scpy(g.name,f[1],sizeof g.name); scpy(g.creator_fp,f[2],FP_LEN); g.created_ms=atoll(f[4]); g.key_epoch=strtoull(f[5],NULL,10);
            while(m && *m && g.nmembers<64){ cm=strchr(m,','); if(cm) *cm++=0; scpy(g.members[g.nmembers++],m,FP_LEN); m=cm; }
            c->groups=(lsm_group*)realloc(c->groups,sizeof(lsm_group)*(c->ngroups+1)); c->groups[c->ngroups++]=g; } }
    free(t); }

static void save_outbox(lsm_core *c){ buf_t b={0}; int i; char u[37]; char *hx;
    for(i=0;i<c->noutbox;i++){ outgoing *o=&c->outbox[i]; lsm_uuid_str(o->id,u); hx=(char*)malloc(o->payload.n*2+1); hex_enc(o->payload.p,o->payload.n,hx);
        bfmt(&b,"%s\t%s\t%s\t",o->fp,u,o->msg_ref[0]?o->msg_ref:"-"); b_put(&b,hx,strlen(hx)); b_u8(&b,'\n'); free(hx); }
    save_sealed(c,"outbox.dat",&b); b_free(&b); }
static void load_outbox(lsm_core *c){ char *t=load_sealed(c,"outbox.dat"), *line, *nx; if(!t) return;
    for(line=t; line && *line; line=nx){ char *f[5]; int n; nx=strchr(line,'\n'); if(nx) *nx++=0; n=split_tabs(line,f,5);
        if(n>=4){ outgoing o; size_t pl=strlen(f[3])/2; memset(&o,0,sizeof o); scpy(o.fp,f[0],FP_LEN); if(lsm_uuid_parse(f[1],o.id)) continue; if(strcmp(f[2],"-")) scpy(o.msg_ref,f[2],40);
            b_res(&o.payload,pl+1); if(hex_dec(f[3],o.payload.p,pl)){ b_free(&o.payload); continue; } o.payload.n=pl;
            c->outbox=(outgoing*)realloc(c->outbox,sizeof(outgoing)*(c->noutbox+1)); c->outbox[c->noutbox++]=o; } }
    free(t); }

/* history: append-only file of [u32 LE len][sealed record] */
/* durable append: a record must be on disk before the event that announces it (a UI or peer may act on 'done' and the
   process may then be killed); _commit is msvcrt (XP-safe) = FlushFileBuffers */
static void file_sync(FILE *f){
#ifdef _WIN32
    _commit(_fileno(f));
#else
    fsync(fileno(f));
#endif
}
static void hist_append(lsm_core *c, buf_t *rec){ char p[600]; size_t sn; uint8_t *s=seal_rand(c->store_key,rec->p,rec->n,&sn); FILE *f; uint8_t l[4]; int i;
    path_join(p,sizeof p,c->data_dir,"history.dat"); f=ufopen(p,"ab"); if(f){ for(i=0;i<4;i++) l[i]=(uint8_t)(sn>>(8*i)); fwrite(l,1,4,f); fwrite(s,1,sn,f); fflush(f); file_sync(f); fclose(f);} free(s); }
static lsm_msg *msg_get(lsm_core *c, const char *id){ int i; for(i=c->nmsgs-1;i>=0;i--) if(!strcmp(c->msgs[i].id,id)) return &c->msgs[i]; return NULL; }
static void hist_rec_msg(lsm_core *c, lsm_msg *m){ buf_t b={0};
    bfmt(&b,"M\t%s\t%s\t%d\t%s\t",m->id,m->conv,m->out,m->sender_fp); esc_put(&b,m->sender_name); bfmt(&b,"\t%d\t",m->kind); esc_put(&b,m->body?m->body:"");
    bfmt(&b,"\t%lld\t%d\t%s\t",(long long)m->ts_ms,m->status,m->file_tid); esc_put(&b,m->file_name); bfmt(&b,"\t%llu\t",(unsigned long long)m->file_size); esc_put(&b,m->file_path);
    hist_append(c,&b); b_free(&b); }
static void hist_rec_upd(lsm_core *c, lsm_msg *m){ buf_t b={0}; bfmt(&b,"S\t%s\t%d\t%d\t",m->id,m->status,m->unread); esc_put(&b,m->file_path); hist_append(c,&b); b_free(&b); }
static lsm_msg *msg_push(lsm_core *c){ lsm_msg *m; c->msgs=(lsm_msg*)realloc(c->msgs,sizeof(lsm_msg)*(c->nmsgs+1)); m=&c->msgs[c->nmsgs++]; memset(m,0,sizeof *m); return m; }
static void tomb_add(lsm_core *c, const char *id); static int msg_unlink(lsm_core *c, const char *id);
static void load_history(lsm_core *c){ char p[600]; size_t n,off=0; uint8_t *d; path_join(p,sizeof p,c->data_dir,"history.dat"); d=read_file(p,&n); if(!d) return;
    while(off+4<=n){ size_t l=d[off]|(d[off+1]<<8)|(d[off+2]<<16)|((size_t)d[off+3]<<24), pn; char *pl, *f[16]; int k; off+=4; if(off+l>n) break;
        pl=(char*)open_rand(c->store_key,d+off,l,&pn); off+=l; if(!pl) continue; k=split_tabs(pl,f,16);
        if(f[0][0]=='M' && k>=14){ lsm_msg *m=msg_get(c,f[1]); /* a later M record for the same id is an update (file fields arrive after the first record) */
            if(m){ m->kind=atoi(f[6]); scpy(m->file_tid,f[10],40); scpy(m->file_name,f[11],260); m->file_size=strtoull(f[12],NULL,10); if(f[13][0]) scpy(m->file_path,f[13],520); free(pl); continue; } m=msg_push(c); scpy(m->id,f[1],40); scpy(m->conv,f[2],64); m->out=atoi(f[3]); scpy(m->sender_fp,f[4],FP_LEN); scpy(m->sender_name,f[5],200); m->kind=atoi(f[6]); m->body=xstrdup(f[7]); m->ts_ms=atoll(f[8]); m->status=atoi(f[9]); scpy(m->file_tid,f[10],40); scpy(m->file_name,f[11],260); m->file_size=strtoull(f[12],NULL,10); scpy(m->file_path,f[13],520); }
        else if(f[0][0]=='D' && k>=2){ tomb_add(c,f[1]); msg_unlink(c,f[1]); }
        else if(f[0][0]=='S' && k>=5){ lsm_msg *m=msg_get(c,f[1]); if(m){ m->status=atoi(f[2]); m->unread=atoi(f[3]); if(f[4][0]) scpy(m->file_path,f[4],520);} }
        free(pl); }
    free(d); }

static int tomb_has(lsm_core *c, const char *id){ int i; for(i=c->ntomb-1;i>=0;i--) if(!strcmp(c->tomb[i],id)) return 1; return 0; }
static void tomb_add(lsm_core *c, const char *id){ if(tomb_has(c,id)) return;
    if(c->ntomb>=20000){ memmove(c->tomb,c->tomb+1,sizeof(c->tomb[0])*(c->ntomb-1)); c->ntomb--; }
    c->tomb=(char(*)[40])realloc(c->tomb,sizeof(c->tomb[0])*(c->ntomb+1)); scpy(c->tomb[c->ntomb++],id,40); }
static int msg_unlink(lsm_core *c, const char *id){ int i; for(i=c->nmsgs-1;i>=0;i--) if(!strcmp(c->msgs[i].id,id)){ free(c->msgs[i].body);
        memmove(&c->msgs[i],&c->msgs[i+1],sizeof(lsm_msg)*(c->nmsgs-i-1)); c->nmsgs--; return 1; } return 0; }
static int seen_has(lsm_core *c, const uint8_t id[16]){ int i; char s[37]; for(i=0;i<c->nseen;i++) if(!memcmp(c->seen[i],id,16)) return 1; lsm_uuid_str(id,s); return tomb_has(c,s) || msg_get(c,s)!=NULL; }
static void seen_add(lsm_core *c, const uint8_t id[16]){ if(c->nseen<20000){ memcpy(c->seen[c->nseen++],id,16); } else { memcpy(c->seen[c->seen_pos],id,16); c->seen_pos=(c->seen_pos+1)%20000; } }

static int load_identity(lsm_core *c){ char p[600]; size_t n; uint8_t *d; path_join(p,sizeof p,c->data_dir,"identity.txt"); d=read_file(p,&n);
    if(d && n>=129 && !hex_dec((char*)d,c->x_sk,32) && !hex_dec((char*)d+65,c->ed_seed,32)){ free(d); }
    else { char t[200]; free(d); rand_bytes(c->x_sk,32); rand_bytes(c->ed_seed,32); hex_enc(c->x_sk,32,t); t[64]='\n'; hex_enc(c->ed_seed,32,t+65); t[129]='\n'; if(write_atomic(p,t,130)) return -1; }
    crypto_x25519_public_key(c->x_pk,c->x_sk);
    { uint8_t seed[32]; memcpy(seed,c->ed_seed,32); crypto_ed25519_key_pair(c->ed_sk,c->ed_pk,seed); }
    fp_from_x(c->x_pk,c->fp);
    path_join(p,sizeof p,c->data_dir,"state.key"); d=read_file(p,&n);
    if(d && n==32) memcpy(c->store_key,d,32); else { rand_bytes(c->store_key,32); if(write_atomic(p,c->store_key,32)) { free(d); return -1; } }
    free(d);
    path_join(p,sizeof p,c->data_dir,"name.txt"); d=read_file(p,&n);
    if(d){ char t[256]; scpy(t,(char*)d,sizeof t); { char *e=strpbrk(t,"\r\n"); if(e) *e=0; } sanitize_name(t,c->name,sizeof c->name); free(d); }
    else { { char hn[256]="XP"; /* default display name: the computer name, as on v0.5 */
#ifdef _WIN32
        wchar_t w[256]; DWORD wl=256; if(GetComputerNameW(w,&wl)) WideCharToMultiByte(CP_UTF8,0,w,-1,hn,sizeof hn,NULL,NULL);
#else
        if(gethostname(hn,sizeof hn)!=0||!hn[0]) strcpy(hn,"Linux"); hn[255]=0;
#endif
        sanitize_name(c->o.name?c->o.name:hn,c->name,sizeof c->name); } write_atomic(p,c->name,strlen(c->name)); }
    return 0; }

/* ------------------------------------------------------------ links */
static conn *conn_by_fp(lsm_core *c, const char *fp){ conn *k; for(k=c->conns;k;k=k->next) if(k->state==4 && !strcmp(k->fp,fp)) return k; return NULL; }
static void raw_send(conn *k, const uint8_t *d, size_t n){ uint8_t l[4]={(uint8_t)(n>>24),(uint8_t)(n>>16),(uint8_t)(n>>8),(uint8_t)n}; b_put(&k->out,l,4); b_put(&k->out,d,n); }
static void send_plain(conn *k, const uint8_t *p, size_t n){
    uint8_t *ct; if(n>65535-16) return; ct=(uint8_t*)malloc(n+16);
    noise_enc(k->tx,k->txn++,p,n,ct); raw_send(k,ct,n+16); free(ct); }
static void send_frame(conn *k, buf_t *f){ send_plain(k,f->p,f->n); }
static int send_to_fp(lsm_core *c, const char *fp, buf_t *f){ conn *k=conn_by_fp(c,fp); if(!k) return 0; send_frame(k,f); return 1; }
static void fr_simple(buf_t *b, uint32_t v){ b->n=0; b_u32(b,v); }

static void addr_str(const struct sockaddr_in *a, char *o, size_t n){ snprintf(o,n,"%s:%u",inet_ntoa(a->sin_addr),(unsigned)ntohs(a->sin_port)); }
static int resolve(const char *s, int defport, struct sockaddr_in *out){
    char host[128]; const char *colon=strrchr(s,':'); int port=defport; unsigned long ip;
    scpy(host,s,sizeof host); if(colon){ host[colon-s]=0; port=atoi(colon+1); }
    memset(out,0,sizeof *out); out->sin_family=AF_INET; out->sin_port=htons((unsigned short)port);
    ip=inet_addr(host); /* inet_pton is Vista+ */
    if(ip==INADDR_NONE){ struct hostent *h=gethostbyname(host); if(!h||h->h_addrtype!=AF_INET) return -1; memcpy(&out->sin_addr,h->h_addr_list[0],4); }
    else out->sin_addr.s_addr=ip;
    return port>0&&port<65536?0:-1; }

static peer_rt *rt_entry(lsm_core *c, const char *fp){ int i; for(i=0;i<c->nrt;i++) if(!strcmp(c->rt[i].fp,fp)) return &c->rt[i]; c->rt=(peer_rt*)realloc(c->rt,sizeof(peer_rt)*(c->nrt+1)); memset(&c->rt[c->nrt],0,sizeof(peer_rt)); scpy(c->rt[c->nrt].fp,fp,FP_LEN); return &c->rt[c->nrt++]; }
static int64_t backoff(int f){ int64_t base=500; int i; for(i=0;i<f && base<30000;i++) base*=2; if(base>30000) base=30000; { uint8_t r; rand_bytes(&r,1); return base+(r*400/256); } }

static void conn_start(lsm_core *c, sock_t s, const struct sockaddr_in *a, int initiator, const char *key){
    conn *k=(conn*)calloc(1,sizeof(conn)); int one=1;
    k->s=s; k->addr=*a; k->initiator=initiator; k->started=mono_ms(); k->last_rx=k->started; k->last_ping=k->started;
    scpy(k->dial_key,key?key:"",sizeof k->dial_key);
    setsockopt(s,IPPROTO_TCP,TCP_NODELAY,(const char*)&one,sizeof one);
    k->state=initiator?1:2;
    noise_init(&k->hs,initiator,c->x_sk);
    k->next=c->conns; c->conns=k; }

static void dial(lsm_core *c, const struct sockaddr_in *a, const char *key){
    sock_t s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); int r, e; char as[64];
    if(s==BADSOCK) return; set_nonblock(s);
    r=connect(s,(const struct sockaddr*)a,sizeof *a); e=SOCKERR;
    addr_str(a,as,sizeof as);
    if(r!=0 && !INPROGRESS(e)){ closesock(s); logf_(c,"dial %s failed immediately (%d)",as,e); return; }
    conn_start(c,s,a,1,key); }

static void hs_begin_initiator(conn *k){ uint8_t m[64]; int n=noise_write(&k->hs,m); raw_send(k,m,(size_t)n); k->state=2; }

static void dial_failed(lsm_core *c, const char *key, const char *why){
    if(!strncmp(key,"fp:",3)){ peer_rt *r=rt_entry(c,key+3); r->dialing=0; r->failures++; r->next_attempt=mono_ms()+backoff(r->failures); if(r->next_attempt<mono_ms()+2000) r->next_attempt=mono_ms()+2000; }
    else if(!strncmp(key,"addr:",5)){ int i; for(i=0;i<c->nmanual;i++) if(!strcmp(c->manual[i].addr,key+5)){ manual_rt *m=&c->manual[i]; m->dialing=0; m->failures++; m->next_attempt=mono_ms()+(m->failures<3?5000:30000);} }
    (void)why; }

static void fail_transfers_for(lsm_core *c, const char *fp, const char *why);
static void on_link_up(lsm_core *c, conn *k);
static void on_frame(lsm_core *c, conn *k, rd_t *r);

struct relay_cl; static void rl_on_link_closed(lsm_core *c, conn *k); static void rl_flush_link(lsm_core *c, conn *k); static void conn_parse(lsm_core *c, conn *k);
/* close the transport of a link: a socket for LAN links, a CLOSE blob for relay links */
static void link_drop(lsm_core *c, conn *k){ if(k->via_relay) rl_on_link_closed(c,k); else closesock(k->s); k->state=9; }
static void conn_close(lsm_core *c, conn *k, const char *why, int bye){
    if(k->state==9) return;
    { char as[64]; addr_str(&k->addr,as,sizeof as); logf_(c,"link %s closed (state %d): %s",as,k->state,why); }
    if(bye && k->state==4){ buf_t b={0}; fr_simple(&b,15); send_frame(k,&b); b_free(&b); if(k->via_relay) rl_flush_link(c,k); else send(k->s,(const char*)k->out.p,(int)k->out.n,0); }
    if(k->state!=4 && k->dial_key[0]) dial_failed(c,k->dial_key,why);
    if(k->state==4){ conn *o; int still=0; for(o=c->conns;o;o=o->next) if(o!=k && o->state==4 && !strcmp(o->fp,k->fp)) still=1;
        if(!still){ lsm_contact *ct=contact_get(c,k->fp); peer_rt *r=rt_entry(c,k->fp); if(ct){ ct->online=0; ct->rtt_ms=-1; }
            fail_transfers_for(c,k->fp,"\xe8\xbf\x9e\xe6\x8e\xa5\xe6\x96\xad\xe5\xbc\x80"); /* 连接断开 */
            { uint8_t j; rand_bytes(&j,1); r->next_attempt=mono_ms()+300+j*700/256; }
            if(ct) ct->via_relay=0; emit(c,"peer\t%s\toffline\t%s",k->fp,why); } }
    link_drop(c,k); }

static void process_wire(lsm_core *c, conn *k, const uint8_t *m, size_t n){
    if(k->state==2){
        if(noise_read(&k->hs,m,n)){ conn_close(c,k,"noise handshake failed",0); return; }
        if(k->hs.step<3){ uint8_t o[128]; int w=noise_write(&k->hs,o); if(w<0){ conn_close(c,k,"noise write",0); return; } raw_send(k,o,(size_t)w); }
        if(k->hs.step==3){ buf_t h={0};
            noise_split(&k->hs,k->tx,k->rx); fp_from_x(k->hs.rs,k->fp);
            if(!strcmp(k->fp,c->fp)){ conn_close(c,k,"connected to self",0); return; }
            b_u16(&h,PROTO); b_str(&h,LSM_APP_VERSION); b_str(&h,c->name); b_put(&h,c->ed_pk,32); b_u16(&h,(uint16_t)c->lport); b_u32(&h,MY_CAPS);
            send_plain(k,h.p,h.n); b_free(&h); k->state=3; }
        return; }
    { uint8_t *pt=(uint8_t*)malloc(n); rd_t r;
      if(n<16 || noise_dec(k->rx,k->rxn,m,n,pt)){ free(pt); conn_close(c,k,"decrypt failed",0); return; }
      k->rxn++; r.p=pt; r.e=pt+n-16; r.err=0;
      if(k->state==3){ uint16_t pv=r_u16(&r); char *ver=r_str(&r), *nm=r_str(&r);
          r_fix(&r,k->remote_ed,32); k->rlisten=r_u16(&r); k->rcaps=(!r.err && r.e-r.p>=4)?r_u32(&r):0;
          scpy(k->rver,ver,sizeof k->rver); sanitize_name(nm,k->rname,sizeof k->rname); free(ver); free(nm);
          if(r.err || pv!=PROTO){ free(pt); conn_close(c,k,pv!=PROTO?"protocol version mismatch":"bad hello",0); return; }
          k->state=4; on_link_up(c,k); }
      else if(k->state==4) on_frame(c,k,&r);
      free(pt); } }

static void conn_readable(lsm_core *c, conn *k){
    uint8_t tmp[16384]; int n;
    for(;;){ n=recv(k->s,(char*)tmp,sizeof tmp,0);
        if(n>0){ b_put(&k->in,tmp,(size_t)n); k->last_rx=mono_ms(); if(n<(int)sizeof tmp) break; continue; }
        if(n==0){ conn_close(c,k,"peer closed",0); return; }
        if(WOULDBLOCK(SOCKERR)) break; conn_close(c,k,"recv error",0); return; }
    conn_parse(c,k); }

static void conn_parse(lsm_core *c, conn *k){
    while(k->state!=9 && k->in.n>=4){ size_t l=((size_t)k->in.p[0]<<24)|(k->in.p[1]<<16)|(k->in.p[2]<<8)|k->in.p[3]; uint8_t *m;
        if(l==0||l>MAX_WIRE){ conn_close(c,k,"bad frame length",0); return; }
        if(k->in.n<4+l) break;
        m=(uint8_t*)malloc(l); memcpy(m,k->in.p+4,l); b_consume(&k->in,4+l);
        process_wire(c,k,m,l); free(m); } }

static void conn_writable(lsm_core *c, conn *k){
    if(k->state==1){ int e=0; socklen_t el=sizeof e; getsockopt(k->s,SOL_SOCKET,SO_ERROR,(char*)&e,&el); if(e){ conn_close(c,k,"connect failed",0); return; } hs_begin_initiator(k); }
    while(k->out.n){ int n=send(k->s,(const char*)k->out.p,(int)(k->out.n>65536?65536:k->out.n),0);
        if(n>0){ b_consume(&k->out,(size_t)n); continue; }
        if(n<0 && WOULDBLOCK(SOCKERR)) break; conn_close(c,k,"send error",0); return; } }

#include "relay.c"

/* ------------------------------------------------------------ discovery */
static void build_announce(lsm_core *c, buf_t *out){
    buf_t a={0}, sa={0}; uint8_t sig[64]; uint64_t nonce;
    rand_bytes(&nonce,8);
    b_u16(&a,PROTO); b_put(&a,c->x_pk,32); b_put(&a,c->ed_pk,32); b_str(&a,c->name); b_u16(&a,(uint16_t)c->lport); b_u16(&a,(uint16_t)c->o.discovery_port);
    b_u64(&a,nonce); b_u64(&a,(uint64_t)wall_ms()); b_u8(&a,1);
    crypto_ed25519_sign(sig,c->ed_sk,a.p,a.n);
    b_bytes(&sa,a.p,a.n); b_bytes(&sa,sig,64);
    out->n=0; b_put(out,"LSM5",4); b_put(out,sa.p,sa.n); b_free(&a); b_free(&sa); }

static void enum_ifaces(lsm_core *c){
    uint32_t ips[32], masks[32]; int n=0, i, j;
#ifdef _WIN32
    ULONG len=0; IP_ADAPTER_INFO *ai, *a;
    GetAdaptersInfo(NULL,&len); ai=(IP_ADAPTER_INFO*)malloc(len?len:sizeof(IP_ADAPTER_INFO));
    if(ai && GetAdaptersInfo(ai,&len)==NO_ERROR){ for(a=ai;a && n<32;a=a->Next){ IP_ADDR_STRING *s; for(s=&a->IpAddressList;s && n<32;s=s->Next){ uint32_t ip=inet_addr(s->IpAddress.String), m=inet_addr(s->IpMask.String); if(ip==0||ip==INADDR_NONE) continue; ips[n]=ip; masks[n]=m; n++; } } }
    free(ai);
#elif defined(__ANDROID__)
    { int fd=socket(AF_INET,SOCK_DGRAM,0); struct ifreq rq[32]; struct ifconf ic; int i, k;
      if(fd>=0){ ic.ifc_len=sizeof rq; ic.ifc_req=rq; if(ioctl(fd,SIOCGIFCONF,&ic)==0){ k=ic.ifc_len/(int)sizeof(struct ifreq);
          for(i=0;i<k && n<32;i++){ struct ifreq f=rq[i]; uint32_t ip; if(rq[i].ifr_addr.sa_family!=AF_INET) continue; ip=((struct sockaddr_in*)&rq[i].ifr_addr)->sin_addr.s_addr;
              if(ioctl(fd,SIOCGIFFLAGS,&f)!=0 || (f.ifr_flags&IFF_LOOPBACK) || !(f.ifr_flags&IFF_UP)) continue; f=rq[i];
              ips[n]=ip; masks[n]=ioctl(fd,SIOCGIFNETMASK,&f)==0?((struct sockaddr_in*)&f.ifr_addr)->sin_addr.s_addr:0; n++; } }
        close(fd); } }
#else
    struct ifaddrs *ia, *p;
    if(getifaddrs(&ia)==0){ for(p=ia;p && n<32;p=p->ifa_next){ if(!p->ifa_addr||p->ifa_addr->sa_family!=AF_INET||(p->ifa_flags&IFF_LOOPBACK)||!(p->ifa_flags&IFF_UP)) continue; ips[n]=((struct sockaddr_in*)p->ifa_addr)->sin_addr.s_addr; masks[n]=p->ifa_netmask?((struct sockaddr_in*)p->ifa_netmask)->sin_addr.s_addr:0; n++; } freeifaddrs(ia); }
#endif
    /* drop interfaces that went away */
    for(i=0;i<c->nifs;){ int keep=0; for(j=0;j<n;j++) if(ips[j]==c->ifs[i].ip) keep=1; if(!keep){ closesock(c->ifs[i].s); c->ifs[i]=c->ifs[--c->nifs]; } else i++; }
    for(j=0;j<n;j++){ int have=0; uint32_t hip=ntohl(ips[j]); if((hip>>24)==127 || (hip>>16)==0xA9FE) continue; /* loopback, link-local */
        for(i=0;i<c->nifs;i++) if(c->ifs[i].ip==ips[j]) have=1;
        if(!have && c->nifs<32){ sock_t s=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP); int one=1; unsigned char ttl=1, loop=1; struct sockaddr_in b; char as[32];
            if(s==BADSOCK) continue;
            setsockopt(s,SOL_SOCKET,SO_BROADCAST,(const char*)&one,sizeof one);
            setsockopt(s,IPPROTO_IP,IP_MULTICAST_IF,(const char*)&ips[j],4);
            setsockopt(s,IPPROTO_IP,IP_MULTICAST_TTL,(const char*)&ttl,1);
            setsockopt(s,IPPROTO_IP,IP_MULTICAST_LOOP,(const char*)&loop,1);
            memset(&b,0,sizeof b); b.sin_family=AF_INET; b.sin_addr.s_addr=ips[j];
            if(bind(s,(struct sockaddr*)&b,sizeof b)){ closesock(s); continue; }
            set_nonblock(s);
            c->ifs[c->nifs].ip=ips[j]; c->ifs[c->nifs].bcast=(ips[j]&masks[j])|~masks[j]; c->ifs[c->nifs].s=s; c->nifs++;
            { struct in_addr x; x.s_addr=ips[j]; scpy(as,inet_ntoa(x),sizeof as); logf_(c,"interface %s used for discovery",as); } }
        if(c->usock!=BADSOCK){ int done=0; for(i=0;i<c->njoined;i++) if(c->joined[i]==ips[j]) done=1;
            if(!done && c->njoined<32){ struct ip_mreq mr; mr.imr_multiaddr.s_addr=inet_addr(MCAST_GROUP); mr.imr_interface.s_addr=ips[j];
                if(!setsockopt(c->usock,IPPROTO_IP,IP_ADD_MEMBERSHIP,(const char*)&mr,sizeof mr)) c->joined[c->njoined++]=ips[j]; } } } }

static void send_announces(lsm_core *c){
    buf_t pkt={0}; int i; struct sockaddr_in d; char tl[1024], *t, *nx;
    build_announce(c,&pkt);
    memset(&d,0,sizeof d); d.sin_family=AF_INET; d.sin_port=htons((unsigned short)c->o.discovery_port);
    for(i=0;i<c->nifs;i++){
        d.sin_addr.s_addr=c->ifs[i].bcast; sendto(c->ifs[i].s,(const char*)pkt.p,(int)pkt.n,0,(struct sockaddr*)&d,sizeof d);
        if(c->ifs[i].bcast!=0xFFFFFFFFu){ d.sin_addr.s_addr=0xFFFFFFFFu; sendto(c->ifs[i].s,(const char*)pkt.p,(int)pkt.n,0,(struct sockaddr*)&d,sizeof d); }
        d.sin_addr.s_addr=inet_addr(MCAST_GROUP); sendto(c->ifs[i].s,(const char*)pkt.p,(int)pkt.n,0,(struct sockaddr*)&d,sizeof d); }
    scpy(tl,c->announce_targets,sizeof tl);
    for(t=tl; t && *t; t=nx){ struct sockaddr_in a; nx=strchr(t,','); if(nx) *nx++=0; while(*t==' ') t++; if(!*t) continue;
        if(!resolve(t,c->o.discovery_port,&a) && c->ufallback!=BADSOCK) sendto(c->ufallback,(const char*)pkt.p,(int)pkt.n,0,(struct sockaddr*)&a,sizeof a); }
    for(i=0;i<c->nmanual;i++){ struct sockaddr_in a=c->manual[i].sa; a.sin_port=htons((unsigned short)c->o.discovery_port); if(c->ufallback!=BADSOCK) sendto(c->ufallback,(const char*)pkt.p,(int)pkt.n,0,(struct sockaddr*)&a,sizeof a); }
    b_free(&pkt); }

static void on_udp(lsm_core *c, const uint8_t *p, size_t n, const struct sockaddr_in *src){
    rd_t r, ab; size_t bl, sl; const uint8_t *body, *sig; uint16_t proto, tcp, disco; uint8_t xpk[32], edpk[32]; char *nm, fp[FP_LEN], name[300]; int want;
    if(n<4 || memcmp(p,"LSM5",4)) return;
    r.p=p+4; r.e=p+n; r.err=0; body=r_bytes(&r,&bl); sig=r_bytes(&r,&sl);
    if(r.err || sl!=64) return;
    ab.p=body; ab.e=body+bl; ab.err=0; proto=r_u16(&ab); r_fix(&ab,xpk,32); r_fix(&ab,edpk,32); nm=r_str(&ab); tcp=r_u16(&ab); disco=r_u16(&ab); r_u64(&ab); r_u64(&ab); want=r_u8(&ab);
    if(ab.err || proto!=PROTO || crypto_ed25519_check(sig,edpk,body,bl)){ free(nm); return; }
    fp_from_x(xpk,fp); sanitize_name(nm,name,sizeof name); free(nm);
    if(!strcmp(fp,c->fp)) return;
    if(want && disco){ int i, slot=-1; int64_t now=mono_ms(); int due=1;
        for(i=0;i<64;i++){ if(c->replies[i].ip==src->sin_addr.s_addr){ slot=i; if(now-c->replies[i].t<=10000) due=0; } }
        if(due){ buf_t pkt={0}; struct sockaddr_in a=*src; if(slot<0){ for(i=0;i<64;i++) if(c->replies[i].t==0||now-c->replies[i].t>60000){slot=i;break;} if(slot<0) slot=0; }
            c->replies[slot].ip=src->sin_addr.s_addr; c->replies[slot].t=now; a.sin_port=htons(disco); build_announce(c,&pkt);
            if(c->ufallback!=BADSOCK) sendto(c->ufallback,(const char*)pkt.p,(int)pkt.n,0,(struct sockaddr*)&a,sizeof a); b_free(&pkt); } }
    { peer_rt *rt=rt_entry(c,fp); lsm_contact *k=contact_entry(c,fp); int fresh=rt->last_announce==0;
      if(!k->online) scpy(k->name,name,sizeof k->name);
      rt->dial=*src; rt->dial.sin_port=htons(tcp); rt->has_dial=1; rt->last_announce=mono_ms();
      if(k){ k->last_announce=rt->last_announce; }
      if(fresh) emit(c,"seen\t%s\t%s\t%s:%u",fp,name,inet_ntoa(src->sin_addr),(unsigned)tcp);
      if(!conn_by_fp(c,fp) && !rt->dialing){ int64_t soon=mono_ms()+(rt->failures==0?0:(backoff(rt->failures)<3000?backoff(rt->failures):3000)); if(!rt->next_attempt || rt->next_attempt>soon) rt->next_attempt=soon; }
      if(!k){ /* remember name hint for UI */ }
      (void)name; } }

/* ------------------------------------------------------------ messages / events */
static void conv_dm(const char *fp, char *o){ snprintf(o,64,"dm:%s",fp); }
static void conv_group(const uint8_t id[16], char *o){ char u[37]; lsm_uuid_str(id,u); snprintf(o,64,"group:%s",u); }
static const char *peer_name(lsm_core *c, const char *fp){ lsm_contact *k=contact_get(c,fp); return k&&k->name[0]?k->name:fp; }
static void emit_msg(lsm_core *c, const char *kind, lsm_msg *m){
    static const char *st[]={"queued","sent","delivered","read","received","failed"};
    char b[600]; scpy(b,m->body?m->body:"",sizeof b); { char *q; for(q=b;*q;q++) if(*q=='\t'||*q=='\n'||*q=='\r') *q=' '; } utf8_trunc(b,400);
    emit(c,"%s\t%s\t%s\t%s\t%s\t%s\t%s",kind,m->id,m->conv,m->out?"out":"in",st[m->status],m->kind==K_FILE?"file":m->kind==K_SYSTEM?"system":"text",m->kind==K_FILE?m->file_name:b); }
static lsm_msg *add_message(lsm_core *c, const char *id, const char *conv, int out, const char *sfp, int kind, const char *body, int64_t ts, int status){
    lsm_msg *m=msg_push(c); scpy(m->id,id,40); scpy(m->conv,conv,64); m->out=out; scpy(m->sender_fp,sfp,FP_LEN);
    scpy(m->sender_name,out?c->name:(sfp[0]?peer_name(c,sfp):""),200); m->kind=kind; m->body=xstrdup(body?body:""); m->ts_ms=ts>0?ts:wall_ms(); m->status=status;
    m->unread=(!out && kind!=K_SYSTEM);
    hist_rec_msg(c,m); emit_msg(c,"msg",m); return m; }
static void set_status(lsm_core *c, lsm_msg *m, int st){ if(!m || m->status==st) return; m->status=st; hist_rec_upd(c,m); emit_msg(c,"msg_update",m); }
static void system_msg(lsm_core *c, const char *conv, const char *fmt, ...){ char t[1024], id[37]; uint8_t u[16]; va_list ap; va_start(ap,fmt); vsnprintf(t,sizeof t,fmt,ap); va_end(ap); uuid_new(u); lsm_uuid_str(u,id); add_message(c,id,conv,0,"",K_SYSTEM,t,0,ST_RECEIVED); }
static void notice(lsm_core *c, const char *fmt, ...){ char t[1024]; va_list ap; va_start(ap,fmt); vsnprintf(t,sizeof t,fmt,ap); va_end(ap); emit(c,"notice\t%s",t); }

/* ------------------------------------------------------------ outbox */
static void frame_msg(buf_t *f, const uint8_t id[16], const buf_t *payload){ f->n=0; b_u32(f,3); b_uuid(f,id); b_put(f,payload->p,payload->n); }
static int send_reliable(lsm_core *c, const char *fp, const buf_t *payload, const uint8_t id[16], const char *msg_ref){
    buf_t f={0}; int online; outgoing o; frame_msg(&f,id,payload); online=send_to_fp(c,fp,&f); b_free(&f);
    memset(&o,0,sizeof o); scpy(o.fp,fp,FP_LEN); memcpy(o.id,id,16); b_put(&o.payload,payload->p,payload->n); if(msg_ref) scpy(o.msg_ref,msg_ref,40);
    c->outbox=(outgoing*)realloc(c->outbox,sizeof(outgoing)*(c->noutbox+1)); c->outbox[c->noutbox++]=o; save_outbox(c); return online; }
static void flush_outbox(lsm_core *c, const char *fp){ int i; buf_t f={0};
    for(i=0;i<c->noutbox;i++){ outgoing *o=&c->outbox[i]; if(strcmp(o->fp,fp)) continue; frame_msg(&f,o->id,&o->payload);
        if(send_to_fp(c,fp,&f) && o->msg_ref[0]){ lsm_msg *m=msg_get(c,o->msg_ref); if(m && m->status==ST_QUEUED) set_status(c,m,ST_SENT); } }
    b_free(&f); }
static void on_ack(lsm_core *c, const char *fp, const uint8_t id[16]){ int i; char ref[40]=""; int changed=0;
    for(i=0;i<c->noutbox;){ if(!strcmp(c->outbox[i].fp,fp) && !memcmp(c->outbox[i].id,id,16)){ if(c->outbox[i].msg_ref[0]) scpy(ref,c->outbox[i].msg_ref,40); b_free(&c->outbox[i].payload); c->outbox[i]=c->outbox[--c->noutbox]; changed=1; } else i++; }
    if(changed) save_outbox(c);
    if(ref[0]){ int remaining=0; for(i=0;i<c->noutbox;i++) if(!strcmp(c->outbox[i].msg_ref,ref)) remaining=1;
        if(!remaining){ lsm_msg *m=msg_get(c,ref); if(m && (m->status==ST_QUEUED||m->status==ST_SENT)) set_status(c,m,ST_DELIVERED); } } }

/* ------------------------------------------------------------ groups */
static void invite_bytes(buf_t *b, lsm_group *g){ int i; b->n=0; b_put(b,g->id,16); b_put(b,g->name,strlen(g->name)); b_u8(b,0); b_put(b,g->creator_fp,strlen(g->creator_fp)); b_u8(b,0);
    { char (*ms)[FP_LEN]=(char(*)[FP_LEN])malloc(FP_LEN*(g->nmembers+1)); memcpy(ms,g->members,FP_LEN*g->nmembers); qsort(ms,g->nmembers,FP_LEN,cmpstr); for(i=0;i<g->nmembers;i++){ b_put(b,ms[i],strlen(ms[i])); b_u8(b,0);} free(ms); }
    b_put(b,g->key,32); b_u64(b,(uint64_t)g->created_ms); }
static void rekey_bytes(buf_t *b, lsm_group *g, const char *removed){ int i; b->n=0; b_put(b,"REKEY1",6); b_put(b,g->id,16);
    { char (*ms)[FP_LEN]=(char(*)[FP_LEN])malloc(FP_LEN*(g->nmembers+1)); memcpy(ms,g->members,FP_LEN*g->nmembers); qsort(ms,g->nmembers,FP_LEN,cmpstr); for(i=0;i<g->nmembers;i++){ b_put(b,ms[i],strlen(ms[i])); b_u8(b,0);} free(ms); }
    b_put(b,g->key,32); b_u64(b,g->key_epoch); b_put(b,removed,strlen(removed)); }
static void payload_invite(lsm_core *c, lsm_group *g, buf_t *p){ buf_t sb={0}; uint8_t sig[64]; int i; invite_bytes(&sb,g); crypto_ed25519_sign(sig,c->ed_sk,sb.p,sb.n); b_free(&sb);
    p->n=0; b_u32(p,2); b_uuid(p,g->id); b_str(p,g->name); b_str(p,g->creator_fp); b_u64(p,(uint64_t)g->nmembers); for(i=0;i<g->nmembers;i++) b_str(p,g->members[i]);
    b_put(p,g->key,32); b_u64(p,(uint64_t)g->created_ms); b_u64(p,g->key_epoch); b_bytes(p,sig,64); }
static void payload_rekey(lsm_core *c, lsm_group *g, const char *removed, buf_t *p){ buf_t sb={0}; uint8_t sig[64]; int i; rekey_bytes(&sb,g,removed); crypto_ed25519_sign(sig,c->ed_sk,sb.p,sb.n); b_free(&sb);
    p->n=0; b_u32(p,3); b_uuid(p,g->id); b_u64(p,(uint64_t)g->nmembers); for(i=0;i<g->nmembers;i++) b_str(p,g->members[i]); b_put(p,g->key,32); b_u64(p,g->key_epoch); b_str(p,removed); b_bytes(p,sig,64); }
static int verify_sig(lsm_core *c, const char *fp, const buf_t *msg, const uint8_t *sig, size_t sl){ lsm_contact *k=contact_get(c,fp); if(!k||!k->has_ed||sl!=64) return 0; return crypto_ed25519_check(sig,k->ed,msg->p,msg->n)==0; }
static void remove_member_rekey(lsm_core *c, lsm_group *g, const char *fp){ buf_t p={0}; uint8_t id[16]; int i; char rm[FP_LEN];
    scpy(rm,fp,FP_LEN); group_remove_member(g,rm); rand_bytes(g->key,32); g->key_epoch++; save_groups(c);
    payload_rekey(c,g,rm,&p); uuid_new(id); send_reliable(c,rm,&p,id,NULL);
    for(i=0;i<g->nmembers;i++) if(strcmp(g->members[i],c->fp)){ uuid_new(id); send_reliable(c,g->members[i],&p,id,NULL); }
    b_free(&p); { char u[37]; lsm_uuid_str(g->id,u); emit(c,"group\t%s\t%s",u,g->name); } }

/* ------------------------------------------------------------ files */
static void rs_sidecar_write(lsm_core *c, lsm_transfer *t); static void rs_sidecar_remove(lsm_transfer *t);
static int rs_pump(lsm_core *c); static void rs_restore(lsm_core *c); static void rs_out_write(lsm_core *c, lsm_transfer *t); static void rs_out_remove(lsm_core *c, lsm_transfer *t); static void rs_start_prefix(lsm_transfer *t, uint64_t off, const uint8_t pre[32]);
static lsm_transfer *xfer_get(lsm_core *c, const uint8_t tid[16]){ int i; for(i=0;i<c->nxfers;i++) if(!memcmp(c->xfers[i].tid,tid,16)) return &c->xfers[i]; return NULL; }
static int xfer_finished(lsm_transfer *t){ return t->state>=TS_DONE; }
static void emit_xfer(lsm_core *c, lsm_transfer *t){ static const char *st[]={"offered","active","verifying","done","failed","rejected","cancelled"}; char u[37]; lsm_uuid_str(t->tid,u);
    if(t->out && t->state>=TS_DONE) rs_out_remove(c,t);
    emit(c,"transfer\t%s\t%s\t%s\t%s\t%llu\t%llu\t%s\t%s",u,t->out?"out":"in",st[t->state],t->peer_fp,(unsigned long long)t->transferred,(unsigned long long)t->size,t->name,t->error); }
static void xfer_close_files(lsm_transfer *t){ if(t->fp_handle){ fclose((FILE*)t->fp_handle); t->fp_handle=NULL; } free(t->hash); t->hash=NULL; free(t->hctx); t->hctx=NULL; t->hashing=0; }
static void mark_file_failed(lsm_core *c, lsm_transfer *t){ lsm_msg *m=msg_get(c,t->msg_id); if(m && m->out) set_status(c,m,ST_FAILED); }
static void xfer_fail(lsm_core *c, lsm_transfer *t, int state, const char *err){ if(xfer_finished(t)) return; xfer_close_files(t); if(!t->out && t->part[0]){ uremove(t->part); rs_sidecar_remove(t); } t->state=state; scpy(t->error,err,sizeof t->error); emit_xfer(c,t); mark_file_failed(c,t); }
static void fail_transfers_for(lsm_core *c, const char *fp, const char *why){ int i; for(i=0;i<c->nxfers;i++){ lsm_transfer *t=&c->xfers[i];
    if(strcmp(t->peer_fp,fp) || !(t->state==TS_ACTIVE||t->state==TS_VERIFYING)) continue;
    if(t->resumable){ /* CAP_RESUME: pause; the sender re-offers on the next link (LAN or relay), the receiver resumes from its partial file */
        if(t->fp_handle){ fflush((FILE*)t->fp_handle); fclose((FILE*)t->fp_handle); t->fp_handle=NULL; }
        t->state=TS_OFFERED; scpy(t->error,"\xe8\xbf\x9e\xe6\x8e\xa5\xe4\xb8\xad\xe6\x96\xad\xef\xbc\x8c\xe9\x87\x8d\xe8\xbf\x9e\xe5\x90\x8e\xe8\x87\xaa\xe5\x8a\xa8\xe7\xbb\xad\xe4\xbc\xa0",sizeof t->error); /* 连接中断，重连后自动续传 */
        emit_xfer(c,t); continue; }
    xfer_fail(c,t,TS_FAILED,why); } }
static const char *mime_guess(const char *n){ const char *e=strrchr(n,'.'); static const char *m[][2]={{"png","image/png"},{"jpg","image/jpeg"},{"jpeg","image/jpeg"},{"gif","image/gif"},{"bmp","image/bmp"},{"webp","image/webp"},{"txt","text/plain"},{"pdf","application/pdf"},{"zip","application/zip"},{"doc","application/msword"},{"docx","application/vnd.openxmlformats-officedocument.wordprocessingml.document"},{"xls","application/vnd.ms-excel"},{"xlsx","application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},{"mp4","video/mp4"},{"mp3","audio/mpeg"}}; size_t i;
    if(e){ char l[16]; scpy(l,e+1,sizeof l); { char *q; for(q=l;*q;q++) *q=(char)tolower((unsigned char)*q);} for(i=0;i<sizeof m/sizeof m[0];i++) if(!strcmp(l,m[i][0])) return m[i][1]; } return "application/octet-stream"; }
static void sanitize_filename(const char *in, char *out, size_t n){ const char *b=in, *p; size_t o=0;
    for(p=in;*p;p++) if(*p=='/'||*p=='\\') b=p+1;
    for(p=b;*p && o+1<n;p++){ unsigned char ch=(unsigned char)*p; if(ch<0x20||strchr("<>:\"|?*",ch)) out[o++]='_'; else out[o++]=(char)ch; } out[o]=0;
    utf8_trunc(out,200); while(o && (out[strlen(out)-1]=='.'||out[strlen(out)-1]==' ')) { out[strlen(out)-1]=0; o--; }
    if(!out[0]||!strcmp(out,".")||!strcmp(out,"..")) scpy(out,"file",n); }
static void unique_path(lsm_core *c, const char *name, char *out, size_t n){ int i; char stem[260], ext[64]=""; const char *dot=strrchr(name,'.');
    path_join(out,n,c->dl_dir,name); if(!uexists(out)) return;
    scpy(stem,name,sizeof stem); if(dot && dot!=name){ scpy(ext,dot,sizeof ext); stem[dot-name]=0; }
    for(i=1;i<10000;i++){ char f[400]; snprintf(f,sizeof f,"%s (%d)%s",stem,i,ext); path_join(out,n,c->dl_dir,f); if(!uexists(out)) return; } }
static void send_offer(lsm_core *c, lsm_transfer *t){ buf_t f={0}; uint8_t mid[16]; lsm_uuid_parse(t->msg_id,mid);
    b_u32(&f,5); b_uuid(&f,t->tid); b_uuid(&f,mid); b_str(&f,t->name); b_u64(&f,t->size); b_str(&f,t->mime); b_put(&f,t->sha,32);
    if(t->has_group){ b_u8(&f,1); b_uuid(&f,t->group_id); } else b_u8(&f,0); b_u64(&f,(uint64_t)t->offer_ts);
    send_to_fp(c,t->peer_fp,&f); b_free(&f); }
static void resend_offers(lsm_core *c, const char *fp){ int i; for(i=0;i<c->nxfers;i++){ lsm_transfer *t=&c->xfers[i]; if(t->out && t->state==TS_OFFERED && !strcmp(t->peer_fp,fp)) send_offer(c,t); } }
static void send_tid_frame(lsm_core *c, const char *fp, uint32_t v, const uint8_t tid[16], const char *reason){ buf_t f={0}; b_u32(&f,v); b_uuid(&f,tid); if(reason) b_str(&f,reason); send_to_fp(c,fp,&f); b_free(&f); }
static void file_complete(lsm_core *c, lsm_transfer *t, int ok, const char *err){ buf_t f={0}; b_u32(&f,11); b_uuid(&f,t->tid); b_u8(&f,(uint8_t)ok); b_str(&f,err); send_to_fp(c,t->peer_fp,&f); b_free(&f); }
static void file_progress(lsm_core *c, lsm_transfer *t){ buf_t f={0}; b_u32(&f,9); b_uuid(&f,t->tid); b_u64(&f,t->transferred); send_to_fp(c,t->peer_fp,&f); b_free(&f); }
static int accept_transfer(lsm_core *c, lsm_transfer *t){ char part[600]; conn *k=conn_by_fp(c,t->peer_fp); umkdir(c->dl_dir);
    t->resumable=(k && (k->rcaps&CAP_RESUME))?1:0; t->accepted=1; t->error[0]=0;
    if(t->resumable && t->hash && t->part[0] && t->transferred>0 && t->transferred<t->size && (t->fp_handle=ufopen(t->part,"r+b"))!=NULL){
        /* resume: tell the sender how much we have + SHA-256 of it (frame 16 FileResume) */
        buf_t f={0}; sha256_ctx tmp=*(sha256_ctx*)t->hash; uint8_t pre[32]; sha256_final(&tmp,pre);
        fseek((FILE*)t->fp_handle,0,SEEK_END);
        if((uint64_t)ftell((FILE*)t->fp_handle)==t->transferred){
            t->resumed_from=t->transferred; t->last_prog=t->transferred; t->state=TS_ACTIVE;
            b_u32(&f,16); b_uuid(&f,t->tid); b_u64(&f,t->transferred); b_put(&f,pre,32); send_to_fp(c,t->peer_fp,&f); b_free(&f);
            rs_sidecar_write(c,t); emit_xfer(c,t); return 0; }
        fclose((FILE*)t->fp_handle); t->fp_handle=NULL; }
    if(t->part[0]){ uremove(t->part); rs_sidecar_remove(t); }
    snprintf(part,sizeof part,"%s" PATHSEP ".%s.part",c->dl_dir,t->name); scpy(t->part,part,sizeof t->part);
    t->fp_handle=ufopen(part,"wb"); if(!t->fp_handle){ return -1; }
    free(t->hash); t->hash=malloc(sizeof(sha256_ctx)); sha256_init((sha256_ctx*)t->hash); t->transferred=0; t->resumed_from=0; t->last_prog=0; t->state=TS_ACTIVE;
    rs_sidecar_write(c,t); send_tid_frame(c,t->peer_fp,6,t->tid,NULL); emit_xfer(c,t); return 0; }
/* sender pump: called every loop iteration; respects 4 MiB credit window and socket backlog */
static void pump_files(lsm_core *c){ int i; uint8_t *buf=NULL;
    for(i=0;i<c->nxfers;i++){ lsm_transfer *t=&c->xfers[i]; conn *k; if(!t->out || t->state!=TS_ACTIVE || !t->fp_handle) continue;
        k=conn_by_fp(c,t->peer_fp); if(!k){ continue; }
        while(t->transferred<t->size && k->out.n<512*1024 && t->transferred-t->credit<=WINDOW){
            size_t want=(size_t)((t->size-t->transferred)<CHUNK?(t->size-t->transferred):CHUNK), n; buf_t f={0};
            if(!buf) buf=(uint8_t*)malloc(CHUNK);
            n=fread(buf,1,want,(FILE*)t->fp_handle);
            if(n==0){ xfer_fail(c,t,TS_FAILED,"\xe6\x96\x87\xe4\xbb\xb6\xe5\x9c\xa8\xe5\x8f\x91\xe9\x80\x81\xe8\xbf\x87\xe7\xa8\x8b\xe4\xb8\xad\xe8\xa2\xab\xe4\xbf\xae\xe6\x94\xb9\xe6\x88\x96\xe6\x88\xaa\xe6\x96\xad"); send_tid_frame(c,t->peer_fp,12,t->tid,"read error"); break; }
            b_u32(&f,8); b_uuid(&f,t->tid); b_u64(&f,t->transferred); b_bytes(&f,buf,n); send_frame(k,&f); b_free(&f); t->transferred+=n; }
        if(t->state==TS_ACTIVE && t->transferred>=t->size){ fclose((FILE*)t->fp_handle); t->fp_handle=NULL; send_tid_frame(c,t->peer_fp,10,t->tid,NULL); emit_xfer(c,t); }
        if(t->state==TS_ACTIVE && t->transferred-t->credit>WINDOW && mono_ms()-t->last_credit_mono>30000){ xfer_fail(c,t,TS_FAILED,"\xe5\xaf\xb9\xe6\x96\xb9\xe9\x95\xbf\xe6\x97\xb6\xe9\x97\xb4\xe6\x97\xa0\xe5\x93\x8d\xe5\xba\x94"); send_tid_frame(c,t->peer_fp,12,t->tid,"timeout"); } }
    free(buf); }

static void on_file_frame(lsm_core *c, const char *fp, uint32_t v, rd_t *r){
    uint8_t tid[16]; lsm_transfer *t; r_uuid(r,tid); if(r->err) return; t=xfer_get(c,tid);
    if(t && strcmp(t->peer_fp,fp)) return;
    switch(v){
    case 5: { uint8_t mid[16], sha[32], gid[16]; char *name=NULL, *mime=NULL, nm[260], conv[64], ms[37]; uint64_t size; int hasg; int64_t ts; lsm_group *g=NULL; lsm_transfer nt;
        r_uuid(r,mid); name=r_str(r); size=r_u64(r); mime=r_str(r); r_fix(r,sha,32); hasg=r_u8(r); if(hasg) r_uuid(r,gid); ts=(int64_t)r_u64(r);
        if(r->err || t){ free(name); free(mime);
            if(!r->err && t && !t->out){ /* re-offer after reconnect of a transfer we know */
                if(t->state==TS_DONE) file_complete(c,t,1,"");
                else if(t->state==TS_OFFERED && t->accepted && t->hashing) t->reoffer_pending=1;
                else if(t->state==TS_OFFERED && t->accepted && accept_transfer(c,t)){ xfer_fail(c,t,TS_FAILED,"cannot create file"); send_tid_frame(c,fp,7,tid,"cannot create file"); } }
            return; }
        sanitize_filename(name,nm,sizeof nm); free(name);
        if(hasg){ g=group_get(c,gid); if(!g||!group_is_member(g,fp)){ send_tid_frame(c,fp,7,tid,"\xe4\xb8\x8d\xe6\x98\xaf\xe7\xbe\xa4\xe6\x88\x90\xe5\x91\x98"); free(mime); return; } conv_group(gid,conv); } else conv_dm(fp,conv);
        lsm_uuid_str(mid,ms); if(tomb_has(c,ms)){ send_tid_frame(c,fp,7,tid,"\xe5\xaf\xb9\xe6\x96\xb9\xe5\xb7\xb2\xe5\x88\xa0\xe9\x99\xa4\xe8\xbf\x99\xe6\x9d\xa1\xe6\xb6\x88\xe6\x81\xaf"); free(mime); return; } /* 对方已删除这条消息 */
        memset(&nt,0,sizeof nt); memcpy(nt.tid,tid,16); lsm_uuid_str(mid,ms); scpy(nt.msg_id,ms,40); scpy(nt.conv,conv,64); scpy(nt.peer_fp,fp,FP_LEN); scpy(nt.name,nm,sizeof nt.name); nt.size=size; scpy(nt.mime,mime,sizeof nt.mime); free(mime);
        memcpy(nt.sha,sha,32); nt.has_group=hasg; if(hasg) memcpy(nt.group_id,gid,16); nt.offer_ts=ts; nt.state=TS_OFFERED;
        c->xfers=(lsm_transfer*)realloc(c->xfers,sizeof(lsm_transfer)*(c->nxfers+1)); c->xfers[c->nxfers++]=nt; t=&c->xfers[c->nxfers-1];
        { lsm_msg *m=msg_get(c,ms); if(!m){ seen_add(c,mid); m=add_message(c,ms,conv,0,fp,K_FILE,"",ts,ST_RECEIVED); }
          { char u[37]; lsm_uuid_str(tid,u); scpy(m->file_tid,u,40); } scpy(m->file_name,nm,260); m->file_size=size; hist_rec_msg(c,m); }
        if(trust_of(c,fp)!=TRUST_TRUSTED){ t->state=TS_REJECTED; scpy(t->error,"untrusted",sizeof t->error); send_tid_frame(c,fp,7,tid,"\xe5\xaf\xb9\xe6\x96\xb9\xe5\xb0\x9a\xe6\x9c\xaa\xe4\xbf\xa1\xe4\xbb\xbb\xe4\xbd\xa0\xef\xbc\x8c\xe6\x96\x87\xe4\xbb\xb6\xe8\xa2\xab\xe6\x8b\x92\xe6\x94\xb6\xef\xbc\x88\xe8\xaf\xb7\xe5\x85\x88\xe4\xba\x92\xe7\x9b\xb8\xe4\xbf\xa1\xe4\xbb\xbb\xef\xbc\x89");
            notice(c,"\xe5\xb7\xb2\xe6\x8b\x92\xe6\x94\xb6\xe6\x9d\xa5\xe8\x87\xaa\xe6\x9c\xaa\xe4\xbf\xa1\xe4\xbb\xbb\xe8\x81\x94\xe7\xb3\xbb\xe4\xba\xba %s \xe7\x9a\x84\xe6\x96\x87\xe4\xbb\xb6 %s",peer_name(c,fp),nm); emit_xfer(c,t); }
        else if(accept_transfer(c,t)){ t->state=TS_REJECTED; send_tid_frame(c,fp,7,tid,"cannot create file"); emit_xfer(c,t); }
        break; }
    case 6: case 16: if(t && t->out && t->state==TS_OFFERED && !t->hashing){ uint64_t off=0; uint8_t pre[32]; conn *k=conn_by_fp(c,fp);
        if(v==16){ off=r_u64(r); r_fix(r,pre,32); if(r->err) break; }
        t->fp_handle=ufopen(t->path,"rb"); if(!t->fp_handle){ xfer_fail(c,t,TS_FAILED,"open failed"); send_tid_frame(c,fp,12,tid,"open failed"); break; }
        t->resumable=(k && (k->rcaps&CAP_RESUME))?1:0; t->error[0]=0;
        if(off>0 && off<=t->size){ rs_start_prefix(t,off,pre); break; } /* resume.c: chunked prefix check, then ACTIVE at off or 0 */
        t->state=TS_ACTIVE; t->transferred=0; t->credit=0; t->resumed_from=0; t->last_credit_mono=mono_ms(); emit_xfer(c,t); } break;
    case 7: { char *rs=r_str(r); if(t && t->out && !xfer_finished(t)){ t->state=TS_REJECTED; scpy(t->error,rs,sizeof t->error); emit_xfer(c,t); mark_file_failed(c,t); } free(rs); break; }
    case 9: { uint64_t rec=r_u64(r); if(t && t->out){ if(rec>t->credit) t->credit=rec; t->last_credit_mono=mono_ms(); emit_xfer(c,t); } break; }
    case 8: { uint64_t off=r_u64(r); size_t n=0; const uint8_t *d=r_bytes(r,&n);
        if(!t || t->out || t->state!=TS_ACTIVE || !d) break;
        if(off==0 && t->transferred>0){ /* sender could not continue (prefix differs): restart from 0 */
            fclose((FILE*)t->fp_handle); t->fp_handle=ufopen(t->part,"wb"); if(!t->fp_handle){ xfer_fail(c,t,TS_FAILED,"write failed"); file_complete(c,t,0,"write failed"); break; }
            sha256_init((sha256_ctx*)t->hash); t->transferred=0; t->resumed_from=0; t->last_prog=0; }
        if(off!=t->transferred || t->transferred+n>t->size){ xfer_fail(c,t,TS_FAILED,"\xe6\x95\xb0\xe6\x8d\xae\xe5\x9d\x97\xe9\xa1\xba\xe5\xba\x8f\xe9\x94\x99\xe8\xaf\xaf"); file_complete(c,t,0,"\xe6\x95\xb0\xe6\x8d\xae\xe5\x9d\x97\xe9\xa1\xba\xe5\xba\x8f\xe9\x94\x99\xe8\xaf\xaf"); break; }
        if(fwrite(d,1,n,(FILE*)t->fp_handle)!=n){ xfer_fail(c,t,TS_FAILED,"write failed"); file_complete(c,t,0,"write failed (disk full?)"); break; }
        sha256_update((sha256_ctx*)t->hash,d,n); t->transferred+=n;
        if(t->transferred-t->last_prog>=PROGRESS_EVERY || t->transferred==t->size){ t->last_prog=t->transferred; file_progress(c,t); emit_xfer(c,t); }
        break; }
    case 10: if(t && !t->out && t->state==TS_ACTIVE){ uint8_t h[32]; char fin[600];
        t->state=TS_VERIFYING; fclose((FILE*)t->fp_handle); t->fp_handle=NULL; sha256_final((sha256_ctx*)t->hash,h); free(t->hash); t->hash=NULL;
        if(t->transferred!=t->size){ char e[128]; snprintf(e,sizeof e,"\xe6\x96\x87\xe4\xbb\xb6\xe4\xb8\x8d\xe5\xae\x8c\xe6\x95\xb4 (%llu/%llu)",(unsigned long long)t->transferred,(unsigned long long)t->size); t->state=TS_ACTIVE; xfer_fail(c,t,TS_FAILED,e); file_complete(c,t,0,e); break; }
        if(memcmp(h,t->sha,32)){ t->state=TS_ACTIVE; xfer_fail(c,t,TS_FAILED,"SHA-256 \xe6\xa0\xa1\xe9\xaa\x8c\xe5\xa4\xb1\xe8\xb4\xa5"); file_complete(c,t,0,"SHA-256 \xe6\xa0\xa1\xe9\xaa\x8c\xe5\xa4\xb1\xe8\xb4\xa5"); break; }
        unique_path(c,t->name,fin,sizeof fin);
        if(urename(t->part,fin)){ t->state=TS_ACTIVE; xfer_fail(c,t,TS_FAILED,"rename failed"); file_complete(c,t,0,"rename failed"); break; }
        scpy(t->path,fin,sizeof t->path); t->state=TS_DONE; rs_sidecar_remove(t);
        /* persist first, then announce: history record (synced) -> msg_update -> transfer 'done' -> ack to the sender */
        { lsm_msg *m=msg_get(c,t->msg_id); if(m){ scpy(m->file_path,fin,520); hist_rec_upd(c,m); emit_msg(c,"msg_update",m);} }
        emit_xfer(c,t); file_complete(c,t,1,""); } break;
    case 11: { int ok=r_u8(r); char *e=r_str(r); if(t && t->out && !xfer_finished(t)){
        if(ok){ int i, all=1; t->state=TS_DONE; t->transferred=t->size;
            for(i=0;i<c->nxfers;i++) if(c->xfers[i].out && !strcmp(c->xfers[i].msg_id,t->msg_id) && c->xfers[i].state!=TS_DONE) all=0;
            if(all || !t->has_group) set_status(c,msg_get(c,t->msg_id),ST_DELIVERED);  /* persisted before 'done' is emitted */
            emit_xfer(c,t); }
        else { char m[300]; snprintf(m,sizeof m,"\xe5\xaf\xb9\xe6\x96\xb9\xe6\xa0\xa1\xe9\xaa\x8c\xe5\xa4\xb1\xe8\xb4\xa5: %s",e); xfer_fail(c,t,TS_FAILED,m); } }
        free(e); break; }
    case 12: { char *rs=r_str(r); if(t && !xfer_finished(t)){ char m[300]; snprintf(m,sizeof m,"\xe5\xaf\xb9\xe6\x96\xb9\xe5\x8f\x96\xe6\xb6\x88: %s",rs); xfer_fail(c,t,TS_CANCELLED,m); } free(rs); break; }
    } }

/* ------------------------------------------------------------ link up / frames */
static void on_link_up(lsm_core *c, conn *k){
    conn *ex=NULL, *o; lsm_contact *ct; peer_rt *r;
    if(!strncmp(k->dial_key,"fp:",3)){ r=rt_entry(c,k->dial_key+3); r->dialing=0; if(strcmp(k->dial_key+3,k->fp)){ r->failures++; r->next_attempt=mono_ms()+backoff(r->failures); } }
    else if(!strncmp(k->dial_key,"addr:",5)){ int i; for(i=0;i<c->nmanual;i++) if(!strcmp(c->manual[i].addr,k->dial_key+5)){ c->manual[i].dialing=0; c->manual[i].failures=0; scpy(c->manual[i].fp,k->fp,FP_LEN);} }
    k->dial_key[0]=0;
    if(trust_of(c,k->fp)==TRUST_BLOCKED){ conn_close(c,k,"blocked",0); return; }
    for(o=c->conns;o;o=o->next) if(o!=k && o->state==4 && !strcmp(o->fp,k->fp)) ex=o;
    if(ex && (ex->via_relay!=0)!=(k->via_relay!=0)){ /* LAN direct always beats the relay */
        conn *loser=k->via_relay?k:ex; logf_(c,"path %s: %s",k->fp,k->via_relay?"keeping direct link, relay link dropped":"direct link up, leaving relay");
        if(loser==k){ link_drop(c,k); return; }
        /* relay -> direct mid-transfer: bytes queued on the relay link are lost with it, so pause the running
         * transfers (CAP_RESUME) and let resend_offers below re-offer them on the direct link; the receiver
         * resumes from its partial file (same as the Rust core, which pauses on every link replacement) */
        fail_transfers_for(c,k->fp,"\xe8\xbf\x9e\xe6\x8e\xa5\xe8\xa2\xab\xe6\x9b\xbf\xe6\x8d\xa2"); /* 连接被替换 */
        link_drop(c,ex); ex=NULL; }
    if(ex){ /* v0.5 tie-break: prefer links initiated by the smaller fingerprint */
        int me_small=strcmp(c->fp,k->fp)<0; int pref_ex=ex->initiator?me_small:!me_small, pref_new=k->initiator?me_small:!me_small, keep_new;
        if(mono_ms()-ex->last_rx>STALE_TIEBREAK) keep_new=1; else if(pref_ex==pref_new) keep_new=1; else keep_new=pref_new;
        if(!keep_new){ link_drop(c,k); return; }
        fail_transfers_for(c,k->fp,"\xe8\xbf\x9e\xe6\x8e\xa5\xe8\xa2\xab\xe6\x9b\xbf\xe6\x8d\xa2"); link_drop(c,ex); }
    ct=contact_entry(c,k->fp); scpy(ct->name,k->rname,sizeof ct->name); memcpy(ct->ed,k->remote_ed,32); ct->has_ed=1; ct->last_seen_ms=wall_ms(); ct->online=1; ct->rtt_ms=-1; scpy(ct->app_version,k->rver,sizeof ct->app_version);
    if(c->o.auto_trust && ct->trust==TRUST_UNKNOWN) ct->trust=TRUST_TRUSTED;
    ct->via_relay=k->via_relay?1:0;
    if(k->rlisten && !k->via_relay){ struct sockaddr_in a=k->addr; if(!k->initiator) a.sin_port=htons(k->rlisten); addr_str(&a,ct->addr,sizeof ct->addr); }
    save_contacts(c);
    r=rt_entry(c,k->fp); r->dialing=0; r->failures=0; r->next_attempt=0;
    { char as[64]; if(k->via_relay) scpy(as,"relay",sizeof as); else addr_str(&k->addr,as,sizeof as); emit(c,"peer\t%s\tonline\t%s\t%s\t%s",k->fp,ct->name,as,k->rver);
      emit(c,"peer\t%s\tpath\t%s",k->fp,k->via_relay?"relay":"direct"); }
    flush_outbox(c,k->fp); resend_offers(c,k->fp); }

static void on_payload(lsm_core *c, const char *fp, const uint8_t id[16], rd_t *r){
    uint32_t v=r_u32(r); char ids[37]; lsm_uuid_str(id,ids);
    switch(v){
    case 0: { char *body=r_str(r); int64_t ts=(int64_t)r_u64(r); char conv[64]; if(r->err){ free(body); return; } utf8_trunc(body,64*1024*3); conv_dm(fp,conv); add_message(c,ids,conv,0,fp,K_TEXT,body,ts,ST_RECEIVED); free(body); break; }
    case 1: { uint8_t gid[16]; uint64_t ep; size_t sl; const uint8_t *sealed; int64_t ts; lsm_group *g; char conv[64]; size_t pn; uint8_t *pl;
        r_uuid(r,gid); ep=r_u64(r); sealed=r_bytes(r,&sl); ts=(int64_t)r_u64(r); if(r->err) return;
        g=group_get(c,gid); if(!g || !group_is_member(g,fp)) return; conv_group(gid,conv);
        pl=open_rand(g->key,sealed,sl,&pn);
        if(!pl){ system_msg(c,conv,"\xe6\x94\xb6\xe5\x88\xb0\xe6\x9d\xa5\xe8\x87\xaa %s \xe7\x9a\x84\xe6\xb6\x88\xe6\x81\xaf\xe4\xbd\x86\xe6\x97\xa0\xe6\xb3\x95\xe8\xa7\xa3\xe5\xaf\x86 (\xe5\xaf\x86\xe9\x92\xa5\xe7\x89\x88\xe6\x9c\xac %llu \xe2\x89\xa0 %llu)",peer_name(c,fp),(unsigned long long)ep,(unsigned long long)g->key_epoch); return; }
        add_message(c,ids,conv,0,fp,K_TEXT,(char*)pl,ts,ST_RECEIVED); free(pl); break; }
    case 2: { lsm_group g, *ex; char *nm, *cf; uint64_t nm_n, i; size_t sl; const uint8_t *sig; buf_t sb={0}; char conv[64], **raw;
        memset(&g,0,sizeof g); r_uuid(r,g.id); nm=r_str(r); cf=r_str(r); nm_n=r_u64(r); if(r->err||nm_n>64){ free(nm); free(cf); return; }
        raw=(char**)calloc((size_t)nm_n+1,sizeof(char*)); for(i=0;i<nm_n;i++) raw[i]=r_str(r);
        r_fix(r,g.key,32); g.created_ms=(int64_t)r_u64(r); g.key_epoch=r_u64(r); sig=r_bytes(r,&sl);
        g.nmembers=(int)nm_n; for(i=0;i<nm_n;i++) scpy(g.members[i],raw[i],FP_LEN); scpy(g.creator_fp,cf,FP_LEN);
        /* signature over the raw fields */
        sb.n=0; b_put(&sb,g.id,16); b_put(&sb,nm,strlen(nm)); b_u8(&sb,0); b_put(&sb,cf,strlen(cf)); b_u8(&sb,0);
        { char **srt=raw; size_t a,b2; for(a=0;a<nm_n;a++) for(b2=a+1;b2<nm_n;b2++) if(strcmp(srt[a],srt[b2])>0){ char *t=srt[a]; srt[a]=srt[b2]; srt[b2]=t; }
          for(a=0;a<nm_n;a++){ b_put(&sb,srt[a],strlen(srt[a])); b_u8(&sb,0);} }
        b_put(&sb,g.key,32); b_u64(&sb,(uint64_t)g.created_ms);
        if(r->err || strcmp(cf,fp) || !verify_sig(c,fp,&sb,sig,sl)){ logf_(c,"invalid group invite from %s",fp); goto inv_done; }
        if(trust_of(c,fp)!=TRUST_TRUSTED){ notice(c,"\xe5\xbf\xbd\xe7\x95\xa5\xe6\x9d\xa5\xe8\x87\xaa\xe6\x9c\xaa\xe4\xbf\xa1\xe4\xbb\xbb\xe8\x81\x94\xe7\xb3\xbb\xe4\xba\xba %s \xe7\x9a\x84\xe7\xbe\xa4\xe7\xbb\x84\xe9\x82\x80\xe8\xaf\xb7\xe3\x80\x8c%s\xe3\x80\x8d\xef\xbc\x88\xe8\xaf\xb7\xe5\x85\x88\xe4\xbf\xa1\xe4\xbb\xbb\xe5\xaf\xb9\xe6\x96\xb9\xef\xbc\x89",peer_name(c,fp),nm); goto inv_done; }
        if(!group_is_member(&g,c->fp)) goto inv_done;
        sanitize_name(nm,g.name,sizeof g.name);
        ex=group_get(c,g.id); conv_group(g.id,conv);
        if(ex) *ex=g; else { c->groups=(lsm_group*)realloc(c->groups,sizeof(lsm_group)*(c->ngroups+1)); c->groups[c->ngroups++]=g; }
        save_groups(c); { char u[37]; lsm_uuid_str(g.id,u); emit(c,"group\t%s\t%s",u,g.name); }
        if(!ex) system_msg(c,conv,"%s \xe9\x82\x80\xe8\xaf\xb7\xe4\xbd\xa0\xe5\x8a\xa0\xe5\x85\xa5\xe4\xba\x86\xe7\xbe\xa4\xe7\xbb\x84\xe3\x80\x8c%s\xe3\x80\x8d",peer_name(c,fp),nm);
    inv_done:
        for(i=0;i<nm_n;i++) free(raw[i]); free(raw); free(nm); free(cf); b_free(&sb); break; }
    case 3: { uint8_t gid[16], key[32]; uint64_t n, i, ep; char **raw, *rm; size_t sl; const uint8_t *sig; lsm_group *g, tmp; buf_t sb={0}; char conv[64];
        r_uuid(r,gid); n=r_u64(r); if(r->err||n>64) return; raw=(char**)calloc((size_t)n+1,sizeof(char*)); for(i=0;i<n;i++) raw[i]=r_str(r);
        r_fix(r,key,32); ep=r_u64(r); rm=r_str(r); sig=r_bytes(r,&sl);
        g=group_get(c,gid);
        if(g && !r->err){ memset(&tmp,0,sizeof tmp); memcpy(tmp.id,gid,16); tmp.nmembers=(int)n; for(i=0;i<n;i++) scpy(tmp.members[i],raw[i],FP_LEN); memcpy(tmp.key,key,32); tmp.key_epoch=ep;
            rekey_bytes(&sb,&tmp,rm); conv_group(gid,conv);
            if(strcmp(g->creator_fp,fp) || !verify_sig(c,fp,&sb,sig,sl)) logf_(c,"invalid group rekey from %s",fp);
            else if(ep>=g->key_epoch){
                if(!group_is_member(&tmp,c->fp)){ char nm[300], u[37]; scpy(nm,g->name,sizeof nm); lsm_uuid_str(gid,u); group_delete(c,gid); save_groups(c); emit(c,"group_removed\t%s",u); notice(c,"\xe4\xbd\xa0\xe5\xb7\xb2\xe8\xa2\xab\xe7\xa7\xbb\xe5\x87\xba\xe7\xbe\xa4\xe7\xbb\x84\xe3\x80\x8c%s\xe3\x80\x8d",nm); }
                else { char u[37]; g->nmembers=tmp.nmembers; memcpy(g->members,tmp.members,sizeof tmp.members); memcpy(g->key,key,32); g->key_epoch=ep; save_groups(c); lsm_uuid_str(gid,u); emit(c,"group\t%s\t%s",u,g->name);
                    if(rm[0]) system_msg(c,conv,"%s \xe5\xb7\xb2\xe8\xa2\xab\xe7\xa7\xbb\xe5\x87\xba\xe7\xbe\xa4\xe7\xbb\x84\xef\xbc\x8c\xe7\xbe\xa4\xe5\xaf\x86\xe9\x92\xa5\xe5\xb7\xb2\xe6\x9b\xb4\xe6\x96\xb0",peer_name(c,rm)); } } }
        for(i=0;i<n;i++) free(raw[i]); free(raw); free(rm); b_free(&sb); break; }
    case 4: { uint8_t gid[16]; lsm_group *g; char conv[64]; r_uuid(r,gid); if(r->err) return; g=group_get(c,gid); if(!g||!group_is_member(g,fp)) return; conv_group(gid,conv);
        if(!strcmp(g->creator_fp,c->fp)) remove_member_rekey(c,g,fp);
        else { char u[37]; group_remove_member(g,fp); save_groups(c); lsm_uuid_str(gid,u); emit(c,"group\t%s\t%s",u,g->name); }
        system_msg(c,conv,"%s \xe9\x80\x80\xe5\x87\xba\xe4\xba\x86\xe7\xbe\xa4\xe7\xbb\x84",peer_name(c,fp)); break; }
    case 5: { uint64_t n=r_u64(r), i; if(n>100000) return; for(i=0;i<n && !r->err;i++){ uint8_t u[16]; char s[37]; lsm_msg *m; r_uuid(r,u); lsm_uuid_str(u,s); m=msg_get(c,s); if(m && m->out) set_status(c,m,ST_READ); } break; }
    } }

static void on_frame(lsm_core *c, conn *k, rd_t *r){
    uint32_t v=r_u32(r); buf_t f={0};
    if(r->err) return;
    switch(v){
    case 0: { uint64_t ts=r_u64(r); b_u32(&f,1); b_u64(&f,ts); send_frame(k,&f); break; }
    case 1: { int64_t ts=(int64_t)r_u64(r); lsm_contact *ct=contact_get(c,k->fp); if(ct){ int64_t rtt=wall_ms()-ts; if(rtt<0) rtt=0; if(rtt>60000) rtt=60000; ct->rtt_ms=(int)rtt; } break; }
    case 2: { char *n=r_str(r); lsm_contact *ct=contact_entry(c,k->fp); sanitize_name(n,ct->name,sizeof ct->name); free(n); save_contacts(c); emit(c,"peer\t%s\tname\t%s",k->fp,ct->name); break; }
    case 3: { uint8_t id[16]; r_uuid(r,id); if(r->err) break; if(!seen_has(c,id)){ seen_add(c,id); on_payload(c,k->fp,id,r); } b_u32(&f,4); b_uuid(&f,id); send_frame(k,&f); break; }
    case 4: { uint8_t id[16]; r_uuid(r,id); if(!r->err) on_ack(c,k->fp,id); break; }
    case 13: { char *pin=r_str(r); int ok=0; char fp[FP_LEN];
        scpy(fp,k->fp,FP_LEN);
        if(c->pin[0] && mono_ms()-c->pin_time<300000 && !strcmp(c->pin,pin)) ok=1; else if(c->pin[0]){ if(++c->pin_tries>=5) c->pin[0]=0; }
        b_u32(&f,14); b_u8(&f,(uint8_t)ok); send_frame(k,&f);
        if(ok){ lsm_contact *ct=contact_entry(c,fp); c->pin[0]=0; ct->trust=TRUST_TRUSTED; save_contacts(c); emit(c,"peer\t%s\ttrust\ttrusted",fp); notice(c,"\xe5\xb7\xb2\xe9\x80\x9a\xe8\xbf\x87 PIN \xe4\xb8\x8e %s \xe9\x85\x8d\xe5\xaf\xb9\xe5\xb9\xb6\xe4\xbf\xa1\xe4\xbb\xbb",peer_name(c,fp)); }
        free(pin); break; }
    case 14: { int ok=r_u8(r); if(ok){ lsm_contact *ct=contact_entry(c,k->fp); ct->trust=TRUST_TRUSTED; save_contacts(c); emit(c,"peer\t%s\ttrust\ttrusted",k->fp); }
        emit(c,"pin_result\t%s\t%s",k->fp,ok?"ok":"fail"); break; }
    case 15: conn_close(c,k,"peer closed",0); break;
    case 5: case 6: case 7: case 8: case 9: case 10: case 11: case 12: case 16: on_file_frame(c,k->fp,v,r); break;
    default: break; }
    b_free(&f); }

/* ------------------------------------------------------------ timers + loop */
static void on_tick(lsm_core *c){
    int64_t now=mono_ms(); conn *k; int i;
    for(k=c->conns;k;k=k->next){
        if(k->state==4){ if(now-k->last_rx>=DEAD_AFTER){ conn_close(c,k,"heartbeat timeout",0); continue; }
            if(now-k->last_ping>=PING_EVERY){ buf_t f={0}; k->last_ping=now; b_u32(&f,0); b_u64(&f,(uint64_t)wall_ms()); send_frame(k,&f); b_free(&f); } }
        else if(k->state!=9 && now-k->started>HS_TIMEOUT) conn_close(c,k,"handshake timeout",0); }
    /* trusted contacts never seen in this run: seed rt entries first, so the very first tick after
       start-up already dials their remembered addresses (fast resume after a restart) */
    for(i=0;i<c->ncontacts;i++) if(c->contacts[i].trust==TRUST_TRUSTED && c->contacts[i].addr[0]) rt_entry(c,c->contacts[i].fp);
    /* redial peers: trusted, recently announced, or with pending outbox */
    for(i=0;i<c->nrt;i++){ peer_rt *r=&c->rt[i]; lsm_contact *ct=contact_get(c,r->fp); int trusted=ct&&ct->trust==TRUST_TRUSTED, recent=r->last_announce&&now-r->last_announce<60000, has_out=0, j; struct sockaddr_in a; char key[80];
        if(conn_direct_by_fp(c,r->fp) || r->dialing) continue; if(ct && ct->trust==TRUST_BLOCKED) continue;
        if(conn_by_fp(c,r->fp) && !recent) continue; /* on the relay: try direct only when the peer is announcing on the LAN */
        for(j=0;j<c->noutbox;j++) if(!strcmp(c->outbox[j].fp,r->fp)) has_out=1;
        if(!(trusted||recent||has_out)) continue;
        if(r->next_attempt && now<r->next_attempt) continue;
        if(recent && r->has_dial) a=r->dial; else if(ct && ct->addr[0] && !resolve(ct->addr,41235,&a)) ; else { r->next_attempt=now+10000; continue; }
        /* simultaneous-dial damping: the larger fingerprint waits briefly so the preferred link wins */
        r->dialing=1; r->next_attempt=now+10000; snprintf(key,sizeof key,"fp:%s",r->fp); dial(c,&a,key);
        { conn *x; int started=0; for(x=c->conns;x;x=x->next) if(x->state!=9 && !strcmp(x->dial_key,key)) started=1; if(!started) r->dialing=0; } }
    for(i=0;i<c->nmanual;i++){ manual_rt *m=&c->manual[i]; char key[80];
        if(m->dialing) continue; if(m->fp[0] && conn_by_fp(c,m->fp)){ m->next_attempt=now+5000; continue; }
        if(m->next_attempt && now<m->next_attempt) continue;
        m->dialing=1; m->next_attempt=now+5000; snprintf(key,sizeof key,"addr:%s",m->addr); dial(c,&m->sa,key);
        { conn *x; int started=0; for(x=c->conns;x;x=x->next) if(x->state!=9 && !strcmp(x->dial_key,key)) started=1; if(!started){ m->dialing=0; m->failures++; } } }
    if(!c->o.no_discovery){
        if(now-c->last_enum>20000){ c->last_enum=now; enum_ifaces(c); }
        if(now-c->last_announce>=c->o.announce_ms){ c->last_announce=now; send_announces(c); } }
    else if(c->announce_targets[0] || c->nmanual){ if(now-c->last_announce>=c->o.announce_ms){ c->last_announce=now; send_announces(c);} }
    rl_tick(c);
}

static void reap(lsm_core *c){ conn **pp=&c->conns; while(*pp){ conn *k=*pp; if(k->state==9){ *pp=k->next; b_free(&k->in); b_free(&k->out); crypto_wipe(k,sizeof *k); free(k); } else pp=&k->next; } }

static void loop_once(lsm_core *c){
    fd_set rs, ws, es; struct timeval tv; conn *k; sock_t mx=0; int n;
    FD_ZERO(&rs); FD_ZERO(&ws); FD_ZERO(&es);
    mu_lock(&c->mu);
#define ADD(s,set) do{ FD_SET(s,set); if((s)>mx) mx=(s); }while(0)
    if(c->lsock!=BADSOCK) ADD(c->lsock,&rs);
    if(c->usock!=BADSOCK) ADD(c->usock,&rs);
    if(c->ufallback!=BADSOCK) ADD(c->ufallback,&rs);
    for(k=c->conns;k;k=k->next){ if(k->state==9 || k->via_relay) continue; if(k->state!=1) ADD(k->s,&rs); if(k->state==1||k->out.n) ADD(k->s,&ws); if(k->state==1) FD_SET(k->s,&es); }
    if(c->rl->s!=BADSOCK){ if(c->rl->st!=RS_CONNECTING) ADD(c->rl->s,&rs); if(c->rl->st==RS_CONNECTING||c->rl->out.n) ADD(c->rl->s,&ws); if(c->rl->st==RS_CONNECTING) FD_SET(c->rl->s,&es); }
    mu_unlock(&c->mu);
    tv.tv_sec=0; tv.tv_usec=c->hash_busy?0:50000;
#ifdef _WIN32
    if(rs.fd_count==0 && ws.fd_count==0){ if(!c->hash_busy) Sleep(50); n=0; } else
#endif
    n=select((int)mx+1,&rs,&ws,&es,&tv); /* Winsock reports a failed non-blocking connect ONLY in exceptfds */
    mu_lock(&c->mu);
    if(n>0){
        if(c->lsock!=BADSOCK && FD_ISSET(c->lsock,&rs)){ struct sockaddr_in a; socklen_t al=sizeof a; sock_t s=accept(c->lsock,(struct sockaddr*)&a,&al); if(s!=BADSOCK){ set_nonblock(s); conn_start(c,s,&a,0,NULL); } }
        { sock_t us[2]; int u; us[0]=c->usock; us[1]=c->ufallback;
          for(u=0;u<2;u++) if(us[u]!=BADSOCK && FD_ISSET(us[u],&rs)){ uint8_t b[2048]; struct sockaddr_in a; socklen_t al=sizeof a; int got=recvfrom(us[u],(char*)b,sizeof b,0,(struct sockaddr*)&a,&al); if(got>0) on_udp(c,b,(size_t)got,&a); } }
        if(c->rl->s!=BADSOCK){ sock_t rs_=c->rl->s;
            if(c->rl->st==RS_CONNECTING && FD_ISSET(rs_,&es)) rl_fail(c,"error","\xe6\x97\xa0\xe6\xb3\x95\xe8\xbf\x9e\xe6\x8e\xa5\xe6\x9c\x8d\xe5\x8a\xa1\xe5\x99\xa8");
            else { if(FD_ISSET(rs_,&ws)) rl_writable(c); if(c->rl->s==rs_ && c->rl->st!=RS_CONNECTING && FD_ISSET(rs_,&rs)) rl_readable(c); } }
        for(k=c->conns;k;k=k->next){ if(k->state==9 || k->via_relay) continue;
            if(k->state==1 && FD_ISSET(k->s,&es)){ conn_close(c,k,"connect failed",0); continue; }
            if(FD_ISSET(k->s,&ws)) conn_writable(c,k);
            if(k->state!=9 && k->state!=1 && FD_ISSET(k->s,&rs)) conn_readable(c,k); } }
    on_tick(c);
    rs_pump(c);
    pump_files(c);
    for(k=c->conns;k;k=k->next) if(k->state!=9 && k->state!=1 && k->out.n && !k->via_relay) conn_writable(c,k);
    rl_pump(c); if(c->rl->s!=BADSOCK && c->rl->out.n && c->rl->st!=RS_CONNECTING) rl_writable(c);
    reap(c);
    mu_unlock(&c->mu); }

#ifdef _WIN32
static DWORD WINAPI thread_main(LPVOID p){ lsm_core *c=(lsm_core*)p; while(!c->stop) loop_once(c); return 0; }
#else
static void *thread_main(void *p){ lsm_core *c=(lsm_core*)p; while(!c->stop) loop_once(c); return NULL; }
#endif

lsm_core *lsm_start(const lsm_opts *o, char *err, size_t errn){
    lsm_core *c=(lsm_core*)calloc(1,sizeof(lsm_core)); struct sockaddr_in a; int one=1; char p[600];
#ifdef _WIN32
    WSADATA w; if(WSAStartup(MAKEWORD(2,2),&w)){ scpy(err,"WSAStartup failed",errn); free(c); return NULL; }
#else
    /* a peer or the relay server closing mid-send must surface as EPIPE, not kill the process */
    signal(SIGPIPE, SIG_IGN);
#endif
    mu_init(&c->mu); c->o=*o; if(!c->o.discovery_port) c->o.discovery_port=41234; if(!c->o.listen_port) c->o.listen_port=41235; if(!c->o.announce_ms) c->o.announce_ms=3000;
    scpy(c->data_dir,o->data_dir,sizeof c->data_dir); umkdir(c->data_dir); path_join(c->dl_dir,sizeof c->dl_dir,c->data_dir,"downloads"); umkdir(c->dl_dir);
    if(o->announce_targets) scpy(c->announce_targets,o->announce_targets,sizeof c->announce_targets);
    c->ev_cap=4096; c->events=(char**)calloc(c->ev_cap,sizeof(char*));
    c->seen=(uint8_t(*)[16])malloc(16*20000);
    c->lsock=c->usock=c->ufallback=BADSOCK;
    if(load_identity(c)){ snprintf(err,errn,"cannot write identity in %s",c->data_dir); free(c); return NULL; }
    load_contacts(c); load_groups(c); load_history(c); load_outbox(c); rl_init(c); rs_restore(c);
    { char *t=load_sealed(c,"manual.dat"), *l, *nx; for(l=t; l && *l; l=nx){ manual_rt m; nx=strchr(l,'\n'); if(nx) *nx++=0; memset(&m,0,sizeof m); if(resolve(l,41235,&m.sa)) continue; scpy(m.addr,l,sizeof m.addr);
        c->manual=(manual_rt*)realloc(c->manual,sizeof(manual_rt)*(c->nmanual+1)); c->manual[c->nmanual++]=m; } free(t); }
    { int i; for(i=0;i<c->ncontacts;i++){ c->contacts[i].online=0; c->contacts[i].rtt_ms=-1; } }
    /* TCP listener: preferred port, fallback ephemeral */
    c->lsock=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    memset(&a,0,sizeof a); a.sin_family=AF_INET; a.sin_addr.s_addr=INADDR_ANY; a.sin_port=htons((unsigned short)c->o.listen_port);
#ifndef _WIN32
    setsockopt(c->lsock,SOL_SOCKET,SO_REUSEADDR,(const char*)&one,sizeof one);
#endif
    if(bind(c->lsock,(struct sockaddr*)&a,sizeof a)){ a.sin_port=0; if(bind(c->lsock,(struct sockaddr*)&a,sizeof a)){ scpy(err,"cannot bind TCP listener",errn); return NULL; } }
    listen(c->lsock,16); set_nonblock(c->lsock);
    { socklen_t al=sizeof a; getsockname(c->lsock,(struct sockaddr*)&a,&al); c->lport=ntohs(a.sin_port); }
    /* UDP discovery receiver */
    if(!c->o.no_discovery){ c->usock=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
#ifndef _WIN32
        setsockopt(c->usock,SOL_SOCKET,SO_REUSEADDR,(const char*)&one,sizeof one);
#endif
        setsockopt(c->usock,SOL_SOCKET,SO_BROADCAST,(const char*)&one,sizeof one);
        a.sin_port=htons((unsigned short)c->o.discovery_port); a.sin_addr.s_addr=INADDR_ANY;
        if(bind(c->usock,(struct sockaddr*)&a,sizeof a)){ closesock(c->usock); c->usock=BADSOCK; emit(c,"notice\tUDP %d \xe7\xbb\x91\xe5\xae\x9a\xe5\xa4\xb1\xe8\xb4\xa5\xef\xbc\x8c\xe8\xaf\xb7\xe7\x94\xa8\xe6\x8c\x89 IP \xe6\xb7\xbb\xe5\x8a\xa0",c->o.discovery_port); }
        else set_nonblock(c->usock); }
    c->ufallback=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP); if(c->ufallback!=BADSOCK){ setsockopt(c->ufallback,SOL_SOCKET,SO_BROADCAST,(const char*)&one,sizeof one); set_nonblock(c->ufallback); }
    c->last_enum=-100000; c->last_announce=-100000;
    (void)p;
    emit(c,"ready\t%s\t%s\t%d\t%d",c->fp,c->name,c->lport,c->o.discovery_port);
#ifdef _WIN32
    c->th=CreateThread(NULL,0,thread_main,c,0,NULL);
#else
    pthread_create(&c->th,NULL,thread_main,c);
#endif
    return c; }

void lsm_stop(lsm_core *c){ conn *k; mu_lock(&c->mu); for(k=c->conns;k;k=k->next) if(k->state==4) conn_close(c,k,"shutdown",1); mu_unlock(&c->mu); c->stop=1;
#ifdef _WIN32
    WaitForSingleObject(c->th,2000);
#else
    pthread_join(c->th,NULL);
#endif
}

/* ------------------------------------------------------------ commands */
#define E(s) do{ if(err) scpy(err,s,256); }while(0)
const char *lsm_resolve_peer(lsm_core *c, const char *q){ int i, hits=0; const char *r=NULL; size_t ql=strlen(q);
    for(i=0;i<c->ncontacts;i++){ lsm_contact *k=&c->contacts[i]; if(!strcmp(k->fp,q)||!strcmp(k->name,q)) return k->fp; }
    for(i=0;i<c->ncontacts;i++){ lsm_contact *k=&c->contacts[i]; if(ql>=4 && !strncmp(k->fp,q,ql)){ hits++; r=k->fp; } }
    return hits==1?r:NULL; }
const char *lsm_resolve_group(lsm_core *c, const char *q, char out[37]){ int i; for(i=0;i<c->ngroups;i++){ lsm_uuid_str(c->groups[i].id,out); if(!strcmp(out,q)||!strcmp(c->groups[i].name,q)||(strlen(q)>=4&&!strncmp(out,q,strlen(q)))) return out; } return NULL; }

int lsm_send_text(lsm_core *c, const char *conv, const char *text, char *msgid_out, char *err){
    uint8_t id[16]; char ids[37]; buf_t p={0}; lsm_msg *m; int rc=0; int64_t ts=wall_ms();
    if(!text[0]){ E("empty"); return -1; }
    mu_lock(&c->mu); uuid_new(id); lsm_uuid_str(id,ids);
    if(!strncmp(conv,"dm:",3)){ int online; b_u32(&p,0); b_str(&p,text); b_u64(&p,(uint64_t)ts);
        m=add_message(c,ids,conv,1,c->fp,K_TEXT,text,ts,ST_QUEUED); online=send_reliable(c,conv+3,&p,id,ids); if(online) set_status(c,msg_get(c,ids),ST_SENT); }
    else if(!strncmp(conv,"group:",6)){ uint8_t gid[16]; lsm_group *g; size_t sn; uint8_t *sealed; int i, any=0;
        if(lsm_uuid_parse(conv+6,gid)||!(g=group_get(c,gid))){ E("\xe7\xbe\xa4\xe7\xbb\x84\xe4\xb8\x8d\xe5\xad\x98\xe5\x9c\xa8"); rc=-1; goto out; }
        sealed=seal_rand(g->key,(const uint8_t*)text,strlen(text),&sn);
        b_u32(&p,1); b_uuid(&p,gid); b_u64(&p,g->key_epoch); b_bytes(&p,sealed,sn); b_u64(&p,(uint64_t)ts); free(sealed);
        m=add_message(c,ids,conv,1,c->fp,K_TEXT,text,ts,ST_QUEUED);
        for(i=0;i<g->nmembers;i++) if(strcmp(g->members[i],c->fp)) any|=send_reliable(c,g->members[i],&p,id,ids);
        if(any) set_status(c,msg_get(c,ids),ST_SENT); else if(g->nmembers<=1) set_status(c,msg_get(c,ids),ST_DELIVERED); }
    else { E("bad conversation"); rc=-1; }
    (void)m;
    if(!rc && msgid_out) scpy(msgid_out,ids,37);
out: b_free(&p); mu_unlock(&c->mu); return rc; }

int lsm_send_file(lsm_core *c, const char *conv, const char *path, char *err){
    FILE *f=ufopen(path,"rb"); uint8_t sha[32], *buf; sha256_ctx h; size_t n; uint64_t size=0; char nm[260], ids[37]; uint8_t mid[16]; lsm_transfer t; int rc=0; int64_t ts=wall_ms();
    const char *targets[64]; int nt=0, i; lsm_group *g=NULL; uint8_t gid[16]; lsm_msg *m;
    if(!f){ E("\xe6\x97\xa0\xe6\xb3\x95\xe6\x89\x93\xe5\xbc\x80\xe6\x96\x87\xe4\xbb\xb6"); return -1; }
    buf=(uint8_t*)malloc(65536); sha256_init(&h); while((n=fread(buf,1,65536,f))>0){ sha256_update(&h,buf,n); size+=n; } fclose(f); free(buf); sha256_final(&h,sha);
    sanitize_filename(path,nm,sizeof nm);
    mu_lock(&c->mu);
    if(!strncmp(conv,"dm:",3)) targets[nt++]=conv+3;
    else if(!strncmp(conv,"group:",6) && !lsm_uuid_parse(conv+6,gid) && (g=group_get(c,gid))){ for(i=0;i<g->nmembers && nt<64;i++) if(strcmp(g->members[i],c->fp)) targets[nt++]=g->members[i]; }
    else { E("bad conversation"); rc=-1; goto out; }
    uuid_new(mid); lsm_uuid_str(mid,ids);
    m=add_message(c,ids,conv,1,c->fp,K_FILE,"",ts,ST_SENT); scpy(m->file_name,nm,260); m->file_size=size; scpy(m->file_path,path,520); hist_rec_msg(c,m); /* persist name/size: the first record was written before they were set */
    for(i=0;i<nt;i++){ memset(&t,0,sizeof t); uuid_new(t.tid); scpy(t.msg_id,ids,40); scpy(t.conv,conv,64); scpy(t.peer_fp,targets[i],FP_LEN); t.out=1; scpy(t.name,nm,sizeof t.name); t.size=size; scpy(t.mime,mime_guess(nm),sizeof t.mime); memcpy(t.sha,sha,32);
        if(g){ t.has_group=1; memcpy(t.group_id,gid,16); } scpy(t.path,path,sizeof t.path); t.offer_ts=ts; t.state=TS_OFFERED;
        if(i==0){ char u[37]; lsm_uuid_str(t.tid,u); scpy(m->file_tid,u,40); }
        c->xfers=(lsm_transfer*)realloc(c->xfers,sizeof(lsm_transfer)*(c->nxfers+1)); c->xfers[c->nxfers++]=t;
        rs_out_write(c,&c->xfers[c->nxfers-1]); send_offer(c,&c->xfers[c->nxfers-1]); emit_xfer(c,&c->xfers[c->nxfers-1]); }
    hist_rec_msg(c,msg_get(c,ids));
out: mu_unlock(&c->mu); return rc; }

int lsm_add_addr(lsm_core *c, const char *addr, char *err){ struct sockaddr_in a; manual_rt m; int i;
    if(resolve(addr,41235,&a)){ E("\xe5\x9c\xb0\xe5\x9d\x80\xe6\x97\xa0\xe6\x95\x88"); return -1; }
    mu_lock(&c->mu); memset(&m,0,sizeof m); addr_str(&a,m.addr,sizeof m.addr); m.sa=a;
    for(i=0;i<c->nmanual;i++) if(!strcmp(c->manual[i].addr,m.addr)){ c->manual[i].next_attempt=0; mu_unlock(&c->mu); return 0; }
    c->manual=(manual_rt*)realloc(c->manual,sizeof(manual_rt)*(c->nmanual+1)); c->manual[c->nmanual++]=m;
    { buf_t b={0}; for(i=0;i<c->nmanual;i++) bfmt(&b,"%s\n",c->manual[i].addr); save_sealed(c,"manual.dat",&b); b_free(&b); } /* persisted like v0.5 contacts.manual */
    mu_unlock(&c->mu); return 0; }
int lsm_set_trust(lsm_core *c, const char *fp, int trust, char *err){ lsm_contact *k; (void)err; mu_lock(&c->mu); k=contact_entry(c,fp); k->trust=trust; save_contacts(c);
    if(trust==TRUST_BLOCKED){ conn *x=conn_by_fp(c,fp); if(x) conn_close(c,x,"blocked",1); }
    emit(c,"peer\t%s\ttrust\t%s",fp,trust==1?"trusted":trust==2?"blocked":"unknown"); mu_unlock(&c->mu); return 0; }
int lsm_pin_offer(lsm_core *c, char pin_out[8]){ uint32_t r; mu_lock(&c->mu); rand_bytes(&r,4); snprintf(c->pin,sizeof c->pin,"%06u",r%1000000u); c->pin_time=mono_ms(); c->pin_tries=0; scpy(pin_out,c->pin,8); mu_unlock(&c->mu); return 0; }
int lsm_pin_pair(lsm_core *c, const char *fp, const char *pin, char *err){ buf_t f={0}; int ok; char t[16]; scpy(t,pin,sizeof t); { char *s=t, *e; while(*s==' ') s++; e=s+strlen(s); while(e>s&&e[-1]==' ') e--; *e=0; memmove(t,s,strlen(s)+1); }
    mu_lock(&c->mu); b_u32(&f,13); b_str(&f,t); ok=send_to_fp(c,fp,&f); b_free(&f); mu_unlock(&c->mu); if(!ok){ E("\xe5\xaf\xb9\xe6\x96\xb9\xe4\xb8\x8d\xe5\x9c\xa8\xe7\xba\xbf"); return -1; } return 0; }
int lsm_group_create(lsm_core *c, const char *name, const char **fps, int n, char *gid_out, char *err){ lsm_group g; int i; buf_t p={0}; uint8_t id[16]; char conv[64];
    mu_lock(&c->mu);
    for(i=0;i<n;i++) if(trust_of(c,fps[i])!=TRUST_TRUSTED){ char e[256]; snprintf(e,sizeof e,"\xe6\x88\x90\xe5\x91\x98 %s \xe5\xb0\x9a\xe6\x9c\xaa\xe4\xbf\xa1\xe4\xbb\xbb\xef\xbc\x8c\xe8\xaf\xb7\xe5\x85\x88\xe4\xbf\xa1\xe4\xbb\xbb\xe5\x90\x8e\xe5\x86\x8d\xe5\xbb\xba\xe7\xbe\xa4",peer_name(c,fps[i])); E(e); mu_unlock(&c->mu); return -1; }
    memset(&g,0,sizeof g); uuid_new(g.id); sanitize_name(name,g.name,sizeof g.name); scpy(g.creator_fp,c->fp,FP_LEN);
    for(i=0;i<n && i<63;i++) scpy(g.members[g.nmembers++],fps[i],FP_LEN); scpy(g.members[g.nmembers++],c->fp,FP_LEN); group_sort(&g);
    rand_bytes(g.key,32); g.created_ms=wall_ms(); g.key_epoch=0;
    c->groups=(lsm_group*)realloc(c->groups,sizeof(lsm_group)*(c->ngroups+1)); c->groups[c->ngroups++]=g; save_groups(c);
    payload_invite(c,&g,&p); for(i=0;i<g.nmembers;i++) if(strcmp(g.members[i],c->fp)){ uuid_new(id); send_reliable(c,g.members[i],&p,id,NULL); }
    b_free(&p); lsm_uuid_str(g.id,gid_out); emit(c,"group\t%s\t%s",gid_out,g.name); conv_group(g.id,conv);
    system_msg(c,conv,"\xe4\xbd\xa0\xe5\x88\x9b\xe5\xbb\xba\xe4\xba\x86\xe7\xbe\xa4\xe7\xbb\x84\xe3\x80\x8c%s\xe3\x80\x8d",g.name);
    mu_unlock(&c->mu); return 0; }
int lsm_group_invite(lsm_core *c, const char *gids, const char **fps, int n, char *err){ uint8_t gid[16]; lsm_group *g; int i; buf_t p={0}; uint8_t id[16];
    mu_lock(&c->mu); if(lsm_uuid_parse(gids,gid)||!(g=group_get(c,gid))){ E("\xe7\xbe\xa4\xe7\xbb\x84\xe4\xb8\x8d\xe5\xad\x98\xe5\x9c\xa8"); mu_unlock(&c->mu); return -1; }
    if(strcmp(g->creator_fp,c->fp)){ E("\xe5\x8f\xaa\xe6\x9c\x89\xe7\xbe\xa4\xe4\xb8\xbb\xe5\x8f\xaf\xe4\xbb\xa5\xe9\x82\x80\xe8\xaf\xb7\xe6\x88\x90\xe5\x91\x98"); mu_unlock(&c->mu); return -1; }
    for(i=0;i<n;i++){ if(trust_of(c,fps[i])!=TRUST_TRUSTED){ E("\xe6\x88\x90\xe5\x91\x98\xe5\xb0\x9a\xe6\x9c\xaa\xe4\xbf\xa1\xe4\xbb\xbb"); mu_unlock(&c->mu); return -1; } if(!group_is_member(g,fps[i]) && g->nmembers<64) scpy(g->members[g->nmembers++],fps[i],FP_LEN); }
    group_sort(g); g->key_epoch++; save_groups(c);
    payload_invite(c,g,&p); for(i=0;i<g->nmembers;i++) if(strcmp(g->members[i],c->fp)){ uuid_new(id); send_reliable(c,g->members[i],&p,id,NULL); }
    b_free(&p); emit(c,"group\t%s\t%s",gids,g->name); mu_unlock(&c->mu); return 0; }
int lsm_group_remove(lsm_core *c, const char *gids, const char *fp, char *err){ uint8_t gid[16]; lsm_group *g;
    mu_lock(&c->mu); if(lsm_uuid_parse(gids,gid)||!(g=group_get(c,gid))){ E("group not found"); mu_unlock(&c->mu); return -1; }
    if(strcmp(g->creator_fp,c->fp)){ E("only creator can remove members"); mu_unlock(&c->mu); return -1; }
    if(!strcmp(fp,c->fp)||!group_is_member(g,fp)){ E("not a member"); mu_unlock(&c->mu); return -1; }
    remove_member_rekey(c,g,fp); mu_unlock(&c->mu); return 0; }
int lsm_group_leave(lsm_core *c, const char *gids, char *err){ uint8_t gid[16], id[16]; lsm_group *g; int i; buf_t p={0};
    mu_lock(&c->mu); if(lsm_uuid_parse(gids,gid)||!(g=group_get(c,gid))){ E("\xe7\xbe\xa4\xe7\xbb\x84\xe4\xb8\x8d\xe5\xad\x98\xe5\x9c\xa8"); mu_unlock(&c->mu); return -1; }
    b_u32(&p,4); b_uuid(&p,gid); for(i=0;i<g->nmembers;i++) if(strcmp(g->members[i],c->fp)){ uuid_new(id); send_reliable(c,g->members[i],&p,id,NULL); }
    b_free(&p); group_delete(c,gid); save_groups(c); emit(c,"group_removed\t%s",gids); mu_unlock(&c->mu); return 0; }
/* delete one message on this device only (the peer keeps its copy); leaves a synced tombstone record */
int lsm_delete_message(lsm_core *c, const char *id){ char conv[64]; lsm_msg *m; buf_t b={0}; mu_lock(&c->mu); m=msg_get(c,id);
    if(!m){ mu_unlock(&c->mu); return -1; } scpy(conv,m->conv,64); msg_unlink(c,id); tomb_add(c,id);
    bfmt(&b,"D\t%s",id); hist_append(c,&b); b_free(&b); emit(c,"msg_delete\t%s\t%s",id,conv); mu_unlock(&c->mu); return 0; }
int lsm_mark_read(lsm_core *c, const char *conv){ int i, n=0; buf_t p={0}, f={0}; uint8_t id[16];
    mu_lock(&c->mu);
    /* group the unread incoming ids by sender, one Read receipt per sender */
    for(i=0;i<c->nmsgs;i++){ lsm_msg *m=&c->msgs[i]; if(!m->unread || strcmp(m->conv,conv)) continue; m->unread=0; hist_rec_upd(c,m); n++; }
    if(n && !strncmp(conv,"dm:",3)){ int j; /* v0.5 sends Read receipts for DMs only */ for(j=0;j<c->ncontacts;j++){ const char *fp=c->contacts[j].fp; uint64_t cnt=0; p.n=0; b_u32(&p,5); b_u64(&p,0);
            for(i=0;i<c->nmsgs;i++){ lsm_msg *m=&c->msgs[i]; uint8_t u[16]; if(m->out||m->kind==K_SYSTEM||strcmp(m->conv,conv)||strcmp(m->sender_fp,fp)||m->status!=ST_RECEIVED) continue; if(lsm_uuid_parse(m->id,u)) continue; b_uuid(&p,u); cnt++; m->status=ST_READ; hist_rec_upd(c,m); }
            if(cnt){ int k2; for(k2=0;k2<8;k2++) p.p[4+k2]=(uint8_t)(cnt>>(8*k2)); uuid_new(id); frame_msg(&f,id,&p); send_to_fp(c,fp,&f); } } }
    b_free(&p); b_free(&f); mu_unlock(&c->mu); return n; }
int lsm_set_name(lsm_core *c, const char *name){ conn *k; buf_t f={0}; char p[600]; mu_lock(&c->mu); sanitize_name(name,c->name,sizeof c->name); path_join(p,sizeof p,c->data_dir,"name.txt"); write_atomic(p,c->name,strlen(c->name));
    b_u32(&f,2); b_str(&f,c->name); for(k=c->conns;k;k=k->next) if(k->state==4) send_frame(k,&f); b_free(&f); mu_unlock(&c->mu); return 0; }
int lsm_transfer_cancel(lsm_core *c, const char *tids, char *err){ uint8_t tid[16]; lsm_transfer *t; mu_lock(&c->mu);
    if(lsm_uuid_parse(tids,tid)||!(t=xfer_get(c,tid))){ E("no such transfer"); mu_unlock(&c->mu); return -1; }
    send_tid_frame(c,t->peer_fp,12,tid,"\xe5\xb7\xb2\xe5\x8f\x96\xe6\xb6\x88"); xfer_fail(c,t,TS_CANCELLED,"\xe5\xb7\xb2\xe5\x8f\x96\xe6\xb6\x88"); mu_unlock(&c->mu); return 0; }
int lsm_drop_links(lsm_core *c){ conn *k; int n=0; mu_lock(&c->mu); for(k=c->conns;k;k=k->next) if(k->state==4){ conn_close(c,k,"killed (test)",0); n++; } mu_unlock(&c->mu); return n; }

/* ------------------------------------------------------------ firewall */
int lsm_firewall_setup(const char *exe, char *report, size_t n){
#ifdef _WIN32
    OSVERSIONINFOA v; char cmd[1400]; int rc; memset(&v,0,sizeof v); v.dwOSVersionInfoSize=sizeof v; GetVersionExA(&v);
    { char ansi[MAX_PATH*2]; wchar_t *w=widen(exe); WideCharToMultiByte(CP_ACP,0,w,-1,ansi,sizeof ansi,NULL,NULL); free(w);
      if(v.dwMajorVersion<6) snprintf(cmd,sizeof cmd,"netsh firewall add allowedprogram program=\"%s\" name=\"LanMessenger XP\" mode=ENABLE scope=SUBNET",ansi);
      else snprintf(cmd,sizeof cmd,"netsh advfirewall firewall add rule name=\"LanMessenger XP\" dir=in action=allow program=\"%s\" enable=yes profile=private,domain",ansi); }
    { STARTUPINFOA si; PROCESS_INFORMATION pi; DWORD ec=1; memset(&si,0,sizeof si); si.cb=sizeof si; si.dwFlags=STARTF_USESHOWWINDOW; si.wShowWindow=SW_HIDE;
      if(CreateProcessA(NULL,cmd,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi)){ WaitForSingleObject(pi.hProcess,15000); GetExitCodeProcess(pi.hProcess,&ec); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
      rc=(int)ec; }
    snprintf(report,n,"Windows %lu.%lu -> %s (exit %d)",v.dwMajorVersion,v.dwMinorVersion,cmd,rc); return rc;
#else
#if defined(__ANDROID__)
    snprintf(report,n,"Android: no host firewall to configure (%s)",exe); return 0;
#else
    /* Linux: ufw (Debian/Ubuntu) or firewalld (Fedora/RHEL/openSUSE). Not root -> pkexec prompt.
     * LSM_FIREWALL_DRYRUN=1 only reports the command (used by tests). */
    { const char *tool=NULL; char cmd[600]; int rc; const char *pre=geteuid()==0?"":"pkexec ";
      if(system("command -v ufw >/dev/null 2>&1")==0) tool="ufw"; else if(system("command -v firewall-cmd >/dev/null 2>&1")==0) tool="firewalld";
      if(!tool){ snprintf(report,n,"no ufw or firewalld found; nothing to do (open TCP 41235 and UDP 41234 manually if you use another firewall)"); return 0; }
      if(!strcmp(tool,"ufw")) snprintf(cmd,sizeof cmd,"%sufw allow 41235/tcp comment 'LAN Messenger' && %sufw allow 41234/udp comment 'LAN Messenger'",pre,pre);
      else snprintf(cmd,sizeof cmd,"%sfirewall-cmd --permanent --add-port=41235/tcp --add-port=41234/udp && %sfirewall-cmd --reload",pre,pre);
      if(getenv("LSM_FIREWALL_DRYRUN")){ snprintf(report,n,"%s -> %s (dry run)",tool,cmd); return 0; }
      rc=system(cmd); snprintf(report,n,"%s -> %s (exit %d)",tool,cmd,rc); return rc?1:0; }
#endif
#endif
}

static int latin_letter(unsigned char c){ return (c>='A'&&c<='Z')||(c>='a'&&c<='z'); }
void lsm_initials(const char *name, char out[8]){
    const unsigned char *p=(const unsigned char*)(name?name:""); int n=0;
    while(*p==' '||*p=='\t'||*p=='\n'||*p=='\r') p++;
    if(!*p){ strcpy(out,"?"); return; }
    if(latin_letter(*p)){
        while(*p && n<2){ if(latin_letter(*p)) out[n++]=(char)((*p>='a')?*p-32:*p);
            while(*p && *p!=' ' && *p!='\t') p++; while(*p==' '||*p=='\t') p++; }
        out[n]=0; return; }
    { int len=*p<0x80?1:(*p>>5)==6?2:(*p>>4)==14?3:(*p>>3)==30?4:1; memcpy(out,p,len); out[len]=0; }
}

#include "resume.c"
