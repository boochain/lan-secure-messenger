# Windows two-daemon loopback smoke test (named-pipe IPC). Used by .github/workflows/windows.yml.
#  1. one-way discovery: B has discovery_listen=false and unicasts announces to A -> both online
#  2. text both ways + delivery receipts
#  3. 2 MB file transfer + hash check
#  4. drop the connection (debug RPC) -> auto reconnect, queued message delivered once
#  5. manual add-by-IP only (C has discovery disabled entirely)
#  6. clean shutdown, logs present
param([string]$Bin = "target\release")
$ErrorActionPreference = "Stop"
$D = Join-Path $Bin "lan-daemon.exe"; $M = Join-Path $Bin "lan-msg.exe"
$T = Join-Path $env:RUNNER_TEMP ("lsm-smoke-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory -Force $T | Out-Null
$PA = Get-Random -Minimum 42000 -Maximum 48000; $PB = $PA + 1; $PC = $PA + 2
$id = [guid]::NewGuid().ToString("N").Substring(0, 8)
$pipeA = "\\.\pipe\lsm-ci-a-$id"; $pipeB = "\\.\pipe\lsm-ci-b-$id"; $pipeC = "\\.\pipe\lsm-ci-c-$id"
$procs = @()

function Start-Daemon($name, $pipe, [string[]]$extra) {
  $argv = @("run", "--data-dir", (Join-Path $T $name), "--ipc", $pipe, "--set", "listen_port=0", "--set", "multicast_enabled=false",
            "--set", "auto_trust=true", "--set", "announce_interval_secs=1", "--set", "display_name=$name",
            "--set", "download_dir=$(Join-Path $T "$name/dl")") + $extra
  # Start-Process joins arguments verbatim, so quote each one MSVC-style ourselves.
  $line = ($argv | ForEach-Object { '"' + ($_ -replace '"', '\"') + '"' }) -join " "
  $p = Start-Process -FilePath $D -ArgumentList $line -PassThru -NoNewWindow `
       -RedirectStandardOutput (Join-Path $T "$name.out") -RedirectStandardError (Join-Path $T "$name.err")
  $script:procs += $p
  return $p
}
function LsmCli($pipe, [string[]]$a) {
  $errf = Join-Path $T "cli-stderr.txt"
  $out = & $M --ipc $pipe @a 2>$errf
  if ($LASTEXITCODE -ne 0) { throw "lan-msg $($a -join ' ') failed ($LASTEXITCODE): $($out -join ' ') $(Get-Content $errf -Raw -ErrorAction SilentlyContinue)" }
  return ($out -join "`n")
}
function LsmJson($pipe, [string[]]$a) { return (LsmCli $pipe (@("--json") + $a)) | ConvertFrom-Json }
function WaitPipe($pipe) {
  $last = ""
  for ($i = 0; $i -lt 100; $i++) { try { LsmJson $pipe @("status") | Out-Null; return } catch { $last = "$_"; Start-Sleep -Milliseconds 200 } }
  throw "daemon on $pipe did not come up; last error: $last"
}
function Ok($msg) { Write-Host "[ok] $msg" -ForegroundColor Green }

try {
  $env:LSM_LOG = "info"
  # Windows cmd-line quoting of JSON arrays: pwsh 7.3+ passes embedded quotes correctly.
  Start-Daemon "a" $pipeA @("--set", "discovery_port=$PA") | Out-Null
  Start-Daemon "b" $pipeB @("--set", "discovery_port=$PB", "--set", "discovery_listen=false",
                            "--set", "announce_targets=[`"127.0.0.1:$PA`"]") | Out-Null
  WaitPipe $pipeA; WaitPipe $pipeB
  $FA = (LsmJson $pipeA @("status")).me.fingerprint
  $FB = (LsmJson $pipeB @("status")).me.fingerprint
  Write-Host "A=$FA B=$FB"
  LsmCli $pipeA @("wait-peer", $FB, "--timeout", "30") | Out-Null
  LsmCli $pipeB @("wait-peer", $FA, "--timeout", "30") | Out-Null
  Ok "one-way discovery -> both online"

  $id1 = (LsmJson $pipeA @("send", $FB, "你好 Bob（来自 Windows CI）")).id
  $id2 = (LsmJson $pipeB @("send", $FA, "hi Alice")).id
  LsmCli $pipeA @("wait-delivered", $id1, "--timeout", "15") | Out-Null
  LsmCli $pipeB @("wait-delivered", $id2, "--timeout", "15") | Out-Null
  if (-not ((LsmCli $pipeB @("history", $FA)) -match "你好 Bob")) { throw "B history missing message" }
  if (-not ((LsmCli $pipeA @("history", $FB)) -match "hi Alice")) { throw "A history missing message" }
  Ok "text both directions + receipts"

  $blob = (Join-Path $T "blob.bin")
  $bytes = New-Object byte[] 2000000; (New-Object Random 42).NextBytes($bytes); [IO.File]::WriteAllBytes($blob, $bytes)
  LsmCli $pipeA @("send-file", $FB, $blob) | Out-Null
  $dst = (Join-Path $T "b/dl/blob.bin")
  for ($i = 0; $i -lt 150 -and -not (Test-Path $dst); $i++) { Start-Sleep -Milliseconds 200 }
  Start-Sleep -Milliseconds 500
  if ((Get-FileHash $blob).Hash -ne (Get-FileHash $dst).Hash) { throw "file hash mismatch" }
  Ok "file transfer verified"

  LsmCli $pipeA @("raw", "debug_drop_connection", "{`"fingerprint`":`"$FB`"}") | Out-Null
  $id3 = (LsmJson $pipeA @("send", $FB, "after cut")).id
  LsmCli $pipeA @("wait-peer", $FB, "--timeout", "30") | Out-Null
  LsmCli $pipeA @("wait-delivered", $id3, "--timeout", "30") | Out-Null
  $n = ([regex]::Matches((LsmCli $pipeB @("history", $FA)), "after cut")).Count
  if ($n -ne 1) { throw "expected exactly one 'after cut', got $n" }
  Ok "reconnect after connection drop, queued message delivered exactly once"

  Start-Daemon "c" $pipeC @("--set", "discovery_port=$PC", "--set", "discovery_enabled=false") | Out-Null
  WaitPipe $pipeC
  $FC = (LsmJson $pipeC @("status")).me.fingerprint
  $portA = (LsmJson $pipeA @("status")).me.listen_port
  LsmCli $pipeC @("add", "127.0.0.1:$portA") | Out-Null
  LsmCli $pipeC @("wait-peer", $FA, "--timeout", "30") | Out-Null
  LsmCli $pipeA @("wait-peer", $FC, "--timeout", "30") | Out-Null
  $id4 = (LsmJson $pipeA @("send", $FC, "manual add works")).id
  LsmCli $pipeA @("wait-delivered", $id4, "--timeout", "15") | Out-Null
  Ok "manual add-by-IP (discovery off)"

  LsmCli $pipeA @("diag") | Out-File (Join-Path $T "diag-a.json") -Encoding utf8
  foreach ($p in @($pipeA, $pipeB, $pipeC)) { LsmCli $p @("shutdown") | Out-Null }
  Start-Sleep -Seconds 2
  if (-not (Get-ChildItem (Join-Path $T "a/logs") -Filter "daemon*.log")) { throw "no daemon log written" }
  Ok "shutdown + logs"
  Write-Host "WINDOWS SMOKE PASS" -ForegroundColor Green
}
catch {
  Write-Host "WINDOWS SMOKE FAIL: $_" -ForegroundColor Red
  foreach ($n in "a", "b", "c") {
    Get-ChildItem (Join-Path $T "$n/logs") -Filter "daemon*.log" -ErrorAction SilentlyContinue | ForEach-Object { Write-Host "---- $n $($_.Name)"; Get-Content $_.FullName -Tail 60 }
    if (Test-Path (Join-Path $T "$n.err")) { Write-Host "---- $n stderr"; Get-Content (Join-Path $T "$n.err") -Tail 30 }
  }
  exit 1
}
finally {
  foreach ($p in $procs) { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } }
}
