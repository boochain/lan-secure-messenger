# LSM 中转协议 v1 (LSM Relay Protocol v1)

状态 (status): 草案，随 `relay-server` 0.1.0 发布 (draft, implemented by `relay-server/` 0.1.0).

中转服务器是**可选**组件：客户端默认关闭，开启后填写服务器地址、端口和网络口令。局域网直连优先，中转只是后备。
The relay is **optional** and off by default. LAN direct is preferred; the relay is the fallback.

## 1. 安全模型 (security model)

* The relay forwards **opaque blobs** between 32-byte peer IDs. Clients put *Noise XX handshake and transport
  messages* (the very same bytes they would send on a LAN TCP connection) inside those blobs. The relay never
  holds a Noise key and never sees plaintext; peer identity is authenticated end-to-end by Noise, not by the relay.
* The **network passphrase** only gates who may use the relay. It is never sent: both sides prove knowledge
  of `K = Argon2id(passphrase)` with a nonce-based challenge-response (mutual: the client also verifies the server).
* A captured handshake allows an *offline* guess of the passphrase (cost: one Argon2id, 19 MiB, per guess) —
  choose a long passphrase (the deploy scripts generate 20+ random chars by default).
* The relay sees metadata: which peer IDs are online, who talks to whom, sizes and timing. Transport is plain TCP
  (no TLS in v1); confidentiality and integrity of content come from Noise.

## 2. 帧格式 (framing)

All integers are **big-endian**. Every message on the TCP stream is a frame:

```
+----------------+--------+------------------+
| len: u32       | type:u8| body (len-1 B)   |
+----------------+--------+------------------+
```

`len` counts `type + body` (so `len >= 1`). `len == 0` or `len > max_frame` is a protocol error and the server
closes the connection. Default `max_frame` = 1 048 640 (1 MiB + 64), announced in `AUTH_OK`; hard ceiling 16 MiB. Clients clamp the announced value to [4096, 16 MiB] before using it
as their own read limit (both cores), so a server cannot make a client buffer more than 16 MiB per frame.
Before authentication the server accepts at most 1024 bytes. Unknown frame types are **ignored** (forward compatibility).

## 3. 消息类型 (message types)

| type | name | dir | body |
|------|------|-----|------|
| 0x00 | HELLO | S→C | `"LSMR"`(4) · version u8 (=1) · salt[16] · argon_m_kib u32 · argon_t u32 · server_nonce[32] |
| 0x01 | AUTH | C→S | version u8 · client_nonce[32] · peer_id[32] · mac[32] · info_len u16 · info[info_len ≤ 512] |
| 0x02 | AUTH_OK | S→C | server_mac[32] · max_frame u32 · heartbeat_secs u16 |
| 0x03 | AUTH_FAIL | S→C | code u8 · utf8 reason; server closes |
| 0x10 | SEND | C→S | dst_id[32] · blob |
| 0x11 | DELIVER | S→C | src_id[32] · blob |
| 0x12 | SEND_FAIL | S→C | dst_id[32] · code u8 |
| 0x20 | PING | both | opaque token (≤ 64 B recommended) |
| 0x21 | PONG | both | echo of the PING token |
| 0x30 | LIST | C→S | empty — asks for a fresh PEER_LIST |
| 0x40 | PEER_LIST | S→C | count u16 · count × entry |
| 0x41 | PEER_JOIN | S→C | one entry (also sent when an existing peer re-logs in: treat as upsert) |
| 0x42 | PEER_LEAVE | S→C | peer_id[32] |
| 0x50 | ERROR | S→C | code u8 · utf8 reason; server closes after sending |
| 0x7F | BYE | C→S | empty — graceful logout |

`entry = peer_id[32] · info_len u16 · info[info_len]`.

`info` is opaque to the server (≤ 512 B). Recommended content: UTF-8 JSON `{"name":"Bill","v":"0.5.0","caps":["file","call"]}`
so a contact can be shown **immediately**, before the Noise handshake confirms the identity.

### Codes

