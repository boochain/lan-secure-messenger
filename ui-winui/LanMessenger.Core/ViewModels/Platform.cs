namespace LanMessenger.Core.ViewModels;

/// <summary>Marshals work onto the UI thread.</summary>
public interface IDispatcher
{
    void Post(Action action);
}

public sealed class ImmediateDispatcher : IDispatcher
{
    public void Post(Action action) => action();
}

/// <summary>OS / shell services the view models need; implemented by the WinUI app (and fakes in tests).</summary>
public interface IPlatform
{
    /// <summary>Start lan-daemon (hidden) if it is not running. Returns false if the exe is missing.</summary>
    bool StartDaemon(string endpoint);
    Task<IReadOnlyList<string>> PickFilesAsync();
    Task<string?> PickFolderAsync();
    Task<string?> PickSaveFileAsync(string suggestedName);
    Task<string?> PickOpenFileAsync();
    void OpenPath(string path);
    void RevealInFolder(string path);
    void CopyToClipboard(string text);
    void ShowToast(string title, string body, string conversationId);
    bool IsWindowActive { get; }
}

public static class Fmt
{
    public static string Bytes(ulong b)
    {
        string[] u = { "B", "KB", "MB", "GB", "TB" };
        double v = b;
        int i = 0;
        while (v >= 1024 && i < u.Length - 1) { v /= 1024; i++; }
        return i == 0 ? $"{b} B" : $"{v:0.#} {u[i]}";
    }

    public static string Time(long ms)
    {
        if (ms <= 0) return "";
        var t = DateTimeOffset.FromUnixTimeMilliseconds(ms).ToLocalTime();
        var now = DateTimeOffset.Now;
        if (t.Date == now.Date) return t.ToString("HH:mm");
        if (t.Year == now.Year) return t.ToString("MM-dd HH:mm");
        return t.ToString("yyyy-MM-dd HH:mm");
    }

    public static string Initial(string name) => string.IsNullOrWhiteSpace(name) ? "?" : name.Trim()[..1].ToUpperInvariant();
}
