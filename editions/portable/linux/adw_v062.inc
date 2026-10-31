/* v0.6.2 (GTK4 / libadwaita): polls (right-click / long-press a poll to vote), new poll, disappearing timer, archive,
 * chat folders + list filter. Included by adw_gui.c after adw_p2.inc. Poll text / timer line come from lsm_display_body. */
static const char *V062_VOTE[]={"vote0","vote1","vote2","vote3","vote4","vote5","vote6","vote7","vote8","vote9"};
static void v062_menu(GtkWidget *box, const char *id){ lsm_msg *m; int n,i,k=0,poll=0,closed=0,out=0,mine[10]; char opt[10][320];
    lsm_lock(C); n=lsm_messages(C,&m);
    for(i=0;i<n;i++) if(!strcmp(m[i].id,id)){ poll=m[i].is_poll&&!m[i].recalled; closed=m[i].poll_closed; out=m[i].out; if(poll) for(k=0;k<10&&lsm_poll_option(&m[i],k,opt[k],sizeof opt[k]);k++) mine[k]=lsm_my_vote(C,&m[i],k); break; }
    lsm_unlock(C); if(!poll) return;
    if(!closed) for(i=0;i<k;i++){ char t[400]; GtkWidget *b; snprintf(t,sizeof t,"%s投票：%d. %s",mine[i]?"✓ ":"",i+1,opt[i]); b=pop_btn(box,"",V062_VOTE[i],id); gtk_button_set_use_underline(GTK_BUTTON(b),FALSE); gtk_button_set_label(GTK_BUTTON(b),t); gtk_widget_set_halign(gtk_button_get_child(GTK_BUTTON(b)),GTK_ALIGN_START); }
    if(out && !closed) pop_btn(box,"结束投票","close-poll",id); }
static int v062_action(const char *what, const char *id){ char err[256]="";
    if(!strncmp(what,"vote",4) && what[4]>='0' && what[4]<='9'){ if(lsm_vote_toggle(C,id,what[4]-'0',err)) info("投票失败",err); chat_dirty=1; return 1; }
    if(!strcmp(what,"close-poll")){ if(lsm_close_poll(C,id,err)) info("操作失败",err); else toast("投票已结束"); chat_dirty=1; return 1; }
    return 0; }

/* ---- dialogs (AdwAlertDialog + extra child; async) ---- */
typedef struct { GtkWidget *a, *b, *c; int kind; char conv[64]; } v062dlg;
static const int V062_TTLS[]={0,30,300,3600,86400,604800};
static const char *V062_TTLN[]={"关闭","30 秒","5 分钟","1 小时","1 天","7 天",NULL};
static char v062_fnames[32][64]; static int v062_nf;
static void v062_resp(AdwAlertDialog *d, const char *r, gpointer p){ v062dlg *x=p; char err[256]="", id[40]; (void)d;
    if(!strcmp(r,"ok")){
        if(x->kind==1){ GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(x->b)); GtkTextIter s,e; char *o; gtk_text_buffer_get_bounds(b,&s,&e); o=gtk_text_buffer_get_text(b,&s,&e,FALSE);
            if(lsm_send_poll(C,x->conv,gtk_editable_get_text(GTK_EDITABLE(x->a)),o,gtk_check_button_get_active(GTK_CHECK_BUTTON(x->c)),id,err)) info("无法发起投票",err); g_free(o); }
        else if(x->kind==2){ guint i=gtk_drop_down_get_selected(GTK_DROP_DOWN(x->a)); if(i<6 && lsm_set_disappearing(C,x->conv,V062_TTLS[i],err)) info("设置失败",err); }
        else if(x->kind==3) lsm_set_conv_folders(C,x->conv,gtk_editable_get_text(GTK_EDITABLE(x->a)));
        else if(x->kind==4){ guint i=gtk_drop_down_get_selected(GTK_DROP_DOWN(x->a)); if(i==0) conv_filter[0]=0; else if((int)i==v062_nf+1) snprintf(conv_filter,sizeof conv_filter,"%s",LSM_FILTER_ARCHIVED); else if((int)i<=v062_nf) snprintf(conv_filter,sizeof conv_filter,"%s",v062_fnames[i-1]); }
        chat_dirty=list_dirty=1; }
    g_free(x); }
