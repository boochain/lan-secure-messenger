package io.github.frec86.lanmessenger

import android.widget.Toast
import androidx.compose.ui.platform.LocalContext
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.material3.SwipeToDismissBox
import androidx.compose.material3.SwipeToDismissBoxValue
import androidx.compose.material3.rememberSwipeToDismissBoxState
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/* ---------------------------------------------------------------- home: app bar · chips · connection banner · chat list · FAB · navigation bar (spec §5.1, Android) */
@Composable
fun Home(st: AppState, openPage: (String) -> Unit = {}, open: (String) -> Unit) {
    var showFr by remember { mutableStateOf(!Native.firstRunDone()) }
    var showRecovery by remember { mutableStateOf(false) }
    var recoveryForced by remember { mutableStateOf(false) }
    if (showFr) { FirstRunScreen { account -> showFr = false; if (account) { recoveryForced = true; showRecovery = true } }; return }
    if (showRecovery) {
        RecoveryCodeScreen(forceConfirm = recoveryForced, onDone = { showRecovery = false; recoveryForced = false }, onBack = { if (!recoveryForced) showRecovery = false })
        return
    }
    val p = LocalPalette.current
    // tabs: 0 聊天, 1 联系人, 2 文件 (传输/文件站子页), 3 设置 — at most 4 destinations
    var tab by rememberSaveable { mutableIntStateOf(0) }; var fileSub by rememberSaveable { mutableIntStateOf(0) }
    var filter by rememberSaveable { mutableIntStateOf(0) }; var folder by rememberSaveable { mutableStateOf("") }
    var menu by remember { mutableStateOf(false) }; var dlg by remember { mutableStateOf("") }
    var upd by remember { mutableStateOf(Triple(2, "", "")) }
    var searching by rememberSaveable { mutableStateOf(false) }; var q by rememberSaveable { mutableStateOf("") }
    // system Back at the root: close search, then go to the 聊天 tab, then double-press within 2 s to exit
    val act = LocalContext.current as? android.app.Activity
    var lastBack by remember { mutableLongStateOf(0L) }
    BackHandler {
        when {
            searching -> { searching = false; q = "" }
            tab != 0 -> tab = 0
            filter != 0 -> filter = 0
            android.os.SystemClock.uptimeMillis() - lastBack < 2000 -> act?.finish()
            else -> { lastBack = android.os.SystemClock.uptimeMillis(); Toast.makeText(act, "再按一次退出", Toast.LENGTH_SHORT).show() }
        }
    }
    val convs = st.convs()
    val totalUnread = convs.sumOf { it.unread }
    Column(Modifier.fillMaxSize().background(p.window).statusBarsPadding()) {
        Row(Modifier.fillMaxWidth().height(64.dp).padding(start = if (searching) 4.dp else 12.dp, end = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            if (searching) {
                IconButton({ searching = false; q = "" }) { Ic(R.drawable.ms_back, "关闭搜索", p.fg) }
                SearchField(q, { q = it }, "搜索联系人和群组", Modifier.weight(1f))
            } else {
                if (tab == 3 && dlg == "relay") IconButton({ dlg = "" }) { Ic(R.drawable.ms_back, "返回", p.fg) }
                // Title bar: app icon, never the app name. Section titles only for non-chat tabs.
                Image(painterResource(R.drawable.ic_app), "局域网安全通讯", modifier = Modifier.size(28.dp).clip(RoundedCornerShape(7.dp)))
                Spacer(Modifier.width(10.dp))
                val title = if (tab == 3 && dlg == "relay") "中转服务器（可选）" else when (tab) { 1 -> "联系人"; 2 -> "文件"; 3 -> "设置"; else -> "" }
                if (title.isNotEmpty()) Text(title, style = MaterialTheme.typography.titleLarge, color = p.fg, modifier = Modifier.weight(1f).widthIn(min = 0.dp), maxLines = 1, overflow = TextOverflow.Ellipsis)
                else Spacer(Modifier.weight(1f))
                if (tab <= 1) IconButton({ searching = true }) { Ic(R.drawable.ms_search, "搜索", p.fg) }
            }
            Box {
                IconButton({ menu = true }, Modifier.semantics { contentDescription = "菜单" }) { Ic(R.drawable.ms_more, "菜单", p.fg) }
                // Overflow holds at most 5 items; everything else lives in settings / add-contact pages.
                DropdownMenu(menu, { menu = false }) {
                    DropdownMenuItem({ Text("添加联系人") }, { menu = false; dlg = "add" }, leadingIcon = { Ic(R.drawable.ms_add_contact) })
                    DropdownMenuItem({ Text("我的二维码") }, { menu = false; dlg = "qr" }, leadingIcon = { Ic(R.drawable.ms_add_contact) })
                    DropdownMenuItem({ Text("扫描二维码") }, { menu = false; dlg = "scan" }, leadingIcon = { Ic(R.drawable.ms_add_contact) })
                    DropdownMenuItem({ Text("创建群组") }, { menu = false; dlg = "group" }, leadingIcon = { Ic(R.drawable.ms_groups) })
                    DropdownMenuItem({ Text("刷新联系人") }, { menu = false; Native.refreshPeers(); Repo.refresh(); Repo.toast.value = "已刷新联系人" }, leadingIcon = { Ic(R.drawable.ms_network) })
                }
            }
        }
        Box(Modifier.weight(1f).fillMaxWidth()) {
            when (tab) {
                1 -> ContactsPane(st, q) { fp -> open("dm:$fp") }
                2 -> FilesPane(st, fileSub) { fileSub = it }
                0 -> {
                    val shown = convs.filter { c -> convVisible(c, folder) && when (filter) { 1 -> c.unread > 0; 2 -> c.group; 3 -> c.via; else -> true } &&
                            (q.isBlank() || c.name.contains(q, true)) }
                    LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(bottom = 88.dp)) {
                        item(key = "chips") { Row(Modifier.horizontalScroll(rememberScrollState()).padding(start = 16.dp, end = 16.dp, top = 4.dp, bottom = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            Chip(filter == 0, "全部", null) { filter = 0 }
                            Chip(filter == 1, if (totalUnread > 0) "未读 ${convs.count { it.unread > 0 }}" else "未读", null) { filter = 1 }
                            Chip(filter == 2, "群组", R.drawable.ms_groups, modifier = Modifier.testTag("LSMF_nav_groups")) { filter = 2 }
                            Chip(filter == 3, "经服务器", R.drawable.ms_via_server) { filter = 3 }
                            convs.flatMap { it.folders }.distinct().sorted().forEach { f -> Chip(folder == f, f, null) { folder = if (folder == f) "" else f } }
                            convs.count { it.archived }.takeIf { it > 0 }?.let { n -> Chip(folder == FILTER_ARCHIVED, "已归档 $n", null) { folder = if (folder == FILTER_ARCHIVED) "" else FILTER_ARCHIVED } }
                        } }
                        item(key = "conn") { ConnBanner(st) }
                        items(shown, key = { it.id }) { c -> ConvRow(c) { open(c.id) } }
                        if (shown.isEmpty()) item(key = "empty") {
                            when {
                                q.isNotBlank() -> EmptyState(R.drawable.ms_search, "没有找到“$q”", "试试别的名字。")
                                filter == 2 -> EmptyState(R.drawable.ms_groups, "还没有群组", "点右上角菜单「创建群组」，选择已信任的联系人。")
                                filter == 3 -> EmptyState(R.drawable.ms_via_server, "没有经服务器中转的会话", "局域网内的联系人都是直连的。")
                                filter == 1 -> EmptyState(R.drawable.ms_tick_double, "全部已读", "没有未读消息。")
                                else -> EmptyState(R.drawable.ms_empty_chat, "还没有联系人", "同一 Wi-Fi 下运行本软件的设备会自动出现在这里。\n也可以点右上角菜单「添加联系人」，按 IP 地址添加（如 192.168.1.20）。")
                            }
                        }
                    }
                    Fab(R.drawable.ms_add_contact, "添加", "按 IP 添加", Modifier.align(Alignment.BottomEnd).padding(end = 8.dp, bottom = 8.dp)) { dlg = "add" }
                }
                else -> if (dlg == "relay") RelayScreen(st) { dlg = "" } else Settings(st, {
                        if (it == "update") { upd = Triple(2, "正在检查…", Updates.RELEASES); Updates.check(st.me.version) { n, m, u -> upd = Triple(n, m, u) } }
                        dlg = it
                    }, openPage, onRecovery = { recoveryForced = false; showRecovery = true })
            }
        }
        HorizontalDivider(thickness = 1.dp, color = p.divider)
        NavigationBar(containerColor = p.navBar, tonalElevation = 0.dp, modifier = Modifier.navigationBarsPadding()) {
            val items = listOf(
                Triple("聊天", R.drawable.ms_chats to R.drawable.ms_chats_fill, "LSMF_nav_chats"),
                Triple("联系人", R.drawable.ms_add_contact to R.drawable.ms_add_contact, "LSMF_nav_contacts"),
                Triple("文件", R.drawable.ms_transfers to R.drawable.ms_transfers_fill, "LSMF_nav_files_tab"),
                Triple("设置", R.drawable.ms_settings to R.drawable.ms_settings_fill, "LSMF_nav_settings"))
            items.forEachIndexed { i, (label, icons, hook) ->
                val (ic, icOn) = icons
                NavigationBarItem(selected = tab == i, onClick = { tab = i; searching = false; q = ""; if (dlg == "relay") dlg = "" }, modifier = Modifier.testTag(hook).padding(horizontal = 4.dp), label = { Text(label, fontWeight = if (tab == i) FontWeight.SemiBold else FontWeight.Medium, maxLines = 1) },
                    icon = { val n = when (i) { 0 -> totalUnread; 2 -> st.transfers.count { it.state == "active" || it.state == "offered" }; else -> 0 }
                        if (n > 0) BadgedBox(badge = { Badge(containerColor = p.badge, contentColor = p.badgeFg) { Text("$n") } }) { Ic(if (tab == i) icOn else ic, null) } else Ic(if (tab == i) icOn else ic, null) },
                    colors = NavigationBarItemDefaults.colors(selectedIconColor = p.onSecCont, selectedTextColor = p.fg, indicatorColor = p.secCont, unselectedIconColor = p.fg2, unselectedTextColor = p.fg2))
            }
        }
    }
    when (dlg) {
        "add" -> TextDialog("按 IP 添加", "输入对方 IP 地址（可带端口，如 192.168.1.20 或 192.168.1.20:41235），或粘贴添加链接 lsm://…：", onOk = { if (it.trim().startsWith("lsm://")) Ux.addUri(it) else { val e = Native.addAddr(it); if (e.isNotEmpty()) Repo.toast.value = e } }, onDismiss = { dlg = "" })
        "link" -> TextDialog("通过链接添加", "粘贴 lsm://add、lsm://relay 或 lsm://pair 链接：", onOk = { Ux.applyUri(it) }, onDismiss = { dlg = "" })
        "scan" -> QrScanScreen(onResult = { Ux.applyUri(it); dlg = "" }, onDismiss = { dlg = "" })
        "qr" -> QrDialog { dlg = "" }
        "name" -> TextDialog("修改昵称", "新的显示名称：", st.me.name, onOk = { Native.setName(it); Repo.refresh() }, onDismiss = { dlg = "" })
        "group" -> PickContacts(st, "创建群组", true, true, true, { fps, n -> val r = Native.groupCreate(n, fps.joinToString(",")); if (r.startsWith("!")) Repo.toast.value = r.drop(1); Repo.refresh() }, { dlg = "" })
        "pin" -> { val pin = remember { Native.pinOffer() }
            LsmAlertDialog(onDismissRequest = { dlg = "" }, title = { Text("PIN 配对") }, text = { Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.fillMaxWidth()) {
                Text(pin, style = MaterialTheme.typography.displayMedium, fontFamily = FontFamily.Monospace, letterSpacing = 6.sp, color = p.fg)
                Spacer(Modifier.height(8.dp)); Text("请让对方在“输入 PIN 配对”中输入（5 分钟内有效）。") } }, confirmButton = { TextButton({ dlg = "" }) { Text("好") } }) }
        "diag" -> LsmAlertDialog(onDismissRequest = { dlg = "" }, title = { Text("诊断信息") },
            text = { Text(Native.diagnostics(), fontSize = 11.sp, modifier = Modifier.verticalScroll(rememberScrollState())) },
            confirmButton = { TextButton({ dlg = "" }) { Text("关闭") } })
        "update" -> LsmAlertDialog(onDismissRequest = { dlg = "" }, title = { Text("检查更新") }, text = { Text(upd.second) },
            confirmButton = { if (upd.first == 1 || upd.first == -1) TextButton({ dlg = ""; try { act?.startActivity(android.content.Intent(android.content.Intent.ACTION_VIEW, android.net.Uri.parse(upd.third))) } catch (_: Exception) {} }) { Text(if (upd.first == 1) "打开下载页" else "打开发布页") } else TextButton({ dlg = "" }) { Text("好") } },
            dismissButton = { if (upd.first == 1 || upd.first == -1) TextButton({ dlg = "" }) { Text("取消") } })
        "about" -> LsmAlertDialog(onDismissRequest = { dlg = "" }, title = { Text("关于") }, text = { Text("局域网安全通讯 Android 版 ${st.me.version}\n\n所有版本互通（协议 v5）：Noise_XX_25519_ChaChaPoly_BLAKE2s 加密、自动发现、按 IP 添加、PIN 配对、群组、文件传输、送达/已读回执。\n\n本机端口 TCP ${st.me.port}") }, confirmButton = { TextButton({ dlg = "" }) { Text("好") } })
    }
}

