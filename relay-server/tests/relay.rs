//! Integration tests: run the real server in-process and talk to it over TCP.
use relay::client::{is_auth_fail, test_id, Client, Event, BAD_PASS};
use relay::config::Config;
use relay::proto::{self, code, t, Frame};
use relay::server;
use std::time::{Duration, Instant};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};

const PASS: &str = "测试口令-correct horse";
const W: Duration = Duration::from_secs(20);

fn cfg(port: u16) -> Config {
    let mut c = Config::default();
    c.server.bind = "127.0.0.1".into();
    c.server.port = port;
    c.server.passphrase = PASS.into();
    c.server.status_addr = "off".into();
    c
}
async fn start(c: Config) -> server::Running {
    server::start(c).await.expect("start")
}
fn free_port() -> u16 {
    std::net::TcpListener::bind("127.0.0.1:0").unwrap().local_addr().unwrap().port()
}
async fn conn(addr: &str, name: &str) -> Client {
    Client::connect(addr, PASS, test_id(name), name.as_bytes()).await.expect("connect")
}
async fn wait_online(c: &mut Client, who: &str) {
    let id = test_id(who);
    c.wait_for(W, |e| match e {
        Event::PeerList(v) => v.iter().any(|(i, _)| *i == id),
        Event::Join(i, _) => *i == id,
        _ => false,
    })
    .await
    .unwrap_or_else(|_| panic!("{who} never appeared"));
}
async fn recv_blob(c: &mut Client) -> (proto::PeerId, Vec<u8>) {
    match c.wait_for(W, |e| matches!(e, Event::Deliver(..))).await.unwrap() {
        Event::Deliver(s, b) => (s, b),
        _ => unreachable!(),
    }
}
/// Reconnect with retries/backoff like a real client would.
async fn reconnect(addr: &str, name: &str) -> (Client, u32) {
    let mut tries = 0;
    loop {
        tries += 1;
        match Client::connect(addr, PASS, test_id(name), name.as_bytes()).await {
            Ok(c) => return (c, tries),
            Err(_) if tries < 100 => tokio::time::sleep(Duration::from_millis(100)).await,
            Err(e) => panic!("reconnect failed: {e}"),
        }
    }
}

#[tokio::test]
async fn presence_and_exchange() {
    let s = start(cfg(0)).await;
    let a_addr = s.addr.to_string();
    let mut a = conn(&a_addr, "alice").await;
    match a.wait_for(W, |e| matches!(e, Event::PeerList(_))).await.unwrap() {
        Event::PeerList(v) => assert!(v.is_empty()),
        _ => unreachable!(),
    }
    let mut b = conn(&a_addr, "bob").await;
    // bob sees alice in the snapshot, alice gets a push immediately
    wait_online(&mut b, "alice").await;
    match a.wait_for(W, |e| matches!(e, Event::Join(..))).await.unwrap() {
        Event::Join(i, info) => {
            assert_eq!(i, test_id("bob"));
            assert_eq!(info, b"bob");
        }
        _ => unreachable!(),
    }
    a.send(&test_id("bob"), b"opaque-ciphertext-1").await.unwrap();
    let (src, blob) = recv_blob(&mut b).await;
    assert_eq!(src, test_id("alice"));
    assert_eq!(blob, b"opaque-ciphertext-1");
    b.send(&test_id("alice"), b"reply").await.unwrap();
    assert_eq!(recv_blob(&mut a).await.1, b"reply");
    // offline target
    a.send(&test_id("nobody"), b"x").await.unwrap();
    match a.wait_for(W, |e| matches!(e, Event::SendFail(..))).await.unwrap() {
        Event::SendFail(d, c) => {
            assert_eq!(d, test_id("nobody"));
            assert_eq!(c, code::PEER_OFFLINE);
        }
        _ => unreachable!(),
    }
    // ping/pong
    a.ping(b"tok").await.unwrap();
    assert!(matches!(a.wait_for(W, |e| matches!(e, Event::Pong(_))).await.unwrap(), Event::Pong(p) if p == b"tok"));
    // leave
    b.bye().await;
    drop(b);
    match a.wait_for(W, |e| matches!(e, Event::Leave(_))).await.unwrap() {
        Event::Leave(i) => assert_eq!(i, test_id("bob")),
        _ => unreachable!(),
    }
    s.stop().await;
}

