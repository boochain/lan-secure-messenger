/* v0.6.1 P2 (XP/Win32): 置顶会话、草稿、置顶/收藏消息、@提及、正在输入、转发、多选 — on the themed prompt() dialog. Included by gui.c after xp_p0.inc. */
#define TIMER_DRAFT 0x5102
#define TIMER_TYPING 0x5103
static int p2_loading; static wchar_t typing_sub[160]; static char typing_conv[64]; static DWORD typing_until;
static char *p2_input_u8(void){ int n=GetWindowTextLengthW(hInput); wchar_t *w=(wchar_t*)malloc((n+1)*sizeof(wchar_t)); char *u=(char*)malloc(n*4+4); GetWindowTextW(hInput,w,n+1); U(w,u,n*4+4); free(w); return u; }
static void p2_set_input_u8(const char *s){ int len=MultiByteToWideChar(CP_UTF8,0,s,-1,NULL,0); wchar_t *w=(wchar_t*)malloc((len+1)*sizeof(wchar_t)); MultiByteToWideChar(CP_UTF8,0,s,-1,w,len+1); p2_loading=1; SetWindowTextW(hInput,w); p2_loading=0; free(w); }
/* drafts: saved 1.5 s after typing stops and when switching chats (sealed by the core) */
static void p2_save_draft(void){ char *u; KillTimer(hMain,TIMER_DRAFT); if(!cur_conv[0]||edit_id[0]) return; u=p2_input_u8(); lsm_set_draft(C,cur_conv,u); free(u); }
static void p2_load_draft(void){ char d[8192]=""; if(cur_conv[0]){ lsm_lock(C); lsm_get_draft(C,cur_conv,d,sizeof d); lsm_unlock(C); } p2_set_input_u8(d); SendMessageW(hInput,EM_SETSEL,(WPARAM)-1,(LPARAM)-1); }
/* call before cur_conv changes / after */
static void p2_leave_conv(void){ if(edit_id[0]) p1_edit_cancel(); else p2_save_draft(); reply_id[0]=0; reply_hint[0]=0; }
static void p2_enter_conv(void){ p2_load_draft(); }
static void p2_input_changed(void){ if(p2_loading||!cur_conv[0]||edit_id[0]) return; if(GetWindowTextLengthW(hInput)>0) lsm_send_typing(C,cur_conv); SetTimer(hMain,TIMER_DRAFT,1500,NULL); }
static void p2_timer(WPARAM w){ if(w==TIMER_DRAFT){ p2_save_draft(); refresh_list(); } else if(w==TIMER_TYPING){ KillTimer(hMain,TIMER_TYPING); typing_conv[0]=0; InvalidateRect(hTitle,NULL,FALSE); } }
/* typing indicator replaces the header subtitle for 5 s */
static void p2_on_typing(char *ev){ char *f[4]; int n=0; char *q=ev; f[n++]=q; while(*q&&n<4){ if(*q=='\t'){ *q=0; f[n++]=q+1; } q++; } if(n<4) return;
    snprintf(typing_conv,sizeof typing_conv,"%s",f[1]); _snwprintf(typing_sub,160,L"  ·  %s 正在输入…",W(f[3])); typing_sub[159]=0; typing_until=GetTickCount()+5000;
    SetTimer(hMain,TIMER_TYPING,5000,NULL); if(!strcmp(typing_conv,cur_conv)) InvalidateRect(hTitle,NULL,FALSE); }
