package io.github.frec86.lanmessenger

import android.content.Context
import android.media.MediaPlayer
import android.media.MediaRecorder
import android.os.Build
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.io.File

/** v0.6.1 P1: reply/quote, reactions, edit/recall, voice messages (AAC .m4a named lsm-voice-<N>s-…). */
val REACTIONS = listOf("👍", "❤️", "😂", "😮", "😢", "🎉")

fun displayBody(m: Msg) = when { m.recalled -> if (m.out) "你撤回了一条消息" else "对方撤回了一条消息"; m.edited -> m.body + "（已编辑）"; else -> m.body }

object Voice {
    private var rec: MediaRecorder? = null; private var tmp: File? = null; private var t0 = 0L
    val recording = mutableStateOf(false)
    private var player: MediaPlayer? = null
    val playing = mutableStateOf("")   // path being played
    fun start(ctx: Context): String {
        if (rec != null) return ""
        val f = File(ctx.cacheDir, "voice").apply { mkdirs() }.let { File(it, "rec-${System.currentTimeMillis()}.m4a") }
        return try {
            val r = if (Build.VERSION.SDK_INT >= 31) MediaRecorder(ctx) else @Suppress("DEPRECATION") MediaRecorder()
            r.setAudioSource(MediaRecorder.AudioSource.MIC); r.setOutputFormat(MediaRecorder.OutputFormat.MPEG_4); r.setAudioEncoder(MediaRecorder.AudioEncoder.AAC)
            r.setAudioSamplingRate(16000); r.setAudioEncodingBitRate(32000); r.setMaxDuration(120_000); r.setOutputFile(f.absolutePath); r.prepare(); r.start()
            rec = r; tmp = f; t0 = System.currentTimeMillis(); recording.value = true; ""
        } catch (e: Exception) { rec = null; f.delete(); "无法开始录音：" + (e.message ?: "") }
    }
    /** stops; returns the finished file (null if too short / failed) */
    fun stop(): File? {
        val r = rec ?: return null; val f = tmp; rec = null; tmp = null; recording.value = false
        val secs = ((System.currentTimeMillis() - t0 + 500) / 1000).toInt()
        try { r.stop() } catch (_: Exception) { r.release(); f?.delete(); return null }
        r.release()
        if (f == null || secs < 1) { f?.delete(); return null }
        val dst = File(f.parentFile, Native.voiceName(secs, "m4a")); return if (f.renameTo(dst)) dst else f
    }
    fun toggle(path: String) {
        if (playing.value == path) { player?.release(); player = null; playing.value = ""; return }
        player?.release(); player = null
        try { player = MediaPlayer().apply { setDataSource(path); setOnCompletionListener { it.release(); if (player === it) { player = null; playing.value = "" } }; prepare(); start() }; playing.value = path }
        catch (e: Exception) { Repo.toast.value = "无法播放：" + (e.message ?: "") }
    }
}

@Composable
fun QuoteStrip(m: Msg) {
    if (m.replyTo.isEmpty() || m.recalled) return
    val p = LocalPalette.current
    Row(Modifier.padding(bottom = 4.dp).height(IntrinsicSize.Min)) {
        Box(Modifier.width(3.dp).fillMaxHeight().background(p.accent))
        Text(m.quote.ifEmpty { "原消息" }, fontSize = 13.sp, lineHeight = 18.sp, maxLines = 3, overflow = TextOverflow.Ellipsis, color = LocalContentColor.current.copy(alpha = .75f), modifier = Modifier.padding(start = 6.dp))
    }
}

@Composable
fun ReactionChips(m: Msg) {
    if (m.reactions.isEmpty() || m.recalled) return
    val p = LocalPalette.current
    Row(Modifier.padding(horizontal = 12.dp, vertical = 2.dp).clip(RoundedCornerShape(12.dp)).background(p.field).padding(horizontal = 8.dp, vertical = 2.dp)) {
        Text(m.reactions, fontSize = 13.sp, color = p.fg)
    }
}

@Composable
fun VoiceBubble(m: Msg, path: String, haveFile: Boolean) {
    val p = LocalPalette.current; val playing by Voice.playing
    Row(Modifier.padding(horizontal = 10.dp, vertical = 8.dp).widthIn(min = 160.dp), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        Box(Modifier.size(36.dp).clip(CircleShape).background(p.accent).clickable(enabled = haveFile) { Voice.toggle(path) }, contentAlignment = Alignment.Center) {
            Ic(if (playing == path && haveFile) R.drawable.ms_pause else R.drawable.ms_play, if (playing == path) "暂停" else "播放", p.onAccent)
        }
        Text(if (haveFile) "语音 ${m.voice}″" else "语音 ${m.voice}″ · 接收中…", fontSize = 15.sp, modifier = Modifier.weight(1f, fill = false))
        Meta(m)
    }
}

@Composable
fun ReplyBar(m: Msg, editing: Boolean, onCancel: () -> Unit) {
    val p = LocalPalette.current
    Row(Modifier.fillMaxWidth().background(p.field).padding(start = 12.dp, end = 4.dp, top = 4.dp, bottom = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Ic(if (editing) R.drawable.ms_edit else R.drawable.ms_reply, null, p.accent)
        Column(Modifier.weight(1f).padding(start = 8.dp)) {
            Text(if (editing) "编辑消息" else "回复 " + (if (m.out) "我" else m.sender), fontSize = 13.sp, color = p.accent, maxLines = 1)
            Text(if (m.kind == 1) m.file else displayBody(m), fontSize = 13.sp, color = p.fg2, maxLines = 1, overflow = TextOverflow.Ellipsis, fontStyle = FontStyle.Normal)
        }
        IconButton(onCancel) { Ic(R.drawable.ms_close, "取消", p.fg2) }
    }
}
