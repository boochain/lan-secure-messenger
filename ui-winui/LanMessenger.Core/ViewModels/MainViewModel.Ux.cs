using LanMessenger.Core.Models;

namespace LanMessenger.Core.ViewModels;

/// <summary>v0.6.1 UX: my QR / lsm:// link, add by link, one-click resend, Ctrl+K quick switcher.</summary>
public partial class MainViewModel
{
    public static bool IsAddLink(string s) => s.Trim().StartsWith("lsm://add?", StringComparison.OrdinalIgnoreCase);

    public async Task<UxUri?> MyUriAsync()
    {
        var c = _client; if (c == null) return null;
        try { return await c.MyUriAsync(); } catch (Exception e) { ShowStatus(e.Message); return null; }
    }

    public Task AddByLinkAsync(string uri) => SafeCall(x => x.AddUriAsync(uri.Trim()), "已按链接添加，正在连接（指纹一致将自动信任）…");

    public Task ResendAsync(MessageItem m) => SafeCall(x => x.ResendAsync(m.Id), "已重新发送");

    /// <summary>quick switcher: chats whose name / note / fingerprint contains q (empty = all, most recent first as listed)</summary>
    public IReadOnlyList<ConversationItem> QuickSwitch(string q)
    {
        q = q.Trim();
        return Conversations.Where(c => q.Length == 0 || c.Title.Contains(q, StringComparison.OrdinalIgnoreCase) || c.Id.Contains(q, StringComparison.OrdinalIgnoreCase)).Take(30).ToList();
    }
}