#[tokio::test]
async fn wrong_passphrase_rejected() {
    let s = start(cfg(0)).await;
    let addr = s.addr.to_string();
    let e = Client::connect(&addr, "wrong", test_id("eve"), b"").await.err().expect("must fail");
    assert!(is_auth_fail(&e, BAD_PASS), "{e}");
    // empty passphrase too
    let e = Client::connect(&addr, "", test_id("eve"), b"").await.err().expect("must fail");
    assert!(is_auth_fail(&e, BAD_PASS), "{e}");
    // correct one still works
    let _ok = conn(&addr, "alice").await;
    s.stop().await;
}

#[tokio::test]
async fn auth_bruteforce_lockout() {
    let mut c = cfg(0);
    c.limits.auth_failures_per_min = 3;
    let s = start(c).await;
    let addr = s.addr.to_string();
    for _ in 0..3 {
        assert!(Client::connect(&addr, "nope", test_id("eve"), b"").await.is_err());
    }
    let e = Client::connect(&addr, PASS, test_id("eve"), b"").await.err().expect("locked out");
    assert!(is_auth_fail(&e, code::TOO_MANY_ATTEMPTS), "{e}");
    s.stop().await;
}

#[tokio::test]
async fn server_restart_then_reconnect() {
    let port = free_port();
    let s = start(cfg(port)).await;
    let addr = s.addr.to_string();
    let mut a = conn(&addr, "alice").await;
    let _b = conn(&addr, "bob").await;
    wait_online(&mut a, "bob").await;
    s.stop().await;
    a.wait_for(W, |e| matches!(e, Event::Closed | Event::Error(..))).await.unwrap();
    let s2 = start(cfg(port)).await;
    let (mut a, ta) = reconnect(&addr, "alice").await;
    let (mut b, _) = reconnect(&addr, "bob").await;
    wait_online(&mut a, "bob").await;
    a.send(&test_id("bob"), b"after-restart").await.unwrap();
    assert_eq!(recv_blob(&mut b).await.1, b"after-restart");
    eprintln!("restart: alice reconnected after {ta} attempt(s)");
    s2.stop().await;
}

#[tokio::test]
async fn port_change_then_reconnect() {
    let p1 = free_port();
    let s = start(cfg(p1)).await;
    let mut a = conn(&s.addr.to_string(), "alice").await;
    s.stop().await;
    a.wait_for(W, |e| matches!(e, Event::Closed | Event::Error(..))).await.unwrap();
    let p2 = free_port();
    let s2 = start(cfg(p2)).await;
    assert!(Client::connect(&format!("127.0.0.1:{p1}"), PASS, test_id("alice"), b"").await.is_err());
    let addr2 = format!("127.0.0.1:{p2}");
    let (mut a, _) = reconnect(&addr2, "alice").await;
    let mut b = conn(&addr2, "bob").await;
    wait_online(&mut a, "bob").await;
    a.send(&test_id("bob"), b"new-port").await.unwrap();
    assert_eq!(recv_blob(&mut b).await.1, b"new-port");
    s2.stop().await;
}

/// TCP proxy we can "unplug" to simulate a network drop.
async fn flaky_proxy(target: String) -> (u16, tokio::sync::watch::Sender<bool>) {
    let l = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = l.local_addr().unwrap().port();
    let (tx, rx) = tokio::sync::watch::channel(false);
    tokio::spawn(async move {
        loop {
            let Ok((mut inb, _)) = l.accept().await else { break };
            let target = target.clone();
            let mut rx = rx.clone();
            tokio::spawn(async move {
                let Ok(mut out) = TcpStream::connect(&target).await else { return };
                tokio::select! {
                    _ = tokio::io::copy_bidirectional(&mut inb, &mut out) => {},
                    _ = rx.changed() => {},   // unplug: drop both sockets abruptly
                }
            });
        }
    });
    (port, tx)
}

