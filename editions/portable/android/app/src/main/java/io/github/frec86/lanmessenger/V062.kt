package io.github.frec86.lanmessenger

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/* v0.6.2: polls, disappearing timer, archive, chat folders */
val TTL_SECS = listOf(0, 30, 300, 3600, 86400, 604800)
fun ttlLabel(s: Int) = when { s <= 0 -> "关闭"; s % 86400 == 0 -> "${s / 86400} 天"; s % 3600 == 0 -> "${s / 3600} 小时"; s % 60 == 0 -> "${s / 60} 分钟"; else -> "$s 秒" }
const val FILTER_ARCHIVED = "\u0001"
fun convVisible(c: Conv, folder: String) = when (folder) { "" -> !c.archived; FILTER_ARCHIVED -> c.archived; else -> !c.archived && folder in c.folders }

/** tap an option to vote (single: select / clear; multi: toggle) */
@Composable
fun PollCard(m: Msg) {
    val p = LocalPalette.current; val poll = m.poll ?: return
    Column(Modifier.padding(top = 6.dp).widthIn(min = 200.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        poll.opts.forEachIndexed { i, o ->
            val frac = if (poll.voters == 0) 0f else o.count.toFloat() / poll.voters
            Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp)).background(p.field).clickable(enabled = !poll.closed) {
                val e = Native.votePoll(m.id, i); if (e.isNotEmpty()) Repo.toast.value = e; Repo.refresh() }
                .semantics { contentDescription = "选项 ${o.text}，${o.count} 票" + if (o.mine) "，已选" else "" }.padding(horizontal = 10.dp, vertical = 6.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text((if (o.mine) "✓ " else "") + o.text, Modifier.weight(1f), fontSize = 15.sp, color = p.fg)
                    Text("${o.count}", fontSize = 13.sp, color = p.fg2)
                }
                LinearProgressIndicator(progress = { frac }, Modifier.fillMaxWidth().padding(top = 4.dp).height(4.dp), color = p.accent, trackColor = p.window)
            }
        }
        Text((if (poll.multi) "多选" else "单选") + " · ${poll.voters} 人参与" + if (poll.closed) " · 已结束" else "", fontSize = 12.sp, color = p.fg2)
        if (m.out && !poll.closed) TextButton({ val e = Native.closePoll(m.id); if (e.isNotEmpty()) Repo.toast.value = e; Repo.refresh() }, contentPadding = PaddingValues(0.dp)) { Text("结束投票") }
    }
}

fun expiresText(m: Msg) = if (m.expires > 0) "⏱ " + java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.getDefault()).format(java.util.Date(m.expires)) + " 删除" else ""

@Composable
fun NewPollDialog(conv: String, dismiss: () -> Unit) {
    var q by remember { mutableStateOf("") }; var opts by remember { mutableStateOf("") }; var multi by remember { mutableStateOf(false) }; var err by remember { mutableStateOf("") }
    LsmAlertDialog(onDismissRequest = dismiss, title = { Text("发起投票") },
        text = { Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedTextField(q, { q = it.take(300) }, label = { Text("问题") }, singleLine = true)
            OutlinedTextField(opts, { opts = it }, label = { Text("选项（每行一个，2–10 个）") }, minLines = 3)
            Row(verticalAlignment = Alignment.CenterVertically) { Checkbox(multi, { multi = it }); Text("允许多选") }
            if (err.isNotEmpty()) Text(err, color = MaterialTheme.colorScheme.error)
        } },
        confirmButton = { TextButton({ val e = Native.sendPoll(conv, q, opts, multi); if (e.isEmpty()) { Repo.refresh(); dismiss() } else err = e }) { Text("发起") } },
        dismissButton = { TextButton(dismiss) { Text("取消") } })
}

@Composable
fun TtlDialog(conv: String, cur: Int, dismiss: () -> Unit) {
    var sel by remember { mutableStateOf(cur) }
    LsmAlertDialog(onDismissRequest = dismiss, title = { Text("定时删除") },
        text = { Column { Text("新消息在多久后自动删除（双方同步；只影响之后的消息）。", fontSize = 13.sp)
            TTL_SECS.forEach { s -> Row(Modifier.fillMaxWidth().clickable { sel = s }, verticalAlignment = Alignment.CenterVertically) { RadioButton(sel == s, { sel = s }); Text(ttlLabel(s)) } } } },
        confirmButton = { TextButton({ if (sel != cur) { val e = Native.setTtl(conv, sel); if (e.isNotEmpty()) Repo.toast.value = e; Repo.refresh() }; dismiss() }) { Text("确定") } },
        dismissButton = { TextButton(dismiss) { Text("取消") } })
}

@Composable
fun FoldersDialog(conv: String, cur: List<String>, all: List<String>, dismiss: () -> Unit) {
    var sel by remember { mutableStateOf(cur.toSet()) }; var add by remember { mutableStateOf("") }; var names by remember { mutableStateOf((all + cur).distinct().sorted()) }
    LsmAlertDialog(onDismissRequest = dismiss, title = { Text("会话分组") },
        text = { Column {
            Text("只保存在本机，用于筛选会话列表。", fontSize = 13.sp)
            names.forEach { f -> Row(Modifier.fillMaxWidth().clickable { sel = if (f in sel) sel - f else sel + f }, verticalAlignment = Alignment.CenterVertically) { Checkbox(f in sel, { sel = if (it) sel + f else sel - f }); Text(f) } }
            Row(verticalAlignment = Alignment.CenterVertically) {
                OutlinedTextField(add, { add = it.replace(",", "").take(32) }, Modifier.weight(1f), label = { Text("新分组") }, singleLine = true)
                TextButton({ val n = add.trim(); if (n.isNotEmpty()) { names = (names + n).distinct().sorted(); sel = sel + n; add = "" } }) { Text("添加") }
            }
        } },
        confirmButton = { TextButton({ Native.setFolders(conv, sel.joinToString(",")); Repo.refresh(); dismiss() }) { Text("保存") } },
        dismissButton = { TextButton(dismiss) { Text("取消") } })
}
