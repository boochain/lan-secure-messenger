package io.github.frec86.lanmessenger

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.BitmapFactory
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.OpenableColumns
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import java.io.File
import java.text.SimpleDateFormat
import java.util.*

class MainActivity : ComponentActivity() {
    private val pendingShare = mutableStateOf<List<Uri>>(emptyList())
    private val openConv = mutableStateOf<String?>(null)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        CoreService.start(this)
        if (Build.VERSION.SDK_INT >= 33 && ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)
        handleIntent(intent)
        setContent { AppTheme { App(pendingShare, openConv) } }
    }
    override fun onNewIntent(intent: Intent) { super.onNewIntent(intent); handleIntent(intent) }
    private fun handleIntent(i: Intent?) {
        i ?: return
        i.getStringExtra("conv")?.let { openConv.value = it }
        @Suppress("DEPRECATION")
        when (i.action) {
            Intent.ACTION_SEND -> (i.getParcelableExtra<Uri>(Intent.EXTRA_STREAM))?.let { pendingShare.value = listOf(it) }
            Intent.ACTION_SEND_MULTIPLE -> i.getParcelableArrayListExtra<Uri>(Intent.EXTRA_STREAM)?.let { pendingShare.value = it }
        }
    }
}

/* ---------------------------------------------------------------- theme: Material You on 12+, readable fallbacks below */
@Composable
fun AppTheme(content: @Composable () -> Unit) {
    // Unified palette (design/palette.json, Fluent 2 hex, identical on all platforms). No dynamic colour: the
    // wallpaper-derived scheme (API 31+) and the stock purple M3 defaults are what made colours drift per device.
    val dark = isSystemInDarkTheme(); val p = if (dark) Palette.Dark else Palette.Light
    CompositionLocalProvider(LocalPalette provides p) {
        MaterialTheme(colorScheme = p.scheme(dark), typography = FluentTypography, content = content)
    }
}

/** Every text/background pair used by the UI. Pairs come from the same row, never mixed. */
data class Pal(val windowBg: Color, val sidebarBg: Color, val cardBg: Color, val chatBg: Color, val bubbleInBg: Color, val bubbleInFg: Color,
               val bubbleOutBg: Color, val bubbleOutFg: Color, val textPrimary: Color, val textSecondary: Color, val accent: Color, val buttonFg: Color,
               val inputBg: Color, val inputFg: Color, val divider: Color, val online: Color, val warning: Color, val error: Color, val badgeBg: Color, val badgeFg: Color) {
    fun scheme(dark: Boolean): ColorScheme = (if (dark) darkColorScheme() else lightColorScheme()).copy(
        primary = accent, onPrimary = buttonFg, primaryContainer = accent, onPrimaryContainer = buttonFg,
        secondary = accent, onSecondary = buttonFg, secondaryContainer = cardBg, onSecondaryContainer = textPrimary,
        tertiary = accent, onTertiary = buttonFg, tertiaryContainer = cardBg, onTertiaryContainer = textPrimary,
        background = windowBg, onBackground = textPrimary, surface = windowBg, onSurface = textPrimary,
        surfaceVariant = bubbleInBg, onSurfaceVariant = textSecondary, surfaceTint = Color.Transparent,
        surfaceContainerLowest = chatBg, surfaceContainerLow = sidebarBg, surfaceContainer = cardBg, surfaceContainerHigh = bubbleInBg, surfaceContainerHighest = inputBg,
        surfaceBright = cardBg, surfaceDim = sidebarBg, inverseSurface = textPrimary, inverseOnSurface = windowBg, inversePrimary = accent,
        error = error, onError = badgeFg, errorContainer = badgeBg, onErrorContainer = badgeFg, outline = textSecondary, outlineVariant = divider, scrim = Color.Black)
    companion object {
        private fun c(v: Long) = Color(0xFF000000 or v)
        val Light = Pal(c(0xF3F3F3), c(0xEEEEEE), c(0xFBFBFB), c(0xF9F9F9), c(0xFDFDFD), c(0x1B1B1B), c(0x005FB8), c(0xFFFFFF), c(0x1A1A1A), c(0x5C5C5C), c(0x005FB8), c(0xFFFFFF),
                        c(0xFDFDFD), c(0x1B1B1B), c(0xE5E5E5), c(0x0F7B0F), c(0x9D5D00), c(0xC42B1C), c(0xC42B1C), c(0xFFFFFF))
        val Dark = Pal(c(0x202020), c(0x1C1C1C), c(0x2B2B2B), c(0x282828), c(0x333333), c(0xFFFFFF), c(0x60CDFF), c(0x000000), c(0xFFFFFF), c(0xCCCCCC), c(0x60CDFF), c(0x000000),
                       c(0x353535), c(0xFFFFFF), c(0x323232), c(0x6CCB5F), c(0xFCE100), c(0xFF99A4), c(0xFF99A4), c(0x000000))
    }
}
val Palette = Pal
val LocalPalette = staticCompositionLocalOf { Pal.Light }

