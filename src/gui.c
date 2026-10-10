/* LanMessenger XP — pure Win32 GUI (no .NET, no GDI+). Chinese UI, system colours only
 * (GetSysColor/COLOR_WINDOW etc., so text follows the user's theme and stays readable),
 * tray icon via Shell_NotifyIcon. Runs the engine in-process. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "core.h"

#define IDC_LIST 101
#define IDC_CHAT 102
#define IDC_INPUT 103
#define IDC_SEND 104
#define IDC_FILE 105
#define IDC_STATUS 106
#define IDC_TITLE 107
#define IDM_FIREWALL 201
#define IDM_EXIT 202
#define IDM_ADDIP 203
#define IDM_TRUST 204
#define IDM_UNTRUST 205
#define IDM_BLOCK 206
#define IDM_PINOFFER 207
#define IDM_PINPAIR 208
#define IDM_GCREATE 209
#define IDM_GINVITE 210
#define IDM_GREMOVE 211
#define IDM_GLEAVE 212
#define IDM_NAME 213
#define IDM_ABOUT 214
#define IDM_DLDIR 215
#define IDM_SHOW 216
#define WM_TRAY (WM_APP+1)
#define TIMER_POLL 1

static lsm_core *C;
static HWND hMain, hList, hChat, hInput, hSend, hFile, hStatus, hTitle;
static HFONT hFont, hFontBold;
static HINSTANCE hInst;
static NOTIFYICONDATAW nid;
static char cur_conv[64];
static char list_ids[512][64]; static int nlist;
static wchar_t exe_dir[MAX_PATH];

static wchar_t *W(const char *s){ static wchar_t buf[8][8192]; static int k; wchar_t *b=buf[k++&7]; MultiByteToWideChar(CP_UTF8,0,s?s:"",-1,b,8192); return b; }
static char *U(const wchar_t *s, char *out, int n){ WideCharToMultiByte(CP_UTF8,0,s,-1,out,n,NULL,NULL); return out; }
static void msgbox(const char *t, UINT f){ MessageBoxW(hMain,W(t),L"局域网安全通讯 XP",f); }

/* ---------------------------------------------------------------- prompt dialogs (no .rc templates) */
typedef struct { const wchar_t *label; wchar_t text[512]; int done, ok; HWND ed, lb; const char **items; int nitems; int sel[64]; int nsel; int multi; } prompt_t;
static LRESULT CALLBACK PromptProc(HWND h, UINT m, WPARAM w, LPARAM l){
    prompt_t *p=(prompt_t*)GetWindowLongPtrW(h,GWLP_USERDATA);
    switch(m){
    case WM_COMMAND:
        if(LOWORD(w)==IDOK){ if(p->ed) GetWindowTextW(p->ed,p->text,512);
            if(p->lb){ int i,n=(int)SendMessageW(p->lb,LB_GETCOUNT,0,0); p->nsel=0; for(i=0;i<n && p->nsel<64;i++) if(SendMessageW(p->lb,LB_GETSEL,i,0)>0) p->sel[p->nsel++]=i; }
            p->ok=1; p->done=1; DestroyWindow(h); }
        else if(LOWORD(w)==IDCANCEL){ p->done=1; DestroyWindow(h); }
        return 0;
    case WM_CLOSE: p->done=1; DestroyWindow(h); return 0;
    case WM_CTLCOLORSTATIC: SetTextColor((HDC)w,GetSysColor(COLOR_BTNTEXT)); SetBkColor((HDC)w,GetSysColor(COLOR_BTNFACE)); return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }
    return DefWindowProcW(h,m,w,l); }
static int prompt(const wchar_t *title, const wchar_t *label, const wchar_t *init, prompt_t *p, int with_edit){
    HWND h, st, ok, ca; MSG msg; RECT r; int H=with_edit?150:120, i, y=12;
    if(p->items) H+=180;
    p->done=p->ok=0; p->label=label; p->ed=p->lb=NULL;
    GetWindowRect(hMain,&r);
    h=CreateWindowExW(WS_EX_DLGMODALFRAME,L"LsmPrompt",title,WS_POPUP|WS_CAPTION|WS_SYSMENU,r.left+80,r.top+80,420,H,hMain,NULL,hInst,NULL);
    SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)p);
    st=CreateWindowW(L"STATIC",label,WS_CHILD|WS_VISIBLE,12,y,390,36,h,NULL,hInst,NULL); SendMessageW(st,WM_SETFONT,(WPARAM)hFont,1); y+=40;
    if(p->items){ p->lb=CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",NULL,WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY|(p->multi?LBS_EXTENDEDSEL|LBS_MULTIPLESEL:0),12,y,390,170,h,NULL,hInst,NULL);
        SendMessageW(p->lb,WM_SETFONT,(WPARAM)hFont,1); for(i=0;i<p->nitems;i++) SendMessageW(p->lb,LB_ADDSTRING,0,(LPARAM)W(p->items[i])); if(!p->multi && p->nitems) SendMessageW(p->lb,LB_SETCURSEL,0,0); y+=178; }
    if(with_edit){ p->ed=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",init?init:L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,12,y,390,24,h,NULL,hInst,NULL); SendMessageW(p->ed,WM_SETFONT,(WPARAM)hFont,1); y+=32; }
    ok=CreateWindowW(L"BUTTON",L"确定",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,220,y,86,26,h,(HMENU)IDOK,hInst,NULL);
    ca=CreateWindowW(L"BUTTON",L"取消",WS_CHILD|WS_VISIBLE|WS_TABSTOP,316,y,86,26,h,(HMENU)IDCANCEL,hInst,NULL);
    SendMessageW(ok,WM_SETFONT,(WPARAM)hFont,1); SendMessageW(ca,WM_SETFONT,(WPARAM)hFont,1);
    SetWindowPos(h,NULL,0,0,420,y+70,SWP_NOMOVE|SWP_NOZORDER);
    EnableWindow(hMain,FALSE); ShowWindow(h,SW_SHOW); if(p->ed) SetFocus(p->ed); else if(p->lb) SetFocus(p->lb);
    while(!p->done && GetMessageW(&msg,NULL,0,0)){ if(!IsDialogMessageW(h,&msg)){ TranslateMessage(&msg); DispatchMessageW(&msg);} }
    EnableWindow(hMain,TRUE); SetForegroundWindow(hMain);
    return p->ok; }

