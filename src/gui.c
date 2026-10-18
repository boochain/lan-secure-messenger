/* LanMessenger XP — pure Win32 GUI (no .NET, no GDI+). Chinese UI, system colours only
 * colours from one theme table (light/dark from design/palette.json, or the system pair), every
 * WM_CTLCOLOR* sets text AND background from the same row,
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
#include <wctype.h>
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
static int tmsg(const wchar_t *title, const wchar_t *text, int cancel);
static void fit_vscroll(HWND e);
static void msgbox(const char *t, UINT f){ (void)f; tmsg(L"局域网安全通讯 XP",W(t),0); }

/* ---------------------------------------------------------------- theme (design/palette.json, Fluent 2)
 * Every text/background pair is taken from ONE row of this table; no handler sets only one of them.
 * Mode: 0 = system (GetSysColor pairs, keeps high-contrast working), 1 = light, 2 = dark. */
typedef struct { COLORREF window_bg, sidebar_bg, card_bg, chat_bg, text_primary, text_secondary, accent, button_bg, button_fg, input_bg, input_fg, divider, online, warning, error; } theme_t;
#define HX(v) RGB(((v)>>16)&255,((v)>>8)&255,(v)&255)
static const theme_t THEME_LIGHT={HX(0xECE9D8),HX(0xFFFFFF),HX(0xFFFFFF),HX(0xFFFFFF),HX(0x000000),HX(0x55534A),HX(0x316AC5),HX(0x316AC5),HX(0xFFFFFF),HX(0xFFFFFF),HX(0x000000),HX(0xACA899),HX(0x217A17),HX(0x9D5D00),HX(0xC42B1C)}; /* Luna (tools/palettes/xp.json) */
static const theme_t THEME_DARK ={HX(0x202020),HX(0x262626),HX(0x2B2B2B),HX(0x1E1E1E),HX(0xFFFFFF),HX(0xCCCCCC),HX(0x3C78C3),HX(0x3C78C3),HX(0xFFFFFF),HX(0x2B2B2B),HX(0xFFFFFF),HX(0x3C3C3C),HX(0x6CCB5F),HX(0xFCE100),HX(0xFF99A4)};
/* Luna palette for the redesigned views (tools/palettes/xp.json) */
typedef struct { COLORREF rowsel, rowselborder, rowhover, accenttext, mine, mineborder, minefg, minemeta, theirs, theirsborder, theirsfg, meta, stroke, badge, badgefg, field, fieldborder, chip, chipfg, service, servicefg, read, tile, tilefg, header, av[8]; } xpal_t;
static const xpal_t PAL_LIGHT={HX(0xC1D2EE),HX(0x316AC5),HX(0xEEF3FB),HX(0x0B4AA8),HX(0xDCE9FA),HX(0xA7C1E6),HX(0x000000),HX(0x4A5568),HX(0xF4F3EE),HX(0xD6D3C4),HX(0x000000),HX(0x55534A),HX(0xD6D3C4),HX(0x316AC5),HX(0xFFFFFF),HX(0xFFFFFF),HX(0x7F9DB9),HX(0xECE9D8),HX(0x3B3A33),HX(0xF1EFE2),HX(0x3B3A33),HX(0x0B4AA8),HX(0xFFFFFF),HX(0x316AC5),HX(0xF6F5EF),
    {HX(0x6D4FC2),HX(0x1F6FB2),HX(0x0F7A6C),HX(0xB4471B),HX(0x2E6B2F),HX(0x9C2F55),HX(0x5A5F14),HX(0x8A4B08)}};
static const xpal_t PAL_DARK={HX(0x1F3A5F),HX(0x3C78C3),HX(0x2C2F35),HX(0x8EC1FF),HX(0x1F3A5A),HX(0x2F5A8A),HX(0xFFFFFF),HX(0xC9D9EC),HX(0x2E2E2E),HX(0x444444),HX(0xFFFFFF),HX(0xC5C5C5),HX(0x3C3C3C),HX(0x2F6AB8),HX(0xFFFFFF),HX(0x2B2B2B),HX(0x5A7AA0),HX(0x333333),HX(0xE0E0E0),HX(0x2B2B2B),HX(0xD6D6D6),HX(0x8EC1FF),HX(0x2B2B2B),HX(0x8EC1FF),HX(0x262626),
    {HX(0x6D4FC2),HX(0x1F6FB2),HX(0x0F7A6C),HX(0xB4471B),HX(0x2E6B2F),HX(0x9C2F55),HX(0x5A5F14),HX(0x8A4B08)}};
static xpal_t P;
static theme_t T; static int theme_mode, theme_cli=-1; /* 0 system,1 light,2 dark */
static HBRUSH brWin, brSide, brChat, brInput, brCard;
static HMENU hMenuTree; static HWND hMenuBtn[5];
#define IDC_MENUBTN 120
#define MENUBAR_H 30
static HFONT fCaption, fBody, fBodyStrong, fSubtitle, fTitle;
#define TITLE_H 32
#define IDM_THEME_SYS 230
#define IDM_THEME_LIGHT 231
#define IDM_THEME_DARK 232
static void theme_apply(void){
    P=theme_mode==2?PAL_DARK:PAL_LIGHT;
    if(theme_mode==1) T=THEME_LIGHT; else if(theme_mode==2) T=THEME_DARK;
    else { T.window_bg=GetSysColor(COLOR_BTNFACE); T.sidebar_bg=GetSysColor(COLOR_WINDOW); T.card_bg=GetSysColor(COLOR_WINDOW); T.chat_bg=GetSysColor(COLOR_WINDOW);
        T.text_primary=GetSysColor(COLOR_WINDOWTEXT); T.text_secondary=GetSysColor(COLOR_WINDOWTEXT); T.accent=GetSysColor(COLOR_HIGHLIGHT); T.button_bg=GetSysColor(COLOR_HIGHLIGHT); T.button_fg=GetSysColor(COLOR_HIGHLIGHTTEXT);
        T.input_bg=GetSysColor(COLOR_WINDOW); T.input_fg=GetSysColor(COLOR_WINDOWTEXT); T.divider=GetSysColor(COLOR_3DSHADOW); T.online=GetSysColor(COLOR_WINDOWTEXT); T.warning=T.online; T.error=T.online; }
    if(brWin){ DeleteObject(brWin); DeleteObject(brSide); DeleteObject(brChat); DeleteObject(brInput); DeleteObject(brCard); }
    brWin=CreateSolidBrush(T.window_bg); brSide=CreateSolidBrush(T.sidebar_bg); brChat=CreateSolidBrush(T.chat_bg); brInput=CreateSolidBrush(T.input_bg); brCard=CreateSolidBrush(T.card_bg); }
/* status bar / title texts are on window_bg; window text is the only body colour on it */
static LRESULT ctl_color(HDC dc, HWND ctl, int kind){ /* kind: 0 static,1 edit,2 listbox,3 button/dlg */
    COLORREF fg=T.text_primary, bg=T.window_bg; HBRUSH b=brWin;
    if(ctl && ctl==hChat){ fg=T.text_primary; bg=T.chat_bg; b=brChat; }
    else if(kind==1){ fg=T.input_fg; bg=T.input_bg; b=brInput; }
    else if(kind==2){ fg=T.text_primary; bg=T.sidebar_bg; b=brSide; }
    SetTextColor(dc,fg); SetBkColor(dc,bg); SetBkMode(dc,OPAQUE); return (LRESULT)b; }
static void theme_load(void){ wchar_t p[MAX_PATH]; FILE *f; _snwprintf(p,MAX_PATH,L"%s\\theme.txt",exe_dir); p[MAX_PATH-1]=0; f=_wfopen(p,L"r"); theme_mode=0; if(f){ int c=fgetc(f); if(c>='0'&&c<='2') theme_mode=c-'0'; fclose(f); } }
static void theme_save(void){ wchar_t p[MAX_PATH]; FILE *f; _snwprintf(p,MAX_PATH,L"%s\\theme.txt",exe_dir); p[MAX_PATH-1]=0; f=_wfopen(p,L"w"); if(f){ fputc('0'+theme_mode,f); fclose(f); } }
/* Fluent type ramp (px at 96 dpi) -> logical height; 14px = 10.5pt body, 12px caption, 20px subtitle, 28px title */
static HFONT mkfont(const wchar_t *face, int px, int weight){ LOGFONTW lf; HDC dc=GetDC(NULL); int dpi=GetDeviceCaps(dc,LOGPIXELSY); ReleaseDC(NULL,dc);
    memset(&lf,0,sizeof lf); lf.lfHeight=-MulDiv(px,dpi,96); lf.lfWeight=weight; lf.lfCharSet=DEFAULT_CHARSET; lf.lfQuality=CLEARTYPE_QUALITY; wcsncpy(lf.lfFaceName,face,31); return CreateFontIndirectW(&lf); }
