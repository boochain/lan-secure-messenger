/* v0.6.1 P1 (XP/Win32): 回复/引用、表情回应、编辑/撤回、语音消息 (waveIn WAV + PlaySound). Included by gui.c before refresh_chat(). */
#include <mmsystem.h>
#define IDC_MIC 111
static char reply_id[40]; static wchar_t reply_hint[200];
static const char *P1_EMO[]={"\xf0\x9f\x91\x8d","\xe2\x9d\xa4\xef\xb8\x8f","\xf0\x9f\x98\x82","\xf0\x9f\x98\xae","\xf0\x9f\x98\xa2","\xf0\x9f\x8e\x89",NULL};
static HWND hMic;
static void refresh_chat(void);
/* v0.6.1 P2 hooks (xp_p2.inc, included after xp_p0.inc) */
static void p2_menu(HMENU pm, const char *id); static int p2_menu_cmd(int cmd, const char *id); static void p2_on_typing(char *ev);
static char edit_id[40]; /* inline edit in the composer (no 512-character prompt limit) */
/* bubble text: 「quote」 line, body (recalled / edited markers), reaction summary */
static wchar_t *p1_rowtext(const lsm_msg *m){ static char b[9000]; char db[8192], rs[300]; size_t o=0; b[0]=0;
    if(m->reply_to[0] && !m->recalled) o+=snprintf(b+o,sizeof b-o,"\xe2\x86\xaa \xe3\x80\x8c%.180s\xe3\x80\x8d\n",m->quote[0]?m->quote:"原消息");
    o+=snprintf(b+o,sizeof b-o,"%s",lsm_display_body(m,db,sizeof db)); if(o>=sizeof b) o=sizeof b-1;
    lsm_reaction_summary(m,rs,sizeof rs); if(rs[0] && !m->recalled){ o+=snprintf(b+o,sizeof b-o,"\n%s",rs); if(o>=sizeof b) o=sizeof b-1; }
    if(!m->recalled && (m->forwarded||m->pinned||m->starred||m->mentions_me)) snprintf(b+o,sizeof b-o,"\n\xe2\x80\x94%s%s%s%s",m->forwarded?" 转发":"",m->pinned?" · 置顶":"",m->starred?" · 收藏":"",m->mentions_me?" · @我":"");
    return wdup(W(b)); }
static void p1_edit_cancel(void);
static void p1_set_reply(const char *id){ lsm_msg *m; int n,i; if(id && edit_id[0]) p1_edit_cancel(); snprintf(reply_id,sizeof reply_id,"%s",id?id:""); reply_hint[0]=0;
    if(reply_id[0]){ lsm_lock(C); n=lsm_messages(C,&m); for(i=0;i<n;i++) if(!strcmp(m[i].id,reply_id)){ char b[200], t[300]; snprintf(t,sizeof t,"回复 %s：%.60s  (Esc 取消)",m[i].out?"我":m[i].sender_name,m[i].kind==K_FILE?m[i].file_name:lsm_display_body(&m[i],b,sizeof b)); wcsncpy(reply_hint,W(t),199); reply_hint[199]=0; break; } lsm_unlock(C); }
    if(hInput){ InvalidateRect(hInput,NULL,TRUE); SetFocus(hInput); } }
static void p1_edit(const char *id){ lsm_msg *m; int n,i,len; wchar_t *w=NULL;
    lsm_lock(C); n=lsm_messages(C,&m); for(i=0;i<n;i++) if(!strcmp(m[i].id,id)){ const char *b=m[i].body?m[i].body:""; len=MultiByteToWideChar(CP_UTF8,0,b,-1,NULL,0); w=(wchar_t*)malloc((len+1)*sizeof(wchar_t)); if(w) MultiByteToWideChar(CP_UTF8,0,b,-1,w,len+1); break; } lsm_unlock(C);
    if(!w) return; reply_id[0]=0; snprintf(edit_id,sizeof edit_id,"%s",id); wcsncpy(reply_hint,L"编辑消息：Enter 保存 · Esc 取消",199);
    if(hInput){ SetWindowTextW(hInput,w); SendMessageW(hInput,EM_SETSEL,(WPARAM)-1,(LPARAM)-1); SetFocus(hInput); InvalidateRect(hInput,NULL,TRUE); } free(w); }
