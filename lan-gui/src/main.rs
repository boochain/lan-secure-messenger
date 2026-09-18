//! 局域网加密通讯 GUI：私聊 / 群聊、首次运行、系统托盘。

use eframe::egui;
use lan_core::config::{AppConfig, ConfigPaths};
use lan_core::group::GroupInfo;
use lan_core::node::{Node, NodeEvent, NodeHandle};
use lan_core::peer::{PeerInfo, TrustState};
use lan_core::protocol::{ChatMessage, MsgDirection};
use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::mpsc::{self, Receiver, Sender};
use std::sync::{Arc, Mutex};
use std::time::Duration;
use tracing_subscriber::EnvFilter;
use uuid::Uuid;

fn main() -> eframe::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(EnvFilter::from_default_env().add_directive("lan_core=info".parse().unwrap()))
        .with_target(false)
        .init();

    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([1000.0, 680.0])
            .with_title("局域网加密通讯")
            .with_icon(make_icon()),
        ..Default::default()
    };
    eframe::run_native(
        "局域网加密通讯",
        options,
        Box::new(|cc| Ok(Box::new(App::new(cc)))),
    )
}

fn make_icon() -> egui::IconData {
    let n = 32usize;
    let mut rgba = vec![0u8; n * n * 4];
    for y in 0..n {
        for x in 0..n {
            let dx = x as f32 - 15.5;
            let dy = y as f32 - 15.5;
            let i = (y * n + x) * 4;
            if dx * dx + dy * dy <= 14.0 * 14.0 {
                rgba[i] = 40; rgba[i+1] = 120; rgba[i+2] = 220; rgba[i+3] = 255;
            }
        }
    }
    egui::IconData { rgba, width: 32, height: 32 }
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Tab { Dm, Groups }

#[derive(Clone)]
enum Target { Peer(String), Group(Uuid) }

enum Cmd {
    SendText(String, String),
    Broadcast(String),
    SendFile(String, PathBuf),
    Trust(String),
    Block(String),
    SetName(String),
    Refresh,
    CreateGroup(String, Vec<String>),
    SendGroupText(Uuid, String),
    SendGroupFile(Uuid, PathBuf),
    LeaveGroup(Uuid),
    Invite(Uuid, String),
    FinishFirstRun(String),
    Search(String),
    Backup(PathBuf, String),
    Restore(PathBuf, String),
    MutePeer(String, bool),
    MuteGroup(Uuid, bool),
    SetDownloadDir(PathBuf),
    OpenDownloadDir,
    RemoveGroupMember(Uuid, String),
    CheckUpdate,
}

enum Msg {
    Ready { fp: String, name: String, handle: NodeHandle, first_run: bool },
    Event(NodeEvent),
    Peers(Vec<PeerInfo>),
    Groups(Vec<GroupInfo>),
    History(Vec<ChatMessage>),
    GroupHistory(Uuid, Vec<ChatMessage>),
    Status(String),
    Error(String),
    SearchResults(Vec<ChatMessage>),
    UpdateInfo(String),
}

#[derive(Clone)]
struct Line { dir: MsgDirection, text: String, ts: String }

struct TrayBits {
    #[cfg(feature = "tray")]
    _tray: Option<tray_icon::TrayIcon>,
    show: Arc<Mutex<bool>>,
    quit: Arc<Mutex<bool>>,
}

struct App {
    my_fp: String,
    my_name: String,
    peers: Vec<PeerInfo>,
    groups: Vec<GroupInfo>,
    tab: Tab,
    target: Option<Target>,
    dm: HashMap<String, Vec<Line>>,
    group_msgs: HashMap<Uuid, Vec<Line>>,
    compose: String,
    status: String,
    unread_dm: HashMap<String, u32>,
    unread_g: HashMap<Uuid, u32>,
    name_edit: String,
    handle: Option<NodeHandle>,
    cmd_tx: Option<Sender<Cmd>>,
    msg_rx: Receiver<Msg>,
    _rt: Option<tokio::runtime::Runtime>,
    trust_prompt: Option<PeerInfo>,
    first_run: bool,
    first_name: String,
    new_group: String,
    show_new_group: bool,
    tray: Option<TrayBits>,
    dark: bool,
    search_query: String,
    search_hits: Vec<ChatMessage>,
    show_search: bool,
    muted_peers: std::collections::HashSet<String>,
    muted_groups: std::collections::HashSet<Uuid>,
    backup_password: String,
    update_note: String,
    download_dir_display: String,
}

impl App {
    fn new(cc: &eframe::CreationContext<'_>) -> Self {
        cc.egui_ctx.set_visuals(egui::Visuals::dark());
        let (msg_tx, msg_rx) = mpsc::channel();
        let (cmd_tx, cmd_rx) = mpsc::channel();
        let rt = tokio::runtime::Runtime::new().expect("rt");
        let tx = msg_tx.clone();
        rt.spawn(async move {
            if let Err(e) = backend(tx.clone(), cmd_rx).await {
                let _ = tx.send(Msg::Error(format!("后端失败: {e}")));
            }
        });
        Self {
            my_fp: String::new(),
            my_name: String::new(),
            peers: vec![],
            groups: vec![],
            tab: Tab::Dm,
            target: None,
            dm: HashMap::new(),
            group_msgs: HashMap::new(),
            compose: String::new(),
            status: "启动中…".into(),
            unread_dm: HashMap::new(),
            unread_g: HashMap::new(),
            name_edit: String::new(),
            handle: None,
            cmd_tx: Some(cmd_tx),
            msg_rx,
            _rt: Some(rt),
            trust_prompt: None,
            first_run: false,
            first_name: host(),
            new_group: String::new(),
            show_new_group: false,
            tray: make_tray(),
            dark: true,
            search_query: String::new(),
            search_hits: Vec::new(),
            show_search: false,
            muted_peers: std::collections::HashSet::new(),
            muted_groups: std::collections::HashSet::new(),
            backup_password: String::new(),
            update_note: String::new(),
            download_dir_display: String::new(),
        }
    }

    fn cmd(&self, c: Cmd) {
        if let Some(tx) = &self.cmd_tx { let _ = tx.send(c); }
    }

    fn pump(&mut self, ctx: &egui::Context) {
        if let Some(t) = &self.tray {
            if let Ok(mut q) = t.quit.lock() {
                if *q { *q = false; ctx.send_viewport_cmd(egui::ViewportCommand::Close); }
            }
            if let Ok(mut s) = t.show.lock() {
                if *s {
                    *s = false;
                    ctx.send_viewport_cmd(egui::ViewportCommand::Visible(true));
                    ctx.send_viewport_cmd(egui::ViewportCommand::Focus);
                }
            }
        }
        while let Ok(m) = self.msg_rx.try_recv() {
            match m {
                Msg::Ready { fp, name, handle, first_run } => {
                    self.my_fp = fp; self.my_name = name.clone(); self.name_edit = name.clone();
                    self.first_name = name; self.handle = Some(handle); self.first_run = first_run;
                    self.status = if first_run { "首次运行：设置显示名即可开始".into() } else { "已就绪".into() };
                    self.cmd(Cmd::Refresh);
                }
                Msg::Peers(p) => self.peers = p,
                Msg::Groups(g) => self.groups = g,
                Msg::History(msgs) => {
                    for m in msgs {
                        if m.group_id.is_some() { continue; }
                        self.dm.entry(m.peer_fp.clone()).or_default().push(Line {
                            dir: m.direction, text: m.body, ts: ts(m.ts_ms),
                        });
                    }
                }
                Msg::GroupHistory(gid, msgs) => {
                    let v = self.group_msgs.entry(gid).or_default();
                    v.clear();
                    for m in msgs {
                        v.push(Line { dir: m.direction, text: format!("{}: {}", m.peer_name, m.body), ts: ts(m.ts_ms) });
                    }
                }
                Msg::Status(s) => self.status = s,
                Msg::Error(e) => self.status = format!("错误: {e}"),
                Msg::SearchResults(hits) => {
                    self.search_hits = hits;
                    self.show_search = true;
                    self.status = format!("搜索结果 {} 条", self.search_hits.len());
                }
                Msg::UpdateInfo(s) => {
                    self.update_note = s.clone();
                    self.status = s;
                }
                Msg::Event(ev) => self.on_ev(ev),
            }
        }
    }

    fn on_ev(&mut self, ev: NodeEvent) {
        match ev {
            NodeEvent::PeerOnline(p) | NodeEvent::PeerUpdated(p) => {
                if let Some(i) = self.peers.iter().position(|x| x.fingerprint == p.fingerprint) { self.peers[i] = p; }
                else { self.peers.push(p); }
            }
            NodeEvent::PeerOffline { fingerprint, .. } => {
                if let Some(p) = self.peers.iter_mut().find(|x| x.fingerprint.as_str() == fingerprint) { p.online = false; }
            }
            NodeEvent::NewPeerNeedsTrust(p) => {
                self.trust_prompt = Some(p.clone());
                if let Some(i) = self.peers.iter().position(|x| x.fingerprint == p.fingerprint) { self.peers[i] = p; }
                else { self.peers.push(p); }
            }
            NodeEvent::Text { from_fp, from_name, body, broadcast, .. } => {
                let mine = from_fp == self.my_fp;
                let key = if mine {
                    match &self.target { Some(Target::Peer(fp)) => fp.clone(), _ => from_fp.clone() }
                } else { from_fp.clone() };
                let prefix = if broadcast { "[广播] " } else { "" };
                self.dm.entry(key).or_default().push(Line {
                    dir: if mine { MsgDirection::Out } else { MsgDirection::In },
                    text: format!("{prefix}{body}"), ts: ts(chrono::Utc::now().timestamp_millis()),
                });
                if !mine {
                    let sel = matches!(&self.target, Some(Target::Peer(fp)) if fp == &from_fp) && self.tab == Tab::Dm;
                    if !sel && !self.muted_peers.contains(&from_fp) {
                        *self.unread_dm.entry(from_fp.clone()).or_insert(0) += 1;
                    }
                    if !self.muted_peers.contains(&from_fp) {
                        self.status = format!("消息来自 {from_name}");
                    }
                }
            }
            NodeEvent::TextDelivered { peer_fp, .. } => {
                if let Some(lines) = self.dm.get_mut(&peer_fp) {
                    if let Some(l) = lines.iter_mut().rev().find(|l| l.dir == MsgDirection::Out && !l.text.ends_with(" ✓")) {
                        l.text.push_str(" ✓");
                    }
                }
            }
            NodeEvent::FileReceived { from_fp, from_name, path, meta, .. } => {
                self.dm.entry(from_fp).or_default().push(Line {
                    dir: MsgDirection::In,
                    text: format!("📥 {} → {}", meta.name, path.display()),
                    ts: ts(chrono::Utc::now().timestamp_millis()),
                });
                self.status = format!("文件来自 {from_name}: {}", meta.name);
            }
            NodeEvent::FileProgress { received, total, .. } => {
                if total > 0 { self.status = format!("进度 {received}/{total} ({}%)", received*100/total); }
            }
            NodeEvent::TransferComplete { name, ok, .. } => {
                self.status = format!("传输 {name}: {}", if ok {"完成"} else {"失败"});
            }
            NodeEvent::PeerStatus { peer_fp, kind, detail } => {
                self.status = format!("{peer_fp}: {kind:?} {detail}");
            }
            NodeEvent::GroupInviteReceived(g) | NodeEvent::GroupUpdated(g) => {
                if let Some(i) = self.groups.iter().position(|x| x.id == g.id) { self.groups[i] = g.clone(); }
                else { self.groups.push(g.clone()); }
                self.status = format!("群: {}", g.name);
            }
            NodeEvent::GroupRemoved { group_id, name } => {
                self.groups.retain(|g| g.id != group_id);
                self.group_msgs.remove(&group_id);
                self.status = format!("已离开 {name}");
            }
            NodeEvent::GroupText { group_id, from_fp, from_name, body, .. } => {
                let mine = from_fp == self.my_fp;
                self.group_msgs.entry(group_id).or_default().push(Line {
                    dir: if mine { MsgDirection::Out } else { MsgDirection::In },
                    text: format!("{from_name}: {body}"),
                    ts: ts(chrono::Utc::now().timestamp_millis()),
                });
                if !mine {
                    let sel = matches!(&self.target, Some(Target::Group(id)) if *id == group_id) && self.tab == Tab::Groups;
                    if !sel { *self.unread_g.entry(group_id).or_insert(0) += 1; }
                }
            }
            NodeEvent::GroupFileReceived { group_id, from_name, path, meta, .. } => {
                self.group_msgs.entry(group_id).or_default().push(Line {
                    dir: MsgDirection::In,
                    text: format!("📥 {from_name}: {} → {}", meta.name, path.display()),
                    ts: ts(chrono::Utc::now().timestamp_millis()),
                });
            }
            NodeEvent::Error(e) => self.status = format!("错误: {e}"),
            NodeEvent::Info(i) => self.status = i,
        }
    }
}

impl eframe::App for App {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        self.pump(ctx);
        ctx.request_repaint_after(Duration::from_millis(200));

        if ctx.input(|i| i.viewport().close_requested()) && self.tray.is_some() {
            ctx.send_viewport_cmd(egui::ViewportCommand::CancelClose);
            ctx.send_viewport_cmd(egui::ViewportCommand::Visible(false));
            self.status = "已最小化到托盘".into();
        }

        if self.first_run {
            egui::Window::new("欢迎 · 一键开始")
                .collapsible(false).resizable(false)
                .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
                .show(ctx, |ui| {
                    ui.label("无需编译配置。设置显示名后即可发现局域网好友。");
                    ui.text_edit_singleline(&mut self.first_name);
                    ui.small(format!("指纹: {}", self.my_fp));
                    if ui.button("开始使用").clicked() {
                        let name = if self.first_name.trim().is_empty() { host() } else { self.first_name.trim().into() };
                        self.cmd(Cmd::FinishFirstRun(name.clone()));
                        self.my_name = name; self.first_run = false;
                    }
                });
            return;
        }

        if let Some(p) = self.trust_prompt.clone() {
            egui::Window::new("新设备 TOFU 确认").collapsible(false)
                .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
                .show(ctx, |ui| {
                    ui.label(format!("{} · {}", p.display_name, p.fingerprint));
                    ui.horizontal(|ui| {
                        if ui.button("信任").clicked() { self.cmd(Cmd::Trust(p.fingerprint.as_str().into())); self.trust_prompt=None; }
                        if ui.button("拉黑").clicked() { self.cmd(Cmd::Block(p.fingerprint.as_str().into())); self.trust_prompt=None; }
                        if ui.button("稍后").clicked() { self.trust_prompt=None; }
                        if ui.button("复制").clicked() { ctx.copy_text(p.fingerprint.as_str().into()); }
                    });
                });
        }

        if self.show_new_group {
            egui::Window::new("创建群聊").show(ctx, |ui| {
                ui.text_edit_singleline(&mut self.new_group);
                let members: Vec<String> = self.peers.iter()
                    .filter(|p| p.trust == TrustState::Trusted)
                    .map(|p| p.fingerprint.as_str().to_string()).collect();
                ui.label(format!("将邀请 {} 个已信任成员", members.len()));
                ui.horizontal(|ui| {
                    if ui.button("创建").clicked() && !self.new_group.trim().is_empty() {
                        self.cmd(Cmd::CreateGroup(self.new_group.trim().into(), members));
                        self.new_group.clear(); self.show_new_group=false; self.tab=Tab::Groups;
                    }
                    if ui.button("取消").clicked() { self.show_new_group=false; }
                });
            });
        }

        egui::TopBottomPanel::top("top").show(ctx, |ui| {
            ui.horizontal(|ui| {
                ui.heading("局域网加密通讯");
                ui.selectable_value(&mut self.tab, Tab::Dm, "私聊");
                ui.selectable_value(&mut self.tab, Tab::Groups, "群聊");
                ui.label(format!("{} · {}", self.my_name, self.my_fp));
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    if ui.button("主题").clicked() {
                        self.dark = !self.dark;
                        ctx.set_visuals(if self.dark { egui::Visuals::dark() } else { egui::Visuals::light() });
                    }
                    if ui.button("复制指纹").clicked() { ctx.copy_text(self.my_fp.clone()); }
                    if ui.button("改名").clicked() && !self.name_edit.is_empty() {
                        self.cmd(Cmd::SetName(self.name_edit.clone())); self.my_name = self.name_edit.clone();
                    }
                    if ui.button("刷新").clicked() { self.cmd(Cmd::Refresh); }
                    if ui.button("全部已读").clicked() {
                        self.unread_dm.clear();
                        self.unread_g.clear();
                        self.status = "已全部标为已读".into();
                    }
                    if ui.button("搜索").clicked() { self.show_search = !self.show_search; }
                    if ui.button("检查更新").clicked() { self.cmd(Cmd::CheckUpdate); }
                    if ui.button("打开下载目录").clicked() { self.cmd(Cmd::OpenDownloadDir); }
                    if self.tab == Tab::Groups && ui.button("新建群").clicked() { self.show_new_group = true; }
                });
            });
            ui.horizontal(|ui| { ui.label("显示名"); ui.text_edit_singleline(&mut self.name_edit); });
            if self.show_search {
                ui.horizontal(|ui| {
                    ui.label("搜索");
                    let r = ui.text_edit_singleline(&mut self.search_query);
                    if (r.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter)))
                        || ui.button("查找").clicked()
                    {
                        if !self.search_query.is_empty() {
                            self.cmd(Cmd::Search(self.search_query.clone()));
                        }
                    }
                });
                for m in &self.search_hits {
                    ui.small(format!("{} {} {}: {}", m.ts_ms, m.peer_name, m.peer_fp, m.body));
                }
            }
            if !self.update_note.is_empty() {
                ui.colored_label(egui::Color32::from_rgb(220, 160, 40), &self.update_note);
            }
            ui.horizontal(|ui| {
                ui.label("备份密码");
                ui.add(egui::TextEdit::singleline(&mut self.backup_password).password(true));
                if ui.button("备份身份").clicked() {
                    if let Some(p) = rfd::FileDialog::new().set_file_name("lan-backup.lsmb").save_file() {
                        self.cmd(Cmd::Backup(p, self.backup_password.clone()));
                    }
                }
                if ui.button("恢复备份").clicked() {
                    if let Some(p) = rfd::FileDialog::new().pick_file() {
                        self.cmd(Cmd::Restore(p, self.backup_password.clone()));
                    }
                }
                if ui.button("下载目录…").clicked() {
                    if let Some(p) = rfd::FileDialog::new().pick_folder() {
                        self.download_dir_display = p.display().to_string();
                        self.cmd(Cmd::SetDownloadDir(p));
                    }
                }
                if !self.download_dir_display.is_empty() {
                    ui.small(&self.download_dir_display);
                }
            });
        });
        egui::TopBottomPanel::bottom("bot").show(ctx, |ui| { ui.label(&self.status); });

        egui::SidePanel::left("side").default_width(250.0).show(ctx, |ui| {
            match self.tab {
                Tab::Dm => {
                    ui.heading("私聊");
                    let peers = self.peers.clone();
                    for p in &peers {
                        let badge = self.unread_dm.get(p.fingerprint.as_str()).filter(|n| **n>0).map(|n| format!(" ({n})")).unwrap_or_default();
                        let label = format!("{} {} {}{}", if p.online{"🟢"}else{"⚫"}, p.display_name,
                            match p.trust { TrustState::Trusted=>"✓", TrustState::Unknown=>"?", TrustState::Blocked=>"✕" }, badge);
                        let sel = matches!(&self.target, Some(Target::Peer(fp)) if fp==p.fingerprint.as_str());
                        if ui.selectable_label(sel, label).clicked() {
                            self.target = Some(Target::Peer(p.fingerprint.as_str().into()));
                            self.unread_dm.remove(p.fingerprint.as_str());
                        }
                        ui.small(p.fingerprint.as_str());
                        ui.horizontal(|ui| {
                            if ui.small_button("信任").clicked() { self.cmd(Cmd::Trust(p.fingerprint.as_str().into())); }
                            if ui.small_button("拉黑").clicked() { self.cmd(Cmd::Block(p.fingerprint.as_str().into())); }
                            if ui.small_button("复制").clicked() { ctx.copy_text(p.fingerprint.as_str().into()); }
                            let muted = self.muted_peers.contains(p.fingerprint.as_str());
                            if ui.small_button(if muted {"取消静音"} else {"静音"}).clicked() {
                                self.cmd(Cmd::MutePeer(p.fingerprint.as_str().into(), !muted));
                                if muted { self.muted_peers.remove(p.fingerprint.as_str()); }
                                else { self.muted_peers.insert(p.fingerprint.as_str().into()); }
                            }
                        });
                        ui.separator();
                    }
                }
                Tab::Groups => {
                    ui.heading("群聊");
                    let groups = self.groups.clone();
                    for g in &groups {
                        let badge = self.unread_g.get(&g.id).filter(|n| **n>0).map(|n| format!(" ({n})")).unwrap_or_default();
                        let label = format!("👥 {} · {}人{}", g.name, g.members.len(), badge);
                        let sel = matches!(&self.target, Some(Target::Group(id)) if *id==g.id);
                        if ui.selectable_label(sel, label).clicked() {
                            self.target = Some(Target::Group(g.id));
                            self.unread_g.remove(&g.id);
                        }
                        ui.small(format!("{} 码:{}", g.id, g.join_code));
                        if ui.small_button("退出").clicked() { self.cmd(Cmd::LeaveGroup(g.id)); }
                        ui.separator();
                    }
                }
            }
        });

        egui::CentralPanel::default().show(ctx, |ui| {
            match (&self.tab, self.target.clone()) {
                (Tab::Dm, Some(Target::Peer(fp))) => {
                    let name = self.peers.iter().find(|p| p.fingerprint.as_str()==fp).map(|p| p.display_name.clone()).unwrap_or(fp.clone());
                    ui.heading(format!("私聊 · {name}"));
                    let lines = self.dm.get(&fp).cloned().unwrap_or_default();
                    egui::ScrollArea::vertical().stick_to_bottom(true).max_height(ui.available_height()-90.0).show(ui, |ui| {
                        for l in &lines {
                            let d = match l.dir { MsgDirection::In=>"←", MsgDirection::Out=>"→" };
                            ui.horizontal(|ui| { ui.small(&l.ts); ui.label(format!("{d} {}", l.text)); });
                        }
                    });
                    ui.separator();
                    ui.horizontal(|ui| {
                        let _ = ui.text_edit_singleline(&mut self.compose);
                        if ui.button("发送").clicked() && !self.compose.is_empty() {
                            self.dm.entry(fp.clone()).or_default().push(Line{dir:MsgDirection::Out,text:self.compose.clone(),ts:ts(chrono::Utc::now().timestamp_millis())});
                            self.cmd(Cmd::SendText(fp.clone(), self.compose.clone())); self.compose.clear();
                        }
                        if ui.button("发文件").clicked() {
                            if let Some(p)=rfd::FileDialog::new().pick_file() { self.cmd(Cmd::SendFile(fp.clone(), p)); }
                        }
                        if ui.button("广播").clicked() && !self.compose.is_empty() {
                            self.cmd(Cmd::Broadcast(self.compose.clone())); self.compose.clear();
                        }
                    });
                    ctx.input(|i| {
                        for f in &i.raw.dropped_files {
                            if let Some(p)=&f.path { self.cmd(Cmd::SendFile(fp.clone(), p.clone())); }
                        }
                    });
                }
                (Tab::Groups, Some(Target::Group(gid))) => {
                    let name = self.groups.iter().find(|g| g.id==gid).map(|g| g.name.clone()).unwrap_or_default();
                    ui.heading(format!("群聊 · {name}"));
                    let lines = self.group_msgs.get(&gid).cloned().unwrap_or_default();
                    egui::ScrollArea::vertical().stick_to_bottom(true).max_height(ui.available_height()-120.0).show(ui, |ui| {
                        for l in &lines { ui.horizontal(|ui| { ui.small(&l.ts); ui.label(&l.text); }); }
                    });
                    ui.separator();
                    ui.horizontal(|ui| {
                        let _ = ui.text_edit_singleline(&mut self.compose);
                        if ui.button("发送").clicked() && !self.compose.is_empty() {
                            self.group_msgs.entry(gid).or_default().push(Line{dir:MsgDirection::Out,text:format!("{}: {}", self.my_name, self.compose),ts:ts(chrono::Utc::now().timestamp_millis())});
                            self.cmd(Cmd::SendGroupText(gid, self.compose.clone())); self.compose.clear();
                        }
                        if ui.button("发文件").clicked() {
                            if let Some(p)=rfd::FileDialog::new().pick_file() { self.cmd(Cmd::SendGroupFile(gid, p)); }
                        }
                    });
                    if let Some(g)=self.groups.iter().find(|g| g.id==gid).cloned() {
                        ui.collapsing("邀请已信任成员", |ui| {
                            for p in &self.peers {
                                if p.trust==TrustState::Trusted && !g.members.iter().any(|m| m==p.fingerprint.as_str()) {
                                    if ui.button(format!("邀请 {}", p.display_name)).clicked() {
                                        self.cmd(Cmd::Invite(gid, p.fingerprint.as_str().into()));
                                    }
                                }
                            }
                        });
                    }
                }
                _ => {
                    ui.vertical_centered(|ui| {
                        ui.add_space(120.0);
                        ui.heading(if self.tab==Tab::Dm {"选择左侧好友开始私聊"} else {"选择或新建群聊"});
                        ui.label("关闭窗口 → 托盘后台运行；双击托盘图标恢复。");
                    });
                }
            }
        });
    }
}