#[tokio::test]
async fn network_drop_and_reconnect() {
    let s = start(cfg(0)).await;
    let (pport, unplug) = flaky_proxy(s.addr.to_string()).await;
    let paddr = format!("127.0.0.1:{pport}");
    let mut a = conn(&s.addr.to_string(), "alice").await;
    let mut b = conn(&paddr, "bob").await; // bob is behind the flaky link
    wait_online(&mut a, "bob").await;
    unplug.send(true).unwrap();
    b.wait_for(W, |e| matches!(e, Event::Closed)).await.unwrap();
    match a.wait_for(W, |e| matches!(e, Event::Leave(_))).await.unwrap() {
        Event::Leave(i) => assert_eq!(i, test_id("bob")),
        _ => unreachable!(),
    }
    let (pport2, _keep) = flaky_proxy(s.addr.to_string()).await;
    let (mut b, _) = reconnect(&format!("127.0.0.1:{pport2}"), "bob").await;
    wait_online(&mut a, "bob").await;
    a.send(&test_id("bob"), b"back").await.unwrap();
    assert_eq!(recv_blob(&mut b).await.1, b"back");
    s.stop().await;
}

#[tokio::test]
async fn same_id_replaces_stale_session() {
    let s = start(cfg(0)).await;
    let addr = s.addr.to_string();
    let mut old = conn(&addr, "alice").await;
    let mut new = conn(&addr, "alice").await;
    match old.wait_for(W, |e| matches!(e, Event::Error(..) | Event::Closed)).await.unwrap() {
        Event::Error(c, _) => assert_eq!(c, code::REPLACED),
        Event::Closed => {}
        _ => unreachable!(),
    }
    let b = conn(&addr, "bob").await;
    b.send(&test_id("alice"), b"to-new").await.unwrap();
    assert_eq!(recv_blob(&mut new).await.1, b"to-new");
    s.stop().await;
}

#[tokio::test]
async fn heartbeat_keeps_alive_idle_drops_silent() {
    let mut c = cfg(0);
    c.limits.heartbeat_secs = 1;
    c.limits.idle_timeout_secs = 3;
    c.limits.auth_timeout_secs = 2;
    let s = start(c).await;
    let addr = s.addr.to_string();
    let mut a = conn(&addr, "alice").await;
    tokio::time::sleep(Duration::from_secs(5)).await;
    let mut b = conn(&addr, "bob").await;
    wait_online(&mut b, "alice").await; // alice survived thanks to heartbeats
    // a silent raw socket that never authenticates is dropped
    let mut raw = TcpStream::connect(&addr).await.unwrap();
    let mut buf = vec![0u8; 4096];
    let t0 = Instant::now();
    loop {
        match tokio::time::timeout(Duration::from_secs(6), raw.read(&mut buf)).await.expect("not dropped") {
            Ok(0) | Err(_) => break,
            Ok(_) => continue,
        }
    }
    assert!(t0.elapsed() < Duration::from_secs(5));
    let _ = a.list().await;
    s.stop().await;
}

#[tokio::test]
async fn max_connections_enforced() {
    let mut c = cfg(0);
    c.limits.max_connections = 3;
    let s = start(c).await;
    let addr = s.addr.to_string();
    let _a = conn(&addr, "a").await;
    let _b = conn(&addr, "b").await;
    let _c = conn(&addr, "c").await;
    let e = Client::connect(&addr, PASS, test_id("d"), b"").await.err().expect("full");
    assert!(is_auth_fail(&e, code::SERVER_FULL), "{e}");
    s.stop().await;
}

#[tokio::test]
async fn rate_limit_slows_sender() {
    let mut c = cfg(0);
    c.limits.rate_bytes_per_sec = 1024 * 1024;
    let s = start(c).await;
    let addr = s.addr.to_string();
    let a = conn(&addr, "alice").await;
    let mut b = conn(&addr, "bob").await;
    let chunk = vec![7u8; 256 * 1024];
    let t0 = Instant::now();
    for _ in 0..12 {
        a.send(&test_id("bob"), &chunk).await.unwrap();
    }
    for _ in 0..12 {
        recv_blob(&mut b).await;
    }
    let el = t0.elapsed();
    eprintln!("rate limit: 3 MiB at 1 MiB/s took {el:?}");
    assert!(el >= Duration::from_millis(1800), "{el:?}");
    s.stop().await;
}

