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

/// Derive retain blob key via static X25519 DH (E2E; relay sees opaque bytes only).
pub fn retain_shared_key(my_secret: &[u8; 32], peer_public: &[u8; 32]) -> [u8; 32] {
    use x25519_dalek::{PublicKey, StaticSecret};
    let sk = StaticSecret::from(*my_secret);
    let pk = PublicKey::from(*peer_public);
    let shared = sk.diffie_hellman(&pk);
    let mut h = blake3::Hasher::new();
    h.update(b"lsm-retain-v1");
    h.update(shared.as_bytes());
    *h.finalize().as_bytes()
}

pub fn seal_retain(my_secret: &[u8; 32], peer_public: &[u8; 32], plaintext: &[u8]) -> Result<Vec<u8>> {
    let key = retain_shared_key(my_secret, peer_public);
    seal_at_rest(&key, plaintext)
}

pub fn open_retain(my_secret: &[u8; 32], peer_public: &[u8; 32], data: &[u8]) -> Result<Vec<u8>> {
    let key = retain_shared_key(my_secret, peer_public);
    open_at_rest(&key, data)
}

#[cfg(test)]
mod retain_tests {
    use super::*;
    use x25519_dalek::{PublicKey, StaticSecret};

    #[test]
    fn seal_retain_roundtrip() {
        let a = StaticSecret::random_from_rng(rand::thread_rng());
        let b = StaticSecret::random_from_rng(rand::thread_rng());
        let a_pub = PublicKey::from(&a);
        let b_pub = PublicKey::from(&b);
        let ct = seal_retain(&a.to_bytes(), b_pub.as_bytes(), b"hello retain").unwrap();
        let pt = open_retain(&b.to_bytes(), a_pub.as_bytes(), &ct).unwrap();
        assert_eq!(pt, b"hello retain");
    }
}
