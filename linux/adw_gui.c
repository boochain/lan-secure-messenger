/* 局域网安全通讯 Linux 版 — GTK 4 + libadwaita (>= 1.4) front end over the portable C core (src/core.c).
 * Follows design/redesign/mockups/gnome-{light,dark}-chat.png and gnome-light-narrow.png:
 * AdwNavigationSplitView (sidebar 300, collapses below 600sp), 42 px avatars with the shared FNV palette and initials rule,
 * content header with avatar + three short facts, bubbles with the time inside, composer with placeholder + right hint.
 * Colours: tools/palettes/gnome.json (linux/adw_palette.h); light/dark follow AdwStyleManager (system), LSM_THEME=light|dark forces.
 * Keyboard (GTK checklist G4): Ctrl+F search, Ctrl+N add contact, Ctrl+Tab / Ctrl+Shift+Tab next/prev chat, Ctrl+O send file,
 * Ctrl+I details, Esc clears search, F10 main menu, Ctrl+Q quit, Ctrl+W close window, Enter send / Shift+Enter newline.
 * Tray: GTK 4 has no tray API; closing keeps the app running in the background (relaunch shows the window). */
#define _GNU_SOURCE
#include <adwaita.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "../src/core.h"
/* the relay client (feat/relay-client) adds lsm_relay_status() and lsm_contact.via_relay; build-adw.sh passes
   -DLSM_HAVE_RELAY when src/core.h has them, so this file builds on main and on the relay branch */
static void save_window(void);
#ifdef LSM_HAVE_RELAY
#define VIA_RELAY(k) ((k).via_relay)
#else
#define VIA_RELAY(k) 0
static const char *lsm_relay_status(lsm_core *c, char *d, size_t n){ (void)c; if(n) d[0]=0; return "off"; }
#endif
static GPtrArray *icon_widgets; static GtkWidget *empty_page; static gboolean test_log_later(gpointer p);
#include "adw_palette.h"
#include "lsm_adw_css.h" /* LSM_ADW_RULES, generated from linux/data/lsm-adw.css by linux/build-adw.sh */

#define APP_ID "io.github.frec86.LanMessenger"
static lsm_core *C;
static AdwApplication *app;
static GtkWidget *hdr_shield, *hdr_more, *hdr_cloud; static int hdr_via; static GtkWidget *win, *split, *conv_list, *search, *chat_box, *chat_scroll, *title_lbl, *sub_lbl, *hdr_avatar, *input, *placeholder, *hint,
                 *content_stack, *add_popover, *add_entry, *menu_btn, *status_lbl, *drop_hint;
static char cur_conv[64], data_dir[1024];
static int list_dirty, chat_dirty, held, bg_notice_shown;
#if !ADW_CHECK_VERSION(1, 4, 0)
#error "libadwaita 1.4 or newer is required (AdwNavigationSplitView, AdwToolbarView, AdwBreakpoint)"
#endif

/* ---------------------------------------------------------------- shared presentation helpers (same rules as WinUI / Android) */
static int avatar_index(const char *id){ uint32_t h=2166136261u; const unsigned char *p=(const unsigned char*)(id?id:""); while(*p){ h^=*p++; h*=16777619u; } return (int)(h%8); }
static const char *status_txt(int s){ switch(s){ case ST_QUEUED: return "排队中"; case ST_SENT: return "已发送"; case ST_DELIVERED: return "已送达"; case ST_READ: return "已读"; case ST_FAILED: return "发送失败"; } return ""; }
static const char *tick_txt(int s){ switch(s){ case ST_QUEUED: case ST_SENT: return "✓"; case ST_DELIVERED: case ST_READ: return "✓✓"; case ST_FAILED: return "!"; } return ""; }
static void clock_txt(int64_t ms, char *o, size_t n){ time_t t=(time_t)(ms/1000); struct tm tm; localtime_r(&t,&tm); strftime(o,n,"%H:%M",&tm); }
static void list_time(int64_t ms, char *o, size_t n){ /* today HH:MM, 昨天, 周X within a week, MM/DD this year, else YYYY/MM/DD */
    static const char *W[]={"周日","周一","周二","周三","周四","周五","周六"}; time_t t=(time_t)(ms/1000), now=time(NULL); struct tm a, b; long days;
    if(ms<=0){ o[0]=0; return; } localtime_r(&t,&a); localtime_r(&now,&b);
    { struct tm a0=a, b0=b; a0.tm_hour=b0.tm_hour=12; a0.tm_min=b0.tm_min=a0.tm_sec=b0.tm_sec=0; days=(long)((mktime(&b0)-mktime(&a0))/86400); }
    if(days<=0) strftime(o,n,"%H:%M",&a); else if(days==1) snprintf(o,n,"昨天"); else if(days<7) snprintf(o,n,"%s",W[a.tm_wday]);
    else if(a.tm_year==b.tm_year) strftime(o,n,"%m/%d",&a); else strftime(o,n,"%Y/%m/%d",&a); }
static void date_label(int64_t ms, char *o, size_t n){ time_t t=(time_t)(ms/1000), now=time(NULL); struct tm a, b; localtime_r(&t,&a); localtime_r(&now,&b);
    if(a.tm_year==b.tm_year && a.tm_yday==b.tm_yday) snprintf(o,n,"今天"); else if(a.tm_year==b.tm_year && a.tm_yday==b.tm_yday-1) snprintf(o,n,"昨天");
    else if(a.tm_year==b.tm_year) strftime(o,n,"%m月%d日",&a); else strftime(o,n,"%Y年%m月%d日",&a); }
static int is_image(const char *name){ const char *e=strrchr(name?name:"",'.'); if(!e) return 0; e++;
    return !g_ascii_strcasecmp(e,"png")||!g_ascii_strcasecmp(e,"jpg")||!g_ascii_strcasecmp(e,"jpeg")||!g_ascii_strcasecmp(e,"gif")||!g_ascii_strcasecmp(e,"webp"); }
static void cls(GtkWidget *w, const char *c){ gtk_widget_add_css_class(w,c); }
static GtkWidget *label(const char *t, const char *css){ GtkWidget *l=gtk_label_new(t); gtk_label_set_xalign(GTK_LABEL(l),0); if(css){ char b[200]; char *s, *tok; snprintf(b,sizeof b,"%s",css); for(tok=strtok_r(b," ",&s);tok;tok=strtok_r(NULL," ",&s)) cls(l,tok); } return l; }
static GtkWidget *avatar(const char *name, const char *key, int size, int online){
    char ini[8], c[16]; GtkWidget *o=gtk_overlay_new(), *l; lsm_initials(name,ini); l=gtk_label_new(ini);
    snprintf(c,sizeof c,"av%d",avatar_index(key)); cls(l,"avatar"); cls(l,c); cls(l,size>=42?"av42":"av36"); gtk_widget_set_size_request(l,size,size);
    gtk_widget_set_valign(o,GTK_ALIGN_CENTER); gtk_widget_set_halign(o,GTK_ALIGN_CENTER); gtk_overlay_set_child(GTK_OVERLAY(o),l);
    if(online){ GtkWidget *d=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0); cls(d,"online-dot"); if(icon_widgets) g_ptr_array_add(icon_widgets,g_object_ref(d)); gtk_widget_set_size_request(d,6,6); gtk_widget_set_halign(d,GTK_ALIGN_END); gtk_widget_set_valign(d,GTK_ALIGN_END); gtk_overlay_add_overlay(GTK_OVERLAY(o),d); }
    return o; }
static void toast(const char *t){ (void)t; /* status line for now */ if(status_lbl) gtk_label_set_text(GTK_LABEL(status_lbl),t); }
static void notify_conv(const char *title, const char *body, const char *conv){ GNotification *n=g_notification_new(title); g_notification_set_body(n,body);
    if(conv&&*conv) g_notification_set_default_action_and_target(n,"app.open-conv","s",conv);   /* click opens that chat (G17) */
    g_application_send_notification(G_APPLICATION(app),"msg",n); g_object_unref(n); }
static void notify(const char *title, const char *body){ notify_conv(title,body,NULL); }

/* ---------------------------------------------------------------- dialogs (AdwAlertDialog on 1.5+, AdwMessageDialog on 1.4) */
typedef void (*confirm_cb)(gpointer data);
typedef struct { confirm_cb cb; gpointer data; GDestroyNotify free; } confirm_t;
static void confirm_resp(GObject *d, const char *resp, gpointer p){ confirm_t *c=p; (void)d; if(!strcmp(resp,"ok")) c->cb(c->data); if(c->free) c->free(c->data); g_free(c); }
static void confirm(const char *heading, const char *body, const char *ok, int destructive, confirm_cb cb, gpointer data, GDestroyNotify fr){
    confirm_t *c=g_new0(confirm_t,1); c->cb=cb; c->data=data; c->free=fr;
#if ADW_CHECK_VERSION(1, 5, 0)
    AdwDialog *d=adw_alert_dialog_new(heading,body); adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(d),"cancel","取消","ok",ok,NULL);
    if(destructive) adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(d),"ok",ADW_RESPONSE_DESTRUCTIVE); else adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(d),"ok",ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(d),"cancel"); adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(d),"cancel");
    g_signal_connect(d,"response",G_CALLBACK(confirm_resp),c); adw_dialog_present(d,win);
#else
    GtkWidget *d=adw_message_dialog_new(GTK_WINDOW(win),heading,body); adw_message_dialog_add_responses(ADW_MESSAGE_DIALOG(d),"cancel","取消","ok",ok,NULL);
    adw_message_dialog_set_response_appearance(ADW_MESSAGE_DIALOG(d),"ok",destructive?ADW_RESPONSE_DESTRUCTIVE:ADW_RESPONSE_SUGGESTED);
    adw_message_dialog_set_default_response(ADW_MESSAGE_DIALOG(d),"cancel"); adw_message_dialog_set_close_response(ADW_MESSAGE_DIALOG(d),"cancel");
    g_signal_connect(d,"response",G_CALLBACK(confirm_resp),c); gtk_window_present(GTK_WINDOW(d));
#endif
}
static void noop(gpointer p){ (void)p; }
static void info(const char *heading, const char *body){
#if ADW_CHECK_VERSION(1, 5, 0)
    AdwDialog *d=adw_alert_dialog_new(heading,body); adw_alert_dialog_add_response(ADW_ALERT_DIALOG(d),"close","关闭"); adw_alert_dialog_set_body_use_markup(ADW_ALERT_DIALOG(d),FALSE); adw_dialog_present(d,win);
#else
    GtkWidget *d=adw_message_dialog_new(GTK_WINDOW(win),heading,body); adw_message_dialog_add_response(ADW_MESSAGE_DIALOG(d),"close","关闭"); gtk_window_present(GTK_WINDOW(d));
#endif
}

/* ---------------------------------------------------------------- conversation list */
typedef struct { char conv[64], title[300], key[64], preview[400], time[32]; int64_t ts; int unread, online, group, trusted, via; } conv_t;
static int conv_cmp(const void *a, const void *b){ const conv_t *x=a, *y=b; return x->ts<y->ts?1:x->ts>y->ts?-1:strcmp(x->title,y->title); }
static void fill_preview(conv_t *cv, lsm_msg *m, int nm){ int j; for(j=0;j<nm;j++){ if(strcmp(m[j].conv,cv->conv)) continue; if(m[j].unread&&!m[j].out) cv->unread++;
        if(m[j].ts_ms>=cv->ts){ const char *b=m[j].kind==K_FILE?(is_image(m[j].file_name)?"照片":m[j].file_name):(m[j].body?m[j].body:""); char from[320]="";
            if(cv->group && m[j].kind!=K_SYSTEM) snprintf(from,sizeof from,"%s: ",m[j].out?"我":m[j].sender_name);
            snprintf(cv->preview,sizeof cv->preview,"%s%s%s",m[j].out&&!cv->group?tick_txt(m[j].status):"",m[j].out&&!cv->group?" ":"",from); strncat(cv->preview,b,sizeof cv->preview-strlen(cv->preview)-1);
            for(char *q=cv->preview;*q;q++) if(*q=='\n') *q=' '; cv->ts=m[j].ts_ms; } } list_time(cv->ts,cv->time,sizeof cv->time); }
