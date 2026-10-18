/* lanmsg-cli: console edition of the XP messenger. Runs the engine in-process and reads
 * commands from stdin (interactive console or script/pipe). Type "help". */
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#include <sys/select.h>
#include <time.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "core.h"

static lsm_core *C;
#ifdef _WIN32
static int is_console_out, is_console_in; static HANDLE hout, hin; static CRITICAL_SECTION out_mu;
static void out_raw(const char *s){ EnterCriticalSection(&out_mu);
    if(is_console_out){ int n=MultiByteToWideChar(CP_UTF8,0,s,-1,NULL,0); wchar_t *w=(wchar_t*)malloc(n*sizeof(wchar_t)); DWORD wr; MultiByteToWideChar(CP_UTF8,0,s,-1,w,n); WriteConsoleW(hout,w,(DWORD)(n-1),&wr,NULL); free(w); }
    else { DWORD wr; const char *q=s, *nl; while(*q){ nl=strchr(q,'\n'); if(!nl){ WriteFile(hout,q,(DWORD)strlen(q),&wr,NULL); break; } if(nl>q && nl[-1]=='\r') WriteFile(hout,q,(DWORD)(nl-q+1),&wr,NULL); else { WriteFile(hout,q,(DWORD)(nl-q),&wr,NULL); WriteFile(hout,"\r\n",2,&wr,NULL); } q=nl+1; } } /* raw handle write: no CRT FILE buffer (ReactOS msvcrt lost the first line when 2 threads printed) */
    LeaveCriticalSection(&out_mu); }
static void sleep_ms(int ms){ Sleep(ms); }
#else
#include <pthread.h>
static pthread_mutex_t out_mu=PTHREAD_MUTEX_INITIALIZER;
static void out_raw(const char *s){ pthread_mutex_lock(&out_mu); fputs(s,stdout); fflush(stdout); pthread_mutex_unlock(&out_mu); }
static void sleep_ms(int ms){ struct timespec t={ms/1000,(ms%1000)*1000000L}; nanosleep(&t,NULL); }
#endif
static void outf(const char *fmt, ...){ /* per-call buffer: the old static buffer was shared by the main and event threads, so concurrent lines got mixed */
    enum { N=300000 }; char *b=(char*)malloc(N); va_list ap; if(!b) return; va_start(ap,fmt); vsnprintf(b,N,fmt,ap); va_end(ap); out_raw(b); free(b); }

static void drain(void){ char e[4096]; while(lsm_poll_event(C,e,sizeof e)){ if(!strncmp(e,"log\t",4) && !getenv("LSM_VERBOSE")) continue; outf("EV %s\n",e); } }
#ifdef _WIN32
static DWORD WINAPI ev_thread(LPVOID p){ (void)p; for(;;){ drain(); Sleep(50);} return 0; }
#else
#include <pthread.h>
static void *ev_thread(void *p){ (void)p; for(;;){ drain(); sleep_ms(50);} return NULL; }
#endif

static const char *conv_of(const char *q, char *buf){
    const char *fp; char g[37];
    if(!strncmp(q,"dm:",3)||!strncmp(q,"group:",6)) { strcpy(buf,q); return buf; }
    lsm_lock(C); fp=lsm_resolve_peer(C,q); if(fp){ snprintf(buf,64,"dm:%s",fp); lsm_unlock(C); return buf; }
    if(lsm_resolve_group(C,q,g)){ snprintf(buf,64,"group:%s",g); lsm_unlock(C); return buf; }
    lsm_unlock(C); return NULL; }
static int peer_of(const char *q, char out[FP_LEN]){ const char *fp; lsm_lock(C); fp=lsm_resolve_peer(C,q); if(fp) strcpy(out,fp); lsm_unlock(C); return fp?0:-1; }
static char *next_tok(char **s){ char *p=*s, *st; while(*p==' ') p++; if(!*p) return NULL; st=p; while(*p && *p!=' ') p++; if(*p) *p++=0; *s=p; return st; }

