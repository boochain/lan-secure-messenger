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
    @JvmStatic external fun refreshPeers()
    @JvmStatic external fun setTrust(fp: String, trust: Int): String
    @JvmStatic external fun setMuted(fp: String, muted: Boolean): String
    @JvmStatic external fun isMuted(fp: String): Boolean
    @JvmStatic external fun diagnostics(): String
    @JvmStatic external fun pinOffer(): String
    @JvmStatic external fun pinPair(fp: String, pin: String): String
    @JvmStatic external fun groupCreate(name: String, fpsCsv: String): String
    @JvmStatic external fun groupInvite(gid: String, fpsCsv: String): String
    @JvmStatic external fun groupRemove(gid: String, fp: String): String
    @JvmStatic external fun groupLeave(gid: String): String
    @JvmStatic external fun markRead(conv: String)
    /** delete one message on this device only (tombstoned in the core); false if not found */
    @JvmStatic external fun deleteMessage(id: String): Boolean
    @JvmStatic external fun clearConversation(conv: String): Int
    @JvmStatic external fun setName(name: String)
    @JvmStatic external fun cancelTransfer(tid: String): String
    @JvmStatic external fun storageInfo(): String
    @JvmStatic external fun cleanupStorage(orphans: Boolean, finishedTransfers: Boolean): Int
    /** Relay server settings. [pass] null = keep the stored passphrase, "" = clear it. Write-only: read [RelayInfo.hasPassphrase]. */
    @JvmStatic external fun relaySet(enabled: Boolean, host: String, port: Int, pass: String?): String
    /** 消息历史: newest first; types/statuses are bitmasks (0 = all), dir 0 both / 1 received / 2 sent; returns {"total":N,"items":[…]} */
    @JvmStatic external fun historyQuery(query: String, convs: String, fromMs: Long, toMs: Long, types: Int, dir: Int, statuses: Int, offset: Int, limit: Int): String
    /** null keeps the field; avatar "" removes it; avatar file must be <= 48 KB */
    @JvmStatic external fun setProfile(signature: String?, avatarPath: String?): String
    @JvmStatic external fun setRemark(fp: String, remark: String)
    // v0.6.1 P1: reply/quote, reactions ("" removes mine), edit/recall (own, 24 h), voice file naming
    @JvmStatic external fun sendReply(conv: String, text: String, replyTo: String): String
    @JvmStatic external fun react(id: String, emoji: String): String
    @JvmStatic external fun editMessage(id: String, body: String): String
    @JvmStatic external fun recallMessage(id: String): String
    @JvmStatic external fun voiceName(secs: Int, ext: String): String
    // v0.6.1 P2
    @JvmStatic external fun sendText2(conv: String, text: String, replyTo: String): String
    // v0.6.2
    @JvmStatic external fun sendPoll(conv: String, question: String, optsNl: String, multi: Boolean): String
    @JvmStatic external fun votePoll(id: String, opt: Int): String
    @JvmStatic external fun closePoll(id: String): String
    @JvmStatic external fun setArchived(conv: String, on: Boolean)
    @JvmStatic external fun setFolders(conv: String, csv: String)
    @JvmStatic external fun setTtl(conv: String, secs: Int): String
    @JvmStatic external fun setConvPinned(conv: String, on: Boolean)
    @JvmStatic external fun setDraft(conv: String, text: String)
    @JvmStatic external fun getDraft(conv: String): String
    @JvmStatic external fun setMsgFlags(id: String, pinned: Int, starred: Int): String
    @JvmStatic external fun sendTyping(conv: String)
    @JvmStatic external fun forward(id: String, conv: String): String
    @JvmStatic external fun flagged(conv: String, which: Int): String
    @JvmStatic external fun mentionCandidates(conv: String): String
}