static void p1_edit_cancel(void){ edit_id[0]=0; reply_hint[0]=0; if(hInput){ SetWindowTextW(hInput,L""); InvalidateRect(hInput,NULL,TRUE); } }
static void p1_recall(const char *id){ char err[256]; if(tmsg(L"撤回消息",L"双方都会看到“撤回了一条消息”。24 小时内可撤回。确定撤回？",1)==1){ if(lsm_recall_message(C,id,err)) msgbox(err,MB_ICONWARNING); refresh_chat(); } }
/* context-menu items: 10 回复, 11 编辑, 12 撤回, 20..25 reactions, 26 remove my reaction */
static void p1_menu(HMENU pm, const char *id){ lsm_msg *m; int n,i,can=0,rec=1,k; char mine[40]=""; HMENU sub;
    lsm_lock(C); n=lsm_messages(C,&m); for(i=0;i<n;i++) if(!strcmp(m[i].id,id)){ can=lsm_can_modify(C,&m[i]); rec=m[i].recalled; lsm_my_reaction(C,&m[i],mine,sizeof mine); break; } lsm_unlock(C);
    if(rec) return; AM(pm,MF_SEPARATOR,0,NULL); AM(pm,MF_STRING,10,L"回复(&R)");
    sub=CreatePopupMenu(); for(k=0;P1_EMO[k];k++){ static wchar_t lab[6][24]; _snwprintf(lab[k],24,L"%s%s",W(P1_EMO[k]),strcmp(mine,P1_EMO[k])?L"":L"  ✓"); lab[k][23]=0; AM(sub,MF_STRING,20+k,lab[k]); }
    if(mine[0]) AM(sub,MF_STRING,26,L"取消我的回应"); menu_bg(sub); AM(pm,MF_POPUP,(UINT_PTR)sub,L"表情回应(&E)");
    if(can){ AM(pm,MF_STRING,11,L"编辑(&T)…"); AM(pm,MF_STRING,12,L"撤回(&W)"); } }
static int p1_menu_cmd(int cmd, const char *id){ char err[256];
    if(cmd==10){ p1_set_reply(id); return 1; } if(cmd==11){ p1_edit(id); return 1; } if(cmd==12){ p1_recall(id); return 1; }
    if(cmd>=20 && cmd<=26){ if(lsm_react(C,id,cmd==26?"":P1_EMO[cmd-20],err)) msgbox(err,MB_ICONWARNING); refresh_chat(); return 1; }
    return 0; }
static int p1_edit_last(void){ lsm_msg *m; int n,i; char id[40]=""; lsm_lock(C); n=lsm_messages(C,&m); for(i=n-1;i>=0;i--) if(!strcmp(m[i].conv,cur_conv) && lsm_can_modify(C,&m[i])){ memcpy(id,m[i].id,40); break; } lsm_unlock(C);
    if(!id[0]) return 0; p1_edit(id); return 1; }

