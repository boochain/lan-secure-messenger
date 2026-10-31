using LanMessenger.Core.Models;
using LanMessenger.Core.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace LanMessenger.Views;

/// <summary>v0.6.1 P2: forward picker, pinned / starred lists (click jumps to the message).</summary>
internal static class P2Dialogs
{
    static MainViewModel VM => App.VM;

    public static async Task ForwardAsync(XamlRoot root, IReadOnlyList<string> ids)
    {
        if (ids.Count == 0) return;
        var lv = new ListView { SelectionMode = ListViewSelectionMode.Multiple, MaxHeight = 420, MinWidth = 380 };
        foreach (var c in VM.Conversations) lv.Items.Add(new TextBlock { Text = (c.IsGroup ? "# " : "") + c.Title, Tag = c.Id });
        var sp = new StackPanel { Spacing = 8 };
        sp.Children.Add(new TextBlock { Text = ids.Count > 1 ? $"逐条转发 {ids.Count} 条消息到（可多选）：" : "转发到（可多选）：" });
        sp.Children.Add(lv);
        var d = Services.ThemeService.Prepare(new ContentDialog { XamlRoot = root, Title = "转发", Content = sp, PrimaryButtonText = "转发", CloseButtonText = "取消", DefaultButton = ContentDialogButton.Primary });
        if (await d.ShowAsync() != ContentDialogResult.Primary) return;
        var convs = lv.SelectedItems.OfType<TextBlock>().Select(t => (string)t.Tag).ToList();
        if (convs.Count == 0) { VM.ShowStatus("未选择会话"); return; }
        await VM.ForwardAsync(ids, convs);
    }

    public static async Task FlaggedAsync(XamlRoot root, ChatPage page, bool starred, bool thisChatOnly)
    {
        var list = await VM.ListFlaggedAsync(starred, thisChatOnly);
        var lv = new ListView { IsItemClickEnabled = true, SelectionMode = ListViewSelectionMode.None, MaxHeight = 460, MinWidth = 460 };
        foreach (var m in Enumerable.Reverse(list))
        {
            var title = VM.Conversations.FirstOrDefault(c => c.Id == m.Conversation)?.Title ?? "";
            var when = DateTimeOffset.FromUnixTimeMilliseconds(m.TsMs).ToLocalTime().ToString("yyyy-MM-dd HH:mm");
            var who = m.Direction == Direction.Out ? "我" : m.SenderName;
            var sp = new StackPanel { Padding = new Thickness(4, 6, 4, 6), Tag = m };
            sp.Children.Add(new TextBlock { Text = $"{when} · {title} · {who}", FontSize = 12, Opacity = 0.75 });
            sp.Children.Add(new TextBlock { Text = m.Kind == MsgKind.File ? "📎 " + (m.File?.Name ?? "") : m.Body.Replace('\n', ' '), TextTrimming = TextTrimming.CharacterEllipsis, MaxLines = 2, TextWrapping = TextWrapping.Wrap });
            lv.Items.Add(sp);
        }
        ContentDialog? d = null;
        lv.ItemClick += (_, e) => { if (e.ClickedItem is StackPanel { Tag: Message m }) { d?.Hide(); page.JumpTo(m.Conversation, m.Id); } };
        object content = list.Count == 0 ? new TextBlock { Text = starred ? "还没有收藏的消息。" : "本会话还没有置顶消息。", Opacity = 0.75 } : lv;
        d = Services.ThemeService.Prepare(new ContentDialog { XamlRoot = root, Title = starred ? "收藏的消息" : "置顶消息", Content = content, CloseButtonText = "关闭", DefaultButton = ContentDialogButton.Close });
        await d.ShowAsync();
    }
}
