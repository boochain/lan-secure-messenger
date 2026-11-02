using LanMessenger.Core.ViewModels;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;

namespace LanMessenger.Views;

/// <summary>v0.6.1 UX: my QR + add link, Ctrl+K quick switcher.</summary>
internal static class UxDialogs
{
    static MainViewModel VM => App.VM;
    static bool _open;

    public static async Task MyQrAsync(XamlRoot root)
    {
        if (_open) return;
        var u = await VM.MyUriAsync(); if (u == null || u.QrSize == 0) return;
        const int cell = 6, quiet = 4; int n = u.QrSize, px = (n + quiet * 2) * cell;
        // QR is always dark-on-white (scanners need it), whatever the app theme
        var cv = new Canvas { Width = px, Height = px, Background = new SolidColorBrush(Colors.White) };
        var black = new SolidColorBrush(Colors.Black);
        for (int y = 0; y < n; y++) for (int x = 0; x < n; x++)
            if (u.Qr[y * n + x] == '1') { var r = new Rectangle { Width = cell, Height = cell, Fill = black }; Canvas.SetLeft(r, (x + quiet) * cell); Canvas.SetTop(r, (y + quiet) * cell); cv.Children.Add(r); }
        AutomationProperties(cv, "我的二维码");
        var link = new TextBox { Text = u.Uri, IsReadOnly = true, TextWrapping = TextWrapping.Wrap, Header = "添加链接" };
        var sp = new StackPanel { Spacing = 12, MinWidth = 340 };
        sp.Children.Add(new TextBlock { Text = "让对方用手机版扫描，或把链接发给对方，在“添加联系人”(Ctrl+N) 里粘贴。指纹一致会自动信任。", TextWrapping = TextWrapping.Wrap });
        sp.Children.Add(new Border { Child = cv, HorizontalAlignment = HorizontalAlignment.Center });
        sp.Children.Add(link);
        var d = Services.ThemeService.Prepare(new ContentDialog { XamlRoot = root, Title = "我的二维码", Content = new ScrollViewer { Content = sp }, PrimaryButtonText = "复制链接", CloseButtonText = "关闭", DefaultButton = ContentDialogButton.Close });
        _open = true;
        try { if (await d.ShowAsync() == ContentDialogResult.Primary) { App.Platform.CopyToClipboard(u.Uri); VM.ShowStatus("已复制添加链接"); } }
        finally { _open = false; }
    }

    static void AutomationProperties(UIElement e, string name) => Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(e, name);

    public static async Task QuickSwitchAsync(XamlRoot root)
    {
        if (_open) return;
        var box = new TextBox { PlaceholderText = "输入名称或指纹，回车切换" };
        var list = new ListView { Height = 320, SelectionMode = ListViewSelectionMode.Single, DisplayMemberPath = "Title" };
        void Fill() { list.ItemsSource = VM.QuickSwitch(box.Text); if (list.Items.Count > 0) list.SelectedIndex = 0; }
        Fill();
        var sp = new StackPanel { Spacing = 8, MinWidth = 360 }; sp.Children.Add(box); sp.Children.Add(list);
        var d = Services.ThemeService.Prepare(new ContentDialog { XamlRoot = root, Title = "切换会话", Content = sp, PrimaryButtonText = "打开", CloseButtonText = "取消", DefaultButton = ContentDialogButton.Primary });
        box.TextChanged += (_, _) => Fill();
        box.KeyDown += (_, e) =>
        {
            if (e.Key == Windows.System.VirtualKey.Down && list.SelectedIndex < list.Items.Count - 1) { list.SelectedIndex++; e.Handled = true; }
            else if (e.Key == Windows.System.VirtualKey.Up && list.SelectedIndex > 0) { list.SelectedIndex--; e.Handled = true; }
        };
        list.DoubleTapped += (_, _) => { d.Hide(); Open(list.SelectedItem); };
        d.Opened += (_, _) => box.Focus(FocusState.Programmatic);
        _open = true;
        try { if (await d.ShowAsync() == ContentDialogResult.Primary) Open(list.SelectedItem); }
        finally { _open = false; }
    }

    static void Open(object? o) { if (o is ConversationItem c) VM.SelectedConversation = c; }
}
