#!/usr/bin/env python3
"""ui_color_audit: readability / palette audit that works on SCREENSHOTS (pixels), not on code.

usage: ui_color_audit.py [--palette tools/palette.json] [--theme auto|light|dark] [--out DIR]
                         [--allow-stray] [--ignore x,y,w,h ...] IMG...
       ui_color_audit.py --self-test
Exit status 1 if any screenshot fails a gate.

Gates per screenshot
 (a) OCR text (tesseract chi_sim+eng). Each word box: pixels are split into 2 clusters in CIELAB
     (k-means); background = the cluster that owns most of the box border; foreground colour = the
     "ink core" (the 3 % of foreground pixels farthest from the background, so anti-aliasing does not
     dilute it). WCAG 2 ratio: body < 4.5 fails, large (>= 24 px tall glyphs ~ 18 pt) < 3.0 fails.
     APCA-W3 0.0.98G Lc: body |Lc| < 60 fails, large |Lc| < 45 fails.
 (b) OCR-independent scan for dark-on-dark / light-on-light strokes: 16 px tiles, per tile the 3rd/97th
     luminance percentiles; tile is "glyph-like" when an edge map of the *contrast-stretched* tile has
     >= 4 small connected stroke components; it fails when the real WCAG ratio between those two
     levels is between 1.04 and 3.0 (visible to the edge detector, invisible-ish to a human).
     Tiles covered by a passing OCR box are ignored; flagged tiles are merged into regions (>= 3 tiles).
 (c) Flat regions (connected components of one exact colour, area >= 0.15 % of the image, at least 8 px
     thick; 1-2 px lines and outlines are separators) that touch along >= 24 px with CIEDE2000 dE < 2.9
     ('about 3'; the palette divider on window_bg is 2.97) and > 0.4 fail as "indistinct adjacent elements".
     Status colours (palette online/warning/error) present on screen are simulated for protanopia and
     deuteranopia (Machado 2009, severity 1); a pair with dE2000 < 10 after simulation is reported
     as CVD warning (colour must not be the only cue; it fails only when --strict-cvd).
 (d) Palette conformance: every colour covering >= 0.4 % of the image must be within dE2000 2 of a
     token of the active theme (auto-detected from the dominant background). Strays fail unless
     inside an --ignore rectangle (e.g. OS window chrome, user-sent images).
Outputs per image: <out>/<name>.json and <name>.annot.png (red = text fail, magenta = stroke scan,
orange = indistinct regions, yellow = stray colour), and <out>/report.md for the run."""
import argparse, json, os, sys, subprocess
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))

# ------------------------------------------------------------------ colour maths
def srgb_to_lin(c):
    c = np.asarray(c, dtype=np.float64) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
def rel_lum(rgb):
    l = srgb_to_lin(rgb); return l[..., 0] * 0.2126 + l[..., 1] * 0.7152 + l[..., 2] * 0.0722
def wcag(a, b):
    la, lb = float(rel_lum(np.array(a))), float(rel_lum(np.array(b)))
    hi, lo = max(la, lb), min(la, lb); return (hi + 0.05) / (lo + 0.05)
def apca(txt, bg):
    """APCA-W3 0.0.98G-4g (SAPC). Returns Lc (signed, positive = dark text on light bg)."""
    def y(c):
        c = np.asarray(c, dtype=np.float64) / 255.0
        return 0.2126729 * c[0] ** 2.4 + 0.7151522 * c[1] ** 2.4 + 0.0721750 * c[2] ** 2.4
    Yt, Yb = y(txt), y(bg)
    bt, bc = 0.022, 1.414
    Yt = Yt + (bt - Yt) ** bc if Yt < bt else Yt
    Yb = Yb + (bt - Yb) ** bc if Yb < bt else Yb
    if abs(Yb - Yt) < 0.0005: return 0.0
    if Yb > Yt:
        s = (Yb ** 0.56 - Yt ** 0.57) * 1.14
        return 0.0 if s < 0.1 else (s - 0.027) * 100
    s = (Yb ** 0.65 - Yt ** 0.62) * 1.14
    return 0.0 if s > -0.1 else (s + 0.027) * 100
