/* host firewall policy (C core): never automatic. off -> FW_OFF_MSG and no command; on -> exact command, applied only on request. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core.h"
int main(void){ char cmd[1400], msg[2048], rep[2048]; int f=0, r;
#define T(c) if(!(c)){ printf("FAIL %s\n",#c); f++; } else printf("PASS %s\n",#c);
    setenv("LSM_FIREWALL_STATE","off",1); r=lsm_firewall_plan("/x",cmd,sizeof cmd,msg,sizeof msg); printf("off: %s\n",msg);
    T(r==0) T(cmd[0]==0) T(strstr(msg,"本机防火墙未启用，不做修改")!=NULL) T(strstr(msg,"安全组")!=NULL)
    T(lsm_firewall_apply(cmd,rep,sizeof rep)==0 && strstr(rep,"不做修改"))
    setenv("LSM_FIREWALL_STATE","on",1); r=lsm_firewall_plan("/x",cmd,sizeof cmd,msg,sizeof msg); printf("on: %s\n",msg);
    T(r==1) T(cmd[0]!=0) T(strstr(msg,cmd)!=NULL) T(strstr(msg,"安全组")!=NULL)
    setenv("LSM_FIREWALL_DRYRUN","1",1); T(lsm_firewall_apply(cmd,rep,sizeof rep)==0 && strstr(rep,"dry run"))
    unsetenv("LSM_FIREWALL_STATE"); r=lsm_firewall_plan("/x",cmd,sizeof cmd,msg,sizeof msg); printf("real host: %d %s\n",r,msg);
    printf("%d failures\n",f); return f!=0; }
