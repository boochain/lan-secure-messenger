package io.github.frec86.lanmessenger

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.toggleable
import androidx.compose.ui.semantics.Role
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/* ---------------------------------------------------------------- 设置 › 中转服务器（可选）
 * Off by default. LAN direct is always preferred; the server only forwards end-to-end encrypted data.
 * The passphrase is write-only: the core only reports whether one is set (已设置). Back returns to 设置. */
@Composable
fun RelayScreen(st: AppState, back: () -> Unit) {
    val p = LocalPalette.current
    val r = st.relay
    val focus = LocalFocusManager.current
    BackHandler(onBack = back)
    var on by remember { mutableStateOf(r.enabled) }
    var host by remember { mutableStateOf(r.host) }
    var port by remember { mutableStateOf(r.port.toString()) }
    var pass by remember { mutableStateOf("") }
    fun save(enabled: Boolean = on) {
        val pt = port.trim().toIntOrNull()
        if (enabled && host.isBlank()) { Repo.toast.value = "请填写服务器地址"; return }
        if (pt == null || pt !in 1..65535) { Repo.toast.value = "端口无效"; return }
        val e = Native.relaySet(enabled, host.trim(), pt, pass.ifEmpty { null })
        if (e.isNotEmpty()) Repo.toast.value = e else { pass = ""; focus.clearFocus(); Repo.toast.value = if (enabled) "中转服务器设置已保存，正在连接…" else "已关闭中转服务器" }
        Repo.refresh()
    }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 16.dp, vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(p.chip).padding(16.dp), verticalAlignment = Alignment.Top) {
            Ic(R.drawable.ms_via_server, null, p.fg2, 20.dp); Spacer(Modifier.width(12.dp))
            Text("不在同一局域网时，可通过你自己部署的中转服务器联系对方。局域网直连始终优先；服务器只转发端到端加密的数据，无法看到消息内容。",
                style = MaterialTheme.typography.bodyMedium, color = p.fg2)
        }
        // the whole row toggles (Material list-item switch pattern): tapping the label works too
        Row(Modifier.fillMaxWidth().heightIn(min = 56.dp).toggleable(on, role = Role.Switch) { v -> on = v; if (!v || host.isNotBlank()) save(v) },
            verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("通过中转服务器连接", fontSize = 16.sp, lineHeight = 24.sp, color = p.fg)
                Text(if (on) "局域网直连不可用时经服务器中转" else "关闭：只使用局域网直连", style = MaterialTheme.typography.bodyMedium, color = p.fg2)
            }
            Switch(on, null, colors = SwitchDefaults.colors(checkedTrackColor = p.accent, checkedThumbColor = p.onAccent))
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedTextField(host, { host = it }, label = { Text("服务器地址") }, placeholder = { Text("relay.example.com") }, singleLine = true, modifier = Modifier.weight(1f),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri, imeAction = ImeAction.Next))
            OutlinedTextField(port, { port = it.filter(Char::isDigit).take(5) }, label = { Text("端口") }, singleLine = true, modifier = Modifier.width(104.dp),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number, imeAction = ImeAction.Next))
        }
        OutlinedTextField(pass, { pass = it }, label = { Text(if (r.hasPassphrase) "网络口令（已设置）" else "网络口令") }, singleLine = true, modifier = Modifier.fillMaxWidth(),
            placeholder = { Text(if (r.hasPassphrase) "已设置（留空则保持不变）" else "未设置") },
            supportingText = { Text("与服务器管理员约定的口令；只用于验证，不会发送到网络，也不会再显示") },
            visualTransformation = PasswordVisualTransformation(),
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password, imeAction = ImeAction.Done),
            keyboardActions = KeyboardActions(onDone = { save() }))
        if (r.hasPassphrase) Row(verticalAlignment = Alignment.CenterVertically) {
            Ic(R.drawable.ms_lock, null, p.fg2, 16.dp); Spacer(Modifier.width(6.dp))
            Text("口令已设置", style = MaterialTheme.typography.bodyMedium, color = p.fg2, modifier = Modifier.weight(1f))
            TextButton({ Native.relaySet(r.enabled, r.host, r.port, ""); Repo.toast.value = "已清除网络口令"; Repo.refresh() }) { Text("清除口令") }
        }
        // status indicator (docs/ipc.md): connecting / online / auth_failed / error
        val dot = when (r.state) { "online" -> p.online; "connecting" -> p.warning; "auth_failed", "error" -> p.error; else -> p.fg2 }
        Row(Modifier.fillMaxWidth().semantics { liveRegion = LiveRegionMode.Polite }, verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.size(10.dp).clip(CircleShape).background(dot)); Spacer(Modifier.width(10.dp))
            Column {
                Text(relayStateZh(r.state), fontSize = 16.sp, lineHeight = 24.sp, color = if (r.state == "auth_failed" || r.state == "error") p.error else p.fg)
                val det = if (r.state == "online") "${r.host}:${r.port} · 服务器上 ${r.peers} 人" else r.detail
                if (det.isNotEmpty()) Text(det, style = MaterialTheme.typography.bodySmall, color = p.fg2, maxLines = 2, overflow = TextOverflow.Ellipsis)
            }
        }
        Button({ save() }, Modifier.fillMaxWidth(), colors = ButtonDefaults.buttonColors(containerColor = p.accent, contentColor = p.onAccent)) { Text("保存并连接") }
        Spacer(Modifier.height(24.dp))
    }
}
