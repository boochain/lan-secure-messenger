using System.Runtime.InteropServices;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Text;
using LanMessenger.Core.ViewModels;
using LanMessenger.Views;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Input;
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
            var relayEnv = Environment.GetEnvironmentVariable("LSM_UI_SMOKE_RELAY");   // host|port|passphrase of the CI test relay
            if (!string.IsNullOrWhiteSpace(relayEnv)) await RelayOnAsync(relayEnv);
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
                    // the LAN peer (Bob) for the regular DM shots; relay peers get their own shot below
                    var dm = App.VM.Conversations.FirstOrDefault(c => c.IsDm && !c.ViaServer && c.Title.StartsWith("Bob")) ?? App.VM.Conversations.FirstOrDefault(c => c.IsDm);
                    if (dm != null)
                    {
                        App.VM.SelectedConversation = dm;
                        await Task.Delay(3000);
                        await ShotAsync(w, dir, "page-chat-dm", label, theme);
                        Log.Add($"chat-dm: title={dm.Title}, messages={App.VM.Messages.Count}, images={App.VM.Messages.Count(m => m.IsImage)}, " +
                                $"incoming={App.VM.Messages.Count(m => m.IsIncoming)}, outgoing={App.VM.Messages.Count(m => m.IsOutgoing)}");
                        if (!App.VM.Messages.Any(m => m.IsImage) || !App.VM.Messages.Any(m => m.IsIncoming && m.IsText) || !App.VM.Messages.Any(m => m.IsOutgoing))
                            Fail("DM is missing the peer's text, the image preview or our reply");

                        // Popup layer: the "更多" menu flyout; then the top of the conversation (peer's first message).
                        if (w.CurrentPage is ChatPage cp)
                        {
                            cp.ScrollMessagesToTopForSmoke();
                            await Task.Delay(1500);
                            await ShotAsync(w, dir, "page-chat-dm-top", label, theme);
                            cp.OpenMoreMenuForSmoke();
                            await Task.Delay(1200);
                            await ShotAsync(w, dir, "popup-menu", label, theme);
                            cp.CloseMoreMenuForSmoke();
                            await Task.Delay(600);
                            await ShortcutChecksAsync(w, cp, dir, label, theme);
                            // contact details dialog (fingerprint / latency moved out of the header)
                            _ = cp.SmokeShowDetailsAsync(); await Task.Delay(1500);
                            await ShotAsync(w, dir, "dialog-details", label, theme);
                            Check(!App.VM.SelectedSubtitle.Contains("指纹") && !App.VM.SelectedSubtitle.Contains(" ms"), $"chat header subtitle has no fingerprint/latency ('{App.VM.SelectedSubtitle}')");
                            { var open = DialogOpen(w); await KeysAsync(VK_ESCAPE); await Task.Delay(600); Check(open && !DialogOpen(w), $"Esc closes the contact-details dialog (open before={open})"); }
                            await ContextMenuChecksAsync(w, cp, dir, label, theme);
                            await ComposerKeysCheckAsync(cp);
                            if (!string.IsNullOrWhiteSpace(relayEnv)) await RelayChatChecksAsync(w, dir, label, theme);
                        }
                    }
                    else Fail("no DM conversation with the peer");
                }
                if (tag == "transfers" && !string.IsNullOrWhiteSpace(relayEnv))
                {
                    var paused = App.VM.Transfers.FirstOrDefault(t => t.IsPaused);
                    Check(paused != null && paused.StateText == "连接中断，重连后自动续传",
                          $"paused transfer shows 连接中断，重连后自动续传 ({string.Join("; ", App.VM.Transfers.Select(t => $"{t.Name}={t.StateText}"))})");
                }
                if (tag == "settings" && !string.IsNullOrWhiteSpace(relayEnv) && w.CurrentPage is SettingsPage sp)
                {
                    sp.ShowRelayCardForSmoke();
                    await Task.Delay(1500);
                    await ShotAsync(w, dir, "page-settings-relay", label, theme);
                    Check(App.VM.RelayHasPassphrase && App.VM.RelayPassphrasePlaceholder.StartsWith("已设置") && App.VM.EditRelayPassphrase.Length == 0,
                          $"relay passphrase is write-only: field empty, placeholder '{App.VM.RelayPassphrasePlaceholder}'");
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
            await TrayCloseCheckAsync(w);
            await WindowMemoryCheckAsync(w);

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

    /// <summary>Turns the relay on through the settings view model (same path as the 保存并连接 button) and waits for 已连接中转服务器.</summary>
    private static async Task RelayOnAsync(string env)
    {
        var f = env.Split('|');
        App.VM.EditRelayEnabled = true;
        App.VM.EditRelayHost = f[0];
        App.VM.EditRelayPort = double.Parse(f[1]);
        App.VM.EditRelayPassphrase = f.Length > 2 ? f[2] : "";
        await App.VM.SaveRelayCommand.ExecuteAsync(null);
        for (int i = 0; i < 60 && App.VM.RelayState != "online"; i++) await Task.Delay(500);
        Check(App.VM.RelayState == "online" && App.VM.RelayStateText == "已连接中转服务器", $"relay toggled on: state={App.VM.RelayState} '{App.VM.RelayStateText}' {App.VM.RelayDetail}");
    }

    /// <summary>A relay peer is in the same list with the 经服务器 chip; its chat header reads 在线 · 已验证身份 · 经服务器.</summary>
    private static async Task RelayChatChecksAsync(MainWindow w, string dir, string label, ElementTheme theme)
    {
        ConversationItem? carol = null;
        for (int i = 0; i < 40 && carol == null; i++)
        {
            carol = App.VM.Conversations.FirstOrDefault(c => c.IsDm && c.ViaServer && c.Title.StartsWith("Carol"));
            if (carol == null) await Task.Delay(500);
        }
        Check(carol != null && App.VM.Peers.Any(p => p.Fingerprint == carol.PeerFingerprint && p.ViaRelay),
              "relay peer badge: Carol-中转 is in the conversation list with ViaServer (经服务器)");
        if (carol == null) return;
        App.VM.SelectedConversation = carol;
        await Task.Delay(2500);
        await ShotAsync(w, dir, "page-chat-relay", label, theme);
        Check(App.VM.SelectedSubtitle == "在线 · 已验证身份 · 经服务器", $"chat header via relay: '{App.VM.SelectedSubtitle}'");
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

    // ---------------------------------------------------------------- runtime checks (real input via SendInput)

    private static void Check(bool ok, string what) { Log.Add((ok ? "PASS: " : "FAIL: ") + what); if (!ok) _ok = false; }

    private static bool FocusWithin(MainWindow w, FrameworkElement target)
    {
        for (var d = FocusManager.GetFocusedElement(w.Content.XamlRoot) as DependencyObject; d != null; d = VisualTreeHelper.GetParent(d))
            if (ReferenceEquals(d, target)) return true;
        return false;
    }

    private static async Task KeysAsync(params ushort[] chord)
    {
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(App.MainWin!);
        SetForegroundWindow(hwnd);
        await Task.Delay(150);
        var ins = new List<INPUT>();
        foreach (var k in chord) ins.Add(Key(k, false));
        for (int i = chord.Length - 1; i >= 0; i--) ins.Add(Key(chord[i], true));
        SendInput((uint)ins.Count, ins.ToArray(), Marshal.SizeOf<INPUT>());
        await Task.Delay(700);
    }
    private static INPUT Key(ushort vk, bool up) => new() { type = 1, U = new InputUnion { ki = new KEYBDINPUT { wVk = vk, dwFlags = up ? 2u : 0u } } };
    private const ushort VK_RETURN = 0x0D, VK_CONTROL = 0x11, VK_SHIFT = 0x10, VK_TAB = 0x09, VK_ESCAPE = 0x1B, VK_F = 0x46, VK_N = 0x4E;

    /// <summary>Ctrl+F → search box, Ctrl+N → add-peer box, Esc clears the search, Ctrl+Tab / Ctrl+Shift+Tab cycle conversations.</summary>
    private static async Task ShortcutChecksAsync(MainWindow w, ChatPage cp, string dir, string label, ElementTheme theme)
    {
        try
        {
            cp.SmokeFocusComposer(); await Task.Delay(300);
            await KeysAsync(VK_CONTROL, VK_F);
            Check(FocusWithin(w, cp.SmokeSearchBox), "shortcut Ctrl+F focuses the search box");
            cp.SmokeSearchText = "zz-smoke"; await Task.Delay(300);
            await KeysAsync(VK_ESCAPE);
            Check(string.IsNullOrEmpty(cp.SmokeSearchText), $"shortcut Esc clears the search (text='{cp.SmokeSearchText}')");
            await KeysAsync(VK_CONTROL, VK_N);
            Check(FocusWithin(w, cp.SmokeAddPeerBox), "shortcut Ctrl+N opens the add-contact flyout and focuses its box");
            await ShotAsync(w, dir, "popup-add-peer", label, theme);
            cp.SmokeCloseAddPeer(); await Task.Delay(600);
            cp.SmokeFocusComposer(); await Task.Delay(300);
            var before = App.VM.SelectedConversation;
            if (App.VM.Conversations.Count >= 2)
            {
                await KeysAsync(VK_CONTROL, VK_TAB);
                var next = App.VM.SelectedConversation;
                Check(next != null && !ReferenceEquals(next, before), $"shortcut Ctrl+Tab selects the next conversation ({before?.Title} -> {next?.Title})");
                await KeysAsync(VK_CONTROL, VK_SHIFT, VK_TAB);
                Check(ReferenceEquals(App.VM.SelectedConversation, before), $"shortcut Ctrl+Shift+Tab goes back ({App.VM.SelectedConversation?.Title})");
            }
            else Check(false, "Ctrl+Tab needs at least 2 conversations");
            App.VM.SelectedConversation = before;
            await Task.Delay(1500);
        }
        catch (Exception e) { Check(false, "shortcuts: " + e.Message); }
    }

    private static bool DialogOpen(MainWindow w) =>
        VisualTreeHelper.GetOpenPopupsForXamlRoot(w.Content.XamlRoot).Any(p => p.Child is ContentDialog || FindDescendant<ContentDialog>(p.Child) != null);

    /// <summary>Shift+Enter inserts a new line (nothing sent); Enter sends and clears the composer.</summary>
    private static async Task ComposerKeysCheckAsync(ChatPage cp)
    {
        try
        {
            cp.SmokeFocusComposer(); cp.SmokeComposerText = "smoke-enter"; await Task.Delay(300);
            int before = App.VM.Messages.Count(m => m.IsOutgoing);
            await KeysAsync(VK_SHIFT, VK_RETURN); await Task.Delay(500);
            var t = cp.SmokeComposerText;
            Check((t.Contains('\r') || t.Contains('\n')) && App.VM.Messages.Count(m => m.IsOutgoing) == before, "Shift+Enter inserts a new line, nothing sent");
            await KeysAsync(VK_RETURN);
            for (int i = 0; i < 20 && App.VM.Messages.Count(m => m.IsOutgoing) == before; i++) await Task.Delay(250);
            Check(App.VM.Messages.Count(m => m.IsOutgoing) == before + 1 && string.IsNullOrEmpty(cp.SmokeComposerText), $"Enter sends and clears the composer (outgoing {before} -> {App.VM.Messages.Count(m => m.IsOutgoing)})");
        }
        catch (Exception e) { Check(false, "composer keys: " + e.Message); }
    }

    /// <summary>Close (WM_CLOSE, as the caption X) hides to the tray while the app keeps running; the tray restores it.</summary>
    private static async Task TrayCloseCheckAsync(MainWindow w)
    {
        try
        {
            if (!w.SmokeHasTray) { Check(false, "close hides to the tray: no tray icon was created"); return; }
            var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(w);
            PostMessage(hwnd, 0x0010 /* WM_CLOSE */, IntPtr.Zero, IntPtr.Zero); await Task.Delay(1500);
            Check(!IsWindowVisible(hwnd) && App.VM.IsConnected, $"close hides to the tray, app keeps running (visible={IsWindowVisible(hwnd)}, connected={App.VM.IsConnected})");
            w.ShowAndFocus(); await Task.Delay(1500);
            Check(IsWindowVisible(hwnd), "tray: show restores the window");
        }
        catch (Exception e) { Check(false, "tray close: " + e.Message); }
    }

    private static async Task RightClickAsync(MainWindow w, FrameworkElement fe)
    {
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(w);
        SetForegroundWindow(hwnd);
        var scale = fe.XamlRoot.RasterizationScale;
        var p = fe.TransformToVisual(null).TransformPoint(new Point(Math.Min(40, fe.ActualWidth / 2), fe.ActualHeight / 2));
        var pt = new POINT { X = (int)(p.X * scale), Y = (int)(p.Y * scale) };
        ClientToScreen(hwnd, ref pt);
        SetCursorPos(pt.X, pt.Y);
        await Task.Delay(200);
        var ins = new[] { new INPUT { type = 0, U = new InputUnion { mi = new MOUSEINPUT { dwFlags = 0x0008 } } }, new INPUT { type = 0, U = new InputUnion { mi = new MOUSEINPUT { dwFlags = 0x0010 } } } };
        SendInput(2, ins, Marshal.SizeOf<INPUT>());
        await Task.Delay(1200);
    }

    private static List<string> OpenMenuItems(MainWindow w)
    {
        var items = new List<string>();
        foreach (var pop in VisualTreeHelper.GetOpenPopupsForXamlRoot(w.Content.XamlRoot))
            if (pop.Child != null) items.AddRange(Descendants(pop.Child).OfType<MenuFlyoutItem>().Select(i => i.Text));
        return items;
    }

    /// <summary>Real right-clicks on a text message and on the DM row open the expected menus.</summary>
    private static async Task ContextMenuChecksAsync(MainWindow w, ChatPage cp, string dir, string label, ElementTheme theme)
    {
        try
        {
            var msgRow = Descendants(cp.SmokeMessageList).OfType<ListViewItem>()
                .FirstOrDefault(i => (i.Content ?? i.DataContext) is LanMessenger.Core.ViewModels.MessageItem m && m.IsText && !m.IsSystem && i.ActualHeight > 0 && Center(i).Y > 0 && Center(i).Y < ((FrameworkElement)w.Content).ActualHeight);
            if (msgRow == null) Check(false, "right-click: no visible text message row");
            else
            {
                await RightClickAsync(w, msgRow);
                var items = OpenMenuItems(w);
                Check(items.Contains("复制") && items.Contains("删除（仅本机）…"), "right-click on a message opens 复制 / 删除（仅本机）… [" + string.Join(", ", items) + "]");
                await ShotAsync(w, dir, "popup-message-menu", label, theme);
                await KeysAsync(VK_ESCAPE);
            }
            var dmRow = Descendants(cp.SmokeConvList).OfType<ListViewItem>().FirstOrDefault(i => (i.Content ?? i.DataContext) is LanMessenger.Core.ViewModels.ConversationItem c && c.IsDm);
            if (dmRow == null) Check(false, "right-click: no DM row");
            else
            {
                await RightClickAsync(w, dmRow);
                var items = OpenMenuItems(w);
                Check(items.Any(t => t is "信任" or "取消信任") && items.Any(t => t.StartsWith("屏蔽") || t == "取消屏蔽") && items.Any(t => t.Contains("免打扰")),
                    "right-click on a contact opens trust / mute / block [" + string.Join(", ", items) + "]");
                await KeysAsync(VK_ESCAPE);
            }
            Check(OpenMenuItems(w).Count == 0, "Esc closes the context menu");
        }
        catch (Exception e) { Check(false, "context menus: " + e.Message); }
    }

    /// <summary>Window memory: move/resize, persist, read back from disk, move elsewhere, re-apply → same placement.</summary>
    private static async Task WindowMemoryCheckAsync(MainWindow w)
    {
        try
        {
            var orig = new Windows.Graphics.RectInt32(w.AppWindow.Position.X, w.AppWindow.Position.Y, w.AppWindow.Size.Width, w.AppWindow.Size.Height);
            var wa = DisplayArea.GetFromWindowId(w.AppWindow.Id, DisplayAreaFallback.Primary).WorkArea;
            var want = new Windows.Graphics.RectInt32(wa.X + 37, wa.Y + 29, Math.Min(900, wa.Width - 60), Math.Min(640, wa.Height - 60));
            w.AppWindow.MoveAndResize(want); await Task.Delay(800);
            w.SavePlacement();
            var disk = UiPrefs.ReadFromDisk();
            Check(disk.X == want.X && disk.Y == want.Y && disk.W == want.Width && disk.H == want.Height,
                $"window memory: saved {disk.X},{disk.Y} {disk.W}x{disk.H} (want {want.X},{want.Y} {want.Width}x{want.Height})");
            w.AppWindow.MoveAndResize(new Windows.Graphics.RectInt32(wa.X + 5, wa.Y + 5, 700, 500)); await Task.Delay(800);
            var applied = w.ApplyPlacement(disk); await Task.Delay(800);
            var now = (w.AppWindow.Position.X, w.AppWindow.Position.Y, w.AppWindow.Size.Width, w.AppWindow.Size.Height);
            Check(applied && now == (want.X, want.Y, want.Width, want.Height), $"window memory: restored to {now}");
            var off = new UiPrefs { X = -40000, Y = -40000, W = 900, H = 640 };
            Check(!w.ApplyPlacement(off), "window memory: an off-screen placement is ignored");
            w.AppWindow.MoveAndResize(orig);
        }
        catch (Exception e) { Check(false, "window memory: " + e.Message); }
    }

    // ---------------------------------------------------------------- capture + checks

    private static async Task ShotAsync(MainWindow w, string dir, string name, string label, ElementTheme theme)
    {
        var file = Path.Combine(dir, $"{name}-{label}.png");
        await ParkPointerAsync(w);
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
        RecordAuditHints(w, dir, file);
    }

    /// <summary>Before every shot: move the mouse to a neutral spot outside the window (no hover row, no hover tooltip)
    /// and close any open ToolTip (a keyboard-focus tooltip such as Ctrl+F). Menus/flyouts being shot stay open.</summary>
    private static async Task ParkPointerAsync(MainWindow w)
    {
        try
        {
            // neutral spot: the empty middle of the navigation rail (no hover state, no tooltip). A real mouse-move
            // input is sent there so XAML raises PointerExited on whatever row the pointer was over (a bare
            // SetCursorPos to outside the window leaves the last row in its hover state).
            var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(w);
            if (w.Content is FrameworkElement root && root.XamlRoot != null)
            {
                var sc = root.XamlRoot.RasterizationScale;
                var pt = new POINT { X = (int)(24 * sc), Y = (int)(root.ActualHeight * 0.55 * sc) };
                ClientToScreen(hwnd, ref pt);
                SetCursorPos(pt.X + 1, pt.Y);
                await Task.Delay(50);
                var mv = new[] { new INPUT { type = 0, U = new InputUnion { mi = new MOUSEINPUT { dx = -1, dy = 0, dwFlags = 0x0001 /* MOVE */ } } } };
                SendInput(1, mv, Marshal.SizeOf<INPUT>());
            }
            await Task.Delay(400);
            if (w.Content?.XamlRoot is { } xr)
                foreach (var pop in VisualTreeHelper.GetOpenPopupsForXamlRoot(xr))
                    if (pop.Child is ToolTip) pop.IsOpen = false;
            await Task.Delay(200);
        }
        catch (Exception e) { Log.Add("park pointer: " + e.Message); }
    }

    // ---------------------------------------------------------------- colour-audit hints
    // The app knows what is a decorative icon (Tag="lsm-icon": online dots, the verified shield, whose meaning is also
    // given in text) and where an open ContentDialog is (the page behind its scrim is inactive UI, WCAG-exempt).
    // Their screen-pixel rectangles go to audit_ignore.json next to the shots; CI merges it into the audit's ignore file.
    private static readonly Dictionary<string, object> AuditHints = new();
    private static readonly Dictionary<string, string> AuditReasons = new();

    private static void RecordAuditHints(MainWindow w, string dir, string file)
    {
        try
        {
            if (w.Content is not FrameworkElement root || root.XamlRoot == null) return;
            var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(w);
            if (DwmGetWindowAttribute(hwnd, 9 /* DWMWA_EXTENDED_FRAME_BOUNDS */, out RECT r, Marshal.SizeOf<RECT>()) != 0) GetWindowRect(hwnd, out r);
            var origin = new POINT(); ClientToScreen(hwnd, ref origin);
            double ox = origin.X - r.Left, oy = origin.Y - r.Top, sc = root.XamlRoot.RasterizationScale;
            int[] Box(FrameworkElement fe, int pad)
            {
                var p = fe.TransformToVisual(null).TransformPoint(new Point(0, 0));
                return new[] { (int)(ox + p.X * sc) - pad, (int)(oy + p.Y * sc) - pad, (int)(fe.ActualWidth * sc) + 2 * pad, (int)(fe.ActualHeight * sc) + 2 * pad };
            }
            var icons = new List<int[]>();
            void Walk(DependencyObject d)
            {
                if (d is UIElement ue && ue.Visibility != Visibility.Visible) return;
                if (d is FrameworkElement fe && fe.Tag as string == "lsm-icon" && fe.ActualWidth > 0 && fe.ActualHeight > 0) icons.Add(Box(fe, 2));
                int n = VisualTreeHelper.GetChildrenCount(d);
                for (int i = 0; i < n; i++) Walk(VisualTreeHelper.GetChild(d, i));
            }
            Walk(root);
            int[]? only = null;
            foreach (var pop in VisualTreeHelper.GetOpenPopupsForXamlRoot(root.XamlRoot))
            {
                var dlg = pop.Child as ContentDialog ?? FindDescendant<ContentDialog>(pop.Child);
                if (dlg == null) continue;
                if (FindNamed(dlg, "BackgroundElement") is FrameworkElement bg && bg.ActualWidth > 0) only = Box(bg, 0);
            }
            var key = Path.GetFileName(file);
            if (only != null)
            {
                AuditHints[key] = new Dictionary<string, object> { ["ignore"] = icons, ["only"] = only };
                AuditReasons[key] = "modal ContentDialog: only the dialog is audited (page behind the scrim is inactive UI, WCAG-exempt); decorative icons inside excluded";
            }
            else if (icons.Count > 0)
            {
                AuditHints[key] = icons;
                AuditReasons[key] = "decorative icons only (Tag=lsm-icon): online dot and verified shield, whose meaning is also given as text; bounds reported by the app";
            }
            var all = new Dictionary<string, object>(AuditHints) { ["_reasons"] = AuditReasons };
            File.WriteAllText(Path.Combine(dir, "audit_ignore.json"), System.Text.Json.JsonSerializer.Serialize(all));
            Log.Add($"audit hints {key}: {icons.Count} icon(s){(only != null ? ", dialog only" : "")}");
        }
        catch (Exception e) { Log.Add("audit hints failed: " + e.Message); }
    }

    private static DependencyObject? FindNamed(DependencyObject root, string name)
    {
        if (root is FrameworkElement fe && fe.Name == name) return root;
        int n = VisualTreeHelper.GetChildrenCount(root);
        for (int i = 0; i < n; i++) { var f = FindNamed(VisualTreeHelper.GetChild(root, i), name); if (f != null) return f; }
        return null;
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

    private static string Trim(string s)
    {
        if (IsGlyph(s)) return "glyph U+" + string.Join(",", s.Select(c => ((int)c).ToString("X4")));
        s = s.Replace("\n", " ").Replace("\r", "");
        return s.Length > 24 ? s[..24] + "…" : s;
    }
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

    private static bool IsGlyph(string? t) => !string.IsNullOrEmpty(t) && t.All(c => c >= '\uE000' && c <= '\uF8FF');

    private static Point Center(FrameworkElement fe)
    {
        try { return fe.TransformToVisual(null).TransformPoint(new Point(fe.ActualWidth / 2, fe.ActualHeight / 2)); }
        catch { return new Point(double.NaN, double.NaN); }
    }

    private static bool Contains(FrameworkElement fe, Point p)
    {
        if (double.IsNaN(p.X) || fe.ActualWidth < 1 || fe.ActualHeight < 1 || fe.Visibility != Visibility.Visible || fe.Opacity < 0.05) return false;
        try
        {
            var b = fe.TransformToVisual(null).TransformBounds(new Rect(0, 0, fe.ActualWidth, fe.ActualHeight));
            return p.X >= b.X && p.X <= b.X + b.Width && p.Y >= b.Y && p.Y <= b.Y + b.Height;
        }
        catch { return false; }
    }

    private static (Color c, double a)? Layer(Brush? b, double opacity = 1) => b switch
    {
        SolidColorBrush s => (s.Color, s.Color.A / 255.0 * s.Opacity * opacity),
        AcrylicBrush ac => (ac.FallbackColor, ac.FallbackColor.A / 255.0 * opacity),
        _ => null,
    };

    private static Color CompositeBackground(FrameworkElement fe, Color baseColor)
    {
        // Painter's algorithm, walking outward: for every ancestor panel first the siblings drawn *behind* the current
        // branch (lower child index, covering the text's centre: e.g. the avatar Ellipse, a TextBox's border element,
        // the coloured circle glyph under an InfoBar icon), then the ancestor's own background. Stop at the first opaque layer.
        var glyph = fe is FontIcon || (fe is TextBlock tb0 && IsGlyph(tb0.Text));
        var center = Center(fe);
        var layers = new List<(Color c, double a)>();
        bool opaque = false;
        void Add((Color c, double a)? l)
        {
            if (opaque || l is not { } v || v.a <= 0.004) return;
            layers.Add(v);
            if (v.a >= 0.995) opaque = true;
        }
        DependencyObject child = fe;
        for (var d = VisualTreeHelper.GetParent(fe); d != null && !opaque; child = d, d = VisualTreeHelper.GetParent(d))
        {
            if (d is Panel panel && child is UIElement childEl)
            {
                int idx = panel.Children.IndexOf(childEl);
                for (int i = idx - 1; i >= 0 && !opaque; i--)
                {
                    if (panel.Children[i] is not FrameworkElement sib || !Contains(sib, center)) continue;
                    switch (sib)
                    {
                        case Microsoft.UI.Xaml.Shapes.Shape sh: Add(Layer(sh.Fill, sh.Opacity)); break;
                        case Border sb: Add(Layer(sb.Background, sb.Opacity)); break;
                        case Panel sp: Add(Layer(sp.Background, sp.Opacity)); break;
                        case TextBlock st when glyph && IsGlyph(st.Text): Add(Layer(st.Foreground, st.Opacity)); break;
                        case FontIcon sf when glyph: Add(Layer(sf.Foreground, sf.Opacity)); break;
                    }
                }
            }
            Brush? b = d switch
            {
                Border x => x.Background,
                Panel x => x.Background,
                ContentPresenter x => x.Background,
                _ => null,
            };
            Add(Layer(b));
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
    [StructLayout(LayoutKind.Sequential)] private struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] private struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)] private struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Explicit)] private struct InputUnion { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
    [StructLayout(LayoutKind.Sequential)] private struct INPUT { public uint type; public InputUnion U; }
    [DllImport("user32.dll", SetLastError = true)] private static extern uint SendInput(uint n, INPUT[] inputs, int size);
    [DllImport("user32.dll")] private static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] private static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] private static extern bool ClientToScreen(IntPtr h, ref POINT p);
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
