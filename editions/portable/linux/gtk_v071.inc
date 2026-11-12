/* v0.7.1: first-run account/device choice + QR URI apply. Hooks: LSMF_account_mode LSMF_qr_scan LSMF_qr_server LSMF_qr_pair LSMF_qr_profile */
static int v071_fr_reshoot;
static void v071_first_run(void){
    GtkWidget *d,*box,*l,*b1,*b2,*hint; char err[256]="";
    int shot=v071_fr_reshoot; v071_fr_reshoot=0;
    if(lsm_first_run_done(C) && !shot) return;
    if(!shot && getenv("LSM_UI_TEST") && !getenv("LSM_SHOT_FIRST_RUN")){ lsm_complete_first_run(C,NULL,"device",NULL,err); return; }
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
    if(!lsm_first_run_done(C)){
      int acc=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b2));
      lsm_complete_first_run(C,NULL,acc?"account":"device",NULL,err);
      if(acc){ char rk[80]=""; if(!lsm_reveal_recovery_key(C,rk,sizeof rk) && rk[0]){
          GtkWidget *n=gtk_message_dialog_new(GTK_WINDOW(win),GTK_DIALOG_MODAL,GTK_MESSAGE_INFO,GTK_BUTTONS_OK,"请抄下恢复密钥并妥善保管：\n%s",rk);
          gtk_dialog_run(GTK_DIALOG(n)); gtk_widget_destroy(n); } } }
    gtk_widget_destroy(d);
}
static void v071_shot_first_run(void){ if(!getenv("LSM_UI_TEST")) return; v071_fr_reshoot=1; v071_first_run(); }
static void v071_apply_scanned(const char *u){ char err[256]=""; if(!u||!u[0]) return;
    if(lsm_apply_uri(C,u,err)) msgbox(GTK_MESSAGE_WARNING,err[0]?err:"无法识别链接"); else msgbox(GTK_MESSAGE_INFO,"已处理链接"); }
static void v071_show_scan_result(const char *path, const char *uri){
    GtkWidget *d,*ca,*l; char miss[160];
    d=gtk_dialog_new_with_buttons("从图片识别",GTK_WINDOW(win),GTK_DIALOG_MODAL,"关闭",GTK_RESPONSE_CLOSE,NULL);
    ca=gtk_dialog_get_content_area(GTK_DIALOG(d)); gtk_container_set_border_width(GTK_CONTAINER(ca),12);
    if(path && path[0] && g_file_test(path,G_FILE_TEST_EXISTS)){ GtkWidget *im=gtk_image_new_from_file(path); gtk_box_pack_start(GTK_BOX(ca),im,FALSE,FALSE,4); }
    if(uri && uri[0]) l=gtk_label_new(uri);
    else { snprintf(miss,sizeof miss,"无法从该文件识别二维码，请改为粘贴 lsm:// 链接（安装 zbarimg 后可从图片识别）。"); l=gtk_label_new(miss); }
    gtk_label_set_selectable(GTK_LABEL(l),TRUE); gtk_label_set_line_wrap(GTK_LABEL(l),TRUE); gtk_label_set_line_wrap_mode(GTK_LABEL(l),PANGO_WRAP_CHAR); gtk_label_set_max_width_chars(GTK_LABEL(l),40);
    gtk_widget_set_name(GTK_WIDGET(d),"LSMF_qr_scan"); gtk_box_pack_start(GTK_BOX(ca),l,FALSE,FALSE,4);
    gtk_widget_show_all(d); gtk_dialog_run(GTK_DIALOG(d)); gtk_widget_destroy(d);
}
static void v071_scan_image_file(void){ /* LSMF_qr_scan — image file where zbarimg is present; else paste */
    char uri[800]=""; const char *shot=getenv("LSM_SHOT_QR_FILE");
    if(shot && shot[0]){ if(lsm_uri_from_file(shot,uri,sizeof uri)) v071_show_scan_result(shot,NULL); else v071_show_scan_result(shot,uri); return; }
    GtkWidget *fc=gtk_file_chooser_dialog_new("选择二维码图片",GTK_WINDOW(win),GTK_FILE_CHOOSER_ACTION_OPEN,"取消",GTK_RESPONSE_CANCEL,"识别",GTK_RESPONSE_ACCEPT,NULL);
    if(gtk_dialog_run(GTK_DIALOG(fc))==GTK_RESPONSE_ACCEPT){ char *f=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
        if(f){ if(lsm_uri_from_file(f,uri,sizeof uri)) msgbox(GTK_MESSAGE_WARNING,"无法从该文件识别二维码，请改为粘贴 lsm:// 链接（安装 zbarimg 后可从图片识别）。"); else v071_apply_scanned(uri); g_free(f); } }
    gtk_widget_destroy(fc); }
