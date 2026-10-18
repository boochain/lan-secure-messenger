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
| `mark_read` | `{conversation}` | `{ok}` (sends read receipts for DMs) |
| `create_group` | `{name, members:[fp]}` | `Group` (members must be trusted) |
| `invite_to_group` | `{group_id, members:[fp]}` | `Group` (creator only) |
| `remove_group_member` | `{group_id, fingerprint}` | `{ok}` (creator only; rotates group key) |
| `leave_group` | `{group_id}` | `{ok}` |
| `search` | `{query, limit?}` | `Message[]` |
| `backup` / `restore` | `{path, password}` | `{ok}` / `{ok, restart_required:true}` |
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
