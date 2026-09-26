using LanMessenger.Core.Models;
using LanMessenger.Core.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Navigation;
using Windows.ApplicationModel.DataTransfer;
using Windows.Storage;
using Windows.System;

namespace LanMessenger.Views;

public sealed partial class ChatPage : Page
{
    public MainViewModel VM => App.VM;
    private bool _syncing;

    public ChatPage()
    {
        InitializeComponent();
        NavigationCacheMode = NavigationCacheMode.Required;
        Loaded += (_, _) => SyncSelection();
        Unloaded += (_, _) => { VM.MessagesAppended -= ScrollToEnd; VM.PropertyChanged -= Vm_PropertyChanged; };
        Loaded += (_, _) => { VM.MessagesAppended -= ScrollToEnd; VM.MessagesAppended += ScrollToEnd; VM.PropertyChanged -= Vm_PropertyChanged; VM.PropertyChanged += Vm_PropertyChanged; };
    }

    private void Vm_PropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(MainViewModel.SelectedConversation)) SyncSelection();
    }

    private void SyncSelection()
    {
        if (!ReferenceEquals(ConvList.SelectedItem, VM.SelectedConversation))
        {
            _syncing = true;
            ConvList.SelectedItem = VM.SelectedConversation;
            _syncing = false;
        }
    }

    /// <summary>UI smoke: open / close the "更多" menu so the flyout (popup layer) is captured in both themes.</summary>
    public void OpenMoreMenuForSmoke() => MoreButton.Flyout?.ShowAt(MoreButton);
    public void CloseMoreMenuForSmoke() => MoreButton.Flyout?.Hide();
    public void ScrollMessagesToTopForSmoke() { if (VM.Messages.Count > 0) MessageList.ScrollIntoView(VM.Messages[0], ScrollIntoViewAlignment.Leading); }

    private void ScrollToEnd()
    {
        DispatcherQueue.TryEnqueue(() =>
        {
            if (VM.Messages.Count > 0) MessageList.ScrollIntoView(VM.Messages[^1]);
        });
    }

    private void ConvList_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_syncing) return;
        if (ConvList.SelectedItem is ConversationItem c && !ReferenceEquals(c, VM.SelectedConversation))
        {
            VM.SelectedConversation = c;
            Composer.Focus(FocusState.Programmatic);
        }
    }

    private void Composer_PreviewKeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (e.Key != VirtualKey.Enter) return;
        var shift = Microsoft.UI.Input.InputKeyboardSource.GetKeyStateForCurrentThread(VirtualKey.Shift)
            .HasFlag(Windows.UI.Core.CoreVirtualKeyStates.Down);
        if (shift) return; // newline
        e.Handled = true;
        // Make sure the two-way binding has the latest text before sending.
        VM.ComposeText = Composer.Text;
        if (VM.SendCommand.CanExecute(null)) VM.SendCommand.Execute(null);
    }

    private void AddPeer_KeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (e.Key == VirtualKey.Enter && sender is TextBox tb)
        {
            VM.AddPeerAddress = tb.Text;
            VM.AddPeerCommand.Execute(null);
            e.Handled = true;
        }
    }

    private async void Search_QuerySubmitted(AutoSuggestBox sender, AutoSuggestBoxQuerySubmittedEventArgs args)
    {
        VM.SearchText = args.QueryText;
        await VM.SearchCommand.ExecuteAsync(null);
        if (VM.SearchResults.Count > 0) SearchTip.IsOpen = true;
    }

    private void SearchResult_ItemClick(object sender, ItemClickEventArgs e)
    {
        if (e.ClickedItem is MessageItem m)
        {
            SearchTip.IsOpen = false;
            VM.SelectConversation(m.Conversation);
        }
    }

    private static MessageItem? ItemOf(object sender) => (sender as FrameworkElement)?.DataContext as MessageItem;

    private void OpenFile_Click(object sender, RoutedEventArgs e) => VM.OpenMessageFileCommand.Execute(ItemOf(sender));
    private void RevealFile_Click(object sender, RoutedEventArgs e) => VM.RevealMessageFileCommand.Execute(ItemOf(sender));
    private void Image_Tapped(object sender, TappedRoutedEventArgs e) => VM.OpenMessageFileCommand.Execute(ItemOf(sender));

    private void Conversation_DragOver(object sender, DragEventArgs e)
    {
        if (VM.HasSelection && e.DataView.Contains(StandardDataFormats.StorageItems))
        {
            e.AcceptedOperation = DataPackageOperation.Copy;
            e.DragUIOverride.Caption = "发送给 " + VM.SelectedTitle;
        }
        else e.AcceptedOperation = DataPackageOperation.None;
    }

    private async void Conversation_Drop(object sender, DragEventArgs e)
    {
        if (!VM.HasSelection || !e.DataView.Contains(StandardDataFormats.StorageItems)) return;
        var items = await e.DataView.GetStorageItemsAsync();
        var paths = items.OfType<StorageFile>().Select(f => f.Path).Where(p => !string.IsNullOrEmpty(p)).ToList();
        if (items.OfType<StorageFolder>().Any()) VM.ShowStatus("暂不支持直接发送文件夹，请先压缩", true);
        if (paths.Count > 0) await VM.SendFilesAsync(paths);
    }

    private void CopyPeerFp_Click(object sender, RoutedEventArgs e)
    {
        var fp = VM.SelectedConversation?.PeerFingerprint;
        if (!string.IsNullOrEmpty(fp)) { App.Platform.CopyToClipboard(fp); VM.ShowStatus("已复制对方指纹"); }
    }

    private async void Forget_Click(object sender, RoutedEventArgs e)
    {
        var c = VM.SelectedConversation;
        if (c == null || !c.IsDm) return;
        var dlg = new ContentDialog
        {
            XamlRoot = XamlRoot,
            Title = "忘记此联系人？",
            Content = $"将删除「{c.Title}」的信任状态和已保存的地址（聊天记录保留）。对方再次上线时会重新出现。",
            PrimaryButtonText = "忘记",
            CloseButtonText = "取消",
            DefaultButton = ContentDialogButton.Close,
        };
        if (await LanMessenger.Services.ThemeService.Prepare(dlg).ShowAsync() == ContentDialogResult.Primary) await VM.ForgetPeerAsync(c.PeerFingerprint);
    }

    private async void Pin_Click(object sender, RoutedEventArgs e)
    {
        var c = VM.SelectedConversation;
        if (c == null || !c.IsDm) return;
        var pinBox = new TextBox { PlaceholderText = "输入对方屏幕上显示的 6 位 PIN", MaxLength = 12 };
        var mine = new TextBlock { TextWrapping = TextWrapping.Wrap, FontSize = 28, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold, IsTextSelectionEnabled = true };
        var gen = new Button { Content = "在本机生成 PIN" };
        gen.Click += async (_, _) =>
        {
            var pin = await VM.PinOfferAsync();
            mine.Text = pin ?? "生成失败";
        };
        var panel = new StackPanel { Spacing = 10 };
        panel.Children.Add(new TextBlock
        {
            TextWrapping = TextWrapping.Wrap,
            Text = "PIN 配对可以在不核对长指纹的情况下互相信任。\n方式：一方点击「在本机生成 PIN」，把 PIN 告诉另一方；另一方在下方输入并点击「配对」。PIN 5 分钟内有效。",
        });
        panel.Children.Add(gen);
        panel.Children.Add(mine);
        panel.Children.Add(pinBox);
        var dlg = new ContentDialog
        {
            XamlRoot = XamlRoot,
            Title = $"与「{c.Title}」PIN 配对",
            Content = panel,
            PrimaryButtonText = "配对",
            CloseButtonText = "关闭",
        };
        if (await LanMessenger.Services.ThemeService.Prepare(dlg).ShowAsync() == ContentDialogResult.Primary && !string.IsNullOrWhiteSpace(pinBox.Text))
            await VM.PinPairAsync(c.PeerFingerprint, pinBox.Text);
    }
}
