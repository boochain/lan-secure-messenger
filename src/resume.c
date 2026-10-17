/* resume.c — persistent paused downloads + chunked hashing (included at the end of core.c).
 *
 * Identical to lan-core (Rust): every resumable download (peer has CAP_RESUME) has a sidecar
 * "<part>.resume" next to its .part file:
 *     LSMRESUME 1\n tid <uuid>\n peer <fp>\n msg <msg id>\n conv <conv>\n size <u64>\n
 *     sha <hex sha256 of whole file>\n mime <mime>\n name <file name>\n
 * The offset and hash state are NOT stored: on start-up the .part is re-hashed (chunked, off the
 * network path) and its length is the offset. The transfer then waits (state offered, accepted)
 * for the sender's re-offer and resumes with FileResume as after a link loss.
 * Hashing jobs (hashing=1 sender prefix check, 2 receiver re-hash) run CHUNK_HASH bytes per loop
 * iteration so a large file never stalls the select() loop. */
#define CHUNK_HASH (2u*1024*1024)

static void rs_hex(const uint8_t *b, size_t n, char *o){ static const char *h="0123456789abcdef"; size_t i; for(i=0;i<n;i++){ o[2*i]=h[b[i]>>4]; o[2*i+1]=h[b[i]&15]; } o[2*n]=0; }
static int rs_unhex(const char *s, uint8_t *b, size_t n){ size_t i; for(i=0;i<n;i++){ int v=0, j; for(j=0;j<2;j++){ char ch=s[2*i+j]; v<<=4;
    if(ch>='0'&&ch<='9') v|=ch-'0'; else if(ch>='a'&&ch<='f') v|=ch-'a'+10; else if(ch>='A'&&ch<='F') v|=ch-'A'+10; else return -1; } b[i]=(uint8_t)v; } return 0; }

static void rs_sidecar_path(const lsm_transfer *t, char *o, size_t n){ snprintf(o,n,"%s.resume",t->part); }
static void rs_sidecar_remove(lsm_transfer *t){ char p[600]; if(t->out || !t->part[0]) return; rs_sidecar_path(t,p,sizeof p); uremove(p); }
static void rs_sidecar_write(lsm_core *c, lsm_transfer *t){ char p[600], tmp[620], u[37], sh[65]; FILE *f; (void)c;
    if(t->out || !t->part[0] || !t->resumable) return;
    rs_sidecar_path(t,p,sizeof p); snprintf(tmp,sizeof tmp,"%s.tmp",p); f=ufopen(tmp,"wb"); if(!f) return;
    lsm_uuid_str(t->tid,u); rs_hex(t->sha,32,sh);
    fprintf(f,"LSMRESUME 1\ntid %s\npeer %s\nmsg %s\nconv %s\nsize %llu\nsha %s\nmime %s\nname %s\n",u,t->peer_fp,t->msg_id,t->conv,(unsigned long long)t->size,sh,t->mime,t->name);
    fclose(f); urename(tmp,p); }

/* start a receiver re-hash of an existing .part (restored download) */
static void rs_start_rehash(lsm_transfer *t){
    t->fp_handle=ufopen(t->part,"rb"); free(t->hash); t->hash=malloc(sizeof(sha256_ctx)); sha256_init((sha256_ctx*)t->hash);
    t->hashing=2; t->hash_done=0; t->transferred=0; }

/* start the sender's prefix check for a FileResume (t->fp_handle open at 0) */
static void rs_start_prefix(lsm_transfer *t, uint64_t off, const uint8_t pre[32]){
    free(t->hctx); t->hctx=malloc(sizeof(sha256_ctx)); sha256_init((sha256_ctx*)t->hctx);
    memcpy(t->hpre,pre,32); t->hash_want=off; t->hash_done=0; t->hashing=1; }

static int accept_transfer(lsm_core *c, lsm_transfer *t);
static void rs_finish_prefix(lsm_core *c, lsm_transfer *t, int ok){
    uint64_t off=ok?t->hash_want:0; conn *k=conn_by_fp(c,t->peer_fp);
    t->hashing=0; free(t->hctx); t->hctx=NULL;
    if(!k){ if(t->fp_handle){ fclose((FILE*)t->fp_handle); t->fp_handle=NULL; } return; } /* link gone meanwhile: stays offered, re-offered later */
    if(!ok) fseek((FILE*)t->fp_handle,0,SEEK_SET);
    t->state=TS_ACTIVE; t->transferred=off; t->credit=off; t->resumed_from=off; t->last_credit_mono=mono_ms(); emit_xfer(c,t); }

