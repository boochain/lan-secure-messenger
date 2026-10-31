/* v0.6.1 P0: 消息历史（搜索+筛选+跳转+分页）、个人资料、联系人资料与备注名。Included by gtk_gui.c. */
/* jump_id declared in gtk_gui.c */
typedef struct { GtkWidget *d,*q,*conv,*type,*dir,*st,*from,*to,*list,*more,*count; int offset; } hist_ui;
static int64_t parse_day(const char *s, int end){ struct tm t; memset(&t,0,sizeof t); if(!s||sscanf(s,"%d-%d-%d",&t.tm_year,&t.tm_mon,&t.tm_mday)!=3) return 0; t.tm_year-=1900; t.tm_mon-=1; if(end) t.tm_mday+=1; t.tm_isdst=-1; return (int64_t)mktime(&t)*1000; }
static void hist_row_act(GtkListBox *lb, GtkListBoxRow *row, gpointer p){ hist_ui *h=p; const char *cv=g_object_get_data(G_OBJECT(row),"conv"), *id=g_object_get_data(G_OBJECT(row),"id"); (void)lb;
    if(!cv) return; snprintf(cur_conv,sizeof cur_conv,"%s",cv); snprintf(jump_id,sizeof jump_id,"%s",id); refresh_list(); refresh_chat(); gtk_widget_destroy(h->d); }
static void hist_run(hist_ui *h, int append){ lsm_hq q; int idx[50], n, tot=0, i; lsm_msg *m; char cnt[100]; static const int TM[]={0,1<<LSM_T_TEXT,1<<LSM_T_IMAGE,1<<LSM_T_FILE,1<<LSM_T_STICKER}; static const int SM[]={0,1<<ST_READ,(1<<ST_FAILED)|(1<<ST_REJECTED),1<<ST_REJECTED};
    const char *cv=gtk_combo_box_get_active_id(GTK_COMBO_BOX(h->conv));
    if(!append){ GList *ch=gtk_container_get_children(GTK_CONTAINER(h->list)),*it; for(it=ch;it;it=it->next) gtk_widget_destroy(GTK_WIDGET(it->data)); g_list_free(ch); h->offset=0; }
    memset(&q,0,sizeof q); q.query=gtk_entry_get_text(GTK_ENTRY(h->q)); q.convs=(cv&&strcmp(cv,"*"))?cv:NULL;
    q.types=TM[MAX(0,gtk_combo_box_get_active(GTK_COMBO_BOX(h->type)))]; q.direction=MAX(0,gtk_combo_box_get_active(GTK_COMBO_BOX(h->dir))); q.statuses=SM[MAX(0,gtk_combo_box_get_active(GTK_COMBO_BOX(h->st)))];
    q.from_ms=parse_day(gtk_entry_get_text(GTK_ENTRY(h->from)),0); q.to_ms=parse_day(gtk_entry_get_text(GTK_ENTRY(h->to)),1); q.offset=h->offset; q.limit=50;
    lsm_lock(C); n=lsm_history_query(C,&q,idx,50,&tot); lsm_messages(C,&m);
    for(i=0;i<n;i++){ lsm_msg *x=&m[idx[i]]; char ts[32], t[1400]; time_t tt=(time_t)(x->ts_ms/1000); struct tm tmv; gchar *who, *b; GtkWidget *row=gtk_list_box_row_new(), *l=gtk_label_new(NULL);
        localtime_r(&tt,&tmv); strftime(ts,sizeof ts,"%Y-%m-%d %H:%M",&tmv); who=g_markup_escape_text(x->out?"我":x->sender_name,-1);
        { char body[300]; snprintf(body,sizeof body,"%s",x->kind==K_FILE?x->file_name:(x->body?x->body:"")); { int k; for(k=0;body[k];k++) if(body[k]=='\n') body[k]=' '; } b=g_markup_escape_text(body,-1); }
        snprintf(t,sizeof t,"<small>%s · %s · %s</small>\n%s",ts,who,status_txt(x->status),b); g_free(who); g_free(b);
        gtk_label_set_markup(GTK_LABEL(l),t); gtk_label_set_xalign(GTK_LABEL(l),0); gtk_label_set_ellipsize(GTK_LABEL(l),PANGO_ELLIPSIZE_END); gtk_widget_set_margin_start(l,8); gtk_widget_set_margin_top(l,4); gtk_widget_set_margin_bottom(l,4);
        gtk_container_add(GTK_CONTAINER(row),l); g_object_set_data_full(G_OBJECT(row),"conv",g_strdup(x->conv),g_free); g_object_set_data_full(G_OBJECT(row),"id",g_strdup(x->id),g_free); gtk_list_box_insert(GTK_LIST_BOX(h->list),row,-1); }
    lsm_unlock(C);
    h->offset+=n; snprintf(cnt,sizeof cnt,"共 %d 条，已显示 %d 条（双击跳转到聊天）",tot,h->offset); gtk_label_set_text(GTK_LABEL(h->count),cnt);
    gtk_widget_set_sensitive(h->more,h->offset<tot); gtk_widget_show_all(h->list); }
