package io.github.frec86.lanmessenger

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import org.json.JSONArray

/* v0.6.1 P2 (Android): pinned chats, drafts, pinned/starred messages, @mentions, typing, forward, multi-select */
fun p2Tags(m: Msg) = buildString { if (m.forwarded) append("↪ 转发 "); if (m.pinned) append("📌 "); if (m.starred) append("⭐ "); if (m.mentionsMe) append("@我 ") }

data class Flagged(val id: String, val conv: String, val sender: String, val body: String, val ts: Long)
fun flaggedList(conv: String, which: Int): List<Flagged> = JSONArray(Native.flagged(conv, which)).let { a -> (0 until a.length()).map { a.getJSONObject(it) }.map { Flagged(it.getString("id"), it.getString("conv"), it.getString("sender"), it.getString("body"), it.getLong("ts")) } }
data class Mention(val fp: String, val name: String)
fun mentionCandidates(conv: String): List<Mention> = JSONArray(Native.mentionCandidates(conv)).let { a -> (0 until a.length()).map { a.getJSONObject(it) }.map { Mention(it.getString("fp"), it.getString("name")) } }

/** newest pinned message of the chat; tap opens the list */
@Composable
fun PinnedBanner(m: Msg, count: Int, click: () -> Unit) {
    val p = LocalPalette.current
    Row(Modifier.fillMaxWidth().background(p.bar).clickable(onClick = click).padding(horizontal = 16.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
        Text("📌", fontSize = 14.sp); Spacer(Modifier.width(8.dp))
        Text((if (count > 1) "置顶 ($count)：" else "置顶：") + (if (m.kind == 1) m.file else m.body).replace('\n', ' '), fontSize = 14.sp, color = p.fg, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
    }
}

/** pinned (1) / starred (2) messages of this chat; picking one scrolls to it */
@Composable
fun FlagListDialog(conv: String, which: Int, pick: (String) -> Unit, dismiss: () -> Unit) {
    val p = LocalPalette.current; val list = remember(conv, which) { flaggedList(conv, which) }
    LsmAlertDialog(onDismissRequest = dismiss, title = { Text(if (which == 1) "置顶消息" else "收藏的消息") },
        text = { if (list.isEmpty()) Text(if (which == 1) "本会话还没有置顶消息。" else "本会话还没有收藏的消息。", color = p.fg2) else
            LazyColumn(Modifier.heightIn(max = 400.dp)) { items(list) { f -> Column(Modifier.fillMaxWidth().clickable { dismiss(); pick(f.id) }.padding(vertical = 8.dp)) {
                Text(f.sender + " · " + listTime(f.ts), fontSize = 12.sp, color = p.fg2); Text(f.body.replace('\n', ' '), color = p.fg, maxLines = 2, overflow = TextOverflow.Ellipsis) } } } },
        confirmButton = { TextButton(dismiss) { Text("关闭") } })
}

/** forward targets: every chat that is not blocked (multi-select) */
@Composable
fun ForwardDialog(st: AppState, ids: List<String>, dismiss: () -> Unit) {
    val p = LocalPalette.current; val convs = remember(st) { st.convs() }; var sel by remember { mutableStateOf(setOf<String>()) }
    LsmAlertDialog(onDismissRequest = dismiss, title = { Text(if (ids.size > 1) "逐条转发 ${ids.size} 条消息到…" else "转发到…") },
        text = { LazyColumn(Modifier.heightIn(max = 420.dp)) { items(convs) { c -> Row(Modifier.fillMaxWidth().clickable { sel = if (c.id in sel) sel - c.id else sel + c.id }.padding(vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            Checkbox(c.id in sel, { sel = if (c.id in sel) sel - c.id else sel + c.id }); Text((if (c.group) "# " else "") + c.name, color = p.fg, maxLines = 1, overflow = TextOverflow.Ellipsis) } } } },
        confirmButton = { TextButton({ var bad = ""; for (cv in sel) for (id in ids) { val e = Native.forward(id, cv); if (e.isNotEmpty()) bad = e }
            Repo.toast.value = if (bad.isNotEmpty()) "部分转发失败：$bad" else "已转发"; Repo.refresh(); dismiss() }, enabled = sel.isNotEmpty()) { Text("转发") } },
        dismissButton = { TextButton(dismiss) { Text("取消") } })
}

/** multi-select action bar (replaces the header) */
@Composable
fun SelectBar(n: Int, forward: () -> Unit, star: () -> Unit, delete: () -> Unit, close: () -> Unit) {
    val p = LocalPalette.current
    Row(Modifier.fillMaxWidth().height(64.dp).background(p.bar).padding(horizontal = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        IconButton(close) { Ic(R.drawable.ms_close, "退出多选", p.fg) }
        Text("已选 $n 条", style = MaterialTheme.typography.titleMedium, color = p.fg, modifier = Modifier.weight(1f).padding(start = 8.dp))
        TextButton(forward, enabled = n > 0) { Text("转发") }; TextButton(star, enabled = n > 0) { Text("收藏") }; TextButton(delete, enabled = n > 0) { Text("删除", color = if (n > 0) p.error else p.fg2) }
    }
}