static gboolean list_filter(GtkListBoxRow *row, gpointer p){ const char *q=gtk_editable_get_text(GTK_EDITABLE(search)), *t=g_object_get_data(G_OBJECT(row),"title"); (void)p;
    if(!q||!*q) return TRUE; { gchar *a=g_utf8_casefold(t?t:"",-1), *b=g_utf8_casefold(q,-1); gboolean r=strstr(a,b)!=NULL; g_free(a); g_free(b); return r; } }
static int selecting;
static void relay_status_text(char *o, size_t n);
static void refresh_list(void){ if(!icon_widgets) icon_widgets=g_ptr_array_new_with_free_func(g_object_unref); g_ptr_array_set_size(icon_widgets,0); if(getenv("LSM_UI_TEST")) g_timeout_add(300,test_log_later,(gpointer)"list");
    lsm_contact *k; lsm_group *g; lsm_msg *m; int nk, ng, nm, i, n=0; conv_t *cv; GtkListBoxRow *sel=NULL;
    lsm_lock(C); nk=lsm_contacts(C,&k); ng=lsm_groups(C,&g); nm=lsm_messages(C,&m); cv=g_new0(conv_t,nk+ng+1);
    for(i=0;i<nk;i++){ conv_t *c=&cv[n++]; snprintf(c->conv,64,"dm:%s",k[i].fp); snprintf(c->title,300,"%s",k[i].name); snprintf(c->key,64,"%s",k[i].fp); c->online=k[i].online; c->via=k[i].online && VIA_RELAY(k[i]); c->trusted=k[i].trust==TRUST_TRUSTED; fill_preview(c,m,nm); }
    for(i=0;i<ng;i++){ conv_t *c=&cv[n++]; char u[37]; lsm_uuid_str(g[i].id,u); snprintf(c->conv,64,"group:%s",u); snprintf(c->title,300,"%s",g[i].name); snprintf(c->key,64,"%s",u); c->group=1; fill_preview(c,m,nm); }
    { char st[600], det[200]=""; const char *rs=lsm_relay_status(C,det,sizeof det); int on=0; for(i=0;i<nk;i++) on+=k[i].online;
      const char *shortst=!strcmp(rs,"online")?"中转：已连接":!strcmp(rs,"connecting")?"中转：正在连接…":!strcmp(rs,"auth_failed")?"中转：网络口令错误":!strcmp(rs,"error")?"中转：无法连接":NULL;
      snprintf(st,sizeof st,"已连接 · 局域网 · %d 人在线%s%s",on,shortst?"\n":"",shortst?shortst:""); gtk_label_set_text(GTK_LABEL(status_lbl),st); /* relay on its own short line; full text in the tooltip */
      if(shortst){ char tip[400]; lsm_unlock(C); relay_status_text(tip,sizeof tip); lsm_lock(C); gtk_widget_set_tooltip_text(status_lbl,tip); } else gtk_widget_set_tooltip_text(status_lbl,NULL); }
    lsm_unlock(C);
    qsort(cv,n,sizeof *cv,conv_cmp); selecting=1; gtk_list_box_remove_all(GTK_LIST_BOX(conv_list));
    for(i=0;i<n;i++){ GtkWidget *row=gtk_list_box_row_new(), *h=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12), *v=gtk_box_new(GTK_ORIENTATION_VERTICAL,2), *t1=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4), *r=gtk_box_new(GTK_ORIENTATION_VERTICAL,4), *nm_l, *pv, *tm;
        if(cv[i].group){ GtkWidget *gi=gtk_image_new_from_icon_name("system-users-symbolic"); cls(gi,"conv-preview"); gtk_box_append(GTK_BOX(t1),gi); }
        nm_l=label(cv[i].title,"conv-title"); gtk_label_set_ellipsize(GTK_LABEL(nm_l),PANGO_ELLIPSIZE_END); gtk_box_append(GTK_BOX(t1),nm_l);
        if(cv[i].trusted){ GtkWidget *sh=gtk_image_new_from_icon_name("security-high-symbolic"); g_ptr_array_add(icon_widgets,g_object_ref(sh)); cls(sh,"trusted"); gtk_widget_set_tooltip_text(sh,"已验证身份"); gtk_box_append(GTK_BOX(t1),sh); }
        pv=label(cv[i].preview[0]?cv[i].preview:" ","conv-preview"); gtk_label_set_ellipsize(GTK_LABEL(pv),PANGO_ELLIPSIZE_END);
        gtk_box_append(GTK_BOX(v),t1);
        if(cv[i].via){ /* second line: [cloud 经服务器] preview, so the name keeps the whole first line */ GtkWidget *l2=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6), *rb=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3), *ci=gtk_image_new_from_icon_name("lsm-cloud-symbolic");
            gtk_image_set_pixel_size(GTK_IMAGE(ci),12); g_ptr_array_add(icon_widgets,g_object_ref(ci)); gtk_box_append(GTK_BOX(rb),ci); gtk_box_append(GTK_BOX(rb),gtk_label_new("经服务器")); cls(rb,"relay-badge"); gtk_widget_set_tooltip_text(rb,"经服务器中转"); gtk_widget_set_valign(rb,GTK_ALIGN_CENTER);
            gtk_box_append(GTK_BOX(l2),rb); gtk_widget_set_hexpand(pv,TRUE); gtk_box_append(GTK_BOX(l2),pv); gtk_box_append(GTK_BOX(v),l2); }
        else gtk_box_append(GTK_BOX(v),pv); gtk_widget_set_hexpand(v,TRUE); gtk_widget_set_valign(v,GTK_ALIGN_CENTER);
        tm=label(cv[i].time,cv[i].unread?"conv-time unread":"conv-time"); gtk_label_set_xalign(GTK_LABEL(tm),1); gtk_box_append(GTK_BOX(r),tm);
        if(cv[i].unread){ char b[16]; GtkWidget *bd; snprintf(b,sizeof b,cv[i].unread>99?"99+":"%d",cv[i].unread); bd=gtk_label_new(b); cls(bd,"badge"); gtk_widget_set_halign(bd,GTK_ALIGN_END); gtk_box_append(GTK_BOX(r),bd); }
        gtk_widget_set_valign(r,GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(h),avatar(cv[i].title,cv[i].key,42,cv[i].online)); gtk_box_append(GTK_BOX(h),v); gtk_box_append(GTK_BOX(h),r); cls(h,"conv-row");
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row),h);
        g_object_set_data_full(G_OBJECT(row),"conv",g_strdup(cv[i].conv),g_free); g_object_set_data_full(G_OBJECT(row),"title",g_strdup(cv[i].title),g_free);
        gtk_list_box_append(GTK_LIST_BOX(conv_list),row); if(!strcmp(cv[i].conv,cur_conv)) sel=GTK_LIST_BOX_ROW(row); }
    if(sel) gtk_list_box_select_row(GTK_LIST_BOX(conv_list),sel);
    selecting=0; g_free(cv); }

/* ---------------------------------------------------------------- chat */
static void refresh_chat(void);
static void delete_do(gpointer id){ if(lsm_delete_message(C,(const char*)id)) toast("删除失败：消息不存在"); chat_dirty=list_dirty=1; }
static void bubble_action(GtkButton *b, gpointer p){ const char *what=g_object_get_data(G_OBJECT(b),"what"); GtkWidget *pop=gtk_widget_get_ancestor(GTK_WIDGET(b),GTK_TYPE_POPOVER);
    if(pop) gtk_popover_popdown(GTK_POPOVER(pop));
    if(!strcmp(what,"copy")) gdk_clipboard_set_text(gtk_widget_get_clipboard(win),(const char*)p);
    else if(!strcmp(what,"delete")) confirm("删除这条消息？","只会从本机删除，对方仍保留这条消息。此操作无法撤销。","删除",1,delete_do,g_strdup((const char*)p),g_free);
    else if(!strcmp(what,"open")){ gchar *u=g_filename_to_uri((const char*)p,NULL,NULL); if(u){ g_app_info_launch_default_for_uri(u,NULL,NULL); g_free(u); } } }
static void pop_closed(GtkPopover *p, gpointer d){ (void)d; g_idle_add_once((GSourceOnceFunc)gtk_widget_unparent,p); }
static GtkWidget *pop_btn(GtkWidget *box, const char *text, const char *what, const char *arg){ GtkWidget *b=gtk_button_new_with_mnemonic(text); cls(b,"flat"); gtk_widget_set_halign(gtk_button_get_child(GTK_BUTTON(b)),GTK_ALIGN_START);
    g_object_set_data(G_OBJECT(b),"what",(gpointer)what); g_signal_connect_data(b,"clicked",G_CALLBACK(bubble_action),g_strdup(arg),(GClosureNotify)g_free,0); gtk_box_append(GTK_BOX(box),b); return b; }
/* right-click / long-press on a bubble: 复制 · 打开 · 删除这条消息（仅本机）… */
static void bubble_menu(GtkWidget *bub, double x, double y){
    const char *id=g_object_get_data(G_OBJECT(bub),"id"), *body=g_object_get_data(G_OBJECT(bub),"body"), *path=g_object_get_data(G_OBJECT(bub),"path");
    GtkWidget *pop=gtk_popover_new(), *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,0); GdkRectangle r={(int)x,(int)y,1,1};
    if(body&&*body) pop_btn(box,"复制(_C)","copy",body);
    if(path&&*path) pop_btn(box,"打开(_O)","open",path);
    pop_btn(box,"删除这条消息（仅本机）(_D)…","delete",id);
    gtk_popover_set_child(GTK_POPOVER(pop),box); gtk_widget_set_parent(pop,bub); gtk_popover_set_pointing_to(GTK_POPOVER(pop),&r); gtk_popover_set_has_arrow(GTK_POPOVER(pop),FALSE);
    cls(pop,"menu"); g_signal_connect(pop,"closed",G_CALLBACK(pop_closed),NULL); gtk_popover_popup(GTK_POPOVER(pop)); if(getenv("LSM_UI_TEST")) g_timeout_add(300,test_log_later,(gpointer)"msg-menu"); }
static void bubble_rclick(GtkGestureClick *g, int n, double x, double y, gpointer p){ (void)n; (void)p; bubble_menu(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g)),x,y); }
static void bubble_lpress(GtkGestureLongPress *g, double x, double y, gpointer p){ (void)p; bubble_menu(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g)),x,y); }
static void bubble_click_open(GtkGestureClick *g, int n, double x, double y, gpointer p){ (void)n;(void)x;(void)y; (void)g; gchar *u=g_filename_to_uri((const char*)p,NULL,NULL); if(u){ g_app_info_launch_default_for_uri(u,NULL,NULL); g_free(u); } }
static GtkWidget *chip(const char *t){ GtkWidget *l=gtk_label_new(t); cls(l,"chip"); gtk_widget_set_halign(l,GTK_ALIGN_CENTER); gtk_label_set_wrap(GTK_LABEL(l),TRUE); return l; }
static void reveal_cb(GtkButton *b, gpointer p){ (void)b; gchar *d=g_path_get_dirname((const char*)p), *u=g_filename_to_uri(d,NULL,NULL); if(u) g_app_info_launch_default_for_uri(u,NULL,NULL); g_free(u); g_free(d); }
static void download_cb(GtkButton *b, gpointer p){ bubble_click_open(NULL,0,0,0,p); (void)b; }