static void hist_search_cb(GtkWidget *w, gpointer p){ (void)w; hist_run(p,0); }
static void hist_more_cb(GtkWidget *w, gpointer p){ (void)w; hist_run(p,1); }
static void combo(GtkWidget **c, const char **items){ int i; *c=gtk_combo_box_text_new(); for(i=0;items[i];i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(*c),items[i]); gtk_combo_box_set_active(GTK_COMBO_BOX(*c),0); }
static void history_dialog(void){ static hist_ui H; hist_ui *h=&H; GtkWidget *box, *g, *sc, *btn; lsm_contact *k; lsm_group *gr; int n,i;
    static const char *TY[]={"全部类型","文字","图片","文件","贴纸",NULL}, *DI[]={"收发","收到的","发出的",NULL}, *ST[]={"全部状态","已读","失败/拒收","已拒收",NULL};
    memset(h,0,sizeof *h); h->d=gtk_dialog_new_with_buttons("消息历史",GTK_WINDOW(win),GTK_DIALOG_DESTROY_WITH_PARENT,"关闭",GTK_RESPONSE_CLOSE,NULL); gtk_window_set_default_size(GTK_WINDOW(h->d),640,560);
    box=gtk_dialog_get_content_area(GTK_DIALOG(h->d)); gtk_container_set_border_width(GTK_CONTAINER(box),8); g=gtk_grid_new(); gtk_grid_set_row_spacing(GTK_GRID(g),6); gtk_grid_set_column_spacing(GTK_GRID(g),6);
    h->q=gtk_entry_new(); gtk_entry_set_icon_from_icon_name(GTK_ENTRY(h->q),GTK_ENTRY_ICON_PRIMARY,"edit-find-symbolic"); gtk_widget_set_hexpand(h->q,TRUE); atk_object_set_name(gtk_widget_get_accessible(h->q),"搜索消息");
    h->conv=gtk_combo_box_text_new(); gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(h->conv),"*","全部会话");
    lsm_lock(C); n=lsm_contacts(C,&k); for(i=0;i<n;i++){ char cv[64]; snprintf(cv,64,"dm:%s",k[i].fp); gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(h->conv),cv,lsm_display_name(&k[i])); }
    n=lsm_groups(C,&gr); for(i=0;i<n;i++){ char u[37], cv[64], nm[400]; lsm_uuid_str(gr[i].id,u); snprintf(cv,64,"group:%s",u); snprintf(nm,sizeof nm,"# %s",gr[i].name); gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(h->conv),cv,nm); } lsm_unlock(C);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(h->conv),cur_conv[0]?cur_conv:"*"); if(gtk_combo_box_get_active(GTK_COMBO_BOX(h->conv))<0) gtk_combo_box_set_active(GTK_COMBO_BOX(h->conv),0);
    combo(&h->type,TY); combo(&h->dir,DI); combo(&h->st,ST); h->from=gtk_entry_new(); h->to=gtk_entry_new(); gtk_entry_set_width_chars(GTK_ENTRY(h->from),11); gtk_entry_set_width_chars(GTK_ENTRY(h->to),11);
    gtk_widget_set_tooltip_text(h->from,"起始日期 YYYY-MM-DD（可空）"); gtk_widget_set_tooltip_text(h->to,"结束日期 YYYY-MM-DD（含当天，可空）");
    btn=gtk_button_new_with_label("搜索"); gtk_style_context_add_class(gtk_widget_get_style_context(btn),"suggested-action");
    gtk_grid_attach(GTK_GRID(g),h->q,0,0,3,1); gtk_grid_attach(GTK_GRID(g),btn,3,0,1,1);
    gtk_grid_attach(GTK_GRID(g),h->conv,0,1,1,1); gtk_grid_attach(GTK_GRID(g),h->type,1,1,1,1); gtk_grid_attach(GTK_GRID(g),h->dir,2,1,1,1); gtk_grid_attach(GTK_GRID(g),h->st,3,1,1,1);
    gtk_grid_attach(GTK_GRID(g),gtk_label_new("日期"),0,2,1,1); gtk_grid_attach(GTK_GRID(g),h->from,1,2,1,1); gtk_grid_attach(GTK_GRID(g),gtk_label_new("至"),2,2,1,1); gtk_grid_attach(GTK_GRID(g),h->to,3,2,1,1);
    gtk_box_pack_start(GTK_BOX(box),g,FALSE,FALSE,0); h->count=gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(h->count),0); gtk_style_context_add_class(gtk_widget_get_style_context(h->count),"dim-label"); gtk_box_pack_start(GTK_BOX(box),h->count,FALSE,FALSE,4);
    sc=gtk_scrolled_window_new(NULL,NULL); h->list=gtk_list_box_new(); gtk_container_add(GTK_CONTAINER(sc),h->list); gtk_box_pack_start(GTK_BOX(box),sc,TRUE,TRUE,0);
    h->more=gtk_button_new_with_label("加载更多"); gtk_box_pack_start(GTK_BOX(box),h->more,FALSE,FALSE,4);
    g_signal_connect(btn,"clicked",G_CALLBACK(hist_search_cb),h); g_signal_connect(h->q,"activate",G_CALLBACK(hist_search_cb),h); g_signal_connect(h->more,"clicked",G_CALLBACK(hist_more_cb),h);
    g_signal_connect(h->conv,"changed",G_CALLBACK(hist_search_cb),h); g_signal_connect(h->type,"changed",G_CALLBACK(hist_search_cb),h); g_signal_connect(h->dir,"changed",G_CALLBACK(hist_search_cb),h); g_signal_connect(h->st,"changed",G_CALLBACK(hist_search_cb),h);
    g_signal_connect(h->list,"row-activated",G_CALLBACK(hist_row_act),h); g_signal_connect(h->d,"response",G_CALLBACK(gtk_widget_destroy),NULL);
    gtk_widget_show_all(h->d); hist_run(h,0); gtk_widget_grab_focus(h->q); }
