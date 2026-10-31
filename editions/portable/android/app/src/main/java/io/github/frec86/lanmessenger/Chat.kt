package io.github.frec86.lanmessenger

import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import java.io.File

/* ---------------------------------------------------------------- chat screen (spec §5.2-5.6, Android column) */
@Composable
fun Chat(st: AppState, conv: String, openPage: (String) -> Unit = {}, back: () -> Unit) {
    val ctx = LocalContext.current; val p = LocalPalette.current
    val rev by Repo.revision.collectAsState()
    val all = remember(rev, conv) { Repo.messages(conv) }
    val contact = st.contacts.firstOrNull { "dm:" + it.fp == conv }
    val group = st.groups.firstOrNull { "group:" + it.id == conv }
    var emojiOpen by remember { mutableStateOf(false) }
    var input by remember { mutableStateOf("") }; var menu by remember { mutableStateOf(false) }; var dlg by remember { mutableStateOf("") }
    var searching by rememberSaveable { mutableStateOf(false) }; var q by rememberSaveable { mutableStateOf("") }
    var msgMenu by remember { mutableStateOf<Msg?>(null) }; var delMsg by remember { mutableStateOf<Msg?>(null) }
    var replyTo by remember(conv) { mutableStateOf<Msg?>(null) }; var editing by remember(conv) { mutableStateOf<Msg?>(null) }; var recallMsg by remember { mutableStateOf<Msg?>(null) }
    val recording by Voice.recording
    val clip = androidx.compose.ui.platform.LocalClipboardManager.current
    // system Back / predictive back: close the in-chat search first, then return to the list (menus/dialogs dismiss themselves)
    BackHandler { if (searching) { searching = false; q = "" } else back() }
    var saveSrc by remember { mutableStateOf<String?>(null) }
    val msgs = if (searching && q.isNotBlank()) all.filter { it.body.contains(q, true) || it.file.contains(q, true) } else all
    val rows = remember(msgs, group != null) { buildRows(msgs, group != null) }
    val listState = rememberLazyListState()
    DisposableEffect(conv) { Repo.openConv = conv; onDispose { Repo.openConv = null } }
    LaunchedEffect(rows.size, rev) { Native.markRead(conv); val j = Repo.jumpId; val ji = if (j != null) rows.indexOfFirst { (it as? ChatRow.Bubble)?.m?.id == j } else -1
        if (ji >= 0) { listState.scrollToItem(maxOf(0, ji - 1)); Repo.jumpId = null; Repo.toast.value = "已定位到该消息" } else if (rows.isNotEmpty()) listState.scrollToItem(rows.size - 1) }
    fun micToggle() {
        if (Voice.recording.value) { val f = Voice.stop(); if (f == null) Repo.toast.value = "录音太短" else { val e = Native.sendFile(conv, f.absolutePath); if (e.isNotEmpty()) Repo.toast.value = errZh(e); Repo.refresh() } }
        else { val e = Voice.start(ctx); if (e.isNotEmpty()) Repo.toast.value = e else Repo.toast.value = "正在录音… 再点一次停止并发送" }
    }
    val micPerm = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { ok -> if (ok) micToggle() else Repo.toast.value = "需要麦克风权限才能发送语音" }
    fun micClick() { if (androidx.core.content.ContextCompat.checkSelfPermission(ctx, android.Manifest.permission.RECORD_AUDIO) == android.content.pm.PackageManager.PERMISSION_GRANTED) micToggle() else micPerm.launch(android.Manifest.permission.RECORD_AUDIO) }
    fun sendNow() {
        if (input.isBlank()) return
        val ed = editing; val rp = replyTo
        val e = when { ed != null -> Native.editMessage(ed.id, input.trim()); rp != null -> Native.sendReply(conv, input.trim(), rp.id); else -> Native.sendText(conv, input.trim()) }
        if (e.isNotEmpty()) Repo.toast.value = e else { input = ""; editing = null; replyTo = null }; Repo.refresh()
    }
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris -> if (uris.isNotEmpty()) sendUris(ctx, conv, uris) }
    val save = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/octet-stream")) { uri ->
        val src = saveSrc; if (uri != null && src != null) try { ctx.contentResolver.openOutputStream(uri)!!.use { o -> File(src).inputStream().use { it.copyTo(o) } }; Repo.toast.value = "已保存" } catch (e: Exception) { Repo.toast.value = "保存失败" } }
    val name = contact?.name ?: group?.name ?: ""
    val key = contact?.fp ?: group?.id ?: conv
    val maxW = ((LocalConfiguration.current.screenWidthDp - 24) * .92f).coerceAtMost(720f).dp
    /* mockup: status bar + messages + composer on the window colour, the gesture strip on `chat` */
    Column(Modifier.fillMaxSize().background(p.chat)) {
      Column(Modifier.fillMaxSize().navigationBarsPadding().imePadding().background(p.window).statusBarsPadding()) {
        /* header: back · avatar(+dot) · name + shield · status · search · more */
        Row(Modifier.fillMaxWidth().height(64.dp).background(p.bar).padding(horizontal = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            IconButton(back) { Ic(R.drawable.ms_back, "返回", p.fg) }
            if (searching) {
                SearchField(q, { q = it }, "搜索聊天记录", Modifier.weight(1f))
                IconButton({ searching = false; q = "" }) { Ic(R.drawable.ms_close, "关闭搜索", p.fg) }
            } else {
                Avatar(name, key, 40.dp, contact?.online == true, p.bar)
                Column(Modifier.weight(1f).padding(start = 12.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(name, style = MaterialTheme.typography.titleMedium, color = p.fg, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f, false))
                        if (contact?.trust == 1) Ic(R.drawable.ms_trusted_fill, "已验证身份", p.accent, 18.dp, Modifier.padding(start = 4.dp))
                    }
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        val s = MaterialTheme.typography.bodySmall
                        if (contact != null) {
                            Text(if (contact.online) "在线" else "离线", style = s, color = if (contact.online) p.online else p.fg2)
                            // 在线 · 已验证身份 · 经服务器 / 局域网直连 (switches live when the core changes the path)
                            val via = contact.online && contact.via
                            val path = if (!contact.online || contact.trust == 2) "" else if (via) " · 经服务器" else " · 局域网直连"
                            Text(" · " + when (contact.trust) { 1 -> "已验证身份"; 2 -> "已屏蔽"; else -> "未信任" } + path, style = s, color = p.fg2, maxLines = 1, overflow = TextOverflow.Ellipsis,
                                modifier = Modifier.weight(1f, false))
                            if (via) Ic(R.drawable.ms_via_server, "经服务器中转", p.fg2, 14.dp, Modifier.padding(start = 4.dp))
                        } else if (group != null) Text("${group.members.size} 位成员 · 密钥版本 ${group.epoch}", style = s, color = p.fg2, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    }
                }
                IconButton({ searching = true }) { Ic(R.drawable.ms_search, "搜索聊天记录", p.fg) }
                Box {
                    IconButton({ menu = true }) { Ic(R.drawable.ms_more, "菜单", p.fg) }
                    DropdownMenu(menu, { menu = false }) {
                        if (contact != null) {
                            DropdownMenuItem({ Text("联系人资料 / 备注名") }, { menu = false; openPage("cprofile:" + contact.fp) }, leadingIcon = { Ic(R.drawable.ms_info) })
                            DropdownMenuItem({ Text("消息历史") }, { menu = false; openPage("history") }, leadingIcon = { Ic(R.drawable.ms_search) })
                            DropdownMenuItem({ Text("信任") }, { menu = false; Native.setTrust(contact.fp, 1); Repo.refresh() }, leadingIcon = { Ic(R.drawable.ms_trusted) })
                            DropdownMenuItem({ Text("取消信任") }, { menu = false; Native.setTrust(contact.fp, 0); Repo.refresh() }, leadingIcon = { Ic(R.drawable.ms_offline) })
                            DropdownMenuItem({ Text(if (Native.isMuted(contact.fp)) "关闭免打扰" else "免打扰") }, { menu = false; Native.setMuted(contact.fp, !Native.isMuted(contact.fp)); Repo.refresh() }, leadingIcon = { Ic(R.drawable.ms_mute) })
                            DropdownMenuItem({ Text("诊断信息") }, { menu = false; dlg = "diag" }, leadingIcon = { Ic(R.drawable.ms_lock) })
                            DropdownMenuItem({ Text("清空聊天记录") }, { menu = false; dlg = "clear" }, leadingIcon = { Ic(R.drawable.ms_delete) })
                            DropdownMenuItem({ Text("屏蔽") }, { menu = false; dlg = "block" }, leadingIcon = { Ic(R.drawable.ms_mute) })
                            DropdownMenuItem({ Text("输入 PIN 配对") }, { menu = false; dlg = "pin" }, leadingIcon = { Ic(R.drawable.ms_lock) })
                            DropdownMenuItem({ Text("查看指纹") }, { menu = false; dlg = "fp" }, leadingIcon = { Ic(R.drawable.ms_info) })
                        }
                        if (group != null) {
                            DropdownMenuItem({ Text("邀请成员") }, { menu = false; dlg = "invite" }, leadingIcon = { Ic(R.drawable.ms_add_contact) })
                            DropdownMenuItem({ Text("移除成员") }, { menu = false; dlg = "remove" }, leadingIcon = { Ic(R.drawable.ms_minimize) })
                            DropdownMenuItem({ Text("退出群组") }, { menu = false; dlg = "leave" }, leadingIcon = { Ic(R.drawable.ms_close) })
                        }
                    }
                }
            }
        }
        /* messages over the doodle wallpaper */
        val wall = WALL_ICONS.map { painterResource(it) }
        Box(Modifier.weight(1f).fillMaxWidth().drawBehind { wallpaper(wall, p.wall, p.wallAlpha) }) {
            if (rows.isEmpty()) Column(Modifier.align(Alignment.Center).padding(32.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                if (searching) Text("没有找到“$q”", color = p.fg2) else {
                    ServicePill(lock = true, text = if (group != null) "消息已端到端加密，只有群成员能看到" else "消息已端到端加密，只有你和${name}能看到")
                    Spacer(Modifier.height(12.dp)); Text("发送第一条消息吧", style = MaterialTheme.typography.bodyMedium, color = p.fg2)
                }
            }
            LazyColumn(Modifier.fillMaxSize(), state = listState, contentPadding = PaddingValues(top = 8.dp, bottom = 8.dp, start = 4.dp, end = 4.dp)) {
                items(rows, key = { it.key }) { r ->
                    when (r) {
                        is ChatRow.Date -> Box(Modifier.fillMaxWidth().padding(top = 12.dp, bottom = 4.dp), contentAlignment = Alignment.Center) { ServicePill(false, r.label) }
                        is ChatRow.E2E -> Box(Modifier.fillMaxWidth().padding(top = 4.dp, bottom = 0.dp), contentAlignment = Alignment.Center) {
                            ServicePill(true, if (group != null) "消息已端到端加密，只有群成员能看到" else "消息已端到端加密，只有你和${name}能看到") }
                        is ChatRow.System -> Box(Modifier.fillMaxWidth().padding(vertical = 4.dp), contentAlignment = Alignment.Center) { ServicePill(false, hm(r.m.ts) + " · " + r.m.body) }
                        is ChatRow.Bubble -> Box(Modifier.fillMaxWidth().pointerInput(r.m.id) { detectTapGestures(onLongPress = { msgMenu = r.m }) }) {
                            MessageRow(r, st.transfers.firstOrNull { it.msg == r.m.id }, maxW) { path -> saveSrc = path; save.launch(r.m.file) }
                            // long-press menu (Material: long press shows the item's actions)
                            DropdownMenu(msgMenu?.id == r.m.id, { msgMenu = null }, Modifier.align(if (r.m.out) Alignment.TopEnd else Alignment.TopStart)) {
                                if (!r.m.recalled) {
                                    DropdownMenuItem({ Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) { REACTIONS.forEach { em ->
                                        Text(em, fontSize = 22.sp, modifier = Modifier.clip(CircleShape).background(if (r.m.mine == em) p.field else Color.Transparent).clickable {
                                            msgMenu = null; val e = Native.react(r.m.id, if (r.m.mine == em) "" else em); if (e.isNotEmpty()) Repo.toast.value = e; Repo.refresh() }.padding(4.dp)) } } }, {})
                                    DropdownMenuItem({ Text("回复") }, { msgMenu = null; editing = null; replyTo = r.m }, leadingIcon = { Ic(R.drawable.ms_reply) })
                                    if (r.m.canModify) {
                                        DropdownMenuItem({ Text("编辑") }, { msgMenu = null; replyTo = null; editing = r.m; input = r.m.body }, leadingIcon = { Ic(R.drawable.ms_edit) })
                                        DropdownMenuItem({ Text("撤回") }, { msgMenu = null; recallMsg = r.m }, leadingIcon = { Ic(R.drawable.ms_back) })
                                    }
                                }
                                if (r.m.kind == 0 && !r.m.recalled) DropdownMenuItem({ Text("复制") }, { clip.setText(androidx.compose.ui.text.AnnotatedString(r.m.body)); msgMenu = null; Repo.toast.value = "已复制" }, leadingIcon = { Ic(R.drawable.ms_copy) })
                                if (r.m.out && r.m.status == "failed") DropdownMenuItem({ Text("重发") }, {
                                    msgMenu = null
                                    val e = if (r.m.kind == 0) Native.sendText(conv, r.m.body)
                                    else if (r.m.path.isNotEmpty()) Native.sendFile(conv, r.m.path) else "无法重发"
                                    if (e.isNotEmpty()) Repo.toast.value = errZh(e) else Repo.toast.value = "已重新发送"
                                    Repo.refresh()
                                }, leadingIcon = { Ic(R.drawable.ms_info) })
                                DropdownMenuItem({ Text("删除（仅本机）") }, { delMsg = r.m; msgMenu = null }, leadingIcon = { Ic(R.drawable.ms_delete) })
                            }
                        }
                    }
                }
            }
        }
        (editing ?: replyTo)?.let { m -> ReplyBar(m, editing != null) { if (editing != null) input = ""; editing = null; replyTo = null } }
        /* composer: [field · attach · mic] (send) */
        Row(Modifier.fillMaxWidth().heightIn(min = 64.dp).padding(8.dp), verticalAlignment = Alignment.Bottom, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(Modifier.weight(1f).heightIn(min = 48.dp).ifThen(!isDark()) { shadow(1.dp, RoundedCornerShape(24.dp)) }.clip(RoundedCornerShape(24.dp)).background(p.field).padding(horizontal = 4.dp),
                verticalAlignment = Alignment.CenterVertically) {
                IconButton({ emojiOpen = !emojiOpen }, Modifier.size(40.dp)) { Ic(R.drawable.ms_emoji, "表情", p.fg2) }
                Box(Modifier.weight(1f).padding(start = 4.dp, top = 12.dp, bottom = 12.dp)) {
                    if (input.isEmpty()) Text("输入消息", style = MaterialTheme.typography.bodyLarge, color = p.fg2)
                    BasicTextField(input, { input = it }, Modifier.fillMaxWidth().semantics { contentDescription = "输入消息" }, maxLines = 5,
                        textStyle = MaterialTheme.typography.bodyLarge.copy(color = p.fg), cursorBrush = SolidColor(p.accent))
                }
                IconButton({ pick.launch(arrayOf("*/*")) }, Modifier.size(40.dp)) { Ic(R.drawable.ms_attach, "发送文件", p.fg2) }
                IconButton({ micClick() }, Modifier.size(40.dp)) { Ic(if (recording) R.drawable.ms_stop else R.drawable.ms_mic, if (recording) "停止并发送语音" else "语音消息", if (recording) p.error else p.fg2) }
            }
            Box(Modifier.size(48.dp).ifThen(!isDark()) { shadow(2.dp, CircleShape) }.clip(CircleShape).background(p.accent)
                .clickable { sendNow() },
                contentAlignment = Alignment.Center) { Ic(R.drawable.ms_send_fill, "发送", p.onAccent) }
        }
        if (emojiOpen) {
            val emojis = listOf("😀", "😃", "😄", "😁", "😆", "😅", "😂", "🙂", "😉", "😊", "😇", "🥰", "😍", "🤩", "😘", "😋", "😜", "🤗", "🤔", "😴", "🥳", "😎", "😢", "😭", "😤", "👍", "👎", "👏", "🙏", "💪", "❤️", "🔥", "⭐", "✨", "🎉", "✅", "❌", "💯")
            val stickers = listOf("thumb", "heart", "fire", "star", "party", "check")
            val ctx = androidx.compose.ui.platform.LocalContext.current
            LazyColumn(Modifier.fillMaxWidth().heightIn(max = 200.dp).padding(horizontal = 8.dp)) {
                items(emojis.chunked(8)) { row ->
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly) {
                        row.forEach { e -> Text(e, fontSize = 26.sp, modifier = Modifier.clickable { input += e }.padding(4.dp)) }
                    }
                }
                item {
                    Text("本地贴纸", color = p.fg2, modifier = Modifier.padding(top = 6.dp, bottom = 4.dp))
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly) {
                        stickers.forEach { name ->
                            Text(name, fontSize = 12.sp, color = p.accent, modifier = Modifier.clickable {
                                try {
                                    val dir = java.io.File(ctx.cacheDir, "stickers"); dir.mkdirs()
                                    val out = java.io.File(dir, "$name.png")
                                    if (!out.exists()) ctx.assets.open("stickers/$name.png").use { inp -> out.outputStream().use { inp.copyTo(it) } }
                                    Native.sendFile(conv, out.absolutePath)
                                    emojiOpen = false
                                } catch (_: Exception) {}
                            }.padding(6.dp))
                        }
                    }
                }
            }
        }
      }
    }
    recallMsg?.let { m ->
        LsmAlertDialog(onDismissRequest = { recallMsg = null }, title = { Text("撤回这条消息？") }, text = { Text("双方都会看到“撤回了一条消息”。24 小时内可撤回。") },
            confirmButton = { TextButton({ recallMsg = null; val e = Native.recallMessage(m.id); if (e.isNotEmpty()) Repo.toast.value = e; Repo.refresh() }) { Text("撤回", color = p.error) } },
            dismissButton = { TextButton({ recallMsg = null }) { Text("取消") } })
    }
    delMsg?.let { m ->
        LsmAlertDialog(onDismissRequest = { delMsg = null }, title = { Text("删除这条消息？") },
            text = { Text("只会从本机删除，对方仍保留这条消息。此操作无法撤销。") },
            confirmButton = { TextButton({ delMsg = null; if (!Native.deleteMessage(m.id)) Repo.toast.value = "删除失败"; Repo.refresh() }) { Text("删除", color = p.error) } },
            dismissButton = { TextButton({ delMsg = null }) { Text("取消") } })
    }
    when (dlg) {
        "clear" -> LsmAlertDialog(onDismissRequest = { dlg = "" }, title = { Text("清空聊天记录？") }, text = { Text("将删除本机上与此会话的全部聊天记录（对方仍保留）。此操作无法撤销。") },
            confirmButton = { TextButton({ dlg = ""; val n = Native.clearConversation(conv); Repo.toast.value = "已清空 $n 条记录"; Repo.refresh() }) { Text("清空", color = p.error) } },
            dismissButton = { TextButton({ dlg = "" }) { Text("取消") } })
        "diag" -> LsmAlertDialog(onDismissRequest = { dlg = "" }, title = { Text("诊断信息") },
            text = { Text(Native.diagnostics(), fontSize = 11.sp, modifier = Modifier.verticalScroll(rememberScrollState())) },
            confirmButton = { TextButton({ dlg = "" }) { Text("关闭") } })
        // destructive actions are confirmed (Material: confirmation dialog for irreversible/disruptive actions)
        "block" -> LsmAlertDialog(onDismissRequest = { dlg = "" }, title = { Text("屏蔽 ${contact?.name ?: ""}？") },
            text = { Text("屏蔽后将不再接收对方的消息和文件。可随时在菜单中取消屏蔽。") },
            confirmButton = { TextButton({ dlg = ""; contact?.let { Native.setTrust(it.fp, 2) }; Repo.refresh() }) { Text("屏蔽", color = p.error) } },
            dismissButton = { TextButton({ dlg = "" }) { Text("取消") } })
        "leave" -> LsmAlertDialog(onDismissRequest = { dlg = "" }, title = { Text("退出群组“${group?.name ?: ""}”？") },
            text = { Text("退出后将不再收到该群的消息，需要成员重新邀请才能加入。") },
            confirmButton = { TextButton({ dlg = ""; group?.let { Native.groupLeave(it.id) }; Repo.refresh(); back() }) { Text("退出", color = p.error) } },
            dismissButton = { TextButton({ dlg = "" }) { Text("取消") } })
        "pin" -> TextDialog("输入 PIN 配对", "输入对方显示的 6 位 PIN：", digits = true, onOk = { val e = Native.pinPair(contact!!.fp, it); if (e.isNotEmpty()) Repo.toast.value = e }, onDismiss = { dlg = "" })
        "fp" -> LsmAlertDialog(onDismissRequest = { dlg = "" }, title = { Text(contact?.name ?: "") }, text = { Text("指纹：${contact?.fp}\n地址：${contact?.addr}\n版本：${contact?.version}\n状态：${trustTxt(contact?.trust ?: 0)}") },
            confirmButton = { TextButton({ dlg = "" }) { Text("好") } })
        "invite" -> PickContacts(st, "邀请成员", true, true, false, { fps, _ -> val e = Native.groupInvite(group!!.id, fps.joinToString(",")); if (e.isNotEmpty()) Repo.toast.value = e; Repo.refresh() }, { dlg = "" })
        "remove" -> PickContacts(st.copy(contacts = st.contacts.filter { it.fp in (group?.members ?: emptyList()) }), "移除成员（将自动更换群密钥）", false, false, false,
            { fps, _ -> val e = Native.groupRemove(group!!.id, fps[0]); if (e.isNotEmpty()) Repo.toast.value = e; Repo.refresh() }, { dlg = "" })
    }
}

