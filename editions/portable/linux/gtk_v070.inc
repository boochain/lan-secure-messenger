/* v0.7.0 (GTK3): Telegram-like pages + chat-list context menu (right-click). */
static GtkWidget *v070_stack, *v070_rail;
static void v070_show_page(const char *name){
    if(v070_stack) gtk_stack_set_visible_child_name(GTK_STACK(v070_stack), name);
}
static void v070_sync_nav(const char *page);
static void v070_nav_cb(GtkButton *b, gpointer name){ (void)b; v070_show_page((const char*)name); v070_sync_nav((const char*)name); }

static void v070_conv_act(GtkMenuItem *it, gpointer conv){
    const char *act=g_object_get_data(G_OBJECT(it),"act"); char err[256]="";
    if(!act||!conv) return;
    if(!strcmp(act,"delete")){ /* LSMF_chat_delete */
        if(confirm("删除会话？","将删除本机上与此会话的全部聊天记录（对方仍保留）。此操作无法撤销。","删除")){ lsm_clear_conversation(C,(const char*)conv); refresh_chat(); refresh_list(); }
    } else if(!strcmp(act,"pin")){ /* LSMF_chat_pin */
        int on; lsm_lock(C); on=!lsm_conv_pinned(C,(const char*)conv); lsm_unlock(C);
        lsm_set_conv_pinned(C,(const char*)conv,on); refresh_list();
    } else if(!strcmp(act,"read")){ /* LSMF_chat_mark_read */
        lsm_mark_read(C,(const char*)conv); refresh_list(); refresh_chat();
    } else if(!strcmp(act,"unread")){ /* LSMF_chat_mark_unread */
        lsm_mark_unread(C,(const char*)conv); refresh_list();
    } else if(!strcmp(act,"mute")){ /* LSMF_chat_mute */
        const char *fp=(const char*)conv; if(!strncmp(fp,"dm:",3)) fp+=3;
        { int muted=0; lsm_contact *k; int n,i; lsm_lock(C); n=lsm_contacts(C,&k); for(i=0;i<n;i++) if(!strcmp(k[i].fp,fp)) muted=k[i].muted; lsm_unlock(C);
          lsm_set_muted(C,fp,!muted,err); refresh_list(); }
    }
}
static gint64 v070_last_pop;
static void v070_popup(GtkWidget *anchor, GdkEventButton *ev, const char *conv){
    gint64 now=g_get_monotonic_time();
    if(now - v070_last_pop < 300000) return; /* gesture + button-press can both fire */
    v070_last_pop=now;
    GtkWidget *menu=gtk_menu_new(), *it; const char *acts[][2]={{"置顶 / 取消置顶","pin"},{"标为已读","read"},{"标为未读","unread"},{"免打扰","mute"},{"删除会话…","delete"}};
    const char *hooks[]={"LSMF_chat_pin","LSMF_chat_mark_read","LSMF_chat_mark_unread","LSMF_chat_mute","LSMF_chat_delete"};
    int i;
    /* LSMF_chat_list_menu */
    gtk_widget_set_name(menu,"LSMF_chat_list_menu");
    for(i=0;i<5;i++){ it=gtk_menu_item_new_with_label(acts[i][0]); gtk_widget_set_name(it,hooks[i]);
        g_object_set_data(G_OBJECT(it),"act",(gpointer)acts[i][1]);
        g_signal_connect_data(it,"activate",G_CALLBACK(v070_conv_act),g_strdup(conv),(GClosureNotify)g_free,0);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu),it); }
    gtk_widget_show_all(menu);
    /* Anchor to the row (not only the pointer) so the menu stays on the list even if
       the GdkEvent coordinates belong to a child widget. */
    gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(anchor), GDK_GRAVITY_SOUTH_EAST, GDK_GRAVITY_NORTH_WEST, (GdkEvent*)ev);
    g_signal_connect(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), NULL);
    if(getenv("LSM_UI_TEST")){ printf("UI conv-menu\n"); fflush(stdout); }
}

