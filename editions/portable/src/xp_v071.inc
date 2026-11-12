/* v0.7.1: first-run account/device choice + QR URI apply.
 * Hooks: LSMF_account_mode LSMF_qr_scan LSMF_qr_server LSMF_qr_pair LSMF_qr_profile */
#define IDM_SERVERQR 267
#define IDM_PAIRQR   268
#define IDM_ACCTMODE 269
#define IDM_REVEALRK 270
#define IDM_SCANIMG  271
static int v071_fr_reshoot, v071_fr_picked;
#define ID_FR_DEV 4001
#define ID_FR_ACC 4002
#define ID_FR_OK  4003
static LRESULT CALLBACK v071_fr_proc(HWND h, UINT m, WPARAM w, LPARAM l){
    if(m==WM_CREATE){
        int sp=scale_px(12), y=scale_px(TITLE_H)+sp, w0=scale_px(516)-sp*2; HWND s,b;
        s=CreateWindowW(L"STATIC",L"请选择身份方式（之后可在设置里更改，不使用服务器密码）。添加联系人、连接服务器、配对新设备都可以通过扫描或粘贴 lsm:// 链接完成。",
            WS_CHILD|WS_VISIBLE, sp,y,w0,scale_px(70),h,NULL,hInst,NULL);
        if(fBody) SendMessageW(s,WM_SETFONT,(WPARAM)fBody,TRUE);
        y+=scale_px(78);
        b=CreateWindowW(L"BUTTON",L"设备模式 — 仅本机，局域网直连，不把身份放到服务器",
            WS_CHILD|WS_VISIBLE|BS_AUTORADIOBUTTON|WS_GROUP, sp,y,w0,scale_px(28),h,(HMENU)(INT_PTR)ID_FR_DEV,hInst,NULL);
        if(fBody) SendMessageW(b,WM_SETFONT,(WPARAM)fBody,TRUE);
        SendMessageW(b,BM_SETCHECK,BST_CHECKED,0);
        y+=scale_px(36);
        b=CreateWindowW(L"BUTTON",L"账号模式 — 身份加密备份在中转上，换机可用恢复密钥或配对二维码恢复",
            WS_CHILD|WS_VISIBLE|BS_AUTORADIOBUTTON, sp,y,w0,scale_px(44),h,(HMENU)(INT_PTR)ID_FR_ACC,hInst,NULL);
        if(fBody) SendMessageW(b,WM_SETFONT,(WPARAM)fBody,TRUE);
        y+=scale_px(52);
        b=CreateWindowW(L"BUTTON",L"开始使用",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW, scale_px(384),y,scale_px(120),scale_px(32),h,(HMENU)IDOK,hInst,NULL);
        if(fBody) SendMessageW(b,WM_SETFONT,(WPARAM)fBody,TRUE);
        return 0;
    }
    if(m==WM_COMMAND && (LOWORD(w)==ID_FR_OK || LOWORD(w)==IDOK)){
        v071_fr_picked = (SendDlgItemMessageW(h,ID_FR_ACC,BM_GETCHECK,0,0)==BST_CHECKED);
        DestroyWindow(h); return 0;
    }
    if(m==WM_CLOSE){ v071_fr_picked=0; DestroyWindow(h); return 0; }
    if(m==WM_CTLCOLORSTATIC || m==WM_CTLCOLORBTN || m==WM_CTLCOLORDLG) return ctl_color((HDC)w,(HWND)l,0);
    if(m==WM_ERASEBKGND){ RECT r; GetClientRect(h,&r); FillRect((HDC)w,&r,brWin); return 1; }
    if(m==WM_PAINT){ PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps); RECT cr,tr; wchar_t t[128]; HBRUSH dv=CreateSolidBrush(T.divider);
        GetClientRect(h,&cr);
        tr=cr; tr.bottom=scale_px(TITLE_H); FillRect(dc,&tr,brCard); tr.top=scale_px(TITLE_H)-1; FillRect(dc,&tr,dv); FrameRect(dc,&cr,dv); DeleteObject(dv);
        GetWindowTextW(h,t,128); tr=cr; tr.left=scale_px(12); tr.bottom=scale_px(TITLE_H); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,T.text_primary); SelectObject(dc,fBodyStrong); DrawTextW(dc,t,-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        EndPaint(h,&ps); return 0; }
    if(m==WM_NCHITTEST){ POINT pt={(short)LOWORD(l),(short)HIWORD(l)}; ScreenToClient(h,&pt); if(pt.y<scale_px(TITLE_H)) return HTCAPTION; }
    if(m==WM_DRAWITEM){ DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT*)l; if(d->CtlType==ODT_BUTTON){ draw_button(d); return TRUE; } }
    return DefWindowProcW(h,m,w,l);
}
static void v071_first_run(void){
    char err[256]=""; static int reg; RECT r; HWND h; MSG msg;
    int shot=v071_fr_reshoot; v071_fr_reshoot=0;
    if(lsm_first_run_done(C) && !shot) return;
    if(!shot && getenv("LSM_UI_TEST") && !getenv("LSM_SHOT_FIRST_RUN")){ lsm_complete_first_run(C,NULL,"device",NULL,err); return; }
    if(!reg){ WNDCLASSEXW wc; memset(&wc,0,sizeof wc); wc.cbSize=sizeof wc; wc.lpfnWndProc=v071_fr_proc; wc.hInstance=hInst; wc.lpszClassName=L"LsmFirstRun"; wc.hCursor=LoadCursor(NULL,IDC_ARROW); wc.hbrBackground=brWin; wc.style=0x00020000; /* CS_DROPSHADOW */ RegisterClassExW(&wc); reg=1; }
    GetWindowRect(hMain,&r);
    v071_fr_picked=0;
    h=CreateWindowExW(WS_EX_TOOLWINDOW,L"LsmFirstRun",L"欢迎使用局域网安全通讯",WS_POPUP,
        r.left+scale_px(80),r.top+scale_px(80),scale_px(540),scale_px(320),hMain,NULL,hInst,NULL); /* no system caption: title bar drawn from the theme */
    EnableWindow(hMain,FALSE);
    ShowWindow(h,SW_SHOW); SetFocus(h);
    while(IsWindow(h) && GetMessage(&msg,NULL,0,0)){
        if(msg.message==WM_QUIT){ PostQuitMessage((int)msg.wParam); break; }
        if(!IsDialogMessageW(h,&msg)){ TranslateMessage(&msg); DispatchMessage(&msg); }
    }
    EnableWindow(hMain,TRUE); SetForegroundWindow(hMain);
    if(!lsm_first_run_done(C)){
        lsm_complete_first_run(C,NULL, v071_fr_picked?"account":"device", NULL, err);
        if(v071_fr_picked){ char rk[80]=""; if(!lsm_reveal_recovery_key(C,rk,sizeof rk) && rk[0]){
            char m[256]; snprintf(m,sizeof m,"请抄下恢复密钥并妥善保管：\r\n%s", rk); msgbox(m,MB_ICONINFORMATION); } }
    }
}
static void v071_shot_first_run(void){ if(!getenv("LSM_UI_TEST")) return; v071_fr_reshoot=1; v071_first_run(); }
static void v071_show_server_qr(void){ char u[400]=""; if(lsm_relay_uri(C,u,sizeof u)||!u[0]){ if(getenv("LSM_UI_TEST")) snprintf(u,sizeof u,"lsm://relay?h=ci.example.invalid&p=9443"); else { msgbox("请先在设置里填写中转服务器",MB_ICONINFORMATION); return; } } ux_show_qr(L"服务器二维码",u); }
static void v071_show_pair_qr(void){ char u[500]="",err[256]=""; if(lsm_account_pair_uri(C,u,sizeof u,err)||!u[0]){ if(getenv("LSM_UI_TEST")) snprintf(u,sizeof u,"lsm://pair?r=ci.example.invalid:9443&t=%s&k=%s","0000000000000000000000000000000000000000000000000000000000000000","1111111111111111111111111111111111111111111111111111111111111111"); else { msgbox(err[0]?err:"无法生成配对码",MB_ICONWARNING); return; } } ux_show_qr(L"配对新设备",u); }
static void v071_paste_uri(void){ prompt_t p; char u8[1600]; memset(&p,0,sizeof p);
    if(prompt(L"扫描或粘贴链接",L"粘贴 lsm://add、lsm://relay 或 lsm://pair 链接：",L"",&p,1) && p.text[0]) ux_add_uri_text(U(p.text,u8,sizeof u8)); }