static int scale_px(int px){ HDC dc=GetDC(NULL); int v=MulDiv(px,GetDeviceCaps(dc,LOGPIXELSY),96); ReleaseDC(NULL,dc); return v; }
/* flat 1px divider frame around a child (WS_EX_CLIENTEDGE draws a system-coloured 3D bevel) */
static void frame_child(HWND parent, HDC dc, HWND c){ RECT r; HBRUSH b; if(!c||!IsWindowVisible(c)) return; GetWindowRect(c,&r); MapWindowPoints(NULL,parent,(POINT*)&r,2); InflateRect(&r,1,1); b=CreateSolidBrush(T.divider); FrameRect(dc,&r,b); DeleteObject(b); }
/* owner-drawn menus: the system menu colours (COLOR_MENU/COLOR_MENUTEXT) would put black text on the dark menu bar */
static void AM(HMENU m, UINT f, UINT_PTR id, const wchar_t *t){ AppendMenuW(m,f|MF_OWNERDRAW,id,(LPCWSTR)t); }
static void menu_bg(HMENU m){ MENUINFO mi; int i,n=GetMenuItemCount(m); memset(&mi,0,sizeof mi); mi.cbSize=sizeof mi; mi.fMask=MIM_BACKGROUND; mi.hbrBack=brCard; for(i=0;i<n;i++){ HMENU sm=GetSubMenu(m,i); if(sm) SetMenuInfo(sm,&mi); } mi.hbrBack=(n&&GetSubMenu(m,0))?brWin:brCard; /* menu bar (has submenus) = window_bg, popups = card_bg */ SetMenuInfo(m,&mi); }
static void measure_menu(MEASUREITEMSTRUCT *mi){ const wchar_t *t=(const wchar_t*)mi->itemData; HDC dc=GetDC(hMain); SIZE sz; HGDIOBJ of=SelectObject(dc,fBody);
    if(!t){ mi->itemWidth=60; mi->itemHeight=scale_px(9); } else { RECT r={0,0,0,0}; DrawTextW(dc,t,-1,&r,DT_CALCRECT|DT_SINGLELINE); GetTextExtentPoint32W(dc,L"M",1,&sz); mi->itemWidth=r.right+scale_px(40); mi->itemHeight=scale_px(28); }
    SelectObject(dc,of); ReleaseDC(hMain,dc); }
static void draw_menu(DRAWITEMSTRUCT *d){ const wchar_t *t=(const wchar_t*)d->itemData; int hot=(d->itemState&(ODS_SELECTED|ODS_HOTLIGHT))!=0, dis=(d->itemState&(ODS_DISABLED|ODS_GRAYED))!=0, bar=0;
    COLORREF bg=hot?T.button_bg:(bar?T.window_bg:T.card_bg), fg=hot?T.button_fg:(dis?T.text_secondary:T.text_primary); HBRUSH b=CreateSolidBrush(bg); RECT r=d->rcItem;
    FillRect(d->hDC,&r,b); DeleteObject(b);
    if(!t){ RECT ln=r; HBRUSH dv=CreateSolidBrush(T.divider); ln.top=(r.top+r.bottom)/2; ln.bottom=ln.top+1; ln.left+=8; ln.right-=8; FillRect(d->hDC,&ln,dv); DeleteObject(dv); return; }
    SetBkMode(d->hDC,TRANSPARENT); SetTextColor(d->hDC,fg); SelectObject(d->hDC,fBody);
    if(bar) DrawTextW(d->hDC,t,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|((d->itemState&ODS_NOACCEL)?DT_HIDEPREFIX:0));
    else { if(d->itemState&ODS_CHECKED){ RECT c=r; c.left+=8; DrawTextW(d->hDC,L"\x2713",1,&c,DT_LEFT|DT_VCENTER|DT_SINGLELINE); } r.left+=scale_px(28); DrawTextW(d->hDC,t,-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|((d->itemState&ODS_NOACCEL)?DT_HIDEPREFIX:0)); } }
static void draw_menubtn(DRAWITEMSTRUCT *d){ wchar_t t[64]; int hot=(d->itemState&(ODS_SELECTED|ODS_FOCUS))!=0; COLORREF bg=hot?T.button_bg:T.window_bg, fg=hot?T.button_fg:T.text_primary; HBRUSH b=CreateSolidBrush(bg);
    FillRect(d->hDC,&d->rcItem,b); DeleteObject(b); GetWindowTextW(d->hwndItem,t,64); SetBkMode(d->hDC,TRANSPARENT); SetTextColor(d->hDC,fg); SelectObject(d->hDC,fBody);
    DrawTextW(d->hDC,t,-1,&d->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE|((d->itemState&ODS_NOACCEL)?DT_HIDEPREFIX:0)); }
static void draw_button(DRAWITEMSTRUCT *d){ wchar_t t[128]; int primary=(d->hwndItem==hSend)||(GetDlgCtrlID(d->hwndItem)==IDOK); COLORREF bg=primary?T.button_bg:T.card_bg, fg=primary?T.button_fg:T.text_primary; HBRUSH b=CreateSolidBrush(bg); HPEN pn=CreatePen(PS_SOLID,1,primary?bg:T.divider); HGDIOBJ op=SelectObject(d->hDC,pn), ob=SelectObject(d->hDC,b);
    if(d->itemState&ODS_SELECTED){ DeleteObject(b); b=CreateSolidBrush(primary?T.accent:T.divider); SelectObject(d->hDC,b); } FillRect(d->hDC,&d->rcItem,brWin); RoundRect(d->hDC,d->rcItem.left,d->rcItem.top,d->rcItem.right,d->rcItem.bottom,6,6); SelectObject(d->hDC,op); SelectObject(d->hDC,ob); DeleteObject(pn); DeleteObject(b);
    GetWindowTextW(d->hwndItem,t,128); SetBkMode(d->hDC,TRANSPARENT); SetTextColor(d->hDC,fg); SelectObject(d->hDC,fBody); DrawTextW(d->hDC,t,-1,&d->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    if(d->itemState&ODS_FOCUS){ RECT r=d->rcItem; InflateRect(&r,-3,-3); SetTextColor(d->hDC,fg); DrawFocusRect(d->hDC,&r); } }
static void draw_list_item(DRAWITEMSTRUCT *d){ wchar_t t[600]; int sel=(d->itemState&ODS_SELECTED)!=0; COLORREF bg=sel?T.button_bg:T.sidebar_bg, fg=sel?T.button_fg:T.text_primary; HBRUSH b; RECT r=d->rcItem;
    if(d->itemID==(UINT)-1){ FillRect(d->hDC,&d->rcItem,brSide); return; }
    b=CreateSolidBrush(bg); FillRect(d->hDC,&r,b); DeleteObject(b);
    SendMessageW(d->hwndItem,LB_GETTEXT,d->itemID,(LPARAM)t); SetBkMode(d->hDC,TRANSPARENT); SelectObject(d->hDC,fBody);
    r.left+=10; if(t[0]==0x25CF && !sel){ SetTextColor(d->hDC,T.online); DrawTextW(d->hDC,t,1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX); r.left+=18; SetTextColor(d->hDC,fg); DrawTextW(d->hDC,t+2,-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS); }
    else { SetTextColor(d->hDC,fg); DrawTextW(d->hDC,t,-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS); }
    if(d->itemState&ODS_FOCUS){ RECT f=d->rcItem; InflateRect(&f,-1,-1); SetTextColor(d->hDC,fg); SetBkColor(d->hDC,bg); DrawFocusRect(d->hDC,&f); } }

/* ---------------------------------------------------------------- prompt dialogs (no .rc templates) */
typedef struct { const wchar_t *label; wchar_t text[512]; int done, ok; HWND ed, lb; const char **items; int nitems; int sel[64]; int nsel; int multi; int no_cancel; } prompt_t;
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
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: case WM_CTLCOLORDLG: return ctl_color((HDC)w,(HWND)l,0);
    case WM_CTLCOLOREDIT: return ctl_color((HDC)w,(HWND)l,1);
    case WM_CTLCOLORLISTBOX: return ctl_color((HDC)w,(HWND)l,2);
    case WM_ERASEBKGND: { RECT r; GetClientRect(h,&r); FillRect((HDC)w,&r,brWin); return 1; }
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps); RECT cr, tr; wchar_t t[128]; HBRUSH dv=CreateSolidBrush(T.divider); GetClientRect(h,&cr);
        tr=cr; tr.bottom=TITLE_H; FillRect(dc,&tr,brCard); tr.top=TITLE_H-1; FillRect(dc,&tr,dv); FrameRect(dc,&cr,dv); DeleteObject(dv);
        GetWindowTextW(h,t,128); tr=cr; tr.left=12; tr.bottom=TITLE_H; SetBkMode(dc,TRANSPARENT); SetTextColor(dc,T.text_primary); SelectObject(dc,fBodyStrong); DrawTextW(dc,t,-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        if(p){ frame_child(h,dc,p->ed); frame_child(h,dc,p->lb); } EndPaint(h,&ps); return 0; }
    case WM_NCHITTEST: { POINT pt={(short)LOWORD(l),(short)HIWORD(l)}; ScreenToClient(h,&pt); if(pt.y<TITLE_H) return HTCAPTION; break; }
    case WM_MEASUREITEM: { MEASUREITEMSTRUCT *mi=(MEASUREITEMSTRUCT*)l; if(mi->CtlType==ODT_LISTBOX){ mi->itemHeight=scale_px(28); return TRUE; } break; }
    case WM_DRAWITEM: { DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT*)l; if(d->CtlType==ODT_LISTBOX){ draw_list_item(d); return TRUE; } if(d->CtlType==ODT_BUTTON){ draw_button(d); return TRUE; } break; }
    }
    return DefWindowProcW(h,m,w,l); }
