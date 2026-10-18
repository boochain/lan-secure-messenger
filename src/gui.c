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
#include <shlobj.h>   /* DROPFILES (test drop hook) */
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
static HWND hMain, hList, hChat, hInput, hSend, hFile, hStatus, hTitle, hAdd, hHdrSearch, hTip;
#define IDC_ADD 109
#define IDC_HDRSEARCH 110
/* header + status-bar state (owner-drawn, xp-*-chat mockups) */
static wchar_t hdr_name[160], hdr_ini[8], hdr_sub[160]; static int hdr_av, hdr_online, hdr_trust, hdr_group, hdr_on, hdr_via;
static wchar_t st_conn[96], st_me[200], st_fp[96], st_tcp[32];
static char ui_icons_path[MAX_PATH*4];
static HFONT hFont, hFontBold;
static HINSTANCE hInst;
static NOTIFYICONDATAW nid;
static char cur_conv[64];
static char list_ids[512][64]; static int nlist;
static wchar_t exe_dir[MAX_PATH];

static wchar_t *W(const char *s){ static wchar_t buf[8][8192]; static int k; wchar_t *b=buf[k++&7]; MultiByteToWideChar(CP_UTF8,0,s?s:"",-1,b,8192); return b; }
static char *U(const wchar_t *s, char *out, int n){ WideCharToMultiByte(CP_UTF8,0,s,-1,out,n,NULL,NULL); return out; }
static int tmsg(const wchar_t *title, const wchar_t *text, int cancel);
static UINT WM_TASKBARCREATED; static int tray_added;
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
static const xpal_t PAL_LIGHT={HX(0xC1D2EE),HX(0x316AC5),HX(0xEEF3FB),HX(0x0B4AA8),HX(0xDCE9FA),HX(0x7DA2CE),HX(0x000000),HX(0x4A5568),HX(0xF4F3EE),HX(0xD6D3C4),HX(0x000000),HX(0x55534A),HX(0xD6D3C4),HX(0x316AC5),HX(0xFFFFFF),HX(0xFFFFFF),HX(0x7F9DB9),HX(0xECE9D8),HX(0x3B3A33),HX(0xF1EFE2),HX(0x3B3A33),HX(0x0B4AA8),HX(0xFFFFFF),HX(0x316AC5),HX(0xF6F5EF),
    {HX(0x6D4FC2),HX(0x1F6FB2),HX(0x0F7A6C),HX(0xB4471B),HX(0x2E6B2F),HX(0x9C2F55),HX(0x5A5F14),HX(0x8A4B08)}};
static const xpal_t PAL_DARK={HX(0x1F3A5F),HX(0x3C78C3),HX(0x2C2F35),HX(0x8EC1FF),HX(0x1F3A5A),HX(0x2F5A8A),HX(0xFFFFFF),HX(0xC9D9EC),HX(0x2E2E2E),HX(0x444444),HX(0xFFFFFF),HX(0xC5C5C5),HX(0x3C3C3C),HX(0x2F6AB8),HX(0xFFFFFF),HX(0x2B2B2B),HX(0x5A7AA0),HX(0x333333),HX(0xE0E0E0),HX(0x2B2B2B),HX(0xD6D6D6),HX(0x8EC1FF),HX(0x2B2B2B),HX(0x8EC1FF),HX(0x262626),
    {HX(0x6D4FC2),HX(0x1F6FB2),HX(0x0F7A6C),HX(0xB4471B),HX(0x2E6B2F),HX(0x9C2F55),HX(0x5A5F14),HX(0x8A4B08)}};
static xpal_t P;
static theme_t T; static int theme_mode, theme_cli=-1; /* 0 system,1 light,2 dark */
static HBRUSH brWin, brSide, brChat, brInput, brCard;
static HMENU hMenuTree; static HWND hMenuBtn[5];
#define IDC_MENUBTN 120
#define MENUBAR_H 30
static HFONT fCaptionStrong, fCaption, fBody, fBodyStrong, fSubtitle, fTitle;
static void relay_dialog_refresh(void);
#define TITLE_H 32
#define IDM_THEME_SYS 230
#define IDM_THEME_LIGHT 231
#define IDM_THEME_DARK 232
#define IDM_RELAY 233
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
/* edit fields: 1px themed border (Luna field blue), accent when focused */
static void frame_field(HWND parent, HDC dc, HWND c){ RECT r; HBRUSH b; if(!c||!IsWindowVisible(c)) return; GetWindowRect(c,&r); MapWindowPoints(NULL,parent,(POINT*)&r,2); InflateRect(&r,1,1); b=CreateSolidBrush(GetFocus()==c?T.accent:P.fieldborder); FrameRect(dc,&r,b); DeleteObject(b); }
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
/* conversation list (xp-*-chat mockups): avatar tile with initials + online dot, name, time, last-message preview, unread badge */
typedef struct { char id[64]; wchar_t name[160], preview[200], time[16]; wchar_t ini[8]; int online, unread, group, trusted, av, via; } conv_t;
static conv_t convs[512];
static int line_h(HDC dc, HFONT f);
static HWND hSearch;
#define IDC_SEARCH 108
static int avatar_index(const char *id){ unsigned h=2166136261u; const unsigned char *p=(const unsigned char*)(id?id:""); while(*p){ h^=*p++; h*=16777619u; } return (int)(h%8); }
static void when(int64_t ms, wchar_t *o, int n){ time_t t=(time_t)(ms/1000), now=time(NULL); struct tm a, b2; o[0]=0; if(!ms) return; a=*localtime(&t); b2=*localtime(&now);
    if(a.tm_year==b2.tm_year && a.tm_yday==b2.tm_yday) wcsftime(o,n,L"%H:%M",&a); else if(a.tm_year==b2.tm_year && a.tm_yday==b2.tm_yday-1) wcsncpy(o,L"昨天",n); else wcsftime(o,n,L"%m/%d",&a); }
static int conv_match(const wchar_t *name, const wchar_t *q){ wchar_t a[160], b2[64]; int i; if(!q[0]) return 1; for(i=0;name[i]&&i<159;i++) a[i]=towlower(name[i]); a[i]=0; for(i=0;q[i]&&i<63;i++) b2[i]=towlower(q[i]); b2[i]=0; return wcsstr(a,b2)!=NULL; }
static void conv_last(lsm_msg *m, int nm, const char *conv, conv_t *c, int64_t *ts){ int j; *ts=0; c->unread=0; c->preview[0]=0;
    for(j=0;j<nm;j++){ if(strcmp(m[j].conv,conv)) continue; if(m[j].unread) c->unread++; *ts=m[j].ts_ms;
        if(m[j].kind==K_FILE) _snwprintf(c->preview,200,L"[文件] %s",W(m[j].file_name)); else if(c->group && !m[j].out && m[j].kind==K_TEXT) _snwprintf(c->preview,200,L"%s: %s",W(m[j].sender_name),W(m[j].body)); else wcsncpy(c->preview,W(m[j].body),199);
        c->preview[199]=0; { wchar_t *q; for(q=c->preview;*q;q++) if(*q==L'\r'||*q==L'\n') *q=L' '; } }
    when(*ts,c->time,16); }
static void refresh_list(void){
    lsm_contact *k; lsm_group *g; lsm_msg *m; int n,i, sel=-1, nm; wchar_t q[64]=L""; char keep[64]; strcpy(keep,cur_conv);
    if(hSearch) GetWindowTextW(hSearch,q,64);
    SendMessageW(hList,WM_SETREDRAW,FALSE,0); SendMessageW(hList,LB_RESETCONTENT,0,0); nlist=0;
    lsm_lock(C); nm=lsm_messages(C,&m);
    n=lsm_contacts(C,&k);
    for(i=0;i<n && nlist<512;i++){ conv_t *c=&convs[nlist]; int64_t ts; char ini[8]; memset(c,0,sizeof *c); snprintf(c->id,64,"dm:%s",k[i].fp); wcsncpy(c->name,W(k[i].name),159);
        if(!conv_match(c->name,q)) continue; c->online=k[i].online; c->via=k[i].online && k[i].via_relay; c->trusted=k[i].trust==1; lsm_initials(k[i].name,ini); wcsncpy(c->ini,W(ini),7); c->av=avatar_index(k[i].fp); conv_last(m,nm,c->id,c,&ts);
        SendMessageW(hList,LB_ADDSTRING,0,(LPARAM)c->name); strcpy(list_ids[nlist],c->id); if(!strcmp(c->id,keep)) sel=nlist; nlist++; }
    n=lsm_groups(C,&g);
    for(i=0;i<n && nlist<512;i++){ conv_t *c=&convs[nlist]; int64_t ts; char u[37], ini[8]; memset(c,0,sizeof *c); lsm_uuid_str(g[i].id,u); snprintf(c->id,64,"group:%s",u); wcsncpy(c->name,W(g[i].name),159);
        if(!conv_match(c->name,q)) continue; c->group=1; lsm_initials(g[i].name,ini); wcsncpy(c->ini,W(ini),7); c->av=avatar_index(u); conv_last(m,nm,c->id,c,&ts); if(!c->preview[0]) _snwprintf(c->preview,200,L"%d 位成员",g[i].nmembers);
        SendMessageW(hList,LB_ADDSTRING,0,(LPARAM)c->name); strcpy(list_ids[nlist],c->id); if(!strcmp(c->id,keep)) sel=nlist; nlist++; }
    { int on=0; n=lsm_contacts(C,&k); for(i=0;i<n;i++) on+=k[i].online; _snwprintf(st_conn,96,L"已连接 · 局域网 · %d 人在线",on); _snwprintf(st_me,200,L"我：%s",W(lsm_my_name(C))); _snwprintf(st_fp,96,L"指纹 %s",W(lsm_my_fp(C))); _snwprintf(st_tcp,32,L"TCP %d",lsm_listen_port(C)); InvalidateRect(hStatus,NULL,FALSE); }
    lsm_unlock(C);
    if(sel>=0) SendMessageW(hList,LB_SETCURSEL,sel,0);
    SendMessageW(hList,WM_SETREDRAW,TRUE,0); InvalidateRect(hList,NULL,TRUE); }
static void draw_cloud(HDC dc, int x, int y, COLORREF col){ /* 12x8 cloud glyph (WinUI/Android relay icon); x = left, y = vertical centre */ HBRUSH cb=CreateSolidBrush(col); HPEN cp=CreatePen(PS_SOLID,1,col); HGDIOBJ ob=SelectObject(dc,cb), op=SelectObject(dc,cp); int u=scale_px(1);
    Ellipse(dc,x,y-u,x+5*u,y+4*u); Ellipse(dc,x+2*u,y-4*u,x+9*u,y+3*u); Ellipse(dc,x+6*u,y-2*u,x+12*u,y+4*u); Rectangle(dc,x+2*u,y+u,x+10*u,y+4*u); SelectObject(dc,ob); SelectObject(dc,op); DeleteObject(cb); DeleteObject(cp); }
