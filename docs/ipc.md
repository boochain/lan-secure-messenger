# lan-daemon local IPC protocol (v1)

UIs (the WinUI 3 app `LanMessenger.exe`, the `lan-msg` CLI, scripts) never touch the network
themselves. They talk to `lan-daemon` over a local, per-user channel and render **only**
what the daemon reports (snapshot + events).

## Transport

| OS | Endpoint | Access control |
|----|----------|----------------|
| Windows | named pipe `\\.\pipe\lan-secure-messenger-<USERNAME>` | DACL `D:P(A;;GA;;;<current user SID>)(A;;GA;;;SY)`, remote clients rejected, `FILE_FLAG_FIRST_PIPE_INSTANCE` (a second daemon fails to start) |
| Linux/macOS | unix socket `$XDG_RUNTIME_DIR/lan-secure-messenger-<USER>.sock` (fallback `/tmp`) | file mode `0600` |

`<USERNAME>` is sanitised to `[A-Za-z0-9._-]` (other characters become `_`).
The endpoint can be overridden with `lan-daemon --ipc <endpoint>` / env `LSM_IPC`
(the same flag exists for `lan-msg`).

## Framing

UTF-8 JSON, **one message per line** (`\n` terminated), max 4 MiB per line.
Messages follow JSON-RPC 2.0:

```json
{"jsonrpc":"2.0","id":1,"method":"send_text","params":{"conversation":"dm:0C6B-6C51-1FB5-8E6C","body":"你好"}}
{"jsonrpc":"2.0","id":1,"result":{"id":"…","status":"sent", …}}
{"jsonrpc":"2.0","id":2,"error":{"code":-32602,"message":"消息不能为空"}}
```

Requests on one connection are processed concurrently; match responses by `id`.
Requests without `id` are notifications (no response).

### Events

Call `subscribe` once; from then on the daemon pushes notifications on the same connection:

```json
{"jsonrpc":"2.0","method":"event","params":{"type":"message_added","message":{…}}}
```

If a slow client falls behind, it receives `{"type":"resync"}` and should call `get_snapshot` again.

Recommended client flow: connect → `hello` → `subscribe` → `get_snapshot` → apply events.
(Subscribing before the snapshot guarantees no event is missed; applying an event twice is harmless
because every event carries the full object.)

## Identifiers

* Peer: fingerprint `XXXX-XXXX-XXXX-XXXX` (first 8 bytes of SHA-256 of the X25519 identity key).
* Conversation: `dm:<fingerprint>` or `group:<uuid>`.
* Messages, transfers, groups: UUID strings.
* Timestamps: `*_ms` = Unix epoch milliseconds (UTC).

## Methods

| method | params | result |
|---|---|---|
| `hello` | `{}` | `{daemon_version, protocol, ipc_version, fingerprint}` |
| `subscribe` | `{}` | `{ok}` then event stream |
| `get_snapshot` | `{}` | `Snapshot` |
| `refresh_peers` | `{}` | `{ok}` — force LAN announce + clear dial backoff + re-emit peer/conversation events |
| `get_storage` | `{}` | `{data_dir,download_dir,history_bytes,downloads_bytes,partials_bytes,orphan_partials_bytes,outgoing_bytes,total_bytes}` |
| `cleanup_storage` | `{failed_partials?,orphans?,finished_transfers?}` | `{ok,removed_bytes,removed_files,storage}` — delete orphan/failed `.part` piles and/or finished transfer rows |
| `get_messages` | `{conversation, before_ts?, limit?=200}` | `Message[]` (oldest first) |
| `send_text` | `{conversation, body}` | `Message` (status `queued` if peer offline — delivered automatically later) |
| `send_file` | `{conversation, path}` | `Transfer` (first recipient; groups fan out) |
| `cancel_transfer` / `retry_transfer` / `accept_transfer` / `reject_transfer` | `{id}` | `{ok}` / `Transfer` |
| `add_peer` | `{address:"192.168.1.23[:41235]"}` | `{ok, address}` — persisted, redialed with backoff |
| `remove_manual_peer` | `{address}` | `{ok}` |
| `set_trust` | `{fingerprint, trust:"trusted"\|"unknown"\|"blocked"}` | `Peer` |
| `forget_peer` | `{fingerprint}` | `{ok}` |
| `set_display_name` | `{name}` | `Me` |
| `complete_first_run` | `{name?}` | `Me` |
| `set_muted` | `{conversation, muted}` | `{ok}` |
| `mark_read` | `{conversation}` | `{ok}` (DM + group: fans out `Payload::Read` to online peers/members) |
| `create_group` | `{name, members:[fp]}` | `Group` (members must be trusted) |
| `invite_to_group` | `{group_id, members:[fp]}` | `Group` (creator only) |
| `remove_group_member` | `{group_id, fingerprint}` | `{ok}` (creator only; rotates group key) |
| `leave_group` | `{group_id}` | `{ok}` |
| `search` | `{query, limit?}` | `Message[]` |
| `backup` / `restore` | `{path, password}` | `{ok}` / `{ok, restart_required:true}` |
| `account_register` / `account_login` | `{user, password}` | `{ok}` — relay online required |
| `account_backup_put` / `account_backup_get` | `{user, password}` | `{ok}` / `{ok, restart_required:true}` — identity via relay |
| `account_revoke` | `{user, password}` | `{ok}` — clear stored device fingerprint on relay |
| `retain_store` | `{id, blob_b64|hex, ttl_secs?, dest_fingerprint?}` | `{ok}` — opaque E2E blob on relay |
| `retain_fetch` / `retain_del` | `{id}` | `{ok, blob_hex?}` / `{ok}` |
| `send_text` / `send_file` | `…, retain?:bool` | per-item server-retain override (default from settings) |
| `pin_offer` | `{}` | `{pin, expires_in_secs}` |
| `pin_pair` | `{fingerprint, pin}` | `{ok:bool}` (both sides become trusted on success) |
| `update_settings` | `{settings:{…partial Settings…}}` | `{ok, restart_required}` |
| `get_diagnostics` | `{skip_firewall?}` | `Diagnostics` |
| `firewall_status` | `{}` | `FirewallStatus` |
| `firewall_setup` | `{include_public?}` | `{ok}` — shows one UAC prompt (Windows) |
| `shutdown` | `{}` | `{ok}`, daemon exits |
| `debug_drop_connection` | `{fingerprint}` | test helper: abruptly drops the link |

