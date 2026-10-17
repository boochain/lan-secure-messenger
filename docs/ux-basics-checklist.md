# UX basics checklist (per platform) — self-audit 2026-10-18

Sources: Android developer guides (Back & predictive back, configuration changes, saved state & process death, window
insets/IME, POST_NOTIFICATIONS, ACTION_SEND, notification tap → PendingIntent), Material 3; Windows App SDK / Fluent
(keyboard accessibility, ContentDialog, keyboard accelerators, windowing, high DPI, drag & drop, context menus,
notifications); Win32 UX guidelines (dialog keys, IsDialogMessage, tray, single instance); GNOME HIG + GTK4 docs
(dialogs, keyboard, shortcuts window, primary menu, GApplication single instance, DnD, popovers).

Status legend: **PASS** · **FAIL** · **FIXED** (was failing, fixed + regression) · **CODE** = judged from source only, not yet
exercised at runtime (runtime test pending) · evidence in brackets.

## Android (Compose, lan-secure-messenger-xp/android)

| # | Item | Status | Evidence |
|---|------|--------|----------|
| A1 | System Back from a chat returns to the list (does not exit) | **FIXED** dd14471 | Chat.kt BackHandler; adb test `Back from chat returns to the list` (tests/android_chat_shots.py) |
| A2 | Back closes an open menu / dialog / sheet first | **PASS** (Popup/Dialog dismissOnBackPress) | adb test `Back closes the menu, stays in the chat` |
| A3 | Back closes in-chat / list search before navigating | **FIXED** | BackHandler order: search → tab → filter → exit |
| A4 | Back on a non-root tab (群组/传输/设置) goes to 聊天 | **FIXED** | adb test `Back on another tab returns to 聊天` |
| A5 | Double-Back to exit at the root, toast "再按一次退出", 2 s window | **FIXED** | adb tests `first Back … does not exit`, `second Back within 2 s exits` |
| A6 | Predictive back opt-in (API 33+) | **FIXED** | manifest `enableOnBackInvokedCallback="true"`; androidx BackHandler is predictive-aware |
| A7 | Rotation / config change keeps the open chat, tab, search | **FIXED** (open chat was `remember`, lost on rotation) | rememberSaveable; adb test `rotation keeps the open chat` |
| A8 | Process death restores the open chat | **FIXED** (CODE) | rememberSaveable → saved instance state; runtime test (`am kill` in background) pending |
| A9 | Keyboard/IME: composer stays above the IME, edge-to-edge insets | **PASS** | adjustResize + imePadding/navigationBarsPadding; CI chat shots |
| A10 | POST_NOTIFICATIONS requested on Android 13+ | **PASS** (CODE) | MainActivity requests it on API 33+; manifest declares it |
| A11 | Share intent (ACTION_SEND files + text/plain) → contact picker | **PASS** | android_chat_shots.py seeds via ACTION_SEND text/plain |
| A12 | Notification tap opens that chat | **CODE** | CoreService openApp(c, conv) → MainActivity onNewIntent "conv" (singleTop); runtime test pending |
| A13 | Destructive actions confirmed (屏蔽, 退出群组, 移除) | **FIXED** | 屏蔽 and 退出群组 ran immediately; now a confirmation dialog (Chat.kt "block"/"leave"); 移除 goes through a picker |
| A14 | Empty states | **PASS** | android-*-01-empty CI shots |

> **Correction (05:59):** the first desktop pass grepped with `\|` (literal in ripgrep, not alternation), so several
> desktop items were wrongly marked FAIL. Re-checked with proper patterns below.

## WinUI 3 (lan-secure-messenger/ui-winui)

