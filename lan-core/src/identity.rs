use crate::config::ConfigPaths;
use anyhow::{Context, Result};
use ed25519_dalek::{Signature, Signer, SigningKey, Verifier, VerifyingKey};
use rand::rngs::OsRng;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::fmt;
use std::fs;
use std::sync::Arc;
use x25519_dalek::{PublicKey as X25519Public, StaticSecret};

/// Human-readable fingerprint of an identity public key.
#[derive(Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct Fingerprint(pub String);

impl Fingerprint {
    pub fn from_x25519(pk: &[u8; 32]) -> Self {
        let hash = Sha256::digest(pk);
        let hex = hex::encode(&hash[..8]).to_uppercase();
        let parts: Vec<_> = hex
            .as_bytes()
            .chunks(4)
            .map(|c| std::str::from_utf8(c).unwrap())
            .collect();
        Self(parts.join("-"))
    }

    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for Fingerprint {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.0)
    }
}

impl fmt::Debug for Fingerprint {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Fingerprint({})", self.0)
    }
}

struct IdentityInner {
    x25519_secret: StaticSecret,
    x25519_public: X25519Public,
    ed25519_signing: SigningKey,
    ed25519_verifying: VerifyingKey,
    fingerprint: Fingerprint,
}

/// Cloneable identity handle (secrets behind Arc).
#[derive(Clone)]
pub struct Identity {
    inner: Arc<IdentityInner>,
}

#[derive(Serialize, Deserialize)]
struct IdentityFile {
    x25519_secret: String,
    ed25519_secret: String,
}

impl Identity {
    pub fn generate() -> Self {
        let mut rng = OsRng;
        let x25519_secret = StaticSecret::random_from_rng(&mut rng);
        let x25519_public = X25519Public::from(&x25519_secret);
        let ed25519_signing = SigningKey::generate(&mut rng);
        let ed25519_verifying = ed25519_signing.verifying_key();
        let fingerprint = Fingerprint::from_x25519(x25519_public.as_bytes());
        Self {
            inner: Arc::new(IdentityInner {
                x25519_secret,
                x25519_public,
                ed25519_signing,
                ed25519_verifying,
                fingerprint,
            }),
        }
    }

    pub fn load_or_create(paths: &ConfigPaths) -> Result<Self> {
        paths.ensure_dirs()?;
        if paths.identity.exists() {
            Self::load(paths)
        } else {
            let id = Self::generate();
            id.save(paths)?;
            Ok(id)
        }
    }

    pub fn load(paths: &ConfigPaths) -> Result<Self> {
        let data = fs::read_to_string(&paths.identity).context("read identity")?;
        let file: IdentityFile = serde_json::from_str(&data)?;
        let x_bytes = hex::decode(&file.x25519_secret).context("decode x25519")?;
        let e_bytes = hex::decode(&file.ed25519_secret).context("decode ed25519")?;
        anyhow::ensure!(x_bytes.len() == 32 && e_bytes.len() == 32, "bad key length");
        let mut x_arr = [0u8; 32];
        let mut e_arr = [0u8; 32];
        x_arr.copy_from_slice(&x_bytes);
        e_arr.copy_from_slice(&e_bytes);
        let x25519_secret = StaticSecret::from(x_arr);
        let x25519_public = X25519Public::from(&x25519_secret);
        let ed25519_signing = SigningKey::from_bytes(&e_arr);
        let ed25519_verifying = ed25519_signing.verifying_key();
        let fingerprint = Fingerprint::from_x25519(x25519_public.as_bytes());
        Ok(Self {
            inner: Arc::new(IdentityInner {
                x25519_secret,
                x25519_public,
                ed25519_signing,
                ed25519_verifying,
                fingerprint,
            }),
        })
    }

    pub fn save(&self, paths: &ConfigPaths) -> Result<()> {
        paths.ensure_dirs()?;
        let file = IdentityFile {
            x25519_secret: hex::encode(self.inner.x25519_secret.to_bytes()),
            ed25519_secret: hex::encode(self.inner.ed25519_signing.to_bytes()),
        };
        let data = serde_json::to_string_pretty(&file)?;
        #[cfg(unix)]
        {
            use std::io::Write;
            use std::os::unix::fs::OpenOptionsExt;
            let mut f = fs::OpenOptions::new()
                .write(true)
                .create(true)
                .truncate(true)
                .mode(0o600)
                .open(&paths.identity)?;
            f.write_all(data.as_bytes())?;
        }
        #[cfg(not(unix))]
        {
            fs::write(&paths.identity, data)?;
        }
        Ok(())
    }

    pub fn fingerprint(&self) -> &Fingerprint {
        &self.inner.fingerprint
    }

    pub fn sign(&self, msg: &[u8]) -> [u8; 64] {
        self.inner.ed25519_signing.sign(msg).to_bytes()
    }

    pub fn verify(pk: &VerifyingKey, msg: &[u8], sig: &[u8; 64]) -> bool {
        let sig = Signature::from_bytes(sig);
        pk.verify(msg, &sig).is_ok()
    }

    pub fn x25519_public_bytes(&self) -> [u8; 32] {
        *self.inner.x25519_public.as_bytes()
    }

    pub fn ed25519_public_bytes(&self) -> [u8; 32] {
        self.inner.ed25519_verifying.to_bytes()
    }

    pub fn snow_private(&self) -> snow::Keypair {
        snow::Keypair {
            private: self.inner.x25519_secret.to_bytes().to_vec(),
            public: self.inner.x25519_public.as_bytes().to_vec(),
        }
    }
}
