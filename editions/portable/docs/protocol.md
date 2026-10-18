# LAN Secure Messenger — v0.5 wire protocol (PROTO_VERSION = 5)

Extracted from the Rust source of `boochain/lan-secure-messenger`, branch `refactor/v0.5`
(tag v0.5.0-beta.2, commit 1504404): `lan-core/src/{wire,crypto,link,discovery,identity,group}.rs`,
`lan-core/src/engine.rs`, `lan-core/src/engine/files.rs`. The XP edition implements exactly this.

## 1. Serialization (bincode 1.x, default `serialize`)

All structures are serde + `bincode::serialize` (fixint, little-endian). Decoding uses
`DefaultOptions::new().with_fixint_encoding().allow_trailing_bytes().with_limit(1 MiB)`,
which accepts the same bytes.

| type | encoding |
|---|---|
| `u16/u32/u64/i64` | fixed width little-endian |
| `bool` | 1 byte, 0/1 |
| `String` | `u64` byte length + UTF-8 bytes |
| `Vec<u8>` | `u64` length + bytes |
| `Vec<String>` / `Vec<Uuid>` | `u64` count + elements |
| `[u8; 32]` | 32 raw bytes (tuple, no length) |
| `Uuid` | `serialize_bytes` (non-human-readable): `u64` = 16, then 16 bytes |
| `Option<T>` | 1 byte tag (0 = None, 1 = Some) then T |
| enum | `u32` variant index, then fields in declaration order |
| struct | fields in declaration order, no tags |

## 2. Identity

* X25519 static key (Noise static), Ed25519 signing key (announces, group invites/rekeys).
* Fingerprint = first 8 bytes of `SHA-256(x25519_public)`, uppercase hex, groups of 4 joined
  with `-`, e.g. `1A2B-3C4D-5E6F-7081`. Fingerprints are the peer ids everywhere (contacts,
  group member lists, conversation ids `dm:<fp>`, `group:<uuid>`).

## 3. Discovery (UDP, default port 41234)

Packet = `"LSM5"` (4 bytes) ‖ bincode(`SignedAnnounce { body: Vec<u8>, sig: Vec<u8> }`),
where `body` = bincode(`Announce`) and `sig` = Ed25519(body) (64 bytes).

```
Announce { proto: u16 (=5), x25519_pk: [u8;32], ed25519_pk: [u8;32], name: String,
           tcp_port: u16, disco_port: u16, nonce: u64 (random), ts_ms: i64 (unix ms),
           want_reply: bool (always true) }
```

* Receivers drop packets with wrong magic, `proto != 5`, `sig.len() != 64` or bad signature.
  No timestamp check. Own fingerprint ignored.
* Sent every `announce_interval_secs` (default 3 s) from a socket bound to each usable IPv4
  interface address to: the interface's subnet broadcast, `255.255.255.255` (if different),
  and multicast `239.255.42.99` (TTL 1), all on the discovery port; also to configured
  unicast `announce_targets` (`host[:port]`).
* On receiving an announce with `want_reply` and `disco_port != 0`, the receiver unicasts its
  own announce to `src_ip:disco_port`, at most once per 10 s per source IP.
* An announce is only a dial hint: dial `src_ip:tcp_port`. Presence = authenticated TCP link.

## 4. TCP link (default listen port 41235, fallback ephemeral)

Raw framing for every message (handshake and transport): `u32` **big-endian** length ‖ bytes,
length must be 1..=65539.

### Noise
* Pattern: `Noise_XX_25519_ChaChaPoly_BLAKE2s`, empty prologue, no PSK. The dialer is the
  initiator. All three handshake payloads are empty:
  `-> e` (32 B) · `<- e, ee, s, es` (32+48+16 = 96 B) · `-> s, se` (48+16 = 64 B).
* Transport uses snow's *stateless* transport: per-direction nonce counter starting at 0,
  incremented per message; ChaChaPoly nonce = 4 zero bytes ‖ LE64(counter), empty AD.
  Initiator sends with k1 / receives with k2 of `Split()`.
