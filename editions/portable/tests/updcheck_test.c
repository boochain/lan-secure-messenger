#include <stdio.h>
#include "updcheck.h"  /* cc -Isrc tests/updcheck_test.c */
int main(void){ int f=0; char m[200],u[200];
#define T(c) if(!(c)){ printf("FAIL %s\n",#c); f++; }
T(upd_cmp("v0.6.0-beta.10","v0.6.0-beta.9")>0) T(upd_cmp("v0.6.0","0.6.0-beta.1")>0) T(upd_cmp("v0.6.0-beta.1","0.5.0")>0) T(upd_cmp("v0.5.0","0.5.0")==0)
T(upd_cmp("v0.4.9","v0.5.0-beta.1")<0) T(upd_cmp("v1.0","0.9.9")>0) T(upd_cmp("v0.6.0-beta","v0.6.0-beta.1")<0)
T(upd_result("[{\"tag_name\":\"v0.5.0\"},{\"author\":{\"x\":1},\"tag_name\":\"v0.6.0-beta.1\"},{\"tag_name\":\"xp-1\"}]","0.5.0",m,200,u,200)==1 && strstr(u,"/tag/v0.6.0-beta.1"))
T(upd_result("[]","0.5.0",m,200,u,200)==0)
printf("%d failures\n",f); return f; }