/** Fluent type ramp (px -> sp): caption 12/16, body 14/20, body-strong 14/20 600, subtitle 20/28 600, title 28/36 600. */
val FluentTypography: Typography = run {
    fun t(size: Int, lh: Int, w: FontWeight) = androidx.compose.ui.text.TextStyle(fontSize = size.sp, lineHeight = lh.sp, fontWeight = w, letterSpacing = 0.sp)
    val r = FontWeight.Normal; val s = FontWeight.SemiBold
    Typography(displayLarge = t(68, 92, s), displayMedium = t(40, 52, s), displaySmall = t(28, 36, s),
        headlineLarge = t(28, 36, s), headlineMedium = t(28, 36, s), headlineSmall = t(20, 28, s),
        titleLarge = t(20, 28, s), titleMedium = t(14, 20, s), titleSmall = t(14, 20, s),
        bodyLarge = t(14, 20, r), bodyMedium = t(14, 20, r), bodySmall = t(12, 16, r),
        labelLarge = t(14, 20, s), labelMedium = t(12, 16, r), labelSmall = t(12, 16, r))
}

/** Copy a SAF / share Uri into app cache so the core can read it by path (it streams in chunks). */
fun importUri(ctx: android.content.Context, uri: Uri): File? = try {
    var name = "file"
    ctx.contentResolver.query(uri, null, null, null, null)?.use { c -> val ix = c.getColumnIndex(OpenableColumns.DISPLAY_NAME); if (ix >= 0 && c.moveToFirst()) name = c.getString(ix) ?: name }
    name = name.replace('/', '_')
    val dir = File(ctx.cacheDir, "outgoing/" + System.nanoTime()).apply { mkdirs() }
    val f = File(dir, name)
    ctx.contentResolver.openInputStream(uri)!!.use { inp -> f.outputStream().use { inp.copyTo(it) } }
    f
} catch (e: Exception) { null }

fun sendUris(ctx: android.content.Context, conv: String, uris: List<Uri>) {
    for (u in uris) { val f = importUri(ctx, u); val err = if (f == null) "无法读取文件" else Native.sendFile(conv, f.absolutePath)
        if (err.isNotEmpty()) Repo.toast.value = errZh(err) }
    Repo.refresh()
}

/* ---------------------------------------------------------------- app shell */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun App(pendingShare: MutableState<List<Uri>>, openConvExt: MutableState<String?>) {
    val st by Repo.state.collectAsState()
    val toast by Repo.toast.collectAsState()
    val ctx = LocalContext.current
    var conv by remember { mutableStateOf<String?>(null) }
    LaunchedEffect(openConvExt.value) { openConvExt.value?.let { conv = it; openConvExt.value = null } }
    LaunchedEffect(toast) { toast?.let { Toast.makeText(ctx, it, Toast.LENGTH_LONG).show(); Repo.toast.value = null } }
    if (pendingShare.value.isNotEmpty()) SharePicker(st, onPick = { c -> sendUris(ctx, c, pendingShare.value); pendingShare.value = emptyList(); conv = c }, onDismiss = { pendingShare.value = emptyList() })
    val c = conv
    if (c == null) Home(st) { conv = it } else { BackHandler { conv = null }; Chat(st, c) { conv = null } }
}