fn make_tray() -> Option<TrayBits> {
    #[cfg(feature = "tray")]
    {
        use tray_icon::menu::{Menu, MenuEvent, MenuItem, PredefinedMenuItem};
        use tray_icon::{TrayIconBuilder, TrayIconEvent};
        let show = Arc::new(Mutex::new(false));
        let quit = Arc::new(Mutex::new(false));
        let show2 = show.clone();
        let quit2 = quit.clone();
        let menu = Menu::new();
        let show_i = MenuItem::new("显示窗口", true, None);
        let quit_i = MenuItem::new("退出", true, None);
        let _ = menu.append(&show_i);
        let _ = menu.append(&PredefinedMenuItem::separator());
        let _ = menu.append(&quit_i);
        let show_id = show_i.id().clone();
        let quit_id = quit_i.id().clone();
        let mut rgba = vec![0u8; 32*32*4];
        for y in 0..32 { for x in 0..32 {
            let dx=x as f32-15.5; let dy=y as f32-15.5; let i=(y*32+x)*4;
            if dx*dx+dy*dy<=14.0*14.0 { rgba[i]=40; rgba[i+1]=120; rgba[i+2]=220; rgba[i+3]=255; }
        }}
        let icon = tray_icon::Icon::from_rgba(rgba, 32, 32).ok()?;
        let tray = TrayIconBuilder::new().with_menu(Box::new(menu)).with_tooltip("局域网加密通讯").with_icon(icon).build().ok()?;
        std::thread::spawn(move || {
            let mc = MenuEvent::receiver();
            let tc = TrayIconEvent::receiver();
            loop {
                if let Ok(ev) = mc.try_recv() {
                    if ev.id == show_id { if let Ok(mut s)=show2.lock() { *s=true; } }
                    else if ev.id == quit_id { if let Ok(mut q)=quit2.lock() { *q=true; } }
                }
                if let Ok(TrayIconEvent::Click{..}) = tc.try_recv() {
                    if let Ok(mut s)=show2.lock() { *s=true; }
                }
                std::thread::sleep(Duration::from_millis(120));
            }
        });
        Some(TrayBits { #[cfg(feature = "tray")] _tray: Some(tray), show, quit })
    }
    #[cfg(not(feature = "tray"))]
    { None }
}

async fn backend(msg_tx: Sender<Msg>, cmd_rx: Receiver<Cmd>) -> anyhow::Result<()> {
    let paths = if let Ok(d)=std::env::var("LAN_MSG_CONFIG") {
        let d=PathBuf::from(d); std::fs::create_dir_all(&d)?; ConfigPaths::from_dir(d)
    } else { ConfigPaths::resolve()? };
    let mut config = AppConfig::load(&paths)?;
    if let Ok(n)=std::env::var("LAN_MSG_NAME") { config.display_name = n; }
    if let Ok(b)=std::env::var("LAN_MSG_BIND") { config.bind_addr = b; }
    if std::env::var("LAN_MSG_AUTO_TRUST").is_ok() { config.auto_trust = true; }
    let first_run = !config.first_run_done;
    let node = Node::open(paths.clone(), config)?;
    let handle = node.start().await?;
    let (fp, name) = handle.fingerprint().await?;
    let _ = msg_tx.send(Msg::Ready { fp, name, handle: handle.clone(), first_run });
    let mut ev = handle.subscribe();
    let tx2 = msg_tx.clone();
    tokio::spawn(async move {
        while let Ok(e) = ev.recv().await {
            if tx2.send(Msg::Event(e)).is_err() { break; }
        }
    });
    loop {
        match cmd_rx.try_recv() {
            Ok(Cmd::SendText(fp, body)) => { if let Err(e)=handle.send_text(&fp,&body).await { let _=msg_tx.send(Msg::Error(e.to_string())); } }
            Ok(Cmd::Broadcast(body)) => {
                match handle.broadcast_text_detailed(&body).await {
                    Ok(r) => { let _=msg_tx.send(Msg::Status(format!("广播 成功{} 失败{}", r.ok.len(), r.failed.len()))); }
                    Err(e) => { let _=msg_tx.send(Msg::Error(e.to_string())); }
                }
            }
            Ok(Cmd::SendFile(fp, path)) => { if let Err(e)=handle.send_file(&fp,&path).await { let _=msg_tx.send(Msg::Error(e.to_string())); } }
            Ok(Cmd::Trust(fp)) => { let _=handle.trust_peer(&fp, TrustState::Trusted).await; let _=msg_tx.send(Msg::Peers(handle.list_peers().await.unwrap_or_default())); }
            Ok(Cmd::Block(fp)) => { let _=handle.trust_peer(&fp, TrustState::Blocked).await; }
            Ok(Cmd::SetName(name)) => { let _=handle.set_display_name(&name).await; }
            Ok(Cmd::FinishFirstRun(name)) => {
                let _=handle.set_display_name(&name).await;
                if let Ok(mut cfg)=AppConfig::load(&paths) { cfg.display_name=name; cfg.first_run_done=true; let _=cfg.save(&paths); }
            }
            Ok(Cmd::Refresh) => {
                let _=msg_tx.send(Msg::Peers(handle.list_peers().await.unwrap_or_default()));
                let _=msg_tx.send(Msg::Groups(handle.list_groups().await.unwrap_or_default()));
                if let Ok(h)=handle.history(None, 300).await { let _=msg_tx.send(Msg::History(h)); }
                if let Ok(gs)=handle.list_groups().await {
                    for g in gs { if let Ok(h)=handle.group_history(g.id, 200).await { let _=msg_tx.send(Msg::GroupHistory(g.id, h)); } }
                }
            }
            Ok(Cmd::CreateGroup(name, members)) => {
                match handle.create_group(&name, members).await {
                    Ok(g) => { let _=msg_tx.send(Msg::Status(format!("已建群 {} 码 {}", g.name, g.join_code))); let _=msg_tx.send(Msg::Groups(handle.list_groups().await.unwrap_or_default())); }
                    Err(e) => { let _=msg_tx.send(Msg::Error(e.to_string())); }
                }
            }
            Ok(Cmd::SendGroupText(gid, body)) => { if let Err(e)=handle.send_group_text(gid,&body).await { let _=msg_tx.send(Msg::Error(e.to_string())); } }
            Ok(Cmd::SendGroupFile(gid, path)) => { if let Err(e)=handle.send_group_file(gid,&path).await { let _=msg_tx.send(Msg::Error(e.to_string())); } }
            Ok(Cmd::LeaveGroup(gid)) => { let _=handle.leave_group(gid).await; let _=msg_tx.send(Msg::Groups(handle.list_groups().await.unwrap_or_default())); }
            Ok(Cmd::Invite(gid, fp)) => { if let Err(e)=handle.invite_to_group(gid,&fp).await { let _=msg_tx.send(Msg::Error(e.to_string())); } }
            Err(mpsc::TryRecvError::Empty) => { tokio::time::sleep(Duration::from_millis(40)).await; }
            
            Ok(Cmd::Search(q)) => {
                match handle.search_history(&q, 100).await {
                    Ok(hits) => { let _ = msg_tx.send(Msg::SearchResults(hits)); }
                    Err(e) => { let _ = msg_tx.send(Msg::Error(e.to_string())); }
                }
            }
            Ok(Cmd::Backup(path, password)) => {
                match handle.backup_identity(&path, &password).await {
                    Ok(()) => { let _ = msg_tx.send(Msg::Status(format!("备份完成 {}", path.display()))); }
                    Err(e) => { let _ = msg_tx.send(Msg::Error(e.to_string())); }
                }
            }
            Ok(Cmd::Restore(path, password)) => {
                match handle.restore_identity(&path, &password).await {
                    Ok(()) => { let _ = msg_tx.send(Msg::Status("恢复完成，请重启应用".into())); }
                    Err(e) => { let _ = msg_tx.send(Msg::Error(e.to_string())); }
                }
            }
            Ok(Cmd::MutePeer(fp, mute)) => {
                let _ = handle.mute_peer(&fp, mute).await;
                let _ = msg_tx.send(Msg::Status(if mute { format!("已静音 {fp}") } else { format!("取消静音 {fp}") }));
            }
            Ok(Cmd::MuteGroup(gid, mute)) => {
                let _ = handle.mute_group(gid, mute).await;
            }
            Ok(Cmd::SetDownloadDir(path)) => {
                match handle.set_download_dir(Some(path.clone())).await {
                    Ok(()) => { let _ = msg_tx.send(Msg::Status(format!("下载目录 {}", path.display()))); }
                    Err(e) => { let _ = msg_tx.send(Msg::Error(e.to_string())); }
                }
            }
            Ok(Cmd::OpenDownloadDir) => {
                match handle.get_download_dir().await {
                    Ok(d) => {
                        #[cfg(target_os = "windows")]
                        { let _ = std::process::Command::new("explorer").arg(&d).spawn(); }
                        #[cfg(target_os = "linux")]
                        { let _ = std::process::Command::new("xdg-open").arg(&d).spawn(); }
                        #[cfg(target_os = "macos")]
                        { let _ = std::process::Command::new("open").arg(&d).spawn(); }
                        let _ = msg_tx.send(Msg::Status(format!("下载目录 {}", d.display())));
                    }
                    Err(e) => { let _ = msg_tx.send(Msg::Error(e.to_string())); }
                }
            }
            Ok(Cmd::RemoveGroupMember(gid, fp)) => {
                match handle.remove_group_member(gid, &fp).await {
                    Ok(g) => {
                        let _ = msg_tx.send(Msg::Status(format!("已移除成员，epoch={}", g.key_epoch)));
                        let _ = msg_tx.send(Msg::Groups(handle.list_groups().await.unwrap_or_default()));
                    }
                    Err(e) => { let _ = msg_tx.send(Msg::Error(e.to_string())); }
                }
            }
            Ok(Cmd::CheckUpdate) => {
                let note = tokio::task::spawn_blocking(|| {
                    let url = "https://api.github.com/repos/FREC86/lan-secure-messenger/releases/latest";
                    let out = std::process::Command::new("curl")
                        .args(["-fsSL", "-H", "User-Agent: lan-secure-messenger", url])
                        .output()
                        .ok()?;
                    if !out.status.success() { return None; }
                    let body = String::from_utf8_lossy(&out.stdout);
                    let v: serde_json::Value = serde_json::from_str(&body).ok()?;
                    let tag = v.get("tag_name")?.as_str()?.to_string();
                    let cur = env!("CARGO_PKG_VERSION");
                    let norm = |s: &str| s.trim().trim_start_matches('v').to_string();
                    if norm(&tag) != norm(cur) {
                        Some(format!("有新版本 {tag}（当前 {cur}）https://github.com/FREC86/lan-secure-messenger/releases"))
                    } else {
                        Some(format!("已是最新 {cur}"))
                    }
                }).await.ok().flatten().unwrap_or_else(|| "检查更新失败（可忽略）".into());
                let _ = msg_tx.send(Msg::UpdateInfo(note));
            }

            Err(mpsc::TryRecvError::Disconnected) => break,
        }
    }
    handle.shutdown().await;
    Ok(())
}

fn ts(ms: i64) -> String {
    use chrono::{Local, TimeZone};
    match Local.timestamp_millis_opt(ms) {
        chrono::LocalResult::Single(dt) => dt.format("%H:%M:%S").to_string(),
        _ => ms.to_string(),
    }
}
fn host() -> String {
    hostname::get().ok().and_then(|h| h.into_string().ok()).unwrap_or_else(|| "我的电脑".into())
}