static gboolean scroll_end(gpointer p){ GtkAdjustment *a=gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(chat_scroll)); (void)p; gtk_adjustment_set_value(a,gtk_adjustment_get_upper(a)); return G_SOURCE_REMOVE; }
static void refresh_chat(void){
    GtkWidget *c; lsm_msg *m; int n,i, first=1; char last_day[32]="", title[400]="", sub[400]="", key[64]="", prev_sender[80]=""; int online=0;
    while((c=gtk_widget_get_first_child(chat_box))) gtk_box_remove(GTK_BOX(chat_box),c);
    if(!cur_conv[0]){ gtk_stack_set_visible_child_name(GTK_STACK(content_stack),"empty"); gtk_label_set_text(GTK_LABEL(title_lbl),""); gtk_label_set_text(GTK_LABEL(sub_lbl),""); if(hdr_shield) gtk_widget_set_visible(hdr_shield,FALSE); if(hdr_more) gtk_widget_set_visible(hdr_more,FALSE); /* contact menu has nothing to act on */ /* app name is already in the sidebar header */ gtk_widget_set_visible(hdr_avatar,FALSE); return; }
    gtk_stack_set_visible_child_name(GTK_STACK(content_stack),"chat");
    lsm_lock(C);
    if(!strncmp(cur_conv,"dm:",3)){ lsm_contact *k; int nk=lsm_contacts(C,&k),j; for(j=0;j<nk;j++) if(!strcmp(k[j].fp,cur_conv+3)){ snprintf(title,sizeof title,"%s",k[j].name); snprintf(key,64,"%s",k[j].fp); online=k[j].online;
            hdr_via=k[j].online && VIA_RELAY(k[j]);
            if(k[j].trust==TRUST_TRUSTED && k[j].online) snprintf(sub,sizeof sub,"在线 · 已验证身份 · %s",VIA_RELAY(k[j])?"经服务器":"局域网直连"); else snprintf(sub,sizeof sub,"%s · %s",k[j].online?"在线":"离线",k[j].trust==TRUST_TRUSTED?"已验证身份":k[j].trust==TRUST_BLOCKED?"已屏蔽":"未信任"); } }
    else { lsm_group *g; int ng=lsm_groups(C,&g),j; char u[37]; for(j=0;j<ng;j++){ lsm_uuid_str(g[j].id,u); if(!strcmp(u,cur_conv+6)){ snprintf(title,sizeof title,"%s",g[j].name); snprintf(key,64,"%s",u); snprintf(sub,sizeof sub,"%d 人",g[j].nmembers); } } }
    n=lsm_messages(C,&m);
    for(i=0;i<n;i++){ GtkWidget *bub, *row, *meta, *grid; char day[32], ck[16], mt[64]; if(strcmp(m[i].conv,cur_conv)) continue;
        date_label(m[i].ts_ms,day,sizeof day); if(strcmp(day,last_day)){ gtk_box_append(GTK_BOX(chat_box),chip(day)); snprintf(last_day,sizeof last_day,"%s",day); prev_sender[0]=0; }
        if(first){ char e2e[400]; snprintf(e2e,sizeof e2e,strncmp(cur_conv,"dm:",3)?"消息已端到端加密，只有群成员能看到":"消息已端到端加密，只有你和%s能看到",title); gtk_box_append(GTK_BOX(chat_box),chip(e2e)); first=0; }
        if(m[i].kind==K_SYSTEM){ gtk_box_append(GTK_BOX(chat_box),chip(m[i].body?m[i].body:"")); prev_sender[0]=0; continue; }
        clock_txt(m[i].ts_ms,ck,sizeof ck); snprintf(mt,sizeof mt,"%s%s%s",ck,m[i].out?"  ":"",m[i].out?tick_txt(m[i].status):"");
        bub=gtk_box_new(GTK_ORIENTATION_VERTICAL,4); cls(bub,"bubble"); cls(bub,m[i].out?"mine":"theirs");
        { const char *who=m[i].out?"me":m[i].sender_fp; if(strcmp(prev_sender,who)) cls(bub,"first"); snprintf(prev_sender,sizeof prev_sender,"%s",who); }
        g_object_set_data_full(G_OBJECT(bub),"id",g_strdup(m[i].id),g_free);
        if(!m[i].out && strncmp(cur_conv,"dm:",3) && gtk_widget_has_css_class(bub,"first")){ GtkWidget *s=label(m[i].sender_name,"sender"); gtk_box_append(GTK_BOX(bub),s); }
        meta=gtk_label_new(mt); cls(meta,"meta"); gtk_widget_set_tooltip_text(meta,m[i].out?status_txt(m[i].status):NULL); gtk_widget_set_valign(meta,GTK_ALIGN_END); gtk_widget_set_halign(meta,GTK_ALIGN_END);
        if(m[i].kind==K_FILE){ const char *path=m[i].file_path[0]&&access(m[i].file_path,R_OK)==0?m[i].file_path:NULL;
            if(path) g_object_set_data_full(G_OBJECT(bub),"path",g_strdup(path),g_free);
            if(path && is_image(m[i].file_name) && gdk_pixbuf_get_file_info(path,NULL,NULL)){ GtkWidget *ov=gtk_overlay_new(), *pic;
                /* GtkPicture reports the image's own size as its natural size, so scale the pixels to the 318x218 cap first */
                GdkPixbuf *pb=gdk_pixbuf_new_from_file_at_scale(path,318,218,TRUE,NULL); if(pb){ GdkTexture *tx=gdk_texture_new_for_pixbuf(pb); pic=gtk_picture_new_for_paintable(GDK_PAINTABLE(tx));
                    gtk_widget_set_size_request(pic,gdk_pixbuf_get_width(pb),gdk_pixbuf_get_height(pb)); g_object_unref(tx); g_object_unref(pb); } else pic=gtk_picture_new_for_filename(path);
                gtk_picture_set_can_shrink(GTK_PICTURE(pic),TRUE); gtk_widget_set_halign(pic,GTK_ALIGN_START); gtk_widget_set_valign(pic,GTK_ALIGN_START); gtk_widget_set_valign(ov,GTK_ALIGN_START); gtk_widget_set_halign(ov,GTK_ALIGN_START);
                gtk_picture_set_content_fit(GTK_PICTURE(pic),GTK_CONTENT_FIT_COVER); cls(pic,"thumb"); gtk_overlay_set_child(GTK_OVERLAY(ov),pic);
                cls(meta,"on-image"); gtk_widget_set_margin_end(meta,6); gtk_widget_set_margin_bottom(meta,6); gtk_overlay_add_overlay(GTK_OVERLAY(ov),meta);
                { GtkGesture *g=gtk_gesture_click_new(); gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(g),1); g_signal_connect_data(g,"released",G_CALLBACK(bubble_click_open),g_strdup(path),(GClosureNotify)g_free,0); gtk_widget_add_controller(pic,GTK_EVENT_CONTROLLER(g)); }
                gtk_widget_set_tooltip_text(pic,"点击用默认程序打开"); gtk_box_append(GTK_BOX(bub),ov); cls(bub,"image"); gtk_widget_set_overflow(bub,GTK_OVERFLOW_HIDDEN); gtk_widget_set_overflow(ov,GTK_OVERFLOW_HIDDEN); if(getenv("LSM_UI_DEBUG")){ g_object_set_data(G_OBJECT(win),"dbg-pic",pic); g_object_set_data(G_OBJECT(win),"dbg-ov",ov); g_object_set_data(G_OBJECT(win),"dbg-bub",bub); } }
            else { GtkWidget *top=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12), *tile, *nm, *sz, *v=gtk_box_new(GTK_ORIENTATION_VERTICAL,2), *bottom=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8); char ext[16]="", szs[220];
                { const char *e=strrchr(m[i].file_name,'.'); if(e){ snprintf(ext,sizeof ext,"%s",e+1); for(char *q=ext;*q;q++) *q=g_ascii_toupper(*q); } }
                tile=gtk_label_new(ext[0]?ext:"FILE"); cls(tile,"tile"); gtk_widget_set_size_request(tile,42,42); gtk_widget_set_valign(tile,GTK_ALIGN_CENTER);
                nm=label(m[i].file_name,"file-name"); gtk_label_set_ellipsize(GTK_LABEL(nm),PANGO_ELLIPSIZE_MIDDLE);
                if(m[i].file_size>=1048576) snprintf(szs,sizeof szs,"%.1f MB · %s",m[i].file_size/1048576.0,ext); else snprintf(szs,sizeof szs,"%.1f KB · %s",m[i].file_size/1024.0,ext);
                double frac=-1; int paused=0; (void)paused;
#ifdef LSM_HAVE_RELAY
                { lsm_transfer *tx; int nt=lsm_transfers(C,&tx), q; for(q=0;q<nt;q++) if(!strcmp(tx[q].msg_id,m[i].id)){ size_t l=strlen(szs);
                    if(tx[q].size && (tx[q].state==1 || (tx[q].state==0 && tx[q].resumable && tx[q].error[0]))){ frac=(double)tx[q].transferred/tx[q].size; paused=tx[q].state==0; }
                    if(tx[q].state==0 && tx[q].resumable && tx[q].error[0]) snprintf(szs+l,sizeof szs-l," · 连接中断，重连后自动续传");
                    else if(tx[q].state==1 && tx[q].resumed_from>0 && tx[q].size) snprintf(szs+l,sizeof szs-l," · 从 %d%%（%.1f MB）处续传 · %d%%",(int)(tx[q].resumed_from*100/tx[q].size),tx[q].resumed_from/1048576.0,(int)(tx[q].transferred*100/tx[q].size));
                    else if(tx[q].state==1 && tx[q].size) snprintf(szs+l,sizeof szs-l," · 传输中 %d%%",(int)(tx[q].transferred*100/tx[q].size)); } }
#endif
                sz=label(szs,"meta"); gtk_box_append(GTK_BOX(v),nm); gtk_box_append(GTK_BOX(v),sz);
                if(frac>=0){ GtkWidget *pb=gtk_progress_bar_new(); gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pb),frac); cls(pb,"xfer"); if(paused) cls(pb,"paused"); gtk_box_append(GTK_BOX(v),pb); } /* thin themed bar, like WinUI/Android/XP */ gtk_widget_set_hexpand(v,TRUE); gtk_widget_set_valign(v,GTK_ALIGN_CENTER);
                gtk_box_append(GTK_BOX(top),tile); gtk_box_append(GTK_BOX(top),v);
                if(!m[i].out){ GtkWidget *dl=gtk_button_new_from_icon_name("folder-download-symbolic"); cls(dl,"circular"); cls(dl,"download"); gtk_widget_set_valign(dl,GTK_ALIGN_CENTER);
                    gtk_widget_set_tooltip_text(dl,path?"打开已下载的文件":"正在接收…"); gtk_widget_set_sensitive(dl,path!=NULL); if(path) g_signal_connect_data(dl,"clicked",G_CALLBACK(download_cb),g_strdup(path),(GClosureNotify)g_free,0); gtk_box_append(GTK_BOX(top),dl); }
                gtk_box_append(GTK_BOX(bub),top);
                if(path){ GtkWidget *rv=gtk_button_new_with_label("在文件夹中显示"); cls(rv,"link"); cls(rv,"flat"); g_signal_connect_data(rv,"clicked",G_CALLBACK(reveal_cb),g_strdup(path),(GClosureNotify)g_free,0); gtk_box_append(GTK_BOX(bottom),rv); }
                { GtkWidget *sp=gtk_label_new(NULL); gtk_widget_set_hexpand(sp,TRUE); gtk_box_append(GTK_BOX(bottom),sp); } gtk_box_append(GTK_BOX(bottom),meta);
                gtk_box_append(GTK_BOX(bub),bottom); cls(bub,"file"); } }
        else { GtkWidget *body=gtk_label_new(m[i].body?m[i].body:""); grid=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12);
            gtk_label_set_wrap(GTK_LABEL(body),TRUE); gtk_label_set_wrap_mode(GTK_LABEL(body),PANGO_WRAP_WORD_CHAR); gtk_label_set_xalign(GTK_LABEL(body),0); gtk_label_set_max_width_chars(GTK_LABEL(body),40); gtk_label_set_selectable(GTK_LABEL(body),TRUE);
            gtk_widget_set_hexpand(body,TRUE); gtk_box_append(GTK_BOX(grid),body); gtk_box_append(GTK_BOX(grid),meta); gtk_box_append(GTK_BOX(bub),grid);
            g_object_set_data_full(G_OBJECT(bub),"body",g_strdup(m[i].body?m[i].body:""),g_free); }
        { GtkGesture *g=gtk_gesture_click_new(); gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(g),3); gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(g),GTK_PHASE_CAPTURE);
          g_signal_connect(g,"pressed",G_CALLBACK(bubble_rclick),NULL); gtk_widget_add_controller(bub,GTK_EVENT_CONTROLLER(g));
          g=gtk_gesture_long_press_new(); gtk_gesture_single_set_touch_only(GTK_GESTURE_SINGLE(g),TRUE); g_signal_connect(g,"pressed",G_CALLBACK(bubble_lpress),NULL); gtk_widget_add_controller(bub,GTK_EVENT_CONTROLLER(g)); }
        /* the row (not the bubble) is aligned: a hexpanding bubble would be asked for its height at the full row width, which stretches image bubbles */
        row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0); gtk_widget_set_halign(row,m[i].out?GTK_ALIGN_END:GTK_ALIGN_START); gtk_box_append(GTK_BOX(row),bub); cls(row,"msg-row");
        gtk_box_append(GTK_BOX(chat_box),row); }
    lsm_unlock(C);
    gtk_label_set_text(GTK_LABEL(title_lbl),title); gtk_label_set_text(GTK_LABEL(sub_lbl),sub); if(hdr_cloud){ gtk_widget_set_visible(hdr_cloud,!strncmp(cur_conv,"dm:",3) && hdr_via); if(hdr_via && getenv("LSM_UI_TEST")) g_ptr_array_add(icon_widgets,g_object_ref(hdr_cloud)); }
    gtk_widget_set_visible(hdr_shield,strstr(sub,"已验证身份")!=NULL); gtk_widget_set_visible(hdr_more,TRUE);  /* green shield next to the name (mockup); the words stay in the subtitle */
    { GtkWidget *old=gtk_widget_get_first_child(hdr_avatar); if(old) gtk_box_remove(GTK_BOX(hdr_avatar),old); gtk_box_append(GTK_BOX(hdr_avatar),avatar(title,key,36,online)); gtk_widget_set_visible(hdr_avatar,TRUE); }
    g_idle_add(scroll_end,NULL); }

