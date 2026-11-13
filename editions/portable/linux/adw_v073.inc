/* v0.7.3: icon-led settings / profile / files pages, menu icons, profile quick actions.
 * Icons are drawn with cairo (no icon theme) so the AppImage never shows tofu. */
enum { IC_PERSON=1, IC_EDIT, IC_LOCK, IC_CLOUD, IC_FOLDER, IC_NET, IC_GROUP, IC_QR, IC_INFO,
       IC_STORAGE, IC_DIAG, IC_SEARCH, IC_COPY, IC_CHAT, IC_TRASH, IC_SHIELD, IC_ADD, IC_DOWNLOAD,
       IC_TICK, IC_MUTE, IC_IMAGE, IC_PIN };
static void toast(const char *m);
static void v073_glyph(cairo_t *cr, int k){
    cairo_set_line_width(cr,2.0); cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND); cairo_set_line_join(cr,CAIRO_LINE_JOIN_ROUND);
    switch(k){
    case IC_PERSON: cairo_arc(cr,12,8,3.2,0,2*G_PI); cairo_stroke(cr);
        cairo_move_to(cr,5.5,20); cairo_curve_to(cr,5.5,14.5,18.5,14.5,18.5,20); cairo_stroke(cr); break;
    case IC_EDIT: cairo_move_to(cr,14,5); cairo_line_to(cr,19,10); cairo_line_to(cr,8,21); cairo_line_to(cr,4,21); cairo_line_to(cr,4,17); cairo_close_path(cr); cairo_stroke(cr); break;
    case IC_LOCK: cairo_rectangle(cr,6,11,12,10); cairo_stroke(cr);
        cairo_move_to(cr,8,11); cairo_line_to(cr,8,8); cairo_arc(cr,12,8,4,G_PI,0); cairo_line_to(cr,16,11); cairo_stroke(cr); break;
    case IC_CLOUD: cairo_move_to(cr,6,17); cairo_arc(cr,8,14,3.2,G_PI*0.7,G_PI*1.6); cairo_arc(cr,13,11,4.2,G_PI*1.05,G_PI*2.05); cairo_arc(cr,18,14,3,G_PI*1.4,G_PI*0.3); cairo_line_to(cr,18,17); cairo_close_path(cr); cairo_stroke(cr); break;
    case IC_FOLDER: cairo_move_to(cr,4,8); cairo_line_to(cr,10,8); cairo_line_to(cr,12,11); cairo_line_to(cr,20,11); cairo_line_to(cr,20,20); cairo_line_to(cr,4,20); cairo_close_path(cr); cairo_stroke(cr); break;
    case IC_NET: cairo_arc(cr,12,12,8,0,2*G_PI); cairo_stroke(cr);
        cairo_move_to(cr,12,4); cairo_line_to(cr,12,20); cairo_move_to(cr,4,12); cairo_line_to(cr,20,12);
        cairo_new_sub_path(cr); cairo_arc(cr,12,12,8,0,2*G_PI); /* keep */ cairo_stroke(cr);
        cairo_move_to(cr,7,7); cairo_curve_to(cr,12,9,12,15,17,17); cairo_stroke(cr); break;
    case IC_GROUP: cairo_arc(cr,8,9,2.6,0,2*G_PI); cairo_stroke(cr); cairo_arc(cr,16,9,2.6,0,2*G_PI); cairo_stroke(cr);
        cairo_move_to(cr,3.5,19); cairo_curve_to(cr,3.5,14.5,12.5,14.5,12.5,19); cairo_stroke(cr);
        cairo_move_to(cr,11.5,19); cairo_curve_to(cr,11.5,14.5,20.5,14.5,20.5,19); cairo_stroke(cr); break;
    case IC_QR: cairo_rectangle(cr,4,4,6,6); cairo_stroke(cr); cairo_rectangle(cr,14,4,6,6); cairo_stroke(cr);
        cairo_rectangle(cr,4,14,6,6); cairo_stroke(cr); cairo_rectangle(cr,14,14,3,3); cairo_fill(cr);
        cairo_rectangle(cr,18,14,2,2); cairo_fill(cr); cairo_rectangle(cr,14,18,2,2); cairo_fill(cr);
        cairo_rectangle(cr,18,18,2,2); cairo_fill(cr); break;
    case IC_INFO: cairo_arc(cr,12,12,8,0,2*G_PI); cairo_stroke(cr);
        cairo_arc(cr,12,8,1.1,0,2*G_PI); cairo_fill(cr);
        cairo_move_to(cr,12,11.5); cairo_line_to(cr,12,17.5); cairo_stroke(cr); break;
    case IC_STORAGE: cairo_arc(cr,12,12,8, -G_PI/2, 1.4); cairo_line_to(cr,12,12); cairo_close_path(cr); cairo_fill(cr);
        cairo_arc(cr,12,12,8,0,2*G_PI); cairo_stroke(cr); break;
    case IC_DIAG: cairo_move_to(cr,5,18); cairo_line_to(cr,9,10); cairo_line_to(cr,12,14); cairo_line_to(cr,16,6); cairo_line_to(cr,19,12); cairo_stroke(cr); break;
    case IC_SEARCH: cairo_arc(cr,11,11,6,0,2*G_PI); cairo_stroke(cr); cairo_move_to(cr,15.4,15.4); cairo_line_to(cr,20,20); cairo_stroke(cr); break;
    case IC_COPY: cairo_rectangle(cr,8,8,10,12); cairo_stroke(cr); cairo_move_to(cr,6,16); cairo_line_to(cr,6,4); cairo_line_to(cr,16,4); cairo_stroke(cr); break;
    case IC_CHAT: cairo_move_to(cr,5,6); cairo_line_to(cr,19,6); cairo_line_to(cr,19,16); cairo_line_to(cr,10,16); cairo_line_to(cr,6,20); cairo_line_to(cr,6,16); cairo_line_to(cr,5,16); cairo_close_path(cr); cairo_stroke(cr); break;
    case IC_TRASH: cairo_move_to(cr,6,8); cairo_line_to(cr,18,8); cairo_move_to(cr,9,8); cairo_line_to(cr,10,5); cairo_line_to(cr,14,5); cairo_line_to(cr,15,8);
        cairo_move_to(cr,8,8); cairo_line_to(cr,9,20); cairo_line_to(cr,15,20); cairo_line_to(cr,16,8); cairo_stroke(cr); break;
    case IC_SHIELD: cairo_move_to(cr,12,4); cairo_line_to(cr,19,7); cairo_line_to(cr,19,13); cairo_curve_to(cr,19,17,12,20,12,20); cairo_curve_to(cr,12,20,5,17,5,13); cairo_line_to(cr,5,7); cairo_close_path(cr); cairo_stroke(cr); break;
    case IC_ADD: cairo_move_to(cr,12,5); cairo_line_to(cr,12,19); cairo_move_to(cr,5,12); cairo_line_to(cr,19,12); cairo_stroke(cr); break;
    case IC_DOWNLOAD: cairo_move_to(cr,12,5); cairo_line_to(cr,12,16); cairo_move_to(cr,8,12); cairo_line_to(cr,12,16); cairo_line_to(cr,16,12);
        cairo_move_to(cr,6,19); cairo_line_to(cr,18,19); cairo_stroke(cr); break;
    case IC_TICK: cairo_move_to(cr,5,12); cairo_line_to(cr,10,17); cairo_line_to(cr,19,7); cairo_stroke(cr); break;
    case IC_MUTE: cairo_move_to(cr,4,10); cairo_line_to(cr,8,10); cairo_line_to(cr,13,6); cairo_line_to(cr,13,18); cairo_line_to(cr,8,14); cairo_line_to(cr,4,14); cairo_close_path(cr); cairo_stroke(cr);
        cairo_move_to(cr,16,8); cairo_line_to(cr,21,16); cairo_move_to(cr,21,8); cairo_line_to(cr,16,16); cairo_stroke(cr); break;
    case IC_IMAGE: cairo_rectangle(cr,5,6,14,12); cairo_stroke(cr); cairo_arc(cr,9,10,1.6,0,2*G_PI); cairo_fill(cr);
        cairo_move_to(cr,5,16); cairo_line_to(cr,10,12); cairo_line_to(cr,14,15); cairo_line_to(cr,19,11); cairo_stroke(cr); break;
    case IC_PIN: cairo_move_to(cr,12,5); cairo_line_to(cr,16,11); cairo_line_to(cr,13,11); cairo_line_to(cr,13,18); cairo_line_to(cr,11,18); cairo_line_to(cr,11,11); cairo_line_to(cr,8,11); cairo_close_path(cr); cairo_stroke(cr); break;
    default: cairo_arc(cr,12,12,6,0,2*G_PI); cairo_stroke(cr); break;
    }
}

