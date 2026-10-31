/* v0.6.1 P2 (GTK3): pinned chats, drafts, pinned/starred messages, @mentions, typing indicator, forward, multi-select.
 * Included by gtk_gui.c after gtk_p1.inc. */
static void refresh_chat(void);
static GtkWidget *pin_bar, *pin_lbl; static char typing_conv[64], typing_name[128]; static gint64 typing_until; static guint draft_src;
/* sidebar: 📌 before pinned chats, "[草稿] …" instead of the status line when a draft is waiting (caller holds lsm_lock) */
static void p2_decorate(const char *conv, char *t, size_t n){ char d[300], tmp[1400], *nl; int pin=lsm_conv_pinned(C,conv), dr=lsm_get_draft(C,conv,d,sizeof d);
    if(!pin && !dr) return; snprintf(tmp,sizeof tmp,"%s%s",pin?"📌 ":"",t); nl=strchr(tmp,'\n');
    if(dr && nl){ gchar *e; int k; for(k=0;d[k];k++) if(d[k]=='\n') d[k]=' '; e=g_markup_escape_text(d,-1); snprintf(nl,sizeof tmp-(nl-tmp),"\n<small><span foreground=\"#c01c28\">[草稿]</span> %s</small>",e); g_free(e); }
    snprintf(t,n,"%s",tmp); }
static gint p2_sort(GtkListBoxRow *a, GtkListBoxRow *b, gpointer p){ int pa=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(a),"pin")), pb=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b),"pin")); (void)p;
    if(pa!=pb) return pb-pa; return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(a),"ord"))-GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b),"ord")); }
static void p2_mark_rows(void){ GList *ch=gtk_container_get_children(GTK_CONTAINER(peer_list)), *it; int i=0; /* caller holds lsm_lock */
    for(it=ch;it;it=it->next,i++){ const char *cv=g_object_get_data(G_OBJECT(it->data),"conv"); g_object_set_data(G_OBJECT(it->data),"ord",GINT_TO_POINTER(i)); g_object_set_data(G_OBJECT(it->data),"pin",GINT_TO_POINTER(cv?lsm_conv_pinned(C,cv):0)); }
    g_list_free(ch); gtk_list_box_set_sort_func(GTK_LIST_BOX(peer_list),p2_sort,NULL,NULL); }
/* drafts: saved (sealed) 1.5 s after the last keystroke and when switching chats */
static char *input_text(void){ GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(input)); GtkTextIter s,e; gtk_text_buffer_get_bounds(b,&s,&e); return gtk_text_buffer_get_text(b,&s,&e,FALSE); }
static void p2_save_draft(void){ char *t; if(draft_src){ g_source_remove(draft_src); draft_src=0; } if(!cur_conv[0]) return; t=input_text(); lsm_set_draft(C,cur_conv,t?t:""); g_free(t); }
static gboolean p2_draft_later(gpointer p){ (void)p; draft_src=0; p2_save_draft(); list_dirty=1; return G_SOURCE_REMOVE; }
static int p2_loading;
static void p2_load_draft(void){ char d[8192]=""; GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(input)); if(cur_conv[0]){ lsm_lock(C); lsm_get_draft(C,cur_conv,d,sizeof d); lsm_unlock(C); }
    p2_loading=1; gtk_text_buffer_set_text(b,d,-1); p2_loading=0; }
static void p2_input_changed(GtkTextBuffer *b, gpointer p){ (void)p; if(p2_loading||!cur_conv[0]) return;
    if(gtk_text_buffer_get_char_count(b)>0) lsm_send_typing(C,cur_conv); /* core throttles to one per 3 s, online CAP_EXT peers only */
    if(draft_src) g_source_remove(draft_src); draft_src=g_timeout_add(1500,p2_draft_later,NULL); }