/* run hashing jobs; returns the number still busy (the loop then polls without sleeping) */
static int rs_pump(lsm_core *c){ int i, busy=0; uint8_t *b=NULL;
    for(i=0;i<c->nxfers;i++){ lsm_transfer *t=&c->xfers[i]; size_t n; uint64_t budget=CHUNK_HASH;
        if(!t->hashing) continue;
        if(!b && !(b=(uint8_t*)malloc(CHUNK))) break;
        if(!t->fp_handle){ if(t->hashing==1) rs_finish_prefix(c,t,0); else { t->hashing=0; t->state=TS_FAILED; scpy(t->error,"partial file missing",sizeof t->error); emit_xfer(c,t); rs_sidecar_remove(t); } continue; }
        if(t->hashing==1){
            while(budget>0 && t->hash_done<t->hash_want){ uint64_t w=t->hash_want-t->hash_done; if(w>CHUNK) w=CHUNK; if(w>budget) w=budget;
                n=fread(b,1,(size_t)w,(FILE*)t->fp_handle); if(n==0) break; sha256_update((sha256_ctx*)t->hctx,b,n); t->hash_done+=n; budget-=n; }
            if(t->hash_done>=t->hash_want){ uint8_t got[32]; sha256_final((sha256_ctx*)t->hctx,got); rs_finish_prefix(c,t,!memcmp(got,t->hpre,32)); }
            else if(budget>0){ rs_finish_prefix(c,t,0); } /* short file */
            else busy++;
        } else {
            while(budget>0 && (n=fread(b,1,(size_t)(budget<CHUNK?budget:CHUNK),(FILE*)t->fp_handle))>0){ sha256_update((sha256_ctx*)t->hash,b,n); t->hash_done+=n; budget-=n; }
            if(budget>0 || feof((FILE*)t->fp_handle)){ /* done */
                fclose((FILE*)t->fp_handle); t->fp_handle=NULL; t->hashing=0; t->transferred=t->hash_done; emit_xfer(c,t);
                if(t->transferred>t->size){ xfer_fail(c,t,TS_FAILED,"partial file larger than offer"); continue; }
                if(t->reoffer_pending && conn_by_fp(c,t->peer_fp)){ t->reoffer_pending=0; if(accept_transfer(c,t)) xfer_fail(c,t,TS_FAILED,"cannot create file"); }
            } else busy++;
        } }
    free(b); c->hash_busy=busy; return busy; }

/* ---- restore sidecars at start-up */
static void rs_restore_one(lsm_core *c, const char *dir, const char *fname){
    char sp[600], part[600], line[1200]; FILE *f; lsm_transfer nt; int got=0; uint8_t tid[16];
    path_join(sp,sizeof sp,dir,fname); scpy(part,sp,sizeof part); part[strlen(part)-7]=0; /* strip ".resume" */
    if(!uexists(part)){ uremove(sp); return; }
    f=ufopen(sp,"rb"); if(!f) return; memset(&nt,0,sizeof nt);
    if(!fgets(line,sizeof line,f) || strncmp(line,"LSMRESUME 1",11)){ fclose(f); return; }
    while(fgets(line,sizeof line,f)){ char *v=strchr(line,' '), *e; if(!v) continue; *v++=0; e=v+strlen(v); while(e>v && (e[-1]=='\n'||e[-1]=='\r')) *--e=0;
        if(!strcmp(line,"tid")){ if(!lsm_uuid_parse(v,tid)){ memcpy(nt.tid,tid,16); got|=1; } }
        else if(!strcmp(line,"peer")){ scpy(nt.peer_fp,v,FP_LEN); got|=2; }
        else if(!strcmp(line,"msg")){ scpy(nt.msg_id,v,sizeof nt.msg_id); got|=4; }
        else if(!strcmp(line,"conv")){ scpy(nt.conv,v,sizeof nt.conv); got|=8; }
        else if(!strcmp(line,"size")){ nt.size=strtoull(v,NULL,10); got|=16; }
        else if(!strcmp(line,"sha")){ if(strlen(v)==64 && !rs_unhex(v,nt.sha,32)) got|=32; }
        else if(!strcmp(line,"mime")) scpy(nt.mime,v,sizeof nt.mime);
        else if(!strcmp(line,"name")){ scpy(nt.name,v,sizeof nt.name); got|=64; } }
    fclose(f);
    if(got!=127 || xfer_get(c,nt.tid)) return;
    if(!strncmp(nt.conv,"group:",6)){ nt.has_group=!lsm_uuid_parse(nt.conv+6,nt.group_id); }
    scpy(nt.part,part,sizeof nt.part); nt.state=TS_OFFERED; nt.accepted=1; nt.resumable=1;
    scpy(nt.error,"\xe7\xad\x89\xe5\xbe\x85\xe5\xaf\xb9\xe6\x96\xb9\xe9\x87\x8d\xe5\x8f\x91\xe4\xbb\xa5\xe7\xbb\xad\xe4\xbc\xa0",sizeof nt.error); /* 等待对方重发以续传 */
    c->xfers=(lsm_transfer*)realloc(c->xfers,sizeof(lsm_transfer)*(c->nxfers+1)); c->xfers[c->nxfers++]=nt;
    rs_start_rehash(&c->xfers[c->nxfers-1]); }

#ifdef _WIN32
static void rs_restore(lsm_core *c){ char pat[600]; wchar_t *w; WIN32_FIND_DATAW fd; HANDLE h;
    path_join(pat,sizeof pat,c->dl_dir,"*.part.resume"); w=widen(pat); h=FindFirstFileW(w,&fd); free(w); if(h==INVALID_HANDLE_VALUE) return;
    do { char n[600]; if(WideCharToMultiByte(CP_UTF8,0,fd.cFileName,-1,n,sizeof n,NULL,NULL)>0) rs_restore_one(c,c->dl_dir,n); } while(FindNextFileW(h,&fd));
    FindClose(h); }
#else
#include <dirent.h>
static void rs_restore(lsm_core *c){ DIR *d=opendir(c->dl_dir); struct dirent *e; size_t l;
    if(!d) return;
    while((e=readdir(d))){ l=strlen(e->d_name); if(l>12 && !strcmp(e->d_name+l-12,".part.resume")) rs_restore_one(c,c->dl_dir,e->d_name); }
    closedir(d); }
#endif