static void fill_round(HDC dc, RECT r, int rad, COLORREF fill, COLORREF edge){ HBRUSH br=CreateSolidBrush(fill); HPEN pn=CreatePen(PS_SOLID,1,edge); HGDIOBJ ob=SelectObject(dc,br), op=SelectObject(dc,pn); RoundRect(dc,r.left,r.top,r.right,r.bottom,rad,rad); SelectObject(dc,ob); SelectObject(dc,op); DeleteObject(br); DeleteObject(pn); }
static void draw_conv_item(DRAWITEMSTRUCT *d){ RECT it=d->rcItem, r, av, tr; conv_t *c; HDC dc=d->hDC; int sel=(d->itemState&ODS_SELECTED)!=0, x, lh;
    FillRect(dc,&it,brSide); if(d->itemID>=(UINT)nlist) return; c=&convs[d->itemID]; SetBkMode(dc,TRANSPARENT);
    if(sel){ r=it; InflateRect(&r,-scale_px(3),-1); fill_round(dc,r,scale_px(6),P.rowsel,P.rowselborder); }
    av.left=it.left+scale_px(10); av.top=(it.top+it.bottom)/2-scale_px(18); av.right=av.left+scale_px(36); av.bottom=av.top+scale_px(36);
    fill_round(dc,av,scale_px(8),P.av[c->av],P.av[c->av]); SelectObject(dc,fBodyStrong); SetTextColor(dc,RGB(255,255,255)); DrawTextW(dc,c->ini,-1,&av,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    if(c->online){ RECT o={av.right-scale_px(9),av.bottom-scale_px(9),av.right+scale_px(2),av.bottom+scale_px(2)}; HBRUSH br=CreateSolidBrush(T.online); HPEN pn=CreatePen(PS_SOLID,2,sel?P.rowsel:T.sidebar_bg); HGDIOBJ ob=SelectObject(dc,br), op=SelectObject(dc,pn); Ellipse(dc,o.left,o.top,o.right,o.bottom); SelectObject(dc,ob); SelectObject(dc,op); DeleteObject(br); DeleteObject(pn); }
    x=av.right+scale_px(10); lh=line_h(dc,fBodyStrong);
    tr.left=x; tr.right=it.right-scale_px(10); tr.top=it.top+scale_px(8); tr.bottom=tr.top+lh;
    SelectObject(dc,fCaption); SetTextColor(dc,c->unread?P.accenttext:sel?T.text_primary:P.meta); DrawTextW(dc,c->time,-1,&tr,DT_RIGHT|DT_SINGLELINE); { SIZE s; GetTextExtentPoint32W(dc,c->time,(int)wcslen(c->time),&s); tr.right-=s.cx+scale_px(8); }
    SelectObject(dc,fBodyStrong); SetTextColor(dc,T.text_primary); DrawTextW(dc,c->name,-1,&tr,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
    tr.right=it.right-scale_px(10); tr.top=tr.bottom+scale_px(2); tr.bottom=tr.top+line_h(dc,fBody);
    if(c->unread){ wchar_t b[8]; SIZE s; RECT bd; _snwprintf(b,8,c->unread>99?L"99+":L"%d",c->unread); b[7]=0; SelectObject(dc,fCaption); GetTextExtentPoint32W(dc,b,(int)wcslen(b),&s);
        bd.right=tr.right; bd.left=bd.right-(s.cx+scale_px(12)>scale_px(20)?s.cx+scale_px(12):scale_px(20)); bd.top=tr.top+1; bd.bottom=bd.top+scale_px(18); fill_round(dc,bd,scale_px(18),P.badge,P.badge); SetTextColor(dc,P.badgefg); DrawTextW(dc,b,-1,&bd,DT_CENTER|DT_VCENTER|DT_SINGLELINE); tr.right=bd.left-scale_px(6); }
    if(c->via){ /* 经服务器 badge (tooltip 经服务器中转): second line, before the preview, so the name keeps the first line */ SIZE bz; RECT bd;
        SelectObject(dc,fBody); GetTextExtentPoint32W(dc,L"经服务器",4,&bz);
        bd.left=tr.left; bd.right=bd.left+bz.cx+scale_px(24); bd.top=tr.top; bd.bottom=tr.top+scale_px(19);
        fill_round(dc,bd,scale_px(6),P.chip,P.stroke); SelectObject(dc,fBody); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,P.chipfg); draw_cloud(dc,bd.left+scale_px(5),(bd.top+bd.bottom)/2,P.chipfg);
        { RECT tb=bd; tb.left+=scale_px(18); tb.right-=scale_px(4); DrawTextW(dc,L"经服务器",-1,&tb,DT_CENTER|DT_VCENTER|DT_SINGLELINE); } tr.left=bd.right+scale_px(6); }
    SelectObject(dc,fBody); SetTextColor(dc,sel?T.text_primary:T.text_secondary); DrawTextW(dc,c->preview,-1,&tr,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
    if((d->itemState&ODS_FOCUS) && GetFocus()==hList){ RECT f=it; InflateRect(&f,-scale_px(4),-2); SetTextColor(dc,T.text_primary); SetBkColor(dc,T.sidebar_bg); DrawFocusRect(dc,&f); } }

/* ---------------------------------------------------------------- chat view: owner-drawn bubble list (xp-*-chat mockups)
 * LISTBOX + LBS_OWNERDRAWVARIABLE: one item per row (day chip, service notice, text bubble, file card). Rounded bubbles
 * with the time (and ticks on outgoing) inside the bubble, file cards with a type tile, open button and folder link.
 * Plain GDI only (RoundRect/Polyline/DrawText): XP SP3 without GDI+. */
enum { R_DAY, R_NOTICE, R_TEXT, R_FILE, R_IMAGE };
/* image thumbnails: GDI+ flat API loaded at run time (gdiplus.dll ships with XP SP3; no import, so the exe still starts without it) */
typedef struct { UINT32 ver; void *cb; BOOL nothread, nocodecs; } gdip_in;
static int (WINAPI *pGdiplusStartup)(ULONG_PTR*, const gdip_in*, void*); static int (WINAPI *pGdipCreateBitmapFromFile)(const WCHAR*, void**);
static int (WINAPI *pGdipCreateHBITMAPFromBitmap)(void*, HBITMAP*, DWORD); static int (WINAPI *pGdipDisposeImage)(void*);
static struct { char path[520]; HBITMAP bm; int w, h; } thumbs[32]; static int nthumbs, gdip_state;
static int thumb_get(const char *path, HBITMAP *bm, int *w, int *h){ int i; void *img=NULL; HBITMAP b=NULL; BITMAP bi;
    for(i=0;i<nthumbs;i++) if(!strcmp(thumbs[i].path,path)){ *bm=thumbs[i].bm; *w=thumbs[i].w; *h=thumbs[i].h; return *bm!=NULL; }
    if(!gdip_state){ HMODULE g=LoadLibraryW(L"gdiplus.dll"); ULONG_PTR tok; gdip_in in={1,NULL,FALSE,FALSE}; gdip_state=-1;
        if(g){ pGdiplusStartup=(void*)GetProcAddress(g,"GdiplusStartup"); pGdipCreateBitmapFromFile=(void*)GetProcAddress(g,"GdipCreateBitmapFromFile"); pGdipCreateHBITMAPFromBitmap=(void*)GetProcAddress(g,"GdipCreateHBITMAPFromBitmap"); pGdipDisposeImage=(void*)GetProcAddress(g,"GdipDisposeImage");
            if(pGdiplusStartup && pGdipCreateBitmapFromFile && pGdipCreateHBITMAPFromBitmap && pGdipDisposeImage && pGdiplusStartup(&tok,&in,NULL)==0) gdip_state=1; } }
    if(gdip_state==1 && pGdipCreateBitmapFromFile(W(path),&img)==0){ pGdipCreateHBITMAPFromBitmap(img,&b,0xFFFFFFFF); pGdipDisposeImage(img); }
    if(nthumbs<32){ i=nthumbs++; strncpy(thumbs[i].path,path,519); thumbs[i].bm=b; thumbs[i].w=thumbs[i].h=0; if(b && GetObject(b,sizeof bi,&bi)){ thumbs[i].w=bi.bmWidth; thumbs[i].h=bi.bmHeight; } *bm=b; *w=thumbs[i].w; *h=thumbs[i].h; }
    return b!=NULL && *w>0; }
static void thumb_fit(int w, int h, int maxw, int *ow, int *oh){ int mw=maxw<scale_px(300)?maxw:scale_px(300), mh=scale_px(200); double k=1.0; if(w>mw) k=(double)mw/w; if(h*k>mh) k=(double)mh/h; *ow=(int)(w*k); *oh=(int)(h*k); if(*ow<scale_px(60)) *ow=scale_px(60); if(*oh<scale_px(40)) *oh=scale_px(40); }
typedef struct { int kind, out, status, tstate, pct, h, bw; char id[40]; char path[520]; wchar_t *text; wchar_t time[8]; wchar_t name[260]; wchar_t meta[96]; wchar_t ext[8]; int emph, prog; } row_t;
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
static int time_w(HDC dc, row_t *r){ SIZE s; HGDIOBJ o=SelectObject(dc,fCaption); GetTextExtentPoint32W(dc,r->time,(int)wcslen(r->time),&s); SelectObject(dc,o); return s.cx+(r->out?scale_px(24):0); }
/* text bubble geometry: returns bubble width, fills height; time goes on the last line when it fits, else on its own line */
static void text_geom(HDC dc, row_t *r, int maxw, int *bw, int *bh, int *tw_inline){ RECT tr={0,0,maxw-2*PADX,0}; HGDIOBJ o=SelectObject(dc,fBody); int lh=line_h(dc,fBody), tw=time_w(dc,r), gap=scale_px(10);
    DrawTextW(dc,r->text,-1,&tr,DT_CALCRECT|DT_WORDBREAK|DT_EDITCONTROL|DT_NOPREFIX); SelectObject(dc,o);
    if(tr.bottom<=lh+1 && tr.right+gap+tw<=maxw-2*PADX){ *bw=tr.right+gap+tw+2*PADX; *bh=lh+2*PADY; *tw_inline=1; }
    else { *bw=(tr.right>tw?tr.right:tw)+2*PADX; *bh=tr.bottom+line_h(dc,fCaption)+2*PADY; *tw_inline=0; } }
static void measure_chat(MEASUREITEMSTRUCT *mi){ row_t *r; HDC dc; int bw,bh,inl; if(mi->itemID>=(UINT)nrows){ mi->itemHeight=1; return; } r=&rows[mi->itemID]; dc=GetDC(hChat);
    if(r->kind==R_DAY||r->kind==R_NOTICE) mi->itemHeight=line_h(dc,fBody)+scale_px(r->kind==R_DAY?18:12); /* 14px: 12px CJK at XP/wine grayscale AA measured < 4.5:1 */
    else if(r->kind==R_TEXT){ text_geom(dc,r,chat_maxw(),&bw,&bh,&inl); r->bw=bw; mi->itemHeight=bh+scale_px(6); }
    else if(r->kind==R_IMAGE){ HBITMAP bm; int w,h,ow,oh; if(thumb_get(r->path,&bm,&w,&h)){ thumb_fit(w,h,chat_maxw(),&ow,&oh); r->bw=ow+scale_px(8); mi->itemHeight=oh+scale_px(8)+scale_px(6); } else { r->bw=scale_px(160); mi->itemHeight=scale_px(100); } }
    else { SIZE mz; HGDIOBJ of=SelectObject(dc,fBody); GetTextExtentPoint32W(dc,r->meta,(int)wcslen(r->meta),&mz); SelectObject(dc,of); r->bw=scale_px(272); if(mz.cx+scale_px(124)>r->bw) r->bw=mz.cx+scale_px(124); if(r->bw>chat_maxw()) r->bw=chat_maxw(); mi->itemHeight=scale_px(r->path[0]?86:66)+scale_px(6); }
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
    if(r->kind==R_DAY||r->kind==R_NOTICE){ SIZE s; HGDIOBJ o=SelectObject(dc,fBody); int w; GetTextExtentPoint32W(dc,r->text,(int)wcslen(r->text),&s); w=s.cx+scale_px(r->kind==R_DAY?24:20);
        b.left=(it.left+it.right-w)/2; b.right=b.left+w; b.top=it.top+scale_px(r->kind==R_DAY?7:3); b.bottom=it.bottom-scale_px(r->kind==R_DAY?7:5);
        bubble(dc,b,r->kind==R_DAY?P.chip:P.service,P.stroke); SetTextColor(dc,r->kind==R_DAY?P.chipfg:P.servicefg); DrawTextW(dc,r->text,-1,&b,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX); SelectObject(dc,o); return; }
    b=row_bubble(r,it); bubble(dc,b,r->out?P.mine:P.theirs,r->out?P.mineborder:P.theirsborder);
    if(r->kind==R_IMAGE){ HBITMAP bm; int w,h; RECT ir=b, tc; InflateRect(&ir,-scale_px(4),-scale_px(4));
        if(thumb_get(r->path,&bm,&w,&h)){ HDC mdc=CreateCompatibleDC(dc); HGDIOBJ ob=SelectObject(mdc,bm); HRGN rg=CreateRoundRectRgn(ir.left,ir.top,ir.right+1,ir.bottom+1,scale_px(10),scale_px(10)); POINT bo;
            SelectClipRgn(dc,rg); GetBrushOrgEx(dc,&bo); SetStretchBltMode(dc,HALFTONE); SetBrushOrgEx(dc,bo.x,bo.y,NULL); StretchBlt(dc,ir.left,ir.top,ir.right-ir.left,ir.bottom-ir.top,mdc,0,0,w,h,SRCCOPY); SelectClipRgn(dc,NULL); DeleteObject(rg); SelectObject(mdc,ob); DeleteDC(mdc); }
        { SIZE sz; SelectObject(dc,fCaption); GetTextExtentPoint32W(dc,r->time,(int)wcslen(r->time),&sz); tc.right=ir.right-scale_px(6); tc.bottom=ir.bottom-scale_px(6); tc.left=tc.right-sz.cx-scale_px(12)-(r->out?scale_px(20):0); tc.top=tc.bottom-sz.cy-scale_px(4);
          fill_round(dc,tc,scale_px(10),RGB(0x20,0x20,0x20),RGB(0x20,0x20,0x20)); { RECT tt=tc; tt.left+=scale_px(6); SetTextColor(dc,RGB(255,255,255)); DrawTextW(dc,r->time,-1,&tt,DT_LEFT|DT_VCENTER|DT_SINGLELINE); }
          if(r->out){ COLORREF keep=P.minemeta; P.minemeta=RGB(255,255,255); ticks(dc,tc.right-scale_px(16),(tc.top+tc.bottom)/2,r->status==3?2:r->status); P.minemeta=keep; } } }
    else if(r->kind==R_TEXT){ int bw,bh,inl; RECT tr=b, tm; text_geom(dc,r,chat_maxw(),&bw,&bh,&inl); lh=line_h(dc,fBody);
        InflateRect(&tr,-PADX,-PADY); SelectObject(dc,fBody); SetTextColor(dc,r->out?P.minefg:P.theirsfg); DrawTextW(dc,r->text,-1,&tr,DT_WORDBREAK|DT_EDITCONTROL|DT_NOPREFIX);
        tm=tr; tm.top=tm.bottom-(inl?lh:line_h(dc,fCaption)); if(r->out) tm.right-=scale_px(24); SelectObject(dc,fCaption); SetTextColor(dc,r->out?P.minemeta:P.meta); DrawTextW(dc,r->time,-1,&tm,DT_RIGHT|DT_BOTTOM|DT_SINGLELINE);
        if(r->out) ticks(dc,tr.right-scale_px(16),(tm.top+tm.bottom)/2+1,r->status); }
    else { RECT tile={b.left+scale_px(10),b.top+scale_px(10),b.left+scale_px(46),b.top+scale_px(46)}, nr, br=file_btn_rect(b), tm; COLORREF fg=r->out?P.minefg:P.theirsfg, meta=r->out?P.minemeta:P.meta;
        bubble(dc,tile,P.tile,P.stroke); SelectObject(dc,fCaptionStrong); SetTextColor(dc,P.tilefg); /* bold: regular 12px Latin measured 4.50:1 / Lc 57 after grayscale AA */ DrawTextW(dc,r->ext,-1,&tile,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        nr.left=tile.right+scale_px(10); nr.right=br.left-scale_px(6); nr.top=b.top+scale_px(9); nr.bottom=nr.top+line_h(dc,fBodyStrong);
        SelectObject(dc,fBodyStrong); SetTextColor(dc,fg); DrawTextW(dc,r->name,-1,&nr,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
        nr.top=nr.bottom+scale_px(1); nr.bottom=nr.top+line_h(dc,fBody); SelectObject(dc,fBody); SetTextColor(dc,r->emph?(r->out?P.minefg:P.theirsfg):meta); /* paused/resumed state in the bubble text colour: meta grey on thin CJK strokes measured 3.5:1. 14px: 12px Latin at wine/XP grayscale AA measured < 4.5:1 */ DrawTextW(dc,r->meta,-1,&nr,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
        if(r->prog){ /* thin themed progress bar (in transit / paused / resumed), like WinUI/Android/GTK */ RECT pb, pf; HBRUSH tb=CreateSolidBrush(r->out?P.mineborder:P.stroke), fb=CreateSolidBrush(r->tstate==0?T.text_secondary:T.accent);
            pb.left=nr.left; pb.right=b.right-scale_px(60); pb.top=b.bottom-scale_px(10); /* stops short of the time */ pb.bottom=pb.top+scale_px(3); FillRect(dc,&pb,tb); pf=pb; pf.right=pf.left+(pb.right-pb.left)*(r->pct<0?0:r->pct>100?100:r->pct)/100; FillRect(dc,&pf,fb); DeleteObject(tb); DeleteObject(fb); }
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
        hdr_on=0;
        if(!strncmp(cur_conv,"dm:",3)){ nk=lsm_contacts(C,&k); for(j=0;j<nk;j++) if(!strcmp(k[j].fp,cur_conv+3)){ char ini[8]; wcsncpy(peer,W(k[j].name),299); hdr_on=1; hdr_group=0; wcsncpy(hdr_name,W(k[j].name),159); lsm_initials(k[j].name,ini); wcsncpy(hdr_ini,W(ini),7); hdr_av=avatar_index(k[j].fp); hdr_online=k[j].online; hdr_trust=k[j].trust;
            hdr_via=k[j].online && k[j].via_relay;
            if(k[j].trust==1) _snwprintf(hdr_sub,160,k[j].online?L"  ·  已验证身份  ·  %s":L"  ·  已验证身份",k[j].via_relay?L"经服务器":L"局域网直连"); else _snwprintf(hdr_sub,160,L"  ·  %s",k[j].trust==2?L"已屏蔽":L"未信任"); /* path only when online and not untrusted/blocked */ _snwprintf(title,400,L"%s    %s  ·  %s  ·  局域网直连",W(k[j].name),k[j].online?L"在线":L"离线",k[j].trust==1?L"已验证身份":k[j].trust==2?L"已屏蔽":L"未验证身份"); } }
        else { char u[37]; nk=lsm_groups(C,&g); for(j=0;j<nk;j++){ lsm_uuid_str(g[j].id,u); if(!strcmp(u,cur_conv+6)){ char ini[8]; _snwprintf(title,400,L"%s    %d 人  ·  群密钥版本 %llu",W(g[j].name),g[j].nmembers,(unsigned long long)g[j].key_epoch); hdr_on=1; hdr_group=1; hdr_online=0; hdr_trust=0; wcsncpy(hdr_name,W(g[j].name),159); lsm_initials(g[j].name,ini); wcsncpy(hdr_ini,W(ini),7); hdr_av=avatar_index(u); _snwprintf(hdr_sub,160,L"%d 位成员  ·  群密钥版本 %llu",g[j].nmembers,(unsigned long long)g[j].key_epoch); } } }
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
                if(r->path[0] && (!wcscmp(r->ext,L"PNG")||!wcscmp(r->ext,L"JPG")||!wcscmp(r->ext,L"JPEG")||!wcscmp(r->ext,L"GIF")||!wcscmp(r->ext,L"BMP"))) r->kind=R_IMAGE;
                { int j3; for(j3=0;j3<nt;j3++) if(!strcmp(t2[j3].msg_id,m[i].id)){ lsm_transfer *tx=&t2[j3]; wchar_t rb[32];
                    if(tx->state==0 && tx->resumable && tx->error[0]){ _snwprintf(r->meta,96,L"%s  ·  连接中断，重连后自动续传",sz); r->meta[95]=0; r->tstate=100; r->emph=1; r->prog=1; }
                    else if(tx->state==1 && tx->resumed_from>0 && tx->size){ fmt_size(tx->resumed_from,rb,32); _snwprintf(r->meta,96,L"从 %d%%（%s）处续传  ·  %d%%",(int)(tx->resumed_from*100/tx->size),rb,r->pct); r->meta[95]=0; r->tstate=101; r->emph=1; r->prog=1; } } }
                if(r->tstate>=100) r->tstate=r->tstate==100?0:1; else
                if(r->tstate==1){ r->prog=1; _snwprintf(r->meta,96,L"%s  ·  %s %d%%",sz,TS[1],r->pct); } else if(r->tstate==3) _snwprintf(r->meta,96,L"%s  ·  %s",sz,r->ext); else _snwprintf(r->meta,96,L"%s  ·  %s",sz,TS[r->tstate<7?r->tstate:4]); r->meta[95]=0; }
        } }
    lsm_unlock(C);
    if(!cur_conv[0]) hdr_on=0; SetWindowTextW(hTitle,cur_conv[0]?title:L""); InvalidateRect(hTitle,NULL,FALSE); ShowWindow(hHdrSearch,hdr_on?SW_SHOW:SW_HIDE);
    for(i=0;i<nrows;i++) SendMessageW(hChat,LB_ADDSTRING,0,(LPARAM)i);
    if(nrows){ RECT cr; int top=nrows-1, acc; GetClientRect(hChat,&cr); acc=rows[top].h; while(top>0 && acc+rows[top-1].h<=cr.bottom){ top--; acc+=rows[top].h; } SendMessageW(hChat,LB_SETCURSEL,nrows-1,0); SendMessageW(hChat,LB_SETTOPINDEX,top,0); } /* SETCURSEL scrolls the last row into view (selection itself is not drawn) */ /* newest at the bottom, scrolled to the end */
    SendMessageW(hChat,WM_SETREDRAW,TRUE,0); if(nrows) PostMessageW(hChat,WM_VSCROLL,SB_BOTTOM,0); /* after redraw is back on: the listbox recomputes its page first */ InvalidateRect(hChat,NULL,TRUE); InvalidateRect(hMain,NULL,FALSE); }

static void copy_text(const wchar_t *t){ size_t n=(wcslen(t)+1)*sizeof(wchar_t); HGLOBAL g; if(!OpenClipboard(hMain)) return; EmptyClipboard(); g=GlobalAlloc(GMEM_MOVEABLE,n); if(g){ memcpy(GlobalLock(g),t,n); GlobalUnlock(g); SetClipboardData(CF_UNICODETEXT,g); } CloseClipboard(); }
static void show_in_folder(const char *path){ wchar_t a[1200]; _snwprintf(a,1200,L"/select,\"%s\"",W(path)); a[1199]=0; ShellExecuteW(hMain,L"open",L"explorer.exe",a,NULL,SW_SHOWNORMAL); }
/* right-click (or Shift+F10 / menu key) on a bubble: 复制 / 打开 / 在文件夹中显示 / 删除这条消息（仅本机）; left-click on the file card's button or link */
/* ---------------------------------------------------------------- slim themed scrollbar for the owner-drawn lists
 * The system scrollbar ignores the app theme (white in dark mode). Both lists are created without WS_VSCROLL; the
 * listbox still scrolls with the wheel, keyboard and WM_VSCROLL, and this 10 px overlay (a sibling above the list's
 * right edge) shows the position and supports thumb drag + page clicks. Hidden when everything fits. */
#define SCROLLTHUMB_LIGHT HX(0xACA899)
#define SCROLLTHUMB_DARK  HX(0x6A6A6A)
static HWND hChatSb, hListSb; static WNDPROC oldListProc;
static void sb_metrics(HWND lb, int *total, int *page, int *pos){ RECT c; int i, top=(int)SendMessageW(lb,LB_GETTOPINDEX,0,0), n=(int)SendMessageW(lb,LB_GETCOUNT,0,0); GetClientRect(lb,&c); *page=c.bottom; *total=0; *pos=0;
    for(i=0;i<n;i++){ int h=(int)SendMessageW(lb,LB_GETITEMHEIGHT,i,0); if(h<0) h=0; if(i<top) *pos+=h; *total+=h; } }
static int sb_thumb(HWND sb, RECT *th){ HWND lb=(HWND)GetWindowLongPtrW(sb,GWLP_USERDATA); RECT c; int total,page,pos,hh; GetClientRect(sb,&c); sb_metrics(lb,&total,&page,&pos);
    if(total<=page||page<=0) return 0; hh=c.bottom*page/total; if(hh<scale_px(28)) hh=scale_px(28); if(pos>total-page) pos=total-page;
    th->left=c.left+scale_px(2); th->right=c.right-scale_px(2); th->top=(c.bottom-hh)*pos/(total-page); th->bottom=th->top+hh; return 1; }
static void sb_update(HWND lb){ HWND sb=lb==hChat?hChatSb:lb==hList?hListSb:NULL; int total,page,pos; if(!sb) return; sb_metrics(lb,&total,&page,&pos);
    ShowWindow(sb,total>page?SW_SHOWNA:SW_HIDE); if(total>page){ SetWindowPos(sb,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE); InvalidateRect(sb,NULL,TRUE); } }
static void sb_place(HWND lb, HWND sb){ RECT r; GetWindowRect(lb,&r); MapWindowPoints(NULL,GetParent(lb),(POINT*)&r,2); MoveWindow(sb,r.right-scale_px(10),r.top,scale_px(10),r.bottom-r.top,TRUE); sb_update(lb); }
static void sb_scroll_to(HWND lb, int px){ int i, n=(int)SendMessageW(lb,LB_GETCOUNT,0,0), acc=0; for(i=0;i<n;i++){ int h=(int)SendMessageW(lb,LB_GETITEMHEIGHT,i,0); if(acc+h/2>px) break; acc+=h; } SendMessageW(lb,LB_SETTOPINDEX,i<n?i:n-1,0); sb_update(lb); }
static LRESULT CALLBACK ScrollProc(HWND h, UINT m, WPARAM w, LPARAM l){ static int drag_off=-1; HWND lb=(HWND)GetWindowLongPtrW(h,GWLP_USERDATA); RECT th, c;
    switch(m){
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps); HBRUSH bg=lb==hChat?brChat:brSide; GetClientRect(h,&c); FillRect(dc,&c,bg);
        if(sb_thumb(h,&th)){ HBRUSH b=CreateSolidBrush(theme_mode==2?SCROLLTHUMB_DARK:SCROLLTHUMB_LIGHT); HPEN pn=(HPEN)GetStockObject(NULL_PEN); HGDIOBJ ob=SelectObject(dc,b), op=SelectObject(dc,pn); RoundRect(dc,th.left,th.top,th.right+1,th.bottom+1,scale_px(6),scale_px(6)); SelectObject(dc,ob); SelectObject(dc,op); DeleteObject(b); }
        EndPaint(h,&ps); return 0; }
    case WM_LBUTTONDOWN: { POINT p={(short)LOWORD(l),(short)HIWORD(l)}; if(!sb_thumb(h,&th)) return 0;
        if(PtInRect(&th,p)){ drag_off=p.y-th.top; SetCapture(h); } else { SendMessageW(lb,WM_VSCROLL,p.y<th.top?SB_PAGEUP:SB_PAGEDOWN,0); sb_update(lb); } return 0; }
    case WM_MOUSEMOVE: if(drag_off>=0 && GetCapture()==h){ int total,page,pos, y=(short)HIWORD(l)-drag_off, span; GetClientRect(h,&c); sb_metrics(lb,&total,&page,&pos); sb_thumb(h,&th); span=c.bottom-(th.bottom-th.top);
            if(span>0){ if(y<0) y=0; if(y>span) y=span; sb_scroll_to(lb,(int)((long long)(total-page)*y/span)); } } return 0;
    case WM_LBUTTONUP: case WM_CAPTURECHANGED: drag_off=-1; if(GetCapture()==h) ReleaseCapture(); return 0;
    case WM_MOUSEWHEEL: return SendMessageW(lb,m,w,l);
    case WM_SETCURSOR: SetCursor(LoadCursor(NULL,IDC_ARROW)); return TRUE;
    }
    return DefWindowProcW(h,m,w,l); }
static LRESULT list_after(HWND h, UINT m, LRESULT r){ if(m==WM_MOUSEWHEEL||m==WM_KEYDOWN||m==WM_VSCROLL||m==WM_LBUTTONDOWN||m==WM_MOUSEMOVE||m==WM_SIZE||m==LB_SETTOPINDEX||m==LB_SETCURSEL||m==LB_RESETCONTENT||m==LB_ADDSTRING||m==WM_SETREDRAW) sb_update(h); return r; }
static LRESULT CALLBACK ListProc(HWND h, UINT m, WPARAM w, LPARAM l){ return list_after(h,m,CallWindowProcW(oldListProc,h,m,w,l)); }
static WNDPROC oldChatProc;
static int chat_hit(LPARAM l, RECT *ir){ DWORD r=(DWORD)SendMessageW(hChat,LB_ITEMFROMPOINT,0,l); int i=(int)LOWORD(r); if(HIWORD(r) || i<0 || i>=nrows) return -1; SendMessageW(hChat,LB_GETITEMRECT,i,(LPARAM)ir); return i; }
static LRESULT CALLBACK ChatProc(HWND h, UINT m, WPARAM w, LPARAM l){
    if(m==WM_LBUTTONUP){ RECT ir; int i=chat_hit(l,&ir); POINT p={(short)LOWORD(l),(short)HIWORD(l)};
        if(i>=0 && rows[i].kind==R_IMAGE){ RECT b=row_bubble(&rows[i],ir); if(PtInRect(&b,p)) ShellExecuteW(hMain,L"open",W(rows[i].path),NULL,NULL,SW_SHOWNORMAL); }
        if(i>=0 && rows[i].kind==R_FILE && rows[i].path[0]){ RECT b=row_bubble(&rows[i],ir), br=file_btn_rect(b), lr=file_link_rect(b);
            if(PtInRect(&br,p)) ShellExecuteW(hMain,L"open",W(rows[i].path),NULL,NULL,SW_SHOWNORMAL); else if(PtInRect(&lr,p)) show_in_folder(rows[i].path); } }
    if(m==WM_SETCURSOR){ POINT p; RECT ir; int i; GetCursorPos(&p); ScreenToClient(h,&p); i=chat_hit(MAKELPARAM(p.x,p.y),&ir);
        if(i>=0 && rows[i].kind==R_FILE && rows[i].path[0]){ RECT b=row_bubble(&rows[i],ir), br=file_btn_rect(b), lr=file_link_rect(b); if(PtInRect(&br,p)||PtInRect(&lr,p)){ SetCursor(LoadCursor(NULL,IDC_HAND)); return TRUE; } } }
    if(m==WM_CONTEXTMENU){ POINT pt; RECT ir; int k=-1, cmd; HMENU pm; row_t *r=NULL;
        if(l==(LPARAM)-1){ k=(int)SendMessageW(h,LB_GETCARETINDEX,0,0); if(k>=0 && k<nrows){ SendMessageW(h,LB_GETITEMRECT,k,(LPARAM)&ir); pt.x=ir.left+scale_px(40); pt.y=ir.top+scale_px(10); ClientToScreen(h,&pt); } else { pt.x=pt.y=0; ClientToScreen(h,&pt); } }
        else { POINT c; pt.x=(short)LOWORD(l); pt.y=(short)HIWORD(l); c=pt; ScreenToClient(h,&c); k=chat_hit(MAKELPARAM(c.x,c.y),&ir); if(k>=0) SendMessageW(h,LB_SETCURSEL,k,0); }
        if(k>=0 && k<nrows && rows[k].kind>=R_TEXT) r=&rows[k];
        pm=CreatePopupMenu();
        AM(pm,MF_STRING|(r?0:MF_GRAYED),1,L"复制(&C)\tCtrl+C");
        if(r && r->kind>=R_FILE){ AM(pm,MF_STRING|(r->path[0]?0:MF_GRAYED),4,L"打开(&O)"); AM(pm,MF_STRING|(r->path[0]?0:MF_GRAYED),5,L"在文件夹中显示(&S)"); }
        AM(pm,MF_SEPARATOR,0,NULL); AM(pm,MF_STRING|(r?0:MF_GRAYED),3,L"删除这条消息（仅本机）(&D)…"); menu_bg(pm);
        cmd=TrackPopupMenu(pm,TPM_RIGHTBUTTON|TPM_RETURNCMD|TPM_NONOTIFY,pt.x,pt.y,0,hMain,NULL); /* owner = main window: with the owner-drawn LISTBOX as owner wine measured every item 0 px wide (blank menu) */ DestroyMenu(pm);
        if(cmd==1 && r) copy_text(r->kind==R_TEXT?r->text:r->name);
        else if(cmd==4 && r) ShellExecuteW(hMain,L"open",W(r->path),NULL,NULL,SW_SHOWNORMAL);
        else if(cmd==5 && r) show_in_folder(r->path);
        else if(cmd==3 && r){ char id[40]; memcpy(id,r->id,40); if(tmsg(L"删除消息",L"只会从本机删除这条消息，对方仍保留。此操作无法撤销。确定删除？",1)==1){ if(lsm_delete_message(C,id)) msgbox("删除失败：消息不存在。",MB_ICONWARNING); refresh_chat(); } }
        return 0; }
    if(m==WM_KEYDOWN && w=='C' && (GetKeyState(VK_CONTROL)&0x8000)){ int k=(int)SendMessageW(h,LB_GETCARETINDEX,0,0); if(k>=0 && k<nrows && rows[k].kind>=R_TEXT) copy_text(rows[k].kind==R_TEXT?rows[k].text:rows[k].name); return 0; }
    return list_after(h,m,CallWindowProcW(oldChatProc,h,m,w,l)); }

/* Edit controls with WS_VSCROLL always show a (system-coloured) scrollbar; show it only when the text overflows. */
static void fit_vscroll(HWND e){ RECT r; TEXTMETRICW tm; HDC dc; HGDIOBJ of; int lines, vis, need; LONG st=GetWindowLongW(e,GWL_STYLE);
    GetClientRect(e,&r); dc=GetDC(e); of=SelectObject(dc,(HFONT)SendMessageW(e,WM_GETFONT,0,0)); GetTextMetricsW(dc,&tm); SelectObject(dc,of); ReleaseDC(e,dc);
    lines=(int)SendMessageW(e,EM_GETLINECOUNT,0,0); vis=tm.tmHeight?(r.bottom-r.top)/tm.tmHeight:1; need=lines>vis;
    if(need!=((st&WS_VSCROLL)!=0)){ ShowScrollBar(e,SB_VERT,need); } }
/* a balloon must point at a real taskbar tray: wine/CI without one draws it at the window's top-left */
static int tray_visible(void){ HWND t=FindWindowW(L"Shell_TrayWnd",NULL), n; RECT r, w, x; if(!t || !IsWindowVisible(t)) return 0; n=FindWindowExW(t,NULL,L"TrayNotifyWnd",NULL); if(!n || !GetWindowRect(n,&r) || r.right-r.left<8) return 0;
    GetWindowRect(hMain,&w); return !(IsWindowVisible(hMain) && IntersectRect(&x,&r,&w)); }
static void tray_balloon(const char *title, const char *body){ if(!tray_added || !tray_visible()) return;
    nid.uFlags=NIF_INFO; wcsncpy(nid.szInfoTitle,W(title),63); nid.szInfoTitle[63]=0; wcsncpy(nid.szInfo,W(body),255); nid.szInfo[255]=0; nid.dwInfoFlags=NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY,&nid); nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP; nid.szInfo[0]=0; }