/* typing indicator in the header subtitle, 5 s */
static gboolean p2_typing_expire(gpointer p){ (void)p; if(typing_conv[0] && g_get_monotonic_time()>=typing_until){ typing_conv[0]=0; chat_dirty=1; } return G_SOURCE_REMOVE; }
static void p2_on_typing(char *ev){ char *f[4]; int n=0; char *q=ev; f[n++]=q; while(*q&&n<4){ if(*q=='\t'){ *q=0; f[n++]=q+1; } q++; } if(n<4) return;
    snprintf(typing_conv,sizeof typing_conv,"%s",f[1]); snprintf(typing_name,sizeof typing_name,"%s",f[3]); typing_until=g_get_monotonic_time()+5000000;
    if(!strcmp(typing_conv,cur_conv)){ char s[300]; snprintf(s,sizeof s,"%s 正在输入…",typing_name); gtk_label_set_text(GTK_LABEL(sub_lbl),s); } g_timeout_add(5100,p2_typing_expire,NULL); }
static void p2_sub_typing(char *sub, size_t n){ if(typing_conv[0] && !strcmp(typing_conv,cur_conv) && g_get_monotonic_time()<typing_until) snprintf(sub,n,"%s 正在输入…",typing_name); }
/* meta-line tags */
static const char *p2_tags(const lsm_msg *m){ static char t[64]; snprintf(t,sizeof t,"%s%s%s%s",m->forwarded?" · ↪ 转发":"",m->pinned?" · 📌":"",m->starred?" · ⭐":"",m->mentions_me?" · @我":""); return t; }
/* ---- conversation picker (contacts that are not blocked + groups), multi-select; returns count ---- */
static int p2_pick_convs(const char *title, char out[][64], int max){ GtkWidget *d=gtk_dialog_new_with_buttons(title,GTK_WINDOW(win),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"取消",GTK_RESPONSE_CANCEL,"转发",GTK_RESPONSE_OK,NULL);
    GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(d)), *lb=gtk_list_box_new(), *sc=gtk_scrolled_window_new(NULL,NULL), *l=gtk_label_new("选择一个或多个会话（Ctrl/Shift 多选）："); lsm_contact *k; lsm_group *g; int n,i,cnt=0,res=0; char (*cv)[64]=g_malloc0(sizeof(char[64])*256);
    gtk_container_set_border_width(GTK_CONTAINER(box),12); gtk_box_set_spacing(GTK_BOX(box),8); gtk_label_set_xalign(GTK_LABEL(l),0); gtk_list_box_set_selection_mode(GTK_LIST_BOX(lb),GTK_SELECTION_MULTIPLE); gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(lb),FALSE);
    lsm_lock(C); n=lsm_contacts(C,&k); for(i=0;i<n&&cnt<256;i++){ GtkWidget *r; if(k[i].trust==TRUST_BLOCKED||!strcmp(k[i].fp,lsm_my_fp(C))) continue; r=gtk_label_new(lsm_display_name(&k[i])); gtk_label_set_xalign(GTK_LABEL(r),0); gtk_widget_set_margin_start(r,8); gtk_widget_set_margin_top(r,5); gtk_widget_set_margin_bottom(r,5); gtk_list_box_insert(GTK_LIST_BOX(lb),r,-1); snprintf(cv[cnt++],64,"dm:%s",k[i].fp); }
    n=lsm_groups(C,&g); for(i=0;i<n&&cnt<256;i++){ char u[37], nm[300]; GtkWidget *r; lsm_uuid_str(g[i].id,u); snprintf(nm,sizeof nm,"# %s",g[i].name); r=gtk_label_new(nm); gtk_label_set_xalign(GTK_LABEL(r),0); gtk_widget_set_margin_start(r,8); gtk_widget_set_margin_top(r,5); gtk_widget_set_margin_bottom(r,5); gtk_list_box_insert(GTK_LIST_BOX(lb),r,-1); snprintf(cv[cnt++],64,"group:%s",u); }
    lsm_unlock(C);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(sc),240); gtk_container_add(GTK_CONTAINER(sc),lb); gtk_box_pack_start(GTK_BOX(box),l,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),sc,TRUE,TRUE,0);
    gtk_window_set_default_size(GTK_WINDOW(d),420,400); gtk_widget_show_all(d);
    if(cnt && gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_OK){ GList *s=gtk_list_box_get_selected_rows(GTK_LIST_BOX(lb)), *it; for(it=s;it&&res<max;it=it->next) snprintf(out[res++],64,"%s",cv[gtk_list_box_row_get_index(GTK_LIST_BOX_ROW(it->data))]); g_list_free(s); }
    gtk_widget_destroy(d); g_free(cv); return res; }
