//! In-process multi-node tests of the v0.5 engine over real loopback TCP/UDP.

use lan_core::engine::{start, EngineHandle, EngineOptions};
use lan_core::ConfigPaths;
use serde_json::{json, Value};
use std::future::Future;
use std::time::{Duration, Instant};

struct Node {
    h: EngineHandle,
    fp: String,
    dir: tempfile::TempDir,
}

fn free_udp_port() -> u16 {
    std::net::UdpSocket::bind("127.0.0.1:0").unwrap().local_addr().unwrap().port()
}

async fn node(name: &str, extra: Value) -> Node {
    let dir = tempfile::tempdir().unwrap();
    let mut ov = json!({
        "display_name": name,
        "listen_port": 0,
        "discovery_enabled": false,
        "multicast_enabled": false,
        "auto_trust": true,
        "announce_interval_secs": 1,
        "exclude_interfaces": [" | "],
        "download_dir": dir.path().join("dl").display().to_string(),
    });
    lan_core::engine::merge_pub(&mut ov, &extra);
    let h = start(EngineOptions { paths: ConfigPaths::from_base(dir.path()), ipc_endpoint: "test".into(), overrides: Some(ov) }).await.unwrap();
    let fp = h.fingerprint.clone();
    Node { h, fp, dir }
}

impl Node {
    async fn call(&self, m: &str, p: Value) -> Value {
        match self.h.call(m, p.clone()).await {
            Ok(v) => v,
            Err(e) => panic!("{m}({p}) failed: {}", e.message),
        }
    }
    async fn snap(&self) -> Value {
        self.call("get_snapshot", json!({})).await
    }
    async fn port(&self) -> u16 {
        self.snap().await["me"]["listen_port"].as_u64().unwrap() as u16
    }
    async fn peer(&self, fp: &str) -> Option<Value> {
        self.snap().await["peers"].as_array().unwrap().iter().find(|p| p["fingerprint"] == fp).cloned()
    }
    async fn online(&self, fp: &str) -> bool {
        self.peer(fp).await.map(|p| p["online"] == true).unwrap_or(false)
    }
    async fn msgs(&self, conv: &str) -> Vec<Value> {
        self.call("get_messages", json!({"conversation": conv, "limit": 2000})).await.as_array().unwrap().clone()
    }
    async fn send(&self, to: &Node, body: &str) -> Value {
        self.call("send_text", json!({"conversation": format!("dm:{}", to.fp), "body": body})).await
    }
}

async fn wait<F, Fut>(what: &str, secs: u64, mut f: F)
where
    F: FnMut() -> Fut,
    Fut: Future<Output = bool>,
{
    let end = Instant::now() + Duration::from_secs(secs);
    loop {
        if f().await {
            return;
        }
        if Instant::now() > end {
            panic!("timeout waiting for: {what}");
        }
        tokio::time::sleep(Duration::from_millis(100)).await;
    }
}

async fn both_online(a: &Node, b: &Node, secs: u64) {
    wait("both online", secs, || async { a.online(&b.fp).await && b.online(&a.fp).await }).await;
}

async fn delivered(n: &Node, conv: &str, id: &str) -> bool {
    n.msgs(conv).await.iter().any(|m| m["id"] == id && (m["status"] == "delivered" || m["status"] == "read"))
}

async fn has_body(n: &Node, conv: &str, body: &str) -> bool {
    n.msgs(conv).await.iter().any(|m| m["body"] == body)
}

async fn chat_both_ways(a: &Node, b: &Node) {
    let m1 = a.send(b, "你好 B，来自 A").await;
    let m2 = b.send(a, "hi A, from B").await;
    let (ca, cb) = (format!("dm:{}", b.fp), format!("dm:{}", a.fp));
    wait("B got A's message", 10, || has_body(b, &cb, "你好 B，来自 A")).await;
    wait("A got B's message", 10, || has_body(a, &ca, "hi A, from B")).await;
    wait("A's msg delivered", 10, || delivered(a, &ca, m1["id"].as_str().unwrap())).await;
    wait("B's msg delivered", 10, || delivered(b, &cb, m2["id"].as_str().unwrap())).await;
}

#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn manual_add_by_ip_only() {
    let a = node("A", json!({})).await;
    let b = node("B", json!({})).await;
    let pb = b.port().await;
    a.call("add_peer", json!({"address": format!("127.0.0.1:{pb}")})).await;
    both_online(&a, &b, 10).await;
    chat_both_ways(&a, &b).await;
    // Manual address persisted.
    let raw = std::fs::read_to_string(a.dir.path().join("config/contacts.json")).unwrap();
    assert!(raw.contains(&format!("127.0.0.1:{pb}")));
}

#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn one_way_discovery_gives_symmetric_presence() {
    let pa = free_udp_port();
    let pb = free_udp_port();
    // A listens for announces but sends none (no interfaces, no targets).
    let a = node("A", json!({"discovery_enabled": true, "discovery_port": pa})).await;
    // B cannot receive UDP (simulated firewall) but its announces reach A.
    let b = node("B", json!({"discovery_enabled": true, "discovery_listen": false, "discovery_port": pb, "announce_targets": [format!("127.0.0.1:{pa}")]})).await;
    both_online(&a, &b, 15).await;
    let pa_view = a.peer(&b.fp).await.unwrap();
    let pb_view = b.peer(&a.fp).await.unwrap();
    assert_eq!(pa_view["direction"], "outbound", "A should have dialed B");
    assert_eq!(pb_view["direction"], "inbound");
    chat_both_ways(&a, &b).await;
}

#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn reconnect_after_connection_killed() {
    let a = node("A", json!({})).await;
    let b = node("B", json!({})).await;
    a.call("add_peer", json!({"address": format!("127.0.0.1:{}", b.port().await)})).await;
    both_online(&a, &b, 10).await;
    chat_both_ways(&a, &b).await;
    for round in 0..3 {
        a.call("debug_drop_connection", json!({"fingerprint": b.fp})).await;
        // Sent while the link is down / reconnecting: must be queued and delivered after reconnect.
        let body = format!("during outage {round}");
        let m = a.send(&b, &body).await;
        let mb = b.send(&a, &format!("reply {round}")).await;
        both_online(&a, &b, 15).await;
        let (ca, cb) = (format!("dm:{}", b.fp), format!("dm:{}", a.fp));
        wait("queued msg arrives", 15, || has_body(&b, &cb, &body)).await;
        wait("queued msg delivered", 15, || delivered(&a, &ca, m["id"].as_str().unwrap())).await;
        wait("B reply delivered", 15, || delivered(&b, &cb, mb["id"].as_str().unwrap())).await;
    }
    // No duplicates.
    let msgs = b.msgs(&format!("dm:{}", a.fp)).await;
    let n = msgs.iter().filter(|m| m["body"] == "during outage 0").count();
    assert_eq!(n, 1, "duplicate delivery");
}