/* ---------------------------------------------------------------- views */
static void refresh_list(void){
    lsm_contact *k; lsm_group *g; int n,i, sel=-1; wchar_t line[600];
    char keep[64]; strcpy(keep,cur_conv);
    SendMessageW(hList,WM_SETREDRAW,FALSE,0); SendMessageW(hList,LB_RESETCONTENT,0,0); nlist=0;
    lsm_lock(C);
    n=lsm_contacts(C,&k);
    for(i=0;i<n && nlist<512;i++){ lsm_msg *m; int nm,j,unread=0; char conv[64]; snprintf(conv,64,"dm:%s",k[i].fp);
        nm=lsm_messages(C,&m); for(j=0;j<nm;j++) if(m[j].unread && !strcmp(m[j].conv,conv)) unread++;
        _snwprintf(line,600,L"%s %s%s%s%s", k[i].online?L"●":L"○", W(k[i].name), k[i].trust==1?L"  [已信任]":k[i].trust==2?L"  [已屏蔽]":L"  [未信任]", unread?L"  (新消息)":L"", L"");
        line[599]=0; SendMessageW(hList,LB_ADDSTRING,0,(LPARAM)line); strcpy(list_ids[nlist],conv); if(!strcmp(conv,keep)) sel=nlist; nlist++; }
    n=lsm_groups(C,&g);
    for(i=0;i<n && nlist<512;i++){ char u[37], conv[64]; lsm_uuid_str(g[i].id,u); snprintf(conv,64,"group:%s",u);
        _snwprintf(line,600,L"# %s  (%d 人)",W(g[i].name),g[i].nmembers); line[599]=0; SendMessageW(hList,LB_ADDSTRING,0,(LPARAM)line); strcpy(list_ids[nlist],conv); if(!strcmp(conv,keep)) sel=nlist; nlist++; }
    { wchar_t st[400]; _snwprintf(st,400,L"我: %s    指纹 %s    TCP %d    版本 %s",W(lsm_my_name(C)),W(lsm_my_fp(C)),lsm_listen_port(C),W(LSM_APP_VERSION)); st[399]=0; SetWindowTextW(hStatus,st); }
    lsm_unlock(C);
    if(sel>=0) SendMessageW(hList,LB_SETCURSEL,sel,0);
    SendMessageW(hList,WM_SETREDRAW,TRUE,0); InvalidateRect(hList,NULL,TRUE); }