/* ---------------------------------------------------------------- dialogs */
@Composable
fun TextDialog(title: String, label: String, init: String = "", digits: Boolean = false, onOk: (String) -> Unit, onDismiss: () -> Unit) {
    var t by remember { mutableStateOf(init) }
    LsmAlertDialog(onDismissRequest = onDismiss, title = { Text(title) },
        text = { Column { Text(label, style = MaterialTheme.typography.bodyMedium); Spacer(Modifier.height(8.dp))
            OutlinedTextField(t, { t = it }, singleLine = true, modifier = Modifier.fillMaxWidth(),
                keyboardOptions = if (digits) androidx.compose.foundation.text.KeyboardOptions(keyboardType = androidx.compose.ui.text.input.KeyboardType.Number) else androidx.compose.foundation.text.KeyboardOptions.Default) } },
        confirmButton = { TextButton({ onOk(t.trim()); onDismiss() }) { Text("确定") } }, dismissButton = { TextButton(onDismiss) { Text("取消") } })
}

@Composable
fun PickContacts(st: AppState, title: String, trustedOnly: Boolean, multi: Boolean, withName: Boolean, onOk: (List<String>, String) -> Unit, onDismiss: () -> Unit) {
    val list = st.contacts.filter { !trustedOnly || it.trust == 1 }
    val sel = remember { mutableStateListOf<String>() }; var name by remember { mutableStateOf("新群组") }
    LsmAlertDialog(onDismissRequest = onDismiss, title = { Text(title) }, text = {
        Column {
            if (list.isEmpty()) Text(if (trustedOnly) "没有已信任的联系人。请先信任对方（或使用 PIN 配对）。" else "没有联系人。")
            list.forEach { k -> Row(Modifier.fillMaxWidth().clickable { if (k.fp in sel) sel.remove(k.fp) else { if (!multi) sel.clear(); sel.add(k.fp) } }.padding(vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                Checkbox(k.fp in sel, null); Spacer(Modifier.width(8.dp)); Column { Text(k.name); Text(k.fp, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant) } } }
            if (withName) OutlinedTextField(name, { name = it }, label = { Text("群名") }, singleLine = true, modifier = Modifier.fillMaxWidth())
        } },
        confirmButton = { TextButton({ if (sel.isNotEmpty()) onOk(sel.toList(), name.trim()); onDismiss() }, enabled = sel.isNotEmpty()) { Text("确定") } },
        dismissButton = { TextButton(onDismiss) { Text("取消") } })
}

@Composable
fun SharePicker(st: AppState, onPick: (String) -> Unit, onDismiss: () -> Unit) {
    LsmAlertDialog(onDismissRequest = onDismiss, title = { Text("发送给…") }, text = {
        Column { if (st.contacts.isEmpty() && st.groups.isEmpty()) Text("还没有联系人")
            st.contacts.forEach { k -> Text(k.name, Modifier.fillMaxWidth().clickable { onPick("dm:" + k.fp) }.padding(vertical = 10.dp)) }
            st.groups.forEach { g -> Text("# " + g.name, Modifier.fillMaxWidth().clickable { onPick("group:" + g.id) }.padding(vertical = 10.dp)) } } },
        confirmButton = {}, dismissButton = { TextButton(onDismiss) { Text("取消") } })
}

/* ---------------------------------------------------------------- home */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun Home(st: AppState, open: (String) -> Unit) {
    var menu by remember { mutableStateOf(false) }; var dlg by remember { mutableStateOf("") }
    Scaffold(topBar = { TopAppBar(title = { Column { Text("局域网安全通讯"); Text(if (st.running) "${st.me.name} · ${st.me.fp}" else "正在启动…", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant) } },
        actions = {
            IconButton({ dlg = "add" }) { Icon(Icons.Default.Add, "按 IP 添加") }
            IconButton({ menu = true }) { Icon(Icons.Default.MoreVert, "菜单") }
            DropdownMenu(menu, { menu = false }) {
                DropdownMenuItem({ Text("按 IP 添加") }, { menu = false; dlg = "add" })
                DropdownMenuItem({ Text("生成 PIN（让对方输入）") }, { menu = false; dlg = "pin" })
                DropdownMenuItem({ Text("创建群组") }, { menu = false; dlg = "group" })
                DropdownMenuItem({ Text("修改昵称") }, { menu = false; dlg = "name" })
                DropdownMenuItem({ Text("关于") }, { menu = false; dlg = "about" })
            } }) },
        floatingActionButton = { ExtendedFloatingActionButton(onClick = { dlg = "add" }, icon = { Icon(Icons.Default.Add, null) }, text = { Text("按 IP 添加") }) }) { pad ->
        LazyColumn(Modifier.padding(pad).fillMaxSize()) {
            if (st.error.isNotEmpty()) item { Text("启动失败：" + st.error, color = MaterialTheme.colorScheme.error, modifier = Modifier.padding(16.dp)) }
            if (st.contacts.isEmpty() && st.groups.isEmpty()) item {
                Column(Modifier.fillMaxWidth().padding(32.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                    Text("同一 Wi-Fi 下的设备会自动出现在这里", style = MaterialTheme.typography.titleMedium)
                    Spacer(Modifier.height(8.dp)); Text("也可以点右下角按 IP 添加（如 192.168.1.20）", color = MaterialTheme.colorScheme.onSurfaceVariant) } }
            items(st.contacts, key = { it.fp }) { k ->
                ListItem(modifier = Modifier.clickable { open("dm:" + k.fp) },
                    leadingContent = { Avatar(k.name, k.online) },
                    headlineContent = { Text(k.name, maxLines = 1, overflow = TextOverflow.Ellipsis, fontWeight = if (k.unread > 0) FontWeight.Bold else FontWeight.Normal) },
                    supportingContent = { Text((if (k.online) "在线" else "离线") + " · " + trustTxt(k.trust) + (if (k.version.isNotEmpty()) " · " + k.version else "")) },
                    trailingContent = { if (k.unread > 0) Badge { Text("${k.unread}") } })
            }
            items(st.groups, key = { it.id }) { g ->
                ListItem(modifier = Modifier.clickable { open("group:" + g.id) }, leadingContent = { Avatar("#", true, group = true) },
                    headlineContent = { Text(g.name, fontWeight = if (g.unread > 0) FontWeight.Bold else FontWeight.Normal) }, supportingContent = { Text("群组 · ${g.members.size} 人") },
                    trailingContent = { if (g.unread > 0) Badge { Text("${g.unread}") } })
            }
        }
    }
    when (dlg) {
        "add" -> TextDialog("按 IP 添加", "输入对方 IP 地址（可带端口，如 192.168.1.20 或 192.168.1.20:41235）：", onOk = { val e = Native.addAddr(it); if (e.isNotEmpty()) Repo.toast.value = e }, onDismiss = { dlg = "" })
        "name" -> TextDialog("修改昵称", "新的显示名称：", st.me.name, onOk = { Native.setName(it); Repo.refresh() }, onDismiss = { dlg = "" })
        "group" -> PickContacts(st, "创建群组", true, true, true, { fps, n -> val r = Native.groupCreate(n, fps.joinToString(",")); if (r.startsWith("!")) Repo.toast.value = r.drop(1); Repo.refresh() }, { dlg = "" })
        "pin" -> { val pin = remember { Native.pinOffer() }
            LsmAlertDialog(onDismissRequest = { dlg = "" }, title = { Text("PIN 配对") }, text = { Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.fillMaxWidth()) {
                Text(pin, style = MaterialTheme.typography.displayMedium, fontFamily = FontFamily.Monospace, letterSpacing = 6.sp)
                Spacer(Modifier.height(8.dp)); Text("请让对方在“输入 PIN 配对”中输入（5 分钟内有效）。") } }, confirmButton = { TextButton({ dlg = "" }) { Text("好") } }) }
        "about" -> LsmAlertDialog(onDismissRequest = { dlg = "" }, title = { Text("关于") }, text = { Text("局域网安全通讯 Android 版 ${st.me.version}\n\n与 Windows v0.5、XP 版和 Linux 版完全互通（协议 v5）：Noise_XX_25519_ChaChaPoly_BLAKE2s 加密、自动发现、按 IP 添加、PIN 配对、群组、文件传输、送达/已读回执。\n\n本机端口 TCP ${st.me.port}") }, confirmButton = { TextButton({ dlg = "" }) { Text("好") } })
    }
}

fun trustTxt(t: Int) = when (t) { 1 -> "已信任"; 2 -> "已屏蔽"; else -> "未信任" }

@Composable
fun Avatar(name: String, online: Boolean, group: Boolean = false) {
    Box(Modifier.size(40.dp).clip(CircleShape).let { it }, contentAlignment = Alignment.Center) {
        Surface(color = if (group) MaterialTheme.colorScheme.tertiaryContainer else MaterialTheme.colorScheme.primaryContainer, shape = CircleShape, modifier = Modifier.fillMaxSize()) {}
        Text(name.take(1), color = if (group) MaterialTheme.colorScheme.onTertiaryContainer else MaterialTheme.colorScheme.onPrimaryContainer, fontWeight = FontWeight.Bold)
        if (!group) Box(Modifier.align(Alignment.BottomEnd).size(12.dp).clip(CircleShape)) { Surface(color = if (online) LocalPalette.current.online else MaterialTheme.colorScheme.outline, modifier = Modifier.fillMaxSize()) {} }
    }
}

/* ---------------------------------------------------------------- chat */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun Chat(st: AppState, conv: String, back: () -> Unit) {
    val ctx = LocalContext.current
    val rev by Repo.revision.collectAsState()
    val msgs = remember(rev, conv) { Repo.messages(conv) }
    val contact = st.contacts.firstOrNull { "dm:" + it.fp == conv }
    val group = st.groups.firstOrNull { "group:" + it.id == conv }
    var input by remember { mutableStateOf("") }; var menu by remember { mutableStateOf(false) }; var dlg by remember { mutableStateOf("") }
    var saveSrc by remember { mutableStateOf<String?>(null) }
    val listState = rememberLazyListState()
    DisposableEffect(conv) { Repo.openConv = conv; onDispose { Repo.openConv = null } }
    LaunchedEffect(msgs.size, rev) { Native.markRead(conv); if (msgs.isNotEmpty()) listState.animateScrollToItem(msgs.size - 1) }
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris -> if (uris.isNotEmpty()) sendUris(ctx, conv, uris) }
    val save = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/octet-stream")) { uri ->
        val src = saveSrc; if (uri != null && src != null) try { ctx.contentResolver.openOutputStream(uri)!!.use { o -> File(src).inputStream().use { it.copyTo(o) } }; Repo.toast.value = "已保存" } catch (e: Exception) { Repo.toast.value = "保存失败" } }
    Scaffold(topBar = { TopAppBar(navigationIcon = { IconButton(back) { Icon(Icons.Default.ArrowBack, "返回") } },
        title = { Column { Text(contact?.name ?: group?.let { "群组「${it.name}」" } ?: "", maxLines = 1, overflow = TextOverflow.Ellipsis)
            Text(contact?.let { (if (it.online) "在线" else "离线") + " · " + trustTxt(it.trust) + " · " + it.fp } ?: group?.let { "${it.members.size} 人 · 密钥版本 ${it.epoch}" } ?: "",
                style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 1, overflow = TextOverflow.Ellipsis) } },
        actions = { IconButton({ menu = true }) { Icon(Icons.Default.MoreVert, "菜单") }
            DropdownMenu(menu, { menu = false }) {
                if (contact != null) {
                    DropdownMenuItem({ Text("信任") }, { menu = false; Native.setTrust(contact.fp, 1); Repo.refresh() })
                    DropdownMenuItem({ Text("取消信任") }, { menu = false; Native.setTrust(contact.fp, 0); Repo.refresh() })
                    DropdownMenuItem({ Text("屏蔽") }, { menu = false; Native.setTrust(contact.fp, 2); Repo.refresh() })
                    DropdownMenuItem({ Text("输入 PIN 配对") }, { menu = false; dlg = "pin" })
                }
                if (group != null) {
                    DropdownMenuItem({ Text("邀请成员") }, { menu = false; dlg = "invite" })
                    DropdownMenuItem({ Text("移除成员") }, { menu = false; dlg = "remove" })
                    DropdownMenuItem({ Text("退出群组") }, { menu = false; Native.groupLeave(group.id); Repo.refresh(); back() })
                }
            } }) },
        bottomBar = { Surface(tonalElevation = 3.dp) { Row(Modifier.fillMaxWidth().padding(8.dp).navigationBarsPadding().imePadding(), verticalAlignment = Alignment.CenterVertically) {
            IconButton({ pick.launch(arrayOf("*/*")) }) { Icon(Icons.Default.AddCircle, "发送文件") }
            OutlinedTextField(input, { input = it }, Modifier.weight(1f), placeholder = { Text("输入消息") }, maxLines = 4)
            IconButton({ if (input.isNotBlank()) { val e = Native.sendText(conv, input.trim()); if (e.isNotEmpty()) Repo.toast.value = e else input = ""; Repo.refresh() } }) { Icon(Icons.Default.Send, "发送", tint = MaterialTheme.colorScheme.primary) }
        } } }) { pad ->
        LazyColumn(Modifier.padding(pad).fillMaxSize(), state = listState, contentPadding = PaddingValues(vertical = 8.dp)) {
            items(msgs, key = { it.id }) { m -> Bubble(m, st.transfers.firstOrNull { it.msg == m.id }) { p -> saveSrc = p; save.launch(m.file) } }
        }
    }
    when (dlg) {
        "pin" -> TextDialog("输入 PIN 配对", "输入对方显示的 6 位 PIN：", digits = true, onOk = { val e = Native.pinPair(contact!!.fp, it); if (e.isNotEmpty()) Repo.toast.value = e }, onDismiss = { dlg = "" })
        "invite" -> PickContacts(st, "邀请成员", true, true, false, { fps, _ -> val e = Native.groupInvite(group!!.id, fps.joinToString(",")); if (e.isNotEmpty()) Repo.toast.value = e; Repo.refresh() }, { dlg = "" })
        "remove" -> PickContacts(st.copy(contacts = st.contacts.filter { it.fp in (group?.members ?: emptyList()) }), "移除成员（将自动更换群密钥）", false, false, false,
            { fps, _ -> val e = Native.groupRemove(group!!.id, fps[0]); if (e.isNotEmpty()) Repo.toast.value = e; Repo.refresh() }, { dlg = "" })
    }
}

