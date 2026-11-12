/* v0.7.1: first-run account/device choice + QR URI apply. Hooks: LSMF_account_mode LSMF_qr_scan LSMF_qr_server LSMF_qr_pair LSMF_qr_profile */
static GtkWidget *v071_acc_btn;
static int v071_fr_reshoot;
static void v071_fr_done(AdwAlertDialog *d, const char *r, gpointer p){
    char err[256]="", rk[80]=""; int acc;
    (void)d;(void)p;(void)r;
    if(v071_fr_reshoot){ v071_fr_reshoot=0; return; }
    if(lsm_first_run_done(C)) return;
    acc = v071_acc_btn && gtk_check_button_get_active(GTK_CHECK_BUTTON(v071_acc_btn));
    lsm_complete_first_run(C,NULL,acc?"account":"device",NULL,err);
    if(acc && !lsm_reveal_recovery_key(C,rk,sizeof rk) && rk[0]) info("请抄下恢复密钥", rk);
}
static void v071_first_run(void){
    GtkWidget *box,*b1,*b2; AdwDialog *d; char err[256]="";
    int shot=v071_fr_reshoot;
    if(lsm_first_run_done(C) && !shot) return;
    if(!shot && getenv("LSM_UI_TEST") && !getenv("LSM_SHOT_FIRST_RUN")){ lsm_complete_first_run(C,NULL,"device",NULL,err); return; }
    box=gtk_box_new(GTK_ORIENTATION_VERTICAL,8);
    b1=gtk_check_button_new_with_label("设备模式 — 仅本机，局域网直连，不把身份放到服务器");
    b2=gtk_check_button_new_with_label("账号模式 — 身份加密备份在中转上，换机可用恢复密钥或配对二维码恢复");
    gtk_check_button_set_group(GTK_CHECK_BUTTON(b2), GTK_CHECK_BUTTON(b1));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(b1), TRUE);
    gtk_widget_set_name(b1,"LSMF_account_mode"); gtk_widget_set_name(b2,"LSMF_account_mode");
    gtk_box_append(GTK_BOX(box),b1); gtk_box_append(GTK_BOX(box),b2);
    v071_acc_btn=b2;
    d=adw_alert_dialog_new("欢迎使用局域网安全通讯","请选择身份方式（之后可在设置里更改）。账号模式会生成恢复密钥，不使用服务器密码。添加联系人、连接服务器、配对新设备都可以通过扫描或粘贴 lsm:// 链接完成。");
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(d),"ok","开始使用",NULL);
    adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(d),box);
    g_signal_connect(d,"response",G_CALLBACK(v071_fr_done),NULL);
    adw_dialog_present(d,win);
}
static void v071_shot_first_run(void){ if(!getenv("LSM_UI_TEST")) return; v071_fr_reshoot=1; v071_first_run(); }
static void v071_show_uri_qr(const char *title, const char *uri){
    AdwDialog *d; GtkWidget *box,*da,*l;
    snprintf(ux_uri,sizeof ux_uri,"%s",uri); ux_qrn=lsm_qr(uri,ux_qrm);
    box=gtk_box_new(GTK_ORIENTATION_VERTICAL,8);
    da=gtk_drawing_area_new(); gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(da),240); gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(da),240);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da),ux_qr_draw,NULL,NULL); gtk_widget_set_halign(da,GTK_ALIGN_CENTER); gtk_box_append(GTK_BOX(box),da);
    l=gtk_label_new(uri); gtk_label_set_selectable(GTK_LABEL(l),TRUE); gtk_label_set_wrap(GTK_LABEL(l),TRUE); gtk_box_append(GTK_BOX(box),l);
    gtk_widget_set_name(box, !strcmp(title,"服务器二维码")?"LSMF_qr_server":!strcmp(title,"我的二维码")?"LSMF_qr_profile":"LSMF_qr_pair");
    d=adw_alert_dialog_new(title,"用另一台设备扫描，或复制下面的链接粘贴。");
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(d),"close","关闭","copy","复制链接",NULL);
    adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(d),box);
    g_signal_connect(d,"response",G_CALLBACK(ux_qr_resp),NULL);
    adw_dialog_present(d,win);
}
static void v071_show_server_qr(void){ char u[400]=""; if(lsm_relay_uri(C,u,sizeof u)||!u[0]){ if(getenv("LSM_UI_TEST")) snprintf(u,sizeof u,"lsm://relay?h=ci.example.invalid&p=9443"); else { toast("请先在设置里填写中转服务器"); return; } } v071_show_uri_qr("服务器二维码",u); }
static void v071_show_pair_qr(void){ char u[500]="",err[256]=""; if(lsm_account_pair_uri(C,u,sizeof u,err)||!u[0]){ if(getenv("LSM_UI_TEST")) snprintf(u,sizeof u,"lsm://pair?r=ci.example.invalid:9443&t=%s&k=%s","0000000000000000000000000000000000000000000000000000000000000000","1111111111111111111111111111111111111111111111111111111111111111"); else { toast(err[0]?err:"无法生成配对码"); return; } } v071_show_uri_qr("配对新设备",u); }
static GtkWidget *v071_uri_entry;
static void v071_img_picked(GObject *src, GAsyncResult *res, gpointer p){
    GFile *f=gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src),res,NULL); char *path, uri[800]="", err[256]=""; (void)p;
    if(!f) return; path=g_file_get_path(f);
    if(!path || lsm_uri_from_file(path,uri,sizeof uri)) toast("无法从该文件识别二维码，请改为粘贴 lsm:// 链接（安装 zbarimg 后可从图片识别）。");
    else if(lsm_apply_uri(C,uri,err)) toast(err[0]?err:"无法识别链接"); else toast("已处理链接");
    g_free(path); g_object_unref(f); }
