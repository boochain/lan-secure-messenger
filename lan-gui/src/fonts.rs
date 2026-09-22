//! CJK / Simplified Chinese font loading for egui.
//!
//! Priority:
//! 1. System fonts (Windows YaHei / SimHei / SimSun; Linux Noto / WenQuanYi / Source Han)
//! 2. Sidecar `fonts/` next to the executable (portable zip)
//! 3. Embedded `LanGuiCJK-Regular.ttf` subset (always available)

use eframe::egui::{self, FontData, FontDefinitions, FontFamily};
use std::path::{Path, PathBuf};

/// Embedded SC-capable subset (LXGW WenKai based) so portable builds never tofu UI strings.
static EMBEDDED_CJK: &[u8] = include_bytes!("../assets/fonts/LanGuiCJK-Regular.ttf");

pub fn install_cjk_fonts(ctx: &egui::Context) {
    let mut fonts = FontDefinitions::default();
    let mut loaded: Vec<String> = Vec::new();

    for (name, data, index) in discover_font_candidates() {
        let key = format!("cjk_{name}");
        let mut fd = FontData::from_owned(data);
        fd.index = index;
        fonts.font_data.insert(key.clone(), fd);
        loaded.push(key);
    }

    fonts
        .font_data
        .insert("cjk_embedded".to_owned(), FontData::from_static(EMBEDDED_CJK));
    loaded.push("cjk_embedded".to_owned());

    // Prefer CJK fonts first so Chinese glyphs resolve before egui's Latin defaults.
    for family in [FontFamily::Proportional, FontFamily::Monospace] {
        let list = fonts.families.entry(family).or_default();
        for key in loaded.iter().rev() {
            list.insert(0, key.clone());
        }
    }

    ctx.set_fonts(fonts);
    tracing::info!(
        cjk_fonts = loaded.len(),
        "installed CJK fonts (system/sidecar/embedded)"
    );
}

fn discover_font_candidates() -> Vec<(String, Vec<u8>, u32)> {
    let mut out = Vec::new();
    for (label, path, index) in candidate_paths() {
        if let Ok(bytes) = std::fs::read(&path) {
            if bytes.len() > 1000 {
                tracing::info!(%label, path = %path.display(), index, "loaded CJK font");
                out.push((label, bytes, index));
            }
        }
    }
    out
}

fn candidate_paths() -> Vec<(String, PathBuf, u32)> {
    let mut v: Vec<(String, PathBuf, u32)> = Vec::new();

    if let Some(dir) = exe_fonts_dir() {
        for name in [
            "LanGuiCJK-Regular.ttf",
            "NotoSansSC-Regular.otf",
            "NotoSansCJKsc-Regular.otf",
            "msyh.ttc",
            "msyh.ttf",
            "LXGWWenKai-Regular.ttf",
        ] {
            v.push((format!("sidecar_{name}"), dir.join(name), 0));
        }
    }

    #[cfg(windows)]
    {
        let windir = std::env::var_os("WINDIR")
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from(r"C:\Windows"));
        let fonts = windir.join("Fonts");
        for (label, file, idx) in [
            ("yahei", "msyh.ttc", 0u32),
            ("yahei_ttf", "msyh.ttf", 0),
            ("yahei_light", "msyhl.ttc", 0),
            ("simhei", "simhei.ttf", 0),
            ("simsun", "simsun.ttc", 0),
            ("simsun_ttf", "simsun.ttf", 0),
            ("msjh", "msjh.ttc", 0),
        ] {
            v.push((label.into(), fonts.join(file), idx));
        }
    }

    #[cfg(target_os = "linux")]
    {
        for (label, path, idx) in [
            (
                "noto_sans_cjk",
                "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                0u32,
            ),
            (
                "noto_sans_cjk_alt",
                "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
                0,
            ),
            (
                "wqy_microhei",
                "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
                0,
            ),
            (
                "wqy_zenhei",
                "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
                0,
            ),
            (
                "source_han",
                "/usr/share/fonts/opentype/source-han-sans/SourceHanSansSC-Regular.otf",
                0,
            ),
            (
                "noto_sc_otf",
                "/usr/share/fonts/opentype/noto/NotoSansSC-Regular.otf",
                0,
            ),
        ] {
            v.push((label.into(), PathBuf::from(path), idx));
        }
    }

    #[cfg(target_os = "macos")]
    {
        for (label, path, idx) in [
            ("pingfang", "/System/Library/Fonts/PingFang.ttc", 0u32),
            ("sthei", "/System/Library/Fonts/STHeiti Light.ttc", 0),
            (
                "hiragino",
                "/System/Library/Fonts/Hiragino Sans GB.ttc",
                0,
            ),
        ] {
            v.push((label.into(), PathBuf::from(path), idx));
        }
    }

    v
}

fn exe_fonts_dir() -> Option<PathBuf> {
    let exe = std::env::current_exe().ok()?;
    let dir = exe.parent()?;
    let fonts = dir.join("fonts");
    if fonts.is_dir() {
        return Some(fonts);
    }
    let cwd = std::env::current_dir().ok()?.join("fonts");
    if cwd.is_dir() {
        return Some(cwd);
    }
    let assets = Path::new("lan-gui/assets/fonts");
    if assets.is_dir() {
        return Some(assets.to_path_buf());
    }
    None
}
