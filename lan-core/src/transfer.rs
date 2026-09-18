//! Chunked file transfer over an encrypted session.

use crate::protocol::{FileMeta, WireMessage};
use crate::session::SecureSession;
use crate::CHUNK_SIZE;
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
) -> Result<FileMeta> {
    let name = path
        .file_name()
        .and_then(|n| n.to_str())
        .unwrap_or("file")
        .to_string();
    let (size, sha256) = hash_file(path).await?;
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
    }
    session.send_msg(&WireMessage::FileDone { id, ok: true }).await?;
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

    tokio::fs::create_dir_all(dest_dir).await?;
    let safe_name = sanitize_filename(&offer.name);
    let dest = unique_path(dest_dir, &safe_name);
    let mut file = File::create(&dest).await.context("create dest file")?;
    let mut received = 0u64;
    let mut hasher = Sha256::new();

    loop {
        match session.recv_msg().await? {
            WireMessage::FileChunk { id, offset, data } if id == offer.id => {
                anyhow::ensure!(offset == received, "out-of-order chunk");
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
    anyhow::ensure!(received == offer.size, "size mismatch");
    info!(path = %dest.display(), size = received, "file receive complete");
    Ok(dest)
}

fn sanitize_filename(name: &str) -> String {
    let base = Path::new(name)
        .file_name()
        .and_then(|n| n.to_str())
        .unwrap_or("file");
    let cleaned: String = base
        .chars()
        .map(|c| if r#"<>:"/\|?*"#.contains(c) { '_' } else { c })
        .collect();
    if cleaned.is_empty() {
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
