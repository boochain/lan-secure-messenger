//! Noise XX session helpers and at-rest history encryption.

use anyhow::{anyhow, Context, Result};
use chacha20poly1305::{
    aead::{Aead, KeyInit},
    ChaCha20Poly1305, Key, Nonce,
};
use rand::RngCore;
use snow::{Builder, HandshakeState, TransportState};
use zeroize::Zeroize;

/// Noise_XX_25519_ChaChaPoly_BLAKE2s — mutual auth + forward secrecy.
pub const NOISE_PATTERN: &str = "Noise_XX_25519_ChaChaPoly_BLAKE2s";

pub fn build_initiator(local: &snow::Keypair) -> Result<HandshakeState> {
    let builder = Builder::new(NOISE_PATTERN.parse().map_err(|e| anyhow!("{e}"))?);
    builder
        .local_private_key(&local.private)
        .build_initiator()
        .map_err(|e| anyhow!("noise initiator: {e}"))
}

pub fn build_responder(local: &snow::Keypair) -> Result<HandshakeState> {
    let builder = Builder::new(NOISE_PATTERN.parse().map_err(|e| anyhow!("{e}"))?);
    builder
        .local_private_key(&local.private)
        .build_responder()
        .map_err(|e| anyhow!("noise responder: {e}"))
}

pub fn handshake_write(state: &mut HandshakeState, payload: &[u8]) -> Result<Vec<u8>> {
    let mut buf = vec![0u8; 65535];
    let n = state
        .write_message(payload, &mut buf)
        .map_err(|e| anyhow!("noise write: {e}"))?;
    buf.truncate(n);
    Ok(buf)
}

pub fn handshake_read(state: &mut HandshakeState, msg: &[u8]) -> Result<Vec<u8>> {
    let mut buf = vec![0u8; 65535];
    let n = state
        .read_message(msg, &mut buf)
        .map_err(|e| anyhow!("noise read: {e}"))?;
    buf.truncate(n);
    Ok(buf)
}

pub fn into_transport(state: HandshakeState) -> Result<TransportState> {
    state
        .into_transport_mode()
        .map_err(|e| anyhow!("into transport: {e}"))
}

pub fn transport_encrypt(state: &mut TransportState, plaintext: &[u8]) -> Result<Vec<u8>> {
    let mut buf = vec![0u8; plaintext.len() + 16 + 64];
    let n = state
        .write_message(plaintext, &mut buf)
        .map_err(|e| anyhow!("encrypt: {e}"))?;
    buf.truncate(n);
    Ok(buf)
}

pub fn transport_decrypt(state: &mut TransportState, ciphertext: &[u8]) -> Result<Vec<u8>> {
    let mut buf = vec![0u8; ciphertext.len()];
    let n = state
        .read_message(ciphertext, &mut buf)
        .map_err(|e| anyhow!("decrypt: {e}"))?;
    buf.truncate(n);
    Ok(buf)
}

/// Generate a random 32-byte key for history encryption.
pub fn generate_history_key() -> [u8; 32] {
    let mut key = [0u8; 32];
    rand::thread_rng().fill_bytes(&mut key);
    key
}

/// Encrypt data at rest with ChaCha20-Poly1305 (random nonce prepended).
pub fn seal_at_rest(key: &[u8; 32], plaintext: &[u8]) -> Result<Vec<u8>> {
    let cipher = ChaCha20Poly1305::new(Key::from_slice(key));
    let mut nonce_bytes = [0u8; 12];
    rand::thread_rng().fill_bytes(&mut nonce_bytes);
    let nonce = Nonce::from_slice(&nonce_bytes);
    let ct = cipher
        .encrypt(nonce, plaintext)
        .map_err(|e| anyhow!("seal: {e}"))?;
    let mut out = Vec::with_capacity(12 + ct.len());
    out.extend_from_slice(&nonce_bytes);
    out.extend_from_slice(&ct);
    Ok(out)
}

pub fn open_at_rest(key: &[u8; 32], data: &[u8]) -> Result<Vec<u8>> {
    anyhow::ensure!(data.len() >= 12 + 16, "ciphertext too short");
    let (nonce_bytes, ct) = data.split_at(12);
    let cipher = ChaCha20Poly1305::new(Key::from_slice(key));
    let nonce = Nonce::from_slice(nonce_bytes);
    cipher
        .decrypt(nonce, ct)
        .map_err(|e| anyhow!("open: {e}"))
        .context("history decrypt failed")
}

pub fn zeroize_key(key: &mut [u8; 32]) {
    key.zeroize();
}
