<#
  UI smoke test in BOTH Windows themes (CI, windows-latest).
  For each theme: set HKCU ...\Themes\Personalize AppsUseLightTheme/SystemUsesLightTheme, broadcast the change,
  launch LanMessenger.exe (fresh data dir), connect a second daemon "Bob-CI" that sends text, an image and a group
  invite, and let the app screenshot every page + a popup menu + the first-run dialog (UiSmoke.cs), which also runs an
  automated contrast check. The dark run finally flips Windows to light while the app is running and checks that the
  app follows live. Fails if any screenshot is missing or the app reports UI_SMOKE_FAIL.
#>
param(
  [string]$Dist = "dist\LanMessenger",
  [string]$Out = "ui-smoke",
  [string]$Image = "ui-winui\LanMessenger\Assets\app.png"
)
$ErrorActionPreference = "Stop"
$Out = (New-Item -ItemType Directory -Force $Out).FullName
$AppExe = (Resolve-Path (Join-Path $Dist "LanMessenger.exe")).Path  # (PowerShell names are case-insensitive: keep distinct from $proc)
$M = (Resolve-Path (Join-Path $Dist "lan-msg.exe")).Path
$D = (Resolve-Path (Join-Path $Dist "lan-daemon.exe")).Path
$Img = (Resolve-Path $Image).Path

Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices;
public static class ThemeBroadcast {
  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  static extern IntPtr SendMessageTimeout(IntPtr h, uint msg, UIntPtr w, string l, uint flags, uint timeout, out UIntPtr res);
  public static void Notify() { UIntPtr r; SendMessageTimeout((IntPtr)0xffff, 0x001A, UIntPtr.Zero, "ImmersiveColorSet", 2, 5000, out r); }
}
"@
Add-Type -AssemblyName System.Windows.Forms, System.Drawing

function Set-WindowsTheme([string]$theme) {
  $k = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize"
  if (-not (Test-Path $k)) { New-Item $k -Force | Out-Null }
  $v = if ($theme -eq "light") { 1 } else { 0 }
  New-ItemProperty $k -Name AppsUseLightTheme -Value $v -PropertyType DWord -Force | Out-Null
  New-ItemProperty $k -Name SystemUsesLightTheme -Value $v -PropertyType DWord -Force | Out-Null
  [ThemeBroadcast]::Notify()
  Start-Sleep -Seconds 2
  Write-Host "Windows theme -> $theme (AppsUseLightTheme=$((Get-ItemProperty $k).AppsUseLightTheme))"
}

function Save-Desktop([string]$path) {
  $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
  $g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
  $bmp.Save($path); $g.Dispose(); $bmp.Dispose()
}

$shots = "page-chat", "page-chat-dm", "page-groups", "page-transfers", "page-settings", "page-settings-bottom",
         "page-diag", "page-diag-bottom", "popup-menu", "first-run"
$problems = @()

