package io.github.frec86.lanmessenger

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import java.io.File

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.unit.dp

/* v0.6.1 UX: my QR (a phone camera opens the lsm:// link straight into this app), add by link with fingerprint-checked
 * auto trust, file size + ETA confirm, Ctrl+K quick switcher (hardware keyboards). Resend + reason live in Chat.kt. */
object Ux {
    /** lsm://add?… opened from a QR scan / link; confirmed by UxHost */
    val pendingUri = mutableStateOf<String?>(null)
    /** files imported for sending, waiting for the size / ETA confirm */
    val pendingFiles = mutableStateOf<Pair<String, List<String>>?>(null)
    val quickSwitch = mutableStateOf(false)
    val openHistory = mutableStateOf(false)
    fun uriName(u: String) = Regex("[?&]n=([^&]*)").find(u)?.groupValues?.get(1)?.let { try { java.net.URLDecoder.decode(it, "UTF-8") } catch (e: Exception) { it } } ?: ""
    fun addUri(u: String) { val e = Native.addUri(u.trim()); Repo.toast.value = if (e.isNotEmpty()) e else "正在连接…指纹一致后自动设为已信任"; Repo.refresh() }
}

fun fuzzy(hay: String, q: String): Int {
    if (q.isEmpty()) return 1
    var score = 1; var run = 0; var j = 0
    for ((i, ch) in hay.lowercase().withIndex()) { if (j < q.length && ch == q[j].lowercaseChar()) { j++; run++; score += run * 2 + if (i == 0) 5 else 0 } else run = 0 }
    return if (j == q.length) score else 0
}

@Composable
fun QrDialog(onDismiss: () -> Unit) {
    val uri = remember { Native.myUri() }
    val qr = remember(uri) { Native.qr(uri) }
    val n = qr.substringBefore(':').toIntOrNull() ?: 0; val bits = qr.substringAfter(':')
    val clip = LocalClipboardManager.current
    LsmAlertDialog(onDismissRequest = onDismiss, title = { Text("我的二维码") },
        text = { Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.fillMaxWidth()) {
            if (n > 0 && uri.contains("a=") && !uri.contains("a=:")) {
                // dark modules on white in both themes: scanners need it
                Canvas(Modifier.size(232.dp).background(Color.White).semantics { contentDescription = "添加我的二维码" }) {
                    val cell = size.minDimension / (n + 8)
                    for (y in 0 until n) for (x in 0 until n) if (bits.getOrNull(y * n + x) == '1')
                        drawRect(Color.Black, Offset((x + 4) * cell, (y + 4) * cell), Size(cell + 0.5f, cell + 0.5f))
                }
            } else Text("未连接局域网，暂时无法生成二维码。")
            Spacer(Modifier.height(8.dp))
            Text("让对方用相机扫描（会直接打开本应用添加你），或把链接发给对方粘贴到「通过链接添加」。指纹一致会自动设为已信任。", style = MaterialTheme.typography.bodySmall)
            Spacer(Modifier.height(6.dp)); Text(uri, style = MaterialTheme.typography.labelSmall)
        } },
        confirmButton = { TextButton({ clip.setText(AnnotatedString(uri)); Repo.toast.value = "链接已复制" }) { Text("复制链接") } },
        dismissButton = { TextButton(onDismiss) { Text("关闭") } })
}

