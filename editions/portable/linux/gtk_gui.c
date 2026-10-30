/* 局域网安全通讯 Linux 版 — GTK 3 front end over the portable C core (src/core.c).
 * GTK 3 (not 4) on purpose: it is present on every desktop distro still in support
 * (Ubuntu 20.04+, Debian 11+, RHEL/Alma 8+, openSUSE Leap 15), and AppIndicator only has a GTK 3 API.
 * All colours come from the active theme (named colours in CSS), so light, dark and high-contrast
 * themes stay readable. Theme preference follows the desktop: XDG portal color-scheme, then GNOME
 * gsettings, then the GTK theme name. */
#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../src/core.h"
#include "upd_gio.h"

#define APP_ID "io.github.frec86.LanMessenger"
static lsm_core *C;
static GtkApplication *app;
static GtkWidget *win, *peer_list, *chat_box, *chat_scroll, *title_lbl, *sub_lbl, *input, *status_lbl, *stack, *drop_hint;
static char cur_conv[64];
static int list_dirty, chat_dirty;

/* ---------------------------------------------------------------- helpers */
static void msgbox(GtkMessageType t, const char *fmt, ...){ va_list ap; char b[2048]; va_start(ap,fmt); vsnprintf(b,sizeof b,fmt,ap); va_end(ap);
    GtkWidget *d=gtk_message_dialog_new(GTK_WINDOW(win),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,t,GTK_BUTTONS_OK,"%s",b);
    gtk_window_set_title(GTK_WINDOW(d),"局域网安全通讯"); gtk_dialog_run(GTK_DIALOG(d)); gtk_widget_destroy(d); }
static void notify(const char *title, const char *body){
    GNotification *n=g_notification_new(title); g_notification_set_body(n,body);
    g_notification_set_icon(n,G_ICON(g_themed_icon_new("mail-message-new")));
    g_application_send_notification(G_APPLICATION(app),"msg",n); g_object_unref(n); }
static int is_image(const char *name){ const char *e=strrchr(name?name:"",'.'); if(!e) return 0; e++;
    return !g_ascii_strcasecmp(e,"png")||!g_ascii_strcasecmp(e,"jpg")||!g_ascii_strcasecmp(e,"jpeg")||!g_ascii_strcasecmp(e,"gif")||!g_ascii_strcasecmp(e,"bmp")||!g_ascii_strcasecmp(e,"webp"); }
static const char *status_txt(int s){ switch(s){ case ST_QUEUED: return "排队中"; case ST_SENT: return "已发送"; case ST_DELIVERED: return "已送达"; case ST_READ: return "已读"; case ST_FAILED: return "发送失败"; } return ""; }
static const char *err_zh(const char *e){ /* core keeps a few wire/OS reason codes in English */
    if(!e||!*e) return ""; if(!strcmp(e,"untrusted")) return "未信任的联系人，已拒收"; if(!strcmp(e,"open failed")) return "无法打开文件";
    if(!strcmp(e,"rename failed")) return "无法保存文件"; if(!strcmp(e,"write failed")) return "写入失败（磁盘已满？）"; return e; }
static char *ask_text(const char *title, const char *label, const char *init){
    GtkWidget *d=gtk_dialog_new_with_buttons(title,GTK_WINDOW(win),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"取消",GTK_RESPONSE_CANCEL,"确定",GTK_RESPONSE_OK,NULL);
    GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(d)), *l=gtk_label_new(label), *e=gtk_entry_new(); char *r=NULL;
    gtk_container_set_border_width(GTK_CONTAINER(box),14); gtk_box_set_spacing(GTK_BOX(box),10); gtk_label_set_xalign(GTK_LABEL(l),0); gtk_label_set_line_wrap(GTK_LABEL(l),TRUE);
    gtk_entry_set_text(GTK_ENTRY(e),init?init:""); gtk_entry_set_activates_default(GTK_ENTRY(e),TRUE); gtk_dialog_set_default_response(GTK_DIALOG(d),GTK_RESPONSE_OK);
    gtk_box_pack_start(GTK_BOX(box),l,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),e,FALSE,FALSE,0); gtk_window_set_default_size(GTK_WINDOW(d),420,-1); gtk_widget_show_all(d);
    if(gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_OK) r=g_strdup(gtk_entry_get_text(GTK_ENTRY(e)));
    gtk_widget_destroy(d); return r; }
/* multi/single select of contacts; returns count, fills out[] */
static int pick_peers(const char *title, const char *label, int trusted_only, int multi, char out[64][FP_LEN], char **name_out, const char *name_init){
    GtkWidget *d=gtk_dialog_new_with_buttons(title,GTK_WINDOW(win),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"取消",GTK_RESPONSE_CANCEL,"确定",GTK_RESPONSE_OK,NULL);
    GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(d)), *l=gtk_label_new(label), *lb=gtk_list_box_new(), *sc=gtk_scrolled_window_new(NULL,NULL), *e=NULL;
    lsm_contact *k; int n,i,cnt=0,res=-1; char fps[64][FP_LEN];
    gtk_container_set_border_width(GTK_CONTAINER(box),14); gtk_box_set_spacing(GTK_BOX(box),10); gtk_label_set_xalign(GTK_LABEL(l),0);
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(lb),multi?GTK_SELECTION_MULTIPLE:GTK_SELECTION_SINGLE); gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(lb),FALSE);
    lsm_lock(C); n=lsm_contacts(C,&k);
    for(i=0;i<n&&cnt<64;i++){ char t[400]; GtkWidget *row; if(trusted_only&&k[i].trust!=TRUST_TRUSTED) continue; snprintf(t,sizeof t,"%s  (%s)",k[i].name,k[i].fp);
        row=gtk_label_new(t); gtk_label_set_xalign(GTK_LABEL(row),0); gtk_widget_set_margin_start(row,8); gtk_widget_set_margin_top(row,6); gtk_widget_set_margin_bottom(row,6);
        gtk_list_box_insert(GTK_LIST_BOX(lb),row,-1); strcpy(fps[cnt++],k[i].fp); }
    lsm_unlock(C);
    if(!cnt){ gtk_widget_destroy(d); msgbox(GTK_MESSAGE_INFO,trusted_only?"没有已信任的联系人。请先信任对方（或使用 PIN 配对）。":"没有联系人。"); return -1; }
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(sc),200); gtk_container_add(GTK_CONTAINER(sc),lb);
    gtk_box_pack_start(GTK_BOX(box),l,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),sc,TRUE,TRUE,0);
    if(name_out){ e=gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(e),name_init?name_init:""); gtk_entry_set_placeholder_text(GTK_ENTRY(e),"群名"); gtk_box_pack_start(GTK_BOX(box),e,FALSE,FALSE,0); }
    gtk_window_set_default_size(GTK_WINDOW(d),460,380); gtk_widget_show_all(d);
    if(gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_OK){ GList *s=gtk_list_box_get_selected_rows(GTK_LIST_BOX(lb)), *it; res=0;
        for(it=s;it;it=it->next) strcpy(out[res++],fps[gtk_list_box_row_get_index(GTK_LIST_BOX_ROW(it->data))]); g_list_free(s);
        if(name_out) *name_out=g_strdup(gtk_entry_get_text(GTK_ENTRY(e))); }
    gtk_widget_destroy(d); return res; }

