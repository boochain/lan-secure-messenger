/* v0.6.1 P2 (GTK4/libadwaita): pinned chats, drafts, pinned/starred messages, @mentions, typing indicator, forward, multi-select.
 * Included by adw_gui.c after adw_p1.inc. */
static void select_conv(const char *cv);
static GtkWidget *pin_bar, *pin_btn; static char typing_conv[64], typing_name[128]; static gint64 typing_until; static guint draft_src; static int p2_loading;
static const char *p2_tags(const lsm_msg *m){ static char t[64]; snprintf(t,sizeof t,"%s%s%s%s",m->forwarded?" · ↪ 转发":"",m->pinned?" · 📌":"",m->starred?" · ⭐":"",m->mentions_me?" · @我":""); return t; }
/* drafts (sealed by the core), saved 1.5 s after typing stops and when switching chats */
static char *input_text(void){ GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(input)); GtkTextIter s,e; gtk_text_buffer_get_bounds(b,&s,&e); return gtk_text_buffer_get_text(b,&s,&e,FALSE); }
static void p2_save_draft(void){ char *t; if(draft_src){ g_source_remove(draft_src); draft_src=0; } if(!cur_conv[0]||p1_mode==2) return; t=input_text(); lsm_set_draft(C,cur_conv,t?t:""); g_free(t); }
static gboolean p2_draft_later(gpointer p){ (void)p; draft_src=0; p2_save_draft(); list_dirty=1; return G_SOURCE_REMOVE; }
static void p2_load_draft(void){ char d[8192]=""; if(cur_conv[0]){ lsm_lock(C); lsm_get_draft(C,cur_conv,d,sizeof d); lsm_unlock(C); } p2_loading=1; gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(input)),d,-1); p2_loading=0; }
static void p2_clear_input(void){ p2_loading=1; gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(input)),"",-1); p2_loading=0; if(cur_conv[0]) lsm_set_draft(C,cur_conv,""); list_dirty=1; }
static void p2_input_changed(GtkTextBuffer *b, gpointer p){ (void)p; if(p2_loading||!cur_conv[0]||p1_mode==2) return;
    if(gtk_text_buffer_get_char_count(b)>0) lsm_send_typing(C,cur_conv);
    if(draft_src) g_source_remove(draft_src); draft_src=g_timeout_add(1500,p2_draft_later,NULL); }
/* typing indicator in the header subtitle for 5 s */
static gboolean p2_typing_expire(gpointer p){ (void)p; if(typing_conv[0] && g_get_monotonic_time()>=typing_until){ typing_conv[0]=0; chat_dirty=1; } return G_SOURCE_REMOVE; }
static void p2_on_typing(char *ev){ char *f[4]; int n=0; char *q=ev; f[n++]=q; while(*q&&n<4){ if(*q=='\t'){ *q=0; f[n++]=q+1; } q++; } if(n<4) return;
    snprintf(typing_conv,sizeof typing_conv,"%s",f[1]); snprintf(typing_name,sizeof typing_name,"%s",f[3]); typing_until=g_get_monotonic_time()+5000000;
    if(!strcmp(typing_conv,cur_conv)){ char s[300]; snprintf(s,sizeof s,"%s 正在输入…",typing_name); gtk_label_set_text(GTK_LABEL(sub_lbl),s); } g_timeout_add(5100,p2_typing_expire,NULL); }
static void p2_sub_typing(char *sub, size_t n){ if(typing_conv[0] && !strcmp(typing_conv,cur_conv) && g_get_monotonic_time()<typing_until) snprintf(sub,n,"%s 正在输入…",typing_name); }
/* ---- small list window used by forward picker, pinned/starred lists and multi-select ---- */
typedef struct { GtkWidget *w, *lb; int kind; char **ids; } p2win; /* kind 1 pinned/starred list, 2 forward picker, 3 multi-select */