* Plaintext of one transport message ≤ 65535−16 bytes; one message = one bincode value.
* Handshake + Hello must complete within 10 s. A link to our own fingerprint is dropped.

### Hello (first transport message in each direction)
```
Hello { proto: u16 (=5), app_version: String, name: String, ed25519_pk: [u8;32],
        listen_port: u16 }   ‖ caps: u32 LE  (optional trailer)
```
`proto != 5` ⇒ close. The remote fingerprint comes from the Noise remote static key, the
Ed25519 key from Hello (stored in the contact, used to verify group signatures).

**Capabilities.** After the bincode `Hello` a peer may append `caps: u32` (little-endian).
Older peers ignore trailing bytes (bincode `allow_trailing_bytes`) and send no trailer, so a
missing trailer means `caps = 0`. Bits: `CAP_RESUME = 1` (byte-offset file resume, §8.1).
A frame that needs a capability is only ever sent to a peer that announced it.

### Frames (all subsequent transport messages): `enum Frame`
| idx | variant | fields |
|---|---|---|
| 0 | Ping | ts_ms: i64 |
| 1 | Pong | ts_ms: i64 (echo) |
| 2 | Name | name: String |
| 3 | Msg | id: Uuid, payload: Payload |
| 4 | Ack | id: Uuid |
| 5 | FileOffer | tid: Uuid, msg_id: Uuid, name: String, size: u64, mime: String, sha256: [u8;32], group_id: Option<Uuid>, ts_ms: i64 |
| 6 | FileAccept | tid |
| 7 | FileReject | tid, reason: String |
| 8 | FileChunk | tid, offset: u64, data: Vec<u8> |
| 9 | FileProgress | tid, received: u64 |
| 10 | FileEnd | tid |
| 11 | FileComplete | tid, ok: bool, error: String |
| 12 | FileCancel | tid, reason: String |
| 13 | PinPair | pin: String |
| 14 | PinResult | ok: bool |
| 15 | Bye | — |
| 16 | FileResume | tid, offset: u64, prefix_sha256: [u8;32] — only to CAP_RESUME peers (§8.1) |

`enum Payload` (inside `Msg`):
| idx | variant | fields |
|---|---|---|
| 0 | Text | body: String, ts_ms: i64 |
| 1 | GroupText | group_id: Uuid, key_epoch: u64, sealed: Vec<u8>, ts_ms: i64 |
| 2 | GroupInvite | group_id, name: String, creator_fp: String, members: Vec<String>, group_key: [u8;32], created_ms: i64, key_epoch: u64, sig: Vec<u8> |
| 3 | GroupRekey | group_id, members: Vec<String>, group_key: [u8;32], key_epoch: u64, removed_fp: String, sig: Vec<u8> |
| 4 | GroupLeave | group_id |
| 5 | Read | ids: Vec<Uuid> |

## 5. Link behaviour
* Heartbeat: `Ping` every 5 s; peer answers `Pong` with the same ts. No received frame for
  20 s ⇒ link dead, closed, redial after 0.3–1 s then exponential backoff (cap 30 s).
* Duplicate links (both sides dialed): links initiated by the side with the lexicographically
  smaller fingerprint are preferred. A new link replaces the existing one if the existing one
  had no rx for 12 s, or if both links are "the same direction preference" (peer
  reconnected); otherwise the preferred one wins. `Bye` = graceful close.
* Redial targets: trusted contacts' last known address, peers announced within 60 s, peers
  with pending outbox, and manually added addresses (add-by-IP, every 5–30 s).
* Display-name change is pushed with `Name`.

## 6. Reliable messages (outbox, exactly-once)
* `Msg{id,payload}` is put in a per-peer outbox and sent when the link is up; re-sent on every
  reconnect until `Ack{id}` arrives. Receiver always replies `Ack{id}`, but processes a given
  id only once (seen-set of 20 000 ids + history). Message states:
  queued → sent → delivered (Ack) → read (`Read` receipt).
