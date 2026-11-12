/* v0.7.0 (XP / Win32): dedicated in-window pages + chat-list actions on the existing right-click menu. */
#define IDM_V070_CONTACTS  470
#define IDM_V070_FILES     471
#define IDM_V070_MARKREAD  472
#define IDM_V070_MARKUNREAD 473
#define IDM_V070_CHATS     474
#define IDM_V070_GROUPS    475
#define IDM_V070_PROFILE   476
#define IDM_V070_DIAG      477
#define IDM_V070_SETTINGS  478
#define IDM_V070_TRANSFERS 479
/* hooks (must appear in this UI file):
 * LSMF_nav_chats LSMF_nav_contacts LSMF_nav_groups LSMF_nav_transfers LSMF_nav_files
 * LSMF_nav_settings LSMF_nav_profile LSMF_nav_diagnostics
 * LSMF_chat_delete LSMF_chat_pin LSMF_chat_mark_read LSMF_chat_mark_unread LSMF_chat_mute
 * LSMF_chat_list_menu
 */
static HWND hV070Page;
static char v070_page[32]="chats";
static void v070_page_text(wchar_t *out, int n){
    char info[2048]="", name[128]="", fp[80]="";
    if(!strcmp(v070_page,"contacts")){ /* LSMF_nav_contacts */
        lsm_contact *k; int nc,i; char b[1800]="联系人\r\n\r\n"; lsm_lock(C); nc=lsm_contacts(C,&k);
        for(i=0;i<nc && strlen(b)<1600;i++){ char line[200]; snprintf(line,sizeof line,"· %s  %s  %s\r\n",lsm_display_name(&k[i]),k[i].trust==TRUST_TRUSTED?"已信任":"未信任",k[i].online?"在线":"离线"); strncat(b,line,sizeof b-strlen(b)-1); }
        lsm_unlock(C); MultiByteToWideChar(CP_UTF8,0,b,-1,out,n); return; }
    if(!strcmp(v070_page,"files")){ /* LSMF_nav_files */
        lsm_storage_info(C,info,sizeof info); { char b[2200]; snprintf(b,sizeof b,"文件站\r\n\r\n%s",info[0]?info:"（存储信息不可用）"); MultiByteToWideChar(CP_UTF8,0,b,-1,out,n); } return; }
    if(!strcmp(v070_page,"profile")){ /* LSMF_nav_profile */
        lsm_lock(C); snprintf(name,sizeof name,"%s",lsm_my_name(C)); snprintf(fp,sizeof fp,"%s",lsm_my_fp(C)); lsm_unlock(C);
        { char b[400]; snprintf(b,sizeof b,"个人资料\r\n\r\n显示名称：%s\r\n身份指纹：%s\r\n版本 %s",name,fp,LSM_APP_VERSION); MultiByteToWideChar(CP_UTF8,0,b,-1,out,n); } return; }
    if(!strcmp(v070_page,"diagnostics")){ /* LSMF_nav_diagnostics */
        size_t need=lsm_diagnostics(C,NULL,0); char *b=(char*)malloc(need+1); if(!b){ out[0]=0; return; } lsm_diagnostics(C,b,need+1);
        MultiByteToWideChar(CP_UTF8,0,b,-1,out,n); free(b); return; }
    if(!strcmp(v070_page,"groups")){ MultiByteToWideChar(CP_UTF8,0,"群组\r\n\r\n从「群组」菜单创建或管理群组。聊天列表中的群也可以直接打开。",-1,out,n); return; } /* LSMF_nav_groups */
    if(!strcmp(v070_page,"transfers")){ MultiByteToWideChar(CP_UTF8,0,"文件传输\r\n\r\n进行中的发送和接收请通过「文件 → 文件传输」查看。",-1,out,n); return; } /* LSMF_nav_transfers */
    if(!strcmp(v070_page,"settings")){ MultiByteToWideChar(CP_UTF8,0,"设置\r\n\r\n外观、中转服务器、个人资料等仍可从顶部「设置」菜单打开。",-1,out,n); return; } /* LSMF_nav_settings */
    MultiByteToWideChar(CP_UTF8,0,"聊天",-1,out,n); /* LSMF_nav_chats */
}
static void v070_relayout(void){ RECT rc; if(!hMain) return; GetClientRect(hMain,&rc); layout(rc.right,rc.bottom); refresh_caption(); }
/* Raise the page over owner-draw chrome (MoveWindow does not change z-order, and
   wine may paint later-moved siblings on top). Hide those controls so the chat
   header / composer / 重连 cannot peek through. */
