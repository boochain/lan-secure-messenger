using System.Diagnostics;
using LanMessenger.Core.ViewModels;
using Windows.ApplicationModel.DataTransfer;
using Windows.Storage.Pickers;
using System.Runtime.InteropServices.WindowsRuntime;

namespace LanMessenger.Services;

public sealed class WindowsPlatform : IPlatform
{
    public MainWindow? Window { get; set; }

    private IntPtr Hwnd => Window == null ? IntPtr.Zero : WinRT.Interop.WindowNative.GetWindowHandle(Window);

    public static string DaemonPath => Path.Combine(AppContext.BaseDirectory, "lan-daemon.exe");

    public bool StartDaemon(string endpoint)
    {
        try
        {
            if (!File.Exists(DaemonPath))
            {
                CrashLog.Write("daemon missing", new FileNotFoundException(DaemonPath));
                return false;
            }
            var psi = new ProcessStartInfo(DaemonPath)
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                WorkingDirectory = AppContext.BaseDirectory,
            };
            psi.ArgumentList.Add("run");
            if (!string.Equals(endpoint, LanMessenger.Core.Ipc.Endpoint.Default(), StringComparison.OrdinalIgnoreCase))
            {
                psi.ArgumentList.Add("--ipc");
                psi.ArgumentList.Add(endpoint);
            }
            Process.Start(psi);
            return true;
        }
        catch (Exception e)
        {
            CrashLog.Write("start daemon", e);
            return false;
        }
    }

    public async Task<IReadOnlyList<string>> PickFilesAsync()
    {
        var p = new FileOpenPicker();
        WinRT.Interop.InitializeWithWindow.Initialize(p, Hwnd);
        p.FileTypeFilter.Add("*");
        var files = await p.PickMultipleFilesAsync();
        if (files == null) return Array.Empty<string>();
        return files.Select(f => f.Path).Where(x => !string.IsNullOrEmpty(x)).ToList();
    }

    public async Task<string?> PickFolderAsync()
    {
        var p = new FolderPicker();
        WinRT.Interop.InitializeWithWindow.Initialize(p, Hwnd);
        p.FileTypeFilter.Add("*");
        var f = await p.PickSingleFolderAsync();
        return f?.Path;
    }

    public async Task<string?> PickSaveFileAsync(string suggestedName)
    {
        var p = new FileSavePicker();
        WinRT.Interop.InitializeWithWindow.Initialize(p, Hwnd);
        p.SuggestedFileName = suggestedName;
        p.FileTypeChoices.Add("LAN Messenger 备份", new List<string> { ".lsmbak" });
        var f = await p.PickSaveFileAsync();
        return f?.Path;
    }

    public async Task<string?> PickOpenFileAsync()
    {
        var p = new FileOpenPicker();
        WinRT.Interop.InitializeWithWindow.Initialize(p, Hwnd);
        p.FileTypeFilter.Add(".lsmbak");
        p.FileTypeFilter.Add("*");
        var f = await p.PickSingleFileAsync();
        return f?.Path;
    }

    
    public async Task<bool> ConfirmSendAsync(string title, string message)
    {
        if (Environment.GetEnvironmentVariable("LSM_UI_SMOKE") is { Length: > 0 }) return true; // UI smoke drives drops / pastes unattended
        var dlg = ThemeService.Prepare(new Microsoft.UI.Xaml.Controls.ContentDialog
        {
            Title = title, Content = message, PrimaryButtonText = "发送", CloseButtonText = "取消",
            DefaultButton = Microsoft.UI.Xaml.Controls.ContentDialogButton.Primary, XamlRoot = Window?.Content?.XamlRoot
        });
        if (dlg.XamlRoot == null) return false;
        return await dlg.ShowAsync() == Microsoft.UI.Xaml.Controls.ContentDialogResult.Primary;
    }

    public async Task<int> ConfirmImageSendAsync(string title, string message)
    {
        if (Environment.GetEnvironmentVariable("LSM_UI_SMOKE") is { Length: > 0 }) return 1;
        var dlg = ThemeService.Prepare(new Microsoft.UI.Xaml.Controls.ContentDialog
        {
            Title = title, Content = message,
            PrimaryButtonText = "压缩发送", SecondaryButtonText = "原图发送", CloseButtonText = "取消",
            DefaultButton = Microsoft.UI.Xaml.Controls.ContentDialogButton.Primary, XamlRoot = Window?.Content?.XamlRoot
        });
        if (dlg.XamlRoot == null) return 0;
        var r = await dlg.ShowAsync();
        if (r == Microsoft.UI.Xaml.Controls.ContentDialogResult.Primary) return 1;
        if (r == Microsoft.UI.Xaml.Controls.ContentDialogResult.Secondary) return 2;
        return 0;
    }

    public async Task<string?> CompressImageAsync(string path)
    {
        try
        {
            if (string.IsNullOrEmpty(path) || !File.Exists(path)) return null;
            using var fs = File.OpenRead(path);
            using var ras = fs.AsRandomAccessStream();
            var dec = await Windows.Graphics.Imaging.BitmapDecoder.CreateAsync(ras);
            double w = dec.PixelWidth, h = dec.PixelHeight, k = 1;
            if (w > 1920 || h > 1920) k = Math.Min(1920 / w, 1920 / h);
            var transform = new Windows.Graphics.Imaging.BitmapTransform
            {
                ScaledWidth = (uint)Math.Max(1, Math.Round(w * k)),
                ScaledHeight = (uint)Math.Max(1, Math.Round(h * k)),
                InterpolationMode = Windows.Graphics.Imaging.BitmapInterpolationMode.Fant
            };
            var sb = await dec.GetSoftwareBitmapAsync(
                Windows.Graphics.Imaging.BitmapPixelFormat.Bgra8,
                Windows.Graphics.Imaging.BitmapAlphaMode.Premultiplied,
                transform,
                Windows.Graphics.Imaging.ExifOrientationMode.RespectExifOrientation,
                Windows.Graphics.Imaging.ColorManagementMode.DoNotColorManage);
            var dest = Path.Combine(Path.GetTempPath(), $"lsm-img-{Environment.ProcessId}-{DateTime.Now.Ticks}.jpg");
            using (var ofs = File.Create(dest))
            using (var os = ofs.AsRandomAccessStream())
            {
                var props = new Windows.Graphics.Imaging.BitmapPropertySet
                {
                    { "ImageQuality", new Windows.Graphics.Imaging.BitmapTypedValue(0.80f, Windows.Foundation.PropertyType.Single) }
                };
                var enc = await Windows.Graphics.Imaging.BitmapEncoder.CreateAsync(Windows.Graphics.Imaging.BitmapEncoder.JpegEncoderId, os, props);
                enc.SetSoftwareBitmap(sb);
                await enc.FlushAsync();
            }
            return dest;
        }
        catch { return null; }
    }

    public async Task<bool> ConfirmAsync(string title, string message)
    {
        var dlg = ThemeService.Prepare(new Microsoft.UI.Xaml.Controls.ContentDialog
        {
            Title = title,
            Content = message,
            PrimaryButtonText = "打开",
            CloseButtonText = "取消",
            DefaultButton = Microsoft.UI.Xaml.Controls.ContentDialogButton.Close,
            XamlRoot = Window?.Content?.XamlRoot
        });
        if (dlg.XamlRoot == null) return false;
        return await dlg.ShowAsync() == Microsoft.UI.Xaml.Controls.ContentDialogResult.Primary;
    }

    public void OpenPath(string path)
    {
        try { Process.Start(new ProcessStartInfo(path) { UseShellExecute = true }); }
        catch (Exception e) { App.VM.ShowStatus("无法打开：" + e.Message, true); }
    }

    public void RevealInFolder(string path)
    {
        try
        {
            if (File.Exists(path)) Process.Start("explorer.exe", $"/select,\"{path}\"");
            else if (Directory.Exists(path)) Process.Start("explorer.exe", $"\"{path}\"");
        }
        catch (Exception e) { App.VM.ShowStatus("无法打开文件夹：" + e.Message, true); }
    }

    public void CopyToClipboard(string text)
    {
        var dp = new DataPackage();
        dp.SetText(text ?? "");
        Clipboard.SetContent(dp);
    }

    public void ShowToast(string title, string body, string conversationId) => Toasts.Show(title, body, conversationId);

    public bool IsWindowActive => Window?.IsActiveAndVisible ?? false;

    public async Task<string?> StageSelfUpdateAsync(string assetUrl, string sumsUrl, string assetName)
    {
        var dest = AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        if (dest.Contains("ui-winui", StringComparison.OrdinalIgnoreCase) || dest.Contains(@"\build\", StringComparison.OrdinalIgnoreCase))
            return "当前不是安装包（开发目录不会被替换）。请从发布页下载。";
        if (string.IsNullOrEmpty(sumsUrl) || string.IsNullOrEmpty(assetName))
            return "这个版本没有 SHA256SUMS，已停止安装。";
        var dir = Path.Combine(Path.GetTempPath(), "lsm-update");
        Directory.CreateDirectory(dir);
        var zip = Path.Combine(dir, "pkg.zip");
        var sums = Path.Combine(dir, "SHA256SUMS.txt");
        var script = Path.Combine(dir, "apply.ps1");
        try
        {
            using var http = new HttpClient { Timeout = TimeSpan.FromMinutes(10) };
            http.DefaultRequestHeaders.UserAgent.ParseAdd("LanMessenger-WinUI");
            if (!await SaveAsync(http, assetUrl, zip) || !await SaveAsync(http, sumsUrl, sums))
                return "下载更新包失败。";
            if (!ShaMatches(sums, assetName, zip))
            {
                try { File.Delete(zip); } catch { }
                return "更新包校验失败，没有替换任何文件。";
            }
            var pid = Environment.ProcessId;
            var exe = Path.Combine(dest, "LanMessenger.exe");
            string Q(string s) => s.Replace("'", "''");
            // Regular strings: '$' must stay a PowerShell variable. $PID is reserved, so the app id is $app.
            File.WriteAllText(script,
                "$ErrorActionPreference='Stop'\r\n" +
                "$app = " + pid + "\r\n" +
                "$zip = '" + Q(zip) + "'\r\n" +
                "$dest = '" + Q(dest) + "'\r\n" +
                "$exe = '" + Q(exe) + "'\r\n" +
                "for ($i = 0; $i -lt 240; $i++) {\r\n" +
                "  if (-not (Get-Process -Id $app -ErrorAction SilentlyContinue)) { break }\r\n" +
                "  Start-Sleep -Milliseconds 250\r\n}\r\n" +
                "$stage = Join-Path $env:TEMP ('lsm-unzip-' + $app)\r\n" +
                "if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }\r\n" +
                "Expand-Archive -LiteralPath $zip -DestinationPath $stage -Force\r\n" +
                "$root = Join-Path $stage 'LanMessenger'\r\n" +
                "if (-not (Test-Path (Join-Path $root 'LanMessenger.exe'))) {\r\n" +
                "  $dirs = @(Get-ChildItem $stage -Directory)\r\n" +
                "  if ($dirs.Count -eq 1) { $root = $dirs[0].FullName } else { $root = $stage }\r\n}\r\n" +
                "function Copy-Tree($from, $to) {\r\n" +
                "  New-Item -ItemType Directory -Force -Path $to | Out-Null\r\n" +
                "  Get-ChildItem $from -Force | ForEach-Object {\r\n" +
                "    $target = Join-Path $to $_.Name\r\n" +
                "    if ($_.PSIsContainer) { Copy-Tree $_.FullName $target }\r\n" +
                "    else {\r\n" +
                "      for ($n = 0; $n -lt 40; $n++) {\r\n" +
                "        try { Copy-Item -LiteralPath $_.FullName -Destination $target -Force; break } catch { Start-Sleep -Milliseconds 250 }\r\n" +
                "      }\r\n    }\r\n  }\r\n}\r\n" +
                "Copy-Tree $root $dest\r\n" +
                "Start-Process -FilePath $exe -WorkingDirectory $dest\r\n");
            var psi = new ProcessStartInfo("cmd.exe", "/c start \"\" /MIN powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" + script + "\"")
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                WindowStyle = ProcessWindowStyle.Hidden,
            };
            Process.Start(psi);
        }
        catch (Exception e)
        {
            return "安装更新失败：" + e.Message;
        }
        try { if (Window != null) await Window.ExitFullyAsync(); } catch { }
        return null;
    }

    private static async Task<bool> SaveAsync(HttpClient http, string url, string path)
    {
        using var res = await http.GetAsync(url, HttpCompletionOption.ResponseHeadersRead);
        if (!res.IsSuccessStatusCode) return false;
        await using var src = await res.Content.ReadAsStreamAsync();
        await using var dst = File.Create(path);
        var buf = new byte[65536];
        long total = 0;
        int n;
        while ((n = await src.ReadAsync(buf)) > 0)
        {
            total += n;
            if (total > 512L * 1024 * 1024) return false;
            await dst.WriteAsync(buf.AsMemory(0, n));
        }
        return total > 0;
    }

    private static bool ShaMatches(string sumsPath, string filename, string file)
    {
        string got;
        using (var sha = System.Security.Cryptography.SHA256.Create())
        using (var s = File.OpenRead(file))
            got = Convert.ToHexString(sha.ComputeHash(s)).ToLowerInvariant();
        foreach (var raw in File.ReadAllLines(sumsPath))
        {
            var line = raw.Trim();
            var sp = line.IndexOf(' ');
            if (sp < 1) continue;
            var hex = line[..sp];
            var name = line[(sp + 1)..].TrimStart(' ', '*');
            if (name.Length >= filename.Length && name.EndsWith(filename, StringComparison.Ordinal) &&
                string.Equals(hex, got, StringComparison.OrdinalIgnoreCase))
                return true;
        }
        return false;
    }
}
