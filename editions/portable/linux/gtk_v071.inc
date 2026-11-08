/* v0.7.1: first-run account/device choice + QR URI apply. Hooks: LSMF_account_mode LSMF_qr_scan LSMF_qr_server LSMF_qr_pair LSMF_qr_profile */
static void v071_first_run(void){
    GtkWidget *d,*box,*l,*b1,*b2,*hint; char err[256]="";
    if(lsm_first_run_done(C)) return;
    if(getenv("LSM_UI_TEST") && !getenv("LSM_SHOT_FIRST_RUN")){ lsm_complete_first_run(C,NULL,"device",NULL,err); return; }
    d=gtk_dialog_new_with_buttons("欢迎使用局域网安全通讯",GTK_WINDOW(win),GTK_DIALOG_MODAL,"开始使用",GTK_RESPONSE_OK,NULL);
    box=gtk_dialog_get_content_area(GTK_DIALOG(d)); gtk_container_set_border_width(GTK_CONTAINER(box),16); gtk_box_set_spacing(GTK_BOX(box),10);
    l=gtk_label_new("请选择身份方式（之后可在设置里更改）。账号模式会生成恢复密钥，不使用服务器密码。");
    gtk_label_set_line_wrap(GTK_LABEL(l),TRUE); gtk_box_pack_start(GTK_BOX(box),l,FALSE,FALSE,0);
    b1=gtk_radio_button_new_with_label(NULL,"设备模式 — 仅本机，局域网直连，不把身份放到服务器");
    b2=gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(b1),"账号模式 — 身份加密备份在中转上，换机可用恢复密钥或配对二维码恢复");
    gtk_widget_set_name(b1,"LSMF_account_mode"); gtk_widget_set_name(b2,"LSMF_account_mode");
    gtk_box_pack_start(GTK_BOX(box),b1,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),b2,FALSE,FALSE,0);
    hint=gtk_label_new("添加联系人、连接服务器、配对新设备都可以通过扫描或粘贴 lsm:// 链接完成。");
    gtk_label_set_line_wrap(GTK_LABEL(hint),TRUE); gtk_widget_set_name(hint,"LSMF_qr_scan"); gtk_box_pack_start(GTK_BOX(box),hint,FALSE,FALSE,0);
    gtk_widget_show_all(d); gtk_dialog_run(GTK_DIALOG(d));
    { int acc=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b2));
      lsm_complete_first_run(C,NULL,acc?"account":"device",NULL,err);
      if(acc){ char rk[80]=""; if(!lsm_reveal_recovery_key(C,rk,sizeof rk) && rk[0]){
          GtkWidget *n=gtk_message_dialog_new(GTK_WINDOW(win),GTK_DIALOG_MODAL,GTK_MESSAGE_INFO,GTK_BUTTONS_OK,"请抄下恢复密钥并妥善保管：\n%s",rk);
          gtk_dialog_run(GTK_DIALOG(n)); gtk_widget_destroy(n); } } }
    gtk_widget_destroy(d);
}
static void v071_paste_uri(void){
    GtkWidget *d=gtk_dialog_new_with_buttons("扫描或粘贴链接",GTK_WINDOW(win),GTK_DIALOG_MODAL,"添加",GTK_RESPONSE_OK,"取消",GTK_RESPONSE_CANCEL,NULL);
    GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(d)), *e; char *t, err[256]="";
    gtk_box_pack_start(GTK_BOX(box),gtk_label_new("粘贴 lsm://add、lsm://relay 或 lsm://pair 链接（桌面也可从图片识别后粘贴）。"),FALSE,FALSE,4);
    e=gtk_entry_new(); gtk_widget_set_name(e,"LSMF_qr_scan"); gtk_box_pack_start(GTK_BOX(box),e,FALSE,FALSE,4);
    gtk_widget_show_all(d);
    if(gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_OK){ t=g_strdup(gtk_entry_get_text(GTK_ENTRY(e))); if(t&&t[0]){ if(lsm_apply_uri(C,t,err)) msgbox(GTK_MESSAGE_WARNING,err[0]?err:"无法识别链接"); else msgbox(GTK_MESSAGE_INFO,"已处理链接"); } g_free(t); }
    gtk_widget_destroy(d);
}
static void v071_show_uri_qr(const char *title, const char *uri){
    GtkWidget *d,*ca,*da,*l; ux_qrn=lsm_qr(uri,ux_qrm);
    d=gtk_dialog_new_with_buttons(title,GTK_WINDOW(win),GTK_DIALOG_MODAL,"复制链接",1,"关闭",GTK_RESPONSE_CLOSE,NULL);
    ca=gtk_dialog_get_content_area(GTK_DIALOG(d)); gtk_container_set_border_width(GTK_CONTAINER(ca),12);
    da=gtk_drawing_area_new(); gtk_widget_set_size_request(da,260,260); g_signal_connect(da,"draw",G_CALLBACK(ux_qr_draw),NULL); gtk_box_pack_start(GTK_BOX(ca),da,FALSE,FALSE,0);
    l=gtk_label_new(uri); gtk_label_set_selectable(GTK_LABEL(l),TRUE); gtk_label_set_line_wrap(GTK_LABEL(l),TRUE); gtk_box_pack_start(GTK_BOX(ca),l,FALSE,FALSE,0);
    gtk_widget_set_name(GTK_WIDGET(d), !strcmp(title,"服务器二维码")?"LSMF_qr_server":"LSMF_qr_pair");
    gtk_widget_show_all(d); while(gtk_dialog_run(GTK_DIALOG(d))==1) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),uri,-1); gtk_widget_destroy(d);
}
static void v071_show_server_qr(void){ char u[400]=""; if(lsm_relay_uri(C,u,sizeof u)){ msgbox(GTK_MESSAGE_INFO,"请先在设置里填写中转服务器"); return; } v071_show_uri_qr("服务器二维码",u); }
static void v071_show_pair_qr(void){ char u[500]="",err[256]=""; if(lsm_account_pair_uri(C,u,sizeof u,err)){ msgbox(GTK_MESSAGE_WARNING,err[0]?err:"无法生成配对码"); return; } v071_show_uri_qr("配对新设备",u); }