static const char *STS[]={"queued","sent","delivered","read","received","failed"};
static const char *TST[]={"offered","active","verifying","done","failed","rejected","cancelled"};

static void help(void){ outf("commands: id | peers | add <ip[:port]> | trust/untrust/block <peer> | send <peer> <text> | sendgroup <group> <text> |\n"
    " sendfile <peer|group> <path> | pin-offer | pin-pair <peer> <pin> | group-create <name> <peer,peer..> | group-invite <group> <peer,..> |\n"
    " group-remove <group> <peer> | group-leave <group> | groups | history <peer|group> [n] | read <peer|group> | transfers | name <new name> |\n"
    " firewall-setup | drop-links | sleep <ms> | quit\n"); }

static int run_cmd(char *line){
    char *s=line, *cmd=next_tok(&s), err[256]="", buf[64], fp[FP_LEN];
    if(!cmd) return 0;
    while(*s==' ') s++;
    if(!strcmp(cmd,"quit")||!strcmp(cmd,"exit")) return 1;
    if(!strcmp(cmd,"help")){ help(); return 0; }
    if(!strcmp(cmd,"id")){ lsm_lock(C); outf("OK id %s %s port=%d version=%s\n",lsm_my_fp(C),lsm_my_name(C),lsm_listen_port(C),LSM_APP_VERSION); lsm_unlock(C); return 0; }
    if(!strcmp(cmd,"peers")){ lsm_contact *k; int n,i; lsm_lock(C); n=lsm_contacts(C,&k); for(i=0;i<n;i++) outf("PEER %s\t%s\t%s\t%s\trtt=%d\t%s",k[i].fp,k[i].online?"online":"offline",k[i].trust==1?"trusted":k[i].trust==2?"blocked":"unknown",k[i].name,k[i].rtt_ms,k[i].addr), outf("%s",k[i].online?(k[i].via_relay?"\tvia=relay\n":"\tvia=direct\n"):"\n"); lsm_unlock(C); outf("OK peers %d\n",n); return 0; }
    if(!strcmp(cmd,"add")){ if(lsm_add_addr(C,s,err)) outf("ERR %s\n",err); else outf("OK add %s\n",s); return 0; }
    if(!strcmp(cmd,"trust")||!strcmp(cmd,"untrust")||!strcmp(cmd,"block")){ if(peer_of(s,fp)){ outf("ERR no peer matches '%s'\n",s); return 0; } lsm_set_trust(C,fp,cmd[0]=='t'?1:cmd[0]=='b'?2:0,err); outf("OK %s %s\n",cmd,fp); return 0; }
    if(!strcmp(cmd,"send")||!strcmp(cmd,"sendgroup")){ char *who=next_tok(&s), id[40]; const char *cv; while(s&&*s==' ') s++; if(!who||!(cv=conv_of(who,buf))){ outf("ERR unknown target\n"); return 0; }
        if(lsm_send_text(C,cv,s,id,err)) outf("ERR %s\n",err); else outf("OK sent %s\n",id); return 0; }
    if(!strcmp(cmd,"sendfile")){ char *who=next_tok(&s); const char *cv; while(s&&*s==' ') s++; if(!who||!(cv=conv_of(who,buf))){ outf("ERR unknown target\n"); return 0; }
        if(lsm_send_file(C,cv,s,err)) outf("ERR %s\n",err); else outf("OK sendfile %s\n",s); return 0; }
    if(!strcmp(cmd,"pin-offer")){ char pin[8]; lsm_pin_offer(C,pin); outf("OK pin %s expires_in_secs=300\n",pin); return 0; }
    if(!strcmp(cmd,"pin-pair")){ char *who=next_tok(&s), *pin=next_tok(&s); if(!who||!pin||peer_of(who,fp)){ outf("ERR usage: pin-pair <peer> <pin>\n"); return 0; } if(lsm_pin_pair(C,fp,pin,err)) outf("ERR %s\n",err); else outf("OK pin-pair sent\n"); return 0; }
    if(!strcmp(cmd,"group-create")||!strcmp(cmd,"group-invite")){ char *a=next_tok(&s), *list=next_tok(&s), *t, *nx; const char *fps[64]; char store[64][FP_LEN]; int n=0; char gid[40], g[37];
        if(!a){ outf("ERR usage\n"); return 0; }
        for(t=list; t && *t && n<64; t=nx){ nx=strchr(t,','); if(nx) *nx++=0; if(peer_of(t,store[n])){ outf("ERR no peer matches '%s'\n",t); return 0; } fps[n]=store[n]; n++; }
        if(cmd[6]=='c'){ if(lsm_group_create(C,a,fps,n,gid,err)) outf("ERR %s\n",err); else outf("OK group %s\n",gid); }
        else { lsm_lock(C); if(!lsm_resolve_group(C,a,g)){ lsm_unlock(C); outf("ERR no group\n"); return 0; } lsm_unlock(C); if(lsm_group_invite(C,g,fps,n,err)) outf("ERR %s\n",err); else outf("OK invited\n"); }
        return 0; }
    if(!strcmp(cmd,"group-remove")){ char *a=next_tok(&s), *p=next_tok(&s), g[37]; lsm_lock(C); if(!a||!p||!lsm_resolve_group(C,a,g)){ lsm_unlock(C); outf("ERR usage\n"); return 0; } lsm_unlock(C); if(peer_of(p,fp)){ outf("ERR no peer\n"); return 0; }
        if(lsm_group_remove(C,g,fp,err)) outf("ERR %s\n",err); else outf("OK removed\n"); return 0; }
    if(!strcmp(cmd,"group-leave")){ char g[37]; lsm_lock(C); if(!lsm_resolve_group(C,s,g)){ lsm_unlock(C); outf("ERR no group\n"); return 0; } lsm_unlock(C); if(lsm_group_leave(C,g,err)) outf("ERR %s\n",err); else outf("OK left\n"); return 0; }
    if(!strcmp(cmd,"groups")){ lsm_group *g; int n,i,j; char u[37]; lsm_lock(C); n=lsm_groups(C,&g); for(i=0;i<n;i++){ lsm_uuid_str(g[i].id,u); outf("GROUP %s\t%s\tepoch=%llu\tcreator=%s\tmembers=",u,g[i].name,(unsigned long long)g[i].key_epoch,g[i].creator_fp); for(j=0;j<g[i].nmembers;j++) outf("%s%s",j?",":"",g[i].members[j]); outf("\n"); } lsm_unlock(C); outf("OK groups %d\n",n); return 0; }
    if(!strcmp(cmd,"history")){ char *who=next_tok(&s); int lim=s&&*s?atoi(s):50, n,i,cnt=0,start; lsm_msg *m; const char *cv; if(!who||!(cv=conv_of(who,buf))){ outf("ERR unknown target\n"); return 0; }
        lsm_lock(C); n=lsm_messages(C,&m); for(i=0;i<n;i++) if(!strcmp(m[i].conv,cv)) cnt++; start=cnt-lim; cnt=0;
        for(i=0;i<n;i++) if(!strcmp(m[i].conv,cv)){ if(cnt++<start) continue; outf("MSG %s\t%s\t%s\t%s\t%s\t%s\n",m[i].id,m[i].out?"out":"in",STS[m[i].status],m[i].sender_name,m[i].kind==1?"[file]":"",m[i].kind==1?m[i].file_name:(m[i].body?m[i].body:"")); }
        lsm_unlock(C); outf("OK history\n"); return 0; }
    if(!strcmp(cmd,"initials")){ char o[8]; lsm_initials(s?s:"",o); outf("OK initials %s\n",o); return 0; }
    if(!strcmp(cmd,"delete")){ char *id=next_tok(&s); if(!id){ outf("ERR usage: delete <msg-id>\n"); return 0; } outf(lsm_delete_message(C,id)?"ERR not found\n":"OK delete\n"); return 0; }
    if(!strcmp(cmd,"read")){ const char *cv=conv_of(s,buf); if(!cv){ outf("ERR unknown target\n"); return 0; } outf("OK read %d\n",lsm_mark_read(C,cv)); return 0; }
    if(!strcmp(cmd,"transfers")){ lsm_transfer *t; int n,i; char u[37]; lsm_lock(C); n=lsm_transfers(C,&t); for(i=0;i<n;i++){ lsm_uuid_str(t[i].tid,u); outf("XFER %s\t%s\t%s\t%llu/%llu\t%s\t%s\t%s\tresumed=%llu\n",u,t[i].out?"out":"in",TST[t[i].state],(unsigned long long)t[i].transferred,(unsigned long long)t[i].size,t[i].name,t[i].path,t[i].error,(unsigned long long)t[i].resumed_from); } lsm_unlock(C); outf("OK transfers %d\n",n); return 0; }
    if(!strcmp(cmd,"name")){ lsm_set_name(C,s); outf("OK name\n"); return 0; }
    if(!strcmp(cmd,"cancel")){ if(lsm_transfer_cancel(C,s,err)) outf("ERR %s\n",err); else outf("OK cancel\n"); return 0; }
    if(!strcmp(cmd,"relay")){ /* relay <host> <port> <passphrase> | relay off | relay status */
        char *a=next_tok(&s), *pt=next_tok(&s), host[256], det[256]; int en, port, hp=0;
        if(a && !strcmp(a,"off")){ lsm_relay_set(C,0,NULL,0,NULL,err); outf("OK relay off\n"); return 0; }
        if(a && strcmp(a,"status") && pt){ while(s&&*s==' ') s++; if(lsm_relay_set(C,1,a,atoi(pt),s?s:"",err)) outf("ERR %s\n",err); else outf("OK relay %s:%d\n",a,atoi(pt)); return 0; }
        lsm_relay_get(C,&en,host,sizeof host,&port,&hp); lsm_lock(C); { const char *st=lsm_relay_status(C,det,sizeof det); outf("OK relay %s %s enabled=%d server=%s:%d peers=%d passphrase=%s\n",st,det[0]?det:"-",en,host,port,lsm_relay_peers(C),hp?"set":"none"); } lsm_unlock(C); return 0; }
    if(!strcmp(cmd,"drop-links")){ outf("OK dropped %d\n",lsm_drop_links(C)); return 0; }
    if(!strcmp(cmd,"sleep")){ sleep_ms(atoi(s)); outf("OK sleep\n"); return 0; }
    if(!strcmp(cmd,"firewall-setup")){ char rep[2048], exe[1024]="";
#ifdef _WIN32
        wchar_t w[MAX_PATH]; GetModuleFileNameW(NULL,w,MAX_PATH); WideCharToMultiByte(CP_UTF8,0,w,-1,exe,sizeof exe,NULL,NULL);
#endif
        lsm_firewall_setup(exe,rep,sizeof rep); outf("OK firewall %s\n",rep); return 0; }
    outf("ERR unknown command '%s' (help)\n",cmd); return 0; }