static const wchar_t *p2_hdr_sub(void){ return typing_conv[0] && !strcmp(typing_conv,cur_conv) && (int)(typing_until-GetTickCount())>0 ? typing_sub : hdr_sub; }
/* sidebar: pinned chats first, "[草稿]" preview (lock held; called from refresh_list after the rows are collected) */
static void p2_list_sort(int *sel, const char *keep){ int i, j=0; conv_t *tmp=(conv_t*)malloc(sizeof(conv_t)*(nlist?nlist:1)); char (*ids)[64]=malloc(64*(nlist?nlist:1)); int *pin=(int*)malloc(sizeof(int)*(nlist?nlist:1));
    for(i=0;i<nlist;i++){ char d[300]; pin[i]=lsm_conv_pinned(C,list_ids[i]);
        if(lsm_get_draft(C,list_ids[i],d,sizeof d)){ wchar_t *q; _snwprintf(convs[i].preview,200,L"[草稿] %s",W(d)); convs[i].preview[199]=0; for(q=convs[i].preview;*q;q++) if(*q==L'\r'||*q==L'\n') *q=L' '; convs[i].draft=1; }
        if(pin[i]){ wchar_t nm[160]; _snwprintf(nm,160,L"[置顶] %s",convs[i].name); nm[159]=0; wcsncpy(convs[i].name,nm,159); convs[i].name[159]=0; } }
    for(i=0;i<nlist;i++) if(pin[i]){ tmp[j]=convs[i]; strcpy(ids[j],list_ids[i]); j++; }
    for(i=0;i<nlist;i++) if(!pin[i]){ tmp[j]=convs[i]; strcpy(ids[j],list_ids[i]); j++; }
    *sel=-1; SendMessageW(hList,LB_RESETCONTENT,0,0);
    for(i=0;i<nlist;i++){ convs[i]=tmp[i]; strcpy(list_ids[i],ids[i]); SendMessageW(hList,LB_ADDSTRING,0,(LPARAM)convs[i].name); if(!strcmp(list_ids[i],keep)) *sel=i; }
    free(tmp); free(ids); free(pin); }
/* ---- pickers on prompt() ---- */
static int p2_pick_convs(char out[][64], int max){ prompt_t p; static char names[256][200]; const char *items[256]; char cv[256][64]; lsm_contact *k; lsm_group *g; int n,i,cnt=0,r=0;
    lsm_lock(C); n=lsm_contacts(C,&k); for(i=0;i<n&&cnt<256;i++){ if(k[i].trust==TRUST_BLOCKED||!strcmp(k[i].fp,lsm_my_fp(C))) continue; snprintf(names[cnt],200,"%s",lsm_display_name(&k[i])); snprintf(cv[cnt],64,"dm:%s",k[i].fp); items[cnt]=names[cnt]; cnt++; }
    n=lsm_groups(C,&g); for(i=0;i<n&&cnt<256;i++){ char u[37]; lsm_uuid_str(g[i].id,u); snprintf(names[cnt],200,"# %s",g[i].name); snprintf(cv[cnt],64,"group:%s",u); items[cnt]=names[cnt]; cnt++; } lsm_unlock(C);
    if(!cnt){ msgbox("没有可以转发的会话。",MB_ICONINFORMATION); return 0; }
    memset(&p,0,sizeof p); p.items=items; p.nitems=cnt; p.multi=1;
    if(!prompt(L"转发到",L"选择一个或多个会话（Ctrl/Shift 多选）：",NULL,&p,0)) return 0;
    for(i=0;i<p.nsel&&r<max;i++) snprintf(out[r++],64,"%s",cv[p.sel[i]]); return r; }
static void p2_forward_ids(char ids[][40], int nid){ char to[64][64], err[256]="", nid_s[40]; int nt=p2_pick_convs(to,64), i, j, bad=0;
    for(j=0;j<nt;j++) for(i=0;i<nid;i++) if(lsm_forward(C,ids[i],to[j],nid_s,err)) bad++;
    if(bad) msgbox(err,MB_ICONWARNING); refresh_list(); refresh_chat(); }
