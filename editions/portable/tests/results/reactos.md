# ReactOS 0.4.15 LiveCD (QEMU TCG, i386, pentium3) run — proxy for XP, NOT real XP

- reactos-01: both x86 exes visible on FAT16 disk C:.
- reactos-02: lanmsg-cli.exe (pre-fix build) loads, runs, creates nested data dir C:\data\lsm\cli,
  generates identity, prints "EV ready <fp> ReactOS-CLI 41235 41234", then exits ("OK bye"):
  ReadConsoleW with a 65535-char buffer failed (CSRSS ~64 KB limit, also applies to real XP).
  Fixed afterwards (2048-char reads); fixed build NOT re-run on ReactOS (time box).
- reactos-03: piped mode (`type c.txt | lanmsg-cli.exe`) produced no output for >3 min and no
  link reached the v0.5 daemon on the host (10.0.2.2:47235) — unresolved; cause unknown
  (pipe stdout buffering under ReactOS vs. networking via slirp). Not verified.
- GUI (LanMessenger.exe) was not launched on ReactOS.
