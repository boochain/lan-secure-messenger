static void v073_enhance(GtkWidget *contacts, GtkWidget *groups, GtkWidget *xfers, GtkWidget *files, GtkWidget *settings, GtkWidget *profile, GtkWidget *diag);
/* v0.7.0 (GTK4 / libadwaita): Telegram-like pages + chat-list right-click menu. */
static GtkWidget *v070_stack, *v070_rail;
static void v070_show_page(const char *name){ if(v070_stack) gtk_stack_set_visible_child_name(GTK_STACK(v070_stack), name); }
static void v070_sync_nav(const char *page);
static void v070_nav_cb(GtkButton *b, gpointer name){ (void)b; v070_show_page((const char*)name); v070_sync_nav((const char*)name); }
static void v070_clear_do(gpointer conv){ /* LSMF_chat_delete */ lsm_clear_conversation(C,(const char*)conv); chat_dirty=list_dirty=1; }
static void v070_conv_act(GtkButton *b, gpointer conv){
    const char *act=g_object_get_data(G_OBJECT(b),"act"); char err[256]="";
    GtkWidget *pop=gtk_widget_get_ancestor(GTK_WIDGET(b), GTK_TYPE_POPOVER);
    if(pop) gtk_popover_popdown(GTK_POPOVER(pop));
    if(!act||!conv) return;
    if(!strcmp(act,"delete")) confirm("删除会话？","将删除本机上与此会话的全部聊天记录（对方仍保留）。此操作无法撤销。","删除",1,v070_clear_do,g_strdup((const char*)conv),g_free);
    else if(!strcmp(act,"pin")){ int on; lsm_lock(C); on=!lsm_conv_pinned(C,(const char*)conv); lsm_unlock(C); lsm_set_conv_pinned(C,(const char*)conv,on); list_dirty=1; }
    else if(!strcmp(act,"read")){ lsm_mark_read(C,(const char*)conv); list_dirty=chat_dirty=1; }
    else if(!strcmp(act,"unread")){ lsm_mark_unread(C,(const char*)conv); list_dirty=1; }
    else if(!strcmp(act,"mute") && !strncmp((const char*)conv,"dm:",3)){ const char *fp=(const char*)conv+3; lsm_set_muted(C,fp,!peer_muted(fp),err); list_dirty=1; }
}
static void v070_pop_closed(GtkPopover *p, gpointer u){ (void)u; gtk_widget_unparent(GTK_WIDGET(p)); }
static void v070_pop_entered(GtkEventControllerMotion *c, gdouble x, gdouble y, gpointer pop);
static gboolean v070_pop_key(GtkEventControllerKey *c, guint keyval, guint kc, GdkModifierType mods, gpointer pop);
static void v070_popup(GtkWidget *row, const char *conv){
    GtkWidget *pop=gtk_popover_new(), *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,0), *btn;
    const char *acts[][2]={{"置顶 / 取消置顶","pin"},{"标为已读","read"},{"标为未读","unread"},{"免打扰","mute"},{"删除会话…","delete"}};
    const char *hooks[]={"LSMF_chat_pin","LSMF_chat_mark_read","LSMF_chat_mark_unread","LSMF_chat_mute","LSMF_chat_delete"};
    int i;
    gtk_widget_set_name(pop,"LSMF_chat_list_menu"); /* LSMF_chat_list_menu */
    for(i=0;i<5;i++){ btn=gtk_button_new_with_label(acts[i][0]); gtk_widget_set_name(btn,hooks[i]); gtk_button_set_has_frame(GTK_BUTTON(btn),FALSE);
        g_object_set_data(G_OBJECT(btn),"act",(gpointer)acts[i][1]);
        g_signal_connect_data(btn,"clicked",G_CALLBACK(v070_conv_act),g_strdup(conv),(GClosureNotify)g_free,0);
        gtk_box_append(GTK_BOX(box),btn); }
    gtk_popover_set_child(GTK_POPOVER(pop),box);
    gtk_widget_set_parent(pop,row);
    g_signal_connect(pop,"closed",G_CALLBACK(v070_pop_closed),NULL);
    /* Keep autohide off until the pointer enters the popover. Re-enabling it
       while the pointer still sits on the row (outside the popover) makes GTK
       treat that as click-outside and dismiss immediately — the test screenshot
       and a user who has not yet moved the mouse both lose the menu. */
    gtk_popover_set_autohide(GTK_POPOVER(pop), FALSE);
    gtk_popover_popup(GTK_POPOVER(pop));
    { GtkEventController *motion=gtk_event_controller_motion_new();
      g_signal_connect(motion,"enter",G_CALLBACK(v070_pop_entered),pop);
      gtk_widget_add_controller(pop,motion); }
    { GtkEventController *keys=gtk_event_controller_key_new();
      g_signal_connect(keys,"key-pressed",G_CALLBACK(v070_pop_key),pop);
      gtk_widget_add_controller(pop,keys); }
    gtk_widget_grab_focus(pop);
    if(getenv("LSM_UI_TEST")){ printf("UI conv-menu\n"); fflush(stdout); }
}
static void v070_pop_entered(GtkEventControllerMotion *c, gdouble x, gdouble y, gpointer pop){
    (void)c; (void)x; (void)y;
    if(GTK_IS_POPOVER(pop)) gtk_popover_set_autohide(GTK_POPOVER(pop), TRUE);
}
static gboolean v070_pop_key(GtkEventControllerKey *c, guint keyval, guint kc, GdkModifierType mods, gpointer pop){
    (void)c; (void)kc; (void)mods;
    if(keyval==GDK_KEY_Escape && GTK_IS_POPOVER(pop)){ gtk_popover_popdown(GTK_POPOVER(pop)); return TRUE; }
    return FALSE;
}

