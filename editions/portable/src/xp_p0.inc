/* v0.6.1 P0 (XP/Win32): 消息历史、个人资料、联系人资料与备注名 — built on the themed prompt() dialog. Included by gui.c. */
static void select_conv(int s);
static void jump_to(const char *conv, const char *id){ int i;
    for(i=0;i<nlist;i++) if(!strcmp(list_ids[i],conv)){ select_conv(i); break; }
    if(i==nlist){ strcpy(cur_conv,conv); refresh_chat(); }
    for(i=0;i<nrows;i++) if(!strcmp(rows[i].id,id)){ SendMessageW(hChat,LB_SETCURSEL,i,0); SendMessageW(hChat,LB_SETTOPINDEX,i>0?i-1:0,0); break; } }
/* filter tokens in the search box, e.g.  会议 类型:图片 方向:收 状态:失败 日期:2026-10-01~2026-10-31 会话:当前 */
static int64_t xp_day(const char *s, int end){ struct tm t; memset(&t,0,sizeof t); if(sscanf(s,"%d-%d-%d",&t.tm_year,&t.tm_mon,&t.tm_mday)!=3) return 0; t.tm_year-=1900; t.tm_mon-=1; if(end) t.tm_mday+=1; t.tm_isdst=-1; return (int64_t)mktime(&t)*1000; }
static void hq_parse(char *in, lsm_hq *q, char *text, size_t tn){ char *tok=strtok(in," "); text[0]=0;
    for(;tok;tok=strtok(NULL," ")){ char *v=strchr(tok,':'); if(!v) v=strstr(tok,"\xef\xbc\x9a"); /* ：full-width */
        if(v){ char key[32]; size_t kl=(size_t)(v-tok); if(kl>=sizeof key) kl=sizeof key-1; memcpy(key,tok,kl); key[kl]=0; v+=(*v==':')?1:3;
            if(!strcmp(key,"类型")){ if(strstr(v,"文字")) q->types|=1<<LSM_T_TEXT; if(strstr(v,"图片")) q->types|=1<<LSM_T_IMAGE; if(strstr(v,"文件")) q->types|=1<<LSM_T_FILE; if(strstr(v,"贴纸")) q->types|=1<<LSM_T_STICKER; continue; }
            if(!strcmp(key,"方向")){ q->direction=strstr(v,"收")?1:strstr(v,"发")?2:0; continue; }
            if(!strcmp(key,"状态")){ if(strstr(v,"已读")) q->statuses|=1<<ST_READ; if(strstr(v,"失败")) q->statuses|=(1<<ST_FAILED)|(1<<ST_REJECTED); if(strstr(v,"拒收")) q->statuses|=1<<ST_REJECTED; continue; }
            if(!strcmp(key,"日期")){ char *t2=strchr(v,'~'); if(t2){ *t2++=0; q->to_ms=xp_day(t2,1); } q->from_ms=xp_day(v,0); if(!t2) q->to_ms=xp_day(v,1); continue; }
            if(!strcmp(key,"会话")){ if(strstr(v,"当前") && cur_conv[0]) q->convs=cur_conv; continue; } }
        if(text[0]) strncat(text," ",tn-strlen(text)-1); strncat(text,tok,tn-strlen(text)-1); } }
