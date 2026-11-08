#!/usr/bin/env python3
"""Fail if any platform listed for a feature is missing that feature's hook id."""
import os, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]

def parse_yml(text):
    # Tiny subset: platforms.<name>.roots list, features list of id/title/hook/platforms
    platforms, features = {}, []
    section = None
    cur_plat = None
    in_roots = False
    cur = None
    for raw in text.splitlines():
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        indent = len(raw) - len(raw.lstrip(" "))
        line = raw.strip()
        if indent == 0 and line.startswith("platforms:"):
            section = "platforms"; cur_plat = None; in_roots = False; continue
        if indent == 0 and line.startswith("features:"):
            section = "features"; cur = None; continue
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
                rest = line[2:]
                if ":" in rest:
                    k, _, v = rest.partition(":")
                    cur[k.strip()] = v.strip()
            elif cur is not None and indent >= 4 and ":" in line:
                k, _, v = line.partition(":")
                k, v = k.strip(), v.strip()
                if v.startswith("[") and v.endswith("]"):
                    cur[k] = [x.strip() for x in v[1:-1].split(",") if x.strip()]
                else:
                    cur[k] = v
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
        for plat in wanted:
            if plat not in cache:
                missing.append(f"{fid}: unknown platform {plat}")
                continue
            if hook not in cache[plat]:
                missing.append(f"{fid} ({hook}) missing on {plat}")
    print(f"checked {len(features)} features across {', '.join(platforms)}")
    if missing:
        print("MISSING:")
        for m in missing:
            print("  -", m)
        sys.exit(1)
    print("ok: every listed platform contains each feature hook")

if __name__ == "__main__":
    main()
