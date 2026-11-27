/* upd_asset_url picks the release asset by file name. gcc -I editions/portable/src upd_asset_test.c */
#include <stdio.h>
#include <string.h>
#include "updcheck.h"
static int fail;
static void expect(int cond, const char *what){ if(!cond){ fprintf(stderr,"FAIL %s\n",what); fail++; } else printf("PASS %s\n",what); }
int main(void){
    const char *json =
        "[{\"tag_name\":\"v0.7.3\",\"name\":\"v0.7.3\",\"draft\":false,\"assets\":["
        "{\"name\":\"LanMessenger-v0.7.3-win-x64.zip\",\"browser_download_url\":\"https://example/old-win\"},"
        "{\"name\":\"SHA256SUMS.txt\",\"browser_download_url\":\"https://example/old-sums\"}]},"
        "{\"tag_name\":\"v0.7.4\",\"name\":\"v0.7.4\",\"draft\":false,\"assets\":["
        "{\"name\":\"LanMessenger-v0.7.4-win-x64.zip\",\"browser_download_url\":\"https://example/win64\"},"
        "{\"name\":\"LanMessenger-v0.7.4-win-arm64.zip\",\"browser_download_url\":\"https://example/winarm\"},"
        "{\"name\":\"LanMessenger-XP-v0.7.4.zip\",\"browser_download_url\":\"https://example/xp\"},"
        "{\"name\":\"LanMessenger-v0.7.4-x86_64.AppImage\",\"browser_download_url\":\"https://example/gtk\"},"
        "{\"name\":\"LanMessenger-adw-v0.7.4-x86_64.AppImage\",\"browser_download_url\":\"https://example/adw\"},"
        "{\"name\":\"lan-messenger-v0.7.4-linux-x86_64.tar.gz\",\"browser_download_url\":\"https://example/tar\"},"
        "{\"name\":\"LanMessenger-v0.7.4-android.apk\",\"browser_download_url\":\"https://example/apk\"},"
        "{\"name\":\"SHA256SUMS.txt\",\"browser_download_url\":\"https://example/sums\"}]}]";
    char url[300], name[200], tag[64], msg[200];
    expect(upd_cmp("v0.7.4","v0.7.3")>0, "0.7.4 newer than 0.7.3");
    expect(upd_cmp("v0.7.4","v0.7.4")==0, "same version");
    upd_pick(json, tag, sizeof tag);
    expect(strcmp(tag,"v0.7.4")==0, "newest tag");
    expect(upd_result(json,"0.7.4",msg,sizeof msg,url,sizeof url)==0, "up to date");
    expect(strncmp(msg,"已是最新版本",sizeof("已是最新版本")-1)==0, "up to date text");
    expect(upd_result(json,"0.7.3",msg,sizeof msg,url,sizeof url)==1, "newer");
    expect(upd_asset_url(json,"v0.7.4","win-x64.zip",NULL,url,sizeof url,name,sizeof name)==1, "win64 found");
    expect(strcmp(url,"https://example/win64")==0 && strcmp(name,"LanMessenger-v0.7.4-win-x64.zip")==0, "win64 url");
    expect(upd_asset_url(json,"v0.7.4","win-arm64.zip",NULL,url,sizeof url,name,sizeof name)==1, "arm64 found");
    expect(strcmp(url,"https://example/winarm")==0, "arm64 url");
    expect(upd_asset_url(json,"v0.7.4","LanMessenger-XP-",NULL,url,sizeof url,name,sizeof name)==1, "xp found");
    expect(strcmp(url,"https://example/xp")==0, "xp url");
    expect(upd_asset_url(json,"v0.7.4","x86_64.AppImage","adw",url,sizeof url,name,sizeof name)==1, "gtk appimage");
    expect(strcmp(url,"https://example/gtk")==0 && !strstr(name,"adw"), "gtk not adw");
    expect(upd_asset_url(json,"v0.7.4","adw-v0.7.4-x86_64.AppImage",NULL,url,sizeof url,name,sizeof name)==1, "adw found");
    expect(strcmp(url,"https://example/adw")==0, "adw url");
    expect(upd_asset_url(json,"v0.7.4","linux-x86_64.tar.gz",NULL,url,sizeof url,name,sizeof name)==1, "tar found");
    expect(upd_asset_url(json,"v0.7.4","android.apk",NULL,url,sizeof url,name,sizeof name)==1, "apk found");
    expect(upd_asset_url(json,"v0.7.4","SHA256SUMS",NULL,url,sizeof url,NULL,0)==1 && strcmp(url,"https://example/sums")==0, "sums");
    expect(upd_asset_url(json,"v0.7.4","no-such",NULL,url,sizeof url,NULL,0)==0, "missing");
    expect(upd_asset_url(json,"v0.7.3","win-x64.zip",NULL,url,sizeof url,name,sizeof name)==1 && strcmp(url,"https://example/old-win")==0, "older release");
    return fail?1:0;
}
