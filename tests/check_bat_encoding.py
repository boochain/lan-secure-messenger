#!/usr/bin/env python3
"""Every .bat / README.txt in the portable folder must be GBK-decodable, CRLF-only, BOM-free, and every
*command* line must be pure ASCII (only echo/rem text may contain Chinese), so they also run on a
non-Chinese XP cmd.exe."""
import os, sys
d = sys.argv[1]; bad = 0
for f in sorted(os.listdir(d)):
    if not (f.endswith(".bat") or f == "README.txt"): continue
    b = open(os.path.join(d, f), "rb").read()
    errs = []
    if b.startswith(b"\xef\xbb\xbf"): errs.append("has UTF-8 BOM")
    if b"\n" in b.replace(b"\r\n", b""): errs.append("bare LF")
    try: t = b.decode("gbk")
    except UnicodeDecodeError as e: errs.append(f"not GBK: {e}"); t = ""
    if f.endswith(".bat"):
        for i, line in enumerate(t.split("\r\n"), 1):
            s = line.strip().lower()
            if s and not (s.startswith("rem") or s.startswith("echo") or s.startswith("::")) and any(ord(c) > 127 for c in line):
                errs.append(f"line {i}: non-ASCII in a command: {line!r}")
    print(("FAIL " if errs else "PASS ") + f + ("" if not errs else ": " + "; ".join(errs))); bad += bool(errs)
sys.exit(1 if bad else 0)
