/* v0.7.1: first-run account/device choice + QR URI apply.
 * Hooks: LSMF_account_mode LSMF_qr_scan LSMF_qr_server LSMF_qr_pair LSMF_qr_profile */
#define IDM_SERVERQR 267
#define IDM_PAIRQR   268
#define IDM_ACCTMODE 269
#define IDM_REVEALRK 270
static void v071_first_run(void){
    char err[256]=""; int r;
    if(lsm_first_run_done(C)) return;
    if(getenv("LSM_UI_TEST") && !getenv("LSM_SHOT_FIRST_RUN")){ lsm_complete_first_run(C,NULL,"device",NULL,err); return; }
    r=MessageBoxW(hMain,
        L"欢迎使用局域网安全通讯\r\n\r\n请选择身份方式（之后可在设置里更改，不使用服务器密码）。\r\n\r\n是 = 设备模式（仅本机，局域网直连）\r\n否 = 账号模式（身份加密备份在中转上，换机可用恢复密钥或配对二维码恢复）",
        L"欢迎使用", MB_YESNO|MB_ICONQUESTION);
    lsm_complete_first_run(C,NULL, r==IDNO?"account":"device", NULL, err);
    if(r==IDNO){ char rk[80]=""; if(!lsm_reveal_recovery_key(C,rk,sizeof rk) && rk[0]){
        char m[256]; snprintf(m,sizeof m,"请抄下恢复密钥并妥善保管：\r\n%s", rk); msgbox(m,MB_ICONINFORMATION); } }
}
static void v071_show_uri(const char *title, const char *uri){ char m[700]; snprintf(m,sizeof m,"%s\r\n\r\n%s", title, uri); msgbox(m,MB_ICONINFORMATION); }
static void v071_show_server_qr(void){ char u[400]=""; if(lsm_relay_uri(C,u,sizeof u)||!u[0]) msgbox("请先在设置里填写中转服务器",MB_ICONINFORMATION); else v071_show_uri("服务器二维码（可复制链接到另一台设备）",u); }
static void v071_show_pair_qr(void){ char u[500]="",err[256]=""; if(lsm_account_pair_uri(C,u,sizeof u,err)) msgbox(err[0]?err:"无法生成配对码",MB_ICONWARNING); else v071_show_uri("配对新设备（一次性链接，10 分钟有效）",u); }
static void v071_switch_mode(void){
    char rk[80]="", err[256]=""; const char *cur=lsm_account_mode(C); int to_acc=strcmp(cur,"account")!=0;
    if(lsm_set_account_mode(C, to_acc?"account":"device", rk, sizeof rk, err)) msgbox(err[0]?err:"切换失败",MB_ICONWARNING);
    else if(to_acc && rk[0]){ char m[256]; snprintf(m,sizeof m,"已切换到账号模式。请抄下恢复密钥：\r\n%s",rk); msgbox(m,MB_ICONINFORMATION); }
    else msgbox(to_acc?"已切换到账号模式":"已切换到设备模式",MB_ICONINFORMATION);
}
static void v071_reveal_rk(void){ char rk[80]=""; if(lsm_reveal_recovery_key(C,rk,sizeof rk)||!rk[0]) msgbox("还没有恢复密钥（请先开启账号模式）",MB_ICONINFORMATION); else { char m[256]; snprintf(m,sizeof m,"恢复密钥（请妥善保管，服务器看不到明文）：\r\n%s",rk); msgbox(m,MB_ICONINFORMATION); } }
