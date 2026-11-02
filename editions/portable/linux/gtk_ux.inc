/* v0.6.1 UX (GTK3): my QR / add by link, one-click resend with reason, file size+ETA confirm, Ctrl+K quick switcher.
   ↑ (edit last) and Ctrl+Shift+F (history search) already live in gtk_gui.c / gtk_p0.inc. */
static void ux_select_conv(const char *cv){ if(!cv||!*cv) return; if(strcmp(cur_conv,cv)){ p2_save_draft(); snprintf(cur_conv,sizeof cur_conv,"%s",cv); p2_load_draft(); }
    list_dirty=1; refresh_list(); refresh_chat(); if(lsm_mark_read(C,cur_conv)) list_dirty=1; gtk_widget_grab_focus(input); }

/* ---- QR */
static uint8_t ux_qrm[177*177]; static int ux_qrn;
static gboolean ux_qr_draw(GtkWidget *w, cairo_t *cr, gpointer p){ int W=gtk_widget_get_allocated_width(w),H=gtk_widget_get_allocated_height(w),s,x,y,m; double sc,ox,oy; (void)p;
    if(!ux_qrn) return FALSE; s=W<H?W:H; m=ux_qrn+8; sc=(double)s/m; ox=(W-s)/2.0; oy=(H-s)/2.0;
    cairo_set_source_rgb(cr,1,1,1); cairo_rectangle(cr,ox,oy,s,s); cairo_fill(cr); cairo_set_source_rgb(cr,0,0,0); /* QR needs dark-on-light in both themes */
    for(y=0;y<ux_qrn;y++) for(x=0;x<ux_qrn;x++) if(ux_qrm[y*ux_qrn+x]) cairo_rectangle(cr,ox+(x+4)*sc,oy+(y+4)*sc,sc+0.3,sc+0.3);
    cairo_fill(cr); return TRUE; }
static void ux_my_qr(void){ char uri[400]; GtkWidget *d,*ca,*da,*l,*hint;
    if(lsm_my_uri(C,uri,sizeof uri)){ msgbox(GTK_MESSAGE_WARNING,"未找到局域网地址，无法生成二维码。"); return; }
    ux_qrn=lsm_qr(uri,ux_qrm);
    d=gtk_dialog_new_with_buttons("我的二维码",GTK_WINDOW(win),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"复制链接",1,"关闭",GTK_RESPONSE_CLOSE,NULL);
    ca=gtk_dialog_get_content_area(GTK_DIALOG(d)); gtk_container_set_border_width(GTK_CONTAINER(ca),12); gtk_box_set_spacing(GTK_BOX(ca),8);
    da=gtk_drawing_area_new(); gtk_widget_set_size_request(da,260,260); g_signal_connect(da,"draw",G_CALLBACK(ux_qr_draw),NULL); gtk_box_pack_start(GTK_BOX(ca),da,FALSE,FALSE,0);
    hint=gtk_label_new("让对方用手机版扫描，或把下面的链接发给对方，在「通过链接添加…」中粘贴。\n添加后会核对指纹，一致即自动设为已信任。"); gtk_label_set_line_wrap(GTK_LABEL(hint),TRUE); gtk_label_set_max_width_chars(GTK_LABEL(hint),40); gtk_box_pack_start(GTK_BOX(ca),hint,FALSE,FALSE,0);
    l=gtk_label_new(uri); gtk_label_set_selectable(GTK_LABEL(l),TRUE); gtk_label_set_line_wrap(GTK_LABEL(l),TRUE); gtk_label_set_line_wrap_mode(GTK_LABEL(l),PANGO_WRAP_CHAR); gtk_label_set_max_width_chars(GTK_LABEL(l),40); gtk_box_pack_start(GTK_BOX(ca),l,FALSE,FALSE,0);
    gtk_widget_show_all(d);
    while(gtk_dialog_run(GTK_DIALOG(d))==1) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),uri,-1);
    gtk_widget_destroy(d); }
static void ux_add_uri(void){ char err[256]=""; gchar *u=ask_text("通过链接添加","粘贴对方的添加链接（lsm://add?…）：","");
    if(!u) return; if(*g_strstrip(u) && lsm_add_uri(C,u,err)) msgbox(GTK_MESSAGE_WARNING,"%s",err); else if(*u) msgbox(GTK_MESSAGE_INFO,"正在连接对方……连接成功并核对指纹一致后会自动设为已信任。"); g_free(u); list_dirty=1; }