private val fmt = SimpleDateFormat("MM-dd HH:mm", Locale.CHINA)
fun statusZh(s: String) = when (s) { "queued" -> "排队中"; "sent" -> "已发送"; "delivered" -> "已送达"; "read" -> "已读"; "failed" -> "发送失败"; else -> "" }
fun xferZh(s: String) = when (s) { "offered" -> "等待接收"; "active" -> "传输中"; "verifying" -> "校验中"; "done" -> "完成"; "failed" -> "失败"; "rejected" -> "被拒收"; "cancelled" -> "已取消"; else -> s }

@Composable
fun Bubble(m: Msg, t: Transfer?, onSave: (String) -> Unit) {
    if (m.kind == 2) { Text(fmt.format(Date(m.ts)) + " · " + m.body, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.fillMaxWidth().padding(8.dp), textAlign = androidx.compose.ui.text.style.TextAlign.Center); return }
    val cs = MaterialTheme.colorScheme
    Column(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 4.dp), horizontalAlignment = if (m.out) Alignment.End else Alignment.Start) {
        Text((if (m.out) "我" else m.sender) + " · " + fmt.format(Date(m.ts)) + (if (m.out) " · " + statusZh(m.status) else ""), style = MaterialTheme.typography.labelSmall, color = cs.onSurfaceVariant)
        val pl = LocalPalette.current
        Surface(color = if (m.out) pl.bubbleOutBg else pl.bubbleInBg, contentColor = if (m.out) pl.bubbleOutFg else pl.bubbleInFg, shape = RoundedCornerShape(8.dp), border = if (m.out) null else androidx.compose.foundation.BorderStroke(1.dp, pl.divider), modifier = Modifier.padding(top = 2.dp).widthIn(max = 300.dp)) {
            Column(Modifier.padding(horizontal = 12.dp, vertical = 8.dp)) {
                if (m.kind == 1) {
                    val path = if (t != null && (t.state == "done" || t.out)) t.path else m.path
                    val st = t?.let { xferZh(it.state) + (if (it.state == "active" && it.size > 0) " ${it.done * 100 / it.size}%" else "") + (if (it.error.isNotEmpty()) " " + errZh(it.error) else "") } ?: if (!m.out && path.isNotEmpty() && File(path).exists()) "已保存" else ""
                    Text("📄 ${m.file}  (${"%.1f".format(m.size / 1024.0)} KB)  $st")
                    if (path.isNotEmpty() && isImage(m.file) && File(path).exists()) {
                        val bmp = remember(path) { decodeSampled(path, 600) }
                        if (bmp != null) Image(bmp.asImageBitmap(), m.file, Modifier.padding(top = 6.dp).widthIn(max = 260.dp).heightIn(max = 220.dp).clip(RoundedCornerShape(8.dp)))
                    }
                    if (!m.out && path.isNotEmpty() && File(path).exists()) TextButton({ onSave(path) }, contentPadding = PaddingValues(0.dp)) { Text("保存到…", color = if (m.out) cs.onPrimary else cs.primary) }
                } else Text(m.body)
            }
        }
    }
}
fun isImage(n: String) = n.substringAfterLast('.', "").lowercase() in setOf("png", "jpg", "jpeg", "gif", "bmp", "webp")
fun decodeSampled(path: String, max: Int): android.graphics.Bitmap? {
    val o = BitmapFactory.Options().apply { inJustDecodeBounds = true }; BitmapFactory.decodeFile(path, o)
    var s = 1; while (o.outWidth / s > max || o.outHeight / s > max) s *= 2
    return BitmapFactory.decodeFile(path, BitmapFactory.Options().apply { inSampleSize = s })
}

