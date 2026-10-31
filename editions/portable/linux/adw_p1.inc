/* v0.6.1 P1 (GTK4/libadwaita): reply/quote, reactions, edit/recall, voice. Included by adw_gui.c. */
#include <signal.h>
static char p1_id[40]; static int p1_mode; /* 1 reply, 2 edit */ static GtkWidget *p1_bar, *p1_lbl, *mic_btn;
static const char *P1_EMO[]={"👍","❤️","😂","😮","😢","🎉",NULL};
static void send_path(const char *path);
static void p1_set(int mode, const char *id){ lsm_msg *m; int n,i; char q[300]="", body[4096]="";
    p1_mode=id&&*id?mode:0; snprintf(p1_id,sizeof p1_id,"%s",p1_mode?id:""); if(!p1_bar) return;
    if(!p1_mode){ gtk_widget_set_visible(p1_bar,FALSE); return; }
    lsm_lock(C); n=lsm_messages(C,&m); for(i=0;i<n;i++) if(!strcmp(m[i].id,p1_id)){ char b[200]; snprintf(q,sizeof q,"%s %s：%.120s",mode==2?"编辑":"回复",m[i].out?"我":m[i].sender_name,m[i].kind==K_FILE?m[i].file_name:lsm_display_body(&m[i],b,sizeof b)); snprintf(body,sizeof body,"%s",m[i].body?m[i].body:""); break; } lsm_unlock(C);
    gtk_label_set_text(GTK_LABEL(p1_lbl),q); gtk_widget_set_visible(p1_bar,TRUE);
    if(mode==2) gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(input)),body,-1);
    gtk_widget_grab_focus(input); }
static void p1_cancel(void){ if(p1_mode==2) gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(input)),"",-1); p1_set(0,NULL); }
static void p1_recall_do(gpointer id){ char err[256]=""; if(lsm_recall_message(C,(const char*)id,err)) info("撤回失败",err); chat_dirty=list_dirty=1; }
static void p1_react_click(GtkButton *b, gpointer id){ char err[256]=""; GtkWidget *pop=gtk_widget_get_ancestor(GTK_WIDGET(b),GTK_TYPE_POPOVER); const char *e=g_object_get_data(G_OBJECT(b),"e");
    if(pop) gtk_popover_popdown(GTK_POPOVER(pop)); if(lsm_react(C,(const char*)id,e,err)) info("表情回应失败",err); chat_dirty=1; }
/* returns 1 when it handled "what" */
static int p1_action(const char *what, const char *id){
    if(!strcmp(what,"reply")){ p1_set(1,id); return 1; }
    if(!strcmp(what,"edit")){ p1_set(2,id); return 1; }
    if(!strcmp(what,"recall")){ confirm("撤回这条消息？","双方都会看到“撤回了一条消息”。24 小时内可撤回。","撤回",1,p1_recall_do,g_strdup(id),g_free); return 1; }
    return 0; }
static GtkWidget *pop_btn(GtkWidget *box, const char *text, const char *what, const char *arg);
static void p1_menu(GtkWidget *box, const char *id){ lsm_msg *m; int n,i,can=0,rec=1,k; char mine[40]="";
    lsm_lock(C); n=lsm_messages(C,&m); for(i=0;i<n;i++) if(!strcmp(m[i].id,id)){ can=lsm_can_modify(C,&m[i]); rec=m[i].recalled; lsm_my_reaction(C,&m[i],mine,sizeof mine); break; } lsm_unlock(C);
    if(rec) return;
    { GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,2); cls(row,"reactbar");
      for(k=0;P1_EMO[k];k++){ GtkWidget *b=gtk_button_new_with_label(P1_EMO[k]); cls(b,"flat"); if(getenv("LSM_UI_TEST") && icon_widgets) g_ptr_array_add(icon_widgets,g_object_ref(gtk_button_get_child(GTK_BUTTON(b)))); /* colour emoji glyphs = pictures, not text (audit ignore) */ if(!strcmp(mine,P1_EMO[k])) cls(b,"accent"); g_object_set_data(G_OBJECT(b),"e",(gpointer)(strcmp(mine,P1_EMO[k])?P1_EMO[k]:""));
          gtk_widget_set_tooltip_text(b,strcmp(mine,P1_EMO[k])?"表情回应":"取消我的回应"); g_signal_connect_data(b,"clicked",G_CALLBACK(p1_react_click),g_strdup(id),(GClosureNotify)g_free,0); gtk_box_append(GTK_BOX(row),b); }
      gtk_box_append(GTK_BOX(box),row); }
    pop_btn(box,"回复(_R)","reply",id);
    if(can){ pop_btn(box,"编辑(_E)","edit",id); pop_btn(box,"撤回(_W)","recall",id); } }
