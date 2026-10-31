package io.github.frec86.lanmessenger

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import org.json.JSONObject
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/* v0.6.1 P0: 消息历史（搜索+筛选+分页+跳转）、个人资料、联系人资料与备注名 */
data class HistItem(val id: String, val conv: String, val out: Boolean, val type: Int, val status: String, val ts: Long, val sender: String, val body: String, val file: String)

@Composable
private fun PageBar(title: String, back: () -> Unit) {
    val p = LocalPalette.current
    Row(Modifier.fillMaxWidth().height(64.dp).background(p.bar).padding(horizontal = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        IconButton(back) { Ic(R.drawable.ms_back, "返回", p.fg) }
        Text(title, style = MaterialTheme.typography.titleLarge, color = p.fg)
    }
}

@Composable
fun AvatarPic(path: String, name: String, key: String, size: androidx.compose.ui.unit.Dp) {
    val bmp = remember(path, File(path).takeIf { path.isNotEmpty() }?.lastModified()) { if (path.isEmpty()) null else BitmapFactory.decodeFile(path) }
    if (bmp != null) Image(bmp.asImageBitmap(), name, Modifier.size(size).clip(CircleShape), contentScale = ContentScale.Crop) else Avatar(name, key, size)
}

@Composable
private fun <T> Choice(label: String, options: List<Pair<String, T>>, sel: T, on: (T) -> Unit) {
    var open by remember { mutableStateOf(false) }
    Box { AssistChip({ open = true }, { Text(options.firstOrNull { it.second == sel }?.first ?: label) })
        DropdownMenu(open, { open = false }) { options.forEach { o -> DropdownMenuItem({ Text(o.first) }, { on(o.second); open = false }) } } }
}

private fun dayMs(s: String, end: Boolean): Long = try { val d = SimpleDateFormat("yyyy-MM-dd", Locale.CHINA).parse(s.trim())!!.time; if (end) d + 86_400_000L else d } catch (e: Exception) { 0L }

@Composable
fun HistoryScreen(st: AppState, initialConv: String?, back: () -> Unit, jump: (String, String) -> Unit) {
    val p = LocalPalette.current
    BackHandler { back() }
    var q by remember { mutableStateOf("") }; var conv by remember { mutableStateOf(initialConv ?: "") }
    var type by remember { mutableIntStateOf(0) }; var dir by remember { mutableIntStateOf(0) }; var stat by remember { mutableIntStateOf(0) }
    var from by remember { mutableStateOf("") }; var to by remember { mutableStateOf("") }
    val items = remember { mutableStateListOf<HistItem>() }; var total by remember { mutableIntStateOf(0) }
    fun run(append: Boolean) {
        if (!append) items.clear()
        val o = JSONObject(Native.historyQuery(q, conv, dayMs(from, false), dayMs(to, true), type, dir, stat, items.size, 50))
        total = o.getInt("total"); val a = o.getJSONArray("items")
        for (i in 0 until a.length()) a.getJSONObject(i).let { items.add(HistItem(it.getString("id"), it.getString("conv"), it.getBoolean("out"), it.getInt("type"), it.getString("status"), it.getLong("ts"), it.getString("sender"), it.getString("body"), it.getString("file"))) }
    }
    LaunchedEffect(q, conv, type, dir, stat, from, to) { run(false) }
    val convs = listOf("全部会话" to "") + st.contacts.map { it.name to "dm:" + it.fp } + st.groups.map { "# " + it.name to "group:" + it.id }
    val names = convs.associate { it.second to it.first }
    Column(Modifier.fillMaxSize().background(p.window).statusBarsPadding().navigationBarsPadding().imePadding()) {
        PageBar("消息历史", back)
        SearchField(q, { q = it }, "搜索消息（支持中文）", Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 4.dp))
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(horizontal = 8.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Choice("会话", convs, conv) { conv = it }
            Choice("类型", listOf("全部类型" to 0, "文字" to 1, "图片" to 2, "文件" to 4, "贴纸" to 8), type) { type = it }
            Choice("方向", listOf("收发" to 0, "收到的" to 1, "发出的" to 2), dir) { dir = it }
            Choice("状态", listOf("全部状态" to 0, "已读" to (1 shl 3), "失败/拒收" to ((1 shl 5) or (1 shl 6)), "已拒收" to (1 shl 6)), stat) { stat = it }
        }
        Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedTextField(from, { from = it }, Modifier.weight(1f), label = { Text("起始 YYYY-MM-DD") }, singleLine = true)
            OutlinedTextField(to, { to = it }, Modifier.weight(1f), label = { Text("结束（含当天）") }, singleLine = true)
        }
        Text("共 $total 条，已显示 ${items.size} 条", color = p.fg2, style = MaterialTheme.typography.bodySmall, modifier = Modifier.padding(horizontal = 16.dp, vertical = 6.dp))
        LazyColumn(Modifier.weight(1f).fillMaxWidth()) {
            items(items, key = { it.id }) { h ->
                Column(Modifier.fillMaxWidth().clickable { jump(h.conv, h.id) }.padding(horizontal = 16.dp, vertical = 10.dp)) {
                    Text(SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.CHINA).format(Date(h.ts)) + " · " + (names[h.conv] ?: "") + " · " + (if (h.out) "我" else h.sender) + " · " + statusZh(h.status),
                        color = p.fg2, style = MaterialTheme.typography.bodySmall, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Text(if (h.type == 0) h.body.replace('\n', ' ') else (if (h.type == 1) "🖼 " else if (h.type == 3) "🏷 " else "📄 ") + h.file, color = p.fg, maxLines = 2, overflow = TextOverflow.Ellipsis)
                }
            }
            if (items.size < total) item { TextButton({ run(true) }, Modifier.fillMaxWidth()) { Text("加载更多") } }
        }
    }
}