/* XP: the shell may not be up yet at autostart, and explorer.exe restarts drop all icons
 * (it then broadcasts "TaskbarCreated"). Keep retrying NIM_ADD until it succeeds. */
static void tray_add(void){ nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP; tray_added=(Shell_NotifyIconW(NIM_ADD,&nid)||Shell_NotifyIconW(NIM_MODIFY,&nid))?1:0; } /* NIM_ADD fails when the icon is already there (TaskbarCreated after a successful add, as wine sends when a tray docks): still added */

static void poll_events(void){
    char e[4096]; int list_dirty=0, chat_dirty=0;
    while(lsm_poll_event(C,e,sizeof e)){
        if(!strncmp(e,"msg\t",4)||!strncmp(e,"msg_update\t",11)){ char *f[8]; int n=0; char *p=e; f[n++]=p; while(*p && n<8){ if(*p=='\t'){*p=0; f[n++]=p+1;} p++; }
            if(n>=7){ if(!strcmp(f[2],cur_conv)) chat_dirty=1; list_dirty=1;
                if(!strcmp(f[0],"msg") && !strcmp(f[3],"in") && strcmp(f[5],"system") && (strcmp(f[2],cur_conv) || GetForegroundWindow()!=hMain)){ tray_balloon("新消息",f[6]); FlashWindow(hMain,TRUE); } } }
        else if(!strncmp(e,"transfer\t",9)) chat_dirty=1;
        else if(!strncmp(e,"notice\t",7)){ tray_balloon("提示",e+7); }
        else if(!strncmp(e,"pin_result\t",11)){ msgbox(strstr(e,"\tok")?"PIN 配对成功，已互相信任。":"PIN 配对失败（PIN 错误或已过期）。",MB_ICONINFORMATION); list_dirty=1; }
        else if(!strncmp(e,"relay\t",6)){ relay_dialog_refresh(); list_dirty=1; }
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

/* ---------------------------------------------------------------- 中转服务器（可选） settings dialog (strings shared with WinUI / Android, relay/NOTES.md) */
#define IDC_R_EN 301
#define IDC_R_HOST 302
#define IDC_R_PORT 303
#define IDC_R_PASS 304
#define IDC_R_CLEAR 305
#define IDC_R_STATUS 306
#define IDC_R_PASSROW 307
static HWND hRelayDlg, hRelayStatus;
static int relay_clear_pending; static char relay_state[16]="off";
static void relay_status_text(wchar_t *o, int n){ char det[256]="", host[256]=""; const char *st; int en=0, port=0, hp=0, peers=0; const wchar_t *t;
    lsm_relay_get(C,&en,host,sizeof host,&port,&hp); lsm_lock(C); st=lsm_relay_status(C,det,sizeof det); peers=lsm_relay_peers(C); lsm_unlock(C);
    snprintf(relay_state,sizeof relay_state,"%s",st); if(hRelayDlg) InvalidateRect(hRelayDlg,NULL,FALSE);
    t=!strcmp(st,"online")?L"已连接中转服务器":!strcmp(st,"connecting")?L"正在连接中转服务器…":!strcmp(st,"auth_failed")?L"网络口令错误":!strcmp(st,"error")?L"无法连接中转服务器":L"未启用";
    if(!strcmp(st,"online")) _snwprintf(o,n,L"%s  ·  %s:%d · 服务器上 %d 人",t,W(host),port,peers); else if(det[0] && strcmp(st,"off")) _snwprintf(o,n,L"%s  ·  %s",t,W(det)); else _snwprintf(o,n,L"%s",t); o[n-1]=0; }
static LRESULT CALLBACK RelayProc(HWND h, UINT m, WPARAM w, LPARAM l){
    switch(m){
    case WM_COMMAND:
        if(LOWORD(w)==IDC_R_CLEAR){ relay_clear_pending=1; ShowWindow(GetDlgItem(h,IDC_R_PASSROW),SW_HIDE); ShowWindow(GetDlgItem(h,IDC_R_CLEAR),SW_HIDE); InvalidateRect(GetDlgItem(h,IDC_R_PASS),NULL,TRUE); tray_balloon("局域网安全通讯","已清除网络口令"); return 0; }
        if(LOWORD(w)==IDOK){ wchar_t hw[256], pw[256], ps[256]; char hu[768], pu[768], err[256]=""; int en=(int)SendMessageW(GetDlgItem(h,IDC_R_EN),BM_GETCHECK,0,0)==BST_CHECKED, port;
            GetWindowTextW(GetDlgItem(h,IDC_R_HOST),hw,256); GetWindowTextW(GetDlgItem(h,IDC_R_PORT),pw,256); GetWindowTextW(GetDlgItem(h,IDC_R_PASS),ps,256); port=_wtoi(pw);
            if(en && !hw[0]){ tmsg(L"中转服务器",L"请填写服务器地址",0); SetFocus(GetDlgItem(h,IDC_R_HOST)); return 0; }
            if(en && (port<1||port>65535)){ tmsg(L"中转服务器",L"端口无效",0); SetFocus(GetDlgItem(h,IDC_R_PORT)); return 0; }
            if(lsm_relay_set(C,en,U(hw,hu,sizeof hu),port?port:9443,ps[0]?U(ps,pu,sizeof pu):(relay_clear_pending?"":NULL),err)){ tmsg(L"中转服务器",W(err),0); return 0; }
            { int sh=ps[0]?SW_SHOW:relay_clear_pending?SW_HIDE:-1; if(sh>=0){ ShowWindow(GetDlgItem(h,IDC_R_PASSROW),sh); ShowWindow(GetDlgItem(h,IDC_R_CLEAR),sh); } } SecureZeroMemory(ps,sizeof ps); relay_clear_pending=0; SetWindowTextW(GetDlgItem(h,IDC_R_PASS),L"");
            { wchar_t st[300]; relay_status_text(st,300); SetWindowTextW(hRelayStatus,en?L"中转服务器设置已保存，正在连接…":L"已关闭中转服务器"); (void)st; } return 0; }
        if(LOWORD(w)==IDCANCEL){ DestroyWindow(h); return 0; }
        return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: hRelayDlg=NULL; hRelayStatus=NULL; EnableWindow(hMain,TRUE); SetForegroundWindow(hMain); return 0;
    case WM_CTLCOLORSTATIC: { HWND c=(HWND)l; LRESULT r=ctl_color((HDC)w,c,0); if(GetDlgCtrlID(c)==IDC_R_STATUS||GetDlgCtrlID(c)==IDC_R_PASSROW) SetTextColor((HDC)w,T.text_secondary); return r; }
    case WM_CTLCOLORBTN: case WM_CTLCOLORDLG: return ctl_color((HDC)w,(HWND)l,0);
    case WM_CTLCOLOREDIT: return ctl_color((HDC)w,(HWND)l,1);
    case WM_ERASEBKGND: { RECT r; GetClientRect(h,&r); FillRect((HDC)w,&r,brWin); return 1; }
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps); RECT cr, tr; HBRUSH dv=CreateSolidBrush(T.divider); GetClientRect(h,&cr);
        DeleteObject(dv); tr=cr; tr.left=scale_px(14); tr.top=scale_px(10); tr.bottom=tr.top+scale_px(22); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,T.text_primary); SelectObject(dc,fBodyStrong); DrawTextW(dc,L"中转服务器（可选）",-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        if(hRelayStatus){ RECT sr; COLORREF dc2=!strcmp(relay_state,"online")?T.online:!strcmp(relay_state,"connecting")?RGB(0xE0,0x9A,0x10):(!strcmp(relay_state,"auth_failed")||!strcmp(relay_state,"error"))?RGB(0xD0,0x30,0x20):T.text_secondary;
            HBRUSH db=CreateSolidBrush(dc2); HPEN dp=CreatePen(PS_SOLID,1,dc2); HGDIOBJ ob2=SelectObject(dc,db), op2=SelectObject(dc,dp); int d=scale_px(10), cy;
            GetWindowRect(hRelayStatus,&sr); MapWindowPoints(NULL,h,(POINT*)&sr,2); cy=sr.top+scale_px(11); Ellipse(dc,scale_px(14),cy-d/2,scale_px(14)+d,cy-d/2+d); SelectObject(dc,ob2); SelectObject(dc,op2); DeleteObject(db); DeleteObject(dp); }
        frame_field(h,dc,GetDlgItem(h,IDC_R_HOST)); frame_field(h,dc,GetDlgItem(h,IDC_R_PORT)); frame_field(h,dc,GetDlgItem(h,IDC_R_PASS)); EndPaint(h,&ps); return 0; }
    case WM_DRAWITEM: { DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT*)l; if(d->CtlID==IDC_R_CLEAR){ wchar_t t[32]; FillRect(d->hDC,&d->rcItem,brWin); SetBkMode(d->hDC,TRANSPARENT); SetTextColor(d->hDC,P.accenttext); SelectObject(d->hDC,fBody); GetWindowTextW(d->hwndItem,t,32); DrawTextW(d->hDC,t,-1,&d->rcItem,DT_LEFT|DT_VCENTER|DT_SINGLELINE); if(d->itemState&ODS_FOCUS){ RECT f=d->rcItem; DrawFocusRect(d->hDC,&f); } return TRUE; }
        if(d->CtlType==ODT_BUTTON){ draw_button(d); return TRUE; } break; }
    case WM_SETCURSOR: if((HWND)w==GetDlgItem(h,IDC_R_CLEAR)){ SetCursor(LoadCursor(NULL,IDC_HAND)); return TRUE; } break;
    }
    return DefWindowProcW(h,m,w,l); }