/** small 经服务器 chip (icon + text) for relay peers: list row and chat header */
@Composable
fun ViaBadge(modifier: Modifier = Modifier) {
    val p = LocalPalette.current
    Row(modifier.clip(RoundedCornerShape(8.dp)).background(p.chip).padding(horizontal = 6.dp, vertical = 1.dp).semantics { contentDescription = "经服务器中转" },
        verticalAlignment = Alignment.CenterVertically) {
        Ic(R.drawable.ms_via_server, null, p.chipFg, 14.dp); Spacer(Modifier.width(3.dp))
        Text("经服务器", fontSize = 12.sp, lineHeight = 16.sp, color = p.chipFg, maxLines = 1)
    }
}

@Composable
fun Chip(on: Boolean, label: String, icon: Int?, modifier: Modifier = Modifier, click: () -> Unit) {
    val p = LocalPalette.current
    FilterChip(selected = on, onClick = click, modifier = modifier, label = { Text(label, style = MaterialTheme.typography.labelLarge) },
        leadingIcon = if (on) ({ Ic(R.drawable.ms_tick, null, size = 18.dp) }) else icon?.let { { Ic(it, null, size = 18.dp) } },
        shape = RoundedCornerShape(8.dp),
        colors = FilterChipDefaults.filterChipColors(selectedContainerColor = p.secCont, selectedLabelColor = p.onSecCont, selectedLeadingIconColor = p.onSecCont, labelColor = p.fg2, iconColor = p.fg2),
        border = FilterChipDefaults.filterChipBorder(enabled = true, selected = on, borderColor = p.divider, selectedBorderColor = p.secCont))
}