static int prompt(const wchar_t *title, const wchar_t *label, const wchar_t *init, prompt_t *p, int with_edit){
    HWND h, st, ok, ca=NULL, prev_focus=GetFocus(); MSG msg; RECT r; int H=with_edit?150:120, i, y=12, lh;
    if(p->items) H+=180;
    p->done=p->ok=0; p->label=label; p->ed=p->lb=NULL;
    GetWindowRect(hMain,&r);
    h=CreateWindowExW(WS_EX_TOOLWINDOW,L"LsmPrompt",title,WS_POPUP,r.left+80,r.top+80,420,H,hMain,NULL,hInst,NULL); /* no system caption: title bar drawn from the theme (WM_PAINT), drag via WM_NCHITTEST */ y+=TITLE_H;
    SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)p);
    { RECT lr={0,0,390,0}; HDC dc=GetDC(h); HGDIOBJ of=SelectObject(dc,fBody); DrawTextW(dc,label,-1,&lr,DT_CALCRECT|DT_WORDBREAK); SelectObject(dc,of); ReleaseDC(h,dc); lh=lr.bottom+4; if(lh<36) lh=36; }
    st=CreateWindowW(L"STATIC",label,WS_CHILD|WS_VISIBLE,12,y,390,lh,h,NULL,hInst,NULL); SendMessageW(st,WM_SETFONT,(WPARAM)hFont,1); y+=lh+8;
    if(p->items){ p->lb=CreateWindowExW(0,L"LISTBOX",NULL,WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|(p->multi?LBS_EXTENDEDSEL|LBS_MULTIPLESEL:0),12,y,390,170,h,NULL,hInst,NULL);
        SendMessageW(p->lb,WM_SETFONT,(WPARAM)hFont,1); for(i=0;i<p->nitems;i++) SendMessageW(p->lb,LB_ADDSTRING,0,(LPARAM)W(p->items[i])); if(!p->multi && p->nitems) SendMessageW(p->lb,LB_SETCURSEL,0,0); y+=178; }
    if(with_edit){ p->ed=CreateWindowExW(0,L"EDIT",init?init:L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,13,y+3,388,22,h,NULL,hInst,NULL); SendMessageW(p->ed,WM_SETFONT,(WPARAM)hFont,1); y+=32; }
    ok=CreateWindowW(L"BUTTON",L"确定",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,p->no_cancel?316:220,y,86,28,h,(HMENU)IDOK,hInst,NULL);
    if(!p->no_cancel) ca=CreateWindowW(L"BUTTON",L"取消",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,316,y,86,28,h,(HMENU)IDCANCEL,hInst,NULL);
    SendMessageW(ok,WM_SETFONT,(WPARAM)hFont,1); if(ca) SendMessageW(ca,WM_SETFONT,(WPARAM)hFont,1);
    SetWindowPos(h,NULL,0,0,420,y+44,SWP_NOMOVE|SWP_NOZORDER);
    EnableWindow(hMain,FALSE); ShowWindow(h,SW_SHOW); if(p->ed) SetFocus(p->ed); else if(p->lb) SetFocus(p->lb); else SetFocus(ok);
    while(!p->done && GetMessageW(&msg,NULL,0,0)){ if(!IsDialogMessageW(h,&msg)){ TranslateMessage(&msg); DispatchMessageW(&msg);} }
    EnableWindow(hMain,TRUE); SetForegroundWindow(hMain); if(prev_focus && IsWindow(prev_focus)) SetFocus(prev_focus); /* keyboard focus survives the dialog */
    return p->ok; }

/* themed replacement for MessageBox (system message boxes ignore the app theme) */
static int tmsg(const wchar_t *title, const wchar_t *text, int cancel){ prompt_t p; HWND pf=GetFocus(); int r; memset(&p,0,sizeof p); p.no_cancel=!cancel; r=prompt(title,text,NULL,&p,0); if(pf && IsWindow(pf)) SetFocus(pf); return r; }

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

/* ---------------------------------------------------------------- chat view: owner-drawn bubble list (xp-*-chat mockups)
 * LISTBOX + LBS_OWNERDRAWVARIABLE: one item per row (day chip, service notice, text bubble, file card). Rounded bubbles
 * with the time (and ticks on outgoing) inside the bubble, file cards with a type tile, open button and folder link.
 * Plain GDI only (RoundRect/Polyline/DrawText): XP SP3 without GDI+. */
enum { R_DAY, R_NOTICE, R_TEXT, R_FILE };
typedef struct { int kind, out, status, tstate, pct, h, bw; char id[40]; char path[520]; wchar_t *text; wchar_t time[8]; wchar_t name[260]; wchar_t meta[96]; wchar_t ext[8]; } row_t;
static row_t *rows; static int nrows, caprows;
static int nchat;
static void rows_clear(void){ int i; for(i=0;i<nrows;i++) free(rows[i].text); nrows=0; }
static row_t *row_add(int kind){ row_t *r; if(nrows==caprows){ caprows=caprows?caprows*2:64; rows=(row_t*)realloc(rows,caprows*sizeof *rows); } r=&rows[nrows++]; memset(r,0,sizeof *r); r->kind=kind; return r; }
static wchar_t *wdup(const wchar_t *s){ size_t n=wcslen(s)+1; wchar_t *d=(wchar_t*)malloc(n*sizeof(wchar_t)); memcpy(d,s,n*sizeof(wchar_t)); return d; }
static void fmt_size(uint64_t b, wchar_t *o, int n){ if(b>=1048576) _snwprintf(o,n,L"%.1f MB",b/1048576.0); else if(b>=1024) _snwprintf(o,n,L"%.0f KB",b/1024.0); else _snwprintf(o,n,L"%u B",(unsigned)b); o[n-1]=0; }
#define PADX 10
#define PADY 6
static int chat_maxw(void){ RECT r; GetClientRect(hChat,&r); int w=(r.right-r.left)-GetSystemMetrics(SM_CXVSCROLL)-scale_px(24); w=w*68/100; if(w>scale_px(520)) w=scale_px(520); if(w<scale_px(160)) w=scale_px(160); return w; }
static int line_h(HDC dc, HFONT f){ TEXTMETRICW tm; HGDIOBJ o=SelectObject(dc,f); GetTextMetricsW(dc,&tm); SelectObject(dc,o); return tm.tmHeight; }
static int time_w(HDC dc, row_t *r){ SIZE s; HGDIOBJ o=SelectObject(dc,fCaption); GetTextExtentPoint32W(dc,r->time,(int)wcslen(r->time),&s); SelectObject(dc,o); return s.cx+(r->out?scale_px(20):0); }
/* text bubble geometry: returns bubble width, fills height; time goes on the last line when it fits, else on its own line */
static void text_geom(HDC dc, row_t *r, int maxw, int *bw, int *bh, int *tw_inline){ RECT tr={0,0,maxw-2*PADX,0}; HGDIOBJ o=SelectObject(dc,fBody); int lh=line_h(dc,fBody), tw=time_w(dc,r), gap=scale_px(10);
    DrawTextW(dc,r->text,-1,&tr,DT_CALCRECT|DT_WORDBREAK|DT_EDITCONTROL|DT_NOPREFIX); SelectObject(dc,o);
    if(tr.bottom<=lh+1 && tr.right+gap+tw<=maxw-2*PADX){ *bw=tr.right+gap+tw+2*PADX; *bh=lh+2*PADY; *tw_inline=1; }
    else { *bw=(tr.right>tw?tr.right:tw)+2*PADX; *bh=tr.bottom+line_h(dc,fCaption)+2*PADY; *tw_inline=0; } }
static void measure_chat(MEASUREITEMSTRUCT *mi){ row_t *r; HDC dc; int bw,bh,inl; if(mi->itemID>=(UINT)nrows){ mi->itemHeight=1; return; } r=&rows[mi->itemID]; dc=GetDC(hChat);
    if(r->kind==R_DAY||r->kind==R_NOTICE) mi->itemHeight=line_h(dc,fCaption)+scale_px(r->kind==R_DAY?20:14);
    else if(r->kind==R_TEXT){ text_geom(dc,r,chat_maxw(),&bw,&bh,&inl); r->bw=bw; mi->itemHeight=bh+scale_px(6); }
    else { r->bw=scale_px(272); mi->itemHeight=scale_px(r->path[0]?86:66)+scale_px(6); }
    r->h=mi->itemHeight; ReleaseDC(hChat,dc); }