/* ---- voice: waveIn 16 kHz mono 16-bit, up to 60 s, saved as lsm-voice-<N>s-….wav ---- */
#define REC_SECS 60
#define REC_BUF 32000
static HWAVEIN rec_h; static WAVEHDR rec_hdr[REC_SECS]; static char *rec_mem; static DWORD rec_t0;
static void put32(FILE *f, unsigned v){ fputc(v&255,f); fputc((v>>8)&255,f); fputc((v>>16)&255,f); fputc((v>>24)&255,f); }
static void put16(FILE *f, unsigned v){ fputc(v&255,f); fputc((v>>8)&255,f); }
static void p1_rec_stop(void){ int i; DWORD tot=0; wchar_t tmp[MAX_PATH], path[MAX_PATH]; char nm[128], u8[MAX_PATH*3], err[256]; FILE *f; int secs;
    waveInReset(rec_h); for(i=0;i<REC_SECS;i++){ tot+=rec_hdr[i].dwBytesRecorded; waveInUnprepareHeader(rec_h,&rec_hdr[i],sizeof(WAVEHDR)); } waveInClose(rec_h); rec_h=NULL;
    SetWindowTextW(hMic,L"语音"); InvalidateRect(hMic,NULL,TRUE);
    secs=(int)(tot/32000); if(tot%32000>=16000) secs++;
    if(secs<1){ free(rec_mem); rec_mem=NULL; msgbox("录音太短。",MB_ICONINFORMATION); return; }
    lsm_voice_name(secs,"wav",nm,sizeof nm); GetTempPathW(MAX_PATH,tmp); _snwprintf(path,MAX_PATH,L"%s%s",tmp,W(nm)); path[MAX_PATH-1]=0;
    f=_wfopen(path,L"wb"); if(f){ fwrite("RIFF",1,4,f); put32(f,36+tot); fwrite("WAVEfmt ",1,8,f); put32(f,16); put16(f,1); put16(f,1); put32(f,16000); put32(f,32000); put16(f,2); put16(f,16); fwrite("data",1,4,f); put32(f,tot);
        for(i=0;i<REC_SECS;i++) fwrite(rec_hdr[i].lpData,1,rec_hdr[i].dwBytesRecorded,f); fclose(f);
        if(lsm_send_file(C,cur_conv,U(path,u8,sizeof u8),err)) msgbox(err,MB_ICONWARNING); refresh_chat(); }
    free(rec_mem); rec_mem=NULL; }
static void p1_mic(void){ WAVEFORMATEX wf; int i;
    if(rec_h){ p1_rec_stop(); return; }
    if(!cur_conv[0]){ msgbox("请先在左侧选择联系人或群组。",MB_ICONINFORMATION); return; }
    memset(&wf,0,sizeof wf); wf.wFormatTag=WAVE_FORMAT_PCM; wf.nChannels=1; wf.nSamplesPerSec=16000; wf.wBitsPerSample=16; wf.nBlockAlign=2; wf.nAvgBytesPerSec=32000;
    if(waveInOpen(&rec_h,WAVE_MAPPER,&wf,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){ rec_h=NULL; msgbox("无法打开麦克风（没有录音设备或被占用）。",MB_ICONWARNING); return; }
    rec_mem=(char*)malloc(REC_SECS*REC_BUF); memset(rec_hdr,0,sizeof rec_hdr);
    for(i=0;i<REC_SECS;i++){ rec_hdr[i].lpData=rec_mem+i*REC_BUF; rec_hdr[i].dwBufferLength=REC_BUF; waveInPrepareHeader(rec_h,&rec_hdr[i],sizeof(WAVEHDR)); waveInAddBuffer(rec_h,&rec_hdr[i],sizeof(WAVEHDR)); }
    waveInStart(rec_h); rec_t0=GetTickCount(); SetWindowTextW(hMic,L"停止"); InvalidateRect(hMic,NULL,TRUE); SetTimer(hMain,0x5101,500,NULL); }
/* WM_TIMER 0x5101: stop at the 60 s cap */
static void p1_rec_tick(void){ if(!rec_h){ KillTimer(hMain,0x5101); return; } if(GetTickCount()-rec_t0>=REC_SECS*1000u-200){ KillTimer(hMain,0x5101); p1_rec_stop(); } }
static void p1_play(const char *path){ const char *e=strrchr(path,'.'); if(e && !_stricmp(e,".wav")){ wchar_t wp[MAX_PATH]; MultiByteToWideChar(CP_UTF8,0,path,-1,wp,MAX_PATH); if(PlaySoundW(wp,NULL,SND_FILENAME|SND_ASYNC|SND_NODEFAULT)) return; }
    open_path_safe(path); }