static void v071_paste_uri(void){
    GtkWidget *d=gtk_dialog_new_with_buttons("扫描或粘贴链接",GTK_WINDOW(win),GTK_DIALOG_MODAL,"从图片识别",2,"添加",GTK_RESPONSE_OK,"取消",GTK_RESPONSE_CANCEL,NULL);
    GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(d)), *e; char *t, err[256]=""; int resp;
    gtk_box_pack_start(GTK_BOX(box),gtk_label_new("粘贴 lsm://add、lsm://relay 或 lsm://pair 链接。也可以选择一张二维码图片识别（需系统安装 zbarimg）。"),FALSE,FALSE,4);
    e=gtk_entry_new(); gtk_widget_set_name(e,"LSMF_qr_scan"); gtk_box_pack_start(GTK_BOX(box),e,FALSE,FALSE,4);
    gtk_widget_show_all(d);
    resp=gtk_dialog_run(GTK_DIALOG(d));
    if(resp==2){ gtk_widget_destroy(d); v071_scan_image_file(); return; }
    if(resp==GTK_RESPONSE_OK){ t=g_strdup(gtk_entry_get_text(GTK_ENTRY(e))); if(t&&t[0]) v071_apply_scanned(t); g_free(t); }
    gtk_widget_destroy(d); (void)err;
}
static void v071_show_uri_qr(const char *title, const char *uri){
    GtkWidget *d,*ca,*da,*l; ux_qrn=lsm_qr(uri,ux_qrm);
    d=gtk_dialog_new_with_buttons(title,GTK_WINDOW(win),GTK_DIALOG_MODAL,"复制链接",1,"关闭",GTK_RESPONSE_CLOSE,NULL);
    ca=gtk_dialog_get_content_area(GTK_DIALOG(d)); gtk_container_set_border_width(GTK_CONTAINER(ca),12);
    da=gtk_drawing_area_new(); gtk_widget_set_size_request(da,260,260); g_signal_connect(da,"draw",G_CALLBACK(ux_qr_draw),NULL); gtk_widget_set_halign(da,GTK_ALIGN_CENTER); gtk_box_pack_start(GTK_BOX(ca),da,FALSE,FALSE,0);
    l=gtk_label_new(uri); gtk_label_set_selectable(GTK_LABEL(l),TRUE); gtk_label_set_line_wrap(GTK_LABEL(l),TRUE); gtk_label_set_line_wrap_mode(GTK_LABEL(l),PANGO_WRAP_CHAR); gtk_label_set_max_width_chars(GTK_LABEL(l),40); gtk_box_pack_start(GTK_BOX(ca),l,FALSE,FALSE,0);
    gtk_widget_set_name(GTK_WIDGET(d), !strcmp(title,"服务器二维码")?"LSMF_qr_server":"LSMF_qr_pair");
    gtk_widget_show_all(d); while(gtk_dialog_run(GTK_DIALOG(d))==1) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),uri,-1); gtk_widget_destroy(d);
}
static void v071_show_server_qr(void){ char u[400]=""; if(lsm_relay_uri(C,u,sizeof u)||!u[0]){ if(getenv("LSM_UI_TEST")) snprintf(u,sizeof u,"lsm://relay?h=ci.example.invalid&p=9443"); else { msgbox(GTK_MESSAGE_INFO,"请先在设置里填写中转服务器"); return; } } v071_show_uri_qr("服务器二维码",u); }
static void v071_show_pair_qr(void){ char u[500]="",err[256]=""; if(lsm_account_pair_uri(C,u,sizeof u,err)||!u[0]){ if(getenv("LSM_UI_TEST")) snprintf(u,sizeof u,"lsm://pair?r=ci.example.invalid:9443&t=%s&k=%s","0000000000000000000000000000000000000000000000000000000000000000","1111111111111111111111111111111111111111111111111111111111111111"); else { msgbox(GTK_MESSAGE_WARNING,err[0]?err:"无法生成配对码"); return; } } v071_show_uri_qr("配对新设备",u); }

static void v071_switch_mode(void){
    char rk[80]="", err[256]=""; const char *cur=lsm_account_mode(C); int to_acc=strcmp(cur,"account")!=0;
    if(lsm_set_account_mode(C, to_acc?"account":"device", rk, sizeof rk, err)) msgbox(GTK_MESSAGE_WARNING, err[0]?err:"切换失败");
    else if(to_acc && rk[0]){ GtkWidget *n=gtk_message_dialog_new(GTK_WINDOW(win),GTK_DIALOG_MODAL,GTK_MESSAGE_INFO,GTK_BUTTONS_OK,"已切换到账号模式。请抄下恢复密钥并妥善保管：\n%s",rk); gtk_dialog_run(GTK_DIALOG(n)); gtk_widget_destroy(n); }
    else msgbox(GTK_MESSAGE_INFO, to_acc?"已切换到账号模式":"已切换到设备模式");
}
static void v071_reveal_rk(void){ char rk[80]=""; if(lsm_reveal_recovery_key(C,rk,sizeof rk)||!rk[0]) msgbox(GTK_MESSAGE_INFO,"还没有恢复密钥（请先开启账号模式）"); else { GtkWidget *n=gtk_message_dialog_new(GTK_WINDOW(win),GTK_DIALOG_MODAL,GTK_MESSAGE_INFO,GTK_BUTTONS_OK,"恢复密钥（请妥善保管，服务器看不到明文）：\n%s",rk); gtk_dialog_run(GTK_DIALOG(n)); gtk_widget_destroy(n); } }
