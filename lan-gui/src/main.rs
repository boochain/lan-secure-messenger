//! LAN Secure Messenger GUI (egui/eframe).

use eframe::egui;
use lan_core::config::{AppConfig, ConfigPaths};
use lan_core::node::{Node, NodeEvent, NodeHandle};
use lan_core::peer::{PeerInfo, TrustState};
use lan_core::protocol::{ChatMessage, MsgDirection};
use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::mpsc::{self, Receiver, Sender};
use std::time::Duration;
use tracing_subscriber::EnvFilter;

fn main() -> eframe::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(EnvFilter::from_default_env().add_directive("lan_core=info".parse().unwrap()))
        .with_target(false)
        .init();

    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([960.0, 640.0])
            .with_title("局域网加密通讯"),
        ..Default::default()
    };
    eframe::run_native(
        "局域网加密通讯",
        options,
        Box::new(|cc| Ok(Box::new(App::new(cc)))),
    )
}

enum UiCmd {
    SendText(String, String),
    Broadcast(String),
    SendFile(String, PathBuf),
    Trust(String),
    Block(String),
    SetName(String),
    Refresh,
}

enum UiMsg {
    Ready {
        fp: String,
        name: String,
        handle: NodeHandle,
    },
    Event(NodeEvent),
    Peers(Vec<PeerInfo>),
    History(Vec<ChatMessage>),
    Status(String),
    Error(String),
}

struct App {
    my_fp: String,
    my_name: String,
    peers: Vec<PeerInfo>,
    selected: Option<String>,
    chat: HashMap<String, Vec<ChatLine>>,
    compose: String,
    status: String,
    handle: Option<NodeHandle>,
    cmd_tx: Option<Sender<UiCmd>>,
    msg_rx: Receiver<UiMsg>,
    /// Keep runtime alive
    _rt_guard: Option<tokio::runtime::Runtime>,
    show_trust_prompt: Option<PeerInfo>,
}

#[derive(Clone)]
struct ChatLine {
    direction: MsgDirection,
    text: String,
    ts: String,
}

impl App {
    fn new(_cc: &eframe::CreationContext<'_>) -> Self {
        let (msg_tx, msg_rx) = mpsc::channel::<UiMsg>();
        let (cmd_tx, cmd_rx) = mpsc::channel::<UiCmd>();

        let rt = tokio::runtime::Runtime::new().expect("tokio runtime");
        let msg_tx2 = msg_tx.clone();
        rt.spawn(async move {
            if let Err(e) = backend_main(msg_tx2, cmd_rx).await {
                let _ = msg_tx.send(UiMsg::Error(format!("后端启动失败: {e}")));
            }
        });

        Self {
            my_fp: String::new(),
            my_name: String::new(),
            peers: Vec::new(),
            selected: None,
            chat: HashMap::new(),
            compose: String::new(),
            status: "正在启动…".into(),
            handle: None,
            cmd_tx: Some(cmd_tx),
            msg_rx,
            _rt_guard: Some(rt),
            show_trust_prompt: None,
        }
    }

    fn pump(&mut self) {
        while let Ok(msg) = self.msg_rx.try_recv() {
            match msg {
                UiMsg::Ready { fp, name, handle } => {
                    self.my_fp = fp;
                    self.my_name = name;
                    self.handle = Some(handle);
                    self.status = "已就绪 — 正在发现局域网对等端".into();
                    self.send_cmd(UiCmd::Refresh);
                }
                UiMsg::Peers(p) => self.peers = p,
                UiMsg::History(msgs) => {
                    for m in msgs {
                        self.push_chat(
                            &m.peer_fp,
                            ChatLine {
                                direction: m.direction,
                                text: m.body,
                                ts: format_ts(m.ts_ms),
                            },
                        );
                    }
                }
                UiMsg::Status(s) => self.status = s,
                UiMsg::Error(e) => self.status = format!("错误: {e}"),
                UiMsg::Event(ev) => self.on_event(ev),
            }
        }
    }

