using System.Runtime.InteropServices;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Text;
using LanMessenger.Views;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Foundation;
using Windows.Graphics.Imaging;
using Windows.Storage;
using Windows.Storage.Streams;
using Windows.UI;

namespace LanMessenger.Services;

/// <summary>
/// CI smoke mode (env LSM_UI_SMOKE=&lt;dir&gt;): waits for the daemon connection, visits every page, screenshots the real
/// window (Mica included) plus a popup menu and the first-run dialog, and checks text contrast of every visible
/// TextBlock/FontIcon against its composited background. Files are suffixed with the theme label
/// (LSM_UI_SMOKE_THEME, e.g. page-chat-dm-dark.png). LSM_EXPECT_THEME=light|dark fails the run if the app did not
/// actually render in that theme. LSM_UI_SMOKE_LIVE_FLIP=1 asks CI to flip the Windows theme at the end and verifies the
/// running app follows it.
/// </summary>
public static class UiSmoke
{
    private const double MinContrast = 3.0;   // fail below (WCAG AA for large text / UI glyphs)
    private const double WarnContrast = 4.5;  // report below (WCAG AA for body text)

    private static readonly List<string> Log = new();
    private static readonly List<string> Contrast = new();
    private static bool _ok = true;

    private static void Fail(string msg) { _ok = false; Log.Add("FAIL: " + msg); }

    public static async Task RunAsync(MainWindow w, string dir)
    {
        var label = Environment.GetEnvironmentVariable("LSM_UI_SMOKE_THEME");
        try
        {
            Directory.CreateDirectory(dir);
            try
            {
                if (w.AppWindow.Presenter is OverlappedPresenter op) op.IsAlwaysOnTop = true; // keep console windows out of the screenshots
            }
            catch { }
            for (int i = 0; i < 100 && !App.VM.IsConnected; i++) await Task.Delay(300);
            Log.Add($"connected={App.VM.IsConnected} fp={App.VM.MyFingerprint} name={App.VM.MyName}");
            if (App.VM.FirstRunNeeded) await App.VM.CompleteFirstRunAsync("CI 测试用户");
            label = string.IsNullOrWhiteSpace(label) ? ThemeService.ActualTheme.ToString().ToLowerInvariant() : label.Trim().ToLowerInvariant();
            Log.Add($"ready fp={App.VM.MyFingerprint}");
            File.WriteAllText(Path.Combine(dir, "ready.txt"), App.VM.MyFingerprint);
            // CI creates <dir>/go after a second daemon has connected and sent messages (max 90 s).
            for (int i = 0; i < 180 && !File.Exists(Path.Combine(dir, "go")); i++) await Task.Delay(500);
            await Task.Delay(3000); // let events render
            w.ShowAndFocus();

            var theme = ThemeService.ActualTheme;
            Log.Add($"theme: actual={theme} mode={ThemeService.Mode} systemDark={ThemeService.SystemIsDark} highContrast={ThemeService.HighContrast} backdrop={w.BackdropActive} label={label}");
            var expect = Environment.GetEnvironmentVariable("LSM_EXPECT_THEME");
            if (!string.IsNullOrWhiteSpace(expect) && !string.Equals(expect.Trim(), theme.ToString(), StringComparison.OrdinalIgnoreCase))
                Fail($"expected theme {expect} but the window rendered {theme}");

            if (App.VM.Conversations.Count > 0) App.VM.SelectedConversation = App.VM.Conversations[0];
            foreach (var tag in new[] { "chat", "groups", "transfers", "settings", "diag" })
            {
                w.NavigateTo(tag);
                if (tag == "diag") await App.VM.RefreshDiagnosticsAsync(false);
                await Task.Delay(2500);
                await ShotAsync(w, dir, $"page-{tag}", label, theme);
                if (tag == "chat")
                {
                    // The DM with the peer (text both ways + image preview).
                    var dm = App.VM.Conversations.FirstOrDefault(c => c.IsDm);
                    if (dm != null)
                    {
                        App.VM.SelectedConversation = dm;
                        await Task.Delay(3000);
                        await ShotAsync(w, dir, "page-chat-dm", label, theme);
                        Log.Add($"chat-dm: title={dm.Title}, messages={App.VM.Messages.Count}, images={App.VM.Messages.Count(m => m.IsImage)}, " +
                                $"incoming={App.VM.Messages.Count(m => m.IsIncoming)}, outgoing={App.VM.Messages.Count(m => m.IsOutgoing)}");
                        if (!App.VM.Messages.Any(m => m.IsImage) || !App.VM.Messages.Any(m => m.IsIncoming && m.IsText) || !App.VM.Messages.Any(m => m.IsOutgoing))
                            Fail("DM is missing the peer's text, the image preview or our reply");

                        // Popup layer: the "更多" menu flyout.
                        if (w.CurrentPage is ChatPage cp)
                        {
                            cp.OpenMoreMenuForSmoke();
                            await Task.Delay(1200);
                            await ShotAsync(w, dir, "popup-menu", label, theme);
                            cp.CloseMoreMenuForSmoke();
                            await Task.Delay(600);
                        }
                    }
                    else Fail("no DM conversation with the peer");
                }
                if (tag is "settings" or "diag")
                {
                    // Second capture scrolled to the bottom so every card of the long pages is checked.
                    var sv = FindDescendant<ScrollViewer>(w.CurrentPage);
                    if (sv != null)
                    {
                        sv.ChangeView(null, sv.ScrollableHeight, null, true);
                        await Task.Delay(1200);
                        await ShotAsync(w, dir, $"page-{tag}-bottom", label, theme);
                        sv.ChangeView(null, 0, null, true);
                    }
                }
            }

            // First-run dialog (ContentDialog in the popup layer), shown non-modally just for the capture.
            try
            {
                var dlg = ThemeService.Prepare(new FirstRunDialog { XamlRoot = w.Content.XamlRoot });
                _ = dlg.ShowAsync();
                await Task.Delay(3000);
                await ShotAsync(w, dir, "first-run", label, theme);
                dlg.Hide();
                await Task.Delay(800);
            }
            catch (Exception e) { Fail("first-run dialog: " + e.Message); }

            if (Environment.GetEnvironmentVariable("LSM_UI_SMOKE_LIVE_FLIP") == "1")
                await LiveFlipAsync(w, dir, theme);

            Log.Add($"peers={App.VM.Peers.Count} online={App.VM.OnlineCount} conversations={App.VM.Conversations.Count} messages={App.VM.Messages.Count}");
            if (!App.VM.IsConnected) Fail("not connected to daemon");
            if (App.VM.OnlineCount < 1) Fail("peer not online");
            if (!App.VM.Groups.Any()) Fail("group from peer not shown");
        }
        catch (Exception e)
        {
            Fail("exception " + e);
        }
        Log.Add(_ok ? "UI_SMOKE_OK" : "UI_SMOKE_FAIL");
        var suffix = string.IsNullOrWhiteSpace(label) ? "" : "-" + label;
        File.WriteAllLines(Path.Combine(dir, $"contrast{suffix}.txt"), Contrast);
        File.WriteAllLines(Path.Combine(dir, "ui-smoke.txt"), Log);
        await App.VM.StopAsync(shutdownDaemon: true);
        w.ExitApp();
    }

