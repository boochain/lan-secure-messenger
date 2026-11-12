/* v0.7.2: dedicated recovery-key window (LSMF_recovery_key) + title-bar icon helper. */
static GtkWidget *v072_rk_win, *v072_rk_ok, *v072_rk_check;
static int v072_rk_force, v072_rk_acked;
static char v072_rk_text[80];
static gboolean v072_rk_unselect_idle(gpointer w){
    if(GTK_IS_LABEL(w)) gtk_label_select_region(GTK_LABEL(w),0,0);
    return G_SOURCE_REMOVE;
}
static void v072_rk_unselect_on_map(GtkWidget *w, gpointer unused){ (void)unused;
    gtk_label_select_region(GTK_LABEL(w),0,0);
    g_idle_add(v072_rk_unselect_idle,w);
}
static int v072_load_key(char *rk, size_t n){
    char err[256]="", tmp[80]="";
    if(rk) rk[0]=0;
    if(!lsm_reveal_recovery_key(C,rk,n) && rk[0]) return 0;
    if(getenv("LSM_UI_TEST")){
        if(!lsm_set_account_mode(C,"account",tmp,sizeof tmp,err) && tmp[0]){ snprintf(rk,n,"%s",tmp); return 0; }
        if(!lsm_reveal_recovery_key(C,rk,n) && rk[0]) return 0;
        snprintf(rk,n,"RK1-TEST-XXXX-XXXX-XXXX"); return 0;
    }
    return -1;
}
static cairo_status_t v072_png_acc(void *p, const unsigned char *d, unsigned int n){ g_byte_array_append((GByteArray*)p,d,n); return CAIRO_STATUS_SUCCESS; }
static GdkPixbuf *v072_pixbuf_from_surface(cairo_surface_t *s){
    GByteArray *a=g_byte_array_new(); GdkPixbufLoader *ld; GdkPixbuf *pb=NULL;
    if(cairo_surface_write_to_png_stream(s,v072_png_acc,a)==CAIRO_STATUS_SUCCESS && a->len){
        ld=gdk_pixbuf_loader_new(); if(gdk_pixbuf_loader_write(ld,a->data,a->len,NULL) && gdk_pixbuf_loader_close(ld,NULL))
            pb=gdk_pixbuf_loader_get_pixbuf(ld); if(pb) g_object_ref(pb); g_object_unref(ld); }
    g_byte_array_unref(a); return pb;
}
static GdkPixbuf *v072_draw_bubble(int px){
    cairo_surface_t *s=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,px,px); cairo_t *cr=cairo_create(s);
    double u=px/32.0, x=3*u, y=3*u, w=26*u, h=20*u, r=5.2*u; GdkPixbuf *pb;
    cairo_set_source_rgb(cr,63/255.0,95/255.0,144/255.0);
    cairo_move_to(cr,x+r,y); cairo_line_to(cr,x+w-r,y); cairo_arc(cr,x+w-r,y+r,r,-G_PI/2,0);
    cairo_line_to(cr,x+w,y+h-r); cairo_arc(cr,x+w-r,y+h-r,r,0,G_PI/2);
    cairo_line_to(cr,x+r,y+h); cairo_arc(cr,x+r,y+h-r,r,G_PI/2,G_PI);
    cairo_line_to(cr,x,y+r); cairo_arc(cr,x+r,y+r,r,G_PI,3*G_PI/2); cairo_close_path(cr);
    cairo_move_to(cr,8*u,y+h-0.5*u); cairo_line_to(cr,6*u,29*u); cairo_line_to(cr,15*u,y+h-0.5*u);
    cairo_fill(cr);
    cairo_set_source_rgb(cr,249/255.0,249/255.0,255/255.0);
    cairo_arc(cr,px/2.0,y+h*0.48,3.2*u,0,2*G_PI); cairo_fill(cr);
    cairo_destroy(cr); pb=v072_pixbuf_from_surface(s); cairo_surface_destroy(s); return pb;
}
static GdkPixbuf *v072_load_icon_pixbuf(int px){
    const char *ad=getenv("APPDIR"); char exe[1024], path[1024]; ssize_t n; char *sl; const char *rel[8]; int i,nrel=0;
    if(ad && ad[0]){ snprintf(path,sizeof path,"%s/lan-messenger.png",ad); if(g_file_test(path,G_FILE_TEST_EXISTS)) return gdk_pixbuf_new_from_file_at_size(path,px,px,NULL); }
    n=readlink("/proc/self/exe",exe,sizeof exe-1);
    if(n>0){ exe[n]=0; sl=strrchr(exe,'/'); if(sl){ *sl=0;
        rel[nrel++]="lan-messenger.png"; rel[nrel++]="../share/icons/hicolor/256x256/apps/lan-messenger.png";
        rel[nrel++]="../../linux/data/lan-messenger.png"; rel[nrel++]="../../linux/data/hicolor/256x256/apps/lan-messenger.png";
        rel[nrel++]="../linux/data/lan-messenger.png";
        for(i=0;i<nrel;i++){ snprintf(path,sizeof path,"%s/%s",exe,rel[i]); if(g_file_test(path,G_FILE_TEST_EXISTS)) return gdk_pixbuf_new_from_file_at_size(path,px,px,NULL); } } }
    return v072_draw_bubble(px);
}
static GtkWidget *v072_app_icon(int px){
    GdkPixbuf *pb; GtkWidget *im; if(px<24) px=28;
    pb=v072_load_icon_pixbuf(px); if(!pb) pb=v072_draw_bubble(px);
    im=gtk_image_new_from_pixbuf(pb); if(pb) g_object_unref(pb);
    gtk_image_set_pixel_size(GTK_IMAGE(im),px);
    gtk_widget_set_size_request(im,px,px);
    gtk_widget_set_halign(im,GTK_ALIGN_CENTER); gtk_widget_set_valign(im,GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(im,FALSE); gtk_widget_add_css_class(im,"lsm-title-icon");
    return im;
}
static void v072_append_sub(GMenu *parent, const char *label, GMenu *child){
    GMenuItem *it=g_menu_item_new(label,NULL);
    g_menu_item_set_submenu(it,G_MENU_MODEL(child));
    g_menu_append_item(parent,it);
    g_object_unref(it);
}
static void v072_rk_copy(GtkButton *b, gpointer p){ (void)b;(void)p;
    gdk_clipboard_set_text(gtk_widget_get_clipboard(v072_rk_win?v072_rk_win:win), v072_rk_text); }
static void v072_rk_saved(GObject *src, GAsyncResult *res, gpointer p){
    GFile *f=gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src),res,NULL); char *path; FILE *fp; (void)p;
    if(!f) return; path=g_file_get_path(f); if(path && (fp=fopen(path,"wb"))){ fputs(v072_rk_text,fp); fputc('\n',fp); fclose(fp); }
    g_free(path); if(f) g_object_unref(f);
}
static void v072_rk_export(GtkButton *b, gpointer p){ (void)b;(void)p;
    GtkFileDialog *d=gtk_file_dialog_new(); gtk_file_dialog_set_title(d,"导出恢复密钥");
    gtk_file_dialog_set_initial_name(d,"recovery-key.txt");
    gtk_file_dialog_save(d,GTK_WINDOW(v072_rk_win?v072_rk_win:win),NULL,v072_rk_saved,NULL); g_object_unref(d); }