Error codes: `-32700` parse, `-32601` unknown method, `-32602` invalid params,
`-32004` peer offline, `-32010` firewall setup failed, `-32000` other.

## Objects (JSON, snake_case)

```text
Snapshot     { me: Me, peers: Peer[], groups: Group[], conversations: Conversation[], transfers: Transfer[], settings: Settings }
Me           { fingerprint, name, version, listen_port, discovery_port, ipc_endpoint, data_dir, log_dir, download_dir, first_run_done }
Peer         { fingerprint, name, trust, online, state:"connected"|"connecting"|"offline", addr?, direction?:"outbound"|"inbound",
               last_seen_ms, muted, unread, rtt_ms?, app_version }
Group        { id, name, creator_fp, is_creator, members:[{fingerprint,name,online}], key_epoch, muted, unread }
Conversation { id, kind:"dm"|"group", title, last_body, last_ts_ms, unread, muted, online }
Message      { id, conversation, direction:"in"|"out", sender_fp, sender_name, kind:"text"|"file"|"system", body, ts_ms,
               status:"queued"|"sent"|"delivered"|"read"|"failed"|"received",
               file?: { transfer_id, name, size, mime, path?, is_image } }
Transfer     { id, message_id, conversation, peer_fp, peer_name, direction, name, size, transferred,
               state:"offered"|"active"|"verifying"|"done"|"failed"|"cancelled"|"rejected",
               mime, path?, error, bytes_per_sec, started_ms, retryable }
Diagnostics  { version, os, tcp_listen, tcp_error, discovery: DiscoStatus, firewall: FirewallStatus, peers: PeerDiag[],
               manual_peers, log_dir, config_dir, ipc_endpoint }
DiscoStatus  { enabled, port, listening, bind_error, multicast, announces_sent, announces_received, announces_rejected,
               send_errors, last_send_error, interfaces:[{name, friendly_name, description, ip, netmask, broadcast, up, used, reason, is_default}] }
FirewallStatus { supported, tcp_rule, udp_rule, rule_path_mismatch, rule_profiles, exe, profiles:[{name, interface, category}], public_network, advice }
PeerDiag     { fingerprint, name, state, addr?, direction?, connected_for_ms?, last_rx_ms_ago?, rtt_ms?, dial_failures, last_error,
               next_retry_in_ms?, candidates:[addr], sources:["discovery"|"outbound"|"inbound"] }
```

### Event types

`peer_updated {peer}`, `peer_removed {fingerprint}`, `message_added {message}`, `message_updated {message}`,
`transfer_updated {transfer}`, `group_updated {group}`, `group_removed {id}`, `conversation_updated {conversation}`,
`me_updated {me}`, `settings_updated {settings}`, `notice {level:"info"|"warn"|"error", text}`,
`notify {conversation, title, body}` (show a toast unless the window is focused on that conversation), `resync`.

## Relay client API (for the UI: settings section, status indicator, 经服务器 badge)

Data the daemon exposes (lan-core, branch feat/relay-client). The C core equivalents are listed for XP/GTK/Android.