static GtkListBoxRow *v070_row_from_xy(GtkWidget *list, GtkWidget *orig, gdouble x, gdouble y){
    GtkWidget *w; GtkListBoxRow *row=NULL; gint ly=(gint)y;
    for(w=orig; w; w=gtk_widget_get_parent(w)){
        if(GTK_IS_LIST_BOX_ROW(w)){ row=GTK_LIST_BOX_ROW(w); break; }
    }
    if(!row){
        if(orig && orig!=list){ gint xx,yy; if(gtk_widget_translate_coordinates(orig,list,(gint)x,(gint)y,&xx,&yy)) ly=yy; }
        row=gtk_list_box_get_row_at_y(GTK_LIST_BOX(list), ly);
    }
    return row;
}
static gboolean v070_list_button(GtkWidget *list, GdkEventButton *ev, gpointer p){
    GtkListBoxRow *row; const char *conv; (void)p;
    if(ev->type!=GDK_BUTTON_PRESS || ev->button!=3) return FALSE;
    row=v070_row_from_xy(list, gtk_get_event_widget((GdkEvent*)ev), ev->x, ev->y); if(!row) return FALSE;
    conv=g_object_get_data(G_OBJECT(row),"conv"); if(!conv) return FALSE;
    v070_popup(GTK_WIDGET(row),ev,conv); return TRUE;
}
/* Gesture gets widget-space coordinates even when a child consumes the GdkEvent. */
static void v070_row_pressed(GtkGestureMultiPress *g, gint n, gdouble x, gdouble y, gpointer p){
    GtkWidget *list=gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
    GtkListBoxRow *row; const char *conv; (void)n; (void)p;
    row=gtk_list_box_get_row_at_y(GTK_LIST_BOX(list),(gint)y); if(!row) return;
    conv=g_object_get_data(G_OBJECT(row),"conv"); if(!conv) return;
    v070_popup(GTK_WIDGET(row),NULL,conv);
}
static GtkWidget *v070_page(const char *title, const char *hook, const char *body){
    GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,8), *h=gtk_label_new(title), *t=gtk_label_new(body);
    gtk_widget_set_name(box,hook); gtk_style_context_add_class(gtk_widget_get_style_context(box),"lsm-v070-page");
    gtk_label_set_xalign(GTK_LABEL(h),0); gtk_label_set_xalign(GTK_LABEL(t),0);
    gtk_label_set_line_wrap(GTK_LABEL(t),TRUE); gtk_widget_set_margin_start(box,16); gtk_widget_set_margin_end(box,16); gtk_widget_set_margin_top(box,12);
    { gchar *m=g_markup_printf_escaped("<span size='16000' weight='600'>%s</span>",title); gtk_label_set_markup(GTK_LABEL(h),m); g_free(m); }
    gtk_box_pack_start(GTK_BOX(box),h,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),t,FALSE,FALSE,0);
    return box;
}
static void v070_fill_contacts(GtkWidget *box){
    GtkWidget *sc=gtk_scrolled_window_new(NULL,NULL), *lb=gtk_list_box_new(); lsm_contact *k; int n,i;
    lsm_lock(C); n=lsm_contacts(C,&k);
    for(i=0;i<n;i++){ char t[400]; GtkWidget *row=gtk_label_new(NULL);
        snprintf(t,sizeof t,"%s\n%s · %s",lsm_display_name(&k[i]),k[i].trust==TRUST_TRUSTED?"已信任":k[i].trust==TRUST_BLOCKED?"已屏蔽":"未信任",k[i].online?"在线":"离线");
        gtk_label_set_text(GTK_LABEL(row),t); gtk_label_set_xalign(GTK_LABEL(row),0); gtk_widget_set_margin_start(row,8); gtk_widget_set_margin_top(row,6); gtk_widget_set_margin_bottom(row,6);
        gtk_list_box_insert(GTK_LIST_BOX(lb),row,-1); }
    lsm_unlock(C);
    gtk_container_add(GTK_CONTAINER(sc),lb); gtk_box_pack_start(GTK_BOX(box),sc,TRUE,TRUE,0);
}
static void v070_fill_files(GtkWidget *box){
    char info[2048]=""; GtkWidget *t;
    lsm_storage_info(C,info,sizeof info);
    t=gtk_label_new(info[0]?info:"（存储信息不可用）"); gtk_label_set_xalign(GTK_LABEL(t),0); gtk_label_set_line_wrap(GTK_LABEL(t),TRUE);
    gtk_box_pack_start(GTK_BOX(box),t,FALSE,FALSE,0);
}
static void v070_fill_profile(GtkWidget *box){
    char b[800];
    lsm_lock(C); snprintf(b,sizeof b,"显示名称：%s\n身份指纹：%s\n版本 %s",lsm_my_name(C),lsm_my_fp(C),LSM_APP_VERSION); lsm_unlock(C);
    gtk_box_pack_start(GTK_BOX(box),gtk_label_new(b),FALSE,FALSE,0);
}
static void v070_fill_diag(GtkWidget *box){
    size_t n=lsm_diagnostics(C,NULL,0); char *b=g_malloc(n+1); GtkWidget *tv=gtk_text_view_new(), *sc=gtk_scrolled_window_new(NULL,NULL);
    lsm_diagnostics(C,b,n+1); gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv)),b,-1); g_free(b);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv),FALSE); gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv),GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(sc),tv); gtk_box_pack_start(GTK_BOX(box),sc,TRUE,TRUE,0);
}
static GtkWidget *v070_nav_btn(const char *label, const char *page, const char *hook){
    GtkWidget *b=gtk_button_new_with_label(label);
    gtk_widget_set_name(b,hook); gtk_widget_set_tooltip_text(b,label); gtk_button_set_relief(GTK_BUTTON(b),GTK_RELIEF_NONE);
    g_object_set_data(G_OBJECT(b),"page",(gpointer)page);
    g_signal_connect(b,"clicked",G_CALLBACK(v070_nav_cb),(gpointer)page);
    { GtkWidget *c=gtk_bin_get_child(GTK_BIN(b)); if(GTK_IS_LABEL(c)){ gtk_label_set_ellipsize(GTK_LABEL(c),PANGO_ELLIPSIZE_NONE); gtk_label_set_line_wrap(GTK_LABEL(c),FALSE); } }
    return b;
}
static void v070_sync_nav(const char *page){
    GList *ch,*l; GtkStyleContext *rsc;
    if(!v070_rail) return;
    rsc=gtk_widget_get_style_context(v070_rail);
    if(page && strcmp(page,"chats")) gtk_style_context_add_class(rsc,"lsm-v070-rail-on-page");
    else gtk_style_context_remove_class(rsc,"lsm-v070-rail-on-page");
    ch=gtk_container_get_children(GTK_CONTAINER(v070_rail));
    for(l=ch;l;l=l->next){
        GtkWidget *b=l->data; const char *p=g_object_get_data(G_OBJECT(b),"page");
        GtkStyleContext *sc=gtk_widget_get_style_context(b);
        if(p && page && !strcmp(p,page)) gtk_style_context_add_class(sc,"lsm-nav-current");
        else gtk_style_context_remove_class(sc,"lsm-nav-current");
    }
    g_list_free(ch);
}
static GtkWidget *v070_wrap(GtkWidget *chats){
    GtkWidget *outer=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0), *rail=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);
    GtkWidget *st=gtk_stack_new();
    GtkWidget *contacts,*groups,*xfers,*files,*settings,*profile,*diag;
    v070_stack=st; v070_rail=rail;
    gtk_widget_set_size_request(rail,112,-1);
    gtk_style_context_add_class(gtk_widget_get_style_context(rail),"lsm-v070-rail");
    gtk_box_pack_start(GTK_BOX(rail),v070_nav_btn("聊天","chats","LSMF_nav_chats"),FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(rail),v070_nav_btn("联系人","contacts","LSMF_nav_contacts"),FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(rail),v070_nav_btn("群组","groups","LSMF_nav_groups"),FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(rail),v070_nav_btn("传输","transfers","LSMF_nav_transfers"),FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(rail),v070_nav_btn("文件站","files","LSMF_nav_files"),FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(rail),v070_nav_btn("设置","settings","LSMF_nav_settings"),FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(rail),v070_nav_btn("个人资料","profile","LSMF_nav_profile"),FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(rail),v070_nav_btn("诊断","diagnostics","LSMF_nav_diagnostics"),FALSE,FALSE,0);
    contacts=v070_page("联系人","LSMF_nav_contacts","同一局域网内发现的设备。"); v070_fill_contacts(contacts);
    groups=v070_page("群组","LSMF_nav_groups","从菜单「群组」创建或管理群组，或从聊天列表选择一个群。");
    xfers=v070_page("文件传输","LSMF_nav_transfers","进行中的发送和接收。也可通过菜单「文件传输」打开详细窗口。");
    files=v070_page("文件站","LSMF_nav_files","接收文件与本机存储。"); v070_fill_files(files);
    settings=v070_page("设置","LSMF_nav_settings","主题跟随系统外观。名称、防火墙、中转服务器等仍可从标题栏菜单打开。");
    profile=v070_page("个人资料","LSMF_nav_profile","本机身份。"); v070_fill_profile(profile);
    diag=v070_page("诊断","LSMF_nav_diagnostics","运行状态与连接信息。"); v070_fill_diag(diag);
    gtk_stack_add_named(GTK_STACK(st),chats,"chats");
    gtk_stack_add_named(GTK_STACK(st),contacts,"contacts");
    gtk_stack_add_named(GTK_STACK(st),groups,"groups");
    gtk_stack_add_named(GTK_STACK(st),xfers,"transfers");
    gtk_stack_add_named(GTK_STACK(st),files,"files");
    gtk_stack_add_named(GTK_STACK(st),settings,"settings");
    gtk_stack_add_named(GTK_STACK(st),profile,"profile");
    gtk_stack_add_named(GTK_STACK(st),diag,"diagnostics");
    gtk_box_pack_start(GTK_BOX(outer),rail,FALSE,FALSE,0);
    /* No extra separator: a 1px divider next to the #EEEEEE sidebar joins the
       border network and fails the colour audit (indistinct #E5E5E5 vs #EEEEEE).
       The rail uses the sidebar colour so it merges with the chat list. */
    gtk_box_pack_start(GTK_BOX(outer),st,TRUE,TRUE,0);
    if(peer_list){
        GtkGesture *g;
        gtk_widget_add_events(peer_list,GDK_BUTTON_PRESS_MASK);
        g_signal_connect(peer_list,"button-press-event",G_CALLBACK(v070_list_button),NULL);
        g=gtk_gesture_multi_press_new(peer_list);
        gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(g), GTK_PHASE_BUBBLE);
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(g),GDK_BUTTON_SECONDARY);
        g_signal_connect(g,"pressed",G_CALLBACK(v070_row_pressed),NULL);
        g_object_set_data(G_OBJECT(peer_list),"v070-gesture",g);
    }
    v070_sync_nav("chats");
    return outer;
}
