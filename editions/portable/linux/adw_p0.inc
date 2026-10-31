/* v0.6.1 P0 (GTK4/libadwaita): 消息历史、个人资料、联系人资料与备注名. Included by adw_gui.c. */

typedef struct { GtkWidget *w,*q,*conv,*type,*dir,*st,*from,*to,*list,*more,*count; int offset; } hist4;
static hist4 H4;
static int64_t day_ms(const char *s, int end){ struct tm t; memset(&t,0,sizeof t); if(!s||sscanf(s,"%d-%d-%d",&t.tm_year,&t.tm_mon,&t.tm_mday)!=3) return 0; t.tm_year-=1900; t.tm_mon-=1; if(end) t.tm_mday+=1; t.tm_isdst=-1; return (int64_t)mktime(&t)*1000; }
static void select_conv(const char *cv);
static void h4_activate(GtkListBox *lb, GtkListBoxRow *row, gpointer p){ const char *cv=g_object_get_data(G_OBJECT(row),"conv"), *id=g_object_get_data(G_OBJECT(row),"id"); (void)lb;(void)p;
    if(!cv) return; snprintf(jump_id,sizeof jump_id,"%s",id); select_conv(cv); list_dirty=1; adw_navigation_split_view_set_show_content(ADW_NAVIGATION_SPLIT_VIEW(split),TRUE); gtk_window_destroy(GTK_WINDOW(H4.w)); }
static GtkWidget *dd(const char *const *items){ GtkWidget *d=gtk_drop_down_new_from_strings(items); return d; }
static void h4_run(int append){ hist4 *h=&H4; lsm_hq q; int idx[50], n, tot=0, i; lsm_msg *m; char cnt[120]; const char *cv=NULL; GtkWidget *ch;
    static const int TM[]={0,1<<LSM_T_TEXT,1<<LSM_T_IMAGE,1<<LSM_T_FILE,1<<LSM_T_STICKER}, SM[]={0,1<<ST_READ,(1<<ST_FAILED)|(1<<ST_REJECTED),1<<ST_REJECTED};
    guint ci=gtk_drop_down_get_selected(GTK_DROP_DOWN(h->conv)); char **convs=g_object_get_data(G_OBJECT(h->conv),"ids");
    if(ci>0 && convs) cv=convs[ci];
    if(!append){ while((ch=gtk_widget_get_first_child(h->list))) gtk_list_box_remove(GTK_LIST_BOX(h->list),ch); h->offset=0; }
    memset(&q,0,sizeof q); q.query=gtk_editable_get_text(GTK_EDITABLE(h->q)); q.convs=cv; q.types=TM[gtk_drop_down_get_selected(GTK_DROP_DOWN(h->type))%5]; q.direction=(int)gtk_drop_down_get_selected(GTK_DROP_DOWN(h->dir))%3; q.statuses=SM[gtk_drop_down_get_selected(GTK_DROP_DOWN(h->st))%4];
    q.from_ms=day_ms(gtk_editable_get_text(GTK_EDITABLE(h->from)),0); q.to_ms=day_ms(gtk_editable_get_text(GTK_EDITABLE(h->to)),1); q.offset=h->offset; q.limit=50;
    lsm_lock(C); n=lsm_history_query(C,&q,idx,50,&tot); lsm_messages(C,&m);
    for(i=0;i<n;i++){ lsm_msg *x=&m[idx[i]]; char ts[32], body[300], t[1400]; time_t tt=(time_t)(x->ts_ms/1000); struct tm tmv; gchar *who,*b; GtkWidget *row=gtk_list_box_row_new(), *l=gtk_label_new(NULL); int k;
        localtime_r(&tt,&tmv); strftime(ts,sizeof ts,"%Y-%m-%d %H:%M",&tmv); snprintf(body,sizeof body,"%s",x->kind==K_FILE?x->file_name:(x->body?x->body:"")); for(k=0;body[k];k++) if(body[k]=='\n') body[k]=' ';
        who=g_markup_escape_text(x->out?"我":x->sender_name,-1); b=g_markup_escape_text(body,-1); snprintf(t,sizeof t,"<small>%s · %s · %s</small>\n%s",ts,who,status_txt(x->status),b); g_free(who); g_free(b);
        gtk_label_set_markup(GTK_LABEL(l),t); gtk_label_set_xalign(GTK_LABEL(l),0); gtk_label_set_ellipsize(GTK_LABEL(l),PANGO_ELLIPSIZE_END); gtk_widget_set_margin_start(l,10); gtk_widget_set_margin_top(l,6); gtk_widget_set_margin_bottom(l,6);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row),l); g_object_set_data_full(G_OBJECT(row),"conv",g_strdup(x->conv),g_free); g_object_set_data_full(G_OBJECT(row),"id",g_strdup(x->id),g_free); gtk_list_box_append(GTK_LIST_BOX(h->list),row); }
    lsm_unlock(C); h->offset+=n; snprintf(cnt,sizeof cnt,"共 %d 条，已显示 %d 条（点击跳转到聊天）",tot,h->offset); gtk_label_set_text(GTK_LABEL(h->count),cnt); gtk_widget_set_sensitive(h->more,h->offset<tot); }