@Composable fun isDark() = LocalPalette.current === Pal.Dark

@Composable
fun SearchField(v: String, on: (String) -> Unit, hint: String, modifier: Modifier = Modifier) {
    val p = LocalPalette.current
    Box(modifier.height(48.dp).padding(horizontal = 4.dp), contentAlignment = Alignment.CenterStart) {
        if (v.isEmpty()) Text(hint, style = MaterialTheme.typography.bodyLarge, color = p.fg2)
        BasicTextField(v, on, Modifier.fillMaxWidth(), singleLine = true, textStyle = MaterialTheme.typography.bodyLarge.copy(color = p.fg), cursorBrush = SolidColor(p.accent))
    }
}

@Composable
fun ServicePill(lock: Boolean, text: String) {
    val p = LocalPalette.current
    Row(Modifier.widthIn(max = 320.dp).clip(RoundedCornerShape(if (lock) 8.dp else 12.dp)).background(p.service).padding(start = 12.dp, end = 12.dp, top = 3.dp, bottom = 4.dp),
        verticalAlignment = Alignment.CenterVertically) {
        if (lock) { Ic(R.drawable.ms_lock, null, p.serviceFg, 14.dp); Spacer(Modifier.width(6.dp)) }
        Text(text, fontSize = 12.sp, lineHeight = 16.sp, fontWeight = FontWeight.Medium, color = p.serviceFg, textAlign = TextAlign.Center)
    }
}

