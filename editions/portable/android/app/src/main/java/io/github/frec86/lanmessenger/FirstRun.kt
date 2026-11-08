package io.github.frec86.lanmessenger

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp

/** First-run: pick 账号模式 (server-bound, E2E history) or 设备模式 (LAN only). LSMF_account_mode */
@Composable
fun FirstRunScreen(onDone: () -> Unit) {
    val p = LocalPalette.current
    var mode by remember { mutableStateOf("device") }
    Column(
        Modifier.fillMaxSize().background(p.window).statusBarsPadding().padding(24.dp)
            .verticalScroll(rememberScrollState()).testTag("LSMF_account_mode")
            .semantics { contentDescription = "LSMF_account_mode" }
    ) {
        Text("欢迎使用局域网安全通讯", style = MaterialTheme.typography.headlineSmall, color = p.fg)
        Spacer(Modifier.height(12.dp))
        Text(
            "请选择身份方式（之后可在设置里更改）。账号模式把端到端加密的身份与消息备份放在中转上，只有本机密钥能解密，服务器看不到明文或密钥；换机可用恢复密钥或配对二维码恢复。设备模式仅本机，局域网直连，不把身份放到服务器。不使用服务器密码。",
            style = MaterialTheme.typography.bodyMedium, color = p.fg2
        )
        Spacer(Modifier.height(20.dp))
        ModeRow("设备模式 — 仅本机，局域网直连", mode == "device") { mode = "device" }
        Spacer(Modifier.height(8.dp))
        ModeRow("账号模式 — 身份加密备份在中转上，换机可恢复", mode == "account") { mode = "account" }
        Spacer(Modifier.height(24.dp))
        Button({
            val e = Native.completeFirstRun("", mode, "")
            if (e.isNotEmpty()) Repo.toast.value = e
            if (mode == "account") {
                val rk = Native.revealRecoveryKey()
                if (rk.isNotEmpty()) Repo.toast.value = "请抄下恢复密钥：$rk"
            }
            Repo.refresh(); onDone()
        }, Modifier.fillMaxWidth()) { Text("开始使用") }
    }
}

@Composable
private fun ModeRow(label: String, selected: Boolean, onPick: () -> Unit) {
    val p = LocalPalette.current
    Row(
        Modifier.fillMaxWidth().selectable(selected, onClick = onPick).padding(vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        RadioButton(selected, onClick = onPick)
        Spacer(Modifier.width(8.dp))
        Text(label, color = p.fg, style = MaterialTheme.typography.bodyLarge)
    }
}
