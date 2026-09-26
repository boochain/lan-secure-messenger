using LanMessenger.Core.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using GroupItem = LanMessenger.Core.ViewModels.GroupItem;

namespace LanMessenger.Views;

public sealed partial class GroupsPage : Page
{
    public MainViewModel VM => App.VM;

    public GroupsPage() { InitializeComponent(); }

    private static GroupItem? GroupOf(object sender)
    {
        DependencyObject? d = sender as DependencyObject;
        while (d != null)
        {
            if (d is FrameworkElement fe && fe.DataContext is GroupItem g) return g;
            d = VisualTreeHelper.GetParent(d);
        }
        return null;
    }

    private async Task<List<string>?> PickMembersAsync(string title, IEnumerable<string>? exclude, TextBox? nameBox)
    {
        var peers = VM.TrustedPeersForPicking(exclude);
        var panel = new StackPanel { Spacing = 8, MinWidth = 360 };
        if (nameBox != null) panel.Children.Add(nameBox);
        if (peers.Count == 0)
            panel.Children.Add(new TextBlock { Text = "没有可选的已信任联系人。请先在聊天页中「信任」对方或完成 PIN 配对。", TextWrapping = TextWrapping.Wrap, Opacity = 0.7 });
        var list = new StackPanel { Spacing = 2 };
        foreach (var p in peers)
        {
            var cb = new CheckBox { Content = p.Name, Tag = p.Fingerprint };
            ToolTipService.SetToolTip(cb, p.Fingerprint);
            list.Children.Add(cb);
        }
        panel.Children.Add(new ScrollViewer { Content = list, MaxHeight = 320 });
        var dlg = new ContentDialog
        {
            XamlRoot = XamlRoot,
            Title = title,
            Content = panel,
            PrimaryButtonText = "确定",
            CloseButtonText = "取消",
            DefaultButton = ContentDialogButton.Primary,
        };
        if (await dlg.ShowAsync() != ContentDialogResult.Primary) return null;
        return list.Children.OfType<CheckBox>().Where(c => c.IsChecked == true).Select(c => (string)c.Tag).ToList();
    }

    private async void Create_Click(object sender, RoutedEventArgs e)
    {
        var nameBox = new TextBox { Header = "群名称", PlaceholderText = "例如：项目组", MaxLength = 40 };
        var members = await PickMembersAsync("创建群组", null, nameBox);
        if (members == null) return;
        if (string.IsNullOrWhiteSpace(nameBox.Text)) { VM.ShowStatus("请输入群名称", true); return; }
        if (await VM.CreateGroupAsync(nameBox.Text.Trim(), members)) App.MainWin?.NavigateTo("chat");
    }

    private async void Invite_Click(object sender, RoutedEventArgs e)
    {
        var g = GroupOf(sender);
        if (g == null) return;
        var members = await PickMembersAsync($"邀请加入「{g.Name}」", g.Members.Select(m => m.Fingerprint), null);
        if (members is { Count: > 0 }) await VM.InviteAsync(g.Id, members);
    }

    private void Open_Click(object sender, RoutedEventArgs e)
    {
        var g = GroupOf(sender);
        if (g != null) App.MainWin?.OpenConversation("group:" + g.Id);
    }

    private async void Leave_Click(object sender, RoutedEventArgs e)
    {
        var g = GroupOf(sender);
        if (g == null) return;
        var dlg = new ContentDialog
        {
            XamlRoot = XamlRoot,
            Title = $"退出群组「{g.Name}」？",
            Content = "退出后将不再收到该群消息。",
            PrimaryButtonText = "退出",
            CloseButtonText = "取消",
            DefaultButton = ContentDialogButton.Close,
        };
        if (await dlg.ShowAsync() == ContentDialogResult.Primary) await VM.LeaveGroupAsync(g.Id);
    }

    private async void RemoveMember_Click(object sender, RoutedEventArgs e)
    {
        var g = GroupOf(sender);
        if (g == null || sender is not FrameworkElement { Tag: string fp }) return;
        await VM.RemoveMemberAsync(g.Id, fp);
    }
}