static void bubble(HDC dc, RECT b, COLORREF fill, COLORREF edge){ HBRUSH br=CreateSolidBrush(fill); HPEN pn=CreatePen(PS_SOLID,1,edge); HGDIOBJ ob=SelectObject(dc,br), op=SelectObject(dc,pn);
    RoundRect(dc,b.left,b.top,b.right,b.bottom,scale_px(14),scale_px(14)); SelectObject(dc,ob); SelectObject(dc,op); DeleteObject(br); DeleteObject(pn); }
/* delivery ticks drawn as strokes (SimSun has no U+2713): 1 = sent, 2 = delivered, 3 = read (accent), 0 = queued (dot), 5 = failed (!) */
static void ticks(HDC dc, int x, int ymid, int st){ COLORREF c=st==3?P.read:st==5?T.error:P.minemeta; HPEN pn=CreatePen(PS_SOLID,scale_px(2)>1?2:1,c); HGDIOBJ op=SelectObject(dc,pn); int s=scale_px(4), k, n=st>=2&&st!=5?2:1;
    if(st==5||st==0){ RECT r={x,ymid-8,x+scale_px(16),ymid+8}; SetTextColor(dc,c); DrawTextW(dc,st==5?L"!":L"\x2026",-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE); }
    else for(k=0;k<n;k++){ int dx=k*scale_px(5); POINT p[3]={{x+dx-s,ymid},{x+dx,ymid+s},{x+dx+2*s,ymid-s}}; Polyline(dc,p,3); }
    SelectObject(dc,op); DeleteObject(pn); }
