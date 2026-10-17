# LSM 中继服务器 部署脚本 (relay-server deploy script) — Windows 10/11, Windows Server 2016/2019/2022/2025 (含 Server Core)
# 交互菜单: deploy.bat     非交互: deploy.ps1 install -Port 9443 -Password xxx [-OpenFirewall]
# 命令 (commands): menu run start install upgrade remove-autostart stop config status uninstall version
param(
  [Parameter(Position = 0)][string]$Action = 'menu',
  [int]$Port = 0,
  [string]$Password = '',
  [string]$Bind = '',
  [switch]$GenPassword,
  [switch]$OpenFirewall,
  [switch]$Yes,
  [switch]$DryRun,
  [switch]$NoStart,
  [switch]$NoService   # 用计划任务代替服务 (use a scheduled task instead of a service)
)
# Continue: on Windows PowerShell 5.1 native-command stderr would otherwise become a terminating error
$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch {}
$ScriptVersion = '0.1.0'
$SvcName = 'LsmRelay'
$TaskName = 'LsmRelay'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$IsAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if ($env:RELAY_HOME) { $InstallDir = Join-Path $env:RELAY_HOME 'bin'; $DataDir = Join-Path $env:RELAY_HOME 'data' }
elseif ($IsAdmin) { $InstallDir = Join-Path $env:ProgramFiles 'LsmRelay'; $DataDir = Join-Path $env:ProgramData 'LsmRelay' }
else { $InstallDir = Join-Path $env:LOCALAPPDATA 'LsmRelay\bin'; $DataDir = Join-Path $env:LOCALAPPDATA 'LsmRelay\data' }
$Bin = Join-Path $InstallDir 'relay-server.exe'
$Conf = Join-Path $DataDir 'relay.toml'
$PidF = Join-Path $DataDir 'relay.pid'
$FwState = Join-Path $DataDir 'firewall-rules.txt'
$LogDir = Join-Path $DataDir 'logs'
$LegacyBins = @('C:\relay-server\relay-server.exe', (Join-Path $env:USERPROFILE 'relay-server.exe'), (Join-Path $env:ProgramFiles 'RelayServer\relay-server.exe'))
if ($env:RELAY_LEGACY_BINS) { $LegacyBins = $env:RELAY_LEGACY_BINS -split ';' }
$LegacyServices = @('RelayServer', 'LsmRelayServer', 'relay-server')
$LegacyTasks = @('RelayServer', 'LsmRelayServer')
$LegacyPids = @((Join-Path $env:TEMP 'lsm-relay.pid'), 'C:\relay-server\relay.pid')
if ($env:RELAY_LEGACY_PIDS) { $LegacyPids = $env:RELAY_LEGACY_PIDS -split ';' }
$Interactive = $false
$script:Salt = ''

function Say($m) { Write-Host $m }
function Ok($m) { Write-Host "[完成] $m" -ForegroundColor Green }
function Warn($m) { Write-Host "[注意] $m" -ForegroundColor Yellow }
function Err($m) { Write-Host "[错误] $m" -ForegroundColor Red }
function Die($m) { Err $m; exit 1 }
# 所有改变系统的操作都经过 Do-It (every system-changing action goes through Do-It)
function Do-It([string]$desc, [scriptblock]$sb) { if ($DryRun) { Say "[dry-run] $desc" } else { & $sb } }