static void p2_msg_label(const lsm_msg *x, char *o, size_t n){ char ts[32], db[300]; time_t tt=(time_t)(x->ts_ms/1000); struct tm *tmv=localtime(&tt); int k;
    if(tmv) strftime(ts,sizeof ts,"%m-%d %H:%M",tmv); else ts[0]=0;
    snprintf(o,n,"%s  %s：%.120s",ts,x->out?"我":x->sender_name,x->kind==K_FILE?x->file_name:lsm_display_body(x,db,sizeof db)); for(k=0;o[k];k++) if(o[k]=='\r'||o[k]=='\n') o[k]=' '; }
/* pinned (which 1) / starred (2) list: pick one to jump to it */
static void p2_flag_list(int which, const char *conv){ prompt_t p; static char lab[64][300]; static char cv[64][64], id[64][40]; const char *items[64]; int idx[64], n, i; lsm_msg *m;
    lsm_lock(C); n=lsm_flagged(C,conv,which,idx,64); lsm_messages(C,&m); for(i=0;i<n;i++){ p2_msg_label(&m[idx[i]],lab[i],300); snprintf(cv[i],64,"%s",m[idx[i]].conv); memcpy(id[i],m[idx[i]].id,40); items[i]=lab[i]; } lsm_unlock(C);
    if(!n){ msgbox(which==1?"本会话还没有置顶消息。":"还没有收藏的消息。",MB_ICONINFORMATION); return; }
    memset(&p,0,sizeof p); p.items=items; p.nitems=n;
    if(prompt(which==1?L"置顶消息":L"收藏的消息",L"选择一条消息，确定后跳转到聊天：",NULL,&p,0) && p.nsel>0) jump_to(cv[p.sel[0]],id[p.sel[0]]); }
/* multi-select: pick messages, then the action */
static void p2_multi(void){ prompt_t p; static char lab[300][300]; static char id[300][40]; const char *items[300]; char sel[64][40], err[256]; int n,i,k=0,cnt=0,first; lsm_msg *m; static const char *acts[]={"逐条转发…","收藏","删除（仅本机）…"};
    if(!cur_conv[0]){ msgbox("请先在左侧选择联系人或群组。",MB_ICONINFORMATION); return; }
    lsm_lock(C); n=lsm_messages(C,&m); for(i=0;i<n;i++) if(!strcmp(m[i].conv,cur_conv)&&m[i].kind!=K_SYSTEM&&!m[i].recalled) cnt++;
    first=cnt>300?cnt-300:0; cnt=0; for(i=0;i<n;i++) if(!strcmp(m[i].conv,cur_conv)&&m[i].kind!=K_SYSTEM&&!m[i].recalled){ if(first>0){ first--; continue; } p2_msg_label(&m[i],lab[cnt],300); memcpy(id[cnt],m[i].id,40); items[cnt]=lab[cnt]; cnt++; } lsm_unlock(C);
    if(!cnt) return; memset(&p,0,sizeof p); p.items=items; p.nitems=cnt; p.multi=1;
    if(!prompt(L"多选消息",L"按住 Ctrl 或 Shift 选择多条消息（最多 64 条）：",NULL,&p,0) || !p.nsel) return;
    for(i=0;i<p.nsel;i++) memcpy(sel[k++],id[p.sel[i]],40);
    memset(&p,0,sizeof p); p.items=acts; p.nitems=3; if(!prompt(L"多选消息",L"对选中的消息：",NULL,&p,0) || !p.nsel) return;
    if(p.sel[0]==0) p2_forward_ids(sel,k);
    else if(p.sel[0]==1){ for(i=0;i<k;i++) lsm_set_msg_flags(C,sel[i],-1,1,err); refresh_chat(); }
    else if(tmsg(L"删除消息",L"只会从本机删除选中的消息，对方仍保留。此操作无法撤销。确定删除？",1)==1){ for(i=0;i<k;i++) lsm_delete_message(C,sel[i]); refresh_chat(); refresh_list(); } }
