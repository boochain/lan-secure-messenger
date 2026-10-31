#!/usr/bin/env python3
"""Single source of the release version for every edition.
  python3 tools/set-version.py          write the version into every place that embeds it
  python3 tools/set-version.py --check  fail if any place differs (CI gate; on a v* tag the tag must match too)
Version = the git tag (GITHUB_REF_TYPE=tag, GITHUB_REF_NAME=v<ver>) when building a tag, else the VERSION file."""
import os, re, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
file_ver = open(os.path.join(ROOT, "VERSION"), encoding="utf-8").read().strip()
tag = os.environ.get("GITHUB_REF_NAME", "") if os.environ.get("GITHUB_REF_TYPE") == "tag" else ""
V = tag[1:] if tag.startswith("v") else file_ver
m = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)(?:-[a-z]+\.(\d+))?", V)
if not m: sys.exit(f"bad version {V!r}")
maj, mi, pa, pre = (int(x) if x else None for x in m.groups())
CODE = maj * 1000000 + mi * 10000 + pa * 100 + (pre if pre is not None else 99)   # Android versionCode, monotonic
NUM = f"{maj},{mi},{pa},{pre or 0}"; DOT = NUM.replace(",", ".")
P = "editions/portable/"
SITES = [  # (file, regex with one group to replace, replacement)
    ("VERSION", r"^(.*)$", V),
    ("Cargo.toml", r'(?m)^\[workspace\.package\][^\[]*?^version = "([^"]*)"', V),
    ("relay-server/Cargo.toml", r'(?m)^version = "([^"]*)"', V),
    ("packaging/windows/README.txt", r"局域网安全通讯  v(\S+)", V),
    ("relay-server/deploy/deploy.sh", r'SCRIPT_VERSION="([^"]*)"', V),
    ("relay-server/deploy/deploy.ps1", r"\$ScriptVersion = '([^']*)'", V),
    ("ui-winui/LanMessenger/LanMessenger.csproj", r"<Version>([^<]*)</Version>", V),
    (P + "src/core.h", r'#define LSM_APP_VERSION "([^"]*)"', V),
    ("ui-winui/LanMessenger/LanMessenger.csproj", r"<InformationalVersion>([^<]*)</InformationalVersion>", V),
    ("ui-winui/LanMessenger/app.manifest", r'<assemblyIdentity version="([^"]*)"', DOT),
    (P + "src/app.manifest", r'<assemblyIdentity version="([^"]*)"', DOT),
    (P + "linux/data/README-zh.txt", r"Linux 版 (\S+)", V),
    (P + "src/res.rc", r'VALUE "ProductVersion", "([^"]*)"', V),
    (P + "src/res.rc", r"FILEVERSION (\S+)", NUM),
    (P + "src/res.rc", r"PRODUCTVERSION (\S+)", NUM),
    (P + "tools/package.py", r'VER = "v([^"]*)"', V),
    (P + "packaging/README.txt.txt", r"legacy edition\) v(\S+)", V),
    (P + "linux/build.sh", r"VER=\$\{VER:-([^}]*)\}", V),
    (P + "linux/build-adw.sh", r"VER=\$\{VER:-([^}]*)\}", V),
    (P + "linux/adw-appimage/build.sh", r"VER=\$\{VER:-([^}]*)\}", V),
    (P + "android/app/src/main/cpp/CMakeLists.txt", r'LSM_APP_VERSION="([^"]*)"', V),
    (P + "android/build-cli.sh", r'LSM_APP_VERSION=\\"([^"\\]*)\\"', V),
    (P + "android/app/build.gradle.kts", r'versionName = "([^"]*)"', V),
    (P + "android/app/build.gradle.kts", r"versionCode = (\d+)", str(CODE)),
]
check = "--check" in sys.argv; bad = []
if tag and tag[1:] != file_ver: bad.append(f"tag {tag} != VERSION {file_ver}")
for f, rx, new in SITES:
    p = os.path.join(ROOT, f); s = open(p, encoding="utf-8", newline="").read()
    mm = re.search(rx, s)
    if not mm: bad.append(f"{f}: pattern {rx!r} not found"); continue
    if mm.group(1) != new:
        if check: bad.append(f"{f}: {mm.group(1)!r} != {new!r}")
        else: s = s[:mm.start(1)] + new + s[mm.end(1):]; open(p, "w", encoding="utf-8", newline="").write(s); print(f"set {f}: {new}")
if bad: sys.exit("version mismatch:\n  " + "\n  ".join(bad))
print(f"version {V} (versionCode {CODE}) {'consistent' if check else 'written'} in {len(SITES)} places")
