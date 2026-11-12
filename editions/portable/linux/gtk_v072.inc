/* v0.7.2: dedicated recovery-key page (LSMF_recovery_key). Replaces the short-lived message dialog. */
static int v072_rk_force, v072_rk_acked;
static GtkWidget *v072_rk_ok, *v072_rk_check;
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
static void v072_rk_copy(GtkButton *b, gpointer p){ (void)b; gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),(const char*)p,-1); }
static void v072_rk_export(GtkButton *b, gpointer p){
    GtkWidget *fc=gtk_file_chooser_dialog_new("导出恢复密钥",GTK_WINDOW(win),GTK_FILE_CHOOSER_ACTION_SAVE,"取消",GTK_RESPONSE_CANCEL,"保存",GTK_RESPONSE_ACCEPT,NULL);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(fc),"recovery-key.txt"); (void)b;
    if(gtk_dialog_run(GTK_DIALOG(fc))==GTK_RESPONSE_ACCEPT){ char *f=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc)); if(f){ FILE *fp=fopen(f,"wb"); if(fp){ fputs((const char*)p,fp); fputc('\n',fp); fclose(fp); } g_free(f); } }
    gtk_widget_destroy(fc);
}
static void v072_rk_toggle(GtkToggleButton *t, gpointer p){ (void)p; v072_rk_acked=gtk_toggle_button_get_active(t); if(v072_rk_ok) gtk_widget_set_sensitive(v072_rk_ok, !v072_rk_force || v072_rk_acked); }
static gboolean v072_rk_delete(GtkWidget *w, GdkEvent *e, gpointer p){ (void)w;(void)e;(void)p; if(v072_rk_force && !v072_rk_acked && !getenv("LSM_UI_TEST")) return TRUE; return FALSE; }
static void v072_show_recovery(int force, const char *preset){
    /* LSMF_recovery_key */
    char rk[80]="", hold[80]; GtkWidget *d,*box,*l,*k,*row,*copy,*exp,*chk; int resp;
    if(preset && preset[0]) snprintf(rk,sizeof rk,"%s",preset);
    else if(v072_load_key(rk,sizeof rk)){ msgbox(GTK_MESSAGE_INFO,"还没有恢复密钥（请先开启账号模式）"); return; }
    snprintf(hold,sizeof hold,"%s",rk);
    v072_rk_force=force; v072_rk_acked=0;
    d=gtk_dialog_new_with_buttons("恢复密钥",GTK_WINDOW(win),GTK_DIALOG_MODAL,NULL);
    gtk_window_set_default_size(GTK_WINDOW(d),520,420); gtk_widget_set_name(d,"LSMF_recovery_key");
    box=gtk_dialog_get_content_area(GTK_DIALOG(d)); gtk_container_set_border_width(GTK_CONTAINER(box),16); gtk_box_set_spacing(GTK_BOX(box),10);
    l=gtk_label_new("这是你的账号恢复密钥。请把它抄到离线的安全地方。任何拿到这串密钥的人都能恢复你的身份。服务器看不到明文。");
    gtk_label_set_line_wrap(GTK_LABEL(l),TRUE); gtk_label_set_xalign(GTK_LABEL(l),0); gtk_box_pack_start(GTK_BOX(box),l,FALSE,FALSE,0);
    k=gtk_label_new(rk); gtk_label_set_selectable(GTK_LABEL(k),TRUE); gtk_label_set_line_wrap(GTK_LABEL(k),TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(k),PANGO_WRAP_CHAR);
    { PangoAttrList *a=pango_attr_list_new(); pango_attr_list_insert(a,pango_attr_family_new("monospace")); pango_attr_list_insert(a,pango_attr_scale_new(1.35)); gtk_label_set_attributes(GTK_LABEL(k),a); pango_attr_list_unref(a); }
    gtk_widget_set_name(k,"LSMF_recovery_key"); gtk_label_set_xalign(GTK_LABEL(k),0); gtk_box_pack_start(GTK_BOX(box),k,FALSE,FALSE,4);
    row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    copy=gtk_button_new_with_label("复制"); exp=gtk_button_new_with_label("导出到文件…");
    g_signal_connect(copy,"clicked",G_CALLBACK(v072_rk_copy),g_strdup(hold));
    g_signal_connect(exp,"clicked",G_CALLBACK(v072_rk_export),g_strdup(hold));
    gtk_box_pack_start(GTK_BOX(row),copy,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(row),exp,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,0);
    chk=gtk_check_button_new_with_label("我已抄下并妥善保管"); v072_rk_check=chk;
    g_signal_connect(chk,"toggled",G_CALLBACK(v072_rk_toggle),NULL);
    gtk_box_pack_start(GTK_BOX(box),chk,FALSE,FALSE,0);
    gtk_dialog_add_button(GTK_DIALOG(d), force?"我已抄好":"完成", GTK_RESPONSE_OK);
    v072_rk_ok=gtk_dialog_get_widget_for_response(GTK_DIALOG(d),GTK_RESPONSE_OK);
    if(v072_rk_ok) gtk_widget_set_sensitive(v072_rk_ok, !force);
    g_signal_connect(d,"delete-event",G_CALLBACK(v072_rk_delete),NULL);
    gtk_widget_show_all(d);
    do { resp=gtk_dialog_run(GTK_DIALOG(d)); if(force && !v072_rk_acked && resp!=GTK_RESPONSE_OK && !getenv("LSM_UI_TEST")) resp=GTK_RESPONSE_NONE; } while(force && !v072_rk_acked && !getenv("LSM_UI_TEST"));
    gtk_widget_destroy(d); v072_rk_ok=NULL; v072_rk_force=0;
}
static void v072_shot_recovery(void){ if(!getenv("LSM_UI_TEST")) return; v072_show_recovery(0,NULL); }
static GtkWidget *v072_msub(GtkWidget *menu, const char *label){
    GtkWidget *sub=gtk_menu_new(), *top=gtk_menu_item_new_with_label(label);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(top),sub);
    gtk_widget_set_direction(menu,GTK_TEXT_DIR_RTL);
    gtk_widget_set_direction(sub,GTK_TEXT_DIR_RTL);
    gtk_widget_set_direction(top,GTK_TEXT_DIR_LTR);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu),top);
    return sub;
}