static const wchar_t *status_txt(int s){ switch(s){ case 0: return L"排队中"; case 1: return L"已发送"; case 2: return L"已送达"; case 3: return L"已读"; case 5: return L"失败"; default: return L""; } }
static void refresh_chat(void){
    lsm_msg *m; int n,i; size_t cap=65536, len=0; wchar_t *buf=(wchar_t*)malloc(cap*sizeof(wchar_t)); wchar_t title[400]=L"";
    buf[0]=0;
    lsm_lock(C);
    if(cur_conv[0]){ lsm_contact *k; lsm_group *g; int nk, j;
        if(!strncmp(cur_conv,"dm:",3)){ nk=lsm_contacts(C,&k); for(j=0;j<nk;j++) if(!strcmp(k[j].fp,cur_conv+3)) _snwprintf(title,400,L"%s    %s    %s%s%s",W(k[j].name),W(k[j].fp),k[j].online?L"在线":L"离线",k[j].app_version[0]?L"  ":L"",W(k[j].app_version)); }
        else { char u[37]; nk=lsm_groups(C,&g); for(j=0;j<nk;j++){ lsm_uuid_str(g[j].id,u); if(!strcmp(u,cur_conv+6)) _snwprintf(title,400,L"群组「%s」  %d 人  密钥版本 %llu",W(g[j].name),g[j].nmembers,(unsigned long long)g[j].key_epoch); } }
        title[399]=0;
        n=lsm_messages(C,&m);
        for(i=0;i<n;i++){ wchar_t line[9000]; time_t t; struct tm *tm; wchar_t ts[32]=L""; size_t ll; if(strcmp(m[i].conv,cur_conv)) continue;
            t=(time_t)(m[i].ts_ms/1000); tm=localtime(&t); if(tm) wcsftime(ts,32,L"%m-%d %H:%M",tm);
            if(m[i].kind==K_SYSTEM) _snwprintf(line,9000,L"[%s] —— %s ——\r\n",ts,W(m[i].body));
            else if(m[i].kind==K_FILE){ lsm_transfer *t2; int nt=lsm_transfers(C,&t2), j2; const wchar_t *fs=L""; wchar_t prog[64]=L"";
                for(j2=0;j2<nt;j2++){ char u[37]; lsm_uuid_str(t2[j2].tid,u); if(!strcmp(t2[j2].msg_id,m[i].id)){ static const wchar_t *TS[]={L"等待接收",L"传输中",L"校验中",L"完成",L"失败",L"被拒收",L"已取消"}; fs=TS[t2[j2].state]; if(t2[j2].size) _snwprintf(prog,64,L" %d%%",(int)(t2[j2].transferred*100/t2[j2].size)); } }
                _snwprintf(line,9000,L"[%s] %s: [文件] %s (%llu 字节) %s%s %s\r\n",ts,m[i].out?L"我":W(m[i].sender_name),W(m[i].file_name),(unsigned long long)m[i].file_size,fs,prog,m[i].out?status_txt(m[i].status):L""); }
            else _snwprintf(line,9000,L"[%s] %s: %s   %s\r\n",ts,m[i].out?L"我":W(m[i].sender_name),W(m[i].body),m[i].out?status_txt(m[i].status):L"");
            line[8999]=0; ll=wcslen(line); if(len+ll+1>cap){ cap=(cap+ll)*2; buf=(wchar_t*)realloc(buf,cap*sizeof(wchar_t)); } wcscpy(buf+len,line); len+=ll; } }
    lsm_unlock(C);
    SetWindowTextW(hTitle,cur_conv[0]?title:L"← 从左侧选择联系人或群组");
    SetWindowTextW(hChat,buf); SendMessageW(hChat,EM_SETSEL,(WPARAM)len,(LPARAM)len); SendMessageW(hChat,EM_SCROLLCARET,0,0);
    free(buf); }

static void tray_balloon(const char *title, const char *body){
    nid.uFlags=NIF_INFO; wcsncpy(nid.szInfoTitle,W(title),63); nid.szInfoTitle[63]=0; wcsncpy(nid.szInfo,W(body),255); nid.szInfo[255]=0; nid.dwInfoFlags=NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY,&nid); nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP; nid.szInfo[0]=0; }
/* XP: the shell may not be up yet at autostart, and explorer.exe restarts drop all icons
 * (it then broadcasts "TaskbarCreated"). Keep retrying NIM_ADD until it succeeds. */
static UINT WM_TASKBARCREATED; static int tray_added;
static void tray_add(void){ nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP; tray_added=Shell_NotifyIconW(NIM_ADD,&nid)?1:0; }

static void poll_events(void){
    char e[4096]; int list_dirty=0, chat_dirty=0;
    while(lsm_poll_event(C,e,sizeof e)){
        if(!strncmp(e,"msg\t",4)||!strncmp(e,"msg_update\t",11)){ char *f[8]; int n=0; char *p=e; f[n++]=p; while(*p && n<8){ if(*p=='\t'){*p=0; f[n++]=p+1;} p++; }
            if(n>=7){ if(!strcmp(f[2],cur_conv)) chat_dirty=1; list_dirty=1;
                if(!strcmp(f[0],"msg") && !strcmp(f[3],"in") && strcmp(f[5],"system") && (strcmp(f[2],cur_conv) || GetForegroundWindow()!=hMain)){ tray_balloon("新消息",f[6]); FlashWindow(hMain,TRUE); } } }
        else if(!strncmp(e,"transfer\t",9)) chat_dirty=1;
        else if(!strncmp(e,"notice\t",7)){ tray_balloon("提示",e+7); }
        else if(!strncmp(e,"pin_result\t",11)){ msgbox(strstr(e,"\tok")?"PIN 配对成功，已互相信任。":"PIN 配对失败（PIN 错误或已过期）。",MB_ICONINFORMATION); list_dirty=1; }
        else if(!strncmp(e,"log\t",4)) ;
        else list_dirty=1; }
    if(list_dirty) refresh_list();
    if(chat_dirty||list_dirty) refresh_chat();
    if(cur_conv[0] && chat_dirty && GetForegroundWindow()==hMain){ if(lsm_mark_read(C,cur_conv)) refresh_list(); } }