static void p2win_free(gpointer p){ p2win *x=p; g_strfreev(x->ids); g_free(x); }
static GtkWidget *p2_row(const char *markup, const char *conv, const char *id){ GtkWidget *row=gtk_list_box_row_new(), *l=gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(l),markup); gtk_label_set_xalign(GTK_LABEL(l),0); gtk_label_set_ellipsize(GTK_LABEL(l),PANGO_ELLIPSIZE_END); gtk_widget_set_margin_start(l,10); gtk_widget_set_margin_end(l,10); gtk_widget_set_margin_top(l,6); gtk_widget_set_margin_bottom(l,6);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row),l); if(conv) g_object_set_data_full(G_OBJECT(row),"conv",g_strdup(conv),g_free); if(id) g_object_set_data_full(G_OBJECT(row),"id",g_strdup(id),g_free); return row; }
static GtkWidget *p2_msg_row(const lsm_msg *x){ char ts[32], t[1400], body[300], db[300]; time_t tt=(time_t)(x->ts_ms/1000); struct tm tmv; gchar *who, *b; int k;
    localtime_r(&tt,&tmv); strftime(ts,sizeof ts,"%Y-%m-%d %H:%M",&tmv); who=g_markup_escape_text(x->out?"我":x->sender_name,-1);
    snprintf(body,sizeof body,"%s",x->kind==K_FILE?x->file_name:lsm_display_body(x,db,sizeof db)); for(k=0;body[k];k++) if(body[k]=='\n') body[k]=' '; b=g_markup_escape_text(body,-1);
    snprintf(t,sizeof t,"<small>%s · %s%s</small>\n%s",ts,who,p2_tags(x),b); g_free(who); g_free(b); return p2_row(t,x->conv,x->id); }
static p2win *p2_window(const char *title, int kind, GtkSelectionMode mode, GtkWidget **header){ p2win *x=g_new0(p2win,1); GtkWidget *tv=adw_toolbar_view_new(), *hb=adw_header_bar_new(), *sc=gtk_scrolled_window_new();
    x->kind=kind; x->w=adw_window_new(); gtk_window_set_title(GTK_WINDOW(x->w),title); gtk_window_set_transient_for(GTK_WINDOW(x->w),GTK_WINDOW(win)); gtk_window_set_modal(GTK_WINDOW(x->w),kind!=1); gtk_window_set_default_size(GTK_WINDOW(x->w),560,520);
    x->lb=gtk_list_box_new(); gtk_list_box_set_selection_mode(GTK_LIST_BOX(x->lb),mode); cls(x->lb,"boxed-list"); gtk_widget_set_margin_start(x->lb,12); gtk_widget_set_margin_end(x->lb,12); gtk_widget_set_margin_top(x->lb,6); gtk_widget_set_margin_bottom(x->lb,12);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc),x->lb); gtk_widget_set_vexpand(sc,TRUE); adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv),hb); adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tv),sc);
    adw_window_set_content(ADW_WINDOW(x->w),tv); g_object_set_data_full(G_OBJECT(x->w),"p2",x,p2win_free); *header=hb; return x; }
static void p2_close(p2win *x){ gtk_window_destroy(GTK_WINDOW(x->w)); }
/* pinned / starred list: activating a row jumps to it */
static void p2_jump_act(GtkListBox *lb, GtkListBoxRow *row, gpointer p){ const char *cv=g_object_get_data(G_OBJECT(row),"conv"), *id=g_object_get_data(G_OBJECT(row),"id"); (void)lb;
    if(!cv) return; snprintf(jump_id,sizeof jump_id,"%s",id); if(strcmp(cv,cur_conv)) select_conv(cv); else chat_dirty=1; list_dirty=1; p2_close(p); }
