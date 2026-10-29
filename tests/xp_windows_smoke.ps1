# XP edition (LanMessenger.exe, i686) on a real Windows desktop (GitHub windows runner):
#   X5  close (WM_CLOSE) hides the window to the notification area, the process keeps running
#   X12 a second start (no --data) exits and the running copy shows its hidden window again
#   X6  window placement survives 文件 > 退出 (IDM_EXIT) and a restart
# usage: pwsh tests/xp_windows_smoke.ps1 <path\to\LanMessenger.exe>
param([Parameter(Mandatory = $true)][string]$Exe)
$ErrorActionPreference = 'Stop'
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class W {
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, string t);
  public static string Dump(uint want) { var sb = new System.Text.StringBuilder();
    EnumWindows((h, l) => { uint pid; GetWindowThreadProcessId(h, out pid); if (pid == want) { var c = new System.Text.StringBuilder(256); var t = new System.Text.StringBuilder(512); GetClassName(h, c, 256); GetWindowText(h, t, 512);
      RECT r; GetWindowRect(h, out r); sb.AppendFormat("  hwnd={0} class={1} title='{2}' visible={3} rect={4},{5},{6},{7}\n", h, c, t, IsWindowVisible(h), r.L, r.T, r.R, r.B);
      IntPtr s = IntPtr.Zero; while ((s = FindWindowEx(h, s, "Static", null)) != IntPtr.Zero) { var st = new System.Text.StringBuilder(1024); GetWindowText(s, st, 1024); if (st.Length > 0) sb.AppendFormat("    static: {0}\n", st); } } return true; }, IntPtr.Zero);
    return sb.ToString(); }
}
'@
$fails = @()
$shots = Join-Path (Get-Location) 'xp-windows-smoke'; New-Item -ItemType Directory -Force $shots | Out-Null
function Shot($name) { Add-Type -AssemblyName System.Windows.Forms, System.Drawing; $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds; $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
  $g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size); $bmp.Save((Join-Path $shots "$name.png")); $g.Dispose(); $bmp.Dispose() }
function Check($ok, $what) { if ($ok) { Write-Host "PASS: $what" } else { Write-Host "FAIL: $what"; $script:fails += $what; if ($script:p -and -not $script:p.HasExited) { Write-Host ([W]::Dump([uint32]$script:p.Id)) }; Shot ("fail-" + $script:fails.Count) } }
function WaitFor($cond, $sec) { $end = (Get-Date).AddSeconds($sec); while ((Get-Date) -lt $end) { $v = & $cond; if ($v) { return $v }; Start-Sleep -Milliseconds 250 }; return $null }
$Exe = (Resolve-Path $Exe).Path; $dir = Split-Path $Exe
$data = Join-Path $env:RUNNER_TEMP 'lsm-xp-smoke'; New-Item -ItemType Directory -Force $data | Out-Null
Remove-Item (Join-Path $dir 'window.txt') -ErrorAction SilentlyContinue
$args1 = @('--data', $data, '--name', 'CI', '--port', '47320', '--disco-port', '47321')
function Start-App { $p = Start-Process $Exe -ArgumentList $args1 -PassThru; $h = WaitFor { $x = [W]::FindWindow('LsmMain', [NullString]::Value); if ($x -ne [IntPtr]::Zero -and [W]::IsWindowVisible($x)) { $x } } 30; return @($p, $h) }

$p, $h = Start-App
Check ($h -ne $null) "XP window appeared (pid $($p.Id))"
Start-Sleep 3   # tray icon added on the first timer tick at the latest
# X5
[W]::PostMessage($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null   # WM_CLOSE
$hidden = WaitFor { -not [W]::IsWindowVisible($h) } 10
Check ($hidden -and -not $p.HasExited) "X5 close hides the window to the notification area, process keeps running"
# X12
$p2 = Start-Process $Exe -PassThru
$ended = $p2.WaitForExit(15000)
$shown = WaitFor { [W]::IsWindowVisible($h) } 10
Check ($ended -and $shown -and -not $p.HasExited) "X12 second start exits (exited=$ended) and the running copy shows its window again (visible=$([bool]$shown))"
if (-not $shown) { [W]::PostMessage($h, 0x0111, [IntPtr]216, [IntPtr]::Zero) | Out-Null; Start-Sleep 1 }   # fall back so X6 can run
# X6
[W]::MoveWindow($h, 60, 50, 900, 640, $true) | Out-Null; Start-Sleep 1
$r = New-Object W+RECT; [W]::GetWindowRect($h, [ref]$r) | Out-Null; $want = "$($r.L),$($r.T) $($r.R-$r.L)x$($r.B-$r.T)"
[W]::PostMessage($h, 0x0111, [IntPtr]202, [IntPtr]::Zero) | Out-Null   # WM_COMMAND IDM_EXIT (文件 > 退出)
Check ($p.WaitForExit(15000)) "文件 > 退出 exits"
$p, $h = Start-App
$r2 = New-Object W+RECT; if ($h) { [W]::GetWindowRect($h, [ref]$r2) | Out-Null }; $have = "$($r2.L),$($r2.T) $($r2.R-$r2.L)x$($r2.B-$r2.T)"
Check ($h -and $want -eq $have) "X6 window placement restored after 退出 + restart ($want -> $have)"
if ($p -and -not $p.HasExited) { $p.Kill() }
if ($fails.Count) { Write-Host "RESULT FAIL $($fails.Count)"; exit 1 } else { Write-Host "RESULT PASS" }