@Composable
fun ConnBanner(st: AppState) {
    val p = LocalPalette.current
    val net = remember(st.contacts.size, st.running) { lanSubnet() }
    val (ok, txt) = when {
        st.error.isNotEmpty() -> false to "启动失败：" + st.error
        !st.running -> false to "正在启动…"
        net == null -> false to "未连接局域网 · 连上 Wi-Fi 后会自动发现联系人"
        else -> true to "已连接 · 局域网 $net" + (if (st.relay.enabled && st.relay.state == "online") " · 中转服务器" else "") + " · ${st.contacts.size} 位联系人"
    }
    Row(Modifier.padding(start = 16.dp, end = 16.dp, bottom = 8.dp).fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(p.chip).padding(horizontal = 16.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically) {
        Ic(if (ok) R.drawable.ms_network else R.drawable.ms_info, null, if (ok) p.online else if (st.error.isNotEmpty()) p.error else p.fg2, 18.dp)
        Spacer(Modifier.width(12.dp)); Text(txt, style = MaterialTheme.typography.bodySmall, color = if (st.error.isNotEmpty()) p.error else p.fg2, maxLines = 2, overflow = TextOverflow.Ellipsis)
    }
}

/** 72 dp row: 48 dp avatar(+dot) · [group icon] name [shield][cloud] · time / [ticks][sender:][file icon] preview · unread pill */
@OptIn(ExperimentalFoundationApi::class, ExperimentalMaterial3Api::class)
@Composable
fun ConvRow(c: Conv, click: () -> Unit) {
    val p = LocalPalette.current; val m = c.last; val unread = c.unread > 0
    var menu by remember { mutableStateOf(false) }
    var confirmDel by remember { mutableStateOf(false) }
    val muted = !c.group && Native.isMuted(c.key)
    fun pin() { Native.setConvPinned(c.id, !c.pinned); Repo.refresh() }
    fun read() { Native.markRead(c.id); Repo.refresh() }
    fun markItUnread() { Native.markUnread(c.id); Repo.refresh() }
    fun mute() { if (!c.group) { Native.setMuted(c.key, !Native.isMuted(c.key)); Repo.refresh() } }
    var revealed by remember { mutableStateOf(false) }
    // LSMF_chat_list_swipe: swipe left reveals inline pin/read/mute/delete (Telegram/WhatsApp style), not a popup.
    // Slide the whole row off-screen so leftover preview text is not mid-glyph clipped at x=0.
    BackHandler(enabled = revealed) { revealed = false }
    Box(Modifier.testTag("LSMF_chat_list_swipe").semantics { contentDescription = "LSMF_chat_list_menu" }
        .clipToBounds()
        .pointerInput(c.id) {
            var acc = 0f
            detectHorizontalDragGestures(
                onDragStart = { acc = 0f },
                onHorizontalDrag = { change, drag ->
                    change.consume()
                    acc += drag
                    if (acc < -16f) revealed = true
                    else if (acc > 16f) revealed = false
                }
            )
        }) {
        if (revealed) {
            Row(Modifier.matchParentSize().background(p.chip).clickable { revealed = false }, verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.End) {
                TextButton({ pin(); revealed = false }, Modifier.testTag("LSMF_chat_pin")) { Text(if (c.pinned) "取消置顶" else "置顶", color = p.fg, fontSize = 13.sp) }
                TextButton({ read(); revealed = false }, Modifier.testTag("LSMF_chat_mark_read")) { Text("已读", color = p.fg, fontSize = 13.sp) }
                if (!c.group) TextButton({ mute(); revealed = false }, Modifier.testTag("LSMF_chat_mute")) { Text(if (muted) "取消免打扰" else "免打扰", color = p.fg, fontSize = 13.sp) }
                TextButton({ confirmDel = true; revealed = false }, Modifier.testTag("LSMF_chat_delete")) { Text("删除", color = p.error, fontSize = 13.sp) }
            }
        }
        BoxWithConstraints {
            Row(Modifier.fillMaxWidth().height(72.dp).offset(x = if (revealed) -maxWidth else 0.dp).background(p.window).combinedClickable(onClick = { if (revealed) revealed = false else click() }, onLongClick = { menu = true }).padding(horizontal = 16.dp), verticalAlignment = Alignment.CenterVertically) {
                Avatar(c.name, c.key, 48.dp, c.online, p.window)
                Spacer(Modifier.width(16.dp))
                Column(Modifier.weight(1f)) {
                    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                      Row(Modifier.weight(1f), verticalAlignment = Alignment.CenterVertically) {
                        if (c.group) { Ic(R.drawable.ms_groups, "群组", p.fg2, 18.dp); Spacer(Modifier.width(6.dp)) }
                        Text(c.name, fontSize = 16.sp, lineHeight = 24.sp, letterSpacing = .15.sp, fontWeight = if (unread) FontWeight.Bold else FontWeight.Normal, color = p.fg,
                            maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f, false))
                        if (c.trusted) Ic(R.drawable.ms_trusted, "已验证身份", p.online, 16.dp, Modifier.padding(start = 4.dp))
                        if (c.via) ViaBadge(Modifier.padding(start = 6.dp))
                        if (c.pinned) Text("📌", fontSize = 12.sp, modifier = Modifier.padding(start = 6.dp).semantics { contentDescription = "已置顶" })
                      }
                        Spacer(Modifier.width(8.dp))
                        if (m != null) Text(listTime(m.ts), fontSize = 12.sp, lineHeight = 16.sp, letterSpacing = .4.sp, color = if (unread) p.accent else p.fg2, fontWeight = if (unread) FontWeight.Bold else FontWeight.Normal)
                    }
                    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                        val pv = if (unread) p.fg else p.fg2
                        if (m != null && m.out) { Ticks(m.status, p.fg2, p.read); Spacer(Modifier.width(4.dp)) }
                        val from = if (c.group && m != null) (if (m.out) "我" else m.sender) + ": " else ""
                        val txt = when { m == null -> if (c.group) "${c.members} 位成员" else if (c.online) "在线" else "离线"; m.kind == 1 -> if (isImage(m.file)) "照片" else m.file; else -> m.body.replace('\n', ' ') }
                        if (m != null && m.kind == 1 && from.isEmpty()) { Ic(fileIcon(m.file), null, pv, 18.dp); Spacer(Modifier.width(4.dp)) }
                        if (c.draft.isNotEmpty()) Text("[草稿] ", fontSize = 14.sp, lineHeight = 20.sp, color = p.error)
                        Text(if (c.draft.isNotEmpty()) c.draft.replace('\n', ' ') else from + txt, fontSize = 14.sp, lineHeight = 20.sp, letterSpacing = .25.sp, color = pv, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
                        if (unread) { Spacer(Modifier.width(8.dp)); CountBadge(c.unread) }
                    }
                }
            }
            DropdownMenu(menu, { menu = false }) {
                DropdownMenuItem({ Text(if (c.pinned) "取消置顶" else "置顶") }, { menu = false; pin() }, modifier = Modifier.testTag("LSMF_chat_pin"))
                DropdownMenuItem({ Text(if (unread) "标为已读" else "标为未读") }, { menu = false; if (unread) read() else markItUnread() }, modifier = Modifier.testTag(if (unread) "LSMF_chat_mark_read" else "LSMF_chat_mark_unread"))
                if (!c.group) DropdownMenuItem({ Text(if (muted) "取消免打扰" else "免打扰") }, { menu = false; mute() }, modifier = Modifier.testTag("LSMF_chat_mute"))
                if (!c.group) DropdownMenuItem({ Text(if (c.trusted) "取消信任" else "信任") }, { menu = false; Native.setTrust(c.key, if (c.trusted) 0 else 1); Repo.refresh() })
                DropdownMenuItem({ Text("删除会话…") }, { menu = false; confirmDel = true }, modifier = Modifier.testTag("LSMF_chat_delete"))
            }
        }
    }
    if (confirmDel) {
        AlertDialog(onDismissRequest = { confirmDel = false }, title = { Text("删除与 ${c.name} 的聊天记录？") },
            text = { Text("只会从本机删除，对方仍保留这些消息。此操作无法撤销。") },
            confirmButton = { TextButton({ confirmDel = false; Native.clearConversation(c.id); Repo.refresh() }) { Text("删除") } },
            dismissButton = { TextButton({ confirmDel = false }) { Text("取消") } })
    }
}