static void history_dialog(void){ prompt_t p; wchar_t init[512]=L""; char in[1600], text[1600];
    memset(&p,0,sizeof p); if(cur_conv[0]) wcscpy(init,L"会话:当前 ");
    if(!prompt(L"消息历史 (Ctrl+Shift+F)",L"输入关键词（支持中文），可加筛选：\r\n类型:文字/图片/文件/贴纸  方向:收/发  状态:已读/失败/拒收\r\n日期:2026-10-01~2026-10-31  会话:当前",init,&p,1)) return;
    U(p.text,in,sizeof in);
    { lsm_hq q; int offset=0; for(;;){ int idx[30], n, tot=0, i, k; static char lines[32][400]; const char *items[32]; char ids[30][40], cvs[30][64]; lsm_msg *m; prompt_t r; wchar_t lab[200]; char tmp[1600];
        memset(&q,0,sizeof q); snprintf(tmp,sizeof tmp,"%s",in); hq_parse(tmp,&q,text,sizeof text); q.query=text; q.offset=offset; q.limit=30;
        lsm_lock(C); n=lsm_history_query(C,&q,idx,30,&tot); lsm_messages(C,&m);
        for(i=0;i<n;i++){ lsm_msg *x=&m[idx[i]]; char ts[32], body[200]; time_t tt=(time_t)(x->ts_ms/1000); strftime(ts,sizeof ts,"%m-%d %H:%M",localtime(&tt)); snprintf(body,sizeof body,"%s",x->kind==K_FILE?x->file_name:(x->body?x->body:"")); for(k=0;body[k];k++) if(body[k]=='\r'||body[k]=='\n') body[k]=' ';
            snprintf(lines[i],400,"%s  %s：%s",ts,x->out?"我":x->sender_name,body); items[i]=lines[i]; snprintf(ids[i],40,"%s",x->id); snprintf(cvs[i],64,"%s",x->conv); }
        lsm_unlock(C);
        if(!tot){ tmsg(L"消息历史",L"没有找到匹配的消息。",0); return; }
        k=n; if(offset+n<tot){ snprintf(lines[k],400,"下一页…（已显示 %d / %d）",offset+n,tot); items[k]=lines[k]; k++; }
        memset(&r,0,sizeof r); r.items=items; r.nitems=k; _snwprintf(lab,200,L"共 %d 条结果，选择一条后按“确定”跳转到聊天：",tot); lab[199]=0;
        if(!prompt(L"消息历史",lab,NULL,&r,0) || r.nsel<1) return;
        if(r.sel[0]>=n){ offset+=n; continue; }
        jump_to(cvs[r.sel[0]],ids[r.sel[0]]); return; } } }
static void my_profile_dialog(void){ prompt_t p; const char *items[4]={"修改昵称…","修改个性签名…","更换头像…（PNG/JPG，≤48 KB）","移除头像"}; char u8[1600], info[1400], err[256]=""; wchar_t init[400];
    for(;;){ lsm_lock(C); snprintf(info,sizeof info,"昵称：%s\r\n个性签名：%s\r\n指纹：%s\r\n版本：%s",lsm_my_name(C),lsm_my_signature(C)[0]?lsm_my_signature(C):"（未设置）",lsm_my_fp(C),LSM_APP_VERSION); lsm_unlock(C);
        memset(&p,0,sizeof p); p.items=items; p.nitems=4; if(!prompt(L"个人资料",W(info),NULL,&p,0) || p.nsel<1) return;
        if(p.sel[0]==0){ prompt_t e; memset(&e,0,sizeof e); lsm_lock(C); wcsncpy(init,W(lsm_my_name(C)),399); init[399]=0; lsm_unlock(C); if(prompt(L"修改昵称",L"新的显示名称：",init,&e,1)){ lsm_set_name(C,U(e.text,u8,sizeof u8)); refresh_list(); } }
        else if(p.sel[0]==1){ prompt_t e; memset(&e,0,sizeof e); lsm_lock(C); wcsncpy(init,W(lsm_my_signature(C)),399); init[399]=0; lsm_unlock(C); if(prompt(L"个性签名",L"最多 120 字，会同步给联系人：",init,&e,1) && lsm_set_profile(C,U(e.text,u8,sizeof u8),NULL,err)) msgbox(err,MB_ICONWARNING); }
        else if(p.sel[0]==2){ OPENFILENAMEW of; wchar_t path[MAX_PATH]=L""; memset(&of,0,sizeof of); of.lStructSize=sizeof of; of.hwndOwner=hMain; of.lpstrFile=path; of.nMaxFile=MAX_PATH; of.lpstrFilter=L"图片 (*.png;*.jpg;*.jpeg;*.bmp;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0"; of.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_EXPLORER;
            if(GetOpenFileNameW(&of) && lsm_set_profile(C,NULL,U(path,u8,sizeof u8),err)) msgbox("头像需小于 48 KB（XP 版不缩放图片，请先缩小到约 96×96）。",MB_ICONWARNING); }
        else if(p.sel[0]==3) lsm_set_profile(C,NULL,"",err); } }
