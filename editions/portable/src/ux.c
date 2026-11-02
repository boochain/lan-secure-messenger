/* v0.6.1 UX helpers shared by every GUI (included from core.c):
   QR add (lsm:// link + QR matrix, fingerprint-checked auto trust), one-click resend with a reason,
   file size / ETA preview, ↑-edit lookup, Ctrl+K fuzzy match, delivery route (LAN / via server). */
#include "third_party/qrcodegen.c"

static char ux_pending_fp[8][FP_LEN]; static int64_t ux_pending_until[8];

void lsm_fmt_size(uint64_t b, char *out, size_t n){
    if(b<1024) snprintf(out,n,"%u B",(unsigned)b);
    else if(b<1024ull*1024) snprintf(out,n,"%.1f KB",b/1024.0);
    else if(b<1024ull*1024*1024) snprintf(out,n,"%.1f MB",b/1048576.0);
    else snprintf(out,n,"%.2f GB",b/1073741824.0); }
void lsm_fmt_dur(int64_t s, char *out, size_t n){
    if(s<1) s=1;
    if(s<60) snprintf(out,n,"%d \xe7\xa7\x92",(int)s);                       /* 秒 */
    else if(s<3600) snprintf(out,n,"%d \xe5\x88\x86 %d \xe7\xa7\x92",(int)(s/60),(int)(s%60)); /* 分 秒 */
    else snprintf(out,n,"%d \xe5\xb0\x8f\xe6\x97\xb6 %d \xe5\x88\x86",(int)(s/3600),(int)(s/60%60)); } /* 小时 分 */

static int ux_route(lsm_core *c, const char *conv){ /* 1 LAN, 2 relay, 0 offline/group */
    conn *k; if(strncmp(conv,"dm:",3)) return 0; k=conn_by_fp(c,conv+3); if(!k) return 0; return k->via_relay?2:1; }

/* "name · 12.3 MB · 预计约 4 秒（局域网直连）" before a file is sent. Takes the lock. */
int lsm_file_preview(lsm_core *c, const char *conv, const char *path, char *out, size_t n){
    FILE *f=fopen(path,"rb"); uint64_t sz=0; char s[32],d[48]; const char *nm=path,*p; int r; double bps;
    if(!f){ snprintf(out,n,"\xe6\x97\xa0\xe6\xb3\x95\xe8\xaf\xbb\xe5\x8f\x96\xe6\x96\x87\xe4\xbb\xb6"); return -1; } /* 无法读取文件 */
    fseek(f,0,SEEK_END); { long l=ftell(f); sz=l>0?(uint64_t)l:0; } fclose(f);
    for(p=path;*p;p++) if(*p=='/'||*p=='\\') nm=p+1;
    mu_lock(&c->mu); r=ux_route(c,conv); mu_unlock(&c->mu);
    bps=r==1?8e6:r==2?1e6:0; lsm_fmt_size(sz,s,sizeof s);
    if(bps>0){ lsm_fmt_dur((int64_t)(sz/bps)+1,d,sizeof d);
        snprintf(out,n,"%s \xc2\xb7 %s \xc2\xb7 \xe9\xa2\x84\xe8\xae\xa1\xe7\xba\xa6 %s\xef\xbc\x88%s\xef\xbc\x89",nm,s,d,
                 r==1?"\xe5\xb1\x80\xe5\x9f\x9f\xe7\xbd\x91\xe7\x9b\xb4\xe8\xbf\x9e":"\xe7\xbb\x8f\xe6\x9c\x8d\xe5\x8a\xa1\xe5\x99\xa8\xe4\xb8\xad\xe8\xbd\xac"); } /* 预计约 … （局域网直连 / 经服务器中转） */
    else snprintf(out,n,"%s \xc2\xb7 %s \xc2\xb7 \xe5\xaf\xb9\xe6\x96\xb9\xe4\xb8\x8a\xe7\xba\xbf\xe5\x90\x8e\xe5\x8f\x91\xe9\x80\x81",nm,s); /* 对方上线后发送 */
    return 0; }

/* live ETA of a running transfer: "3.2 MB/s · 剩余约 12 秒" (lock held by caller) */
void lsm_transfer_eta(lsm_transfer *t, char *out, size_t n){
    char r[32],d[48]; out[0]=0; if(!t->rate_bps || t->size<=t->transferred) return;
    lsm_fmt_size(t->rate_bps,r,sizeof r); lsm_fmt_dur((int64_t)((t->size-t->transferred)/t->rate_bps)+1,d,sizeof d);
    snprintf(out,n,"%s/s \xc2\xb7 \xe5\x89\xa9\xe4\xbd\x99\xe7\xba\xa6 %s",r,d); } /* 剩余约 */