foreach ($theme in "light", "dark") {
  Write-Host "===================== UI smoke: $theme ====================="
  Set-WindowsTheme $theme
  $dir = Join-Path $Out $theme
  New-Item -ItemType Directory -Force $dir | Out-Null
  $env:LSM_UI_SMOKE = $dir
  $env:LSM_UI_SMOKE_THEME = $theme
  $env:LSM_EXPECT_THEME = $theme
  $env:LSM_DATA_DIR = Join-Path $env:RUNNER_TEMP "lsm-ui-$theme"
  $env:LSM_UI_SMOKE_LIVE_FLIP = if ($theme -eq "dark") { "1" } else { $null }
  $env:LSM_NO_DIALOG = "1"

  $proc = Start-Process $AppExe -PassThru
  for ($i = 0; $i -lt 120 -and -not (Test-Path "$dir\ready.txt"); $i++) {
    if ($proc.HasExited) { throw "[$theme] LanMessenger.exe exited early with code $($proc.ExitCode)" }
    Start-Sleep -Milliseconds 500
  }
  if (-not (Test-Path "$dir\ready.txt")) { throw "[$theme] UI never reported ready" }
  $FA = (Get-Content "$dir\ready.txt" -Raw).Trim()
  $portA = (& $M --json status | ConvertFrom-Json).me.listen_port
  Write-Host "[$theme] UI daemon $FA on port $portA"

  $bobPipe = "\\.\pipe\lsm-ci-bob-$theme"
  $bobData = Join-Path $env:RUNNER_TEMP "lsm-bob-$theme"
  $bob = Start-Process $D -ArgumentList "run --data-dir `"$bobData`" --ipc $bobPipe --set listen_port=0 --set discovery_enabled=false --set auto_trust=true --set display_name=Bob-CI" -PassThru -NoNewWindow
  Start-Sleep -Seconds 2
  & $M --ipc $bobPipe add "127.0.0.1:$portA"
  & $M --ipc $bobPipe wait-peer $FA --timeout 30
  $FB = (& $M --ipc $bobPipe --json status | ConvertFrom-Json).me.fingerprint
  & $M trust $FB
  & $M --ipc $bobPipe send $FA "你好！这是来自 CI 第二个节点的消息 👋"
  & $M send $FB "收到，界面渲染测试中。"
  & $M --ipc $bobPipe send-file $FA $Img
  & $M --ipc $bobPipe group-create "CI 测试群" $FA
  Start-Sleep -Seconds 4
  New-Item "$dir\go" -ItemType File -Force | Out-Null
  Start-Sleep -Seconds 8
  Save-Desktop (Join-Path $dir "desktop-$theme.png")

  $deadline = (Get-Date).AddSeconds(240)
  while (-not $proc.HasExited -and (Get-Date) -lt $deadline) {
    if ((Test-Path "$dir\flip-request") -and -not (Test-Path "$dir\flipped")) {
      Write-Host "[$theme] app asked for a live theme flip"
      Set-WindowsTheme ($(if ($theme -eq "dark") { "light" } else { "dark" }))
      New-Item "$dir\flipped" -ItemType File -Force | Out-Null
    }
    Start-Sleep -Milliseconds 500
  }
  if (-not $proc.HasExited) { Stop-Process $proc -Force; $problems += "[$theme] UI smoke timed out" }
  Stop-Process $bob -Force -ErrorAction SilentlyContinue
  Start-Sleep -Seconds 2

  if (Test-Path "$dir\ui-smoke.txt") { Get-Content "$dir\ui-smoke.txt" | ForEach-Object { Write-Host "[$theme] $_" } }
  else { $problems += "[$theme] ui-smoke.txt missing" }
  if (Test-Path "$dir\contrast-$theme.txt") { Get-Content "$dir\contrast-$theme.txt" | Where-Object { $_ -match "FAIL|warn|checked=" } | ForEach-Object { Write-Host "[$theme] $_" } }
  else { $problems += "[$theme] contrast-$theme.txt missing" }
  if (-not (Select-String -Path "$dir\ui-smoke.txt" -Pattern "UI_SMOKE_OK" -Quiet -ErrorAction SilentlyContinue)) { $problems += "[$theme] app reported UI_SMOKE_FAIL" }
  $need = @($shots | ForEach-Object { "$_-$theme.png" })
  if ($theme -eq "dark") { $need += "page-chat-dm-live-light.png" }
  foreach ($f in $need) {
    $p = Join-Path $dir $f
    if (-not (Test-Path $p)) { $problems += "[$theme] missing screenshot $f" }
    elseif ((Get-Item $p).Length -lt 5000) { $problems += "[$theme] screenshot $f is suspiciously small" }
  }
  # keep the daemon/UI logs of this run
  foreach ($l in (Join-Path $env:LSM_DATA_DIR "logs"), (Join-Path $env:APPDATA "FREC86\lan-secure-messenger\logs")) {
    if (Test-Path $l) { Copy-Item $l (Join-Path $dir "logs") -Recurse -Force }
  }
}

Set-WindowsTheme "light"
Get-ChildItem $Out -Recurse -Filter *.png | Format-Table FullName, Length
if ($problems.Count -gt 0) {
  $problems | ForEach-Object { Write-Host "::error::$_" }
  throw "UI smoke failed: $($problems.Count) problem(s)"
}
Write-Host "UI smoke OK in light and dark"
