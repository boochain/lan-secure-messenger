/* upd_apply.h - download a GitHub release asset and replace this install.
 * Chat history lives in XDG data (~/.local/share/lan-messenger), not in the install tree, so the
 * copy does not touch it. A dev build (path contains /build/ or /editions/) is never replaced.
 * The helper script waits until this process exits, then moves the file or unpacks the tarball. */
#ifndef LSM_UPD_APPLY_H
#define LSM_UPD_APPLY_H
#include "upd_gio.h"
#include "../src/xcrypto.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <errno.h>

static void upd_self_pkg(char *needle, size_t nn, char *avoid, size_t an){ struct utsname u; const char *ai=getenv("APPIMAGE"); char arch[64]="x86_64", exe[1024]; ssize_t n;
    avoid[0]=0; if(uname(&u)==0 && u.machine[0]) snprintf(arch,sizeof arch,"%s",u.machine);
    n=readlink("/proc/self/exe",exe,sizeof exe-1); if(n<0) n=0; exe[n]=0;
    if((ai && strstr(ai,"adw")) || strstr(exe,"adw")){ snprintf(needle,nn,"adw-%s.AppImage",arch); return; }
    if(ai){ snprintf(needle,nn,"%s.AppImage",arch); snprintf(avoid,an,"adw"); return; }
    snprintf(needle,nn,"linux-%s.tar.gz",arch); }

/* 1 = this process is a packaged install we may replace. dest is the AppImage path or the tarball prefix. kind 1 = single file, 2 = directory. */
static int upd_install_dest(char *dest, size_t n, int *kind){ const char *ai=getenv("APPIMAGE"); char exe[1024]; ssize_t l; char *bin, *slash;
    if(ai && ai[0] && !strstr(ai,"/build/") && !strstr(ai,"/editions/")){ snprintf(dest,n,"%s",ai); *kind=1; return 1; }
    l=readlink("/proc/self/exe",exe,sizeof exe-1); if(l<0) return 0; exe[l]=0;
    if(strstr(exe,"/build/") || strstr(exe,"/editions/")) return 0;
    bin=strrchr(exe,'/'); if(!bin || strcmp(bin+1,"lan-messenger-gtk") && strcmp(bin+1,"lan-messenger-adw")) return 0;
    *bin=0; slash=strrchr(exe,'/'); if(!slash || strcmp(slash+1,"bin")) return 0;
    *slash=0; snprintf(dest,n,"%s",exe); *kind=2; return 1; }

static int upd_sha_file(const char *path, char hex[65]){ FILE *f=fopen(path,"rb"); sha256_ctx c; unsigned char buf[65536]; uint8_t out[32]; size_t n; int i;
    if(!f) return 0; sha256_init(&c); while((n=fread(buf,1,sizeof buf,f))>0) sha256_update(&c,buf,n); fclose(f); sha256_final(&c,out);
    for(i=0;i<32;i++) sprintf(hex+i*2,"%02x",out[i]); hex[64]=0; return 1; }
static int upd_sha_match(const char *sums, const char *filename, const char *file){ FILE *f=fopen(sums,"rb"); char line[800], got[65], *sp; size_t fl;
    if(!f) return 0; fl=strlen(filename);
    while(fgets(line,sizeof line,f)){ char *nl=strchr(line,'\n'); if(nl) *nl=0; sp=strchr(line,' '); if(!sp) continue; *sp++=0;
        while(*sp==' '||*sp=='*') sp++; if(strlen(sp)>=fl && !strcmp(sp+strlen(sp)-fl,filename)){ fclose(f); if(!upd_sha_file(file,got)) return 0; return !strcmp(line,got); } }
    fclose(f); return 0; }
static int upd_curl(const char *url, const char *dest){ GError *e=NULL; GSubprocess *sp; const char *tok=getenv("GITHUB_TOKEN"); char *auth=NULL; int ok;
    if(!tok||!*tok) tok=getenv("GH_TOKEN");
    if(tok&&*tok) auth=g_strdup_printf("Authorization: Bearer %s",tok);
    if(auth) sp=g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE|G_SUBPROCESS_FLAGS_STDERR_SILENCE,&e,"curl","-fsSL","--max-time","600","--retry","2","--retry-delay","1","--retry-connrefused","-H",auth,"-A","LanMessenger-Linux","-o",dest,url,NULL);
    else sp=g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE|G_SUBPROCESS_FLAGS_STDERR_SILENCE,&e,"curl","-fsSL","--max-time","600","--retry","2","--retry-delay","1","--retry-connrefused","-A","LanMessenger-Linux","-o",dest,url,NULL);
    g_free(auth); if(!sp){ g_clear_error(&e); return 0; }
    ok=g_subprocess_wait_check(sp,NULL,&e); g_object_unref(sp); g_clear_error(&e); return ok; }

