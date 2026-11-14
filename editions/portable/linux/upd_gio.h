/* upd_gio.h - update check for the GTK UIs: curl (GSubprocess, async) fetches the GitHub releases API,
 * src/updcheck.h parses it. No libsoup dependency (GTK 3 build targets Ubuntu 20.04 and newer).
 * CI exports GITHUB_TOKEN; unauthenticated api.github.com is 60/hour/IP and runners share IPs, so a
 * bare curl -f often dies with 403 and the GUI reports "网络不可用". The token is not logged. */
#include "../src/updcheck.h"
typedef void (*upd_cb)(int newer /* 1 newer, 0 up to date, -1 failed */, const char *msg, const char *url);
typedef struct { upd_cb cb; } upd_ctx;
static void upd_open_url(const char *url){ GError *e=NULL; if(!g_app_info_launch_default_for_uri(url,NULL,&e)) g_clear_error(&e); }
static void upd_fetch_done(GObject *src, GAsyncResult *res, gpointer p){ upd_ctx *x=p; char *out=NULL, m[300], url[300]; GError *e=NULL;
    if(!g_subprocess_communicate_utf8_finish(G_SUBPROCESS(src),res,&out,NULL,&e) || !g_subprocess_get_successful(G_SUBPROCESS(src)) || !out)
        x->cb(-1,"无法连接 GitHub 检查更新（网络不可用或未安装 curl）。",UPD_RELEASES_URL);
    else { int r=upd_result(out,LSM_APP_VERSION,m,sizeof m,url,sizeof url); x->cb(r,m,url); }
    g_free(out); g_clear_error(&e); g_object_unref(src); g_free(x); }
static void upd_start(upd_cb cb){ GError *e=NULL; upd_ctx *x; GSubprocess *sp; const char *tok=getenv("GITHUB_TOKEN"); char *auth=NULL;
    if(!tok||!*tok) tok=getenv("GH_TOKEN");
    if(tok&&*tok) auth=g_strdup_printf("Authorization: Bearer %s",tok);
    /* --max-time covers retries. Connection drops retry; HTTP 4xx does not (a token avoids the CI 403). */
    if(auth)
        sp=g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_SILENCE,&e,"curl","-fsSL","--max-time","20","--retry","2","--retry-delay","1","--retry-connrefused","-H","Accept: application/vnd.github+json","-H",auth,"-A","LanMessenger-Linux","https://" UPD_API_HOST UPD_API_PATH,NULL);
    else
        sp=g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_SILENCE,&e,"curl","-fsSL","--max-time","20","--retry","2","--retry-delay","1","--retry-connrefused","-H","Accept: application/vnd.github+json","-A","LanMessenger-Linux","https://" UPD_API_HOST UPD_API_PATH,NULL);
    g_free(auth);
    if(!sp){ g_clear_error(&e); cb(-1,"未找到 curl，无法检查更新。",UPD_RELEASES_URL); return; }
    x=g_new0(upd_ctx,1); x->cb=cb; g_subprocess_communicate_utf8_async(sp,NULL,NULL,upd_fetch_done,x); }