/* ---------------------------------------------------------------- views */
static void refresh_list(void){
    GList *ch=gtk_container_get_children(GTK_CONTAINER(peer_list)), *it; lsm_contact *k; lsm_group *g; int n,i; GtkListBoxRow *sel=NULL;
    for(it=ch;it;it=it->next) gtk_widget_destroy(GTK_WIDGET(it->data)); g_list_free(ch);
    lsm_lock(C);
    n=lsm_contacts(C,&k);
    for(i=0;i<n;i++){ char conv[64], t[700], esc_n[700]; lsm_msg *m; int nm,j,unread=0; GtkWidget *row=gtk_list_box_row_new(), *h=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8), *dot=gtk_label_new(k[i].online?"●":"○"), *lab=gtk_label_new(NULL);
        snprintf(conv,64,"dm:%s",k[i].fp); nm=lsm_messages(C,&m); for(j=0;j<nm;j++) if(m[j].unread&&!strcmp(m[j].conv,conv)) unread++;
        { gchar *e=g_markup_escape_text(k[i].name,-1); snprintf(esc_n,sizeof esc_n,"%s",e); g_free(e); }
        snprintf(t,sizeof t,"<b>%s</b>%s\n<small>%s · %s</small>",esc_n,unread?"  •":"",k[i].trust==TRUST_TRUSTED?"已信任":k[i].trust==TRUST_BLOCKED?"已屏蔽":"未信任",k[i].online?"在线":"离线");
        gtk_label_set_markup(GTK_LABEL(lab),t); gtk_label_set_xalign(GTK_LABEL(lab),0); gtk_label_set_ellipsize(GTK_LABEL(lab),PANGO_ELLIPSIZE_END);
        gtk_style_context_add_class(gtk_widget_get_style_context(dot),k[i].online?"online":"offline");
        gtk_box_pack_start(GTK_BOX(h),dot,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(h),lab,TRUE,TRUE,0); gtk_widget_set_margin_start(h,10); gtk_widget_set_margin_end(h,10); gtk_widget_set_margin_top(h,6); gtk_widget_set_margin_bottom(h,6);
        gtk_container_add(GTK_CONTAINER(row),h); g_object_set_data_full(G_OBJECT(row),"conv",g_strdup(conv),g_free); gtk_list_box_insert(GTK_LIST_BOX(peer_list),row,-1);
        if(!strcmp(conv,cur_conv)) sel=GTK_LIST_BOX_ROW(row); }
    n=lsm_groups(C,&g);
    for(i=0;i<n;i++){ char u[37], conv[64], t[700]; GtkWidget *row=gtk_list_box_row_new(), *h=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8), *dot=gtk_label_new("#"), *lab=gtk_label_new(NULL); gchar *e;
        lsm_uuid_str(g[i].id,u); snprintf(conv,64,"group:%s",u); e=g_markup_escape_text(g[i].name,-1); snprintf(t,sizeof t,"<b>%s</b>\n<small>群组 · %d 人</small>",e,g[i].nmembers); g_free(e);
        gtk_label_set_markup(GTK_LABEL(lab),t); gtk_label_set_xalign(GTK_LABEL(lab),0); gtk_label_set_ellipsize(GTK_LABEL(lab),PANGO_ELLIPSIZE_END);
        gtk_box_pack_start(GTK_BOX(h),dot,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(h),lab,TRUE,TRUE,0); gtk_widget_set_margin_start(h,10); gtk_widget_set_margin_end(h,10); gtk_widget_set_margin_top(h,6); gtk_widget_set_margin_bottom(h,6);
        gtk_container_add(GTK_CONTAINER(row),h); g_object_set_data_full(G_OBJECT(row),"conv",g_strdup(conv),g_free); gtk_list_box_insert(GTK_LIST_BOX(peer_list),row,-1);
        if(!strcmp(conv,cur_conv)) sel=GTK_LIST_BOX_ROW(row); }
    { char st[600]; snprintf(st,sizeof st,"我：%s    指纹 %s    TCP %d    版本 %s",lsm_my_name(C),lsm_my_fp(C),lsm_listen_port(C),LSM_APP_VERSION); gtk_label_set_text(GTK_LABEL(status_lbl),st); }
    lsm_unlock(C);
    gtk_widget_show_all(peer_list);
    if(sel){ g_signal_handlers_block_matched(peer_list,G_SIGNAL_MATCH_DATA,0,0,NULL,NULL,(gpointer)1); gtk_list_box_select_row(GTK_LIST_BOX(peer_list),sel); g_signal_handlers_unblock_matched(peer_list,G_SIGNAL_MATCH_DATA,0,0,NULL,NULL,(gpointer)1); } }

static gboolean open_file_cb(GtkWidget *w, GdkEventButton *ev, gpointer p){ (void)w; (void)ev; gchar *uri=g_filename_to_uri((const char*)p,NULL,NULL); if(uri){ g_app_info_launch_default_for_uri(uri,NULL,NULL); g_free(uri);} return TRUE; }
/* destructive actions are confirmed; the destructive button is styled and Cancel is the default */
static int confirm(const char *title, const char *body, const char *ok){
    GtkWidget *d=gtk_message_dialog_new(GTK_WINDOW(win),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,GTK_MESSAGE_QUESTION,GTK_BUTTONS_NONE,"%s",title), *b; int r;
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d),"%s",body); gtk_dialog_add_button(GTK_DIALOG(d),"取消",GTK_RESPONSE_CANCEL);
    b=gtk_dialog_add_button(GTK_DIALOG(d),ok,GTK_RESPONSE_OK); gtk_style_context_add_class(gtk_widget_get_style_context(b),"destructive-action");
    gtk_dialog_set_default_response(GTK_DIALOG(d),GTK_RESPONSE_CANCEL); gtk_window_set_title(GTK_WINDOW(d),"局域网安全通讯");
    r=gtk_dialog_run(GTK_DIALOG(d)); gtk_widget_destroy(d); return r==GTK_RESPONSE_OK; }
static void refresh_chat(void);
/* 删除这条消息（仅本机）: runs from idle so the bubble that owns the menu is not destroyed inside its own signal */
static gboolean delete_msg_idle(gpointer id){
    if(confirm("删除这条消息？","只会从本机删除，对方仍保留这条消息。此操作无法撤销。","删除")){ if(lsm_delete_message(C,(const char*)id)) msgbox(GTK_MESSAGE_WARNING,"删除失败：消息不存在。"); refresh_chat(); }
    g_free(id); return G_SOURCE_REMOVE; }
static void delete_msg_cb(GtkMenuItem *mi_, gpointer id){ (void)mi_; g_idle_add(delete_msg_idle,g_strdup((const char*)id)); }
static void add_delete_item(GtkMenuShell *menu, const char *id){ GtkWidget *it=gtk_menu_item_new_with_mnemonic("删除这条消息（仅本机）(_D)…");
    g_signal_connect_data(it,"activate",G_CALLBACK(delete_msg_cb),g_strdup(id),(GClosureNotify)g_free,0); gtk_menu_shell_append(menu,gtk_separator_menu_item_new()); gtk_menu_shell_append(menu,it); gtk_widget_show_all(GTK_WIDGET(menu)); }
/* selectable text labels have their own 复制/全选 menu: extend it */
static void label_popup_cb(GtkLabel *l, GtkMenu *menu, gpointer id){ (void)l; add_delete_item(GTK_MENU_SHELL(menu),(const char*)id); }
/* file / image bubbles: right-click (or long-press on touch) opens a small menu */
static gboolean bubble_press_cb(GtkWidget *w, GdkEventButton *ev, gpointer id){ GtkWidget *menu;
    if(ev->type!=GDK_BUTTON_PRESS || ev->button!=3) return FALSE;
    menu=gtk_menu_new(); add_delete_item(GTK_MENU_SHELL(menu),(const char*)id); gtk_menu_attach_to_widget(GTK_MENU(menu),w,NULL);
    g_signal_connect(menu,"selection-done",G_CALLBACK(gtk_widget_destroy),NULL); gtk_menu_popup_at_pointer(GTK_MENU(menu),(GdkEvent*)ev); return TRUE; }