static void p2_flag_list(int which, const char *conv){ GtkWidget *hb; p2win *x=p2_window(which==1?"置顶消息":"收藏的消息",1,GTK_SELECTION_NONE,&hb); int idx[500], n, i; lsm_msg *m; (void)hb;
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(x->lb),TRUE);
    lsm_lock(C); n=lsm_flagged(C,conv,which,idx,500); lsm_messages(C,&m); for(i=0;i<n;i++) gtk_list_box_append(GTK_LIST_BOX(x->lb),p2_msg_row(&m[idx[i]])); lsm_unlock(C);
    if(!n) gtk_list_box_set_placeholder(GTK_LIST_BOX(x->lb),label(which==1?"本会话还没有置顶消息。":"还没有收藏的消息。","dim-label"));
    g_signal_connect(x->lb,"row-activated",G_CALLBACK(p2_jump_act),x); gtk_window_present(GTK_WINDOW(x->w)); }
/* forward picker */
static void p2_fwd_go(GtkButton *b, gpointer p){ p2win *x=p; GList *s=gtk_list_box_get_selected_rows(GTK_LIST_BOX(x->lb)), *it; int bad=0, i; char err[256]="", nid[40]; (void)b;
    for(it=s;it;it=it->next){ const char *cv=g_object_get_data(G_OBJECT(it->data),"conv"); for(i=0;x->ids&&x->ids[i];i++) if(lsm_forward(C,x->ids[i],cv,nid,err)) bad++; } g_list_free(s);
    if(bad) info("部分转发失败",err); else toast("已转发"); chat_dirty=list_dirty=1; p2_close(x); }
static void p2_forward_ids(char **ids){ GtkWidget *hb, *go; p2win *x=p2_window(g_strv_length(ids)>1?"逐条转发到…":"转发到…",2,GTK_SELECTION_MULTIPLE,&hb); lsm_contact *k; lsm_group *g; int n,i;
    x->ids=g_strdupv(ids); go=gtk_button_new_with_label("转发"); cls(go,"suggested-action"); g_signal_connect(go,"clicked",G_CALLBACK(p2_fwd_go),x); adw_header_bar_pack_end(ADW_HEADER_BAR(hb),go);
    lsm_lock(C); n=lsm_contacts(C,&k); for(i=0;i<n;i++){ char cv[64]; gchar *e; if(k[i].trust==TRUST_BLOCKED||!strcmp(k[i].fp,lsm_my_fp(C))) continue; snprintf(cv,64,"dm:%s",k[i].fp); e=g_markup_escape_text(lsm_display_name(&k[i]),-1); gtk_list_box_append(GTK_LIST_BOX(x->lb),p2_row(e,cv,NULL)); g_free(e); }
    n=lsm_groups(C,&g); for(i=0;i<n;i++){ char u[37], cv[64]; gchar *e, *t; lsm_uuid_str(g[i].id,u); snprintf(cv,64,"group:%s",u); e=g_markup_escape_text(g[i].name,-1); t=g_strdup_printf("# %s",e); gtk_list_box_append(GTK_LIST_BOX(x->lb),p2_row(t,cv,NULL)); g_free(t); g_free(e); } lsm_unlock(C);
    gtk_window_present(GTK_WINDOW(x->w)); }
/* multi-select: forward / star / delete */
static char **p2_sel_ids(p2win *x){ GList *s=gtk_list_box_get_selected_rows(GTK_LIST_BOX(x->lb)), *it; char **v=g_new0(char*,g_list_length(s)+1); int k=0; for(it=s;it;it=it->next) v[k++]=g_strdup(g_object_get_data(G_OBJECT(it->data),"id")); g_list_free(s); return v; }
static void p2_ms_del_do(gpointer p){ char **v=p; int i; for(i=0;v[i];i++) lsm_delete_message(C,v[i]); chat_dirty=list_dirty=1; }
static void p2_ms_act(GtkButton *b, gpointer p){ p2win *x=p; const char *what=g_object_get_data(G_OBJECT(b),"what"); char **v=p2_sel_ids(x), err[256]; int i, n=g_strv_length(v);
    if(!n){ g_strfreev(v); toast("请先选择消息"); return; }
    if(!strcmp(what,"fwd")){ p2_close(x); p2_forward_ids(v); g_strfreev(v); return; }
    if(!strcmp(what,"star")){ for(i=0;v[i];i++) lsm_set_msg_flags(C,v[i],-1,1,err); g_strfreev(v); toast("已收藏"); chat_dirty=1; p2_close(x); return; }
    { char q[120]; snprintf(q,sizeof q,"删除选中的 %d 条消息？",n); p2_close(x); confirm(q,"只会从本机删除，对方仍保留。此操作无法撤销。","删除",1,p2_ms_del_do,v,(GDestroyNotify)g_strfreev); } }
