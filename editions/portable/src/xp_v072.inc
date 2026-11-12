/* v0.7.2: dedicated recovery-key page (LSMF_recovery_key). Replaces the short-lived msgbox. */
#define ID_RK_COPY 4101
#define ID_RK_EXPORT 4102
#define ID_RK_ACK 4103
#define ID_RK_KEY 4104
static int v072_rk_force, v072_rk_acked, v072_rk_done;
static char v072_rk_text[80];
static HWND v072_rk_ok, v072_rk_win;
static int v072_load_key(char *rk, size_t n){
    char err[256]="", tmp[80]="";
    if(rk) rk[0]=0;
    if(!lsm_reveal_recovery_key(C,rk,n) && rk[0]) return 0;
    if(getenv("LSM_UI_TEST")){
        if(!lsm_set_account_mode(C,"account",tmp,sizeof tmp,err) && tmp[0]){ snprintf(rk,n,"%s",tmp); return 0; }
        if(!lsm_reveal_recovery_key(C,rk,n) && rk[0]) return 0;
        snprintf(rk,n,"RK1-TEST-XXXX-XXXX-XXXX"); return 0;
    }
    return -1;
}
static void v072_rk_export(void){
    OPENFILENAMEW of; wchar_t path[MAX_PATH]; FILE *fp; char u8[MAX_PATH];
    memset(&of,0,sizeof of); of.lStructSize=sizeof of; of.hwndOwner=v072_rk_win?v072_rk_win:hMain;
    wcscpy(path,L"recovery-key.txt"); of.lpstrFile=path; of.nMaxFile=MAX_PATH;
    of.lpstrFilter=L"文本文件\0*.txt\0所有文件\0*.*\0"; of.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_HIDEREADONLY;
    of.lpstrTitle=L"导出恢复密钥";
    if(!GetSaveFileNameW(&of)) return;
    WideCharToMultiByte(CP_UTF8,0,path,-1,u8,sizeof u8,NULL,NULL);
    fp=fopen(u8,"wb"); if(fp){ fputs(v072_rk_text,fp); fputc('\n',fp); fclose(fp); }
}
static void v072_rk_enable(void){ if(v072_rk_ok) EnableWindow(v072_rk_ok, !v072_rk_force || v072_rk_acked); }
static LRESULT CALLBACK v072_rk_proc(HWND h, UINT m, WPARAM w, LPARAM l){
    if(m==WM_CREATE){
        int sp=scale_px(16), y=scale_px(TITLE_H)+sp, w0=scale_px(520)-sp*2; HWND s,b,e;
        s=CreateWindowW(L"STATIC",L"恢复密钥",WS_CHILD|WS_VISIBLE, sp,y,w0,scale_px(28),h,NULL,hInst,NULL);
        if(fBodyStrong) SendMessageW(s,WM_SETFONT,(WPARAM)fBodyStrong,TRUE);
        y+=scale_px(32);
        s=CreateWindowW(L"STATIC",L"这是你的账号恢复密钥。请把它抄到离线的安全地方。任何拿到这串密钥的人都能恢复你的身份。服务器看不到明文。",
            WS_CHILD|WS_VISIBLE, sp,y,w0,scale_px(64),h,NULL,hInst,NULL);
        if(fBody) SendMessageW(s,WM_SETFONT,(WPARAM)fBody,TRUE);
        y+=scale_px(72);
        e=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",W(v072_rk_text),
            WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL, sp,y,w0,scale_px(88),h,(HMENU)(INT_PTR)ID_RK_KEY,hInst,NULL);
        { static HFONT mono; if(!mono) mono=mkfont(L"Consolas",scale_px(18),400); SendMessageW(e,WM_SETFONT,(WPARAM)(mono?mono:fBody),TRUE); }
        y+=scale_px(100);
        b=CreateWindowW(L"BUTTON",L"复制",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW, sp,y,scale_px(88),scale_px(32),h,(HMENU)(INT_PTR)ID_RK_COPY,hInst,NULL);
        if(fBody) SendMessageW(b,WM_SETFONT,(WPARAM)fBody,TRUE);
        b=CreateWindowW(L"BUTTON",L"导出到文件…",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW, sp+scale_px(100),y,scale_px(130),scale_px(32),h,(HMENU)(INT_PTR)ID_RK_EXPORT,hInst,NULL);
        if(fBody) SendMessageW(b,WM_SETFONT,(WPARAM)fBody,TRUE);
        y+=scale_px(44);
        b=CreateWindowW(L"BUTTON",L"我已抄下并妥善保管",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, sp,y,w0,scale_px(28),h,(HMENU)(INT_PTR)ID_RK_ACK,hInst,NULL);
        if(fBody) SendMessageW(b,WM_SETFONT,(WPARAM)fBody,TRUE);
        y+=scale_px(40);
        b=CreateWindowW(L"BUTTON",v072_rk_force?L"我已抄好":L"完成",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW, scale_px(520)-sp-scale_px(120),y,scale_px(120),scale_px(32),h,(HMENU)IDOK,hInst,NULL);
        if(fBody) SendMessageW(b,WM_SETFONT,(WPARAM)fBody,TRUE);
        v072_rk_ok=b; v072_rk_enable();
        return 0;
    }
    if(m==WM_COMMAND){
        int id=LOWORD(w);
        if(id==ID_RK_COPY){ copy_text(W(v072_rk_text)); return 0; }
        if(id==ID_RK_EXPORT){ v072_rk_export(); return 0; }
        if(id==ID_RK_ACK){ v072_rk_acked=(SendDlgItemMessageW(h,ID_RK_ACK,BM_GETCHECK,0,0)==BST_CHECKED); v072_rk_enable(); return 0; }
        if(id==IDOK || id==IDCANCEL){ if(id==IDCANCEL && v072_rk_force && !v072_rk_acked && !getenv("LSM_UI_TEST")) return 0;
            if(id==IDOK && v072_rk_force && !v072_rk_acked && !getenv("LSM_UI_TEST")) return 0;
            v072_rk_done=1; DestroyWindow(h); return 0; }
    }
    if(m==WM_KEYDOWN && w==VK_ESCAPE){ if(v072_rk_force && !v072_rk_acked && !getenv("LSM_UI_TEST")) return 0; v072_rk_done=1; DestroyWindow(h); return 0; }
    if(m==WM_CLOSE){ if(v072_rk_force && !v072_rk_acked && !getenv("LSM_UI_TEST")) return 0; v072_rk_done=1; DestroyWindow(h); return 0; }
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
static void v072_show_recovery(int force, const char *preset){ /* LSMF_recovery_key */
    char err[256]=""; static int reg; RECT r; HWND h; MSG msg;
    if(preset && preset[0]) snprintf(v072_rk_text,sizeof v072_rk_text,"%s",preset);
    else if(v072_load_key(v072_rk_text,sizeof v072_rk_text)){ msgbox("还没有恢复密钥（请先开启账号模式）",MB_ICONINFORMATION); return; }
    v072_rk_force=force; v072_rk_acked=0; v072_rk_done=0;
    if(!reg){ WNDCLASSEXW wc; memset(&wc,0,sizeof wc); wc.cbSize=sizeof wc; wc.lpfnWndProc=v072_rk_proc; wc.hInstance=hInst; wc.lpszClassName=L"LsmRecovery"; wc.hCursor=LoadCursor(NULL,IDC_ARROW); wc.hbrBackground=brWin; wc.style=0x00020000; RegisterClassExW(&wc); reg=1; }
    GetWindowRect(hMain,&r);
    h=CreateWindowExW(WS_EX_TOOLWINDOW,L"LsmRecovery",L"恢复密钥",WS_POPUP,
        r.left+scale_px(60),r.top+scale_px(40),scale_px(540),scale_px(480),hMain,NULL,hInst,NULL);
    v072_rk_win=h; EnableWindow(hMain,FALSE); ShowWindow(h,SW_SHOW); SetFocus(h);
    while(IsWindow(h) && GetMessage(&msg,NULL,0,0)){
        if(msg.message==WM_QUIT){ PostQuitMessage((int)msg.wParam); break; }
        if(!IsDialogMessageW(h,&msg)){ TranslateMessage(&msg); DispatchMessage(&msg); }
    }
    v072_rk_win=NULL; EnableWindow(hMain,TRUE); SetForegroundWindow(hMain); v072_rk_force=0; (void)err;
}
static void v072_shot_recovery(void){ if(!getenv("LSM_UI_TEST")) return; v072_show_recovery(0,NULL); }
static HMENU v072_mpopup(HMENU parent, const wchar_t *label){
    HMENU s=CreatePopupMenu(); AM(parent,MF_POPUP,(UINT_PTR)s,label); return s;
}