fun isSendImage(path: String): Boolean {
    val n = path.substringAfterLast('/').lowercase()
    if (path.contains("sticker") || n.contains("lsm-sticker")) return false
    return n.endsWith(".png") || n.endsWith(".jpg") || n.endsWith(".jpeg") || n.endsWith(".bmp") || n.endsWith(".webp")
}
fun compressImageFile(src: String): String? {
    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
    BitmapFactory.decodeFile(src, bounds)
    val w = bounds.outWidth; val h = bounds.outHeight
    if (w <= 0 || h <= 0) return null
    val sample = (maxOf(w, h) / 1920).coerceAtLeast(1)
    val bmp = BitmapFactory.decodeFile(src, BitmapFactory.Options().apply { inSampleSize = sample }) ?: return null
    val outBmp = if (bmp.width > 1920 || bmp.height > 1920) {
        val k = 1920f / maxOf(bmp.width, bmp.height)
        Bitmap.createScaledBitmap(bmp, (bmp.width * k).toInt().coerceAtLeast(1), (bmp.height * k).toInt().coerceAtLeast(1), true).also { if (it !== bmp) bmp.recycle() }
    } else bmp
    val dest = File(System.getProperty("java.io.tmpdir") ?: ".", "lsm-img-${System.currentTimeMillis()}.jpg")
    return try {
        dest.outputStream().use { outBmp.compress(Bitmap.CompressFormat.JPEG, 80, it) }
        dest.absolutePath
    } catch (_: Exception) { null } finally { if (!outBmp.isRecycled) outBmp.recycle() }
}
/** global dialogs: scanned link confirm, file size / ETA confirm, Ctrl+K */
@Composable
fun UxHost(st: AppState, open: (String) -> Unit) {
    Ux.pendingUri.value?.let { u ->
        val name = Ux.uriName(u)
        LsmAlertDialog(onDismissRequest = { Ux.pendingUri.value = null }, title = { Text("添加联系人？") },
            text = { Text((if (name.isNotEmpty()) "「$name」\n" else "") + "将连接对方并核对指纹，一致后自动设为已信任。") },
            confirmButton = { TextButton({ Ux.addUri(u); Ux.pendingUri.value = null }) { Text("添加") } },
            dismissButton = { TextButton({ Ux.pendingUri.value = null }) { Text("取消") } })
    }
    Ux.pendingFiles.value?.let { (conv, files) ->
        val lines = remember(files) { files.map { Native.filePreview(conv, it) } }
        val imgs = files.filter { isSendImage(it) }
        fun sendAll(compress: Boolean) {
            Ux.pendingFiles.value = null
            for (f in files) {
                val path = if (compress && isSendImage(f)) (compressImageFile(f) ?: f) else f
                val e = Native.sendFile(conv, path); if (e.isNotEmpty()) Repo.toast.value = errZh(e)
            }
            Repo.refresh()
        }
        LsmAlertDialog(onDismissRequest = { Ux.pendingFiles.value = null },
            title = { Text(if (imgs.isNotEmpty() && imgs.size == files.size) (if (files.size == 1) "发送图片？" else "发送 ${files.size} 张图片？") else if (files.size == 1) "发送文件？" else "发送 ${files.size} 个文件？") },
            text = { Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                lines.forEach { Text(it, style = MaterialTheme.typography.bodyMedium) }
                if (imgs.isNotEmpty()) Text("默认压缩后发送（最长边 1920 像素，JPEG 画质约 80，去掉拍摄信息）。", style = MaterialTheme.typography.bodyMedium)
                if (imgs.isNotEmpty()) TextButton({ sendAll(false) }) { Text("原图发送") }
            } },
            confirmButton = { TextButton({ sendAll(imgs.isNotEmpty()) }) { Text(if (imgs.isNotEmpty()) "压缩发送" else "发送") } },
            dismissButton = { TextButton({ Ux.pendingFiles.value = null }) { Text("取消") } })
    }
    if (Ux.quickSwitch.value) {
        var q by remember { mutableStateOf("") }
        val fr = remember { FocusRequester() }
        val hits = remember(q, st) { st.convs().map { it to fuzzy(it.name, q) }.filter { it.second > 0 }.sortedByDescending { it.second }.take(30).map { it.first } }
        LsmAlertDialog(onDismissRequest = { Ux.quickSwitch.value = false }, title = { Text("切换会话") },
            text = { Column {
                OutlinedTextField(q, { q = it }, Modifier.fillMaxWidth().focusRequester(fr), singleLine = true, label = { Text("名称的任意几个字") },
                    keyboardActions = androidx.compose.foundation.text.KeyboardActions(onDone = { hits.firstOrNull()?.let { Ux.quickSwitch.value = false; open(it.id) } }),
                    keyboardOptions = androidx.compose.foundation.text.KeyboardOptions(imeAction = androidx.compose.ui.text.input.ImeAction.Done))
                LazyColumn(Modifier.heightIn(max = 320.dp)) { items(hits, key = { it.id }) { c ->
                    Text(c.name, Modifier.fillMaxWidth().clickable { Ux.quickSwitch.value = false; open(c.id) }.padding(vertical = 10.dp)) } }
            } },
            confirmButton = { TextButton({ Ux.quickSwitch.value = false }) { Text("关闭") } })
        LaunchedEffect(Unit) { fr.requestFocus() }
    }
}