typedef struct { GtkWidget *row; char *conv; } V070PopIdle;
static gboolean v070_popup_idle(gpointer data){
    V070PopIdle *p=data; v070_popup(p->row,p->conv); g_free(p->conv); g_free(p); return G_SOURCE_REMOVE;
}
/* Open on released, then idle: even on release, GtkPopover autohide can still
   see the same secondary-click as click-outside if popup() runs inside the handler. */
static void v070_released(GtkGestureClick *g, gint n, gdouble x, gdouble y, gpointer p){
    GtkWidget *list=gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
    GtkListBoxRow *row; const char *conv; V070PopIdle *idle; (void)n; (void)p;
    row=gtk_list_box_get_row_at_y(GTK_LIST_BOX(list),(int)y); if(!row) return;
    conv=g_object_get_data(G_OBJECT(row),"conv"); if(!conv) return;
    gtk_gesture_set_state(GTK_GESTURE(g), GTK_EVENT_SEQUENCE_CLAIMED);
    idle=g_new0(V070PopIdle,1); idle->row=GTK_WIDGET(row); idle->conv=g_strdup(conv);
    g_idle_add(v070_popup_idle, idle);
}
static GtkWidget *v070_page(const char *title, const char *hook, const char *body){
    GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,8), *h=gtk_label_new(title), *t=gtk_label_new(body);
    gtk_widget_set_name(box,hook); gtk_widget_add_css_class(box,"lsm-v070-page");
    gtk_label_set_xalign(GTK_LABEL(h),0); gtk_label_set_xalign(GTK_LABEL(t),0); gtk_label_set_wrap(GTK_LABEL(t),TRUE);
    gtk_widget_set_margin_start(box,16); gtk_widget_set_margin_end(box,16); gtk_widget_set_margin_top(box,12);
    { char *m=g_markup_printf_escaped("<span size='16000' weight='600'>%s</span>",title); gtk_label_set_markup(GTK_LABEL(h),m); g_free(m); }
    gtk_box_append(GTK_BOX(box),h); gtk_box_append(GTK_BOX(box),t); return box;
}
static void v070_fill_contacts(GtkWidget *box){
    GtkWidget *sc=gtk_scrolled_window_new(), *lb=gtk_list_box_new(); lsm_contact *k; int n,i;
    lsm_lock(C); n=lsm_contacts(C,&k);
    for(i=0;i<n;i++){ char t[400]; GtkWidget *row=gtk_label_new(NULL);
        snprintf(t,sizeof t,"%s\n%s · %s",lsm_display_name(&k[i]),k[i].trust==TRUST_TRUSTED?"已信任":k[i].trust==TRUST_BLOCKED?"已屏蔽":"未信任",k[i].online?"在线":"离线");
        gtk_label_set_text(GTK_LABEL(row),t); gtk_label_set_xalign(GTK_LABEL(row),0);
        gtk_list_box_append(GTK_LIST_BOX(lb),row); }
    lsm_unlock(C); gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc),lb); gtk_widget_set_vexpand(sc,TRUE); gtk_box_append(GTK_BOX(box),sc);
}
static void v070_fill_files(GtkWidget *box){ char info[2048]=""; GtkWidget *t; lsm_storage_info(C,info,sizeof info); t=gtk_label_new(info[0]?info:"（存储信息不可用）"); gtk_label_set_xalign(GTK_LABEL(t),0); gtk_label_set_wrap(GTK_LABEL(t),TRUE); gtk_box_append(GTK_BOX(box),t); }
static void v070_fill_profile(GtkWidget *box){ char b[800]; GtkWidget *t; lsm_lock(C); snprintf(b,sizeof b,"显示名称：%s\n身份指纹：%s\n版本 %s",lsm_my_name(C),lsm_my_fp(C),LSM_APP_VERSION); lsm_unlock(C); t=gtk_label_new(b); gtk_label_set_xalign(GTK_LABEL(t),0); gtk_box_append(GTK_BOX(box),t); }
static void v070_fill_diag(GtkWidget *box){
    size_t n=lsm_diagnostics(C,NULL,0); char *b=g_malloc(n+1); GtkWidget *tv=gtk_text_view_new(), *sc=gtk_scrolled_window_new();
    lsm_diagnostics(C,b,n+1); gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv)),b,-1); g_free(b);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv),FALSE); gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv),GTK_WRAP_WORD_CHAR);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc),tv); gtk_widget_set_vexpand(sc,TRUE); gtk_box_append(GTK_BOX(box),sc);
}
static GtkWidget *v070_nav_btn(const char *label, const char *page, const char *hook){
    GtkWidget *b=gtk_button_new_with_label(label);
    gtk_widget_set_name(b,hook); gtk_widget_set_tooltip_text(b,label); gtk_button_set_has_frame(GTK_BUTTON(b),FALSE);
    g_object_set_data(G_OBJECT(b),"page",(gpointer)page);
    g_signal_connect(b,"clicked",G_CALLBACK(v070_nav_cb),(gpointer)page);
    { GtkWidget *c=gtk_button_get_child(GTK_BUTTON(b)); if(GTK_IS_LABEL(c)){ gtk_label_set_ellipsize(GTK_LABEL(c),PANGO_ELLIPSIZE_NONE); gtk_label_set_wrap(GTK_LABEL(c),FALSE); } }
    return b;
}
static void v070_sync_nav(const char *page){
    GtkWidget *b;
    if(!v070_rail) return;
    for(b=gtk_widget_get_first_child(v070_rail); b; b=gtk_widget_get_next_sibling(b)){
        const char *p=g_object_get_data(G_OBJECT(b),"page");
        if(p && page && !strcmp(p,page)) gtk_widget_add_css_class(b,"lsm-nav-current");
        else gtk_widget_remove_css_class(b,"lsm-nav-current");
    }
}
static GtkWidget *v070_wrap(GtkWidget *chats){
    GtkWidget *outer=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0), *rail=gtk_box_new(GTK_ORIENTATION_VERTICAL,2), *st=gtk_stack_new();
    GtkWidget *contacts,*groups,*xfers,*files,*settings,*profile,*diag; GtkGesture *g;
    v070_stack=st; v070_rail=rail; gtk_widget_set_size_request(rail,112,-1);
    gtk_widget_add_css_class(rail,"lsm-v070-rail");
    gtk_box_append(GTK_BOX(rail),v070_nav_btn("聊天","chats","LSMF_nav_chats"));
    gtk_box_append(GTK_BOX(rail),v070_nav_btn("联系人","contacts","LSMF_nav_contacts"));
    gtk_box_append(GTK_BOX(rail),v070_nav_btn("群组","groups","LSMF_nav_groups"));
    gtk_box_append(GTK_BOX(rail),v070_nav_btn("传输","transfers","LSMF_nav_transfers"));
    gtk_box_append(GTK_BOX(rail),v070_nav_btn("文件站","files","LSMF_nav_files"));
    gtk_box_append(GTK_BOX(rail),v070_nav_btn("设置","settings","LSMF_nav_settings"));
    gtk_box_append(GTK_BOX(rail),v070_nav_btn("个人资料","profile","LSMF_nav_profile"));
    gtk_box_append(GTK_BOX(rail),v070_nav_btn("诊断","diagnostics","LSMF_nav_diagnostics"));
    contacts=v070_page("联系人","LSMF_nav_contacts","同一局域网内发现的设备。"); v070_fill_contacts(contacts);
    groups=v070_page("群组","LSMF_nav_groups","从菜单创建或管理群组，或从聊天列表选择一个群。");
    xfers=v070_page("文件传输","LSMF_nav_transfers","进行中的发送和接收。也可通过菜单打开详细窗口。");
    files=v070_page("文件站","LSMF_nav_files","接收文件与本机存储。"); v070_fill_files(files);
    settings=v070_page("设置","LSMF_nav_settings","主题跟随系统外观。名称、中转服务器等仍可从菜单打开。");
    profile=v070_page("个人资料","LSMF_nav_profile","本机身份。"); v070_fill_profile(profile);
    diag=v070_page("诊断","LSMF_nav_diagnostics","运行状态与连接信息。"); v070_fill_diag(diag);
    gtk_stack_add_named(GTK_STACK(st),chats,"chats"); gtk_stack_add_named(GTK_STACK(st),contacts,"contacts");
    gtk_stack_add_named(GTK_STACK(st),groups,"groups"); gtk_stack_add_named(GTK_STACK(st),xfers,"transfers");
    gtk_stack_add_named(GTK_STACK(st),files,"files"); gtk_stack_add_named(GTK_STACK(st),settings,"settings");
    gtk_stack_add_named(GTK_STACK(st),profile,"profile"); gtk_stack_add_named(GTK_STACK(st),diag,"diagnostics");
    gtk_widget_set_hexpand(st,TRUE); gtk_box_append(GTK_BOX(outer),rail); gtk_box_append(GTK_BOX(outer),st);
    if(conv_list){ g=gtk_gesture_click_new(); gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(g),GDK_BUTTON_SECONDARY);
        g_signal_connect(g,"released",G_CALLBACK(v070_released),NULL); gtk_widget_add_controller(conv_list,GTK_EVENT_CONTROLLER(g)); }
    v073_enhance(contacts,groups,xfers,files,settings,profile,diag);
    v070_sync_nav("chats");
    return outer;
}