### Settings (part of `Settings`; read via `get_snapshot().settings`, write via `update_settings`)
| field | type | default | notes |
|---|---|---|---|
| `relay_enabled` | bool | `false` | off by default |
| `relay_host` | string | `""` | hostname or IP of the relay server |
| `relay_port` | u16 | `9443` | |
| `relay_passphrase` | string | `""` | network passphrase. **Write-only.** Never sent over the network (challenge-response). On disk it is sealed in `data/relay.dat` (ChaCha20-Poly1305, history key), never in settings.json. It is **never returned over IPC**: `snapshot.settings` and `settings_updated` omit the key. In an `update_settings` patch, leave the key out to keep the passphrase, send `""` to clear it, or send any other value to set it. |
| `relay_has_passphrase` | bool | `false` | read-only: whether a passphrase is configured (omitted when false). Use it to show "已设置" / a masked placeholder. Ignored in `update_settings`. |

`relay_status` / `snapshot.relay` also carries `has_passphrase` (bool).

Write example: `{"method":"update_settings","params":{"settings":{"relay_enabled":true,"relay_host":"relay.example.com","relay_port":9443,"relay_passphrase":"…"}}}`.
Changing any relay field reconnects the relay immediately; `relay_enabled:false` disconnects.

### Relay status: `RelayStatus`
```json
{"enabled":true,"state":"online","detail":"relay.example.com:9443","server":"relay.example.com:9443","peers":3,"relayed_links":1}
```
* `state`: `off` | `connecting` | `online` | `auth_failed` | `error`.
  Suggested copy: off 未启用 · connecting 正在连接中转服务器… · online 已连接中转服务器 · auth_failed 中转服务器拒绝连接 · error 无法连接中转服务器 (show `detail` as secondary text/tooltip).
* `detail`: human-readable Chinese/English detail (server address when online, reason on error).
* `peers`: peers currently present on the server; `relayed_links`: live links carried by the relay.
* Sources: `get_snapshot().relay` (initial), RPC `relay_status` (any time), and event
  `{"type":"relay_updated","relay":RelayStatus}` (pushed on every change). Event names use the same serde
  tagging as the other `Event` variants (`#[serde(tag="type", rename_all="snake_case")]`, e.g. `peer_updated`).
* On a first `auth_failed` the daemon also sends a `Notice{level:"error"}` (中转服务器认证失败: …).

### Per-contact path: `Peer.via_relay`
* `Peer.via_relay: bool`, true when the live link to this contact goes through the relay server. It is
  false when offline or direct. Show the badge (cloud/server icon after the name, tooltip 经服务器中转) when
  `online && via_relay`. The header subtitle shows 经服务器中转 in place of 局域网直连.
* `Peer.addr` is `null` for relay links. `direction` is still outbound/inbound.
* Relay peers show up in the same contact list (same sorting, unread counts, notifications) via the
  usual `PeerUpdated` events. The path switches automatically (direct preferred) and a `PeerUpdated` is sent
  on every switch, so the UI just re-renders the badge.
* Android filter chip 经服务器 = `peers.filter { it.online && it.via_relay }`.

### Transfers
* `Transfer.resumed_from: u64`: offset the last (re)start continued from (0 = from the beginning). While a link is
  down, an interrupted transfer has `state:"offered"` with `error:"连接中断，重连后自动续传"` and resumes by itself.

### C core (XP / GTK / Android JNI): same data
* Settings: `lsm_relay_set(c, enabled, host, port, pass, err)` / `lsm_relay_get(c,&enabled,host,hn,&port,&has_pass)` (sealed `relay.dat`; `pass` NULL = keep, "" = clear; there is no passphrase getter).
* Status: `lsm_relay_status(c, detail, dn)` returns the state string; event `relay\t<state>\t<detail>`.
* Peers on server: `lsm_relay_peers(c)`; events `relay_peer\t<fp>\t<join|leave>\t<name>`.
* Per contact: `lsm_contact.via_relay`; event `peer\t<fp>\tpath\t<direct|relay>` on every switch.
* Transfers: `lsm_transfer.resumed_from`, `resumable`.

### C core (XP / GTK / Android): same rule
The relay passphrase is write-only there too.
- `lsm_relay_set(c, enabled, host, port, pass, err)` takes it: `NULL` keeps it, `""` clears it.
- `lsm_relay_get(c, &enabled, host, hn, &port, &has_pass)` reports only `has_pass` (0/1).
- CLI: `relay status` → `OK relay <state> <detail> enabled=<0|1> server=<host:port> peers=<n> passphrase=set|none`.
- Android JNI (xp `android/app/src/main/cpp/jni.c`, `Native.kt`):
  - `Native.relaySet(enabled: Boolean, host: String, port: Int, pass: String?): String` returns "" on success or the error text.
    `pass` null = keep the stored passphrase, "" = clear it, anything else = set it.
  - `Native.state()` JSON gains `"relay":{"enabled","host","port","has_pass","state","detail","peers"}` (Kotlin `RelayInfo.hasPassphrase`),
    `contacts[].via` (online through the relay) and `transfers[].resumed` (byte offset a resumed transfer continued from).
  - There is no getter for the passphrase.