static void h4_changed(void){ h4_run(0); }
static void h4_more(void){ h4_run(1); }
static void h4_closed(GtkWindow *w, gpointer p){ (void)w;(void)p; H4.w=NULL; }
static void history_dialog(void){ hist4 *h=&H4; GtkWidget *tv, *box, *g, *sc, *btn; lsm_contact *k; lsm_group *gr; int n,i, nids=1, sel=0; GPtrArray *names=g_ptr_array_new_with_free_func(g_free); char **ids;
    static const char *const TY[]={"全部类型","文字","图片","文件","贴纸",NULL}, *const DI[]={"收发","收到的","发出的",NULL}, *const ST[]={"全部状态","已读","失败/拒收","已拒收",NULL};
    if(h->w){ gtk_window_present(GTK_WINDOW(h->w)); return; }
    memset(h,0,sizeof *h); h->w=adw_window_new(); gtk_window_set_title(GTK_WINDOW(h->w),"消息历史"); gtk_window_set_transient_for(GTK_WINDOW(h->w),GTK_WINDOW(win)); gtk_window_set_default_size(GTK_WINDOW(h->w),640,600); gtk_window_set_hide_on_close(GTK_WINDOW(h->w),FALSE);
    tv=adw_toolbar_view_new(); adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv),adw_header_bar_new()); box=gtk_box_new(GTK_ORIENTATION_VERTICAL,6); gtk_widget_set_margin_start(box,12); gtk_widget_set_margin_end(box,12); gtk_widget_set_margin_bottom(box,12);
    lsm_lock(C); n=lsm_contacts(C,&k); { lsm_group *g2; int ng=lsm_groups(C,&g2); ids=g_new0(char*,n+ng+2); } ids[0]=g_strdup("*"); g_ptr_array_add(names,g_strdup("全部会话"));
    for(i=0;i<n;i++){ ids[nids]=g_strdup_printf("dm:%s",k[i].fp); if(!strcmp(ids[nids],cur_conv)) sel=nids; nids++; g_ptr_array_add(names,g_strdup(lsm_display_name(&k[i]))); }
    n=lsm_groups(C,&gr); for(i=0;i<n;i++){ char u[37]; lsm_uuid_str(gr[i].id,u); ids[nids]=g_strdup_printf("group:%s",u); if(!strcmp(ids[nids],cur_conv)) sel=nids; nids++; g_ptr_array_add(names,g_strdup_printf("# %s",gr[i].name)); } lsm_unlock(C);
    g_ptr_array_add(names,NULL); h->conv=gtk_drop_down_new_from_strings((const char*const*)names->pdata); g_object_set_data_full(G_OBJECT(h->conv),"ids",ids,(GDestroyNotify)g_strfreev); g_ptr_array_free(names,TRUE); gtk_drop_down_set_selected(GTK_DROP_DOWN(h->conv),sel);
    h->q=gtk_search_entry_new(); gtk_widget_set_hexpand(h->q,TRUE); gtk_accessible_update_property(GTK_ACCESSIBLE(h->q),GTK_ACCESSIBLE_PROPERTY_LABEL,"搜索消息",-1);
    h->type=dd(TY); h->dir=dd(DI); h->st=dd(ST); h->from=gtk_entry_new(); h->to=gtk_entry_new(); gtk_editable_set_width_chars(GTK_EDITABLE(h->from),11); gtk_editable_set_width_chars(GTK_EDITABLE(h->to),11);
    gtk_widget_set_tooltip_text(h->from,"起始日期 YYYY-MM-DD（可空）"); gtk_widget_set_tooltip_text(h->to,"结束日期 YYYY-MM-DD（含当天，可空）");
    g=gtk_grid_new(); gtk_grid_set_row_spacing(GTK_GRID(g),6); gtk_grid_set_column_spacing(GTK_GRID(g),6); btn=gtk_button_new_with_label("搜索"); cls(btn,"suggested-action");
    gtk_grid_attach(GTK_GRID(g),h->q,0,0,3,1); gtk_grid_attach(GTK_GRID(g),btn,3,0,1,1); gtk_grid_attach(GTK_GRID(g),h->conv,0,1,1,1); gtk_grid_attach(GTK_GRID(g),h->type,1,1,1,1); gtk_grid_attach(GTK_GRID(g),h->dir,2,1,1,1); gtk_grid_attach(GTK_GRID(g),h->st,3,1,1,1);
    gtk_grid_attach(GTK_GRID(g),gtk_label_new("日期"),0,2,1,1); gtk_grid_attach(GTK_GRID(g),h->from,1,2,1,1); gtk_grid_attach(GTK_GRID(g),gtk_label_new("至"),2,2,1,1); gtk_grid_attach(GTK_GRID(g),h->to,3,2,1,1);
    gtk_box_append(GTK_BOX(box),g); h->count=label("","dim-label"); gtk_box_append(GTK_BOX(box),h->count);
    sc=gtk_scrolled_window_new(); gtk_widget_set_vexpand(sc,TRUE); h->list=gtk_list_box_new(); cls(h->list,"boxed-list"); gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc),h->list); gtk_box_append(GTK_BOX(box),sc);
    h->more=gtk_button_new_with_label("加载更多"); gtk_box_append(GTK_BOX(box),h->more);
    g_signal_connect_swapped(btn,"clicked",G_CALLBACK(h4_changed),NULL); g_signal_connect_swapped(h->q,"activate",G_CALLBACK(h4_changed),NULL); g_signal_connect_swapped(h->q,"search-changed",G_CALLBACK(h4_changed),NULL); g_signal_connect_swapped(h->more,"clicked",G_CALLBACK(h4_more),NULL);
    g_signal_connect_swapped(h->conv,"notify::selected",G_CALLBACK(h4_changed),NULL); g_signal_connect_swapped(h->type,"notify::selected",G_CALLBACK(h4_changed),NULL); g_signal_connect_swapped(h->dir,"notify::selected",G_CALLBACK(h4_changed),NULL); g_signal_connect_swapped(h->st,"notify::selected",G_CALLBACK(h4_changed),NULL);
    g_signal_connect(h->list,"row-activated",G_CALLBACK(h4_activate),NULL); g_signal_connect(h->w,"destroy",G_CALLBACK(h4_closed),NULL);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tv),box); adw_window_set_content(ADW_WINDOW(h->w),tv);
    { GtkEventController *kc=gtk_shortcut_controller_new(); gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(kc),gtk_shortcut_new(gtk_shortcut_trigger_parse_string("Escape"),gtk_named_action_new("window.close"))); gtk_widget_add_controller(h->w,kc); }
    gtk_window_present(GTK_WINDOW(h->w)); h4_run(0); gtk_widget_grab_focus(h->q); }
