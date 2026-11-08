#!/usr/bin/env python3
"""Fail if any platform listed for a feature is missing implemented / automated_test / gui_verified.

implemented     = hook id present in that platform's UI sources
automated_test  = listed file exists and contains the #fragment (engine/C unit tests OK)
gui_verified    = listed file exists, contains the #fragment, and is a screenshot/smoke
                  driver — unit tests cannot count as GUI verification
"""
import os, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]

UNIT_MARKERS = ("lan-core/tests/", "c_unit_test.c", "/engine.rs")
GUI_MARKERS = (
    "UiSmoke", "ui_smoke", "_shots.", "android_chat_shots",
    "xp_ui_shots", "linux_gui_shots", "adw_gui_shots", "xp_basics",
)

def is_unit_path(path):
    p = path.replace("\\", "/")
    return any(m in p for m in UNIT_MARKERS)

def is_gui_path(path):
    p = path.replace("\\", "/")
    return any(m in p for m in GUI_MARKERS) and not is_unit_path(p)

def parse_yml(text):
    # Tiny subset: platforms.<name>.roots list, features list of maps (incl. nested maps)
    platforms, features = {}, []
    section = None
    cur_plat = None
    in_roots = False
    cur = None
    nest_key = None
    for raw in text.splitlines():
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        indent = len(raw) - len(raw.lstrip(" "))
        line = raw.strip()
        if indent == 0 and line.startswith("platforms:"):
            section = "platforms"; cur_plat = None; in_roots = False; continue
        if indent == 0 and line.startswith("features:"):
            section = "features"; cur = None; nest_key = None; continue
        if section == "platforms":
            if indent == 2 and line.endswith(":") and not line.startswith("-"):
                cur_plat = line[:-1]; platforms[cur_plat] = []; in_roots = False
            elif indent == 4 and line.startswith("roots:"):
                in_roots = True
            elif in_roots and indent >= 6 and line.startswith("- "):
                platforms[cur_plat].append(line[2:].strip())
            else:
                in_roots = False
        elif section == "features":
            if indent == 2 and line.startswith("- "):
                cur = {}
                features.append(cur)
                nest_key = None
                rest = line[2:]
                if ":" in rest:
                    k, _, v = rest.partition(":")
                    cur[k.strip()] = v.strip()
            elif cur is not None and indent == 4 and ":" in line:
                k, _, v = line.partition(":")
                k, v = k.strip(), v.strip()
                if v.startswith("[") and v.endswith("]"):
                    cur[k] = [x.strip() for x in v[1:-1].split(",") if x.strip()]
                    nest_key = None
                elif not v:
                    cur[k] = {}
                    nest_key = k
                else:
                    cur[k] = v
                    nest_key = None
            elif cur is not None and nest_key and indent >= 6 and ":" in line:
                k, _, v = line.partition(":")
                cur[nest_key][k.strip()] = v.strip()
    return platforms, features

def collect_text(roots):
    chunks = []
    for r in roots:
        p = ROOT / r
        if p.is_file():
            try: chunks.append(p.read_text(encoding="utf-8", errors="replace"))
            except OSError: pass
            continue
        if not p.is_dir():
            continue
        for fp in p.rglob("*"):
            if not fp.is_file(): continue
            if fp.suffix.lower() not in {".c", ".h", ".inc", ".cs", ".xaml", ".kt", ".xml", ".py", ".js"}:
                continue
            if "bin/" in str(fp) or "obj/" in str(fp) or "__pycache__" in str(fp):
                continue
            try: chunks.append(fp.read_text(encoding="utf-8", errors="replace"))
            except OSError: pass
    return "\n".join(chunks)

def split_refs(val):
    if isinstance(val, list):
        items = val
    else:
        items = [x.strip() for x in str(val).split(",") if x.strip()]
    out = []
    for item in items:
        if "#" in item:
            path, frag = item.split("#", 1)
        else:
            path, frag = item, ""
        out.append((path.strip(), frag.strip()))
    return out

def check_refs(feat_id, plat, kind, val, missing):
    if not val:
        missing.append(f"{feat_id}: {plat} has no {kind} path")
        return
    for path, frag in split_refs(val):
        fp = ROOT / path
        if not fp.is_file():
            missing.append(f"{feat_id}: {kind} file missing on {plat}: {path}")
            continue
        if kind == "gui_verified":
            if is_unit_path(path) or not is_gui_path(path):
                missing.append(
                    f"{feat_id}: {plat} gui_verified {path}: unit tests cannot count as GUI verification"
                )
                continue
        try:
            text = fp.read_text(encoding="utf-8", errors="replace")
        except OSError:
            missing.append(f"{feat_id}: {kind} unreadable on {plat}: {path}")
            continue
        if frag and frag not in text:
            missing.append(f"{feat_id}: {kind} {path} on {plat} missing fragment {frag!r}")

def main():
    doc = (ROOT / "docs" / "features.yml").read_text(encoding="utf-8")
    platforms, features = parse_yml(doc)
    if not platforms or not features:
        sys.exit("docs/features.yml: failed to parse platforms/features")
    cache = {name: collect_text(roots) for name, roots in platforms.items()}
    missing = []
    for feat in features:
        hook = feat.get("hook") or ""
        fid = feat.get("id") or hook
        wanted = feat.get("platforms") or list(platforms)
        if isinstance(wanted, str):
            wanted = [wanted]
        auto = feat.get("automated_test") if isinstance(feat.get("automated_test"), dict) else {}
        gui = feat.get("gui_verified") if isinstance(feat.get("gui_verified"), dict) else {}
        if not auto:
            missing.append(f"{fid}: missing automated_test map")
        if not gui:
            missing.append(f"{fid}: missing gui_verified map")
        for plat in wanted:
            if plat not in cache:
                missing.append(f"{fid}: unknown platform {plat}")
                continue
            if hook not in cache[plat]:
                missing.append(f"{fid} ({hook}) not implemented on {plat}")
            check_refs(fid, plat, "automated_test", auto.get(plat), missing)
            check_refs(fid, plat, "gui_verified", gui.get(plat), missing)
    print(f"checked {len(features)} features across {', '.join(platforms)} "
          f"(implemented / automated_test / gui_verified)")
    if missing:
        print("MISSING:")
        for m in missing:
            print("  -", m)
        sys.exit(1)
    print("ok: every listed platform has implemented + automated test + GUI verified (screenshot/smoke)")

if __name__ == "__main__":
    main()
