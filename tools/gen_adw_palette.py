#!/usr/bin/env python3
"""Regenerate linux/adw_palette.h (GTK4/libadwaita @define-color table) from tools/palettes/gnome.json."""
import json, os
R = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
d = json.load(open(os.path.join(R, "tools/palettes/gnome.json")))["colors"]
out = ["/* generated from tools/palettes/gnome.json (python3 tools/gen_adw_palette.py) — do not edit */", "static const char *LSM_ADW_PALETTE[2] = {"]
for th in ("light", "dark"):
    out.append('  "' + "".join(f"@define-color lsm_{k} {v};" for k, v in d[th].items()) + '",')
out.append("};")
open(os.path.join(R, "linux/adw_palette.h"), "w").write("\n".join(out) + "\n")