| # | Item | Status | Evidence |
|---|------|--------|----------|
| W1 | Esc closes dialogs | **PASS** (CODE) | all dialogs are ContentDialog (Esc = Close); Esc also clears chat search (51b1df0) |
| W2 | Enter sends, Shift+Enter new line | **PASS** (CODE) | ChatPage Composer_PreviewKeyDown (VirtualKey.Enter + Shift) |
| W3 | Tab order / focus visuals | **CODE** – runtime check pending | default XAML tab order |
| W4 | Keyboard shortcuts | **FIXED** 51b1df0 | Ctrl+F search, Ctrl+N add contact, Ctrl+Tab / Ctrl+Shift+Tab next/prev chat (Page.KeyboardAccelerators) |
| W5 | Window close vs tray | **FIXED** 51b1df0 | tray existed (H.NotifyIcon, close = hide); added setting 关闭窗口时最小化到通知区域 + first-time notice |
| W6 | Remember window size/position | **FIXED** 51b1df0 | config/window.json; restored only if the rect is on a display; maximized state kept |
| W7 | High DPI | **PASS** (CODE) | WinUI per-monitor DPI |
| W8 | Drag & drop files into the chat | **PASS** (CODE) | ChatPage AllowDrop + Conversation_Drop |
| W9 | Right-click menus | **FIXED** 51b1df0/efb2a08 | messages: 复制 / 打开 / 在文件夹中显示 / 复制路径 / 删除（仅本机, confirmed; daemon delete_message, real-daemon test); conversations: 信任 / 免打扰 / 屏蔽 (confirmed) |
| W10 | Copy message text | **PASS** | IsTextSelectionEnabled + 复制 menu |
| W11 | Tooltips | **PASS** (CODE) | ToolTipService on icon buttons in ChatPage |
| W12 | Single instance | **PASS** (CODE) | App.xaml.cs named Mutex + Show event |
| W13 | Confirm destructive actions | **PASS** (CODE) | ContentDialogs; block + delete confirmed |
| W14 | Plain-language errors | **PASS** | Chinese sentences, no codes |
| W15 | Empty / loading states | **PASS** (CODE) | "选择左侧的联系人或群组开始聊天" empty state |
| W16 | Scroll to bottom on new message | **PASS** (CODE) | MessagesAppended → ScrollIntoView(last) |
| W17 | Notification click opens the chat | **PASS** (CODE) | Toasts.cs activation → App.MainWin.OpenConversation(conv) |

Runtime verification on Windows CI (scripts/ui_smoke_windows.ps1) is still to be added for W4/W6/W9.

## XP Win32 (lan-secure-messenger-xp/src/gui.c)

| # | Item | Status | Evidence |
|---|------|--------|----------|
| X1 | Esc closes dialogs | **PASS** (CODE) | IDCANCEL handling |
| X2 | Enter sends, Shift+Enter new line | **PASS** (CODE) | VK_RETURN + GetKeyState(VK_SHIFT) |
| X4 | Keyboard shortcuts / accelerators | **FAIL** | no accelerator table |
| X5 | Close vs tray | **PASS** (CODE) | Shell_NotifyIcon, WM_CLOSE |
| X6 | Remember window placement | **FAIL** | no Get/SetWindowPlacement persistence |
| X7 | High DPI | **PARTIAL** | fonts/metrics scaled by LOGPIXELSY; no DPI-aware manifest |
| X8 | Drag & drop files | **FAIL** | no DragAcceptFiles / WM_DROPFILES |
| X9 | Right-click context menu (messages/contacts) | **PARTIAL → FIXED for messages** a789d79 | chat EDIT subclassed: WM_CONTEXTMENU (mouse + Shift+F10) → 复制 / 全选 / 删除这条消息（仅本机）…; contact-list menu still pending |
| X10 | Copy message text | **FIXED** a789d79 | 复制 (WM_COPY) in the chat menu, Ctrl+A select-all, Ctrl+C native |
| X11 | Tooltips | **FAIL** | no tooltip control |
| X12 | Single instance | **PASS** (CODE) | CreateMutex |
| X13 | Confirm destructive actions | **PASS** (CODE) | tmsg(…,cancel=1) confirms 退出群组 and 删除消息 (re-checked: the custom prompt is the yes/no) |
| X17 | Notification (balloon) | **PASS** (CODE) | NIIF balloons; click → open chat: to verify |

## Linux GTK 4 + libadwaita (linux/adw_gui.c, `lan-messenger-adw`; GTK3 linux/gtk_gui.c still ships until the port is finished)

Runtime = checked by tests/adw_gui_shots.py under Xvfb (real key presses / clicks via xdotool, the app logs the action it ran
and the focused widget); CI job `linux-adw` (ubuntu-24.04). CODE = implemented, no runtime check yet.