static void scroll_bottom(void){ GtkAdjustment *a=gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(chat_scroll)); gtk_adjustment_set_value(a,gtk_adjustment_get_upper(a)); }
static gboolean scroll_later(gpointer p){ (void)p; scroll_bottom(); return G_SOURCE_REMOVE; }

static void refresh_chat(void){
    GList *ch=gtk_container_get_children(GTK_CONTAINER(chat_box)), *it; lsm_msg *m; int n,i; char title[600]="", sub[600]="";
    for(it=ch;it;it=it->next) gtk_widget_destroy(GTK_WIDGET(it->data)); g_list_free(ch);
    if(!cur_conv[0]){ gtk_stack_set_visible_child_name(GTK_STACK(stack),"empty"); gtk_label_set_text(GTK_LABEL(title_lbl),"局域网安全通讯"); gtk_label_set_text(GTK_LABEL(sub_lbl),"从左侧选择联系人或群组"); return; }
    gtk_stack_set_visible_child_name(GTK_STACK(stack),"chat");
    lsm_lock(C);
    if(!strncmp(cur_conv,"dm:",3)){ lsm_contact *k; int nk=lsm_contacts(C,&k),j; for(j=0;j<nk;j++) if(!strcmp(k[j].fp,cur_conv+3)){ snprintf(title,sizeof title,"%s",k[j].name); snprintf(sub,sizeof sub,"%s · %s%s%s · %s",k[j].fp,k[j].online?"在线":"离线",k[j].app_version[0]?" · ":"",k[j].app_version,k[j].trust==TRUST_TRUSTED?"已信任":k[j].trust==TRUST_BLOCKED?"已屏蔽":"未信任（可在菜单中信任或 PIN 配对）"); } }
    else { lsm_group *g; int ng=lsm_groups(C,&g),j; char u[37]; for(j=0;j<ng;j++){ lsm_uuid_str(g[j].id,u); if(!strcmp(u,cur_conv+6)){ snprintf(title,sizeof title,"群组「%s」",g[j].name); snprintf(sub,sizeof sub,"%d 人 · 密钥版本 %llu",g[j].nmembers,(unsigned long long)g[j].key_epoch); } } }
    n=lsm_messages(C,&m);
    for(i=0;i<n;i++){ GtkWidget *row, *bub, *meta, *body; char ts[32]="", mt[700]; time_t t=(time_t)(m[i].ts_ms/1000); struct tm tmv; if(strcmp(m[i].conv,cur_conv)) continue;
        localtime_r(&t,&tmv); strftime(ts,sizeof ts,"%m-%d %H:%M",&tmv);
        if(m[i].kind==K_SYSTEM){ char b[1200]; snprintf(b,sizeof b,"%s · %s",ts,m[i].body?m[i].body:""); body=gtk_label_new(b); gtk_label_set_line_wrap(GTK_LABEL(body),TRUE); gtk_style_context_add_class(gtk_widget_get_style_context(body),"sysmsg"); gtk_widget_set_halign(body,GTK_ALIGN_CENTER); gtk_box_pack_start(GTK_BOX(chat_box),body,FALSE,FALSE,0); continue; }
        row=gtk_box_new(GTK_ORIENTATION_VERTICAL,2); bub=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);
        gtk_style_context_add_class(gtk_widget_get_style_context(bub),"bubble"); gtk_style_context_add_class(gtk_widget_get_style_context(bub),m[i].out?"out":"in");
        { gchar *e=g_markup_escape_text(m[i].out?"我":m[i].sender_name,-1); snprintf(mt,sizeof mt,"<small>%s · %s%s%s</small>",e,ts,m[i].out?" · ":"",m[i].out?status_txt(m[i].status):""); g_free(e); }
        meta=gtk_label_new(NULL); gtk_label_set_markup(GTK_LABEL(meta),mt); gtk_style_context_add_class(gtk_widget_get_style_context(meta),"meta"); gtk_widget_set_halign(meta,m[i].out?GTK_ALIGN_END:GTK_ALIGN_START);
        if(m[i].kind==K_FILE){ lsm_transfer *tr; int nt=lsm_transfers(C,&tr),j; char fs[200]="", line[800]; const char *path=NULL; static const char *TS[]={"等待接收","传输中","校验中","完成","失败","被拒收","已取消"};
            for(j=0;j<nt;j++) if(!strcmp(tr[j].msg_id,m[i].id)){ snprintf(fs,sizeof fs,"%s%s",TS[tr[j].state],tr[j].error[0]?" ":""); if(tr[j].error[0]) strncat(fs,err_zh(tr[j].error),sizeof fs-strlen(fs)-1);
                if(tr[j].state==TS_ACTIVE && tr[j].size){ char p[32]; snprintf(p,sizeof p," %d%%",(int)(tr[j].transferred*100/tr[j].size)); strncat(fs,p,sizeof fs-strlen(fs)-1);} if(tr[j].state==TS_DONE||tr[j].out) path=tr[j].path; }
            if(!path && m[i].file_path[0]){ path=m[i].file_path; if(!fs[0] && !m[i].out && access(path,R_OK)==0) snprintf(fs,sizeof fs,"已保存"); }
            snprintf(line,sizeof line,"[文件] %s  (%.1f KB)  %s",m[i].file_name,m[i].file_size/1024.0,fs);
            body=gtk_label_new(line); gtk_label_set_line_wrap(GTK_LABEL(body),TRUE); gtk_label_set_xalign(GTK_LABEL(body),0); gtk_box_pack_start(GTK_BOX(bub),body,FALSE,FALSE,0);
            if(path && is_image(m[i].file_name) && access(path,R_OK)==0){ GError *er=NULL; GdkPixbuf *pb=gdk_pixbuf_new_from_file_at_scale(path,260,200,TRUE,&er);
                if(pb){ GtkWidget *img=gtk_image_new_from_pixbuf(pb), *ev=gtk_event_box_new(); g_object_unref(pb); gtk_container_add(GTK_CONTAINER(ev),img); gtk_widget_set_halign(ev,GTK_ALIGN_START);
                    gtk_widget_set_tooltip_text(ev,"点击用默认程序打开"); g_signal_connect_data(ev,"button-press-event",G_CALLBACK(open_file_cb),g_strdup(path),(GClosureNotify)g_free,0); gtk_box_pack_start(GTK_BOX(bub),ev,FALSE,FALSE,0); }
                else if(er) g_error_free(er); } }
        else { body=gtk_label_new(m[i].body?m[i].body:""); gtk_label_set_line_wrap(GTK_LABEL(body),TRUE); gtk_label_set_line_wrap_mode(GTK_LABEL(body),PANGO_WRAP_WORD_CHAR); gtk_label_set_selectable(GTK_LABEL(body),TRUE); gtk_label_set_xalign(GTK_LABEL(body),0); gtk_label_set_max_width_chars(GTK_LABEL(body),48); gtk_box_pack_start(GTK_BOX(bub),body,FALSE,FALSE,0);
            g_signal_connect_data(body,"populate-popup",G_CALLBACK(label_popup_cb),g_strdup(m[i].id),(GClosureNotify)g_free,0); }
        gtk_widget_set_halign(bub,m[i].out?GTK_ALIGN_END:GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(row),meta,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(row),bub,FALSE,FALSE,0);
        { GtkWidget *eb=gtk_event_box_new(); gtk_event_box_set_visible_window(GTK_EVENT_BOX(eb),FALSE); gtk_container_add(GTK_CONTAINER(eb),row);
          g_signal_connect_data(eb,"button-press-event",G_CALLBACK(bubble_press_cb),g_strdup(m[i].id),(GClosureNotify)g_free,0); gtk_box_pack_start(GTK_BOX(chat_box),eb,FALSE,FALSE,0); } }
    lsm_unlock(C);
    gtk_label_set_text(GTK_LABEL(title_lbl),title); gtk_label_set_text(GTK_LABEL(sub_lbl),sub);
    gtk_widget_show_all(chat_box); g_idle_add(scroll_later,NULL); }