/* 2 = helper spawned, caller must exit. 0 = failed (err set). Does not delete the data directory. */
static int upd_fetch_and_apply(const char *asset_url, const char *sums_url, const char *name, char *err, size_t en){
    char dir[128], pkg[256], sums[256], dest[1024], script[256], exe[1024]; int kind=0; FILE *sf; ssize_t el; GError *e=NULL;
    if(!upd_install_dest(dest,sizeof dest,&kind)){ snprintf(err,en,"当前不是安装包（开发目录不会被替换）。请从发布页下载。"); return 0; }
    if(!sums_url||!sums_url[0]){ snprintf(err,en,"这个版本没有 SHA256SUMS，已停止安装。"); return 0; }
    snprintf(dir,sizeof dir,"/tmp/lsm-update-%d",(int)getpid()); g_mkdir_with_parents(dir,0700);
    snprintf(pkg,sizeof pkg,"%s/pkg",dir); snprintf(sums,sizeof sums,"%s/SHA256SUMS.txt",dir);
    if(!upd_curl(asset_url,pkg) || !upd_curl(sums_url,sums)){ snprintf(err,en,"下载更新包失败。"); return 0; }
    if(!upd_sha_match(sums,name,pkg)){ snprintf(err,en,"更新包校验失败，没有替换任何文件。"); unlink(pkg); return 0; }
    snprintf(script,sizeof script,"%s/apply.sh",dir); sf=fopen(script,"w"); if(!sf){ snprintf(err,en,"无法写入安装脚本。"); return 0; }
    el=readlink("/proc/self/exe",exe,sizeof exe-1); if(el<0) el=0; exe[el]=0;
    fputs("#!/bin/sh\n"
        "PID=\"$1\"; MODE=\"$2\"; SRC=\"$3\"; DEST=\"$4\"; RESTART=\"$5\"\n"
        "i=0\nwhile kill -0 \"$PID\" 2>/dev/null; do i=$((i+1)); [ \"$i\" -gt 200 ] && exit 1; sleep 0.25; done\n"
        "if [ \"$MODE\" = file ]; then mv -f \"$SRC\" \"$DEST\" && chmod 755 \"$DEST\" && exec \"$DEST\"; fi\n"
        "stage=$(mktemp -d /tmp/lsm-upd-XXXXXX) || exit 1\n"
        "tar -xzf \"$SRC\" -C \"$stage\" || exit 1\n"
        "top=$(find \"$stage\" -mindepth 1 -maxdepth 1 -type d | head -n 1)\n"
        "[ -n \"$top\" ] || exit 1\n"
        "cp -a \"$top\"/. \"$DEST\"/\n"
        "exec \"$RESTART\"\n", sf);
    fclose(sf); chmod(script,0700);
    { char pidb[32], mode[8]; char *av[8]; snprintf(pidb,sizeof pidb,"%d",(int)getpid()); snprintf(mode,sizeof mode,"%s",kind==1?"file":"dir");
      av[0]="setsid"; av[1]=script; av[2]=pidb; av[3]=mode; av[4]=pkg; av[5]=dest; av[6]=kind==1?dest:exe; av[7]=NULL;
      if(!g_spawn_async(NULL,av,NULL,G_SPAWN_SEARCH_PATH|G_SPAWN_DO_NOT_REAP_CHILD|G_SPAWN_STDOUT_TO_DEV_NULL|G_SPAWN_STDERR_TO_DEV_NULL,NULL,NULL,NULL,&e)){
          snprintf(err,en,"无法启动安装程序：%s",e&&e->message?e->message:"spawn"); g_clear_error(&e); return 0; } }
    snprintf(err,en,"即将退出并安装更新。聊天记录仍在数据目录。"); return 2; }
#endif
