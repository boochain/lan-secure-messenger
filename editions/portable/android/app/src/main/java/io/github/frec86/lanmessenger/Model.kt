package io.github.frec86.lanmessenger

import kotlinx.coroutines.flow.MutableStateFlow
import org.json.JSONArray
import org.json.JSONObject

data class Contact(val fp: String, val name: String, val trust: Int, val online: Boolean, val unread: Int, val addr: String, val version: String, val via: Boolean = false, val nick: String = "", val remark: String = "", val sig: String = "", val avatar: String = "", val pinned: Boolean = false, val draft: String = "")
data class Group(val id: String, val name: String, val members: List<String>, val epoch: Long, val unread: Int, val pinned: Boolean = false, val draft: String = "")
data class Transfer(val id: String, val msg: String, val name: String, val out: Boolean, val size: Long, val done: Long, val state: String, val path: String, val error: String, val resumed: Long = 0, val status: String = "") {
    /** link lost: the core keeps it "offered" with 连接中断，重连后自动续传 and resumes by itself */
    val paused get() = state == "offered" && error.isNotEmpty()
}
/** Relay server status (docs/ipc.md): state off | connecting | online | auth_failed | error. The passphrase itself is never read back. */
data class RelayInfo(val enabled: Boolean = false, val host: String = "", val port: Int = 9443, val hasPassphrase: Boolean = false, val state: String = "off", val detail: String = "", val peers: Int = 0)
fun relayStateZh(s: String) = when (s) { "connecting" -> "正在连接中转服务器…"; "online" -> "已连接中转服务器"; "auth_failed" -> "中转服务器拒绝连接"; "error" -> "无法连接中转服务器"; else -> "未启用" }
data class Msg(val id: String, val out: Boolean, val kind: Int, val status: String, val ts: Long, val sender: String, val body: String, val file: String, val size: Long, val path: String,
               val replyTo: String = "", val quote: String = "", val reactions: String = "", val mine: String = "", val edited: Boolean = false, val recalled: Boolean = false, val canModify: Boolean = false, val voice: Int = -1,
               val forwarded: Boolean = false, val pinned: Boolean = false, val starred: Boolean = false, val mentionsMe: Boolean = false)
data class Me(val fp: String = "", val name: String = "", val port: Int = 0, val version: String = "", val downloads: String = "", val sig: String = "", val avatar: String = "")
data class AppState(val me: Me = Me(), val contacts: List<Contact> = emptyList(), val groups: List<Group> = emptyList(), val transfers: List<Transfer> = emptyList(), val running: Boolean = false, val error: String = "",
                    val last: Map<String, Msg> = emptyMap(), val relay: RelayInfo = RelayInfo())
/** One chat-list row (contact or group), sorted by last activity like WhatsApp/Telegram. `via` = online through the relay server (经服务器). */
data class Conv(val id: String, val name: String, val group: Boolean, val online: Boolean, val trusted: Boolean, val via: Boolean, val unread: Int, val last: Msg?, val members: Int, val key: String, val pinned: Boolean = false, val draft: String = "")
fun AppState.convs(): List<Conv> {
    val l = contacts.filter { it.trust != 2 }.map { Conv("dm:" + it.fp, it.name, false, it.online, it.trust == 1, it.online && it.via, it.unread, last["dm:" + it.fp], 0, it.fp, it.pinned, it.draft) } +
            groups.map { Conv("group:" + it.id, it.name, true, false, false, false, it.unread, last["group:" + it.id], it.members.size, it.id, it.pinned, it.draft) }
    return l.sortedWith(compareByDescending<Conv> { it.pinned }.thenByDescending { it.last?.ts ?: 0L }.thenBy { it.name })
}

/** Process-wide state, refreshed by CoreService after every core event. */
object Repo {
    val state = MutableStateFlow(AppState())
    val revision = MutableStateFlow(0L)       // bumps on every event so open chats reload
    val toast = MutableStateFlow<String?>(null)
    var openConv: String? = null
    /** message id to scroll to when a chat opens from 消息历史 */
    @Volatile var jumpId: String? = null
    @Volatile var appForeground: Boolean = true
    /** v0.6.1 P2: conv -> (name, until ms) of the peer that is typing */
    val typing = MutableStateFlow<Map<String, Pair<String, Long>>>(emptyMap())

    fun refresh() {
        if (!Native.running()) { state.value = state.value.copy(running = false); return }
        val o = JSONObject(Native.state())
        val me = o.getJSONObject("me")
        state.value = AppState(
            Me(me.getString("fp"), me.getString("name"), me.getInt("port"), me.getString("version"), me.getString("downloads"), me.optString("sig"), me.optString("avatar")),
            o.getJSONArray("contacts").objs().map { Contact(it.getString("fp"), it.getString("name"), it.getInt("trust"), it.getBoolean("online"), it.getInt("unread"), it.getString("addr"), it.getString("version"), it.optBoolean("via"), it.optString("nick"), it.optString("remark"), it.optString("sig"), it.optString("avatar"), it.optBoolean("pinned"), it.optString("draft")) },
            o.getJSONArray("groups").objs().map { g -> Group(g.getString("id"), g.getString("name"), g.getJSONArray("members").let { a -> (0 until a.length()).map { a.getString(it) } }, g.getLong("epoch"), g.getInt("unread"), g.optBoolean("pinned"), g.optString("draft")) },
            o.getJSONArray("transfers").objs().map { Transfer(it.getString("id"), it.getString("msg"), it.getString("name"), it.getBoolean("out"), it.getLong("size"), it.getLong("done"), it.getString("state"), it.getString("path"), it.getString("error"), it.optLong("resumed"), it.optString("status")) },
            running = true,
            relay = o.optJSONObject("relay")?.let { r -> RelayInfo(r.optBoolean("enabled"), r.optString("host"), r.optInt("port", 9443).takeIf { it > 0 } ?: 9443, r.optBoolean("has_pass"), r.optString("state", "off"), r.optString("detail"), r.optInt("peers")) } ?: RelayInfo())
        val last = HashMap<String, Msg>()
        for (cv in state.value.contacts.map { "dm:" + it.fp } + state.value.groups.map { "group:" + it.id })
            messages(cv).lastOrNull { it.kind != 2 }?.let { last[cv] = it }
        state.value = state.value.copy(last = last)
        revision.value = revision.value + 1
    }
    fun messages(conv: String): List<Msg> = JSONArray(Native.messages(conv)).objs().map {
        Msg(it.getString("id"), it.getBoolean("out"), it.getInt("kind"), it.getString("status"), it.getLong("ts"), it.getString("sender"), it.getString("body"), it.getString("file"), it.getLong("size"), it.getString("path"),
            it.optString("reply_to"), it.optString("quote"), it.optString("reactions"), it.optString("mine"), it.optBoolean("edited"), it.optBoolean("recalled"), it.optBoolean("can_modify"), it.optInt("voice", -1),
            it.optBoolean("forwarded"), it.optBoolean("pinned"), it.optBoolean("starred"), it.optBoolean("mentions_me"))
    }
    private fun JSONArray.objs() = (0 until length()).map { getJSONObject(it) }
}

fun errZh(e: String) = when (e) { "untrusted" -> "未信任的联系人，已拒收"; "open failed" -> "无法打开文件"; "write failed" -> "写入失败"; "rename failed" -> "无法保存文件"; else -> e }