static void p1_quote(GtkWidget *bub, const lsm_msg *m){ char t[480]; GtkWidget *q; if(!m->reply_to[0]||m->recalled) return; snprintf(t,sizeof t,"%s",m->quote[0]?m->quote:"原消息");
    q=label(t,"quote"); gtk_label_set_wrap(GTK_LABEL(q),TRUE); gtk_label_set_max_width_chars(GTK_LABEL(q),40); gtk_box_append(GTK_BOX(bub),q); }
static void p1_reacts(GtkWidget *bub, const lsm_msg *m){ char s[300]; GtkWidget *l; if(m->recalled) return; lsm_reaction_summary(m,s,sizeof s); if(!s[0]) return;
    l=label(s,"reactions"); gtk_widget_set_halign(l,GTK_ALIGN_START); gtk_box_append(GTK_BOX(bub),l); }
static int p1_edit_last(void){ lsm_msg *m; int n,i; char id[40]=""; lsm_lock(C); n=lsm_messages(C,&m); for(i=n-1;i>=0;i--) if(!strcmp(m[i].conv,cur_conv) && lsm_can_modify(C,&m[i])){ snprintf(id,sizeof id,"%s",m[i].id); break; } lsm_unlock(C);
    if(!id[0]) return 0; p1_set(2,id); return 1; }
/* send_current hook: returns 1 when it consumed the text */
static int p1_send(const char *t, char *err){ char id[40]; int r;
    if(p1_mode==2){ r=lsm_edit_message(C,p1_id,t,err); if(!r) p1_set(0,NULL); return r?-1:1; }
    if(p1_mode==1){ r=lsm_send_text2(C,cur_conv,t,p1_id,NULL,id,err); if(!r) p1_set(0,NULL); return r?-1:1; }
    return 0; }
static GtkWidget *p1_bar_new(void){ GtkWidget *x=gtk_button_new_from_icon_name("window-close-symbolic"); p1_bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6); cls(p1_bar,"replybar");
    p1_lbl=label("","quote"); gtk_label_set_ellipsize(GTK_LABEL(p1_lbl),PANGO_ELLIPSIZE_END); gtk_widget_set_hexpand(p1_lbl,TRUE); cls(x,"flat"); cls(x,"circular"); gtk_widget_set_tooltip_text(x,"取消 (Esc)");
    g_signal_connect_swapped(x,"clicked",G_CALLBACK(p1_cancel),NULL); gtk_box_append(GTK_BOX(p1_bar),p1_lbl); gtk_box_append(GTK_BOX(p1_bar),x); gtk_widget_set_visible(p1_bar,FALSE); return p1_bar; }

/* ---- voice ---- */
static GPid rec_pid; static gint64 rec_t0; static char rec_tmp[512];
static int have_prog(const char *p){ gchar *x=g_find_program_in_path(p); int r=x!=NULL; g_free(x); return r; }
static void rec_done(GPid pid, gint st, gpointer d){ int secs=(int)((g_get_monotonic_time()-rec_t0+500000)/1000000); char nm[128], dst[700]; (void)st;(void)d; g_spawn_close_pid(pid); rec_pid=0;
    gtk_button_set_icon_name(GTK_BUTTON(mic_btn),"audio-input-microphone-symbolic"); gtk_widget_remove_css_class(mic_btn,"destructive-action");
    if(secs<1){ unlink(rec_tmp); toast("录音太短"); return; }
    lsm_voice_name(secs,"wav",nm,sizeof nm); { gchar *dir=g_path_get_dirname(rec_tmp); snprintf(dst,sizeof dst,"%s/%s",dir,nm); g_free(dir); } if(rename(rec_tmp,dst)==0) send_path(dst); }