/* ---------------------------------------------------------------- actions */
static void select_conv(const char *cv){ snprintf(cur_conv,sizeof cur_conv,"%s",cv); refresh_chat(); if(lsm_mark_read(C,cur_conv)) list_dirty=1;
    adw_navigation_split_view_set_show_content(ADW_NAVIGATION_SPLIT_VIEW(split),TRUE); gtk_widget_grab_focus(input); }
static void on_row(GtkListBox *lb, GtkListBoxRow *row, gpointer p){ const char *cv; (void)lb; (void)p; if(selecting||!row) return; cv=g_object_get_data(G_OBJECT(row),"conv"); if(cv && strcmp(cv,cur_conv)) select_conv(cv); }
static void cycle(int d){ int n=0, cur=-1, i; GtkListBoxRow *r;
    for(i=0;(r=gtk_list_box_get_row_at_index(GTK_LIST_BOX(conv_list),i));i++){ if(!gtk_widget_get_child_visible(GTK_WIDGET(r))) continue; n++; if(!strcmp(g_object_get_data(G_OBJECT(r),"conv"),cur_conv)) cur=i; }
    if(!n) return; i=cur<0?(d>0?0:n-1):((cur+d)%n+n)%n; r=gtk_list_box_get_row_at_index(GTK_LIST_BOX(conv_list),i); if(r){ gtk_list_box_select_row(GTK_LIST_BOX(conv_list),r); select_conv(g_object_get_data(G_OBJECT(r),"conv")); } }
static void send_current(void){ GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(input)); GtkTextIter s,e; char *t, err[256]="", id[40];
    if(!cur_conv[0]) return; gtk_text_buffer_get_bounds(b,&s,&e); t=gtk_text_buffer_get_text(b,&s,&e,FALSE);
    if(t && *g_strstrip(t)){ if(lsm_send_text(C,cur_conv,t,id,err)) info("发送失败",err); else gtk_text_buffer_set_text(b,"",-1); }
    g_free(t); chat_dirty=list_dirty=1; }
static gboolean input_key(GtkEventControllerKey *k, guint kv, guint code, GdkModifierType st, gpointer p){ (void)k;(void)code;(void)p;
    if((kv==GDK_KEY_Return||kv==GDK_KEY_KP_Enter) && !(st&GDK_SHIFT_MASK)){ send_current(); return TRUE; } return FALSE; }
static void buf_changed(GtkTextBuffer *b, gpointer p){ gboolean empty=gtk_text_buffer_get_char_count(b)==0; (void)p; gtk_widget_set_visible(placeholder,empty); gtk_widget_set_visible(hint,empty); }
static void send_path(const char *path){ char err[256]=""; if(!cur_conv[0]){ info("未选择会话","请先在左侧选择联系人或群组。"); return; } if(lsm_send_file(C,cur_conv,path,err)) info("无法发送文件",err); chat_dirty=1; }
static void files_picked(GObject *src, GAsyncResult *res, gpointer p){ GListModel *l=gtk_file_dialog_open_multiple_finish(GTK_FILE_DIALOG(src),res,NULL); guint i; (void)p; if(!l) return;
    for(i=0;i<g_list_model_get_n_items(l);i++){ GFile *f=g_list_model_get_item(l,i); char *ph=g_file_get_path(f); if(ph){ send_path(ph); g_free(ph); } g_object_unref(f); } g_object_unref(l); }
static gboolean on_drop(GtkDropTarget *t, const GValue *v, double x, double y, gpointer p){ GSList *l, *it; (void)t;(void)x;(void)y;(void)p; gtk_widget_set_visible(drop_hint,FALSE);
    if(!G_VALUE_HOLDS(v,GDK_TYPE_FILE_LIST)) return FALSE; l=gdk_file_list_get_files(g_value_get_boxed(v));
    for(it=l;it;it=it->next){ char *ph=g_file_get_path(it->data); if(ph){ send_path(ph); g_free(ph); } } g_slist_free(l); return TRUE; }
static GdkDragAction drop_enter(GtkDropTarget *t, double x, double y, gpointer p){ (void)t;(void)x;(void)y;(void)p; if(cur_conv[0]) gtk_widget_set_visible(drop_hint,TRUE); return GDK_ACTION_COPY; }
static void drop_leave(GtkDropTarget *t, gpointer p){ (void)t;(void)p; gtk_widget_set_visible(drop_hint,FALSE); }

static void add_contact_go(void){ const char *s=gtk_editable_get_text(GTK_EDITABLE(add_entry)); char err[256]=""; if(!s||!*s) return;
    if(lsm_add_addr(C,s,err)) info("无法添加",err); else { gtk_editable_set_text(GTK_EDITABLE(add_entry),""); gtk_popover_popdown(GTK_POPOVER(add_popover)); toast("正在连接…"); } list_dirty=1; }
static void trust_do(gpointer t){ char err[256]=""; if(!strncmp(cur_conv,"dm:",3)) lsm_set_trust(C,cur_conv+3,GPOINTER_TO_INT(t),err); list_dirty=chat_dirty=1; }
static void details(void){ char b[2400]="", fp[200]=""; if(!cur_conv[0]) return; lsm_lock(C);
    if(!strncmp(cur_conv,"dm:",3)){ lsm_contact *k; int nk=lsm_contacts(C,&k),j; for(j=0;j<nk;j++) if(!strcmp(k[j].fp,cur_conv+3)){ int q=0; const char *f=k[j].fp; for(;*f;f++){ if(*f=='-') continue; if(q&&q%4==0) strcat(fp," "); strncat(fp,f,1); q++; }
            char rt[32]=""; if(k[j].online&&k[j].rtt_ms>=0) snprintf(rt,sizeof rt," · %d ms",k[j].rtt_ms);
            snprintf(b,sizeof b,"状态：%s%s\n身份：%s\n连接：%s\n地址：%s\n版本：%s\n指纹：%s",k[j].online?"在线":"离线",rt,k[j].trust==TRUST_TRUSTED?"已验证身份":k[j].trust==TRUST_BLOCKED?"已屏蔽":"未验证",
                VIA_RELAY(k[j])?"经服务器中转":"局域网直连",k[j].addr[0]?k[j].addr:"—",k[j].app_version[0]?k[j].app_version:"—",fp); } }
    else { lsm_group *g; int ng=lsm_groups(C,&g),j; char u[37]; for(j=0;j<ng;j++){ lsm_uuid_str(g[j].id,u); if(!strcmp(u,cur_conv+6)) snprintf(b,sizeof b,"成员：%d 人\n密钥版本：%llu\n群组 ID：%s",g[j].nmembers,(unsigned long long)g[j].key_epoch,u); } }
    lsm_unlock(C); info(strncmp(cur_conv,"dm:",3)?"群组详情":"联系人详情",b); }
static void pin_show(void){ char pin[8], b[300]; lsm_pin_offer(C,pin); snprintf(b,sizeof b,"%s\n\n让对方在“输入 PIN 配对”中输入（5 分钟内有效）。",pin); info("请把这个 PIN 告诉对方",b); }

static void log_status_icon(GtkWidget *w){ /* AdwStatusPage illustration: large symbolic GtkImage, decorative (title + description carry the meaning) */
    for(GtkWidget *c=gtk_widget_get_first_child(w);c;c=gtk_widget_get_next_sibling(c)){ graphene_rect_t r;
        if(GTK_IS_IMAGE(c)){ if(gtk_widget_get_mapped(c) && gtk_widget_compute_bounds(c,win,&r)) printf("ICON status-page %d,%d,%d,%d\n",(int)r.origin.x-2,(int)r.origin.y-2,(int)r.size.width+4,(int)r.size.height+4); }
        else log_status_icon(c); } }
static void test_log(const char *what){
    /* decorative icons (GtkImage, no text) for the colour audit's ignore list; printed with every UI log line */
    if(getenv("LSM_UI_TEST") && hdr_avatar){ /* header avatar's online dot is rebuilt with the chat, outside icon_widgets */
        for(GtkWidget *a=gtk_widget_get_first_child(hdr_avatar);a;a=gtk_widget_get_next_sibling(a)) for(GtkWidget *d=gtk_widget_get_first_child(a);d;d=gtk_widget_get_next_sibling(d)){ graphene_rect_t r;
            if(gtk_widget_has_css_class(d,"online-dot") && gtk_widget_get_mapped(d) && gtk_widget_compute_bounds(d,win,&r)) printf("ICON online-dot %d,%d,%d,%d\n",(int)r.origin.x-2,(int)r.origin.y-2,(int)r.size.width+4,(int)r.size.height+4); } }
    if(getenv("LSM_UI_TEST") && empty_page && gtk_widget_get_mapped(empty_page)) log_status_icon(empty_page);
    if(getenv("LSM_UI_TEST") && icon_widgets){ if(hdr_shield && !g_ptr_array_find(icon_widgets,hdr_shield,NULL)) g_ptr_array_add(icon_widgets,g_object_ref(hdr_shield)); for(guint q=0;q<icon_widgets->len;q++){ graphene_rect_t r; GtkWidget *w=icon_widgets->pdata[q];
        if(gtk_widget_get_mapped(w) && gtk_widget_compute_bounds(w,win,&r)) printf("ICON %s %d,%d,%d,%d\n",GTK_IS_IMAGE(w)?gtk_image_get_icon_name(GTK_IMAGE(w)):"online-dot",(int)r.origin.x-2,(int)r.origin.y-2,(int)r.size.width+4,(int)r.size.height+4); } } const char *t=getenv("LSM_UI_TEST");
    if(getenv("LSM_UI_DEBUG")){ const char *nm[]={"dbg-tv","dbg-frame","dbg-comp","dbg-hint"}; for(int q=0;q<4;q++){ GtkWidget *w=g_object_get_data(G_OBJECT(win),nm[q]); int mn,nat; if(!w) continue; gtk_widget_measure(w,GTK_ORIENTATION_VERTICAL,-1,&mn,&nat,NULL,NULL); printf("DBG %s h=%d min=%d nat=%d\n",nm[q],gtk_widget_get_height(w),mn,nat); } { int mn,nat; gtk_widget_measure(input,GTK_ORIENTATION_VERTICAL,-1,&mn,&nat,NULL,NULL); printf("DBG input h=%d min=%d nat=%d\n",gtk_widget_get_height(input),mn,nat); } }
    if(getenv("LSM_UI_DEBUG")){ GtkWidget *a=g_object_get_data(G_OBJECT(win),"dbg-pic"),*b=g_object_get_data(G_OBJECT(win),"dbg-ov"),*c=g_object_get_data(G_OBJECT(win),"dbg-bub"); if(a&&b&&c) for(GtkWidget *ch=gtk_widget_get_first_child(c);ch;ch=gtk_widget_get_next_sibling(ch)) printf("DBG child %s %dx%d\n",G_OBJECT_TYPE_NAME(ch),gtk_widget_get_width(ch),gtk_widget_get_height(ch)); printf("DBG pic %dx%d ov %dx%d bub %dx%d\n",gtk_widget_get_width(a),gtk_widget_get_height(a),gtk_widget_get_width(b),gtk_widget_get_height(b),gtk_widget_get_width(c),gtk_widget_get_height(c)); }
    if(t){ printf("UI %s conv=%s focus=%s\n",what,cur_conv,
        gtk_window_get_focus(GTK_WINDOW(win))?G_OBJECT_TYPE_NAME(gtk_window_get_focus(GTK_WINDOW(win))):"-"); fflush(stdout); } }
