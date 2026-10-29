/* updcheck.h - shared update-check logic for the C UIs (XP Win32, GTK). The HTTP fetch is per platform
 * (WinHTTP on Windows, curl via GSubprocess on Linux); this file only parses the GitHub releases API answer
 * (GET https://api.github.com/repos/boochain/lan-secure-messenger/releases?per_page=10) and compares versions.
 * Same rules as the WinUI MainViewModel.CompareVersions (SemVer, numeric pre-release identifiers). */
#ifndef LSM_UPDCHECK_H
#define LSM_UPDCHECK_H
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#define UPD_API_HOST "api.github.com"
#define UPD_API_PATH "/repos/boochain/lan-secure-messenger/releases?per_page=10"
#define UPD_RELEASES_URL "https://github.com/boochain/lan-secure-messenger/releases"

static void upd_parse(const char *v, long n[3], const char **pre){ int i; if(*v=='v'||*v=='V') v++;
    for(i=0;i<3;i++){ n[i]=0; while(isdigit((unsigned char)*v)) n[i]=n[i]*10+(*v++-'0'); if(*v=='.') v++; else { i++; for(;i<3;i++) n[i]=0; break; } }
    while(*v && *v!='-' && *v!='+') v++; *pre=(*v=='-')?v+1:""; }
/* <0 / 0 / >0 like strcmp */
static int upd_cmp(const char *a, const char *b){ long an[3], bn[3]; const char *ap, *bp; int i;
    upd_parse(a,an,&ap); upd_parse(b,bn,&bp);
    for(i=0;i<3;i++) if(an[i]!=bn[i]) return an[i]<bn[i]?-1:1;
    if(!*ap||*ap=='+') return (!*bp||*bp=='+')?0:1; if(!*bp||*bp=='+') return -1;
    while(*ap && *bp && *ap!='+' && *bp!='+'){ size_t la=strcspn(ap,".+"), lb=strcspn(bp,".+"); int na=1, nb=1, c; size_t k;
        for(k=0;k<la;k++) if(!isdigit((unsigned char)ap[k])) na=0; for(k=0;k<lb;k++) if(!isdigit((unsigned char)bp[k])) nb=0;
        if(na&&nb){ long x=strtol(ap,NULL,10), y=strtol(bp,NULL,10); c=x<y?-1:x>y; } else if(na) c=-1; else if(nb) c=1; else { c=strncmp(ap,bp,la<lb?la:lb); if(!c) c=la<lb?-1:la>lb; }
        if(c) return c; ap+=la; bp+=lb; if(*ap=='.') ap++; if(*bp=='.') bp++; }
    return (*ap&&*ap!='+')?1:(*bp&&*bp!='+')?-1:0; }
/* newest "v<digit>..." tag_name in the API answer -> tag (empty if none). Drafts are not visible unauthenticated. */
static void upd_pick(const char *json, char *tag, size_t tn){ const char *p=json; tag[0]=0;
    while(json && (p=strstr(p,"\"tag_name\""))){ const char *q=strchr(p+10,'"'), *e; char t[64]; size_t l;
        if(!q) break; q++; e=strchr(q,'"'); if(!e) break; l=(size_t)(e-q); p=e;
        if(l<2||l>=sizeof t||q[0]!='v'||!isdigit((unsigned char)q[1])) continue; memcpy(t,q,l); t[l]=0;
        if(!tag[0]||upd_cmp(t,tag)>0){ strncpy(tag,t,tn-1); tag[tn-1]=0; } } }
/* human text for the result; returns 1 if a newer release exists */
static int upd_result(const char *json, const char *local, char *msg, size_t mn, char *url, size_t un){ char tag[64]; upd_pick(json,tag,sizeof tag);
    if(!tag[0]){ snprintf(msg,mn,"未找到发布版本。"); snprintf(url,un,"%s",UPD_RELEASES_URL); return 0; }
    snprintf(url,un,"%s/tag/%s",UPD_RELEASES_URL,tag);
    if(upd_cmp(tag,local)>0){ snprintf(msg,mn,"发现新版本 %s（当前 %s）。",tag,local); return 1; }
    snprintf(msg,mn,"已是最新版本（当前 %s，最新发布 %s）。",local,tag); return 0; }
#endif