#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn simultaneous_dial_tie_break() {
    for _ in 0..3 {
        let a = node("A", json!({})).await;
        let b = node("B", json!({})).await;
        let (pa, pb) = (a.port().await, b.port().await);
        let (r1, r2) = tokio::join!(
            a.h.call("add_peer", json!({"address": format!("127.0.0.1:{pb}")})),
            b.h.call("add_peer", json!({"address": format!("127.0.0.1:{pa}")}))
        );
        r1.unwrap();
        r2.unwrap();
        both_online(&a, &b, 10).await;
        tokio::time::sleep(Duration::from_secs(3)).await;
        let va = a.peer(&b.fp).await.unwrap();
        let vb = b.peer(&a.fp).await.unwrap();
        assert!(va["online"] == true && vb["online"] == true);
        assert_ne!(va["direction"], vb["direction"], "both ends must agree on the same link");
        let lower_is_a = a.fp < b.fp;
        assert_eq!(va["direction"], if lower_is_a { "outbound" } else { "inbound" });
        chat_both_ways(&a, &b).await;
    }
}

#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn file_transfer_and_cancel() {
    let a = node("A", json!({})).await;
    let b = node("B", json!({})).await;
    a.call("add_peer", json!({"address": format!("127.0.0.1:{}", b.port().await)})).await;
    both_online(&a, &b, 10).await;
    let src = a.dir.path().join("big 文件.bin");
    let data: Vec<u8> = (0..(12 * 1024 * 1024 + 123)).map(|i| (i * 31 % 251) as u8).collect();
    std::fs::write(&src, &data).unwrap();
    let t = a.call("send_file", json!({"conversation": format!("dm:{}", b.fp), "path": src.display().to_string()})).await;
    let tid = t["id"].as_str().unwrap().to_string();
    wait("sender done", 30, || async {
        a.snap().await["transfers"].as_array().unwrap().iter().any(|x| x["id"] == tid.as_str() && x["state"] == "done")
    })
    .await;
    let bt = b.snap().await["transfers"].as_array().unwrap().iter().find(|x| x["id"] == tid.as_str()).cloned().unwrap();
    assert_eq!(bt["state"], "done");
    let got = std::fs::read(bt["path"].as_str().unwrap()).unwrap();
    assert_eq!(got.len(), data.len());
    assert!(got == data, "content mismatch");
    // Image message metadata + message status.
    let msgs = a.msgs(&format!("dm:{}", b.fp)).await;
    assert!(msgs.iter().any(|m| m["kind"] == "file" && m["status"] == "delivered"));
    // Cancel mid-way then retry.
    let t2 = a.call("send_file", json!({"conversation": format!("dm:{}", b.fp), "path": src.display().to_string()})).await;
    let tid2 = t2["id"].as_str().unwrap().to_string();
    wait("active", 10, || async {
        a.snap().await["transfers"].as_array().unwrap().iter().any(|x| x["id"] == tid2.as_str() && (x["state"] == "active" || x["state"] == "done"))
    })
    .await;
    a.call("cancel_transfer", json!({"id": tid2})).await;
    let st = a.snap().await["transfers"].as_array().unwrap().iter().find(|x| x["id"] == tid2.as_str()).cloned().unwrap();
    if st["state"] == "cancelled" {
        wait("receiver sees cancel", 10, || async {
            b.snap().await["transfers"].as_array().unwrap().iter().any(|x| x["id"] == tid2.as_str() && x["state"] == "cancelled")
        })
        .await;
        let r = a.call("retry_transfer", json!({"id": tid2})).await;
        let tid3 = r["id"].as_str().unwrap().to_string();
        wait("retry done", 30, || async {
            a.snap().await["transfers"].as_array().unwrap().iter().any(|x| x["id"] == tid3.as_str() && x["state"] == "done")
        })
        .await;
    }
    // No .part leftovers.
    let dl = b.dir.path().join("dl");
    let leftovers: Vec<_> = std::fs::read_dir(&dl).unwrap().filter_map(|e| e.ok()).filter(|e| e.file_name().to_string_lossy().ends_with(".part")).collect();
    assert!(leftovers.is_empty(), "leftover part files");
}

#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn file_checksum_mismatch_retries() {
    lan_core::engine::test_force_sha_fail(1);
    let a = node("A", json!({})).await;
    let b = node("B", json!({})).await;
    a.call("add_peer", json!({"address": format!("127.0.0.1:{}", b.port().await)})).await;
    both_online(&a, &b, 10).await;
    let src = a.dir.path().join("retry.bin");
    let data: Vec<u8> = (0..(256 * 1024 + 77)).map(|i| (i * 17 % 251) as u8).collect();
    std::fs::write(&src, &data).unwrap();
    let t = a.call("send_file", json!({"conversation": format!("dm:{}", b.fp), "path": src.display().to_string()})).await;
    let tid = t["id"].as_str().unwrap().to_string();
    wait("checksum retry succeeds", 30, || async {
        a.snap().await["transfers"].as_array().unwrap().iter().any(|x| x["id"] == tid.as_str() && x["state"] == "done")
    })
    .await;
    let bt = b.snap().await["transfers"].as_array().unwrap().iter().find(|x| x["id"] == tid.as_str()).cloned().unwrap();
    assert_eq!(bt["state"], "done", "{bt}");
    let got = std::fs::read(bt["path"].as_str().unwrap()).unwrap();
    assert_eq!(got, data, "content mismatch after checksum retry");

    lan_core::engine::test_force_sha_fail(4);
    let src2 = a.dir.path().join("fail.bin");
    std::fs::write(&src2, &data).unwrap();
    let t2 = a.call("send_file", json!({"conversation": format!("dm:{}", b.fp), "path": src2.display().to_string()})).await;
    let tid2 = t2["id"].as_str().unwrap().to_string();
    wait("checksum retries exhausted", 30, || async {
        let bf = b.snap().await["transfers"].as_array().unwrap().iter().any(|x| x["id"] == tid2.as_str() && x["state"] == "failed");
        let af = a.snap().await["transfers"].as_array().unwrap().iter().any(|x| x["id"] == tid2.as_str() && x["state"] == "failed");
        bf && af
    })
    .await;
}