#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn large_blob_stream_200mb() {
    let total: usize = std::env::var("RELAY_BIG_MB").ok().and_then(|v| v.parse().ok()).unwrap_or(200) * 1024 * 1024;
    let s = start(cfg(0)).await;
    let addr = s.addr.to_string();
    let a = conn(&addr, "alice").await;
    let mut b = conn(&addr, "bob").await;
    let chunk = a.max_payload();
    let t0 = Instant::now();
    let sender = tokio::spawn(async move {
        use blake2::{Blake2b512, Digest};
        let mut h = Blake2b512::new();
        let mut sent = 0usize;
        let mut i = 0u64;
        while sent < total {
            let n = chunk.min(total - sent);
            let mut buf = vec![0u8; n];
            for (j, x) in buf.iter_mut().enumerate() {
                *x = (i as usize * 31 + j) as u8;
            }
            h.update(&buf);
            a.send(&test_id("bob"), &buf).await.unwrap();
            sent += n;
            i += 1;
        }
        (h.finalize().to_vec(), a)
    });
    use blake2::{Blake2b512, Digest};
    let mut h = Blake2b512::new();
    let mut got = 0usize;
    while got < total {
        let (_, blob) = recv_blob(&mut b).await;
        got += blob.len();
        h.update(&blob);
    }
    let (sh, _a) = sender.await.unwrap();
    assert_eq!(h.finalize().to_vec(), sh, "hash mismatch");
    let el = t0.elapsed().as_secs_f64();
    eprintln!("large stream: {} MiB in {:.2}s = {:.1} MiB/s", total >> 20, el, (total >> 20) as f64 / el);
    s.stop().await;
}

#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn fifty_simultaneous_senders() {
    let senders = 50;
    let per = 200;
    let s = start(cfg(0)).await;
    let addr = s.addr.to_string();
    let mut sink = conn(&addr, "sink").await;
    let mut hs = Vec::new();
    for k in 0..senders {
        let addr = addr.clone();
        hs.push(tokio::spawn(async move {
            let c = conn(&addr, &format!("sender-{k}")).await;
            for i in 0..per {
                let mut m = vec![k as u8; 4096];
                m[..4].copy_from_slice(&(i as u32).to_be_bytes());
                c.send(&test_id("sink"), &m).await.unwrap();
            }
            c
        }));
    }
    let t0 = Instant::now();
    let mut counts = vec![0usize; senders];
    let mut last = vec![-1i64; senders];
    for _ in 0..senders * per {
        let (_, b) = recv_blob(&mut sink).await;
        let k = b[4] as usize;
        let i = u32::from_be_bytes(b[..4].try_into().unwrap()) as i64;
        assert_eq!(i, last[k] + 1, "per-sender order preserved");
        last[k] = i;
        counts[k] += 1;
    }
    for h in hs {
        h.await.unwrap();
    }
    assert!(counts.iter().all(|&c| c == per));
    eprintln!("50 senders: {} frames in {:?}", senders * per, t0.elapsed());
    s.stop().await;
}

