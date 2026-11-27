package io.github.frec86.lanmessenger

import android.os.Handler
import android.os.Looper
import java.net.HttpURLConnection
import java.net.URL
import org.json.JSONArray

/** Update check against the GitHub releases API (HttpURLConnection on a worker thread).
 *  Same version rules as src/updcheck.h and the WinUI CompareVersions (SemVer, numeric pre-release ids). */
object Updates {
    const val RELEASES = "https://github.com/boochain/lan-secure-messenger/releases"
    private const val API = "https://api.github.com/repos/boochain/lan-secure-messenger/releases?per_page=10"

    /** result: (newer: 1 / 0 / -1 failed, message, url, raw json), delivered on the main thread.
     *  json is empty on failure. The up-to-date / newer strings stay the same as updcheck.h. */
    fun check(local: String, done: (Int, String, String, String) -> Unit) {
        Thread {
            val r = try {
                val c = URL(API).openConnection() as HttpURLConnection
                c.connectTimeout = 10000; c.readTimeout = 15000
                c.setRequestProperty("Accept", "application/vnd.github+json"); c.setRequestProperty("User-Agent", "LanMessenger-Android")
                try {
                    if (c.responseCode != 200) Quad(-1, "检查更新失败（HTTP ${c.responseCode}）。", RELEASES, "")
                    else { val body = c.inputStream.bufferedReader().use { it.readText() }; val t = result(body, local); Quad(t.first, t.second, t.third, body) }
                } finally { c.disconnect() }
            } catch (e: Exception) { Quad(-1, "无法连接 GitHub 检查更新：${e.message ?: e.javaClass.simpleName}", RELEASES, "") }
            Handler(Looper.getMainLooper()).post { done(r.a, r.b, r.c, r.d) }
        }.start()
    }

    private class Quad(val a: Int, val b: String, val c: String, val d: String)

    fun result(json: String, local: String): Triple<Int, String, String> {
        val a = JSONArray(json); var best: String? = null
        for (i in 0 until a.length()) {
            val o = a.optJSONObject(i) ?: continue
            if (o.optBoolean("draft")) continue
            val t = o.optString("tag_name")
            if (t.length < 2 || t[0] != 'v' || !t[1].isDigit()) continue
            if (best == null || compare(t, best) > 0) best = t
        }
        if (best == null) return Triple(0, "未找到发布版本。", RELEASES)
        val url = "$RELEASES/tag/$best"
        return if (compare(best, local) > 0) Triple(1, "发现新版本 $best（当前 $local）。", url)
        else Triple(0, "已是最新版本（当前 $local，最新发布 $best）。", url)
    }

    fun compare(a: String, b: String): Int {
        fun parse(v0: String): Pair<List<Int>, String> {
            val v = v0.trimStart('v', 'V').substringBefore('+'); val d = v.indexOf('-')
            val core = if (d >= 0) v.substring(0, d) else v
            return Pair((core.split('.').map { it.toIntOrNull() ?: 0 } + listOf(0, 0, 0)).take(3), if (d >= 0) v.substring(d + 1) else "")
        }
        val (an, ap) = parse(a); val (bn, bp) = parse(b)
        for (i in 0 until 3) if (an[i] != bn[i]) return an[i].compareTo(bn[i])
        if (ap == bp) return 0
        if (ap.isEmpty()) return 1
        if (bp.isEmpty()) return -1
        val aa = ap.split('.'); val bb = bp.split('.')
        for (i in 0 until minOf(aa.size, bb.size)) {
            val x = aa[i].toIntOrNull(); val y = bb[i].toIntOrNull()
            val c = if (x != null && y != null) x.compareTo(y) else if (x != null) -1 else if (y != null) 1 else aa[i].compareTo(bb[i])
            if (c != 0) return c
        }
        return aa.size.compareTo(bb.size)
    }

