//! relay-test-client — tiny Rust test peer.  Blobs are sealed with
//! XChaCha20-Poly1305 (nonce24 || ct || tag16) under a test key shared by the
//! two peers, standing in for real Noise transport messages: the relay only
//! ever sees ciphertext.
use anyhow::{bail, Result};
use chacha20poly1305::aead::{Aead, KeyInit};
use chacha20poly1305::{XChaCha20Poly1305, XNonce};
use clap::Parser;
use rand::RngCore;
use relay::client::{test_id, Client, Event};
use std::time::{Duration, Instant};

#[derive(Parser)]
struct A {
    #[arg(long, default_value = "127.0.0.1:9443")]
    addr: String,
    #[arg(long)]
    password: String,
    #[arg(long)]
    name: String,
    /// Echo every received blob back to its sender
    #[arg(long)]
    echo: bool,
    /// Peer name to send to (waits for it to come online)
    #[arg(long)]
    to: Option<String>,
    #[arg(long, default_value_t = 10)]
    count: usize,
    #[arg(long, default_value_t = 1024)]
    size: usize,
    /// Exit after this many seconds (echo mode)
    #[arg(long, default_value_t = 60)]
    timeout: u64,
    /// E2E test key (hex 32 bytes)
    #[arg(long, default_value = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f")]
    e2e_key: String,
}

fn seal(c: &XChaCha20Poly1305, pt: &[u8]) -> Vec<u8> {
    let mut n = [0u8; 24];
    rand::thread_rng().fill_bytes(&mut n);
    let mut out = n.to_vec();
    out.extend(c.encrypt(XNonce::from_slice(&n), pt).expect("seal"));
    out
}
fn open(c: &XChaCha20Poly1305, b: &[u8]) -> Result<Vec<u8>> {
    if b.len() < 40 { bail!("short blob"); }
    c.decrypt(XNonce::from_slice(&b[..24]), &b[24..]).map_err(|_| anyhow::anyhow!("解密失败 (decrypt failed)"))
}

#[tokio::main]
async fn main() -> Result<()> {
    let a = A::parse();
    let key = hex::decode(&a.e2e_key)?;
    let aead = XChaCha20Poly1305::new_from_slice(&key)?;
    let me = test_id(&a.name);
    let mut c = Client::connect(&a.addr, &a.password, me, a.name.as_bytes()).await?;
    println!("READY name={} id={}", a.name, hex::encode(&me[..6]));
    let deadline = Instant::now() + Duration::from_secs(a.timeout);
    if let Some(to) = &a.to {
        let dst = test_id(to);
        // wait for presence
        c.wait_for(Duration::from_secs(a.timeout), |e| match e {
            Event::PeerList(v) => v.iter().any(|(i, _)| *i == dst),
            Event::Join(i, _) => *i == dst,
            _ => false,
        }).await?;
        println!("PEER_ONLINE {to}");
        let t0 = Instant::now();
        let mut got = 0usize;
        for i in 0..a.count {
            let mut pt = vec![0u8; a.size];
            pt[..8.min(a.size)].copy_from_slice(&(i as u64).to_be_bytes()[..8.min(a.size)]);
            c.send(&dst, &seal(&aead, &pt)).await?;
        }
        while got < a.count {
            let ev = c.wait_for(deadline.saturating_duration_since(Instant::now()), |e| matches!(e, Event::Deliver(..) | Event::SendFail(..))).await?;
            match ev {
                Event::Deliver(src, b) if src == dst => {
                    let pt = open(&aead, &b)?;
                    if pt.len() != a.size { bail!("size mismatch"); }
                    got += 1;
                }
                Event::SendFail(_, code) => bail!("SEND_FAIL code={code}"),
                _ => {}
            }
        }
        println!("OK roundtrip count={} size={} ms={}", got, a.size, t0.elapsed().as_millis());
        c.bye().await;
        tokio::time::sleep(Duration::from_millis(100)).await;
        return Ok(());
    }
    // echo / listen mode
    let mut n = 0u64;
    loop {
        let left = deadline.saturating_duration_since(Instant::now());
        let ev = match c.wait_for(left, |_| true).await { Ok(e) => e, Err(_) => break };
        match ev {
            Event::PeerList(v) => println!("PEER_LIST {}", v.len()),
            Event::Join(_, info) => println!("JOIN {}", String::from_utf8_lossy(&info)),
            Event::Leave(i) => println!("LEAVE {}", hex::encode(&i[..6])),
            Event::Deliver(src, b) => {
                let pt = open(&aead, &b)?;
                n += 1;
                if a.echo {
                    c.send(&src, &seal(&aead, &pt)).await?;
                }
            }
            Event::Closed => break,
            _ => {}
        }
    }
    println!("DONE received={n}");
    Ok(())
}