static gboolean test_log_later(gpointer p){ test_log((const char*)p); return G_SOURCE_REMOVE; }
/* ---------------------------------------------------------------- 中转服务器（可选） (strings shared with WinUI / Android / XP: relay/NOTES.md) */
static GtkWidget *rl_win, *rl_sw, *rl_host, *rl_port, *rl_pass, *rl_passrow, *rl_status; static int rl_clear;
static void relay_status_text(char *o, size_t n){ char det[256]="";
#ifdef LSM_HAVE_RELAY
    char host[256]=""; int en=0, port=0, hp=0, peers; const char *st; lsm_relay_get(C,&en,host,sizeof host,&port,&hp); lsm_lock(C); st=lsm_relay_status(C,det,sizeof det); peers=lsm_relay_peers(C); lsm_unlock(C);
    const char *t=!strcmp(st,"online")?"已连接中转服务器":!strcmp(st,"connecting")?"正在连接中转服务器…":!strcmp(st,"auth_failed")?"网络口令错误":!strcmp(st,"error")?"无法连接中转服务器":"未启用";
    if(!strcmp(st,"online")) snprintf(o,n,"%s · %s:%d · 服务器上 %d 人",t,host,port,peers); else if(det[0]&&strcmp(st,"off")) snprintf(o,n,"%s · %s",t,det); else snprintf(o,n,"%s",t);
#else
    (void)det; snprintf(o,n,"未启用");
#endif
}
static void relay_refresh(void){ char s[400]; if(!rl_status) return; relay_status_text(s,sizeof s); gtk_label_set_text(GTK_LABEL(rl_status),s); }
static GtkWidget *rl_passfield; /* the hint sits in the row subtitle: GTK's entry placeholder is drawn at reduced alpha (APCA |Lc| 55 in dark) */
static void relay_pass_placeholder(int hp){ adw_action_row_set_subtitle(ADW_ACTION_ROW(rl_passfield),hp?"已设置（留空则保持不变）":"未设置"); gtk_widget_set_visible(rl_passrow,hp); }
static void relay_clear_cb(GtkButton *b, gpointer p){ (void)b;(void)p; rl_clear=1; relay_pass_placeholder(0); toast("已清除网络口令"); }
static void relay_save_cb(GtkButton *b, gpointer p){ (void)b;(void)p;
#ifdef LSM_HAVE_RELAY
    char err[256]=""; const char *h=gtk_editable_get_text(GTK_EDITABLE(rl_host)), *ps=gtk_editable_get_text(GTK_EDITABLE(rl_pass)); int en=gtk_switch_get_active(GTK_SWITCH(rl_sw)), port=atoi(gtk_editable_get_text(GTK_EDITABLE(rl_port)));
    if(en && !*h){ gtk_label_set_text(GTK_LABEL(rl_status),"请填写服务器地址"); gtk_widget_grab_focus(rl_host); return; }
    if(en && (port<1||port>65535)){ gtk_label_set_text(GTK_LABEL(rl_status),"端口无效"); gtk_widget_grab_focus(rl_port); return; }
    if(lsm_relay_set(C,en,h,port?port:9443,*ps?ps:(rl_clear?"":NULL),err)){ gtk_label_set_text(GTK_LABEL(rl_status),err); return; }
    if(*ps) relay_pass_placeholder(1); rl_clear=0; gtk_editable_set_text(GTK_EDITABLE(rl_pass),"");
    gtk_label_set_text(GTK_LABEL(rl_status),en?"中转服务器设置已保存，正在连接…":"已关闭中转服务器"); toast(en?"中转服务器设置已保存，正在连接…":"已关闭中转服务器"); list_dirty=1;
#endif
}
static void relay_sw_changed(GObject *o, GParamSpec *ps, gpointer r){ (void)ps; adw_action_row_set_subtitle(ADW_ACTION_ROW(r),gtk_switch_get_active(GTK_SWITCH(o))?"局域网直连不可用时经服务器中转":"关闭：只使用局域网直连"); }
static void relay_closed(GtkWindow *w, gpointer p){ (void)w;(void)p; rl_win=rl_status=NULL; }
static GtkWidget *rl_row(GtkWidget *grp, const char *title, const char *sub, GtkWidget *suffix){ GtkWidget *r=adw_action_row_new(); adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r),title); if(sub) adw_action_row_set_subtitle(ADW_ACTION_ROW(r),sub);
    gtk_widget_set_valign(suffix,GTK_ALIGN_CENTER); adw_action_row_add_suffix(ADW_ACTION_ROW(r),suffix); adw_preferences_group_add(ADW_PREFERENCES_GROUP(grp),r); return r; }
static void relay_dialog(void){ GtkWidget *tv, *hb, *pg, *grp, *grp2, *box, *btn, *clr; char host[256]="", ps[16]; int en=0, port=9443, hp=0;
    if(rl_win){ gtk_window_present(GTK_WINDOW(rl_win)); return; }
#ifdef LSM_HAVE_RELAY
    lsm_relay_get(C,&en,host,sizeof host,&port,&hp);
#endif
    rl_clear=0; rl_win=adw_window_new(); gtk_window_set_title(GTK_WINDOW(rl_win),"中转服务器（可选）"); gtk_window_set_transient_for(GTK_WINDOW(rl_win),GTK_WINDOW(win)); gtk_window_set_modal(GTK_WINDOW(rl_win),TRUE); gtk_window_set_default_size(GTK_WINDOW(rl_win),520,560);
    g_signal_connect(rl_win,"destroy",G_CALLBACK(relay_closed),NULL); cls(rl_win,"lsm-relay");
    tv=gtk_box_new(GTK_ORIENTATION_VERTICAL,0); hb=adw_header_bar_new(); gtk_box_append(GTK_BOX(tv),hb);
    pg=adw_preferences_page_new(); gtk_widget_set_vexpand(pg,TRUE); gtk_box_append(GTK_BOX(tv),pg);
    grp=adw_preferences_group_new();
    { GtkWidget *g0=adw_preferences_group_new(), *dl=label("不在同一局域网时，可通过你自己部署的中转服务器联系对方。局域网直连始终优先；服务器只转发端到端加密的数据，无法看到消息内容。","relay-status");
      gtk_label_set_wrap(GTK_LABEL(dl),TRUE); adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(g0),"中转服务器（可选）"); adw_preferences_group_add(ADW_PREFERENCES_GROUP(g0),dl); adw_preferences_page_add(ADW_PREFERENCES_PAGE(pg),ADW_PREFERENCES_GROUP(g0)); }
    rl_sw=gtk_switch_new(); gtk_switch_set_active(GTK_SWITCH(rl_sw),en); gtk_widget_set_tooltip_text(rl_sw,"关闭时只使用局域网直连");
    { GtkWidget *r=rl_row(grp,"通过中转服务器连接",en?"局域网直连不可用时经服务器中转":"关闭：只使用局域网直连",rl_sw); adw_action_row_set_activatable_widget(ADW_ACTION_ROW(r),rl_sw); g_signal_connect(rl_sw,"notify::active",G_CALLBACK(relay_sw_changed),r); }
    { GtkEventController *ek=gtk_shortcut_controller_new(); gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(ek),gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_Escape,0),gtk_named_action_new("window.close"))); gtk_widget_add_controller(rl_win,ek); }
    rl_host=gtk_entry_new(); gtk_editable_set_text(GTK_EDITABLE(rl_host),host); gtk_entry_set_placeholder_text(GTK_ENTRY(rl_host),"relay.example.com"); gtk_widget_set_size_request(rl_host,240,-1); rl_row(grp,"服务器地址",NULL,rl_host);
    snprintf(ps,sizeof ps,"%d",port?port:9443); rl_port=gtk_entry_new(); gtk_editable_set_text(GTK_EDITABLE(rl_port),ps); gtk_entry_set_input_purpose(GTK_ENTRY(rl_port),GTK_INPUT_PURPOSE_DIGITS); gtk_editable_set_width_chars(GTK_EDITABLE(rl_port),6); rl_row(grp,"端口",NULL,rl_port);
    rl_pass=gtk_entry_new(); gtk_entry_set_visibility(GTK_ENTRY(rl_pass),FALSE); gtk_entry_set_input_purpose(GTK_ENTRY(rl_pass),GTK_INPUT_PURPOSE_PASSWORD); gtk_widget_set_size_request(rl_pass,240,-1); rl_passfield=rl_row(grp,"网络口令",NULL,rl_pass);
    clr=gtk_button_new_with_label("清除口令"); cls(clr,"flat"); cls(clr,"link"); g_signal_connect(clr,"clicked",G_CALLBACK(relay_clear_cb),NULL); rl_passrow=rl_row(grp,"口令已设置",NULL,clr);
    relay_pass_placeholder(hp); adw_preferences_page_add(ADW_PREFERENCES_PAGE(pg),ADW_PREFERENCES_GROUP(grp));
    grp2=adw_preferences_group_new(); box=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12); rl_status=label("","relay-status live"); gtk_label_set_wrap(GTK_LABEL(rl_status),TRUE); gtk_widget_set_hexpand(rl_status,TRUE); gtk_box_append(GTK_BOX(box),rl_status);
    btn=gtk_button_new_with_label("保存并连接"); cls(btn,"suggested-action"); cls(btn,"pill"); g_signal_connect(btn,"clicked",G_CALLBACK(relay_save_cb),NULL); gtk_box_append(GTK_BOX(box),btn);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(grp2),box); adw_preferences_page_add(ADW_PREFERENCES_PAGE(pg),ADW_PREFERENCES_GROUP(grp2));
    g_signal_connect_swapped(rl_pass,"activate",G_CALLBACK(relay_save_cb),NULL);
    adw_window_set_content(ADW_WINDOW(rl_win),tv); relay_refresh(); gtk_window_present(GTK_WINDOW(rl_win)); if(getenv("LSM_UI_TEST")) g_timeout_add(600,test_log_later,(gpointer)"relay"); }

static gboolean relay_test_fill(gpointer p){ /* LSM_UI_TEST_RELAY="host port pass": open the dialog, fill it and press 保存并连接 like a user */ char h[256], ps[256]; int port;
    if(sscanf((const char*)p,"%255s %d %255s",h,&port,ps)!=3) return G_SOURCE_REMOVE; relay_dialog(); { char b[16]; snprintf(b,sizeof b,"%d",port); gtk_switch_set_active(GTK_SWITCH(rl_sw),TRUE); gtk_editable_set_text(GTK_EDITABLE(rl_host),h); gtk_editable_set_text(GTK_EDITABLE(rl_port),b); gtk_editable_set_text(GTK_EDITABLE(rl_pass),ps); }
    relay_save_cb(NULL,NULL); return G_SOURCE_REMOVE; }