* Read receipts: `Msg{ id: new uuid, payload: Read{ids} }` sent best-effort (not queued) when
  the user opens the conversation; ids are the incoming message ids.
* Group messages fan out as one `Msg` per member with the **same** message id.

## 7. Groups
* Group = `{id: Uuid v4, name, creator_fp, members: [fp..] (incl. creator, sorted, dedup),
  group_key: 32 random bytes, created_ms, key_epoch}`.
* `GroupText.sealed` = nonce(12 random) ‖ ChaCha20-Poly1305(group_key, nonce, AD = empty,
  UTF-8 body) (RFC 8439). Receivers drop texts from non-members.
* Invite signature: Ed25519 by the creator over
  `uuid(16) ‖ name ‖ 0 ‖ creator_fp ‖ 0 ‖ for m in sorted(members): m ‖ 0 ‖ group_key(32) ‖ LE64(created_ms)`.
  Accepted only if `creator_fp` = sending peer, signature valid against that peer's Hello
  Ed25519 key, sender is **trusted**, and we are in `members`. Invites are sent to every
  member (also to re-add members) carrying the current key_epoch.
* Rekey signature over
  `"REKEY1" ‖ uuid(16) ‖ for m in sorted(members): m ‖ 0 ‖ group_key ‖ LE64(key_epoch) ‖ removed_fp`.
  Only from the creator; ignored if `key_epoch` is older. If we are not in `members` the
  group is deleted locally ("removed").
* Removing a member: creator drops the member, generates a new key, `key_epoch += 1`, sends
  `GroupRekey` to remaining members **and** to the removed one (so it learns it was removed).
* `GroupLeave{group_id}` to all members: non-creators drop the sender; the creator performs a
  remove + rekey.

## 8. Files
* Sender: `FileOffer` (sha256 of whole file, mime guess, optional group id, msg_id is the
  chat message id; for a group one offer per member with distinct tid, same msg_id).
* Receiver: rejects (`FileReject`) if the sender is not trusted, the size exceeds its limit,
  or the group is unknown/sender is not a member; otherwise `FileAccept` (auto-accept
  default on) and writes to a `.part` file.
* Sender streams `FileChunk` of 32 KiB (`CHUNK = 32*1024`) at increasing contiguous
  offsets, keeping at most 4 MiB unacknowledged (`offset - received > 4 MiB` ⇒ wait for
  `FileProgress`, 30 s timeout), then `FileEnd`.
* Receiver: out-of-order offset ⇒ fail. Sends `FileProgress{received}` every 256 KiB and at
  the end; on `FileEnd` checks size + SHA-256, renames to a unique name in the download dir,
  replies `FileComplete{ok, error}`.
* `FileCancel` from either side aborts. Pending offers are re-sent after reconnect. A known
  `tid` re-offered is ignored by old peers; CAP_RESUME peers handle it as in §8.1.

### 8.1 Resume (CAP_RESUME, both peers)
Identical in lan-core (Rust) and the portable C core.
* **Link loss** (either path, LAN or relay, or link replaced): an active transfer is *paused*
  instead of failed — state goes back to `offered`, error text `连接中断，重连后自动续传`.
  The receiver keeps its `.part` file and its running SHA-256 state; the chat message is not
  marked failed.
* **Reconnect**: the sender re-sends `FileOffer` with the same `tid` for every paused
  outgoing transfer (as for any pending offer).
* **Receiver**, on a re-offer of a `tid` it accepted before:
  * already `done` → reply `FileComplete{ok:true}` again (the first one may have been lost);
  * has `n` bytes (0 < n < size) → reply `FileResume{tid, offset:n, prefix_sha256: SHA-256(first n bytes)}`
    and keep appending;
  * otherwise → `FileAccept` (restart from 0, fresh `.part`).