| code | meaning |
|---|---|
| 1 BAD_PASSPHRASE | MAC mismatch (wrong network passphrase) |
| 2 BAD_VERSION | unsupported protocol version |
| 3 MALFORMED | malformed AUTH |
| 4 SERVER_FULL | `max_connections` / `max_connections_per_ip` reached |
| 5 TOO_MANY_ATTEMPTS | too many failed logins from this IP in the last minute |
| 6 REPLACED | same peer_id logged in on another connection (old one is closed) |
| 10 PEER_OFFLINE | SEND target not online |
| 11 PEER_BUSY | target's queue stayed full for `send_timeout_ms` (slow receiver) |
| 12 FRAME_TOO_LARGE | frame length invalid; connection closed |
| 13 IDLE_TIMEOUT | nothing received for `idle_timeout_secs` |
| 14 SHUTDOWN | server stopping — reconnect with back-off |

## 4. 握手 (handshake)

```
C ──TCP connect──▶ S
C ◀── HELLO(salt, m, t, Ns) ── S
      K   = Argon2id(pass = UTF-8 passphrase, salt, m_kib = m, t = t, lanes = 1, out = 32, version 0x13)
      Nc  = 32 random bytes
      mac = BLAKE2b-256(key = K, "LSMR-v1-client" ‖ Ns ‖ Nc ‖ peer_id)
C ── AUTH(1, Nc, peer_id, mac, info) ──▶ S          (within auth_timeout_secs, default 10 s)
C ◀── AUTH_OK(smac, max_frame, hb) ── S
      client checks smac == BLAKE2b-256(key = K, "LSMR-v1-server" ‖ Nc ‖ Ns ‖ peer_id)  (constant-time)
C ◀── PEER_LIST ── S                               (snapshot of everybody else online)
      all other peers receive PEER_JOIN(peer_id, info) at the same moment
```

* BLAKE2b is keyed BLAKE2b (RFC 7693) with a 32-byte key and 32-byte output — `crypto_blake2b_keyed()` in
  Monocypher 4, `Blake2bMac<U32>` in the Rust `blake2` crate.
* Argon2id: `crypto_argon2(.., {CRYPTO_ARGON2_ID, m, t, 1}, ..)` in Monocypher 4 (work area = `m × 1024` bytes),
  `argon2::Argon2::new(Argon2id, V0x13, Params::new(m, t, 1, Some(32)))` in Rust. v1 servers send m = 19456, t = 2.
  Clients should refuse absurd parameters (e.g. m > 1 GiB) and may cache K per (server, salt).
* `salt` is per installation (random 16 bytes generated by the deploy scripts, kept across upgrades);
  if absent the server uses the built-in `"LSMR-default-slt"`.
* Test vector (salt = `"LSMR-default-slt"`, passphrase `"test-pass"`): `K = 63d2d3a326f89cb06409e204bbe49c08d46a56b4725318c66b4f97ac6d637d5d`;
  `BLAKE2b-256(K, "LSMR-v1-client" ‖ 01×32 ‖ 02×32 ‖ 03×32) = 84e6b26fd79d428db2267c1360133ff87c720f067cf59b2becaa38eddb24a7b1`.
* Wrong passphrase → `AUTH_FAIL(1)` after ~300 ms; after `auth_failures_per_min` (default 10) failures an IP gets
  `AUTH_FAIL(5)` immediately for the rest of the minute.

### peer_id

`peer_id` = **SHA-256 of the peer's static Noise X25519 public key** (the same fingerprint shown in the UI).
The relay does not verify it (it cannot: X25519 keys don't sign). A peer that lies about its ID just fails the
end-to-end Noise XX handshake; the worst it can do is a presence-level denial of service (see §8).

## 5. 转发 (forwarding)

* `SEND(dst, blob)` → the server rewrites the first 32 bytes to the sender's ID and queues `DELIVER(src, blob)`
  on the receiver. Max blob = `max_frame − 33`. Frames from one sender to one receiver are delivered **in order**.
* Each connection has a bounded queue (`queue_frames`, default 256). If it is full the server waits up to
  `send_timeout_ms` (default 15 s), back-pressuring the sender's TCP stream; then replies `SEND_FAIL(dst, 11)`.
* Rate limit per connection: token buckets of `rate_bytes_per_sec` (64 MiB/s) and `rate_frames_per_sec` (5000);
  excess traffic is **delayed**, not dropped.
* Large transfers: split into ≤ max_payload chunks at the client's Noise layer (each chunk its own Noise transport
  message, ≤ 65535 bytes is the natural choice since Noise messages are capped at 64 KiB).

## 6. 心跳与重连 (heartbeat & reconnect)

