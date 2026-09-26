using LanMessenger.Core.ViewModels;
using LanMessenger.Services;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;

namespace LanMessenger;

public partial class App : Application
{
    public static MainViewModel VM { get; private set; } = null!;
    public static MainWindow? MainWin { get; private set; }
    public static WindowsPlatform Platform { get; } = new();
    private static Mutex? _instanceMutex;
    private static EventWaitHandle? _showEvent;

    public App()
    {
        // Chinese UI: make XAML pick zh-Hans resources and CJK font fallback even on en-US Windows.
        try { Microsoft.Windows.Globalization.ApplicationLanguages.PrimaryLanguageOverride = "zh-Hans-CN"; }
        catch (Exception e) { CrashLog.Write("language override", e); }
        // Theme must be decided before any XAML loads (Application.RequestedTheme can only be set here).
        ThemeService.LoadAndApplyToApplication(this);
        InitializeComponent();
        UnhandledException += (_, e) =>
        {
            CrashLog.Write("UI unhandled", e.Exception);
            e.Handled = true;
        };
        AppDomain.CurrentDomain.UnhandledException += (_, e) => CrashLog.Write("domain unhandled", e.ExceptionObject as Exception);
        TaskScheduler.UnobservedTaskException += (_, e) =>
        {
            CrashLog.Write("unobserved task", e.Exception);
            e.SetObserved();
        };
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        var tag = Environment.UserName.Replace('\\', '_');
        _instanceMutex = new Mutex(true, $"FREC86.LanMessenger.{tag}", out var createdNew);
        _showEvent = new EventWaitHandle(false, EventResetMode.AutoReset, $"FREC86.LanMessenger.Show.{tag}");
        if (!createdNew)
        {
            // Already running: ask the first instance to show its window, then quit.
            _showEvent.Set();
            Exit();
            return;
        }

        var dq = DispatcherQueue.GetForCurrentThread();
        VM = new MainViewModel(new WinDispatcher(dq), Platform);
        Toasts.Init(dq);
        MainWin = new MainWindow();
        Platform.Window = MainWin;
        MainWin.Activate();

        var showEvent = _showEvent;
        var t = new Thread(() =>
        {
            while (showEvent.WaitOne())
                dq.TryEnqueue(() => MainWin?.ShowAndFocus());
        }) { IsBackground = true, Name = "show-request" };
        t.Start();

        _ = VM.StartAsync();
        var smoke = Environment.GetEnvironmentVariable("LSM_UI_SMOKE");
        if (!string.IsNullOrEmpty(smoke))
            _ = UiSmoke.RunAsync(MainWin, smoke);
    }
}
