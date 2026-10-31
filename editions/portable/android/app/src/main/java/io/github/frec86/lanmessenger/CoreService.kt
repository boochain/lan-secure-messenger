package io.github.frec86.lanmessenger

import android.app.*
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.net.wifi.WifiManager
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat

/** Foreground service that owns the C core: keeps TCP links, discovery and transfers alive while the UI is closed. */
class CoreService : Service() {
    private var mlock: WifiManager.MulticastLock? = null
    private var wlock: WifiManager.WifiLock? = null
    @Volatile private var pumping = false

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        channels(this)
        val n = NotificationCompat.Builder(this, CH_SERVICE).setSmallIcon(R.drawable.ic_stat).setContentTitle("局域网安全通讯正在运行")
            .setContentText("保持连接以接收消息和文件").setOngoing(true).setContentIntent(openApp(this)).build()
        ServiceCompat.startForeground(this, 1, n, if (Build.VERSION.SDK_INT >= 29) ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC else 0)
        val wm = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
        // Android drops incoming broadcast/multicast UDP to save power unless a MulticastLock is held: discovery needs it.
        mlock = wm.createMulticastLock("lsm-discovery").apply { setReferenceCounted(false); acquire() }
        @Suppress("DEPRECATION")
        wlock = wm.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "lsm-wifi").apply { setReferenceCounted(false); acquire() }
        if (!Native.running()) {
            val name = getSharedPreferences("lsm", 0).getString("name", null) ?: (Build.MODEL ?: "Android")
            val err = Native.start(filesDir.absolutePath + "/lsm", name, broadcastTargets(wm), 0)
            Repo.state.value = Repo.state.value.copy(error = err)
        }
        pumping = true
        Thread({ pump() }, "lsm-events").start()
    }

    /** Wi-Fi subnet broadcast as an extra announce target (getifaddrs-free path that works on every API level). */
    @Suppress("DEPRECATION")
    private fun broadcastTargets(wm: WifiManager): String {
        val d = wm.dhcpInfo ?: return ""
        if (d.ipAddress == 0) return ""
        val mask = if (d.netmask != 0) d.netmask else 0x00ffffff
        val b = (d.ipAddress and mask) or mask.inv()
        return "%d.%d.%d.%d".format(b and 0xff, (b shr 8) and 0xff, (b shr 16) and 0xff, (b ushr 24) and 0xff)
    }

    private fun pump() {
        Repo.refresh()
        while (pumping) {
            var any = false
            while (true) {
                val e = Native.poll() ?: break
                any = true
                handle(e)
            }
            if (any) Repo.refresh()
            Thread.sleep(150)
        }
    }

    private fun handle(e: String) {
        val f = e.split('\t')
        when (f[0]) {
            "msg" -> if (f.size >= 7 && f[3] == "in" && f[5] != "system" && (!Repo.appForeground || f[2] != Repo.openConv)) {
                val who = Repo.state.value.contacts.firstOrNull { "dm:" + it.fp == f[2] }?.name ?: "群消息"
                notifyMsg(this, f[2], who, if (f[5] == "file") "[文件] " + f[6] else f[6])
            }
            "pin_result" -> Repo.toast.value = if (e.contains("\tok")) "PIN 配对成功，已互相信任" else "PIN 配对失败（PIN 错误或已过期）"
            "notice" -> Repo.toast.value = f.getOrElse(1) { "" }
        }
    }

    override fun onDestroy() {
        pumping = false
        Native.stop()
        mlock?.release(); wlock?.release()
        Repo.state.value = Repo.state.value.copy(running = false)
        super.onDestroy()
    }

    companion object {
        const val CH_SERVICE = "service"; const val CH_MSG = "messages"
        fun start(c: Context) {
            val i = Intent(c, CoreService::class.java)
            if (Build.VERSION.SDK_INT >= 26) c.startForegroundService(i) else c.startService(i)
        }
        fun channels(c: Context) {
            if (Build.VERSION.SDK_INT < 26) return
            val nm = c.getSystemService(NotificationManager::class.java)
            nm.createNotificationChannel(NotificationChannel(CH_SERVICE, "后台运行", NotificationManager.IMPORTANCE_MIN))
            nm.createNotificationChannel(NotificationChannel(CH_MSG, "新消息", NotificationManager.IMPORTANCE_HIGH))
        }
        fun openApp(c: Context, conv: String? = null): PendingIntent {
            val i = Intent(c, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP)
            if (conv != null) i.putExtra("conv", conv)
            return PendingIntent.getActivity(c, conv?.hashCode() ?: 0, i, PendingIntent.FLAG_UPDATE_CURRENT or (if (Build.VERSION.SDK_INT >= 23) PendingIntent.FLAG_IMMUTABLE else 0))
        }
        fun notifyMsg(c: Context, conv: String, title: String, text: String) {
            if (conv.startsWith("dm:") && Native.isMuted(conv.substring(3))) return  // 免打扰
            val n = NotificationCompat.Builder(c, CH_MSG).setSmallIcon(R.drawable.ic_stat).setContentTitle(title).setContentText(text)
                .setAutoCancel(true).setPriority(NotificationCompat.PRIORITY_HIGH).setContentIntent(openApp(c, conv)).build()
            try { (c.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager).notify(conv.hashCode(), n) } catch (_: SecurityException) {}
        }
    }
}