static gboolean jump_now(gpointer p){ GtkWidget *c; (void)p; if(!jump_id[0]) return G_SOURCE_REMOVE;
    for(c=gtk_widget_get_first_child(chat_box);c;c=gtk_widget_get_next_sibling(c)){ const char *id=g_object_get_data(G_OBJECT(c),"mid"); if(id && !strcmp(id,jump_id)){ double x=0,y=0; GtkAdjustment *a=gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(chat_scroll));
            G_GNUC_BEGIN_IGNORE_DEPRECATIONS gtk_widget_translate_coordinates(c,chat_box,0,0,&x,&y); G_GNUC_END_IGNORE_DEPRECATIONS gtk_adjustment_set_value(a,y>40?y-40:0); cls(c,"jumphl"); break; } }
    jump_id[0]=0; return G_SOURCE_REMOVE; }

/* ---- profiles ---- */
static GtkWidget *avatar_pic(const char *fp, const char *name, int px){ char p[600]; lsm_lock(C); lsm_avatar_path(C,fp,p,sizeof p); lsm_unlock(C);
    if(p[0]){ GdkTexture *t=gdk_texture_new_from_filename(p,NULL); if(t){ GtkWidget *a=adw_avatar_new(px,name,TRUE); adw_avatar_set_custom_image(ADW_AVATAR(a),GDK_PAINTABLE(t)); g_object_unref(t); return a; } }
    return adw_avatar_new(px,name,TRUE); }
