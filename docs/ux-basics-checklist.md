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

## WinUI 3 (lan-secure-messenger/ui-winui) — from source; runtime checks to be added to scripts/ui_smoke_windows.ps1

| # | Item | Status | Evidence |
|---|------|--------|----------|
| W1 | Esc closes dialogs | **PASS** (CODE) | all dialogs are ContentDialog (Esc = Close by default) |
| W2 | Enter sends, Shift+Enter new line | **CODE** | ChatPage.xaml.cs handles Shift in the composer KeyDown |
| W3 | Tab order / focus visuals | **CODE** – review pending | |
| W4 | Keyboard shortcuts (Ctrl+F search, Ctrl+N, Ctrl+, settings) | **FAIL** | no KeyboardAccelerator anywhere |
| W5 | Window close vs tray | **FAIL** | no tray / Closing handler: close quits UI (daemon keeps running) – decide + implement |
| W6 | Remember window size/position | **FAIL** | no placement persistence |
| W7 | High DPI | **PASS** (CODE) | WinUI is per-monitor DPI aware; app.manifest |
| W8 | Drag & drop files onto chat to send | **FAIL** | no AllowDrop |
| W9 | Right-click context menu on messages (复制/保存/转发) | **FAIL** | no ContextFlyout |
| W10 | Copy message text | **PASS** (CODE) | IsTextSelectionEnabled on bubbles |
| W11 | Tooltips on icon buttons | **PARTIAL** | ToolTipService on some buttons only |
| W12 | Single instance (activate the existing window) | **PASS** (CODE) | App.xaml.cs Mutex + Show event |
| W13 | Confirm destructive actions | **CODE** – review pending | ContentDialog in Chat/Groups pages |
| W14 | Plain-language errors | **PASS** | earlier audit: errors are Chinese sentences, no codes |
| W15 | Empty / loading states | **CODE** – review pending | |
| W16 | Scroll to bottom on a new message | **CODE** | MessagesAppended event → ChatPage scroll |
| W17 | Notification click opens the chat | **FAIL** | no AppNotification usage |

## XP Win32 (lan-secure-messenger-xp/src/gui.c) — from source; runtime checks under wine pending

| # | Item | Status | Evidence |
|---|------|--------|----------|
| X1 | Esc closes dialogs | **CODE** | IDCANCEL handled (dialog manager maps Esc→IDCANCEL) |
| X2 | Enter sends, Shift/Ctrl+Enter new line | **CODE** – check | VK_RETURN handled once, no Shift check found |
| X3 | Tab order (IsDialogMessage / WS_TABSTOP) | **CODE** – review pending | |
| X4 | Keyboard shortcuts / accelerators | **FAIL** | no accelerator table |
| X5 | Close vs tray | **PASS** (CODE) | Shell_NotifyIcon + WM_CLOSE handling |
| X6 | Remember window placement | **FAIL** | no Get/SetWindowPlacement |
| X7 | High DPI | **FAIL** | no LOGPIXELS scaling / DPI awareness |
| X8 | Drag & drop files | **FAIL** | no DragAcceptFiles |
| X9 | Right-click context menu | **FAIL** | no TrackPopupMenu |
| X10 | Copy message text | **FAIL** (to verify) | no copy support found |
| X11 | Tooltips | **FAIL** | no TOOLTIPS_CLASS |
| X12 | Single instance | **PASS** (CODE) | CreateMutex |
| X13 | Confirm destructive actions | **FAIL** (to verify) | no MB_YESNO found |
| X14–X17 | errors / empty states / scroll to bottom / notification click | pending | |

## Linux GTK (linux/gtk_gui.c; GTK4 port pending) — from source; runtime checks under xvfb pending

| # | Item | Status | Evidence |
|---|------|--------|----------|
| G1 | Esc closes dialogs | **CODE** – check | no explicit Escape binding (GtkDialog handles Esc natively) |
| G2 | Enter sends, Shift+Enter new line | **CODE** | GDK_KEY_Return + SHIFT_MASK handled |
| G4 | Keyboard shortcuts (Ctrl+F, Ctrl+Q, shortcuts window) | **FAIL** | no accelerators |
| G5 | Close vs tray / background | **FAIL** (to verify) | no delete-event handling |
| G6 | Remember window size | **FAIL** | no geometry persistence |
| G8 | Drag & drop | **PASS** (CODE) | drag handlers present |
| G9 | Context menu | **FAIL** | no popup menu |
| G11 | Tooltips | **PASS** (CODE) | tooltips set |
| G12 | Single instance | **FAIL** | no GApplication |
| G13 | Confirm destructive actions | **FAIL** (to verify) | |

Next: the desktop FAILs get fixed as part of each platform's redesign port (WinUI next, GTK4 port, XP list), each with a
runtime check added to its CI smoke script.