#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn stress_long_run() {
    let secs: u64 = std::env::var("RELAY_STRESS_SECS").ok().and_then(|v| v.parse().ok()).unwrap_or(10);
    let s = start(cfg(0)).await;
    let addr = s.addr.to_string();
    let pairs = 10;
    let mut hs = Vec::new();
    let deadline = Instant::now() + Duration::from_secs(secs);
    for p in 0..pairs {
        let addr = addr.clone();
        hs.push(tokio::spawn(async move {
            let (na, nb) = (format!("sa{p}"), format!("sb{p}"));
            let mut a = conn(&addr, &na).await;
            let mut b = conn(&addr, &nb).await;
            wait_online(&mut a, &nb).await;
            let mut n = 0u64;
            let mut bytes = 0u64;
            let mut rng_len = 100 + p * 997;
            while Instant::now() < deadline {
                rng_len = (rng_len * 7919 + 13) % 60000 + 1;
                let m = vec![p as u8; rng_len];
                a.send(&test_id(&nb), &m).await.unwrap();
                let (_, got) = recv_blob(&mut b).await;
                assert_eq!(got.len(), rng_len);
                b.send(&test_id(&na), &got).await.unwrap();
                let (_, back) = recv_blob(&mut a).await;
                assert_eq!(back, m);
                n += 2;
                bytes += 2 * rng_len as u64;
                // churn: occasionally reconnect bob
                if n % 2000 == 0 {
                    drop(b);
                    b = reconnect(&addr, &nb).await.0;
                    wait_online(&mut a, &nb).await;
                }
            }
            (n, bytes)
        }));
    }
    let mut tot = (0u64, 0u64);
    for h in hs {
        let (n, b) = h.await.unwrap();
        tot.0 += n;
        tot.1 += b;
    }
    eprintln!("stress: {secs}s, {pairs} pairs, {} frames, {} MiB, 0 lost", tot.0, tot.1 >> 20);
    // server still healthy
    let mut x = conn(&addr, "x").await;
    let _y = conn(&addr, "y").await;
    wait_online(&mut x, "y").await;
    s.stop().await;
}

#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn fuzz_garbage_and_oversized_frames() {
    let iters: usize = std::env::var("RELAY_FUZZ_ITERS").ok().and_then(|v| v.parse().ok()).unwrap_or(1500);
    let mut c = cfg(0);
    c.limits.auth_failures_per_min = u32::MAX;
    c.limits.auth_timeout_secs = 1;
    c.limits.max_frame = 64 * 1024;
    let s = start(c).await;
    let addr = s.addr.to_string();
    let mut sink = conn(&addr, "sink").await;
    let mut seed: u64 = 0x9E3779B97F4A7C15;
    let mut rnd = move || {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        seed
    };
    let mut hs = Vec::new();
    for i in 0..iters {
        let kind = i % 6;
        let r1 = rnd();
        let len = (rnd() % 3000) as usize;
        let mut garbage = vec![0u8; len];
        for b in garbage.iter_mut() {
            *b = rnd() as u8;
        }
        let addr = addr.clone();
        hs.push(tokio::spawn(async move {
            let Ok(mut s) = TcpStream::connect(&addr).await else { return };
            let mut hello = [0u8; 70];
            let _ = s.read_exact(&mut hello[..4]).await;
            let _ = s.read(&mut hello[4..]).await;
            match kind {
                0 => { let _ = s.write_all(&garbage).await; } // pure garbage pre-auth
                1 => { let _ = s.write_all(&[0xff, 0xff, 0xff, 0xff, 1]).await; } // 4 GiB length
                2 => { let _ = s.write_all(&[0, 0, 0, 0]).await; } // zero length
                3 => { // valid length, truncated body, then hang up
                    let _ = s.write_all(&[0, 0, 0, 200, t::AUTH, 1, 2]).await;
                }
                4 => { // AUTH with absurd info length
                    let mut b = vec![1u8; 97];
                    b.extend_from_slice(&[0xff, 0xff]);
                    let _ = s.write_all(&Frame::new(t::AUTH, b).encode()).await;
                }
                _ => { // random type & body framed correctly
                    let _ = s.write_all(&Frame::new(r1 as u8, garbage).encode()).await;
                }
            }
            let mut buf = [0u8; 256];
            let _ = tokio::time::timeout(Duration::from_millis(1500), s.read(&mut buf)).await;
        }));
        if hs.len() >= 64 {
            for h in hs.drain(..) {
                h.await.unwrap();
            }
        }
    }
    for h in hs {
        h.await.unwrap();
    }
    // authenticated fuzz: garbage frames after login, incl. oversized
    for round in 0..50 {
        let g = conn(&addr, &format!("fuzzer{round}")).await;
        let n = (rnd() % 40000) as usize;
        let mut body = vec![0u8; n];
        for b in body.iter_mut() {
            *b = rnd() as u8;
        }
        let ty = [t::SEND, t::LIST, t::PING, 0x99, t::AUTH, t::DELIVER][round % 6];
        let _ = g.raw(Frame::new(ty, body).encode()).await;
        let _ = g.raw(Frame::new(t::SEND, vec![1, 2, 3]).encode()).await; // SEND shorter than an id
        if round % 5 == 0 {
            let _ = g.raw(vec![0x00, 0x10, 0x00, 0x00, t::SEND]).await; // 1 MiB > max_frame 64 KiB
        }
    }
    // still alive and correct
    let a = conn(&addr, "after-fuzz").await;
    a.send(&test_id("sink"), b"still alive").await.unwrap();
    loop {
        let (src, b) = recv_blob(&mut sink).await;
        if src == test_id("after-fuzz") {
            assert_eq!(b, b"still alive");
            break;
        }
    }
    eprintln!("fuzz: {iters} hostile connections + 50 authenticated garbage sessions, server alive");
    s.stop().await;
}

