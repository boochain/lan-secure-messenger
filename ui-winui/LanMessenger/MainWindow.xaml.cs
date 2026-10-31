using System.Runtime.InteropServices;
using H.NotifyIcon;
using LanMessenger.Core.ViewModels;
using LanMessenger.Services;
using LanMessenger.Views;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;

namespace LanMessenger;

public sealed partial class MainWindow : Window
{
    public MainViewModel VM => App.VM;
    private TaskbarIcon? _tray;
    private bool _exiting;
    private bool _active = true;
    private bool _visible = true;
    private bool _firstRunShown;
    private readonly DispatcherTimer _statusTimer = new() { Interval = TimeSpan.FromSeconds(6) };

    public MainWindow()
    {
        InitializeComponent();
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(AppTitleBar);
        try { ThemeService.Attach(this, RootGrid); }
        catch (Exception e) { CrashLog.Write("theme attach", e); } // theming must never stop the app from starting
        ApplyBackdrop();
        ThemeService.Changed += ApplyBackdrop;

        try
        {
            AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory, "Assets", "app.ico"));
            // Fit small screens (e.g. 1024x768 / 125% scaling laptops): clamp to the work area and center.
            var wa = DisplayArea.GetFromWindowId(AppWindow.Id, DisplayAreaFallback.Primary).WorkArea;
            int w = Math.Min(1100, Math.Max(640, wa.Width - 40)), h = Math.Min(740, Math.Max(480, wa.Height - 40));
            var rect = new Windows.Graphics.RectInt32(wa.X + (wa.Width - w) / 2, wa.Y + (wa.Height - h) / 2, w, h);
            // remember size + position (Windows guidelines: restore the last placement if it is still on a display)
            var pr = UiPrefs.Current;
            if (pr.X != int.MinValue && pr.W >= 640 && pr.H >= 480 && Environment.GetEnvironmentVariable("LSM_UI_SMOKE") is not { Length: > 0 })
            {
                var saved = new Windows.Graphics.RectInt32(pr.X, pr.Y, pr.W, pr.H);
                var da = DisplayArea.GetFromRect(saved, DisplayAreaFallback.None);
                if (da != null) { var a = da.WorkArea; saved.Width = Math.Min(saved.Width, a.Width); saved.Height = Math.Min(saved.Height, a.Height); rect = saved; }
            }
            AppWindow.MoveAndResize(rect);
            if (pr.Maximized && AppWindow.Presenter is OverlappedPresenter op0 && Environment.GetEnvironmentVariable("LSM_UI_SMOKE") is not { Length: > 0 }) op0.Maximize();
            AppWindow.Closing += AppWindow_Closing;
        }
        catch (Exception e) { CrashLog.Write("appwindow", e); }

        Activated += (_, e) =>
        {
            _active = e.WindowActivationState != WindowActivationState.Deactivated;
            // Inactive title: secondary text brush (theme-aware), never a fixed colour.
            AppTitleText.Style = (Style)Application.Current.Resources[_active ? "TitleBarActiveTextStyle" : "TitleBarInactiveTextStyle"];
            if (ConnectionStatusText != null)
                ConnectionStatusText.Style = (Style)Application.Current.Resources[_active ? "TitleBarActiveTextStyle" : "TitleBarInactiveTextStyle"];
            if (_active) VM.OnWindowActivated();
        };

        VM.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(MainViewModel.StatusMessage) || e.PropertyName == nameof(MainViewModel.StatusIsError))
            {
                StatusBar.Severity = VM.StatusIsError ? InfoBarSeverity.Error : InfoBarSeverity.Informational;
                _statusTimer.Stop();
                if (!VM.StatusIsError) _statusTimer.Start();
            }
            if (e.PropertyName == nameof(MainViewModel.OnlineCount) && _tray != null)
                _tray.ToolTipText = $"LAN Secure Messenger · {VM.OnlineCount} 人在线";
        };
        _statusTimer.Tick += (_, _) => { _statusTimer.Stop(); VM.HasStatus = false; };
        VM.FirstRunRequested += () => DispatcherQueue.TryEnqueue(async () => await ShowFirstRunAsync());

        Nav.SelectedItem = Nav.MenuItems[0];
        NavigateTo("chat");
        CreateTray();
    }

    public bool IsActiveAndVisible => _active && _visible;
    public Page? CurrentPage => ContentFrame.Content as Page;
    public FrameworkElement Root => RootGrid;

    /// <summary>True when Mica or desktop Acrylic is drawing the window background.</summary>
    public bool BackdropActive { get; private set; }

    private void ApplyBackdrop()
    {
        // High Contrast: no translucent materials, solid system window colour behind everything.
        if (ThemeService.HighContrast || Environment.GetEnvironmentVariable("LSM_NO_BACKDROP") == "1")
        {
            SystemBackdrop = null;
            BackdropActive = false;
        }
        else if (SystemBackdrop == null)
        {
            try
            {
                if (Microsoft.UI.Composition.SystemBackdrops.MicaController.IsSupported())
                    SystemBackdrop = new MicaBackdrop();
                else if (Microsoft.UI.Composition.SystemBackdrops.DesktopAcrylicController.IsSupported())
                    SystemBackdrop = new DesktopAcrylicBackdrop();
                BackdropActive = SystemBackdrop != null;
            }
            catch (Exception e)
            {
                CrashLog.Write("backdrop", e);
                SystemBackdrop = null;
                BackdropActive = false;
            }
        }
        // MicaBackdrop/DesktopAcrylicBackdrop take their Light/Dark tint from this window's root ActualTheme, so they
        // follow RequestedTheme and live changes; the fallback border covers every case without a backdrop.
        FallbackBackground.Visibility = BackdropActive ? Visibility.Collapsed : Visibility.Visible;
    }

    private void CreateTray()
    {
        try
        {
            var menu = new MenuFlyout();
            var show = new MenuFlyoutItem { Text = "显示主窗口" };
            show.Click += (_, _) => ShowAndFocus();
            var diag = new MenuFlyoutItem { Text = "诊断" };
            diag.Click += (_, _) => { ShowAndFocus(); NavigateTo("diag"); };
            var exit = new MenuFlyoutItem { Text = "退出（同时停止后台服务）" };
            exit.Click += async (_, _) => await ExitFullyAsync();
            menu.Items.Add(show);
            menu.Items.Add(diag);
            menu.Items.Add(new MenuFlyoutSeparator());
            menu.Items.Add(exit);

            _tray = new TaskbarIcon
            {
                ToolTipText = "LAN Secure Messenger",
                IconSource = new BitmapImage(new Uri(Path.Combine(AppContext.BaseDirectory, "Assets", "app.ico"))),
                ContextMenuMode = ContextMenuMode.PopupMenu,
                ContextFlyout = menu,
                NoLeftClickDelay = true,
                LeftClickCommand = new CommunityToolkit.Mvvm.Input.RelayCommand(ShowAndFocus),
            };
            _tray.ForceCreate(false);
        }
        catch (Exception e)
        {
            CrashLog.Write("tray", e);
            _tray = null;
        }
    }

    /// <summary>Restore a saved placement (same rule as startup: only if it is still on a display). UI smoke uses it too.</summary>
    internal bool ApplyPlacement(UiPrefs pr)
    {
        if (pr.X == int.MinValue || pr.W < 640 || pr.H < 480) return false;
        var saved = new Windows.Graphics.RectInt32(pr.X, pr.Y, pr.W, pr.H);
        var da = DisplayArea.GetFromRect(saved, DisplayAreaFallback.None);
        if (da == null) return false;
        var a = da.WorkArea; saved.Width = Math.Min(saved.Width, a.Width); saved.Height = Math.Min(saved.Height, a.Height);
        AppWindow.MoveAndResize(saved);
        return true;
    }

    internal void SavePlacement()
    {
        try
        {
            var pr = UiPrefs.Current;
            pr.Maximized = AppWindow.Presenter is OverlappedPresenter p && p.State == OverlappedPresenterState.Maximized;
            if (!pr.Maximized && !(AppWindow.Presenter is OverlappedPresenter q && q.State == OverlappedPresenterState.Minimized))
            { pr.X = AppWindow.Position.X; pr.Y = AppWindow.Position.Y; pr.W = AppWindow.Size.Width; pr.H = AppWindow.Size.Height; }
            pr.Save();
        }
        catch (Exception e) { CrashLog.Write("placement", e); }
    }

    private void AppWindow_Closing(AppWindow sender, AppWindowClosingEventArgs args)
    {
        SavePlacement();
        if (_exiting) return;
        if (_tray != null && UiPrefs.Current.CloseToTray)
        {
            // Close = minimise to tray (setting 关闭窗口时最小化到托盘); the daemon keeps running and receiving.
            args.Cancel = true;
            sender.Hide();
            _visible = false;
            if (!UiPrefs.Current.TrayNoticeShown)
            {
                // first time only: say where the window went and how to quit for real
                UiPrefs.Current.TrayNoticeShown = true; UiPrefs.Current.Save();
                try { Toasts.Show("LAN Secure Messenger 仍在运行", "窗口已最小化到通知区域，继续接收消息。右键托盘图标选择“退出”可完全退出；可在 设置 中关闭此行为。", ""); } catch { }
            }
            return;
        }
        args.Cancel = true;
        _ = ExitFullyAsync();
    }

    internal bool SmokeHasTray => _tray != null;

    public void ShowAndFocus()
    {
        try
        {
            AppWindow.Show();
            _visible = true;
            if (AppWindow.Presenter is OverlappedPresenter p && p.State == OverlappedPresenterState.Minimized) p.Restore();
            Activate();
            SetForegroundWindow(WinRT.Interop.WindowNative.GetWindowHandle(this));
        }
        catch (Exception e) { CrashLog.Write("show", e); }
    }

    public async Task ExitFullyAsync()
    {
        try { await VM.StopAsync(shutdownDaemon: true); } catch { }
        ExitApp();
    }

    public void ExitApp()
    {
        _exiting = true;
        try { _tray?.Dispose(); } catch { }
        Toasts.Shutdown();
        try { Close(); } catch { }
        Application.Current.Exit();
    }

    public void NavigateTo(string tag)
    {
        Type page = tag switch
        {
            "groups" => typeof(GroupsPage),
            "transfers" => typeof(TransfersPage),
            "settings" => typeof(SettingsPage),
            "diag" => typeof(DiagnosticsPage),
            _ => typeof(ChatPage),
        };
        foreach (var item in Nav.MenuItems.Concat(Nav.FooterMenuItems).OfType<NavigationViewItem>())
        {
            if ((string)item.Tag == tag && !ReferenceEquals(Nav.SelectedItem, item)) { Nav.SelectedItem = item; break; }
        }
        if (ContentFrame.CurrentSourcePageType != page) ContentFrame.Navigate(page);
    }

    public void OpenConversation(string id)
    {
        NavigateTo("chat");
        VM.SelectConversation(id);
    }

    private void Nav_SelectionChanged(NavigationView sender, NavigationViewSelectionChangedEventArgs args)
    {
        if (args.SelectedItem is NavigationViewItem item && item.Tag is string tag) NavigateTo(tag);
    }

    private void StatusBar_Closed(InfoBar sender, InfoBarClosedEventArgs args) => VM.HasStatus = false;

    private async Task ShowFirstRunAsync()
    {
        if (_firstRunShown) return;
        _firstRunShown = true;
        if (Environment.GetEnvironmentVariable("LSM_UI_SMOKE") is { Length: > 0 }) return;
        var dlg = ThemeService.Prepare(new FirstRunDialog { XamlRoot = Content.XamlRoot });
        await dlg.ShowAsync();
    }

    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr hWnd);
}