static void p2_forward_ids(char **ids, int nid){ char to[64][64], err[256], nid_s[40]; int nt=p2_pick_convs(nid>1?"逐条转发":"转发消息",to,64), i, j, bad=0;
    for(j=0;j<nt;j++) for(i=0;i<nid;i++) if(lsm_forward(C,ids[i],to[j],nid_s,err)) bad++;
    if(nt && bad) msgbox(GTK_MESSAGE_WARNING,"%d 条转发失败：%s",bad,err); list_dirty=chat_dirty=1; }
static gboolean p2_forward_idle(gpointer id){ char *v[1]={(char*)id}; p2_forward_ids(v,1); g_free(id); return G_SOURCE_REMOVE; }
static void p2_forward_cb(GtkMenuItem *i, gpointer id){ (void)i; g_idle_add(p2_forward_idle,g_strdup((const char*)id)); }
static void p2_flag_cb(GtkMenuItem *i, gpointer id){ char err[256]; int w=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(i),"w")), on=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(i),"on"));
    if(lsm_set_msg_flags(C,(const char*)id,w==1?on:-1,w==2?on:-1,err)) msgbox(GTK_MESSAGE_WARNING,"%s",err); chat_dirty=1; }
static void p2_multi(void);
static gboolean p2_multi_idle(gpointer p){ (void)p; p2_multi(); return G_SOURCE_REMOVE; }
static void p2_multi_cb(GtkMenuItem *i, gpointer p){ (void)i; (void)p; g_idle_add(p2_multi_idle,NULL); }
static void p2_menu_items(GtkMenuShell *menu, const char *id){ lsm_msg *m; int n,i,pin=0,star=0,rec=1; GtkWidget *it;
    lsm_lock(C); n=lsm_messages(C,&m); for(i=0;i<n;i++) if(!strcmp(m[i].id,id)){ pin=m[i].pinned; star=m[i].starred; rec=m[i].recalled; break; } lsm_unlock(C);
    if(i==n || rec) return;
    p1_item(menu,"转发(_F)…",G_CALLBACK(p2_forward_cb),id);
    it=gtk_menu_item_new_with_mnemonic(pin?"取消置顶(_P)":"置顶(_P)"); g_object_set_data(G_OBJECT(it),"w",GINT_TO_POINTER(1)); g_object_set_data(G_OBJECT(it),"on",GINT_TO_POINTER(!pin)); g_signal_connect_data(it,"activate",G_CALLBACK(p2_flag_cb),g_strdup(id),(GClosureNotify)g_free,0); gtk_menu_shell_append(menu,it);
    it=gtk_menu_item_new_with_mnemonic(star?"取消收藏(_S)":"收藏(_S)"); g_object_set_data(G_OBJECT(it),"w",GINT_TO_POINTER(2)); g_object_set_data(G_OBJECT(it),"on",GINT_TO_POINTER(!star)); g_signal_connect_data(it,"activate",G_CALLBACK(p2_flag_cb),g_strdup(id),(GClosureNotify)g_free,0); gtk_menu_shell_append(menu,it);
    it=gtk_menu_item_new_with_mnemonic("多选(_M)…"); g_signal_connect(it,"activate",G_CALLBACK(p2_multi_cb),NULL); gtk_menu_shell_append(menu,it); }