static WNDPROC oldPassProc;
static LRESULT CALLBACK PassProc(HWND h, UINT m, WPARAM w, LPARAM l){ if(m==WM_PAINT && GetWindowTextLengthW(h)==0){ int en,port,hp; char host[256]; LRESULT r=CallWindowProcW(oldPassProc,h,m,w,l); HDC dc=GetDC(h); RECT cr; GetClientRect(h,&cr); cr.left+=3;
        lsm_relay_get(C,&en,host,sizeof host,&port,&hp); if(relay_clear_pending) hp=0; SetBkMode(dc,TRANSPARENT); SetTextColor(dc,T.text_secondary); SelectObject(dc,fBody); DrawTextW(dc,hp?L"已设置（留空则保持不变）":L"未设置",-1,&cr,DT_LEFT|DT_VCENTER|DT_SINGLELINE); ReleaseDC(h,dc); return r; }
    if(m==WM_SETFOCUS||m==WM_KILLFOCUS){ InvalidateRect(h,NULL,TRUE); InvalidateRect(GetParent(h),NULL,FALSE); } return CallWindowProcW(oldPassProc,h,m,w,l); }
static void relay_dialog(void){ RECT r; HWND h, c; int y=scale_px(40), en=0, port=9443, hp=0; char host[256]=""; wchar_t ps[16], st[300];
    if(hRelayDlg){ SetForegroundWindow(hRelayDlg); return; }
    lsm_relay_get(C,&en,host,sizeof host,&port,&hp); relay_clear_pending=0; GetWindowRect(hMain,&r);
    h=hRelayDlg=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,L"LsmRelay",L"中转服务器",WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN,r.left,r.top,460,380,hMain,NULL,hInst,NULL);
    { HMENU sm=GetSystemMenu(h,FALSE); if(sm){ DeleteMenu(sm,SC_SIZE,MF_BYCOMMAND); DeleteMenu(sm,SC_MAXIMIZE,MF_BYCOMMAND); DeleteMenu(sm,SC_MINIMIZE,MF_BYCOMMAND); DeleteMenu(sm,SC_RESTORE,MF_BYCOMMAND); } }