    /** Download URL and file name of the newest release's asset whose name contains needle. */
    fun asset(json: String, needle: String): Pair<String, String>? {
        if (json.isEmpty()) return null
        val a = try { JSONArray(json) } catch (_: Exception) { return null }
        var best: org.json.JSONObject? = null; var bestTag: String? = null
        for (i in 0 until a.length()) {
            val o = a.optJSONObject(i) ?: continue
            if (o.optBoolean("draft")) continue
            val t = o.optString("tag_name")
            if (t.length < 2 || t[0] != 'v' || !t[1].isDigit()) continue
            if (bestTag == null || compare(t, bestTag) > 0) { bestTag = t; best = o }
        }
        val assets = best?.optJSONArray("assets") ?: return null
        for (i in 0 until assets.length()) {
            val o = assets.optJSONObject(i) ?: continue
            val name = o.optString("name")
            if (name.contains(needle)) return o.optString("browser_download_url") to name
        }
        return null
    }

    /** Download the APK, check SHA256SUMS, then hand it to the system installer. Returns a status line. Does not delete app data. */
    fun beginInstall(ctx: android.content.Context, json: String, onMsg: (String) -> Unit) {
        val apk = asset(json, "android.apk")
        val sums = asset(json, "SHA256SUMS")
        if (apk == null || sums == null || apk.first.isEmpty()) { onMsg("这个版本没有可安装的包或 SHA256SUMS。请改从发布页下载。"); return }
        if (android.os.Build.VERSION.SDK_INT >= 26 && !ctx.packageManager.canRequestPackageInstalls()) {
            try { ctx.startActivity(android.content.Intent(android.provider.Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, android.net.Uri.parse("package:" + ctx.packageName)).addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK)) } catch (_: Exception) {}
            onMsg("请先允许本应用安装未知应用，然后再次点下载并安装。")
            return
        }
        onMsg("正在下载更新…")
        Thread {
            val msg = try { fetchAndInstall(ctx, apk.first, apk.second, sums.first) } catch (e: Exception) { "下载更新包失败：${e.message ?: e.javaClass.simpleName}" }
            Handler(Looper.getMainLooper()).post { onMsg(msg) }
        }.start()
    }

    private fun fetchAndInstall(ctx: android.content.Context, apkUrl: String, apkName: String, sumsUrl: String): String {
        val dir = java.io.File(ctx.cacheDir, "updates"); dir.mkdirs()
        val apk = java.io.File(dir, "update.apk"); val sums = java.io.File(dir, "SHA256SUMS.txt")
        if (!httpGet(apkUrl, apk) || !httpGet(sumsUrl, sums)) { apk.delete(); return "下载更新包失败。" }
        val want = sums.readText().lineSequence().map { it.trim() }.firstNotNullOfOrNull { line ->
            val sp = line.indexOf(' '); if (sp < 1) return@firstNotNullOfOrNull null
            val hex = line.substring(0, sp); val name = line.substring(sp + 1).trimStart(' ', '*')
            if (name.endsWith(apkName)) hex else null
        }
        val got = sha256(apk)
        if (want == null || !want.equals(got, ignoreCase = true)) { apk.delete(); return "更新包校验失败，没有替换任何文件。" }
        val uri = androidx.core.content.FileProvider.getUriForFile(ctx, ctx.packageName + ".fileprovider", apk)
        val view = android.content.Intent(android.content.Intent.ACTION_VIEW).apply {
            setDataAndType(uri, "application/vnd.android.package-archive")
            addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION or android.content.Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        ctx.startActivity(view)
        return "已打开系统安装器。请确认安装。聊天记录会保留。"
    }

    private fun httpGet(url: String, dest: java.io.File): Boolean {
        val c = URL(url).openConnection() as HttpURLConnection
        c.instanceFollowRedirects = true; c.connectTimeout = 15000; c.readTimeout = 120000
        c.setRequestProperty("User-Agent", "LanMessenger-Android")
        try {
            if (c.responseCode != 200) return false
            c.inputStream.use { inp -> dest.outputStream().use { out ->
                val buf = ByteArray(65536); var n: Int; var total = 0L
                while (inp.read(buf).also { n = it } > 0) { total += n; if (total > 512L * 1024 * 1024) return false; out.write(buf, 0, n) }
            } }
            return dest.length() > 0
        } finally { c.disconnect() }
    }

    private fun sha256(f: java.io.File): String {
        val md = java.security.MessageDigest.getInstance("SHA-256")
        f.inputStream().use { inp -> val buf = ByteArray(65536); var n: Int; while (inp.read(buf).also { n = it } > 0) md.update(buf, 0, n) }
        return md.digest().joinToString("") { "%02x".format(it) }
    }
}
