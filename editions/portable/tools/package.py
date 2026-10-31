#!/usr/bin/env python3
"""Assemble dist/LanMessenger-XP/ and the release zip.
.bat and README.txt are written in GBK (cp936, the ANSI code page of Chinese XP) with CRLF and no
BOM, because XP's cmd.exe cannot run UTF-8 batch files reliably. A UTF-8 (BOM) copy of the README
is included for non-Chinese systems."""
import os, shutil, sys, zipfile
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VER = "v0.6.0"
DIST = os.path.join(ROOT, "dist"); OUT = os.path.join(DIST, "LanMessenger-XP")
if os.path.exists(DIST): shutil.rmtree(DIST)
os.makedirs(os.path.join(OUT, "x64"))
for a, b in [("build/x86/LanMessenger.exe", "LanMessenger.exe"), ("build/x86/lanmsg-cli.exe", "lanmsg-cli.exe"),
             ("build/x64/LanMessenger.exe", "x64/LanMessenger.exe"), ("build/x64/lanmsg-cli.exe", "x64/lanmsg-cli.exe")]:
    shutil.copy(os.path.join(ROOT, a), os.path.join(OUT, b))
def gbk_crlf(src, dst):
    text = open(os.path.join(ROOT, "packaging", src), encoding="utf-8").read().replace("\r\n", "\n")
    data = text.replace("\n", "\r\n").encode("gbk")   # raises if a char is not representable in GBK
    open(os.path.join(OUT, dst), "wb").write(data)
    assert b"\r\n" in data and not data.startswith(b"\xef\xbb\xbf") and b"\n" not in data.replace(b"\r\n", b"")
for f in ["start.bat", "cli.bat", "firewall-setup.bat", "README.txt"]:
    gbk_crlf(f + ".txt", f)
readme = open(os.path.join(ROOT, "packaging", "README.txt.txt"), encoding="utf-8").read().replace("\n", "\r\n")
open(os.path.join(OUT, "README-UTF8.txt"), "wb").write(b"\xef\xbb\xbf" + readme.encode("utf-8"))
shutil.copy(os.path.join(ROOT, "LICENSE"), os.path.join(OUT, "LICENSE.txt"))
z = os.path.join(DIST, f"LanMessenger-XP-{VER}.zip")
with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED) as zf:
    for dp, dn, fn in os.walk(OUT):
        for f in sorted(fn):
            p = os.path.join(dp, f); zf.write(p, os.path.relpath(p, DIST))
print(z)