/* ---- message list dialogs: pinned / starred (double-click jumps), multi-select (forward / star / delete) ---- */
static void p2_jump_act(GtkListBox *lb, GtkListBoxRow *row, gpointer d){ const char *cv=g_object_get_data(G_OBJECT(row),"conv"), *id=g_object_get_data(G_OBJECT(row),"id"); (void)lb;
    if(!cv) return; p2_save_draft(); snprintf(cur_conv,sizeof cur_conv,"%s",cv); snprintf(jump_id,sizeof jump_id,"%s",id); p2_load_draft(); refresh_list(); refresh_chat(); gtk_widget_destroy(GTK_WIDGET(d)); }
static void p2_msg_row(GtkWidget *lb, const lsm_msg *x){ char ts[32], t[1400], body[300], db[300]; time_t tt=(time_t)(x->ts_ms/1000); struct tm tmv; gchar *who, *b; int k; GtkWidget *row=gtk_list_box_row_new(), *l=gtk_label_new(NULL);
    localtime_r(&tt,&tmv); strftime(ts,sizeof ts,"%Y-%m-%d %H:%M",&tmv); who=g_markup_escape_text(x->out?"我":x->sender_name,-1);
    snprintf(body,sizeof body,"%s",x->kind==K_FILE?x->file_name:lsm_display_body(x,db,sizeof db)); for(k=0;body[k];k++) if(body[k]=='\n') body[k]=' '; b=g_markup_escape_text(body,-1);
    snprintf(t,sizeof t,"<small>%s · %s%s</small>\n%s",ts,who,p2_tags(x),b); g_free(who); g_free(b);
    gtk_label_set_markup(GTK_LABEL(l),t); gtk_label_set_xalign(GTK_LABEL(l),0); gtk_label_set_ellipsize(GTK_LABEL(l),PANGO_ELLIPSIZE_END); gtk_widget_set_margin_start(l,8); gtk_widget_set_margin_top(l,4); gtk_widget_set_margin_bottom(l,4);
    gtk_container_add(GTK_CONTAINER(row),l); g_object_set_data_full(G_OBJECT(row),"conv",g_strdup(x->conv),g_free); g_object_set_data_full(G_OBJECT(row),"id",g_strdup(x->id),g_free); gtk_list_box_insert(GTK_LIST_BOX(lb),row,-1); }
static void p2_flag_dialog(int which, const char *conv){ GtkWidget *d=gtk_dialog_new_with_buttons(which==1?"置顶消息":"收藏的消息",GTK_WINDOW(win),GTK_DIALOG_DESTROY_WITH_PARENT,"关闭",GTK_RESPONSE_CLOSE,NULL), *box=gtk_dialog_get_content_area(GTK_DIALOG(d)), *sc=gtk_scrolled_window_new(NULL,NULL), *lb=gtk_list_box_new(), *l;
    int idx[500], n, i; lsm_msg *m; char cnt[120];
    lsm_lock(C); n=lsm_flagged(C,conv,which,idx,500); lsm_messages(C,&m); for(i=0;i<n;i++) p2_msg_row(lb,&m[idx[i]]); lsm_unlock(C);
    if(n) snprintf(cnt,sizeof cnt,"共 %d 条（双击跳转到聊天）",n); else snprintf(cnt,sizeof cnt,"还没有%s的消息。",which==1?"置顶":"收藏");
    l=gtk_label_new(cnt); gtk_label_set_xalign(GTK_LABEL(l),0); gtk_style_context_add_class(gtk_widget_get_style_context(l),"dim-label");
    gtk_container_set_border_width(GTK_CONTAINER(box),8); gtk_box_pack_start(GTK_BOX(box),l,FALSE,FALSE,4); gtk_container_add(GTK_CONTAINER(sc),lb); gtk_box_pack_start(GTK_BOX(box),sc,TRUE,TRUE,0);
    g_signal_connect(lb,"row-activated",G_CALLBACK(p2_jump_act),d); g_signal_connect(d,"response",G_CALLBACK(gtk_widget_destroy),NULL); gtk_window_set_default_size(GTK_WINDOW(d),560,460); gtk_widget_show_all(d); }