static void contact_profile_dialog(void){ const char *fp0=selected_peer(); char fp[FP_LEN], conv[64], info[2000]="", u8[800]; const char *items[4]={"修改备注名…","共享的文件与图片…","消息历史…","发消息"}; lsm_contact *k; int n,i; prompt_t p;
    if(!fp0) return; snprintf(fp,sizeof fp,"%s",fp0); snprintf(conv,sizeof conv,"dm:%s",fp);
    for(;;){ lsm_lock(C); n=lsm_contacts(C,&k); for(i=0;i<n;i++) if(!strcmp(k[i].fp,fp)) snprintf(info,sizeof info,"备注名：%s\r\n昵称：%s\r\n个性签名：%s\r\n状态：%s%s\r\n身份：%s\r\n版本：%s\r\n指纹：%s",k[i].remark[0]?k[i].remark:"（无）",k[i].name,k[i].signature[0]?k[i].signature:"（未设置或对方版本较旧）",k[i].online?"在线":"离线",k[i].online?(k[i].via_relay?" · 经服务器":" · 局域网直连"):"",k[i].trust==1?"已验证身份":k[i].trust==2?"已屏蔽":"未验证",k[i].app_version[0]?k[i].app_version:"—",fp); lsm_unlock(C);
        memset(&p,0,sizeof p); p.items=items; p.nitems=4; if(!prompt(L"联系人资料",W(info),NULL,&p,0) || p.nsel<1) return;
        if(p.sel[0]==0){ prompt_t e; wchar_t init[200]=L""; memset(&e,0,sizeof e); lsm_lock(C); n=lsm_contacts(C,&k); for(i=0;i<n;i++) if(!strcmp(k[i].fp,fp)){ wcsncpy(init,W(k[i].remark),199); init[199]=0; } lsm_unlock(C);
            if(prompt(L"备注名",L"仅本机可见；留空则显示对方昵称：",init,&e,1)){ lsm_set_remark(C,fp,U(e.text,u8,sizeof u8)); refresh_list(); refresh_chat(); } }
        else if(p.sel[0]==1){ static char lines[64][320]; const char *it[64], *paths[64]; static char pth[64][520]; lsm_msg *m; int nm, c=0; prompt_t r;
            lsm_lock(C); nm=lsm_messages(C,&m); for(i=nm-1;i>=0&&c<64;i--) if(m[i].kind==K_FILE && !strcmp(m[i].conv,conv) && !lsm_is_sticker_name(m[i].file_name)){ snprintf(lines[c],320,"%s %s%s",lsm_msg_type(&m[i])==LSM_T_IMAGE?"[图片]":"[文件]",m[i].file_name,m[i].file_path[0]?"":"（未接收）"); snprintf(pth[c],520,"%s",m[i].file_path); it[c]=lines[c]; paths[c]=pth[c]; c++; } lsm_unlock(C);
            if(!c){ tmsg(L"共享的文件与图片",L"暂无。",0); continue; }
            memset(&r,0,sizeof r); r.items=it; r.nitems=c; if(prompt(L"共享的文件与图片",L"选择后按“确定”打开：",NULL,&r,0) && r.nsel>0 && paths[r.sel[0]][0]) open_path_safe(paths[r.sel[0]]); }
        else if(p.sel[0]==2){ history_dialog(); return; }
        else { SetFocus(hInput); return; } } }