static GtkWidget *prow(GtkWidget *grp, const char *title, const char *val){ GtkWidget *r=adw_action_row_new(); adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r),title); adw_action_row_set_subtitle(ADW_ACTION_ROW(r),val&&*val?val:"—"); adw_action_row_set_subtitle_selectable(ADW_ACTION_ROW(r),TRUE); adw_preferences_group_add(ADW_PREFERENCES_GROUP(grp),r); return r; }
static GtkWidget *pf_win, *pf_name, *pf_sig, *pf_av; static char pf_fp[FP_LEN];
static void pf_closed(GtkWindow *w, gpointer p){ (void)w;(void)p; pf_win=NULL; }
static GtkWidget *pf_shell(const char *title, GtkWidget **page){ GtkWidget *w=adw_window_new(), *tv=adw_toolbar_view_new(); gtk_window_set_title(GTK_WINDOW(w),title); gtk_window_set_transient_for(GTK_WINDOW(w),GTK_WINDOW(win)); gtk_window_set_modal(GTK_WINDOW(w),TRUE); gtk_window_set_default_size(GTK_WINDOW(w),480,640);
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv),adw_header_bar_new()); *page=adw_preferences_page_new(); adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tv),*page); adw_window_set_content(ADW_WINDOW(w),tv);
    { GtkEventController *kc=gtk_shortcut_controller_new(); gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(kc),gtk_shortcut_new(gtk_shortcut_trigger_parse_string("Escape"),gtk_named_action_new("window.close"))); gtk_widget_add_controller(w,kc); }
    g_signal_connect(w,"destroy",G_CALLBACK(pf_closed),NULL); return w; }
static void my_profile_dialog(void);
static void pf_save(void){ char err[256]=""; lsm_set_name(C,gtk_editable_get_text(GTK_EDITABLE(pf_name))); if(lsm_set_profile(C,gtk_editable_get_text(GTK_EDITABLE(pf_sig)),NULL,err)) info("保存失败",err); else toast("个人资料已保存"); list_dirty=chat_dirty=1; gtk_window_destroy(GTK_WINDOW(pf_win)); }
static void av_picked(GObject *src, GAsyncResult *res, gpointer p){ GFile *f=gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src),res,NULL); (void)p; if(!f) return;
    { char *path=g_file_get_path(f); GdkPixbuf *pb=path?gdk_pixbuf_new_from_file_at_scale(path,96,96,TRUE,NULL):NULL; char tmp[600], err[256]="";
      G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      if(pb){ snprintf(tmp,sizeof tmp,"%s/lsm-avatar-%d.png",g_get_tmp_dir(),(int)getpid()); if(gdk_pixbuf_save(pb,tmp,"png",NULL,NULL)){ if(lsm_set_profile(C,NULL,tmp,err)) info("头像",err); remove(tmp); } g_object_unref(pb); } else info("头像","无法读取图片。");
      G_GNUC_END_IGNORE_DEPRECATIONS
      g_free(path); } g_object_unref(f); if(pf_win){ gtk_window_destroy(GTK_WINDOW(pf_win)); my_profile_dialog(); } }