static void v071_switch_mode(void){
    char rk[80]="", err[256]=""; const char *cur=lsm_account_mode(C); int to_acc=strcmp(cur,"account")!=0;
    if(lsm_set_account_mode(C, to_acc?"account":"device", rk, sizeof rk, err)) msgbox(err[0]?err:"切换失败",MB_ICONWARNING);
    else if(to_acc && rk[0]){ char m[256]; snprintf(m,sizeof m,"已切换到账号模式。请抄下恢复密钥：\r\n%s",rk); msgbox(m,MB_ICONINFORMATION); }
    else msgbox(to_acc?"已切换到账号模式":"已切换到设备模式",MB_ICONINFORMATION);
}
static void v071_reveal_rk(void){ char rk[80]=""; if(lsm_reveal_recovery_key(C,rk,sizeof rk)||!rk[0]) msgbox("还没有恢复密钥（请先开启账号模式）",MB_ICONINFORMATION); else { char m[256]; snprintf(m,sizeof m,"恢复密钥（请妥善保管，服务器看不到明文）：\r\n%s",rk); msgbox(m,MB_ICONINFORMATION); } }

static void v071_scan_image(void){ /* LSMF_qr_scan — image file or text containing lsm://; zbarimg if present */
    OPENFILENAMEW of; wchar_t path[MAX_PATH]; char u8[1200], uri[800]="";
    const char *shot=getenv("LSM_SHOT_QR_FILE");
    if(shot && shot[0]){
        if(lsm_uri_from_file(shot,uri,sizeof uri)||!uri[0]) msgbox("无法从该文件识别二维码，请改为粘贴 lsm:// 链接。",MB_ICONWARNING);
        else ux_show_qr(L"从图片识别",uri);
        return;
    }
    memset(&of,0,sizeof of); of.lStructSize=sizeof of; of.hwndOwner=hMain; path[0]=0; of.lpstrFile=path; of.nMaxFile=MAX_PATH;
    of.lpstrFilter=L"图片或文本\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.txt\0所有文件\0*.*\0";
    of.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_HIDEREADONLY;
    if(!GetOpenFileNameW(&of)) return;
    WideCharToMultiByte(CP_UTF8,0,path,-1,u8,sizeof u8,NULL,NULL);
    if(lsm_uri_from_file(u8,uri,sizeof uri)) msgbox("无法从该文件识别二维码，请改为粘贴 lsm:// 链接。",MB_ICONWARNING);
    else ux_add_uri_text(uri);
}