static int read_line(char *b, size_t n){
#ifdef _WIN32
    if(is_console_in){ /* XP/ReactOS CSRSS caps one ReadConsole at ~64 KB of buffer: read in 2K-char slices */
        static wchar_t w[65536]; size_t wl=0; for(;;){ DWORD got=0; if(!ReadConsoleW(hin,w+wl,2048,&got,NULL)||got==0){ if(!wl) return 0; break; }
            wl+=got; if(w[wl-1]==L'\n'||wl+2048>=65535) break; }
        w[wl]=0; WideCharToMultiByte(CP_UTF8,0,w,-1,b,(int)n,NULL,NULL); }
    else { /* pipe/file: raw ReadFile, independent of CRT text-mode buffering limits */
        static char rb[65536]; static DWORD rlen, rpos; size_t o=0; int got_any=0;
        for(;;){ if(rpos>=rlen){ if(!ReadFile(hin,rb,sizeof rb,&rlen,NULL)||rlen==0){ rlen=rpos=0; if(!got_any) return 0; break; } rpos=0; }
            got_any=1; { char ch=rb[rpos++]; if(ch=='\n') break; if(o+1<n) b[o++]=ch; } }
        b[o]=0; }
    if(0)
#endif
    if(!fgets(b,(int)n,stdin)) return 0;
    { char *e=strpbrk(b,"\r\n"); if(e) *e=0; } return 1; }