    fn on_event(&mut self, ev: NodeEvent) {
        match ev {
            NodeEvent::PeerOnline(p) | NodeEvent::PeerUpdated(p) => {
                if let Some(i) = self.peers.iter().position(|x| x.fingerprint == p.fingerprint) {
                    self.peers[i] = p;
                } else {
                    self.peers.push(p);
                }
            }
            NodeEvent::PeerOffline { fingerprint, .. } => {
                if let Some(p) = self.peers.iter_mut().find(|x| x.fingerprint.as_str() == fingerprint)
                {
                    p.online = false;
                }
            }
            NodeEvent::NewPeerNeedsTrust(p) => {
                self.show_trust_prompt = Some(p.clone());
                if let Some(i) = self.peers.iter().position(|x| x.fingerprint == p.fingerprint) {
                    self.peers[i] = p;
                } else {
                    self.peers.push(p);
                }
            }
            NodeEvent::Text {
                from_fp,
                from_name,
                body,
                broadcast,
                ..
            } => {
                let mine = from_fp == self.my_fp;
                let key = if mine {
                    self.selected.clone().unwrap_or_else(|| from_fp.clone())
                } else {
                    from_fp.clone()
                };
                let prefix = if broadcast { "[广播] " } else { "" };
                self.push_chat(
                    &key,
                    ChatLine {
                        direction: if mine {
                            MsgDirection::Out
                        } else {
                            MsgDirection::In
                        },
                        text: format!("{prefix}{body}"),
                        ts: format_ts(chrono::Utc::now().timestamp_millis()),
                    },
                );
                if !mine {
                    self.status = format!("新消息来自 {from_name}");
                }
            }
            NodeEvent::FileReceived {
                from_fp,
                from_name,
                path,
                meta,
                ..
            } => {
                self.push_chat(
                    &from_fp,
                    ChatLine {
                        direction: MsgDirection::In,
                        text: format!("📥 文件 {} → {}", meta.name, path.display()),
                        ts: format_ts(chrono::Utc::now().timestamp_millis()),
                    },
                );
                self.status = format!("已收文件来自 {from_name}: {}", meta.name);
            }
            NodeEvent::FileProgress {
                received, total, ..
            } => {
                if total > 0 {
                    self.status = format!(
                        "传输进度 {}/{} ({}%)",
                        received,
                        total,
                        received * 100 / total
                    );
                }
            }
            NodeEvent::TransferComplete { name, ok, .. } => {
                self.status = format!("传输 {}: {}", name, if ok { "完成" } else { "失败" });
            }
            NodeEvent::Error(e) => self.status = format!("错误: {e}"),
            NodeEvent::Info(i) => self.status = i,
        }
    }

    fn push_chat(&mut self, fp: &str, line: ChatLine) {
        self.chat.entry(fp.to_string()).or_default().push(line);
    }

    fn send_cmd(&self, cmd: UiCmd) {
        if let Some(tx) = &self.cmd_tx {
            let _ = tx.send(cmd);
        }
    }
}

impl eframe::App for App {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        self.pump();
        ctx.request_repaint_after(Duration::from_millis(200));

        // Trust modal
        if let Some(p) = self.show_trust_prompt.clone() {
            egui::Window::new("新设备待确认 (TOFU)")
                .collapsible(false)
                .resizable(false)
                .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
                .show(ctx, |ui| {
                    ui.label(format!("名称: {}", p.display_name));
                    ui.label(format!("指纹: {}", p.fingerprint));
                    ui.label("请与对方当面核对指纹后再信任。");
                    ui.horizontal(|ui| {
                        if ui.button("信任").clicked() {
                            self.send_cmd(UiCmd::Trust(p.fingerprint.as_str().to_string()));
                            self.show_trust_prompt = None;
                        }
                        if ui.button("拉黑").clicked() {
                            self.send_cmd(UiCmd::Block(p.fingerprint.as_str().to_string()));
                            self.show_trust_prompt = None;
                        }
                        if ui.button("稍后").clicked() {
                            self.show_trust_prompt = None;
                        }
                    });
                });
        }

