using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Graphics.Imaging;
using Windows.Storage;
using Windows.Storage.Streams;

namespace LanMessenger.Services;

/// <summary>
/// CI smoke mode (env LSM_UI_SMOKE=&lt;dir&gt;): waits for the daemon connection, visits every page,
/// renders each to PNG with RenderTargetBitmap and writes a summary, then exits.
/// </summary>
public static class UiSmoke
{
    public static async Task RunAsync(MainWindow w, string dir)
    {
        var log = new List<string>();
        try
        {
            Directory.CreateDirectory(dir);
            for (int i = 0; i < 100 && !App.VM.IsConnected; i++) await Task.Delay(300);
            log.Add($"connected={App.VM.IsConnected} fp={App.VM.MyFingerprint} name={App.VM.MyName}");
            if (App.VM.FirstRunNeeded) await App.VM.CompleteFirstRunAsync("CI 测试用户");
            log.Add($"ready fp={App.VM.MyFingerprint}");
            File.WriteAllText(Path.Combine(dir, "ready.txt"), App.VM.MyFingerprint);
            // CI creates <dir>/go after a second daemon has connected and sent messages (max 90 s).
            for (int i = 0; i < 180 && !File.Exists(Path.Combine(dir, "go")); i++) await Task.Delay(500);
            await Task.Delay(3000); // let events render
            if (App.VM.Conversations.Count > 0) App.VM.SelectedConversation = App.VM.Conversations[0];
            foreach (var tag in new[] { "chat", "groups", "transfers", "settings", "diag" })
            {
                w.NavigateTo(tag);
                if (tag == "diag") await App.VM.RefreshDiagnosticsAsync(false);
                await Task.Delay(2500);
                var file = Path.Combine(dir, $"page-{tag}.png");
                await CaptureAsync((UIElement)w.Content, file);
                log.Add($"{tag}: {new FileInfo(file).Length} bytes");
            }
            log.Add($"peers={App.VM.Peers.Count} online={App.VM.OnlineCount} conversations={App.VM.Conversations.Count} messages={App.VM.Messages.Count}");
            log.Add("UI_SMOKE_OK");
        }
        catch (Exception e)
        {
            log.Add("UI_SMOKE_FAIL " + e);
        }
        File.WriteAllLines(Path.Combine(dir, "ui-smoke.txt"), log);
        await App.VM.StopAsync(shutdownDaemon: true);
        w.ExitApp();
    }

    private static async Task CaptureAsync(UIElement root, string path)
    {
        var rtb = new RenderTargetBitmap();
        await rtb.RenderAsync(root);
        var pixels = await rtb.GetPixelsAsync();
        var folder = await StorageFolder.GetFolderFromPathAsync(Path.GetDirectoryName(path)!);
        var file = await folder.CreateFileAsync(Path.GetFileName(path), CreationCollisionOption.ReplaceExisting);
        using IRandomAccessStream stream = await file.OpenAsync(FileAccessMode.ReadWrite);
        var enc = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, stream);
        enc.SetPixelData(BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied, (uint)rtb.PixelWidth, (uint)rtb.PixelHeight, 96, 96, pixels.ToArray());
        await enc.FlushAsync();
    }
}