static void p1_mic(GtkButton *b, gpointer d){ GError *e=NULL; (void)b;(void)d;
    if(rec_pid){ kill(rec_pid,SIGINT); return; }
    if(!cur_conv[0]){ info("未选择会话","请先在左侧选择联系人或群组。"); return; }
    { gchar *dir=g_build_filename(g_get_user_cache_dir(),"lan-messenger",NULL); g_mkdir_with_parents(dir,0700); snprintf(rec_tmp,sizeof rec_tmp,"%s/rec-%ld.wav",dir,(long)time(NULL)); g_free(dir); }
    { const char *pa[]={"parecord","--file-format=wav","--rate=16000","--channels=1",rec_tmp,NULL}, *al[]={"arecord","-q","-f","S16_LE","-r","16000","-c","1",rec_tmp,NULL};
      const char **argv=have_prog("parecord")?pa:have_prog("arecord")?al:NULL;
      if(!argv){ info("无法录音","未找到录音程序（parecord 或 arecord）。请安装 pulseaudio-utils 或 alsa-utils。"); return; }
      if(!g_spawn_async(NULL,(gchar**)argv,NULL,G_SPAWN_SEARCH_PATH|G_SPAWN_DO_NOT_REAP_CHILD,NULL,NULL,&rec_pid,&e)){ info("无法录音",e?e->message:"?"); if(e) g_error_free(e); rec_pid=0; return; } }
    rec_t0=g_get_monotonic_time(); g_child_watch_add(rec_pid,rec_done,NULL); gtk_button_set_icon_name(GTK_BUTTON(mic_btn),"media-playback-stop-symbolic"); cls(mic_btn,"destructive-action"); toast("正在录音… 再点一次停止并发送"); }
static void p1_play(GtkButton *b, gpointer path){ GtkMediaStream *s=g_object_get_data(G_OBJECT(b),"media"); (void)b;
    if(!s){ s=gtk_media_file_new_for_filename((const char*)path); g_object_set_data_full(G_OBJECT(b),"media",s,g_object_unref); }
    if(gtk_media_stream_get_playing(s)) gtk_media_stream_pause(s); else { if(gtk_media_stream_get_ended(s)) gtk_media_stream_seek(s,0); gtk_media_stream_play(s); }
    if(gtk_media_stream_get_error(s)){ const char *pa[]={"paplay",(const char*)path,NULL}; if(!have_prog("paplay")||!g_spawn_async(NULL,(gchar**)pa,NULL,G_SPAWN_SEARCH_PATH,NULL,NULL,NULL,NULL)) open_path_safe((const char*)path); } }
/* voice bubble body: ▶ button + "语音 N″" */
static void p1_voice(GtkWidget *bub, const lsm_msg *m, const char *path, GtkWidget *meta){ GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,10), *pb=gtk_button_new_from_icon_name("media-playback-start-symbolic"), *l; char t[64];
    cls(pb,"circular"); gtk_widget_set_sensitive(pb,path!=NULL); gtk_widget_set_tooltip_text(pb,path?"播放 / 暂停":"正在接收…"); if(path) g_signal_connect_data(pb,"clicked",G_CALLBACK(p1_play),g_strdup(path),(GClosureNotify)g_free,0);
    snprintf(t,sizeof t,"语音 %d″",lsm_voice_secs(m->file_name)); l=label(t,NULL); gtk_widget_set_hexpand(l,TRUE); gtk_widget_set_valign(l,GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(row),pb); gtk_box_append(GTK_BOX(row),l); gtk_box_append(GTK_BOX(row),meta); gtk_box_append(GTK_BOX(bub),row); cls(bub,"voice"); }