        egui::TopBottomPanel::top("top").show(ctx, |ui| {
            ui.horizontal(|ui| {
                ui.heading("局域网加密通讯");
                ui.separator();
                ui.label(format!("{}  ·  {}", self.my_name, self.my_fp));
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    if ui.button("改名").clicked() {
                        // simple prompt via compose area reuse
                        if !self.compose.is_empty() {
                            self.send_cmd(UiCmd::SetName(self.compose.clone()));
                            self.my_name = self.compose.clone();
                            self.compose.clear();
                        } else {
                            self.status = "在输入框输入新名称后点「改名」".into();
                        }
                    }
                    if ui.button("刷新").clicked() {
                        self.send_cmd(UiCmd::Refresh);
                    }
                });
            });
        });

        egui::TopBottomPanel::bottom("bottom").show(ctx, |ui| {
            ui.label(&self.status);
        });

        egui::SidePanel::left("peers")
            .default_width(220.0)
            .show(ctx, |ui| {
                ui.heading("对等端");
                ui.separator();
                egui::ScrollArea::vertical().show(ui, |ui| {
                    for p in &self.peers {
                        let selected = self.selected.as_deref() == Some(p.fingerprint.as_str());
                        let label = format!(
                            "{} {} {}",
                            if p.online { "🟢" } else { "⚫" },
                            p.display_name,
                            match p.trust {
                                TrustState::Trusted => "✓",
                                TrustState::Unknown => "?",
                                TrustState::Blocked => "✕",
                            }
                        );
                        if ui.selectable_label(selected, label).clicked() {
                            self.selected = Some(p.fingerprint.as_str().to_string());
                        }
                        ui.small(p.fingerprint.as_str());
                        ui.horizontal(|ui| {
                            if ui.small_button("信任").clicked() {
                                self.send_cmd(UiCmd::Trust(p.fingerprint.as_str().to_string()));
                            }
                            if ui.small_button("拉黑").clicked() {
                                self.send_cmd(UiCmd::Block(p.fingerprint.as_str().to_string()));
                            }
                        });
                        ui.separator();
                    }
                });
            });

        egui::CentralPanel::default().show(ctx, |ui| {
            let fp = self.selected.clone();
            if let Some(ref fp) = fp {
                let peer_name = self
                    .peers
                    .iter()
                    .find(|p| p.fingerprint.as_str() == fp)
                    .map(|p| p.display_name.clone())
                    .unwrap_or_else(|| fp.clone());
                ui.heading(format!("与 {peer_name} 的会话"));
                ui.separator();

                let lines = self.chat.get(fp).cloned().unwrap_or_default();
                egui::ScrollArea::vertical()
                    .stick_to_bottom(true)
                    .max_height(ui.available_height() - 80.0)
                    .show(ui, |ui| {
                        for line in &lines {
                            let prefix = match line.direction {
                                MsgDirection::In => "←",
                                MsgDirection::Out => "→",
                            };
                            ui.horizontal(|ui| {
                                ui.small(&line.ts);
                                ui.label(format!("{prefix} {}", line.text));
                            });
                        }
                    });

                ui.separator();
                ui.horizontal(|ui| {
                    let edit = ui.text_edit_singleline(&mut self.compose);
                    if (edit.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter)))
                        || ui.button("发送").clicked()
                    {
                        if !self.compose.is_empty() {
                            self.push_chat(
                                fp,
                                ChatLine {
                                    direction: MsgDirection::Out,
                                    text: self.compose.clone(),
                                    ts: format_ts(chrono::Utc::now().timestamp_millis()),
                                },
                            );
                            self.send_cmd(UiCmd::SendText(fp.clone(), self.compose.clone()));
                            self.compose.clear();
                        }
                    }
                    if ui.button("发文件").clicked() {
                        if let Some(path) = rfd::FileDialog::new().pick_file() {
                            self.send_cmd(UiCmd::SendFile(fp.clone(), path));
                        }
                    }
                    if ui.button("广播").clicked() {
                        if !self.compose.is_empty() {
                            self.send_cmd(UiCmd::Broadcast(self.compose.clone()));
                            self.compose.clear();
                        }
                    }
                });

                // Drag-drop files
                ctx.input(|i| {
                    if !i.raw.dropped_files.is_empty() {
                        for f in &i.raw.dropped_files {
                            if let Some(path) = &f.path {
                                self.send_cmd(UiCmd::SendFile(fp.clone(), path.clone()));
                            }
                        }
                    }
                });
            } else {
                ui.vertical_centered(|ui| {
                    ui.add_space(120.0);
                    ui.heading("选择左侧对等端开始聊天");
                    ui.label("支持文本、图片/文件拖放发送、广播已信任在线节点");
                });
            }
        });
    }
}