static int v073_dark(void){
    AdwStyleManager *sm=adw_style_manager_get_default();
    return sm && adw_style_manager_get_dark(sm);
}
static GdkPixbuf *v073_pixbuf(int k, int px, int tile){
    cairo_surface_t *s=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,px,px); cairo_t *cr=cairo_create(s); GdkPixbuf *pb; int dark=v073_dark();
    cairo_set_operator(cr,CAIRO_OPERATOR_CLEAR); cairo_paint(cr); cairo_set_operator(cr,CAIRO_OPERATOR_OVER);
    if(tile){
        double r=px*0.22;
        if(dark) cairo_set_source_rgb(cr,0x24/255.0,0x3C/255.0,0x5B/255.0);
        else cairo_set_source_rgb(cr,0xDB/255.0,0xE9/255.0,0xFA/255.0);
        cairo_move_to(cr,r,0); cairo_arc(cr,px-r,r,r,-G_PI/2,0); cairo_arc(cr,px-r,px-r,r,0,G_PI/2);
        cairo_arc(cr,r,px-r,r,G_PI/2,G_PI); cairo_arc(cr,r,r,r,G_PI,3*G_PI/2); cairo_close_path(cr); cairo_fill(cr);
        /* primary fg on the tile: tilefg+AA measured 4.30:1 / Lc 57.9 and failed the audit */
        if(dark) cairo_set_source_rgb(cr,1,1,1);
        else cairo_set_source_rgb(cr,0x32/255.0,0x32/255.0,0x35/255.0);
    }else{
        if(dark) cairo_set_source_rgb(cr,0x81/255.0,0xD0/255.0,1);
        else cairo_set_source_rgb(cr,0x04/255.0,0x61/255.0,0xBE/255.0);
    }
    cairo_save(cr); cairo_translate(cr,px*2.0/24.0,px*2.0/24.0); cairo_scale(cr,px*20.0/24.0/24.0,px*20.0/24.0/24.0); v073_glyph(cr,k); cairo_restore(cr);
    cairo_destroy(cr); pb=v072_pixbuf_from_surface(s); cairo_surface_destroy(s); return pb;
}
static GtkWidget *v073_img(int k, int px, int tile){ GdkPixbuf *pb=v073_pixbuf(k,px,tile); GtkWidget *im=gtk_image_new_from_pixbuf(pb); if(pb) g_object_unref(pb); gtk_widget_set_valign(im,GTK_ALIGN_CENTER); return im; }
static GIcon *v073_gicon(int k){
    GdkPixbuf *pb=v073_pixbuf(k,16,0); gchar *buf=NULL; gsize n=0; GBytes *b; GIcon *ic=NULL;
    if(pb && gdk_pixbuf_save_to_buffer(pb,&buf,&n,"png",NULL,NULL)){ b=g_bytes_new_take(buf,n); ic=g_bytes_icon_new(b); g_bytes_unref(b); }
    if(pb) g_object_unref(pb); return ic;
}
static void v073_append(GMenu *m, const char *label, const char *action, int icon){
    GMenuItem *it=g_menu_item_new(label,action); GIcon *ic=v073_gicon(icon);
    if(ic){ g_menu_item_set_icon(it,ic); g_object_unref(ic); }
    g_menu_append_item(m,it); g_object_unref(it);
}
static void v073_run(const char *action){ if(action && win) g_action_group_activate_action(G_ACTION_GROUP(win),action,NULL); }
static void v073_row_act(AdwActionRow *r, gpointer p){ const char *a=g_object_get_data(G_OBJECT(r),"act"); (void)p; if(a) v073_run(a); }
static GtkWidget *v073_arow(int icon, const char *title, const char *sub, const char *action){
    GtkWidget *row=adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row),title);
    adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row),FALSE);
    if(sub && sub[0]) adw_action_row_set_subtitle(ADW_ACTION_ROW(row),sub);
    adw_action_row_add_prefix(ADW_ACTION_ROW(row),v073_img(icon,36,1));
    if(action){ g_object_set_data_full(G_OBJECT(row),"act",g_strdup(action),g_free); gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row),TRUE); g_signal_connect(row,"activated",G_CALLBACK(v073_row_act),NULL); }
    return row;
}
static GtkWidget *v073_grp(const char *title){ GtkWidget *g=adw_preferences_group_new(); if(title) adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(g),title); return g; }
static void v073_clear_body(GtkWidget *page){
    GtkWidget *c=gtk_widget_get_first_child(page), *n; int i=0;
    while(c){ n=gtk_widget_get_next_sibling(c); if(i>0) gtk_widget_unparent(c); c=n; i++; }
}
static void v073_copy_fp(GtkButton *b, gpointer p){ char fp[80]=""; (void)b;(void)p; lsm_lock(C); snprintf(fp,sizeof fp,"%s",lsm_my_fp(C)); lsm_unlock(C);
    gdk_clipboard_set_text(gtk_widget_get_clipboard(win),fp); toast("已复制指纹"); }
