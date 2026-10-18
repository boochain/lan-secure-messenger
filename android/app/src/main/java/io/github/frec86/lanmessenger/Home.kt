package io.github.frec86.lanmessenger

import android.widget.Toast
import androidx.compose.ui.platform.LocalContext
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
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
fun Home(st: AppState, open: (String) -> Unit) {
    val p = LocalPalette.current
    var tab by rememberSaveable { mutableIntStateOf(0) }; var filter by rememberSaveable { mutableIntStateOf(0) }
    var menu by remember { mutableStateOf(false) }; var dlg by remember { mutableStateOf("") }
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
        Row(Modifier.fillMaxWidth().height(64.dp).padding(start = if (searching) 4.dp else 16.dp, end = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            if (searching) {
                IconButton({ searching = false; q = "" }) { Ic(R.drawable.ms_back, "关闭搜索", p.fg) }
                SearchField(q, { q = it }, "搜索联系人和群组", Modifier.weight(1f))
            } else {
                if (tab == 3 && dlg == "relay") IconButton({ dlg = "" }) { Ic(R.drawable.ms_back, "返回", p.fg) }
                Text(if (tab == 3 && dlg == "relay") "中转服务器（可选）" else when (tab) { 1 -> "群组"; 2 -> "文件传输"; 3 -> "设置"; else -> "局域网安全通讯" }, style = MaterialTheme.typography.titleLarge, color = p.fg, modifier = Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis)
                if (tab <= 1) IconButton({ searching = true }) { Ic(R.drawable.ms_search, "搜索", p.fg) }
            }
            Box {
                IconButton({ menu = true }) { Ic(R.drawable.ms_more, "菜单", p.fg) }
                DropdownMenu(menu, { menu = false }) {
                    DropdownMenuItem({ Text("按 IP 添加") }, { menu = false; dlg = "add" }, leadingIcon = { Ic(R.drawable.ms_add_contact) })
                    DropdownMenuItem({ Text("生成 PIN（让对方输入）") }, { menu = false; dlg = "pin" }, leadingIcon = { Ic(R.drawable.ms_lock) })
                    DropdownMenuItem({ Text("创建群组") }, { menu = false; dlg = "group" }, leadingIcon = { Ic(R.drawable.ms_groups) })
                    DropdownMenuItem({ Text("修改昵称") }, { menu = false; dlg = "name" }, leadingIcon = { Ic(R.drawable.ms_edit) })
                    DropdownMenuItem({ Text("关于") }, { menu = false; dlg = "about" }, leadingIcon = { Ic(R.drawable.ms_info) })
                }
            }
        }
        Box(Modifier.weight(1f).fillMaxWidth()) {
            when (tab) {
                0, 1 -> {
                    val shown = convs.filter { c -> (tab == 0 || c.group) && when (filter) { 1 -> c.unread > 0; 2 -> c.group; 3 -> c.via; else -> true } &&
                            (q.isBlank() || c.name.contains(q, true)) }
                    LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(bottom = 88.dp)) {
                        if (tab == 0) item(key = "chips") { Row(Modifier.horizontalScroll(rememberScrollState()).padding(start = 16.dp, end = 16.dp, top = 4.dp, bottom = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            Chip(filter == 0, "全部", null) { filter = 0 }
                            Chip(filter == 1, if (totalUnread > 0) "未读 ${convs.count { it.unread > 0 }}" else "未读", null) { filter = 1 }
                            Chip(filter == 2, "群组", R.drawable.ms_groups) { filter = 2 }
                            Chip(filter == 3, "经服务器", R.drawable.ms_via_server) { filter = 3 }
                        } }
                        if (tab == 0) item(key = "conn") { ConnBanner(st) }
                        items(shown, key = { it.id }) { c -> ConvRow(c) { open(c.id) } }
                        if (shown.isEmpty()) item(key = "empty") {
                            when {
                                q.isNotBlank() -> EmptyState(R.drawable.ms_search, "没有找到“$q”", "试试别的名字。")
                                tab == 1 || filter == 2 -> EmptyState(R.drawable.ms_groups, "还没有群组", "点右下角「创建群组」，选择已信任的联系人。")
                                filter == 3 -> EmptyState(R.drawable.ms_via_server, "没有经服务器中转的会话", "局域网内的联系人都是直连的。")
                                filter == 1 -> EmptyState(R.drawable.ms_tick_double, "全部已读", "没有未读消息。")
                                else -> EmptyState(R.drawable.ms_empty_chat, "还没有联系人", "同一 Wi-Fi 下运行本软件的设备会自动出现在这里。\n也可以点右下角「添加」，按 IP 地址添加（如 192.168.1.20）。")
                            }
                        }
                    }
                    if (tab == 0) Fab(R.drawable.ms_add_contact, "添加", "按 IP 添加", Modifier.align(Alignment.BottomEnd)) { dlg = "add" }
                    else Fab(R.drawable.ms_groups, "创建群组", "创建群组", Modifier.align(Alignment.BottomEnd)) { dlg = "group" }
                }
                2 -> Transfers(st)
                else -> if (dlg == "relay") RelayScreen(st) { dlg = "" } else Settings(st) { dlg = it }
            }
        }
        HorizontalDivider(thickness = 1.dp, color = p.divider)   // mockup: navbar border-top 1px divider
        NavigationBar(containerColor = p.navBar, tonalElevation = 0.dp) {
            val items = listOf(Triple("聊天", R.drawable.ms_chats, R.drawable.ms_chats_fill), Triple("群组", R.drawable.ms_groups, R.drawable.ms_groups_fill),
                Triple("传输", R.drawable.ms_transfers, R.drawable.ms_transfers_fill), Triple("设置", R.drawable.ms_settings, R.drawable.ms_settings_fill))
            items.forEachIndexed { i, (label, ic, icOn) ->
                NavigationBarItem(selected = tab == i, onClick = { tab = i; searching = false; q = ""; if (dlg == "relay") dlg = "" }, label = { Text(label, fontWeight = if (tab == i) FontWeight.SemiBold else FontWeight.Medium) },
                    icon = { val n = when (i) { 0 -> totalUnread; 1 -> convs.filter { it.group }.sumOf { it.unread }; 2 -> st.transfers.count { it.state == "active" || it.state == "offered" }; else -> 0 }
                        if (n > 0) BadgedBox(badge = { Badge(containerColor = p.badge, contentColor = p.badgeFg) { Text("$n") } }) { Ic(if (tab == i) icOn else ic, null) } else Ic(if (tab == i) icOn else ic, null) },
                    colors = NavigationBarItemDefaults.colors(selectedIconColor = p.onSecCont, selectedTextColor = p.fg, indicatorColor = p.secCont, unselectedIconColor = p.fg2, unselectedTextColor = p.fg2))
            }
        }
    }
    when (dlg) {
        "add" -> TextDialog("按 IP 添加", "输入对方 IP 地址（可带端口，如 192.168.1.20 或 192.168.1.20:41235）：", onOk = { val e = Native.addAddr(it); if (e.isNotEmpty()) Repo.toast.value = e }, onDismiss = { dlg = "" })
        "name" -> TextDialog("修改昵称", "新的显示名称：", st.me.name, onOk = { Native.setName(it); Repo.refresh() }, onDismiss = { dlg = "" })
        "group" -> PickContacts(st, "创建群组", true, true, true, { fps, n -> val r = Native.groupCreate(n, fps.joinToString(",")); if (r.startsWith("!")) Repo.toast.value = r.drop(1); Repo.refresh() }, { dlg = "" })
        "pin" -> { val pin = remember { Native.pinOffer() }
            LsmAlertDialog(onDismissRequest = { dlg = "" }, title = { Text("PIN 配对") }, text = { Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.fillMaxWidth()) {
                Text(pin, style = MaterialTheme.typography.displayMedium, fontFamily = FontFamily.Monospace, letterSpacing = 6.sp, color = p.fg)
                Spacer(Modifier.height(8.dp)); Text("请让对方在“输入 PIN 配对”中输入（5 分钟内有效）。") } }, confirmButton = { TextButton({ dlg = "" }) { Text("好") } }) }
        "about" -> LsmAlertDialog(onDismissRequest = { dlg = "" }, title = { Text("关于") }, text = { Text("局域网安全通讯 Android 版 ${st.me.version}\n\n与 Windows v0.5、XP 版和 Linux 版完全互通（协议 v5）：Noise_XX_25519_ChaChaPoly_BLAKE2s 加密、自动发现、按 IP 添加、PIN 配对、群组、文件传输、送达/已读回执。\n\n本机端口 TCP ${st.me.port}") }, confirmButton = { TextButton({ dlg = "" }) { Text("好") } })
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
fun Chip(on: Boolean, label: String, icon: Int?, click: () -> Unit) {
    val p = LocalPalette.current
    FilterChip(selected = on, onClick = click, label = { Text(label, style = MaterialTheme.typography.labelLarge) },
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
@Composable
fun ConvRow(c: Conv, click: () -> Unit) {
    val p = LocalPalette.current; val m = c.last; val unread = c.unread > 0
    Row(Modifier.fillMaxWidth().height(72.dp).clickable(onClick = click).padding(horizontal = 16.dp), verticalAlignment = Alignment.CenterVertically) {
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
                Text(from + txt, fontSize = 14.sp, lineHeight = 20.sp, letterSpacing = .25.sp, color = pv, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
                if (unread) { Spacer(Modifier.width(8.dp)); CountBadge(c.unread) }
            }
        }
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
fun Settings(st: AppState, dlg: (String) -> Unit) {
    val p = LocalPalette.current
    @Composable fun SettingRow(icon: Int, title: String, sub: String?, click: (() -> Unit)?) =
        Row(Modifier.fillMaxWidth().heightIn(min = 56.dp).ifThen(click != null) { clickable { click!!() } }.padding(horizontal = 16.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            Ic(icon, null, p.fg2); Spacer(Modifier.width(16.dp))
            Column(Modifier.weight(1f)) { Text(title, fontSize = 16.sp, lineHeight = 24.sp, color = p.fg); if (sub != null) Text(sub, style = MaterialTheme.typography.bodyMedium, color = p.fg2, maxLines = 2, overflow = TextOverflow.Ellipsis) }
        }
    LazyColumn(Modifier.fillMaxSize()) {
        item { Row(Modifier.fillMaxWidth().padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
            Avatar(st.me.name, st.me.fp, 64.dp); Spacer(Modifier.width(16.dp))
            Column { Text(st.me.name, style = MaterialTheme.typography.titleLarge, color = p.fg); Text("指纹 " + st.me.fp, style = MaterialTheme.typography.bodySmall, color = p.fg2) } } }
        item { HorizontalDivider(color = p.divider) }
        item { SettingRow(R.drawable.ms_edit, "修改昵称", st.me.name) { dlg("name") } }
        item { SettingRow(R.drawable.ms_lock, "生成 PIN（让对方输入）", "与新设备配对并互相信任") { dlg("pin") } }
        item { SettingRow(R.drawable.ms_add_contact, "按 IP 添加", "不在同一网段时手动添加") { dlg("add") } }
        item { SettingRow(R.drawable.ms_groups, "创建群组", "从已信任的联系人中选择") { dlg("group") } }
        item { SettingRow(R.drawable.ms_via_server, "中转服务器（可选）", if (st.relay.enabled) relayStateZh(st.relay.state) + (if (st.relay.host.isNotEmpty()) " · ${st.relay.host}" else "") else "未启用 · 局域网直连优先") { dlg("relay") } }
        item { SettingRow(R.drawable.ms_network, "本机端口", "TCP ${st.me.port}", null) }
        item { SettingRow(R.drawable.ms_open_folder, "接收的文件", st.me.downloads, null) }
        item { SettingRow(R.drawable.ms_info, "关于", "版本 ${st.me.version}") { dlg("about") } }
    }
}