static void av_choose(void){ GtkFileDialog *d=gtk_file_dialog_new(); gtk_file_dialog_set_title(d,"选择头像"); gtk_file_dialog_open(d,GTK_WINDOW(pf_win),NULL,av_picked,NULL); g_object_unref(d); }
static void av_remove(void){ char err[256]; lsm_set_profile(C,NULL,"",err); gtk_window_destroy(GTK_WINDOW(pf_win)); my_profile_dialog(); }
static void my_profile_dialog(void){ GtkWidget *page, *grp, *bx, *b; char name[300], sg[400], fp[FP_LEN];
    if(pf_win) gtk_window_destroy(GTK_WINDOW(pf_win));
    lsm_lock(C); snprintf(name,sizeof name,"%s",lsm_my_name(C)); snprintf(sg,sizeof sg,"%s",lsm_my_signature(C)); snprintf(fp,sizeof fp,"%s",lsm_my_fp(C)); lsm_unlock(C);
    pf_win=pf_shell("个人资料",&page); grp=adw_preferences_group_new(); bx=gtk_box_new(GTK_ORIENTATION_VERTICAL,8); gtk_box_append(GTK_BOX(bx),avatar_pic(NULL,name,96));
    { GtkWidget *h=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8); gtk_widget_set_halign(h,GTK_ALIGN_CENTER); b=gtk_button_new_with_label("更换头像…"); g_signal_connect_swapped(b,"clicked",G_CALLBACK(av_choose),NULL); gtk_box_append(GTK_BOX(h),b); b=gtk_button_new_with_label("移除头像"); g_signal_connect_swapped(b,"clicked",G_CALLBACK(av_remove),NULL); gtk_box_append(GTK_BOX(h),b); gtk_box_append(GTK_BOX(bx),h); }
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(grp),bx); adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),ADW_PREFERENCES_GROUP(grp));
    grp=adw_preferences_group_new(); pf_name=adw_entry_row_new(); adw_preferences_row_set_title(ADW_PREFERENCES_ROW(pf_name),"昵称"); gtk_editable_set_text(GTK_EDITABLE(pf_name),name); adw_preferences_group_add(ADW_PREFERENCES_GROUP(grp),pf_name);
    pf_sig=adw_entry_row_new(); adw_preferences_row_set_title(ADW_PREFERENCES_ROW(pf_sig),"个性签名（最多 120 字）"); gtk_editable_set_text(GTK_EDITABLE(pf_sig),sg); adw_preferences_group_add(ADW_PREFERENCES_GROUP(grp),pf_sig);
    prow(grp,"指纹",fp); prow(grp,"版本",LSM_APP_VERSION); adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),ADW_PREFERENCES_GROUP(grp));
    grp=adw_preferences_group_new(); b=gtk_button_new_with_label("保存"); cls(b,"suggested-action"); cls(b,"pill"); gtk_widget_set_halign(b,GTK_ALIGN_CENTER); g_signal_connect_swapped(b,"clicked",G_CALLBACK(pf_save),NULL); adw_preferences_group_add(ADW_PREFERENCES_GROUP(grp),b); adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),ADW_PREFERENCES_GROUP(grp));
    gtk_window_present(GTK_WINDOW(pf_win)); }
