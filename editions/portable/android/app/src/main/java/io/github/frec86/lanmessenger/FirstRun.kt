package io.github.frec86.lanmessenger

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp

/** First-run: pick 账号模式 or 设备模式 once. LSMF_account_mode */
@Composable
fun FirstRunScreen(lockAccount: Boolean = false, onDone: () -> Unit) {
    val p = LocalPalette.current
    var step by remember { mutableIntStateOf(if (lockAccount) 1 else 0) }
    var mode by remember { mutableStateOf("device") }
    var host by remember { mutableStateOf("") }
    var port by remember { mutableStateOf("9443") }
    var netPass by remember { mutableStateOf("") }
    var user by remember { mutableStateOf("") }
    var pass by remember { mutableStateOf("") }
    var register by remember { mutableStateOf(true) }
    var busy by remember { mutableStateOf(false) }
    Column(
        Modifier.fillMaxSize().background(p.window).statusBarsPadding().navigationBarsPadding().padding(horizontal = 16.dp, vertical = 24.dp)
            .verticalScroll(rememberScrollState()).testTag("LSMF_account_mode")
            .semantics { contentDescription = "LSMF_account_mode" }
    ) {
        IconWell(R.drawable.ms_lock, p.pCont, p.onPCont, 56.dp)
        Spacer(Modifier.height(16.dp))
        Text(when (step) { 1 -> "中转服务器"; 2 -> "账号密码"; else -> "选择身份" }, style = MaterialTheme.typography.headlineSmall, color = p.fg)
        Spacer(Modifier.height(16.dp))
        if (step == 0) {
            InfoCard(R.drawable.ms_info, "两种方式", "只问这一次，之后在设置里改。设备模式只留在本机。账号模式要填中转服务器，再用账号密码登录。")
            Spacer(Modifier.height(12.dp))
            ChoiceCard(mode == "device", R.drawable.ms_network, "设备模式", "仅本机，局域网直连") { mode = "device" }
            Spacer(Modifier.height(8.dp))
            ChoiceCard(mode == "account", R.drawable.ms_lock, "账号模式", "中转服务器上的账号，换机用同一账号密码") { mode = "account" }
        } else if (step == 1) {
            InfoCard(R.drawable.ms_info, "先连上你的中转", "地址是你自己部署的服务器。没有网络口令就留空。")
            Spacer(Modifier.height(12.dp))
            OutlinedTextField(host, { host = it }, Modifier.fillMaxWidth(), label = { Text("服务器地址") }, singleLine = true)
            Spacer(Modifier.height(8.dp))
            OutlinedTextField(port, { port = it.filter { ch -> ch.isDigit() }.take(5) }, Modifier.fillMaxWidth(), label = { Text("端口") }, singleLine = true, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number))
            Spacer(Modifier.height(8.dp))
            OutlinedTextField(netPass, { netPass = it }, Modifier.fillMaxWidth(), label = { Text("网络口令（没有就留空）") }, singleLine = true, visualTransformation = PasswordVisualTransformation())
        } else {
            InfoCard(R.drawable.ms_info, "账号密码", "服务器只保存口令哈希。聊天记录用这个密码加密后上传，换机用同一账号密码取回。")
            Spacer(Modifier.height(12.dp))
            OutlinedTextField(user, { user = it.trim().lowercase() }, Modifier.fillMaxWidth(), label = { Text("账号") }, singleLine = true)
            Spacer(Modifier.height(8.dp))
            OutlinedTextField(pass, { pass = it }, Modifier.fillMaxWidth(), label = { Text("密码（至少 8 位）") }, singleLine = true, visualTransformation = PasswordVisualTransformation())
            Spacer(Modifier.height(8.dp))
            ChoiceCard(register, R.drawable.ms_lock, "注册新账号", "这台中转上还没有这个名字") { register = true }
            Spacer(Modifier.height(8.dp))
            ChoiceCard(!register, R.drawable.ms_network, "登录已有账号", "换机或重装后取回原来的身份") { register = false }
        }
        Spacer(Modifier.height(24.dp))
        if (step > 0) {
            TextButton({
                if (busy) return@TextButton
                if (step == 1 && lockAccount) onDone() else step -= 1
            }, Modifier.fillMaxWidth()) { Text(if (step == 1 && lockAccount) "取消" else "上一步") }
            Spacer(Modifier.height(4.dp))
        }
        Button({
            if (busy) return@Button
            when (step) {
                0 -> if (mode == "device") {
                    val e = Native.completeFirstRun("", "device", "")
                    if (e.isNotEmpty()) Repo.toast.value = e else { Repo.refresh(); onDone() }
                } else step = 1
                1 -> if (host.isBlank()) Repo.toast.value = "请填写中转服务器地址" else step = 2
                else -> {
                    busy = true
                    val h = host.trim(); val pt = port.toIntOrNull() ?: 9443; val np = netPass; val u = user.trim(); val pw = pass; val reg = register
                    Thread {
                        val e = Native.accountSignIn(h, pt, np, u, pw, reg)
                        android.os.Handler(android.os.Looper.getMainLooper()).post {
                            busy = false
                            if (e.isNotEmpty()) Repo.toast.value = e else { Repo.refresh(); onDone() }
                        }
                    }.start()
                }
            }
        }, Modifier.fillMaxWidth().height(48.dp), enabled = !busy) {
            Text(when {
                busy -> "正在连接…"
                step == 0 && mode == "device" -> "开始使用"
                step == 2 -> if (register) "注册并开始" else "登录并开始"
                else -> "下一步"
            })
        }
    }
}