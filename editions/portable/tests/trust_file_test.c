/* Sending a file to an untrusted DM must fail before any message is recorded. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "core.h"
int main(void){
    char err[256]="", dir[]="/tmp/lsm-trustXXXXXX"; lsm_opts o={0}; lsm_core *c; lsm_msg *m; int n, f=0;
    if(!mkdtemp(dir)){ perror("mkdtemp"); return 1; }
    o.data_dir=dir; o.display_name="A"; o.listen_port=0; o.discovery_port=0; o.no_discovery=1;
    c=lsm_start(&o,err,sizeof err); if(!c){ printf("start %s\n",err); return 1; }
    /* invented peer fp (64 hex) — not trusted */
    char conv[80]; snprintf(conv,sizeof conv,"dm:%s","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    { FILE *fp=fopen("/tmp/lsm-trust-file.bin","wb"); fputs("hi",fp); fclose(fp); }
    if(lsm_send_file(c,conv,"/tmp/lsm-trust-file.bin",err)==0){ printf("FAIL: send should have been rejected\n"); f++; }
    else if(!strstr(err,"信任") && !strstr(err,"trust")){ printf("FAIL: err=%s\n",err); f++; }
    else printf("PASS: blocked with: %s\n",err);
    lsm_lock(c); n=lsm_messages(c,&m); if(n!=0){ printf("FAIL: %d messages recorded\n",n); f++; } else printf("PASS: no optimistic message\n"); lsm_unlock(c);
    lsm_stop(c); printf("%d failures\n",f); return f!=0;
}
