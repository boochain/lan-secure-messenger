using CommunityToolkit.Mvvm.ComponentModel;
using LanMessenger.Core.Ipc;
using LanMessenger.Core.Models;

namespace LanMessenger.Core.ViewModels;

/// <summary>v0.6.1 P1: reply/quote, reactions, edit/recall (own messages, 24 h).</summary>
public partial class MainViewModel
{
    public static readonly string[] QuickReactions = { "👍", "❤️", "😂", "😮", "😢", "🎉" };
    [ObservableProperty] private MessageItem? _replyItem;
    [ObservableProperty] private MessageItem? _editItem;
    public bool HasReplyBar => ReplyItem != null || EditItem != null;
    public string ReplyBarTitle => EditItem != null ? "编辑消息" : ReplyItem == null ? "" : "回复 " + (ReplyItem.IsOutgoing ? "我" : ReplyItem.SenderName);
    public string ReplyBarText { get { var m = EditItem ?? ReplyItem; return m == null ? "" : m.IsFile ? m.FileName : m.Body; } }
    partial void OnReplyItemChanged(MessageItem? value) => BarChanged();
    partial void OnEditItemChanged(MessageItem? value) => BarChanged();
    private void BarChanged() { OnPropertyChanged(nameof(HasReplyBar)); OnPropertyChanged(nameof(ReplyBarTitle)); OnPropertyChanged(nameof(ReplyBarText)); }

    public void StartReply(MessageItem m) { EditItem = null; ReplyItem = m; }
    public void StartEdit(MessageItem m) { if (!m.CanModify) return; ReplyItem = null; EditItem = m; ComposeText = m.RawBody; }
    public void CancelReply() { if (EditItem != null) ComposeText = ""; EditItem = null; ReplyItem = null; }
    /// <summary>↑ in an empty composer: edit my last editable message in this chat.</summary>
    public bool EditLast()
    {
        var m = Messages.LastOrDefault(x => x.CanModify);
        if (m == null) return false; StartEdit(m); return true;
    }

    private async Task SendP1Async(DaemonClient client, string conv, string text)
    {
        var ed = EditItem; var rp = ReplyItem;
        try
        {
            if (ed != null) await client.EditMessageAsync(ed.Id, text.TrimEnd());
            else if (rp != null) await client.SendReplyAsync(conv, text.TrimEnd(), rp.Id);
            _ui.Post(() => { EditItem = null; ReplyItem = null; });
        }
        catch (Exception e)
        {
            _ui.Post(() => { ComposeText = text; ShowStatus((ed != null ? "编辑失败：" : "发送失败：") + e.Message, true); });
        }
    }
    public Task ReactAsync(MessageItem m, string emoji) => SafeCall(c => c.ReactAsync(m.Id, emoji));
    public Task RecallAsync(MessageItem m) => SafeCall(c => c.RecallMessageAsync(m.Id));
}
