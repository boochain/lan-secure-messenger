/* v0.6.1 UX (GTK4 / libadwaita): my QR / add by link (the Ctrl+N box also takes lsm:// links), one-click resend with
 * the reason, file size + ETA confirm before sending, Ctrl+K quick switcher. ↑-edit and Ctrl+Shift+F already exist. */
static uint8_t ux_qrm[177*177]; static int ux_qrn;
static void ux_qr_draw(GtkDrawingArea *a, cairo_t *cr, int W, int H, gpointer p){ int s=W<H?W:H,x,y,m=ux_qrn+8; double sc,ox,oy; (void)a;(void)p; if(!ux_qrn) return;
    sc=(double)s/m; ox=(W-s)/2.0; oy=(H-s)/2.0; cairo_set_source_rgb(cr,1,1,1); cairo_rectangle(cr,ox,oy,s,s); cairo_fill(cr); cairo_set_source_rgb(cr,0,0,0);
    for(y=0;y<ux_qrn;y++) for(x=0;x<ux_qrn;x++) if(ux_qrm[y*ux_qrn+x]) cairo_rectangle(cr,ox+(x+4)*sc,oy+(y+4)*sc,sc+0.3,sc+0.3); cairo_fill(cr); }
static char ux_uri[400];
static void ux_qr_resp(AdwAlertDialog *d, const char *r, gpointer p){ (void)d;(void)p; if(!strcmp(r,"copy")){ gdk_clipboard_set_text(gtk_widget_get_clipboard(win),ux_uri); toast("链接已复制"); } }
static void ux_my_qr(void){ AdwDialog *d; GtkWidget *box,*da,*l;
    if(lsm_my_uri(C,ux_uri,sizeof ux_uri)){ info("无法生成二维码","未找到局域网地址。"); return; }
    ux_qrn=lsm_qr(ux_uri,ux_qrm);
    box=gtk_box_new(GTK_ORIENTATION_VERTICAL,8); da=gtk_drawing_area_new(); gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(da),240); gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(da),240);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da),ux_qr_draw,NULL,NULL); gtk_widget_set_halign(da,GTK_ALIGN_CENTER); gtk_box_append(GTK_BOX(box),da);
    if(getenv("LSM_UI_TEST")) g_ptr_array_add(icon_widgets,g_object_ref(da)); /* the code is a picture, not text */
    l=gtk_label_new(ux_uri); gtk_label_set_selectable(GTK_LABEL(l),TRUE); gtk_label_set_wrap(GTK_LABEL(l),TRUE); gtk_label_set_wrap_mode(GTK_LABEL(l),PANGO_WRAP_CHAR); gtk_label_set_max_width_chars(GTK_LABEL(l),36); cls(l,"caption"); gtk_box_append(GTK_BOX(box),l);
    d=adw_alert_dialog_new("我的二维码","让对方用手机版扫描，或把链接发给对方，在添加联系人 (Ctrl+N) 里粘贴。指纹一致会自动设为已信任。");
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(d),"close","关闭","copy","复制链接",NULL); adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(d),"copy",ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(d),"close"); adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(d),box);
    g_signal_connect(d,"response",G_CALLBACK(ux_qr_resp),NULL); adw_dialog_present(d,win); }
/* add_contact_go(): lsm:// links go through the fingerprint-checked path */
static int ux_try_uri(const char *s){ char err[256]=""; if(strncmp(s,"lsm://",6)) return 0;
    if(lsm_add_uri(C,s,err)) info("无法添加",err); else { gtk_editable_set_text(GTK_EDITABLE(add_entry),""); gtk_popover_popdown(GTK_POPOVER(add_popover)); toast("正在连接…指纹一致后自动设为已信任"); } list_dirty=1; return 1; }