/* Status line for an outgoing message, including why it failed and the route. Lock held. */
const char *lsm_status_text(lsm_core *c, const lsm_msg *m){
    if(!m->out) return "";
    switch(m->status){
    case ST_QUEUED: return "\xe7\xad\x89\xe5\xbe\x85\xe5\x8f\x91\xe9\x80\x81\xef\xbc\x9a\xe5\xaf\xb9\xe6\x96\xb9\xe7\xa6\xbb\xe7\xba\xbf\xef\xbc\x8c\xe4\xbf\x9d\xe5\xad\x98\xe5\x9c\xa8\xe6\x9c\xac\xe6\x9c\xba\xef\xbc\x8c\xe4\xb8\x8a\xe7\xba\xbf\xe5\x90\x8e\xe8\x87\xaa\xe5\x8a\xa8\xe5\x8f\x91\xe9\x80\x81"; /* 等待发送：对方离线，保存在本机，上线后自动发送 */
    case ST_SENT: return m->route==2?"\xe5\xb7\xb2\xe5\x8f\x91\xe9\x80\x81\xef\xbc\x88\xe7\xbb\x8f\xe6\x9c\x8d\xe5\x8a\xa1\xe5\x99\xa8\xe4\xb8\xad\xe8\xbd\xac\xef\xbc\x89":"\xe5\xb7\xb2\xe5\x8f\x91\xe9\x80\x81"; /* 已发送（经服务器中转） */
    case ST_DELIVERED: return m->route==2?"\xe5\xb7\xb2\xe9\x80\x81\xe8\xbe\xbe\xef\xbc\x88\xe7\xbb\x8f\xe6\x9c\x8d\xe5\x8a\xa1\xe5\x99\xa8\xe4\xb8\xad\xe8\xbd\xac\xef\xbc\x89":m->route==1?"\xe5\xb7\xb2\xe9\x80\x81\xe8\xbe\xbe\xef\xbc\x88\xe5\xb1\x80\xe5\x9f\x9f\xe7\xbd\x91\xe7\x9b\xb4\xe8\xbf\x9e\xef\xbc\x89":"\xe5\xb7\xb2\xe9\x80\x81\xe8\xbe\xbe"; /* 已送达（…） */
    case ST_READ: return "\xe5\xb7\xb2\xe8\xaf\xbb"; /* 已读 */
    case ST_REJECTED: return "\xe5\x8f\x91\xe9\x80\x81\xe5\xa4\xb1\xe8\xb4\xa5\xef\xbc\x9a\xe5\xaf\xb9\xe6\x96\xb9\xe6\x8b\x92\xe7\xbb\x9d\xe6\x8e\xa5\xe6\x94\xb6"; /* 发送失败：对方拒绝接收 */
    case ST_FAILED: { int i; for(i=0;i<c->nxfers;i++) if(!strcmp(c->xfers[i].msg_id,m->id) && c->xfers[i].error[0]){
            static char b[300]; snprintf(b,sizeof b,"\xe5\x8f\x91\xe9\x80\x81\xe5\xa4\xb1\xe8\xb4\xa5\xef\xbc\x9a%s",c->xfers[i].error); return b; }
        return "\xe5\x8f\x91\xe9\x80\x81\xe5\xa4\xb1\xe8\xb4\xa5\xef\xbc\x9a\xe8\xbf\x9e\xe6\x8e\xa5\xe4\xb8\xad\xe6\x96\xad"; } /* 发送失败：连接中断 */
    } return ""; }
int lsm_can_resend(const lsm_msg *m){ return m->out && !m->recalled && !m->is_poll && (m->status==ST_FAILED||m->status==ST_REJECTED||m->status==ST_QUEUED) && (m->kind==K_TEXT||(m->kind==K_FILE&&m->file_path[0])); }

/* Resend a failed / stuck message as a new one (old copy removed on this device). */
int lsm_resend(lsm_core *c, const char *id, char *newid, char *err){
    char conv[64],path[520],reply[40],ment[512]; char *body=NULL; int kind,r;
    mu_lock(&c->mu); { lsm_msg *m=msg_get(c,id); if(!m || !lsm_can_resend(m)){ mu_unlock(&c->mu); E("\xe6\xad\xa4\xe6\xb6\x88\xe6\x81\xaf\xe4\xb8\x8d\xe8\x83\xbd\xe9\x87\x8d\xe5\x8f\x91"); return -1; } /* 此消息不能重发 */
        scpy(conv,m->conv,sizeof conv); scpy(path,m->file_path,sizeof path); scpy(reply,m->reply_to,sizeof reply); scpy(ment,m->mentions,sizeof ment); kind=m->kind; body=strdup(m->body?m->body:""); }
    mu_unlock(&c->mu);
    if(kind==K_FILE){ r=lsm_send_file(c,conv,path,err); if(newid) newid[0]=0; }
    else r=lsm_send_text2(c,conv,body,reply[0]?reply:NULL,ment[0]?ment:NULL,newid,err);
    free(body); if(r==0) lsm_delete_message(c,id); return r; }

/* ↑ in an empty composer: newest own editable text message in conv. Lock held. */
int lsm_last_editable(lsm_core *c, const char *conv, char *id_out){
    int i; for(i=c->nmsgs-1;i>=0;i--){ lsm_msg *m=&c->msgs[i]; if(strcmp(m->conv,conv)) continue; if(m->out && m->kind==K_TEXT && !m->is_poll && lsm_can_modify(c,m)){ scpy(id_out,m->id,40); return 1; } }
    return 0; }

