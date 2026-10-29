# v0.6 代码审计 / Code audit (unify/v0.6)

Scope: Rust core (`lan-core`, `lan-daemon`, `lan-cli`), relay server (`relay-server`), C core
(`editions/portable/src/core.c` + `relay.c` + `resume.c`, `xcrypto.c`, `cli.c`). UI code (WinUI, XP Win32, GTK, Android
Compose) was only read for the feature-parity table, not changed. Date: 2026-10-29 (Asia/Shanghai).

## 1. Rust ↔ C protocol diff

### 1.1 Peer link (Noise XX, bincode fixint)

| # | Frame | Rust `wire::Frame` | C `core.c` | Notes |
|---|---|---|---|---|
| 0 | Ping{ts} | ✓ | ✓ | |
| 1 | Pong{ts} | ✓ | ✓ | |
| 2 | Name | ✓ | ✓ | |
| 3 | Msg{id, Payload} | ✓ | ✓ | Payload 0–5 (Text, GroupText, GroupInvite, GroupRekey, GroupLeave, Read) on both |
| 4 | Ack | ✓ | ✓ | |
| 5 | FileOffer | ✓ | ✓ | same field order (tid, msg_id, name, size, mime, sha256, Option<group>, ts) |
| 6 | FileAccept | ✓ | ✓ | |
| 7 | FileReject | ✓ | ✓ | |
| 8 | FileChunk | ✓ | ✓ | receiver requires offset == transferred on both |
| 9 | FileProgress | ✓ | ✓ | drives the 4 MiB credit window on both |
| 10 | FileEnd | ✓ | ✓ | |
| 11 | FileComplete | ✓ | ✓ | |
| 12 | FileCancel | ✓ | ✓ | |
| 13 | PinPair | ✓ | ✓ | |
| 14 | PinResult | ✓ | ✓ | |
| 15 | Bye | ✓ | ✓ | |
| 16 | FileResume{tid, offset, prefix_sha256} | ✓ | ✓ | only to peers announcing CAP_RESUME (=1) on both |

**Differences found**

| ID | Difference | Severity | Status |
|---|---|---|---|
| P1 | Unknown/undecodable frame: C ignored it, Rust dropped the link (a newer peer would cause a reconnect loop with Rust peers only) | Medium (forward compat) | **Fixed**: Rust ignores it too (`LinkReader::recv_lenient`) |
| P2 | Relay `AUTH_OK.max_frame`: C clamped only the lower bound (4096), Rust used `max(1024)`; neither had an upper bound → a hostile server could make a client allocate up to 4 GiB per frame | Medium (DoS) | **Fixed**: both clamp to [4096, 16 MiB]; documented in relay-protocol.md |
| P3 | Relay `LIST` (0x30): C sends it, Rust relies on the pushed PEER_LIST/JOIN/LEAVE only | Info | Not a bug (server pushes on auth); left as is |
| P4 | `sanitize_filename` rules differed (Windows device names, control chars, leading dot) | Medium (path safety) | **Fixed earlier** (80948d8), shared test vectors |
| P5 | Mid-transfer relay↔direct switch: C lost queued bytes, Rust had two races | High (data loss → 失败) | **Fixed** (C c123207, Rust 9e13825); matrix checks 4 directions per pair |

Relay frame types (HELLO 0x00, AUTH 0x01, AUTH_OK 0x02, AUTH_FAIL 0x03, SEND 0x10, DELIVER 0x11, SEND_FAIL 0x12,
PING 0x20, PONG 0x21, LIST 0x30, PEER_LIST 0x40, JOIN 0x41, LEAVE 0x42, ERROR 0x50, BYE 0x7f) are identical in
the server, the Rust client and the C client.

## 2. Feature parity (4 platforms)

Evidence: grep of each UI (keyword hits, not a click-through of each app; verify the — cells before planning) + the core APIs (`core.h` for C, `api.rs` for Rust); "core only" = core supports it but
the UI has no entry point. ✓ present, — missing, n/a not applicable.

