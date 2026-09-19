//! Opt-in shared folder: browse / get / put sandboxed to a chosen root.

use anyhow::{anyhow, bail, Context, Result};
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Component, Path, PathBuf};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ShareEntry {
    pub name: String,
    pub is_dir: bool,
    pub size: u64,
}

#[derive(Debug, Clone)]
pub struct ShareRoot {
    /// Canonical absolute root; all access must stay under this.
    root: PathBuf,
}

impl ShareRoot {
    pub fn open(path: impl AsRef<Path>) -> Result<Self> {
        let p = path.as_ref();
        fs::create_dir_all(p).with_context(|| format!("create share root {}", p.display()))?;
        let root = fs::canonicalize(p).with_context(|| format!("canonicalize {}", p.display()))?;
        if !root.is_dir() {
            bail!("share root is not a directory: {}", root.display());
        }
        Ok(Self { root })
    }

    pub fn root(&self) -> &Path {
        &self.root
    }

    /// Resolve a relative UTF-8 path under the share root. Rejects `..`, absolute, and escapes.
    pub fn resolve(&self, rel: &str) -> Result<PathBuf> {
        let rel = rel.trim();
        // Reject absolute / rooted paths BEFORE stripping separators (otherwise
        // "/etc/passwd" becomes "etc/passwd" and would land under the share root).
        if rel.starts_with('/') || rel.starts_with('\\') || Path::new(rel).is_absolute() {
            bail!("absolute paths not allowed");
        }
        let rel = rel.trim_start_matches('/');
        if rel.is_empty() || rel == "." {
            return Ok(self.root.clone());
        }
        let candidate = Path::new(rel);
        if candidate.is_absolute() {
            bail!("absolute paths not allowed");
        }
        for c in candidate.components() {
            match c {
                Component::Normal(s) => {
                    let s = s.to_string_lossy();
                    if s.contains('\0') || s == ".." {
                        bail!("invalid path component");
                    }
                }
                Component::CurDir => {}
                _ => bail!("path traversal rejected"),
            }
        }
        let joined = self.root.join(candidate);
        // If path exists, canonicalize and check prefix; if not (for put parent), check parent.
        if joined.exists() {
            let canon = fs::canonicalize(&joined)
                .with_context(|| format!("canonicalize {}", joined.display()))?;
            ensure_under(&self.root, &canon)?;
            Ok(canon)
        } else {
            // Ensure we never escape even with non-existing leaf: walk parents.
            let mut check = joined.clone();
            while !check.exists() {
                match check.parent() {
                    Some(p) if p != check => check = p.to_path_buf(),
                    _ => break,
                }
            }
            if check.exists() {
                let canon = fs::canonicalize(&check)?;
                ensure_under(&self.root, &canon)?;
            } else {
                // Nothing exists yet — joined must still start with root as Path prefix
                // after normalizing `..` away (already rejected above).
                ensure_under_lexical(&self.root, &joined)?;
            }
            Ok(joined)
        }
    }

    pub fn list(&self, rel: &str) -> Result<Vec<ShareEntry>> {
        let dir = self.resolve(rel)?;
        if !dir.is_dir() {
            bail!("not a directory");
        }
        let mut out = Vec::new();
        for ent in fs::read_dir(&dir)? {
            let ent = ent?;
            let name = ent.file_name().to_string_lossy().to_string();
            if name.starts_with('.') {
                continue; // skip hidden
            }
            let meta = ent.metadata()?;
            out.push(ShareEntry {
                name,
                is_dir: meta.is_dir(),
                size: if meta.is_file() { meta.len() } else { 0 },
            });
        }
        out.sort_by(|a, b| b.is_dir.cmp(&a.is_dir).then(a.name.cmp(&b.name)));
        Ok(out)
    }

    pub fn read_file_path(&self, rel: &str) -> Result<PathBuf> {
        let p = self.resolve(rel)?;
        if !p.is_file() {
            bail!("not a file");
        }
        Ok(p)
    }

    /// Destination path for an upload into `rel_dir` / `filename` (filename sanitized).
    pub fn put_dest(&self, rel_dir: &str, filename: &str) -> Result<PathBuf> {
        let safe = sanitize_share_name(filename)?;
        let dir = self.resolve(rel_dir)?;
        if !dir.is_dir() {
            bail!("put target is not a directory");
        }
        let dest = dir.join(&safe);
        ensure_under_lexical(&self.root, &dest)?;
        if dest.exists() {
            // unique
            for i in 1..1000 {
                let alt = dir.join(format!("{safe}.{i}"));
                if !alt.exists() {
                    return Ok(alt);
                }
            }
            bail!("too many name collisions");
        }
        Ok(dest)
    }
}

fn ensure_under(root: &Path, path: &Path) -> Result<()> {
    if path == root || path.starts_with(root) {
        Ok(())
    } else {
        Err(anyhow!(
            "path escapes share root: {} not under {}",
            path.display(),
            root.display()
        ))
    }
}

fn ensure_under_lexical(root: &Path, path: &Path) -> Result<()> {
    // Compare component-wise after both are absolute-ish under root join.
    let root_s = root.to_string_lossy();
    let path_s = path.to_string_lossy();
    if path_s == root_s || path_s.starts_with(&format!("{root_s}{}", std::path::MAIN_SEPARATOR)) {
        Ok(())
    } else {
        Err(anyhow!("lexical escape rejected"))
    }
}

fn sanitize_share_name(name: &str) -> Result<String> {
    let base = Path::new(name)
        .file_name()
        .and_then(|n| n.to_str())
        .unwrap_or("");
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
    if cleaned.is_empty() || cleaned == ".." {
        bail!("invalid filename");
    }
    Ok(cleaned)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::env;

    #[test]
    fn blocks_traversal() {
        let dir = env::temp_dir().join(format!("lan-share-{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        let share = ShareRoot::open(&dir).unwrap();
        assert!(share.resolve("../etc/passwd").is_err());
        assert!(share.resolve("/etc/passwd").is_err());
        assert!(share.resolve("a/../../x").is_err());
        fs::write(dir.join("hi.txt"), b"hi").unwrap();
        let p = share.resolve("hi.txt").unwrap();
        assert!(p.ends_with("hi.txt"));
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn list_and_put_dest() {
        let dir = env::temp_dir().join(format!("lan-share2-{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        let share = ShareRoot::open(&dir).unwrap();
        fs::write(dir.join("a.txt"), b"a").unwrap();
        fs::create_dir(dir.join("sub")).unwrap();
        let ents = share.list("").unwrap();
        assert!(ents.iter().any(|e| e.name == "a.txt" && !e.is_dir));
        assert!(ents.iter().any(|e| e.name == "sub" && e.is_dir));
        let dest = share.put_dest("", "b.txt").unwrap();
        assert!(dest.ends_with("b.txt"));
        let _ = fs::remove_dir_all(&dir);
    }
}
