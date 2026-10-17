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
import androidx.activity.enableEdgeToEdge
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
    private val pendingText = mutableStateOf<String?>(null)

    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        CoreService.start(this)
        if (Build.VERSION.SDK_INT >= 33 && ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)
        handleIntent(intent)
        setContent { AppTheme { App(pendingShare, openConv, pendingText) } }
    }
    override fun onNewIntent(intent: Intent) { super.onNewIntent(intent); handleIntent(intent) }
    private fun handleIntent(i: Intent?) {
        i ?: return
        i.getStringExtra("conv")?.let { openConv.value = it }
        @Suppress("DEPRECATION")
        when (i.action) {
            Intent.ACTION_SEND -> { val u = i.getParcelableExtra<Uri>(Intent.EXTRA_STREAM)
                if (u != null) pendingShare.value = listOf(u) else i.getStringExtra(Intent.EXTRA_TEXT)?.takeIf { it.isNotBlank() }?.let { pendingText.value = it } }
            Intent.ACTION_SEND_MULTIPLE -> i.getParcelableArrayListExtra<Uri>(Intent.EXTRA_STREAM)?.let { pendingShare.value = it }
        }
    }
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
fun App(pendingShare: MutableState<List<Uri>>, openConvExt: MutableState<String?>, pendingText: MutableState<String?>) {
    val st by Repo.state.collectAsState()
    val toast by Repo.toast.collectAsState()
    val ctx = LocalContext.current
    var conv by remember { mutableStateOf<String?>(null) }
    LaunchedEffect(openConvExt.value) { openConvExt.value?.let { conv = it; openConvExt.value = null } }
    LaunchedEffect(toast) { toast?.let { Toast.makeText(ctx, it, Toast.LENGTH_LONG).show(); Repo.toast.value = null } }
    if (pendingShare.value.isNotEmpty()) SharePicker(st, onPick = { c -> sendUris(ctx, c, pendingShare.value); pendingShare.value = emptyList(); conv = c }, onDismiss = { pendingShare.value = emptyList() })
    pendingText.value?.let { txt -> SharePicker(st, onPick = { c -> val e = Native.sendText(c, txt.trim()); if (e.isNotEmpty()) Repo.toast.value = e; Repo.refresh(); pendingText.value = null; conv = c },
        onDismiss = { pendingText.value = null }) }
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
                Checkbox(k.fp in sel, null); Spacer(Modifier.width(8.dp)); Avatar(k.name, k.fp, 40.dp, k.online, LocalPalette.current.dialog); Spacer(Modifier.width(12.dp))
                Column { Text(k.name, color = LocalPalette.current.fg); Text(k.fp, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant) } } }
            if (withName) OutlinedTextField(name, { name = it }, label = { Text("群名") }, singleLine = true, modifier = Modifier.fillMaxWidth())
        } },
        confirmButton = { TextButton({ if (sel.isNotEmpty()) onOk(sel.toList(), name.trim()); onDismiss() }, enabled = sel.isNotEmpty()) { Text("确定") } },
        dismissButton = { TextButton(onDismiss) { Text("取消") } })
}

@Composable
fun SharePicker(st: AppState, onPick: (String) -> Unit, onDismiss: () -> Unit) {
    LsmAlertDialog(onDismissRequest = onDismiss, title = { Text("发送给…") }, text = {
        Column { if (st.contacts.isEmpty() && st.groups.isEmpty()) Text("还没有联系人")
            st.convs().forEach { k -> Row(Modifier.fillMaxWidth().clickable { onPick(k.id) }.padding(vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                Avatar(k.name, k.key, 40.dp, k.online, LocalPalette.current.dialog); Spacer(Modifier.width(16.dp))
                if (k.group) { Ic(R.drawable.ms_groups, "群组", size = 18.dp); Spacer(Modifier.width(6.dp)) }
                Text(k.name, color = LocalPalette.current.fg, fontSize = 16.sp, maxLines = 1, overflow = TextOverflow.Ellipsis) } } } },
        confirmButton = {}, dismissButton = { TextButton(onDismiss) { Text("取消") } })
}

fun trustTxt(t: Int) = when (t) { 1 -> "已信任"; 2 -> "已屏蔽"; else -> "未信任" }
fun statusZh(s: String) = when (s) { "queued" -> "排队中"; "sent" -> "已发送"; "delivered" -> "已送达"; "read" -> "已读"; "failed" -> "发送失败"; else -> "" }
fun xferZh(s: String) = when (s) { "offered" -> "等待接收"; "active" -> "传输中"; "verifying" -> "校验中"; "done" -> "完成"; "failed" -> "失败"; "rejected" -> "被拒收"; "cancelled" -> "已取消"; else -> s }
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
        properties = androidx.compose.ui.window.DialogProperties(usePlatformDefaultWidth = false)) {
        val w = (androidx.compose.ui.platform.LocalView.current.parent as? androidx.compose.ui.window.DialogWindowProvider)?.window
        SideEffect { w?.setDimAmount(0f) }
        Box(Modifier.fillMaxSize().background(Color(0x99000000)).imePadding().clickable(indication = null,
                interactionSource = remember { androidx.compose.foundation.interaction.MutableInteractionSource() }) { onDismissRequest() },
            contentAlignment = Alignment.Center) {
            Surface(color = p.dialog, contentColor = p.fg, shape = RoundedCornerShape(28.dp),
                modifier = Modifier.padding(horizontal = 24.dp).widthIn(min = 280.dp, max = 560.dp).fillMaxWidth()
                    .clickable(indication = null, interactionSource = remember { androidx.compose.foundation.interaction.MutableInteractionSource() }) {}) {
                Column(Modifier.padding(24.dp)) {
                    if (title != null) { ProvideTextStyle(MaterialTheme.typography.headlineSmall) { title() }; Spacer(Modifier.height(16.dp)) }
                    if (text != null) { ProvideTextStyle(MaterialTheme.typography.bodyMedium) { CompositionLocalProvider(LocalContentColor provides p.fg2) { text() } } }
                    Row(Modifier.fillMaxWidth().padding(top = 24.dp), horizontalArrangement = Arrangement.End, verticalAlignment = Alignment.CenterVertically) {
                        if (dismissButton != null) { dismissButton(); Spacer(Modifier.width(8.dp)) }
                        confirmButton()
                    }
                }
            }
        }
    }
}
