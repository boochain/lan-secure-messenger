#!/usr/bin/env python3
"""Screenshot overflow / clipping detector.

Looks for text-like (high-frequency, high-contrast) pixels that are cut off at
the image edge or at a sharp interior panel edge. Also flags overlapping
Android bottom-nav icons when the filename looks like an Android page shot.

usage: ui_overflow_audit.py [--out DIR] [--ignore-file JSON] [--self-test] PNG...
"""
from __future__ import annotations
import argparse, json, os, sys
from pathlib import Path

def luma(im):
    import numpy as np
    a = np.asarray(im.convert("RGB"), dtype=np.int16)
    return (0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2])

def edge_cut_score(y, band=2):
    """True when ink-like pixels sit on the image border and continue inward
    as short glyph-sized runs (cut letters), not as a long window chrome line."""
    import numpy as np
    h, w = y.shape
    if h < 8 or w < 8:
        return False, ""
    bg = float(np.median(y))
    ink = np.abs(y - bg) > 28
    hits = []
    def cut_on_edge(col_or_row: np.ndarray, inward: np.ndarray, axis_name: str, length: int):
        # col_or_row / inward are 1-D booleans along the edge
        # runs of ink 3..28 px long that also have ink just inside
        edge = col_or_row.astype(np.uint8)
        pad = np.r_[0, edge, 0]
        d = np.diff(pad.astype(np.int8))
        starts = np.where(d == 1)[0]
        ends = np.where(d == -1)[0]
        ncut = 0
        for s, e in zip(starts, ends):
            run = e - s
            if 2 <= run <= 40 and inward[s:e].mean() > 0.35:
                ncut += 1
        if ncut >= 2:
            hits.append(f"{axis_name}:{ncut}")
    # right / left: inspect a 1px border, inward is 3px inside
    cut_on_edge(ink[:, -1], ink[:, -4] if w > 4 else ink[:, -1], "right", h)
    cut_on_edge(ink[:, 0], ink[:, 3] if w > 4 else ink[:, 0], "left", h)
    cut_on_edge(ink[-1, :], ink[-4, :] if h > 4 else ink[-1, :], "bottom", w)
    cut_on_edge(ink[0, :], ink[3, :] if h > 4 else ink[0, :], "top", w)
    if hits:
        return True, ", ".join(hits)
    return False, ""

def android_nav_overlap(im, path):
    """Bottom 14% of an Android page shot: large dark-ish icon blobs should be ≤4 and not overlap."""
    import numpy as np, cv2
    name = Path(path).name.lower()
    if not (name.startswith("android-") and "page-" in name):
        return False, ""
    if any(x in name for x in ("profile", "diagnostics", "scan", "qr")):
        # inner pages may hide the bottom nav
        return False, ""
    a = np.asarray(im.convert("RGB"))
    h, w = a.shape[:2]
    y0 = int(h * 0.86)
    band = a[y0:, :, :]
    # icons are typically colored or dark on a light/dark bar; find connected components of non-bar pixels
    bar = np.median(band.reshape(-1, 3), axis=0)
    dist = np.sqrt(((band.astype(np.int16) - bar.astype(np.int16)) ** 2).sum(-1))
    mask = (dist > 18).astype("uint8")
    k = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5))
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, k)
    n, _, st, _ = cv2.connectedComponentsWithStats(mask, connectivity=8)
    boxes = []
    for i in range(1, n):
        x, y, bw, bh, area = (int(st[i, j]) for j in range(5))
        if area < 80 or bw < 8 or bh < 8:
            continue
        if bw > w * 0.45:  # indicator pill spanning the bar
            continue
        boxes.append((x, y, x + bw, y + bh, area))
    # keep the largest 8, then those in the icon size range
    boxes.sort(key=lambda b: -b[4])
    icons = boxes[:8]
    # overlap?
    for i, a_ in enumerate(icons):
        for b in icons[i + 1:]:
            ix = min(a_[2], b[2]) - max(a_[0], b[0])
            iy = min(a_[3], b[3]) - max(a_[1], b[1])
            if ix > 4 and iy > 4:
                return True, f"bottom-nav icons overlap ({len(icons)} blobs)"
    if len(icons) > 6:
        # 4 icons + maybe labels as separate blobs; >6 is cramped
        return True, f"bottom-nav looks cramped ({len(icons)} blobs, expected ≤4 icons + labels)"
    return False, ""

def load_ignore(path):
    if not path or not Path(path).exists():
        return {}
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))

def audit_one(path, ignore):
    from PIL import Image
    im = Image.open(path).convert("RGB")
    name = Path(path).name
    if name in ignore or ignore.get(name) == "*":
        return None
    y = luma(im)
    cut, why = edge_cut_score(y)
    nav, nwhy = android_nav_overlap(im, path)
    reasons = []
    if cut:
        reasons.append("edge-clipped text: " + why)
    if nav:
        reasons.append(nwhy)
    return "; ".join(reasons) if reasons else None

def self_test(out: Path) -> int:
    from PIL import Image, ImageDraw, ImageFont
    out.mkdir(parents=True, exist_ok=True)
    # clean: text well inside the margins
    ok = Image.new("RGB", (400, 160), (249, 249, 255))
    d = ImageDraw.Draw(ok)
    font = ImageFont.load_default()
    from pathlib import Path as _P
    for fp in (
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    ):
        if _P(fp).exists():
            try:
                font = ImageFont.truetype(fp, 36)
                break
            except Exception:
                pass
    d.text((40, 50), "VISIBLE TITLE TEXT", fill=(38, 71, 119), font=font)
    p_ok = out / "ok.png"; ok.save(p_ok)
    wide = Image.new("RGB", (400, 80), (249, 249, 255))
    ImageDraw.Draw(wide).text((8, 16), "CLIPPED TEXT ABCDEFGH", fill=(20, 20, 20), font=font)
    bad = wide.crop((0, 0, 120, 80))
    p_bad = out / "bad.png"; bad.save(p_bad)
    r_ok = audit_one(p_ok, {})
    r_bad = audit_one(p_bad, {})
    print("self-test clean:", r_ok, "clipped:", r_bad)
    if r_ok:
        print("SELFTEST FAIL: clean image flagged:", r_ok)
        return 1
    if not r_bad:
        print("SELFTEST FAIL: clipped image was not flagged")
        return 1
    print("SELFTEST OK")
    return 0

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="ui-audit/overflow")
    ap.add_argument("--ignore-file")
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("pngs", nargs="*")
    args = ap.parse_args()
    out = Path(args.out); out.mkdir(parents=True, exist_ok=True)
    if args.self_test:
        sys.exit(self_test(out / "self-test"))
    ignore = load_ignore(args.ignore_file)
    fails = []
    report = ["# ui_overflow_audit", ""]
    for p in args.pngs:
        try:
            why = audit_one(p, ignore)
        except Exception as e:
            why = f"error: {e}"
        if why:
            fails.append((p, why))
            report.append(f"- FAIL {p}: {why}")
        else:
            report.append(f"- ok {p}")
    (out / "overflow-report.md").write_text("\n".join(report) + "\n", encoding="utf-8")
    print(f"ui_overflow_audit: {len(args.pngs)} shots, {len(fails)} failing")
    for p, w in fails:
        print(f"  FAIL {p}: {w}")
    sys.exit(1 if fails else 0)

if __name__ == "__main__":
    main()
