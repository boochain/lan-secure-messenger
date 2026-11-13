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
            # Rounded window corners leave short AA runs at both ends of each
            # edge; those are not clipped glyphs. Real cut text sits mid-edge.
            if s < 16 or e > length - 16:
                continue
            if 2 <= run <= 40 and inward[s:e].mean() > 0.35:
                # Full-width hairline dividers touch the left and right edges as
                # 2px runs; they are chrome, not cut glyphs.
                if axis_name in ("left", "right") and ink[s:e, :].mean() > 0.6:
                    continue
                if axis_name in ("top", "bottom") and ink[:, s:e].mean() > 0.6:
                    continue
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
    """Navbar band (above the gesture inset): at most 4 items, no horizontal overlap.

    Icon + label + badge of one tab are clustered into a single column. A loose
    14% band also includes the last list row and was false-flagging its glyphs.
    """
    import numpy as np, cv2
    name = Path(path).name.lower()
    if not (name.startswith("android-") and "page-" in name):
        return False, ""
    if any(x in name for x in ("profile", "diagnostics", "scan", "qr", "recovery")):
        # inner pages may hide the bottom nav
        return False, ""
    a = np.asarray(im.convert("RGB"))
    h, w = a.shape[:2]
    gesture = 40
    nav_h = 180
    y0 = max(0, h - gesture - nav_h)
    y1 = max(y0 + 1, h - gesture)
    band = a[y0:y1, :, :]
    bar = np.median(band.reshape(-1, 3), axis=0)
    dist = np.sqrt(((band.astype(np.int32) - bar.astype(np.int32)) ** 2).sum(-1))
    mask = (dist > 18).astype("uint8")
    k = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5))
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, k)
    n, _, st, _ = cv2.connectedComponentsWithStats(mask, connectivity=8)
    boxes = []
    for i in range(1, n):
        x, y, bw, bh, area = (int(st[i, j]) for j in range(5))
        if area < 80 or bw < 8 or bh < 8:
            continue
        if bw > w * 0.45:  # indicator pill / selected-tab highlight spanning the bar
            continue
        boxes.append((x, y, x + bw, y + bh, area))
    # Cluster icon + label + badge of one tab into a column.
    boxes.sort(key=lambda b: (b[0] + b[2]) / 2)
    cols = []  # (x0, x1, blobs)
    for b in boxes:
        cx = (b[0] + b[2]) / 2
        if cols and cx - cols[-1][1] < 24:
            x0, x1, group = cols[-1]
            group.append(b)
            cols[-1] = (min(x0, b[0]), max(x1, b[2]), group)
        else:
            cols.append((b[0], b[2], [b]))
    if len(cols) > 4:
        return True, f"bottom-nav looks cramped ({len(cols)} items, expected ≤4)"
    for i, a_ in enumerate(cols):
        for b in cols[i + 1:]:
            gap = b[0] - a_[1]
            if gap < 8:
                return True, f"bottom-nav icons overlap (gap {gap}px, {len(cols)} items)"
    return False, ""


def _text_font(size=28):
    from PIL import ImageFont
    from pathlib import Path as _P
    font = ImageFont.load_default()
    for fp in (
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    ):
        if _P(fp).exists():
            try:
                return ImageFont.truetype(fp, size)
            except Exception:
                pass
    return font

def _ellipsis_spans(ink_row):
    """Start/end x of typographic ellipses (three tightly spaced short dots)."""
    import numpy as np
    sl = ink_row.astype(np.uint8)
    pad = np.r_[0, sl, 0]
    d = np.diff(pad.astype(np.int8))
    starts = np.where(d == 1)[0]
    ends = np.where(d == -1)[0]
    runs = [(int(s), int(e) - int(s)) for s, e in zip(starts, ends)]
    dots = [r for r in runs if 1 <= r[1] <= 6]
    spans = []
    for i in range(len(dots) - 2):
        last = dots[i:i + 3]
        # similarly sized dots, regular gaps
        widths = [last[j][1] for j in range(3)]
        if max(widths) > min(widths) * 3 + 2:
            continue
        gaps = [last[j + 1][0] - (last[j][0] + last[j][1]) for j in range(2)]
        if all(0 <= g <= 8 for g in gaps) and abs(gaps[0] - gaps[1]) <= 4:
            width = last[2][0] + last[2][1] - last[0][0]
            if 6 <= width <= 28:
                spans.append((last[0][0], last[2][0] + last[2][1]))
    return spans

