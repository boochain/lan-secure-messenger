package io.github.frec86.lanmessenger

/** Thin JNI surface over the portable C core (src/core.h). Errors come back as "" (ok) or a message. */
object Native {
    init { System.loadLibrary("lsmcore") }
    @JvmStatic external fun start(dataDir: String, name: String, targets: String, port: Int): String
    @JvmStatic external fun stop()
    @JvmStatic external fun running(): Boolean
    @JvmStatic external fun poll(): String?
    @JvmStatic external fun state(): String
    @JvmStatic external fun messages(conv: String): String
    @JvmStatic external fun sendText(conv: String, text: String): String
    @JvmStatic external fun sendFile(conv: String, path: String): String
    @JvmStatic external fun addAddr(addr: String): String
    @JvmStatic external fun setTrust(fp: String, trust: Int): String
    @JvmStatic external fun pinOffer(): String
    @JvmStatic external fun pinPair(fp: String, pin: String): String
    @JvmStatic external fun groupCreate(name: String, fpsCsv: String): String
    @JvmStatic external fun groupInvite(gid: String, fpsCsv: String): String
    @JvmStatic external fun groupRemove(gid: String, fp: String): String
    @JvmStatic external fun groupLeave(gid: String): String
    @JvmStatic external fun markRead(conv: String)
    /** delete one message on this device only (tombstoned in the core); false if not found */
    @JvmStatic external fun deleteMessage(id: String): Boolean
    @JvmStatic external fun setName(name: String)
    @JvmStatic external fun cancelTransfer(tid: String): String
    /** Relay server settings. [pass] null = keep the stored passphrase, "" = clear it. Write-only: read [RelayInfo.hasPassphrase]. */
    @JvmStatic external fun relaySet(enabled: Boolean, host: String, port: Int, pass: String?): String
}