static void v062_show(const char *heading, const char *body, GtkWidget *extra, v062dlg *x, const char *ok){ AdwDialog *d=adw_alert_dialog_new(heading,body);
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(d),"cancel","取消","ok",ok,NULL); adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(d),"ok",ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(d),"ok"); adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(d),"cancel"); adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(d),extra);
    g_signal_connect(d,"response",G_CALLBACK(v062_resp),x); adw_dialog_present(d,win); }
static void v062_new_poll(void){ v062dlg *x; GtkWidget *box, *sc; if(!cur_conv[0]) return; x=g_new0(v062dlg,1); x->kind=1; snprintf(x->conv,64,"%s",cur_conv);
    box=gtk_box_new(GTK_ORIENTATION_VERTICAL,8); x->a=gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(x->a),"问题，例如：周会改到哪天？"); gtk_entry_set_max_length(GTK_ENTRY(x->a),300);
    x->b=gtk_text_view_new(); gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(x->b),GTK_WRAP_WORD_CHAR); sc=gtk_scrolled_window_new(); gtk_widget_set_size_request(sc,320,140); gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc),x->b); cls(sc,"card");
    x->c=gtk_check_button_new_with_mnemonic("允许多选(_M)");
    gtk_box_append(GTK_BOX(box),x->a); gtk_box_append(GTK_BOX(box),label("选项（每行一个，2–10 个）：","dim-label")); gtk_box_append(GTK_BOX(box),sc); gtk_box_append(GTK_BOX(box),x->c);
    v062_show("发起投票",NULL,box,x,"发起"); }
static void v062_ttl(void){ v062dlg *x; int cur,i; if(!cur_conv[0]) return; x=g_new0(v062dlg,1); x->kind=2; snprintf(x->conv,64,"%s",cur_conv);
    lsm_lock(C); cur=lsm_conv_ttl(C,cur_conv); lsm_unlock(C); x->a=gtk_drop_down_new_from_strings(V062_TTLN); for(i=0;i<6;i++) if(V062_TTLS[i]==cur) gtk_drop_down_set_selected(GTK_DROP_DOWN(x->a),i);
    v062_show("定时删除","新消息在多久后自动删除（双方同步；只影响之后的消息）。",x->a,x,"确定"); }
static void v062_archive(void){ int a; if(!cur_conv[0]) return; lsm_lock(C); a=lsm_conv_archived(C,cur_conv); lsm_unlock(C); lsm_set_conv_archived(C,cur_conv,!a); toast(a?"已取消归档":"已归档（收到新消息会自动回到列表）"); list_dirty=1; }
static void v062_folders(void){ v062dlg *x; char f[400], body[1400]; int n,i; size_t o; if(!cur_conv[0]) return; x=g_new0(v062dlg,1); x->kind=3; snprintf(x->conv,64,"%s",cur_conv);
    lsm_lock(C); lsm_conv_folders(C,cur_conv,f,sizeof f); n=lsm_folder_names(C,v062_fnames,32); lsm_unlock(C);
    o=(size_t)snprintf(body,sizeof body,"用英文逗号分隔，留空 = 不属于任何分组。分组只保存在本机。%s",n?"\n已有：":""); for(i=0;i<n&&o<sizeof body;i++) o+=(size_t)snprintf(body+o,sizeof body-o,"%s%s",i?"，":"",v062_fnames[i]);
    x->a=gtk_entry_new(); gtk_editable_set_text(GTK_EDITABLE(x->a),f); gtk_entry_set_activates_default(GTK_ENTRY(x->a),TRUE);
    v062_show("会话分组",body,x->a,x,"保存"); }
static void v062_filter(void){ v062dlg *x=g_new0(v062dlg,1); const char *it[35]; char al[64]; int i,k=0,arch,cur=0;
    lsm_lock(C); v062_nf=lsm_folder_names(C,v062_fnames,32); arch=lsm_archived_count(C); lsm_unlock(C);
    it[k++]="全部会话"; for(i=0;i<v062_nf;i++){ if(!strcmp(conv_filter,v062_fnames[i])) cur=k; it[k++]=v062_fnames[i]; } snprintf(al,sizeof al,"已归档（%d）",arch); if(!strcmp(conv_filter,LSM_FILTER_ARCHIVED)) cur=k; it[k++]=al; it[k]=NULL;
    x->kind=4; x->a=gtk_drop_down_new_from_strings(it); gtk_drop_down_set_selected(GTK_DROP_DOWN(x->a),cur);
    v062_show("筛选会话","只显示：",x->a,x,"确定"); }