static RECT file_btn_rect(RECT b){ RECT r; r.right=b.right-scale_px(10); r.left=r.right-scale_px(28); r.top=b.top+scale_px(12); r.bottom=r.top+scale_px(28); return r; }
static RECT file_link_rect(RECT b){ RECT r; r.left=b.left+scale_px(10); r.right=b.left+scale_px(140); r.bottom=b.bottom-scale_px(6); r.top=r.bottom-scale_px(20); return r; }
static RECT row_bubble(row_t *r, RECT it){ RECT b=it; int m=scale_px(12); b.bottom-=scale_px(6); if(r->out){ b.right-=m; b.left=b.right-r->bw; } else { b.left+=m; b.right=b.left+r->bw; } return b; }
static void draw_chat_item(DRAWITEMSTRUCT *d){ RECT it=d->rcItem, b; row_t *r; HDC dc=d->hDC; HBRUSH bg=brChat; int lh;
    FillRect(dc,&it,bg); if(d->itemID>=(UINT)nrows) return; r=&rows[d->itemID]; SetBkMode(dc,TRANSPARENT);
    if(r->kind==R_DAY||r->kind==R_NOTICE){ SIZE s; HGDIOBJ o=SelectObject(dc,fCaption); int w; GetTextExtentPoint32W(dc,r->text,(int)wcslen(r->text),&s); w=s.cx+scale_px(r->kind==R_DAY?24:20);
        b.left=(it.left+it.right-w)/2; b.right=b.left+w; b.top=it.top+scale_px(r->kind==R_DAY?7:3); b.bottom=it.bottom-scale_px(r->kind==R_DAY?7:5);
        bubble(dc,b,r->kind==R_DAY?P.chip:P.service,P.stroke); SetTextColor(dc,r->kind==R_DAY?P.chipfg:P.servicefg); DrawTextW(dc,r->text,-1,&b,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX); SelectObject(dc,o); return; }
    b=row_bubble(r,it); bubble(dc,b,r->out?P.mine:P.theirs,r->out?P.mineborder:P.theirsborder);
    if(r->kind==R_TEXT){ int bw,bh,inl; RECT tr=b, tm; text_geom(dc,r,chat_maxw(),&bw,&bh,&inl); lh=line_h(dc,fBody);
        InflateRect(&tr,-PADX,-PADY); SelectObject(dc,fBody); SetTextColor(dc,r->out?P.minefg:P.theirsfg); DrawTextW(dc,r->text,-1,&tr,DT_WORDBREAK|DT_EDITCONTROL|DT_NOPREFIX);
        tm=tr; tm.top=tm.bottom-(inl?lh:line_h(dc,fCaption)); if(r->out) tm.right-=scale_px(20); SelectObject(dc,fCaption); SetTextColor(dc,r->out?P.minemeta:P.meta); DrawTextW(dc,r->time,-1,&tm,DT_RIGHT|DT_BOTTOM|DT_SINGLELINE);
        if(r->out) ticks(dc,tr.right-scale_px(16),(tm.top+tm.bottom)/2+1,r->status); }
    else { RECT tile={b.left+scale_px(10),b.top+scale_px(10),b.left+scale_px(46),b.top+scale_px(46)}, nr, br=file_btn_rect(b), tm; COLORREF fg=r->out?P.minefg:P.theirsfg, meta=r->out?P.minemeta:P.meta;
        bubble(dc,tile,P.tile,P.stroke); SelectObject(dc,fCaption); SetTextColor(dc,P.tilefg); DrawTextW(dc,r->ext,-1,&tile,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        nr.left=tile.right+scale_px(10); nr.right=br.left-scale_px(6); nr.top=b.top+scale_px(9); nr.bottom=nr.top+line_h(dc,fBodyStrong);
        SelectObject(dc,fBodyStrong); SetTextColor(dc,fg); DrawTextW(dc,r->name,-1,&nr,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
        nr.top=nr.bottom+scale_px(2); nr.bottom=nr.top+line_h(dc,fCaption); SelectObject(dc,fCaption); SetTextColor(dc,meta); DrawTextW(dc,r->meta,-1,&nr,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
        if(r->path[0]){ /* open button: rounded square with a down-arrow into a tray */ HPEN pn=CreatePen(PS_SOLID,2,P.accenttext); HGDIOBJ op; int cx=(br.left+br.right)/2, cy=(br.top+br.bottom)/2, a=scale_px(6);
            bubble(dc,br,P.tile,P.stroke); op=SelectObject(dc,pn); { POINT s1[2]={{cx,cy-a},{cx,cy+a/2}}, s2[3]={{cx-a/2-1,cy},{cx,cy+a/2+1},{cx+a/2+1,cy}}, s3[2]={{cx-a,cy+a},{cx+a+1,cy+a}}; Polyline(dc,s1,2); Polyline(dc,s2,3); Polyline(dc,s3,2); } SelectObject(dc,op); DeleteObject(pn);
            { RECT lr=file_link_rect(b); SetTextColor(dc,P.accenttext); DrawTextW(dc,L"在文件夹中显示",-1,&lr,DT_LEFT|DT_VCENTER|DT_SINGLELINE); } }
        tm=b; InflateRect(&tm,-PADX,-PADY); if(r->out) tm.right-=scale_px(20); SetTextColor(dc,meta); DrawTextW(dc,r->time,-1,&tm,DT_RIGHT|DT_BOTTOM|DT_SINGLELINE);
        if(r->out) ticks(dc,b.right-PADX-scale_px(16),tm.bottom-line_h(dc,fCaption)/2,r->status); }
    if((d->itemState&ODS_FOCUS) && GetFocus()==hChat){ RECT f=b; InflateRect(&f,2,2); SetTextColor(dc,T.text_primary); SetBkColor(dc,T.chat_bg); DrawFocusRect(dc,&f); } }
static void refresh_chat(void){
    lsm_msg *m; int n,i, lastday=-1; wchar_t title[400]=L"", peer[300]=L"";
    SendMessageW(hChat,WM_SETREDRAW,FALSE,0); SendMessageW(hChat,LB_RESETCONTENT,0,0); rows_clear(); nchat=0;
    lsm_lock(C);
    if(cur_conv[0]){ lsm_contact *k; lsm_group *g; int nk, j;
        if(!strncmp(cur_conv,"dm:",3)){ nk=lsm_contacts(C,&k); for(j=0;j<nk;j++) if(!strcmp(k[j].fp,cur_conv+3)){ wcsncpy(peer,W(k[j].name),299); _snwprintf(title,400,L"%s    %s  ·  %s  ·  局域网直连",W(k[j].name),k[j].online?L"在线":L"离线",k[j].trust==1?L"已验证身份":k[j].trust==2?L"已屏蔽":L"未验证身份"); } }
        else { char u[37]; nk=lsm_groups(C,&g); for(j=0;j<nk;j++){ lsm_uuid_str(g[j].id,u); if(!strcmp(u,cur_conv+6)) _snwprintf(title,400,L"%s    %d 人  ·  群密钥版本 %llu",W(g[j].name),g[j].nmembers,(unsigned long long)g[j].key_epoch); } }
        title[399]=0; peer[299]=0;
        n=lsm_messages(C,&m);
        for(i=0;i<n;i++){ time_t t; struct tm *tm; row_t *r; if(strcmp(m[i].conv,cur_conv)) continue;
            t=(time_t)(m[i].ts_ms/1000); tm=localtime(&t);
            if(tm && tm->tm_yday+tm->tm_year*400!=lastday){ time_t now=time(NULL); struct tm nw=*localtime(&now); wchar_t dl[32]; lastday=tm->tm_yday+tm->tm_year*400;
                if(tm->tm_year==nw.tm_year && tm->tm_yday==nw.tm_yday) wcscpy(dl,L"今天"); else if(tm->tm_year==nw.tm_year && tm->tm_yday==nw.tm_yday-1) wcscpy(dl,L"昨天"); else wcsftime(dl,32,L"%m月%d日",tm);
                row_add(R_DAY)->text=wdup(dl);
                if(nrows==1 && peer[0]){ wchar_t nt[400]; _snwprintf(nt,400,L"消息已端到端加密，只有你和%s能看到",peer); nt[399]=0; row_add(R_NOTICE)->text=wdup(nt); } }
            if(m[i].kind==K_SYSTEM){ row_add(R_NOTICE)->text=wdup(W(m[i].body)); continue; }
            r=row_add(m[i].kind==K_FILE?R_FILE:R_TEXT); r->out=m[i].out; r->status=m[i].status; memcpy(r->id,m[i].id,40); if(tm) wcsftime(r->time,8,L"%H:%M",tm);
            if(r->kind==R_TEXT) r->text=wdup(W(m[i].body));
            else { lsm_transfer *t2; int nt=lsm_transfers(C,&t2), j2; const wchar_t *dot; wchar_t sz[32]; static const wchar_t *TS[]={L"等待接收",L"传输中",L"校验中",L"",L"失败",L"被拒收",L"已取消"};
                wcsncpy(r->name,W(m[i].file_name),259); dot=wcsrchr(r->name,L'.'); if(dot && wcslen(dot+1)<=4){ int q; for(q=0;dot[1+q];q++) r->ext[q]=(wchar_t)towupper(dot[1+q]); } else wcscpy(r->ext,L"FILE");
                fmt_size(m[i].file_size,sz,32); r->tstate=3;
                for(j2=0;j2<nt;j2++) if(!strcmp(t2[j2].msg_id,m[i].id)){ r->tstate=t2[j2].state; if(t2[j2].size) r->pct=(int)(t2[j2].transferred*100/t2[j2].size); if(t2[j2].state==3) strncpy(r->path,t2[j2].path,519); }
                if(!r->path[0] && m[i].file_path[0] && r->tstate==3) strncpy(r->path,m[i].file_path,519);
                if(r->tstate==1) _snwprintf(r->meta,96,L"%s  ·  %s %d%%",sz,TS[1],r->pct); else if(r->tstate==3) _snwprintf(r->meta,96,L"%s  ·  %s",sz,r->ext); else _snwprintf(r->meta,96,L"%s  ·  %s",sz,TS[r->tstate<7?r->tstate:4]); r->meta[95]=0; }
        } }
    lsm_unlock(C);
    SetWindowTextW(hTitle,cur_conv[0]?title:L"");
    for(i=0;i<nrows;i++) SendMessageW(hChat,LB_ADDSTRING,0,(LPARAM)i);
    if(nrows){ RECT cr; int top=nrows-1, acc; GetClientRect(hChat,&cr); acc=rows[top].h; while(top>0 && acc+rows[top-1].h<=cr.bottom){ top--; acc+=rows[top].h; } SendMessageW(hChat,LB_SETTOPINDEX,top,0); } /* newest at the bottom, scrolled to the end */
    SendMessageW(hChat,WM_SETREDRAW,TRUE,0); InvalidateRect(hChat,NULL,TRUE); InvalidateRect(hMain,NULL,FALSE); }

static void copy_text(const wchar_t *t){ size_t n=(wcslen(t)+1)*sizeof(wchar_t); HGLOBAL g; if(!OpenClipboard(hMain)) return; EmptyClipboard(); g=GlobalAlloc(GMEM_MOVEABLE,n); if(g){ memcpy(GlobalLock(g),t,n); GlobalUnlock(g); SetClipboardData(CF_UNICODETEXT,g); } CloseClipboard(); }
static void show_in_folder(const char *path){ wchar_t a[1200]; _snwprintf(a,1200,L"/select,\"%s\"",W(path)); a[1199]=0; ShellExecuteW(hMain,L"open",L"explorer.exe",a,NULL,SW_SHOWNORMAL); }
/* right-click (or Shift+F10 / menu key) on a bubble: 复制 / 打开 / 在文件夹中显示 / 删除这条消息（仅本机）; left-click on the file card's button or link */
static WNDPROC oldChatProc;
static int chat_hit(LPARAM l, RECT *ir){ DWORD r=(DWORD)SendMessageW(hChat,LB_ITEMFROMPOINT,0,l); int i=(int)LOWORD(r); if(HIWORD(r) || i<0 || i>=nrows) return -1; SendMessageW(hChat,LB_GETITEMRECT,i,(LPARAM)ir); return i; }
static LRESULT CALLBACK ChatProc(HWND h, UINT m, WPARAM w, LPARAM l){
    if(m==WM_LBUTTONUP){ RECT ir; int i=chat_hit(l,&ir); POINT p={(short)LOWORD(l),(short)HIWORD(l)};
        if(i>=0 && rows[i].kind==R_FILE && rows[i].path[0]){ RECT b=row_bubble(&rows[i],ir), br=file_btn_rect(b), lr=file_link_rect(b);
            if(PtInRect(&br,p)) ShellExecuteW(hMain,L"open",W(rows[i].path),NULL,NULL,SW_SHOWNORMAL); else if(PtInRect(&lr,p)) show_in_folder(rows[i].path); } }
    if(m==WM_SETCURSOR){ POINT p; RECT ir; int i; GetCursorPos(&p); ScreenToClient(h,&p); i=chat_hit(MAKELPARAM(p.x,p.y),&ir);
        if(i>=0 && rows[i].kind==R_FILE && rows[i].path[0]){ RECT b=row_bubble(&rows[i],ir), br=file_btn_rect(b), lr=file_link_rect(b); if(PtInRect(&br,p)||PtInRect(&lr,p)){ SetCursor(LoadCursor(NULL,IDC_HAND)); return TRUE; } } }
    if(m==WM_CONTEXTMENU){ POINT pt; RECT ir; int k=-1, cmd; HMENU pm; row_t *r=NULL;
        if(l==(LPARAM)-1){ k=(int)SendMessageW(h,LB_GETCARETINDEX,0,0); if(k>=0 && k<nrows){ SendMessageW(h,LB_GETITEMRECT,k,(LPARAM)&ir); pt.x=ir.left+scale_px(40); pt.y=ir.top+scale_px(10); ClientToScreen(h,&pt); } else { pt.x=pt.y=0; ClientToScreen(h,&pt); } }
        else { POINT c; pt.x=(short)LOWORD(l); pt.y=(short)HIWORD(l); c=pt; ScreenToClient(h,&c); k=chat_hit(MAKELPARAM(c.x,c.y),&ir); if(k>=0) SendMessageW(h,LB_SETCURSEL,k,0); }
        if(k>=0 && k<nrows && (rows[k].kind==R_TEXT||rows[k].kind==R_FILE)) r=&rows[k];
        pm=CreatePopupMenu();
        AM(pm,MF_STRING|(r?0:MF_GRAYED),1,L"复制(&C)\tCtrl+C");
        if(r && r->kind==R_FILE){ AM(pm,MF_STRING|(r->path[0]?0:MF_GRAYED),4,L"打开(&O)"); AM(pm,MF_STRING|(r->path[0]?0:MF_GRAYED),5,L"在文件夹中显示(&S)"); }
        AM(pm,MF_SEPARATOR,0,NULL); AM(pm,MF_STRING|(r?0:MF_GRAYED),3,L"删除这条消息（仅本机）(&D)…"); menu_bg(pm);
        cmd=TrackPopupMenu(pm,TPM_RIGHTBUTTON|TPM_RETURNCMD|TPM_NONOTIFY,pt.x,pt.y,0,h,NULL); DestroyMenu(pm);
        if(cmd==1 && r) copy_text(r->kind==R_TEXT?r->text:r->name);
        else if(cmd==4 && r) ShellExecuteW(hMain,L"open",W(r->path),NULL,NULL,SW_SHOWNORMAL);
        else if(cmd==5 && r) show_in_folder(r->path);
        else if(cmd==3 && r){ char id[40]; memcpy(id,r->id,40); if(tmsg(L"删除消息",L"只会从本机删除这条消息，对方仍保留。此操作无法撤销。确定删除？",1)==1){ if(lsm_delete_message(C,id)) msgbox("删除失败：消息不存在。",MB_ICONWARNING); refresh_chat(); } }
        return 0; }
    if(m==WM_KEYDOWN && w=='C' && (GetKeyState(VK_CONTROL)&0x8000)){ int k=(int)SendMessageW(h,LB_GETCARETINDEX,0,0); if(k>=0 && k<nrows && rows[k].kind>=R_TEXT) copy_text(rows[k].kind==R_TEXT?rows[k].text:rows[k].name); return 0; }
    return CallWindowProcW(oldChatProc,h,m,w,l); }

/* Edit controls with WS_VSCROLL always show a (system-coloured) scrollbar; show it only when the text overflows. */
static void fit_vscroll(HWND e){ RECT r; TEXTMETRICW tm; HDC dc; HGDIOBJ of; int lines, vis, need; LONG st=GetWindowLongW(e,GWL_STYLE);
    GetClientRect(e,&r); dc=GetDC(e); of=SelectObject(dc,(HFONT)SendMessageW(e,WM_GETFONT,0,0)); GetTextMetricsW(dc,&tm); SelectObject(dc,of); ReleaseDC(e,dc);
    lines=(int)SendMessageW(e,EM_GETLINECOUNT,0,0); vis=tm.tmHeight?(r.bottom-r.top)/tm.tmHeight:1; need=lines>vis;
    if(need!=((st&WS_VSCROLL)!=0)){ ShowScrollBar(e,SB_VERT,need); } }
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
    case IDM_THEME_SYS: case IDM_THEME_LIGHT: case IDM_THEME_DARK: { HMENU mb=hMenuTree; theme_mode=id-IDM_THEME_SYS; theme_save(); theme_apply();
        CheckMenuRadioItem(GetSubMenu(mb,3),IDM_THEME_SYS,IDM_THEME_DARK,id,MF_BYCOMMAND); menu_bg(mb); RedrawWindow(hMain,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_FRAME); break; }
    case IDM_SHOW: ShowWindow(hMain,SW_SHOW); ShowWindow(hMain,SW_RESTORE); SetForegroundWindow(hMain); break;
    case IDM_FIREWALL: { wchar_t exe[MAX_PATH]; char e8[1024], rep[2048]; GetModuleFileNameW(NULL,exe,MAX_PATH); U(exe,e8,sizeof e8);
        if(tmsg(L"防火墙设置",L"将为本程序添加 Windows 防火墙例外（XP 使用 netsh firewall，Vista 及以上使用 netsh advfirewall）。需要管理员权限。继续？",1)!=1) break;
        { int rc=lsm_firewall_setup(e8,rep,sizeof rep); char m[2300]; snprintf(m,sizeof m,"%s\r\n\r\n%s",rc==0?"已添加防火墙例外。":"添加失败（可能需要以管理员身份运行）。",rep); msgbox(m,rc==0?MB_ICONINFORMATION:MB_ICONWARNING); } break; }
    case IDM_ADDIP: if(prompt(L"按 IP 添加",L"输入对方 IP 地址（可带端口，如 192.168.1.20 或 192.168.1.20:41235）：",L"",&p,1)){ if(lsm_add_addr(C,U(p.text,u8,sizeof u8),err)) msgbox(err,MB_ICONWARNING); } break;
    case IDM_TRUST: case IDM_UNTRUST: case IDM_BLOCK: { const char *fp=selected_peer(); if(fp){ lsm_set_trust(C,fp,id==IDM_TRUST?1:id==IDM_BLOCK?2:0,err); refresh_list(); } break; }
    case IDM_PINOFFER: { char pin[8], m[256]; lsm_pin_offer(C,pin); snprintf(m,sizeof m,"请把下面的 PIN 告诉对方，并让对方在“输入 PIN 配对”中输入（5 分钟内有效）：\r\n\r\n        %s",pin); msgbox(m,MB_ICONINFORMATION); break; }
    case IDM_PINPAIR: { const char *fp=selected_peer(); if(fp && prompt(L"输入 PIN 配对",L"输入对方显示的 6 位 PIN：",L"",&p,1)){ if(lsm_pin_pair(C,fp,U(p.text,u8,sizeof u8),err)) msgbox(err,MB_ICONWARNING); } break; }
    case IDM_GCREATE: { char sel[64][FP_LEN]; const char *fps[64]; wchar_t nm[512]; int n=pick_peers(L"创建群组",L"选择成员（可多选，仅显示已信任联系人），并在下方输入群名：",1,1,sel,L"新群组",nm,1), i; char gid[40];
        if(n<0) break; for(i=0;i<n;i++) fps[i]=sel[i]; if(lsm_group_create(C,U(nm,u8,sizeof u8),fps,n,gid,err)) msgbox(err,MB_ICONWARNING); else { snprintf(cur_conv,64,"group:%s",gid); refresh_list(); refresh_chat(); } break; }
    case IDM_GINVITE: { const char *g=selected_group(); char sel[64][FP_LEN]; const char *fps[64]; int n,i; if(!g) break; n=pick_peers(L"邀请成员",L"选择要邀请的成员（可多选）：",1,1,sel,NULL,NULL,0); if(n<=0) break; for(i=0;i<n;i++) fps[i]=sel[i]; if(lsm_group_invite(C,g,fps,n,err)) msgbox(err,MB_ICONWARNING); break; }
    case IDM_GREMOVE: { const char *g=selected_group(); char sel[64][FP_LEN]; int n; if(!g) break; n=pick_peers(L"移除成员",L"选择要移出群组的成员（将自动更换群密钥）：",0,0,sel,NULL,NULL,0); if(n<=0) break; if(lsm_group_remove(C,g,sel[0],err)) msgbox(err,MB_ICONWARNING); break; }
    case IDM_GLEAVE: { const char *g=selected_group(); if(g && tmsg(L"退出群组",L"确定退出该群组？",1)==1){ if(lsm_group_leave(C,g,err)) msgbox(err,MB_ICONWARNING); cur_conv[0]=0; refresh_list(); refresh_chat(); } break; }
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

static void layout(int W_, int H_0){
    int T0=MENUBAR_H, H_=H_0-T0, sb=26, lw=W_/3<260?W_/3:260, ih=64, bw=96, i, x=4;
    for(i=0;i<5;i++){ wchar_t t[32]; RECT r={0,0,0,0}; HDC dc=GetDC(hMain); HGDIOBJ of=SelectObject(dc,fBody); GetWindowTextW(hMenuBtn[i],t,32); DrawTextW(dc,t,-1,&r,DT_CALCRECT|DT_SINGLELINE); SelectObject(dc,of); ReleaseDC(hMain,dc);
        MoveWindow(hMenuBtn[i],x,2,r.right+20,T0-4,TRUE); x+=r.right+22; }
    MoveWindow(hList,6,T0+6,lw,H_-sb-12,TRUE);
    MoveWindow(hTitle,lw+12,T0+6,W_-lw-18,22,TRUE); /* body_strong 14px */
    MoveWindow(hChat,lw+12,T0+30,W_-lw-18,H_-sb-30-ih-14,TRUE);
    MoveWindow(hInput,lw+12,T0+H_-sb-ih-6,W_-lw-18-bw-6,ih,TRUE);
    MoveWindow(hSend,W_-bw-6,T0+H_-sb-ih-6,bw,30,TRUE);
    MoveWindow(hFile,W_-bw-6,T0+H_-sb-ih+28,bw,30,TRUE);
    MoveWindow(hStatus,8,T0+H_-sb,W_-16,sb,TRUE); }
static void open_menu(int i){ RECT r; GetWindowRect(hMenuBtn[i],&r); SendMessageW(hMenuBtn[i],BM_SETSTATE,TRUE,0);
    TrackPopupMenu(GetSubMenu(hMenuTree,i),TPM_LEFTALIGN|TPM_TOPALIGN|TPM_LEFTBUTTON,r.left,r.bottom,0,hMain,NULL); SendMessageW(hMenuBtn[i],BM_SETSTATE,FALSE,0); }

static HMENU build_menu(void){
    HMENU m=CreateMenu(), f=CreatePopupMenu(), c=CreatePopupMenu(), g=CreatePopupMenu(), s=CreatePopupMenu(), h=CreatePopupMenu();
    AM(f,MF_STRING,IDM_DLDIR,L"打开接收文件夹(&D)"); AM(f,MF_STRING,IDM_FIREWALL,L"设置防火墙例外(&F)..."); AM(f,MF_SEPARATOR,0,NULL); AM(f,MF_STRING,IDM_EXIT,L"退出(&X)");
    AM(c,MF_STRING,IDM_ADDIP,L"按 IP 添加(&A)..."); AM(c,MF_SEPARATOR,0,NULL); AM(c,MF_STRING,IDM_TRUST,L"信任(&T)"); AM(c,MF_STRING,IDM_UNTRUST,L"取消信任(&U)"); AM(c,MF_STRING,IDM_BLOCK,L"屏蔽(&B)");
    AM(c,MF_SEPARATOR,0,NULL); AM(c,MF_STRING,IDM_PINOFFER,L"生成 PIN（让对方输入）(&P)"); AM(c,MF_STRING,IDM_PINPAIR,L"输入 PIN 配对(&I)...");
    AM(g,MF_STRING,IDM_GCREATE,L"创建群组(&C)..."); AM(g,MF_STRING,IDM_GINVITE,L"邀请成员(&I)..."); AM(g,MF_STRING,IDM_GREMOVE,L"移除成员(&R)..."); AM(g,MF_STRING,IDM_GLEAVE,L"退出群组(&L)");
    AM(s,MF_STRING,IDM_NAME,L"修改昵称(&N)..."); AM(s,MF_SEPARATOR,0,NULL); AM(s,MF_STRING|(theme_mode==0?MF_CHECKED:0),IDM_THEME_SYS,L"外观：跟随系统(&Y)"); AM(s,MF_STRING|(theme_mode==1?MF_CHECKED:0),IDM_THEME_LIGHT,L"外观：浅色(&L)"); AM(s,MF_STRING|(theme_mode==2?MF_CHECKED:0),IDM_THEME_DARK,L"外观：深色(&K)");
    AM(h,MF_STRING,IDM_ABOUT,L"关于(&A)");
    AM(m,MF_POPUP,(UINT_PTR)f,L"文件(&F)"); AM(m,MF_POPUP,(UINT_PTR)c,L"联系人(&C)"); AM(m,MF_POPUP,(UINT_PTR)g,L"群组(&G)"); AM(m,MF_POPUP,(UINT_PTR)s,L"设置(&S)"); AM(m,MF_POPUP,(UINT_PTR)h,L"帮助(&H)");
    menu_bg(m);
    return m; }

static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l){
    switch(m){
    case WM_CREATE: {
        DWORD es=0; /* flat: 1px divider frame painted in WM_PAINT */
        { static const wchar_t *mt[5]={L"文件(&F)",L"联系人(&C)",L"群组(&G)",L"设置(&S)",L"帮助(&H)"}; int i;
          for(i=0;i<5;i++){ hMenuBtn[i]=CreateWindowW(L"BUTTON",mt[i],WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,0,0,0,0,h,(HMENU)(INT_PTR)(IDC_MENUBTN+i),hInst,NULL); SendMessageW(hMenuBtn[i],WM_SETFONT,(WPARAM)fBody,1); } }
        hList=CreateWindowExW(es,L"LISTBOX",NULL,WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_TABSTOP|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS,0,0,0,0,h,(HMENU)IDC_LIST,hInst,NULL);
        hTitle=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_LEFTNOWORDWRAP,0,0,0,0,h,(HMENU)IDC_TITLE,hInst,NULL);
        hChat=CreateWindowExW(es,L"LISTBOX",NULL,WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_TABSTOP|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWVARIABLE,0,0,0,0,h,(HMENU)IDC_CHAT,hInst,NULL); /* owner-drawn bubbles */
        hInput=CreateWindowExW(es,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,0,0,0,0,h,(HMENU)IDC_INPUT,hInst,NULL);
        hSend=CreateWindowW(L"BUTTON",L"发送 (Enter)",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,h,(HMENU)IDC_SEND,hInst,NULL);
        hFile=CreateWindowW(L"BUTTON",L"发送文件...",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,h,(HMENU)IDC_FILE,hInst,NULL);
        hStatus=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_LEFTNOWORDWRAP|SS_CENTERIMAGE,0,0,0,0,h,(HMENU)IDC_STATUS,hInst,NULL); /* themed static, not the system-coloured status bar */
        SendMessageW(hList,WM_SETFONT,(WPARAM)hFont,1); SendMessageW(hChat,WM_SETFONT,(WPARAM)hFont,1); SendMessageW(hInput,WM_SETFONT,(WPARAM)hFont,1);
        SendMessageW(hSend,WM_SETFONT,(WPARAM)hFont,1); SendMessageW(hFile,WM_SETFONT,(WPARAM)hFont,1); SendMessageW(hStatus,WM_SETFONT,(WPARAM)fBody,1); /* body: 12px caption CJK at wine/XP grayscale AA measured below 4.5:1 */ SendMessageW(hTitle,WM_SETFONT,(WPARAM)fBodyStrong,1);
        SendMessageW(hInput,EM_LIMITTEXT,60000,0);
        oldInputProc=(WNDPROC)SetWindowLongPtrW(hInput,GWLP_WNDPROC,(LONG_PTR)InputProc); oldChatProc=(WNDPROC)SetWindowLongPtrW(hChat,GWLP_WNDPROC,(LONG_PTR)ChatProc); ShowScrollBar(hInput,SB_VERT,FALSE);
        SetTimer(h,TIMER_POLL,300,NULL);
        return 0; }
    case WM_SIZE: if(w!=SIZE_MINIMIZED){ layout(LOWORD(l),HIWORD(l)); if(C) refresh_chat(); fit_vscroll(hInput); InvalidateRect(h,NULL,TRUE); } else ShowWindow(h,SW_HIDE); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO*)l)->ptMinTrackSize.x=640; ((MINMAXINFO*)l)->ptMinTrackSize.y=420; return 0;
    case WM_SETFOCUS: SetFocus(hList); return 0; /* keyboard users land in the contact list */
    case WM_TIMER: if(!tray_added) tray_add(); poll_events(); return 0;
    /* system colours only: readable in any theme incl. high-contrast */
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: case WM_CTLCOLORDLG: return ctl_color((HDC)w,(HWND)l,0);
    case WM_CTLCOLOREDIT: return ctl_color((HDC)w,(HWND)l,1);
    case WM_CTLCOLORLISTBOX: return ctl_color((HDC)w,(HWND)l,2);
    case WM_ERASEBKGND: { RECT r; GetClientRect(h,&r); FillRect((HDC)w,&r,brWin); return 1; }
    case WM_MEASUREITEM: { MEASUREITEMSTRUCT *mi=(MEASUREITEMSTRUCT*)l; if(mi->CtlType==ODT_MENU){ measure_menu(mi); return TRUE; } if(mi->CtlType==ODT_LISTBOX && mi->CtlID==IDC_CHAT){ measure_chat(mi); return TRUE; } if(mi->CtlType==ODT_LISTBOX){ mi->itemHeight=scale_px(32); return TRUE; } break; }
    case WM_DRAWITEM: { DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT*)l; if(d->CtlType==ODT_MENU){ draw_menu(d); return TRUE; } if(d->CtlType==ODT_BUTTON && d->CtlID>=IDC_MENUBTN && d->CtlID<IDC_MENUBTN+5){ draw_menubtn(d); return TRUE; } if(d->CtlType==ODT_LISTBOX && d->CtlID==IDC_CHAT){ draw_chat_item(d); return TRUE; } if(d->CtlType==ODT_LISTBOX){ draw_list_item(d); return TRUE; } if(d->CtlType==ODT_BUTTON){ draw_button(d); return TRUE; } break; }
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps); frame_child(h,dc,hList); frame_child(h,dc,hChat); frame_child(h,dc,hInput); { RECT cr; HBRUSH dv=CreateSolidBrush(T.divider); GetClientRect(h,&cr); cr.top=MENUBAR_H-1; cr.bottom=MENUBAR_H; FillRect(dc,&cr,dv); DeleteObject(dv); } EndPaint(h,&ps); return 0; }
    case WM_MENUCHAR: { HMENU mm=(HMENU)l; int i,n=GetMenuItemCount(mm); wchar_t ch=(wchar_t)towlower((wint_t)LOWORD(w)); /* mnemonics for owner-drawn items */
        for(i=0;i<n;i++){ MENUITEMINFOW ii; const wchar_t *t,*a; memset(&ii,0,sizeof ii); ii.cbSize=sizeof ii; ii.fMask=MIIM_DATA; if(!GetMenuItemInfoW(mm,i,TRUE,&ii)) continue; t=(const wchar_t*)ii.dwItemData; if(!t) continue; a=wcschr(t,L'&'); if(a && towlower(a[1])==ch) return MAKELRESULT(i,MNC_EXECUTE); }
        return MAKELRESULT(0,MNC_IGNORE); }
    case WM_COMMAND:
        if(LOWORD(w)==IDC_LIST && HIWORD(w)==LBN_SELCHANGE){ int s=(int)SendMessageW(hList,LB_GETCURSEL,0,0); if(s>=0 && s<nlist){ strcpy(cur_conv,list_ids[s]); refresh_chat(); if(lsm_mark_read(C,cur_conv)) refresh_list(); SetFocus(hInput); } return 0; }
        if(LOWORD(w)==IDC_INPUT && HIWORD(w)==EN_CHANGE){ fit_vscroll(hInput); return 0; }
        if(LOWORD(w)>=IDC_MENUBTN && LOWORD(w)<IDC_MENUBTN+5){ open_menu(LOWORD(w)-IDC_MENUBTN); return 0; }
        if(LOWORD(w)==IDC_SEND){ send_current(); return 0; }
        if(LOWORD(w)==IDC_FILE){ send_file_dlg(); return 0; }
        do_command(LOWORD(w)); return 0;
    case WM_TRAY:
        if(l==WM_LBUTTONDBLCLK||l==NIN_BALLOONUSERCLICK) do_command(IDM_SHOW);
        else if(l==WM_RBUTTONUP){ POINT pt; HMENU pm=CreatePopupMenu(); AM(pm,MF_STRING,IDM_SHOW,L"显示主窗口"); AM(pm,MF_SEPARATOR,0,NULL); AM(pm,MF_STRING,IDM_EXIT,L"退出"); menu_bg(pm); GetCursorPos(&pt); SetForegroundWindow(h); TrackPopupMenu(pm,TPM_RIGHTBUTTON,pt.x,pt.y,0,h,NULL); DestroyMenu(pm); }
        return 0;
    case WM_CLOSE: ShowWindow(h,SW_HIDE); tray_balloon("局域网安全通讯","程序仍在后台运行，双击托盘图标可打开。"); return 0;
    case WM_DESTROY: KillTimer(h,TIMER_POLL); Shell_NotifyIconW(NIM_DELETE,&nid); PostQuitMessage(0); return 0;
    case WM_SYSCOLORCHANGE: theme_apply(); InvalidateRect(h,NULL,TRUE); return 0;
    }
    if(WM_TASKBARCREATED && m==WM_TASKBARCREATED){ tray_add(); return 0; }
    return DefWindowProcW(h,m,w,l); }