#define CTL(cls,txt,st2,x,yy,w,hh,id) c=CreateWindowW(cls,txt,WS_CHILD|WS_VISIBLE|(st2),x,yy,w,hh,h,(HMENU)(INT_PTR)(id),hInst,NULL); SendMessageW(c,WM_SETFONT,(WPARAM)fBody,1);
    CTL(L"STATIC",L"不在同一局域网时，可通过你自己部署的中转服务器联系对方。局域网直连始终优先；服务器只转发端到端加密的数据，无法看到消息内容。",0,14,y,430,62,0) y+=66;
    CTL(L"BUTTON",L"通过中转服务器连接",WS_TABSTOP|BS_AUTOCHECKBOX,14,y,300,24,IDC_R_EN) SendMessageW(c,BM_SETCHECK,en?BST_CHECKED:BST_UNCHECKED,0); y+=32;
    CTL(L"STATIC",L"服务器地址",0,14,y+3,90,22,0) CTL(L"EDIT",W(host),WS_TABSTOP|ES_AUTOHSCROLL,110,y,236,24,IDC_R_HOST)
    CTL(L"STATIC",L"端口",0,356,y+3,34,22,0) _snwprintf(ps,16,L"%d",port?port:9443); CTL(L"EDIT",ps,WS_TABSTOP|ES_NUMBER,392,y,52,24,IDC_R_PORT) y+=34;
    CTL(L"STATIC",L"网络口令",0,14,y+3,90,22,0) CTL(L"EDIT",L"",WS_TABSTOP|ES_PASSWORD|ES_AUTOHSCROLL,110,y,334,24,IDC_R_PASS) oldPassProc=(WNDPROC)SetWindowLongPtrW(c,GWLP_WNDPROC,(LONG_PTR)PassProc); y+=30;
    CTL(L"STATIC",L"口令已设置",0,110,y+2,80,22,IDC_R_PASSROW) CTL(L"BUTTON",L"清除口令",WS_TABSTOP|BS_OWNERDRAW,192,y,70,24,IDC_R_CLEAR) if(!hp){ ShowWindow(GetDlgItem(h,IDC_R_PASSROW),SW_HIDE); ShowWindow(c,SW_HIDE); } y+=32;
    relay_status_text(st,300); CTL(L"STATIC",st,SS_LEFTNOWORDWRAP|SS_ENDELLIPSIS,32,y,412,22,IDC_R_STATUS) hRelayStatus=c; y+=36;
    CTL(L"BUTTON",L"保存并连接",WS_TABSTOP|BS_OWNERDRAW,254,y,100,28,IDOK) CTL(L"BUTTON",L"关闭",WS_TABSTOP|BS_OWNERDRAW,360,y,84,28,IDCANCEL)
#undef CTL
    { RECT wr={0,0,460,y+44}, mr; AdjustWindowRectEx(&wr,WS_POPUP|WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME); GetWindowRect(hMain,&mr);
      SetWindowPos(h,NULL,(mr.left+mr.right-(wr.right-wr.left))/2,(mr.top+mr.bottom-(wr.bottom-wr.top))/2,wr.right-wr.left,wr.bottom-wr.top,SWP_NOZORDER); } /* centred on the owner */
    EnableWindow(hMain,FALSE); ShowWindow(h,SW_SHOW); SetFocus(GetDlgItem(h,IDC_R_EN)); }
static void relay_dialog_refresh(void){ wchar_t st[300]; if(!hRelayStatus) return; relay_status_text(st,300); SetWindowTextW(hRelayStatus,st); }