/* ---------------------------------------------------------------- tray (AppIndicator via dlopen, GtkStatusIcon fallback) */
typedef void *(*ai_new_t)(const char*,const char*,int); typedef void (*ai_set_status_t)(void*,int); typedef void (*ai_set_menu_t)(void*,GtkMenu*); typedef void (*ai_set_title_t)(void*,const char*);
static const char *tray_kind="无";
static char icon_path[1024];
static void find_icon(void){ /* AppImage: $APPDIR/lan-messenger.png ; tarball: next to the binary ; system install: hicolor theme */
    char exe[900]; ssize_t n=readlink("/proc/self/exe",exe,sizeof exe-1); const char *ad=getenv("APPDIR"); char *sl;
    if(ad){ snprintf(icon_path,sizeof icon_path,"%s/lan-messenger.png",ad); if(access(icon_path,R_OK)==0) return; }
    if(n>0){ exe[n]=0; sl=strrchr(exe,'/'); if(sl){ *sl=0; snprintf(icon_path,sizeof icon_path,"%s/lan-messenger.png",exe); if(access(icon_path,R_OK)==0) return;
        snprintf(icon_path,sizeof icon_path,"%s/../share/icons/hicolor/256x256/apps/lan-messenger.png",exe); if(access(icon_path,R_OK)==0) return; } }
    icon_path[0]=0; }
static void show_main(void){ gtk_widget_show(win); gtk_window_present(GTK_WINDOW(win)); }
static void quit_app(void){ g_application_quit(G_APPLICATION(app)); }
static void tray_popup(GtkStatusIcon *si, guint b, guint t, gpointer menu){ gtk_menu_popup(GTK_MENU(menu),NULL,NULL,gtk_status_icon_position_menu,si,b,t); }
static void setup_tray(void){
    GtkWidget *menu=gtk_menu_new(), *a=gtk_menu_item_new_with_label("显示主窗口"), *q=gtk_menu_item_new_with_label("退出");
    const char *libs[]={"libayatana-appindicator3.so.1","libappindicator3.so.1",NULL}; void *h=NULL; int i;
    g_signal_connect_swapped(a,"activate",G_CALLBACK(show_main),NULL); g_signal_connect_swapped(q,"activate",G_CALLBACK(quit_app),NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu),a); gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new()); gtk_menu_shell_append(GTK_MENU_SHELL(menu),q); gtk_widget_show_all(menu);
    if(getenv("LSM_NO_TRAY")) return;
    for(i=0;libs[i]&&!h;i++) h=dlopen(libs[i],RTLD_LAZY);
    if(h){ ai_new_t n=(ai_new_t)dlsym(h,"app_indicator_new"); ai_set_status_t s=(ai_set_status_t)dlsym(h,"app_indicator_set_status"); ai_set_menu_t m=(ai_set_menu_t)dlsym(h,"app_indicator_set_menu"); ai_set_title_t ti=(ai_set_title_t)dlsym(h,"app_indicator_set_title");
        if(n&&s&&m){ void *ind; typedef void *(*ai_newp_t)(const char*,const char*,int,const char*); ai_newp_t np=(ai_newp_t)dlsym(h,"app_indicator_new_with_path");
            if(icon_path[0] && np){ char dir[1024]; char *sl; snprintf(dir,sizeof dir,"%s",icon_path); sl=strrchr(dir,'/'); if(sl) *sl=0; ind=np("lan-messenger","lan-messenger",0,dir); } /* icon by name from our dir: works for SNI and the XEmbed fallback */
            else ind=n("lan-messenger","mail-message-new",0 /*APPLICATION_STATUS*/); s(ind,1 /*ACTIVE*/); m(ind,GTK_MENU(menu)); if(ti) ti(ind,"局域网安全通讯"); tray_kind="AppIndicator"; return; } }
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    { GtkStatusIcon *si=icon_path[0]?gtk_status_icon_new_from_file(icon_path):gtk_status_icon_new_from_icon_name("mail-message-new"); gtk_status_icon_set_tooltip_text(si,"局域网安全通讯");
      g_signal_connect(si,"activate",G_CALLBACK(show_main),NULL); g_signal_connect(si,"popup-menu",G_CALLBACK(tray_popup),menu); tray_kind="GtkStatusIcon(XEmbed)"; }
    G_GNUC_END_IGNORE_DEPRECATIONS }

/* ---------------------------------------------------------------- theme following */
static int portal_color_scheme(void){ /* 0 none,1 dark,2 light */
    GDBusConnection *b=g_bus_get_sync(G_BUS_TYPE_SESSION,NULL,NULL); GVariant *r; int v=0; if(!b) return -1;
    r=g_dbus_connection_call_sync(b,"org.freedesktop.portal.Desktop","/org/freedesktop/portal/desktop","org.freedesktop.portal.Settings","Read",
        g_variant_new("(ss)","org.freedesktop.appearance","color-scheme"),NULL,G_DBUS_CALL_FLAGS_NONE,500,NULL,NULL);
    if(r){ GVariant *a=NULL, *x; g_variant_get(r,"(v)",&a); x=a; while(x && g_variant_is_of_type(x,G_VARIANT_TYPE_VARIANT)){ GVariant *y=g_variant_get_variant(x); if(x!=a) g_variant_unref(x); x=y; }
        if(x && g_variant_is_of_type(x,G_VARIANT_TYPE_UINT32)) v=(int)g_variant_get_uint32(x); if(x&&x!=a) g_variant_unref(x); if(a) g_variant_unref(a); g_variant_unref(r); g_object_unref(b); return v; }
    g_object_unref(b); return -1; }
static void apply_theme(void){
    GtkSettings *s=gtk_settings_get_default(); int dark=-1; const char *force=getenv("LSM_THEME");
    if(force){ dark=!strcmp(force,"dark"); }
    else { int p=portal_color_scheme(); if(p==1) dark=1; else if(p==2) dark=0;
        if(dark<0){ GSettingsSchemaSource *src=g_settings_schema_source_get_default(); GSettingsSchema *sc=src?g_settings_schema_source_lookup(src,"org.gnome.desktop.interface",TRUE):NULL;
            if(sc){ if(g_settings_schema_has_key(sc,"color-scheme")){ GSettings *gs=g_settings_new("org.gnome.desktop.interface"); gchar *v=g_settings_get_string(gs,"color-scheme"); dark=!strcmp(v,"prefer-dark")?1:-1; g_free(v); g_object_unref(gs); } g_settings_schema_unref(sc); } } }
    if(dark>=0) g_object_set(s,"gtk-application-prefer-dark-theme",dark?TRUE:FALSE,NULL); }

/* ---------------------------------------------------------------- actions */
static void send_current(void){
    GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(input)); GtkTextIter s,e; char *t, err[256], id[40];
    if(!cur_conv[0]) return; gtk_text_buffer_get_bounds(b,&s,&e); t=gtk_text_buffer_get_text(b,&s,&e,FALSE);
    if(t && *g_strstrip(t)){ if(lsm_send_text(C,cur_conv,t,id,err)) msgbox(GTK_MESSAGE_WARNING,"%s",err); else gtk_text_buffer_set_text(b,"",-1); }
    g_free(t); refresh_chat(); }
