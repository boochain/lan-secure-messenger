/* v0.7.2: dedicated recovery-key window (LSMF_recovery_key) + title-bar icon helper. */
static GtkWidget *v072_rk_win, *v072_rk_ok, *v072_rk_check;
static int v072_rk_force, v072_rk_acked;
static char v072_rk_text[80];
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
static GtkWidget *v072_app_icon(int px){
    const char *cands[6]; int i=0; char exe[1024], dir[1024]; ssize_t n; char *sl;
    const char *ad=getenv("APPDIR");
    if(ad && ad[0]){ static char a[1024]; snprintf(a,sizeof a,"%s/lan-messenger.png",ad); cands[i++]=a; }
    n=readlink("/proc/self/exe",exe,sizeof exe-1);
    if(n>0){ exe[n]=0; sl=strrchr(exe,'/'); if(sl){ *sl=0; static char b[1024],c[1024];
        snprintf(b,sizeof b,"%s/lan-messenger.png",exe); cands[i++]=b;
        snprintf(c,sizeof c,"%s/../share/icons/hicolor/256x256/apps/lan-messenger.png",exe); cands[i++]=c; } }
    cands[i]=NULL;
    for(i=0;cands[i];i++) if(g_file_test(cands[i],G_FILE_TEST_EXISTS)){
        GtkWidget *im=gtk_image_new_from_file(cands[i]); gtk_image_set_pixel_size(GTK_IMAGE(im),px); return im; }
    { GtkWidget *im=gtk_image_new_from_icon_name("lan-messenger"); gtk_image_set_pixel_size(GTK_IMAGE(im),px); return im; }
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
static void v072_rk_done(GtkButton *b, gpointer p){ (void)b;(void)p; if(v072_rk_force && !v072_rk_acked) return; v072_rk_close(); }
static gboolean v072_rk_close_req(GtkWindow *w, gpointer p){ (void)w;(void)p; if(v072_rk_force && !v072_rk_acked) return TRUE; v072_rk_win=NULL; v072_rk_force=0; return FALSE; }
static gboolean v072_rk_key(GtkEventControllerKey *c, guint keyval, guint kc, GdkModifierType st, gpointer p){
    (void)c;(void)kc;(void)st;(void)p;
    if(keyval==GDK_KEY_Escape){ if(v072_rk_force && !v072_rk_acked) return TRUE; v072_rk_close(); return TRUE; }
    return FALSE;
}
static void v072_rk_toggle(GtkCheckButton *t, gpointer p){ (void)p; v072_rk_acked=gtk_check_button_get_active(t); if(v072_rk_ok) gtk_widget_set_sensitive(v072_rk_ok, !v072_rk_force || v072_rk_acked); }
static void v072_show_recovery(int force, const char *preset){
    /* LSMF_recovery_key */
    char rk[80]=""; GtkWidget *box,*l,*k,*row,*copy,*exp,*chk,*ok;
    if(preset && preset[0]) snprintf(rk,sizeof rk,"%s",preset);
    else if(v072_load_key(rk,sizeof rk)){ toast("还没有恢复密钥（请先开启账号模式）"); return; }
    snprintf(v072_rk_text,sizeof v072_rk_text,"%s",rk);
    v072_rk_force=force; v072_rk_acked=0;
    if(v072_rk_win) gtk_window_destroy(GTK_WINDOW(v072_rk_win));
    v072_rk_win=gtk_window_new(); gtk_window_set_title(GTK_WINDOW(v072_rk_win),"恢复密钥");
    gtk_window_set_transient_for(GTK_WINDOW(v072_rk_win),GTK_WINDOW(win));
    gtk_window_set_modal(GTK_WINDOW(v072_rk_win),TRUE); gtk_window_set_default_size(GTK_WINDOW(v072_rk_win),520,420);
    gtk_widget_set_name(v072_rk_win,"LSMF_recovery_key");
    box=gtk_box_new(GTK_ORIENTATION_VERTICAL,10); gtk_widget_set_margin_top(box,16); gtk_widget_set_margin_bottom(box,16);
    gtk_widget_set_margin_start(box,20); gtk_widget_set_margin_end(box,20);
    l=gtk_label_new("这是你的账号恢复密钥。请把它抄到离线的安全地方。任何拿到这串密钥的人都能恢复你的身份。服务器看不到明文。");
    gtk_label_set_wrap(GTK_LABEL(l),TRUE); gtk_label_set_xalign(GTK_LABEL(l),0); gtk_box_append(GTK_BOX(box),l);
    k=gtk_label_new(rk); gtk_label_set_selectable(GTK_LABEL(k),TRUE); gtk_label_set_wrap(GTK_LABEL(k),TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(k),PANGO_WRAP_CHAR);
    { PangoAttrList *a=pango_attr_list_new(); pango_attr_list_insert(a,pango_attr_family_new("monospace")); pango_attr_list_insert(a,pango_attr_scale_new(1.35)); gtk_label_set_attributes(GTK_LABEL(k),a); pango_attr_list_unref(a); }
    gtk_widget_set_name(k,"LSMF_recovery_key"); gtk_label_set_xalign(GTK_LABEL(k),0); gtk_box_append(GTK_BOX(box),k);
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
    gtk_window_set_child(GTK_WINDOW(v072_rk_win),box);
    g_signal_connect(v072_rk_win,"close-request",G_CALLBACK(v072_rk_close_req),NULL);
    gtk_window_present(GTK_WINDOW(v072_rk_win));
}
static void v072_shot_recovery(void){ if(!getenv("LSM_UI_TEST")) return; v072_show_recovery(0,NULL); }
