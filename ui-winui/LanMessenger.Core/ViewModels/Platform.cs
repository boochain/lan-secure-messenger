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
    /// <summary>Modal yes/no. Returns true when the user confirms.</summary>
    Task<bool> ConfirmAsync(string title, string message);
    void CopyToClipboard(string text);
    void ShowToast(string title, string body, string conversationId);
    bool IsWindowActive { get; }
}

public static class FileRisk
{
    static readonly HashSet<string> Dangerous = new(StringComparer.OrdinalIgnoreCase)
    {
        ".exe",".bat",".cmd",".com",".msi",".scr",".ps1",".vbs",".js",".jse",".wsf",".wsh",
        ".cpl",".msc",".jar",".apk",".app",".dmg",".pkg",".sh",".bash",".command",".reg",".lnk",".hta"
    };
    public static bool IsDangerous(string path)
    {
        var ext = System.IO.Path.GetExtension(path ?? "");
        return ext.Length > 0 && Dangerous.Contains(ext);
    }
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

    public static string Initial(string name) => Initials(name);

    // ---- redesign (design/redesign/spec.md §6): the same rules on all four clients ----
    /// <summary>
    /// Avatar initials (same rule on WinUI / Android / GTK / XP): a name starting with a CJK character -> that one character
    /// ("王工" -> "王"); a Latin name -> the first letters of up to two Latin words, never mixed with CJK ("Alice Chen" -> "AC",
    /// "CI 测试群" -> "C"); anything else -> the first character.
    /// </summary>
    public static string Initials(string name)
    {
        var n = (name ?? "").Trim();
        if (n.Length == 0) return "?";
        if (IsLatinLetter(n[0]))
            return string.Concat(n.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries)
                .Where(w => IsLatinLetter(w[0])).Take(2).Select(w => char.ToUpperInvariant(w[0])));
        return char.IsSurrogatePair(n, 0) ? n[..2] : n[..1];
    }
    private static bool IsLatinLetter(char c) => (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');

    /// <summary>Avatar colour index: FNV-1a 32 over the UTF-8 id (fingerprint / group id), mod 8.</summary>
    public static int AvatarIndex(string id)
    {
        uint h = 2166136261;
        foreach (var b in System.Text.Encoding.UTF8.GetBytes(id ?? "")) { h ^= b; h *= 16777619; }
        return (int)(h % 8);
    }
    public static readonly string[] AvatarColors = { "#7C61C8", "#2176BC", "#108172", "#C24D1D", "#378139", "#C6406F", "#727819", "#AC5E0A" };

    static readonly string[] Week = { "周日", "周一", "周二", "周三", "周四", "周五", "周六" };
    static int DaysAgo(DateTime t, DateTime now) => (int)Math.Round((now.Date - t.Date).TotalDays);

    /// <summary>Chat-list time: today HH:mm, 昨天, 周X within a week, MM/dd this year, else yyyy/MM/dd.</summary>
    public static string ListTime(long ms, DateTime? nowOpt = null)
    {
        if (ms <= 0) return "";
        var t = DateTimeOffset.FromUnixTimeMilliseconds(ms).ToLocalTime().DateTime; var now = nowOpt ?? DateTime.Now;
        var d = DaysAgo(t, now);
        if (d <= 0) return t.ToString("HH:mm");
        if (d == 1) return "昨天";
        if (d < 7) return Week[(int)t.DayOfWeek];
        return t.Year == now.Year ? t.ToString("MM/dd") : t.ToString("yyyy/MM/dd");
    }

    /// <summary>Date separator: 今天 / 昨天 / M月d日 / yyyy年M月d日.</summary>
    public static string DateLabel(long ms, DateTime? nowOpt = null)
    {
        var t = DateTimeOffset.FromUnixTimeMilliseconds(ms).ToLocalTime().DateTime; var now = nowOpt ?? DateTime.Now;
        var d = DaysAgo(t, now);
        if (d <= 0) return "今天";
        if (d == 1) return "昨天";
        return t.Year == now.Year ? $"{t.Month}月{t.Day}日" : $"{t.Year}年{t.Month}月{t.Day}日";
    }

    public static string Clock(long ms) => ms <= 0 ? "" : DateTimeOffset.FromUnixTimeMilliseconds(ms).ToLocalTime().ToString("HH:mm");
}

/// <summary>Bubble grouping + date separators (spec §6.2): a new group starts on a sender change, a new day or a gap over 5 min.</summary>
public static class Grouping
{
    public const long GapMs = 5 * 60 * 1000;
    static long Day(long ms) => DateTimeOffset.FromUnixTimeMilliseconds(ms).ToLocalTime().Date.Ticks;
    public static bool Same(MessageItem a, MessageItem b) =>
        !a.IsSystem && !b.IsSystem && a.IsOutgoing == b.IsOutgoing && a.SenderName == b.SenderName && Day(a.TsMs) == Day(b.TsMs) && b.TsMs - a.TsMs <= GapMs;

    /// <summary>Recompute First/Last/DateLabel/ShowE2E for items [from..] (and the neighbour before it).</summary>
    public static void Apply(IList<MessageItem> l, int from = 0)
    {
        from = Math.Max(0, from - 1);
        for (int i = from; i < l.Count; i++)
        {
            var m = l[i]; var prev = i > 0 ? l[i - 1] : null; var next = i + 1 < l.Count ? l[i + 1] : null;
            var newDay = prev == null || Day(prev.TsMs) != Day(m.TsMs);
            m.DateLabel = newDay ? Fmt.DateLabel(m.TsMs) : "";
            m.ShowE2E = i == 0;
            m.IsFirstInGroup = prev == null || !Same(prev, m);
            m.IsLastInGroup = next == null || !Same(m, next);
            m.ShowSender = m.IsGroupConversation && m.IsIncoming && m.IsFirstInGroup;
        }
    }
}
