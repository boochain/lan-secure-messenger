package io.github.frec86.lanmessenger

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** Dedicated recovery-key page (Signal / WhatsApp style): the key stays on screen,
 *  can be copied or exported, and first-run cannot be dismissed until the user confirms. */
@Composable
fun RecoveryCodeScreen(forceConfirm: Boolean, onDone: () -> Unit, onBack: () -> Unit) {
    val p = LocalPalette.current
    val ctx = LocalContext.current
    val rk = remember { Native.revealRecoveryKey() }
    var written by remember { mutableStateOf(false) }
    val export = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("text/plain")) { uri ->
        if (uri != null && rk.isNotEmpty()) {
            try {
                ctx.contentResolver.openOutputStream(uri)?.use { it.write(("局域网安全通讯恢复密钥\n$rk\n\n请妥善保管。丢失后无法找回账号和加密历史。服务器看不到明文。\n").toByteArray()) }
                Repo.toast.value = "已保存恢复密钥"
            } catch (e: Exception) {
                Repo.toast.value = "保存失败：${e.message}"
            }
        }
    }
    BackHandler {
        if (!forceConfirm || written) onBack()
        else Repo.toast.value = "请先抄下恢复密钥并确认“我已抄下并妥善保管”"
    }
    Column(
        Modifier.fillMaxSize().background(p.window).statusBarsPadding().navigationBarsPadding()
            .testTag("LSMF_recovery_key")
            .semantics { contentDescription = "LSMF_recovery_key" }
    ) {
        Row(Modifier.fillMaxWidth().height(64.dp).padding(horizontal = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            if (!forceConfirm) IconButton(onBack) { Ic(R.drawable.ms_back, "返回", p.fg) }
            else Spacer(Modifier.width(48.dp))
            Text("恢复密钥", style = MaterialTheme.typography.titleLarge, color = p.fg, modifier = Modifier.weight(1f))
        }
        Column(Modifier.weight(1f).verticalScroll(rememberScrollState()).padding(horizontal = 16.dp, vertical = 8.dp)) {
            InfoCard(R.drawable.ms_lock, "唯一凭证", "换机恢复账号和加密历史只用这一串。抄下来或导出。丢失无法找回，服务器也看不到明文。")
            Spacer(Modifier.height(12.dp))
            if (rk.isEmpty()) {
                InfoCard(R.drawable.ms_info, "还没有密钥", "请先在设置里开启账号模式。")
            } else {
                Text(
                    rk,
                    fontFamily = FontFamily.Monospace,
                    fontSize = 20.sp,
                    lineHeight = 32.sp,
                    textAlign = TextAlign.Center,
                    color = p.fg,
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(12.dp))
                        .background(p.chip)
                        .border(1.dp, p.divider, RoundedCornerShape(12.dp))
                        .padding(horizontal = 16.dp, vertical = 20.dp)
                        .semantics { contentDescription = "恢复密钥" }
                )
            }
            Spacer(Modifier.height(12.dp))
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                ActionCard(R.drawable.ms_copy, "复制", Modifier.weight(1f)) {
                    if (rk.isNotEmpty()) {
                        val cm = ctx.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                        cm.setPrimaryClip(ClipData.newPlainText("恢复密钥", rk))
                        Repo.toast.value = "已复制恢复密钥"
                    }
                }
                ActionCard(R.drawable.ms_download, "导出", Modifier.weight(1f)) {
                    if (rk.isNotEmpty()) export.launch("lan-messenger-recovery-key.txt")
                }
            }
            Spacer(Modifier.height(12.dp))
            ChoiceCard(written, R.drawable.ms_tick, "我已抄下", "确认后才能离开首次设置", onClick = { if (rk.isNotEmpty()) written = !written })
            Spacer(Modifier.height(16.dp))
            Button(
                onDone,
                Modifier.fillMaxWidth().height(48.dp),
                enabled = !forceConfirm || written
            ) { Text(if (forceConfirm) "开始使用" else "完成") }
        }
    }
}