/** Time + ticks; `over` = white on a dark pill over a photo. */
@Composable
fun Meta(m: Msg, over: Boolean = false) {
    val p = LocalPalette.current; val c = if (over) Color.White else if (m.out) p.mineMeta else p.meta
    Row(Modifier.ifThen(over) { clip(CircleShape).background(p.imageScrim).padding(horizontal = 8.dp, vertical = 1.dp) }, verticalAlignment = Alignment.CenterVertically) {
        Text(hm(m.ts), fontSize = 11.sp, lineHeight = 14.sp, letterSpacing = .3.sp, color = c)
        if (m.out) { Spacer(Modifier.width(3.dp)); Ticks(m.status, c, if (over) Color.White else p.read) }
    }
}

@Composable
fun MessageRow(r: ChatRow.Bubble, t: Transfer?, maxW: androidx.compose.ui.unit.Dp, onSave: (String) -> Unit) {
    val m = r.m; val p = LocalPalette.current
    val bg = if (m.out) p.mine else p.theirs; val fg = if (m.out) p.mineFg else p.theirsFg
    val shape = BubbleShape(m.out, r.first)
    val path = if (m.kind == 1) (if (t != null && (t.state == "done" || t.out)) t.path else m.path) else ""
    val haveFile = path.isNotEmpty() && File(path).exists()
    Column(Modifier.fillMaxWidth()) {
        Row(Modifier.fillMaxWidth().padding(top = if (r.first) 12.dp else 2.dp), horizontalArrangement = if (m.out) Arrangement.End else Arrangement.Start) {
        // the shape reserves 8 dp for the tail on the outer side, so every bubble of a run shares one edge
        Box(Modifier.widthIn(max = maxW + 8.dp).ifThen(!isDark()) { shadow(.5.dp, shape) }.clip(shape).background(bg)
            .padding(start = if (m.out) 0.dp else 8.dp, end = if (m.out) 8.dp else 0.dp)) {
            CompositionLocalProvider(LocalContentColor provides fg) {
                when {
                    m.kind == 1 && m.voice >= 0 -> VoiceBubble(m, path, haveFile)
                    m.kind == 1 && haveFile && isImage(m.file) -> ImageBubble(m, path, r.first)
                    m.kind == 1 -> FileBubble(m, t, path, haveFile, maxW, onSave)
                    else -> TextBubble(m, if (r.showSender) m.sender else null)
                }
            }
        }
    }
    if (m.reactions.isNotEmpty() && !m.recalled) Row(Modifier.fillMaxWidth().padding(top = 2.dp), horizontalArrangement = if (m.out) Arrangement.End else Arrangement.Start) { ReactionChips(m) }
    }
}