static const char *selected_peer(void){ static char fp[FP_LEN]; if(strncmp(cur_conv,"dm:",3)){ msgbox("请先在左侧选择一个联系人。",MB_ICONINFORMATION); return NULL; } strcpy(fp,cur_conv+3); return fp; }
static const char *selected_group(void){ static char g[40]; if(strncmp(cur_conv,"group:",6)){ msgbox("请先在左侧选择一个群组。",MB_ICONINFORMATION); return NULL; } strcpy(g,cur_conv+6); return g; }

static int pick_peers(const wchar_t *title, const wchar_t *label, int trusted_only, int multi, char out[64][FP_LEN], const wchar_t *init_edit, wchar_t *edit_out, int with_edit){
    prompt_t p; lsm_contact *k; int n,i, cnt=0; static char names[64][400]; const char *items[64]; char fps[64][FP_LEN];
    memset(&p,0,sizeof p);
    lsm_lock(C); n=lsm_contacts(C,&k); for(i=0;i<n && cnt<64;i++){ if(trusted_only && k[i].trust!=TRUST_TRUSTED) continue; snprintf(names[cnt],400,"%s  (%s)",k[i].name,k[i].fp); items[cnt]=names[cnt]; strcpy(fps[cnt],k[i].fp); cnt++; } lsm_unlock(C);
    if(!cnt){ msgbox(trusted_only?"没有已信任的联系人。请先信任对方（或使用 PIN 配对）。":"没有联系人。",MB_ICONINFORMATION); return -1; }
    p.items=items; p.nitems=cnt; p.multi=multi;
    if(!prompt(title,label,init_edit,&p,with_edit)) return -1;
    if(edit_out) wcscpy(edit_out,p.text);
    if(!multi){ int s=(int)SendMessageW(p.lb,LB_GETCURSEL,0,0); (void)s; }
    for(i=0;i<p.nsel;i++) strcpy(out[i],fps[p.sel[i]]);
    return p.nsel; }

