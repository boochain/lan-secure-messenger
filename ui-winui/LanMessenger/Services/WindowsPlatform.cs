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
}