/** Text + time/ticks, Telegram-style: the meta sits on the last line when it fits, otherwise on its own line below. */
@Composable
fun TextBubble(m: Msg, sender: String?) {
    val holder = remember { arrayOfNulls<androidx.compose.ui.text.TextLayoutResult>(1) }
    Column(Modifier.padding(start = 12.dp, end = 12.dp, top = 6.dp, bottom = 7.dp)) {
        QuoteStrip(m)
        if (sender != null) Text(sender, fontSize = 14.sp, lineHeight = 20.sp, fontWeight = FontWeight.Medium, color = avatarColorFor(sender), maxLines = 1, overflow = TextOverflow.Ellipsis)
        androidx.compose.ui.layout.Layout({
            Text(displayBody(m), fontSize = 16.sp, lineHeight = 22.sp, onTextLayout = { holder[0] = it }, fontStyle = if (m.recalled) androidx.compose.ui.text.font.FontStyle.Italic else null)
            Meta(m)
        }) { ms, c ->
            val text = ms[0].measure(c.copy(minWidth = 0)); val meta = ms[1].measure(c.copy(minWidth = 0))
            val tl = holder[0]; val gap = 8.dp.roundToPx()
            val lastW = if (tl == null || tl.lineCount == 0) text.width else kotlin.math.ceil(tl.getLineRight(tl.lineCount - 1) - tl.getLineLeft(tl.lineCount - 1)).toInt()
            val inline = lastW + gap + meta.width <= c.maxWidth
            val w = maxOf(text.width, if (inline) lastW + gap + meta.width else meta.width).coerceAtMost(c.maxWidth)
            val my = if (inline) text.height - meta.height + 1.dp.roundToPx() else text.height
            layout(w, maxOf(text.height, my + meta.height)) { text.place(0, 0); meta.place(w - meta.width, my) }
        }
    }
}
@Composable fun avatarColorFor(sender: String) = if (isDark()) LocalPalette.current.accent else avatarColor(sender)