@Composable
private fun KV(k: String, v: String) {
    val p = LocalPalette.current
    Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp)) { Text(k, color = p.fg2, style = MaterialTheme.typography.bodySmall); Text(v.ifEmpty { "—" }, color = p.fg) }
}

@Composable
fun MyProfileScreen(st: AppState, back: () -> Unit) {
    val p = LocalPalette.current; val ctx = LocalContext.current
    BackHandler { back() }
    var name by remember { mutableStateOf(st.me.name) }; var sig by remember { mutableStateOf(st.me.sig) }
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        if (uri != null) try {
            val src = ctx.contentResolver.openInputStream(uri)!!.use { BitmapFactory.decodeStream(it) }
            val s = 96f / maxOf(src.width, src.height); val b = Bitmap.createScaledBitmap(src, maxOf(1, (src.width * s).toInt()), maxOf(1, (src.height * s).toInt()), true)
            val f = File(ctx.cacheDir, "avatar-upload.png"); f.outputStream().use { b.compress(Bitmap.CompressFormat.PNG, 100, it) }
            val e = Native.setProfile(null, f.absolutePath); f.delete(); Repo.toast.value = if (e.isEmpty()) "头像已更新" else e; Repo.refresh()
        } catch (e: Exception) { Repo.toast.value = "无法读取图片" }
    }
    Column(Modifier.fillMaxSize().background(p.window).statusBarsPadding().navigationBarsPadding().imePadding()) {
        PageBar("个人资料", back)
        Column(Modifier.weight(1f).verticalScroll(rememberScrollState()), horizontalAlignment = Alignment.CenterHorizontally) {
            Spacer(Modifier.height(16.dp)); AvatarPic(st.me.avatar, st.me.name, st.me.fp, 96.dp)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) { TextButton({ pick.launch("image/*") }) { Text("更换头像") }; if (st.me.avatar.isNotEmpty()) TextButton({ Native.setProfile(null, ""); Repo.refresh() }) { Text("移除头像") } }
            OutlinedTextField(name, { name = it }, Modifier.fillMaxWidth().padding(horizontal = 16.dp), label = { Text("昵称") }, singleLine = true)
            OutlinedTextField(sig, { if (it.codePointCount(0, it.length) <= 120) sig = it }, Modifier.fillMaxWidth().padding(16.dp), label = { Text("个性签名（最多 120 字）") })
            KV("指纹", st.me.fp); KV("版本", st.me.version)
            Button({ Native.setName(name.trim()); val e = Native.setProfile(sig.trim(), null); Repo.toast.value = if (e.isEmpty()) "个人资料已保存" else e; Repo.refresh(); back() },
                Modifier.padding(16.dp), colors = ButtonDefaults.buttonColors(containerColor = p.accent, contentColor = p.onAccent)) { Text("保存") }
        }
    }
}

