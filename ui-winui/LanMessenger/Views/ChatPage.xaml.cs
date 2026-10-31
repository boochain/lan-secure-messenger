using System.IO;
using LanMessenger.Core.Models;
using LanMessenger.Core.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
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
        // keyboard shortcuts (Fluent: keyboard accelerators): Ctrl+F search, Ctrl+N add contact, Ctrl+(Shift+)Tab next/prev chat, Esc
        AddAccel(VirtualKey.F, VirtualKeyModifiers.Control, () => FocusSearch());
        AddAccel(VirtualKey.N, VirtualKeyModifiers.Control, OpenAddPeer);
        // Ctrl+Tab / Ctrl+Shift+Tab: Tab is consumed by focus navigation (and the multi-line composer) before
        // KeyboardAccelerators run (found by the UI smoke runtime check), so handle it in the tunnelling PreviewKeyDown.
        PreviewKeyDown += (_, e) =>
        {
            if (e.Key != VirtualKey.Tab) return;
            bool Down(VirtualKey k) => Microsoft.UI.Input.InputKeyboardSource.GetKeyStateForCurrentThread(k).HasFlag(Windows.UI.Core.CoreVirtualKeyStates.Down);
            if (!Down(VirtualKey.Control)) return;
            CycleConversation(Down(VirtualKey.Shift) ? -1 : +1);
            e.Handled = true;
        };
        AddAccel(VirtualKey.Escape, VirtualKeyModifiers.None, () =>
        {
            if (SearchTip.IsOpen) { SearchTip.IsOpen = false; return true; }
            if (!string.IsNullOrEmpty(SearchBox.Text)) { SearchBox.Text = ""; Composer.Focus(FocusState.Keyboard); return true; }
            return false;   // let dialogs/flyouts handle Esc themselves
        });
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

    private void AddAccel(VirtualKey key, VirtualKeyModifiers mods, Func<bool> run)
    {
        var a = new KeyboardAccelerator { Key = key, Modifiers = mods };
        a.Invoked += (_, e) => e.Handled = run();
        KeyboardAccelerators.Add(a);
    }
    private void AddAccel(VirtualKey key, VirtualKeyModifiers mods, Action run) => AddAccel(key, mods, () => { run(); return true; });

    public void FocusSearch() => SearchBox.Focus(FocusState.Keyboard);
    private void FocusSearch_Click(object sender, RoutedEventArgs e) => FocusSearch();
    /// <summary>Ctrl+N / the header button: the add-by-IP flyout, with its box focused.</summary>
    public void OpenAddPeer() => AddPeerFlyout.ShowAt(AddPeerButton);
    private void AddPeerFlyout_Opened(object? sender, object e) => DispatcherQueue.TryEnqueue(() => AddPeerBox.Focus(FocusState.Keyboard));
    internal void SmokeCloseAddPeer() => AddPeerFlyout.Hide();

    /// <summary>Contact / group details (fingerprint, latency, address, key epoch) — kept out of the header.</summary>
    private void Details_Tapped(object sender, TappedRoutedEventArgs e) => _ = ShowDetailsAsync();
    private void Details_Click(object sender, RoutedEventArgs e) => _ = ShowDetailsAsync();
    private async Task ShowDetailsAsync()
    {
        if (!VM.HasSelection) return;
        var body = new TextBlock { Text = VM.SelectedDetailsText, TextWrapping = TextWrapping.Wrap, IsTextSelectionEnabled = true, LineHeight = 24 };
        var d = Services.ThemeService.Prepare(new ContentDialog
        {
            XamlRoot = XamlRoot, Title = VM.SelectedIsDm ? $"联系人详情 · {VM.SelectedTitle}" : $"群组详情 · {VM.SelectedTitle}", Content = body,
            CloseButtonText = "关闭", DefaultButton = ContentDialogButton.Close,
            PrimaryButtonText = VM.SelectedIsDm ? "复制指纹" : "",
        });
        if (await d.ShowAsync() == ContentDialogResult.Primary && VM.SelectedConversation is { } c)
        {
            var dp = new DataPackage(); dp.SetText(c.PeerFingerprint); Clipboard.SetContent(dp); VM.ShowStatus("指纹已复制");
        }
    }
    internal Task SmokeShowDetailsAsync() => ShowDetailsAsync();

    /// <summary>Incoming file card: download = save a copy wherever the user wants (files are received into the download folder).</summary>
    private async void SaveAs_Click(object sender, RoutedEventArgs e)
    {
        if (ItemOf(sender) is not { HasFilePath: true } m) return;
        try
        {
            var picker = new Windows.Storage.Pickers.FileSavePicker { SuggestedFileName = Path.GetFileNameWithoutExtension(m.FileName) };
            WinRT.Interop.InitializeWithWindow.Initialize(picker, WinRT.Interop.WindowNative.GetWindowHandle(App.MainWin!));
            var ext = Path.GetExtension(m.FileName);
            picker.FileTypeChoices.Add(ext.Length > 1 ? ext.TrimStart('.').ToUpperInvariant() + " 文件" : "文件", new List<string> { ext.Length > 1 ? ext : ".bin" });
            var f = await picker.PickSaveFileAsync();
            if (f == null) return;
            File.Copy(m.FilePath, f.Path, true);
            VM.ShowStatus("已保存到 " + f.Path);
        }
        catch (Exception ex) { VM.ShowStatus("保存失败：" + ex.Message); }
    }
    private void NewGroup_Click(object sender, RoutedEventArgs e) => App.MainWin?.NavigateTo("groups");

    public void CycleConversation(int d)
    {
        var n = VM.Conversations.Count; if (n == 0) return;
        var i = VM.SelectedConversation == null ? -1 : VM.Conversations.IndexOf(VM.SelectedConversation);
        VM.SelectedConversation = VM.Conversations[((i < 0 ? (d > 0 ? -1 : 0) : i) + d + n) % n];
        Composer.Focus(FocusState.Keyboard);
    }

    /* right-click menus (Fluent: context menus offer the item's common commands) */
    private void MessageList_RightTapped(object sender, Microsoft.UI.Xaml.Input.RightTappedRoutedEventArgs e)
    {
        if ((e.OriginalSource as FrameworkElement)?.DataContext is not MessageItem m || m.IsSystem) return;
        var f = new MenuFlyout();
        void Add(string text, string glyph, Action a) { var it = new MenuFlyoutItem { Text = text, Icon = new FontIcon { Glyph = glyph } }; it.Click += (_, _) => a(); f.Items.Add(it); }
        if (m.IsText) Add("复制", "\uE8C8", () => { var dp = new DataPackage(); dp.SetText(m.Body); Clipboard.SetContent(dp); VM.ShowStatus("已复制"); });
        if (m.IsFile && m.HasFilePath)
        {
            Add("打开", "\uE8E5", () => VM.OpenMessageFileCommand.Execute(m));
            Add("在文件夹中显示", "\uE838", () => VM.RevealMessageFileCommand.Execute(m));
            Add("复制文件路径", "\uE8C8", () => { var dp = new DataPackage(); dp.SetText(m.FilePath); Clipboard.SetContent(dp); VM.ShowStatus("已复制"); });
        }
        if (m.IsFile) Add("复制文件名", "\uE8C8", () => { var dp = new DataPackage(); dp.SetText(m.FileName); Clipboard.SetContent(dp); VM.ShowStatus("已复制"); });
        if (f.Items.Count > 0) f.Items.Add(new MenuFlyoutSeparator());
        var del = new MenuFlyoutItem { Text = "删除（仅本机）…", Icon = new FontIcon { Glyph = "\uE74D" } };
        del.Click += async (_, _) =>
        {
            var d = Services.ThemeService.Prepare(new ContentDialog { XamlRoot = XamlRoot, Title = "删除这条消息？", Content = "只会从本机删除，对方仍保留这条消息。此操作无法撤销。",
                PrimaryButtonText = "删除", CloseButtonText = "取消", DefaultButton = ContentDialogButton.Close });
            if (await d.ShowAsync() == ContentDialogResult.Primary) await VM.DeleteMessageAsync(m);
        };
        f.Items.Add(del);
        f.ShowAt((FrameworkElement)e.OriginalSource, e.GetPosition((FrameworkElement)e.OriginalSource));
        e.Handled = true;
    }

    private void ConvList_RightTapped(object sender, Microsoft.UI.Xaml.Input.RightTappedRoutedEventArgs e)
    {
        if ((e.OriginalSource as FrameworkElement)?.DataContext is not ConversationItem c) return;
        VM.SelectedConversation = c;
        var f = new MenuFlyout();
        void Add(string text, string glyph, System.Windows.Input.ICommand cmd) { var it = new MenuFlyoutItem { Text = text, Icon = new FontIcon { Glyph = glyph }, Command = cmd }; f.Items.Add(it); }
        if (c.IsDm)
        {
            if (VM.SelectedTrusted) Add("取消信任", "\uE8F8", VM.UntrustSelectedCommand); else Add("信任", "\uE8FB", VM.TrustSelectedCommand);
            Add(VM.SelectedMuted ? "取消免打扰" : "免打扰", "\uE7ED", VM.ToggleMuteCommand);
            f.Items.Add(new MenuFlyoutSeparator());
            var block = new MenuFlyoutItem { Text = VM.SelectedBlocked ? "取消屏蔽" : "屏蔽…", Icon = new FontIcon { Glyph = "\uE8F8" } };
            block.Click += async (_, _) =>
            {
                if (VM.SelectedBlocked) { VM.UntrustSelectedCommand.Execute(null); return; }
                var d = Services.ThemeService.Prepare(new ContentDialog { XamlRoot = XamlRoot, Title = $"屏蔽 {c.Title}？", Content = "屏蔽后将不再接收对方的消息和文件。可随时取消屏蔽。",
                    PrimaryButtonText = "屏蔽", CloseButtonText = "取消", DefaultButton = ContentDialogButton.Close });
                if (await d.ShowAsync() == ContentDialogResult.Primary) VM.BlockSelectedCommand.Execute(null);
            };
            f.Items.Add(block);
        }
        else Add(VM.SelectedMuted ? "取消免打扰" : "免打扰", "\uE7ED", VM.ToggleMuteCommand);
        f.ShowAt((FrameworkElement)e.OriginalSource, e.GetPosition((FrameworkElement)e.OriginalSource));
        e.Handled = true;
    }

    /// <summary>UI smoke: open / close the "更多" menu so the flyout (popup layer) is captured in both themes.</summary>
    public void OpenMoreMenuForSmoke() => MoreButton.Flyout?.ShowAt(MoreButton);
    public void CloseMoreMenuForSmoke() => MoreButton.Flyout?.Hide();
    // UI smoke runtime checks (real keyboard / mouse input is injected by UiSmoke)
    internal FrameworkElement SmokeSearchBox => SearchBox;
    internal FrameworkElement SmokeAddPeerBox => AddPeerBox;
    internal ListView SmokeMessageList => MessageList;
    internal ListView SmokeConvList => ConvList;
    internal string SmokeSearchText { get => SearchBox.Text; set => SearchBox.Text = value; }
    internal void SmokeFocusComposer() => Composer.Focus(FocusState.Keyboard);
    internal string SmokeComposerText { get => Composer.Text; set { Composer.Text = value; Composer.Select(value.Length, 0); } }
    public void ScrollMessagesToTopForSmoke() { if (VM.Messages.Count > 0) MessageList.ScrollIntoView(VM.Messages[0], ScrollIntoViewAlignment.Leading); }

    private void ScrollToEnd()
    {
        DispatcherQueue.TryEnqueue(() =>
        {
            if (VM.Messages.Count > 0) MessageList.ScrollIntoView(VM.Messages[^1]);
        });
        // ScrollIntoView runs before tall items (images, file cards) are measured, which left the newest
        // message half below the composer: pin to the real end once layout has run, and again after decoding
        DispatcherQueue.TryEnqueue(Microsoft.UI.Dispatching.DispatcherQueuePriority.Low, PinToEnd);
        var t = DispatcherQueue.CreateTimer(); t.Interval = TimeSpan.FromMilliseconds(400); t.IsRepeating = false;
        t.Tick += (_, _) => PinToEnd(); t.Start();
    }

    private void PinToEnd()
    {
        for (var d = (DependencyObject)MessageList; d != null; )
        {
            if (d is ScrollViewer sv) { sv.UpdateLayout(); sv.ChangeView(null, sv.ScrollableHeight, null, true); return; }
            d = Microsoft.UI.Xaml.Media.VisualTreeHelper.GetChildrenCount(d) > 0 ? Microsoft.UI.Xaml.Media.VisualTreeHelper.GetChild(d, 0) : null;
        }
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

    private async void Composer_PreviewKeyDown(object sender, KeyRoutedEventArgs e)
    {
        // Ctrl+V: paste image/files from clipboard into the open chat
        if (e.Key == VirtualKey.V && Microsoft.UI.Input.InputKeyboardSource.GetKeyStateForCurrentThread(VirtualKey.Control).HasFlag(Windows.UI.Core.CoreVirtualKeyStates.Down))
        {
            e.Handled = true;
            await PasteClipboardAsync();
            return;
        }
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
    private async void Image_Tapped(object sender, TappedRoutedEventArgs e)
    {
        e.Handled = true;
        var m = ItemOf(sender);
        if (m == null || string.IsNullOrEmpty(m.FilePath) || !System.IO.File.Exists(m.FilePath)) return;
        // in-app fullscreen viewer (Esc / click closes); "在文件夹中显示" remains on the file card
        var img = new Image { Source = new Microsoft.UI.Xaml.Media.Imaging.BitmapImage(new Uri(m.FilePath)), Stretch = Stretch.Uniform, MaxWidth = 1200, MaxHeight = 800 };
        var box = new Border { Background = new SolidColorBrush(Microsoft.UI.Colors.Black), Child = img, Padding = new Thickness(8) };
        var dlg = new ContentDialog
        {
            XamlRoot = Content.XamlRoot,
            Title = m.FileName,
            Content = new ScrollViewer { Content = box, ZoomMode = ZoomMode.Enabled, MinZoomFactor = 0.5f, MaxZoomFactor = 4, HorizontalScrollMode = ScrollMode.Auto, VerticalScrollMode = ScrollMode.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Auto, VerticalScrollBarVisibility = ScrollBarVisibility.Auto },
            CloseButtonText = "关闭",
            DefaultButton = ContentDialogButton.Close,
        };
        box.Tapped += (_, __) => { try { dlg.Hide(); } catch { } }; // click image closes (Esc via Close button)
        try { await Services.ThemeService.Prepare(dlg).ShowAsync(); } catch { }
    }

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
        await SendDroppedAsync(await e.DataView.GetStorageItemsAsync());
    }

    /// <summary>Ctrl+V: send clipboard bitmap or file paths (same trust gate as drag-drop).</summary>
    private async Task PasteClipboardAsync()
    {
        if (!VM.HasSelection) { VM.ShowStatus("请先选择联系人或群组", true); return; }
        try
        {
            var content = Clipboard.GetContent();
            if (content.Contains(StandardDataFormats.StorageItems))
            {
                var items = await content.GetStorageItemsAsync();
                await SendDroppedAsync(items);
                return;
            }
            if (content.Contains(StandardDataFormats.Bitmap))
            {
                var streamRef = await content.GetBitmapAsync();
                using var stream = await streamRef.OpenReadAsync();
                var dir = Path.Combine(Path.GetTempPath(), "lsm-paste");
                Directory.CreateDirectory(dir);
                var path = Path.Combine(dir, $"paste-{DateTime.Now:yyyyMMdd-HHmmss}.bmp");
                using (var outFile = File.Create(path))
                {
                    var reader = new Windows.Storage.Streams.DataReader(stream);
                    await reader.LoadAsync((uint)stream.Size);
                    var bytes = new byte[stream.Size];
                    reader.ReadBytes(bytes);
                    await outFile.WriteAsync(bytes, 0, bytes.Length);
                }
                // Bitmap from clipboard is often BMP/DIB in a random stream; if decode fails the send still tries the bytes.
                await VM.SendFilesAsync(new[] { path });
            }
        }
        catch (Exception ex) { VM.ShowStatus("粘贴失败：" + ex.Message, true); }
    }

    /// <summary>Everything a drop does after the OLE transfer (also driven by the W8 smoke check).</summary>
    internal async Task SendDroppedAsync(IReadOnlyList<IStorageItem> items)
    {
        if (!VM.HasSelection) return;
        var paths = items.OfType<StorageFile>().Select(f => f.Path).Where(p => !string.IsNullOrEmpty(p)).ToList();
        if (items.OfType<StorageFolder>().Any()) VM.ShowStatus("暂不支持直接发送文件夹，请先压缩", true);
        if (paths.Count > 0) await VM.SendFilesAsync(paths);
    }

    private void CopyPeerFp_Click(object sender, RoutedEventArgs e)
    {
        var fp = VM.SelectedConversation?.PeerFingerprint;
        if (!string.IsNullOrEmpty(fp)) { App.Platform.CopyToClipboard(fp); VM.ShowStatus("已复制对方指纹"); }
    }

    private async void ClearChat_Click(object sender, RoutedEventArgs e)
    {
        if (VM.SelectedConversation == null) return;
        var d = Services.ThemeService.Prepare(new ContentDialog { XamlRoot = XamlRoot, Title = "清空聊天记录？", Content = "将删除本机上与此会话的全部聊天记录（对方仍保留）。此操作无法撤销。",
            PrimaryButtonText = "清空", CloseButtonText = "取消", DefaultButton = ContentDialogButton.Close });
        if (await d.ShowAsync() == ContentDialogResult.Primary) await VM.ClearConversationAsync();
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