/* ---- resend */
static gboolean ux_resend_idle(gpointer id){ char err[256]="",nid[40]; if(lsm_resend(C,(const char*)id,nid,err)) msgbox(GTK_MESSAGE_WARNING,"%s",err); g_free(id); chat_dirty=list_dirty=1; refresh_chat(); return G_SOURCE_REMOVE; }
static void ux_resend_cb(GtkWidget *w, gpointer id){ (void)w; g_idle_add(ux_resend_idle,g_strdup((const char*)id)); }
static void ux_menu_items(GtkMenuShell *menu, const char *id){ lsm_msg *m; int n,i,can=0; lsm_lock(C); n=lsm_messages(C,&m); for(i=0;i<n;i++) if(!strcmp(m[i].id,id)){ can=lsm_can_resend(&m[i]); break; } lsm_unlock(C);
    if(can){ GtkWidget *it=gtk_menu_item_new_with_mnemonic("重新发送(_R)"); g_signal_connect_data(it,"activate",G_CALLBACK(ux_resend_cb),g_strdup(id),(GClosureNotify)g_free,0); gtk_menu_shell_append(menu,it); } }
/* under a failed / waiting own message: the reason + a one-click 重新发送 button (lock held) */
static void ux_row_extras(GtkWidget *row, GtkWidget *meta, const lsm_msg *m){ const char *st=lsm_status_text(C,m);
    if(st[0]) gtk_widget_set_tooltip_text(meta,st);
    if(m->out && (m->status==ST_FAILED||m->status==ST_REJECTED)){ GtkWidget *h=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6),*l=gtk_label_new(st);
        gtk_label_set_line_wrap(GTK_LABEL(l),TRUE); gtk_style_context_add_class(gtk_widget_get_style_context(l),"meta"); gtk_box_pack_start(GTK_BOX(h),l,FALSE,FALSE,0);
        if(lsm_can_resend(m)){ GtkWidget *b=gtk_button_new_with_mnemonic("重新发送(_R)"); g_signal_connect_data(b,"clicked",G_CALLBACK(ux_resend_cb),g_strdup(m->id),(GClosureNotify)g_free,0); gtk_box_pack_start(GTK_BOX(h),b,FALSE,FALSE,0); }
        gtk_widget_set_halign(h,GTK_ALIGN_END); gtk_box_pack_start(GTK_BOX(row),h,FALSE,FALSE,0); } }

/* ---- file preview before sending */
static int ux_confirm_file(const char *path){ char pv[700]; lsm_file_preview(C,cur_conv,path,pv,sizeof pv); return confirm("发送文件？",pv,"发送"); }

/* ---- Ctrl+K quick switcher */
typedef struct { GtkWidget *d,*e,*lb; } ux_qs;
static void ux_qs_fill(ux_qs *q){ GList *ch=gtk_container_get_children(GTK_CONTAINER(q->lb)),*it; const char *t=gtk_entry_get_text(GTK_ENTRY(q->e)); lsm_contact *k; lsm_group *g; int nk,ng,i,j,cnt=0;
    struct { int sc; char lab[320]; char cv[64]; } *r; for(it=ch;it;it=it->next) gtk_widget_destroy(GTK_WIDGET(it->data)); g_list_free(ch);
    lsm_lock(C); nk=lsm_contacts(C,&k); ng=lsm_groups(C,&g); r=g_new0(__typeof__(*r),nk+ng+1);
    for(i=0;i<nk;i++){ int s; if(k[i].trust==TRUST_BLOCKED) continue; s=lsm_fuzzy(lsm_display_name(&k[i]),t); if(!s) s=lsm_fuzzy(k[i].fp,t)/2; if(!s) continue; r[cnt].sc=s; snprintf(r[cnt].lab,320,"%s",lsm_display_name(&k[i])); snprintf(r[cnt].cv,64,"dm:%s",k[i].fp); cnt++; }
    for(i=0;i<ng;i++){ char u[37]; int s=lsm_fuzzy(g[i].name,t); if(!s) continue; lsm_uuid_str(g[i].id,u); r[cnt].sc=s; snprintf(r[cnt].lab,320,"群组「%s」",g[i].name); snprintf(r[cnt].cv,64,"group:%s",u); cnt++; }
    lsm_unlock(C);
    for(i=0;i<cnt;i++) for(j=i+1;j<cnt;j++) if(r[j].sc>r[i].sc){ __typeof__(*r) x=r[i]; r[i]=r[j]; r[j]=x; }
    for(i=0;i<cnt && i<30;i++){ GtkWidget *row=gtk_list_box_row_new(),*l=gtk_label_new(r[i].lab); gtk_widget_set_halign(l,GTK_ALIGN_START); gtk_widget_set_margin_start(l,8); gtk_widget_set_margin_top(l,4); gtk_widget_set_margin_bottom(l,4);
        gtk_container_add(GTK_CONTAINER(row),l); g_object_set_data_full(G_OBJECT(row),"conv",g_strdup(r[i].cv),g_free); gtk_list_box_insert(GTK_LIST_BOX(q->lb),row,-1); }
    g_free(r); gtk_widget_show_all(q->lb); { GtkListBoxRow *f=gtk_list_box_get_row_at_index(GTK_LIST_BOX(q->lb),0); if(f) gtk_list_box_select_row(GTK_LIST_BOX(q->lb),f); } }