int main(int argc, char **argv){
    lsm_opts o; char err[256], data[1024]=""; static char line[300000]; int i;
    memset(&o,0,sizeof o);
#ifdef _WIN32
    { wchar_t **wa; int wn; DWORD mode; InitializeCriticalSection(&out_mu); hout=GetStdHandle(STD_OUTPUT_HANDLE); hin=GetStdHandle(STD_INPUT_HANDLE);
      is_console_out=GetConsoleMode(hout,&mode)?1:0; is_console_in=GetConsoleMode(hin,&mode)?1:0;
      wa=CommandLineToArgvW(GetCommandLineW(),&wn); argv=(char**)malloc(sizeof(char*)*(wn+1)); for(i=0;i<wn;i++){ int l=WideCharToMultiByte(CP_UTF8,0,wa[i],-1,NULL,0,NULL,NULL); argv[i]=(char*)malloc(l); WideCharToMultiByte(CP_UTF8,0,wa[i],-1,argv[i],l,NULL,NULL);} argc=wn; argv[argc]=NULL;
      { wchar_t exe[MAX_PATH], *sl; GetModuleFileNameW(NULL,exe,MAX_PATH); sl=wcsrchr(exe,L'\\'); if(sl) *sl=0; wcscat(exe,L"\\data"); WideCharToMultiByte(CP_UTF8,0,exe,-1,data,sizeof data,NULL,NULL); } }
#else
    strcpy(data,"./data");
#endif
    for(i=1;i<argc;i++){
        if(!strcmp(argv[i],"--data")&&i+1<argc) snprintf(data,sizeof data,"%s",argv[++i]);
        else if(!strcmp(argv[i],"--name")&&i+1<argc) o.name=argv[++i];
        else if(!strcmp(argv[i],"--disco-port")&&i+1<argc) o.discovery_port=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--port")&&i+1<argc) o.listen_port=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--targets")&&i+1<argc) o.announce_targets=argv[++i];
        else if(!strcmp(argv[i],"--announce-ms")&&i+1<argc) o.announce_ms=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--no-discovery")) o.no_discovery=1;
        else if(!strcmp(argv[i],"--auto-trust")) o.auto_trust=1;
        else if(!strcmp(argv[i],"--version")){ printf("lanmsg-cli %s (protocol v5)\n",LSM_APP_VERSION); return 0; }
        else { printf("usage: lanmsg-cli [--data DIR] [--name NAME] [--port 41235] [--disco-port 41234] [--targets ip,ip] [--no-discovery] [--auto-trust]\n"); return 2; } }
    o.data_dir=data;
    C=lsm_start(&o,err,sizeof err);
    if(!C){ outf("ERR start: %s\n",err); return 1; }
#ifdef _WIN32
    CreateThread(NULL,0,ev_thread,NULL,0,NULL);
#else
    { pthread_t t; pthread_create(&t,NULL,ev_thread,NULL); }
#endif
#ifdef _WIN32
    if(is_console_in) help();
#else
    if(isatty(0)) help();
#endif
    while(read_line(line,sizeof line)){ if(run_cmd(line)) break; }
    sleep_ms(100); drain();
    lsm_stop(C); drain(); outf("OK bye\n");
    return 0; }