/* resend */
static void ux_menu(GtkWidget *box, const char *id){ lsm_msg *m; int n,i,can=0; lsm_lock(C); n=lsm_messages(C,&m); for(i=0;i<n;i++) if(!strcmp(m[i].id,id)){ can=lsm_can_resend(&m[i]); break; } lsm_unlock(C);
    if(can) pop_btn(box,"重新发送(_R)","resend",id); }
static int ux_action(const char *what, const char *id){ char err[256]="",nid[40]; if(strcmp(what,"resend")) return 0; if(lsm_resend(C,id,nid,err)) info("无法重新发送",err); chat_dirty=list_dirty=1; return 1; }
static void ux_resend_btn(GtkButton *b, gpointer id){ (void)b; ux_action("resend",(const char*)id); }
static void ux_fail_bar(GtkWidget *bub, const lsm_msg *m){ GtkWidget *h,*l; /* lock held */
    if(!m->out || (m->status!=ST_FAILED && m->status!=ST_REJECTED)) return;
    h=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6); l=label(lsm_status_text(C,m),"fail-reason"); gtk_label_set_wrap(GTK_LABEL(l),TRUE); gtk_widget_set_hexpand(l,TRUE); gtk_box_append(GTK_BOX(h),l);
    if(lsm_can_resend(m)){ GtkWidget *b=gtk_button_new_with_mnemonic("重新发送(_R)"); cls(b,"pill"); g_signal_connect_data(b,"clicked",G_CALLBACK(ux_resend_btn),g_strdup(m->id),(GClosureNotify)g_free,0); gtk_box_append(GTK_BOX(h),b); }
    gtk_box_append(GTK_BOX(bub),h); }

/* file size + ETA before sending */
static void ux_send_now(gpointer p){ char err[256]=""; if(lsm_send_file(C,cur_conv,(const char*)p,err)) info("无法发送文件",err); chat_dirty=1; }
static void ux_confirm_file(const char *path){ char pv[700]; lsm_file_preview(C,cur_conv,path,pv,sizeof pv); confirm("发送文件？",pv,"发送",0,ux_send_now,g_strdup(path),g_free); }

/* Ctrl+K quick switcher */
static GtkWidget *ux_qs_e,*ux_qs_lb; static AdwDialog *ux_qs_d;
static void ux_qs_fill(void){ const char *t=gtk_editable_get_text(GTK_EDITABLE(ux_qs_e)); lsm_contact *k; lsm_group *g; int nk,ng,i,j,cnt=0; GtkWidget *c;
    struct { int sc; char lab[320]; char cv[64]; } *r; while((c=gtk_widget_get_first_child(ux_qs_lb))) gtk_list_box_remove(GTK_LIST_BOX(ux_qs_lb),c);
    lsm_lock(C); nk=lsm_contacts(C,&k); ng=lsm_groups(C,&g); r=g_new0(__typeof__(*r),nk+ng+1);
    for(i=0;i<nk;i++){ int s; if(k[i].trust==TRUST_BLOCKED) continue; s=lsm_fuzzy(lsm_display_name(&k[i]),t); if(!s) s=lsm_fuzzy(k[i].fp,t)/2; if(!s) continue; r[cnt].sc=s; snprintf(r[cnt].lab,320,"%s",lsm_display_name(&k[i])); snprintf(r[cnt].cv,64,"dm:%s",k[i].fp); cnt++; }
    for(i=0;i<ng;i++){ char u[37]; int s=lsm_fuzzy(g[i].name,t); if(!s) continue; lsm_uuid_str(g[i].id,u); r[cnt].sc=s; snprintf(r[cnt].lab,320,"群组「%s」",g[i].name); snprintf(r[cnt].cv,64,"group:%s",u); cnt++; }
    lsm_unlock(C);
    for(i=0;i<cnt;i++) for(j=i+1;j<cnt;j++) if(r[j].sc>r[i].sc){ __typeof__(*r) x=r[i]; r[i]=r[j]; r[j]=x; }
    for(i=0;i<cnt && i<30;i++){ GtkWidget *row=adw_action_row_new(); adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row),FALSE); adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row),r[i].lab);
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row),TRUE); g_object_set_data_full(G_OBJECT(row),"conv",g_strdup(r[i].cv),g_free); gtk_list_box_append(GTK_LIST_BOX(ux_qs_lb),row); }
    g_free(r); { GtkListBoxRow *f=gtk_list_box_get_row_at_index(GTK_LIST_BOX(ux_qs_lb),0); if(f) gtk_list_box_select_row(GTK_LIST_BOX(ux_qs_lb),f); } }