#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn groups_fanout_and_rekey() {
    let a = node("A", json!({})).await;
    let b = node("B", json!({})).await;
    let c = node("C", json!({})).await;
    let (pb, pc) = (b.port().await, c.port().await);
    a.call("add_peer", json!({"address": format!("127.0.0.1:{pb}")})).await;
    a.call("add_peer", json!({"address": format!("127.0.0.1:{pc}")})).await;
    b.call("add_peer", json!({"address": format!("127.0.0.1:{pc}")})).await;
    both_online(&a, &b, 10).await;
    both_online(&a, &c, 10).await;
    both_online(&b, &c, 10).await;
    let g = a.call("create_group", json!({"name": "测试群", "members": [b.fp, c.fp]})).await;
    let gid = g["id"].as_str().unwrap().to_string();
    let conv = format!("group:{gid}");
    wait("B,C joined", 10, || async {
        let hb = b.snap().await["groups"].as_array().unwrap().iter().any(|x| x["id"] == gid.as_str());
        let hc = c.snap().await["groups"].as_array().unwrap().iter().any(|x| x["id"] == gid.as_str());
        hb && hc
    })
    .await;
    b.call("send_text", json!({"conversation": conv, "body": "B 群消息"})).await;
    wait("A,C got group msg", 10, || async { has_body(&a, &conv, "B 群消息").await && has_body(&c, &conv, "B 群消息").await }).await;
    a.call("remove_group_member", json!({"group_id": gid, "fingerprint": c.fp})).await;
    wait("C removed", 10, || async { !c.snap().await["groups"].as_array().unwrap().iter().any(|x| x["id"] == gid.as_str()) }).await;
    wait("B epoch 1", 10, || async {
        b.snap().await["groups"].as_array().unwrap().iter().any(|x| x["id"] == gid.as_str() && x["key_epoch"] == 1 && x["members"].as_array().unwrap().len() == 2)
    })
    .await;
    a.call("send_text", json!({"conversation": conv, "body": "after rekey"})).await;
    wait("B got post-rekey", 10, || has_body(&b, &conv, "after rekey")).await;
    tokio::time::sleep(Duration::from_millis(500)).await;
    assert!(!has_body(&c, &conv, "after rekey").await);
}

#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn pin_pairing_and_untrusted_file_rejected() {
    let a = node("A", json!({"auto_trust": false})).await;
    let b = node("B", json!({"auto_trust": false})).await;
    a.call("add_peer", json!({"address": format!("127.0.0.1:{}", b.port().await)})).await;
    both_online(&a, &b, 10).await;
    assert_eq!(a.peer(&b.fp).await.unwrap()["trust"], "unknown");
    // Text from untrusted peers is allowed; files are rejected.
    chat_both_ways(&a, &b).await;
    let f = a.dir.path().join("x.txt");
    std::fs::write(&f, b"hello").unwrap();
    // Trust pre-check: the send is refused BEFORE anything goes on the wire (never fail-after-send).
    let e = a.h.call("send_file", json!({"conversation": format!("dm:{}", b.fp), "path": f.display().to_string()})).await;
    assert!(e.is_err(), "send_file to an untrusted peer must be refused up front");
    assert!(a.snap().await["transfers"].as_array().unwrap().is_empty(), "no transfer may be created");
    let pin = b.call("pin_offer", json!({})).await["pin"].as_str().unwrap().to_string();
    let bad = a.h.call("pin_pair", json!({"fingerprint": b.fp, "pin": "000000x"})).await.unwrap();
    assert_eq!(bad["ok"], false);
    let r = a.call("pin_pair", json!({"fingerprint": b.fp, "pin": pin})).await;
    assert_eq!(r["ok"], true);
    assert_eq!(a.peer(&b.fp).await.unwrap()["trust"], "trusted");
    wait("B trusts A", 5, || async { b.peer(&a.fp).await.unwrap()["trust"] == "trusted" }).await;
}

#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn stress_bidirectional_messages() {
    let a = node("A", json!({})).await;
    let b = node("B", json!({})).await;
    a.call("add_peer", json!({"address": format!("127.0.0.1:{}", b.port().await)})).await;
    both_online(&a, &b, 10).await;
    const N: usize = 400;
    let (ah, bh) = (a.h.clone(), b.h.clone());
    let (afp, bfp) = (a.fp.clone(), b.fp.clone());
    let t1 = tokio::spawn(async move {
        for i in 0..N {
            ah.call("send_text", json!({"conversation": format!("dm:{bfp}"), "body": format!("a->b {i}")})).await.unwrap();
        }
    });
    let t2 = tokio::spawn(async move {
        for i in 0..N {
            bh.call("send_text", json!({"conversation": format!("dm:{afp}"), "body": format!("b->a {i}")})).await.unwrap();
        }
    });
    t1.await.unwrap();
    t2.await.unwrap();
    let (ca, cb) = (format!("dm:{}", b.fp), format!("dm:{}", a.fp));
    wait("all received", 30, || async {
        let ra = a.msgs(&ca).await.iter().filter(|m| m["direction"] == "in").count();
        let rb = b.msgs(&cb).await.iter().filter(|m| m["direction"] == "in").count();
        ra == N && rb == N
    })
    .await;
    wait("all delivered", 30, || async {
        a.msgs(&ca).await.iter().filter(|m| m["direction"] == "out").all(|m| m["status"] == "delivered")
            && b.msgs(&cb).await.iter().filter(|m| m["direction"] == "out").all(|m| m["status"] == "delivered")
    })
    .await;
    // Per-link ordering preserved.
    let inb: Vec<String> = b.msgs(&cb).await.iter().filter(|m| m["direction"] == "in").map(|m| m["body"].as_str().unwrap().to_string()).collect();
    for (i, s) in inb.iter().enumerate() {
        assert_eq!(s, &format!("a->b {i}"));
    }
}

#[tokio::test]
async fn storage_info_and_orphan_cleanup() {
    let a = node("stor", json!({})).await;
    let dl = a.dir.path().join("dl");
    std::fs::create_dir_all(&dl).unwrap();
    let orphan = dl.join(".orphan.bin.part");
    std::fs::write(&orphan, b"x").unwrap();
    let s = a.call("get_storage", json!({})).await;
    assert!(s["orphan_partials_bytes"].as_u64().unwrap() >= 1, "{s}");
    assert!(s["partials_bytes"].as_u64().unwrap() >= 1, "{s}");
    let r = a.call("cleanup_storage", json!({"orphans": true, "failed_partials": true, "finished_transfers": true})).await;
    assert_eq!(r["ok"], true, "{r}");
    assert!(r["removed_files"].as_u64().unwrap() >= 1, "{r}");
    assert!(!orphan.exists(), "orphan should be gone");
    let s2 = a.call("get_storage", json!({})).await;
    assert_eq!(s2["orphan_partials_bytes"].as_u64().unwrap(), 0, "{s2}");
}

