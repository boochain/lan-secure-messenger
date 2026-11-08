using LanMessenger.Core.Models;

namespace LanMessenger.Core.ViewModels;

/// <summary>v0.7.0: chat-list actions (pin / read / unread / mute / delete) on a specific conversation.</summary>
public partial class MainViewModel
{
    public Task PinConversationAsync(ConversationItem c) =>
        SafeCall(x => x.SetConvPinnedAsync(c.Id, !c.Pinned), c.Pinned ? "已取消置顶" : "已置顶会话");

    public Task MarkConversationReadAsync(ConversationItem c) =>
        SafeCall(x => x.MarkReadAsync(c.Id), "已标为已读");

    public Task MarkConversationUnreadAsync(ConversationItem c) =>
        SafeCall(x => x.MarkUnreadAsync(c.Id), "已标为未读");

    public Task MuteConversationAsync(ConversationItem c) =>
        SafeCall(x => x.SetMutedAsync(c.Id, !c.Muted), c.Muted ? "已取消免打扰" : "已设为免打扰");

    public async Task DeleteConversationAsync(ConversationItem c)
    {
        await SafeCall(x => x.ClearConversationAsync(c.Id), "已删除本机聊天记录");
        if (SelectedConversation?.Id == c.Id) SelectedConversation = Conversations.FirstOrDefault(x => x.Id != c.Id);
    }
}