static gboolean input_key(GtkWidget *w, GdkEventKey *ev, gpointer p){ (void)w; (void)p; if((ev->keyval==GDK_KEY_Return||ev->keyval==GDK_KEY_KP_Enter) && !(ev->state&GDK_SHIFT_MASK)){ send_current(); return TRUE; } return FALSE; }
static void send_path(const char *path){ char err[256]; if(!cur_conv[0]){ msgbox(GTK_MESSAGE_INFO,"请先在左侧选择联系人或群组。"); return; } if(lsm_send_file(C,cur_conv,path,err)) msgbox(GTK_MESSAGE_WARNING,"%s：%s",path,err); }
static void send_file_dlg(void){
    GtkWidget *d; if(!cur_conv[0]) return;
    d=gtk_file_chooser_dialog_new("选择要发送的文件",GTK_WINDOW(win),GTK_FILE_CHOOSER_ACTION_OPEN,"取消",GTK_RESPONSE_CANCEL,"发送",GTK_RESPONSE_ACCEPT,NULL); gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(d),TRUE);
    if(gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_ACCEPT){ GSList *l=gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(d)), *it; for(it=l;it;it=it->next){ send_path((char*)it->data); g_free(it->data);} g_slist_free(l); }
    gtk_widget_destroy(d); refresh_chat(); }
static void on_drop(GtkWidget *w, GdkDragContext *ctx, gint x, gint y, GtkSelectionData *sd, guint info, guint time_, gpointer p){
    gchar **uris=gtk_selection_data_get_uris(sd); int i, n=0; (void)w;(void)x;(void)y;(void)info;(void)p;
    for(i=0;uris&&uris[i];i++){ gchar *f=g_filename_from_uri(uris[i],NULL,NULL); if(f){ send_path(f); g_free(f); n++; } }
    g_strfreev(uris); gtk_drag_finish(ctx,n>0,FALSE,time_); gtk_widget_hide(drop_hint); refresh_chat(); }
static gboolean on_drag_motion(GtkWidget *w, GdkDragContext *c, gint x, gint y, guint t, gpointer p){ (void)w;(void)c;(void)x;(void)y;(void)t;(void)p; if(cur_conv[0]) gtk_widget_show(drop_hint); return FALSE; }
static void on_drag_leave(GtkWidget *w, GdkDragContext *c, guint t, gpointer p){ (void)w;(void)c;(void)t;(void)p; gtk_widget_hide(drop_hint); }
static void on_row(GtkListBox *lb, GtkListBoxRow *row, gpointer p){ const char *cv; (void)lb; (void)p; if(!row) return; cv=g_object_get_data(G_OBJECT(row),"conv"); if(!cv) return;
    snprintf(cur_conv,sizeof cur_conv,"%s",cv); refresh_chat(); if(lsm_mark_read(C,cur_conv)) list_dirty=1; gtk_widget_grab_focus(input); }
static int peer_muted(const char *fp){ lsm_contact *k; int n,i,m=0; lsm_lock(C); n=lsm_contacts(C,&k); for(i=0;i<n;i++) if(!strcmp(k[i].fp,fp)){ m=k[i].muted; break; } lsm_unlock(C); return m; }
static void upd_test_note(int newer, const char *msg){ const char *tf=getenv("LSM_TEST_UPDCHECK"); FILE *f; /* CI release gate: record what the check said */
    if(tf && *tf && (f=fopen(tf,"w"))){ fprintf(f,"newer=%d version=%s %s\n",newer,LSM_APP_VERSION,msg); fclose(f); } }
static gboolean upd_test_start(gpointer p); 
static void upd_show(int newer, const char *msg, const char *url){ GtkWidget *d;
    upd_test_note(newer,msg);
    if(newer==0){ msgbox(GTK_MESSAGE_INFO,"%s",msg); return; }
    d=gtk_message_dialog_new(GTK_WINDOW(win),GTK_DIALOG_MODAL,newer>0?GTK_MESSAGE_INFO:GTK_MESSAGE_WARNING,GTK_BUTTONS_YES_NO,"%s\n%s",msg,newer>0?"是否打开下载页？":"是否在浏览器中打开发布页？");
    gtk_window_set_title(GTK_WINDOW(d),"检查更新"); if(gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_YES) upd_open_url(url); gtk_widget_destroy(d); }
static const char *sel_peer(void){ static char fp[FP_LEN]; if(strncmp(cur_conv,"dm:",3)){ msgbox(GTK_MESSAGE_INFO,"请先在左侧选择一个联系人。"); return NULL; } snprintf(fp,sizeof fp,"%s",cur_conv+3); return fp; }
static const char *sel_group(void){ static char g[40]; if(strncmp(cur_conv,"group:",6)){ msgbox(GTK_MESSAGE_INFO,"请先在左侧选择一个群组。"); return NULL; } snprintf(g,sizeof g,"%s",cur_conv+6); return g; }

static void show_transfers(void){
    GtkWidget *d=gtk_dialog_new_with_buttons("文件传输",GTK_WINDOW(win),GTK_DIALOG_DESTROY_WITH_PARENT,"关闭",GTK_RESPONSE_CLOSE,NULL), *box=gtk_dialog_get_content_area(GTK_DIALOG(d)), *lb=gtk_list_box_new(), *sc=gtk_scrolled_window_new(NULL,NULL);
    lsm_transfer *t; int n,i; static const char *TS[]={"等待接收","传输中","校验中","完成","失败","被拒收","已取消"};
    lsm_lock(C); n=lsm_transfers(C,&t);
    for(i=0;i<n;i++){ char l[1200]; GtkWidget *v=gtk_box_new(GTK_ORIENTATION_VERTICAL,4), *a=gtk_label_new(NULL), *pb=gtk_progress_bar_new(); gchar *e=g_markup_escape_text(t[i].name,-1);
        snprintf(l,sizeof l,"<b>%s %s</b>   <small>%s · %.1f / %.1f KB %s</small>",t[i].out?"↑":"↓",e,TS[t[i].state],t[i].transferred/1024.0,t[i].size/1024.0,err_zh(t[i].error)); g_free(e);
        gtk_label_set_markup(GTK_LABEL(a),l); gtk_label_set_xalign(GTK_LABEL(a),0); gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pb),t[i].size?(double)t[i].transferred/t[i].size:(t[i].state==TS_DONE?1:0));
        gtk_box_pack_start(GTK_BOX(v),a,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(v),pb,FALSE,FALSE,0); gtk_widget_set_margin_start(v,10); gtk_widget_set_margin_end(v,10); gtk_widget_set_margin_top(v,8); gtk_widget_set_margin_bottom(v,8); gtk_list_box_insert(GTK_LIST_BOX(lb),v,-1); }
    lsm_unlock(C);
    if(!n){ GtkWidget *e=gtk_label_new("本次运行暂无文件传输（历史文件见聊天记录与接收文件夹）"); gtk_widget_set_margin_top(e,30); gtk_style_context_add_class(gtk_widget_get_style_context(e),"dim-label"); gtk_list_box_set_placeholder(GTK_LIST_BOX(lb),e); gtk_widget_show(e); }
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(lb),GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(sc),lb); gtk_box_pack_start(GTK_BOX(box),sc,TRUE,TRUE,0); gtk_window_set_default_size(GTK_WINDOW(d),560,380); gtk_widget_show_all(d);
    g_signal_connect(d,"response",G_CALLBACK(gtk_widget_destroy),NULL); }