# ---------------- 架构 (arch) ----------------
function Get-Arch {
  $a = $env:PROCESSOR_ARCHITEW6432; if (-not $a) { $a = $env:PROCESSOR_ARCHITECTURE }
  switch ($a) { 'AMD64' { 'x64' } 'ARM64' { 'arm64' } 'x86' { 'x86' } default { $a } }
}
function Find-NewBin {
  $a = Get-Arch
  foreach ($c in @($env:RELAY_BIN, (Join-Path $ScriptDir "bin\win-$a\relay-server.exe"), (Join-Path $ScriptDir 'relay-server.exe'),
      (Join-Path $ScriptDir '..\target\x86_64-pc-windows-msvc\release\relay-server.exe'), (Join-Path $ScriptDir '..\target\release\relay-server.exe'))) {
    if ($c -and (Test-Path $c)) { return (Resolve-Path $c).Path }
  }
  return $null
}
function Get-BinVersion($p) {
  if (-not $p -or -not (Test-Path $p)) { return 'none' }
  try { $o = & $p --version 2>$null | Select-Object -First 1; if ($o -match 'relay-server\s+(\S+)') { return $Matches[1] } } catch {}
  return 'unknown'
}

# ---------------- 配置 (config) ----------------
function Toml-Get($file, $key) {
  if (-not (Test-Path $file)) { return '' }
  $sec = ''
  foreach ($l in Get-Content -LiteralPath $file -Encoding UTF8) {
    if ($l -match '^\s*\[(.+)\]') { $sec = $Matches[1].Trim(); continue }
    if ($sec -eq 'server' -and $l -match "^\s*$key\s*=\s*(.*)$") {
      $v = $Matches[1].Trim()
      if ($v.StartsWith('"')) { $v = $v.Substring(1, $v.LastIndexOf('"') - 1).Replace('\"', '"').Replace('\\', '\') } else { $v = ($v -split '\s+#')[0].Trim() }
      return $v
    }
  }
  return ''
}
function Toml-Str($s) { '"' + $s.Replace('\', '\\').Replace('"', '\"') + '"' }
function New-Pass { $b = New-Object byte[] 18; [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($b); ([Convert]::ToBase64String($b) -replace '[+/=]', '').Substring(0, 20) }
function New-Salt { $b = New-Object byte[] 16; [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($b); ($b | ForEach-Object { $_.ToString('x2') }) -join '' }
function Load-Existing {
  foreach ($f in @($Conf, 'C:\relay-server\relay.toml')) {
    if (-not (Test-Path $f)) { continue }
    if (-not $script:Port) { $p = Toml-Get $f 'port'; if ($p) { $script:Port = [int]$p } }
    if (-not $script:Password) { $script:Password = Toml-Get $f 'passphrase' }
    if (-not $script:Bind) { $script:Bind = Toml-Get $f 'bind' }
    if (-not $script:Salt) { $script:Salt = Toml-Get $f 'salt' }
    if ($script:Port -and $script:Password) { break }
  }
}
function Write-Config {
  if (-not $script:Salt) { $script:Salt = New-Salt }
  if ($DryRun) { Say "[dry-run] write $Conf (port=$script:Port bind=$script:Bind)"; return }
  New-Item -ItemType Directory -Force -Path $DataDir, $LogDir | Out-Null
  $logf = Join-Path $LogDir 'relay.log'
  $txt = @"
# LSM 中继服务器配置 (relay-server config) — 由 deploy.ps1 生成
[server]
bind = $(Toml-Str $script:Bind)
port = $($script:Port)
# 网络口令：客户端设置中需填写相同口令 (clients must use the same passphrase)
passphrase = $(Toml-Str $script:Password)
salt = "$($script:Salt)"
status_addr = ""

[limits]
max_frame = 1048640
max_connections = 1024
max_connections_per_ip = 64

[log]
level = "info"
file = $(Toml-Str $logf)
"@
  $tmp = "$Conf.tmp"
  [IO.File]::WriteAllText($tmp, $txt, (New-Object Text.UTF8Encoding($false)))
  Move-Item -Force $tmp $Conf
  # 仅管理员/SYSTEM 与当前用户可读 (restrict ACL)
  try { & icacls $Conf /inheritance:r /grant:r "*S-1-5-18:F" "*S-1-5-32-544:F" "$($env:USERNAME):F" 2>&1 | Out-Null } catch {}
  Ok "配置已写入 (config written): $Conf"
}

# ---------------- 端口/口令 (port & passphrase) ----------------
function Test-PortInUse([int]$p) {
  try { $l = New-Object Net.Sockets.TcpListener([Net.IPAddress]::Any, $p); $l.Start(); $l.Stop(); return $false } catch { return $true }
}
function Test-OurPort([int]$p) { ([int](Toml-Get $Conf 'port') -eq $p) -and (Test-Running) }
function Ask-Port {
  $def = if ($script:Port) { $script:Port } else { 9443 }
  while ($true) {
    $in = Read-Host "端口 (port) [1-65535，默认 $def]"; if (-not $in) { $in = "$def" }
    $n = 0; if (-not [int]::TryParse($in, [ref]$n) -or $n -lt 1 -or $n -gt 65535) { Err '端口必须是 1-65535 的数字 (invalid port)'; continue }
    if ((Test-PortInUse $n) -and -not (Test-OurPort $n)) { Err "端口 $n 已被占用 (port in use)，请换一个"; continue }
    $script:Port = $n; return
  }
}
function Ask-Pass {
  if ($script:Password) { $a = Read-Host '保留现有口令？(keep current passphrase) [Y/n]'; if ($a -notmatch '^[nN]') { return } }
  while ($true) {
    $a = Read-Host '口令：1) 手动输入 (type)  2) 随机生成 (generate) [2]'
    if (-not $a -or $a -eq '2') { $script:Password = New-Pass; Say "已生成口令 (generated): $($script:Password)"; Say '请记下并填入各客户端 (write it down for the clients)'; return }
    $s1 = Read-Host '输入口令 (passphrase, 至少 8 位)' -AsSecureString
    $s2 = Read-Host '再次输入确认 (confirm)' -AsSecureString
    $p1 = [Runtime.InteropServices.Marshal]::PtrToStringBSTR([Runtime.InteropServices.Marshal]::SecureStringToBSTR($s1))
    $p2 = [Runtime.InteropServices.Marshal]::PtrToStringBSTR([Runtime.InteropServices.Marshal]::SecureStringToBSTR($s2))
    if ($p1.Length -lt 8) { Err '口令太短 (min 8 chars)'; continue }
    if ($p1 -ne $p2) { Err '两次输入不一致 (mismatch)'; continue }
    $script:Password = $p1; return
  }
}
function Ask-Bind {
  $def = if ($script:Bind) { $script:Bind } else { '0.0.0.0' }
  $b = Read-Host "监听地址 (bind) [0.0.0.0=全部 IPv4, ::=IPv4+IPv6，默认 $def]"; $script:Bind = if ($b) { $b } else { $def }
}

# ---------------- 防火墙 (firewall; never automatic) ----------------
function Test-FirewallActive {
  if ($env:RELAY_FAKE_FIREWALL) { return ($env:RELAY_FAKE_FIREWALL -eq 'active') }
  try { return [bool](Get-NetFirewallProfile -ErrorAction Stop | Where-Object { $_.Enabled -eq $true -or $_.Enabled -eq 'True' }) }
  catch { $o = (& netsh advfirewall show allprofiles state) -join ' '; return ($o -match 'ON') }
}
function Fw-RuleName([int]$p) { "LSM Relay TCP $p" }
function Invoke-Fw([string]$cmd) {
  if ($env:RELAY_FWLOG) { Add-Content -Path $env:RELAY_FWLOG -Value $cmd; return }   # 测试钩子 (test hook)
  Invoke-Expression $cmd
}
function Cloud-Note { Warn "云服务器还需在服务商控制台的安全组/防火墙中放行 TCP $($script:Port) (cloud: also open the port in the provider's security group)" }
function Firewall-Step {
  if (-not (Test-FirewallActive)) { Say '本机防火墙未启用，不做修改 (host firewall inactive, not touching it)'; Cloud-Note; return }
  $name = Fw-RuleName $script:Port
  if ((Test-Path $FwState) -and (Get-Content $FwState) -contains $name) { Ok "防火墙规则已存在 (rule exists): $name"; Cloud-Note; return }
  $cmd = if (Get-Command New-NetFirewallRule -ErrorAction SilentlyContinue) {
    "New-NetFirewallRule -DisplayName '$name' -Name '$name' -Direction Inbound -Protocol TCP -LocalPort $($script:Port) -Action Allow | Out-Null"
  } else { "netsh advfirewall firewall add rule name=`"$name`" dir=in action=allow protocol=TCP localport=$($script:Port) | Out-Null" }
  $doit = $false
  if ($Interactive) {
    Say '检测到 Windows 防火墙已启用 (firewall active)'
    Say "将执行的命令 (command to run): $cmd"
    $a = Read-Host '是否添加放行规则？(add allow rule) [y/N]'; $doit = ($a -match '^(y|Y|yes|YES)$')
  } elseif ($OpenFirewall) { $doit = $true; Say "执行 (running): $cmd" }
  else { Say "防火墙已启用，未传 -OpenFirewall，不做修改。手动放行: $cmd" }
  if ($doit) {
    if (-not $IsAdmin -and -not $env:RELAY_FWLOG) { Err '添加防火墙规则需要管理员权限 (admin required)'; Cloud-Note; return }
    Do-It $cmd { Invoke-Fw $cmd; New-Item -ItemType Directory -Force -Path $DataDir | Out-Null; Add-Content -Path $FwState -Value $name }
    Ok '已添加防火墙规则 (rule added)'
  } else { Say '未修改防火墙 (firewall unchanged)' }
  Cloud-Note
}
function Firewall-RemoveOurs([int]$onlyPort = 0) {
  if (-not (Test-Path $FwState)) { return }
  $keep = @()
  foreach ($name in Get-Content $FwState) {
    if (-not $name) { continue }
    if ($onlyPort -and $name -ne (Fw-RuleName $onlyPort)) { $keep += $name; continue }
    $cmd = if (Get-Command Remove-NetFirewallRule -ErrorAction SilentlyContinue) { "Remove-NetFirewallRule -Name '$name' -ErrorAction SilentlyContinue" } else { "netsh advfirewall firewall delete rule name=`"$name`" | Out-Null" }
    Say "删除本脚本创建的防火墙规则 (removing our rule): $cmd"
    Do-It $cmd { try { Invoke-Fw $cmd } catch { Warn "删除失败 (failed): $_" } }
  }
  if (-not $DryRun) { if ($keep.Count) { Set-Content -Path $FwState -Value $keep } else { Remove-Item -Force $FwState } }
}

# ---------------- 进程/服务 (processes & service) ----------------
function Get-OurProcs {
  @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -in @('relay-server', 'lsm-relay', 'lsm-relay-server') })
}
function Get-Svc($n) { Get-Service -Name $n -ErrorAction SilentlyContinue }
function Test-TaskExists($n) { & cmd.exe /c "schtasks /Query /TN `"$n`" >nul 2>&1"; return ($LASTEXITCODE -eq 0) }
function Test-Autostart { [bool](Get-Svc $SvcName) -or (Test-TaskExists $TaskName) }
function Test-Running { $s = Get-Svc $SvcName; if ($s -and $s.Status -eq 'Running') { return $true }; return ((Get-OurProcs).Count -gt 0) }
function Read-Pid($f) { if (Test-Path $f) { $t = (Get-Content $f -Raw) -replace '\D', ''; if ($t) { return [int]$t } }; return 0 }
function Stop-All {
  $stopped = $false
  foreach ($n in @($SvcName) + $LegacyServices) {
    $s = Get-Svc $n
    if ($s -and $s.Status -ne 'Stopped') { Do-It "Stop-Service $n" { Stop-Service -Name $n -Force -ErrorAction SilentlyContinue; try { $s.WaitForStatus('Stopped', '00:00:15') } catch {} }; $stopped = $true }
  }
  foreach ($t in @($TaskName) + $LegacyTasks) { if (Test-TaskExists $t) { Do-It "schtasks /End /TN $t" { & schtasks /End /TN $t 2>&1 | Out-Null } } }
  foreach ($f in @($PidF) + $LegacyPids) {
    $p = Read-Pid $f
    if ($p -and (Get-Process -Id $p -ErrorAction SilentlyContinue)) { Do-It "Stop-Process $p" { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue }; $stopped = $true }
    if (Test-Path $f) { Do-It "Remove $f" { Remove-Item -Force $f -ErrorAction SilentlyContinue } }
  }
  foreach ($p in Get-OurProcs) { Do-It "Stop-Process $($p.Id) ($($p.Path))" { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }; $stopped = $true }
  if (-not $DryRun) { for ($i = 0; $i -lt 30 -and (Get-OurProcs).Count; $i++) { Start-Sleep -Milliseconds 200 } }
  if ($stopped) { Ok '服务器已停止 (stopped)' } else { Say '没有正在运行的服务器 (nothing running)' }
}
function Require-Installed { if (-not (Test-Path $Bin) -or -not (Test-Path $Conf)) { Die '尚未安装，请先选择 安装/持久运行 (not installed; run install first)' } }
function Ensure-Installed {
  $nb = Find-NewBin; if (-not $nb) { Die "找不到 relay-server.exe (binary not found for $(Get-Arch))；设置 RELAY_BIN=路径" }
  $newv = Get-BinVersion $nb; $oldv = Get-BinVersion $Bin
  $found = @()
  foreach ($lb in $LegacyBins) { if ($lb -and (Test-Path $lb)) { $found += $lb; if ($oldv -eq 'none') { $oldv = Get-BinVersion $lb } } }
  foreach ($n in $LegacyServices) { if (Get-Svc $n) { $found += "service:$n" } }
  foreach ($t in $LegacyTasks) { if (Test-TaskExists $t) { $found += "task:$t" } }
  foreach ($f in @($PidF) + $LegacyPids) { if ($f -and (Test-Path $f)) { $found += "$f(pid)" } }
  if (Get-Svc $SvcName) { $found += "service:$SvcName" }
  if (Test-TaskExists $TaskName) { $found += "task:$TaskName" }
  $procs = Get-OurProcs
  $script:WasAutostart = Test-Autostart
  $script:WasService = [bool](Get-Svc $SvcName)
  if ($oldv -ne 'none' -or $found.Count -or $procs.Count) {
    Say "检测到已有安装或残留 (existing install / leftovers): $($found -join ', ') $(if ($procs.Count) { '运行中进程 pids: ' + (($procs | ForEach-Object Id) -join ' ') })"
    if ($oldv -eq $newv) { Say "版本相同 (same version): $newv — 重新应用 (re-applying)" } else { Say "升级 (upgrade): $oldv → $newv" }
    Stop-All
    if (Test-Path $Conf) { $bk = "$Conf.bak.$(Get-Date -Format yyyyMMddHHmmss)"; Do-It "backup $bk" { Copy-Item $Conf $bk }; Ok "配置已备份 (config backed up): $bk" }
    Load-Existing
    foreach ($n in $LegacyServices) { if (Get-Svc $n) { Do-It "sc.exe delete $n" { & sc.exe delete $n | Out-Null }; Say "已删除旧服务 (removed old service) $n" } }
    foreach ($t in $LegacyTasks) { if (Test-TaskExists $t) { Do-It "schtasks /Delete /TN $t" { & schtasks /Delete /TN $t /F | Out-Null }; Say "已删除旧计划任务 (removed old task) $t" } }
    foreach ($lb in $LegacyBins) { if ($lb -and (Test-Path $lb) -and ((Resolve-Path $lb).Path -ne $nb)) { Do-It "Remove $lb" { Remove-Item -Force $lb }; Say "已删除旧文件 (removed) $lb" } }
  }
  Do-It "copy $nb -> $Bin" {
    New-Item -ItemType Directory -Force -Path $InstallDir, $DataDir, $LogDir | Out-Null
    if ($nb -ne $Bin) { for ($i = 0; $i -lt 20; $i++) { try { Copy-Item -Force $nb $Bin -ErrorAction Stop; break } catch { Start-Sleep -Milliseconds 300 } } }
    Copy-Item -Force $MyInvocation.ScriptName (Join-Path $InstallDir 'deploy.ps1') -ErrorAction SilentlyContinue
    Set-Content -Path (Join-Path $DataDir 'version.txt') -Value $newv
  }
  Ok "二进制已就绪 (binary ready): $Bin ($newv, $(Get-Arch))"
}
function Params-Step {
  Load-Existing
  if ($Interactive) { Ask-Port; Ask-Pass; Ask-Bind; return }
  if (-not $script:Port) { $script:Port = 9443 }; if (-not $script:Bind) { $script:Bind = '0.0.0.0' }
  if ($script:Port -lt 1 -or $script:Port -gt 65535) { Die "无效端口 (invalid port): $($script:Port)" }
  if ((Test-PortInUse $script:Port) -and -not (Test-OurPort $script:Port)) { Die "端口 $($script:Port) 已被占用 (port in use)" }
  if ($GenPassword) { $script:Password = New-Pass; Say "已生成口令 (generated passphrase): $($script:Password)" }
  if (-not $script:Password) { Die '需要 -Password 或 -GenPassword (passphrase required)' }
}
function Start-Bg {
  Require-Installed
  $s = Get-Svc $SvcName
  if ($s) { Do-It "Start-Service $SvcName" { Restart-Service -Name $SvcName -Force; $s.WaitForStatus('Running', '00:00:20') }; Ok '已通过 Windows 服务启动 (started via service)'; return }
  if (Test-Running) { Stop-All | Out-Null }
  if ($DryRun) { Say "[dry-run] Start-Process $Bin --config $Conf (hidden)"; return }
  $p = Start-Process -FilePath $Bin -ArgumentList @('--config', "`"$Conf`"", '--pid-file', "`"$PidF`"") -WindowStyle Hidden -PassThru `
       -RedirectStandardError (Join-Path $LogDir 'console.err.log') -RedirectStandardOutput (Join-Path $LogDir 'console.out.log')
  for ($i = 0; $i -lt 30; $i++) { Start-Sleep -Milliseconds 200; if (Read-Pid $PidF) { break } }
  if (-not $p.HasExited) { Ok "已在后台运行 (running in background) PID $($p.Id)，关闭窗口后继续运行" }
  else { Err "启动失败 (start failed)，日志: $LogDir"; Get-Content (Join-Path $LogDir 'console.err.log') -Tail 5 -ErrorAction SilentlyContinue; exit 1 }
}

# ---------------- 自启动 (autostart) ----------------
function Install-Autostart {
  Require-Installed
  Stop-All | Out-Null
  if ($IsAdmin -and -not $NoService) {
    $bp = "`"$Bin`" --service --config `"$Conf`""
    Do-It "sc.exe create $SvcName binPath= $bp start= auto" {
      if (Get-Svc $SvcName) { & sc.exe config $SvcName binPath= $bp start= auto | Out-Null }
      else { New-Service -Name $SvcName -BinaryPathName $bp -DisplayName 'LSM Relay Server (中继服务器)' -StartupType Automatic -Description 'LAN Secure Messenger relay: forwards end-to-end encrypted traffic only' | Out-Null }
      & sc.exe failure $SvcName reset= 86400 actions= restart/3000/restart/3000/restart/10000 | Out-Null
      Start-Service -Name $SvcName; (Get-Svc $SvcName).WaitForStatus('Running', '00:00:20')
    }
    Ok "已安装 Windows 服务 (service installed): $SvcName (自动启动, 失败自动重启)"
  } else {
    if (-not $IsAdmin) { Warn '非管理员：改用当前用户登录时启动的计划任务 (not admin: using a logon scheduled task)' }
    $tr = "`"$Bin`" --config `"$Conf`" --pid-file `"$PidF`""
    $sched = if ($IsAdmin) { @('/SC', 'ONSTART', '/RU', 'SYSTEM') } else { @('/SC', 'ONLOGON') }
    Do-It "schtasks /Create /TN $TaskName $sched" {
      & schtasks /Create /TN $TaskName /TR $tr @sched /RL HIGHEST /F 2>&1 | Out-Null
      if ($LASTEXITCODE -ne 0) { & schtasks /Create /TN $TaskName /TR $tr @sched /F | Out-Null }
      & schtasks /Run /TN $TaskName | Out-Null
    }
    Ok "已安装计划任务 (scheduled task installed): $TaskName"
  }
}
function Remove-Autostart {
  $any = $false
  if (Get-Svc $SvcName) { Do-It "sc.exe stop/delete $SvcName" { Stop-Service $SvcName -Force -ErrorAction SilentlyContinue; & sc.exe delete $SvcName | Out-Null }; $any = $true }
  if (Test-TaskExists $TaskName) { Do-It "schtasks /Delete /TN $TaskName" { & schtasks /End /TN $TaskName 2>&1 | Out-Null; & schtasks /Delete /TN $TaskName /F | Out-Null }; $any = $true }
  if ($any) { Ok '已删除开机自启 (autostart removed)' } else { Say '未安装开机自启 (no autostart installed)' }
}

# ---------------- 状态 (status) ----------------
function Show-Status {
  Say '==== LSM 中继服务器状态 (status) ===='
  Say "安装目录 (install dir): $InstallDir  配置 (config): $Conf"
  $nb = Find-NewBin
  Say "已安装版本 (installed): $(Get-BinVersion $Bin)   本包版本 (bundled): $(if ($nb) { Get-BinVersion $nb } else { '-' })   CPU: $(Get-Arch)"
  Say "端口 (port): $(Toml-Get $Conf 'port')   监听 (bind): $(Toml-Get $Conf 'bind')"
  $s = Get-Svc $SvcName
  if ($s) { Say "开机自启 (autostart): Windows 服务 $SvcName ($($s.Status))" } elseif (Test-TaskExists $TaskName) { Say "开机自启 (autostart): 计划任务 $TaskName" } else { Say '开机自启 (autostart): 未安装 (not installed)' }
  if (Test-Running) {
    Say "运行状态 (running): 运行中 (running) pids: $((Get-OurProcs | ForEach-Object Id) -join ' ')"
    try {
      $j = (& $Bin --status --config $Conf 2>$null) -join "`n" | ConvertFrom-Json
      Say "在线人数 (online): $($j.online)   连接数 (connections): $($j.connections)"
      Say "运行版本 (running version): $($j.version)   已运行 (uptime): $($j.uptime_secs)s   已转发 (forwarded): $($j.bytes_forwarded) 字节"
    } catch { Warn '状态接口无响应 (status endpoint not reachable)' }
  } else { Say '运行状态 (running): 未运行 (stopped)' }
  if (Test-Path $FwState) { Say "本脚本添加的防火墙规则 (our firewall rules): $((Get-Content $FwState) -join ', ')" }
}

# ---------------- 动作 (actions) ----------------
function Do-Install { Ensure-Installed; Params-Step; Write-Config; Firewall-Step; if ($NoStart) { return }; Install-Autostart; Start-Sleep 1; Show-Status }
function Do-Run { Ensure-Installed; Params-Step; Write-Config; Firewall-Step; Stop-All | Out-Null; Say '前台运行，按 Ctrl+C 停止 (foreground; Ctrl+C to stop)'; if (-not $DryRun) { & $Bin --config $Conf } }
function Do-Bg { Ensure-Installed; Params-Step; Write-Config; Firewall-Step; Start-Bg }
function Do-Config {
  Require-Installed
  $old = [int](Toml-Get $Conf 'port'); $newPortArg = $script:Port
  $script:Port = 0; Load-Existing; if ($newPortArg) { $script:Port = $newPortArg }
  if ($Interactive) { Ask-Port; Ask-Pass; Ask-Bind }
  if ($script:Port -ne $old -and (Test-PortInUse $script:Port)) { Die "端口 $($script:Port) 已被占用 (port in use)" }
  $was = Test-Running
  Write-Config
  if ($script:Port -ne $old) { Say "端口变更 (port changed): $old → $($script:Port)"; Firewall-RemoveOurs $old; Firewall-Step }
  if ($was) { Stop-All | Out-Null; Start-Bg; Ok '已用新参数重启 (restarted with new settings)' }
}
function Do-Uninstall {
  if ($Interactive -and -not $Yes) { $a = Read-Host '确定卸载？配置与日志也将删除 (really uninstall?) [y/N]'; if ($a -notmatch '^[yY]') { Say '已取消'; return } }
  Stop-All; Remove-Autostart; Firewall-RemoveOurs
  Do-It "Remove $InstallDir, $DataDir" { foreach ($d in @($InstallDir, $DataDir)) { if (Test-Path $d) { Remove-Item -Recurse -Force $d } } }
  Ok '已卸载 (uninstalled)'
}
function Show-Menu {
  $script:Interactive = $true; Set-Variable -Name Interactive -Value $true -Scope Script
  while ($true) {
    Say ''
    Say "===== LSM 中继服务器 部署工具 v$ScriptVersion (relay-server deploy, Windows) ====="
    Say '中继仅转发端到端加密数据，看不到明文 (relay only forwards end-to-end encrypted data)'
    if (-not $IsAdmin) { Warn '当前非管理员：安装服务/防火墙规则需"以管理员身份运行" (run as Administrator for service/firewall)' }
    Say '  1. 临时运行 (run in foreground, Ctrl+C 停止)'
    Say '  2. 持久运行 (run in background, 关闭窗口后继续)'
    Say '  3. 安装开机自启服务 (install Windows service / scheduled task)'
    Say '  4. 删除开机自启 (remove autostart)'
    Say '  5. 停止正在运行的服务器 (stop server)'
    Say '  6. 修改端口/口令等参数 (change port/passphrase)'
    Say '  7. 查看状态 (status)'
    Say '  8. 卸载 (uninstall)'
    Say '  0. 退出 (exit)'
    $c = Read-Host '请选择 (choose) [0-8]'
    try {
      switch ($c) {
        '1' { Do-Run } '2' { Do-Bg } '3' { Ensure-Installed; Params-Step; Write-Config; Firewall-Step; Install-Autostart } '4' { Remove-Autostart }
        '5' { Stop-All } '6' { Do-Config } '7' { Show-Status } '8' { Do-Uninstall }
        '0' { Say '再见 (bye)'; return } default { Err '无效选择 (invalid choice)' }
      }
    } catch { Err "$_" }
  }
}

$script:Port = $Port; $script:Password = $Password; $script:Bind = $Bind
switch ($Action.ToLower()) {
  'menu' { Show-Menu }
  'run' { Do-Run }
  { $_ -in 'start', 'background' } { Do-Bg }
  { $_ -in 'install', 'upgrade' } { Do-Install }
  'remove-autostart' { Remove-Autostart }
  'stop' { Stop-All }
  'config' { Do-Config }
  'status' { Show-Status }
  'uninstall' { Do-Uninstall }
  'version' { Say "deploy.ps1 $ScriptVersion; relay-server $(Get-BinVersion $Bin)" }
  default { Die "未知命令 (unknown command): $Action" }
}
