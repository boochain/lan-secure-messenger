//! Chunked file transfer over an encrypted session.

use crate::protocol::{FileMeta, WireMessage};
use crate::session::SecureSession;
use crate::CHUNK_SIZE;

/// Reject chunks larger than this (CHUNK_SIZE + small slack).
pub const MAX_CHUNK_BYTES: usize = CHUNK_SIZE + 4096;
/// Hard cap for a single inbound file (also gated by AppConfig.max_file_bytes).
pub const ABSOLUTE_MAX_FILE_BYTES: u64 = 2 * 1024 * 1024 * 1024; // 2 GiB
use anyhow::{Context, Result};
use sha2::{Digest, Sha256};
use std::path::{Path, PathBuf};
use tokio::fs::File;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tracing::info;
use uuid::Uuid;

pub fn mime_guess(path: &Path) -> String {
    match path
        .extension()
        .and_then(|e| e.to_str())
        .map(|s| s.to_lowercase())
        .as_deref()
    {
        Some("png") => "image/png".into(),
        Some("jpg") | Some("jpeg") => "image/jpeg".into(),
        Some("gif") => "image/gif".into(),
        Some("webp") => "image/webp".into(),
        Some("txt") => "text/plain".into(),
        Some("pdf") => "application/pdf".into(),
        Some("zip") => "application/zip".into(),
        _ => "application/octet-stream".into(),
    }
}

pub fn is_image_mime(mime: &str) -> bool {
    mime.starts_with("image/")
}

pub async fn hash_file(path: &Path) -> Result<(u64, String)> {
    let mut f = File::open(path).await?;
    let mut hasher = Sha256::new();
    let mut buf = vec![0u8; 128 * 1024];
    let mut size = 0u64;
    loop {
        let n = f.read(&mut buf).await?;
        if n == 0 {
            break;
        }
        hasher.update(&buf[..n]);
        size += n as u64;
    }
    Ok((size, hex::encode(hasher.finalize())))
}

pub async fn send_file(
    session: &mut SecureSession,
    path: &Path,
    progress: &mut (dyn FnMut(u64, u64) + Send),
    should_cancel: &mut (dyn FnMut() -> bool + Send),
) -> Result<FileMeta> {
    let name = path
        .file_name()
        .and_then(|n| n.to_str())
        .unwrap_or("file")
        .to_string();
    let (size, sha256) = hash_file(path).await?;
    anyhow::ensure!(size <= ABSOLUTE_MAX_FILE_BYTES, "file too large: {size} bytes");
    let mime = mime_guess(path);
    let id = Uuid::new_v4();
    let meta = FileMeta {
        id,
        name: name.clone(),
        size,
        mime: mime.clone(),
        sha256: sha256.clone(),
    };
    let ts_ms = chrono::Utc::now().timestamp_millis();
    session
        .send_msg(&WireMessage::FileOffer {
            id,
            name,
            size,
            mime,
            sha256,
            ts_ms,
        })
        .await?;

    // Wait for accept
    loop {
        match session.recv_msg().await? {
            WireMessage::FileAccept { id: aid, accept } if aid == id => {
                if !accept {
                    anyhow::bail!("peer rejected file");
                }
                break;
            }
            WireMessage::FileCancel { id: cid } if cid == id => {
                anyhow::bail!("transfer cancelled before accept");
            }
            WireMessage::Ping { ts_ms } => {
                session.send_msg(&WireMessage::Pong { ts_ms }).await?;
            }
            other => {
                tracing::debug!(?other, "ignoring while waiting file accept");
            }
        }
    }

    let mut file = File::open(path).await?;
    let mut buf = vec![0u8; CHUNK_SIZE];
    let mut offset = 0u64;
    while offset < size {
        if should_cancel() {
            session.send_msg(&WireMessage::FileCancel { id }).await?;
            anyhow::bail!("transfer cancelled");
        }
        let n = file.read(&mut buf).await?;
        if n == 0 {
            break;
        }
        session
            .send_msg(&WireMessage::FileChunk {
                id,
                offset,
                data: buf[..n].to_vec(),
            })
            .await?;
        offset += n as u64;
        progress(offset, size);
        // Drain optional acks / cancel from peer
        // (non-blocking not available; rely on receiver FileAck path in separate msgs if interleaved)
    }
    session.send_msg(&WireMessage::FileDone { id, ok: true }).await?;
    // Give the receiver a moment to finish hashing / writing before we drop TCP.
    // Best-effort drain of trailing FileAcks (Noise is duplex-independent, but
    // closing too fast can RST before the last frames are read).
    let _ = tokio::time::timeout(std::time::Duration::from_millis(500), async {
        loop {
            match session.recv_msg().await {
                Ok(WireMessage::FileAck { .. }) => continue,
                Ok(WireMessage::Ping { ts_ms }) => {
                    let _ = session.send_msg(&WireMessage::Pong { ts_ms }).await;
                }
                _ => break,
            }
        }
    })
    .await;
    // Half-close write side politely if possible
    tokio::time::sleep(std::time::Duration::from_millis(50)).await;
    info!(file = %meta.name, size, "file send complete");
    Ok(meta)
}