    private static async Task LiveFlipAsync(MainWindow w, string dir, ElementTheme before)
    {
        App.VM.SelectedConversation = App.VM.Conversations.FirstOrDefault(c => c.IsDm) ?? App.VM.SelectedConversation;
        w.NavigateTo("chat");
        File.WriteAllText(Path.Combine(dir, "flip-request"), before.ToString());
        for (int i = 0; i < 120 && !File.Exists(Path.Combine(dir, "flipped")); i++) await Task.Delay(500);
        if (!File.Exists(Path.Combine(dir, "flipped"))) { Fail("live flip: CI never flipped the theme"); return; }
        var want = before == ElementTheme.Dark ? ElementTheme.Light : ElementTheme.Dark;
        for (int i = 0; i < 40 && ThemeService.ActualTheme != want; i++) await Task.Delay(250);
        await Task.Delay(1500);
        var now = ThemeService.ActualTheme;
        Log.Add($"live flip: {before} -> {now} (systemDark={ThemeService.SystemIsDark})");
        if (now != want) { Fail($"live flip: window stayed {now} after Windows switched to {want}"); return; }
        await ShotAsync(w, dir, "page-chat-dm-live", now.ToString().ToLowerInvariant(), now);
    }

    // ---------------------------------------------------------------- capture + checks

