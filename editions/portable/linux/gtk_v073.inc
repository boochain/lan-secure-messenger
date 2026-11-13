/* v0.7.3: icon-led settings / profile / files pages, menu icons, profile quick actions.
 * Icons are drawn with cairo (no icon theme) so AppImage never shows tofu. */
enum { IC_PERSON=1, IC_EDIT, IC_LOCK, IC_CLOUD, IC_FOLDER, IC_NET, IC_GROUP, IC_QR, IC_INFO,
       IC_STORAGE, IC_DIAG, IC_SEARCH, IC_COPY, IC_CHAT, IC_TRASH, IC_SHIELD, IC_ADD, IC_DOWNLOAD,
       IC_TICK, IC_MUTE, IC_IMAGE, IC_PIN };
static void act(GtkWidget *w, gpointer p);
static int v073_dark(void){ gboolean d=FALSE; GtkSettings *s=gtk_settings_get_default(); if(s) g_object_get(s,"gtk-application-prefer-dark-theme",&d,NULL); return d; }
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
static GdkPixbuf *v073_pixbuf(int k, int px, int tile){
    cairo_surface_t *s=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,px,px); cairo_t *cr=cairo_create(s); GdkPixbuf *pb; int dark=v073_dark();
    cairo_set_operator(cr,CAIRO_OPERATOR_CLEAR); cairo_paint(cr); cairo_set_operator(cr,CAIRO_OPERATOR_OVER);
    if(tile){
        double r=px*0.22, x=0, y=0, w=px, h=px;
        if(dark) cairo_set_source_rgb(cr,0x60/255.0,0xCD/255.0,1);
        else cairo_set_source_rgb(cr,0,0x5F/255.0,0xB8/255.0);
        cairo_move_to(cr,x+r,y); cairo_arc(cr,x+w-r,y+r,r,-G_PI/2,0); cairo_arc(cr,x+w-r,y+h-r,r,0,G_PI/2);
        cairo_arc(cr,x+r,y+h-r,r,G_PI/2,G_PI); cairo_arc(cr,x+r,y+r,r,G_PI,3*G_PI/2); cairo_close_path(cr); cairo_fill(cr);
        if(dark) cairo_set_source_rgb(cr,0,0,0); else cairo_set_source_rgb(cr,1,1,1);
    }else{
        if(dark) cairo_set_source_rgb(cr,0x60/255.0,0xCD/255.0,1);
        else cairo_set_source_rgb(cr,0,0x5F/255.0,0xB8/255.0);
    }
    cairo_save(cr); cairo_translate(cr,px*2.0/24.0,px*2.0/24.0); cairo_scale(cr,px*20.0/24.0/24.0,px*20.0/24.0/24.0); v073_glyph(cr,k); cairo_restore(cr);
    pb=gdk_pixbuf_get_from_surface(s,0,0,px,px); cairo_destroy(cr); cairo_surface_destroy(s); return pb;
}
static GtkWidget *v073_img(int k, int px, int tile){ GdkPixbuf *pb=v073_pixbuf(k,px,tile); GtkWidget *im=gtk_image_new_from_pixbuf(pb); if(pb) g_object_unref(pb); return im; }
static int v073_icon_for(int id){
    switch(id){
    case A_PROFILE: case A_NAME: case A_CPROFILE: return IC_PERSON;
    case A_REVEALRK: case A_ACCTMODE: case A_PINOFFER: case A_PINPAIR: return IC_LOCK;
    case A_SERVERQR: case A_PAIRQR: case A_MYQR: case A_ADDURI: return IC_QR;
    case A_RELAY: return IC_CLOUD;
    case A_ADDIP: return IC_ADD;
    case A_GCREATE: case A_GINVITE: case A_GREMOVE: case A_GLEAVE: return IC_GROUP;
    case A_DLDIR: return IC_FOLDER;
    case A_STORAGE: return IC_STORAGE;
    case A_XFERS: return IC_DOWNLOAD;
    case A_FIREWALL: return IC_SHIELD;
    case A_DIAG: return IC_DIAG;
    case A_UPDATE: case A_ABOUT: return IC_INFO;
    case A_REFRESH: return IC_NET;
    case A_TRUST: case A_UNTRUST: return IC_SHIELD;
    case A_MUTE: return IC_MUTE;
    case A_HISTORY: return IC_SEARCH;
    case A_QUIT: return IC_TRASH;
    default: return IC_INFO;
    }
}
static GtkWidget *v073_row(int icon, const char *title, const char *sub, int action){
    GtkWidget *row=gtk_list_box_row_new(), *box=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12), *txt=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);
    GtkWidget *t=gtk_label_new(title), *s=NULL;
    gtk_style_context_add_class(gtk_widget_get_style_context(row),"lsm-icon-row");
    gtk_widget_set_margin_start(box,8); gtk_widget_set_margin_end(box,8); gtk_widget_set_margin_top(box,8); gtk_widget_set_margin_bottom(box,8);
    gtk_label_set_xalign(GTK_LABEL(t),0); gtk_label_set_line_wrap(GTK_LABEL(t),TRUE);
    gtk_box_pack_start(GTK_BOX(txt),t,FALSE,FALSE,0);
    if(sub && sub[0]){ s=gtk_label_new(sub); gtk_label_set_xalign(GTK_LABEL(s),0); gtk_label_set_line_wrap(GTK_LABEL(s),TRUE);
        gtk_style_context_add_class(gtk_widget_get_style_context(s),"dim-label"); gtk_box_pack_start(GTK_BOX(txt),s,FALSE,FALSE,0); }
    gtk_box_pack_start(GTK_BOX(box),v073_img(icon,36,1),FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(box),txt,TRUE,TRUE,0);
    gtk_container_add(GTK_CONTAINER(row),box);
    if(action){ g_object_set_data(G_OBJECT(row),"act",GINT_TO_POINTER(action)); gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row),TRUE); }
    else gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row),FALSE);
    return row;
}
static void v073_row_act(GtkListBox *b, GtkListBoxRow *r, gpointer p){
    int id=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(r),"act")); (void)b;(void)p;
    if(id) act(NULL,GINT_TO_POINTER(id));
}
static GtkWidget *v073_list(void){
    GtkWidget *sc=gtk_scrolled_window_new(NULL,NULL), *lb=gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(lb),GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(sc),lb);
    g_signal_connect(lb,"row-activated",G_CALLBACK(v073_row_act),NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
    g_object_set_data(G_OBJECT(sc),"lb",lb);
    return sc;
}
static void v073_add(GtkWidget *sc, int icon, const char *title, const char *sub, int action){
    GtkWidget *lb=g_object_get_data(G_OBJECT(sc),"lb");
    gtk_list_box_insert(GTK_LIST_BOX(lb),v073_row(icon,title,sub,action),-1);
}
static void v073_clear_body(GtkWidget *page){
    GList *ch=gtk_container_get_children(GTK_CONTAINER(page)), *l; int i=0;
    for(l=ch;l;l=l->next,i++) if(i>0) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(ch);
}
static void v073_qa_clicked(GtkButton *b, gpointer p){ int id=GPOINTER_TO_INT(p); (void)b; if(id==A_NAME){ /* copy fingerprint special: id 0 handled elsewhere */ } if(id) act(NULL,GINT_TO_POINTER(id)); }
static GtkWidget *v073_qa(int icon, const char *caption, int action){
    GtkWidget *b=gtk_button_new(), *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,4), *l=gtk_label_new(caption);
    gtk_style_context_add_class(gtk_widget_get_style_context(b),"lsm-qa-btn");
    gtk_button_set_relief(GTK_BUTTON(b),GTK_RELIEF_NONE);
    gtk_label_set_line_wrap(GTK_LABEL(l),TRUE); gtk_label_set_justify(GTK_LABEL(l),GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(box),v073_img(icon,36,1),FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(box),l,FALSE,FALSE,0);
    gtk_widget_set_halign(box,GTK_ALIGN_CENTER);
    gtk_container_add(GTK_CONTAINER(b),box);
    g_signal_connect(b,"clicked",G_CALLBACK(v073_qa_clicked),GINT_TO_POINTER(action));
    return b;
}
static void v073_copy_fp(GtkButton *b, gpointer p){ char fp[80]=""; (void)b;(void)p; lsm_lock(C); snprintf(fp,sizeof fp,"%s",lsm_my_fp(C)); lsm_unlock(C);
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),fp,-1); }
static void v073_relay_dialog(void){
    char host[256]="", err[256]=""; int en=0, port=9443, hp=0;
    GtkWidget *d,*box,*sw,*he,*pe,*l; char ps[16];
    lsm_relay_get(C,&en,host,sizeof host,&port,&hp);
    d=gtk_dialog_new_with_buttons("中转服务器",GTK_WINDOW(win),GTK_DIALOG_MODAL,"取消",GTK_RESPONSE_CANCEL,"保存并连接",GTK_RESPONSE_OK,NULL);
    box=gtk_dialog_get_content_area(GTK_DIALOG(d)); gtk_container_set_border_width(GTK_CONTAINER(box),16); gtk_box_set_spacing(GTK_BOX(box),10);
    l=gtk_label_new("不在同一局域网时，可通过你自己部署的中转联系对方。局域网直连始终优先；服务器只转发端到端加密的数据。");
    gtk_label_set_line_wrap(GTK_LABEL(l),TRUE); gtk_label_set_xalign(GTK_LABEL(l),0); gtk_box_pack_start(GTK_BOX(box),l,FALSE,FALSE,0);
    sw=gtk_check_button_new_with_label("通过中转服务器连接"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sw),en);
    gtk_box_pack_start(GTK_BOX(box),sw,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(box),gtk_label_new("服务器地址"),FALSE,FALSE,0);
    he=gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(he),host); gtk_entry_set_placeholder_text(GTK_ENTRY(he),"例如 relay.example.com"); gtk_box_pack_start(GTK_BOX(box),he,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(box),gtk_label_new("端口"),FALSE,FALSE,0);
    pe=gtk_entry_new(); snprintf(ps,sizeof ps,"%d",port?port:9443); gtk_entry_set_text(GTK_ENTRY(pe),ps); gtk_box_pack_start(GTK_BOX(box),pe,FALSE,FALSE,0);
    gtk_widget_show_all(d);
    if(gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_OK){
        const char *h=gtk_entry_get_text(GTK_ENTRY(he)); int po=atoi(gtk_entry_get_text(GTK_ENTRY(pe))); if(po<=0) po=9443;
        if(lsm_relay_set(C, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sw))?1:0, h, po, "", err))
            msgbox(GTK_MESSAGE_WARNING,"%s",err[0]?err:"无法保存中转设置");
        else msgbox(GTK_MESSAGE_INFO,"已保存中转设置");
    }
    gtk_widget_destroy(d);
}
static void v073_fill_settings(GtkWidget *page){
    GtkWidget *sc; char sub[200]; const char *cur;
    v073_clear_body(page); sc=v073_list();
    cur=lsm_account_mode(C);
    v073_add(sc,IC_PERSON,"个人资料","名称、指纹、头像与二维码",A_PROFILE);
    v073_add(sc,IC_EDIT,"修改昵称","更改本机显示名称",A_NAME);
    snprintf(sub,sizeof sub,"%s", !strcmp(cur,"account")?"身份在中转上加密备份，点按可切回本机":"仅本机保存身份，点按可切换为账号模式");
    v073_add(sc,IC_LOCK, !strcmp(cur,"account")?"账号模式":"设备模式", sub, A_ACCTMODE);
    v073_add(sc,IC_LOCK,"恢复密钥","查看、复制或导出；仅本机可见",A_REVEALRK);
    { int en=0,port=0,hp=0; char host[256]=""; lsm_relay_get(C,&en,host,sizeof host,&port,&hp); (void)port;(void)hp;
      snprintf(sub,sizeof sub,"%s", en?(host[0]?host:"已启用"):"未启用 · 局域网直连优先");
      v073_add(sc,IC_CLOUD,"中转服务器",sub,A_RELAY); }
    v073_add(sc,IC_QR,"服务器二维码","让另一台设备扫码接入同一中转",A_SERVERQR);
    v073_add(sc,IC_QR,"配对新设备","一次性配对码，需账号模式并已连中转",A_PAIRQR);
    v073_add(sc,IC_LOCK,"生成 PIN","让对方输入，配对并互相信任",A_PINOFFER);
    v073_add(sc,IC_ADD,"按 IP 添加","不在同一网段时手动添加",A_ADDIP);
    v073_add(sc,IC_GROUP,"创建群组","从已信任的联系人中选择",A_GCREATE);
    v073_add(sc,IC_QR,"扫描链接","添加联系人、接入中转或配对新设备",A_ADDURI);
    v073_add(sc,IC_FOLDER,"接收的文件","打开下载文件夹",A_DLDIR);
    v073_add(sc,IC_STORAGE,"存储管理","查看占用并清理半成品",A_STORAGE);
    v073_add(sc,IC_SHIELD,"防火墙","为 ufw / firewalld 添加放行规则",A_FIREWALL);
    v073_add(sc,IC_DIAG,"诊断信息","连接与排障",A_DIAG);
    v073_add(sc,IC_INFO,"检查更新","查看是否有新版本",A_UPDATE);
    v073_add(sc,IC_INFO,"关于","版本与协议说明",A_ABOUT);
    gtk_box_pack_start(GTK_BOX(page),sc,TRUE,TRUE,0); gtk_widget_show_all(page);
}
static void v073_fill_profile(GtkWidget *page){
    char name[128]="", fp[80]="", b[200]; GtkWidget *head,*qa,*sc,*ini; const char *ch;
    v073_clear_body(page);
    lsm_lock(C); snprintf(name,sizeof name,"%s",lsm_my_name(C)); snprintf(fp,sizeof fp,"%s",lsm_my_fp(C)); lsm_unlock(C);
    head=gtk_box_new(GTK_ORIENTATION_VERTICAL,6); gtk_widget_set_halign(head,GTK_ALIGN_CENTER); gtk_widget_set_margin_top(head,8);
    ini=gtk_label_new(NULL);
    { gchar *m; char ic[8]="?"; const unsigned char *u=(const unsigned char*)name;
      if(u[0]>=0xE0){ memcpy(ic,name,3); ic[3]=0; } else if(name[0]){ ic[0]=name[0]; ic[1]=0; }
      m=g_markup_printf_escaped("<span size='18000' weight='600'>%s</span>",ic); gtk_label_set_markup(GTK_LABEL(ini),m); g_free(m); }
    gtk_widget_set_size_request(ini,64,64);
    gtk_box_pack_start(GTK_BOX(head),ini,FALSE,FALSE,0);
    { GtkWidget *n=gtk_label_new(name); gtk_style_context_add_class(gtk_widget_get_style_context(n),"subtitle-text"); gtk_box_pack_start(GTK_BOX(head),n,FALSE,FALSE,0); }
    qa=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8); gtk_widget_set_halign(qa,GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(qa),v073_qa(IC_EDIT,"昵称",A_NAME),FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(qa),v073_qa(IC_QR,"二维码",A_MYQR),FALSE,FALSE,0);
    { GtkWidget *c=v073_qa(IC_COPY,"复制",0); g_signal_connect(c,"clicked",G_CALLBACK(v073_copy_fp),NULL); gtk_box_pack_start(GTK_BOX(qa),c,FALSE,FALSE,0); }
    gtk_box_pack_start(GTK_BOX(qa),v073_qa(IC_TICK,"资料",A_PROFILE),FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(head),qa,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(page),head,FALSE,FALSE,0);
    sc=v073_list();
    snprintf(b,sizeof b,"%s",fp); v073_add(sc,IC_LOCK,"指纹",b,0);
    snprintf(b,sizeof b,"版本 %s",LSM_APP_VERSION); v073_add(sc,IC_INFO,"版本",b,0);
    gtk_box_pack_start(GTK_BOX(page),sc,TRUE,TRUE,0); gtk_widget_show_all(page);
}
static void v073_open_dm(GtkListBox *b, GtkListBoxRow *r, gpointer p){
    const char *fp=g_object_get_data(G_OBJECT(r),"fp"); (void)b;(void)p;
    if(!fp) return; snprintf(cur_conv,sizeof cur_conv,"dm:%s",fp); v070_show_page("chats"); list_dirty=chat_dirty=1; refresh_list(); refresh_chat();
}
static void v073_fill_contacts(GtkWidget *page){
    GtkWidget *sc,*lb; lsm_contact *k; int n,i;
    v073_clear_body(page); sc=v073_list(); lb=g_object_get_data(G_OBJECT(sc),"lb");
    g_signal_handlers_disconnect_by_func(lb, (gpointer)v073_row_act, NULL);
    g_signal_connect(lb,"row-activated",G_CALLBACK(v073_open_dm),NULL);
    lsm_lock(C); n=lsm_contacts(C,&k);
    for(i=0;i<n;i++){ char sub[160]; GtkWidget *row;
        snprintf(sub,sizeof sub,"%s · %s", k[i].trust==TRUST_TRUSTED?"已信任":k[i].trust==TRUST_BLOCKED?"已屏蔽":"未信任", k[i].online?"在线":"离线");
        row=v073_row(k[i].trust==TRUST_TRUSTED?IC_SHIELD:IC_PERSON, lsm_display_name(&k[i]), sub, 0);
        g_object_set_data_full(G_OBJECT(row),"fp",g_strdup(k[i].fp),g_free);
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row),TRUE);
        gtk_list_box_insert(GTK_LIST_BOX(lb),row,-1);
    }
    lsm_unlock(C);
    if(!n){ GtkWidget *e=gtk_label_new("还没有联系人。同一网段下运行本软件的设备会自动出现在这里。");
        gtk_style_context_add_class(gtk_widget_get_style_context(e),"dim-label"); gtk_label_set_line_wrap(GTK_LABEL(e),TRUE); gtk_box_pack_start(GTK_BOX(page),e,FALSE,FALSE,8); }
    gtk_box_pack_start(GTK_BOX(page),sc,TRUE,TRUE,0); gtk_widget_show_all(page);
}
static void v073_fill_files(GtkWidget *page){
    char info[2048]=""; GtkWidget *sc;
    v073_clear_body(page); sc=v073_list();
    lsm_storage_info(C,info,sizeof info);
    v073_add(sc,IC_STORAGE,"存储用量",info[0]?info:"（存储信息不可用）",A_STORAGE);
    v073_add(sc,IC_FOLDER,"接收文件夹","打开已下载的文件",A_DLDIR);
    v073_add(sc,IC_DOWNLOAD,"文件传输","查看进行中的发送和接收",A_XFERS);
    gtk_box_pack_start(GTK_BOX(page),sc,TRUE,TRUE,0); gtk_widget_show_all(page);
}
static void v073_fill_transfers(GtkWidget *page){
    GtkWidget *sc; v073_clear_body(page); sc=v073_list();
    v073_add(sc,IC_DOWNLOAD,"打开传输窗口","查看进行中的发送和接收",A_XFERS);
    v073_add(sc,IC_FOLDER,"接收文件夹","打开发布到本机的文件",A_DLDIR);
    gtk_box_pack_start(GTK_BOX(page),sc,TRUE,TRUE,0); gtk_widget_show_all(page);
}
static void v073_fill_groups(GtkWidget *page){
    GtkWidget *sc; v073_clear_body(page); sc=v073_list();
    v073_add(sc,IC_GROUP,"创建群组","从已信任的联系人中选择成员",A_GCREATE);
    v073_add(sc,IC_ADD,"邀请成员","向当前群组添加联系人",A_GINVITE);
    v073_add(sc,IC_TRASH,"退出群组","离开当前选中的群",A_GLEAVE);
    gtk_box_pack_start(GTK_BOX(page),sc,TRUE,TRUE,0); gtk_widget_show_all(page);
}
static GtkWidget *v073_menu_item(const char *label, int id){
    GtkWidget *i=gtk_menu_item_new(), *box=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8), *l=gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(l),0); gtk_widget_set_direction(i,GTK_TEXT_DIR_LTR); gtk_widget_set_direction(box,GTK_TEXT_DIR_LTR);
    gtk_box_pack_start(GTK_BOX(box),v073_img(v073_icon_for(id),18,0),FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(box),l,TRUE,TRUE,0);
    gtk_container_add(GTK_CONTAINER(i),box);
    return i;
}
static void v073_enhance(GtkWidget *contacts, GtkWidget *groups, GtkWidget *xfers, GtkWidget *files, GtkWidget *settings, GtkWidget *profile, GtkWidget *diag){
    (void)diag;
    v073_fill_contacts(contacts); v073_fill_groups(groups); v073_fill_transfers(xfers);
    v073_fill_files(files); v073_fill_settings(settings); v073_fill_profile(profile);
}