#[tokio::test]
async fn status_endpoint_reports_online() {
    let mut c = cfg(0);
    c.server.status_addr = "127.0.0.1:0".into();
    let s = start(c).await;
    let addr = s.addr.to_string();
    let _a = conn(&addr, "a").await;
    let mut b = conn(&addr, "b").await;
    wait_online(&mut b, "a").await;
    let j = server::query_status(&s.status_addr.unwrap().to_string()).await.unwrap();
    let v: serde_json::Value = serde_json::from_str(&j).unwrap();
    assert_eq!(v["online"], 2);
    assert_eq!(v["version"], server::VERSION);
    s.stop().await;
}

/// Cross-implementation test: C client <-> Rust client through the relay (POSIX C client).
#[cfg(unix)]
#[tokio::test(flavor = "multi_thread", worker_threads = 2)]
async fn interop_c_and_rust_clients() {
    let cbin = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("c-client/relay_client");
    if !cbin.exists() {
        let st = std::process::Command::new("make").current_dir(cbin.parent().unwrap()).status();
        if !st.map(|s| s.success()).unwrap_or(false) {
            eprintln!("SKIP: cannot build C client");
            return;
        }
    }
    let rbin = env!("CARGO_BIN_EXE_relay-test-client");
    let s = start(cfg(0)).await;
    let port = s.addr.port().to_string();
    let addr = s.addr.to_string();
    // C echo peer, Rust sender
    let mut cecho = tokio::process::Command::new(&cbin).args(["127.0.0.1", &port, PASS, "c-echo", "echo", "20"]).kill_on_drop(true).spawn().unwrap();
    let out = tokio::process::Command::new(rbin)
        .args(["--addr", &addr, "--password", PASS, "--name", "r-send", "--to", "c-echo", "--count", "64", "--size", "65536", "--timeout", "30"])
        .output().await.unwrap();
    let so = String::from_utf8_lossy(&out.stdout);
    assert!(out.status.success() && so.contains("OK roundtrip count=64"), "{so}{}", String::from_utf8_lossy(&out.stderr));
    // Rust echo peer, C sender
    let mut recho = tokio::process::Command::new(rbin).args(["--addr", &addr, "--password", PASS, "--name", "r-echo", "--echo", "--timeout", "20"]).kill_on_drop(true).spawn().unwrap();
    let out = tokio::process::Command::new(&cbin).args(["127.0.0.1", &port, PASS, "c-send", "send", "r-echo", "64", "65536"]).output().await.unwrap();
    let so = String::from_utf8_lossy(&out.stdout);
    assert!(out.status.success() && so.contains("OK roundtrip count=64"), "{so}{}", String::from_utf8_lossy(&out.stderr));
    // C client with wrong passphrase
    let out = tokio::process::Command::new(&cbin).args(["127.0.0.1", &port, "bad", "c-bad", "send", "r-echo", "1", "1"]).output().await.unwrap();
    assert_eq!(out.status.code(), Some(2));
    let _ = cecho.kill().await;
    let _ = recho.kill().await;
    s.stop().await;
}
