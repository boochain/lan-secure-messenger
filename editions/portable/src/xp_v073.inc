/* v0.7.3: icon-led in-window pages (settings, profile, files, contacts, transfers, groups).
 * Icons are GDI-drawn (no symbol fonts) so Wine / XP never show tofu. */
enum { IC_PERSON=1, IC_EDIT, IC_LOCK, IC_CLOUD, IC_FOLDER, IC_NET, IC_GROUP, IC_QR, IC_INFO,
       IC_STORAGE, IC_DIAG, IC_SEARCH, IC_COPY, IC_CHAT, IC_TRASH, IC_SHIELD, IC_ADD, IC_DOWNLOAD,
       IC_TICK, IC_MUTE, IC_IMAGE, IC_PIN };
static HWND hV073Page;
static char v073_kind[32];
typedef struct { int icon, id, qa; RECT rc; wchar_t title[48], sub[200]; char fp[72]; } v073_row;
static v073_row v073_rows[28];
static int v073_n, v073_hit, v073_scroll;
static void v073_glyph(HDC dc, RECT r, int k, COLORREF fg){
    HPEN pn=CreatePen(PS_SOLID, scale_px(2)>1?scale_px(2):2, fg); HGDIOBJ op=SelectObject(dc,pn), ob=SelectObject(dc,GetStockObject(NULL_BRUSH));
    int x=r.left, y=r.top, w=r.right-r.left, h=r.bottom-r.top;
    /* map 24-unit glyph into the rect with 2-unit padding */
    #define X(u) (x + (int)((u)*w/24.0))
    #define Y(u) (y + (int)((u)*h/24.0))
    switch(k){
    case IC_PERSON: Ellipse(dc,X(8.8),Y(4.8),X(15.2),Y(11.2)); Arc(dc,X(5),Y(14),X(19),Y(28),X(19),Y(20),X(5),Y(20)); break;
    case IC_EDIT: { POINT p[5]={{X(14),Y(5)},{X(19),Y(10)},{X(8),Y(21)},{X(4),Y(21)},{X(4),Y(17)}}; Polyline(dc,p,5); MoveToEx(dc,X(4),Y(17),NULL); LineTo(dc,X(14),Y(5)); } break;
    case IC_LOCK: Rectangle(dc,X(6),Y(11),X(18),Y(21)); Arc(dc,X(8),Y(4),X(16),Y(12),X(16),Y(11),X(8),Y(11)); break;
    case IC_CLOUD: { POINT p[4]={{X(6),Y(17)},{X(8),Y(11)},{X(16),Y(10)},{X(18),Y(17)}}; Polyline(dc,p,4); MoveToEx(dc,X(6),Y(17),NULL); LineTo(dc,X(18),Y(17)); } break;
    case IC_FOLDER: { POINT p[7]={{X(4),Y(8)},{X(10),Y(8)},{X(12),Y(11)},{X(20),Y(11)},{X(20),Y(20)},{X(4),Y(20)},{X(4),Y(8)}}; Polyline(dc,p,7); } break;
    case IC_NET: Ellipse(dc,X(4),Y(4),X(20),Y(20)); MoveToEx(dc,X(12),Y(4),NULL); LineTo(dc,X(12),Y(20)); MoveToEx(dc,X(4),Y(12),NULL); LineTo(dc,X(20),Y(12)); break;
    case IC_GROUP: Ellipse(dc,X(5),Y(6),X(11),Y(12)); Ellipse(dc,X(13),Y(6),X(19),Y(12)); Arc(dc,X(2),Y(14),X(13),Y(26),X(13),Y(20),X(2),Y(20)); Arc(dc,X(11),Y(14),X(22),Y(26),X(22),Y(20),X(11),Y(20)); break;
    case IC_QR: Rectangle(dc,X(4),Y(4),X(10),Y(10)); Rectangle(dc,X(14),Y(4),X(20),Y(10)); Rectangle(dc,X(4),Y(14),X(10),Y(20));
        { HBRUSH b=CreateSolidBrush(fg); RECT a={X(14),Y(14),X(17),Y(17)}, c={X(18),Y(18),X(20),Y(20)}; FillRect(dc,&a,b); FillRect(dc,&c,b); DeleteObject(b);} break;
    case IC_INFO: Ellipse(dc,X(4),Y(4),X(20),Y(20)); { HBRUSH b=CreateSolidBrush(fg); RECT d={X(11),Y(7),X(13),Y(9)}; FillRect(dc,&d,b); DeleteObject(b);} MoveToEx(dc,X(12),Y(11),NULL); LineTo(dc,X(12),Y(17)); break;
    case IC_STORAGE: Ellipse(dc,X(4),Y(4),X(20),Y(20)); { HBRUSH b=CreateSolidBrush(fg); Pie(dc,X(4),Y(4),X(20),Y(20),X(12),Y(4),X(20),Y(12)); DeleteObject(b);} break;
    case IC_DIAG: { POINT p[5]={{X(5),Y(18)},{X(9),Y(10)},{X(12),Y(14)},{X(16),Y(6)},{X(19),Y(12)}}; Polyline(dc,p,5);} break;
    case IC_SEARCH: Ellipse(dc,X(5),Y(5),X(17),Y(17)); MoveToEx(dc,X(15),Y(15),NULL); LineTo(dc,X(20),Y(20)); break;
    case IC_COPY: Rectangle(dc,X(8),Y(8),X(18),Y(20)); MoveToEx(dc,X(6),Y(16),NULL); LineTo(dc,X(6),Y(4)); LineTo(dc,X(16),Y(4)); break;
    case IC_CHAT: { POINT p[7]={{X(5),Y(6)},{X(19),Y(6)},{X(19),Y(16)},{X(10),Y(16)},{X(6),Y(20)},{X(6),Y(16)},{X(5),Y(16)}}; Polyline(dc,p,7); LineTo(dc,X(5),Y(6)); } break;
    case IC_TRASH: MoveToEx(dc,X(6),Y(8),NULL); LineTo(dc,X(18),Y(8)); MoveToEx(dc,X(9),Y(8),NULL); LineTo(dc,X(10),Y(5)); LineTo(dc,X(14),Y(5)); LineTo(dc,X(15),Y(8));
        MoveToEx(dc,X(8),Y(8),NULL); LineTo(dc,X(9),Y(20)); LineTo(dc,X(15),Y(20)); LineTo(dc,X(16),Y(8)); break;
    case IC_SHIELD: { POINT p[5]={{X(12),Y(4)},{X(19),Y(7)},{X(19),Y(13)},{X(12),Y(20)},{X(5),Y(13)}}; Polyline(dc,p,5); LineTo(dc,X(5),Y(7)); LineTo(dc,X(12),Y(4)); } break;
    case IC_ADD: MoveToEx(dc,X(12),Y(5),NULL); LineTo(dc,X(12),Y(19)); MoveToEx(dc,X(5),Y(12),NULL); LineTo(dc,X(19),Y(12)); break;
    case IC_DOWNLOAD: MoveToEx(dc,X(12),Y(5),NULL); LineTo(dc,X(12),Y(16)); MoveToEx(dc,X(8),Y(12),NULL); LineTo(dc,X(12),Y(16)); LineTo(dc,X(16),Y(12)); MoveToEx(dc,X(6),Y(19),NULL); LineTo(dc,X(18),Y(19)); break;
    case IC_TICK: MoveToEx(dc,X(5),Y(12),NULL); LineTo(dc,X(10),Y(17)); LineTo(dc,X(19),Y(7)); break;
    case IC_MUTE: { POINT p[6]={{X(4),Y(10)},{X(8),Y(10)},{X(13),Y(6)},{X(13),Y(18)},{X(8),Y(14)},{X(4),Y(14)}}; Polygon(dc,p,6);
        MoveToEx(dc,X(16),Y(8),NULL); LineTo(dc,X(21),Y(16)); MoveToEx(dc,X(21),Y(8),NULL); LineTo(dc,X(16),Y(16)); } break;
    case IC_IMAGE: Rectangle(dc,X(5),Y(6),X(19),Y(18)); Ellipse(dc,X(7),Y(8),X(11),Y(12)); MoveToEx(dc,X(5),Y(16),NULL); LineTo(dc,X(10),Y(12)); LineTo(dc,X(14),Y(15)); LineTo(dc,X(19),Y(11)); break;
    case IC_PIN: { POINT p[6]={{X(12),Y(5)},{X(16),Y(11)},{X(13),Y(11)},{X(13),Y(18)},{X(11),Y(18)},{X(11),Y(11)}}; Polyline(dc,p,6); LineTo(dc,X(8),Y(11)); LineTo(dc,X(12),Y(5)); } break;
    default: Ellipse(dc,X(6),Y(6),X(18),Y(18)); break;
    }
    #undef X
    #undef Y
    SelectObject(dc,op); SelectObject(dc,ob); DeleteObject(pn);
}
static void v073_tile(HDC dc, RECT r, int k){
    bubble(dc,r,P.tile,P.stroke);
    { RECT g=r; InflateRect(&g,-scale_px(6),-scale_px(6)); v073_glyph(dc,g,k,P.tilefg); }
}
static void v073_add(int icon, const wchar_t *title, const wchar_t *sub, int id){
    if(v073_n>=28) return;
    v073_rows[v073_n].icon=icon; v073_rows[v073_n].id=id; v073_rows[v073_n].qa=0; v073_rows[v073_n].fp[0]=0;
    wcsncpy(v073_rows[v073_n].title,title,47); v073_rows[v073_n].title[47]=0;
    wcsncpy(v073_rows[v073_n].sub,sub?sub:L"",199); v073_rows[v073_n].sub[199]=0;
    v073_n++;
}
static void v073_fill(const char *name){
    v073_n=0; v073_hit=-1; v073_scroll=0; snprintf(v073_kind,sizeof v073_kind,"%s",name);
    if(!strcmp(name,"settings")){
        char host[256]="", subu[200]; wchar_t sub[200]; int en=0,port=0,hp=0; const char *cur=lsm_account_mode(C);
        v073_add(IC_PERSON,L"个人资料",L"名称、指纹、头像与二维码",IDM_PROFILE);
        v073_add(IC_EDIT,L"修改昵称",L"更改本机显示名称",IDM_NAME);
        v073_add(IC_LOCK, strcmp(cur,"account")?L"设备模式":L"账号模式",
                 strcmp(cur,"account")?L"仅本机保存身份，点按可切换为账号模式":L"身份在中转上加密备份，点按可切回本机", IDM_ACCTMODE);
        v073_add(IC_LOCK,L"恢复密钥",L"查看、复制或导出；仅本机可见",IDM_REVEALRK);
        lsm_relay_get(C,&en,host,sizeof host,&port,&hp); (void)port;(void)hp;
        snprintf(subu,sizeof subu,"%s", en?(host[0]?host:"已启用"):"未启用 · 局域网直连优先");
        MultiByteToWideChar(CP_UTF8,0,subu,-1,sub,200);
        v073_add(IC_CLOUD,L"中转服务器",sub,IDM_RELAY);
        v073_add(IC_QR,L"服务器二维码",L"让另一台设备扫码接入同一中转",IDM_SERVERQR);
        v073_add(IC_QR,L"配对新设备",L"一次性配对码，需账号模式并已连中转",IDM_PAIRQR);
        v073_add(IC_LOCK,L"生成 PIN",L"让对方输入，配对并互相信任",IDM_PINOFFER);
        v073_add(IC_ADD,L"按 IP 添加",L"不在同一网段时手动添加",IDM_ADDIP);
        v073_add(IC_GROUP,L"创建群组",L"从已信任的联系人中选择",IDM_GCREATE);
        v073_add(IC_QR,L"扫描链接",L"添加联系人、接入中转或配对新设备",IDM_ADDURI);
        v073_add(IC_FOLDER,L"接收的文件",L"打开下载文件夹",IDM_DLDIR);
        v073_add(IC_STORAGE,L"存储管理",L"查看占用并清理半成品",IDM_STORAGE);
        v073_add(IC_SHIELD,L"防火墙",L"添加本机防火墙例外",IDM_FIREWALL);
        v073_add(IC_DIAG,L"诊断信息",L"连接与排障",IDM_DIAG);
        v073_add(IC_INFO,L"检查更新",L"查看是否有新版本",IDM_UPDATE);
        v073_add(IC_INFO,L"关于",L"版本与协议说明",IDM_ABOUT);
    }else if(!strcmp(name,"profile")){
        char nameu[128]="", fp[80]="", bu[240]; wchar_t w[240];
        lsm_lock(C); snprintf(nameu,sizeof nameu,"%s",lsm_my_name(C)); snprintf(fp,sizeof fp,"%s",lsm_my_fp(C)); lsm_unlock(C);
        snprintf(bu,sizeof bu,"%s",nameu); MultiByteToWideChar(CP_UTF8,0,bu,-1,w,240);
        v073_add(IC_EDIT,L"修改昵称",w,IDM_NAME);
        v073_add(IC_QR,L"我的二维码",L"让对方扫描以添加你",IDM_MYQR);
        v073_add(IC_COPY,L"复制指纹",W(fp),-10); /* special */
        v073_add(IC_PERSON,L"完整资料",L"头像与个性签名",IDM_PROFILE);
        snprintf(bu,sizeof bu,"指纹  %s",fp); MultiByteToWideChar(CP_UTF8,0,bu,-1,w,240);
        v073_add(IC_LOCK,L"身份指纹",w,0);
        snprintf(bu,sizeof bu,"版本 %s",LSM_APP_VERSION); MultiByteToWideChar(CP_UTF8,0,bu,-1,w,240);
        v073_add(IC_INFO,L"版本",w,0);
    }else if(!strcmp(name,"files")){
        char info[2048]=""; wchar_t w[400];
        lsm_storage_info(C,info,sizeof info);
        MultiByteToWideChar(CP_UTF8,0,info[0]?info:"（存储信息不可用）",-1,w,400);
        v073_add(IC_STORAGE,L"存储用量",w,IDM_STORAGE);
        v073_add(IC_FOLDER,L"接收文件夹",L"打开已下载的文件",IDM_DLDIR);
        v073_add(IC_DOWNLOAD,L"文件传输",L"查看进行中的发送和接收",IDM_XFERS);
    }else if(!strcmp(name,"transfers")){
        v073_add(IC_DOWNLOAD,L"打开传输窗口",L"查看进行中的发送和接收",IDM_XFERS);
        v073_add(IC_FOLDER,L"接收文件夹",L"打开发布到本机的文件",IDM_DLDIR);
    }else if(!strcmp(name,"groups")){
        v073_add(IC_GROUP,L"创建群组",L"从已信任的联系人中选择成员",IDM_GCREATE);
        v073_add(IC_ADD,L"邀请成员",L"向当前群组添加联系人",IDM_GINVITE);
        v073_add(IC_TRASH,L"退出群组",L"离开当前选中的群",IDM_GLEAVE);
    }else if(!strcmp(name,"contacts")){
        lsm_contact *k; int n,i;
        lsm_lock(C); n=lsm_contacts(C,&k);
        for(i=0;i<n && v073_n<28;i++){ char subu[160]; wchar_t t[48],s[200];
            snprintf(subu,sizeof subu,"%s · %s", k[i].trust==TRUST_TRUSTED?"已信任":k[i].trust==TRUST_BLOCKED?"已屏蔽":"未信任", k[i].online?"在线":"离线");
            MultiByteToWideChar(CP_UTF8,0,lsm_display_name(&k[i]),-1,t,48);
            MultiByteToWideChar(CP_UTF8,0,subu,-1,s,200);
            v073_add(k[i].trust==TRUST_TRUSTED?IC_SHIELD:IC_PERSON,t,s,-20-i);
            snprintf(v073_rows[v073_n-1].fp,sizeof v073_rows[v073_n-1].fp,"%s",k[i].fp);
        }
        lsm_unlock(C);
        if(!v073_n) v073_add(IC_PERSON,L"还没有联系人",L"同一网段下运行本软件的设备会自动出现在这里。",0);
    }
}
static void v073_click_id(int id, int idx){
    if(id==-10){ char fp[80]=""; lsm_lock(C); snprintf(fp,sizeof fp,"%s",lsm_my_fp(C)); lsm_unlock(C); copy_text(W(fp)); return; }
    if(id<=-20){ const char *fp=v073_rows[idx].fp; if(fp[0]){ snprintf(cur_conv,sizeof cur_conv,"dm:%s",fp); v070_show_page("chats"); refresh_list(); refresh_chat(); } return; }
    if(id>0) SendMessageW(hMain,WM_COMMAND,(WPARAM)id,0);
}
static int v073_content_h(void){ return scale_px(44)+v073_n*scale_px(60)+scale_px(16); }
static void v073_sync_scroll(HWND h){
    RECT cr; SCROLLINFO si; int maxs, page;
    if(!h) return; GetClientRect(h,&cr); page=cr.bottom>0?cr.bottom:1;
    maxs=v073_content_h()-page; if(maxs<0) maxs=0;
    if(v073_scroll<0) v073_scroll=0; if(v073_scroll>maxs) v073_scroll=maxs;
    memset(&si,0,sizeof si); si.cbSize=sizeof si; si.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;
    si.nMin=0; si.nMax=v073_content_h()>0?v073_content_h()-1:0; si.nPage=(UINT)page; si.nPos=v073_scroll;
    SetScrollInfo(h,SB_VERT,&si,TRUE);
}
static void v073_scroll_by(HWND h, int delta){
    v073_scroll+=delta; v073_sync_scroll(h); InvalidateRect(h,NULL,TRUE);
}
static LRESULT CALLBACK v073_proc(HWND h, UINT m, WPARAM w, LPARAM l){
    if(m==WM_PAINT){
        PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps); RECT cr,r; int i, y, row_h=scale_px(60), tile=scale_px(36), pad=scale_px(16);
        const wchar_t *title=L"";
        GetClientRect(h,&cr); FillRect(dc,&cr,brWin);
        SetBkMode(dc,TRANSPARENT);
        if(!strcmp(v073_kind,"settings")) title=L"设置";
        else if(!strcmp(v073_kind,"profile")) title=L"个人资料";
        else if(!strcmp(v073_kind,"files")) title=L"文件站";
        else if(!strcmp(v073_kind,"transfers")) title=L"文件传输";
        else if(!strcmp(v073_kind,"groups")) title=L"群组";
        else if(!strcmp(v073_kind,"contacts")) title=L"联系人";
        r=cr; r.left=pad; r.top=scale_px(10); r.bottom=r.top+scale_px(28); r.right=cr.right-pad;
        SetTextColor(dc,T.text_primary); SelectObject(dc,fBodyStrong); DrawTextW(dc,title,-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        v073_sync_scroll(h);
        y=scale_px(44)-v073_scroll;
        for(i=0;i<v073_n;i++){
            RECT tr, sr, ir;
            v073_rows[i].rc.left=0; v073_rows[i].rc.right=cr.right; v073_rows[i].rc.top=y; v073_rows[i].rc.bottom=y+row_h;
            if(v073_hit==i){ HBRUSH b=CreateSolidBrush(P.rowhover); FillRect(dc,&v073_rows[i].rc,b); DeleteObject(b); }
            ir.left=pad; ir.top=y+(row_h-tile)/2; ir.right=ir.left+tile; ir.bottom=ir.top+tile;
            v073_tile(dc,ir,v073_rows[i].icon);
            tr.left=ir.right+scale_px(12); tr.right=cr.right-pad; tr.top=y+scale_px(8); tr.bottom=tr.top+scale_px(22);
            SetTextColor(dc,T.text_primary); SelectObject(dc,fBodyStrong);
            DrawTextW(dc,v073_rows[i].title,-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
            sr=tr; sr.top=tr.bottom; sr.bottom=y+row_h-scale_px(6);
            SetTextColor(dc,T.text_secondary); SelectObject(dc,fBody);
            DrawTextW(dc,v073_rows[i].sub,-1,&sr,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_NOPREFIX);
            y+=row_h;
        }
        EndPaint(h,&ps); return 0;
    }
    if(m==WM_LBUTTONDOWN || m==WM_MOUSEMOVE){
        POINT pt={(short)LOWORD(l),(short)HIWORD(l)}; int i, hit=-1;
        for(i=0;i<v073_n;i++) if(PtInRect(&v073_rows[i].rc,pt) && (v073_rows[i].id || v073_rows[i].fp[0])) hit=i;
        if(m==WM_MOUSEMOVE && hit!=v073_hit){ v073_hit=hit; InvalidateRect(h,NULL,FALSE); }
        if(m==WM_LBUTTONDOWN && hit>=0){ v073_click_id(v073_rows[hit].id, hit); }
        return 0;
    }
    if(m==WM_MOUSELEAVE){ v073_hit=-1; InvalidateRect(h,NULL,FALSE); return 0; }
    if(m==WM_VSCROLL){
        int n=v073_scroll, page; RECT cr; GetClientRect(h,&cr); page=cr.bottom>0?cr.bottom:1;
        switch(LOWORD(w)){
        case SB_LINEUP: n-=scale_px(20); break;
        case SB_LINEDOWN: n+=scale_px(20); break;
        case SB_PAGEUP: n-=page; break;
        case SB_PAGEDOWN: n+=page; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: n=(int)HIWORD(w); break;
        default: return 0;
        }
        v073_scroll_by(h, n-v073_scroll); return 0;
    }
    if(m==WM_MOUSEWHEEL){ short d=(short)HIWORD(w); v073_scroll_by(h, -(int)d*scale_px(20)/120); return 0; }
    if(m==WM_SIZE){ v073_sync_scroll(h); return 0; }
    if(m==WM_ERASEBKGND){ RECT r; GetClientRect(h,&r); FillRect((HDC)w,&r,brWin); return 1; }
    return DefWindowProcW(h,m,w,l);
}
static void v073_ensure(void){
    static int reg; WNDCLASSEXW wc;
    if(hV073Page) return;
    if(!reg){ memset(&wc,0,sizeof wc); wc.cbSize=sizeof wc; wc.lpfnWndProc=v073_proc; wc.hInstance=hInst; wc.lpszClassName=L"LsmIconPage"; wc.hCursor=LoadCursor(NULL,IDC_HAND); wc.hbrBackground=brWin; RegisterClassExW(&wc); reg=1; }
    hV073Page=CreateWindowExW(0,L"LsmIconPage",L"",WS_CHILD|WS_VSCROLL,0,0,0,0,hMain,(HMENU)(INT_PTR)498,hInst,NULL);
}
static int v073_uses(const char *name){
    return !strcmp(name,"settings")||!strcmp(name,"profile")||!strcmp(name,"files")||!strcmp(name,"transfers")||!strcmp(name,"groups")||!strcmp(name,"contacts");
}
static void v073_on_show(const char *name){
    if(!v073_uses(name)){ if(hV073Page) ShowWindow(hV073Page,SW_HIDE); return; }
    v073_ensure(); v073_fill(name);
    if(hV070Page) ShowWindow(hV070Page,SW_HIDE);
    ShowWindow(hV073Page,SW_SHOW); BringWindowToTop(hV073Page); InvalidateRect(hV073Page,NULL,TRUE);
}
static void v073_place(int T0, int W_, int H_, int sb){
    if(!hV073Page || !IsWindowVisible(hV073Page)) return;
    SetWindowPos(hV073Page,HWND_TOP,0,T0,W_,H_-sb,SWP_SHOWWINDOW);
    v073_sync_scroll(hV073Page); InvalidateRect(hV073Page,NULL,TRUE);
}
/* TrackMouseEvent so hover clears. Called from proc via WM_MOUSEMOVE first time — skip, simple hover is enough. */

static int v073_page_visible(void){ return hV073Page && IsWindowVisible(hV073Page); }