static void ux_qs_changed(GtkEditable *e, gpointer p){ (void)e;(void)p; ux_qs_fill(); }
static void ux_qs_pick(GtkListBoxRow *row){ char cv[64]; if(!row) row=gtk_list_box_get_selected_row(GTK_LIST_BOX(ux_qs_lb)); if(!row) return;
    snprintf(cv,sizeof cv,"%s",(const char*)g_object_get_data(G_OBJECT(row),"conv")); adw_dialog_close(ux_qs_d); select_conv(cv); }
static void ux_qs_act(GtkEntry *e, gpointer p){ (void)e;(void)p; ux_qs_pick(NULL); }
static void ux_qs_row(GtkListBox *lb, GtkListBoxRow *row, gpointer p){ (void)lb;(void)p; ux_qs_pick(row); }
static gboolean ux_qs_key(GtkEventControllerKey *k, guint kv, guint kc, GdkModifierType st, gpointer p){ GtkListBoxRow *r; int i; (void)k;(void)kc;(void)st;(void)p;
    if(kv!=GDK_KEY_Down && kv!=GDK_KEY_Up) return FALSE; r=gtk_list_box_get_selected_row(GTK_LIST_BOX(ux_qs_lb)); i=(r?gtk_list_box_row_get_index(r):-1)+(kv==GDK_KEY_Down?1:-1);
    r=i>=0?gtk_list_box_get_row_at_index(GTK_LIST_BOX(ux_qs_lb),i):NULL; if(r) gtk_list_box_select_row(GTK_LIST_BOX(ux_qs_lb),r); return TRUE; }
static void ux_quick_switch(void){ GtkWidget *tv=adw_toolbar_view_new(),*box=gtk_box_new(GTK_ORIENTATION_VERTICAL,8),*sw=gtk_scrolled_window_new(); GtkEventController *kc=gtk_event_controller_key_new();
    ux_qs_d=adw_dialog_new(); adw_dialog_set_title(ux_qs_d,"切换会话"); adw_dialog_set_content_width(ux_qs_d,380); adw_dialog_set_content_height(ux_qs_d,420);
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv),adw_header_bar_new());
    ux_qs_e=gtk_search_entry_new(); gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(ux_qs_e),"输入名称的任意几个字"); gtk_widget_set_margin_start(box,12); gtk_widget_set_margin_end(box,12); gtk_widget_set_margin_bottom(box,12);
    ux_qs_lb=gtk_list_box_new(); cls(ux_qs_lb,"boxed-list"); gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw),ux_qs_lb); gtk_widget_set_vexpand(sw,TRUE);
    gtk_box_append(GTK_BOX(box),ux_qs_e); gtk_box_append(GTK_BOX(box),sw); adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tv),box); adw_dialog_set_child(ux_qs_d,tv);
    g_signal_connect(ux_qs_e,"search-changed",G_CALLBACK(ux_qs_changed),NULL); g_signal_connect(ux_qs_e,"activate",G_CALLBACK(ux_qs_act),NULL); g_signal_connect(ux_qs_lb,"row-activated",G_CALLBACK(ux_qs_row),NULL);
    g_signal_connect(kc,"key-pressed",G_CALLBACK(ux_qs_key),NULL); gtk_widget_add_controller(ux_qs_e,kc);
    ux_qs_fill(); adw_dialog_set_focus(ux_qs_d,ux_qs_e); adw_dialog_present(ux_qs_d,win); }
