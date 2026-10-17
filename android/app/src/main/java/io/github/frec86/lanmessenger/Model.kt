package io.github.frec86.lanmessenger

import kotlinx.coroutines.flow.MutableStateFlow
import org.json.JSONArray
import org.json.JSONObject

data class Contact(val fp: String, val name: String, val trust: Int, val online: Boolean, val unread: Int, val addr: String, val version: String)
data class Group(val id: String, val name: String, val members: List<String>, val epoch: Long, val unread: Int)
data class Transfer(val id: String, val msg: String, val name: String, val out: Boolean, val size: Long, val done: Long, val state: String, val path: String, val error: String)
data class Msg(val id: String, val out: Boolean, val kind: Int, val status: String, val ts: Long, val sender: String, val body: String, val file: String, val size: Long, val path: String)
data class Me(val fp: String = "", val name: String = "", val port: Int = 0, val version: String = "", val downloads: String = "")
data class AppState(val me: Me = Me(), val contacts: List<Contact> = emptyList(), val groups: List<Group> = emptyList(), val transfers: List<Transfer> = emptyList(), val running: Boolean = false, val error: String = "",
                    val last: Map<String, Msg> = emptyMap())
/** One chat-list row (contact or group), sorted by last activity like WhatsApp/Telegram. `via` = relayed through a server (UI hook; no protocol data yet). */
data class Conv(val id: String, val name: String, val group: Boolean, val online: Boolean, val trusted: Boolean, val via: Boolean, val unread: Int, val last: Msg?, val members: Int, val key: String)
fun AppState.convs(): List<Conv> {
    val l = contacts.filter { it.trust != 2 }.map { Conv("dm:" + it.fp, it.name, false, it.online, it.trust == 1, false, it.unread, last["dm:" + it.fp], 0, it.fp) } +
            groups.map { Conv("group:" + it.id, it.name, true, false, false, false, it.unread, last["group:" + it.id], it.members.size, it.id) }
    return l.sortedWith(compareByDescending<Conv> { it.last?.ts ?: 0L }.thenBy { it.name })
}

/** Process-wide state, refreshed by CoreService after every core event. */
object Repo {
    val state = MutableStateFlow(AppState())
    val revision = MutableStateFlow(0L)       // bumps on every event so open chats reload
    val toast = MutableStateFlow<String?>(null)
    var openConv: String? = null

    fun refresh() {
        if (!Native.running()) { state.value = state.value.copy(running = false); return }
        val o = JSONObject(Native.state())
        val me = o.getJSONObject("me")
        state.value = AppState(
            Me(me.getString("fp"), me.getString("name"), me.getInt("port"), me.getString("version"), me.getString("downloads")),
            o.getJSONArray("contacts").objs().map { Contact(it.getString("fp"), it.getString("name"), it.getInt("trust"), it.getBoolean("online"), it.getInt("unread"), it.getString("addr"), it.getString("version")) },
            o.getJSONArray("groups").objs().map { g -> Group(g.getString("id"), g.getString("name"), g.getJSONArray("members").let { a -> (0 until a.length()).map { a.getString(it) } }, g.getLong("epoch"), g.getInt("unread")) },
            o.getJSONArray("transfers").objs().map { Transfer(it.getString("id"), it.getString("msg"), it.getString("name"), it.getBoolean("out"), it.getLong("size"), it.getLong("done"), it.getString("state"), it.getString("path"), it.getString("error")) },
            running = true)
        val last = HashMap<String, Msg>()
        for (cv in state.value.contacts.map { "dm:" + it.fp } + state.value.groups.map { "group:" + it.id })
            messages(cv).lastOrNull { it.kind != 2 }?.let { last[cv] = it }
        state.value = state.value.copy(last = last)
        revision.value = revision.value + 1
    }
    fun messages(conv: String): List<Msg> = JSONArray(Native.messages(conv)).objs().map {
        Msg(it.getString("id"), it.getBoolean("out"), it.getInt("kind"), it.getString("status"), it.getLong("ts"), it.getString("sender"), it.getString("body"), it.getString("file"), it.getLong("size"), it.getString("path"))
    }
    private fun JSONArray.objs() = (0 until length()).map { getJSONObject(it) }
}

fun errZh(e: String) = when (e) { "untrusted" -> "未信任的联系人，已拒收"; "open failed" -> "无法打开文件"; "write failed" -> "写入失败"; "rename failed" -> "无法保存文件"; else -> e }