/* Ctrl+K: case-insensitive subsequence match; returns a score (higher = better), 0 = no match. */
int lsm_fuzzy(const char *hay, const char *q){
    int score=1,run=0; const unsigned char *h=(const unsigned char*)hay,*p=(const unsigned char*)q; if(!*p) return 1;
    for(;*h && *p;h++){ int a=tolower(*h),b=tolower(*p); if(a==b){ p++; run++; score+=run*2+(h==(const unsigned char*)hay?5:0); } else run=0; }
    return *p?0:score; }

/* lsm://add?a=<ip:port>&fp=<fingerprint>&n=<name> */
int lsm_my_uri(lsm_core *c, char *out, size_t n){
    uint32_t ip=first_lan_ip(); struct in_addr ia; char nm[200]; size_t j=0; const char *s;
    ia.s_addr=ip; mu_lock(&c->mu);
    for(s=c->name;*s && j+4<sizeof nm;s++){ unsigned char ch=(unsigned char)*s; if(isalnum(ch)||ch=='-'||ch=='.'||ch=='_'||ch>=0x80) nm[j++]=(char)ch; else { snprintf(nm+j,4,"%%%02X",ch); j+=3; } } nm[j]=0;
    snprintf(out,n,"lsm://add?a=%s:%d&fp=%s&n=%s",ip?inet_ntoa(ia):"",c->lport,c->fp,nm);
    mu_unlock(&c->mu); return ip?0:-1; }
static void ux_param(const char *uri, const char *key, char *out, size_t n){
    const char *p=strchr(uri,'?'); size_t kl=strlen(key),j=0; out[0]=0; if(!p) return; p++;
    while(*p){ if(!strncmp(p,key,kl) && p[kl]=='='){ p+=kl+1; while(*p && *p!='&' && j+1<n){ if(*p=='%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])){ char h[3]={p[1],p[2],0}; out[j++]=(char)strtol(h,NULL,16); p+=3; } else out[j++]=*p++; } out[j]=0; return; }
        while(*p && *p!='&') p++; if(*p) p++; } }
/* Scanned / pasted link: dial the address; when the peer that answers proves this fingerprint it is trusted (in-person QR = verified). */
int lsm_add_uri(lsm_core *c, const char *uri, char *err){
    char a[64],fp[FP_LEN]; int i,slot=0;
    while(*uri==' ') uri++;
    if(strncmp(uri,"lsm://add?",10)){ E("\xe4\xb8\x8d\xe6\x98\xaf\xe6\x9c\x89\xe6\x95\x88\xe7\x9a\x84\xe6\xb7\xbb\xe5\x8a\xa0\xe9\x93\xbe\xe6\x8e\xa5"); return -1; } /* 不是有效的添加链接 */
    ux_param(uri,"a",a,sizeof a); ux_param(uri,"fp",fp,sizeof fp);
    if(!a[0]){ E("\xe9\x93\xbe\xe6\x8e\xa5\xe7\xbc\xba\xe5\xb0\x91\xe5\x9c\xb0\xe5\x9d\x80"); return -1; } /* 链接缺少地址 */
    if(lsm_add_addr(c,a,err)) return -1;
    if(fp[0]){ mu_lock(&c->mu); for(i=0;i<8;i++) if(ux_pending_until[i]<ux_pending_until[slot]) slot=i;
        scpy(ux_pending_fp[slot],fp,FP_LEN); ux_pending_until[slot]=mono_ms()+10*60*1000; mu_unlock(&c->mu); }
    return 0; }
static void ux_tick(lsm_core *c){ int i; int64_t now=mono_ms();
    for(i=0;i<8;i++) if(ux_pending_until[i]){ if(now>ux_pending_until[i]){ ux_pending_until[i]=0; continue; }
        if(conn_by_fp(c,ux_pending_fp[i])){ lsm_contact *k=contact_entry(c,ux_pending_fp[i]); ux_pending_until[i]=0;
            if(k->trust!=TRUST_BLOCKED){ k->trust=TRUST_TRUSTED; save_contacts(c); emit(c,"peer\t%s\ttrust\ttrusted",k->fp); } } } }
static void ux_on_status(lsm_core *c, lsm_msg *m){ if(m->out && (m->status==ST_SENT||m->status==ST_DELIVERED) && !m->route) m->route=ux_route(c,m->conv); }

/* QR matrix (1 byte per module, 1 = dark). Returns the side length, 0 on error. out must hold 177*177. */
int lsm_qr(const char *text, uint8_t *out){
    static uint8_t q[qrcodegen_BUFFER_LEN_MAX], t[qrcodegen_BUFFER_LEN_MAX]; int s,x,y;
    if(!qrcodegen_encodeText(text,t,q,qrcodegen_Ecc_MEDIUM,qrcodegen_VERSION_MIN,qrcodegen_VERSION_MAX,qrcodegen_Mask_AUTO,true)) return 0;
    s=qrcodegen_getSize(q); for(y=0;y<s;y++) for(x=0;x<s;x++) out[y*s+x]=qrcodegen_getModule(q,x,y)?1:0; return s; }
