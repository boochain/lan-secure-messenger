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
  $argv = @("run", "--data-dir", "$T\$name", "--ipc", $pipe, "--set", "listen_port=0", "--set", "multicast_enabled=false",
            "--set", "auto_trust=true", "--set", "announce_interval_secs=1", "--set", "display_name=$name",
            "--set", "download_dir=$T\$name\dl") + $extra
  # Start-Process joins arguments verbatim, so quote each one MSVC-style ourselves.
  $line = ($argv | ForEach-Object { '"' + ($_ -replace '"', '\"') + '"' }) -join " "
  $p = Start-Process -FilePath $D -ArgumentList $line -PassThru -NoNewWindow `
       -RedirectStandardOutput "$T\$name.out" -RedirectStandardError "$T\$name.err"
  $script:procs += $p
  return $p
}
function Cli($pipe, [string[]]$a) {
  $out = & $M --ipc $pipe @a
  if ($LASTEXITCODE -ne 0) { throw "lan-msg $($a -join ' ') failed ($LASTEXITCODE): $out" }
  return ($out -join "`n")
}
function Json($pipe, [string[]]$a) { return (Cli $pipe (@("--json") + $a)) | ConvertFrom-Json }
function WaitPipe($pipe) {
  for ($i = 0; $i -lt 100; $i++) { try { Json $pipe @("status") | Out-Null; return } catch { Start-Sleep -Milliseconds 200 } }
  throw "daemon on $pipe did not come up"
}
function Ok($msg) { Write-Host "[ok] $msg" -ForegroundColor Green }

try {
  $env:LSM_LOG = "info"
  # Windows cmd-line quoting of JSON arrays: pwsh 7.3+ passes embedded quotes correctly.
  Start-Daemon "a" $pipeA @("--set", "discovery_port=$PA") | Out-Null
  Start-Daemon "b" $pipeB @("--set", "discovery_port=$PB", "--set", "discovery_listen=false",
                            "--set", "announce_targets=[`"127.0.0.1:$PA`"]") | Out-Null
  WaitPipe $pipeA; WaitPipe $pipeB
  $FA = (Json $pipeA @("status")).me.fingerprint
  $FB = (Json $pipeB @("status")).me.fingerprint
  Write-Host "A=$FA B=$FB"
  Cli $pipeA @("wait-peer", $FB, "--timeout", "30") | Out-Null
  Cli $pipeB @("wait-peer", $FA, "--timeout", "30") | Out-Null
  Ok "one-way discovery -> both online"

  $id1 = (Json $pipeA @("send", $FB, "你好 Bob（来自 Windows CI）")).id
  $id2 = (Json $pipeB @("send", $FA, "hi Alice")).id
  Cli $pipeA @("wait-delivered", $id1, "--timeout", "15") | Out-Null
  Cli $pipeB @("wait-delivered", $id2, "--timeout", "15") | Out-Null
  if (-not ((Cli $pipeB @("history", $FA)) -match "你好 Bob")) { throw "B history missing message" }
  if (-not ((Cli $pipeA @("history", $FB)) -match "hi Alice")) { throw "A history missing message" }
  Ok "text both directions + receipts"

  $blob = "$T\blob.bin"
  $bytes = New-Object byte[] 2000000; (New-Object Random 42).NextBytes($bytes); [IO.File]::WriteAllBytes($blob, $bytes)
  Cli $pipeA @("send-file", $FB, $blob) | Out-Null
  $dst = "$T\b\dl\blob.bin"
  for ($i = 0; $i -lt 150 -and -not (Test-Path $dst); $i++) { Start-Sleep -Milliseconds 200 }
  Start-Sleep -Milliseconds 500
  if ((Get-FileHash $blob).Hash -ne (Get-FileHash $dst).Hash) { throw "file hash mismatch" }
  Ok "file transfer verified"

  Cli $pipeA @("raw", "debug_drop_connection", "{`"fingerprint`":`"$FB`"}") | Out-Null
  $id3 = (Json $pipeA @("send", $FB, "after cut")).id
  Cli $pipeA @("wait-peer", $FB, "--timeout", "30") | Out-Null
  Cli $pipeA @("wait-delivered", $id3, "--timeout", "30") | Out-Null
  $n = ([regex]::Matches((Cli $pipeB @("history", $FA)), "after cut")).Count
  if ($n -ne 1) { throw "expected exactly one 'after cut', got $n" }
  Ok "reconnect after connection drop, queued message delivered exactly once"

  Start-Daemon "c" $pipeC @("--set", "discovery_port=$PC", "--set", "discovery_enabled=false") | Out-Null
  WaitPipe $pipeC
  $FC = (Json $pipeC @("status")).me.fingerprint
  $portA = (Json $pipeA @("status")).me.listen_port
  Cli $pipeC @("add", "127.0.0.1:$portA") | Out-Null
  Cli $pipeC @("wait-peer", $FA, "--timeout", "30") | Out-Null
  Cli $pipeA @("wait-peer", $FC, "--timeout", "30") | Out-Null
  $id4 = (Json $pipeA @("send", $FC, "manual add works")).id
  Cli $pipeA @("wait-delivered", $id4, "--timeout", "15") | Out-Null
  Ok "manual add-by-IP (discovery off)"

  Cli $pipeA @("diag") | Out-File "$T\diag-a.json" -Encoding utf8
  foreach ($p in @($pipeA, $pipeB, $pipeC)) { Cli $p @("shutdown") | Out-Null }
  Start-Sleep -Seconds 2
  if (-not (Get-ChildItem "$T\a\logs" -Filter "daemon*.log")) { throw "no daemon log written" }
  Ok "shutdown + logs"
  Write-Host "WINDOWS SMOKE PASS" -ForegroundColor Green
}
catch {
  Write-Host "WINDOWS SMOKE FAIL: $_" -ForegroundColor Red
  foreach ($n in "a", "b", "c") {
    Get-ChildItem "$T\$n\logs" -Filter "daemon*.log" -ErrorAction SilentlyContinue | ForEach-Object { Write-Host "---- $n $($_.Name)"; Get-Content $_.FullName -Tail 60 }
    if (Test-Path "$T\$n.err") { Write-Host "---- $n stderr"; Get-Content "$T\$n.err" -Tail 30 }
  }
  exit 1
}
finally {
  foreach ($p in $procs) { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } }
}