enum { MS_FWD=10, MS_STAR, MS_DEL };
static void p2_multi(void){ GtkWidget *d, *box, *sc, *lb, *l; lsm_msg *m; int n,i,r; if(!cur_conv[0]) return;
    d=gtk_dialog_new_with_buttons("多选消息",GTK_WINDOW(win),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"取消",GTK_RESPONSE_CANCEL,"删除（仅本机）…",MS_DEL,"收藏",MS_STAR,"逐条转发…",MS_FWD,NULL);
    box=gtk_dialog_get_content_area(GTK_DIALOG(d)); sc=gtk_scrolled_window_new(NULL,NULL); lb=gtk_list_box_new(); l=gtk_label_new("按住 Ctrl 或 Shift 选择多条消息，然后选择操作："); gtk_label_set_xalign(GTK_LABEL(l),0);
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(lb),GTK_SELECTION_MULTIPLE); gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(lb),FALSE);
    lsm_lock(C); n=lsm_messages(C,&m); for(i=0;i<n;i++) if(!strcmp(m[i].conv,cur_conv) && m[i].kind!=K_SYSTEM && !m[i].recalled) p2_msg_row(lb,&m[i]); lsm_unlock(C);
    gtk_container_set_border_width(GTK_CONTAINER(box),8); gtk_box_pack_start(GTK_BOX(box),l,FALSE,FALSE,4); gtk_container_add(GTK_CONTAINER(sc),lb); gtk_box_pack_start(GTK_BOX(box),sc,TRUE,TRUE,0);
    gtk_window_set_default_size(GTK_WINDOW(d),600,520); gtk_widget_show_all(d);
    { GList *ch=gtk_container_get_children(GTK_CONTAINER(lb)); GList *last=g_list_last(ch); if(last) gtk_list_box_select_row(GTK_LIST_BOX(lb),GTK_LIST_BOX_ROW(last->data)); g_list_free(ch); }
    r=gtk_dialog_run(GTK_DIALOG(d));
    if(r==MS_FWD||r==MS_STAR||r==MS_DEL){ GList *s=gtk_list_box_get_selected_rows(GTK_LIST_BOX(lb)), *it; int k=0, cnt=g_list_length(s); char **ids=g_new0(char*,cnt+1), err[256];
        for(it=s;it;it=it->next) ids[k++]=g_strdup(g_object_get_data(G_OBJECT(it->data),"id")); g_list_free(s); gtk_widget_destroy(d); d=NULL;
        if(r==MS_FWD && k) p2_forward_ids(ids,k);
        else if(r==MS_STAR) for(i=0;i<k;i++) lsm_set_msg_flags(C,ids[i],-1,1,err);
        else if(r==MS_DEL && k){ char q[120]; snprintf(q,sizeof q,"删除选中的 %d 条消息？",k); if(confirm(q,"只会从本机删除，对方仍保留。此操作无法撤销。","删除")) for(i=0;i<k;i++) lsm_delete_message(C,ids[i]); }
        g_strfreev(ids); chat_dirty=1; }
    if(d) gtk_widget_destroy(d); }
/* pinned-message banner above the chat: newest pinned message; click opens the list */
static void p2_pin_banner(void){ int idx[1], n=0; lsm_msg *m; char t[400]="";
    if(!pin_bar) return; if(cur_conv[0]){ lsm_lock(C); n=lsm_flagged(C,cur_conv,1,idx,1); if(n){ char db[300]; lsm_messages(C,&m); snprintf(t,sizeof t,"📌 置顶：%.160s",m[idx[0]].kind==K_FILE?m[idx[0]].file_name:lsm_display_body(&m[idx[0]],db,sizeof db)); { int k; for(k=0;t[k];k++) if(t[k]=='\n') t[k]=' '; } } lsm_unlock(C); }
    if(!n){ gtk_widget_hide(pin_bar); return; } gtk_button_set_label(GTK_BUTTON(pin_lbl),t); gtk_widget_show(pin_bar); gtk_widget_show(pin_lbl); }
