#include <stdio.h>
#include "updcheck.h"  /* cc -Isrc tests/updcheck_test.c */
int main(int argc, char **argv){ int f=0; char m[200],u[200];
if(argc==3){ /* live mode: updcheck_test releases.json <local version> -> prints the app's message, fails unless up to date */
  static char j[1<<20]; FILE *fp=fopen(argv[1],"rb"); size_t n=fp?fread(j,1,sizeof j-1,fp):0; j[n]=0; if(fp) fclose(fp);
  int r=upd_result(j,argv[2],m,sizeof m,u,sizeof u); printf("local %s -> %s\n",argv[2],m); return r==0&&strstr(m,"已是最新版本")?0:1; }
#define T(c) if(!(c)){ printf("FAIL %s\n",#c); f++; }
T(upd_cmp("v0.6.0-beta.10","v0.6.0-beta.9")>0) T(upd_cmp("v0.6.0","0.6.0-beta.1")>0) T(upd_cmp("v0.6.0-beta.1","0.5.0")>0) T(upd_cmp("v0.5.0","0.5.0")==0)
T(upd_cmp("v0.4.9","v0.5.0-beta.1")<0) T(upd_cmp("v1.0","0.9.9")>0) T(upd_cmp("v0.6.0-beta","v0.6.0-beta.1")<0)
T(upd_result("[{\"tag_name\":\"v0.5.0\"},{\"author\":{\"x\":1},\"tag_name\":\"v0.6.0-beta.1\"},{\"tag_name\":\"xp-1\"}]","0.5.0",m,200,u,200)==1 && strstr(u,"/tag/v0.6.0-beta.1"))
T(upd_result("[]","0.5.0",m,200,u,200)==0)
/* release v0.6.0: final > every beta, equal = up to date */
T(upd_cmp("v0.6.0","0.6.0-beta.2")>0) T(upd_cmp("v0.6.0","0.6.0")==0)
T(upd_result("[{\"tag_name\":\"v0.6.0\"},{\"tag_name\":\"v0.6.0-beta.2\"}]","0.6.0",m,200,u,200)==0)
/* release v0.6.0-beta.2: the same version is up to date, beta.2 > beta.1 */
T(upd_cmp("v0.6.0-beta.2","0.6.0-beta.2")==0) T(upd_cmp("v0.6.0-beta.2","0.6.0-beta.1")>0) T(upd_cmp("v0.6.0-beta.1","0.6.0-beta.2")<0)
T(upd_result("[{\"tag_name\":\"v0.6.0-beta.2\"},{\"tag_name\":\"v0.6.0-beta.1\"}]","0.6.0-beta.2",m,200,u,200)==0 && strstr(m,"已是最新版本"))
T(upd_result("[{\"tag_name\":\"v0.6.0-beta.2\"}]","0.6.0-beta.1",m,200,u,200)==1)
printf("%d failures\n",f); return f; }