def _label_left(ink_row, x_end, gap=80):
    """Left edge of the label ending at x_end, allowing wide ' / ' gaps."""
    i = x_end
    left = x_end
    while i >= 0:
        empty = 0
        while i >= 0 and not ink_row[i]:
            empty += 1
            i -= 1
        if i < 0 or empty > gap:
            break
        while i >= 0 and ink_row[i]:
            left = i
            i -= 1
    return left

def _gutter_rows(ink, a, b):
    """Row indices in [a, b) whose ink is low enough to be menu-card padding."""
    return [i for i in range(a, b) if ink[i].mean() < 0.08]

def _panel_pad(y, ink, y0, y1, h, w, x_end):
    """Padding from x_end to the next vertical panel/card edge.

    Only gutter rows (between menu items, or above/below the card's text)
    are consulted, so the next item's label cannot look like a card border.
    If no gutter is available, return a large pad (do not guess).
    """
    import numpy as np
    gut = _gutter_rows(ink, max(0, y0 - 28), y0) + _gutter_rows(ink, y1, min(h, y1 + 28))
    if len(gut) < 4:
        return 48
    x_int0, x_int1 = max(0, x_end - 16), max(1, x_end)
    card_bg = float(np.median(y[np.array(gut), :][:, x_int0:x_int1]))
    pad = 0
    for x in range(x_end + 1, min(w, x_end + 48)):
        col = y[np.array(gut), x]
        rule = float(ink[np.array(gut), x].mean())
        # A real vertical border inks the gutters on both sides of the item.
        if rule > 0.35 or abs(float(np.median(col)) - card_bg) > 12:
            return pad
        pad += 1
    return pad

def ellipsis_in_menu(im, path):
    """Flag a menu/popup item whose label is cut off with a trailing ellipsis.

    Intentional short labels such as '导出到文件…' have padding after the dots
    and stay under the long-label threshold, so they are not flagged. A long
    combined label that WinUI/GTK ellipsizes is flagged even when the flyout
    itself is wider than the text layout box.
    """
    import numpy as np
    name = Path(path).name.lower()
    if not any(h in name for h in ("menu", "flyout", "overflow")):
        return False, ""
    h_im = im.height if hasattr(im, "height") else im.size[1]
    w = im.width if hasattr(im, "width") else im.size[0]
    if h_im < 20 or w < 40:
        return False, ""
    y = luma(im)
    h, w = y.shape
    bg = float(np.median(y))
    ink = np.abs(y - bg) > 28
    flagged = 0
    seen = set()
    y0 = 0
    while y0 < h:
        if ink[y0].mean() < 0.02 or ink[y0].mean() > 0.85:
            y0 += 1
            continue
        y1 = y0 + 1
        while y1 < h and 0.02 <= ink[y1].mean() <= 0.85:
            y1 += 1
        band_h = y1 - y0
        # Title bar / composer chrome on a full-window shot is not a menu item.
        if y0 < 44 or y1 > h - 70:
            y0 = max(y1, y0 + 1)
            continue
        if 6 <= band_h <= 40:
            mid0, mid1 = y0 + max(1, band_h // 5), y1 - max(1, band_h // 6)
            # Union of mid-glyph rows keeps thin CJK strokes that a mean
            # threshold would drop.
            union = ink[mid0:mid1].any(axis=0) if mid1 > mid0 else ink[y0]
            votes = {}
            for ry in range(mid0, max(mid0 + 1, mid1)):
                for s, e in _ellipsis_spans(ink[ry]):
                    key = (round(s / 3) * 3, round(e / 3) * 3)
                    votes.setdefault(key, [0, s, e])
                    votes[key][0] += 1
                    votes[key][1] = min(votes[key][1], s)
                    votes[key][2] = max(votes[key][2], e)
            # Cluster overlapping/nearby spans so a single ellipsis that
            # jittered across rows is counted once. Use the tightest
            # typographic member (not the union) so the preceding CJK
            # glyph is not absorbed into the dot columns.
            items = sorted(votes.values(), key=lambda v: v[1])
            clustered = []  # [votes, members]
            for c, s, e in items:
                if clustered and s <= max(m[1] for m in clustered[-1][1]) + 6:
                    clustered[-1][0] += c
                    clustered[-1][1].append((s, e))
                else:
                    clustered.append([c, [(s, e)]])
            uniq = []
            for c, members in clustered:
                if c < 2:
                    continue
                typo = [(s, e) for s, e in members if 6 <= (e - s) <= 28]
                if not typo:
                    continue
                typo.sort(key=lambda se: (se[1] - se[0], se[0]))
                uniq.append(typo[0])  # shortest typographic span
            for e0, e1 in uniq:
                left = _label_left(union, e1)
                width = e1 - left
                # Sidebar/search text often merges to x=0; menu labels do not.
                # XP dark menu shots are full-window: a file-transfer card
                # ("195 KB · BIN") measures 249-260px and is not truncated.
                # Menu labels stay well under 250px.
                if e0 < left or left < 24 or width < 40 or width >= 250:
                    continue
                if e1 >= w - 12:
                    continue  # window-border AA, not a menu card
                # Look past the last dot for more letters (ignore 1px AA).
                tail = union[e1 + 1:min(w, e1 + 12)]
                run = 0
                more = False
                for v in tail:
                    run = run + 1 if v else 0
                    if run >= 3:
                        more = True
                        break
                if more:
                    continue
                # Typographic dots are short; emoji and icons are taller.
                col = ink[y0:y1, e0:e1]
                if col.size:
                    rows = col.any(axis=1)
                    if rows.any():
                        ys = __import__("numpy").where(rows)[0]
                        if int(ys[-1] - ys[0] + 1) > 12:
                            continue
                pad = _panel_pad(y, ink, y0, y1, h, w, e1)
                long_label = width >= 150
                flush = pad <= 8
                if not (flush or long_label):
                    continue
                key = (y0, left // 8, e1 // 8)
                if key in seen:
                    continue
                seen.add(key)
                flagged += 1
        y0 = max(y1, y0 + 1)
    if flagged:
        return True, f"ellipsized menu item x{flagged}"
    return False, ""

def load_ignore(path):
    if not path or not Path(path).exists():
        return {}
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))