#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn profile_sync_remark_and_history_query() {
    let a = node("Alice", json!({})).await;
    let b = node("Bob", json!({})).await;
    let pb = b.port().await;
    a.call("add_peer", json!({"address": format!("127.0.0.1:{pb}")})).await;
    both_online(&a, &b, 20).await;
    // profile: signature + avatar propagate live (CAP_PROFILE)
    let av = a.dir.path().join("me.png");
    std::fs::write(&av, b"\x89PNG fake avatar").unwrap();
    a.call("set_profile", json!({"signature": "在 302 会议室", "avatar_path": av.display().to_string()})).await;
    wait("signature on bob", 15, || async { b.peer(&a.fp).await.map(|p| p["signature"] == "在 302 会议室" && p["avatar_path"].is_string()).unwrap_or(false) }).await;
    let me = a.call("get_profile", json!({})).await;
    assert_eq!(me["signature"], "在 302 会议室");
    // remark is local only
    b.call("set_remark", json!({"fingerprint": a.fp, "remark": "老板"})).await;
    let p = b.peer(&a.fp).await.unwrap();
    assert_eq!((p["name"].as_str(), p["nickname"].as_str()), (Some("老板"), Some("Alice")));
    // history query across conversations with filters
    a.send(&b, "下午三点评审").await;
    a.send(&b, "hello world").await;
    wait("two msgs on bob", 15, || async { b.msgs(&format!("dm:{}", a.fp)).await.len() >= 2 }).await;
    let r = b.call("history_query", json!({"query": "评审"})).await;
    assert_eq!(r["total"], 1);
    let r = b.call("history_query", json!({"direction": "in", "types": ["text"], "limit": 1})).await;
    assert_eq!((r["total"].as_u64(), r["has_more"].as_bool()), (Some(2), Some(true)));
    let id = r["items"][0]["id"].as_str().unwrap().to_string();
    let ctx = b.call("message_context", json!({"id": id})).await;
    assert_eq!(ctx["conversation"], format!("dm:{}", a.fp));
    let cp = b.call("contact_profile", json!({"fingerprint": a.fp})).await;
    assert_eq!(cp["peer"]["signature"], "在 302 会议室");
}

#[tokio::test(flavor = "multi_thread")]
async fn ext_reply_react_edit_recall() {
    let a = node("Alice", json!({})).await;
    let b = node("Bob", json!({})).await;
    let pb = b.port().await;
    a.call("add_peer", json!({"address": format!("127.0.0.1:{pb}")})).await;
    both_online(&a, &b, 20).await;
    let conv_b = format!("dm:{}", a.fp);
    let conv_a = format!("dm:{}", b.fp);
    a.send(&b, "明天几点开会？").await;
    wait("first msg", 15, || async { b.msgs(&conv_b).await.len() >= 1 }).await;
    let orig = b.msgs(&conv_b).await.last().unwrap()["id"].as_str().unwrap().to_string();
    // reply with quote
    b.call("send_text", json!({"conversation": conv_b, "body": "十点", "reply_to": orig})).await;
    wait("reply on alice", 15, || async { a.msgs(&conv_a).await.iter().any(|m| m["body"] == "十点" && m["reply_to"] == orig.as_str() && m["quote"].as_str().unwrap_or("").contains("明天几点开会")) }).await;
    // reaction (toggle)
    b.call("react", json!({"id": orig, "emoji": "👍"})).await;
    wait("reaction on alice", 15, || async { a.msgs(&conv_a).await.iter().any(|m| m["id"] == orig.as_str() && m["reactions"][0]["emoji"] == "👍") }).await;
    // edit + recall by the sender
    a.call("edit_message", json!({"id": orig, "body": "明天几点开评审会？"})).await;
    wait("edit on bob", 15, || async { b.msgs(&conv_b).await.iter().any(|m| m["id"] == orig.as_str() && m["body"] == "明天几点开评审会？" && m["edited"] == true) }).await;
    a.call("recall_message", json!({"id": orig})).await;
    wait("recall on bob", 15, || async { b.msgs(&conv_b).await.iter().any(|m| m["id"] == orig.as_str() && m["recalled"] == true && m["body"] == "") }).await;
    // the receiver cannot edit someone else's message
    let r = b.h.call("edit_message", json!({"id": orig, "body": "x"})).await;
    assert!(r.is_err());
}

#[tokio::test(flavor = "multi_thread")]
async fn p2_pins_drafts_flags_typing_forward_mentions() {
    let a = node("Alice", json!({})).await;
    let b = node("Bob", json!({})).await;
    let pb = b.port().await;
    let mut ev_b = b.h.subscribe();
    a.call("add_peer", json!({"address": format!("127.0.0.1:{pb}")})).await;
    both_online(&a, &b, 20).await;
    let conv_a = format!("dm:{}", b.fp);
    let conv_b = format!("dm:{}", a.fp);
    // pinned chat + draft show in the snapshot (local only)
    a.call("set_conv_pinned", json!({"conversation": conv_a, "pinned": true})).await;
    a.call("set_draft", json!({"conversation": conv_a, "text": "草稿：别忘了带电脑"})).await;
    let s = a.call("get_snapshot", json!({})).await;
    let c = s["conversations"].as_array().unwrap().iter().find(|c| c["id"] == conv_a.as_str()).unwrap().clone();
    assert_eq!(c["pinned"], true);
    assert_eq!(c["draft"], "草稿：别忘了带电脑");
    // typing reaches Bob as an event (never queued)
    a.call("send_typing", json!({"conversation": conv_a})).await;
    let got = tokio::time::timeout(std::time::Duration::from_secs(10), async {
        loop {
            if let Ok(e) = ev_b.recv().await {
                if let lan_core::api::Event::Typing { conversation, fingerprint, .. } = e {
                    if conversation == conv_b && fingerprint == a.fp { return true; }
                }
            }
        }
    }).await.unwrap_or(false);
    assert!(got, "typing event");
    // mention + forward
    let m = a.call("send_text", json!({"conversation": conv_a, "body": "@Bob 看一下", "mentions": [b.fp]})).await;
    let mid = m["id"].as_str().unwrap().to_string();
    wait("mention on bob", 15, || async { b.msgs(&conv_b).await.iter().any(|m| m["id"] == mid.as_str() && m["mentions_me"] == true) }).await;
    let f = a.call("forward_message", json!({"id": mid, "conversations": [conv_a]})).await;
    let fid = f[0]["id"].as_str().unwrap().to_string();
    wait("forward on bob", 15, || async { b.msgs(&conv_b).await.iter().any(|m| m["id"] == fid.as_str() && m["forwarded"] == true && m["body"] == "@Bob 看一下") }).await;
    // local flags
    a.call("set_message_flags", json!({"id": mid, "pinned": true, "starred": true})).await;
    let st = a.call("list_flagged", json!({"flag": "starred"})).await;
    assert!(st.as_array().unwrap().iter().any(|m| m["id"] == mid.as_str()));
    let pn = a.call("list_flagged", json!({"flag": "pinned", "conversation": conv_a})).await;
    assert_eq!(pn.as_array().unwrap().len(), 1);
    a.call("set_draft", json!({"conversation": conv_a, "text": ""})).await;
    let s = a.call("get_snapshot", json!({})).await;
    assert_eq!(s["conversations"].as_array().unwrap().iter().find(|c| c["id"] == conv_a.as_str()).unwrap()["draft"], "");
}

