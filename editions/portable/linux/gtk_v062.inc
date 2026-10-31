/* v0.6.2 (GTK3): polls (right-click a poll to vote), new poll, disappearing timer, archive, chat folders + list filter.
 * Included by gtk_gui.c after gtk_p2.inc. Poll text / timer line come from lsm_display_body (shared core). */
static char conv_filter[64]; /* "" = 全部, LSM_FILTER_ARCHIVED, or a folder name */
static void v062_vote_cb(GtkMenuItem *i, gpointer id){ char err[256]; int k=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(i),"opt")); if(lsm_vote_toggle(C,(const char*)id,k,err)) msgbox(GTK_MESSAGE_WARNING,"%s",err); chat_dirty=1; }
static void v062_close_cb(GtkMenuItem *i, gpointer id){ char err[256]; (void)i; if(lsm_close_poll(C,(const char*)id,err)) msgbox(GTK_MESSAGE_WARNING,"%s",err); chat_dirty=1; }
static void v062_menu_items(GtkMenuShell *menu, const char *id){ lsm_msg *m; int n,i,k,poll=0,closed=0,out=0,mine[10]; char opt[10][320];
    lsm_lock(C); n=lsm_messages(C,&m); k=0;
    for(i=0;i<n;i++) if(!strcmp(m[i].id,id)){ poll=m[i].is_poll&&!m[i].recalled; closed=m[i].poll_closed; out=m[i].out; if(poll) for(k=0;k<10&&lsm_poll_option(&m[i],k,opt[k],sizeof opt[k]);k++) mine[k]=lsm_my_vote(C,&m[i],k); break; }
    lsm_unlock(C); if(!poll) return;
    if(!closed) for(i=0;i<k;i++){ char t[400]; GtkWidget *it; snprintf(t,sizeof t,"%s投票：%d. %s",mine[i]?"✓ ":"",i+1,opt[i]); it=gtk_menu_item_new_with_label(t); g_object_set_data(G_OBJECT(it),"opt",GINT_TO_POINTER(i));
        g_signal_connect_data(it,"activate",G_CALLBACK(v062_vote_cb),g_strdup(id),(GClosureNotify)g_free,0); gtk_menu_shell_append(menu,it); }
    if(out && !closed){ GtkWidget *it=gtk_menu_item_new_with_label("结束投票"); g_signal_connect_data(it,"activate",G_CALLBACK(v062_close_cb),g_strdup(id),(GClosureNotify)g_free,0); gtk_menu_shell_append(menu,it); }
    gtk_menu_shell_append(menu,gtk_separator_menu_item_new()); }
static void v062_new_poll(void){ GtkWidget *d, *box, *q, *ov, *sc, *mu, *l1, *l2; char err[256], id[40];
    if(!cur_conv[0]) return;
    d=gtk_dialog_new_with_buttons("发起投票",GTK_WINDOW(win),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"取消",GTK_RESPONSE_CANCEL,"发起",GTK_RESPONSE_OK,NULL);
    box=gtk_dialog_get_content_area(GTK_DIALOG(d)); gtk_container_set_border_width(GTK_CONTAINER(box),14); gtk_box_set_spacing(GTK_BOX(box),8);
    l1=gtk_label_new("问题："); gtk_label_set_xalign(GTK_LABEL(l1),0); q=gtk_entry_new(); gtk_entry_set_max_length(GTK_ENTRY(q),300);
    l2=gtk_label_new("选项（每行一个，2–10 个）："); gtk_label_set_xalign(GTK_LABEL(l2),0); ov=gtk_text_view_new(); sc=gtk_scrolled_window_new(NULL,NULL); gtk_widget_set_size_request(sc,-1,140); gtk_container_add(GTK_CONTAINER(sc),ov);
    mu=gtk_check_button_new_with_mnemonic("允许多选(_M)");
    gtk_box_pack_start(GTK_BOX(box),l1,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),q,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),l2,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),sc,TRUE,TRUE,0); gtk_box_pack_start(GTK_BOX(box),mu,FALSE,FALSE,0);
    gtk_window_set_default_size(GTK_WINDOW(d),420,-1); gtk_widget_show_all(d);
    while(gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_OK){ GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(ov)); GtkTextIter s,e; char *o; int r;
        gtk_text_buffer_get_bounds(b,&s,&e); o=gtk_text_buffer_get_text(b,&s,&e,FALSE);
        r=lsm_send_poll(C,cur_conv,gtk_entry_get_text(GTK_ENTRY(q)),o,gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mu)),id,err); g_free(o);
        if(!r){ chat_dirty=list_dirty=1; break; } msgbox(GTK_MESSAGE_WARNING,"%s",err); }
    gtk_widget_destroy(d); }