/* scroll to and flash the message we jumped to (called after refresh_chat) */
static gboolean jump_later(gpointer p){ GList *ch, *it; (void)p; if(!jump_id[0]) return G_SOURCE_REMOVE; ch=gtk_container_get_children(GTK_CONTAINER(chat_box));
    for(it=ch;it;it=it->next){ const char *id=g_object_get_data(G_OBJECT(it->data),"mid"); if(id && !strcmp(id,jump_id)){ GtkAllocation a; GtkAdjustment *adj=gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(chat_scroll)); gtk_widget_get_allocation(GTK_WIDGET(it->data),&a); gtk_adjustment_set_value(adj,MAX(0,a.y-40)); gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(it->data)),"jumphl"); break; } }
    g_list_free(ch); jump_id[0]=0; return G_SOURCE_REMOVE; }

static GtkWidget *avatar_img(const char *fp, int px){ char p[600]; GdkPixbuf *pb=NULL; GtkWidget *w; lsm_lock(C); lsm_avatar_path(C,fp,p,sizeof p); lsm_unlock(C);
    if(p[0]) pb=gdk_pixbuf_new_from_file_at_scale(p,px,px,TRUE,NULL);
    if(pb){ w=gtk_image_new_from_pixbuf(pb); g_object_unref(pb); } else { w=gtk_image_new_from_icon_name("avatar-default-symbolic",GTK_ICON_SIZE_DIALOG); gtk_image_set_pixel_size(GTK_IMAGE(w),px); } return w; }
