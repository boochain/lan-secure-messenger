//! Small helpers for durable writes and input sanitization.

use anyhow::{Context, Result};
use std::fs::{self, File};
use std::io::Write;
use std::path::Path;

/// Write `data` to `path` via temp file + fsync + rename (best-effort atomic).
pub fn atomic_write(path: &Path, data: &[u8]) -> Result<()> {
    let parent = path.parent().unwrap_or_else(|| Path::new("."));
    fs::create_dir_all(parent).with_context(|| format!("create parent {}", parent.display()))?;
    let tmp = parent.join(format!(
        ".{}.tmp-{}",
        path.file_name()
            .and_then(|s| s.to_str())
            .unwrap_or("dat"),
        std::process::id()
    ));
    {
        let mut f = File::create(&tmp).with_context(|| format!("create temp {}", tmp.display()))?;
        f.write_all(data)?;
        f.sync_all()?;
    }
    fs::rename(&tmp, path).with_context(|| {
        format!("rename {} -> {}", tmp.display(), path.display())
    })?;
    // Best-effort directory fsync (helps durability on crash).
    if let Ok(dir) = File::open(parent) {
        let _ = dir.sync_all();
    }
    Ok(())
}

/// Cap + strip control chars from display names (UI / discovery).
pub fn sanitize_display_name(name: &str) -> String {
    let cleaned: String = name
        .chars()
        .filter(|c| !c.is_control())
        .take(64)
        .collect::<String>()
        .trim()
        .to_string();
    if cleaned.is_empty() {
        "未命名".into()
    } else {
        cleaned
    }
}