static void do_command(int id){
    char err[256]="", u8[2048]; prompt_t p; memset(&p,0,sizeof p);
    switch(id){
    case IDM_EXIT: DestroyWindow(hMain); break;
    case IDM_SHOW: ShowWindow(hMain,SW_SHOW); ShowWindow(hMain,SW_RESTORE); SetForegroundWindow(hMain); break;
    case IDM_FIREWALL: { wchar_t exe[MAX_PATH]; char e8[1024], rep[2048]; GetModuleFileNameW(NULL,exe,MAX_PATH); U(exe,e8,sizeof e8);
        if(MessageBoxW(hMain,L"将为本程序添加 Windows 防火墙例外（XP 使用 netsh firewall，Vista 及以上使用 netsh advfirewall）。需要管理员权限。继续？",L"防火墙设置",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK) break;
        { int rc=lsm_firewall_setup(e8,rep,sizeof rep); char m[2300]; snprintf(m,sizeof m,"%s\r\n\r\n%s",rc==0?"已添加防火墙例外。":"添加失败（可能需要以管理员身份运行）。",rep); msgbox(m,rc==0?MB_ICONINFORMATION:MB_ICONWARNING); } break; }
    case IDM_ADDIP: if(prompt(L"按 IP 添加",L"输入对方 IP 地址（可带端口，如 192.168.1.20 或 192.168.1.20:41235）：",L"",&p,1)){ if(lsm_add_addr(C,U(p.text,u8,sizeof u8),err)) msgbox(err,MB_ICONWARNING); } break;
    case IDM_TRUST: case IDM_UNTRUST: case IDM_BLOCK: { const char *fp=selected_peer(); if(fp){ lsm_set_trust(C,fp,id==IDM_TRUST?1:id==IDM_BLOCK?2:0,err); refresh_list(); } break; }
    case IDM_PINOFFER: { char pin[8], m[256]; lsm_pin_offer(C,pin); snprintf(m,sizeof m,"请把下面的 PIN 告诉对方，并让对方在“输入 PIN 配对”中输入（5 分钟内有效）：\r\n\r\n        %s",pin); msgbox(m,MB_ICONINFORMATION); break; }
    case IDM_PINPAIR: { const char *fp=selected_peer(); if(fp && prompt(L"输入 PIN 配对",L"输入对方显示的 6 位 PIN：",L"",&p,1)){ if(lsm_pin_pair(C,fp,U(p.text,u8,sizeof u8),err)) msgbox(err,MB_ICONWARNING); } break; }
    case IDM_GCREATE: { char sel[64][FP_LEN]; const char *fps[64]; wchar_t nm[512]; int n=pick_peers(L"创建群组",L"选择成员（可多选，仅显示已信任联系人），并在下方输入群名：",1,1,sel,L"新群组",nm,1), i; char gid[40];
        if(n<0) break; for(i=0;i<n;i++) fps[i]=sel[i]; if(lsm_group_create(C,U(nm,u8,sizeof u8),fps,n,gid,err)) msgbox(err,MB_ICONWARNING); else { snprintf(cur_conv,64,"group:%s",gid); refresh_list(); refresh_chat(); } break; }
    case IDM_GINVITE: { const char *g=selected_group(); char sel[64][FP_LEN]; const char *fps[64]; int n,i; if(!g) break; n=pick_peers(L"邀请成员",L"选择要邀请的成员（可多选）：",1,1,sel,NULL,NULL,0); if(n<=0) break; for(i=0;i<n;i++) fps[i]=sel[i]; if(lsm_group_invite(C,g,fps,n,err)) msgbox(err,MB_ICONWARNING); break; }
    case IDM_GREMOVE: { const char *g=selected_group(); char sel[64][FP_LEN]; int n; if(!g) break; n=pick_peers(L"移除成员",L"选择要移出群组的成员（将自动更换群密钥）：",0,0,sel,NULL,NULL,0); if(n<=0) break; if(lsm_group_remove(C,g,sel[0],err)) msgbox(err,MB_ICONWARNING); break; }
    case IDM_GLEAVE: { const char *g=selected_group(); if(g && MessageBoxW(hMain,L"确定退出该群组？",L"退出群组",MB_OKCANCEL|MB_ICONQUESTION)==IDOK){ if(lsm_group_leave(C,g,err)) msgbox(err,MB_ICONWARNING); cur_conv[0]=0; refresh_list(); refresh_chat(); } break; }
    case IDM_NAME: { wchar_t init[300]; lsm_lock(C); wcsncpy(init,W(lsm_my_name(C)),299); init[299]=0; lsm_unlock(C); if(prompt(L"修改昵称",L"新的显示名称：",init,&p,1)){ lsm_set_name(C,U(p.text,u8,sizeof u8)); refresh_list(); } break; }
    case IDM_DLDIR: { lsm_lock(C); strcpy(u8,lsm_download_dir(C)); lsm_unlock(C); ShellExecuteW(hMain,L"open",W(u8),NULL,NULL,SW_SHOWNORMAL); break; }
    case IDM_ABOUT: msgbox("局域网安全通讯 XP 版 " LSM_APP_VERSION "\r\n\r\n与 v0.5 完全兼容的旧系统版本（协议 v5）：Noise_XX_25519_ChaChaPoly_BLAKE2s 加密、自动发现、按 IP 添加、PIN 配对、群组、文件传输、送达/已读回执。\r\n\r\n支持 Windows XP SP3 及以上。",MB_ICONINFORMATION); break;
    } }

static void send_current(void){
    wchar_t *w; int n; char *u, err[256], id[40];
    if(!cur_conv[0]){ msgbox("请先在左侧选择联系人或群组。",MB_ICONINFORMATION); return; }
    n=GetWindowTextLengthW(hInput); if(n<=0) return; w=(wchar_t*)malloc((n+1)*sizeof(wchar_t)); GetWindowTextW(hInput,w,n+1);
    u=(char*)malloc(n*4+4); U(w,u,n*4+4); free(w);
    if(lsm_send_text(C,cur_conv,u,id,err)) msgbox(err,MB_ICONWARNING); else SetWindowTextW(hInput,L"");
    free(u); refresh_chat(); }
static void send_file_dlg(void){
    OPENFILENAMEW of; wchar_t path[MAX_PATH]=L""; char u8[MAX_PATH*4], err[256];
    if(!cur_conv[0]){ msgbox("请先在左侧选择联系人或群组。",MB_ICONINFORMATION); return; }
    memset(&of,0,sizeof of); of.lStructSize=sizeof of; of.hwndOwner=hMain; of.lpstrFile=path; of.nMaxFile=MAX_PATH; of.lpstrFilter=L"所有文件 (*.*)\0*.*\0"; of.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_EXPLORER;
    if(!GetOpenFileNameW(&of)) return;
    if(lsm_send_file(C,cur_conv,U(path,u8,sizeof u8),err)) msgbox(err,MB_ICONWARNING); refresh_chat(); }

static WNDPROC oldInputProc;
static LRESULT CALLBACK InputProc(HWND h, UINT m, WPARAM w, LPARAM l){ if(m==WM_KEYDOWN && w==VK_TAB){ SetFocus(GetNextDlgTabItem(hMain,h,(GetKeyState(VK_SHIFT)&0x8000)?TRUE:FALSE)); return 0; } if(m==WM_CHAR && w=='\t') return 0; if(m==WM_KEYDOWN && w==VK_RETURN && !(GetKeyState(VK_SHIFT)&0x8000)){ send_current(); return 0; } if(m==WM_CHAR && w=='\r' && !(GetKeyState(VK_SHIFT)&0x8000)) return 0; return CallWindowProcW(oldInputProc,h,m,w,l); }

static void layout(int W_, int H_){
    int sb=22, lw=W_/3<260?W_/3:260, ih=64, bw=96;
    MoveWindow(hList,6,6,lw,H_-sb-12,TRUE);
    MoveWindow(hTitle,lw+12,6,W_-lw-18,22,TRUE);
    MoveWindow(hChat,lw+12,30,W_-lw-18,H_-sb-30-ih-14,TRUE);
    MoveWindow(hInput,lw+12,H_-sb-ih-6,W_-lw-18-bw-6,ih,TRUE);
    MoveWindow(hSend,W_-bw-6,H_-sb-ih-6,bw,30,TRUE);
    MoveWindow(hFile,W_-bw-6,H_-sb-ih+28,bw,30,TRUE);
    SendMessageW(hStatus,WM_SIZE,0,0); }

static HMENU build_menu(void){
    HMENU m=CreateMenu(), f=CreatePopupMenu(), c=CreatePopupMenu(), g=CreatePopupMenu(), s=CreatePopupMenu(), h=CreatePopupMenu();
    AppendMenuW(f,MF_STRING,IDM_DLDIR,L"打开接收文件夹(&D)"); AppendMenuW(f,MF_STRING,IDM_FIREWALL,L"设置防火墙例外(&F)..."); AppendMenuW(f,MF_SEPARATOR,0,NULL); AppendMenuW(f,MF_STRING,IDM_EXIT,L"退出(&X)");
    AppendMenuW(c,MF_STRING,IDM_ADDIP,L"按 IP 添加(&A)..."); AppendMenuW(c,MF_SEPARATOR,0,NULL); AppendMenuW(c,MF_STRING,IDM_TRUST,L"信任(&T)"); AppendMenuW(c,MF_STRING,IDM_UNTRUST,L"取消信任(&U)"); AppendMenuW(c,MF_STRING,IDM_BLOCK,L"屏蔽(&B)");
    AppendMenuW(c,MF_SEPARATOR,0,NULL); AppendMenuW(c,MF_STRING,IDM_PINOFFER,L"生成 PIN（让对方输入）(&P)"); AppendMenuW(c,MF_STRING,IDM_PINPAIR,L"输入 PIN 配对(&I)...");
    AppendMenuW(g,MF_STRING,IDM_GCREATE,L"创建群组(&C)..."); AppendMenuW(g,MF_STRING,IDM_GINVITE,L"邀请成员(&I)..."); AppendMenuW(g,MF_STRING,IDM_GREMOVE,L"移除成员(&R)..."); AppendMenuW(g,MF_STRING,IDM_GLEAVE,L"退出群组(&L)");
    AppendMenuW(s,MF_STRING,IDM_NAME,L"修改昵称(&N)...");
    AppendMenuW(h,MF_STRING,IDM_ABOUT,L"关于(&A)");
    AppendMenuW(m,MF_POPUP,(UINT_PTR)f,L"文件(&F)"); AppendMenuW(m,MF_POPUP,(UINT_PTR)c,L"联系人(&C)"); AppendMenuW(m,MF_POPUP,(UINT_PTR)g,L"群组(&G)"); AppendMenuW(m,MF_POPUP,(UINT_PTR)s,L"设置(&S)"); AppendMenuW(m,MF_POPUP,(UINT_PTR)h,L"帮助(&H)");
    return m; }

static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l){
    switch(m){
    case WM_CREATE: {
        DWORD es=WS_EX_CLIENTEDGE;
        hList=CreateWindowExW(es,L"LISTBOX",NULL,WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_TABSTOP|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,0,0,0,0,h,(HMENU)IDC_LIST,hInst,NULL);
        hTitle=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_LEFTNOWORDWRAP,0,0,0,0,h,(HMENU)IDC_TITLE,hInst,NULL);
        hChat=CreateWindowExW(es,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_TABSTOP|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,0,0,0,0,h,(HMENU)IDC_CHAT,hInst,NULL);
        hInput=CreateWindowExW(es,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,0,0,0,0,h,(HMENU)IDC_INPUT,hInst,NULL);
        hSend=CreateWindowW(L"BUTTON",L"发送 (Enter)",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,0,0,0,0,h,(HMENU)IDC_SEND,hInst,NULL);
        hFile=CreateWindowW(L"BUTTON",L"发送文件...",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,0,0,0,0,h,(HMENU)IDC_FILE,hInst,NULL);
        hStatus=CreateWindowExW(0,STATUSCLASSNAMEW,L"",WS_CHILD|WS_VISIBLE|SBARS_SIZEGRIP,0,0,0,0,h,(HMENU)IDC_STATUS,hInst,NULL);
        SendMessageW(hList,WM_SETFONT,(WPARAM)hFont,1); SendMessageW(hChat,WM_SETFONT,(WPARAM)hFont,1); SendMessageW(hInput,WM_SETFONT,(WPARAM)hFont,1);
        SendMessageW(hSend,WM_SETFONT,(WPARAM)hFont,1); SendMessageW(hFile,WM_SETFONT,(WPARAM)hFont,1); SendMessageW(hStatus,WM_SETFONT,(WPARAM)hFont,1); SendMessageW(hTitle,WM_SETFONT,(WPARAM)hFontBold,1);
        SendMessageW(hInput,EM_LIMITTEXT,60000,0);
        oldInputProc=(WNDPROC)SetWindowLongPtrW(hInput,GWLP_WNDPROC,(LONG_PTR)InputProc);
        SetTimer(h,TIMER_POLL,300,NULL);
        return 0; }
    case WM_SIZE: if(w!=SIZE_MINIMIZED){ layout(LOWORD(l),HIWORD(l)); InvalidateRect(h,NULL,TRUE); } else ShowWindow(h,SW_HIDE); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO*)l)->ptMinTrackSize.x=640; ((MINMAXINFO*)l)->ptMinTrackSize.y=420; return 0;
    case WM_SETFOCUS: SetFocus(hList); return 0; /* keyboard users land in the contact list */
    case WM_TIMER: if(!tray_added) tray_add(); poll_events(); return 0;
    /* system colours only: readable in any theme incl. high-contrast */
    case WM_CTLCOLORSTATIC: if((HWND)l==hChat){ SetTextColor((HDC)w,GetSysColor(COLOR_WINDOWTEXT)); SetBkColor((HDC)w,GetSysColor(COLOR_WINDOW)); return (LRESULT)GetSysColorBrush(COLOR_WINDOW); }
        SetTextColor((HDC)w,GetSysColor(COLOR_BTNTEXT)); SetBkColor((HDC)w,GetSysColor(COLOR_BTNFACE)); return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    case WM_COMMAND:
        if(LOWORD(w)==IDC_LIST && HIWORD(w)==LBN_SELCHANGE){ int s=(int)SendMessageW(hList,LB_GETCURSEL,0,0); if(s>=0 && s<nlist){ strcpy(cur_conv,list_ids[s]); refresh_chat(); if(lsm_mark_read(C,cur_conv)) refresh_list(); SetFocus(hInput); } return 0; }
        if(LOWORD(w)==IDC_SEND){ send_current(); return 0; }
        if(LOWORD(w)==IDC_FILE){ send_file_dlg(); return 0; }
        do_command(LOWORD(w)); return 0;
    case WM_TRAY:
        if(l==WM_LBUTTONDBLCLK||l==NIN_BALLOONUSERCLICK) do_command(IDM_SHOW);
        else if(l==WM_RBUTTONUP){ POINT pt; HMENU pm=CreatePopupMenu(); AppendMenuW(pm,MF_STRING,IDM_SHOW,L"显示主窗口"); AppendMenuW(pm,MF_SEPARATOR,0,NULL); AppendMenuW(pm,MF_STRING,IDM_EXIT,L"退出"); GetCursorPos(&pt); SetForegroundWindow(h); TrackPopupMenu(pm,TPM_RIGHTBUTTON,pt.x,pt.y,0,h,NULL); DestroyMenu(pm); }
        return 0;
    case WM_CLOSE: ShowWindow(h,SW_HIDE); tray_balloon("局域网安全通讯","程序仍在后台运行，双击托盘图标可打开。"); return 0;
    case WM_DESTROY: KillTimer(h,TIMER_POLL); Shell_NotifyIconW(NIM_DELETE,&nid); PostQuitMessage(0); return 0;
    case WM_SYSCOLORCHANGE: InvalidateRect(h,NULL,TRUE); return 0;
    }
    if(WM_TASKBARCREATED && m==WM_TASKBARCREATED){ tray_add(); return 0; }
    return DefWindowProcW(h,m,w,l); }