static void ux_qs_changed(GtkEditable *e, gpointer p){ (void)e; ux_qs_fill(p); }
static void ux_qs_pick(ux_qs *q, GtkListBoxRow *row){ if(!row) row=gtk_list_box_get_selected_row(GTK_LIST_BOX(q->lb)); if(!row) return; { char cv[64]; snprintf(cv,sizeof cv,"%s",(const char*)g_object_get_data(G_OBJECT(row),"conv")); gtk_dialog_response(GTK_DIALOG(q->d),GTK_RESPONSE_CLOSE); ux_select_conv(cv); } }
static void ux_qs_act(GtkEntry *e, gpointer p){ (void)e; ux_qs_pick(p,NULL); }
static void ux_qs_row(GtkListBox *lb, GtkListBoxRow *row, gpointer p){ (void)lb; ux_qs_pick(p,row); }
static gboolean ux_qs_key(GtkWidget *w, GdkEventKey *ev, gpointer p){ ux_qs *q=p; (void)w; if(ev->keyval==GDK_KEY_Down||ev->keyval==GDK_KEY_Up){ GtkListBoxRow *r=gtk_list_box_get_selected_row(GTK_LIST_BOX(q->lb)); int i=r?gtk_list_box_row_get_index(r):-1; i+=ev->keyval==GDK_KEY_Down?1:-1; r=i>=0?gtk_list_box_get_row_at_index(GTK_LIST_BOX(q->lb),i):NULL; if(r) gtk_list_box_select_row(GTK_LIST_BOX(q->lb),r); return TRUE; } return FALSE; }
static void ux_quick_switch(void){ ux_qs q; GtkWidget *ca,*sw;
    q.d=gtk_dialog_new_with_buttons("切换会话 (Ctrl+K)",GTK_WINDOW(win),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"关闭",GTK_RESPONSE_CLOSE,NULL); gtk_window_set_default_size(GTK_WINDOW(q.d),380,360);
    ca=gtk_dialog_get_content_area(GTK_DIALOG(q.d)); gtk_container_set_border_width(GTK_CONTAINER(ca),8); gtk_box_set_spacing(GTK_BOX(ca),6);
    q.e=gtk_entry_new(); gtk_widget_set_tooltip_text(q.e,"输入名称的任意几个字，回车打开"); gtk_entry_set_icon_from_icon_name(GTK_ENTRY(q.e),GTK_ENTRY_ICON_PRIMARY,"edit-find-symbolic"); gtk_box_pack_start(GTK_BOX(ca),q.e,FALSE,FALSE,0);
    sw=gtk_scrolled_window_new(NULL,NULL); q.lb=gtk_list_box_new(); gtk_container_add(GTK_CONTAINER(sw),q.lb); gtk_box_pack_start(GTK_BOX(ca),sw,TRUE,TRUE,0);
    g_signal_connect(q.e,"changed",G_CALLBACK(ux_qs_changed),&q); g_signal_connect(q.e,"activate",G_CALLBACK(ux_qs_act),&q); g_signal_connect(q.e,"key-press-event",G_CALLBACK(ux_qs_key),&q); g_signal_connect(q.lb,"row-activated",G_CALLBACK(ux_qs_row),&q);
    ux_qs_fill(&q); gtk_widget_show_all(q.d); if(getenv("LSM_UI_TEST")) printf("UI quick-switch\n"), fflush(stdout); gtk_dialog_run(GTK_DIALOG(q.d)); gtk_widget_destroy(q.d); }