| Feature | Windows WinUI (Rust) | Windows XP (C, Win32) | Linux GTK 4 (C) | Android (C, Compose) |
|---|---|---|---|---|
| DM text, files, images | ✓ | ✓ | ✓ | ✓ |
| Groups (signed member list, rekey) | ✓ | ✓ | ✓ | ✓ |
| PIN pairing / TOFU trust | ✓ | ✓ | ✓ | ✓ |
| Block | ✓ | ✓ | ✓ | ✓ |
| Mute (免打扰) | ✓ | — | — | — |
| Read receipts | ✓ (status) | ✓ | ✓ | ✓ |
| Resume (断点续传) | ✓ | ✓ | ✓ | ✓ |
| 中转服务器 client | ✓ | ✓ | ✓ | ✓ |
| Message search | ✓ | ✓ (list filter) | ✓ (list filter) | ✓ |
| Encrypted history + backup/restore (Argon2id) | ✓ | — (no C API) | — | — |
| Tray / background | ✓ | ✓ | — | n/a (service) |
| System notifications | ✓ | — | ✓ | ✓ |
| Drag & drop send | ✓ | ✓ | ✓ | n/a (share sheet) |
| Firewall setup | ✓ | ✓ (netsh) | n/a | n/a |
| Diagnostics page | ✓ | — | — | — |
| Update check | ✓ | — | — | — |
| Light / dark theme | ✓ | ✓ | ✓ | ✓ |

Gaps to schedule (C core work first, then UI by the UI worker): mute, encrypted backup/restore, diagnostics,
update check; XP notifications (balloon tips are available on XP).

## 3. Security review

### 3.1 Tools

| Tool | Target | Result |
|---|---|---|
| `cargo clippy --workspace --all-targets` | Rust core/daemon/cli | 25 warnings, all style/complexity (`type_complexity`, `sort_by_key`, `field_reassign_with_default`, …); **no correctness/suspicious lints** |
| `cargo clippy` | relay-server | clean |
| `cargo test -p lan-core` | | 15/15 pass |
| `cppcheck 2.22 --enable=warning,portability,performance` | C core + cli | 67× `nullPointerOutOfMemory`, 1× `ctunullpointer`, 1× `nullPointerArithmeticOutOfMemory` → **fixed**; remaining: 4× in `cli.c` argv (CLI only), 1 syntaxError = cppcheck not understanding Monocypher's C++ namespace macro (FP) |
| `gcc 14 -fanalyzer` | core.c, xcrypto.c, cli.c | 4 findings: `b_put` memcpy with NULL/0 (**fixed**), event ring `xstrdup` "leak" (FP: freed on overwrite/stop), BLAKE2s `v[12]` uninitialised (FP: set by the loop), `resume.c` NULL file handle (FP: guarded by `hashing`); implicit-declaration warnings only appear when compiling the `#include`d units standalone |
| `-Wall -Wextra` | C core | only `-Wmisleading-indentation` (one-line style), no real warnings |

### 3.2 Findings

| ID | Finding | Severity | Status |
|---|---|---|---|
| S1 | C core: raw `malloc/realloc/calloc` results used unchecked (67 sites) → NULL deref on OOM | Low (local DoS) | **Fixed**: `xmalloc/xrealloc/xcalloc` abort on OOM (like Rust) |
| S2 | Relay `max_frame` unbounded on the client (P2) | Medium | **Fixed** |
| S3 | Incoming file names (P4) | Medium | **Fixed** earlier |
| S4 | Unknown frame → link drop (P1) | Low | **Fixed** |
| S5 | `unsafe` in Rust: 9 sites, all Windows FFI (named-pipe SDDL `D:P(A;;GA;;;<user SID>)`, `IsUserAnAdmin`, `ShellExecuteExW` runas, `MessageBoxW`) | Info | Reviewed, OK |
| S6 | 48 `unwrap()` in lan-core non-test code; the relay AUTH_OK parse ones follow a length check. **Not all individually reviewed** in this pass | Info | **Open**: review the ones on network input |
| S7 | Relay server: per-IP and total connection caps, auth-failure rate limit, 1 KiB pre-auth limit, hard 16 MiB frame ceiling; passphrase never logged; transport content is end-to-end encrypted between peers (server sees only fingerprints and sizes) | Info | OK |
| S8 | Relay deploy scripts never change the firewall unless asked (default No / `--open-firewall`), uninstall removes only rules recorded in the state file | Info | OK (tested in relay-server CI) |
| S9 | `cargo audit` / dependency CVE scan not run (tool not installed on the audit box) | — | **Open**: add `cargo audit` to CI |

## 4. Follow-ups

1. Add `cargo audit` and `cppcheck` as CI jobs (fail on new correctness findings).
2. C core API for mute and encrypted backup/restore, then the UI entries (UI worker).
3. Remove `editions/portable/reference/v0.5` once nothing needs the frozen v0.5 daemon (interop.py backward-compat test only).