def rgb_to_lab(rgb):
    l = srgb_to_lin(rgb)
    M = np.array([[0.4124564, 0.3575761, 0.1804375], [0.2126729, 0.7151522, 0.0721750], [0.0193339, 0.1191920, 0.9503041]])
    xyz = l @ M.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 216 / 24389, np.cbrt(xyz), (24389 / 27 * xyz + 16) / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)
def de2000(lab1, lab2):
    L1, a1, b1 = np.moveaxis(np.asarray(lab1, float), -1, 0); L2, a2, b2 = np.moveaxis(np.asarray(lab2, float), -1, 0)
    C1, C2 = np.hypot(a1, b1), np.hypot(a2, b2); Cb = (C1 + C2) / 2
    G = 0.5 * (1 - np.sqrt(Cb ** 7 / (Cb ** 7 + 25 ** 7)))
    a1p, a2p = (1 + G) * a1, (1 + G) * a2; C1p, C2p = np.hypot(a1p, b1), np.hypot(a2p, b2)
    h1p = np.degrees(np.arctan2(b1, a1p)) % 360; h2p = np.degrees(np.arctan2(b2, a2p)) % 360
    dL, dC = L2 - L1, C2p - C1p
    dh = h2p - h1p; dh = np.where(dh > 180, dh - 360, np.where(dh < -180, dh + 360, dh)); dh = np.where(C1p * C2p == 0, 0, dh)
    dH = 2 * np.sqrt(C1p * C2p) * np.sin(np.radians(dh / 2))
    Lb, Cbp = (L1 + L2) / 2, (C1p + C2p) / 2
    hs = h1p + h2p; hb = np.where(np.abs(h1p - h2p) > 180, np.where(hs < 360, (hs + 360) / 2, (hs - 360) / 2), hs / 2); hb = np.where(C1p * C2p == 0, hs, hb)
    T = 1 - 0.17 * np.cos(np.radians(hb - 30)) + 0.24 * np.cos(np.radians(2 * hb)) + 0.32 * np.cos(np.radians(3 * hb + 6)) - 0.20 * np.cos(np.radians(4 * hb - 63))
    dth = 30 * np.exp(-(((hb - 275) / 25) ** 2)); Rc = 2 * np.sqrt(Cbp ** 7 / (Cbp ** 7 + 25 ** 7))
    Sl = 1 + 0.015 * (Lb - 50) ** 2 / np.sqrt(20 + (Lb - 50) ** 2); Sc = 1 + 0.045 * Cbp; Sh = 1 + 0.015 * Cbp * T
    Rt = -np.sin(np.radians(2 * dth)) * Rc
    return np.sqrt((dL / Sl) ** 2 + (dC / Sc) ** 2 + (dH / Sh) ** 2 + Rt * (dC / Sc) * (dH / Sh))
MACHADO = {"protanopia": np.array([[0.152286, 1.052583, -0.204868], [0.114503, 0.786281, 0.099216], [-0.003882, -0.048116, 1.051998]]),
           "deuteranopia": np.array([[0.367322, 0.860646, -0.227968], [0.280085, 0.672501, 0.047413], [-0.011820, 0.042940, 0.968881]])}
def simulate(rgb, kind):
    l = np.clip(srgb_to_lin(rgb) @ MACHADO[kind].T, 0, 1)
    return np.clip(np.where(l <= 0.0031308, 12.92 * l, 1.055 * l ** (1 / 2.4) - 0.055) * 255, 0, 255)
def hx(s): s = s.lstrip("#"); return np.array([int(s[i:i + 2], 16) for i in (0, 2, 4)], float)
def tohex(c): return "#%02X%02X%02X" % tuple(int(round(v)) for v in c)