/* bubble context menu: 30 转发, 31 置顶, 32 收藏, 33 多选 */
static void p2_menu(HMENU pm, const char *id){ lsm_msg *m; int n,i,pin=0,star=0,rec=1;
    lsm_lock(C); n=lsm_messages(C,&m); for(i=0;i<n;i++) if(!strcmp(m[i].id,id)){ pin=m[i].pinned; star=m[i].starred; rec=m[i].recalled; break; } lsm_unlock(C);
    if(i==n||rec) return; AM(pm,MF_STRING,30,L"转发(&F)…"); AM(pm,MF_STRING,31,pin?L"取消置顶(&P)":L"置顶(&P)"); AM(pm,MF_STRING,32,star?L"取消收藏(&S)":L"收藏(&S)"); AM(pm,MF_STRING,33,L"多选(&M)…"); }
static int p2_menu_cmd(int cmd, const char *id){ lsm_msg *m; int n,i,pin=0,star=0; char err[256]="";
    if(cmd==30){ char v[1][40]; memcpy(v[0],id,40); p2_forward_ids(v,1); return 1; }
    if(cmd==33){ p2_multi(); return 1; }
    if(cmd!=31 && cmd!=32) return 0;
    lsm_lock(C); n=lsm_messages(C,&m); for(i=0;i<n;i++) if(!strcmp(m[i].id,id)){ pin=m[i].pinned; star=m[i].starred; break; } lsm_unlock(C);
    if(lsm_set_msg_flags(C,id,cmd==31?!pin:-1,cmd==32?!star:-1,err)) msgbox(err,MB_ICONWARNING); refresh_chat(); return 1; }
static void p2_toggle_conv_pin(void){ int on; if(!cur_conv[0]) return; lsm_lock(C); on=!lsm_conv_pinned(C,cur_conv); lsm_unlock(C); lsm_set_conv_pinned(C,cur_conv,on); refresh_list(); }
/* @mentions: "@" in a group composer pops the member list; picked name is inserted */
static void p2_at_popup(void){ char nm[64][128], fp[64][FP_LEN]; int n,i,cmd; HMENU pm; POINT pt; static wchar_t lab[64][130];
    lsm_lock(C); n=lsm_mention_candidates(C,cur_conv,nm,fp,64); lsm_unlock(C); if(!n) return;
    pm=CreatePopupMenu(); for(i=0;i<n;i++){ wcsncpy(lab[i],W(nm[i]),129); lab[i][129]=0; AM(pm,MF_STRING,100+i,lab[i]); } menu_bg(pm);
    if(!GetCaretPos(&pt)){ pt.x=pt.y=0; } ClientToScreen(hInput,&pt); pt.y+=scale_px(18);
    if(getenv("LSM_UI_TEST")) fprintf(stderr,"UITEST at-menu %d\n",n);
    cmd=TrackPopupMenu(pm,TPM_LEFTALIGN|TPM_RETURNCMD|TPM_NONOTIFY,pt.x,pt.y,0,hMain,NULL); DestroyMenu(pm);
    if(cmd>=100 && cmd<100+n){ wchar_t ins[140]; _snwprintf(ins,140,L"%s ",lab[cmd-100]); ins[139]=0; SendMessageW(hInput,EM_REPLACESEL,TRUE,(LPARAM)ins); } SetFocus(hInput); }
/* send: edit / reply / mentions */
static int p2_send(const char *u, char *err){ char id[40], csv[64*(FP_LEN+1)]; int r;
    if(edit_id[0]){ r=lsm_edit_message(C,edit_id,u,err); if(!r){ edit_id[0]=0; reply_hint[0]=0; } return r; }
    lsm_lock(C); lsm_mentions_in(C,cur_conv,u,csv,sizeof csv); lsm_unlock(C);
    if(reply_id[0]||csv[0]) r=lsm_send_text2(C,cur_conv,u,reply_id[0]?reply_id:NULL,csv[0]?csv:NULL,id,err); else r=lsm_send_text(C,cur_conv,u,id,err);
    if(!r){ if(reply_id[0]) p1_set_reply(NULL); lsm_set_draft(C,cur_conv,""); } return r; }
