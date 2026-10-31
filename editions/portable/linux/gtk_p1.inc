/* v0.6.1 P1 (GTK3): reply/quote, reactions, edit/recall, voice messages. Included by gtk_gui.c. */
#include <signal.h>
static char reply_id[40]; static GtkWidget *reply_bar, *reply_lbl, *mic_btn;
static const char *P1_EMO[]={"👍","❤️","😂","😮","😢","🎉",NULL};
static void p1_set_reply(const char *id){ lsm_msg *m; int n,i; char q[300]="";
    snprintf(reply_id,sizeof reply_id,"%s",id?id:""); if(!reply_bar) return;
    if(!reply_id[0]){ gtk_widget_hide(reply_bar); return; }
    lsm_lock(C); n=lsm_messages(C,&m); for(i=0;i<n;i++) if(!strcmp(m[i].id,reply_id)){ char b[200]; snprintf(q,sizeof q,"回复 %s：%.120s",m[i].out?"我":m[i].sender_name,m[i].kind==K_FILE?m[i].file_name:lsm_display_body(&m[i],b,sizeof b)); break; } lsm_unlock(C);
    gtk_label_set_text(GTK_LABEL(reply_lbl),q); gtk_widget_show(reply_bar); gtk_widget_grab_focus(input); }
static void p1_reply_cb(GtkMenuItem *i, gpointer id){ (void)i; p1_set_reply((const char*)id); }
static void p1_react_cb(GtkMenuItem *i, gpointer id){ char err[256]; const char *e=g_object_get_data(G_OBJECT(i),"e"); if(lsm_react(C,(const char*)id,e,err)) msgbox(GTK_MESSAGE_WARNING,"%s",err); refresh_chat(); }
static void p1_edit_id(const char *id){ lsm_msg *m; int n,i; char *cur=NULL, *t, err[256];
    lsm_lock(C); n=lsm_messages(C,&m); for(i=0;i<n;i++) if(!strcmp(m[i].id,id)){ cur=g_strdup(m[i].body?m[i].body:""); break; } lsm_unlock(C);
    if(!cur) return; t=ask_text("编辑消息","修改后的内容（24 小时内可编辑）：",cur); g_free(cur);
    if(t && *g_strstrip(t) && lsm_edit_message(C,id,t,err)) msgbox(GTK_MESSAGE_WARNING,"%s",err); g_free(t); refresh_chat(); }
static gboolean p1_edit_idle(gpointer id){ p1_edit_id((const char*)id); g_free(id); return G_SOURCE_REMOVE; }
static void p1_edit_cb(GtkMenuItem *i, gpointer id){ (void)i; g_idle_add(p1_edit_idle,g_strdup((const char*)id)); }
static gboolean p1_recall_idle(gpointer id){ char err[256]; if(confirm("撤回这条消息？","双方都会看到“撤回了一条消息”。24 小时内可撤回。","撤回") && lsm_recall_message(C,(const char*)id,err)) msgbox(GTK_MESSAGE_WARNING,"%s",err); g_free(id); refresh_chat(); return G_SOURCE_REMOVE; }
static void p1_recall_cb(GtkMenuItem *i, gpointer id){ (void)i; g_idle_add(p1_recall_idle,g_strdup((const char*)id)); }
static void p1_item(GtkMenuShell *menu, const char *label, GCallback cb, const char *id){ GtkWidget *it=gtk_menu_item_new_with_mnemonic(label); g_signal_connect_data(it,"activate",cb,g_strdup(id),(GClosureNotify)g_free,0); gtk_menu_shell_append(menu,it); }
static void p1_menu_items(GtkMenuShell *menu, const char *id){ lsm_msg *m; int n,i,can=0,recalled=0,k; char mine[40]="";
    lsm_lock(C); n=lsm_messages(C,&m); for(i=0;i<n;i++) if(!strcmp(m[i].id,id)){ can=lsm_can_modify(C,&m[i]); recalled=m[i].recalled; lsm_my_reaction(C,&m[i],mine,sizeof mine); break; } lsm_unlock(C);
    if(i==n || recalled) return;
    gtk_menu_shell_append(menu,gtk_separator_menu_item_new());
    p1_item(menu,"回复(_R)",G_CALLBACK(p1_reply_cb),id);
    { GtkWidget *sub=gtk_menu_new(), *top=gtk_menu_item_new_with_mnemonic("表情回应(_E)");
      for(k=0;P1_EMO[k];k++){ char l[64]; GtkWidget *it; snprintf(l,sizeof l,"%s%s",P1_EMO[k],strcmp(mine,P1_EMO[k])?"":"  ✓"); it=gtk_menu_item_new_with_label(l); g_object_set_data(G_OBJECT(it),"e",(gpointer)P1_EMO[k]);
          g_signal_connect_data(it,"activate",G_CALLBACK(p1_react_cb),g_strdup(id),(GClosureNotify)g_free,0); gtk_menu_shell_append(GTK_MENU_SHELL(sub),it); }
      if(mine[0]){ GtkWidget *it=gtk_menu_item_new_with_label("取消我的回应"); g_object_set_data(G_OBJECT(it),"e",(gpointer)""); g_signal_connect_data(it,"activate",G_CALLBACK(p1_react_cb),g_strdup(id),(GClosureNotify)g_free,0); gtk_menu_shell_append(GTK_MENU_SHELL(sub),it); }
      gtk_menu_item_set_submenu(GTK_MENU_ITEM(top),sub); gtk_menu_shell_append(menu,top); }
    if(can){ p1_item(menu,"编辑(_T)…",G_CALLBACK(p1_edit_cb),id); p1_item(menu,"撤回(_W)",G_CALLBACK(p1_recall_cb),id); } }