pub async fn receive_file(
    session: &mut SecureSession,
    offer: FileMeta,
    dest_dir: &Path,
    auto_accept: bool,
    progress: &mut (dyn FnMut(u64, u64) + Send),
) -> Result<PathBuf> {
    session
        .send_msg(&WireMessage::FileAccept {
            id: offer.id,
            accept: auto_accept,
        })
        .await?;
    if !auto_accept {
        anyhow::bail!("file rejected locally");
    }

    anyhow::ensure!(
        offer.size <= ABSOLUTE_MAX_FILE_BYTES,
        "file too large: {} bytes",
        offer.size
    );
    tokio::fs::create_dir_all(dest_dir).await?;
    let safe_name = sanitize_filename(&offer.name);
    let dest = unique_path(dest_dir, &safe_name);
    let part = dest_dir.join(format!(
        "{}.part",
        dest.file_name().and_then(|s| s.to_str()).unwrap_or("download")
    ));

    let cleanup = |p: &Path| {
        let _ = std::fs::remove_file(p);
    };

    let mut file = match File::create(&part).await {
        Ok(f) => f,
        Err(e) => return Err(e).context("create temp download"),
    };
    let mut received = 0u64;
    let mut hasher = Sha256::new();

    let result = async {
        loop {
            match session.recv_msg().await? {
                WireMessage::FileChunk { id, offset, data } if id == offer.id => {
                    anyhow::ensure!(data.len() <= MAX_CHUNK_BYTES, "chunk too large: {}", data.len());
                    anyhow::ensure!(offset == received, "out-of-order chunk at {offset}");
                    anyhow::ensure!(received + data.len() as u64 <= offer.size, "chunk exceeds declared size");
                    file.write_all(&data).await?;
                    hasher.update(&data);
                    received += data.len() as u64;
                    progress(received, offer.size);
                    session
                        .send_msg(&WireMessage::FileAck {
                            id,
                            received,
                        })
                        .await?;
                }
                WireMessage::FileDone { id, ok } if id == offer.id => {
                    anyhow::ensure!(ok, "sender aborted");
                    break;
                }
                WireMessage::FileCancel { id } if id == offer.id => {
                    anyhow::bail!("transfer cancelled by peer");
                }
                WireMessage::Ping { ts_ms } => {
                    session.send_msg(&WireMessage::Pong { ts_ms }).await?;
                }
                other => {
                    tracing::debug!(?other, "ignoring during file recv");
                }
            }
        }
        file.flush().await?;
        drop(file);
        let digest = hex::encode(hasher.finalize());
        anyhow::ensure!(
            digest == offer.sha256,
            "sha256 mismatch: expected {} got {}",
            offer.sha256,
            digest
        );
        anyhow::ensure!(received == offer.size, "size mismatch: got {received} expected {}", offer.size);
        tokio::fs::rename(&part, &dest)
            .await
            .with_context(|| format!("finalize {} -> {}", part.display(), dest.display()))?;
        Ok::<_, anyhow::Error>(dest.clone())
    }
    .await;

    match result {
        Ok(p) => {
            info!(path = %p.display(), size = received, "file receive complete");
            Ok(p)
        }
        Err(e) => {
            cleanup(&part);
            let _ = tokio::fs::remove_file(&dest).await; // in case of partial rename races
            Err(e)
        }
    }
}

fn sanitize_filename(name: &str) -> String {
    let base = Path::new(name)
        .file_name()
        .and_then(|n| n.to_str())
        .unwrap_or("file");
    let cleaned: String = base
        .chars()
        .map(|c| {
            if c.is_control() || r#"<>:"/\|?*"#.contains(c) || c == '\0' {
                '_'
            } else {
                c
            }
        })
        .take(180)
        .collect();
    let cleaned = cleaned.trim().trim_start_matches('.').to_string();
    if cleaned.is_empty() || cleaned == "." || cleaned == ".." {
        "file".into()
    } else {
        cleaned
    }
}

fn unique_path(dir: &Path, name: &str) -> PathBuf {
    let p = dir.join(name);
    if !p.exists() {
        return p;
    }
    let stem = Path::new(name)
        .file_stem()
        .and_then(|s| s.to_str())
        .unwrap_or("file");
    let ext = Path::new(name)
        .extension()
        .and_then(|e| e.to_str())
        .map(|e| format!(".{e}"))
        .unwrap_or_default();
    for i in 1..10000 {
        let candidate = dir.join(format!("{stem}_{i}{ext}"));
        if !candidate.exists() {
            return candidate;
        }
    }
    dir.join(format!("{stem}_{}{ext}", Uuid::new_v4()))
}
