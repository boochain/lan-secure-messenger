using CommunityToolkit.Mvvm.ComponentModel;
using LanMessenger.Core.Ipc;
using LanMessenger.Core.Models;

namespace LanMessenger.Core.ViewModels;

/// <summary>v0.6.1 P2: pinned chats, drafts, pinned/starred messages, @mentions, typing indicator, forward, multi-select.</summary>
public partial class MainViewModel
{
    [ObservableProperty] private string _typingText = "";
    [ObservableProperty] private bool _isMultiSelect;
    [ObservableProperty] private int _pickedCount;
    public bool HasTyping => TypingText.Length > 0;
    partial void OnTypingTextChanged(string value) => OnPropertyChanged(nameof(HasTyping));
    private string _typingConv = ""; private long _typingUntil;
    private bool _loadingDraft; private CancellationTokenSource? _draftCts;

    // ---- pinned banner (newest pinned message of the open chat)
    public MessageItem? PinnedItem => Messages.LastOrDefault(m => m.IsPinned && !m.IsRecalled);
    public bool HasPinned => PinnedItem != null;
    public string PinnedText { get { var m = PinnedItem; if (m == null) return ""; var n = Messages.Count(x => x.IsPinned && !x.IsRecalled); return (n > 1 ? $"置顶（{n}）：" : "置顶：") + (m.IsFile ? m.FileName : m.Body.Replace('\n', ' ')); } }
    public void RefreshPinned() { OnPropertyChanged(nameof(PinnedItem)); OnPropertyChanged(nameof(HasPinned)); OnPropertyChanged(nameof(PinnedText)); }

    /// <summary>pinned chats first, then newest (insert / move target index)</summary>
    private int ConvTarget(ConversationItem item) => Conversations.Count(x => x != item && ((x.Pinned && !item.Pinned) || (x.Pinned == item.Pinned && x.LastTsMs > item.LastTsMs)));

    // ---- drafts: saved by the daemon (sealed with the history key) on switch and 1.5 s after typing stops
    partial void OnSelectedConversationChanging(ConversationItem? value)
    {
        var old = SelectedConversation;
        if (old != null && EditItem == null && old.Id != value?.Id) { var t = ComposeText; old.Draft = t; old.HasDraft = t.Trim().Length > 0; _ = SafeCall(c => c.SetDraftAsync(old.Id, t)); }
        EditItem = null; ReplyItem = null; IsMultiSelect = false;
    }
    private void LoadDraft(ConversationItem? c) { _loadingDraft = true; ComposeText = c?.Draft ?? ""; _loadingDraft = false; TypingText = ""; }
    partial void OnComposeTextChanged(string value)
    {
        var c = SelectedConversation; if (_loadingDraft || c == null || EditItem != null) return;
        if (value.Length > 0) _ = SafeCall(x => x.SendTypingAsync(c.Id));   // daemon throttles (3 s) and skips offline / old peers
        _draftCts?.Cancel(); var cts = _draftCts = new CancellationTokenSource(); var id = c.Id;
        _ = Task.Delay(1500, cts.Token).ContinueWith(t => { if (!t.IsCanceled) _ = SafeCall(x => x.SetDraftAsync(id, value)); }, TaskScheduler.Default);
    }

    // ---- typing (event)
    private void OnTyping(string conv, string name)
    {
        _typingConv = conv; _typingUntil = Environment.TickCount64 + 5000;
        if (SelectedConversation?.Id == conv) TypingText = $"{name} 正在输入…";
        _ = Task.Delay(5100).ContinueWith(_ => _ui.Post(() => { if (Environment.TickCount64 >= _typingUntil) { _typingConv = ""; TypingText = ""; } }), TaskScheduler.Default);
    }

    // ---- @mentions: members of the open group whose "@name" is in the text
    public List<GroupMemberItem> MentionCandidates()
    {
        var c = SelectedConversation; if (c == null || !c.IsGroup) return new();
        return Groups.FirstOrDefault(g => g.Id == c.GroupId)?.Members.Where(m => m.Fingerprint != MyFingerprint).ToList() ?? new();
    }
    private List<string> MentionsIn(string text) => MentionCandidates().Where(m => m.Name.Length > 0 && text.Contains("@" + m.Name)).Select(m => m.Fingerprint).Distinct().ToList();
    private async Task SendTextP2Async(DaemonClient client, string conv, string text, string? replyTo)
    {
        var mentions = MentionsIn(text);
        if (mentions.Count == 0 && replyTo == null) await client.SendTextAsync(conv, text.TrimEnd());
        else await client.SendTextExAsync(conv, text.TrimEnd(), replyTo, mentions);
        _ = SafeCall(x => x.SetDraftAsync(conv, ""));
    }

    // ---- pin / star / forward
    public async Task TogglePinConvAsync()
    {
        var c = SelectedConversation; if (c == null) return;
        await SafeCall(x => x.SetConvPinnedAsync(c.Id, !c.Pinned), c.Pinned ? "已取消置顶" : "已置顶会话");
    }
    public Task TogglePinAsync(MessageItem m) => SafeCall(async x => { var r = await x.SetMessageFlagsAsync(m.Id, !m.IsPinned, null); _ui.Post(() => { m.Update(r); RefreshPinned(); }); });
    public Task ToggleStarAsync(MessageItem m) => SafeCall(async x => { var r = await x.SetMessageFlagsAsync(m.Id, null, !m.IsStarred); _ui.Post(() => m.Update(r)); }, m.IsStarred ? "已取消收藏" : "已收藏");
    public async Task<List<Message>> ListFlaggedAsync(bool starred, bool thisChatOnly)
    {
        var c = _client; if (c == null) return new();
        try { return await c.ListFlaggedAsync(starred ? "starred" : "pinned", thisChatOnly ? SelectedConversation?.Id : null); }
        catch (Exception e) { ShowStatus(e.Message, true); return new(); }
    }
    public async Task ForwardAsync(IReadOnlyList<string> ids, IReadOnlyList<string> convs)
    {
        var c = _client; if (c == null || ids.Count == 0 || convs.Count == 0) return; var bad = "";
        foreach (var id in ids) { try { await c.ForwardMessageAsync(id, convs); } catch (Exception e) { bad = e.Message; } }
        ShowStatus(bad.Length > 0 ? "部分转发失败：" + bad : "已转发", bad.Length > 0);
    }

    // ---- multi-select
    public void StartMultiSelect(MessageItem? first = null) { foreach (var m in Messages) m.IsPicked = false; if (first != null) first.IsPicked = true; IsMultiSelect = true; UpdatePicked(); }
    public void EndMultiSelect() { foreach (var m in Messages) m.IsPicked = false; IsMultiSelect = false; UpdatePicked(); }
    public void TogglePicked(MessageItem m) { m.IsPicked = !m.IsPicked; UpdatePicked(); }
    public void UpdatePicked() => PickedCount = Messages.Count(m => m.IsPicked);
    public List<MessageItem> Picked() => Messages.Where(m => m.IsPicked).ToList();
    public async Task StarPickedAsync() { foreach (var m in Picked()) await SafeCall(async x => { var r = await x.SetMessageFlagsAsync(m.Id, null, true); _ui.Post(() => m.Update(r)); }); ShowStatus("已收藏"); EndMultiSelect(); }
    public async Task DeletePickedAsync() { foreach (var m in Picked()) await DeleteMessageAsync(m); EndMultiSelect(); }
}