static void do_command(int id){
    char err[256]="", u8[2048]; prompt_t p; memset(&p,0,sizeof p);
    switch(id){
    case IDM_EXIT: DestroyWindow(hMain); break;
    case IDM_RELAY: relay_dialog(); break;
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

/* ---------------------------------------------------------------- header, status-bar panes, icon buttons (xp-*-chat mockups) */
#define SHIELD_FILL RGB(0xE8,0xB3,0x0E)
#define SHIELD_EDGE RGB(0x9A,0x6A,0x00)
static void draw_shield(HDC dc, int x, int y, int s){ POINT p[6]={{x,y+s/6},{x+s/2,y},{x+s,y+s/6},{x+s,y+s/2},{x+s/2,y+s},{x,y+s/2}}; HBRUSH br=CreateSolidBrush(SHIELD_FILL); HPEN pn=CreatePen(PS_SOLID,1,SHIELD_EDGE); HGDIOBJ ob=SelectObject(dc,br), op=SelectObject(dc,pn); Polygon(dc,p,6); SelectObject(dc,ob); SelectObject(dc,op); DeleteObject(br); DeleteObject(pn); }
static void online_dot(HDC dc, RECT av, COLORREF ring){ RECT o={av.right-scale_px(9),av.bottom-scale_px(9),av.right+scale_px(3),av.bottom+scale_px(3)}; HBRUSH br=CreateSolidBrush(T.online); HPEN pn=CreatePen(PS_SOLID,2,ring); HGDIOBJ ob=SelectObject(dc,br), op=SelectObject(dc,pn); Ellipse(dc,o.left,o.top,o.right,o.bottom); SelectObject(dc,ob); SelectObject(dc,op); DeleteObject(br); DeleteObject(pn); }
static RECT hdr_avatar_rect(RECT c){ RECT a; a.left=c.left+scale_px(10); a.top=(c.top+c.bottom)/2-scale_px(17); a.right=a.left+scale_px(34); a.bottom=a.top+scale_px(34); return a; }
static void draw_header(DRAWITEMSTRUCT *d){ RECT c=d->rcItem, av, t; HDC dc=d->hDC; HBRUSH bg=CreateSolidBrush(P.header); SIZE sz; int x, lh;
    FillRect(dc,&c,bg); DeleteObject(bg); if(!hdr_on) return; SetBkMode(dc,TRANSPARENT);
    av=hdr_avatar_rect(c); fill_round(dc,av,scale_px(8),P.av[hdr_av],P.av[hdr_av]); SelectObject(dc,fBodyStrong); SetTextColor(dc,RGB(255,255,255)); DrawTextW(dc,hdr_ini,-1,&av,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    if(hdr_online) online_dot(dc,av,P.header);
    x=av.right+scale_px(10); lh=line_h(dc,fSubtitle);
    t.left=x; t.top=c.top+scale_px(5); t.right=c.right-scale_px(8); t.bottom=t.top+lh; SelectObject(dc,fBodyStrong); SetTextColor(dc,T.text_primary); DrawTextW(dc,hdr_name,-1,&t,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
    GetTextExtentPoint32W(dc,hdr_name,(int)wcslen(hdr_name),&sz); if(hdr_trust==1) draw_shield(dc,x+sz.cx+scale_px(6),(t.top+t.bottom)/2-scale_px(7),scale_px(14));
    t.top=t.bottom; t.bottom=c.bottom-scale_px(4); SelectObject(dc,fBody);
    if(!hdr_group && hdr_via){ draw_cloud(dc,t.left,(t.top+t.bottom)/2,T.text_secondary); t.left+=scale_px(16); } /* relay peer: cloud before 在线 (WinUI) */
    if(!hdr_group){ const wchar_t *on=hdr_online?L"在线":L"离线"; SetTextColor(dc,hdr_online?(theme_mode==2?HX(0x8EE07F):T.online):T.text_secondary); /* onlinetext (xp.json): the dot green #6CCB5F is Lc 57 as 14px text on the dark header */ DrawTextW(dc,on,-1,&t,DT_LEFT|DT_VCENTER|DT_SINGLELINE); GetTextExtentPoint32W(dc,on,(int)wcslen(on),&sz); t.left+=sz.cx; }
    SetTextColor(dc,T.text_secondary); DrawTextW(dc,hdr_sub,-1,&t,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS); }
static const int pane_w[4]={0,190,230,90}; /* 0 = takes the rest */
static void status_panes(RECT c, RECT out[4]){ int i, x=c.right; for(i=3;i>=1;i--){ out[i]=c; out[i].right=x; out[i].left=x-scale_px(pane_w[i]); x=out[i].left-scale_px(3); } out[0]=c; out[0].right=x; }
static void draw_status(DRAWITEMSTRUCT *d){ RECT c=d->rcItem, pn[4]; const wchar_t *tx[4]={st_conn,st_me,st_fp,st_tcp}; int i; HDC dc=d->hDC; FillRect(dc,&c,brWin); SetBkMode(dc,TRANSPARENT); SelectObject(dc,fBody); SetTextColor(dc,T.text_primary);
    InflateRect(&c,0,-2); status_panes(c,pn);
    for(i=0;i<4;i++){ RECT t=pn[i]; HPEN sh=CreatePen(PS_SOLID,1,T.divider), hi=CreatePen(PS_SOLID,1,theme_mode==2?RGB(0x40,0x40,0x40):RGB(255,255,255)); HGDIOBJ op=SelectObject(dc,sh); /* sunken pane: shadow top/left, highlight bottom/right */
        MoveToEx(dc,t.left,t.bottom-1,NULL); LineTo(dc,t.left,t.top); LineTo(dc,t.right-1,t.top); SelectObject(dc,hi); LineTo(dc,t.right-1,t.bottom-1); LineTo(dc,t.left,t.bottom-1); SelectObject(dc,op); DeleteObject(sh); DeleteObject(hi);
        t.left+=scale_px(6); t.right-=scale_px(4); if(i==1) { draw_shield(dc,t.left,(t.top+t.bottom)/2-scale_px(6),scale_px(12)); t.left+=scale_px(18); } SelectObject(dc,i==1?fBodyStrong:fBody); /* identity pane in semibold: regular 14px i/l stems straddle pixels under grayscale AA (ink ~#817F76, 3.3:1) */ DrawTextW(dc,tx[i],-1,&t,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS); } }
/* icon buttons: paperclip (attach), magnifier (header search), person+ (添加), paper plane + 发送(S) (default button, Luna-style blue edge) */
static void draw_icon_button(DRAWITEMSTRUCT *d){ RECT r=d->rcItem; HDC dc=d->hDC; int id=d->CtlID, hot=(d->itemState&ODS_SELECTED)!=0, cx, cy, u=scale_px(1); wchar_t t[64]; HPEN pn; HGDIOBJ op, ob;
    COLORREF bg=id==IDC_HDRSEARCH?P.header:T.window_bg; HBRUSH b=CreateSolidBrush(bg); FillRect(dc,&r,b); DeleteObject(b); SetBkMode(dc,TRANSPARENT);
    if(id==IDC_SEND||id==IDC_ADD||id==IDC_HDRSEARCH){ RECT f=r; if(id!=IDC_SEND) InflateRect(&f,-u,-u); fill_round(dc,f,scale_px(6),hot?P.rowsel:(id==IDC_SEND?T.card_bg:bg),id==IDC_SEND?P.rowselborder:(hot?P.rowselborder:P.stroke)); if(id==IDC_SEND){ RECT g=f; InflateRect(&g,-1,-1); { HBRUSH nb=(HBRUSH)GetStockObject(NULL_BRUSH); HPEN p2=CreatePen(PS_SOLID,1,P.fieldborder); op=SelectObject(dc,p2); ob=SelectObject(dc,nb); RoundRect(dc,g.left,g.top,g.right,g.bottom,scale_px(5),scale_px(5)); SelectObject(dc,op); SelectObject(dc,ob); DeleteObject(p2); } } }
    pn=CreatePen(PS_SOLID,2,id==IDC_FILE?T.text_secondary:P.accenttext); op=SelectObject(dc,pn); ob=SelectObject(dc,GetStockObject(NULL_BRUSH));
    cy=(r.top+r.bottom)/2;
    if(id==IDC_FILE){ cx=(r.left+r.right)/2; /* paperclip: two nested rounded loops, tilted by drawing as a polyline */ { POINT p[]={{cx+3*u,cy-6*u},{cx+3*u,cy+5*u},{cx,cy+8*u},{cx-3*u,cy+5*u},{cx-3*u,cy-8*u},{cx-1*u,cy-10*u},{cx+1*u,cy-10*u},{cx+1*u+0,cy-8*u},{cx+1*u,cy+3*u}}; Polyline(dc,p,9); } }
    else if(id==IDC_HDRSEARCH){ cx=(r.left+r.right)/2; Ellipse(dc,cx-6*u,cy-6*u,cx+3*u,cy+3*u); MoveToEx(dc,cx+2*u,cy+2*u,NULL); LineTo(dc,cx+6*u,cy+6*u); }
    else { int ix=r.left+scale_px(10); RECT tr=r; GetWindowTextW(d->hwndItem,t,64);
        if(id==IDC_SEND){ POINT p[4]={{ix,cy-6*u},{ix+13*u,cy},{ix,cy+6*u},{ix+3*u,cy}}; HBRUSH pb=CreateSolidBrush(P.accenttext); SelectObject(dc,pb); Polygon(dc,p,4); SelectObject(dc,GetStockObject(NULL_BRUSH)); DeleteObject(pb); }
        else { Ellipse(dc,ix+1*u,cy-7*u,ix+7*u,cy-1*u); Arc(dc,ix-2*u,cy,ix+10*u,cy+12*u,ix+10*u,cy+6*u,ix-2*u,cy+6*u); MoveToEx(dc,ix+11*u,cy-1*u,NULL); LineTo(dc,ix+11*u,cy+6*u); MoveToEx(dc,ix+8*u,cy+3*u,NULL); LineTo(dc,ix+15*u,cy+3*u); }
        tr.left=ix+scale_px(18); SelectObject(dc,fBody); SetTextColor(dc,T.text_primary); DrawTextW(dc,t,-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|((d->itemState&ODS_NOACCEL)?DT_HIDEPREFIX:0)); }
    SelectObject(dc,op); SelectObject(dc,ob); DeleteObject(pn);
    if((d->itemState&ODS_FOCUS) && id==IDC_SEND){ RECT f=r; InflateRect(&f,-4,-4); SetTextColor(dc,T.text_primary); SetBkColor(dc,T.card_bg); DrawFocusRect(dc,&f); } }
/* LSM_UI_TEST: decorative icon rects (online dots, shields), relative to the window's top-left, for the colour audit's ignore list */
static void ui_icons_dump(void){ static char last[4096]; char buf[4096]; int n=0, i, top, cnt; RECT wr, ir, av; POINT o; FILE *f;
    if(!ui_icons_path[0] || !IsWindowVisible(hMain)) return; GetWindowRect(hMain,&wr); buf[0]=0;
#define ADDR(hw,rc,nm) { POINT a={(rc).left,(rc).top}; ClientToScreen(hw,&a); n+=snprintf(buf+n,sizeof buf-n,"ICON %s %ld,%ld,%ld,%ld\n",nm,a.x-wr.left-3,a.y-wr.top-3,(rc).right-(rc).left+6,(rc).bottom-(rc).top+6); }
    top=(int)SendMessageW(hList,LB_GETTOPINDEX,0,0); cnt=(int)SendMessageW(hList,LB_GETCOUNT,0,0);
    for(i=top;i<cnt && i<nlist && n<3800;i++){ RECT d2; if(SendMessageW(hList,LB_GETITEMRECT,i,(LPARAM)&ir)==LB_ERR) break; { RECT cl; GetClientRect(hList,&cl); if(ir.top>cl.bottom) break; }
        av.left=ir.left+scale_px(10); av.top=(ir.top+ir.bottom)/2-scale_px(18); av.right=av.left+scale_px(36); av.bottom=av.top+scale_px(36);
        if(convs[i].online){ d2=av; d2.right+=scale_px(3); d2.bottom+=scale_px(3); ADDR(hList,d2,"avatar+online-dot"); } } /* OCR merges the dot with the monogram tile: report the tile + dot */
    if(hdr_on){ RECT c; GetClientRect(hTitle,&c); av=hdr_avatar_rect(c); if(hdr_online){ RECT d2=av; d2.right+=scale_px(3); d2.bottom+=scale_px(3); ADDR(hTitle,d2,"avatar+online-dot"); }
        if(hdr_trust==1){ HDC dc=GetDC(hTitle); SIZE sz; HGDIOBJ of=SelectObject(dc,fBodyStrong); RECT s2; int lh=line_h(dc,fSubtitle); GetTextExtentPoint32W(dc,hdr_name,(int)wcslen(hdr_name),&sz); SelectObject(dc,of); ReleaseDC(hTitle,dc);
            s2.left=av.right+scale_px(10)+sz.cx+scale_px(6); s2.top=c.top+scale_px(5)+lh/2-scale_px(7); s2.right=s2.left+scale_px(14); s2.bottom=s2.top+scale_px(14); ADDR(hTitle,s2,"shield"); } }
    { RECT c, pn[4]; GetClientRect(hStatus,&c); InflateRect(&c,0,-2); status_panes(c,pn); { RECT s2; s2.left=pn[1].left+scale_px(6); s2.top=(pn[1].top+pn[1].bottom)/2-scale_px(6); s2.right=s2.left+scale_px(12); s2.bottom=s2.top+scale_px(12); ADDR(hStatus,s2,"shield"); } }
    { int ct=(int)SendMessageW(hChat,LB_GETTOPINDEX,0,0), cn=(int)SendMessageW(hChat,LB_GETCOUNT,0,0); RECT cl; GetClientRect(hChat,&cl); /* photo thumbnails: user content, not UI colours */
      for(i=ct;i<cn && i<nrows && n<3800;i++){ RECT b; if(SendMessageW(hChat,LB_GETITEMRECT,i,(LPARAM)&ir)==LB_ERR || ir.top>cl.bottom) break; if(rows[i].kind!=R_IMAGE) continue; b=row_bubble(&rows[i],ir); InflateRect(&b,-scale_px(4),-scale_px(4)); if(b.bottom>cl.bottom) b.bottom=cl.bottom; if(b.top<0) b.top=0; ADDR(hChat,b,"image"); } }
    { RECT c; POINT a={0,0}; ClientToScreen(hMain,&a); if(a.y>wr.top){ n+=snprintf(buf+n,sizeof buf-n,"ICON system-frame 0,0,%ld,%ld\n",wr.right-wr.left,a.y-wr.top); } (void)c; } /* caption + frame: drawn by the OS / wine, not from the app palette */
    { HWND sc[2]={hChat,hList}; int q; for(q=0;q<2;q++) if(GetWindowLongW(sc[q],GWL_STYLE)&WS_VSCROLL){ RECT c, b; GetClientRect(sc[q],&c); b.left=c.right; b.right=c.right+GetSystemMetrics(SM_CXVSCROLL); b.top=c.top; b.bottom=c.bottom; ADDR(sc[q],b,"system-scrollbar"); } } /* drawn by the OS theme, not from the app palette */
    if(hRelayDlg && IsWindowVisible(hRelayDlg)){ RECT dr, sr, d2; POINT a={0,0}; GetWindowRect(hRelayDlg,&dr); ClientToScreen(hRelayDlg,&a); /* relay dialog: OS caption, and the status dot (state is spelled out next to it) */
        n+=snprintf(buf+n,sizeof buf-n,"ICON system-frame %ld,%ld,%ld,%ld\n",dr.left-wr.left,dr.top-wr.top,dr.right-dr.left,a.y-dr.top);
        if(hRelayStatus){ GetWindowRect(hRelayStatus,&sr); MapWindowPoints(NULL,hRelayDlg,(POINT*)&sr,2); d2.left=scale_px(14); d2.top=sr.top+scale_px(6); d2.right=d2.left+scale_px(10); d2.bottom=d2.top+scale_px(10); ADDR(hRelayDlg,d2,"relay-status-dot"); } }
#undef ADDR
    (void)o; if(!strcmp(buf,last)) return; strcpy(last,buf); f=fopen(ui_icons_path,"w"); if(f){ fputs(buf,f); fclose(f); } }

static WNDPROC oldInputProc, oldSearchProc;
/* search placeholder in the theme's secondary text colour (EM_SETCUEBANNER uses a fixed system grey: 2.6:1 on the dark field) */
static LRESULT CALLBACK SearchProc(HWND h, UINT m, WPARAM w, LPARAM l){ if(m==WM_PAINT && GetWindowTextLengthW(h)==0){ LRESULT r=CallWindowProcW(oldSearchProc,h,m,w,l); HDC dc=GetDC(h); RECT cr; GetClientRect(h,&cr); cr.left+=scale_px(3);
        SetBkMode(dc,TRANSPARENT); SetTextColor(dc,T.text_secondary); SelectObject(dc,fBody); DrawTextW(dc,L"搜索联系人和群组 (Ctrl+F)",-1,&cr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX); ReleaseDC(h,dc); return r; }
    if(m==WM_SETFOCUS||m==WM_KILLFOCUS){ RECT r; InvalidateRect(h,NULL,TRUE); GetWindowRect(h,&r); MapWindowPoints(NULL,hMain,(POINT*)&r,2); InflateRect(&r,2,2); InvalidateRect(hMain,&r,FALSE); } return CallWindowProcW(oldSearchProc,h,m,w,l); }
static LRESULT CALLBACK InputProc(HWND h, UINT m, WPARAM w, LPARAM l){ if(m==WM_PAINT && GetWindowTextLengthW(h)==0){ LRESULT r=CallWindowProcW(oldInputProc,h,m,w,l); HDC dc=GetDC(h); RECT cr; GetClientRect(h,&cr); InflateRect(&cr,-scale_px(4),-scale_px(2)); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,T.text_secondary);
        SelectObject(dc,fBody); DrawTextW(dc,L"输入消息",-1,&cr,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_NOPREFIX); SelectObject(dc,fCaption); DrawTextW(dc,L"Enter 发送 · Shift+Enter 换行",-1,&cr,DT_RIGHT|DT_TOP|DT_SINGLELINE|DT_NOPREFIX); ReleaseDC(h,dc); return r; }
    if(m==WM_SETFOCUS||m==WM_KILLFOCUS){ RECT r; InvalidateRect(h,NULL,TRUE); GetWindowRect(h,&r); MapWindowPoints(NULL,hMain,(POINT*)&r,2); InflateRect(&r,2,2); InvalidateRect(hMain,&r,FALSE); }
    if(m==WM_KEYDOWN && w==VK_TAB){ SetFocus(GetNextDlgTabItem(hMain,h,(GetKeyState(VK_SHIFT)&0x8000)?TRUE:FALSE)); return 0; } if(m==WM_CHAR && w=='\t') return 0; if(m==WM_KEYDOWN && w==VK_RETURN && !(GetKeyState(VK_SHIFT)&0x8000)){ send_current(); return 0; } if(m==WM_CHAR && w=='\r' && !(GetKeyState(VK_SHIFT)&0x8000)) return 0; return CallWindowProcW(oldInputProc,h,m,w,l); }

static void layout(int W_, int H_0){
    int T0=MENUBAR_H, H_=H_0-T0, sb=26, lw=W_/3<260?W_/3:260, ih=64, bw=96, i, x=4;
    for(i=0;i<5;i++){ wchar_t t[32]; RECT r={0,0,0,0}; HDC dc=GetDC(hMain); HGDIOBJ of=SelectObject(dc,fBody); GetWindowTextW(hMenuBtn[i],t,32); DrawTextW(dc,t,-1,&r,DT_CALCRECT|DT_SINGLELINE); SelectObject(dc,of); ReleaseDC(hMain,dc);
        MoveWindow(hMenuBtn[i],x,2,r.right+20,T0-4,TRUE); x+=r.right+22; }
    { int ph=34, hh=50, cb=56, top=T0+6, rx=lw+12, rw=W_-lw-18, bot=T0+H_-sb;
      MoveWindow(hAdd,lw-64,top+5,66,24,TRUE);                      /* 联系人与群组 pane header (text painted in WM_PAINT) */
      MoveWindow(hSearch,7,top+ph+1,lw-2,24,TRUE);
      MoveWindow(hList,6,top+ph+32,lw,bot-6-(top+ph+32),TRUE);
      MoveWindow(hTitle,rx,top,rw-44,hh,TRUE); MoveWindow(hHdrSearch,rx+rw-40,top+(hh-30)/2,32,30,TRUE);
      MoveWindow(hChat,rx,top+hh+1,rw,bot-cb-(top+hh+1)-6,TRUE);
      MoveWindow(hFile,rx,bot-cb+(cb-30)/2-3,30,30,TRUE);
      MoveWindow(hInput,rx+36,bot-cb,rw-36-98,cb-6,TRUE);
      MoveWindow(hSend,rx+rw-92,bot-cb+(cb-30)/2-3,92,30,TRUE);
      MoveWindow(hStatus,0,bot,W_,sb,TRUE); (void)ih; (void)bw; if(hChatSb){ sb_place(hChat,hChatSb); sb_place(hList,hListSb); } } }
static void open_menu(int i){ RECT r; GetWindowRect(hMenuBtn[i],&r); SendMessageW(hMenuBtn[i],BM_SETSTATE,TRUE,0);
    TrackPopupMenu(GetSubMenu(hMenuTree,i),TPM_LEFTALIGN|TPM_TOPALIGN|TPM_LEFTBUTTON,r.left,r.bottom,0,hMain,NULL); SendMessageW(hMenuBtn[i],BM_SETSTATE,FALSE,0); }

static HMENU build_menu(void){
    HMENU m=CreateMenu(), f=CreatePopupMenu(), c=CreatePopupMenu(), g=CreatePopupMenu(), s=CreatePopupMenu(), h=CreatePopupMenu();
    AM(f,MF_STRING,IDM_DLDIR,L"打开接收文件夹(&D)"); AM(f,MF_STRING,IDM_FIREWALL,L"设置防火墙例外(&F)..."); AM(f,MF_SEPARATOR,0,NULL); AM(f,MF_STRING,IDM_EXIT,L"退出(&X)");
    AM(c,MF_STRING,IDM_ADDIP,L"按 IP 添加(&A)..."); AM(c,MF_SEPARATOR,0,NULL); AM(c,MF_STRING,IDM_TRUST,L"信任(&T)"); AM(c,MF_STRING,IDM_UNTRUST,L"取消信任(&U)"); AM(c,MF_STRING,IDM_BLOCK,L"屏蔽(&B)");
    AM(c,MF_SEPARATOR,0,NULL); AM(c,MF_STRING,IDM_PINOFFER,L"生成 PIN（让对方输入）(&P)"); AM(c,MF_STRING,IDM_PINPAIR,L"输入 PIN 配对(&I)...");
    AM(g,MF_STRING,IDM_GCREATE,L"创建群组(&C)..."); AM(g,MF_STRING,IDM_GINVITE,L"邀请成员(&I)..."); AM(g,MF_STRING,IDM_GREMOVE,L"移除成员(&R)..."); AM(g,MF_STRING,IDM_GLEAVE,L"退出群组(&L)");
    AM(s,MF_STRING,IDM_NAME,L"修改昵称(&N)..."); AM(s,MF_STRING,IDM_RELAY,L"中转服务器(&R)..."); AM(s,MF_SEPARATOR,0,NULL); AM(s,MF_STRING|(theme_mode==0?MF_CHECKED:0),IDM_THEME_SYS,L"外观：跟随系统(&Y)"); AM(s,MF_STRING|(theme_mode==1?MF_CHECKED:0),IDM_THEME_LIGHT,L"外观：浅色(&L)"); AM(s,MF_STRING|(theme_mode==2?MF_CHECKED:0),IDM_THEME_DARK,L"外观：深色(&K)");
    AM(h,MF_STRING,IDM_ABOUT,L"关于(&A)");
    AM(m,MF_POPUP,(UINT_PTR)f,L"文件(&F)"); AM(m,MF_POPUP,(UINT_PTR)c,L"联系人(&C)"); AM(m,MF_POPUP,(UINT_PTR)g,L"群组(&G)"); AM(m,MF_POPUP,(UINT_PTR)s,L"设置(&S)"); AM(m,MF_POPUP,(UINT_PTR)h,L"帮助(&H)");
    menu_bg(m);
    return m; }

/* ---------------------------------------------------------------- XP basics: shortcuts, window memory, drag-drop, tooltips, contact menu */
static void select_conv(int s){ if(s<0||s>=nlist) return; SendMessageW(hList,LB_SETCURSEL,s,0); strcpy(cur_conv,list_ids[s]); refresh_chat(); if(lsm_mark_read(C,cur_conv)) refresh_list(); }
/* same keys as WinUI and GTK: Ctrl+F search, Ctrl+N add by IP, Ctrl+Tab / Ctrl+Shift+Tab next/previous chat, Esc clears the search
 * (Enter / Shift+Enter live in InputProc). Returns 1 when the key was handled. */
static int app_key(MSG *msg){ int ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0, shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
    if(msg->message!=WM_KEYDOWN || !IsWindowEnabled(hMain) || (GetAncestor(msg->hwnd,GA_ROOT)!=hMain)) return 0;
    if(ctrl && msg->wParam=='F'){ SetFocus(hSearch); SendMessageW(hSearch,EM_SETSEL,0,-1); return 1; }
    if(ctrl && msg->wParam=='N'){ do_command(IDM_ADDIP); return 1; }
    if(ctrl && msg->wParam==VK_TAB){ int s=(int)SendMessageW(hList,LB_GETCURSEL,0,0); if(nlist){ s=shift?(s<=0?nlist-1:s-1):(s+1)%nlist; select_conv(s); } return 1; }
    if(msg->wParam==VK_ESCAPE && msg->hwnd==hSearch){ if(GetWindowTextLengthW(hSearch)) SetWindowTextW(hSearch,L""); else SetFocus(hList); return 1; }
    return 0; }
static void win_path(wchar_t *p){ _snwprintf(p,MAX_PATH,L"%s\\window.txt",exe_dir); p[MAX_PATH-1]=0; }
static void win_save(void){ WINDOWPLACEMENT wp; wchar_t p[MAX_PATH]; FILE *f; memset(&wp,0,sizeof wp); wp.length=sizeof wp; if(!GetWindowPlacement(hMain,&wp)) return; win_path(p); f=_wfopen(p,L"w");
    if(f){ fprintf(f,"%ld %ld %ld %ld %d\n",wp.rcNormalPosition.left,wp.rcNormalPosition.top,wp.rcNormalPosition.right,wp.rcNormalPosition.bottom,wp.showCmd==SW_SHOWMAXIMIZED); fclose(f); } }
static int win_load(RECT *r, int *maxed){ wchar_t p[MAX_PATH]; FILE *f; long a,b,c,d; int mx=0, ok=0; win_path(p); f=_wfopen(p,L"r"); if(!f) return 0;
    if(fscanf(f,"%ld %ld %ld %ld %d",&a,&b,&c,&d,&mx)>=4 && c-a>=640 && d-b>=420){ RECT t={a,b,c,d}; RECT wa; SystemParametersInfoW(SPI_GETWORKAREA,0,&wa,0); RECT x; /* only when it is still mostly on screen (resolution may have changed) */
        if(IntersectRect(&x,&t,&wa) && (x.right-x.left)>=200 && (x.bottom-x.top)>=100){ *r=t; *maxed=mx; ok=1; } }
    fclose(f); return ok; }
static void drop_files(HDROP hd){ wchar_t path[MAX_PATH]; char u8[MAX_PATH*4], err[256]; UINT i, n=DragQueryFileW(hd,0xFFFFFFFF,NULL,0);
    if(!cur_conv[0]) msgbox("请先在左侧选择联系人或群组，再把文件拖进来。",MB_ICONINFORMATION);
    else for(i=0;i<n;i++){ if(DragQueryFileW(hd,i,path,MAX_PATH) && !(GetFileAttributesW(path)&FILE_ATTRIBUTE_DIRECTORY)){ if(lsm_send_file(C,cur_conv,U(path,u8,sizeof u8),err)){ msgbox(err,MB_ICONWARNING); break; } } }
    DragFinish(hd); refresh_chat(); }
static void add_tip(HWND tt, HWND ctl, const wchar_t *text){ TOOLINFOW ti; memset(&ti,0,sizeof ti); ti.cbSize=sizeof ti; ti.uFlags=TTF_IDISHWND|TTF_SUBCLASS; ti.hwnd=hMain; ti.uId=(UINT_PTR)ctl; ti.lpszText=(LPWSTR)text; SendMessageW(tt,TTM_ADDTOOLW,0,(LPARAM)&ti); }
#define IDM_OPENCHAT 240
#define IDM_COPYFP 241
static void contact_menu(LPARAM l){ POINT pt; int s=-1, dm, cmd; HMENU pm;
    if(l==(LPARAM)-1){ RECT ir; s=(int)SendMessageW(hList,LB_GETCURSEL,0,0); if(s<0) return; SendMessageW(hList,LB_GETITEMRECT,s,(LPARAM)&ir); pt.x=ir.left+scale_px(48); pt.y=ir.top+scale_px(28); ClientToScreen(hList,&pt); }
    else { POINT c; DWORD r; pt.x=(short)LOWORD(l); pt.y=(short)HIWORD(l); c=pt; ScreenToClient(hList,&c); r=(DWORD)SendMessageW(hList,LB_ITEMFROMPOINT,0,MAKELPARAM(c.x,c.y)); if(HIWORD(r)) return; s=LOWORD(r); }
    if(s<0||s>=nlist) return; select_conv(s); dm=!strncmp(cur_conv,"dm:",3);
    pm=CreatePopupMenu(); AM(pm,MF_STRING,IDM_OPENCHAT,L"打开聊天(&O)"); AM(pm,MF_SEPARATOR,0,NULL);
    if(dm){ AM(pm,MF_STRING,IDM_TRUST,L"信任(&T)"); AM(pm,MF_STRING,IDM_UNTRUST,L"取消信任(&U)"); AM(pm,MF_STRING,IDM_PINPAIR,L"输入 PIN 配对(&I)..."); AM(pm,MF_STRING,IDM_COPYFP,L"复制指纹(&F)"); AM(pm,MF_SEPARATOR,0,NULL); AM(pm,MF_STRING,IDM_BLOCK,L"屏蔽(&B)"); }
    else { AM(pm,MF_STRING,IDM_GINVITE,L"邀请成员(&I)..."); AM(pm,MF_STRING,IDM_GREMOVE,L"移除成员(&R)..."); AM(pm,MF_SEPARATOR,0,NULL); AM(pm,MF_STRING,IDM_GLEAVE,L"退出群组(&L)"); }
    menu_bg(pm); cmd=TrackPopupMenu(pm,TPM_RIGHTBUTTON|TPM_RETURNCMD|TPM_NONOTIFY,pt.x,pt.y,0,hMain,NULL); DestroyMenu(pm);
    if(cmd==IDM_OPENCHAT) SetFocus(hInput); else if(cmd==IDM_COPYFP) copy_text(W(cur_conv+3)); else if(cmd) do_command(cmd); }

/* LSM_UI_TEST_DROP=<path> (test only): once a chat is open, hand that path to the real WM_DROPFILES handler in a
   shell-style HDROP (DROPFILES + wide path list), exactly what Explorer posts on a drag & drop (X8) */
static void test_drop(HWND h){ static int done; wchar_t p[MAX_PATH]; DWORD n; HGLOBAL g; DROPFILES *df; if(done || !cur_conv[0]) return;
    n=GetEnvironmentVariableW(L"LSM_UI_TEST_DROP",p,MAX_PATH); if(!n || n>=MAX_PATH) { done=1; return; } done=1;
    g=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,sizeof(DROPFILES)+(n+2)*sizeof(wchar_t)); if(!g) return; df=(DROPFILES*)GlobalLock(g); df->pFiles=sizeof(DROPFILES); df->fWide=TRUE;
    memcpy((char*)df+sizeof(DROPFILES),p,n*sizeof(wchar_t)); GlobalUnlock(g); SendMessageW(h,WM_DROPFILES,(WPARAM)g,0); }
static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l){
    switch(m){
    case WM_CREATE: {
        DWORD es=0; /* flat: 1px divider frame painted in WM_PAINT */
        { static const wchar_t *mt[5]={L"文件(&F)",L"联系人(&C)",L"群组(&G)",L"设置(&S)",L"帮助(&H)"}; int i;
          for(i=0;i<5;i++){ hMenuBtn[i]=CreateWindowW(L"BUTTON",mt[i],WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,0,0,0,0,h,(HMENU)(INT_PTR)(IDC_MENUBTN+i),hInst,NULL); SendMessageW(hMenuBtn[i],WM_SETFONT,(WPARAM)fBody,1); } }
        hList=CreateWindowExW(es,L"LISTBOX",NULL,WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_TABSTOP|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS,0,0,0,0,h,(HMENU)IDC_LIST,hInst,NULL);
        hSearch=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,0,0,0,0,h,(HMENU)IDC_SEARCH,hInst,NULL); SendMessageW(hSearch,WM_SETFONT,(WPARAM)fBody,1); oldSearchProc=(WNDPROC)SetWindowLongPtrW(hSearch,GWLP_WNDPROC,(LONG_PTR)SearchProc);
        hAdd=CreateWindowW(L"BUTTON",L"添加",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,h,(HMENU)IDC_ADD,hInst,NULL); hHdrSearch=CreateWindowW(L"BUTTON",L"",WS_CHILD|BS_OWNERDRAW,0,0,0,0,h,(HMENU)IDC_HDRSEARCH,hInst,NULL);
        hTitle=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_OWNERDRAW,0,0,0,0,h,(HMENU)IDC_TITLE,hInst,NULL);
        hChat=CreateWindowExW(es,L"LISTBOX",NULL,WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_TABSTOP|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWVARIABLE,0,0,0,0,h,(HMENU)IDC_CHAT,hInst,NULL); /* owner-drawn bubbles */
        hInput=CreateWindowExW(es,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,0,0,0,0,h,(HMENU)IDC_INPUT,hInst,NULL);
        hSend=CreateWindowW(L"BUTTON",L"发送(&S)",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,h,(HMENU)IDC_SEND,hInst,NULL);
        hFile=CreateWindowW(L"BUTTON",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,h,(HMENU)IDC_FILE,hInst,NULL);
        hStatus=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_OWNERDRAW,0,0,0,0,h,(HMENU)IDC_STATUS,hInst,NULL); /* themed static, not the system-coloured status bar */
        SendMessageW(hList,WM_SETFONT,(WPARAM)hFont,1); SendMessageW(hChat,WM_SETFONT,(WPARAM)hFont,1); SendMessageW(hInput,WM_SETFONT,(WPARAM)hFont,1);
        SendMessageW(hSend,WM_SETFONT,(WPARAM)hFont,1); SendMessageW(hFile,WM_SETFONT,(WPARAM)hFont,1); SendMessageW(hStatus,WM_SETFONT,(WPARAM)fBody,1); /* body: 12px caption CJK at wine/XP grayscale AA measured below 4.5:1 */ SendMessageW(hTitle,WM_SETFONT,(WPARAM)fBodyStrong,1);
        SendMessageW(hInput,EM_LIMITTEXT,60000,0); SendMessageW(hAdd,WM_SETFONT,(WPARAM)fBody,1);
        oldInputProc=(WNDPROC)SetWindowLongPtrW(hInput,GWLP_WNDPROC,(LONG_PTR)InputProc); oldChatProc=(WNDPROC)SetWindowLongPtrW(hChat,GWLP_WNDPROC,(LONG_PTR)ChatProc); oldListProc=(WNDPROC)SetWindowLongPtrW(hList,GWLP_WNDPROC,(LONG_PTR)ListProc);
        hChatSb=CreateWindowExW(0,L"LsmScroll",NULL,WS_CHILD,0,0,0,0,h,NULL,hInst,NULL); SetWindowLongPtrW(hChatSb,GWLP_USERDATA,(LONG_PTR)hChat); hListSb=CreateWindowExW(0,L"LsmScroll",NULL,WS_CHILD,0,0,0,0,h,NULL,hInst,NULL); SetWindowLongPtrW(hListSb,GWLP_USERDATA,(LONG_PTR)hList); ShowScrollBar(hInput,SB_VERT,FALSE);
        DragAcceptFiles(h,TRUE); /* drop files anywhere on the window: sent to the open chat */
        { HWND tt=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,NULL,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,h,NULL,hInst,NULL);
          hTip=tt; SendMessageW(tt,TTM_SETDELAYTIME,TTDT_AUTOPOP,4000); /* hover-only, gone after 4 s or when the pointer leaves (TTF_SUBCLASS) */
          add_tip(tt,hSend,L"发送 (Enter)；Shift+Enter 换行"); add_tip(tt,hFile,L"发送文件（也可以把文件拖到窗口里）"); add_tip(tt,hHdrSearch,L"搜索 (Ctrl+F)"); add_tip(tt,hAdd,L"按 IP 添加联系人 (Ctrl+N)"); }
        SetTimer(h,TIMER_POLL,300,NULL);
        return 0; }
    case WM_DROPFILES: drop_files((HDROP)w); return 0;
    case WM_CONTEXTMENU: if((HWND)w==hList){ contact_menu(l); return 0; } break;
    case WM_SIZE: if(w!=SIZE_MINIMIZED){ if(hTip) SendMessageW(hTip,TTM_POP,0,0); layout(LOWORD(l),HIWORD(l)); if(C) refresh_chat(); fit_vscroll(hInput); InvalidateRect(h,NULL,TRUE); } else ShowWindow(h,SW_HIDE); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO*)l)->ptMinTrackSize.x=640; ((MINMAXINFO*)l)->ptMinTrackSize.y=420; return 0;
    case WM_SETFOCUS: SetFocus(hList); return 0; /* keyboard users land in the contact list */
    case WM_TIMER: if(!tray_added) tray_add(); poll_events(); ui_icons_dump(); test_drop(h); return 0;
    /* system colours only: readable in any theme incl. high-contrast */
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: case WM_CTLCOLORDLG: return ctl_color((HDC)w,(HWND)l,0);
    case WM_CTLCOLOREDIT: return ctl_color((HDC)w,(HWND)l,1);
    case WM_CTLCOLORLISTBOX: return ctl_color((HDC)w,(HWND)l,2);
    case WM_ERASEBKGND: { RECT r; GetClientRect(h,&r); FillRect((HDC)w,&r,brWin); return 1; }
    case WM_MEASUREITEM: { MEASUREITEMSTRUCT *mi=(MEASUREITEMSTRUCT*)l; if(mi->CtlType==ODT_MENU){ measure_menu(mi); return TRUE; } if(mi->CtlType==ODT_LISTBOX && mi->CtlID==IDC_CHAT){ measure_chat(mi); return TRUE; } if(mi->CtlType==ODT_LISTBOX){ mi->itemHeight=scale_px(56); return TRUE; } break; }
    case WM_DRAWITEM: { DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT*)l; if(d->CtlType==ODT_MENU){ draw_menu(d); return TRUE; } if(d->CtlType==ODT_BUTTON && d->CtlID>=IDC_MENUBTN && d->CtlID<IDC_MENUBTN+5){ draw_menubtn(d); return TRUE; } if(d->CtlType==ODT_STATIC && d->CtlID==IDC_TITLE){ draw_header(d); return TRUE; } if(d->CtlType==ODT_STATIC && d->CtlID==IDC_STATUS){ draw_status(d); return TRUE; } if(d->CtlType==ODT_BUTTON && (d->CtlID==IDC_SEND||d->CtlID==IDC_FILE||d->CtlID==IDC_ADD||d->CtlID==IDC_HDRSEARCH)){ draw_icon_button(d); return TRUE; } if(d->CtlType==ODT_LISTBOX && d->CtlID==IDC_CHAT){ draw_chat_item(d); return TRUE; } if(d->CtlType==ODT_LISTBOX && d->CtlID==IDC_LIST){ draw_conv_item(d); return TRUE; } if(d->CtlType==ODT_LISTBOX){ draw_list_item(d); return TRUE; } if(d->CtlType==ODT_BUTTON){ draw_button(d); return TRUE; } break; }
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps); { RECT lr, hb; GetWindowRect(hAdd,&lr); MapWindowPoints(NULL,h,(POINT*)&lr,2); lr.left=12; lr.right-=70; SetBkMode(dc,TRANSPARENT); SetTextColor(dc,T.text_primary); SelectObject(dc,fBodyStrong); DrawTextW(dc,L"联系人与群组",-1,&lr,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
          GetWindowRect(hTitle,&hb); MapWindowPoints(NULL,h,(POINT*)&hb,2); hb.right+=44; { HBRUSH b=CreateSolidBrush(P.header); FillRect(dc,&hb,b); DeleteObject(b); } { RECT f=hb; InflateRect(&f,1,1); f.bottom=hb.bottom+1; { HBRUSH dv=CreateSolidBrush(T.divider); FrameRect(dc,&f,dv); DeleteObject(dv); } } }
        frame_child(h,dc,hList); frame_field(h,dc,hSearch); frame_child(h,dc,hChat); frame_field(h,dc,hInput); { RECT cr; HBRUSH dv=CreateSolidBrush(T.divider); GetClientRect(h,&cr); cr.top=MENUBAR_H-1; cr.bottom=MENUBAR_H; FillRect(dc,&cr,dv); DeleteObject(dv); } EndPaint(h,&ps); return 0; }
    case WM_MENUCHAR: { HMENU mm=(HMENU)l; int i,n=GetMenuItemCount(mm); wchar_t ch=(wchar_t)towlower((wint_t)LOWORD(w)); /* mnemonics for owner-drawn items */
        for(i=0;i<n;i++){ MENUITEMINFOW ii; const wchar_t *t,*a; memset(&ii,0,sizeof ii); ii.cbSize=sizeof ii; ii.fMask=MIIM_DATA; if(!GetMenuItemInfoW(mm,i,TRUE,&ii)) continue; t=(const wchar_t*)ii.dwItemData; if(!t) continue; a=wcschr(t,L'&'); if(a && towlower(a[1])==ch) return MAKELRESULT(i,MNC_EXECUTE); }
        return MAKELRESULT(0,MNC_IGNORE); }
    case WM_COMMAND:
        if(LOWORD(w)==IDC_LIST && HIWORD(w)==LBN_SELCHANGE){ int s=(int)SendMessageW(hList,LB_GETCURSEL,0,0); if(s>=0 && s<nlist){ strcpy(cur_conv,list_ids[s]); refresh_chat(); if(lsm_mark_read(C,cur_conv)) refresh_list(); SetFocus(hInput); } return 0; }
        if(LOWORD(w)==IDC_SEARCH && HIWORD(w)==EN_CHANGE){ InvalidateRect(hSearch,NULL,TRUE); refresh_list(); return 0; }
        if(LOWORD(w)==IDC_INPUT && HIWORD(w)==EN_CHANGE){ InvalidateRect(hInput,NULL,TRUE); fit_vscroll(hInput); return 0; }
        if(LOWORD(w)>=IDC_MENUBTN && LOWORD(w)<IDC_MENUBTN+5){ open_menu(LOWORD(w)-IDC_MENUBTN); return 0; }
        if(LOWORD(w)==IDC_SEND){ send_current(); return 0; }
        if(LOWORD(w)==IDC_ADD){ do_command(IDM_ADDIP); return 0; }
        if(LOWORD(w)==IDC_HDRSEARCH){ SetFocus(hSearch); SendMessageW(hSearch,EM_SETSEL,0,-1); return 0; }
        if(LOWORD(w)==IDC_FILE){ send_file_dlg(); return 0; }
        do_command(LOWORD(w)); return 0;
    case WM_TRAY:
        if(l==WM_LBUTTONDBLCLK||l==NIN_BALLOONUSERCLICK) do_command(IDM_SHOW);
        else if(l==WM_RBUTTONUP){ POINT pt; HMENU pm=CreatePopupMenu(); AM(pm,MF_STRING,IDM_SHOW,L"显示主窗口"); AM(pm,MF_SEPARATOR,0,NULL); AM(pm,MF_STRING,IDM_EXIT,L"退出"); menu_bg(pm); GetCursorPos(&pt); SetForegroundWindow(h); TrackPopupMenu(pm,TPM_RIGHTBUTTON,pt.x,pt.y,0,h,NULL); DestroyMenu(pm); }
        return 0;
    case WM_CLOSE: win_save(); if(!tray_added) tray_add(); if(!tray_added){ DestroyWindow(h); return 0; } /* no notification area: close really exits (never hide with no way back) */
        ShowWindow(h,SW_HIDE); { static int told; if(!told){ told=1; tray_balloon("局域网安全通讯","程序仍在后台运行，双击托盘图标可打开。"); } } return 0;
    case WM_DESTROY: win_save(); KillTimer(h,TIMER_POLL); Shell_NotifyIconW(NIM_DELETE,&nid); PostQuitMessage(0); return 0;
    case WM_SYSCOLORCHANGE: theme_apply(); InvalidateRect(h,NULL,TRUE); return 0;
    }
    if(WM_TASKBARCREATED && m==WM_TASKBARCREATED){ tray_add(); return 0; }
    return DefWindowProcW(h,m,w,l); }