#[tokio::test(flavor = "multi_thread")]
async fn v062_polls_folders_archive_disappearing() {
    let a = node("Alice", json!({})).await;
    let b = node("Bob", json!({})).await;
    let pb = b.port().await;
    a.call("add_peer", json!({"address": format!("127.0.0.1:{pb}")})).await;
    both_online(&a, &b, 20).await;
    let conv_a = format!("dm:{}", b.fp);
    let conv_b = format!("dm:{}", a.fp);
    // poll: create, vote both ways, close
    let pm = a.call("send_poll", json!({"conversation": conv_a, "question": "午饭吃什么？", "options": ["面", "饭", "饺子"]})).await;
    let pid = pm["id"].as_str().unwrap().to_string();
    wait("poll on bob", 15, || async { b.msgs(&conv_b).await.iter().any(|m| m["id"] == pid.as_str() && m["poll"]["options"].as_array().map(|o| o.len()) == Some(3)) }).await;
    b.call("vote_poll", json!({"id": pid, "choices": [2, 0]})).await; // single choice: keeps the first after sort (0)
    wait("vote on alice", 15, || async { a.msgs(&conv_a).await.iter().any(|m| m["id"] == pid.as_str() && m["poll"]["votes"][0]["choices"] == json!([0])) }).await;
    assert!(b.h.call("close_poll", json!({"id": pid})).await.is_err(), "only the creator closes");
    a.call("close_poll", json!({"id": pid})).await;
    wait("closed on bob", 15, || async { b.msgs(&conv_b).await.iter().any(|m| m["id"] == pid.as_str() && m["poll"]["closed"] == true) }).await;
    assert!(b.h.call("vote_poll", json!({"id": pid, "choices": [1]})).await.is_err());
    // folders + archive (local)
    a.call("set_folder", json!({"name": "工作", "conversations": [conv_a]})).await;
    a.call("set_conv_archived", json!({"conversation": conv_a, "archived": true})).await;
    let conv = |s: Value| s["conversations"].as_array().unwrap().iter().find(|c| c["id"] == conv_a.as_str()).unwrap().clone();
    let c = conv(a.call("get_snapshot", json!({})).await);
    assert_eq!(c["archived"], true);
    assert_eq!(c["folders"], json!(["工作"]));
    assert_eq!(a.call("list_folders", json!({})).await[0]["name"], "工作");
    // new incoming message un-archives
    b.call("send_text", json!({"conversation": conv_b, "body": "在吗"})).await;
    wait("unarchived", 15, || async { conv(a.call("get_snapshot", json!({})).await)["archived"] == false }).await;
    a.call("set_folder", json!({"name": "工作", "conversations": []})).await;
    assert_eq!(a.call("list_folders", json!({})).await.as_array().unwrap().len(), 0);
    // disappearing: timer syncs to the peer, new messages expire on both sides
    assert!(a.h.call("set_disappearing", json!({"conversation": conv_a, "secs": 7})).await.is_err());
    a.call("set_disappearing", json!({"conversation": conv_a, "secs": 30})).await;
    wait("ttl on bob", 15, || async { b.call("get_snapshot", json!({})).await["conversations"].as_array().unwrap().iter().any(|c| c["id"] == conv_b.as_str() && c["ttl_secs"] == 30) }).await;
    let m = a.call("send_text", json!({"conversation": conv_a, "body": "阅后即焚"})).await;
    let mid = m["id"].as_str().unwrap().to_string();
    wait("ttl msg on bob", 15, || async { b.msgs(&conv_b).await.iter().any(|m| m["id"] == mid.as_str() && m["expires_ms"].as_i64().unwrap_or(0) > 0) }).await;
    assert!(a.msgs(&conv_a).await.iter().any(|m| m["id"] == mid.as_str() && m["expires_ms"].as_i64().unwrap_or(0) > 0));
    // real expiry: gone on both sides after ~30 s
    wait("expired on alice", 45, || async { !a.msgs(&conv_a).await.iter().any(|m| m["id"] == mid.as_str()) }).await;
    wait("expired on bob", 15, || async { !b.msgs(&conv_b).await.iter().any(|m| m["id"] == mid.as_str()) }).await;
    // the timer-change notices are system messages and never expire
    assert!(a.msgs(&conv_a).await.iter().any(|m| m["kind"] == "system"));
}

#[tokio::test(flavor = "multi_thread")]
async fn ux_qr_add_resend_preview_route() {
    let a = node("Alice", json!({})).await;
    let b = node("Bob", json!({})).await;
    let pb = b.port().await;
    let u = b.call("my_uri", json!({})).await;
    let uri = u["uri"].as_str().unwrap().to_string();
    assert!(uri.starts_with("lsm://add?a=") && uri.contains(&format!("fp={}", b.fp)), "{uri}");
    assert!(u["qr_size"].as_u64().unwrap() >= 21);
    // the test peers talk over loopback: swap the address, keep the fingerprint
    let fp_part = &uri[uri.find("&fp=").unwrap()..];
    let r = a.call("add_uri", json!({"uri": format!("lsm://add?a=127.0.0.1:{pb}{fp_part}")})).await;
    assert_eq!(r["fingerprint"], b.fp.as_str());
    assert!(a.h.call("add_uri", json!({"uri": "http://x"})).await.is_err());
    both_online(&a, &b, 20).await;
    wait("qr add trusts the proven fingerprint", 15, || async { a.peer(&b.fp).await.map(|p| p["trust"] == "trusted").unwrap_or(false) }).await;
    let conv_a = format!("dm:{}", b.fp);
    let m = a.call("send_text", json!({"conversation": conv_a, "body": "路由"})).await;
    let mid = m["id"].as_str().unwrap().to_string();
    wait("route LAN", 15, || async { a.msgs(&conv_a).await.iter().any(|x| x["id"] == mid.as_str() && x["route"] == 1) }).await;
    assert!(a.h.call("resend", json!({"id": mid})).await.is_err(), "delivered messages are not resent");
    let dir = tempfile::tempdir().unwrap();
    let f = dir.path().join("big.bin");
    std::fs::write(&f, vec![0u8; 3 * 1024 * 1024]).unwrap();
    let pv = a.call("file_preview", json!({"conversation": conv_a, "path": f.display().to_string()})).await;
    assert_eq!(pv["size"], 3 * 1024 * 1024);
    assert!(pv["text"].as_str().unwrap().contains("3.0 MB") && pv["text"].as_str().unwrap().contains("局域网直连"), "{pv}");
    // offline peer: queued -> one-click resend replaces it with a fresh message
    b.call("shutdown", json!({})).await;
    wait("bob offline", 30, || async { !a.online(&b.fp).await }).await;
    let q = a.call("send_text", json!({"conversation": conv_a, "body": "稍后"})).await;
    let qid = q["id"].as_str().unwrap().to_string();
    let r = a.call("resend", json!({"id": qid})).await;
    let nid = r["id"].as_str().unwrap().to_string();
    let ms = a.msgs(&conv_a).await;
    assert!(!ms.iter().any(|x| x["id"] == qid.as_str()) && ms.iter().any(|x| x["id"] == nid.as_str() && x["body"] == "稍后"));
}


