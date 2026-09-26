using System.Runtime.InteropServices;
using System.Text.Json;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Windows.UI;
using Windows.UI.ViewManagement;

namespace LanMessenger.Services;

public enum AppThemeMode { System, Light, Dark }

/// <summary>
/// Single owner of the app theme: the user's choice (跟随系统 / 浅色 / 深色, persisted in ui-settings.json, env LSM_THEME
/// overrides it), the window root's RequestedTheme, live system theme / high-contrast changes, the caption buttons of
/// our custom title bar, the DWM frame and the tray (Win32) menu. Everything that is not XAML-themed goes through here.
/// </summary>
public static class ThemeService
{
    private static readonly UISettings _ui = new();
    private static readonly AccessibilitySettings _a11y = new();
    private static FrameworkElement? _root;
    private static Window? _window;

    public static AppThemeMode Mode { get; private set; } = AppThemeMode.System;
    public static event Action? Changed;

    private static string SettingsFile
    {
        get
        {
            var baseDir = Environment.GetEnvironmentVariable("LSM_DATA_DIR");
            var d = string.IsNullOrWhiteSpace(baseDir)
                ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "FREC86", "lan-secure-messenger", "config")
                : Path.Combine(baseDir, "config");
            try { Directory.CreateDirectory(d); } catch { }
            return Path.Combine(d, "ui-settings.json");
        }
    }

    private static AppThemeMode Parse(string? s) => (s ?? "").Trim().ToLowerInvariant() switch
    {
        "light" or "浅色" => AppThemeMode.Light,
        "dark" or "深色" => AppThemeMode.Dark,
        _ => AppThemeMode.System,
    };

    /// <summary>Call from the App constructor, before InitializeComponent.</summary>
    public static void LoadAndApplyToApplication(Application app)
    {
        try
        {
            var env = Environment.GetEnvironmentVariable("LSM_THEME");
            if (!string.IsNullOrWhiteSpace(env)) Mode = Parse(env);
            else if (File.Exists(SettingsFile))
            {
                using var doc = JsonDocument.Parse(File.ReadAllText(SettingsFile));
                if (doc.RootElement.TryGetProperty("theme", out var t)) Mode = Parse(t.GetString());
            }
        }
        catch (Exception e) { CrashLog.Write("theme load", e); }
        // A fixed choice is applied app-wide so popups, flyouts and dialogs match. "System" leaves the framework default,
        // which follows Windows (AppsUseLightTheme) including live changes.
        try
        {
            if (Mode == AppThemeMode.Light) app.RequestedTheme = ApplicationTheme.Light;
            else if (Mode == AppThemeMode.Dark) app.RequestedTheme = ApplicationTheme.Dark;
        }
        catch (Exception e) { CrashLog.Write("theme app", e); }
        SetWin32MenuMode();
    }

    public static void Attach(Window window, FrameworkElement root)
    {
        _window = window;
        _root = root;
        root.RequestedTheme = RequestedFor(Mode);
        root.ActualThemeChanged += (_, _) => { UpdateChrome(); Changed?.Invoke(); };
        _ui.ColorValuesChanged += (_, _) => root.DispatcherQueue.TryEnqueue(OnSystemThemeChanged);
        _a11y.HighContrastChanged += (_, _) => root.DispatcherQueue.TryEnqueue(() => { UpdateChrome(); Changed?.Invoke(); });
        window.Activated += (_, _) => UpdateChrome();
        UpdateChrome();
    }

    public static void SetMode(AppThemeMode mode)
    {
        Mode = mode;
        try { File.WriteAllText(SettingsFile, JsonSerializer.Serialize(new { theme = mode.ToString().ToLowerInvariant() })); }
        catch (Exception e) { CrashLog.Write("theme save", e); }
        if (_root != null)
        {
            _root.RequestedTheme = mode == AppThemeMode.System ? SystemElementTheme() : RequestedFor(mode);
            UpdateChrome();
        }
        SetWin32MenuMode();
        Changed?.Invoke();
    }

    public static bool SystemIsDark
    {
        get
        {
            // Background colour of the system app theme: black in Dark mode, white in Light mode.
            var bg = _ui.GetColorValue(UIColorType.Background);
            return bg.R + bg.G + bg.B < 3 * 128;
        }
    }

    public static bool HighContrast => _a11y.HighContrast;

    public static ElementTheme ActualTheme => _root?.ActualTheme ?? (SystemIsDark ? ElementTheme.Dark : ElementTheme.Light);

    private static ElementTheme RequestedFor(AppThemeMode m) => m switch
    {
        AppThemeMode.Light => ElementTheme.Light,
        AppThemeMode.Dark => ElementTheme.Dark,
        _ => ElementTheme.Default,
    };

    private static ElementTheme SystemElementTheme() => SystemIsDark ? ElementTheme.Dark : ElementTheme.Light;

    private static void OnSystemThemeChanged()
    {
        if (_root == null) return;
        if (Mode == AppThemeMode.System)
        {
            // Normally ElementTheme.Default follows the new system theme by itself; if the framework did not pick the
            // change up (seen on some unpackaged setups), pin the root to the system theme explicitly.
            var want = SystemElementTheme();
            if (_root.ActualTheme != want) _root.RequestedTheme = want;
        }
        SetWin32MenuMode();
        UpdateChrome();
        Changed?.Invoke();
    }

    /// <summary>Code-created dialogs live in the popup layer and do not inherit the root's RequestedTheme.</summary>
    public static T Prepare<T>(T dlg) where T : ContentDialog
    {
        if (Application.Current.Resources.TryGetValue("DefaultContentDialogStyle", out var s) && s is Style st && dlg.Style == null) dlg.Style = st;
        if (_root != null) dlg.RequestedTheme = _root.ActualTheme;
        return dlg;
    }

    /// <summary>Caption buttons + DWM frame. Called on start, activation and every theme / contrast change.</summary>
    public static void UpdateChrome()
    {
        if (_window == null || _root == null) return;
        var dark = _root.ActualTheme == ElementTheme.Dark;
        try
        {
            var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(_window);
            int v = dark ? 1 : 0;
            DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, ref v, sizeof(int));
        }
        catch (Exception e) { CrashLog.Write("dwm dark", e); }
        try
        {
            if (!AppWindowTitleBar.IsCustomizationSupported()) return;
            var tb = _window.AppWindow.TitleBar;
            if (HighContrast)
            {
                // Let Windows paint the caption buttons with the high-contrast system colours.
                tb.ButtonForegroundColor = tb.ButtonHoverForegroundColor = tb.ButtonPressedForegroundColor = tb.ButtonInactiveForegroundColor = null;
                tb.ButtonBackgroundColor = tb.ButtonHoverBackgroundColor = tb.ButtonPressedBackgroundColor = tb.ButtonInactiveBackgroundColor = null;
                return;
            }
            Color fg = dark ? Colors.White : Colors.Black;
            tb.ButtonBackgroundColor = Colors.Transparent;
            tb.ButtonInactiveBackgroundColor = Colors.Transparent;
            tb.ButtonForegroundColor = fg;
            tb.ButtonHoverForegroundColor = fg;
            tb.ButtonPressedForegroundColor = fg;
            tb.ButtonInactiveForegroundColor = dark ? Color.FromArgb(0xFF, 0x9A, 0x9A, 0x9A) : Color.FromArgb(0xFF, 0x6E, 0x6E, 0x6E);
            tb.ButtonHoverBackgroundColor = dark ? Color.FromArgb(0x15, 0xFF, 0xFF, 0xFF) : Color.FromArgb(0x0F, 0x00, 0x00, 0x00);
            tb.ButtonPressedBackgroundColor = dark ? Color.FromArgb(0x0B, 0xFF, 0xFF, 0xFF) : Color.FromArgb(0x0A, 0x00, 0x00, 0x00);
        }
        catch (Exception e) { CrashLog.Write("titlebar colors", e); }
    }

    /// <summary>
    /// The tray context menu is a native Win32 popup menu, which ignores XAML themes. uxtheme's (undocumented but
    /// widely used) SetPreferredAppMode lets it follow the dark theme. Failure is harmless: the menu stays light.
    /// </summary>
    private static void SetWin32MenuMode()
    {
        try
        {
            if (Environment.OSVersion.Version.Build < 18362) return;
            var ux = LoadLibraryW("uxtheme.dll");
            if (ux == IntPtr.Zero) return;
            var setMode = GetProcAddress(ux, (IntPtr)135);
            var flush = GetProcAddress(ux, (IntPtr)136);
            if (setMode == IntPtr.Zero) return;
            int mode = Mode switch { AppThemeMode.Dark => 2 /* ForceDark */, AppThemeMode.Light => 3 /* ForceLight */, _ => 1 /* AllowDark */ };
            Marshal.GetDelegateForFunctionPointer<SetPreferredAppModeFn>(setMode)(mode);
            if (flush != IntPtr.Zero) Marshal.GetDelegateForFunctionPointer<FlushMenuThemesFn>(flush)();
        }
        catch (Exception e) { CrashLog.Write("menu theme", e); }
    }

    [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate int SetPreferredAppModeFn(int mode);
    [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate void FlushMenuThemesFn();
    [DllImport("dwmapi.dll")] private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr LoadLibraryW(string name);
    [DllImport("kernel32.dll")] private static extern IntPtr GetProcAddress(IntPtr module, IntPtr ordinal);
}