@Composable
fun ImageBubble(m: Msg, path: String, first: Boolean) {
    val bmp = remember(path) { decodeSampled(path, 720) }
    var full by remember { mutableStateOf(false) }
    Box(Modifier.padding(4.dp).clickable { full = true }) {
        val inner = RoundedCornerShape(topStart = if (!m.out && first) 2.dp else 13.dp, topEnd = if (m.out && first) 2.dp else 13.dp, bottomEnd = 13.dp, bottomStart = 13.dp)
        if (bmp != null) Image(bmp.asImageBitmap(), m.file, Modifier.size(240.dp, 165.dp).clip(inner), contentScale = ContentScale.Crop)
        else Box(Modifier.size(240.dp, 165.dp).clip(inner).background(LocalPalette.current.tile))
        Box(Modifier.align(Alignment.BottomEnd).padding(8.dp)) { Meta(m, over = true) }
    }
    if (full) {
        val big = remember(path) { decodeSampled(path, 2048) }
        Dialog(onDismissRequest = { full = false }, properties = DialogProperties(usePlatformDefaultWidth = false)) {
            Box(Modifier.fillMaxSize().background(Color.Black).clickable { full = false }, contentAlignment = Alignment.Center) {
                if (big != null) Image(big.asImageBitmap(), m.file, Modifier.fillMaxWidth().fillMaxHeight(), contentScale = ContentScale.Fit)
                Text("点击或返回关闭", color = Color.White.copy(alpha = .7f), modifier = Modifier.align(Alignment.BottomCenter).padding(24.dp), fontSize = 13.sp)
            }
        }
    }
}

