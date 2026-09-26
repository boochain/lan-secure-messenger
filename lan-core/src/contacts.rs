//! Persistent contact / trust store (TOFU), known addresses, manual peers, mute flags.

use crate::config::ConfigPaths;
use crate::util::atomic_write;
use anyhow::Result;
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::fs;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default)]
#[serde(rename_all = "snake_case")]
pub enum Trust {
    #[default]
    Unknown,
    Trusted,
    Blocked,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
#[serde(default)]
pub struct Contact {
    pub fingerprint: String,
    pub name: String,
    pub trust: Trust,
    /// Hex ed25519 public key (from Hello), used to verify group signatures.
    pub ed25519_pk: String,
    /// Last addresses we successfully connected on / were announced from (`ip:port`), newest first.
    pub last_addrs: Vec<String>,
    pub last_seen_ms: i64,
    pub muted: bool,
}

#[derive(Debug, Default, Serialize, Deserialize)]
#[serde(default)]
struct ContactsFile {
    contacts: Vec<Contact>,
    /// Manually added addresses (`host[:port]`), redialed forever.
    manual: Vec<String>,
    muted_groups: Vec<String>,
}

pub struct ContactStore {
    pub contacts: BTreeMap<String, Contact>,
    pub manual: Vec<String>,
    pub muted_groups: Vec<String>,
    path: std::path::PathBuf,
}

impl ContactStore {
    pub fn load(paths: &ConfigPaths) -> Result<Self> {
        let mut s = Self { contacts: BTreeMap::new(), manual: vec![], muted_groups: vec![], path: paths.contacts.clone() };
        if paths.contacts.exists() {
            let f: ContactsFile = serde_json::from_slice(&fs::read(&paths.contacts)?).unwrap_or_default();
            for c in f.contacts {
                s.contacts.insert(c.fingerprint.clone(), c);
            }
            s.manual = f.manual;
            s.muted_groups = f.muted_groups;
        } else if paths.legacy_peers.exists() {
            // Best-effort import of the v0.3 trust store (fingerprint/name/trust only).
            if let Ok(v) = serde_json::from_slice::<serde_json::Value>(&fs::read(&paths.legacy_peers)?) {
                if let Some(arr) = v.get("peers").and_then(|p| p.as_array()) {
                    for p in arr {
                        let fp = p.get("fingerprint").and_then(|x| x.as_str()).unwrap_or_default().to_string();
                        if fp.is_empty() {
                            continue;
                        }
                        let trust = match p.get("trust").and_then(|x| x.as_str()) {
                            Some("Trusted") | Some("trusted") => Trust::Trusted,
                            Some("Blocked") | Some("blocked") => Trust::Blocked,
                            _ => Trust::Unknown,
                        };
                        let name = p.get("display_name").and_then(|x| x.as_str()).unwrap_or("").to_string();
                        s.contacts.insert(fp.clone(), Contact { fingerprint: fp, name, trust, ..Default::default() });
                    }
                }
            }
            let _ = s.save();
        }
        Ok(s)
    }

    pub fn save(&self) -> Result<()> {
        let f = ContactsFile {
            contacts: self.contacts.values().cloned().collect(),
            manual: self.manual.clone(),
            muted_groups: self.muted_groups.clone(),
        };
        atomic_write(&self.path, &serde_json::to_vec_pretty(&f)?)
    }

    pub fn get(&self, fp: &str) -> Option<&Contact> {
        self.contacts.get(fp)
    }

    pub fn trust(&self, fp: &str) -> Trust {
        self.contacts.get(fp).map(|c| c.trust).unwrap_or_default()
    }

    pub fn entry(&mut self, fp: &str) -> &mut Contact {
        self.contacts.entry(fp.to_string()).or_insert_with(|| Contact { fingerprint: fp.to_string(), ..Default::default() })
    }

    /// Remember an address (newest first, max 4).
    pub fn note_addr(&mut self, fp: &str, addr: &str) -> bool {
        let c = self.entry(fp);
        if c.last_addrs.first().map(|a| a == addr).unwrap_or(false) {
            return false;
        }
        c.last_addrs.retain(|a| a != addr);
        c.last_addrs.insert(0, addr.to_string());
        c.last_addrs.truncate(4);
        true
    }
}
