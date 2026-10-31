using LanMessenger.Core.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace LanMessenger.Views;

/// <summary>v0.6.2: new poll, disappearing timer, chat folders.</summary>
internal static class V062Dialogs
{
    static MainViewModel VM => App.VM;

    public static async Task NewPollAsync(XamlRoot root)
    {
        if (VM.SelectedConversation == null) return;
        var q = new TextBox { Header = "问题", PlaceholderText = "例如：周会改到哪天？", MaxLength = 300 };
        var opts = new TextBox { Header = "选项（每行一个，2–10 个）", AcceptsReturn = true, TextWrapping = TextWrapping.Wrap, MinHeight = 120, PlaceholderText = "周一\n周三\n周五" };
        var multi = new CheckBox { Content = "允许多选" };
        var err = new TextBlock { Foreground = (Microsoft.UI.Xaml.Media.Brush)Application.Current.Resources["SystemFillColorCriticalBrush"], TextWrapping = TextWrapping.Wrap };
        var sp = new StackPanel { Spacing = 10, MinWidth = 380 };
        foreach (var x in new UIElement[] { q, opts, multi, err }) sp.Children.Add(x);
        var d = Services.ThemeService.Prepare(new ContentDialog { XamlRoot = root, Title = "发起投票", Content = sp, PrimaryButtonText = "发起", CloseButtonText = "取消", DefaultButton = ContentDialogButton.Primary });
        d.PrimaryButtonClick += (_, a) =>
        {
            var list = Options(opts.Text);
            if (q.Text.Trim().Length == 0) { err.Text = "请填写问题"; a.Cancel = true; }
            else if (list.Count < 2 || list.Count > 10) { err.Text = "需要 2–10 个选项"; a.Cancel = true; }
        };
        if (await d.ShowAsync() != ContentDialogResult.Primary) return;
        await VM.SendPollAsync(q.Text.Trim(), Options(opts.Text), multi.IsChecked == true);
    }
    static List<string> Options(string t) => t.Replace("\r", "\n").Split('\n').Select(x => x.Trim()).Where(x => x.Length > 0).Distinct().ToList();

    public static async Task TtlAsync(XamlRoot root, ConversationItem c)
    {
        var rb = new RadioButtons { Header = "新消息在多久后自动删除（双方同步）" };
        foreach (var s in MainViewModel.TtlChoices) rb.Items.Add(MainViewModel.TtlLabel(s));
        rb.SelectedIndex = Math.Max(0, Array.IndexOf(MainViewModel.TtlChoices, c.TtlSecs));
        var sp = new StackPanel { Spacing = 8 };
        sp.Children.Add(rb);
        sp.Children.Add(new TextBlock { Text = "只影响之后发送和收到的消息；对方也会看到设置变化。截图或旧版本客户端仍可能留存内容。", TextWrapping = TextWrapping.Wrap, Opacity = 0.75, MaxWidth = 380 });
        var d = Services.ThemeService.Prepare(new ContentDialog { XamlRoot = root, Title = "定时删除", Content = sp, PrimaryButtonText = "确定", CloseButtonText = "取消", DefaultButton = ContentDialogButton.Primary });
        if (await d.ShowAsync() != ContentDialogResult.Primary || rb.SelectedIndex < 0) return;
        var secs = MainViewModel.TtlChoices[rb.SelectedIndex];
        if (secs != c.TtlSecs) await VM.SetDisappearingAsync(c, secs);
    }

    public static async Task FoldersAsync(XamlRoot root, ConversationItem c)
    {
        var all = VM.AllFolders();
        var lv = new ListView { SelectionMode = ListViewSelectionMode.Multiple, MaxHeight = 280, MinWidth = 340 };
        foreach (var f in all) { lv.Items.Add(f); }
        foreach (var f in c.Folders) if (all.Contains(f)) lv.SelectedItems.Add(f);
        var add = new TextBox { PlaceholderText = "新分组名（回车添加）", MaxLength = 32 };
        add.KeyDown += (_, e) =>
        {
            if (e.Key != Windows.System.VirtualKey.Enter) return; var n = add.Text.Trim(); if (n.Length == 0) return;
            if (!lv.Items.Contains(n)) lv.Items.Add(n); lv.SelectedItems.Add(n); add.Text = ""; e.Handled = true;
        };
        var rename = new Button { Content = "重命名所选…" }; var del = new Button { Content = "删除所选分组" };
        ContentDialog? d = null; string? pendingRename = null, pendingName = null; var pendingDelete = new List<string>();
        rename.Click += (_, _) =>
        {
            if (lv.SelectedItems.Count != 1) { VM.ShowStatus("请只选择一个分组"); return; }
            var old = (string)lv.SelectedItems[0]; var n = add.Text.Trim(); if (n.Length == 0) { VM.ShowStatus("先在输入框里填写新名字"); return; }
            pendingRename = old; pendingName = n; d?.Hide();
        };
        del.Click += (_, _) => { pendingDelete = lv.SelectedItems.Cast<string>().Where(all.Contains).ToList(); if (pendingDelete.Count > 0) d?.Hide(); };
        var tools = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 }; tools.Children.Add(rename); tools.Children.Add(del);
        var sp = new StackPanel { Spacing = 8 };
        sp.Children.Add(new TextBlock { Text = $"“{c.Title}”属于这些分组（可多选）：", TextWrapping = TextWrapping.Wrap });
        sp.Children.Add(lv); sp.Children.Add(add); sp.Children.Add(tools);
        sp.Children.Add(new TextBlock { Text = "分组只保存在本机，用于筛选会话列表。", Opacity = 0.7 });
        d = Services.ThemeService.Prepare(new ContentDialog { XamlRoot = root, Title = "会话分组", Content = sp, PrimaryButtonText = "保存", CloseButtonText = "取消", DefaultButton = ContentDialogButton.Primary });
        var r = await d.ShowAsync();
        if (pendingRename != null && pendingName != null) { await VM.RenameFolderAsync(pendingRename, pendingName); return; }
        if (pendingDelete.Count > 0) { foreach (var f in pendingDelete) await VM.DeleteFolderAsync(f); return; }
        if (r == ContentDialogResult.Primary) await VM.SetChatFoldersAsync(c, lv.SelectedItems.Cast<string>().ToList());
    }
}
