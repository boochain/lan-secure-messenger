/* v0.6.1 UX (XP Win32): my QR (link copied to the clipboard), add by link (also accepted by 按 IP 添加),
 * resend from the message menu + the reason under a failed message, file size / ETA confirm, Ctrl+K quick switch,
 * ↑ in an empty composer edits the last own message. Included by gui.c after xp_v062.inc. */
#define IDM_MYQR 263
#define IDM_ADDURI 264
#define IDM_QUICK 265
static uint8_t ux_qrm[177*177]; static int ux_qrn; static wchar_t ux_wuri[400];
static LRESULT CALLBACK ux_qr_proc(HWND h, UINT m, WPARAM w, LPARAM l){
    if(m==WM_PAINT){ PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps); RECT r; int s,x,y,q,ox,oy; HBRUSH wb=(HBRUSH)GetStockObject(WHITE_BRUSH), bb=(HBRUSH)GetStockObject(BLACK_BRUSH);
        GetClientRect(h,&r); FillRect(dc,&r,wb); s=scale_px(240); q=ux_qrn?s/(ux_qrn+8):1; ox=(r.right-q*(ux_qrn+8))/2+4*q; oy=scale_px(10)+4*q; /* dark on white in both themes: scanners need it */
        for(y=0;y<ux_qrn;y++) for(x=0;x<ux_qrn;x++) if(ux_qrm[y*ux_qrn+x]){ RECT c={ox+x*q,oy+y*q,ox+x*q+q,oy+y*q+q}; FillRect(dc,&c,bb); }
        { RECT t={scale_px(12),scale_px(10)+q*(ux_qrn+8)+scale_px(6),r.right-scale_px(12),r.bottom-scale_px(8)}; SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(0,0,0)); SelectObject(dc,fBody);
          wchar_t t2[700]; _snwprintf(t2,700,L"%s\n\n链接已复制到剪贴板。让对方用手机版扫描，或把链接发给对方在「通过链接添加」中粘贴；指纹一致会自动设为已信任。单击或按 Esc 关闭。",ux_wuri); t2[699]=0;
          DrawTextW(dc,t2,-1,&t,DT_WORDBREAK|DT_EDITCONTROL|DT_NOPREFIX); }
        EndPaint(h,&ps); return 0; }
    if(m==WM_LBUTTONUP || (m==WM_KEYDOWN && w==VK_ESCAPE)){ DestroyWindow(h); return 0; }
    return DefWindowProcW(h,m,w,l); }
static void ux_my_qr(void){ char uri[400]; static int reg; RECT r; HWND h;
    if(lsm_my_uri(C,uri,sizeof uri)){ msgbox("未找到局域网地址，无法生成二维码。",MB_ICONWARNING); return; }
    ux_qrn=lsm_qr(uri,ux_qrm); wcsncpy(ux_wuri,W(uri),399); ux_wuri[399]=0; copy_text(ux_wuri);
    if(!reg){ WNDCLASSW wc; memset(&wc,0,sizeof wc); wc.lpfnWndProc=ux_qr_proc; wc.hInstance=hInst; wc.lpszClassName=L"LsmQr"; wc.hCursor=LoadCursor(NULL,IDC_ARROW); RegisterClassW(&wc); reg=1; }
    GetWindowRect(hMain,&r);
    h=CreateWindowExW(WS_EX_TOOLWINDOW,L"LsmQr",L"我的二维码",WS_POPUP|WS_CAPTION|WS_SYSMENU,r.left+scale_px(120),r.top+scale_px(60),scale_px(300),scale_px(440),hMain,NULL,hInst,NULL);
    ShowWindow(h,SW_SHOW); SetFocus(h); }
static void ux_add_uri_text(const char *u){ char err[256]=""; if(lsm_add_uri(C,u,err)) msgbox(err,MB_ICONWARNING); else msgbox("正在连接对方……连接成功并核对指纹一致后会自动设为已信任。",MB_ICONINFORMATION); refresh_list(); }
static void ux_add_uri(void){ prompt_t p; char u8[1600]; memset(&p,0,sizeof p);
    if(prompt(L"通过链接添加",L"粘贴对方的添加链接（lsm://add?…）：",L"",&p,1) && p.text[0]) ux_add_uri_text(U(p.text,u8,sizeof u8)); }

/* file size + ETA before sending: 0 sent, 1 cancelled, -1 error (err filled) */
static int ux_send_file(const char *path, char *err){ char pv[700], q[800]; lsm_file_preview(C,cur_conv,path,pv,sizeof pv); snprintf(q,sizeof q,"发送文件？\n%s",pv);
    if(tmsg(L"发送文件",W(q),1)!=1) return 1; return lsm_send_file(C,cur_conv,path,err)?-1:0; }

/* message menu: 重新发送 (cmd 75) */
static void ux_menu(HMENU pm, const char *id){ lsm_msg *m; int n,i,can=0; lsm_lock(C); n=lsm_messages(C,&m); for(i=0;i<n;i++) if(!strcmp(m[i].id,id)){ can=lsm_can_resend(&m[i]); break; } lsm_unlock(C);
    if(can){ AM(pm,MF_STRING,75,L"重新发送(&R)"); AM(pm,MF_SEPARATOR,0,NULL); } }
static int ux_menu_cmd(int cmd, const char *id){ char err[256]="",nid[40]; if(cmd!=75) return 0; if(lsm_resend(C,id,nid,err)) msgbox(err,MB_ICONWARNING); refresh_chat(); refresh_list(); return 1; }
/* failed own text: reason appended under the text (lock held) */
static wchar_t *ux_fail_text(wchar_t *t, const lsm_msg *m){ size_t n; wchar_t *o; const wchar_t *r;
    if(!t || !m->out || (m->status!=ST_FAILED && m->status!=ST_REJECTED)) return t;
    r=W(lsm_status_text(C,m)); n=wcslen(t)+wcslen(r)+40; o=(wchar_t*)malloc(n*sizeof(wchar_t)); if(!o) return t;
    _snwprintf(o,n,L"%s\n⚠ %s%s",t,r,lsm_can_resend(m)?L"（右键 → 重新发送）":L""); o[n-1]=0; free(t); return o; }

/* Ctrl+K: type a few letters, Enter jumps to the best match */
static void ux_quick_switch(void){ prompt_t p; char q[400], nm[640]; int i,best=-1,bs=0; memset(&p,0,sizeof p);
    if(!prompt(L"切换会话 (Ctrl+K)",L"输入联系人或群组名称的任意几个字，回车跳到最匹配的会话：",L"",&p,1) || !p.text[0]) return;
    U(p.text,q,sizeof q); for(i=0;i<nlist;i++){ int s=lsm_fuzzy(U(convs[i].name,nm,sizeof nm),q); if(s>bs){ bs=s; best=i; } }
    if(best<0){ msgbox("没有匹配的会话。",MB_ICONINFORMATION); return; } select_conv(best); SetFocus(hInput); }
/* ↑ in an empty composer */
static int ux_up_edit(void){ char id[40]; int r; if(!cur_conv[0] || edit_id[0]) return 0; lsm_lock(C); r=lsm_last_editable(C,cur_conv,id); lsm_unlock(C); if(!r) return 0; p1_edit(id); return 1; }