static void p2_multi(void){ GtkWidget *hb; p2win *x; lsm_msg *m; int n,i; static const char *W[]={"fwd","star","del"}, *L[]={"逐条转发…","收藏","删除…"}; if(!cur_conv[0]) return;
    x=p2_window("多选消息",3,GTK_SELECTION_MULTIPLE,&hb);
    for(i=2;i>=0;i--){ GtkWidget *b=gtk_button_new_with_label(L[i]); g_object_set_data(G_OBJECT(b),"what",(gpointer)W[i]); if(i==0) cls(b,"suggested-action"); if(i==2) cls(b,"destructive-action"); g_signal_connect(b,"clicked",G_CALLBACK(p2_ms_act),x); adw_header_bar_pack_end(ADW_HEADER_BAR(hb),b); }
    lsm_lock(C); n=lsm_messages(C,&m); for(i=0;i<n;i++) if(!strcmp(m[i].conv,cur_conv) && m[i].kind!=K_SYSTEM && !m[i].recalled) gtk_list_box_append(GTK_LIST_BOX(x->lb),p2_msg_row(&m[i])); lsm_unlock(C);
    gtk_window_present(GTK_WINDOW(x->w)); }
/* bubble popover entries + their actions (returns 1 when handled) */
static void p2_menu(GtkWidget *box, const char *id){ lsm_msg *m; int n,i,pin=0,star=0,rec=1;
    lsm_lock(C); n=lsm_messages(C,&m); for(i=0;i<n;i++) if(!strcmp(m[i].id,id)){ pin=m[i].pinned; star=m[i].starred; rec=m[i].recalled; break; } lsm_unlock(C);
    if(i==n||rec) return; pop_btn(box,"转发(_F)…","forward",id); pop_btn(box,pin?"取消置顶(_P)":"置顶(_P)",pin?"unpin":"pin",id); pop_btn(box,star?"取消收藏(_S)":"收藏(_S)",star?"unstar":"star",id); pop_btn(box,"多选(_M)…","multi",id); }
static int p2_action(const char *what, const char *id){ char err[256]="";
    if(!strcmp(what,"forward")){ char *v[2]={(char*)id,NULL}; p2_forward_ids(v); return 1; }
    if(!strcmp(what,"pin")||!strcmp(what,"unpin")){ if(lsm_set_msg_flags(C,id,what[0]=='p',-1,err)) info("操作失败",err); chat_dirty=1; return 1; }
    if(!strcmp(what,"star")||!strcmp(what,"unstar")){ if(lsm_set_msg_flags(C,id,-1,what[0]=='s',err)) info("操作失败",err); else toast(what[0]=='s'?"已收藏":"已取消收藏"); chat_dirty=1; return 1; }
    if(!strcmp(what,"multi")){ p2_multi(); return 1; }
    return 0; }
/* pinned banner above the chat */
static void p2_pin_banner(void){ int idx[1], n=0; lsm_msg *m; char t[400]="";
    if(!pin_bar) return; if(cur_conv[0]){ lsm_lock(C); n=lsm_flagged(C,cur_conv,1,idx,1); if(n){ char db[300]; int k; lsm_messages(C,&m); snprintf(t,sizeof t,"📌 置顶：%.160s",m[idx[0]].kind==K_FILE?m[idx[0]].file_name:lsm_display_body(&m[idx[0]],db,sizeof db)); for(k=0;t[k];k++) if(t[k]=='\n') t[k]=' '; } lsm_unlock(C); }
    gtk_widget_set_visible(pin_bar,n>0); if(n) gtk_button_set_label(GTK_BUTTON(pin_btn),t); }