static int CALLBACK font_found(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD t, LPARAM l){ (void)lf; (void)tm; (void)t; (void)l; return 0; /* stop: exists */ }

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE prev, LPWSTR cmd, int show){
    int win_w=900, win_h=600, win_x=CW_USEDEFAULT, win_y=CW_USEDEFAULT; WNDCLASSEXW wc; MSG msg; lsm_opts o; char data[MAX_PATH*4], err[256]; INITCOMMONCONTROLSEX ic; wchar_t *sl, dw[MAX_PATH]; LOGFONTW lf; NONCLIENTMETRICSW ncm; HANDLE mtx;
    (void)prev; hInst=hi;
    mtx=CreateMutexW(NULL,TRUE,L"LanSecureMessengerXP_Single"); if(mtx && GetLastError()==ERROR_ALREADY_EXISTS && !wcsstr(cmd,L"--data")){ HWND o2=FindWindowW(L"LsmMain",NULL); if(o2){ AllowSetForegroundWindow(ASFW_ANY); PostMessageW(o2,WM_COMMAND,IDM_SHOW,0); } return 0; } /* the running copy shows itself (a cross-process ShowWindow of a hidden window did nothing under wine) */
    ic.dwSize=sizeof ic; ic.dwICC=ICC_BAR_CLASSES|ICC_STANDARD_CLASSES; InitCommonControlsEx(&ic);
    GetModuleFileNameW(NULL,exe_dir,MAX_PATH); sl=wcsrchr(exe_dir,L'\\'); if(sl) *sl=0;
    _snwprintf(dw,MAX_PATH,L"%s\\data",exe_dir); dw[MAX_PATH-1]=0;
    { int argc; wchar_t **av=CommandLineToArgvW(GetCommandLineW(),&argc); int i; memset(&o,0,sizeof o);
      static char nm[300], tg[1024];
      for(i=1;i<argc;i++){ if(!wcscmp(av[i],L"--data")&&i+1<argc) wcsncpy(dw,av[++i],MAX_PATH-1); else if(!wcscmp(av[i],L"--name")&&i+1<argc){ U(av[++i],nm,sizeof nm); o.name=nm; } else if(!wcscmp(av[i],L"--theme")&&i+1<argc){ i++; theme_cli=!wcscmp(av[i],L"dark")?2:!wcscmp(av[i],L"light")?1:0; } else if(!wcscmp(av[i],L"--auto-trust")) o.auto_trust=1; else if(!wcscmp(av[i],L"--port")&&i+1<argc) o.listen_port=_wtoi(av[++i]); else if(!wcscmp(av[i],L"--disco-port")&&i+1<argc) o.discovery_port=_wtoi(av[++i]); else if(!wcscmp(av[i],L"--targets")&&i+1<argc){ U(av[++i],tg,sizeof tg); o.announce_targets=tg; } } }
    U(dw,data,sizeof data); o.data_dir=data;
    if(GetEnvironmentVariableA("LSM_UI_TEST",NULL,0)) snprintf(ui_icons_path,sizeof ui_icons_path,"%s\\ui_icons.txt",data);
    if(!o.name){ static char cn[64]; wchar_t wn[64]; DWORD n=64; if(GetComputerNameW(wn,&n)){ U(wn,cn,sizeof cn); o.name=cn; } }
    (void)lf; (void)ncm;
    /* UI face: Microsoft YaHei UI (Windows Vista+ zh UI font), SimSun on XP; WenQuanYi/Noto only on wine/ReactOS test boxes.
     * Sizes: Fluent type ramp caption 12 / body 14 / body_strong 14 semibold / subtitle 20 / title 28 px. */
    { static const wchar_t *faces[]={L"Microsoft YaHei UI",L"Microsoft YaHei",L"SimSun",L"\x5b8b\x4f53",L"NSimSun",L"Noto Sans CJK SC",L"WenQuanYi Zen Hei",L"Tahoma",NULL}; const wchar_t *face=L"Tahoma"; int i; HDC dc=GetDC(NULL);
      for(i=0;faces[i];i++){ LOGFONTW q; memset(&q,0,sizeof q); q.lfCharSet=DEFAULT_CHARSET; wcsncpy(q.lfFaceName,faces[i],31);
        if(!EnumFontFamiliesExW(dc,&q,(FONTENUMPROCW)font_found,0,0)){ face=faces[i]; break; } }
      ReleaseDC(NULL,dc);
      fCaption=mkfont(face,12,FW_NORMAL); fCaptionStrong=mkfont(face,12,FW_BOLD); fBody=mkfont(face,14,FW_NORMAL); fBodyStrong=mkfont(face,14,FW_SEMIBOLD); fSubtitle=mkfont(face,20,FW_SEMIBOLD); fTitle=mkfont(face,28,FW_SEMIBOLD);
      hFont=fBody; hFontBold=fBodyStrong; }
    theme_load(); if(theme_cli>=0) theme_mode=theme_cli; theme_apply();
    C=lsm_start(&o,err,sizeof err);
    if(!C){ MessageBoxW(NULL,W(err),L"启动失败",MB_ICONSTOP); return 1; }
    memset(&wc,0,sizeof wc); wc.cbSize=sizeof wc; wc.lpfnWndProc=MainProc; wc.hInstance=hi; wc.hIcon=LoadIconW(hi,MAKEINTRESOURCEW(1)); wc.hIconSm=wc.hIcon; wc.hCursor=LoadCursor(NULL,IDC_ARROW); wc.hbrBackground=NULL; /* WM_ERASEBKGND paints window_bg */ wc.lpszClassName=L"LsmMain";
    RegisterClassExW(&wc);
    wc.lpfnWndProc=ScrollProc; wc.lpszClassName=L"LsmScroll"; RegisterClassExW(&wc);
    wc.lpfnWndProc=RelayProc; wc.lpszClassName=L"LsmRelay"; wc.style|=0x00020000; RegisterClassExW(&wc); wc.style&=~0x00020000;
    wc.lpfnWndProc=PromptProc; wc.lpszClassName=L"LsmPrompt"; wc.style|=0x00020000 /* CS_DROPSHADOW (XP+): lifts the borderless dialog off a same-colour page */; wc.hIcon=NULL; wc.hIconSm=NULL; RegisterClassExW(&wc);
    { RECT wa; if(SystemParametersInfoW(SPI_GETWORKAREA,0,&wa,0)){ if(win_w>wa.right-wa.left) win_w=wa.right-wa.left; if(win_h>wa.bottom-wa.top) win_h=wa.bottom-wa.top;
        win_x=wa.left+((wa.right-wa.left)-win_w)/2; win_y=wa.top+((wa.bottom-wa.top)-win_h)/2; } } /* 800x600 XP screens: fit and centre (CW_USEDEFAULT pushed it off-screen) */
    hMenuTree=build_menu(); /* not attached: the system menu bar ignores the app theme (wine/XP paint it with COLOR_MENU) */
    { RECT wr; int mx=0; if(win_load(&wr,&mx)){ win_x=wr.left; win_y=wr.top; win_w=wr.right-wr.left; win_h=wr.bottom-wr.top; if(mx && show==SW_SHOWNORMAL) show=SW_SHOWMAXIMIZED; } } /* window memory */
    hMain=CreateWindowExW(0,L"LsmMain",L"局域网安全通讯 XP 版",WS_OVERLAPPEDWINDOW,win_x,win_y,win_w,win_h,NULL,NULL,hi,NULL);
    memset(&nid,0,sizeof nid); nid.cbSize=NOTIFYICONDATAW_V2_SIZE; nid.hWnd=hMain; nid.uID=1; nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP; nid.uCallbackMessage=WM_TRAY; nid.hIcon=wc.hIcon?wc.hIcon:LoadIcon(NULL,IDI_APPLICATION);
    { HICON ic2=LoadIconW(hi,MAKEINTRESOURCEW(1)); if(ic2) nid.hIcon=ic2; }
    wcscpy(nid.szTip,L"局域网安全通讯 XP 版"); WM_TASKBARCREATED=RegisterWindowMessageW(L"TaskbarCreated"); tray_add();
    ShowWindow(hMain,show); UpdateWindow(hMain);
    refresh_list(); refresh_chat();
    while(GetMessageW(&msg,NULL,0,0)>0){ if(hRelayDlg && IsDialogMessageW(hRelayDlg,&msg)) continue; if(app_key(&msg)) continue; if(IsDialogMessageW(hMain,&msg)) continue; /* Tab / Shift+Tab / Alt+mnemonics between controls */ TranslateMessage(&msg); DispatchMessageW(&msg); }
    lsm_stop(C);
    return 0; }