static int CALLBACK font_found(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD t, LPARAM l){ (void)lf; (void)tm; (void)t; (void)l; return 0; /* stop: exists */ }

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE prev, LPWSTR cmd, int show){
    int win_w=900, win_h=600, win_x=CW_USEDEFAULT, win_y=CW_USEDEFAULT; WNDCLASSEXW wc; MSG msg; lsm_opts o; char data[MAX_PATH*4], err[256]; INITCOMMONCONTROLSEX ic; wchar_t *sl, dw[MAX_PATH]; LOGFONTW lf; NONCLIENTMETRICSW ncm; HANDLE mtx;
    (void)prev; hInst=hi;
    mtx=CreateMutexW(NULL,TRUE,L"LanSecureMessengerXP_Single"); if(mtx && GetLastError()==ERROR_ALREADY_EXISTS && !wcsstr(cmd,L"--data")){ HWND o2=FindWindowW(L"LsmMain",NULL); if(o2){ ShowWindow(o2,SW_SHOW); ShowWindow(o2,SW_RESTORE); SetForegroundWindow(o2);} return 0; }
    ic.dwSize=sizeof ic; ic.dwICC=ICC_BAR_CLASSES|ICC_STANDARD_CLASSES; InitCommonControlsEx(&ic);
    GetModuleFileNameW(NULL,exe_dir,MAX_PATH); sl=wcsrchr(exe_dir,L'\\'); if(sl) *sl=0;
    _snwprintf(dw,MAX_PATH,L"%s\\data",exe_dir); dw[MAX_PATH-1]=0;
    { int argc; wchar_t **av=CommandLineToArgvW(GetCommandLineW(),&argc); int i; memset(&o,0,sizeof o);
      static char nm[300], tg[1024];
      for(i=1;i<argc;i++){ if(!wcscmp(av[i],L"--data")&&i+1<argc) wcsncpy(dw,av[++i],MAX_PATH-1); else if(!wcscmp(av[i],L"--name")&&i+1<argc){ U(av[++i],nm,sizeof nm); o.name=nm; } else if(!wcscmp(av[i],L"--theme")&&i+1<argc){ i++; theme_cli=!wcscmp(av[i],L"dark")?2:!wcscmp(av[i],L"light")?1:0; } else if(!wcscmp(av[i],L"--auto-trust")) o.auto_trust=1; else if(!wcscmp(av[i],L"--port")&&i+1<argc) o.listen_port=_wtoi(av[++i]); else if(!wcscmp(av[i],L"--disco-port")&&i+1<argc) o.discovery_port=_wtoi(av[++i]); else if(!wcscmp(av[i],L"--targets")&&i+1<argc){ U(av[++i],tg,sizeof tg); o.announce_targets=tg; } } }
    U(dw,data,sizeof data); o.data_dir=data;
    if(!o.name){ static char cn[64]; wchar_t wn[64]; DWORD n=64; if(GetComputerNameW(wn,&n)){ U(wn,cn,sizeof cn); o.name=cn; } }
    (void)lf; (void)ncm;
    /* UI face: Microsoft YaHei UI (Windows Vista+ zh UI font), SimSun on XP; WenQuanYi/Noto only on wine/ReactOS test boxes.
     * Sizes: Fluent type ramp caption 12 / body 14 / body_strong 14 semibold / subtitle 20 / title 28 px. */
    { static const wchar_t *faces[]={L"Microsoft YaHei UI",L"Microsoft YaHei",L"SimSun",L"\x5b8b\x4f53",L"NSimSun",L"Noto Sans CJK SC",L"WenQuanYi Zen Hei",L"Tahoma",NULL}; const wchar_t *face=L"Tahoma"; int i; HDC dc=GetDC(NULL);
      for(i=0;faces[i];i++){ LOGFONTW q; memset(&q,0,sizeof q); q.lfCharSet=DEFAULT_CHARSET; wcsncpy(q.lfFaceName,faces[i],31);
        if(!EnumFontFamiliesExW(dc,&q,(FONTENUMPROCW)font_found,0,0)){ face=faces[i]; break; } }
      ReleaseDC(NULL,dc);
      fCaption=mkfont(face,12,FW_NORMAL); fBody=mkfont(face,14,FW_NORMAL); fBodyStrong=mkfont(face,14,FW_SEMIBOLD); fSubtitle=mkfont(face,20,FW_SEMIBOLD); fTitle=mkfont(face,28,FW_SEMIBOLD);
      hFont=fBody; hFontBold=fBodyStrong; }
    theme_load(); if(theme_cli>=0) theme_mode=theme_cli; theme_apply();
    C=lsm_start(&o,err,sizeof err);
    if(!C){ MessageBoxW(NULL,W(err),L"启动失败",MB_ICONSTOP); return 1; }
    memset(&wc,0,sizeof wc); wc.cbSize=sizeof wc; wc.lpfnWndProc=MainProc; wc.hInstance=hi; wc.hIcon=LoadIconW(hi,MAKEINTRESOURCEW(1)); wc.hIconSm=wc.hIcon; wc.hCursor=LoadCursor(NULL,IDC_ARROW); wc.hbrBackground=NULL; /* WM_ERASEBKGND paints window_bg */ wc.lpszClassName=L"LsmMain";
    RegisterClassExW(&wc);
    wc.lpfnWndProc=PromptProc; wc.lpszClassName=L"LsmPrompt"; wc.style|=0x00020000 /* CS_DROPSHADOW (XP+): lifts the borderless dialog off a same-colour page */; wc.hIcon=NULL; wc.hIconSm=NULL; RegisterClassExW(&wc);
    { RECT wa; if(SystemParametersInfoW(SPI_GETWORKAREA,0,&wa,0)){ if(win_w>wa.right-wa.left) win_w=wa.right-wa.left; if(win_h>wa.bottom-wa.top) win_h=wa.bottom-wa.top;
        win_x=wa.left+((wa.right-wa.left)-win_w)/2; win_y=wa.top+((wa.bottom-wa.top)-win_h)/2; } } /* 800x600 XP screens: fit and centre (CW_USEDEFAULT pushed it off-screen) */
    hMenuTree=build_menu(); /* not attached: the system menu bar ignores the app theme (wine/XP paint it with COLOR_MENU) */
    hMain=CreateWindowExW(0,L"LsmMain",L"局域网安全通讯 XP 版",WS_OVERLAPPEDWINDOW,win_x,win_y,win_w,win_h,NULL,NULL,hi,NULL);
    memset(&nid,0,sizeof nid); nid.cbSize=NOTIFYICONDATAW_V2_SIZE; nid.hWnd=hMain; nid.uID=1; nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP; nid.uCallbackMessage=WM_TRAY; nid.hIcon=wc.hIcon?wc.hIcon:LoadIcon(NULL,IDI_APPLICATION);
    { HICON ic2=LoadIconW(hi,MAKEINTRESOURCEW(1)); if(ic2) nid.hIcon=ic2; }
    wcscpy(nid.szTip,L"局域网安全通讯 XP 版"); WM_TASKBARCREATED=RegisterWindowMessageW(L"TaskbarCreated"); tray_add();
    ShowWindow(hMain,show); UpdateWindow(hMain);
    refresh_list(); refresh_chat();
    while(GetMessageW(&msg,NULL,0,0)>0){ if(IsDialogMessageW(hMain,&msg)) continue; /* Tab / Shift+Tab / Alt+mnemonics between controls */ TranslateMessage(&msg); DispatchMessageW(&msg); }
    lsm_stop(C);
    return 0; }
