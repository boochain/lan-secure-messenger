use crate::config::ConfigPaths;
use crate::identity::Fingerprint;
use anyhow::{Context, Result};
use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs;
use std::net::SocketAddr;
use std::time::{Duration, Instant};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum TrustState {
    Unknown,
    Trusted,
    Blocked,
}

/// What changed when a discovery announce was applied.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DiscoveryChange {
    /// Peer newly discovered (or first live sighting this run).
    CameOnline,
    /// Already online; metadata refreshed (name/addr) — do not spam logs.
    Refreshed,
    /// Metadata meaningful change while online.
    Updated,
}


#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PeerRecord {
    pub fingerprint: String,
    pub display_name: String,
    pub x25519_pk: [u8; 32],
    pub ed25519_pk: [u8; 32],
    pub trust: TrustState,
    pub first_seen: DateTime<Utc>,
    pub last_seen: DateTime<Utc>,
}

#[derive(Debug, Clone)]
pub struct PeerInfo {
    pub fingerprint: Fingerprint,
    pub display_name: String,
    pub x25519_pk: [u8; 32],
    pub ed25519_pk: [u8; 32],
    pub addr: SocketAddr,
    pub listen_port: u16,
    pub trust: TrustState,
    pub last_seen: Instant,
    pub online: bool,
}

#[derive(Debug, Default, Serialize, Deserialize)]
struct PeerFile {
    peers: Vec<PeerRecord>,
}

#[derive(Debug, Default)]
pub struct PeerStore {
    records: HashMap<String, PeerRecord>,
    live: HashMap<String, PeerInfo>,
}

impl PeerStore {
    pub fn load(paths: &ConfigPaths) -> Result<Self> {
        let mut store = Self::default();
        if paths.peers.exists() {
            let data = fs::read_to_string(&paths.peers).context("read peers")?;
            let file: PeerFile = serde_json::from_str(&data)?;
            for r in file.peers {
                store.records.insert(r.fingerprint.clone(), r);
            }
        }
        Ok(store)
    }

    pub fn save(&self, paths: &ConfigPaths) -> Result<()> {
        paths.ensure_dirs()?;
        let file = PeerFile {
            peers: self.records.values().cloned().collect(),
        };
        fs::write(&paths.peers, serde_json::to_string_pretty(&file)?)?;
        Ok(())
    }

    pub fn upsert_discovery(
        &mut self,
        fp: Fingerprint,
        display_name: String,
        x25519_pk: [u8; 32],
        ed25519_pk: [u8; 32],
        addr: SocketAddr,
        listen_port: u16,
        auto_trust: bool,
    ) -> (PeerInfo, bool, DiscoveryChange) {
        let now = Utc::now();
        let is_new = !self.records.contains_key(fp.as_str());
        let trust = if let Some(r) = self.records.get(fp.as_str()) {
            r.trust
        } else if auto_trust {
            TrustState::Trusted
        } else {
            TrustState::Unknown
        };

        let prev = self.live.get(fp.as_str()).cloned();
        let was_online = prev.as_ref().map(|p| p.online).unwrap_or(false);
        let name_changed = prev
            .as_ref()
            .map(|p| p.display_name != display_name)
            .unwrap_or(false);
        let addr_changed = prev
            .as_ref()
            .map(|p| p.addr != addr || p.listen_port != listen_port)
            .unwrap_or(false);

        let record = PeerRecord {
            fingerprint: fp.as_str().to_string(),
            display_name: display_name.clone(),
            x25519_pk,
            ed25519_pk,
            trust,
            first_seen: self
                .records
                .get(fp.as_str())
                .map(|r| r.first_seen)
                .unwrap_or(now),
            last_seen: now,
        };
        self.records.insert(fp.as_str().to_string(), record);

        let info = PeerInfo {
            fingerprint: fp.clone(),
            display_name,
            x25519_pk,
            ed25519_pk,
            addr,
            listen_port,
            trust,
            last_seen: Instant::now(),
            online: true,
        };
        self.live.insert(fp.as_str().to_string(), info.clone());

        // Address can jitter on multi-homed hosts; only treat name changes as "Updated".
        let change = if !was_online {
            DiscoveryChange::CameOnline
        } else if name_changed {
            DiscoveryChange::Updated
        } else {
            let _ = addr_changed; // still refresh last_seen/addr silently
            DiscoveryChange::Refreshed
        };
        (info, is_new, change)
    }

    pub fn set_trust(&mut self, fp: &str, trust: TrustState) -> bool {
        let prev = self.records.get(fp).map(|r| r.trust);
        if let Some(r) = self.records.get_mut(fp) {
            r.trust = trust;
        }
        if let Some(l) = self.live.get_mut(fp) {
            l.trust = trust;
        }
        prev != Some(trust)
    }

    pub fn get_live(&self, fp: &str) -> Option<&PeerInfo> {
        self.live.get(fp)
    }

    pub fn list_online(&self) -> Vec<PeerInfo> {
        self.live.values().filter(|p| p.online).cloned().collect()
    }

    pub fn list_all(&self) -> Vec<PeerInfo> {
        self.live.values().cloned().collect()
    }

    pub fn mark_offline_stale(&mut self, timeout: Duration) -> Vec<String> {
        let mut gone = Vec::new();
        for (fp, p) in self.live.iter_mut() {
            if p.online && p.last_seen.elapsed() > timeout {
                p.online = false;
                gone.push(fp.clone());
            }
        }
        gone
    }

    pub fn is_trusted(&self, fp: &str) -> bool {
        self.records
            .get(fp)
            .map(|r| r.trust == TrustState::Trusted)
            .unwrap_or(false)
    }

    pub fn get_record(&self, fp: &str) -> Option<&PeerRecord> {
        self.records.get(fp)
    }
}