/* downscale to 96 px PNG so it fits the 48 KB wire limit */
static int avatar_shrink(const char *src, char *out, size_t n){ GdkPixbuf *pb=gdk_pixbuf_new_from_file_at_scale(src,96,96,TRUE,NULL); int ok; if(!pb) return 0; snprintf(out,n,"%s/lsm-avatar-%d.png",g_get_tmp_dir(),(int)getpid()); ok=gdk_pixbuf_save(pb,out,"png",NULL,NULL); g_object_unref(pb); return ok; }
static void kv(GtkWidget *g, int row, const char *k, const char *v){ GtkWidget *a=gtk_label_new(k), *b=gtk_label_new(v); gtk_label_set_xalign(GTK_LABEL(a),1); gtk_style_context_add_class(gtk_widget_get_style_context(a),"dim-label"); gtk_label_set_xalign(GTK_LABEL(b),0); gtk_label_set_selectable(GTK_LABEL(b),TRUE); gtk_label_set_line_wrap(GTK_LABEL(b),TRUE); gtk_grid_attach(GTK_GRID(g),a,0,row,1,1); gtk_grid_attach(GTK_GRID(g),b,1,row,1,1); }
static void my_profile_dialog(void){ GtkWidget *d, *box, *g, *nm, *sig; char name[300], sg[400], fp[FP_LEN]; int r;
    for(;;){ lsm_lock(C); snprintf(name,sizeof name,"%s",lsm_my_name(C)); snprintf(sg,sizeof sg,"%s",lsm_my_signature(C)); snprintf(fp,sizeof fp,"%s",lsm_my_fp(C)); lsm_unlock(C);
    d=gtk_dialog_new_with_buttons("个人资料",GTK_WINDOW(win),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"更换头像…",1,"移除头像",2,"取消",GTK_RESPONSE_CANCEL,"保存",GTK_RESPONSE_OK,NULL);
    box=gtk_dialog_get_content_area(GTK_DIALOG(d)); gtk_container_set_border_width(GTK_CONTAINER(box),12); gtk_box_pack_start(GTK_BOX(box),avatar_img(NULL,96),FALSE,FALSE,6);
    g=gtk_grid_new(); gtk_grid_set_row_spacing(GTK_GRID(g),6); gtk_grid_set_column_spacing(GTK_GRID(g),10); nm=gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(nm),name); sig=gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(sig),sg); gtk_entry_set_max_length(GTK_ENTRY(sig),120); gtk_widget_set_hexpand(sig,TRUE);
    gtk_grid_attach(GTK_GRID(g),gtk_label_new("昵称"),0,0,1,1); gtk_grid_attach(GTK_GRID(g),nm,1,0,1,1); gtk_grid_attach(GTK_GRID(g),gtk_label_new("个性签名"),0,1,1,1); gtk_grid_attach(GTK_GRID(g),sig,1,1,1,1);
    kv(g,2,"指纹",fp); kv(g,3,"本机",LSM_APP_VERSION); gtk_box_pack_start(GTK_BOX(box),g,FALSE,FALSE,0); gtk_widget_show_all(d); r=gtk_dialog_run(GTK_DIALOG(d));
    if(r==GTK_RESPONSE_OK){ char err[256]=""; lsm_set_name(C,gtk_entry_get_text(GTK_ENTRY(nm))); if(lsm_set_profile(C,gtk_entry_get_text(GTK_ENTRY(sig)),NULL,err)) msgbox(GTK_MESSAGE_WARNING,"%s",err); list_dirty=1; gtk_widget_destroy(d); return; }
    if(r==1){ GtkWidget *fc=gtk_file_chooser_dialog_new("选择头像",GTK_WINDOW(d),GTK_FILE_CHOOSER_ACTION_OPEN,"取消",GTK_RESPONSE_CANCEL,"选择",GTK_RESPONSE_ACCEPT,NULL); GtkFileFilter *ff=gtk_file_filter_new(); gtk_file_filter_add_pixbuf_formats(ff); gtk_file_filter_set_name(ff,"图片"); gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fc),ff);
        if(gtk_dialog_run(GTK_DIALOG(fc))==GTK_RESPONSE_ACCEPT){ char *f=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc)), sm[600], err[256]=""; if(f && avatar_shrink(f,sm,sizeof sm)){ if(lsm_set_profile(C,NULL,sm,err)) msgbox(GTK_MESSAGE_WARNING,"%s",err); remove(sm); } else msgbox(GTK_MESSAGE_WARNING,"无法读取图片。"); g_free(f); }
        gtk_widget_destroy(fc); }
    if(r==2){ char err[256]; lsm_set_profile(C,NULL,"",err); }
    gtk_widget_destroy(d); if(r!=1 && r!=2) return; } }