/* LSM_UI_TEST_DROP=<path> (test only): once a chat is open, emit "drop" on the chat's real GtkDropTarget with a
   GdkFileList, the same signal + value a desktop drag & drop delivers (G8) */
static GtkDropTarget *test_dt;
static void test_drop(void){ static int done; const char *pth=getenv("LSM_UI_TEST_DROP"); GSList *l; GdkFileList *fl; GValue v=G_VALUE_INIT; gboolean ret=FALSE;
    if(done || !pth || !test_dt || !cur_conv[0]) return; done=1;
    l=g_slist_append(NULL,g_file_new_for_path(pth)); fl=gdk_file_list_new_from_list(l); g_value_init(&v,GDK_TYPE_FILE_LIST); g_value_take_boxed(&v,fl);
    g_signal_emit_by_name(test_dt,"drop",&v,10.0,10.0,&ret); g_value_unset(&v); g_slist_free_full(l,g_object_unref); printf("TESTDROP %s ret=%d\n",pth,ret); fflush(stdout); }
static gboolean ui_state_tick(gpointer p){ /* LSM_UI_TEST: open dialogs / popover / window size, for the basics test (Esc closes, window memory) */ int nd=0; GListModel *tl=gtk_window_get_toplevels(); guint i; (void)p;
    for(i=0;i<g_list_model_get_n_items(tl);i++){ GtkWindow *t=g_list_model_get_item(tl,i); if(t!=GTK_WINDOW(win) && gtk_widget_get_visible(GTK_WIDGET(t))) nd++; g_object_unref(t); }
#if ADW_CHECK_VERSION(1,5,0)
    if(adw_application_window_get_visible_dialog(ADW_APPLICATION_WINDOW(win))) nd++;
#endif
    test_drop();
    printf("UISTATE dialogs=%d addpop=%d size=%dx%d visible=%d conv=%s\n",nd,add_popover&&gtk_widget_get_visible(add_popover),gtk_widget_get_width(win),gtk_widget_get_height(win),gtk_widget_get_visible(win),cur_conv[0]?cur_conv:"-"); fflush(stdout); return G_SOURCE_CONTINUE; }
static void win_act(GSimpleAction *a, GVariant *v, gpointer p){ const char *n=g_action_get_name(G_ACTION(a)); (void)v;(void)p;
    g_timeout_add(400,test_log_later,(gpointer)n);
    if(!strcmp(n,"search")){ adw_navigation_split_view_set_show_content(ADW_NAVIGATION_SPLIT_VIEW(split),FALSE); gtk_widget_grab_focus(search); }
    else if(!strcmp(n,"add-contact")){ adw_navigation_split_view_set_show_content(ADW_NAVIGATION_SPLIT_VIEW(split),FALSE); gtk_popover_popup(GTK_POPOVER(add_popover)); gtk_widget_grab_focus(add_entry); }
    else if(!strcmp(n,"relay")) relay_dialog();
    else if(!strcmp(n,"next-conv")) cycle(+1);
    else if(!strcmp(n,"prev-conv")) cycle(-1);
    else if(!strcmp(n,"send-file")){ if(cur_conv[0]){ GtkFileDialog *d=gtk_file_dialog_new(); gtk_file_dialog_set_title(d,"选择要发送的文件"); gtk_file_dialog_open_multiple(d,GTK_WINDOW(win),NULL,files_picked,NULL); g_object_unref(d); } }
    else if(!strcmp(n,"details")) details();
    else if(!strcmp(n,"trust")) trust_do(GINT_TO_POINTER(TRUST_TRUSTED));
    else if(!strcmp(n,"untrust")) trust_do(GINT_TO_POINTER(TRUST_UNKNOWN));
    else if(!strcmp(n,"block")){ if(!strncmp(cur_conv,"dm:",3)) confirm("屏蔽这个联系人？","屏蔽后将不再接收对方的消息和文件。可随时取消屏蔽。","屏蔽",1,trust_do,GINT_TO_POINTER(TRUST_BLOCKED),NULL); }
    else if(!strcmp(n,"pin-offer")) pin_show();
    else if(!strcmp(n,"downloads")){ gchar *u; lsm_lock(C); u=g_filename_to_uri(lsm_download_dir(C),NULL,NULL); lsm_unlock(C); if(u){ g_app_info_launch_default_for_uri(u,NULL,NULL); g_free(u); } }
    else if(!strcmp(n,"menu")) gtk_menu_button_popup(GTK_MENU_BUTTON(menu_btn));
    else if(!strcmp(n,"close")) gtk_window_close(GTK_WINDOW(win));
    else if(!strcmp(n,"about")){ char b[400]; snprintf(b,sizeof b,"版本 %s（协议 v5）\n与 Windows v0.5、XP 版和 Android 版互通。\nGTK %d.%d · libadwaita %d.%d",LSM_APP_VERSION,gtk_get_major_version(),gtk_get_minor_version(),adw_get_major_version(),adw_get_minor_version()); info("局域网安全通讯",b); }
    else if(!strcmp(n,"quit")){ save_window(); /* 退出 / Ctrl+Q also remembers the window */ if(held){ g_application_release(G_APPLICATION(app)); held=0; } g_application_quit(G_APPLICATION(app)); } }

/* ---------------------------------------------------------------- window memory + close-to-background */
static void prefs_path(char *o, size_t n){ snprintf(o,n,"%s/window.ini",data_dir); }
static void save_window(void){ GKeyFile *k=g_key_file_new(); char p[1100]; int w,h; prefs_path(p,sizeof p); g_key_file_load_from_file(k,p,0,NULL);
    gtk_window_get_default_size(GTK_WINDOW(win),&w,&h); g_key_file_set_integer(k,"window","width",w); g_key_file_set_integer(k,"window","height",h);
    g_key_file_set_boolean(k,"window","maximized",gtk_window_is_maximized(GTK_WINDOW(win))); g_key_file_set_boolean(k,"window","background_notice_shown",bg_notice_shown);
    g_key_file_save_to_file(k,p,NULL); g_key_file_unref(k); }
static void load_window(void){ GKeyFile *k=g_key_file_new(); char p[1100]; prefs_path(p,sizeof p);
    if(g_key_file_load_from_file(k,p,0,NULL)){ int w=g_key_file_get_integer(k,"window","width",NULL), h=g_key_file_get_integer(k,"window","height",NULL);
        if(w>=360&&h>=400) gtk_window_set_default_size(GTK_WINDOW(win),w,h); if(g_key_file_get_boolean(k,"window","maximized",NULL)) gtk_window_maximize(GTK_WINDOW(win));
        bg_notice_shown=g_key_file_get_boolean(k,"window","background_notice_shown",NULL); }
    g_key_file_unref(k); }
static gboolean on_close(GtkWindow *w, gpointer p){ (void)p; save_window(); if(getenv("LSM_QUIT_ON_CLOSE")) return FALSE;
    if(!held){ g_application_hold(G_APPLICATION(app)); held=1; } gtk_widget_set_visible(GTK_WIDGET(w),FALSE);
    if(!bg_notice_shown){ notify("局域网安全通讯","程序仍在后台运行，可以继续接收消息；再次打开即可显示窗口，菜单里可以退出。"); bg_notice_shown=1; save_window(); }
    return TRUE; }

/* ---------------------------------------------------------------- event pump (core thread -> GTK main loop) */
static gboolean poll_cb(gpointer p){ char e[4096]; (void)p;
    while(lsm_poll_event(C,e,sizeof e)){
        if(!strncmp(e,"msg\t",4)||!strncmp(e,"msg_update\t",11)){ char *f[8]; int n=0; char *q=e; f[n++]=q; while(*q&&n<8){ if(*q=='\t'){ *q=0; f[n++]=q+1; } q++; }
            if(n>=7){ if(!strcmp(f[2],cur_conv)) chat_dirty=1; list_dirty=1;
                if(!strcmp(f[0],"msg")&&!strcmp(f[3],"in")&&strcmp(f[5],"system")&&(strcmp(f[2],cur_conv)||!gtk_window_is_active(GTK_WINDOW(win)))) notify_conv("新消息",!strcmp(f[5],"file")?"收到文件":f[6],f[2]); } }
        else if(!strncmp(e,"transfer\t",9)) chat_dirty=1;
        else if(!strncmp(e,"relay\t",6)){ relay_refresh(); list_dirty=1; }
        else if(!strncmp(e,"peer\t",5) && strstr(e,"\tpath\t")) list_dirty=chat_dirty=1;
        else if(!strncmp(e,"notice\t",7)) notify("提示",e+7);
        else if(!strncmp(e,"pin_result\t",11)){ notify("PIN 配对",strstr(e,"\tok")?"配对成功，已互相信任。":"配对失败（PIN 错误或已过期）。"); list_dirty=chat_dirty=1; }
        else if(!strncmp(e,"log\t",4)){ if(getenv("LSM_VERBOSE")) fprintf(stderr,"%s\n",e); }
        else { list_dirty=1; chat_dirty=1; } }
    if(list_dirty){ list_dirty=0; refresh_list(); }
    if(chat_dirty){ chat_dirty=0; refresh_chat(); if(cur_conv[0]&&gtk_window_is_active(GTK_WINDOW(win))&&lsm_mark_read(C,cur_conv)) list_dirty=1; }
    return G_SOURCE_CONTINUE; }

/* ---------------------------------------------------------------- theme */
static GtkCssProvider *pal_cp;
static void load_palette(void){ int dark=adw_style_manager_get_dark(adw_style_manager_get_default());
    if(!pal_cp){ pal_cp=gtk_css_provider_new(); gtk_style_context_add_provider_for_display(gdk_display_get_default(),GTK_STYLE_PROVIDER(pal_cp),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION); }
    { GString *s=g_string_new(LSM_ADW_PALETTE[dark?1:0]); char *css=NULL; gsize n=0; char ph[1200]; const char *ad=getenv("LSM_ADW_CSS");
      /* the rules (lsm-adw.css) are compiled in; LSM_ADW_CSS=<file> overrides for design iteration */
      if(ad && g_file_get_contents(ad,&css,&n,NULL)){ g_string_append(s,css); g_free(css); } else g_string_append(s,LSM_ADW_RULES); (void)ph;
#if GTK_CHECK_VERSION(4, 12, 0)
      gtk_css_provider_load_from_string(pal_cp,s->str);
#else
      gtk_css_provider_load_from_data(pal_cp,s->str,-1);
#endif
      g_string_free(s,TRUE); } }
static void on_dark(GObject *o, GParamSpec *ps, gpointer p){ (void)o;(void)ps;(void)p; load_palette(); }

/* ---------------------------------------------------------------- UI */
static GtkWidget *icon_btn(const char *icon, const char *tip, const char *action){ GtkWidget *b=gtk_button_new_from_icon_name(icon); gtk_widget_set_tooltip_text(b,tip); cls(b,"flat"); if(action) gtk_actionable_set_action_name(GTK_ACTIONABLE(b),action); return b; }
static void add_shortcut(GtkShortcutController *sc, const char *trigger, const char *action){ gtk_shortcut_controller_add_shortcut(sc,gtk_shortcut_new(gtk_shortcut_trigger_parse_string(trigger),gtk_named_action_new(action))); }
static void search_changed(GtkSearchEntry *e, gpointer p){ (void)e;(void)p; gtk_list_box_invalidate_filter(GTK_LIST_BOX(conv_list)); }
static void search_stop(GtkSearchEntry *e, gpointer p){ (void)p; gtk_editable_set_text(GTK_EDITABLE(e),""); if(cur_conv[0]) gtk_widget_grab_focus(input); }
static void add_entry_act(GtkEntry *e, gpointer p){ (void)e;(void)p; add_contact_go(); }
static void add_btn_clicked(GtkButton *b, gpointer p){ (void)b;(void)p; add_contact_go(); }