* Client sends `PING` every `heartbeat_secs` (from AUTH_OK, default 25 s). The server answers `PONG`.
  The server closes connections silent for `idle_timeout_secs` (default 90 s) with `ERROR(13)`.
  A client that gets no PONG within 2 × heartbeat should reconnect.
* Reconnect with exponential back-off: 1 s, 2 s, 4 s … max 60 s, ±20 % jitter. After reconnect the new
  `PEER_LIST` is authoritative: drop relay presence for peers not in it.
* Logging in with an ID that is already online replaces the old session (`ERROR(6)` to the old one); no
  PEER_LEAVE is emitted, others receive a fresh PEER_JOIN (upsert).

## 7. 客户端集成指引 (client integration guide)

1. **Transport abstraction.** Treat "relay path to peer X" as just another byte pipe: whatever the LAN code writes to
   its TCP socket (Noise handshake + transport frames incl. their 2-byte length prefixes) goes into `SEND(X, bytes)`;
   bytes from `DELIVER(X, …)` are fed to the same Noise/session parser. No feature needs relay-specific code.
2. **Contact list.** On `PEER_LIST` / `PEER_JOIN` add the peer to the *same* contact list as LAN peers, marked
   with a small "经服务器" badge, and start the Noise XX handshake over the relay. On `PEER_LEAVE` remove only the
   relay path.
3. **Direct preferred.** When LAN discovery finds the same fingerprint, open a direct session and move traffic
   to it; keep the relay path as standby. Optionally peers can exchange their LAN/public endpoints over the
   encrypted relay channel and attempt a direct TCP connection (hole punching is out of scope for v1).
4. **Settings.** `relay.enabled` (default false), `relay.host`, `relay.port` (default 9443), `relay.passphrase`.

Reference implementations: `relay-server/src/client.rs` (Rust, tokio) and `relay-server/c-client/relay_client.c`
(C99 + Monocypher 4 + BSD sockets, ~250 lines) — both are exercised against each other in CI.

## 8. 已知限制 (known limitations, v1)

* No TLS: metadata (IDs, `info`, sizes) is visible on the wire. Put sensitive data only inside Noise.
* peer_id ownership is not proven to the relay → an authorised (passphrase-holding) client could squat an ID
  and cause presence DoS. v2 idea: prove ownership with a Noise-NK-style `DH(peer_static, server_ephemeral)` MAC.
* No store-and-forward: offline peers get `SEND_FAIL(10)`.

## 9. 状态接口 (status endpoint)

`relay-server --status [--config f | --port p]` (or `GET http://127.0.0.1:<port+1>/status`, local only) returns JSON:
`{"name","version","protocol","pid","bind","port","online","connections","connections_total","auth_failures",
"frames_forwarded","bytes_forwarded","send_failures","protocol_errors","uptime_secs","started_unix"}`.

## Appendix: LSM messenger client conventions (v1)

The relay server never parses these; they are how the LAN Secure Messenger clients
(Rust `lan-core/src/engine/relay.rs`, C core `src/relay.c`) use the relay.

* **peer_id** = SHA-256 of the client's static X25519 public key.
* **AUTH info** = UTF-8 `LSM5\t<fingerprint>\t<display name>\t<app version>` (≤ 512 bytes).
  Clients ignore PEER_LIST/PEER_JOIN entries whose info does not start with `LSM5`.
* **Stream layer** inside every SEND/DELIVER blob:

  | offset | size | field |
  |---|---|---|
  | 0 | 1 | kind: 0 DATA, 1 OPEN, 2 CLOSE |
  | 1 | 4 | session id (u32 BE, random, non-zero) |
  | 5 | n | bytes |

  A session carries exactly the byte stream a LAN TCP connection would carry
  (Noise XX handshake, then length-prefixed encrypted frames), so every
  messenger feature works unchanged. Only the peer with the numerically
  smaller peer_id sends OPEN (and is the Noise initiator); OPEN replaces any
  previous session with that peer. DATA for an unknown session is answered with
  CLOSE. Either side sends CLOSE when its link ends. DATA chunks are ≤ 60000 bytes.
* **Path preference**: LAN direct is always preferred. While a peer is on the relay
  the client keeps dialing it on the LAN when it is announced there; a direct link
  replaces the relay link (the relay session is closed). When the direct link drops,
  the client opens a relay session again within ~2 s.
