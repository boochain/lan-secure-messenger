#!/usr/bin/env python3
"""Per-function check that every imported DLL/function exists on Windows XP SP3 (NT 5.1, x86)
or Windows XP x64 / Server 2003 (NT 5.2, x64).

Source of truth: ReactOS .spec export files (tools/xp_exports/reactos/, fetched from
github.com/reactos/reactos master). ReactOS targets the NT 5.2 ABI and annotates every export
that is *newer* than that with `-version=0x600+` (Vista) etc.  Rules applied here:
  * export must exist in the spec for that DLL;
  * `-version=` ranges must include the target (0x501 for x86, 0x502 for x64);
    anything marked 0x502-only is rejected for the x86/XP target (stricter than ReactOS);
  * `-arch=` must match; `-impsym` / `-stub` / `-private` exports are rejected
    (impsym = ReactOS-only alias, not a real XP export);
  * a hard deny-list of well-known post-XP APIs is checked as a second opinion;
  * DLL must be in the XP system DLL allow-list;
  * PE Major/MinorSubsystemVersion and OS version must be <= target (5.01 / 5.02).
Exit code 0 = pass.
"""
import os, re, sys, pefile

HERE = os.path.dirname(os.path.abspath(__file__))
SPECDIR = os.path.join(HERE, "xp_exports", "reactos")
XP_DLLS = {"kernel32","user32","gdi32","ws2_32","advapi32","shell32","comctl32","comdlg32","iphlpapi","msvcrt","winmm"}
DENY = {"GetTickCount64","inet_pton","inet_ntop","InitializeConditionVariable","SleepConditionVariableCS","WakeConditionVariable",
        "InitializeSRWLock","AcquireSRWLockExclusive","AcquireSRWLockShared","CancelIoEx","InitOnceExecuteOnce","GetAdaptersAddresses_Vista",
        "BCryptGenRandom","BCryptOpenAlgorithmProvider","WSAPoll","GetFileInformationByHandleEx","CreateSymbolicLinkW","FlsAlloc_Vista",
        "GetLocaleInfoEx","LCIDToLocaleName","GetQueuedCompletionStatusEx","CompareStringEx","SetFileInformationByHandle","GetErrorMode",
        "RegGetValueW","IsWow64Process2","GetSystemTimePreciseAsFileTime","QueryFullProcessImageNameW","GetFinalPathNameByHandleW",
        "AddDllDirectory","SetDefaultDllDirectories","CreateEventExW","CreateMutexExW","InitializeCriticalSectionEx","GetThreadId","GetProcessId",
        "_create_locale","_wfopen_s","fopen_s","strcpy_s","sprintf_s","_vscwprintf_l","__stdio_common_vfprintf","_localtime64_s"}

def parse_spec(path):
    out = {}
    for line in open(path, encoding="utf-8", errors="replace"):
        line = line.split("#",1)[0].strip()
        if not (line.startswith("@") or line[:1].isdigit()): continue
        toks = line.split()
        if len(toks) < 3: continue
        flags = [t for t in toks[2:] if t.startswith("-")]
        rest = [t for t in toks[2:] if not t.startswith("-")]
        if not rest: continue
        name = re.split(r"[\(\s]", rest[0])[0]
        out.setdefault(name, []).append(flags)
    return out

def version_ok(flags, target):
    for f in flags:
        if f.startswith("-version="):
            spec = f.split("=",1)[1]
            for part in spec.split(","):
                if part.endswith("+"):
                    if target < int(part[:-1],16): return False
                    return True
                if "-" in part[2:]:
                    lo, hi = part.split("-")
                    if int(lo,16) <= target <= int(hi,16): return True
                    return False
                if int(part,16) != target: return False
    return True

def arch_ok(flags, is64):
    for f in flags:
        if f.startswith("-arch="):
            arches = f.split("=",1)[1].split(",")
            want = {"x86_64","win64","amd64"} if is64 else {"i386","win32","x86"}
            neg = [a[1:] for a in arches if a.startswith("!")]
            if neg: return not any(a in want for a in neg)
            return any(a in want for a in arches)
    return True

def check(exe):
    pe = pefile.PE(exe)
    is64 = pe.FILE_HEADER.Machine == 0x8664
    target = 0x502 if is64 else 0x501
    oh = pe.OPTIONAL_HEADER
    errors, n = [], 0
    sub = (oh.MajorSubsystemVersion, oh.MinorSubsystemVersion)
    osv = (oh.MajorOperatingSystemVersion, oh.MinorOperatingSystemVersion)
    lim = (5, 2 if is64 else 1)
    if sub > lim: errors.append(f"subsystem version {sub} > {lim}")
    if osv > lim: errors.append(f"OS version {osv} > {lim}")
    specs = {}
    for e in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        dll = e.dll.decode().lower().removesuffix(".dll")
        if dll not in XP_DLLS:
            errors.append(f"DLL {dll} not in XP allow-list"); continue
        if dll not in specs: specs[dll] = parse_spec(os.path.join(SPECDIR, dll + ".spec"))
        for imp in e.imports:
            n += 1
            if imp.name is None:
                errors.append(f"{dll}!#{imp.ordinal} imported by ordinal (cannot verify)"); continue
            fn = imp.name.decode()
            if fn in DENY and not os.environ.get("XPCHECK_NO_DENY"): errors.append(f"{dll}!{fn} is a known post-XP API"); continue
            variants = specs[dll].get(fn)
            if not variants: errors.append(f"{dll}!{fn} not exported on NT {target:#x}"); continue
            good = False
            for fl in variants:
                if any(f in ("-impsym","-stub","-private") for f in fl): continue
                if version_ok(fl, target) and arch_ok(fl, is64): good = True; break
            if not good: errors.append(f"{dll}!{fn} requires a newer Windows than NT {target:#x} ({variants})")
    return is64, sub, n, errors

def main(paths):
    bad = 0
    for p in paths:
        is64, sub, n, errs = check(p)
        tag = "x64/NT5.2" if is64 else "x86/NT5.1 (XP SP3)"
        if errs:
            bad += 1
            print(f"FAIL {p} [{tag}] subsystem={sub[0]}.{sub[1]:02d} imports={n}")
            for e in errs: print("   -", e)
        else:
            print(f"PASS {p} [{tag}] subsystem={sub[0]}.{sub[1]:02d} imports={n} (all functions present on target)")
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