static void v072_rk_close(void){ if(v072_rk_win){ gtk_window_destroy(GTK_WINDOW(v072_rk_win)); v072_rk_win=NULL; } v072_rk_force=0; }
static void v072_rk_done(GtkButton *b, gpointer p){ (void)b;(void)p; if(v072_rk_force && !v072_rk_acked && !getenv("LSM_UI_TEST")) return; v072_rk_close(); }
static gboolean v072_rk_close_req(GtkWindow *w, gpointer p){ (void)w;(void)p; if(v072_rk_force && !v072_rk_acked && !getenv("LSM_UI_TEST")) return TRUE; v072_rk_win=NULL; v072_rk_force=0; return FALSE; }
static gboolean v072_rk_key(GtkEventControllerKey *c, guint keyval, guint kc, GdkModifierType st, gpointer p){
    (void)c;(void)kc;(void)st;(void)p;
    if(keyval==GDK_KEY_Escape){ if(v072_rk_force && !v072_rk_acked && !getenv("LSM_UI_TEST")) return TRUE; v072_rk_close(); return TRUE; }
    return FALSE;
}
static void v072_rk_toggle(GtkCheckButton *t, gpointer p){ (void)p; v072_rk_acked=gtk_check_button_get_active(t); if(v072_rk_ok) gtk_widget_set_sensitive(v072_rk_ok, !v072_rk_force || v072_rk_acked); }
static void v072_show_recovery(int force, const char *preset){
    /* LSMF_recovery_key: a dedicated AdwWindow with a header bar and an in-content heading,
       so the title is visible (plain GtkWindow had no CSD header; the title was clipped). */
    char rk[80]=""; GtkWidget *tv,*hb,*box,*h,*l,*k,*row,*copy,*exp,*chk,*ok; GtkEventController *kc;
    if(preset && preset[0]) snprintf(rk,sizeof rk,"%s",preset);
    else if(v072_load_key(rk,sizeof rk)){ toast("还没有恢复密钥（请先开启账号模式）"); return; }
    snprintf(v072_rk_text,sizeof v072_rk_text,"%s",rk);
    v072_rk_force=force; v072_rk_acked=0;
    if(v072_rk_win) gtk_window_destroy(GTK_WINDOW(v072_rk_win));
    v072_rk_win=adw_window_new(); gtk_window_set_title(GTK_WINDOW(v072_rk_win),"恢复密钥");
    gtk_window_set_transient_for(GTK_WINDOW(v072_rk_win),GTK_WINDOW(win));
    gtk_window_set_modal(GTK_WINDOW(v072_rk_win),TRUE); gtk_window_set_default_size(GTK_WINDOW(v072_rk_win),560,480);
    gtk_widget_set_name(v072_rk_win,"LSMF_recovery_key");
    tv=adw_toolbar_view_new(); hb=adw_header_bar_new(); adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv),hb);
    box=gtk_box_new(GTK_ORIENTATION_VERTICAL,12); gtk_widget_set_margin_top(box,20); gtk_widget_set_margin_bottom(box,20);
    gtk_widget_set_margin_start(box,24); gtk_widget_set_margin_end(box,24);
    h=gtk_label_new("恢复密钥"); gtk_widget_add_css_class(h,"title-1"); gtk_label_set_xalign(GTK_LABEL(h),0); gtk_box_append(GTK_BOX(box),h);
    l=gtk_label_new("这是你的账号恢复密钥。请把它抄到离线的安全地方。任何拿到这串密钥的人都能恢复你的身份。服务器看不到明文。");
    gtk_label_set_wrap(GTK_LABEL(l),TRUE); gtk_label_set_xalign(GTK_LABEL(l),0); gtk_box_append(GTK_BOX(box),l);
    k=gtk_label_new(rk); gtk_label_set_selectable(GTK_LABEL(k),TRUE); gtk_label_set_wrap(GTK_LABEL(k),TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(k),PANGO_WRAP_CHAR);
    { PangoAttrList *a=pango_attr_list_new(); pango_attr_list_insert(a,pango_attr_family_new("monospace")); pango_attr_list_insert(a,pango_attr_scale_new(1.35)); gtk_label_set_attributes(GTK_LABEL(k),a); pango_attr_list_unref(a); }
    gtk_widget_set_name(k,"LSMF_recovery_key"); gtk_label_set_xalign(GTK_LABEL(k),0);
    /* Wrap in .bubble.mine so the key uses the approved palette (GTK's selectable
       selection colour #BED6F4 was a stray dE 5.29 from 'mine' #D7E6FA). */
    { GtkWidget *card=gtk_box_new(GTK_ORIENTATION_VERTICAL,0); gtk_widget_add_css_class(card,"bubble"); gtk_widget_add_css_class(card,"mine");
      gtk_widget_set_margin_top(k,6); gtk_widget_set_margin_bottom(k,6); gtk_widget_set_margin_start(k,8); gtk_widget_set_margin_end(k,8);
      gtk_box_append(GTK_BOX(card),k); gtk_box_append(GTK_BOX(box),card);
      /* Do not leave the key selected: GTK's selection fill (#A6C8F3 / #2A578E)
         is off-palette and the light pairing is 0.4 short of the APCA gate. */
      gtk_widget_set_can_focus(k,FALSE);
      gtk_label_select_region(GTK_LABEL(k),0,0);
      g_signal_connect(k,"map",G_CALLBACK(v072_rk_unselect_on_map),NULL); }
    row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    copy=gtk_button_new_with_label("复制"); exp=gtk_button_new_with_label("导出到文件…");
    g_signal_connect(copy,"clicked",G_CALLBACK(v072_rk_copy),NULL);
    g_signal_connect(exp,"clicked",G_CALLBACK(v072_rk_export),NULL);
    gtk_box_append(GTK_BOX(row),copy); gtk_box_append(GTK_BOX(row),exp); gtk_box_append(GTK_BOX(box),row);
    chk=gtk_check_button_new_with_label("我已抄下并妥善保管"); v072_rk_check=chk;
    g_signal_connect(chk,"toggled",G_CALLBACK(v072_rk_toggle),NULL); gtk_box_append(GTK_BOX(box),chk);
    ok=gtk_button_new_with_label(force?"我已抄好":"完成"); v072_rk_ok=ok;
    gtk_widget_add_css_class(ok,"suggested-action"); gtk_widget_set_sensitive(ok,!force);
    g_signal_connect(ok,"clicked",G_CALLBACK(v072_rk_done),NULL); gtk_box_append(GTK_BOX(box),ok);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tv),box); adw_window_set_content(ADW_WINDOW(v072_rk_win),tv);
    g_signal_connect(v072_rk_win,"close-request",G_CALLBACK(v072_rk_close_req),NULL);
    kc=gtk_event_controller_key_new(); g_signal_connect(kc,"key-pressed",G_CALLBACK(v072_rk_key),NULL);
    gtk_widget_add_controller(v072_rk_win,kc);
    gtk_window_present(GTK_WINDOW(v072_rk_win));
    gtk_label_select_region(GTK_LABEL(k),0,0);
    gtk_widget_grab_focus(copy);
    g_idle_add(v072_rk_unselect_idle,k);
}
static void v072_shot_recovery(void){ if(!getenv("LSM_UI_TEST")) return; v072_show_recovery(0,NULL); }
