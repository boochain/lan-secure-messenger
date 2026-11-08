package io.github.frec86.lanmessenger

import android.Manifest
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import com.google.zxing.BarcodeFormat
import com.google.zxing.BinaryBitmap
import com.google.zxing.DecodeHintType
import com.google.zxing.MultiFormatReader
import com.google.zxing.PlanarYUVLuminanceSource
import com.google.zxing.common.HybridBinarizer
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/** CameraX + ZXing QR scan (no ML Kit). Paste fallback: LSMF_qr_scan LSMF_qr_server LSMF_qr_pair */
@Composable
fun QrScanScreen(onResult: (String) -> Unit, onDismiss: () -> Unit) {
    val ctx = LocalContext.current
    val owner = LocalLifecycleOwner.current
    val p = LocalPalette.current
    var hasCam by remember { mutableStateOf(ContextCompat.checkSelfPermission(ctx, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) }
    var paste by remember { mutableStateOf("") }
    val req = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { hasCam = it }
    LaunchedEffect(Unit) { if (!hasCam) req.launch(Manifest.permission.CAMERA) }
    val done = remember { AtomicBoolean(false) }
    fun emit(s: String) { if (s.startsWith("lsm://") && done.compareAndSet(false, true)) onResult(s) }
    Column(Modifier.fillMaxSize().background(p.window).statusBarsPadding().testTag("LSMF_qr_scan")) {
        Row(Modifier.fillMaxWidth().height(64.dp).padding(horizontal = 4.dp), verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
            IconButton(onDismiss) { Ic(R.drawable.ms_back, "返回", p.fg) }
            Text("扫描二维码", style = MaterialTheme.typography.titleLarge, color = p.fg)
        }
        if (hasCam) {
            AndroidView(factory = { c ->
                val view = PreviewView(c)
                val future = ProcessCameraProvider.getInstance(c)
                future.addListener({
                    try {
                        val provider = future.get()
                        val preview = Preview.Builder().build().also { it.setSurfaceProvider(view.surfaceProvider) }
                        val analysis = ImageAnalysis.Builder().setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST).build()
                        val exec = Executors.newSingleThreadExecutor()
                        analysis.setAnalyzer(exec) { image -> decodeQr(image, ::emit) }
                        provider.unbindAll()
                        provider.bindToLifecycle(owner, CameraSelector.DEFAULT_BACK_CAMERA, preview, analysis)
                    } catch (_: Exception) { }
                }, ContextCompat.getMainExecutor(c))
                view
            }, modifier = Modifier.fillMaxWidth().weight(1f))
        } else {
            Text("未获得相机权限时，请在下方粘贴 lsm:// 链接（添加联系人、连接服务器或配对新设备）。",
                color = p.fg2, modifier = Modifier.padding(16.dp).weight(1f), style = MaterialTheme.typography.bodyMedium)
        }
        OutlinedTextField(
            paste, { paste = it },
            label = { Text("或粘贴 lsm:// 链接") },
            modifier = Modifier.fillMaxWidth().padding(16.dp).semantics { contentDescription = "LSMF_qr_scan" }
        )
        Button({ emit(paste.trim()) }, Modifier.padding(start = 16.dp, end = 16.dp, bottom = 16.dp).fillMaxWidth()) { Text("应用链接") }
    }
}

private fun decodeQr(image: ImageProxy, onQr: (String) -> Unit) {
    try {
        val buf = image.planes[0].buffer
        val data = ByteArray(buf.remaining()); buf.get(data)
        val src = PlanarYUVLuminanceSource(data, image.width, image.height, 0, 0, image.width, image.height, false)
        val hints = mapOf(DecodeHintType.POSSIBLE_FORMATS to listOf(BarcodeFormat.QR_CODE), DecodeHintType.TRY_HARDER to true)
        val reader = MultiFormatReader().apply { setHints(hints) }
        val result = reader.decodeWithState(BinaryBitmap(HybridBinarizer(src)))
        onQr(result.text)
    } catch (_: Exception) {
    } finally { image.close() }
}
