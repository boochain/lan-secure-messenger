package io.github.frec86.lanmessenger

import android.Manifest
import android.content.pm.PackageManager
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import com.google.zxing.RGBLuminanceSource
import java.io.File
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
    BackHandler { onDismiss() }
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
        val testCam = remember { File("/data/local/tmp/lsm-ui-test").exists() }
        var fromBmp by remember { mutableStateOf<Bitmap?>(null) }
        var fromText by remember { mutableStateOf("") }
        if (testCam) {
            val uri = remember { Native.myUri() }
            val qr = remember(uri) { Native.qr(uri) }
            val n = qr.substringBefore(':').toIntOrNull() ?: 0; val bits = qr.substringAfter(':')
            Column(Modifier.fillMaxWidth().weight(1f).padding(16.dp), horizontalAlignment = androidx.compose.ui.Alignment.CenterHorizontally) {
                Text("测试预览（模拟相机，无真实摄像头）", color = p.fg2, style = MaterialTheme.typography.bodyMedium)
                Spacer(Modifier.height(8.dp))
                if (n > 0) Canvas(Modifier.size(240.dp).background(Color.White).semantics { contentDescription = "模拟相机预览" }) {
                    val cell = size.minDimension / (n + 8)
                    for (y in 0 until n) for (x in 0 until n) if (bits.getOrNull(y * n + x) == '1')
                        drawRect(Color.Black, Offset((x + 4) * cell, (y + 4) * cell), Size(cell + 0.5f, cell + 0.5f))
                }
            }
        } else if (hasCam) {
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
            Text("未获得相机权限时，请在下方粘贴 lsm:// 链接，或从图片识别（添加联系人、连接服务器或配对新设备）。",
                color = p.fg2, modifier = Modifier.padding(16.dp).weight(1f), style = MaterialTheme.typography.bodyMedium)
        }
        fromBmp?.let { bmp ->
            Text("从图片识别", color = p.fg, modifier = Modifier.padding(horizontal = 16.dp), style = MaterialTheme.typography.titleMedium)
            Image(bmp.asImageBitmap(), contentDescription = "识别的二维码图片", modifier = Modifier.size(200.dp).padding(8.dp), contentScale = ContentScale.Fit)
            if (fromText.isNotEmpty()) Text(fromText, color = p.fg, style = MaterialTheme.typography.bodySmall, modifier = Modifier.padding(horizontal = 16.dp))
        }
        OutlinedTextField(
            paste, { paste = it },
            label = { Text("或粘贴 lsm:// 链接") },
            modifier = Modifier.fillMaxWidth().padding(16.dp).semantics { contentDescription = "LSMF_qr_scan" }
        )
        Row(Modifier.fillMaxWidth().padding(start = 16.dp, end = 16.dp, bottom = 16.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button({
                val path = "/data/local/tmp/lsm-qr-sample.png"
                val bmp = BitmapFactory.decodeFile(path)
                val text = bmp?.let { decodeQrBitmap(it) } ?: ""
                fromBmp = bmp; fromText = text
                if (text.startsWith("lsm://")) paste = text
            }, Modifier.weight(1f)) { Text("从图片识别") }
            Button({ emit(paste.trim()) }, Modifier.weight(1f)) { Text("应用链接") }
        }
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

private fun decodeQrBitmap(bmp: Bitmap): String? {
    return try {
        val pixels = IntArray(bmp.width * bmp.height)
        bmp.getPixels(pixels, 0, bmp.width, 0, 0, bmp.width, bmp.height)
        val src = RGBLuminanceSource(bmp.width, bmp.height, pixels)
        val hints = mapOf(DecodeHintType.POSSIBLE_FORMATS to listOf(BarcodeFormat.QR_CODE), DecodeHintType.TRY_HARDER to true)
        val reader = MultiFormatReader().apply { setHints(hints) }
        reader.decodeWithState(BinaryBitmap(HybridBinarizer(src))).text
    } catch (_: Exception) { null }
}
