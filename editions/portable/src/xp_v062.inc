/* v0.6.2 (XP Win32): polls (right-click a poll to vote), new poll, disappearing timer, archive, chat folders + list filter.
 * Included by gui.c after xp_p2.inc. Poll text / timer line come from lsm_display_body (shared core). */
#define IDM_POLL 258
#define IDM_TTL 259
#define IDM_ARCHIVE 260
#define IDM_FOLDERS 261
#define IDM_FILTER 262
static char conv_filter[64];
static void v062_menu(HMENU pm, const char *id){ lsm_msg *m; int n,i,k=0,poll=0,closed=0,out=0,mine[10]; char opt[10][320];
    lsm_lock(C); n=lsm_messages(C,&m);
    for(i=0;i<n;i++) if(!strcmp(m[i].id,id)){ poll=m[i].is_poll&&!m[i].recalled; closed=m[i].poll_closed; out=m[i].out; if(poll) for(k=0;k<10&&lsm_poll_option(&m[i],k,opt[k],sizeof opt[k]);k++) mine[k]=lsm_my_vote(C,&m[i],k); break; }
    lsm_unlock(C); if(!poll) return;
    if(!closed) for(i=0;i<k;i++){ char t[400]; snprintf(t,sizeof t,"%s\xe6\x8a\x95\xe7\xa5\xa8\xef\xbc\x9a%d. %s",mine[i]?"\xe2\x88\x9a ":"",i+1,opt[i]); AM(pm,MF_STRING,60+i,W(t)); } /* 投票： */
    if(out && !closed) AM(pm,MF_STRING,70,L"结束投票(&E)");
    AM(pm,MF_SEPARATOR,0,NULL); }
static int v062_menu_cmd(int cmd, const char *id){ char err[256]="";
    if(cmd>=60 && cmd<70){ if(lsm_vote_toggle(C,id,cmd-60,err)) msgbox(err,MB_ICONWARNING); refresh_chat(); return 1; }
    if(cmd==70){ if(lsm_close_poll(C,id,err)) msgbox(err,MB_ICONWARNING); refresh_chat(); return 1; }
    return 0; }
/* drop chats hidden by the folder / archive filter (lock held; after p2_list_sort) */
static void v062_list_filter(int *sel, const char *keep){ int i, j=0;
    for(i=0;i<nlist;i++){ if(!lsm_conv_visible(C,list_ids[i],conv_filter) && strcmp(list_ids[i],keep)) continue;
        { int t=lsm_conv_ttl(C,list_ids[i]), a=lsm_conv_archived(C,list_ids[i]); if(t||a){ wchar_t nm[160]; _snwprintf(nm,160,L"%s%s%s",t?L"[定时] ":L"",a?L"[归档] ":L"",convs[i].name); nm[159]=0; wcsncpy(convs[i].name,nm,159); convs[i].name[159]=0; } }
        if(j!=i){ convs[j]=convs[i]; strcpy(list_ids[j],list_ids[i]); } j++; }
    nlist=j; *sel=-1; SendMessageW(hList,LB_RESETCONTENT,0,0);
    for(i=0;i<nlist;i++){ SendMessageW(hList,LB_ADDSTRING,0,(LPARAM)convs[i].name); if(!strcmp(list_ids[i],keep)) *sel=i; } }
static int v062_pick(const wchar_t *title, const wchar_t *label, const char **items, int n){ prompt_t p; memset(&p,0,sizeof p); p.items=items; p.nitems=n; if(!prompt(title,label,NULL,&p,0)||p.nsel<1) return -1; return p.sel[0]; }
static void v062_new_poll(void){ prompt_t p; char q[1000], opts[2000], err[256], id[40]; const char *mm[]={"单选","多选（可选多个）"}; int multi; char *s;
    if(!cur_conv[0]) return; memset(&p,0,sizeof p);
    if(!prompt(L"发起投票",L"问题：",L"",&p,1)) return; U(p.text,q,sizeof q);
    memset(&p,0,sizeof p); if(!prompt(L"发起投票",L"选项（2–10 个，用 | 或中文逗号“，”分隔）：",L"",&p,1)) return; U(p.text,opts,sizeof opts);
    for(s=opts;*s;s++) if(*s=='|') *s='\n';
    { char tmp[2000]; size_t o=0; const char *r=opts; while(*r && o<sizeof tmp-1){ if(!strncmp(r,"\xef\xbc\x8c",3)){ tmp[o++]='\n'; r+=3; } else tmp[o++]=*r++; } tmp[o]=0; strcpy(opts,tmp); }
    multi=v062_pick(L"发起投票",L"投票方式：",mm,2); if(multi<0) return;
    if(lsm_send_poll(C,cur_conv,q,opts,multi==1,id,err)) msgbox(err,MB_ICONWARNING); refresh_chat(); refresh_list(); }
static void v062_ttl(void){ static const int T[]={0,30,300,3600,86400,604800}; const char *it[]={"关闭","30 秒","5 分钟","1 小时","1 天","7 天"}; int r; char err[256];
    if(!cur_conv[0]) return; r=v062_pick(L"定时删除",L"新消息在多久后自动删除（双方同步；只影响之后的消息）：",it,6); if(r<0) return;
    if(lsm_set_disappearing(C,cur_conv,T[r],err)) msgbox(err,MB_ICONWARNING); refresh_chat(); refresh_list(); }
static void v062_archive(void){ int a; if(!cur_conv[0]) return; lsm_lock(C); a=lsm_conv_archived(C,cur_conv); lsm_unlock(C); lsm_set_conv_archived(C,cur_conv,!a);
    msgbox(a?"已取消归档。":"已归档。收到新消息会自动回到列表；“文件 → 筛选会话”可查看已归档的会话。",MB_ICONINFORMATION); refresh_list(); }
static void v062_folders(void){ prompt_t p; char f[400], u8[512], names[32][64], lab[1400]; int n,i; size_t o;
    if(!cur_conv[0]) return; lsm_lock(C); lsm_conv_folders(C,cur_conv,f,sizeof f); n=lsm_folder_names(C,names,32); lsm_unlock(C);
    o=(size_t)snprintf(lab,sizeof lab,"当前会话所属的分组（英文逗号分隔，留空 = 不属于任何分组；只保存在本机）%s",n?"。已有：":"："); for(i=0;i<n&&o<sizeof lab;i++) o+=(size_t)snprintf(lab+o,sizeof lab-o,"%s%s",i?"，":"",names[i]);
    memset(&p,0,sizeof p); if(!prompt(L"会话分组",W(lab),W(f),&p,1)) return; lsm_set_conv_folders(C,cur_conv,U(p.text,u8,sizeof u8)); refresh_list(); }
static void v062_filter(void){ char names[32][64], al[64]; const char *it[34]; int n,i,k=0,arch,r;
    lsm_lock(C); n=lsm_folder_names(C,names,32); arch=lsm_archived_count(C); lsm_unlock(C);
    it[k++]="全部会话"; for(i=0;i<n;i++) it[k++]=names[i]; snprintf(al,sizeof al,"已归档（%d）",arch); it[k++]=al;
    r=v062_pick(L"筛选会话",L"会话列表只显示：",it,k); if(r<0) return;
    if(r==0) conv_filter[0]=0; else if(r==k-1) strcpy(conv_filter,LSM_FILTER_ARCHIVED); else snprintf(conv_filter,sizeof conv_filter,"%s",names[r-1]); refresh_list(); }