@Composable
fun FileBubble(m: Msg, t: Transfer?, path: String, haveFile: Boolean, maxW: androidx.compose.ui.unit.Dp, onSave: (String) -> Unit) {
    val p = LocalPalette.current
    val state = t?.let { if (it.status.isNotEmpty() && it.state != "done") it.status else xferZh(it.state) + (if (it.state == "active" && it.size > 0) " ${it.done * 100 / it.size}%" else "") + (if (it.error.isNotEmpty()) " · " + errZh(it.error) else "") }
        ?: if (!m.out && haveFile) "已接收" else ""
    Column(Modifier.width(minOf(maxW, 280.dp)).padding(start = 8.dp, end = 8.dp, top = 8.dp, bottom = 6.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.size(40.dp).clip(RoundedCornerShape(8.dp)).background(if (m.out) p.theirs else p.tile), contentAlignment = Alignment.Center) { Ic(fileIcon(m.file), null, p.tileFg, 24.dp) }
            Column(Modifier.weight(1f).padding(horizontal = 12.dp)) {
                Text(m.file, fontSize = 14.sp, lineHeight = 20.sp, fontWeight = FontWeight.Medium, maxLines = 1, overflow = TextOverflow.Ellipsis)
                Text(humanSize(m.size) + " · " + ext(m.file), fontSize = 12.sp, lineHeight = 16.sp, color = if (m.out) p.mineMeta else p.meta, maxLines = 1)
            }
            if (!m.out && haveFile) Box(Modifier.size(40.dp).clip(CircleShape).border(1.dp, p.divider, CircleShape).clickable { onSave(path) }, contentAlignment = Alignment.Center) {
                Ic(R.drawable.ms_download, "保存到…", p.accent, 20.dp) }
        }
        if (t != null && t.state == "active" && t.size > 0) LinearProgressIndicator(progress = { t.done.toFloat() / t.size }, Modifier.fillMaxWidth().padding(top = 8.dp), color = p.accent, trackColor = p.divider)
        Row(Modifier.fillMaxWidth().padding(top = 6.dp).heightIn(min = 20.dp), verticalAlignment = Alignment.CenterVertically) {
            if (!m.out && haveFile) Row(Modifier.clickable { onSave(path) }, verticalAlignment = Alignment.CenterVertically) {
                Ic(R.drawable.ms_open_folder, null, p.accent, 16.dp); Spacer(Modifier.width(4.dp)); Text("保存到…", fontSize = 12.sp, fontWeight = FontWeight.Medium, color = p.accent) }
            else if (state.isNotEmpty()) Text(state, fontSize = 12.sp, color = if (m.out) p.mineMeta else p.meta, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f, false))
            Spacer(Modifier.weight(1f)); Meta(m)
        }
    }
}