/** Modal dialog drawn entirely by us: palette card on our own scrim inside ONE full-screen dialog window
 *  (platform dim disabled). The M3 AlertDialog used `surface` and the platform dim layer was composited over it
 *  on the test emulator, giving a non-palette grey card (#D9D9D9 / #1D1D1D). */
@Composable
fun LsmAlertDialog(onDismissRequest: () -> Unit, confirmButton: @Composable () -> Unit, dismissButton: (@Composable () -> Unit)? = null,
                   title: (@Composable () -> Unit)? = null, text: (@Composable () -> Unit)? = null) {
    val p = LocalPalette.current
    androidx.compose.ui.window.Dialog(onDismissRequest = onDismissRequest,
        properties = androidx.compose.ui.window.DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        val w = (androidx.compose.ui.platform.LocalView.current.parent as? androidx.compose.ui.window.DialogWindowProvider)?.window
        SideEffect { w?.setDimAmount(0f) }
        Box(Modifier.fillMaxSize().background(Color(0x99000000)).clickable(indication = null,
                interactionSource = remember { androidx.compose.foundation.interaction.MutableInteractionSource() }) { onDismissRequest() },
            contentAlignment = Alignment.Center) {
            Surface(color = p.cardBg, contentColor = p.textPrimary, shape = RoundedCornerShape(8.dp),
                border = androidx.compose.foundation.BorderStroke(1.dp, p.divider),
                modifier = Modifier.padding(horizontal = 32.dp).widthIn(max = 560.dp).fillMaxWidth()
                    .clickable(indication = null, interactionSource = remember { androidx.compose.foundation.interaction.MutableInteractionSource() }) {}) {
                Column(Modifier.padding(24.dp)) {
                    if (title != null) { ProvideTextStyle(MaterialTheme.typography.titleLarge) { title() }; Spacer(Modifier.height(16.dp)) }
                    if (text != null) { ProvideTextStyle(MaterialTheme.typography.bodyMedium) { CompositionLocalProvider(LocalContentColor provides p.textPrimary) { text() } } }
                    Row(Modifier.fillMaxWidth().padding(top = 24.dp), horizontalArrangement = Arrangement.End) {
                        if (dismissButton != null) { dismissButton(); Spacer(Modifier.width(8.dp)) }
                        confirmButton()
                    }
                }
            }
        }
    }
}