# ------------------------------------------------------------------ helpers
def kmeans2(lab, iters=12):
    if len(lab) < 4: return None
    L = lab[:, 0]; c = np.stack([lab[L.argmin()], lab[L.argmax()]])
    if np.allclose(c[0], c[1]): return None
    for _ in range(iters):
        d = ((lab[:, None, :] - c[None]) ** 2).sum(-1); lab_ = d.argmin(1)
        nc = np.stack([lab[lab_ == k].mean(0) if (lab_ == k).any() else c[k] for k in (0, 1)])
        if np.allclose(nc, c): break
        c = nc
    return lab_, c

def ocr_boxes(img):
    out = _ocr_boxes(img, "--psm 11")
    # tesseract 5.3 (Ubuntu 24.04 CI) can return nothing for a whole crop in sparse mode where 5.5 does not:
    # fall back to automatic page segmentation before concluding there is no text
    return out or _ocr_boxes(img, "--psm 3")

def _ocr_boxes(img, cfg):
    import pytesseract
    up = img.resize((img.width * 2, img.height * 2), Image.LANCZOS)
    boxes = []
    for inv in (False, True):  # tesseract prefers dark-on-light; run on the inverted image too
        src = Image.fromarray(255 - np.asarray(up)) if inv else up
        d = pytesseract.image_to_data(src, lang="chi_sim+eng", config=cfg, output_type=pytesseract.Output.DICT)
        for i, t in enumerate(d["text"]):
            t = t.strip()
            if not t or float(d["conf"][i]) < 55 or not any(ch.isalnum() for ch in t): continue
            x, y, w, h = (d[k][i] // 2 for k in ("left", "top", "width", "height"))
            if w < 4 or h < 6 or h > 120: continue
            if len(t) == 1 and w < 10 and h < 10: continue  # a lone sub-10px "glyph" is an icon/status dot (8px Ellipse read as "e"), not text; non-text is judged by (b)/(c)
            boxes.append((x, y, w, h, t, float(d["conf"][i])))
    # de-duplicate (normal + inverted runs)
    out = []
    for b in sorted(boxes, key=lambda b: -b[5]):
        if all(not (abs(b[0] - o[0]) < 4 and abs(b[1] - o[1]) < 4) for o in out): out.append(b)
    return out

def text_check(arr, box, mask=None):
    x, y, w, h, t, conf = box; H, W = arr.shape[:2]
    x0, y0, x1, y1 = max(0, x - 2), max(0, y - 2), min(W, x + w + 2), min(H, y + h + 2)
    border = np.zeros((y1 - y0, x1 - x0), bool); border[0, :] = border[-1, :] = border[:, 0] = border[:, -1] = True
    keep = mask[y0:y1, x0:x1].reshape(-1) if mask is not None else np.ones((y1 - y0) * (x1 - x0), bool)
    # pixels inside an --ignore rectangle are excluded from every gate, also inside a word box
    # (an app-reported decorative online dot on an avatar must not become the "ink" of the avatar initial)
    if keep.sum() < 9: return None
    patch = arr[y0:y1, x0:x1].reshape(-1, 3).astype(float)[keep]; border = border.reshape(-1)[keep]
    if not border.any(): return None
    lab = rgb_to_lab(patch); km = kmeans2(lab)
    if km is None: return None
    lbl, cent = km
    bl = lbl[border]; bgk = int(np.bincount(bl, minlength=2).argmax()); fgk = 1 - bgk
    bgpix, fgpix = patch[lbl == bgk], patch[lbl == fgk]
    if len(fgpix) < 3: return None
    bg = np.median(bgpix, 0)
    dist = de2000(rgb_to_lab(fgpix), rgb_to_lab(bg)[None].repeat(len(fgpix), 0))
    core = fgpix[dist >= np.quantile(dist, 0.97)]; fg = np.median(core, 0)
    ratio = wcag(fg, bg); lc = apca(fg, bg); large = h >= 24
    fail = []
    if ratio < (3.0 if large else 4.5): fail.append(f"WCAG {ratio:.2f} < {'3.0' if large else '4.5'}")
    if abs(lc) < (45 if large else 60): fail.append(f"APCA |Lc| {abs(lc):.1f} < {45 if large else 60}")
    return dict(text=t, box=[x, y, w, h], fg=tohex(fg), bg=tohex(bg), wcag=round(ratio, 2), apca=round(lc, 1), large=large, fail=fail)

def stroke_scan(arr, passing_boxes, T=16):
    import cv2
    H, W = arr.shape[:2]; lum = rel_lum(arr.astype(float)); flagged = np.zeros((H // T, W // T), bool)
    cover = np.zeros((H, W), bool)
    for b in passing_boxes: x, y, w, h = b; cover[max(0, y - 3):y + h + 3, max(0, x - 3):x + w + 3] = True
    for ty in range(H // T):
        for tx in range(W // T):
            sl = (slice(ty * T, ty * T + T), slice(tx * T, tx * T + T))
            if cover[sl].mean() > 0.3: continue
            lt = lum[sl]; lo, hi = np.percentile(lt, 3), np.percentile(lt, 97)
            r = (hi + 0.05) / (lo + 0.05)
            if not (1.04 < r < 3.0): continue
            g = (np.clip((lt - lo) / max(hi - lo, 1e-6), 0, 1) * 255).astype(np.uint8)
            e = cv2.Canny(g, 80, 160)
            n, lab, st, _ = cv2.connectedComponentsWithStats(e, connectivity=8)
            # glyph strokes are 2-D; a 1-px straight edge (divider, frame) is not a glyph
            small = [i for i in range(1, n) if 3 <= st[i, cv2.CC_STAT_AREA] <= 60 and st[i, cv2.CC_STAT_WIDTH] >= 2 and st[i, cv2.CC_STAT_HEIGHT] >= 2]
            # glyph-like: several separate small stroke pieces, minority pixels between 4 % and 45 %
            minority = (g > 128).mean(); minority = min(minority, 1 - minority)
            if len(small) >= 4 and 0.04 < minority < 0.45: flagged[ty, tx] = True
    import cv2 as _c
    n, lab, st, _ = _c.connectedComponentsWithStats(flagged.astype(np.uint8), connectivity=8)
    regs = []
    for i in range(1, n):
        if st[i, _c.CC_STAT_AREA] < 3: continue
        x, y, w, h = (int(st[i, k]) * T for k in (_c.CC_STAT_LEFT, _c.CC_STAT_TOP, _c.CC_STAT_WIDTH, _c.CC_STAT_HEIGHT))
        p = arr[y:y + h, x:x + w].reshape(-1, 3).astype(float); lt = rel_lum(p)
        lo, hi = p[lt <= np.percentile(lt, 3)].mean(0), p[lt >= np.percentile(lt, 97)].mean(0)
        if wcag(lo, hi) >= 3.0: continue  # merged region really has enough contrast (tile edges of normal text)
        regs.append(dict(box=[x, y, w, h], tiles=int(st[i, _c.CC_STAT_AREA]), dark=tohex(lo), light=tohex(hi), wcag=round(wcag(lo, hi), 2)))
    return regs

def flat_regions(arr, min_frac=0.0015):
    import cv2
    H, W = arr.shape[:2]; q = arr.astype(np.int32)  # exact colours: UI fills are flat
    key = (q[..., 0] << 16) | (q[..., 1] << 8) | q[..., 2]
    vals, inv, cnt = np.unique(key.reshape(-1), return_inverse=True, return_counts=True); inv = inv.reshape(H, W)
    labels = np.zeros((H, W), np.int32); regs = []; nxt = 1; min_area = max(200, int(H * W * min_frac))
    for vi in np.where(cnt >= min_area)[0]:
        m = (inv == vi).astype(np.uint8); n, lab, st, _ = cv2.connectedComponentsWithStats(m, connectivity=4)
        for i in range(1, n):
            if st[i, cv2.CC_STAT_AREA] < min_area: continue
            w_, h_ = st[i, cv2.CC_STAT_WIDTH], st[i, cv2.CC_STAT_HEIGHT]
            if min(w_, h_) < 8 or st[i, cv2.CC_STAT_AREA] < 4 * (w_ + h_): continue  # lines / 1-2 px outlines are separators, not elements
            sel = lab == i; labels[sel] = nxt; col = arr[sel].reshape(-1, 3).mean(0)
            regs.append(dict(id=nxt, color=col, area=int(st[i, cv2.CC_STAT_AREA]), box=[int(st[i, k]) for k in (0, 1, 2, 3)])); nxt += 1
    return labels, regs

DE_MIN = 2.9  # 'about 3': the palette's own divider on window_bg (#E5E5E5/#F3F3F3) measures 2.97
def adjacency(labels, regs, min_len=24):
    pairs = {}
    for a, b in ((labels[:, :-1], labels[:, 1:]), (labels[:-1, :], labels[1:, :])):
        m = (a > 0) & (b > 0) & (a != b); A, B = a[m], b[m]
        for x, y in zip(np.minimum(A, B), np.maximum(A, B)): pairs[(int(x), int(y))] = pairs.get((int(x), int(y)), 0) + 1
    byid = {r["id"]: r for r in regs}; out = []
    for (i, j), n in pairs.items():
        if n < min_len: continue
        d = float(de2000(rgb_to_lab(byid[i]["color"]), rgb_to_lab(byid[j]["color"])))
        if 0.4 < d < DE_MIN: out.append(dict(a=tohex(byid[i]["color"]), b=tohex(byid[j]["color"]), de=round(d, 2), boundary_px=n, box_a=byid[i]["box"], box_b=byid[j]["box"]))
    return out

def load_palette(p):
    d = json.load(open(p)); return {th: {k: hx(v) for k, v in d["colors"][th].items()} for th in ("light", "dark")}

CONTENT = set()
def load_content(paths):
    """User content (images sent in the chat) is not UI: its exact colours are masked out of every gate."""
    for p in paths:
        a = np.asarray(Image.open(p).convert("RGB")).reshape(-1, 3).astype(np.int64) >> 3  # scaled thumbnails: match at 5-bit precision
        CONTENT.update(np.unique((a[:, 0] << 10) | (a[:, 1] << 5) | a[:, 2]).tolist())

def drop_ui_colours_from_content(pal):
    """Never treat a UI token colour as 'content': the app icon contains white, which used to mask a whole white
    dialog out of every gate (first-run-light was audited as blank). Any 5-bit bucket within dE 3 of a palette token
    in either theme is removed."""
    if not CONTENT: return
    toks = np.stack([v for th in pal.values() for v in th.values()]); tl = rgb_to_lab(toks)
    keep = set()
    for k in CONTENT:
        c = np.array([(k >> 10) & 31, (k >> 5) & 31, k & 31], float) * 8 + 4
        if de2000(rgb_to_lab(c)[None].repeat(len(tl), 0), tl).min() > 3: keep.add(k)
    CONTENT.intersection_update(keep)
def audit(path, pal, theme="auto", out=None, ignore=(), strict_cvd=False, ocr=True):
    img = Image.open(path).convert("RGB"); arr = np.asarray(img).copy(); H, W = arr.shape[:2]
    mask = np.ones((H, W), bool)
    if CONTENT:
        import cv2
        q5 = arr.astype(np.int64) >> 3; k = (q5[..., 0] << 10) | (q5[..., 1] << 5) | q5[..., 2]
        cm = np.isin(k, np.fromiter(CONTENT, np.int64)).astype(np.uint8)
        n, lab, st, _ = cv2.connectedComponentsWithStats(cm, connectivity=8)
        for i in range(1, n):  # only sizeable blobs of content colour (an image), not stray matching pixels
            x, y, w, h = (int(st[i, j]) for j in range(4))
            # an image is a compact blob; a dark photo's shades also occur in dark UI surfaces/wallpaper, and such a
            # sprawling blob (low fill, or most of the screen) must not mask the whole page out of every gate
            if st[i, cv2.CC_STAT_AREA] >= 400 and st[i, cv2.CC_STAT_AREA] >= 0.35 * w * h and w * h <= 0.25 * W * H:
                mask[max(0, y - 3):y + h + 3, max(0, x - 3):x + w + 3] = False
    for (x, y, w, h) in ignore: mask[y:y + h, x:x + w] = False
    if theme == "auto": theme = "dark" if np.median(rel_lum(arr[mask].astype(float))) < 0.18 else "light"
    tok = pal[theme]; toklab = rgb_to_lab(np.stack(list(tok.values())))
    res = dict(file=path, theme=theme, size=[W, H], text=[], text_fail=[], strokes=[], indistinct=[], cvd=[], stray=[])
    # (a)
    boxes = []
    if ocr:  # OCR only the audited area: a dimmed full-page grab around a modal confuses tesseract's layout pass
        ys, xs = np.nonzero(mask); bx0, by0, bx1, by1 = (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1) if len(xs) else (0, 0, W, H)
        boxes = [(x + bx0, y + by0, w, h, t, c) for (x, y, w, h, t, c) in ocr_boxes(img.crop((bx0, by0, bx1, by1)))]
    passing = []
    for b in boxes:
        if not mask[min(H - 1, b[1] + b[3] // 2), min(W - 1, b[0] + b[2] // 2)]: continue
        r = text_check(arr, b, mask)
        if not r: continue
        res["text"].append(r)
        if r["fail"]: res["text_fail"].append(r)
        else: passing.append(r["box"])
    # (b)
    a2 = arr.copy(); a2[~mask] = 0
    res["strokes"] = [s for s in stroke_scan(a2, passing) if mask[s["box"][1] + s["box"][3] // 2, s["box"][0] + s["box"][2] // 2]]
    # (c)
    labels, regs = flat_regions(arr); labels[~mask] = 0
    res["indistinct"] = adjacency(labels, regs)
    on_screen = {}
    q = arr[mask].reshape(-1, 3)
    for k in ("online", "warning", "error"):
        if (np.abs(q.astype(int) - tok[k]).max(1) <= 6).sum() >= 20: on_screen[k] = tok[k]
    ks = list(on_screen)
    for i in range(len(ks)):
        for j in range(i + 1, len(ks)):
            for kind in MACHADO:
                d = float(de2000(rgb_to_lab(simulate(on_screen[ks[i]], kind)), rgb_to_lab(simulate(on_screen[ks[j]], kind))))
                if d < 10: res["cvd"].append(dict(pair=[ks[i], ks[j]], sim=kind, de=round(d, 2)))
    # (d)
    # palette conformance applies to FILLS (flat regions >= 8 px thick). Glyph anti-aliasing is judged by (a)/(b),
    # and 1-2 px OS window borders / hairlines are not fills.
    fill = labels[mask] > 0; qf = q[fill]
    key = (qf.astype(np.int32) // 4); kk = (key[:, 0] << 12) | (key[:, 1] << 6) | key[:, 2]
    vals, inv, cnt = np.unique(kk, return_inverse=True, return_counts=True)
    for vi in np.where(cnt >= 0.004 * len(q))[0]:
        col = qf[inv == vi].mean(0); d = de2000(rgb_to_lab(col)[None].repeat(len(toklab), 0), toklab)
        if d.min() > 2.0:
            sel = np.zeros((H, W), bool); sm = np.zeros(len(q), bool); sm[np.nonzero(fill)[0][inv == vi]] = True; sel[mask] = sm; ys, xs = np.nonzero(sel)
            res["stray"].append(dict(color=tohex(col), frac=round(float(cnt[vi]) / len(q), 4), nearest=list(tok)[int(d.argmin())], de=round(float(d.min()), 2),
                                     box=[int(xs.min()), int(ys.min()), int(xs.max() - xs.min() + 1), int(ys.max() - ys.min() + 1)]))
    res["ocr_words"] = len(boxes)
    res["no_text"] = bool(ocr and not boxes and W * H >= 10000)  # a real app screen always has text: blank/black/mis-cropped grabs must not pass
    res["fail"] = bool(res["no_text"] or res["text_fail"] or res["strokes"] or res["indistinct"] or res["stray"] or (strict_cvd and res["cvd"]))
    if out:
        os.makedirs(out, exist_ok=True); name = os.path.splitext(os.path.basename(path))[0]
        an = img.copy(); dr = ImageDraw.Draw(an)
        for (x, y, w, h) in ignore: dr.rectangle([x, y, x + w, y + h], outline=(128, 128, 128))
        for r in res["text_fail"]: x, y, w, h = r["box"]; dr.rectangle([x - 1, y - 1, x + w + 1, y + h + 1], outline=(255, 0, 0), width=2)
        for r in res["strokes"]: x, y, w, h = r["box"]; dr.rectangle([x, y, x + w, y + h], outline=(255, 0, 255), width=3)
        for r in res["indistinct"]:
            for bx in (r["box_a"], r["box_b"]): x, y, w, h = bx; dr.rectangle([x, y, x + w, y + h], outline=(255, 140, 0), width=2)
        for r in res["stray"]: x, y, w, h = r["box"]; dr.rectangle([x, y, x + w, y + h], outline=(255, 220, 0), width=2)
        an.save(f"{out}/{name}.annot.png"); json.dump(res, open(f"{out}/{name}.json", "w"), ensure_ascii=False, indent=1)
    return res

def summary(r):
    return (f"{'FAIL' if r['fail'] else 'PASS'} {os.path.basename(r['file'])} [{r['theme']}] text={len(r['text'])} "
            f"text_fail={len(r['text_fail'])} strokes={len(r['strokes'])} indistinct={len(r['indistinct'])} stray={len(r['stray'])} cvd_warn={len(r['cvd'])}"
            + (f" min_wcag={min(t['wcag'] for t in r['text']):.2f}" if r['text'] else "")) + (" NO_TEXT(blank or mis-cropped grab)" if r.get('no_text') else "") + (f" ocr_words={r.get('ocr_words')}" if not r['text'] else "")

# ------------------------------------------------------------------ self test
def self_test(pal, out):
    from PIL import ImageFont
    fonts = [p for p in ("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf") if os.path.exists(p)]
    f = ImageFont.truetype(fonts[0], 16) if fonts else ImageFont.load_default()
    os.makedirs(out, exist_ok=True); ok = True
    def mk(name, bg, fg, extra=None):
        im = Image.new("RGB", (480, 200), bg); d = ImageDraw.Draw(im)
        for i, line in enumerate(["你好，这是一条测试消息 Hello", "设置 群组 传输 诊断 联系人", "Black text on dark grey 12345"]): d.text((20, 30 + i * 40), line, fill=fg, font=f)
        if extra: extra(d)
        p = f"{out}/{name}.png"; im.save(p); return p
    cases = [("synthetic-black-on-202020", "#202020", "#000000", True),
             ("synthetic-333-on-202020", "#202020", "#333333", True),
             ("synthetic-light-on-light", "#F3F3F3", "#E0E0E0", True),
             ("synthetic-good-dark", "#202020", "#FFFFFF", False),
             ("synthetic-good-light", "#F3F3F3", "#1A1A1A", False)]
    for name, bg, fg, expect in cases:
        r = audit(mk(name, bg, fg), pal, out=out); got = r["fail"]; print(("ok  " if got == expect else "BAD ") + summary(r)); ok &= got == expect
    # (c) indistinct card + (d) stray colour
    def card(d): d.rounded_rectangle([20, 140, 460, 190], 8, fill="#232323")
    r = audit(mk("synthetic-indistinct-card", "#202020", "#FFFFFF", card), pal, out=out)
    good = bool(r["indistinct"]); print(("ok  " if good else "BAD ") + summary(r) + " (expect indistinct)"); ok &= good
    def badge(d): d.rectangle([20, 140, 300, 190], fill="#8E44AD")
    r = audit(mk("synthetic-stray-colour", "#202020", "#FFFFFF", badge), pal, out=out)
    good = bool(r["stray"]); print(("ok  " if good else "BAD ") + summary(r) + " (expect stray)"); ok &= good
    # (e) a blank grab (black window / mis-cropped tray icon) must not pass silently
    from PIL import Image as _I; bp = os.path.join(out, "synthetic-blank-grab.png"); _I.new("RGB", (480, 240), (0, 0, 0)).save(bp)
    r = audit(bp, pal, out=out); good = bool(r["no_text"] and r["fail"]); print(("ok  " if good else "BAD ") + summary(r) + " (expect NO_TEXT)"); ok &= good
    # unit checks of the maths against published values
    t = [abs(wcag(hx("#000000"), hx("#FFFFFF")) - 21) < 1e-6, abs(wcag(hx("#000000"), hx("#202020")) - 1.29) < 0.01,
         abs(apca(hx("#000000"), hx("#FFFFFF")) - 106.04) < 0.1, abs(apca(hx("#FFFFFF"), hx("#000000")) + 107.88) < 0.1,
         abs(float(de2000(np.array([50, 2.6772, -79.7751]), np.array([50, 0, -82.7485]))) - 2.0425) < 1e-3]  # Sharma 2005 pair 1
    print("maths:", t); ok &= all(t)
    return ok

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("imgs", nargs="*"); ap.add_argument("--palette", default=os.path.join(HERE, "palette.json"))
    ap.add_argument("--theme", default="auto"); ap.add_argument("--out", default="ui-audit")
    ap.add_argument("--ignore", action="append", default=[], help="x,y,w,h rectangle excluded from all gates (OS chrome / user content); may repeat")
    ap.add_argument("--ignore-file", help="JSON {basename: [[x,y,w,h],...] | {\"ignore\": [...], \"only\": [x,y,w,h]}}; every entry must carry a reason in _reasons")
    ap.add_argument("--content", action="append", default=[], help="image file sent as chat content (masked, not UI); may repeat")
    ap.add_argument("--strict-cvd", action="store_true"); ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args(); pal = load_palette(a.palette); load_content(a.content); drop_ui_colours_from_content(pal)
    if a.self_test: sys.exit(0 if self_test(pal, a.out) else 1)
    ign = [tuple(map(int, s.split(","))) for s in a.ignore]; per = json.load(open(a.ignore_file)) if a.ignore_file else {}
    per = {k: v for k, v in per.items() if not k.startswith('_')}
    rows, bad = [], 0
    for p in a.imgs:
        th = a.theme
        if th == "auto": th = "dark" if "dark" in os.path.basename(p).lower() else "light" if "light" in os.path.basename(p).lower() else "auto"
        spec = per.get(os.path.basename(p), [])
        rects = ign + [tuple(x) for x in (spec.get("ignore", []) if isinstance(spec, dict) else spec)]
        if isinstance(spec, dict) and "only" in spec:  # modal dialog: content behind the scrim is inactive (WCAG-exempt)
            ox, oy, ow, oh = spec["only"]; Wd, Hd = Image.open(p).size
            rects += [(0, 0, Wd, oy), (0, oy + oh, Wd, Hd - oy - oh), (0, oy, ox, oh), (ox + ow, oy, Wd - ox - ow, oh)]
        r = audit(p, pal, th, a.out, rects, a.strict_cvd); s = summary(r); print(s, flush=True); rows.append((r, s)); bad += r["fail"]
    with open(os.path.join(a.out, "report.md"), "w") as f:
        f.write(f"# ui_color_audit report\n\n{len(rows)} screenshots, {bad} failing\n\n")
        for r, s in rows:
            f.write(f"- {s}\n")
            for t in r["text_fail"]: f.write(f"  - text '{t['text']}' fg {t['fg']} on {t['bg']}: {'; '.join(t['fail'])} (APCA {t['apca']})\n")
            for t in r["strokes"]: f.write(f"  - low-contrast strokes at {t['box']}: {t['dark']} vs {t['light']} = {t['wcag']}:1\n")
            for t in r["indistinct"]: f.write(f"  - indistinct regions {t['a']} | {t['b']} dE {t['de']}\n")
            for t in r["stray"]: f.write(f"  - stray colour {t['color']} ({t['frac']*100:.1f} %, nearest {t['nearest']} dE {t['de']}) box {t['box']}\n")
            for t in r["cvd"]: f.write(f"  - CVD warning {t['pair']} under {t['sim']}: dE {t['de']}\n")
    sys.exit(1 if bad else 0)