enum { A_ADDIP=1, A_TRUST, A_UNTRUST, A_BLOCK, A_PINOFFER, A_PINPAIR, A_GCREATE, A_GINVITE, A_GREMOVE, A_GLEAVE, A_NAME, A_XFERS, A_DLDIR, A_FIREWALL, A_ABOUT, A_QUIT, A_MUTE, A_DIAG, A_UPDATE };
static void act(GtkWidget *w, gpointer p){ int id=GPOINTER_TO_INT(p); char err[256]=""; (void)w;
    switch(id){
    case A_ADDIP: { char *s=ask_text("按 IP 添加","输入对方 IP 地址（可带端口，如 192.168.1.20 或 192.168.1.20:41235）：",""); if(s){ if(lsm_add_addr(C,s,err)) msgbox(GTK_MESSAGE_WARNING,"%s",err); g_free(s);} break; }
    case A_MUTE: { const char *fp=sel_peer(); if(fp){ lsm_set_muted(C,fp,!peer_muted(fp),err); list_dirty=chat_dirty=1; } break; }
    case A_DIAG: { size_t need=lsm_diagnostics(C,NULL,0); char *d=g_malloc(need+1); lsm_diagnostics(C,d,need+1); msgbox(GTK_MESSAGE_INFO,"%s",d); g_free(d); break; }
    case A_UPDATE: upd_start(upd_show); break;
    case A_TRUST: case A_UNTRUST: case A_BLOCK: { const char *fp=sel_peer(); if(fp){ lsm_set_trust(C,fp,id==A_TRUST?TRUST_TRUSTED:id==A_BLOCK?TRUST_BLOCKED:TRUST_UNKNOWN,err); list_dirty=chat_dirty=1; } break; }
    case A_PINOFFER: { char pin[8], mk[256]; GtkWidget *d; lsm_pin_offer(C,pin); d=gtk_message_dialog_new(GTK_WINDOW(win),GTK_DIALOG_MODAL,GTK_MESSAGE_INFO,GTK_BUTTONS_OK,"请把下面的 PIN 告诉对方");
        snprintf(mk,sizeof mk,"<span size='21504' weight='600' font_family='monospace'>%s</span>\n\n让对方在“输入 PIN 配对”中输入（5 分钟内有效）。",pin); gtk_message_dialog_format_secondary_markup(GTK_MESSAGE_DIALOG(d),"%s",mk); gtk_window_set_title(GTK_WINDOW(d),"PIN 配对"); gtk_dialog_run(GTK_DIALOG(d)); gtk_widget_destroy(d); break; }
    case A_PINPAIR: { const char *fp=sel_peer(); char *s; if(fp && (s=ask_text("输入 PIN 配对","输入对方显示的 6 位 PIN：",""))){ if(lsm_pin_pair(C,fp,s,err)) msgbox(GTK_MESSAGE_WARNING,"%s",err); g_free(s);} break; }
    case A_GCREATE: { char sel[64][FP_LEN]; const char *fps[64]; char *nm=NULL, gid[40]; int n=pick_peers("创建群组","选择成员（可多选，按住 Ctrl；仅显示已信任联系人），并在下方输入群名：",1,1,sel,&nm,"新群组"), i;
        if(n>0){ for(i=0;i<n;i++) fps[i]=sel[i]; if(lsm_group_create(C,nm,fps,n,gid,err)) msgbox(GTK_MESSAGE_WARNING,"%s",err); else { snprintf(cur_conv,64,"group:%s",gid); list_dirty=chat_dirty=1; } } g_free(nm); break; }
    case A_GINVITE: { const char *g=sel_group(); char sel[64][FP_LEN]; const char *fps[64]; int n,i; if(!g) break; n=pick_peers("邀请成员","选择要邀请的成员（可多选）：",1,1,sel,NULL,NULL); if(n<=0) break; for(i=0;i<n;i++) fps[i]=sel[i]; if(lsm_group_invite(C,g,fps,n,err)) msgbox(GTK_MESSAGE_WARNING,"%s",err); break; }
    case A_GREMOVE: { const char *g=sel_group(); char sel[64][FP_LEN]; int n; if(!g) break; n=pick_peers("移除成员","选择要移出群组的成员（将自动更换群密钥）：",0,0,sel,NULL,NULL); if(n<=0) break; if(lsm_group_remove(C,g,sel[0],err)) msgbox(GTK_MESSAGE_WARNING,"%s",err); break; }
    case A_GLEAVE: { const char *g=sel_group(); if(g){ if(lsm_group_leave(C,g,err)) msgbox(GTK_MESSAGE_WARNING,"%s",err); cur_conv[0]=0; list_dirty=chat_dirty=1; } break; }
    case A_NAME: { char *s, init[300]; lsm_lock(C); snprintf(init,sizeof init,"%s",lsm_my_name(C)); lsm_unlock(C); if((s=ask_text("修改昵称","新的显示名称：",init))){ lsm_set_name(C,s); g_free(s); list_dirty=1; } break; }
    case A_XFERS: show_transfers(); break;
    case A_DLDIR: { char d[600]; gchar *u; lsm_lock(C); snprintf(d,sizeof d,"%s",lsm_download_dir(C)); lsm_unlock(C); u=g_filename_to_uri(d,NULL,NULL); if(u){ g_app_info_launch_default_for_uri(u,NULL,NULL); g_free(u);} break; }
    case A_FIREWALL: { char rep[2048]; int rc=lsm_firewall_setup("",rep,sizeof rep); msgbox(rc==0?GTK_MESSAGE_INFO:GTK_MESSAGE_WARNING,"%s\n\n%s",rc==0?"防火墙设置完成。":"防火墙设置未完成。",rep); break; }
    case A_ABOUT: { char ab[2048]; GtkWidget *d; int r; snprintf(ab,sizeof ab,"局域网安全通讯 Linux 版 %s\n\n与 Windows v0.5 及 XP 版完全互通（协议 v5）：Noise_XX_25519_ChaChaPoly_BLAKE2s 加密、自动发现、按 IP 添加、PIN 配对、群组、文件传输、送达/已读回执。\n\n托盘：%s",LSM_APP_VERSION,tray_kind);
        /* 诊断信息 / 检查更新 live here (not in the main menu, which must stay clear of the input row) */
        d=gtk_message_dialog_new(GTK_WINDOW(win),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,GTK_MESSAGE_INFO,GTK_BUTTONS_NONE,"%s",ab);
        gtk_dialog_add_buttons(GTK_DIALOG(d),"诊断信息",1,"检查更新…",2,"确定",GTK_RESPONSE_OK,NULL); gtk_dialog_set_default_response(GTK_DIALOG(d),GTK_RESPONSE_OK);
        r=gtk_dialog_run(GTK_DIALOG(d)); gtk_widget_destroy(d); if(r==1) act(NULL,GINT_TO_POINTER(A_DIAG)); else if(r==2) act(NULL,GINT_TO_POINTER(A_UPDATE)); break; }
    case A_QUIT: quit_app(); break; } }

static GtkWidget *mi(GtkWidget *menu, const char *label, int id){ GtkWidget *i=gtk_menu_item_new_with_label(label); g_signal_connect(i,"activate",G_CALLBACK(act),GINT_TO_POINTER(id)); gtk_menu_shell_append(GTK_MENU_SHELL(menu),i); return i; }
static void sep(GtkWidget *m){ gtk_menu_shell_append(GTK_MENU_SHELL(m),gtk_separator_menu_item_new()); }

/* ---------------------------------------------------------------- event pump (core thread -> GTK main loop) */
static gboolean poll_cb(gpointer p){ char e[4096]; (void)p;
    while(lsm_poll_event(C,e,sizeof e)){
        if(!strncmp(e,"msg\t",4)||!strncmp(e,"msg_update\t",11)){ char *f[8]; int n=0; char *q=e; f[n++]=q; while(*q&&n<8){ if(*q=='\t'){ *q=0; f[n++]=q+1; } q++; }
            if(n>=7){ if(!strcmp(f[2],cur_conv)) chat_dirty=1; list_dirty=1;
                if(!strcmp(f[0],"msg")&&!strcmp(f[3],"in")&&strcmp(f[5],"system")&&(strcmp(f[2],cur_conv)||!gtk_window_is_active(GTK_WINDOW(win)))&&!(!strncmp(f[2],"dm:",3)&&peer_muted(f[2]+3))) notify("新消息",!strcmp(f[5],"file")?"收到文件":f[6]); } }
        else if(!strncmp(e,"transfer\t",9)) chat_dirty=1;
        else if(!strncmp(e,"notice\t",7)) notify("提示",e+7);
        else if(!strncmp(e,"pin_result\t",11)){ notify("PIN 配对",strstr(e,"\tok")?"配对成功，已互相信任。":"配对失败（PIN 错误或已过期）。"); list_dirty=chat_dirty=1; }
        else if(!strncmp(e,"log\t",4)){ if(getenv("LSM_VERBOSE")) fprintf(stderr,"%s\n",e); }
        else list_dirty=1; }
    if(list_dirty){ list_dirty=0; refresh_list(); }
    if(chat_dirty){ chat_dirty=0; refresh_chat(); if(cur_conv[0]&&gtk_window_is_active(GTK_WINDOW(win))&&lsm_mark_read(C,cur_conv)) list_dirty=1; }
    return G_SOURCE_CONTINUE; }