static char *g_name, *g_targets; static int g_port, g_dport, g_nodisc, g_autotrust;
static void install_icons(void){
    static const char plane[]="<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path fill='#2e3436' d='M1.4 1.2a.6.6 0 0 0-.8.7L2.3 7.4 9 8l-6.7.6L.6 14.1a.6.6 0 0 0 .8.7l14-6.2a.6.6 0 0 0 0-1.1z'/></svg>";
    char *dir=g_build_filename(g_get_user_cache_dir(),"lan-messenger","icons",NULL), *ad=g_build_filename(dir,"hicolor","scalable","actions",NULL), *f=g_build_filename(ad,"lsm-send-symbolic.svg",NULL);
    static const char cloud[]="<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path fill='#2e3436' d='M4.6 13.5h7.1a3.4 3.4 0 0 0 .5-6.8 4.6 4.6 0 0 0-8.8 1.1A2.9 2.9 0 0 0 4.6 13.5z'/></svg>";
    g_mkdir_with_parents(ad,0700); if(!g_file_test(f,G_FILE_TEST_EXISTS)) g_file_set_contents(f,plane,-1,NULL);
    { char *cf=g_build_filename(ad,"lsm-cloud-symbolic.svg",NULL); g_file_set_contents(cf,cloud,-1,NULL); g_free(cf); } /* 经服务器 badge icon (WinUI/Android: cloud) */
    gtk_icon_theme_add_search_path(gtk_icon_theme_get_for_display(gdk_display_get_default()),dir); g_free(f); g_free(ad); g_free(dir); }
static void activate(GtkApplication *a, gpointer p){
    GtkWidget *sidebar_tv, *sb_hdr, *sb_box, *sc, *content_tv, *ct_hdr, *tbox, *tv, *composer, *ov, *send, *attach, *chat_ov, *empty, *add_btn, *ab, *frame;
    lsm_opts o; char err[256]; GMenu *menu, *sec; (void)p;
    static const char *acts[]={"search","add-contact","next-conv","prev-conv","send-file","details","trust","untrust","block","pin-offer","relay","downloads","menu","close","about","quit"};
    if(win){ gtk_window_present(GTK_WINDOW(win)); return; }
    { const char *t=getenv("LSM_THEME"); AdwStyleManager *sm=adw_style_manager_get_default(); if(t&&!strcmp(t,"dark")) adw_style_manager_set_color_scheme(sm,ADW_COLOR_SCHEME_FORCE_DARK); else if(t&&!strcmp(t,"light")) adw_style_manager_set_color_scheme(sm,ADW_COLOR_SCHEME_FORCE_LIGHT);
      g_signal_connect(sm,"notify::dark",G_CALLBACK(on_dark),NULL); }
    memset(&o,0,sizeof o); o.data_dir=data_dir; o.name=g_name; o.listen_port=g_port; o.discovery_port=g_dport; o.announce_targets=g_targets; o.no_discovery=g_nodisc; o.auto_trust=g_autotrust;
    C=lsm_start(&o,err,sizeof err); if(!C){ fprintf(stderr,"启动失败：%s\n",err); exit(1); }
    if(getenv("LSM_UI_TEST")){ lsm_lock(C); printf("LSM_FP %s\nLSM_TOOLKIT gtk %u.%u.%u adwaita %u.%u.%u\n",lsm_my_fp(C),gtk_get_major_version(),gtk_get_minor_version(),gtk_get_micro_version(),adw_get_major_version(),adw_get_minor_version(),adw_get_micro_version()); lsm_unlock(C); fflush(stdout); }
    load_palette();
    if(getenv("LSM_UI_TEST")) g_timeout_add(500,ui_state_tick,NULL);
    if(getenv("LSM_UI_TEST") && getenv("LSM_UI_TEST_RELAY")) g_timeout_add(2500,relay_test_fill,(gpointer)getenv("LSM_UI_TEST_RELAY"));
    install_icons(); win=adw_application_window_new(a); gtk_window_set_title(GTK_WINDOW(win),"局域网安全通讯"); gtk_window_set_default_size(GTK_WINDOW(win),1024,694); gtk_window_set_icon_name(GTK_WINDOW(win),"lan-messenger");
    gtk_widget_set_size_request(win,360,400);
    for(size_t i=0;i<G_N_ELEMENTS(acts);i++){ GSimpleAction *sa=g_simple_action_new(acts[i],NULL); g_signal_connect(sa,"activate",G_CALLBACK(win_act),NULL); g_action_map_add_action(G_ACTION_MAP(win),G_ACTION(sa)); g_object_unref(sa); }
    { GtkEventController *sc2=gtk_shortcut_controller_new(); gtk_event_controller_set_propagation_phase(sc2,GTK_PHASE_CAPTURE); gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(sc2),GTK_SHORTCUT_SCOPE_GLOBAL);
      add_shortcut(GTK_SHORTCUT_CONTROLLER(sc2),"<Control>f","win.search"); add_shortcut(GTK_SHORTCUT_CONTROLLER(sc2),"<Control>n","win.add-contact");
      add_shortcut(GTK_SHORTCUT_CONTROLLER(sc2),"<Control>Tab","win.next-conv"); add_shortcut(GTK_SHORTCUT_CONTROLLER(sc2),"<Control>Page_Down","win.next-conv");
      add_shortcut(GTK_SHORTCUT_CONTROLLER(sc2),"<Control><Shift>ISO_Left_Tab","win.prev-conv"); add_shortcut(GTK_SHORTCUT_CONTROLLER(sc2),"<Control><Shift>Tab","win.prev-conv"); add_shortcut(GTK_SHORTCUT_CONTROLLER(sc2),"<Control>Page_Up","win.prev-conv");
      add_shortcut(GTK_SHORTCUT_CONTROLLER(sc2),"<Control>o","win.send-file"); add_shortcut(GTK_SHORTCUT_CONTROLLER(sc2),"<Control>i","win.details"); add_shortcut(GTK_SHORTCUT_CONTROLLER(sc2),"F10","win.menu");
      add_shortcut(GTK_SHORTCUT_CONTROLLER(sc2),"<Control>q","win.quit"); add_shortcut(GTK_SHORTCUT_CONTROLLER(sc2),"<Control>w","win.close");
      gtk_widget_add_controller(win,sc2); }

    /* sidebar */
    sidebar_tv=adw_toolbar_view_new(); sb_hdr=adw_header_bar_new(); cls(sb_hdr,"lsm-hdr"); adw_header_bar_set_title_widget(ADW_HEADER_BAR(sb_hdr),adw_window_title_new("局域网安全通讯",NULL));
    add_btn=gtk_menu_button_new(); gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(add_btn),"contact-new-symbolic"); gtk_widget_set_tooltip_text(add_btn,"按 IP 添加联系人 (Ctrl+N)");
    add_popover=gtk_popover_new(); ab=gtk_box_new(GTK_ORIENTATION_VERTICAL,8); gtk_widget_set_size_request(ab,280,-1);
    { GtkWidget *t=label("按 IP 添加联系人","heading"), *d=label("对方不在自动发现列表时，输入对方电脑的 IP（可带端口）。","sub"), *hb=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6), *go=gtk_button_new_with_label("添加");
      gtk_label_set_wrap(GTK_LABEL(d),TRUE); add_entry=gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(add_entry),"如 192.168.1.20"); gtk_widget_set_hexpand(add_entry,TRUE);
      g_signal_connect(add_entry,"activate",G_CALLBACK(add_entry_act),NULL); cls(go,"suggested-action"); g_signal_connect(go,"clicked",G_CALLBACK(add_btn_clicked),NULL);
      gtk_box_append(GTK_BOX(hb),add_entry); gtk_box_append(GTK_BOX(hb),go); gtk_box_append(GTK_BOX(ab),t); gtk_box_append(GTK_BOX(ab),d); gtk_box_append(GTK_BOX(ab),hb); }
    gtk_popover_set_child(GTK_POPOVER(add_popover),ab); gtk_menu_button_set_popover(GTK_MENU_BUTTON(add_btn),add_popover); adw_header_bar_pack_start(ADW_HEADER_BAR(sb_hdr),add_btn);
    menu=g_menu_new(); sec=g_menu_new(); g_menu_append(sec,"发送文件… (Ctrl+O)","win.send-file"); g_menu_append(sec,"打开接收文件夹","win.downloads"); g_menu_append_section(menu,NULL,G_MENU_MODEL(sec)); g_object_unref(sec);
    sec=g_menu_new(); g_menu_append(sec,"生成 PIN（让对方输入）","win.pin-offer"); g_menu_append(sec,"中转服务器…","win.relay"); g_menu_append_section(menu,NULL,G_MENU_MODEL(sec)); g_object_unref(sec);
    sec=g_menu_new(); g_menu_append(sec,"关于","win.about"); g_menu_append(sec,"退出 (Ctrl+Q)","win.quit"); g_menu_append_section(menu,NULL,G_MENU_MODEL(sec)); g_object_unref(sec);
    menu_btn=gtk_menu_button_new(); gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_btn),"open-menu-symbolic"); gtk_widget_set_tooltip_text(menu_btn,"主菜单 (F10)"); gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_btn),G_MENU_MODEL(menu)); g_object_unref(menu);
    adw_header_bar_pack_end(ADW_HEADER_BAR(sb_hdr),menu_btn); adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(sidebar_tv),sb_hdr);
    sb_box=gtk_box_new(GTK_ORIENTATION_VERTICAL,0); search=gtk_search_entry_new(); gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(search),"搜索联系人和群组"); cls(search,"sidebar-search");
    g_signal_connect(search,"search-changed",G_CALLBACK(search_changed),NULL); g_signal_connect(search,"stop-search",G_CALLBACK(search_stop),NULL);
    conv_list=gtk_list_box_new(); cls(conv_list,"navigation-sidebar"); gtk_list_box_set_filter_func(GTK_LIST_BOX(conv_list),list_filter,NULL,NULL); g_signal_connect(conv_list,"row-selected",G_CALLBACK(on_row),NULL);
    sc=gtk_scrolled_window_new(); gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC); gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc),conv_list); gtk_widget_set_vexpand(sc,TRUE);
    gtk_box_append(GTK_BOX(sb_box),search); gtk_box_append(GTK_BOX(sb_box),sc); adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(sidebar_tv),sb_box);
    status_lbl=label("","status"); gtk_label_set_ellipsize(GTK_LABEL(status_lbl),PANGO_ELLIPSIZE_END); adw_toolbar_view_add_bottom_bar(ADW_TOOLBAR_VIEW(sidebar_tv),status_lbl);

    /* content */
    content_tv=adw_toolbar_view_new(); ct_hdr=adw_header_bar_new(); cls(ct_hdr,"lsm-hdr"); tbox=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,10);
    hdr_avatar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0); { GtkWidget *v=gtk_box_new(GTK_ORIENTATION_VERTICAL,0); title_lbl=label("","chat-title"); sub_lbl=label("","chat-sub"); gtk_label_set_ellipsize(GTK_LABEL(sub_lbl),PANGO_ELLIPSIZE_END);
      { GtkWidget *th=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6); hdr_shield=gtk_image_new_from_icon_name("security-high-symbolic"); cls(hdr_shield,"trusted"); gtk_widget_set_tooltip_text(hdr_shield,"已验证身份"); gtk_widget_set_visible(hdr_shield,FALSE);
        gtk_box_append(GTK_BOX(th),title_lbl); gtk_box_append(GTK_BOX(th),hdr_shield); gtk_box_append(GTK_BOX(v),th); } { GtkWidget *sb=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4); hdr_cloud=gtk_image_new_from_icon_name("lsm-cloud-symbolic"); gtk_image_set_pixel_size(GTK_IMAGE(hdr_cloud),14); cls(hdr_cloud,"chat-sub"); gtk_widget_set_tooltip_text(hdr_cloud,"经服务器中转"); gtk_widget_set_visible(hdr_cloud,FALSE); gtk_box_append(GTK_BOX(sb),hdr_cloud); gtk_box_append(GTK_BOX(sb),sub_lbl); gtk_box_append(GTK_BOX(v),sb); } /* relay peer: cloud before 在线 (WinUI) */ gtk_widget_set_valign(v,GTK_ALIGN_CENTER); gtk_box_append(GTK_BOX(tbox),hdr_avatar); gtk_box_append(GTK_BOX(tbox),v); }
    { GtkGesture *g=gtk_gesture_click_new(); g_signal_connect_swapped(g,"released",G_CALLBACK(details),NULL); gtk_widget_add_controller(tbox,GTK_EVENT_CONTROLLER(g)); gtk_widget_set_tooltip_text(tbox,"联系人详情 (Ctrl+I)"); }
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(ct_hdr),gtk_label_new(NULL)); adw_header_bar_pack_start(ADW_HEADER_BAR(ct_hdr),tbox);
    { GtkWidget *more=gtk_menu_button_new(); GMenu *cm=g_menu_new(), *s1=g_menu_new(), *s2=g_menu_new();
      g_menu_append(s1,"联系人详情 (Ctrl+I)","win.details"); g_menu_append_section(cm,NULL,G_MENU_MODEL(s1));
      g_menu_append(s2,"信任（核对指纹后）","win.trust"); g_menu_append(s2,"取消信任","win.untrust"); g_menu_append(s2,"屏蔽…","win.block"); g_menu_append_section(cm,NULL,G_MENU_MODEL(s2));
      gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(more),"view-more-symbolic"); gtk_widget_set_tooltip_text(more,"更多"); gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(more),G_MENU_MODEL(cm)); g_object_unref(cm); g_object_unref(s1); g_object_unref(s2);
      hdr_more=more; adw_header_bar_pack_end(ADW_HEADER_BAR(ct_hdr),more); adw_header_bar_pack_end(ADW_HEADER_BAR(ct_hdr),icon_btn("system-search-symbolic","搜索 (Ctrl+F)","win.search")); }
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(content_tv),ct_hdr);
    content_stack=gtk_stack_new();
    empty=empty_page=adw_status_page_new(); adw_status_page_set_icon_name(ADW_STATUS_PAGE(empty),"mail-message-new-symbolic"); adw_status_page_set_title(ADW_STATUS_PAGE(empty),"选择一个会话");
    adw_status_page_set_description(ADW_STATUS_PAGE(empty),"同一局域网内的设备会自动出现在左侧，也可以按 Ctrl+N 按 IP 添加。\n所有消息均端到端加密，只在局域网内传输。");
    chat_ov=gtk_overlay_new(); chat_scroll=gtk_scrolled_window_new(); chat_box=gtk_box_new(GTK_ORIENTATION_VERTICAL,0); cls(chat_box,"chatlist");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(chat_scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
    { GtkWidget *clamp=adw_clamp_new(); adw_clamp_set_maximum_size(ADW_CLAMP(clamp),900); adw_clamp_set_tightening_threshold(ADW_CLAMP(clamp),900); adw_clamp_set_child(ADW_CLAMP(clamp),chat_box); gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(chat_scroll),clamp); }
    cls(chat_scroll,"chatbg"); gtk_widget_set_vexpand(chat_scroll,TRUE); gtk_overlay_set_child(GTK_OVERLAY(chat_ov),chat_scroll);
    drop_hint=gtk_label_new("松开即可发送文件"); cls(drop_hint,"drophint"); gtk_widget_set_halign(drop_hint,GTK_ALIGN_CENTER); gtk_widget_set_valign(drop_hint,GTK_ALIGN_CENTER); gtk_widget_set_visible(drop_hint,FALSE); gtk_overlay_add_overlay(GTK_OVERLAY(chat_ov),drop_hint);
    { GtkDropTarget *dt=gtk_drop_target_new(GDK_TYPE_FILE_LIST,GDK_ACTION_COPY); test_dt=dt; g_signal_connect(dt,"drop",G_CALLBACK(on_drop),NULL); g_signal_connect(dt,"enter",G_CALLBACK(drop_enter),NULL); g_signal_connect(dt,"leave",G_CALLBACK(drop_leave),NULL); gtk_widget_add_controller(chat_ov,GTK_EVENT_CONTROLLER(dt)); }
    /* composer: attach · [field: 输入消息 … Enter 发送 · Shift+Enter 换行] · send (no emoji button until there is a picker) */
    composer=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8); cls(composer,"composer");
    attach=icon_btn("mail-attachment-symbolic","发送文件 (Ctrl+O)，也可以把文件拖进来","win.send-file"); cls(attach,"circular"); gtk_widget_set_valign(attach,GTK_ALIGN_END);
    frame=gtk_overlay_new(); cls(frame,"field"); gtk_widget_set_hexpand(frame,TRUE);
    /* EXTERNAL: scrolls (wheel, caret) without a scrollbar, whose minimum height would force the field to ~58px */
    tv=gtk_scrolled_window_new(); gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tv),GTK_POLICY_NEVER,GTK_POLICY_EXTERNAL); gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(tv),140); gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(tv),20); gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(tv),TRUE);
    input=gtk_text_view_new(); gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(input),GTK_WRAP_WORD_CHAR); cls(input,"input"); gtk_text_view_set_top_margin(GTK_TEXT_VIEW(input),8); gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(input),8); gtk_text_view_set_left_margin(GTK_TEXT_VIEW(input),12); gtk_text_view_set_right_margin(GTK_TEXT_VIEW(input),190); gtk_accessible_update_property(GTK_ACCESSIBLE(input),GTK_ACCESSIBLE_PROPERTY_LABEL,"输入消息",-1);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(tv),input); gtk_overlay_set_child(GTK_OVERLAY(frame),tv);
    placeholder=label("输入消息","placeholder"); gtk_widget_set_can_target(placeholder,FALSE); gtk_widget_set_halign(placeholder,GTK_ALIGN_START); gtk_widget_set_valign(placeholder,GTK_ALIGN_CENTER); gtk_overlay_add_overlay(GTK_OVERLAY(frame),placeholder);
    hint=label("Enter 发送 · Shift+Enter 换行","hint"); gtk_widget_set_can_target(hint,FALSE); gtk_widget_set_halign(hint,GTK_ALIGN_END); gtk_widget_set_valign(hint,GTK_ALIGN_CENTER); gtk_overlay_add_overlay(GTK_OVERLAY(frame),hint);
    g_signal_connect(gtk_text_view_get_buffer(GTK_TEXT_VIEW(input)),"changed",G_CALLBACK(buf_changed),NULL);
    { GtkEventController *k=gtk_event_controller_key_new(); g_signal_connect(k,"key-pressed",G_CALLBACK(input_key),NULL); gtk_widget_add_controller(input,k); }
    send=gtk_button_new_from_icon_name("lsm-send-symbolic"); cls(send,"circular"); cls(send,"suggested-action"); cls(send,"send"); gtk_widget_set_tooltip_text(send,"发送 (Enter)"); gtk_widget_set_valign(send,GTK_ALIGN_END);
    g_signal_connect_swapped(send,"clicked",G_CALLBACK(send_current),NULL);
    g_object_set_data(G_OBJECT(win),"dbg-tv",tv); g_object_set_data(G_OBJECT(win),"dbg-frame",frame); g_object_set_data(G_OBJECT(win),"dbg-comp",composer); g_object_set_data(G_OBJECT(win),"dbg-hint",hint);
    gtk_box_append(GTK_BOX(composer),attach); gtk_box_append(GTK_BOX(composer),frame); gtk_box_append(GTK_BOX(composer),send);
    { GtkWidget *chat=gtk_box_new(GTK_ORIENTATION_VERTICAL,0); gtk_box_append(GTK_BOX(chat),chat_ov); gtk_box_append(GTK_BOX(chat),composer);
      gtk_stack_add_named(GTK_STACK(content_stack),empty,"empty"); gtk_stack_add_named(GTK_STACK(content_stack),chat,"chat"); }
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(content_tv),content_stack);

    split=adw_navigation_split_view_new(); { AdwNavigationPage *sp=adw_navigation_page_new(sidebar_tv,"会话"), *cp=adw_navigation_page_new(content_tv,"聊天");
      adw_navigation_split_view_set_sidebar(ADW_NAVIGATION_SPLIT_VIEW(split),sp); adw_navigation_split_view_set_content(ADW_NAVIGATION_SPLIT_VIEW(split),cp); }
    adw_navigation_split_view_set_min_sidebar_width(ADW_NAVIGATION_SPLIT_VIEW(split),260); adw_navigation_split_view_set_max_sidebar_width(ADW_NAVIGATION_SPLIT_VIEW(split),260); adw_navigation_split_view_set_sidebar_width_fraction(ADW_NAVIGATION_SPLIT_VIEW(split),0.3);
    { AdwBreakpoint *bp=adw_breakpoint_new(adw_breakpoint_condition_parse("max-width: 600sp")); GValue v=G_VALUE_INIT; g_value_init(&v,G_TYPE_BOOLEAN); g_value_set_boolean(&v,TRUE);
      adw_breakpoint_add_setter(bp,G_OBJECT(split),"collapsed",&v); g_value_unset(&v); adw_application_window_add_breakpoint(ADW_APPLICATION_WINDOW(win),bp); }
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(win),split); (void)ov;
    load_window(); g_signal_connect(win,"close-request",G_CALLBACK(on_close),NULL);
    gtk_window_present(GTK_WINDOW(win)); refresh_list(); refresh_chat();
    { const char *oc=getenv("LSM_OPEN_CONV"); if(oc) select_conv(oc); }
    { const char *ws=getenv("LSM_WINDOW_SIZE"); int w,h; if(ws && sscanf(ws,"%dx%d",&w,&h)==2) gtk_window_set_default_size(GTK_WINDOW(win),w,h); }  /* tests: fixed size beats window.ini */
    g_timeout_add(300,poll_cb,NULL); }