def audit_one(path, ignore):
    from PIL import Image
    im = Image.open(path).convert("RGB")
    name = Path(path).name
    # Colour-audit ignore files list decorative rects in mixed formats; they
    # are not a skip list for overflow. Only drop a shot when it is explicitly
    # starred or is known OS chrome / a user-photo viewer.
    if ignore.get(name) == "*" or ignore.get("*") == "*":
        return None
    low = name.lower()
    if any(k in low for k in ("file-chooser", "send-image", "desktop-", "image-viewer")):
        return None
    y = luma(im)
    cut, why = edge_cut_score(y)
    nav, nwhy = android_nav_overlap(im, path)
    ell, ewhy = ellipsis_in_menu(im, path)
    reasons = []
    if cut:
        reasons.append("edge-clipped text: " + why)
    if nav:
        reasons.append(nwhy)
    if ell:
        reasons.append(ewhy)
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
    # menu item ellipsis: a flyout card whose last item is flush-truncated
    menu_font = _text_font(22)
    def menu_shot(truncated, name):
        canvas = Image.new("RGB", (640, 360), (240, 240, 245))
        d = ImageDraw.Draw(canvas)
        d.text((24, 16), "Chat page", fill=(40, 40, 50), font=menu_font)
        box = (360, 40, 600, 240)
        d.rectangle(box, fill=(255, 255, 255))
        d.text((376, 56), "会话", fill=(30, 30, 40), font=menu_font)
        d.text((376, 92), "消息", fill=(30, 30, 40), font=menu_font)
        # Deterministic ink so the self-test does not depend on font shaping:
        # a long bar (the label) plus three dots, either flush or padded.
        if truncated:
            # Long label flush to the card, plus three dots 4px from the edge.
            d.rectangle((376, 134, 584, 150), fill=(30, 30, 40))
            for i in range(3):
                d.ellipse((586 + i * 4, 138, 588 + i * 4, 146), fill=(30, 30, 40))
        else:
            # Short label + typographic dots with ample padding to the card edge.
            d.rectangle((376, 134, 500, 150), fill=(30, 30, 40))
            for i in range(3):
                d.ellipse((504 + i * 4, 138, 506 + i * 4, 146), fill=(30, 30, 40))
        path = out / name
        canvas.save(path)
        return path
    p_menu_ok = menu_shot(False, "popup-menu-ok.png")
    p_menu_bad = menu_shot(True, "popup-menu-bad.png")
    r_menu_ok = audit_one(p_menu_ok, {})
    r_menu_bad = audit_one(p_menu_bad, {})
    print("self-test menu padded:", r_menu_ok, "ellipsized:", r_menu_bad)
    if r_menu_ok:
        print("SELFTEST FAIL: padded menu item flagged:", r_menu_ok)
        return 1
    if not r_menu_bad or "ellipsized" not in r_menu_bad:
        print("SELFTEST FAIL: ellipsized menu item was not flagged:", r_menu_bad)
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
