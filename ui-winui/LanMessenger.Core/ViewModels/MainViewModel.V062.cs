using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using LanMessenger.Core.Models;

namespace LanMessenger.Core.ViewModels;

/// <summary>v0.6.2: polls, chat folders + archive (filter), disappearing messages.</summary>
public partial class MainViewModel
{
    public const string FilterAll = "全部", FilterArchived = "已归档";
    public static readonly uint[] TtlChoices = { 0, 30, 300, 3600, 86400, 604800 };
    public static string TtlLabel(uint s) => s == 0 ? "关闭" : s % 86400 == 0 ? $"{s / 86400} 天" : s % 3600 == 0 ? $"{s / 3600} 小时" : s % 60 == 0 ? $"{s / 60} 分钟" : $"{s} 秒";

    public ObservableCollection<string> FilterOptions { get; } = new() { FilterAll };
    [ObservableProperty] private string _convFilter = FilterAll;
    /// <summary>raised when the visible set of chats may have changed (the view re-applies container visibility)</summary>
    public event Action? FilterChanged;
    partial void OnConvFilterChanged(string value) => FilterChanged?.Invoke();

    public bool InFilter(ConversationItem c) => ConvFilter switch
    {
        null or "" or FilterAll => !c.Archived,
        FilterArchived => c.Archived,
        var f => c.Folders.Contains(f) && !c.Archived,
    };

    /// <summary>rebuild the filter list from the conversations (folders sorted, archived last if any)</summary>
    public void RefreshFilters()
    {
        var want = new List<string> { FilterAll };
        want.AddRange(Conversations.SelectMany(c => c.Folders).Distinct().OrderBy(x => x, StringComparer.CurrentCulture));
        var na = Conversations.Count(c => c.Archived);
        if (na > 0) want.Add(FilterArchived);
        if (!want.SequenceEqual(FilterOptions))
        {
            var keep = ConvFilter;
            FilterOptions.Clear(); foreach (var w in want) FilterOptions.Add(w);
            ConvFilter = want.Contains(keep) ? keep : FilterAll;
        }
        ArchivedCount = na; OnPropertyChanged(nameof(HasFilters));
        FilterChanged?.Invoke();
    }
    [ObservableProperty] private int _archivedCount;
    public bool HasFilters => FilterOptions.Count > 1;

    public string SelectedTtlText => SelectedConversation is { HasTtl: true } c ? "⏱ 定时删除：" + TtlLabel(c.TtlSecs) : "";

    private void OnMessageRemoved(string id)
    {
        var i = Messages.ToList().FindIndex(m => m.Id == id);
        if (i >= 0) { Messages.RemoveAt(i); if (Messages.Count > 0) Grouping.Apply(Messages, Math.Max(0, i - 1)); RefreshPinned(); }
    }

    public async Task SendPollAsync(string question, IReadOnlyList<string> options, bool multi)
    {
        var c = SelectedConversation; if (c == null) return;
        await SafeCall(x => x.SendPollAsync(c.Id, question, options, multi), "已发起投票");
    }
    public Task VoteAsync(MessageItem m, int option)
    {
        if (m.PollClosed) { ShowStatus("投票已结束"); return Task.CompletedTask; }
        var set = new HashSet<int>(m.MyChoices);
        if (m.PollMulti) { if (!set.Remove(option)) set.Add(option); }
        else { if (set.Contains(option)) set.Clear(); else { set.Clear(); set.Add(option); } }
        return SafeCall(async x => { var r = await x.VotePollAsync(m.Id, set.OrderBy(v => v)); _ui.Post(() => m.Update(r)); });
    }
    public Task ClosePollAsync(MessageItem m) => SafeCall(async x => { var r = await x.ClosePollAsync(m.Id); _ui.Post(() => m.Update(r)); }, "投票已结束");
    public Task ToggleArchiveAsync(ConversationItem c) => SafeCall(x => x.SetConvArchivedAsync(c.Id, !c.Archived), c.Archived ? "已取消归档" : "已归档（收到新消息会自动回到列表）");
    public Task SetDisappearingAsync(ConversationItem c, uint secs) => SafeCall(x => x.SetDisappearingAsync(c.Id, secs), secs == 0 ? "已关闭定时删除" : "定时删除：" + TtlLabel(secs));

    public List<string> AllFolders() => Conversations.SelectMany(c => c.Folders).Distinct().OrderBy(x => x, StringComparer.CurrentCulture).ToList();
    /// <summary>set which folders a chat belongs to (folders are lists of chats on the daemon side)</summary>
    public async Task SetChatFoldersAsync(ConversationItem c, IReadOnlyCollection<string> folders)
    {
        var all = AllFolders().Union(folders).ToList();
        foreach (var f in all)
        {
            var members = Conversations.Where(x => x.Folders.Contains(f)).Select(x => x.Id).ToHashSet();
            var want = folders.Contains(f);
            if (want == members.Contains(c.Id)) continue;
            if (want) members.Add(c.Id); else members.Remove(c.Id);
            await SafeCall(x => x.SetFolderAsync(f, members));
        }
        ShowStatus("分组已更新");
    }
    public async Task RenameFolderAsync(string old, string name)
    {
        var members = Conversations.Where(x => x.Folders.Contains(old)).Select(x => x.Id).ToList();
        await SafeCall(x => x.SetFolderAsync(name, members, old), "分组已重命名");
        if (ConvFilter == old) ConvFilter = name;
    }
    public Task DeleteFolderAsync(string name) => SafeCall(x => x.SetFolderAsync(name, Array.Empty<string>()), "分组已删除");
}
