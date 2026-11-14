//! Fixed relay data directory and additive schema migration.
//!
//! Empty `data_dir` used to mean `./relay-data`, so a systemd unit with no
//! WorkingDirectory wrote accounts under `/` and an upgrade that changed the
//! working directory looked like a wiped server. The directory is now a fixed
//! path. Older trees are copied in; the source is never deleted.
//!
//! Schema versions use a `SCHEMA` file (same idea as SQLite `PRAGMA user_version`):
//! read it on startup, apply only missing additive steps, write the new number
//! after the step succeeds. A failed copy leaves the old tree untouched.
//! SCHEMA=1 writes the marker only. Device files live under users/<lookup>/devices/.
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

pub const SCHEMA: u32 = 1;

pub fn prepare(configured: &str) -> io::Result<PathBuf> {
    let explicit = !configured.is_empty();
    let dest = if explicit { PathBuf::from(configured) } else { default_dir() };
    fs::create_dir_all(&dest)?;
    if !explicit {
        import_legacy(&dest)?;
    }
    if read_schema(&dest) < SCHEMA {
        // v1 is the marker only. Device records are created later under
        // users/<lookup>/devices/ on login. Nothing is renamed or removed.
        fs::write(dest.join("SCHEMA"), format!("{SCHEMA}\n"))?;
    }
    Ok(dest)
}

fn read_schema(dir: &Path) -> u32 {
    fs::read_to_string(dir.join("SCHEMA")).ok().and_then(|s| s.trim().parse().ok()).unwrap_or(0)
}

fn default_dir() -> PathBuf {
    if let Ok(p) = std::env::var("LSM_RELAY_DATA") {
        if !p.is_empty() {
            return PathBuf::from(p);
        }
    }
    if cfg!(windows) {
        if let Ok(pd) = std::env::var("ProgramData") {
            if !pd.is_empty() {
                return PathBuf::from(pd).join("LsmRelay").join("data");
            }
        }
        if let Ok(local) = std::env::var("LOCALAPPDATA") {
            if !local.is_empty() {
                return PathBuf::from(local).join("LsmRelay").join("data");
            }
        }
    } else if writable(&PathBuf::from("/var/lib/lsm-relay")) {
        return PathBuf::from("/var/lib/lsm-relay/data");
    }
    if let Ok(home) = std::env::var("HOME") {
        if !home.is_empty() {
            return PathBuf::from(home).join(".lsm-relay").join("state").join("data");
        }
    }
    PathBuf::from("relay-data")
}

fn writable(dir: &Path) -> bool {
    if fs::create_dir_all(dir).is_err() {
        return false;
    }
    let probe = dir.join(".write-probe");
    match fs::write(&probe, b"1") {
        Ok(()) => {
            let _ = fs::remove_file(probe);
            true
        }
        Err(_) => false,
    }
}

fn import_legacy(dest: &Path) -> io::Result<()> {
    if read_schema(dest) >= SCHEMA {
        return Ok(());
    }
    if !dir_is_empty(&dest.join("users")) {
        return Ok(());
    }
    let dest_c = fs::canonicalize(dest).unwrap_or_else(|_| dest.to_path_buf());
    for src in legacy_candidates() {
        if !src.join("users").is_dir() {
            continue;
        }
        let src_c = fs::canonicalize(&src).unwrap_or_else(|_| src.clone());
        if src_c == dest_c {
            continue;
        }
        copy_tree(&src, dest)?;
        fs::write(dest.join("MIGRATED_FROM"), format!("{}\n", src.display()))?;
        break;
    }
    Ok(())
}

fn legacy_candidates() -> Vec<PathBuf> {
    let mut v = vec![
        PathBuf::from("relay-data"),
        PathBuf::from("/relay-data"),
        PathBuf::from("/var/lib/lsm-relay/relay-data"),
        PathBuf::from("/opt/lsm-relay/relay-data"),
    ];
    if let Ok(home) = std::env::var("HOME") {
        v.push(PathBuf::from(home).join(".lsm-relay").join("relay-data"));
    }
    v
}

fn dir_is_empty(p: &Path) -> bool {
    let Ok(rd) = fs::read_dir(p) else { return true };
    rd.flatten().next().is_none()
}

fn copy_tree(src: &Path, dst: &Path) -> io::Result<()> {
    fs::create_dir_all(dst)?;
    for ent in fs::read_dir(src)? {
        let ent = ent?;
        let name = ent.file_name();
        let to = dst.join(&name);
        let meta = ent.file_type()?;
        if meta.is_dir() {
            copy_tree(&ent.path(), &to)?;
        } else if meta.is_file() {
            if to.exists() {
                // Never replace a file the new tree already has.
                continue;
            }
            fs::copy(ent.path(), &to)?;
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    #[test]
    fn copies_legacy_and_keeps_the_source() {
        let n = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos();
        let root = std::env::temp_dir().join(format!("lsm-dd-{n}"));
        let legacy = root.join("relay-data");
        let dest = root.join("fixed");
        fs::create_dir_all(legacy.join("users").join("alice")).unwrap();
        fs::write(legacy.join("users").join("alice").join("identity.bak"), b"LSMBAK02keep").unwrap();
        // Point the importer at this legacy tree by running from `root` is not required:
        // call the pieces directly.
        fs::create_dir_all(&dest).unwrap();
        copy_tree(&legacy, &dest).unwrap();
        fs::write(dest.join("SCHEMA"), b"1\n").unwrap();
        assert_eq!(fs::read(dest.join("users/alice/identity.bak")).unwrap(), b"LSMBAK02keep");
        assert_eq!(fs::read(legacy.join("users/alice/identity.bak")).unwrap(), b"LSMBAK02keep");
        assert_eq!(read_schema(&dest), 1);
        let _ = fs::remove_dir_all(root);
    }
}