/* quote strip above the body, reaction chips below it; returns the text for the body label */
static void p1_quote(GtkWidget *bub, const lsm_msg *m){ GtkWidget *q; char t[480]; if(!m->reply_to[0] || m->recalled) return;
    snprintf(t,sizeof t,"「%s」",m->quote[0]?m->quote:"原消息"); q=gtk_label_new(t); gtk_label_set_line_wrap(GTK_LABEL(q),TRUE); gtk_label_set_xalign(GTK_LABEL(q),0); gtk_label_set_max_width_chars(GTK_LABEL(q),48);
    gtk_style_context_add_class(gtk_widget_get_style_context(q),"quote"); gtk_style_context_add_class(gtk_widget_get_style_context(q),"meta"); gtk_box_pack_start(GTK_BOX(bub),q,FALSE,FALSE,0); }
static void p1_reacts(GtkWidget *bub, const lsm_msg *m){ char s[300]; GtkWidget *l; if(m->recalled) return; lsm_reaction_summary(m,s,sizeof s); if(!s[0]) return;
    l=gtk_label_new(s); gtk_label_set_xalign(GTK_LABEL(l),0); gtk_style_context_add_class(gtk_widget_get_style_context(l),"reactions"); gtk_box_pack_start(GTK_BOX(bub),l,FALSE,FALSE,0); }
/* ↑ in an empty composer edits my last message */
static int p1_edit_last(void){ lsm_msg *m; int n,i; char id[40]=""; lsm_lock(C); n=lsm_messages(C,&m); for(i=n-1;i>=0;i--) if(!strcmp(m[i].conv,cur_conv) && lsm_can_modify(C,&m[i])){ snprintf(id,sizeof id,"%s",m[i].id); break; } lsm_unlock(C);
    if(!id[0]) return 0; g_idle_add(p1_edit_idle,g_strdup(id)); return 1; }

/* ---- voice: record with parecord/arecord (WAV, 16 kHz mono), play with paplay/aplay ---- */
static GPid rec_pid; static gint64 rec_t0; static char rec_tmp[512];
static void send_path(const char *path);
static int have_prog(const char *p){ gchar *x=g_find_program_in_path(p); int r=x!=NULL; g_free(x); return r; }
static void rec_done(GPid pid, gint st, gpointer d){ int secs=(int)((g_get_monotonic_time()-rec_t0+500000)/1000000); char nm[128], dst[700]; (void)st; (void)d; g_spawn_close_pid(pid); rec_pid=0;
    gtk_button_set_label(GTK_BUTTON(mic_btn),"🎤 语音"); if(secs<1){ unlink(rec_tmp); msgbox(GTK_MESSAGE_INFO,"录音太短。"); return; }
    lsm_voice_name(secs,"wav",nm,sizeof nm); { gchar *dir=g_path_get_dirname(rec_tmp); snprintf(dst,sizeof dst,"%s/%s",dir,nm); g_free(dir); }
    if(rename(rec_tmp,dst)==0) send_path(dst); }
static void p1_mic(GtkButton *b, gpointer d){ (void)b; (void)d; GError *e=NULL;
    if(rec_pid){ kill(rec_pid,SIGINT); return; }
    if(!cur_conv[0]){ msgbox(GTK_MESSAGE_INFO,"请先在左侧选择联系人或群组。"); return; }
    { gchar *dir=g_build_filename(g_get_user_cache_dir(),"lan-messenger",NULL); g_mkdir_with_parents(dir,0700); snprintf(rec_tmp,sizeof rec_tmp,"%s/rec-%ld.wav",dir,(long)time(NULL)); g_free(dir); }
    { const char *pa[]={"parecord","--file-format=wav","--rate=16000","--channels=1",rec_tmp,NULL}, *al[]={"arecord","-q","-f","S16_LE","-r","16000","-c","1",rec_tmp,NULL};
      const char **argv=have_prog("parecord")?pa:have_prog("arecord")?al:NULL;
      if(!argv){ msgbox(GTK_MESSAGE_WARNING,"未找到录音程序（parecord 或 arecord）。请安装 pulseaudio-utils 或 alsa-utils。"); return; }
      if(!g_spawn_async(NULL,(gchar**)argv,NULL,G_SPAWN_SEARCH_PATH|G_SPAWN_DO_NOT_REAP_CHILD,NULL,NULL,&rec_pid,&e)){ msgbox(GTK_MESSAGE_WARNING,"无法开始录音：%s",e?e->message:"?"); if(e) g_error_free(e); rec_pid=0; return; } }
    rec_t0=g_get_monotonic_time(); g_child_watch_add(rec_pid,rec_done,NULL); gtk_button_set_label(GTK_BUTTON(mic_btn),"■ 停止并发送"); }
static void p1_play(const char *path){ const char *pa[]={"paplay",path,NULL}, *al[]={"aplay","-q",path,NULL}; const char **argv=have_prog("paplay")?pa:have_prog("aplay")?al:NULL;
    if(!argv || !g_spawn_async(NULL,(gchar**)argv,NULL,G_SPAWN_SEARCH_PATH,NULL,NULL,NULL,NULL)) open_path_default(path); }
static gboolean p1_play_cb(GtkWidget *w, GdkEventButton *ev, gpointer p){ (void)w; if(ev->type==GDK_BUTTON_PRESS && ev->button==1){ p1_play((const char*)p); return TRUE; } return FALSE; }