@Composable
fun EmptyState(icon: Int, title: String, body: String) {
    val p = LocalPalette.current
    Column(Modifier.fillMaxWidth().padding(horizontal = 32.dp, vertical = 48.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        Box(Modifier.size(112.dp).clip(CircleShape).background(p.pCont), contentAlignment = Alignment.Center) { Ic(icon, null, p.onPCont, 56.dp) }
        Spacer(Modifier.height(24.dp)); Text(title, style = MaterialTheme.typography.titleLarge, color = p.fg, textAlign = TextAlign.Center)
        Spacer(Modifier.height(8.dp)); Text(body, style = MaterialTheme.typography.bodyMedium, color = p.fg2, textAlign = TextAlign.Center)
    }
}

@Composable
fun Fab(icon: Int, label: String, desc: String, modifier: Modifier, click: () -> Unit) {
    val p = LocalPalette.current
    ExtendedFloatingActionButton(onClick = click, modifier = modifier.padding(16.dp).semantics { contentDescription = desc }, containerColor = p.pCont, contentColor = p.onPCont,
        elevation = FloatingActionButtonDefaults.elevation(3.dp, 3.dp, 3.dp, 4.dp),  // mockup: box-shadow 0 2px 6px .18 (M3 default 6dp casts a visibly larger shadow)
        icon = { Ic(icon, null) }, text = { Text(label, style = MaterialTheme.typography.labelLarge) })
}

@Composable
fun Transfers(st: AppState) {
    val p = LocalPalette.current
    if (st.transfers.isEmpty()) { EmptyState(R.drawable.ms_transfers, "没有文件传输", "在聊天里点回形针发送文件，进度会显示在这里。"); return }
    LazyColumn(Modifier.fillMaxSize()) {
        items(st.transfers, key = { it.id }) { t ->
            Row(Modifier.fillMaxWidth().heightIn(min = 72.dp).padding(horizontal = 16.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.size(40.dp).clip(RoundedCornerShape(8.dp)).background(p.tile), contentAlignment = Alignment.Center) { Ic(fileIcon(t.name), null, p.tileFg) }
                Column(Modifier.weight(1f).padding(horizontal = 16.dp)) {
                    Text(t.name, fontSize = 16.sp, lineHeight = 24.sp, color = p.fg, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Text((if (t.out) "发送 · " else "接收 · ") + humanSize(t.size) + " · " + (if (t.paused) t.error else xferZh(t.state) + (if (t.state == "active" && t.size > 0) " ${t.done * 100 / t.size}%" else "") +
                        (if (t.error.isNotEmpty()) " · " + errZh(t.error) else "")), style = MaterialTheme.typography.bodyMedium, color = p.fg2, maxLines = 2, overflow = TextOverflow.Ellipsis)
                    // resumed transfer: where it continued from
                    if (t.resumed > 0 && t.size > 0 && !t.paused) Text("从 ${t.resumed * 100 / t.size}%（${humanSize(t.resumed)}）处续传", style = MaterialTheme.typography.bodySmall, color = p.fg2)
                    if (t.paused && t.size > 0) LinearProgressIndicator(progress = { t.done.toFloat() / t.size }, Modifier.fillMaxWidth().padding(top = 6.dp), color = p.fg2, trackColor = p.divider)
                    if (t.state == "active" && t.size > 0) LinearProgressIndicator(progress = { t.done.toFloat() / t.size }, Modifier.fillMaxWidth().padding(top = 6.dp), color = p.accent, trackColor = p.divider)
                }
                if (t.state == "active" || t.state == "offered") IconButton({ val e = Native.cancelTransfer(t.id); if (e.isNotEmpty()) Repo.toast.value = e; Repo.refresh() }) { Ic(R.drawable.ms_close, "取消", p.fg2) }
            }
        }
    }
}

@Composable
fun Settings(st: AppState, dlg: (String) -> Unit, openPage: (String) -> Unit = {}, onRecovery: () -> Unit = { openPage("recovery") }) {
    val p = LocalPalette.current
    var storageSub by remember { mutableStateOf("查看占用并清理半成品") }
    var storageAsk by remember { mutableStateOf(false) }
    var qrShow by remember { mutableStateOf<Pair<String, String>?>(null) }
    fun refreshStorage() {
        val raw = Native.storageInfo()
        var hist = 0L; var dl = 0L; var part = 0L; var orph = 0L; var tot = 0L
        for (line in raw.lineSequence()) {
            val i = line.indexOf('\t'); if (i < 0) continue
            val k = line.substring(0, i); val v = line.substring(i + 1)
            when (k) {
                "history_bytes" -> hist = v.toLongOrNull() ?: 0
                "downloads_bytes" -> dl = v.toLongOrNull() ?: 0
                "partials_bytes" -> part = v.toLongOrNull() ?: 0
                "orphan_partials_bytes" -> orph = v.toLongOrNull() ?: 0
                "total_bytes" -> tot = v.toLongOrNull() ?: 0
            }
        }
        fun mb(n: Long) = if (n >= 1048576) "%.1f MB".format(n / 1048576.0) else if (n >= 1024) "%.0f KB".format(n / 1024.0) else "$n B"
        storageSub = "合计 ${mb(tot)} · 下载 ${mb(dl)} · 半成品 ${mb(part)}（孤儿 ${mb(orph)}）· 记录 ${mb(hist)}"
    }
    LaunchedEffect(Unit) { refreshStorage() }
    @Composable fun SettingRow(icon: Int, title: String, sub: String?, click: (() -> Unit)?) =
        Row(Modifier.fillMaxWidth().heightIn(min = 56.dp).ifThen(click != null) { clickable { click!!() } }.padding(horizontal = 16.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            Ic(icon, null, p.fg2); Spacer(Modifier.width(16.dp))
            Column(Modifier.weight(1f)) { Text(title, fontSize = 16.sp, lineHeight = 24.sp, color = p.fg); if (sub != null) Text(sub, style = MaterialTheme.typography.bodyMedium, color = p.fg2, maxLines = 2, overflow = TextOverflow.Ellipsis) }
        }
    LazyColumn(Modifier.fillMaxSize().testTag("LSMF_nav_settings")) {
        item { Row(Modifier.fillMaxWidth().padding(16.dp).testTag("LSMF_nav_profile"), verticalAlignment = Alignment.CenterVertically) {
            Avatar(st.me.name, st.me.fp, 64.dp); Spacer(Modifier.width(16.dp))
            Column { Text(st.me.name, style = MaterialTheme.typography.titleLarge, color = p.fg); Text("指纹 " + st.me.fp, style = MaterialTheme.typography.bodySmall, color = p.fg2) } } }
        item { HorizontalDivider(color = p.divider) }
        item { SettingRow(R.drawable.ms_edit, "个人资料", st.me.name) { openPage("profile") } }
        item { SettingRow(R.drawable.ms_edit, "修改昵称", st.me.name) { dlg("name") } }
        item { SettingRow(R.drawable.ms_lock, "生成 PIN（让对方输入）", "与新设备配对并互相信任") { dlg("pin") } }
        item { SettingRow(R.drawable.ms_add_contact, "按 IP 添加", "不在同一网段时手动添加") { dlg("add") } }
        item { SettingRow(R.drawable.ms_groups, "创建群组", "从已信任的联系人中选择") { dlg("group") } }
        item { SettingRow(R.drawable.ms_via_server, "中转服务器（可选）", if (st.relay.enabled) relayStateZh(st.relay.state) + (if (st.relay.host.isNotEmpty()) " · ${st.relay.host}" else "") else "未启用 · 局域网直连优先") { dlg("relay") } }
        item {
            val mode = Native.accountMode()
            SettingRow(R.drawable.ms_lock, if (mode == "account") "账号模式（点按切换为设备模式）" else "设备模式（点按切换为账号模式）",
                "账号模式把加密身份放在中转上；设备模式仅本机。不使用服务器密码。") {
                val next = if (mode == "account") "device" else "account"
                val r = Native.setAccountMode(next)
                Repo.refresh()
                when {
                    next == "account" && (r.startsWith("RK") || Native.revealRecoveryKey().isNotEmpty()) -> onRecovery()
                    r.isNotEmpty() && !r.startsWith("RK") -> Repo.toast.value = r
                    next == "account" -> Repo.toast.value = "已切换到账号模式"
                    else -> Repo.toast.value = "已切换到设备模式"
                }
            }
        }
        item { SettingRow(R.drawable.ms_lock, "恢复密钥", "查看、复制或导出；仅本机可见，服务器看不到明文") {
            onRecovery()  // the page itself explains when no key exists yet
        } }
        item { SettingRow(R.drawable.ms_add_contact, "扫描或粘贴链接", "添加联系人、连接服务器或配对新设备") { dlg("scan") } }
        item { SettingRow(R.drawable.ms_via_server, "服务器二维码", "给另一台设备扫描以连接同一中转") {
            val u = Native.relayUri().ifEmpty { if (lsmUiTest()) lsmTestRelayUri() else "" }
            if (u.startsWith("lsm://")) qrShow = "服务器二维码" to u else Repo.toast.value = "请先填写中转服务器"
        } }
        item { SettingRow(R.drawable.ms_lock, "配对新设备", "生成一次性配对二维码（需账号模式且已连中转）") {
            val u = Native.accountPairUri().let { if (it.startsWith("lsm://")) it else if (lsmUiTest()) lsmTestPairUri() else it }
            if (u.startsWith("lsm://")) qrShow = "配对新设备" to u else Repo.toast.value = u.ifEmpty { "无法生成配对码" }
        } }
        item { SettingRow(R.drawable.ms_network, "本机端口", "TCP ${st.me.port}", null) }
        item { SettingRow(R.drawable.ms_open_folder, "接收的文件", st.me.downloads, null) }
        item { SettingRow(R.drawable.ms_delete, "存储管理", storageSub) { storageAsk = true } }
        item { SettingRow(R.drawable.ms_info, "诊断信息", "连接与排障") { openPage("diag") } }
        item { SettingRow(R.drawable.ms_info, "检查更新", "查看是否有新版本") { dlg("update") } }
        item { SettingRow(R.drawable.ms_info, "关于", "版本 ${st.me.version}") { dlg("about") } }
    }
    qrShow?.let { (title, uri) -> QrDialog(title, uri) { qrShow = null } }
    if (storageAsk) {
        AlertDialog(
            onDismissRequest = { storageAsk = false },
            title = { Text("存储管理") },
            text = { Text(storageSub + "\n\n清理孤儿半成品并清除已完成的传输列表项？") },
            confirmButton = {
                TextButton({
                    val n = Native.cleanupStorage(true, true)
                    Repo.toast.value = "已清理 $n 项"; refreshStorage(); storageAsk = false
                }) { Text("清理") }
            },
            dismissButton = { TextButton({ storageAsk = false }) { Text("取消") } }
        )
    }
}


@Composable
fun FilesPane(st: AppState, sub: Int, onSub: (Int) -> Unit) {
    val p = LocalPalette.current
    Column(Modifier.fillMaxSize()) {
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(start = 16.dp, end = 16.dp, top = 8.dp, bottom = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            FilterChip(selected = sub == 0, onClick = { onSub(0) }, modifier = Modifier.testTag("LSMF_nav_transfers"),
                label = { Text("传输") }, colors = FilterChipDefaults.filterChipColors(selectedContainerColor = p.secCont, selectedLabelColor = p.onSecCont, labelColor = p.fg2))
            FilterChip(selected = sub == 1, onClick = { onSub(1) }, modifier = Modifier.testTag("LSMF_nav_files"),
                label = { Text("文件站") }, colors = FilterChipDefaults.filterChipColors(selectedContainerColor = p.secCont, selectedLabelColor = p.onSecCont, labelColor = p.fg2))
        }
        Box(Modifier.weight(1f)) { if (sub == 0) Transfers(st) else FilesSite(st) }
    }
}

@Composable
fun ContactsPane(st: AppState, q: String, openFp: (String) -> Unit) {
    val p = LocalPalette.current
    val list = st.contacts.filter { q.isBlank() || it.name.contains(q, true) || it.fp.contains(q, true) }
    if (list.isEmpty()) { EmptyState(R.drawable.ms_add_contact, "还没有联系人", "同一 Wi-Fi 下运行本软件的设备会自动出现在这里。"); return }
    LazyColumn(Modifier.fillMaxSize().testTag("LSMF_nav_contacts"), contentPadding = PaddingValues(bottom = 88.dp)) {
        items(list, key = { it.fp }) { k ->
            Row(Modifier.fillMaxWidth().height(72.dp).clickable { openFp(k.fp) }.padding(horizontal = 16.dp), verticalAlignment = Alignment.CenterVertically) {
                Avatar(k.name, k.fp, 48.dp, k.online, p.window); Spacer(Modifier.width(16.dp))
                Column(Modifier.weight(1f)) {
                    Text(k.name, fontSize = 16.sp, color = p.fg, fontWeight = FontWeight.Medium, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Text((if (k.trust == 1) "已信任" else if (k.trust == 2) "已屏蔽" else "未信任") + " · " + (if (k.online) "在线" else "离线"), style = MaterialTheme.typography.bodyMedium, color = p.fg2)
                }
            }
        }
    }
}

@Composable
fun FilesSite(st: AppState) {
    val p = LocalPalette.current
    val info = remember { Native.storageInfo() }
    val files = remember(st.convs(), st.transfers) {
        st.convs().flatMap { c -> Repo.messages(c.id).filter { it.kind == 1 && !isStickerName(it.file) }.map { it } }
            .sortedByDescending { it.ts }.take(200)
    }
    LazyColumn(Modifier.fillMaxSize().testTag("LSMF_nav_files"), contentPadding = PaddingValues(bottom = 24.dp)) {
        item {
            Text("文件站", style = MaterialTheme.typography.titleLarge, color = p.fg, modifier = Modifier.padding(16.dp, 12.dp, 16.dp, 4.dp))
            Text(info.ifBlank { "（存储信息不可用）" }, style = MaterialTheme.typography.bodySmall, color = p.fg2, modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp))
            Text("接收目录 ${st.me.downloads}", style = MaterialTheme.typography.bodySmall, color = p.fg2, modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp))
        }
        if (files.isEmpty() && st.transfers.isEmpty()) item { EmptyState(R.drawable.ms_open_folder, "暂无文件", "在聊天中发送或接收的文件会出现在这里。") }
        items(st.transfers, key = { "t-" + it.id }) { t ->
            Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                Ic(fileIcon(t.name), null, p.fg2); Spacer(Modifier.width(12.dp))
                Column(Modifier.weight(1f)) {
                    Text(t.name, color = p.fg, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Text((if (t.out) "发送" else "接收") + " · " + xferZh(t.state), style = MaterialTheme.typography.bodySmall, color = p.fg2)
                }
            }
        }
        items(files, key = { it.id }) { m ->
            Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                Ic(if (isImageName(m.file)) R.drawable.ms_image else R.drawable.ms_file, null, p.fg2); Spacer(Modifier.width(12.dp))
                Text(m.file, color = p.fg, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
        }
    }
}

@Composable
fun DiagScreen(back: () -> Unit) {
    val p = LocalPalette.current
    androidx.activity.compose.BackHandler { back() }
    Column(Modifier.fillMaxSize().background(p.window).statusBarsPadding().testTag("LSMF_nav_diagnostics")) {
        Row(Modifier.fillMaxWidth().height(64.dp).padding(horizontal = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            IconButton(back) { Ic(R.drawable.ms_back, "返回", p.fg) }
            Text("诊断信息", style = MaterialTheme.typography.titleLarge, color = p.fg)
        }
        Text(Native.diagnostics(), fontSize = 11.sp, color = p.fg, modifier = Modifier.padding(16.dp).verticalScroll(rememberScrollState()))
    }
}