/* app.open-conv(s): default action of a message notification -> show the window and open that chat */
static void open_conv_act(GSimpleAction *a, GVariant *v, gpointer p){ (void)a; (void)p; if(!win) g_application_activate(G_APPLICATION(app)); if(!win) return;
    gtk_widget_set_visible(win,TRUE); gtk_window_present(GTK_WINDOW(win)); select_conv(g_variant_get_string(v,NULL)); list_dirty=1; }
int main(int argc, char **argv){
    int i, st; const char *xdg=getenv("XDG_DATA_HOME"), *home=getenv("HOME");
    if(xdg&&*xdg) snprintf(data_dir,sizeof data_dir,"%s/lan-messenger",xdg); else snprintf(data_dir,sizeof data_dir,"%s/.local/share/lan-messenger",home?home:".");
    for(i=1;i<argc;i++){
        if(!strcmp(argv[i],"--data")&&i+1<argc) snprintf(data_dir,sizeof data_dir,"%s",argv[++i]);
        else if(!strcmp(argv[i],"--name")&&i+1<argc) g_name=argv[++i];
        else if(!strcmp(argv[i],"--port")&&i+1<argc) g_port=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--disco-port")&&i+1<argc) g_dport=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--targets")&&i+1<argc) g_targets=argv[++i];
        else if(!strcmp(argv[i],"--no-discovery")) g_nodisc=1;
        else if(!strcmp(argv[i],"--auto-trust")) g_autotrust=1;
        else if(!strcmp(argv[i],"--version")){ printf("lan-messenger-adw %s (protocol v5, GTK 4 + libadwaita)\n",LSM_APP_VERSION); return 0; } }
    if(!g_name){ static char hn[256]; if(gethostname(hn,sizeof hn)==0) g_name=hn; }
    { char id[256]; snprintf(id,sizeof id,APP_ID "%s",getenv("LSM_INSTANCE")?getenv("LSM_INSTANCE"):""); app=adw_application_new(id,G_APPLICATION_DEFAULT_FLAGS); }
    g_signal_connect(app,"activate",G_CALLBACK(activate),NULL);
    { GSimpleAction *oc=g_simple_action_new("open-conv",G_VARIANT_TYPE_STRING); g_signal_connect(oc,"activate",G_CALLBACK(open_conv_act),NULL); g_action_map_add_action(G_ACTION_MAP(app),G_ACTION(oc)); g_object_unref(oc); }
    { char *av[]={argv[0],NULL}; st=g_application_run(G_APPLICATION(app),1,av); }
    if(C) lsm_stop(C);
    return st; }