async fn restart_node(n: Node) -> Node {
    let dir = n.dir;
    let old_fp = n.fp.clone();
    let notified = n.h.shutdown.notified();
    let _ = n.h.call("shutdown", json!({})).await;
    let _ = tokio::time::timeout(Duration::from_secs(8), notified).await;
    drop(n.h);
    let ov = json!({
        "listen_port": 0,
        "discovery_enabled": false,
        "multicast_enabled": false,
        "auto_trust": true,
        "announce_interval_secs": 1,
        "exclude_interfaces": [" | "],
        "download_dir": dir.path().join("dl").display().to_string(),
    });
    let h = start(EngineOptions { paths: ConfigPaths::from_base(dir.path()), ipc_endpoint: "test".into(), overrides: Some(ov) }).await.unwrap();
    assert_eq!(h.fingerprint, old_fp, "restart must keep identity");
    Node { fp: h.fingerprint.clone(), h, dir }
}

fn conv<'a>(snap: &'a Value, id: &str) -> Option<&'a Value> {
    snap["conversations"].as_array()?.iter().find(|c| c["id"] == id)
}

/// Pin & mute persist across engine restart; mark_read/unread update unread
/// badge counts without touching another conversation; clear_conversation
/// removes only that conversation's local history.
#[tokio::test(flavor = "multi_thread")]
async fn v070_chat_list_actions() {
    let a = node("Alice", json!({})).await;
    let b = node("Bob", json!({})).await;
    let c = node("Carol", json!({})).await;
    let pb = b.port().await;
    let pc = c.port().await;
    a.call("add_peer", json!({"address": format!("127.0.0.1:{pb}")})).await;
    a.call("add_peer", json!({"address": format!("127.0.0.1:{pc}")})).await;
    both_online(&a, &b, 20).await;
    both_online(&a, &c, 20).await;
    let conv_ab = format!("dm:{}", b.fp);
    let conv_ac = format!("dm:{}", c.fp);
    b.send(&a, "from Bob").await;
    c.send(&a, "from Carol").await;
    wait("alice has bob msg", 15, || has_body(&a, &conv_ab, "from Bob")).await;
    wait("alice has carol msg", 15, || has_body(&a, &conv_ac, "from Carol")).await;
    wait("unread badges after incoming", 10, || async {
        let s = a.snap().await;
        conv(&s, &conv_ab).and_then(|c| c["unread"].as_u64()).unwrap_or(0) >= 1
            && conv(&s, &conv_ac).and_then(|c| c["unread"].as_u64()).unwrap_or(0) >= 1
    }).await;
    let unread_c = conv(&a.snap().await, &conv_ac).unwrap()["unread"].as_u64().unwrap();
    a.call("mark_read", json!({"conversation": conv_ab})).await;
    let s = a.snap().await;
    assert_eq!(conv(&s, &conv_ab).unwrap()["unread"].as_u64().unwrap(), 0, "mark_read must clear unread badge");
    assert_eq!(conv(&s, &conv_ac).unwrap()["unread"].as_u64().unwrap(), unread_c, "mark_read must not change the other conversation");
    a.call("mark_unread", json!({"conversation": conv_ab})).await;
    let s = a.snap().await;
    assert!(conv(&s, &conv_ab).unwrap()["unread"].as_u64().unwrap() >= 1, "mark_unread must restore unread badge");
    assert_eq!(conv(&s, &conv_ac).unwrap()["unread"].as_u64().unwrap(), unread_c, "mark_unread must not change the other conversation");
    a.call("set_conv_pinned", json!({"conversation": conv_ab, "pinned": true})).await;
    a.call("set_muted", json!({"conversation": conv_ab, "muted": true})).await;
    let s = a.snap().await;
    assert_eq!(conv(&s, &conv_ab).unwrap()["pinned"], true);
    assert_eq!(a.peer(&b.fp).await.unwrap()["muted"], true);
    assert_eq!(a.peer(&c.fp).await.unwrap()["muted"], false);
    let n_ab = a.msgs(&conv_ab).await.len();
    assert!(n_ab >= 1);
    let n_ac = a.msgs(&conv_ac).await.len();
    assert!(n_ac >= 1);
    let n_c_local = c.msgs(&format!("dm:{}", a.fp)).await.len();
    a.call("clear_conversation", json!({"conversation": conv_ac})).await;
    assert_eq!(a.msgs(&conv_ac).await.len(), 0, "delete must clear that conversation locally");
    assert_eq!(a.msgs(&conv_ab).await.len(), n_ab, "delete must not remove another conversation");
    assert_eq!(c.msgs(&format!("dm:{}", a.fp)).await.len(), n_c_local, "delete is local-only; peer keeps their copy");
    let a = restart_node(a).await;
    let s = a.snap().await;
    assert_eq!(conv(&s, &conv_ab).unwrap()["pinned"], true, "pin must persist across restart");
    assert_eq!(a.peer(&b.fp).await.unwrap()["muted"], true, "mute must persist across restart");
    assert_eq!(a.msgs(&conv_ac).await.len(), 0, "deleted conversation must stay gone after restart");
    assert_eq!(a.msgs(&conv_ab).await.len(), n_ab, "other conversation must survive restart");
}