static void v071_show_scan_result(const char *path, const char *uri){
    AdwDialog *d; GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,8), *l;
    if(path && path[0] && g_file_test(path,G_FILE_TEST_EXISTS)){ GtkWidget *pic=gtk_image_new_from_file(path); gtk_widget_set_size_request(pic,240,240); gtk_box_append(GTK_BOX(box),pic); }
    l=gtk_label_new(uri && uri[0] ? uri : "无法从该文件识别二维码，请改为粘贴 lsm:// 链接（安装 zbarimg 后可从图片识别）。");
    gtk_label_set_selectable(GTK_LABEL(l),TRUE); gtk_label_set_wrap(GTK_LABEL(l),TRUE); gtk_box_append(GTK_BOX(box),l);
    gtk_widget_set_name(box,"LSMF_qr_scan");
    d=adw_alert_dialog_new("从图片识别", uri && uri[0] ? "已从图片识别出下面的链接。" : "未能识别。");
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(d),"close","关闭",NULL);
    adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(d),box);
    adw_dialog_present(d,win);
}
static void v071_scan_image_file(void){ /* LSMF_qr_scan */
    const char *shot=getenv("LSM_SHOT_QR_FILE"); char uri[800]="";
    if(shot && shot[0]){ if(lsm_uri_from_file(shot,uri,sizeof uri)) v071_show_scan_result(shot,NULL); else v071_show_scan_result(shot,uri); return; }
    GtkFileDialog *d=gtk_file_dialog_new();
    gtk_file_dialog_set_title(d,"选择二维码图片"); gtk_file_dialog_open(d,GTK_WINDOW(win),NULL,v071_img_picked,NULL); g_object_unref(d); }
static void v071_paste_done(AdwAlertDialog *d, const char *r, gpointer p){
    char err[256]=""; const char *t; (void)d;(void)p;
    if(r && !strcmp(r,"img")){ v071_scan_image_file(); return; }
    if(r && !strcmp(r,"ok") && v071_uri_entry){ t=gtk_editable_get_text(GTK_EDITABLE(v071_uri_entry));
        if(t&&t[0]){ if(lsm_apply_uri(C,t,err)) toast(err[0]?err:"无法识别链接"); else toast("已处理链接"); } } }
static void v071_paste_uri(void){
    GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,8), *e=gtk_entry_new(); AdwDialog *d;
    gtk_entry_set_placeholder_text(GTK_ENTRY(e),"lsm://add?… 或 lsm://relay?… 或 lsm://pair?…");
    gtk_widget_set_name(e,"LSMF_qr_scan"); v071_uri_entry=e; gtk_box_append(GTK_BOX(box),e);
    d=adw_alert_dialog_new("扫描或粘贴链接","粘贴 lsm:// 链接，或从图片识别（需系统安装 zbarimg）。");
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(d),"img","从图片识别","ok","添加","cancel","取消",NULL);
    adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(d),box);
    g_signal_connect(d,"response",G_CALLBACK(v071_paste_done),NULL);
    adw_dialog_present(d,win);
}
static void v071_switch_mode(void){
    char rk[80]="", err[256]=""; const char *cur=lsm_account_mode(C); int to_acc=strcmp(cur,"account")!=0;
    if(lsm_set_account_mode(C, to_acc?"account":"device", rk, sizeof rk, err)) toast(err[0]?err:"切换失败");
    else if(to_acc && rk[0]) info("请抄下恢复密钥", rk);
    else toast(to_acc?"已切换到账号模式":"已切换到设备模式");
}
static void v071_reveal_rk(void){ char rk[80]=""; if(lsm_reveal_recovery_key(C,rk,sizeof rk)||!rk[0]) toast("还没有恢复密钥（请先开启账号模式）"); else info("恢复密钥", rk); }
