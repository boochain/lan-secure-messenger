# deploy.ps1 端到端测试 (Windows / Windows Server, run elevated in CI)
param([string]$NewBin, [string]$Cli)
$ErrorActionPreference = 'Continue'
$NewBin = (Resolve-Path $NewBin).Path; $Cli = (Resolve-Path $Cli).Path
$D = Join-Path $PSScriptRoot '..\deploy\deploy.ps1'
$T = Join-Path $env:RUNNER_TEMP ("rt" + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force $T | Out-Null
$env:RELAY_HOME = Join-Path $T 'home'; $env:RELAY_BIN = $NewBin
$script:pass = 0; $script:fail = 0
function Check($name, [scriptblock]$cond) { $r = $false; try { $r = [bool](& $cond) } catch {}; if ($r) { "PASS: $name"; $script:pass++ } else { "FAIL: $name"; $script:fail++ } }
function Deploy {
  # Run deploy.ps1 in a child Windows PowerShell with a hard timeout; output goes to files so that
  # background processes started by the script cannot keep a pipe open.
  param([Parameter(ValueFromRemainingArguments)]$a)
  $script:n++; $out = Join-Path $T "d$($script:n).out"; $err = Join-Path $T "d$($script:n).err"
  $argl = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$D`"") + $a
  $p = Start-Process powershell.exe -ArgumentList $argl -NoNewWindow -PassThru -RedirectStandardOutput $out -RedirectStandardError $err
  if (-not $p.WaitForExit(180000)) { Write-Host "TIMEOUT: deploy $($a -join ' ')"; Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
  $txt = ((Get-Content $out -Raw -Encoding UTF8 -ErrorAction SilentlyContinue) + (Get-Content $err -Raw -Encoding UTF8 -ErrorAction SilentlyContinue))
  Write-Host "----- deploy $($a -join ' ') -----"; Write-Host $txt
  return $txt
}
$script:n = 0
function RoundTrip($port, $pw) {
  $e = Start-Process -FilePath $Cli -ArgumentList "--addr 127.0.0.1:$port --password $pw --name echo$port --echo --timeout 8" -PassThru -WindowStyle Hidden
  Start-Sleep -Milliseconds 800
  $o = & $Cli --addr "127.0.0.1:$port" --password $pw --name "send$port" --to "echo$port" --count 20 --size 2048 --timeout 8 2>&1 | Out-String
  Stop-Process -Id $e.Id -Force -ErrorAction SilentlyContinue
  return ($o -match 'OK roundtrip')
}
"== deploy.ps1 test on $((Get-CimInstance Win32_OperatingSystem).Caption) home=$env:RELAY_HOME"
# 1. install (Windows service) with the firewall reported inactive -> no firewall command
$env:RELAY_FAKE_FIREWALL = 'inactive'; $env:RELAY_FWLOG = Join-Path $T 'fw.log'
$o = Deploy install -Port 19801 -Password 'Test-Pass-1'; $o | Set-Content -Encoding UTF8 (Join-Path $T 'install.log')
Check 'install: service created' { (Get-Service LsmRelay).Status -eq 'Running' }
Check 'install: service is a real SCM service (--service)' { (Get-CimInstance Win32_Service -Filter "Name='LsmRelay'").PathName -match '--service' }
Check 'inactive firewall message' { $o -match '本机防火墙未启用，不做修改' }
Check 'inactive firewall: no firewall command executed' { -not (Test-Path $env:RELAY_FWLOG) }
Check 'cloud reminder' { $o -match '安全组' }
Check 'round-trip via service' { RoundTrip 19801 'Test-Pass-1' }
$s = Deploy status
Check 'status shows running + online' { $s -match '运行中' -and $s -match '在线人数' }
# 2. idempotent
$o = Deploy install -Port 19801 -Password 'Test-Pass-1'
Check 're-install idempotent' { $o -match '版本相同' -and (Get-Service LsmRelay).Status -eq 'Running' }
# 3. port change
$o = Deploy config -Port 19811; Start-Sleep 2
Check 'port changed' { (Get-Content "$env:RELAY_HOME\data\relay.toml") -match 'port = 19811' }
Check 'round-trip on new port' { RoundTrip 19811 'Test-Pass-1' }
# 4. upgrade over fake older version with leftovers (old binary, old service, old task, stale pid, running process)
$leg = Join-Path $T 'legacy'; New-Item -ItemType Directory -Force $leg | Out-Null
Copy-Item $NewBin (Join-Path $leg 'relay-server.exe')
$env:RELAY_LEGACY_BINS = (Join-Path $leg 'relay-server.exe'); $env:RELAY_LEGACY_PIDS = (Join-Path $leg 'stale.pid')
Set-Content (Join-Path $leg 'stale.pid') '999999'
$null = Deploy stop
# installed binary replaced by an 'old build' whose version cannot be read (stands in for a pre-0.1 build)
Copy-Item -Force $Cli "$env:RELAY_HOME\bin\relay-server.exe"
& sc.exe create RelayServer binPath= "`"$leg\relay-server.exe`" --service --port 19899 --password x" start= demand | Out-Null
& schtasks /Create /TN RelayServer /TR "`"$leg\relay-server.exe`" --port 19898 --password x" /SC ONLOGON /F | Out-Null
$old = Start-Process -FilePath (Join-Path $leg 'relay-server.exe') -ArgumentList '--port 19897 --password oldpass --bind 127.0.0.1' -PassThru -WindowStyle Hidden
Start-Sleep 1
$o = Deploy upgrade; $o | Set-Content -Encoding UTF8 (Join-Path $T 'upgrade.log')
Check 'upgrade detected leftovers' { $o -match '检测到已有安装或残留' }
$newV = ((Get-Content (Join-Path $PSScriptRoot '..\Cargo.toml')) | Select-String '^version = "(.*)"' | Select-Object -First 1).Matches[0].Groups[1].Value
Check 'upgrade report (old → new)' { $o.Contains("unknown → $newV") }
Check 'old service removed' { -not (Get-Service RelayServer -ErrorAction SilentlyContinue) }
Check 'old task removed' { & cmd.exe /c 'schtasks /Query /TN RelayServer >nul 2>&1'; $LASTEXITCODE -ne 0 }
Check 'old process stopped' { -not (Get-Process -Id $old.Id -ErrorAction SilentlyContinue) }
Check 'legacy binary removed' { -not (Test-Path (Join-Path $leg 'relay-server.exe')) }
Check 'stale pid removed' { -not (Test-Path (Join-Path $leg 'stale.pid')) }
Check 'config backed up' { (Get-ChildItem "$env:RELAY_HOME\data" -Filter 'relay.toml.bak*').Count -ge 1 }
Check 'kept port + passphrase' { $c = Get-Content "$env:RELAY_HOME\data\relay.toml" -Raw; $c -match 'port = 19811' -and $c -match 'Test-Pass-1' }
Start-Sleep 2
Check 'round-trip after upgrade' { RoundTrip 19811 'Test-Pass-1' }
# 5. remove autostart, background run, stop
$o = Deploy remove-autostart
Check 'service removed' { -not (Get-Service LsmRelay -ErrorAction SilentlyContinue) }
$o = Deploy start; Start-Sleep 1
Check 'background start' { (Deploy status) -match '运行中' }
Check 'round-trip background' { RoundTrip 19811 'Test-Pass-1' }
$o = Deploy stop
Check 'stopped' { (Deploy status) -match '未运行' }
# 6. scheduled-task fallback
$o = Deploy install -NoService; Start-Sleep 2
Check 'scheduled task installed' { & cmd.exe /c 'schtasks /Query /TN LsmRelay >nul 2>&1'; $LASTEXITCODE -eq 0 }
$o = Deploy remove-autostart
Check 'scheduled task removed' { & cmd.exe /c 'schtasks /Query /TN LsmRelay >nul 2>&1'; $LASTEXITCODE -ne 0 }
# 7. active firewall: unchanged without flag; real rule with -OpenFirewall; uninstall removes only ours
Remove-Item Env:RELAY_FWLOG; $env:RELAY_FAKE_FIREWALL = 'active'
$o = Deploy config -Port 19821
Check 'active fw, no flag -> unchanged' { $o -match '未传 -OpenFirewall' -and -not (Get-NetFirewallRule -Name 'LSM Relay TCP 19821' -ErrorAction SilentlyContinue) }
$o = Deploy install -Port 19822 -OpenFirewall
Check '-OpenFirewall creates rule' { [bool](Get-NetFirewallRule -Name 'LSM Relay TCP 19822' -ErrorAction SilentlyContinue) }
New-NetFirewallRule -Name 'User Rule 19822' -DisplayName 'User Rule 19822' -Direction Inbound -Protocol TCP -LocalPort 19822 -Action Allow | Out-Null
# 8. uninstall
$o = Deploy uninstall -Yes
Check 'uninstall removed our rule' { -not (Get-NetFirewallRule -Name 'LSM Relay TCP 19822' -ErrorAction SilentlyContinue) }
Check 'uninstall kept user rule' { [bool](Get-NetFirewallRule -Name 'User Rule 19822' -ErrorAction SilentlyContinue) }
Remove-NetFirewallRule -Name 'User Rule 19822' -ErrorAction SilentlyContinue
Check 'uninstall removed service + files' { -not (Get-Service LsmRelay -ErrorAction SilentlyContinue) -and -not (Test-Path "$env:RELAY_HOME\bin") }
Check 'nothing running' { -not (Get-Process relay-server -ErrorAction SilentlyContinue) }
# 9. dry-run
$o = Deploy install -Port 19831 -Password 'x12345678' -DryRun
Check 'dry-run creates nothing' { $o -match '\[dry-run\]' -and -not (Test-Path "$env:RELAY_HOME\bin\relay-server.exe") }
"== deploy.ps1 test: $script:pass passed, $script:fail failed"
if ($script:fail) { Get-ChildItem $T -Filter *.log | ForEach-Object { "--- $_"; Get-Content $_.FullName }; exit 1 }
exit 0
