using Microsoft.UI.Dispatching;
using Microsoft.Windows.AppNotifications;
using Microsoft.Windows.AppNotifications.Builder;

namespace LanMessenger.Services;

/// <summary>Windows toast notifications via the Windows App SDK (works unpackaged).</summary>
public static class Toasts
{
    private static bool _ok;

    public static void Init(DispatcherQueue dq)
    {
        try
        {
            AppNotificationManager.Default.NotificationInvoked += (_, args) =>
            {
                args.Arguments.TryGetValue("conv", out var conv);
                dq.TryEnqueue(() => Activate(conv));
            };
            AppNotificationManager.Default.Register();
            _ok = true;
        }
        catch (Exception e)
        {
            CrashLog.Write("toast register", e);
        }
    }

    /// <summary>A toast click: show the window and open that chat (W17; the smoke check calls this too).</summary>
    public static void Activate(string? conv)
    {
        App.MainWin?.ShowAndFocus();
        if (!string.IsNullOrEmpty(conv)) App.MainWin?.OpenConversation(conv);
    }

    public static void Show(string title, string body, string conversationId)
    {
        if (!_ok) return;
        try
        {
            var n = new AppNotificationBuilder()
                .AddArgument("conv", conversationId ?? "")
                .AddText(title)
                .AddText(body)
                .BuildNotification();
            AppNotificationManager.Default.Show(n);
        }
        catch (Exception e)
        {
            CrashLog.Write("toast show", e);
        }
    }

    public static void Shutdown()
    {
        if (!_ok) return;
        try { AppNotificationManager.Default.Unregister(); } catch { }
    }
}