static void v071_switch_mode(void){
    char rk[80]="", err[256]=""; const char *cur=lsm_account_mode(C); int to_acc=strcmp(cur,"account")!=0;
    if(lsm_set_account_mode(C, to_acc?"account":"device", rk, sizeof rk, err)) msgbox(GTK_MESSAGE_WARNING, err[0]?err:"切换失败");
    else if(to_acc && rk[0]){ GtkWidget *n=gtk_message_dialog_new(GTK_WINDOW(win),GTK_DIALOG_MODAL,GTK_MESSAGE_INFO,GTK_BUTTONS_OK,"已切换到账号模式。请抄下恢复密钥并妥善保管：\n%s",rk); gtk_dialog_run(GTK_DIALOG(n)); gtk_widget_destroy(n); }
    else msgbox(GTK_MESSAGE_INFO, to_acc?"已切换到账号模式":"已切换到设备模式");
}
static void v071_reveal_rk(void){ char rk[80]=""; if(lsm_reveal_recovery_key(C,rk,sizeof rk)||!rk[0]) msgbox(GTK_MESSAGE_INFO,"还没有恢复密钥（请先开启账号模式）"); else { GtkWidget *n=gtk_message_dialog_new(GTK_WINDOW(win),GTK_DIALOG_MODAL,GTK_MESSAGE_INFO,GTK_BUTTONS_OK,"恢复密钥（请妥善保管，服务器看不到明文）：\n%s",rk); gtk_dialog_run(GTK_DIALOG(n)); gtk_widget_destroy(n); } }