/* combo chooser; returns index or -1 */
static int v062_choose(const char *title, const char *label, const char **items, int n, int cur){ GtkWidget *d=gtk_dialog_new_with_buttons(title,GTK_WINDOW(win),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"取消",GTK_RESPONSE_CANCEL,"确定",GTK_RESPONSE_OK,NULL);
    GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(d)), *l=gtk_label_new(label), *cb=gtk_combo_box_text_new(); int i, r=-1;
    gtk_container_set_border_width(GTK_CONTAINER(box),14); gtk_box_set_spacing(GTK_BOX(box),10); gtk_label_set_xalign(GTK_LABEL(l),0); gtk_label_set_line_wrap(GTK_LABEL(l),TRUE);
    for(i=0;i<n;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cb),items[i]); gtk_combo_box_set_active(GTK_COMBO_BOX(cb),cur<0?0:cur);
    gtk_box_pack_start(GTK_BOX(box),l,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),cb,FALSE,FALSE,0); gtk_dialog_set_default_response(GTK_DIALOG(d),GTK_RESPONSE_OK); gtk_window_set_default_size(GTK_WINDOW(d),380,-1); gtk_widget_show_all(d);
    if(gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_OK) r=gtk_combo_box_get_active(GTK_COMBO_BOX(cb)); gtk_widget_destroy(d); return r; }
static const int V062_TTLS[]={0,30,300,3600,86400,604800};
static void v062_ttl(void){ const char *it[]={"关闭","30 秒","5 分钟","1 小时","1 天","7 天"}; int cur, i, r; char err[256];
    if(!cur_conv[0]) return; lsm_lock(C); cur=lsm_conv_ttl(C,cur_conv); lsm_unlock(C); for(i=0;i<6;i++) if(V062_TTLS[i]==cur) break;
    r=v062_choose("定时删除","新消息在多久后自动删除（双方同步；只影响之后的消息）：",it,6,i<6?i:0);
    if(r>=0 && V062_TTLS[r]!=cur){ if(lsm_set_disappearing(C,cur_conv,V062_TTLS[r],err)) msgbox(GTK_MESSAGE_WARNING,"%s",err); chat_dirty=list_dirty=1; } }
static void v062_archive(void){ int a; if(!cur_conv[0]) return; lsm_lock(C); a=lsm_conv_archived(C,cur_conv); lsm_unlock(C); lsm_set_conv_archived(C,cur_conv,!a); notify("会话",a?"已取消归档":"已归档（收到新消息会自动回到列表；菜单“筛选会话…”可查看归档）"); list_dirty=1; }
static void v062_folders(void){ char f[400], names[32][64], lab[1400]; int n,i; size_t o; char *s;
    if(!cur_conv[0]) return; lsm_lock(C); lsm_conv_folders(C,cur_conv,f,sizeof f); n=lsm_folder_names(C,names,32); lsm_unlock(C);
    o=(size_t)snprintf(lab,sizeof lab,"当前会话所属的分组（用英文逗号分隔，留空=不属于任何分组）。分组只保存在本机。%s",n?"\n已有分组：":""); for(i=0;i<n&&o<sizeof lab;i++) o+=(size_t)snprintf(lab+o,sizeof lab-o,"%s%s",i?"，":"",names[i]);
    if((s=ask_text("会话分组",lab,f))){ lsm_set_conv_folders(C,cur_conv,s); g_free(s); list_dirty=1; } }
static void v062_filter(void){ char names[32][64]; const char *it[34]; int n,i,k=0,cur=0,arch,r;
    lsm_lock(C); n=lsm_folder_names(C,names,32); arch=lsm_archived_count(C); lsm_unlock(C);
    it[k++]="全部会话"; for(i=0;i<n;i++){ if(!strcmp(conv_filter,names[i])) cur=k; it[k++]=names[i]; } { static char al[64]; snprintf(al,sizeof al,"已归档（%d）",arch); if(!strcmp(conv_filter,LSM_FILTER_ARCHIVED)) cur=k; it[k++]=al; }
    r=v062_choose("筛选会话","只显示：",it,k,cur); if(r<0) return;
    if(r==0) conv_filter[0]=0; else if(r==k-1) snprintf(conv_filter,sizeof conv_filter,"%s",LSM_FILTER_ARCHIVED); else snprintf(conv_filter,sizeof conv_filter,"%s",names[r-1]); list_dirty=1; }