/// Local throughput check (not run in CI): `cargo test -p lan-core --test engine file_transfer_1gb_direct -- --ignored --nocapture`
#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
#[ignore]
async fn file_transfer_1gb_direct() {
    use std::io::{Read, Write};
    use sha2::{Digest, Sha256};
    let n: u64 = std::env::var("LSM_XFER_BYTES").ok().and_then(|s| s.parse().ok()).unwrap_or(1024 * 1024 * 1024);
    fn write_pat(p: &std::path::Path, n: u64) {
        let mut f = std::fs::File::create(p).unwrap();
        let mut buf = vec![0u8; 1024 * 1024];
        let mut left = n;
        let mut i = 0u64;
        while left > 0 {
            let m = left.min(buf.len() as u64) as usize;
            for (j, b) in buf[..m].iter_mut().enumerate() {
                *b = ((i.wrapping_mul(131) + j as u64) % 251) as u8;
            }
            f.write_all(&buf[..m]).unwrap();
            left -= m as u64;
            i += 1;
        }
    }
    fn sha_file(p: &std::path::Path) -> String {
        let mut h = Sha256::new();
        let mut f = std::fs::File::open(p).unwrap();
        let mut buf = vec![0u8; 1024 * 1024];
        loop {
            let n = f.read(&mut buf).unwrap();
            if n == 0 { break; }
            h.update(&buf[..n]);
        }
        format!("{:x}", h.finalize())
    }
    let a = node("A", json!({})).await;
    let b = node("B", json!({})).await;
    a.call("add_peer", json!({"address": format!("127.0.0.1:{}", b.port().await)})).await;
    both_online(&a, &b, 15).await;
    let src = a.dir.path().join("one-gib.bin");
    write_pat(&src, n);
    let want = sha_file(&src);
    let t0 = Instant::now();
    let t = a.call("send_file", json!({"conversation": format!("dm:{}", b.fp), "path": src.display().to_string()})).await;
    let tid = t["id"].as_str().unwrap().to_string();
    wait("large file sender done", 600, || async {
        a.snap().await["transfers"].as_array().unwrap().iter().any(|x| x["id"] == tid.as_str() && x["state"] == "done")
    }).await;
    let elapsed = t0.elapsed();
    let bt = b.snap().await["transfers"].as_array().unwrap().iter().find(|x| x["id"] == tid.as_str()).cloned().unwrap();
    assert_eq!(bt["state"], "done", "{bt}");
    let path = bt["path"].as_str().unwrap();
    assert_eq!(std::fs::metadata(path).unwrap().len(), n);
    assert_eq!(sha_file(std::path::Path::new(path)), want, "content mismatch");
    let mbps = (n as f64 / 1_000_000.0) / elapsed.as_secs_f64();
    println!(
        "direct transfer {} bytes ok in {:.2}s ({:.1} MB/s, CHUNK={} WINDOW 16 MiB) tid={}",
        n, elapsed.as_secs_f64(), mbps, lan_core::wire::CHUNK, tid
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn v071_account_mode_local() {
    let a = node("Ada", json!({})).await;
    let me = a.call("complete_first_run", json!({"name": "Ada", "mode": "device"})).await;
    assert_eq!(me["first_run_done"], true);
    assert_eq!(me["account_mode"], "device");
    let setup = a.call("account_setup", json!({})).await;
    assert!(setup["recovery_key"].as_str().unwrap().starts_with("RK1-"), "{setup}");
    assert_eq!(setup["lookup"].as_str().unwrap().len(), 32);
    assert_eq!(a.snap().await["me"]["account_mode"], "account");
    assert_eq!(a.snap().await["me"]["has_recovery_key"], true);
    let shown = a.call("reveal_recovery_key", json!({})).await;
    assert_eq!(shown["recovery_key"], setup["recovery_key"]);
    let uri = format!("lsm://relay?h=127.0.0.1&p=9443");
    let r = a.call("apply_uri", json!({"uri": uri})).await;
    assert_eq!(r["ok"], true);
    assert_eq!(a.snap().await["settings"]["relay_host"], "127.0.0.1");
    a.call("account_set_mode", json!({"mode": "device"})).await;
    assert_eq!(a.snap().await["me"]["account_mode"], "device");
    let add = lan_core::account::parse_lsm_uri("lsm://add?a=10.0.0.1:1&fp=FF&n=x").unwrap();
    match add {
        lan_core::account::LsmUri::Add { addr, .. } => assert_eq!(addr, "10.0.0.1:1"),
        _ => panic!("add"),
    }
}

fn assert_no_plaintext(root: &std::path::Path, text: &str, file_bytes: &[u8]) {
    fn walk(p: &std::path::Path, text: &str, file_bytes: &[u8]) {
        if p.is_dir() {
            if let Ok(rd) = std::fs::read_dir(p) {
                for e in rd.flatten() {
                    walk(&e.path(), text, file_bytes);
                }
            }
            return;
        }
        let Ok(b) = std::fs::read(p) else { return };
        assert!(
            !b.windows(text.len()).any(|w| w == text.as_bytes()),
            "plaintext message leaked into {}",
            p.display()
        );
        if file_bytes.len() >= 8 {
            assert!(
                !b.windows(file_bytes.len()).any(|w| w == file_bytes),
                "plaintext file leaked into {}",
                p.display()
            );
        }
    }
    walk(root, text, file_bytes);
}

async fn start_named(name: &str, base: &std::path::Path) -> EngineHandle {
    let ov = serde_json::json!({
        "display_name": name,
        "listen_port": 0,
        "discovery_enabled": false,
        "multicast_enabled": false,
        "auto_trust": true,
        "announce_interval_secs": 1,
        "exclude_interfaces": [" | "],
        "download_dir": base.join("dl").display().to_string(),
    });
    start(EngineOptions { paths: ConfigPaths::from_base(base), ipc_endpoint: "test".into(), overrides: Some(ov) }).await.unwrap()
}

/// Device A in account mode sends/receives text + a file; a fresh device B restores
/// the encrypted backup and sees both. Anything written like a relay `data_dir`
/// (the LSMBAK02 blob) must not contain plaintext.
#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn v071_account_history_restore() {
    const SECRET_OUT: &str = "LSM-V071-SECRET-MSG-账号历史同步";
    const SECRET_IN: &str = "LSM-V071-SECRET-REPLY-from-peer";
    const FILE_BYTES: &[u8] = b"LSM-V071-FILE-PAYLOAD-NO-PLAINTEXT-ON-SERVER";

    let a = node("Ada", json!({})).await;
    let p = node("Pat", json!({})).await;
    a.call("add_peer", json!({"address": format!("127.0.0.1:{}", p.port().await)})).await;
    both_online(&a, &p, 10).await;

    let setup = a.call("account_setup", json!({})).await;
    let rk = setup["recovery_key"].as_str().unwrap().to_string();
    let lookup = setup["lookup"].as_str().unwrap().to_string();
    assert!(rk.starts_with("RK1-"), "{setup}");
    assert_eq!(a.snap().await["me"]["account_mode"], "account");

    let mout = a.send(&p, SECRET_OUT).await;
    let min = p.send(&a, SECRET_IN).await;
    let (ca, cb) = (format!("dm:{}", p.fp), format!("dm:{}", a.fp));
    wait("peer got secret", 10, || has_body(&p, &cb, SECRET_OUT)).await;
    wait("A got reply", 10, || has_body(&a, &ca, SECRET_IN)).await;
    wait("out delivered", 10, || delivered(&a, &ca, mout["id"].as_str().unwrap())).await;
    wait("in delivered", 10, || delivered(&p, &cb, min["id"].as_str().unwrap())).await;

    let src = a.dir.path().join("secret-file.bin");
    std::fs::write(&src, FILE_BYTES).unwrap();
    let t = a.call("send_file", json!({"conversation": ca, "path": src.display().to_string()})).await;
    let tid = t["id"].as_str().unwrap().to_string();
    wait("file sent", 20, || async {
        a.snap().await["transfers"].as_array().unwrap().iter().any(|x| x["id"] == tid.as_str() && x["state"] == "done")
    }).await;
    wait("file received", 20, || async {
        p.snap().await["transfers"].as_array().unwrap().iter().any(|x| x["id"] == tid.as_str() && x["state"] == "done")
    }).await;

    let bak = a.dir.path().join("account-hist.lsmbak");
    a.call("backup", json!({"path": bak.display().to_string(), "password": rk})).await;
    let blob = std::fs::read(&bak).unwrap();
    assert!(blob.starts_with(b"LSMBAK02"), "backup must be LSMBAK02");
    assert_no_plaintext(&bak, SECRET_OUT, FILE_BYTES);
    assert_no_plaintext(&bak, SECRET_IN, FILE_BYTES);

    let server_dir = tempfile::tempdir().unwrap();
    let user_dir = server_dir.path().join("users").join(&lookup);
    std::fs::create_dir_all(&user_dir).unwrap();
    std::fs::write(user_dir.join("identity.bak"), &blob).unwrap();
    assert_no_plaintext(server_dir.path(), SECRET_OUT, FILE_BYTES);
    assert_no_plaintext(server_dir.path(), SECRET_IN, FILE_BYTES);

    let bdir = tempfile::tempdir().unwrap();
    lan_core::backup::import_backup(&ConfigPaths::from_base(bdir.path()), &rk, &bak).unwrap();
    let bh = start_named("AdaRestored", bdir.path()).await;
    assert_eq!(bh.fingerprint, a.fp, "restored device must keep the same identity");
    let bnode = Node { h: bh, fp: a.fp.clone(), dir: bdir };
    let msgs = bnode.msgs(&ca).await;
    assert!(msgs.iter().any(|m| m["body"] == SECRET_OUT), "outgoing text missing on B: {msgs:?}");
    assert!(msgs.iter().any(|m| m["body"] == SECRET_IN), "incoming text missing on B: {msgs:?}");
    let fmsg = msgs.iter().find(|m| m["kind"] == "file").cloned().expect("file message missing on B");
    let fpath = fmsg["file"]["path"].as_str().expect("file path missing after restore");
    assert_eq!(std::fs::read(fpath).unwrap(), FILE_BYTES, "restored file content mismatch at {fpath}");

    // Optional live relay PUT/GET when the binary is already built (not required on every CI job).
    let bin = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../relay-server/target/debug/relay-server");
    if bin.exists() {
        let rdir = tempfile::tempdir().unwrap();
        let port = free_udp_port();
        let cfg = rdir.path().join("relay.toml");
        let data = rdir.path().join("data");
        std::fs::create_dir_all(&data).unwrap();
        std::fs::write(&cfg, format!(
            "[server]\nbind = \"127.0.0.1\"\nport = {port}\npassphrase = \"\"\ndata_dir = \"{}\"\nstatus_addr = \"off\"\n[log]\nlevel = \"error\"\n",
            data.display().to_string().replace('\\', "/")
        )).unwrap();
        let mut child = std::process::Command::new(&bin)
            .arg("--config").arg(&cfg)
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .spawn()
            .expect("spawn relay-server");
        tokio::time::sleep(Duration::from_millis(400)).await;
        let setup2 = a.call("account_setup", json!({"relay_host": "127.0.0.1", "relay_port": port})).await;
        assert_eq!(setup2["lookup"], lookup);
        // Ensure the latest history+file blob is what the server stores.
        let put = a.call("account_backup_put", json!({"user": lookup, "password": rk})).await;
        assert_eq!(put["ok"], true, "{put}");
        tokio::time::sleep(Duration::from_millis(200)).await;
        assert_no_plaintext(&data, SECRET_OUT, FILE_BYTES);
        assert_no_plaintext(&data, SECRET_IN, FILE_BYTES);

        let cdir = tempfile::tempdir().unwrap();
        let ch = start_named("FreshB", cdir.path()).await;
        let cfp_before = ch.fingerprint.clone();
        let cnode = Node { h: ch, fp: cfp_before.clone(), dir: cdir };
        let rest = cnode.call("account_restore", json!({"recovery_key": rk, "relay_host": "127.0.0.1", "relay_port": port})).await;
        assert_eq!(rest["ok"], true, "{rest}");
        let _ = cnode.h.call("shutdown", json!({})).await;
        tokio::time::sleep(Duration::from_millis(200)).await;
        let ch2 = start_named("FreshB2", cnode.dir.path()).await;
        assert_eq!(ch2.fingerprint, a.fp, "relay restore must recover identity, was {cfp_before}");
        let c2 = Node { h: ch2, fp: a.fp.clone(), dir: tempfile::tempdir().unwrap() };
        // msgs live on cnode.dir (same path as ch2)
        let msgs2 = {
            // query via restored handle
            c2.h.call("get_messages", json!({"conversation": ca, "limit": 2000})).await.unwrap()
        };
        let arr = msgs2.as_array().cloned().unwrap();
        assert!(arr.iter().any(|m| m["body"] == SECRET_OUT), "relay-restored text missing: {arr:?}");
        assert!(arr.iter().any(|m| m["body"] == SECRET_IN), "relay-restored reply missing: {arr:?}");
        let f2 = arr.iter().find(|m| m["kind"] == "file").cloned().expect("relay-restored file missing");
        let p2 = f2["file"]["path"].as_str().expect("relay-restored file path");
        assert_eq!(std::fs::read(p2).unwrap(), FILE_BYTES);
        let _ = child.kill();
        let _ = child.wait();
    }
}