| # | Item | Status | Evidence |
|---|------|--------|----------|
| G1 | Esc closes dialogs / popovers | **CODE** | AdwAlertDialog (1.5+) / AdwMessageDialog: Esc = 取消; popovers close on Esc; Esc in search = stop-search. The test uses Esc to close them; no explicit assert yet |
| G2 | Enter sends, Shift+Enter new line | **PASS** (runtime) | `Enter in the composer sends (peer received it)` light + dark |
| G4 | Keyboard shortcuts | **FIXED** (runtime) 16ee088, 7002dd8 | GtkShortcutController (capture, global): Ctrl+F search, Ctrl+N add-contact popover with focus in its box, Ctrl+Tab / Ctrl+Shift+Tab (also Ctrl+PgDn/PgUp) next/prev chat, Ctrl+I details, F10 main menu, Ctrl+O send file, Ctrl+W close, Ctrl+Q quit — each asserted light + dark; Ctrl+Tab / Ctrl+F also in the narrow layout |
| G5 | Close vs tray | **DECIDED** | GTK 4 has no tray: close keeps running in the background + one-time notice (LSM_QUIT_ON_CLOSE=1 disables). Later: StatusNotifierItem over GDBus (no GTK3) |
| G6 | Remember window size | **CODE** | window.ini (GKeyFile) width/height/maximized, restored on start |
| G7 | Adaptive layout | **PASS** (runtime) | AdwNavigationSplitView + AdwBreakpoint (max-width 600sp); 560x720 shots page-chat-narrow / page-list-narrow |
| G8 | Drag & drop files | **CODE** | GtkDropTarget GDK_TYPE_FILE_LIST on the chat + drop hint |
| G9 | Context menu on messages | **PASS** (runtime) | right-click (GtkGestureClick button 3) / long-press (touch) → 复制 / 打开 / 删除这条消息（仅本机）…; `right-click on a message (file card) opens its menu` |
| G10 | Copy message text | **PASS** (CODE) | selectable labels + 复制 in the menu |
| G11 | Tooltips | **PASS** (CODE) | all icon buttons (with shortcut in the text) |
| G12 | Single instance | **PASS** (CODE) | AdwApplication unique app id |
| G13 | Confirm destructive actions | **PASS** (CODE) | confirm(): destructive response, 取消 default — 删除消息, 屏蔽 |
| G14 | Colour audit (WCAG/APCA, edges, palette) | **PASS** locally light/dark/narrow (7002dd8) | gnome palette tokens: light fg2 #505055, dark fg2 #DADADF, dark timestamps (meta) #CDCDD2 → Lc ≥ 60 as rendered; decorative icons only (shield GtkImage, online dot) excluded via app-reported bounds in audit_ignore.json; dialog shots excluded (scrim) |
| G15 | Header without piled text | **PASS** | 在线 · 已验证身份 · 局域网直连; fingerprint / address / version in 联系人详情 (Ctrl+I, ⋯) |
| G16 | Composer | **PASS** | 36px field, placeholder 输入消息, right hint Enter 发送 · Shift+Enter 换行, no emoji button (no picker) |
| G17 | Notification click opens chat | **PARTIAL** | GNotification; click activates the app (window raised) but has no per-chat target yet — next: app.open-conv(s) default action |
| G18 | Older distros (Debian 12 has libadwaita 1.2) | **IN CI** | AppImage bundles glib 2.78 / GTK 4.12 / libadwaita 1.4 built on debian:12 (linux/adw-appimage/build.sh); job adw-appimage-debian12 launches it on stock debian:12 and takes a screenshot |

XP and GTK failures are fixed in their redesign phases (GTK4 port, XP custom-drawn list), each with runtime checks.


## Parity: delete message (this device only) + tombstone
| platform | UI | confirmation | core | tombstone (peer resend / file re-offer cannot resurrect) | test |
|---|---|---|---|---|---|
| WinUI | message right-click → 删除（仅本机）… | ContentDialog, 取消 default | Rust `delete_message` RPC (efb2a08) | `history.deleted` sealed, synced, 20k cap (de04d2b) | lan-core unit test + DaemonTests; smoke right-click check (921829d) |
| Android | long-press bubble → 复制 / 删除（仅本机） | LsmAlertDialog | JNI `deleteMessage` → `lsm_delete_message` (bc624b3) | C `D` history record (597b4fa) | adb step in android_chat_shots.py |
| XP | chat right-click / Shift+F10 → 删除这条消息（仅本机）… | tmsg yes/cancel | `lsm_delete_message` | C `D` record, `seen_has` + file-offer reject | regress.py §5 (restart keeps it gone, peer keeps it) |
| GTK | bubble right-click / long-press → 删除这条消息（仅本机）… (GTK3 and GTK4) | confirm() / AdwAlertDialog | `lsm_delete_message` | same C core | regress.py §5 (CLI); GTK4 menu opens at runtime (adw_gui_shots.py), delete itself not yet clicked in the GUI test |