static void p2_pin_click(GtkButton *b, gpointer p){ (void)b; (void)p; p2_flag_dialog(1,cur_conv); }
static GtkWidget *p2_pin_bar_new(void){ pin_bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0); pin_lbl=gtk_button_new_with_label(""); gtk_button_set_relief(GTK_BUTTON(pin_lbl),GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(pin_lbl,"查看本会话的置顶消息"); gtk_widget_set_halign(pin_lbl,GTK_ALIGN_START); { GtkWidget *c=gtk_bin_get_child(GTK_BIN(pin_lbl)); if(GTK_IS_LABEL(c)) gtk_label_set_ellipsize(GTK_LABEL(c),PANGO_ELLIPSIZE_END); }
    g_signal_connect(pin_lbl,"clicked",G_CALLBACK(p2_pin_click),NULL); gtk_box_pack_start(GTK_BOX(pin_bar),pin_lbl,TRUE,TRUE,0); gtk_style_context_add_class(gtk_widget_get_style_context(pin_bar),"quote"); gtk_widget_set_no_show_all(pin_bar,TRUE); return pin_bar; }
/* ---- @mentions: typing "@" in a group offers its members ---- */
static void p2_at_pick(GtkMenuItem *i, gpointer nm){ GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(input)); char t[140]; (void)i; snprintf(t,sizeof t,"%s ",(const char*)nm); gtk_text_buffer_insert_at_cursor(b,t,-1); gtk_widget_grab_focus(input); }
static gboolean p2_at_popup(gpointer p){ char nm[64][128], fp[64][FP_LEN]; int n,i; GtkWidget *menu; (void)p; lsm_lock(C); n=lsm_mention_candidates(C,cur_conv,nm,fp,64); lsm_unlock(C); if(!n) return G_SOURCE_REMOVE;
    menu=gtk_menu_new(); for(i=0;i<n;i++){ GtkWidget *it=gtk_menu_item_new_with_label(nm[i]); g_signal_connect_data(it,"activate",G_CALLBACK(p2_at_pick),g_strdup(nm[i]),(GClosureNotify)g_free,0); gtk_menu_shell_append(GTK_MENU_SHELL(menu),it); }
    gtk_menu_attach_to_widget(GTK_MENU(menu),input,NULL); g_signal_connect(menu,"selection-done",G_CALLBACK(gtk_widget_destroy),NULL); gtk_widget_show_all(menu);
    gtk_menu_popup_at_widget(GTK_MENU(menu),input,GDK_GRAVITY_NORTH_WEST,GDK_GRAVITY_SOUTH_WEST,NULL); if(getenv("LSM_UI_TEST")) fprintf(stderr,"UITEST at-menu %d\n",n); return G_SOURCE_REMOVE; }
static void p2_insert_text(GtkTextBuffer *b, GtkTextIter *loc, gchar *text, gint len, gpointer p){ (void)b; (void)loc; (void)p; if(!p2_loading && len==1 && text[0]=='@' && !strncmp(cur_conv,"group:",6)) g_idle_add(p2_at_popup,NULL); }
/* send: reply and/or mentions go through lsm_send_text2 */
static int p2_send_text(const char *t, char *id, char *err){ char csv[64*(FP_LEN+1)]; lsm_lock(C); lsm_mentions_in(C,cur_conv,t,csv,sizeof csv); lsm_unlock(C);
    if(reply_id[0]||csv[0]) return lsm_send_text2(C,cur_conv,t,reply_id[0]?reply_id:NULL,csv[0]?csv:NULL,id,err); return lsm_send_text(C,cur_conv,t,id,err); }
static void p2_toggle_conv_pin(void){ int on; if(!cur_conv[0]) return; lsm_lock(C); on=!lsm_conv_pinned(C,cur_conv); lsm_unlock(C); lsm_set_conv_pinned(C,cur_conv,on); list_dirty=1; }