static gboolean shared_open(GtkListBox *lb, GtkListBoxRow *row, gpointer p){ const char *path=g_object_get_data(G_OBJECT(row),"path"); (void)lb;(void)p; if(path) open_path_default(path); return TRUE; }
static void contact_profile_dialog(void){ const char *fp0=sel_peer(); char fp[FP_LEN], nick[300]="", rem[200]="", sig[400]="", ver[64]="", tr[40]="", on[80]=""; GtkWidget *d, *box, *g, *re, *sc, *lst; lsm_contact *k; lsm_msg *m; int n,i,r, nfiles=0; char conv[64];
    if(!fp0) return; snprintf(fp,sizeof fp,"%s",fp0); snprintf(conv,sizeof conv,"dm:%s",fp);
    lsm_lock(C); n=lsm_contacts(C,&k); for(i=0;i<n;i++) if(!strcmp(k[i].fp,fp)){ snprintf(nick,sizeof nick,"%s",k[i].name); snprintf(rem,sizeof rem,"%s",k[i].remark); snprintf(sig,sizeof sig,"%s",k[i].signature[0]?k[i].signature:"（未设置或对方版本较旧）"); snprintf(ver,sizeof ver,"%s",k[i].app_version);
        snprintf(tr,sizeof tr,"%s",k[i].trust==TRUST_TRUSTED?"已信任":k[i].trust==TRUST_BLOCKED?"已屏蔽":"未信任"); snprintf(on,sizeof on,"%s%s",k[i].online?"在线":"离线",k[i].online?(k[i].via_relay?" · 经服务器":" · 局域网直连"):""); }
    lsm_unlock(C);
    d=gtk_dialog_new_with_buttons("联系人资料",GTK_WINDOW(win),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"消息历史",1,"关闭",GTK_RESPONSE_CANCEL,"发消息",2,"保存备注",GTK_RESPONSE_OK,NULL); gtk_window_set_default_size(GTK_WINDOW(d),480,520);
    box=gtk_dialog_get_content_area(GTK_DIALOG(d)); gtk_container_set_border_width(GTK_CONTAINER(box),12); gtk_box_pack_start(GTK_BOX(box),avatar_img(fp,96),FALSE,FALSE,6);
    g=gtk_grid_new(); gtk_grid_set_row_spacing(GTK_GRID(g),6); gtk_grid_set_column_spacing(GTK_GRID(g),10); re=gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(re),rem); gtk_widget_set_hexpand(re,TRUE); gtk_widget_set_tooltip_text(re,"仅本机可见；留空则显示对方昵称");
    gtk_grid_attach(GTK_GRID(g),gtk_label_new("备注名"),0,0,1,1); gtk_grid_attach(GTK_GRID(g),re,1,0,1,1); kv(g,1,"昵称",nick); kv(g,2,"个性签名",sig); kv(g,3,"指纹",fp); kv(g,4,"状态",on); kv(g,5,"信任",tr); kv(g,6,"版本",ver[0]?ver:"—");
    gtk_box_pack_start(GTK_BOX(box),g,FALSE,FALSE,0);
    { GtkWidget *l=gtk_label_new(NULL); gtk_label_set_markup(GTK_LABEL(l),"<b>共享的文件与图片</b>"); gtk_label_set_xalign(GTK_LABEL(l),0); gtk_box_pack_start(GTK_BOX(box),l,FALSE,FALSE,6); }
    sc=gtk_scrolled_window_new(NULL,NULL); lst=gtk_list_box_new(); gtk_container_add(GTK_CONTAINER(sc),lst); gtk_box_pack_start(GTK_BOX(box),sc,TRUE,TRUE,0);
    lsm_lock(C); n=lsm_messages(C,&m); for(i=n-1;i>=0 && nfiles<100;i--) if(m[i].kind==K_FILE && !strcmp(m[i].conv,conv) && !lsm_is_sticker_name(m[i].file_name)){ GtkWidget *row=gtk_list_box_row_new(), *l; char t[400]; snprintf(t,sizeof t,"%s %s%s",lsm_msg_type(&m[i])==LSM_T_IMAGE?"🖼":"📄",m[i].file_name,m[i].file_path[0]?"":"（未接收）"); l=gtk_label_new(t); gtk_label_set_xalign(GTK_LABEL(l),0); gtk_label_set_ellipsize(GTK_LABEL(l),PANGO_ELLIPSIZE_MIDDLE); gtk_container_add(GTK_CONTAINER(row),l); if(m[i].file_path[0]) g_object_set_data_full(G_OBJECT(row),"path",g_strdup(m[i].file_path),g_free); gtk_list_box_insert(GTK_LIST_BOX(lst),row,-1); nfiles++; }
    lsm_unlock(C); if(!nfiles){ GtkWidget *l=gtk_label_new("暂无"); gtk_style_context_add_class(gtk_widget_get_style_context(l),"dim-label"); gtk_list_box_insert(GTK_LIST_BOX(lst),l,-1); }
    g_signal_connect(lst,"row-activated",G_CALLBACK(shared_open),NULL);
    gtk_widget_show_all(d); r=gtk_dialog_run(GTK_DIALOG(d));
    if(r==GTK_RESPONSE_OK){ lsm_set_remark(C,fp,gtk_entry_get_text(GTK_ENTRY(re))); list_dirty=chat_dirty=1; }
    gtk_widget_destroy(d);
    if(r==1) history_dialog(); else if(r==2) gtk_widget_grab_focus(input); }