static int CALLBACK font_found(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD t, LPARAM l){ (void)lf; (void)tm; (void)t; (void)l; return 0; /* stop: exists */ }

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE prev, LPWSTR cmd, int show){
    int win_w=900, win_h=600; WNDCLASSEXW wc; MSG msg; lsm_opts o; char data[MAX_PATH*4], err[256]; INITCOMMONCONTROLSEX ic; wchar_t *sl, dw[MAX_PATH]; LOGFONTW lf; NONCLIENTMETRICSW ncm; HANDLE mtx;
    (void)prev; hInst=hi;
    mtx=CreateMutexW(NULL,TRUE,L"LanSecureMessengerXP_Single"); if(mtx && GetLastError()==ERROR_ALREADY_EXISTS && !wcsstr(cmd,L"--data")){ HWND o2=FindWindowW(L"LsmMain",NULL); if(o2){ ShowWindow(o2,SW_SHOW); ShowWindow(o2,SW_RESTORE); SetForegroundWindow(o2);} return 0; }
    ic.dwSize=sizeof ic; ic.dwICC=ICC_BAR_CLASSES|ICC_STANDARD_CLASSES; InitCommonControlsEx(&ic);
    GetModuleFileNameW(NULL,exe_dir,MAX_PATH); sl=wcsrchr(exe_dir,L'\\'); if(sl) *sl=0;
    _snwprintf(dw,MAX_PATH,L"%s\\data",exe_dir); dw[MAX_PATH-1]=0;
    { int argc; wchar_t **av=CommandLineToArgvW(GetCommandLineW(),&argc); int i; memset(&o,0,sizeof o);
      static char nm[300], tg[1024];
      for(i=1;i<argc;i++){ if(!wcscmp(av[i],L"--data")&&i+1<argc) wcsncpy(dw,av[++i],MAX_PATH-1); else if(!wcscmp(av[i],L"--name")&&i+1<argc){ U(av[++i],nm,sizeof nm); o.name=nm; } else if(!wcscmp(av[i],L"--port")&&i+1<argc) o.listen_port=_wtoi(av[++i]); else if(!wcscmp(av[i],L"--disco-port")&&i+1<argc) o.discovery_port=_wtoi(av[++i]); else if(!wcscmp(av[i],L"--targets")&&i+1<argc){ U(av[++i],tg,sizeof tg); o.announce_targets=tg; } } }
    U(dw,data,sizeof data); o.data_dir=data;
    if(!o.name){ static char cn[64]; wchar_t wn[64]; DWORD n=64; if(GetComputerNameW(wn,&n)){ U(wn,cn,sizeof cn); o.name=cn; } }
    /* UI font: system message font (follows the theme / DPI); falls back to DEFAULT_GUI_FONT */
    memset(&ncm,0,sizeof ncm); ncm.cbSize=sizeof(ncm)-sizeof(int); /* XP-sized struct (no iPaddedBorderWidth) */
    if(SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,ncm.cbSize,&ncm,0)){ lf=ncm.lfMessageFont; hFont=CreateFontIndirectW(&lf); lf.lfWeight=FW_BOLD; hFontBold=CreateFontIndirectW(&lf); }
    else { hFont=(HFONT)GetStockObject(DEFAULT_GUI_FONT); hFontBold=hFont; }
    /* Chinese-capable face: on Chinese XP the message font already is 宋体; on other XP installs
     * (East Asian language support) prefer SimSun explicitly; wine/ReactOS test boxes: WenQuanYi. */
    { static const wchar_t *faces[]={L"Microsoft YaHei",L"SimSun",L"\x5b8b\x4f53",L"NSimSun",L"WenQuanYi Zen Hei",L"Droid Sans Fallback",NULL}; int i; HDC dc=GetDC(NULL);
      for(i=0;faces[i];i++){ LOGFONTW q; memset(&q,0,sizeof q); q.lfCharSet=DEFAULT_CHARSET; wcsncpy(q.lfFaceName,faces[i],31);
        if(!EnumFontFamiliesExW(dc,&q,(FONTENUMPROCW)font_found,0,0)){ LOGFONTW lf2; GetObjectW(hFont,sizeof lf2,&lf2); if(hFontBold && hFontBold!=hFont) DeleteObject(hFontBold); DeleteObject(hFont); /* no GDI leak */ wcsncpy(lf2.lfFaceName,faces[i],31); lf2.lfCharSet=DEFAULT_CHARSET; if(lf2.lfHeight>-14 && lf2.lfHeight<0) lf2.lfHeight=-14; hFont=CreateFontIndirectW(&lf2); lf2.lfWeight=FW_BOLD; hFontBold=CreateFontIndirectW(&lf2); break; } }
      ReleaseDC(NULL,dc); }
    C=lsm_start(&o,err,sizeof err);
    if(!C){ MessageBoxW(NULL,W(err),L"启动失败",MB_ICONSTOP); return 1; }
    memset(&wc,0,sizeof wc); wc.cbSize=sizeof wc; wc.lpfnWndProc=MainProc; wc.hInstance=hi; wc.hIcon=LoadIconW(hi,MAKEINTRESOURCEW(1)); wc.hIconSm=wc.hIcon; wc.hCursor=LoadCursor(NULL,IDC_ARROW); wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1); wc.lpszClassName=L"LsmMain";
    RegisterClassExW(&wc);
    wc.lpfnWndProc=PromptProc; wc.lpszClassName=L"LsmPrompt"; wc.hIcon=NULL; wc.hIconSm=NULL; RegisterClassExW(&wc);
    { RECT wa; if(SystemParametersInfoW(SPI_GETWORKAREA,0,&wa,0)){ if(win_w>wa.right-wa.left) win_w=wa.right-wa.left; if(win_h>wa.bottom-wa.top) win_h=wa.bottom-wa.top; } } /* 800x600 XP screens */
    hMain=CreateWindowExW(0,L"LsmMain",L"局域网安全通讯 XP 版",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,win_w,win_h,NULL,build_menu(),hi,NULL);
    memset(&nid,0,sizeof nid); nid.cbSize=NOTIFYICONDATAW_V2_SIZE; nid.hWnd=hMain; nid.uID=1; nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP; nid.uCallbackMessage=WM_TRAY; nid.hIcon=wc.hIcon?wc.hIcon:LoadIcon(NULL,IDI_APPLICATION);
    { HICON ic2=LoadIconW(hi,MAKEINTRESOURCEW(1)); if(ic2) nid.hIcon=ic2; }
    wcscpy(nid.szTip,L"局域网安全通讯 XP 版"); WM_TASKBARCREATED=RegisterWindowMessageW(L"TaskbarCreated"); tray_add();
    ShowWindow(hMain,show); UpdateWindow(hMain);
    refresh_list(); refresh_chat();
    while(GetMessageW(&msg,NULL,0,0)>0){ if(IsDialogMessageW(hMain,&msg)) continue; /* Tab / Shift+Tab / Alt+mnemonics between controls */ TranslateMessage(&msg); DispatchMessageW(&msg); }
    lsm_stop(C);
    return 0; }
