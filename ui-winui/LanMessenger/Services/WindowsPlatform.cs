using System.Diagnostics;
using LanMessenger.Core.ViewModels;
using Windows.ApplicationModel.DataTransfer;
using Windows.Storage.Pickers;

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
        return files == null ? Array.Empty<string>() : files.Select(f => f.Path).Where(x => !string.IsNullOrEmpty(x)).ToList();
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