static void v073_qa_act(GtkButton *b, gpointer p){ (void)b; v073_run((const char*)p); }
static GtkWidget *v073_qa(int icon, const char *caption, const char *action){
    GtkWidget *b=gtk_button_new(), *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,4), *l=gtk_label_new(caption);
    gtk_widget_add_css_class(b,"flat"); gtk_widget_add_css_class(b,"lsm-qa-btn");
    gtk_widget_set_halign(l,GTK_ALIGN_CENTER); gtk_label_set_wrap(GTK_LABEL(l),TRUE);
    gtk_box_append(GTK_BOX(box),v073_img(icon,36,1)); gtk_box_append(GTK_BOX(box),l);
    gtk_widget_set_halign(box,GTK_ALIGN_CENTER);
    gtk_button_set_child(GTK_BUTTON(b),box);
    if(action) g_signal_connect(b,"clicked",G_CALLBACK(v073_qa_act),(gpointer)action);
    return b;
}
static GtkWidget *v073_scroll_groups(GtkWidget *page){
    GtkWidget *sc=gtk_scrolled_window_new(), *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,12);
    gtk_widget_set_hexpand(sc,TRUE); gtk_widget_set_vexpand(sc,TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc),box);
    gtk_widget_set_margin_start(box,12); gtk_widget_set_margin_end(box,12); gtk_widget_set_margin_bottom(box,16);
    gtk_box_append(GTK_BOX(page),sc);
    g_object_set_data(G_OBJECT(page),"box",box);
    return box;
}
static void v073_fill_settings(GtkWidget *page){
    GtkWidget *box, *g; char sub[200]; const char *cur;
    v073_clear_body(page); box=v073_scroll_groups(page);
    cur=lsm_account_mode(C);
    g=v073_grp("账号");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g),v073_arow(IC_PERSON,"个人资料","名称、指纹、头像与二维码","profile"));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g),v073_arow(IC_EDIT,"修改昵称","在个人资料里更改显示名称","profile"));
    snprintf(sub,sizeof sub,"%s", !strcmp(cur,"account")?"身份在中转上加密备份，点按可切回本机":"仅本机保存身份，点按可切换为账号模式");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g),v073_arow(IC_LOCK, !strcmp(cur,"account")?"账号模式":"设备模式", sub, "acct-mode"));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g),v073_arow(IC_LOCK,"恢复密钥","查看、复制或导出；仅本机可见","reveal-rk"));
    gtk_box_append(GTK_BOX(box),g);
    g=v073_grp("连接");
    { int en=0,port=0,hp=0; char host[256]=""; lsm_relay_get(C,&en,host,sizeof host,&port,&hp); (void)port;(void)hp;
      snprintf(sub,sizeof sub,"%s", en?(host[0]?host:"已启用"):"未启用 · 局域网直连优先");
      adw_preferences_group_add(ADW_PREFERENCES_GROUP(g),v073_arow(IC_CLOUD,"中转服务器",sub,"relay")); }
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g),v073_arow(IC_QR,"服务器二维码","让另一台设备扫码接入同一中转","server-qr"));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g),v073_arow(IC_QR,"配对新设备","一次性配对码，需账号模式并已连中转","pair-qr"));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g),v073_arow(IC_NET,"重连服务器","重新连接已配置的中转","reconnect"));
    gtk_box_append(GTK_BOX(box),g);
    g=v073_grp("联系人");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g),v073_arow(IC_LOCK,"生成 PIN","让对方输入，配对并互相信任","pin-offer"));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g),v073_arow(IC_QR,"扫描链接","添加联系人、接入中转或配对新设备","paste-uri"));
    gtk_box_append(GTK_BOX(box),g);
    g=v073_grp("存储");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g),v073_arow(IC_FOLDER,"接收的文件","打开下载文件夹","downloads"));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g),v073_arow(IC_STORAGE,"存储管理","查看占用并清理半成品","storage"));
    gtk_box_append(GTK_BOX(box),g);
    g=v073_grp("关于");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g),v073_arow(IC_DIAG,"诊断信息","连接与排障","diagnostics"));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g),v073_arow(IC_INFO,"检查更新","查看是否有新版本","check-update"));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g),v073_arow(IC_INFO,"关于","版本与协议说明","about"));
    gtk_box_append(GTK_BOX(box),g);
}
static void v073_fill_profile(GtkWidget *page){
    char name[128]="", fp[80]="", b[200]; GtkWidget *head,*qa,*g,*box,*ini;
    v073_clear_body(page); box=v073_scroll_groups(page);
    lsm_lock(C); snprintf(name,sizeof name,"%s",lsm_my_name(C)); snprintf(fp,sizeof fp,"%s",lsm_my_fp(C)); lsm_unlock(C);
    head=gtk_box_new(GTK_ORIENTATION_VERTICAL,6); gtk_widget_set_halign(head,GTK_ALIGN_CENTER); gtk_widget_set_margin_top(head,8);
    ini=gtk_label_new(NULL);
    { gchar *m; char ic[8]="?"; const unsigned char *u=(const unsigned char*)name;
      if(u[0]>=0xE0){ memcpy(ic,name,3); ic[3]=0; } else if(name[0]){ ic[0]=name[0]; ic[1]=0; }
      m=g_markup_printf_escaped("<span size='18000' weight='600'>%s</span>",ic); gtk_label_set_markup(GTK_LABEL(ini),m); g_free(m); }
    gtk_box_append(GTK_BOX(head),ini);
    gtk_box_append(GTK_BOX(head),gtk_label_new(name));
    qa=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8); gtk_widget_set_halign(qa,GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(qa),v073_qa(IC_EDIT,"资料","profile"));
    gtk_box_append(GTK_BOX(qa),v073_qa(IC_QR,"二维码","my-qr"));
    { GtkWidget *c=v073_qa(IC_COPY,"复制",NULL); g_signal_connect(c,"clicked",G_CALLBACK(v073_copy_fp),NULL); gtk_box_append(GTK_BOX(qa),c); }
    gtk_box_append(GTK_BOX(qa),v073_qa(IC_SEARCH,"历史","history"));
    gtk_box_append(GTK_BOX(head),qa);
    gtk_box_append(GTK_BOX(box),head);
    g=v073_grp(NULL);
    snprintf(b,sizeof b,"%s",fp); adw_preferences_group_add(ADW_PREFERENCES_GROUP(g),v073_arow(IC_LOCK,"指纹",b,NULL));
    snprintf(b,sizeof b,"版本 %s",LSM_APP_VERSION); adw_preferences_group_add(ADW_PREFERENCES_GROUP(g),v073_arow(IC_INFO,"版本",b,NULL));
    gtk_box_append(GTK_BOX(box),g);
}
static void v073_open_dm(AdwActionRow *r, gpointer p){
    const char *fp=g_object_get_data(G_OBJECT(r),"fp"); (void)p;
    if(!fp) return; snprintf(cur_conv,sizeof cur_conv,"dm:%s",fp); v070_show_page("chats"); list_dirty=chat_dirty=1;
}
static void v073_fill_contacts(GtkWidget *page){
    GtkWidget *box,*g; lsm_contact *k; int n,i;
    v073_clear_body(page); box=v073_scroll_groups(page); g=v073_grp("同一局域网内发现的设备");
    lsm_lock(C); n=lsm_contacts(C,&k);
    for(i=0;i<n;i++){ char sub[160]; GtkWidget *row;
        snprintf(sub,sizeof sub,"%s · %s", k[i].trust==TRUST_TRUSTED?"已信任":k[i].trust==TRUST_BLOCKED?"已屏蔽":"未信任", k[i].online?"在线":"离线");
        row=v073_arow(k[i].trust==TRUST_TRUSTED?IC_SHIELD:IC_PERSON, lsm_display_name(&k[i]), sub, NULL);
        g_object_set_data_full(G_OBJECT(row),"fp",g_strdup(k[i].fp),g_free);
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row),TRUE);
        g_signal_connect(row,"activated",G_CALLBACK(v073_open_dm),NULL);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(g),row);
    }
    lsm_unlock(C);
    if(!n) adw_preferences_group_add(ADW_PREFERENCES_GROUP(g),v073_arow(IC_PERSON,"还没有联系人","同一网段下运行本软件的设备会自动出现在这里。",NULL));
    gtk_box_append(GTK_BOX(box),g);
}
static void v073_fill_files(GtkWidget *page){
    char info[2048]=""; GtkWidget *box,*g; v073_clear_body(page); box=v073_scroll_groups(page);
    lsm_storage_info(C,info,sizeof info); g=v073_grp("文件站");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g),v073_arow(IC_STORAGE,"存储用量",info[0]?info:"（存储信息不可用）","storage"));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g),v073_arow(IC_FOLDER,"接收文件夹","打开已下载的文件","downloads"));
    gtk_box_append(GTK_BOX(box),g);
}
static void v073_fill_transfers(GtkWidget *page){
    GtkWidget *box,*g; v073_clear_body(page); box=v073_scroll_groups(page); g=v073_grp("传输");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g),v073_arow(IC_DOWNLOAD,"接收文件夹","打开发布到本机的文件","downloads"));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g),v073_arow(IC_STORAGE,"存储管理","查看占用并清理半成品","storage"));
    gtk_box_append(GTK_BOX(box),g);
}
static void v073_fill_groups(GtkWidget *page){
    GtkWidget *box,*g; v073_clear_body(page); box=v073_scroll_groups(page); g=v073_grp("群组");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g),v073_arow(IC_CHAT,"从聊天列表打开","在聊天页选择一个群",NULL));
    gtk_box_append(GTK_BOX(box),g);
}
static void v073_enhance(GtkWidget *contacts, GtkWidget *groups, GtkWidget *xfers, GtkWidget *files, GtkWidget *settings, GtkWidget *profile, GtkWidget *diag){
    (void)diag; v073_fill_contacts(contacts); v073_fill_groups(groups); v073_fill_transfers(xfers);
    v073_fill_files(files); v073_fill_settings(settings); v073_fill_profile(profile);
}
