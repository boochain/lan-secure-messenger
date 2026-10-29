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

    /** result: (newer: 1 / 0 / -1 failed, message, url), delivered on the main thread */
    fun check(local: String, done: (Int, String, String) -> Unit) {
        Thread {
            val r = try {
                val c = URL(API).openConnection() as HttpURLConnection
                c.connectTimeout = 10000; c.readTimeout = 15000
                c.setRequestProperty("Accept", "application/vnd.github+json"); c.setRequestProperty("User-Agent", "LanMessenger-Android")
                try {
                    if (c.responseCode != 200) Triple(-1, "检查更新失败（HTTP ${c.responseCode}）。", RELEASES)
                    else result(c.inputStream.bufferedReader().use { it.readText() }, local)
                } finally { c.disconnect() }
            } catch (e: Exception) { Triple(-1, "无法连接 GitHub 检查更新：${e.message ?: e.javaClass.simpleName}", RELEASES) }
            Handler(Looper.getMainLooper()).post { done(r.first, r.second, r.third) }
        }.start()
    }

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
}