#include "lsm_theme_css.h" /* generated from linux/data/lsm-theme.css by linux/build.sh */
static GtkCssProvider *theme_cp;
static void load_theme_css(void){ /* shared block + the light or dark block of the one CSS file */
    gboolean dark=FALSE; const char *sh=strstr(LSM_THEME_CSS,"/*@shared*/"), *li=strstr(LSM_THEME_CSS,"/*@light*/"), *dk=strstr(LSM_THEME_CSS,"/*@dark*/"); GString *g;
    g_object_get(gtk_settings_get_default(),"gtk-application-prefer-dark-theme",&dark,NULL);
    g=g_string_new(NULL); g_string_append_len(g,sh,li-sh); if(dark) g_string_append(g,dk); else g_string_append_len(g,li,dk-li);
    if(!theme_cp){ theme_cp=gtk_css_provider_new(); gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),GTK_STYLE_PROVIDER(theme_cp),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION); }
    gtk_css_provider_load_from_data(theme_cp,g->str,-1,NULL); g_string_free(g,TRUE); }
static void on_dark_changed(GObject *o, GParamSpec *ps, gpointer p){ (void)o; (void)ps; (void)p; load_theme_css(); }

static GtkWidget *menu_btn;
static gboolean win_key(GtkWidget *w, GdkEventKey *ev, gpointer p){ (void)w; (void)p; if((ev->state&GDK_CONTROL_MASK) && (ev->keyval==GDK_KEY_m||ev->keyval==GDK_KEY_M)){ act(NULL,GINT_TO_POINTER(A_MUTE)); return TRUE; } /* 免打扰 toggle (kept out of the full main menu) */
    if(ev->keyval==GDK_KEY_F10){ gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(menu_btn),TRUE); return TRUE; } return FALSE; }
static gboolean on_delete(GtkWidget *w, GdkEvent *e, gpointer p){ (void)e; (void)p; if(strcmp(tray_kind,"无")){ gtk_widget_hide(w); notify("局域网安全通讯","程序仍在后台运行，可从托盘图标打开。"); return TRUE; } return FALSE; }

