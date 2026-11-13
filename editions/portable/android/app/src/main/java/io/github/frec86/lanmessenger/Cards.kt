package io.github.frec86.lanmessenger

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/* Material 3 containment, not a custom look.
 * Cards: filled = surface-container-highest, corner medium 12dp, padding 16dp
 *   https://m3.material.io/components/cards/specs
 * Lists: two-line row 72dp, 16dp padding, leading slot 40dp, icon 24dp
 *   https://m3.material.io/components/lists/specs
 * Icon buttons: 24dp glyph inside a 40dp round container (default shape is round)
 *   https://m3.material.io/components/icon-buttons/specs
 * Selected choice: primary-container + check icon (card selection, same page).
 * Palette roles already mapped in Theme.kt (chip = surfaceContainerHighest, pCont = primaryContainer).
 */

private val CardShape = RoundedCornerShape(12.dp)

@Composable
fun IconWell(icon: Int, container: androidx.compose.ui.graphics.Color, tint: androidx.compose.ui.graphics.Color, well: Dp = 40.dp) {
    Box(Modifier.size(well).clip(CircleShape).background(container), contentAlignment = Alignment.Center) {
        Ic(icon, null, tint, 24.dp)
    }
}

@Composable
fun SectionLabel(text: String) {
    val p = LocalPalette.current
    Text(text, style = MaterialTheme.typography.titleSmall, color = p.fg2,
        modifier = Modifier.padding(start = 16.dp, end = 16.dp, top = 20.dp, bottom = 8.dp))
}

/** Filled card (surface-container-highest) with a 1dp outline so it stays distinct from the page. */
@Composable
fun SurfaceCard(modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    val p = LocalPalette.current
    Column(modifier.fillMaxWidth().clip(CardShape).background(p.chip).border(1.dp, p.divider, CardShape).padding(16.dp), content = content)
}

/** Mutually exclusive option. Icon is the primary mark; title is one line; supporting text is secondary. */
@Composable
fun ChoiceCard(selected: Boolean, icon: Int, title: String, sub: String?, modifier: Modifier = Modifier, onClick: () -> Unit) {
    val p = LocalPalette.current
    val bg = if (selected) p.pCont else p.chip
    val fg = if (selected) p.onPCont else p.fg
    val subFg = if (selected) p.onPCont else p.fg2
    Row(
        modifier.fillMaxWidth().heightIn(min = 72.dp).clip(CardShape).background(bg)
            .border(if (selected) 2.dp else 1.dp, if (selected) p.accent else p.divider, CardShape)
            .clickable(onClick = onClick)
            .padding(horizontal = 16.dp, vertical = 12.dp)
            .semantics { contentDescription = if (selected) "$title，已选择" else title },
        verticalAlignment = Alignment.CenterVertically
    ) {
        IconWell(icon, if (selected) p.theirs else p.secCont, if (selected) p.onPCont else p.onSecCont)
        Spacer(Modifier.width(16.dp))
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.titleMedium, color = fg, maxLines = 1, overflow = TextOverflow.Ellipsis)
            if (!sub.isNullOrBlank()) Text(sub, style = MaterialTheme.typography.bodySmall, color = subFg, maxLines = 2, overflow = TextOverflow.Ellipsis)
        }
        if (selected) {
            Spacer(Modifier.width(12.dp))
            Ic(R.drawable.ms_tick, "已选择", p.onPCont, 24.dp)
        }
    }
}

/** Square action: 40dp round icon, short label underneath. Text is the caption, not the control. */
@Composable
fun ActionCard(icon: Int, title: String, modifier: Modifier = Modifier, onClick: () -> Unit) {
    val p = LocalPalette.current
    Column(
        modifier.heightIn(min = 104.dp).clip(CardShape).background(p.chip).border(1.dp, p.divider, CardShape)
            .clickable(onClick = onClick).padding(12.dp).semantics { contentDescription = title },
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        IconWell(icon, p.secCont, p.onSecCont)
        Spacer(Modifier.height(8.dp))
        Text(title, style = MaterialTheme.typography.labelLarge, color = p.fg, textAlign = TextAlign.Center, maxLines = 2, overflow = TextOverflow.Ellipsis)
    }
}

@Composable
fun InfoCard(icon: Int, title: String, body: String, modifier: Modifier = Modifier) {
    val p = LocalPalette.current
    Row(
        modifier.fillMaxWidth().heightIn(min = 72.dp).clip(CardShape).background(p.chip).border(1.dp, p.divider, CardShape).padding(16.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        IconWell(icon, p.secCont, p.onSecCont)
        Spacer(Modifier.width(16.dp))
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.titleMedium, color = p.fg, maxLines = 1, overflow = TextOverflow.Ellipsis)
            if (body.isNotEmpty()) Text(body, style = MaterialTheme.typography.bodySmall, color = p.fg2, maxLines = 3, overflow = TextOverflow.Ellipsis)
        }
    }
}

private data class DiagCard(val icon: Int, val title: String, val body: String)

private fun diagCards(raw: String): List<DiagCard> {
    if (raw.isBlank()) return listOf(DiagCard(R.drawable.ms_info, "诊断", "暂无诊断信息"))
    val out = ArrayList<DiagCard>()
    for (line in raw.lineSequence()) {
        if (line.isBlank()) continue
        val tab = line.indexOf('\t')
        val key = if (tab < 0) line else line.substring(0, tab)
        val rest = if (tab < 0) "" else line.substring(tab + 1)
        val parts = rest.split('\t')
        fun join() = parts.filter { it.isNotEmpty() }.joinToString(" · ")
        val card = when (key) {
            "version" -> DiagCard(R.drawable.ms_info, "版本", join())
            "fingerprint" -> DiagCard(R.drawable.ms_lock, "指纹", join())
            "name" -> DiagCard(R.drawable.ms_edit, "名称", join())
            "data_dir" -> DiagCard(R.drawable.ms_open_folder, "数据目录", join())
            "download_dir" -> DiagCard(R.drawable.ms_open_folder, "接收目录", join())
            "tcp_listen" -> DiagCard(R.drawable.ms_network, "TCP 监听", join())
            "udp_discovery" -> DiagCard(R.drawable.ms_network, "局域网发现", join())
            "announce_targets" -> DiagCard(R.drawable.ms_network, "发现目标", join().ifEmpty { "无" })
            "interface" -> DiagCard(R.drawable.ms_network, "网卡", join())
            "relay" -> DiagCard(R.drawable.ms_via_server, "中转", join())
            "peer" -> DiagCard(R.drawable.ms_add_contact, parts.getOrNull(1)?.ifBlank { "联系人" } ?: "联系人", parts.drop(2).filter { it.isNotEmpty() }.joinToString(" · "))
            "outbox" -> DiagCard(R.drawable.ms_send, "发件箱", join())
            "transfers_active" -> DiagCard(R.drawable.ms_transfers, "进行中的传输", join())
            else -> DiagCard(R.drawable.ms_info, key, join())
        }
        out.add(card)
    }
    return out
}

@Composable
fun DiagCards(raw: String, modifier: Modifier = Modifier) {
    val rows = diagCards(raw)
    LazyColumn(modifier.fillMaxSize(), contentPadding = PaddingValues(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        items(rows) { row -> InfoCard(row.icon, row.title, row.body) }
    }
}