    private static async Task ShotAsync(MainWindow w, string dir, string name, string label, ElementTheme theme)
    {
        var file = Path.Combine(dir, $"{name}-{label}.png");
        var (px, width, height, how) = await GrabAsync(w, theme);
        await SavePngAsync(file, px, width, height);
        var luma = MeanLuma(px);
        Log.Add($"{name}-{label}: {new FileInfo(file).Length} bytes, {width}x{height} via {how}, meanLuma={luma:F2}");
        if (new FileInfo(file).Length < 5000) Fail($"{name}: screenshot looks empty");
        if (!ThemeService.HighContrast)
        {
            if (theme == ElementTheme.Dark && luma > 0.45) Fail($"{name}: dark theme but screenshot is bright (mean luma {luma:F2})");
            if (theme == ElementTheme.Light && luma < 0.55) Fail($"{name}: light theme but screenshot is dark (mean luma {luma:F2})");
        }
        CheckContrast(w, $"{name}-{label}", theme);
    }

    private static double MeanLuma(byte[] bgra)
    {
        double sum = 0; long n = 0;
        for (int i = 0; i + 3 < bgra.Length; i += 16) // every 4th pixel is plenty
        {
            sum += (0.0722 * bgra[i] + 0.7152 * bgra[i + 1] + 0.2126 * bgra[i + 2]) / 255.0;
            n++;
        }
        return n == 0 ? 0 : sum / n;
    }

    private static Color BaseColor(ElementTheme theme)
    {
        if (ThemeService.HighContrast)
            return new Windows.UI.ViewManagement.UISettings().UIElementColor(Windows.UI.ViewManagement.UIElementType.Window);
        // Mica/fallback base tint (WindowFallbackBackgroundBrush)
        return theme == ElementTheme.Dark ? Color.FromArgb(255, 0x20, 0x20, 0x20) : Color.FromArgb(255, 0xF3, 0xF3, 0xF3);
    }

    /// <summary>Screen pixels of the window (includes Mica, popups and dialogs); falls back to RenderTargetBitmap.</summary>
    private static async Task<(byte[] px, int w, int h, string how)> GrabAsync(MainWindow w, ElementTheme theme)
    {
        try
        {
            var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(w);
            SetForegroundWindow(hwnd);
            await Task.Delay(150);
            if (DwmGetWindowAttribute(hwnd, 9 /* DWMWA_EXTENDED_FRAME_BOUNDS */, out RECT r, Marshal.SizeOf<RECT>()) != 0) GetWindowRect(hwnd, out r);
            int width = r.Right - r.Left, height = r.Bottom - r.Top;
            if (width < 100 || height < 100) throw new InvalidOperationException($"window rect {width}x{height}");
            var screen = GetDC(IntPtr.Zero);
            var mem = CreateCompatibleDC(screen);
            var bmp = CreateCompatibleBitmap(screen, width, height);
            var old = SelectObject(mem, bmp);
            try
            {
                if (!BitBlt(mem, 0, 0, width, height, screen, r.Left, r.Top, 0x00CC0020 | 0x40000000 /* SRCCOPY|CAPTUREBLT */))
                    throw new InvalidOperationException("BitBlt failed");
                var bi = new BITMAPINFO { biSize = 40, biWidth = width, biHeight = -height, biPlanes = 1, biBitCount = 32 };
                var px = new byte[width * height * 4];
                SelectObject(mem, old);
                if (GetDIBits(mem, bmp, 0, (uint)height, px, ref bi, 0) == 0) throw new InvalidOperationException("GetDIBits failed");
                for (int i = 3; i < px.Length; i += 4) px[i] = 255;
                return (px, width, height, "screen");
            }
            finally
            {
                DeleteObject(bmp);
                DeleteDC(mem);
                ReleaseDC(IntPtr.Zero, screen);
            }
        }
        catch (Exception e)
        {
            Log.Add("screen capture failed, using RenderTargetBitmap: " + e.Message);
            var rtb = new RenderTargetBitmap();
            await rtb.RenderAsync((UIElement)w.Content);
            var px = (await rtb.GetPixelsAsync()).ToArray();
            // RTB has no backdrop: composite the premultiplied pixels over the theme's base colour.
            var b = BaseColor(theme);
            for (int i = 0; i + 3 < px.Length; i += 4)
            {
                int a = px[i + 3], inv = 255 - a;
                px[i] = (byte)Math.Min(255, px[i] + b.B * inv / 255);
                px[i + 1] = (byte)Math.Min(255, px[i + 1] + b.G * inv / 255);
                px[i + 2] = (byte)Math.Min(255, px[i + 2] + b.R * inv / 255);
                px[i + 3] = 255;
            }
            return (px, rtb.PixelWidth, rtb.PixelHeight, "rtb");
        }
    }

