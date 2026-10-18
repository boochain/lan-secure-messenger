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
    let t = a.call("send_file", json!({"conversation": format!("dm:{}", b.fp), "path": f.display().to_string()})).await;
    let tid = t["id"].as_str().unwrap().to_string();
    wait("rejected", 10, || async { a.snap().await["transfers"].as_array().unwrap().iter().any(|x| x["id"] == tid.as_str() && x["state"] == "rejected") }).await;
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