static char *g_data, *g_name, *g_targets; static int g_port, g_dport, g_nodisc;
static void activate(GtkApplication *a, gpointer p){
    GtkWidget *hb, *mb, *menu, *paned, *left, *sc, *right, *hdr, *tv, *ib, *btn, *fbtn, *vb, *overlay, *empty; GtkCssProvider *cp; lsm_opts o; char err[256]; (void)p;
    static const GtkTargetEntry tg[]={{"text/uri-list",0,0}};
    if(win){ show_main(); return; }
    apply_theme();
    memset(&o,0,sizeof o); o.data_dir=g_data; o.name=g_name; o.listen_port=g_port; o.discovery_port=g_dport; o.announce_targets=g_targets; o.no_discovery=g_nodisc;
    C=lsm_start(&o,err,sizeof err); if(!C){ GtkWidget *d=gtk_message_dialog_new(NULL,0,GTK_MESSAGE_ERROR,GTK_BUTTONS_OK,"启动失败：%s",err); gtk_dialog_run(GTK_DIALOG(d)); exit(1); }
    (void)cp; load_theme_css(); g_signal_connect(gtk_settings_get_default(),"notify::gtk-application-prefer-dark-theme",G_CALLBACK(on_dark_changed),NULL);
    win=gtk_application_window_new(a); gtk_window_set_default_size(GTK_WINDOW(win),980,640); gtk_window_set_title(GTK_WINDOW(win),"局域网安全通讯"); /* WM_NAME for taskbars / alt-tab (header bar title alone leaves it empty) */ find_icon(); if(icon_path[0]) gtk_window_set_icon_from_file(GTK_WINDOW(win),icon_path,NULL); else gtk_window_set_icon_name(GTK_WINDOW(win),"mail-message-new");
    hb=gtk_header_bar_new(); gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hb),TRUE);
    vb=gtk_box_new(GTK_ORIENTATION_VERTICAL,0); title_lbl=gtk_label_new(""); sub_lbl=gtk_label_new(""); gtk_style_context_add_class(gtk_widget_get_style_context(title_lbl),"title"); gtk_style_context_add_class(gtk_widget_get_style_context(sub_lbl),"subtitle");
    gtk_style_context_add_class(gtk_widget_get_style_context(sub_lbl),"dim-label"); gtk_label_set_ellipsize(GTK_LABEL(sub_lbl),PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(vb),title_lbl,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(vb),sub_lbl,FALSE,FALSE,0); gtk_header_bar_set_custom_title(GTK_HEADER_BAR(hb),vb);
    mb=gtk_menu_button_new(); gtk_button_set_image(GTK_BUTTON(mb),gtk_image_new_from_icon_name("open-menu-symbolic",GTK_ICON_SIZE_BUTTON)); gtk_widget_set_tooltip_text(mb,"菜单");
    menu=gtk_menu_new();
    mi(menu,"按 IP 添加…",A_ADDIP); sep(menu); mi(menu,"信任当前联系人",A_TRUST); mi(menu,"取消信任",A_UNTRUST); mi(menu,"屏蔽",A_BLOCK); sep(menu);
    mi(menu,"生成 PIN（让对方输入）",A_PINOFFER); mi(menu,"输入 PIN 配对…",A_PINPAIR); sep(menu);
    mi(menu,"创建群组…",A_GCREATE); mi(menu,"邀请成员…",A_GINVITE); mi(menu,"移除成员…",A_GREMOVE); mi(menu,"退出群组",A_GLEAVE); sep(menu);
    mi(menu,"文件传输",A_XFERS); mi(menu,"打开接收文件夹",A_DLDIR); mi(menu,"修改昵称…",A_NAME); mi(menu,"设置防火墙（ufw / firewalld）…",A_FIREWALL); sep(menu); mi(menu,"关于",A_ABOUT); mi(menu,"退出",A_QUIT);
    gtk_widget_show_all(menu); gtk_widget_set_halign(menu,GTK_ALIGN_END); /* open leftwards, inside the window */ gtk_menu_button_set_popup(GTK_MENU_BUTTON(mb),menu); gtk_header_bar_pack_end(GTK_HEADER_BAR(hb),mb); menu_btn=mb;
    { GtkWidget *add=gtk_button_new_from_icon_name("list-add-symbolic",GTK_ICON_SIZE_BUTTON); gtk_widget_set_tooltip_text(add,"按 IP 添加"); g_signal_connect(add,"clicked",G_CALLBACK(act),GINT_TO_POINTER(A_ADDIP)); gtk_header_bar_pack_start(GTK_HEADER_BAR(hb),add); }
    gtk_window_set_titlebar(GTK_WINDOW(win),hb);
    paned=gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    left=gtk_box_new(GTK_ORIENTATION_VERTICAL,0); sc=gtk_scrolled_window_new(NULL,NULL); peer_list=gtk_list_box_new(); gtk_container_add(GTK_CONTAINER(sc),peer_list);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC); gtk_widget_set_size_request(left,260,-1);
    g_signal_connect_data(peer_list,"row-selected",G_CALLBACK(on_row),(gpointer)1,NULL,0);
    hdr=gtk_label_new(NULL); gtk_label_set_markup(GTK_LABEL(hdr),"<b>联系人与群组</b>"); gtk_label_set_xalign(GTK_LABEL(hdr),0); gtk_widget_set_margin_start(hdr,12); gtk_widget_set_margin_top(hdr,10); gtk_widget_set_margin_bottom(hdr,6);
    gtk_style_context_add_class(gtk_widget_get_style_context(left),"sidebar"); gtk_box_pack_start(GTK_BOX(left),hdr,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(left),sc,TRUE,TRUE,0);
    right=gtk_box_new(GTK_ORIENTATION_VERTICAL,0); stack=gtk_stack_new();
    empty=gtk_label_new(NULL); gtk_label_set_markup(GTK_LABEL(empty),"<span size='15360' weight='600'>👋 欢迎使用局域网安全通讯</span>\n\n同一局域网内的设备会自动出现在左侧。\n也可以点左上角 ＋ 按 IP 添加。\n\n选择联系人后，可直接把文件拖到聊天区发送。"); gtk_label_set_justify(GTK_LABEL(empty),GTK_JUSTIFY_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(empty),"dim-label");
    overlay=gtk_overlay_new(); chat_scroll=gtk_scrolled_window_new(NULL,NULL); chat_box=gtk_box_new(GTK_ORIENTATION_VERTICAL,2); /* padding via CSS (.chatlist): widget margins are outside the viewport's painted area and showed as a black strip */ gtk_style_context_add_class(gtk_widget_get_style_context(chat_box),"chatlist");
    gtk_container_add(GTK_CONTAINER(chat_scroll),chat_box); gtk_style_context_add_class(gtk_widget_get_style_context(chat_scroll),"chatbg"); gtk_container_add(GTK_CONTAINER(overlay),chat_scroll);
    drop_hint=gtk_label_new("松开即可发送文件"); gtk_style_context_add_class(gtk_widget_get_style_context(drop_hint),"drophint"); gtk_widget_set_halign(drop_hint,GTK_ALIGN_CENTER); gtk_widget_set_valign(drop_hint,GTK_ALIGN_CENTER); gtk_widget_set_no_show_all(drop_hint,TRUE);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay),drop_hint);
    gtk_drag_dest_set(overlay,GTK_DEST_DEFAULT_ALL,tg,1,GDK_ACTION_COPY); g_signal_connect(overlay,"drag-data-received",G_CALLBACK(on_drop),NULL); g_signal_connect(overlay,"drag-motion",G_CALLBACK(on_drag_motion),NULL); g_signal_connect(overlay,"drag-leave",G_CALLBACK(on_drag_leave),NULL);
    { GtkWidget *chat=gtk_box_new(GTK_ORIENTATION_VERTICAL,0); ib=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6); gtk_container_set_border_width(GTK_CONTAINER(ib),8);
      tv=gtk_scrolled_window_new(NULL,NULL); input=gtk_text_view_new(); gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(input),GTK_WRAP_WORD_CHAR); gtk_container_add(GTK_CONTAINER(tv),input); gtk_widget_set_size_request(tv,-1,64);
      gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(tv),GTK_SHADOW_IN); g_signal_connect(input,"key-press-event",G_CALLBACK(input_key),NULL);
      { GtkWidget *bb=gtk_box_new(GTK_ORIENTATION_VERTICAL,6); btn=gtk_button_new_with_label("发送"); gtk_style_context_add_class(gtk_widget_get_style_context(btn),"suggested-action"); fbtn=gtk_button_new_with_label("发送文件…");
        g_signal_connect_swapped(btn,"clicked",G_CALLBACK(send_current),NULL); g_signal_connect_swapped(fbtn,"clicked",G_CALLBACK(send_file_dlg),NULL); gtk_box_pack_start(GTK_BOX(bb),btn,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(bb),fbtn,FALSE,FALSE,0);
        gtk_box_pack_start(GTK_BOX(ib),tv,TRUE,TRUE,0); gtk_box_pack_start(GTK_BOX(ib),bb,FALSE,FALSE,0); }
      gtk_box_pack_start(GTK_BOX(chat),overlay,TRUE,TRUE,0); gtk_box_pack_start(GTK_BOX(chat),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(chat),ib,FALSE,FALSE,0);
      gtk_stack_add_named(GTK_STACK(stack),empty,"empty"); gtk_stack_add_named(GTK_STACK(stack),chat,"chat"); }
    gtk_box_pack_start(GTK_BOX(right),stack,TRUE,TRUE,0);
    gtk_paned_pack1(GTK_PANED(paned),left,FALSE,FALSE); gtk_paned_pack2(GTK_PANED(paned),right,TRUE,FALSE);
    { GtkWidget *root=gtk_box_new(GTK_ORIENTATION_VERTICAL,0); status_lbl=gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(status_lbl),0); gtk_style_context_add_class(gtk_widget_get_style_context(status_lbl),"statusbar"); gtk_label_set_selectable(GTK_LABEL(status_lbl),TRUE);
      gtk_box_pack_start(GTK_BOX(root),paned,TRUE,TRUE,0); gtk_box_pack_start(GTK_BOX(root),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(root),status_lbl,FALSE,FALSE,0); gtk_container_add(GTK_CONTAINER(win),root); }
    g_signal_connect(win,"delete-event",G_CALLBACK(on_delete),NULL); g_signal_connect(win,"key-press-event",G_CALLBACK(win_key),NULL); /* F10 opens the menu */
    setup_tray();
    gtk_widget_show_all(win); refresh_list(); refresh_chat(); if(getenv("LSM_TEST_UPDCHECK")) g_timeout_add(1500,upd_test_start,NULL);
    g_timeout_add(300,poll_cb,NULL); }

int main(int argc, char **argv){
    int i, st; char def[1024]; const char *xdg=getenv("XDG_DATA_HOME"), *home=getenv("HOME");
    if(xdg&&*xdg) snprintf(def,sizeof def,"%s/lan-messenger",xdg); else snprintf(def,sizeof def,"%s/.local/share/lan-messenger",home?home:".");
    g_data=def;
    for(i=1;i<argc;i++){
        if(!strcmp(argv[i],"--data")&&i+1<argc) g_data=argv[++i];
        else if(!strcmp(argv[i],"--name")&&i+1<argc) g_name=argv[++i];
        else if(!strcmp(argv[i],"--port")&&i+1<argc) g_port=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--disco-port")&&i+1<argc) g_dport=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--targets")&&i+1<argc) g_targets=argv[++i];
        else if(!strcmp(argv[i],"--no-discovery")) g_nodisc=1;
        else if(!strcmp(argv[i],"--version")){ printf("lan-messenger-gtk %s (protocol v5)\n",LSM_APP_VERSION); return 0; } }
    if(!g_name){ static char hn[256]; if(gethostname(hn,sizeof hn)==0) g_name=hn; }
    /* separate data dirs (tests) get their own D-Bus app id so several instances can run */
    { char id[256]; snprintf(id,sizeof id,APP_ID "%s",getenv("LSM_INSTANCE")?getenv("LSM_INSTANCE"):"");
      app=gtk_application_new(id,G_APPLICATION_NON_UNIQUE); }
    g_signal_connect(app,"activate",G_CALLBACK(activate),NULL);
    { char *av[]={argv[0],NULL}; st=g_application_run(G_APPLICATION(app),1,av); }
    if(C) lsm_stop(C);
    return st; }

static gboolean upd_test_start(gpointer p){ (void)p; upd_start(upd_show); return G_SOURCE_REMOVE; }