    private static async Task SavePngAsync(string path, byte[] px, int width, int height)
    {
        var folder = await StorageFolder.GetFolderFromPathAsync(Path.GetDirectoryName(path)!);
        var file = await folder.CreateFileAsync(Path.GetFileName(path), CreationCollisionOption.ReplaceExisting);
        using IRandomAccessStream stream = await file.OpenAsync(FileAccessMode.ReadWrite);
        var enc = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, stream);
        enc.SetPixelData(BitmapPixelFormat.Bgra8, BitmapAlphaMode.Ignore, (uint)width, (uint)height, 96, 96, px);
        await enc.FlushAsync();
    }

    // ---------------------------------------------------------------- contrast

    private static void CheckContrast(MainWindow w, string shot, ElementTheme theme)
    {
        var root = (FrameworkElement)w.Content;
        var bounds = new Rect(0, 0, root.ActualWidth, root.ActualHeight);
        var roots = new List<DependencyObject> { root };
        try { roots.AddRange(VisualTreeHelper.GetOpenPopupsForXamlRoot(root.XamlRoot).Select(p => (DependencyObject)p.Child).Where(c => c != null)); }
        catch { }
        var baseColor = BaseColor(theme);
        int checkedCount = 0, warn = 0, fail = 0;
        double min = double.MaxValue; string minWhat = "";
        foreach (var r in roots)
        {
            foreach (var el in Descendants(r))
            {
                string? text; Brush? fgBrush;
                switch (el)
                {
                    case TextBlock tb:
                        text = tb.Text;
                        if (string.IsNullOrWhiteSpace(text)) text = string.Concat(tb.Inlines.OfType<Run>().Select(x => x.Text));
                        fgBrush = tb.Foreground;
                        break;
                    case FontIcon fi:
                        text = "icon U+" + string.Join(",", (fi.Glyph ?? "").Select(c => ((int)c).ToString("X4")));
                        fgBrush = fi.Foreground;
                        break;
                    default: continue;
                }
                if (string.IsNullOrWhiteSpace(text) || fgBrush is not SolidColorBrush fg) continue;
                var fe = (FrameworkElement)el;
                if (fe.ActualWidth < 1 || fe.ActualHeight < 1) continue;
                if (!EffectivelyVisible(fe, out var opacity)) continue;
                Rect box;
                try { box = fe.TransformToVisual(null).TransformBounds(new Rect(0, 0, fe.ActualWidth, fe.ActualHeight)); }
                catch { continue; }
                double ix = Math.Min(box.X + box.Width, bounds.Width) - Math.Max(box.X, 0);
                double iy = Math.Min(box.Y + box.Height, bounds.Height) - Math.Max(box.Y, 0);
                if (ix < 2 || iy < 2) continue; // off-screen / scrolled out

                var bg = CompositeBackground(fe, baseColor);
                var fgAlpha = fg.Color.A / 255.0 * fg.Opacity * opacity;
                var fgc = Over(fg.Color, fgAlpha, bg);
                var ratio = ContrastRatio(fgc, bg);
                checkedCount++;
                var what = $"{el.GetType().Name} '{Trim(text)}' fg={Hex(fg.Color)}*{fg.Opacity * opacity:F2} -> {Hex(fgc)} on {Hex(bg)} = {ratio:F2}:1";
                if (ratio < min) { min = ratio; minWhat = what; }
                if (ratio < MinContrast) { fail++; Contrast.Add($"[{shot}] FAIL {what}"); }
                else if (ratio < WarnContrast) { warn++; Contrast.Add($"[{shot}] warn {what}"); }
            }
        }
        Contrast.Add($"[{shot}] checked={checkedCount} warn={warn} fail={fail} min={(checkedCount > 0 ? min : 0):F2} ({minWhat})");
        Log.Add($"contrast {shot}: checked={checkedCount} warn={warn} fail={fail} min={(checkedCount > 0 ? min : 0):F2}");
        if (checkedCount < 5) Fail($"contrast {shot}: only {checkedCount} text elements found");
        if (fail > 0) Fail($"contrast {shot}: {fail} element(s) below {MinContrast}:1, worst {minWhat}");
    }

    private static string Trim(string s) { s = s.Replace("\n", " ").Replace("\r", ""); return s.Length > 24 ? s[..24] + "…" : s; }
    private static string Hex(Color c) => $"#{c.A:X2}{c.R:X2}{c.G:X2}{c.B:X2}";

    private static bool EffectivelyVisible(FrameworkElement fe, out double opacity)
    {
        opacity = 1;
        for (DependencyObject? d = fe; d != null; d = VisualTreeHelper.GetParent(d))
        {
            if (d is UIElement u)
            {
                if (u.Visibility != Visibility.Visible) return false;
                opacity *= u.Opacity;
                if (opacity < 0.05) return false;
            }
            if (d is Control c && !c.IsEnabled) return false; // disabled text is allowed to be dim
        }
        return true;
    }

    private static Color CompositeBackground(FrameworkElement fe, Color baseColor)
    {
        // Collect background layers from the element outward until an opaque one, then paint them back-to-front.
        var layers = new List<(Color c, double a)>();
        for (var d = VisualTreeHelper.GetParent(fe); d != null; d = VisualTreeHelper.GetParent(d))
        {
            Brush? b = d switch
            {
                Border x => x.Background,
                Panel x => x.Background,
                ContentPresenter x => x.Background,
                _ => null,
            };
            (Color c, double a)? layer = b switch
            {
                SolidColorBrush s => (s.Color, s.Color.A / 255.0 * s.Opacity),
                AcrylicBrush ac => (ac.FallbackColor, ac.FallbackColor.A / 255.0),
                _ => null,
            };
            if (layer is { } l && l.a > 0.004)
            {
                layers.Add(l);
                if (l.a >= 0.995) break;
            }
        }
        var res = baseColor;
        for (int i = layers.Count - 1; i >= 0; i--) res = Over(layers[i].c, layers[i].a, res);
        return res;
    }

    private static Color Over(Color top, double alpha, Color under)
    {
        alpha = Math.Clamp(alpha, 0, 1); // callers pass the full alpha (colour A x brush opacity x element opacity)
        byte mix(byte t, byte u) => (byte)Math.Round(t * alpha + u * (1 - alpha));
        return Color.FromArgb(255, mix(top.R, under.R), mix(top.G, under.G), mix(top.B, under.B));
    }

    private static double Lum(Color c)
    {
        static double ch(byte v) { var s = v / 255.0; return s <= 0.03928 ? s / 12.92 : Math.Pow((s + 0.055) / 1.055, 2.4); }
        return 0.2126 * ch(c.R) + 0.7152 * ch(c.G) + 0.0722 * ch(c.B);
    }

    private static double ContrastRatio(Color a, Color b)
    {
        double la = Lum(a), lb = Lum(b);
        return (Math.Max(la, lb) + 0.05) / (Math.Min(la, lb) + 0.05);
    }

    private static IEnumerable<DependencyObject> Descendants(DependencyObject root)
    {
        var stack = new Stack<DependencyObject>();
        stack.Push(root);
        while (stack.Count > 0)
        {
            var d = stack.Pop();
            yield return d;
            int n = VisualTreeHelper.GetChildrenCount(d);
            for (int i = 0; i < n; i++) stack.Push(VisualTreeHelper.GetChild(d, i));
        }
    }

    private static T? FindDescendant<T>(DependencyObject? root) where T : DependencyObject
        => root == null ? null : Descendants(root).OfType<T>().FirstOrDefault();

    // ---------------------------------------------------------------- Win32

    [StructLayout(LayoutKind.Sequential)] private struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    private struct BITMAPINFO
    {
        public uint biSize; public int biWidth, biHeight; public ushort biPlanes, biBitCount;
        public uint biCompression, biSizeImage; public int biXPelsPerMeter, biYPelsPerMeter; public uint biClrUsed, biClrImportant;
        public uint bmiColors;
    }
    [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("dwmapi.dll")] private static extern int DwmGetWindowAttribute(IntPtr h, int attr, out RECT r, int size);
    [DllImport("user32.dll")] private static extern IntPtr GetDC(IntPtr h);
    [DllImport("user32.dll")] private static extern int ReleaseDC(IntPtr h, IntPtr dc);
    [DllImport("gdi32.dll")] private static extern IntPtr CreateCompatibleDC(IntPtr dc);
    [DllImport("gdi32.dll")] private static extern IntPtr CreateCompatibleBitmap(IntPtr dc, int w, int h);
    [DllImport("gdi32.dll")] private static extern IntPtr SelectObject(IntPtr dc, IntPtr o);
    [DllImport("gdi32.dll")] private static extern bool BitBlt(IntPtr dst, int x, int y, int w, int h, IntPtr src, int sx, int sy, int rop);
    [DllImport("gdi32.dll")] private static extern int GetDIBits(IntPtr dc, IntPtr bmp, uint start, uint lines, [Out] byte[] bits, ref BITMAPINFO bi, uint usage);
    [DllImport("gdi32.dll")] private static extern bool DeleteObject(IntPtr o);
    [DllImport("gdi32.dll")] private static extern bool DeleteDC(IntPtr dc);
}
