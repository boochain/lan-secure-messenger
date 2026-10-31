using System.Text.Json;
using LanMessenger.Core.Models;
using LanMessenger.Core.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Graphics.Imaging;
using Windows.Storage;
using Windows.Storage.Streams;

namespace LanMessenger.Views;

/// <summary>v0.6.1 P0: 消息历史（搜索+筛选+分页+跳转）、个人资料（头像/签名）、联系人资料与备注名。Same core RPCs on every edition.</summary>
internal static class P0Dialogs
{
    static MainViewModel VM => App.VM;

    static ComboBox Combo(params string[] items) { var c = new ComboBox { MinWidth = 110 }; foreach (var i in items) c.Items.Add(i); c.SelectedIndex = 0; return c; }

    public static async Task ShowHistoryAsync(XamlRoot root, ChatPage? page, string? conv)
    {
        if (VM.Client is not { } cl) return;
        var q = new TextBox { PlaceholderText = "搜索消息（支持中文）", MinWidth = 420 };
        var convBox = new ComboBox { MinWidth = 150 }; var convIds = new List<string> { "" }; convBox.Items.Add("全部会话");
        foreach (var c in VM.Conversations) { convIds.Add(c.Id); convBox.Items.Add(c.Title); }
        convBox.SelectedIndex = Math.Max(0, conv == null ? 0 : convIds.IndexOf(conv));
        var type = Combo("全部类型", "文字", "图片", "文件", "贴纸"); var dir = Combo("收发", "收到的", "发出的"); var stat = Combo("全部状态", "已读", "失败/拒收", "已拒收");
        var from = new CalendarDatePicker { PlaceholderText = "起始日期" }; var to = new CalendarDatePicker { PlaceholderText = "结束日期" };
        var count = new TextBlock { Opacity = 0.8 };
        var list = new ListView { Height = 380, SelectionMode = ListViewSelectionMode.None, IsItemClickEnabled = true };
        var more = new Button { Content = "加载更多", HorizontalAlignment = HorizontalAlignment.Stretch };
        var items = new List<Message>(); int total = 0;
        ContentDialog? dlg = null;
        async Task Run(bool append)
        {
            if (!append) { items.Clear(); list.Items.Clear(); }
            var hq = new HistoryQuery { Query = q.Text, Offset = items.Count, Limit = 50 };
            if (convBox.SelectedIndex > 0) hq.Conversations.Add(convIds[convBox.SelectedIndex]);
            if (type.SelectedIndex > 0) hq.Types.Add(new[] { "", "text", "image", "file", "sticker" }[type.SelectedIndex]);
            hq.Direction = new[] { "", "in", "out" }[Math.Max(0, dir.SelectedIndex)];
            switch (stat.SelectedIndex) { case 1: hq.Statuses.Add("read"); break; case 2: hq.Statuses.AddRange(new[] { "failed", "rejected" }); break; case 3: hq.Statuses.Add("rejected"); break; }
            if (from.Date is { } f) hq.FromMs = new DateTimeOffset(f.Date, TimeZoneInfo.Local.GetUtcOffset(f.Date)).ToUnixTimeMilliseconds();
            if (to.Date is { } t) hq.ToMs = new DateTimeOffset(t.Date.AddDays(1), TimeZoneInfo.Local.GetUtcOffset(t.Date)).ToUnixTimeMilliseconds();
            try
            {
                var r = await cl.HistoryQueryAsync(hq); total = r.Total;
                foreach (var m in r.Items)
                {
                    items.Add(m);
                    var title = VM.Conversations.FirstOrDefault(c => c.Id == m.Conversation)?.Title ?? "";
                    var when = DateTimeOffset.FromUnixTimeMilliseconds(m.TsMs).ToLocalTime().ToString("yyyy-MM-dd HH:mm");
                    var who = m.Direction == Direction.Out ? "我" : m.SenderName;
                    var body = m.Kind == MsgKind.File ? "📎 " + (m.File?.Name ?? "") : m.Body.Replace('\n', ' ');
                    var sp = new StackPanel { Padding = new Thickness(4, 6, 4, 6), Tag = m };
                    sp.Children.Add(new TextBlock { Text = $"{when} · {title} · {who}", FontSize = 12, Opacity = 0.75 });
                    sp.Children.Add(new TextBlock { Text = body, TextTrimming = TextTrimming.CharacterEllipsis, MaxLines = 2, TextWrapping = TextWrapping.Wrap });
                    list.Items.Add(sp);
                }
            }
            catch (Exception ex) { VM.ShowStatus("搜索失败：" + ex.Message); }
            count.Text = $"共 {total} 条，已显示 {items.Count} 条（点击跳转到聊天）";
            more.Visibility = items.Count < total ? Visibility.Visible : Visibility.Collapsed;
        }
        q.TextChanged += async (_, _) => await Run(false);
        foreach (var cb in new[] { convBox, type, dir, stat }) cb.SelectionChanged += async (_, _) => await Run(false);
        from.DateChanged += async (_, _) => await Run(false); to.DateChanged += async (_, _) => await Run(false);
        more.Click += async (_, _) => await Run(true);
        list.ItemClick += (_, e) => { if (e.ClickedItem is StackPanel { Tag: Message m }) { dlg?.Hide(); page?.JumpTo(m.Conversation, m.Id); } };
        var filters = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 }; foreach (var c in new UIElement[] { convBox, type, dir, stat }) filters.Children.Add(c);
        var dates = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 }; dates.Children.Add(from); dates.Children.Add(new TextBlock { Text = "至", VerticalAlignment = VerticalAlignment.Center }); dates.Children.Add(to);
        var root2 = new StackPanel { Spacing = 8, MinWidth = 560 }; foreach (var c in new UIElement[] { q, filters, dates, count, list, more }) root2.Children.Add(c);
        dlg = Services.ThemeService.Prepare(new ContentDialog { XamlRoot = root, Title = "消息历史", Content = root2, CloseButtonText = "关闭", DefaultButton = ContentDialogButton.Close });
        dlg.Resources["ContentDialogMaxWidth"] = 720.0;
        _ = Run(false);
        await dlg.ShowAsync();
    }

    static UIElement Avatar(string? path, string name)
    {
        var pp = new PersonPicture { Width = 96, Height = 96, DisplayName = name };
        if (!string.IsNullOrEmpty(path) && File.Exists(path)) { try { var bi = new BitmapImage(); using var fs = File.OpenRead(path); var ms = new InMemoryRandomAccessStream(); fs.CopyTo(ms.AsStreamForWrite()); ms.Seek(0); bi.SetSource(ms); pp.ProfilePicture = bi; } catch { } }
        return pp;
    }
    static TextBlock KV(string k, string v) { var t = new TextBlock { TextWrapping = TextWrapping.Wrap, IsTextSelectionEnabled = true }; t.Inlines.Add(new Microsoft.UI.Xaml.Documents.Run { Text = k + "：", FontWeight = Microsoft.UI.Text.FontWeights.SemiBold }); t.Inlines.Add(new Microsoft.UI.Xaml.Documents.Run { Text = string.IsNullOrEmpty(v) ? "—" : v }); return t; }

    /// <summary>Downscale to a 96 px PNG so it fits the 48 KB wire limit.</summary>
    static async Task<string> ShrinkAsync(StorageFile f)
    {
        using var src = await f.OpenReadAsync();
        var dec = await BitmapDecoder.CreateAsync(src);
        var k = 96.0 / Math.Max(dec.PixelWidth, dec.PixelHeight);
        var tmp = Path.Combine(Path.GetTempPath(), $"lsm-avatar-{Environment.ProcessId}.png");
        // simple path: decode scaled pixels and encode PNG
        var sb = await dec.GetSoftwareBitmapAsync(BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied, new BitmapTransform { ScaledWidth = (uint)Math.Max(1, dec.PixelWidth * Math.Min(1, k)), ScaledHeight = (uint)Math.Max(1, dec.PixelHeight * Math.Min(1, k)), InterpolationMode = BitmapInterpolationMode.Fant }, ExifOrientationMode.RespectExifOrientation, ColorManagementMode.DoNotColorManage);
        using (var fs = File.Create(tmp)) using (var ras = fs.AsRandomAccessStream())
        {
            var e2 = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, ras); e2.SetSoftwareBitmap(sb); await e2.FlushAsync();
        }
        return tmp;
    }

    public static async Task ShowMyProfileAsync(XamlRoot root)
    {
        if (VM.Client is not { } cl) return;
        Me me; try { me = await cl.Connection.CallAsync<Me>("get_profile"); } catch (Exception ex) { VM.ShowStatus("读取失败：" + ex.Message); return; }
        var name = new TextBox { Header = "昵称", Text = me.Name, MaxLength = 40 };
        var sig = new TextBox { Header = "个性签名（最多 120 字）", Text = me.Signature, MaxLength = 120, TextWrapping = TextWrapping.Wrap, AcceptsReturn = false };
        var pick = new Button { Content = "更换头像…" }; var rm = new Button { Content = "移除头像" };
        var av = new ContentControl { Content = Avatar(me.AvatarPath, me.Name), HorizontalAlignment = HorizontalAlignment.Center };
        pick.Click += async (_, _) =>
        {
            var p = new Windows.Storage.Pickers.FileOpenPicker(); foreach (var e in new[] { ".png", ".jpg", ".jpeg", ".bmp", ".gif" }) p.FileTypeFilter.Add(e);
            WinRT.Interop.InitializeWithWindow.Initialize(p, WinRT.Interop.WindowNative.GetWindowHandle(App.MainWin!));
            var f = await p.PickSingleFileAsync(); if (f == null) return;
            try { var tmp = await ShrinkAsync(f); var m2 = await cl.SetProfileAsync(null, tmp); File.Delete(tmp); av.Content = Avatar(m2.AvatarPath, m2.Name); VM.ShowStatus("头像已更新"); }
            catch (Exception ex) { VM.ShowStatus("头像：" + ex.Message); }
        };
        rm.Click += async (_, _) => { try { var m2 = await cl.SetProfileAsync(null, ""); av.Content = Avatar(null, m2.Name); } catch (Exception ex) { VM.ShowStatus(ex.Message); } };
        var btns = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, HorizontalAlignment = HorizontalAlignment.Center }; btns.Children.Add(pick); btns.Children.Add(rm);
        var sp = new StackPanel { Spacing = 10, MinWidth = 420 };
        foreach (var c in new UIElement[] { av, btns, name, sig, KV("指纹", me.Fingerprint), KV("账号", me.AccountUser), KV("版本", me.Version) }) sp.Children.Add(c);
        var d = Services.ThemeService.Prepare(new ContentDialog { XamlRoot = root, Title = "个人资料", Content = new ScrollViewer { Content = sp }, PrimaryButtonText = "保存", CloseButtonText = "取消", DefaultButton = ContentDialogButton.Primary });
        if (await d.ShowAsync() != ContentDialogResult.Primary) return;
        try
        {
            if (name.Text.Trim() != me.Name && name.Text.Trim().Length > 0) await cl.SetDisplayNameAsync(name.Text.Trim());
            await cl.SetProfileAsync(sig.Text.Trim(), null); VM.ShowStatus("个人资料已保存");
        }
        catch (Exception ex) { VM.ShowStatus("保存失败：" + ex.Message); }
    }

    public static async Task ShowContactProfileAsync(XamlRoot root, ChatPage? page, string fp)
    {
        if (VM.Client is not { } cl) return;
        JsonElement r; try { r = await cl.ContactProfileAsync(fp); } catch (Exception ex) { VM.ShowStatus("读取失败：" + ex.Message); return; }
        var peer = r.GetProperty("peer").Deserialize<Peer>(Json.Options)!;
        var media = r.GetProperty("recent_media").Deserialize<List<Message>>(Json.Options) ?? new();
        var shared = r.GetProperty("shared_files").GetInt32();
        var remark = new TextBox { Header = "备注名（仅本机可见，留空显示对方昵称）", Text = peer.Remark, MaxLength = 64 };
        var sp = new StackPanel { Spacing = 8, MinWidth = 440 };
        sp.Children.Add(new ContentControl { Content = Avatar(peer.AvatarPath, peer.Name), HorizontalAlignment = HorizontalAlignment.Center });
        sp.Children.Add(new TextBlock { Text = peer.Name, Style = (Style)Application.Current.Resources["SubtitleTextBlockStyle"], HorizontalAlignment = HorizontalAlignment.Center });
        sp.Children.Add(remark);
        foreach (var kv in new[] { ("昵称", string.IsNullOrEmpty(peer.Nickname) ? peer.Name : peer.Nickname), ("个性签名", peer.ProfileSync || peer.Signature.Length > 0 ? peer.Signature : "（对方版本较旧，不支持资料同步）"),
                 ("状态", (peer.Online ? "在线" : "离线") + (peer.Online ? (peer.ViaRelay ? " · 经服务器中转" : " · 局域网直连") : "")), ("身份", peer.Trust == Trust.Trusted ? "已验证身份" : peer.Trust == Trust.Blocked ? "已屏蔽" : "未验证"),
                 ("版本", peer.AppVersion), ("指纹", peer.Fingerprint) }) sp.Children.Add(KV(kv.Item1, kv.Item2));
        sp.Children.Add(new TextBlock { Text = $"共享的文件与图片（{shared}）", FontWeight = Microsoft.UI.Text.FontWeights.SemiBold, Margin = new Thickness(0, 8, 0, 0) });
        var lv = new ListView { MaxHeight = 220, IsItemClickEnabled = true, SelectionMode = ListViewSelectionMode.None };
        foreach (var m in media) lv.Items.Add(new TextBlock { Text = "📎 " + (m.File?.Name ?? "") + "  " + DateTimeOffset.FromUnixTimeMilliseconds(m.TsMs).ToLocalTime().ToString("MM-dd HH:mm"), Tag = m });
        if (media.Count == 0) sp.Children.Add(new TextBlock { Text = "暂无", Opacity = 0.7 }); else sp.Children.Add(lv);
        ContentDialog? d = null;
        lv.ItemClick += (_, e) => { if (e.ClickedItem is TextBlock { Tag: Message m }) { d?.Hide(); page?.JumpTo(m.Conversation, m.Id); } };
        d = Services.ThemeService.Prepare(new ContentDialog { XamlRoot = root, Title = "联系人资料", Content = new ScrollViewer { Content = sp }, PrimaryButtonText = "保存备注", SecondaryButtonText = "消息历史", CloseButtonText = "发消息", DefaultButton = ContentDialogButton.Primary });
        var res = await d.ShowAsync();
        if (res == ContentDialogResult.Primary) { try { await cl.SetRemarkAsync(fp, remark.Text.Trim()); VM.ShowStatus("备注名已保存"); } catch (Exception ex) { VM.ShowStatus(ex.Message); } }
        else if (res == ContentDialogResult.Secondary) await ShowHistoryAsync(root, page, "dm:" + fp);
    }
}