async fn backend_main(msg_tx: Sender<UiMsg>, cmd_rx: Receiver<UiCmd>) -> anyhow::Result<()> {
    let paths = if let Ok(dir) = std::env::var("LAN_MSG_CONFIG") {
        let dir = PathBuf::from(dir);
        std::fs::create_dir_all(&dir)?;
        ConfigPaths::from_dir(dir)
    } else {
        ConfigPaths::resolve()?
    };

    let mut config = AppConfig::load(&paths)?;
    if let Ok(name) = std::env::var("LAN_MSG_NAME") {
        config.display_name = name;
    }
    if let Ok(bind) = std::env::var("LAN_MSG_BIND") {
        config.bind_addr = bind;
    }
    if let Ok(p) = std::env::var("LAN_MSG_DISCOVERY_PORT") {
        if let Ok(v) = p.parse() {
            config.discovery_port = v;
        }
    }
    if let Ok(p) = std::env::var("LAN_MSG_LISTEN_PORT") {
        if let Ok(v) = p.parse() {
            config.listen_port = v;
        }
    }
    if let Ok(b) = std::env::var("LAN_MSG_BROADCAST") {
        config.broadcast_addr = Some(b);
    }
    if std::env::var("LAN_MSG_AUTO_TRUST").is_ok() {
        config.auto_trust = true;
    }

    let node = Node::open(paths, config)?;
    let handle = node.start().await?;
    let (fp, name) = handle.fingerprint().await?;
    let _ = msg_tx.send(UiMsg::Ready {
        fp,
        name,
        handle: handle.clone(),
    });

    let mut ev = handle.subscribe();
    let msg_tx2 = msg_tx.clone();
    tokio::spawn(async move {
        while let Ok(e) = ev.recv().await {
            if msg_tx2.send(UiMsg::Event(e)).is_err() {
                break;
            }
        }
    });

    // Command loop in blocking thread via async
    loop {
        // poll cmd channel without blocking runtime too hard
        match cmd_rx.try_recv() {
            Ok(UiCmd::SendText(fp, body)) => {
                match handle.send_text(&fp, &body).await {
                    Ok(_) => {
                        let _ = msg_tx.send(UiMsg::Status("已发送".into()));
                    }
                    Err(e) => {
                        let _ = msg_tx.send(UiMsg::Error(e.to_string()));
                    }
                }
            }
            Ok(UiCmd::Broadcast(body)) => match handle.broadcast_text(&body).await {
                Ok(n) => {
                    let _ = msg_tx.send(UiMsg::Status(format!("已广播给 {n} 人")));
                }
                Err(e) => {
                    let _ = msg_tx.send(UiMsg::Error(e.to_string()));
                }
            },
            Ok(UiCmd::SendFile(fp, path)) => match handle.send_file(&fp, &path).await {
                Ok(meta) => {
                    let _ = msg_tx.send(UiMsg::Status(format!("文件已发送: {}", meta.name)));
                }
                Err(e) => {
                    let _ = msg_tx.send(UiMsg::Error(e.to_string()));
                }
            },
            Ok(UiCmd::Trust(fp)) => {
                let _ = handle.trust_peer(&fp, TrustState::Trusted).await;
                let _ = msg_tx.send(UiMsg::Status(format!("已信任 {fp}")));
                let _ = msg_tx.send(UiMsg::Peers(handle.list_peers().await.unwrap_or_default()));
            }
            Ok(UiCmd::Block(fp)) => {
                let _ = handle.trust_peer(&fp, TrustState::Blocked).await;
                let _ = msg_tx.send(UiMsg::Status(format!("已拉黑 {fp}")));
            }
            Ok(UiCmd::SetName(name)) => {
                let _ = handle.set_display_name(&name).await;
                let _ = msg_tx.send(UiMsg::Status(format!("显示名: {name}")));
            }
            Ok(UiCmd::Refresh) => {
                let peers = handle.list_peers().await.unwrap_or_default();
                let _ = msg_tx.send(UiMsg::Peers(peers));
                if let Ok(h) = handle.history(None, 200).await {
                    let _ = msg_tx.send(UiMsg::History(h));
                }
            }
            Err(mpsc::TryRecvError::Empty) => {
                tokio::time::sleep(Duration::from_millis(50)).await;
            }
            Err(mpsc::TryRecvError::Disconnected) => break,
        }
    }
    handle.shutdown().await;
    Ok(())
}

fn format_ts(ts_ms: i64) -> String {
    use chrono::{Local, TimeZone};
    match Local.timestamp_millis_opt(ts_ms) {
        chrono::LocalResult::Single(dt) => dt.format("%H:%M:%S").to_string(),
        _ => ts_ms.to_string(),
    }
}
