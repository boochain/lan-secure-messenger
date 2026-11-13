package io.github.frec86.lanmessenger

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp

/** First-run: pick 账号模式 or 设备模式 as M3 choice cards. LSMF_account_mode */
@Composable
fun FirstRunScreen(onDone: (account: Boolean) -> Unit) {
    val p = LocalPalette.current
    var mode by remember { mutableStateOf("device") }
    Column(
        Modifier.fillMaxSize().background(p.window).statusBarsPadding().navigationBarsPadding().padding(horizontal = 16.dp, vertical = 24.dp)
            .verticalScroll(rememberScrollState()).testTag("LSMF_account_mode")
            .semantics { contentDescription = "LSMF_account_mode" }
    ) {
        IconWell(R.drawable.ms_lock, p.pCont, p.onPCont, 56.dp)
        Spacer(Modifier.height(16.dp))
        Text("选择身份", style = MaterialTheme.typography.headlineSmall, color = p.fg)
        Spacer(Modifier.height(16.dp))
        InfoCard(R.drawable.ms_info, "两种方式", "之后可在设置里更改。不使用服务器密码。账号模式把加密身份放在中转上，换机用恢复密钥找回；设备模式只留在本机。")
        Spacer(Modifier.height(12.dp))
        ChoiceCard(mode == "device", R.drawable.ms_network, "设备模式", "仅本机，局域网直连") { mode = "device" }
        Spacer(Modifier.height(8.dp))
        ChoiceCard(mode == "account", R.drawable.ms_lock, "账号模式", "加密备份在中转上，换机可恢复") { mode = "account" }
        Spacer(Modifier.height(24.dp))
        Button({
            val e = Native.completeFirstRun("", mode, "")
            if (e.isNotEmpty()) Repo.toast.value = e
            Repo.refresh(); onDone(mode == "account")
        }, Modifier.fillMaxWidth().height(48.dp)) { Text(if (mode == "account") "下一步" else "开始使用") }
    }
}
