package io.github.frec86.lanmessenger

import androidx.annotation.DrawableRes
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.RoundRect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.*
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.text.SimpleDateFormat
import java.util.*

/** Material Symbols Outlined (res/drawable/ms_*.xml, Apache-2.0). */
@Composable
fun Ic(@DrawableRes id: Int, desc: String? = null, tint: Color = LocalContentColor.current, size: Dp = 24.dp, modifier: Modifier = Modifier) =
    Icon(painterResource(id), desc, modifier.size(size), tint = tint)

/** Round avatar with the stable colour (spec §6.1), initials in white, optional online dot with a ring in the pane colour. */
@Composable
fun Avatar(name: String, key: String, size: Dp, online: Boolean = false, ring: Color = MaterialTheme.colorScheme.surface) {
    val ini = initials(name)
    Box(Modifier.size(size)) {
        Box(Modifier.fillMaxSize().clip(CircleShape).background(avatarColor(key)), contentAlignment = Alignment.Center) {
            Text(ini, color = Color.White, fontWeight = FontWeight.Medium, fontSize = (size.value * (if (ini.length > 1) .36f else .42f)).sp, lineHeight = (size.value * .5f).sp)
        }
        if (online) {
            val d = if (size >= 48.dp) 14.dp else 12.dp
            Box(Modifier.align(Alignment.BottomEnd).offset(1.dp, 1.dp).size(d).clip(CircleShape).background(ring).padding(2.dp).clip(CircleShape).background(LocalPalette.current.online))
        }
    }
}

/** Receipt ticks: sent = one check, delivered = double check, read = double check in the accent (read = UI hook). */
@Composable
fun Ticks(status: String, tint: Color, readTint: Color, size: Dp = 16.dp) {
    when (status) {
        "sent" -> Ic(R.drawable.ms_tick, "已发送", tint, size)
        "delivered" -> Ic(R.drawable.ms_tick_double, "已送达", tint, size)
        "read" -> Ic(R.drawable.ms_tick_double, "已读", readTint, size)
        "queued" -> Ic(R.drawable.ms_tick, "排队中", tint.copy(alpha = .5f), size)
        "failed" -> Ic(R.drawable.ms_info, "发送失败", LocalPalette.current.error, size)
    }
}

@Composable
fun CountBadge(n: Int) {
    val p = LocalPalette.current
    Box(Modifier.defaultMinSize(minWidth = 20.dp).height(20.dp).clip(CircleShape).background(p.badge).padding(horizontal = 6.dp), contentAlignment = Alignment.Center) {
        Text(if (n > 99) "99+" else "$n", color = p.badgeFg, fontSize = 11.sp, lineHeight = 16.sp, fontWeight = FontWeight.Bold)
    }
}

fun fileIcon(name: String) = when (name.substringAfterLast('.', "").lowercase()) {
    "pdf" -> R.drawable.ms_file_pdf
    "png", "jpg", "jpeg", "gif", "bmp", "webp" -> R.drawable.ms_image
    else -> R.drawable.ms_file
}
fun humanSize(b: Long): String = when {
    b >= 1L shl 30 -> "%.1f GB".format(b / 1073741824.0)
    b >= 1L shl 20 -> "%.1f MB".format(b / 1048576.0)
    b >= 1024 -> "%.0f KB".format(b / 1024.0)
    else -> "$b B"
}
fun ext(name: String) = name.substringAfterLast('.', "").uppercase().ifEmpty { "文件" }

/* ---------------------------------------------------------------- time + grouping (spec §6: same rules on every platform) */
private fun cal(ms: Long) = Calendar.getInstance().apply { timeInMillis = ms }
private fun dayIndex(ms: Long): Long { val c = cal(ms); return c.get(Calendar.YEAR) * 400L + c.get(Calendar.DAY_OF_YEAR) }
private fun daysAgo(ms: Long, now: Long = System.currentTimeMillis()): Int {
    val a = cal(ms).apply { set(Calendar.HOUR_OF_DAY, 12); set(Calendar.MINUTE, 0) }; val b = cal(now).apply { set(Calendar.HOUR_OF_DAY, 12); set(Calendar.MINUTE, 0) }
    return Math.round((b.timeInMillis - a.timeInMillis) / 86400000.0).toInt()
}
fun hm(ms: Long): String = SimpleDateFormat("HH:mm", Locale.CHINA).format(Date(ms))
private val WEEK = arrayOf("周日", "周一", "周二", "周三", "周四", "周五", "周六")
/** Chat-list time: today HH:mm, 昨天, 周X within a week, else MM/dd (yyyy/MM/dd in another year). */
fun listTime(ms: Long): String { val d = daysAgo(ms)
    return when { d <= 0 -> hm(ms); d == 1 -> "昨天"; d < 7 -> WEEK[cal(ms).get(Calendar.DAY_OF_WEEK) - 1]
        cal(ms).get(Calendar.YEAR) == cal(System.currentTimeMillis()).get(Calendar.YEAR) -> SimpleDateFormat("MM/dd", Locale.CHINA).format(Date(ms))
        else -> SimpleDateFormat("yyyy/MM/dd", Locale.CHINA).format(Date(ms)) } }
/** Date separator: 今天 / 昨天 / M月d日 / yyyy年M月d日. */
fun dateLabel(ms: Long): String { val d = daysAgo(ms)
    return when { d <= 0 -> "今天"; d == 1 -> "昨天"
        cal(ms).get(Calendar.YEAR) == cal(System.currentTimeMillis()).get(Calendar.YEAR) -> SimpleDateFormat("M月d日", Locale.CHINA).format(Date(ms))
        else -> SimpleDateFormat("yyyy年M月d日", Locale.CHINA).format(Date(ms)) } }