static void p2_pin_click(GtkButton *b, gpointer p){ (void)b; (void)p; p2_flag_list(1,cur_conv); }
static GtkWidget *p2_pin_bar_new(void){ pin_bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0); cls(pin_bar,"replybar"); pin_btn=gtk_button_new_with_label(""); cls(pin_btn,"flat"); gtk_widget_set_hexpand(pin_btn,TRUE);
    { GtkWidget *c=gtk_button_get_child(GTK_BUTTON(pin_btn)); if(GTK_IS_LABEL(c)){ gtk_label_set_ellipsize(GTK_LABEL(c),PANGO_ELLIPSIZE_END); gtk_label_set_xalign(GTK_LABEL(c),0); } }
    gtk_widget_set_tooltip_text(pin_btn,"查看本会话的置顶消息"); g_signal_connect(pin_btn,"clicked",G_CALLBACK(p2_pin_click),NULL); gtk_box_append(GTK_BOX(pin_bar),pin_btn); gtk_widget_set_visible(pin_bar,FALSE); return pin_bar; }
/* @mentions: typing "@" in a group pops the member list */
static GtkWidget *at_pop;
static void p2_at_pick(GtkButton *b, gpointer nm){ char t[140]; (void)b; snprintf(t,sizeof t,"%s ",(const char*)nm); gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(input)),t,-1); if(at_pop) gtk_popover_popdown(GTK_POPOVER(at_pop)); gtk_widget_grab_focus(input); }
static gboolean p2_at_popup(gpointer p){ char nm[64][128], fp[64][FP_LEN]; int n,i; GtkWidget *box; (void)p; lsm_lock(C); n=lsm_mention_candidates(C,cur_conv,nm,fp,64); lsm_unlock(C); if(!n) return G_SOURCE_REMOVE;
    if(at_pop){ gtk_widget_unparent(at_pop); at_pop=NULL; } at_pop=gtk_popover_new(); box=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
    for(i=0;i<n;i++){ GtkWidget *b=gtk_button_new_with_label(nm[i]); cls(b,"flat"); g_signal_connect_data(b,"clicked",G_CALLBACK(p2_at_pick),g_strdup(nm[i]),(GClosureNotify)g_free,0); gtk_box_append(GTK_BOX(box),b); }
    gtk_popover_set_child(GTK_POPOVER(at_pop),box); gtk_widget_set_parent(at_pop,input); gtk_popover_set_position(GTK_POPOVER(at_pop),GTK_POS_TOP); cls(at_pop,"menu"); gtk_popover_popup(GTK_POPOVER(at_pop));
    if(getenv("LSM_UI_TEST")) fprintf(stderr,"UITEST at-menu %d\n",n); return G_SOURCE_REMOVE; }
static void p2_insert_text(GtkTextBuffer *b, GtkTextIter *loc, gchar *text, gint len, gpointer p){ (void)b; (void)loc; (void)p; if(!p2_loading && len==1 && text[0]=='@' && !strncmp(cur_conv,"group:",6)) g_idle_add(p2_at_popup,NULL); }
/* plain send with mentions (reply/edit go through p1_send, which also picks mentions up) */
static int p2_send_text(const char *t, char *id, char *err){ char csv[64*(FP_LEN+1)]; lsm_lock(C); lsm_mentions_in(C,cur_conv,t,csv,sizeof csv); lsm_unlock(C);
    if(p1_mode==1||csv[0]){ int r=lsm_send_text2(C,cur_conv,t,p1_mode==1?p1_id:NULL,csv[0]?csv:NULL,id,err); if(!r && p1_mode==1) p1_set(0,NULL); return r; } return lsm_send_text(C,cur_conv,t,id,err); }
static void p2_toggle_conv_pin(void){ int on; if(!cur_conv[0]) return; lsm_lock(C); on=!lsm_conv_pinned(C,cur_conv); lsm_unlock(C); lsm_set_conv_pinned(C,cur_conv,on); toast(on?"已置顶会话":"已取消置顶"); list_dirty=1; }