static void v070_place_overlay(int T0, int W_, int H_, int sb){
    HWND chrome[]={hReconnect,hAdd,hSearch,hList,hTitle,hHdrSearch,hChat,hFile,hInput,hMic,hSend,hChatSb,hListSb};
    HWND show[]={hReconnect,hAdd,hSearch,hList,hTitle,hChat,hFile,hInput,hMic,hSend};
    int i, on=hV070Page && IsWindowVisible(hV070Page) && strcmp(v070_page,"chats");
    if(on){
        for(i=0;i<(int)(sizeof chrome/sizeof chrome[0]);i++) if(chrome[i]) ShowWindow(chrome[i],SW_HIDE);
        SetWindowPos(hV070Page,HWND_TOP,0,T0,W_,H_-sb,SWP_SHOWWINDOW);
        ShowScrollBar(hV070Page,SB_BOTH,FALSE);
    }else{
        for(i=0;i<(int)(sizeof show/sizeof show[0]);i++) if(show[i]) ShowWindow(show[i],SW_SHOW);
    }
}
static void v070_show_page(const char *name){
    wchar_t w[4000]; snprintf(v070_page,sizeof v070_page,"%s",name);
    if(!strcmp(name,"chats")){ if(hV070Page) ShowWindow(hV070Page,SW_HIDE); v070_relayout(); return; }
    if(!hV070Page) return;
    v070_page_text(w,4000); SetWindowTextW(hV070Page,w); ShowWindow(hV070Page,SW_SHOW); ShowScrollBar(hV070Page,SB_BOTH,FALSE); BringWindowToTop(hV070Page); v070_relayout();
}
static void v070_on_command(int id){
    if(id==IDM_V070_CHATS){ if(hV070Page) ShowWindow(hV070Page,SW_HIDE); snprintf(v070_page,sizeof v070_page,"chats"); v070_relayout(); }
    else if(id==IDM_V070_CONTACTS) v070_show_page("contacts");
    else if(id==IDM_V070_GROUPS) v070_show_page("groups");
    else if(id==IDM_V070_FILES) v070_show_page("files");
    else if(id==IDM_V070_PROFILE) v070_show_page("profile");
    else if(id==IDM_V070_DIAG) v070_show_page("diagnostics");
    else if(id==IDM_V070_SETTINGS) v070_show_page("settings");
    else if(id==IDM_V070_TRANSFERS) v070_show_page("transfers");
    else if(id==IDM_V070_MARKREAD && cur_conv[0]){ lsm_mark_read(C,cur_conv); refresh_list(); refresh_chat(); } /* LSMF_chat_mark_read */
    else if(id==IDM_V070_MARKUNREAD && cur_conv[0]){ lsm_mark_unread(C,cur_conv); refresh_list(); } /* LSMF_chat_mark_unread */
}
static void v070_extend_contact_menu(HMENU pm){ (void)pm; /* mark-read/unread nested in contact_menu (v0.7.2 ≤5) LSMF_chat_mark_read LSMF_chat_mark_unread */ }
static void v070_extend_menus(HMENU filem, HMENU contactsm, HMENU settingsm){ (void)filem;(void)contactsm;(void)settingsm; /* pages now nested in build_menu (v0.7.2 ≤5) */ }