sealed class ChatRow { abstract val key: String
    data class Date(val label: String, override val key: String) : ChatRow()
    data class E2E(override val key: String) : ChatRow()
    data class System(val m: Msg, override val key: String) : ChatRow()
    data class Bubble(val m: Msg, val first: Boolean, val last: Boolean, val showSender: Boolean, override val key: String) : ChatRow()
}
const val GROUP_GAP_MS = 5 * 60 * 1000L
fun sameGroup(a: Msg, b: Msg) = a.kind != 2 && b.kind != 2 && a.out == b.out && a.sender == b.sender && dayIndex(a.ts) == dayIndex(b.ts) && b.ts - a.ts <= GROUP_GAP_MS
/** Messages -> list rows: a date separator per day, an E2E notice after the first one, and first/last flags for bubble grouping. */
fun buildRows(msgs: List<Msg>, isGroup: Boolean): List<ChatRow> {
    val out = ArrayList<ChatRow>(); var day = -1L
    for ((i, m) in msgs.withIndex()) {
        if (dayIndex(m.ts) != day) { day = dayIndex(m.ts); out += ChatRow.Date(dateLabel(m.ts), "d$day"); if (out.size == 1) out += ChatRow.E2E("e2e") }
        if (m.kind == 2) { out += ChatRow.System(m, m.id); continue }
        val prev = msgs.getOrNull(i - 1); val next = msgs.getOrNull(i + 1)
        val first = prev == null || !sameGroup(prev, m); val last = next == null || !sameGroup(m, next)
        out += ChatRow.Bubble(m, first, last, isGroup && !m.out && first, m.id)
    }
    return out
}

/* ---------------------------------------------------------------- bubble shape with a tail (Telegram/WhatsApp): the tail sits at the top outer corner of the first bubble of a group */
class BubbleShape(private val mine: Boolean, private val tail: Boolean, private val r: Dp = 16.dp, private val small: Dp = 2.dp, private val t: Dp = 8.dp) : Shape {
    override fun createOutline(size: Size, layoutDirection: LayoutDirection, density: Density): Outline = with(density) {
        val rr = r.toPx(); val sm = small.toPx(); val tw = t.toPx()
        val body = Rect(if (mine) 0f else tw, 0f, if (mine) size.width - tw else size.width, size.height)
        val p = Path().apply {
            addRoundRect(RoundRect(body, topLeft = CornerRadius(if (!mine && tail) sm else rr), topRight = CornerRadius(if (mine && tail) sm else rr),
                bottomRight = CornerRadius(rr), bottomLeft = CornerRadius(rr)))
        }
        if (!tail) return Outline.Generic(p)
        val tri = Path().apply {
            if (mine) { moveTo(body.right - 1f, 0f); lineTo(size.width, 0f); lineTo(body.right - 1f, tw); close() }
            else { moveTo(body.left + 1f, 0f); lineTo(0f, 0f); lineTo(body.left + 1f, tw); close() }
        }
        Outline.Generic(Path.combine(PathOperation.Union, p, tri))
    }
}

/** Subtle tiled doodle wallpaper (spec §6.4): 10 symbols in a 220 x 176 dp tile at 3.5-4 % opacity. */
fun DrawScope.wallpaper(painters: List<Painter>, color: Color, alpha: Float) {
    val pos = listOf(Triple(14, 12, 0), Triple(78, 20, 20), Triple(140, 8, -15), Triple(40, 70, 12), Triple(110, 66, -20), Triple(170, 64, 8), Triple(12, 128, -8), Triple(76, 122, 15), Triple(138, 132, -12), Triple(186, 120, 25))
    val tw = 220.dp.toPx(); val th = 176.dp.toPx(); val s = 22.dp.toPx(); val cf = ColorFilter.tint(color)
    var y = 0f
    while (y < size.height) { var x = 0f
        while (x < size.width) {
            for ((i, q) in pos.withIndex()) { val px = x + q.first.dp.toPx(); val py = y + q.second.dp.toPx()
                translate(px, py) { rotate(q.third.toFloat(), Offset(s / 2, s / 2)) { with(painters[i]) { draw(Size(s, s), alpha, cf) } } } }
            x += tw }
        y += th }
}
val WALL_ICONS = listOf(R.drawable.ms_chats, R.drawable.ms_image, R.drawable.ms_attach, R.drawable.ms_lock, R.drawable.ms_send, R.drawable.ms_emoji, R.drawable.ms_file, R.drawable.ms_groups, R.drawable.ms_trusted, R.drawable.ms_network)

/** Local LAN as "192.168.1.0/24" for the connection banner (null when offline). */
fun lanSubnet(): String? = try {
    java.net.NetworkInterface.getNetworkInterfaces()?.toList().orEmpty().filter { it.isUp && !it.isLoopback }.flatMap { it.interfaceAddresses }
        .firstOrNull { it.address is java.net.Inet4Address && it.address.isSiteLocalAddress }?.let { ia ->
            val b = ia.address.address; val pl = ia.networkPrefixLength.toInt().coerceIn(0, 32)
            val v = ((b[0].toInt() and 255) shl 24) or ((b[1].toInt() and 255) shl 16) or ((b[2].toInt() and 255) shl 8) or (b[3].toInt() and 255)
            val n = if (pl == 0) 0 else v and (-1 shl (32 - pl))
            "${(n ushr 24) and 255}.${(n ushr 16) and 255}.${(n ushr 8) and 255}.${n and 255}/$pl" }
} catch (e: Exception) { null }

fun Modifier.ifThen(c: Boolean, f: Modifier.() -> Modifier) = if (c) f() else this
@Suppress("unused") fun Modifier.ring(c: Color, w: Dp) = border(w, c, CircleShape)