@Composable
fun ContactProfileScreen(st: AppState, fp: String, back: () -> Unit, openChat: () -> Unit, history: () -> Unit, jump: (String, String) -> Unit) {
    val p = LocalPalette.current
    BackHandler { back() }
    val c = st.contacts.firstOrNull { it.fp == fp } ?: run { back(); return }
    var remark by remember(fp) { mutableStateOf(c.remark) }
    val rev by Repo.revision.collectAsState()
    val files = remember(rev, fp) { Repo.messages("dm:$fp").filter { it.kind == 1 && !isStickerName(it.file) }.reversed().take(100) }
    Column(Modifier.fillMaxSize().background(p.window).statusBarsPadding().navigationBarsPadding().imePadding()) {
        PageBar("联系人资料", back)
        LazyColumn(Modifier.weight(1f), horizontalAlignment = Alignment.CenterHorizontally) {
            item {
                Spacer(Modifier.height(16.dp)); AvatarPic(c.avatar, c.name, c.fp, 96.dp)
                Text(c.name, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold, color = p.fg, modifier = Modifier.padding(top = 8.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.padding(8.dp)) {
                    Button(openChat, colors = ButtonDefaults.buttonColors(containerColor = p.accent, contentColor = p.onAccent)) { Text("发消息") }
                    OutlinedButton(history) { Text("消息历史") }
                }
                Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp), verticalAlignment = Alignment.CenterVertically) {
                    OutlinedTextField(remark, { remark = it }, Modifier.weight(1f), label = { Text("备注名（仅本机）") }, singleLine = true)
                    TextButton({ Native.setRemark(fp, remark.trim()); Repo.refresh(); Repo.toast.value = "备注名已保存" }) { Text("保存") }
                }
                KV("昵称", c.nick.ifEmpty { c.name }); KV("个性签名", c.sig.ifEmpty { "（未设置或对方版本较旧）" })
                KV("状态", (if (c.online) "在线" else "离线") + if (c.online) (if (c.via) " · 经服务器中转" else " · 局域网直连") else "")
                KV("身份", trustTxt(c.trust)); KV("版本", c.version); KV("指纹", c.fp)
                Text("共享的文件与图片", fontWeight = FontWeight.SemiBold, color = p.fg, modifier = Modifier.fillMaxWidth().padding(start = 16.dp, top = 12.dp, bottom = 4.dp))
                if (files.isEmpty()) Text("暂无", color = p.fg2, modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp))
            }
            items(files, key = { it.id }) { m ->
                Row(Modifier.fillMaxWidth().clickable { jump("dm:$fp", m.id) }.padding(horizontal = 16.dp, vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
                    Ic(if (isImageName(m.file)) R.drawable.ms_image else R.drawable.ms_file, null, p.fg2); Spacer(Modifier.width(12.dp))
                    Column(Modifier.weight(1f)) { Text(m.file, color = p.fg, maxLines = 1, overflow = TextOverflow.Ellipsis); Text(SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.CHINA).format(Date(m.ts)) + if (m.path.isEmpty()) " · 未接收" else "", color = p.fg2, style = MaterialTheme.typography.bodySmall) }
                }
            }
        }
    }
}

fun isStickerName(n: String): Boolean { val b = n.substringAfterLast('/').lowercase(); return b.startsWith("lsm-sticker-") || (b.endsWith(".png") && b.removeSuffix(".png") in setOf("thumb", "heart", "fire", "star", "party", "check")) }
fun isImageName(n: String) = n.substringAfterLast('.', "").lowercase() in setOf("png", "jpg", "jpeg", "gif", "bmp", "webp")