* **Sender**, on `FileResume`: hashes the first `offset` bytes of its file; if equal to
  `prefix_sha256` it continues with `FileChunk` at `offset` (flow-control credit starts at
  `offset`), else it restarts at 0. A receiver that sees a first chunk at offset 0 while
  holding data truncates its `.part` and restarts.
* The final check is unchanged: size + SHA-256 of the whole file vs `FileOffer.sha256`.
* **Old peers** (no CAP_RESUME): never receive `FileResume`; link loss fails the transfer as
  before and a manual retry restarts from 0.
* Implementations expose the offset the last (re)start continued from (`Transfer.resumed_from`
  in lan-core, `lsm_transfer.resumed_from` / CLI `XFER … resumed=<n>` in the C core).
* **Receiver restart.** Every resumable download has a sidecar `<part>.resume` next to its
  `.part` (written on accept, removed on done/fail/cancel), identical in both cores:
  ```
  LSMRESUME 1
  tid <uuid>
  peer <fingerprint>
  msg <chat message id>
  conv <dm:… | group:…>
  size <u64>
  sha <hex SHA-256 of the whole file>
  mime <mime>
  name <file name>
  ```
  The offset and hash state are not stored. On start-up the receiver re-creates the transfer
  (state `offered`, accepted) and re-hashes the `.part`, which takes its length as the offset.
  It does this off the network path: Rust uses a blocking-pool task, C a chunked job of
  2 MiB per loop turn. A re-offer that arrives before the hash finishes is deferred. Then
  the receiver resumes with `FileResume` as after a link loss.
* The sender's prefix check runs off the network path too: Rust in the async send task, C as
  a chunked job.
* Sender restart is persisted too. When an offer is created, the sender writes
  `<data>/outgoing/<tid>.out`; it removes the file once the transfer is finished (done, failed or
  cancelled). The format is the same in Rust and C:
  ```
  LSMOUT 1
  tid <hex>   peer <fp>   msg <id>   conv <…>   size <u64>   sha <hex>   mime <mime>
  mtime <unix secs of the source file>   offer_ts <unix secs>   path <absolute source path>   name <file name>
  ```
  There is one key/value per line; the inline layout above is only for brevity. On start-up
  each record re-creates the transfer (state `offered`, out). If the source is missing, the
  transfer fails with `源文件不存在: <path>`. If its size or mtime differs, it fails with
  `源文件已更改: <path>`. Otherwise the sender re-offers the same tid as soon as the peer's link
  comes up, and the receiver answers with `FileResume` from its own `.part`.
* Expiry: at start-up both cores delete anything abandoned for more than 30 days, judged by
  mtime. That covers `.resume` sidecars with their `.part` files, bare `.<name>*.part` files
  with no sidecar, and `outgoing/*.out` records (the transfer is marked failed:
  `发送记录已过期 (30 天)`).
* Fast redial (checked in the code of both cores):
  - Rust: at start-up every contact's `next_attempt` is set to now. The 500 ms engine tick
    fires at once, so trusted peers with remembered addresses are dialed on the first tick.
    The relay client gets `Configure` before the engine loop starts, so it connects at once.
    Every `PeerJoin` from the relay opens a session at once; the 2 s periodic `Open` is only
    the retry path.
  - C: trusted contacts with a remembered address are seeded into the redial table before the
    first redial pass, so the first loop turn (≤ 50 ms) dials them. The relay's
    `next_attempt` is 0, so it connects on the first tick. Relay peers start with
    `next_try = 0`, so the first session open is immediate.
  - The matrix asserts that resume starts < 10 s after a kill -9 + restart of the receiver or
    the sender. Measured: 0.3–3.2 s.

## 9. PIN pairing
1. Peer A calls `pin_offer`: random 6-digit PIN, valid 300 s, max 5 wrong tries.
2. User of B enters it: B sends `PinPair{pin}` to A over the established link.
3. A replies `PinResult{ok}`; on success A marks B trusted and clears the PIN; B marks A trusted
   when it receives `PinResult{ok: true}`.
Trust is required for group invites and incoming files (`auto_trust` default off).