static void cp_save(void){ lsm_set_remark(C,pf_fp,gtk_editable_get_text(GTK_EDITABLE(pf_name))); toast("备注名已保存"); list_dirty=chat_dirty=1; }
static void cp_send(void){ char cv[64]; snprintf(cv,sizeof cv,"dm:%s",pf_fp); gtk_window_destroy(GTK_WINDOW(pf_win)); select_conv(cv); gtk_widget_grab_focus(input); }
static void cp_hist(void){ gtk_window_destroy(GTK_WINDOW(pf_win)); history_dialog(); }
static void cp_file(GtkListBox *lb, GtkListBoxRow *row, gpointer p){ const char *path=g_object_get_data(G_OBJECT(row),"path"); (void)lb;(void)p; if(path) open_path_safe(path); }
static void contact_profile_dialog(void){ GtkWidget *page, *grp, *bx, *b, *lst; lsm_contact *k; lsm_msg *m; int n,i,nf=0; char conv[64], nick[300]="", rem[200]="", sig[400]="", ver[64]="", tr[40]="", on[80]="";
    if(strncmp(cur_conv,"dm:",3)){ info("联系人资料","请先选择一个联系人。"); return; } snprintf(pf_fp,sizeof pf_fp,"%s",cur_conv+3); snprintf(conv,sizeof conv,"%s",cur_conv);
    if(pf_win) gtk_window_destroy(GTK_WINDOW(pf_win));
    lsm_lock(C); n=lsm_contacts(C,&k); for(i=0;i<n;i++) if(!strcmp(k[i].fp,pf_fp)){ snprintf(nick,sizeof nick,"%s",k[i].name); snprintf(rem,sizeof rem,"%s",k[i].remark); snprintf(sig,sizeof sig,"%s",k[i].signature[0]?k[i].signature:"（未设置或对方版本较旧）"); snprintf(ver,sizeof ver,"%s",k[i].app_version);
        snprintf(tr,sizeof tr,"%s",k[i].trust==TRUST_TRUSTED?"已验证身份":k[i].trust==TRUST_BLOCKED?"已屏蔽":"未验证"); snprintf(on,sizeof on,"%s%s",k[i].online?"在线":"离线",k[i].online?(VIA_RELAY(k[i])?" · 经服务器中转":" · 局域网直连"):""); } lsm_unlock(C);
    pf_win=pf_shell("联系人资料",&page); grp=adw_preferences_group_new(); bx=gtk_box_new(GTK_ORIENTATION_VERTICAL,8); gtk_box_append(GTK_BOX(bx),avatar_pic(pf_fp,rem[0]?rem:nick,96)); gtk_box_append(GTK_BOX(bx),label(rem[0]?rem:nick,"title-2")); gtk_widget_set_halign(gtk_widget_get_last_child(bx),GTK_ALIGN_CENTER);
    { GtkWidget *h=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8); gtk_widget_set_halign(h,GTK_ALIGN_CENTER); b=gtk_button_new_with_label("发消息"); cls(b,"suggested-action"); cls(b,"pill"); g_signal_connect_swapped(b,"clicked",G_CALLBACK(cp_send),NULL); gtk_box_append(GTK_BOX(h),b); b=gtk_button_new_with_label("消息历史"); cls(b,"pill"); g_signal_connect_swapped(b,"clicked",G_CALLBACK(cp_hist),NULL); gtk_box_append(GTK_BOX(h),b); gtk_box_append(GTK_BOX(bx),h); }
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(grp),bx); adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),ADW_PREFERENCES_GROUP(grp));
    grp=adw_preferences_group_new(); pf_name=adw_entry_row_new(); adw_preferences_row_set_title(ADW_PREFERENCES_ROW(pf_name),"备注名（仅本机）"); gtk_editable_set_text(GTK_EDITABLE(pf_name),rem); adw_entry_row_set_show_apply_button(ADW_ENTRY_ROW(pf_name),TRUE); g_signal_connect_swapped(pf_name,"apply",G_CALLBACK(cp_save),NULL); adw_preferences_group_add(ADW_PREFERENCES_GROUP(grp),pf_name);
    prow(grp,"昵称",nick); prow(grp,"个性签名",sig); prow(grp,"状态",on); prow(grp,"身份",tr); prow(grp,"版本",ver); prow(grp,"指纹",pf_fp); adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),ADW_PREFERENCES_GROUP(grp));
    grp=adw_preferences_group_new(); adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(grp),"共享的文件与图片"); lst=gtk_list_box_new(); cls(lst,"boxed-list"); gtk_list_box_set_selection_mode(GTK_LIST_BOX(lst),GTK_SELECTION_NONE);
    lsm_lock(C); n=lsm_messages(C,&m); for(i=n-1;i>=0&&nf<100;i--) if(m[i].kind==K_FILE && !strcmp(m[i].conv,conv) && !lsm_is_sticker_name(m[i].file_name)){ GtkWidget *r=adw_action_row_new(); adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r),m[i].file_name); adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(r),FALSE); adw_action_row_set_subtitle(ADW_ACTION_ROW(r),lsm_msg_type(&m[i])==LSM_T_IMAGE?"图片":(m[i].file_path[0]?"文件":"文件（未接收）")); if(m[i].file_path[0]){ g_object_set_data_full(G_OBJECT(r),"path",g_strdup(m[i].file_path),g_free); gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(r),TRUE); } gtk_list_box_append(GTK_LIST_BOX(lst),r); nf++; }
    lsm_unlock(C); if(!nf){ GtkWidget *r=adw_action_row_new(); adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r),"暂无"); gtk_list_box_append(GTK_LIST_BOX(lst),r); }
    g_signal_connect(lst,"row-activated",G_CALLBACK(cp_file),NULL); adw_preferences_group_add(ADW_PREFERENCES_GROUP(grp),lst); adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),ADW_PREFERENCES_GROUP(grp));
    gtk_window_present(GTK_WINDOW(pf_win)); }
