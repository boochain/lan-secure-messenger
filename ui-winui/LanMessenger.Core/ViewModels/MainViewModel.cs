using System.Collections.ObjectModel;
using System.Net.Http;
using System.Text.Json;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using LanMessenger.Core.Ipc;
using LanMessenger.Core.Models;

namespace LanMessenger.Core.ViewModels;

/// <summary>
/// Single source of UI state. Everything shown is derived from the daemon's snapshot and
/// pushed events; user actions are forwarded as JSON-RPC calls.
/// </summary>
public partial class MainViewModel : ObservableObject
{
    private readonly IDispatcher _ui;
    private readonly IPlatform _platform;
    private readonly string _endpoint;
    private DaemonClient? _client;
    private bool _stopping;
    private int _reconnecting;

    public ObservableCollection<ConversationItem> Conversations { get; } = new();
    public ObservableCollection<PeerItem> Peers { get; } = new();
    public ObservableCollection<GroupItem> Groups { get; } = new();
    public ObservableCollection<TransferItem> Transfers { get; } = new();
    public ObservableCollection<MessageItem> Messages { get; } = new();
    public ObservableCollection<MessageItem> SearchResults { get; } = new();
    public ObservableCollection<InterfaceItem> Interfaces { get; } = new();
    public ObservableCollection<PeerDiagItem> PeerDiags { get; } = new();
    public ObservableCollection<string> ManualPeers { get; } = new();

    public DaemonClient? Client => _client;
    public string Endpoint => _endpoint;

    /// <summary>Raised on the UI thread after messages were appended (views scroll to bottom).</summary>
    public event Action? MessagesAppended;
    /// <summary>Raised when the first-run page should be shown.</summary>
    public event Action? FirstRunRequested;

    public MainViewModel(IDispatcher ui, IPlatform platform, string? endpoint = null)
    {
        _ui = ui;
        _platform = platform;
        _endpoint = endpoint ?? Ipc.Endpoint.Default();
    }

    // ------------------------------------------------------------ observable state
    [ObservableProperty] private bool _isConnected;
    [ObservableProperty] private string _connectionText = "正在连接后台服务…";
    [ObservableProperty] private string _statusMessage = "";
    [ObservableProperty] private bool _statusIsError;
    [ObservableProperty] private bool _hasStatus;

    [ObservableProperty] private string _myName = "";
    [ObservableProperty] private string _myFingerprint = "";
    [ObservableProperty] private string _version = "";
    [ObservableProperty] private string _logDir = "";
    [ObservableProperty] private string _dataDir = "";
    [ObservableProperty] private string _downloadDir = "";
    [ObservableProperty] private string _portsText = "";
    [ObservableProperty] private bool _firstRunNeeded;

    [ObservableProperty] private ConversationItem? _selectedConversation;
    [ObservableProperty] private bool _hasSelection;
    [ObservableProperty] private string _selectedTitle = "";
    [ObservableProperty] private string _selectedSubtitle = "";
    [ObservableProperty] private bool _selectedIsDm;
    [ObservableProperty] private bool _selectedTrusted;
    [ObservableProperty] private bool _selectedBlocked;
    [ObservableProperty] private bool _selectedMuted;
    [ObservableProperty] private string _selectedTrustText = "";
    [ObservableProperty] private string _composeText = "";
    [ObservableProperty] private string _addPeerAddress = "";
    [ObservableProperty] private string _searchText = "";
    [ObservableProperty] private bool _hasTransfersActive;
    [ObservableProperty] private string _transfersSummary = "";
    [ObservableProperty] private int _onlineCount;

    // Settings editor
    [ObservableProperty] private string _editDisplayName = "";
    [ObservableProperty] private string _editDownloadDir = "";
    [ObservableProperty] private bool _editAutoAccept = true;
    [ObservableProperty] private bool _editHistory = true;
    [ObservableProperty] private bool _editDiscovery = true;
    [ObservableProperty] private bool _editMulticast = true;
    [ObservableProperty] private string _editTargets = "";
    [ObservableProperty] private string _editInclude = "";
    [ObservableProperty] private string _editExclude = "";
    [ObservableProperty] private double _editMaxFileMb = 4096;
    [ObservableProperty] private bool _editCheckUpdates = true;
    [ObservableProperty] private string _updateText = "";
    [ObservableProperty] private string _pinText = "";

    // Diagnostics
    [ObservableProperty] private string _firewallSummary = "尚未检测";
    [ObservableProperty] private bool _firewallOk;
    [ObservableProperty] private bool _publicNetwork;
    [ObservableProperty] private string _firewallAdvice = "";
    [ObservableProperty] private string _profilesText = "";
    [ObservableProperty] private string _discoveryText = "";
    [ObservableProperty] private string _diagnosticsJson = "";
    [ObservableProperty] private bool _includePublic;
    [ObservableProperty] private bool _diagBusy;

    partial void OnStatusMessageChanged(string value) => HasStatus = !string.IsNullOrEmpty(value);

    // ------------------------------------------------------------ connection
    public async Task StartAsync()
    {
        await ConnectLoopAsync();
    }

    private async Task ConnectLoopAsync()
    {
        if (Interlocked.Exchange(ref _reconnecting, 1) == 1) return;
        try
        {
            var started = false;
            var deadline = DateTime.UtcNow.AddSeconds(25);
            while (!_stopping)
            {
                try
                {
                    var conn = await DaemonConnection.ConnectAsync(_endpoint, TimeSpan.FromMilliseconds(1500));
                    await OnConnectedAsync(conn);
                    return;
                }
                catch (Exception e) when (e is not OperationCanceledException || !_stopping)
                {
                    if (!started)
                    {
                        started = true;
                        _ui.Post(() => ConnectionText = "正在启动后台服务 lan-daemon…");
                        if (!_platform.StartDaemon(_endpoint))
                        {
                            _ui.Post(() =>
                            {
                                ConnectionText = "找不到 lan-daemon.exe，请确认它与 LanMessenger.exe 在同一文件夹";
                                ShowStatus(ConnectionText, true);
                            });
                        }
                    }
                    if (DateTime.UtcNow > deadline)
                    {
                        _ui.Post(() =>
                        {
                            ConnectionText = "无法连接后台服务，正在重试…（详见日志）";
                        });
                        deadline = DateTime.UtcNow.AddSeconds(25);
                        started = false;
                    }
                    await Task.Delay(400);
                }
            }
        }
        finally
        {
            Interlocked.Exchange(ref _reconnecting, 0);
        }
    }

    private async Task OnConnectedAsync(DaemonConnection conn)
    {
        var client = new DaemonClient(conn);
        conn.EventReceived += e => _ui.Post(() => ApplyEvent(e));
        conn.Disconnected += _ => _ui.Post(OnDisconnected);
        await client.HelloAsync();
        await client.SubscribeAsync();
        var snap = await client.GetSnapshotAsync();
        var tcs = new TaskCompletionSource();
        _ui.Post(() =>
        {
            _client = client;
            IsConnected = true;
            ConnectionText = "已连接";
            ApplySnapshot(snap);
            tcs.TrySetResult();
        });
        await tcs.Task;
        _ = RefreshDiagnosticsAsync(skipFirewall: true);
    }

    private void OnDisconnected()
    {
        if (_stopping) return;
        IsConnected = false;
        _client = null;
        ConnectionText = "与后台服务断开，正在重新连接…";
        _ = Task.Run(ConnectLoopAsync);
    }

    public async Task StopAsync(bool shutdownDaemon)
    {
        _stopping = true;
        var c = _client;
        if (c == null) return;
        try
        {
            if (shutdownDaemon) await c.ShutdownAsync();
        }
        catch { }
        await c.Connection.DisposeAsync();
    }

    // ------------------------------------------------------------ snapshot / events
    public void ApplySnapshot(Snapshot s)
    {
        ApplyMe(s.Me);
        ApplySettings(s.Settings);
        Sync(Peers, s.Peers, p => p.Fingerprint, (p) => new PeerItem(p), (i, p) => i.Update(p), (i) => i.Fingerprint);
        Sync(Groups, s.Groups, g => g.Id, g => new GroupItem(g), (i, g) => i.Update(g), i => i.Id);
        Conversations.Clear();
        foreach (var c in s.Conversations.OrderByDescending(c => c.LastTsMs)) Conversations.Add(new ConversationItem(c));
        Transfers.Clear();
        foreach (var t in s.Transfers.OrderByDescending(t => t.StartedMs)) Transfers.Add(new TransferItem(t));
        UpdateCounters();
        if (SelectedConversation != null)
        {
            var again = Conversations.FirstOrDefault(c => c.Id == SelectedConversation.Id);
            SelectedConversation = again;
        }
        if (!s.Me.FirstRunDone && !FirstRunNeeded)
        {
            FirstRunNeeded = true;
            FirstRunRequested?.Invoke();
        }
    }

    private static void Sync<TItem, TModel>(ObservableCollection<TItem> col, List<TModel> models, Func<TModel, string> key,
        Func<TModel, TItem> create, Action<TItem, TModel> update, Func<TItem, string> itemKey)
    {
        var keys = models.Select(key).ToHashSet();
        for (int i = col.Count - 1; i >= 0; i--)
            if (!keys.Contains(itemKey(col[i]))) col.RemoveAt(i);
        foreach (var m in models)
        {
            var existing = col.FirstOrDefault(x => itemKey(x) == key(m));
            if (existing == null) col.Add(create(m));
            else update(existing, m);
        }
    }

    private void ApplyMe(Me me)
    {
        MyName = me.Name;
        MyFingerprint = me.Fingerprint;
        Version = me.Version;
        LogDir = me.LogDir;
        DataDir = me.DataDir;
        DownloadDir = me.DownloadDir;
        PortsText = $"TCP {me.ListenPort} · UDP 发现 {me.DiscoveryPort}";
        FirstRunNeeded = !me.FirstRunDone && FirstRunNeeded;
    }

    private void ApplySettings(Settings s)
    {
        EditDisplayName = s.DisplayName;
        EditDownloadDir = s.DownloadDir ?? "";
        EditAutoAccept = s.AutoAcceptFiles;
        EditHistory = s.HistoryEnabled;
        EditDiscovery = s.DiscoveryEnabled;
        EditMulticast = s.MulticastEnabled;
        EditTargets = string.Join(Environment.NewLine, s.AnnounceTargets);
        EditInclude = string.Join(", ", s.IncludeInterfaces);
        EditExclude = string.Join(", ", s.ExcludeInterfaces);
        EditMaxFileMb = s.MaxFileBytes / 1024.0 / 1024.0;
        EditCheckUpdates = s.CheckUpdates;
    }

    public void ApplyEvent(DaemonEvent e)
    {
        switch (e.Type)
        {
            case "peer_updated":
            {
                var p = e.Get<Peer>("peer");
                if (p == null) break;
                var item = Peers.FirstOrDefault(x => x.Fingerprint == p.Fingerprint);
                if (item == null) Peers.Add(new PeerItem(p)); else item.Update(p);
                if (SelectedConversation?.PeerFingerprint == p.Fingerprint) UpdateSelectionHeader();
                UpdateCounters();
                break;
            }
            case "peer_removed":
            {
                var fp = e.GetString("fingerprint");
                var item = Peers.FirstOrDefault(x => x.Fingerprint == fp);
                if (item != null) Peers.Remove(item);
                var conv = Conversations.FirstOrDefault(c => c.PeerFingerprint == fp);
                if (conv != null)
                {
                    if (SelectedConversation == conv) SelectedConversation = null;
                    Conversations.Remove(conv);
                }
                UpdateCounters();
                break;
            }
            case "conversation_updated":
            {
                var c = e.Get<Conversation>("conversation");
                if (c == null) break;
                var item = Conversations.FirstOrDefault(x => x.Id == c.Id);
                if (item == null)
                {
                    item = new ConversationItem(c);
                    Conversations.Add(item);
                }
                else item.Update(c);
                // Keep newest first (Move keeps the ListView selection intact).
                var idx = Conversations.IndexOf(item);
                var target = Conversations.Count(x => x != item && x.LastTsMs > item.LastTsMs);
                if (target != idx) Conversations.Move(idx, target);
                if (SelectedConversation == item) UpdateSelectionHeader();
                break;
            }
            case "message_added":
            {
                var m = e.Get<Message>("message");
                if (m == null) break;
                if (SelectedConversation?.Id == m.Conversation && Messages.All(x => x.Id != m.Id))
                {
                    Messages.Add(new MessageItem(m));
                    MessagesAppended?.Invoke();
                    if (m.Direction == Direction.In && _platform.IsWindowActive) _ = SafeCall(c => c.MarkReadAsync(m.Conversation));
                }
                break;
            }
            case "message_updated":
            {
                var m = e.Get<Message>("message");
                if (m == null) break;
                Messages.FirstOrDefault(x => x.Id == m.Id)?.Update(m);
                break;
            }
            case "transfer_updated":
            {
                var t = e.Get<Transfer>("transfer");
                if (t == null) break;
                var item = Transfers.FirstOrDefault(x => x.Id == t.Id);
                if (item == null) Transfers.Insert(0, new TransferItem(t)); else item.Update(t);
                UpdateCounters();
                break;
            }
            case "group_updated":
            {
                var g = e.Get<Group>("group");
                if (g == null) break;
                var item = Groups.FirstOrDefault(x => x.Id == g.Id);
                if (item == null) Groups.Add(new GroupItem(g)); else item.Update(g);
                if (SelectedConversation?.GroupId == g.Id) UpdateSelectionHeader();
                break;
            }
            case "group_removed":
            {
                var id = e.GetString("id");
                var item = Groups.FirstOrDefault(x => x.Id == id);
                if (item != null) Groups.Remove(item);
                var conv = Conversations.FirstOrDefault(c => c.GroupId == id);
                if (conv != null)
                {
                    if (SelectedConversation == conv) SelectedConversation = null;
                    Conversations.Remove(conv);
                }
                break;
            }
            case "me_updated":
            {
                var me = e.Get<Me>("me");
                if (me != null) ApplyMe(me);
                break;
            }
            case "settings_updated":
            {
                var s = e.Get<Settings>("settings");
                if (s != null) ApplySettings(s);
                break;
            }
            case "notice":
                ShowStatus(e.GetString("text"), e.GetString("level") is "warn" or "error");
                break;
            case "notify":
            {
                var conv = e.GetString("conversation");
                var focused = _platform.IsWindowActive && SelectedConversation?.Id == conv;
                if (!focused) _platform.ShowToast(e.GetString("title"), e.GetString("body"), conv);
                break;
            }
            case "resync":
                _ = ResyncAsync();
                break;
        }
    }

    private async Task ResyncAsync()
    {
        var c = _client;
        if (c == null) return;
        try
        {
            var s = await c.GetSnapshotAsync();
            _ui.Post(() => ApplySnapshot(s));
        }
        catch { }
    }

    private void UpdateCounters()
    {
        OnlineCount = Peers.Count(p => p.Online);
        var active = Transfers.Count(t => t.IsActive);
        HasTransfersActive = active > 0;
        TransfersSummary = active > 0 ? $"{active} 个传输进行中" : "";
    }

    public void ShowStatus(string text, bool error = false)
    {
        StatusIsError = error;
        StatusMessage = text;
    }

    private async Task SafeCall(Func<DaemonClient, Task> f, string? okText = null)
    {
        var c = _client;
        if (c == null)
        {
            ShowStatus("后台服务未连接", true);
            return;
        }
        try
        {
            await f(c);
            if (okText != null) _ui.Post(() => ShowStatus(okText));
        }
        catch (Exception e)
        {
            _ui.Post(() => ShowStatus(e.Message, true));
        }
    }

    // ------------------------------------------------------------ selection
    partial void OnSelectedConversationChanged(ConversationItem? value)
    {
        HasSelection = value != null;
        Messages.Clear();
        UpdateSelectionHeader();
        if (value != null) _ = LoadMessagesAsync(value.Id);
    }

    public void SelectConversation(string id)
    {
        var c = Conversations.FirstOrDefault(x => x.Id == id);
        if (c != null) SelectedConversation = c;
    }

    public void SelectPeer(string fingerprint) => SelectConversation("dm:" + fingerprint);

    private void UpdateSelectionHeader()
    {
        var c = SelectedConversation;
        if (c == null)
        {
            SelectedTitle = "";
            SelectedSubtitle = "";
            SelectedIsDm = false;
            return;
        }
        SelectedTitle = c.Title;
        SelectedIsDm = c.IsDm;
        SelectedMuted = c.Muted;
        if (c.IsDm)
        {
            var p = Peers.FirstOrDefault(x => x.Fingerprint == c.PeerFingerprint);
            SelectedTrusted = p?.IsTrusted ?? false;
            SelectedBlocked = p?.IsBlocked ?? false;
            SelectedTrustText = p?.TrustText ?? "";
            SelectedSubtitle = p == null ? c.PeerFingerprint : $"{p.StateText} · {p.TrustText} · 指纹 {p.Fingerprint}";
        }
        else
        {
            var g = Groups.FirstOrDefault(x => x.Id == c.GroupId);
            SelectedSubtitle = g == null ? "" : $"{g.MembersText} · {g.EpochText}";
        }
    }

    private async Task LoadMessagesAsync(string conv)
    {
        var c = _client;
        if (c == null) return;
        try
        {
            var list = await c.GetMessagesAsync(conv, 300);
            _ui.Post(() =>
            {
                if (SelectedConversation?.Id != conv) return;
                Messages.Clear();
                foreach (var m in list) Messages.Add(new MessageItem(m));
                MessagesAppended?.Invoke();
            });
            await c.MarkReadAsync(conv);
        }
        catch (Exception e)
        {
            _ui.Post(() => ShowStatus(e.Message, true));
        }
    }

    public void OnWindowActivated()
    {
        var c = SelectedConversation;
        if (c != null && c.HasUnread) _ = SafeCall(x => x.MarkReadAsync(c.Id));
    }

    // ------------------------------------------------------------ commands: chat
    [RelayCommand]
    private async Task SendAsync()
    {
        var c = SelectedConversation;
        var text = ComposeText;
        if (c == null || string.IsNullOrWhiteSpace(text)) return;
        ComposeText = "";
        var client = _client;
        if (client == null)
        {
            ComposeText = text;
            ShowStatus("后台服务未连接", true);
            return;
        }
        try
        {
            await client.SendTextAsync(c.Id, text.TrimEnd());
        }
        catch (Exception e)
        {
            _ui.Post(() =>
            {
                ComposeText = text;
                ShowStatus("发送失败：" + e.Message, true);
            });
        }
    }

    [RelayCommand]
    private async Task AttachAsync()
    {
        if (SelectedConversation == null) return;
        var files = await _platform.PickFilesAsync();
        await SendFilesAsync(files);
    }

    public async Task SendFilesAsync(IEnumerable<string> paths)
    {
        var c = SelectedConversation;
        if (c == null) return;
        foreach (var p in paths)
            await SafeCall(x => x.SendFileAsync(c.Id, p));
    }

    [RelayCommand]
    private void OpenMessageFile(MessageItem? m)
    {
        if (m == null || string.IsNullOrEmpty(m.FilePath)) return;
        if (File.Exists(m.FilePath)) _platform.OpenPath(m.FilePath);
        else ShowStatus("文件尚未接收完成或已被移动", true);
    }

    [RelayCommand]
    private void RevealMessageFile(MessageItem? m)
    {
        if (m != null && !string.IsNullOrEmpty(m.FilePath)) _platform.RevealInFolder(m.FilePath);
    }

    [RelayCommand]
    private async Task AddPeerAsync()
    {
        var a = AddPeerAddress.Trim();
        if (a.Length == 0) return;
        await SafeCall(async x =>
        {
            await x.AddPeerAsync(a);
            _ui.Post(() => AddPeerAddress = "");
        }, $"已添加 {a}，正在连接…");
    }

    [RelayCommand]
    private Task TrustSelectedAsync() => SetSelectedTrust(Trust.Trusted);
    [RelayCommand]
    private Task UntrustSelectedAsync() => SetSelectedTrust(Trust.Unknown);
    [RelayCommand]
    private Task BlockSelectedAsync() => SetSelectedTrust(Trust.Blocked);

    private async Task SetSelectedTrust(Trust t)
    {
        var fp = SelectedConversation?.PeerFingerprint;
        if (string.IsNullOrEmpty(fp)) return;
        await SafeCall(x => x.SetTrustAsync(fp, t));
    }

    public Task SetTrustAsync(string fp, Trust t) => SafeCall(x => x.SetTrustAsync(fp, t));
    public Task ForgetPeerAsync(string fp) => SafeCall(x => x.ForgetPeerAsync(fp));

    [RelayCommand]
    private async Task ToggleMuteAsync()
    {
        var c = SelectedConversation;
        if (c == null) return;
        await SafeCall(x => x.SetMutedAsync(c.Id, !c.Muted), c.Muted ? "已取消免打扰" : "已设为免打扰");
    }

    [RelayCommand]
    private async Task SearchAsync()
    {
        var q = SearchText.Trim();
        SearchResults.Clear();
        if (q.Length == 0) return;
        await SafeCall(async x =>
        {
            var r = await x.SearchAsync(q, 200);
            _ui.Post(() =>
            {
                SearchResults.Clear();
                foreach (var m in r) SearchResults.Add(new MessageItem(m));
                ShowStatus($"找到 {r.Count} 条消息");
            });
        });
    }

    public async Task<string?> PinOfferAsync()
    {
        string? pin = null;
        await SafeCall(async x => { pin = await x.PinOfferAsync(); });
        if (pin != null) PinText = pin;
        return pin;
    }

    public async Task<bool> PinPairAsync(string fp, string pin)
    {
        var ok = false;
        await SafeCall(async x => { ok = await x.PinPairAsync(fp, pin.Trim()); });
        ShowStatus(ok ? "PIN 配对成功，已互相信任" : "PIN 不正确或已过期", !ok);
        return ok;
    }

    // ------------------------------------------------------------ transfers
    [RelayCommand]
    private Task CancelTransferAsync(TransferItem? t) => t == null ? Task.CompletedTask : SafeCall(x => x.CancelTransferAsync(t.Id));
    [RelayCommand]
    private Task RetryTransferAsync(TransferItem? t) => t == null ? Task.CompletedTask : SafeCall(x => x.RetryTransferAsync(t.Id));
    [RelayCommand]
    private void OpenTransfer(TransferItem? t)
    {
        if (t != null && t.CanOpen) _platform.OpenPath(t.Path);
    }
    [RelayCommand]
    private void RevealTransfer(TransferItem? t)
    {
        if (t != null && !string.IsNullOrEmpty(t.Path)) _platform.RevealInFolder(t.Path);
    }
    [RelayCommand]
    private void ClearFinishedTransfers()
    {
        for (int i = Transfers.Count - 1; i >= 0; i--)
            if (!Transfers[i].IsActive) Transfers.RemoveAt(i);
    }

    // ------------------------------------------------------------ groups
    public List<PickPeer> TrustedPeersForPicking(IEnumerable<string>? exclude = null)
    {
        var ex = exclude?.ToHashSet() ?? new HashSet<string>();
        return Peers.Where(p => p.IsTrusted && !ex.Contains(p.Fingerprint))
            .Select(p => new PickPeer { Fingerprint = p.Fingerprint, Name = $"{p.Name}  ({p.Fingerprint})" }).ToList();
    }

    public async Task<bool> CreateGroupAsync(string name, IEnumerable<string> members)
    {
        if (string.IsNullOrWhiteSpace(name)) { ShowStatus("请输入群名称", true); return false; }
        var ok = false;
        await SafeCall(async x =>
        {
            var g = await x.CreateGroupAsync(name.Trim(), members);
            ok = true;
            _ui.Post(() => { ShowStatus($"群组「{g.Name}」已创建"); SelectConversation("group:" + g.Id); });
        });
        return ok;
    }

    public Task InviteAsync(string groupId, IEnumerable<string> members) => SafeCall(x => x.InviteToGroupAsync(groupId, members), "已发送邀请");
    public Task RemoveMemberAsync(string groupId, string fp) => SafeCall(x => x.RemoveGroupMemberAsync(groupId, fp), "已移除成员并更新群密钥");
    public Task LeaveGroupAsync(string groupId) => SafeCall(x => x.LeaveGroupAsync(groupId), "已退出群组");

    // ------------------------------------------------------------ settings
    private static List<string> SplitList(string s) =>
        s.Split(new[] { ',', ';', '\n', '\r', '，' }, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries).ToList();

    [RelayCommand]
    private async Task SaveSettingsAsync()
    {
        var patch = new Dictionary<string, object?>
        {
            ["display_name"] = EditDisplayName.Trim(),
            ["download_dir"] = string.IsNullOrWhiteSpace(EditDownloadDir) ? null : EditDownloadDir.Trim(),
            ["auto_accept_files"] = EditAutoAccept,
            ["history_enabled"] = EditHistory,
            ["discovery_enabled"] = EditDiscovery,
            ["multicast_enabled"] = EditMulticast,
            ["announce_targets"] = SplitList(EditTargets),
            ["include_interfaces"] = SplitList(EditInclude),
            ["exclude_interfaces"] = SplitList(EditExclude),
            ["max_file_bytes"] = (ulong)Math.Max(1, EditMaxFileMb) * 1024UL * 1024UL,
            ["check_updates"] = EditCheckUpdates,
        };
        await SafeCall(async x =>
        {
            var restart = await x.UpdateSettingsAsync(patch);
            _ui.Post(() => ShowStatus(restart ? "设置已保存；网络相关设置将在重启后台服务后生效" : "设置已保存"));
        });
    }

    [RelayCommand]
    private async Task PickDownloadDirAsync()
    {
        var d = await _platform.PickFolderAsync();
        if (d != null) EditDownloadDir = d;
    }

    public async Task CompleteFirstRunAsync(string name)
    {
        await SafeCall(async x =>
        {
            await x.CompleteFirstRunAsync(string.IsNullOrWhiteSpace(name) ? null : name.Trim());
            _ui.Post(() => FirstRunNeeded = false);
        });
    }

    public async Task<bool> BackupAsync(string path, string password)
    {
        var ok = false;
        await SafeCall(async x => { await x.BackupAsync(path, password); ok = true; }, "备份已保存：" + path);
        return ok;
    }

    public async Task<bool> RestoreAsync(string path, string password)
    {
        var ok = false;
        await SafeCall(async x => { await x.RestoreAsync(path, password); ok = true; });
        if (ok) await RestartDaemonAsync();
        return ok;
    }

    [RelayCommand]
    public async Task RestartDaemonAsync()
    {
        var c = _client;
        if (c == null) return;
        try { await c.ShutdownAsync(); } catch { }
        ShowStatus("正在重启后台服务…");
        // Disconnected handler reconnects and starts the daemon again.
    }

    [RelayCommand]
    private void OpenLogs()
    {
        if (!string.IsNullOrEmpty(LogDir)) _platform.OpenPath(LogDir);
    }

    [RelayCommand]
    private void OpenDownloads()
    {
        if (!string.IsNullOrEmpty(DownloadDir))
        {
            Directory.CreateDirectory(DownloadDir);
            _platform.OpenPath(DownloadDir);
        }
    }

    [RelayCommand]
    private void CopyFingerprint() { _platform.CopyToClipboard(MyFingerprint); ShowStatus("指纹已复制"); }

    [RelayCommand]
    private async Task CheckUpdateAsync()
    {
        UpdateText = "正在检查…";
        try
        {
            using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(10) };
            http.DefaultRequestHeaders.UserAgent.ParseAdd("LanMessenger/" + Version);
            var json = await http.GetStringAsync("https://api.github.com/repos/FREC86/lan-secure-messenger/releases?per_page=10");
            using var doc = JsonDocument.Parse(json);
            string? best = null; string? url = null;
            foreach (var r in doc.RootElement.EnumerateArray())
            {
                var tag = r.GetProperty("tag_name").GetString() ?? "";
                if (!tag.StartsWith("v0.5") && !tag.StartsWith("v1")) continue;
                if (best == null || CompareVersions(tag, best) > 0) { best = tag; url = r.GetProperty("html_url").GetString(); }
            }
            UpdateText = best == null ? "未找到新版本" :
                CompareVersions(best, "v" + Version) > 0 ? $"发现新版本 {best}：{url}" : $"已是最新版本 ({Version})";
        }
        catch (Exception e)
        {
            UpdateText = "检查更新失败：" + e.Message;
        }
    }

    public static int CompareVersions(string a, string b)
    {
        static (int[] nums, string pre) Parse(string v)
        {
            v = v.TrimStart('v', 'V');
            var dash = v.IndexOf('-');
            var core = dash >= 0 ? v[..dash] : v;
            var pre = dash >= 0 ? v[(dash + 1)..] : "";
            var nums = core.Split('.').Select(x => int.TryParse(x, out var n) ? n : 0).Concat(new[] { 0, 0, 0 }).Take(3).ToArray();
            return (nums, pre);
        }
        var (an, ap) = Parse(a);
        var (bn, bp) = Parse(b);
        for (int i = 0; i < 3; i++) if (an[i] != bn[i]) return an[i].CompareTo(bn[i]);
        if (ap == bp) return 0;
        if (ap == "") return 1;
        if (bp == "") return -1;
        return string.CompareOrdinal(ap, bp);
    }

    // ------------------------------------------------------------ diagnostics
    [RelayCommand]
    private Task RefreshDiagnostics() => RefreshDiagnosticsAsync(false);

    public async Task RefreshDiagnosticsAsync(bool skipFirewall)
    {
        var c = _client;
        if (c == null) return;
        _ui.Post(() => DiagBusy = true);
        try
        {
            var d = await c.GetDiagnosticsAsync(skipFirewall);
            _ui.Post(() => ApplyDiagnostics(d, skipFirewall));
        }
        catch (Exception e)
        {
            _ui.Post(() => ShowStatus(e.Message, true));
        }
        finally
        {
            _ui.Post(() => DiagBusy = false);
        }
    }

    public void ApplyDiagnostics(Diagnostics d, bool skipFirewall = false)
    {
        Interfaces.Clear();
        foreach (var i in d.Discovery.Interfaces)
        {
            var title = string.IsNullOrEmpty(i.FriendlyName) ? i.Name : i.FriendlyName;
            Interfaces.Add(new InterfaceItem
            {
                Title = $"{title}  {i.Ip}/{i.Netmask}",
                Detail = (i.Used ? $"广播 {i.Broadcast}" : $"跳过：{i.Reason}") + (string.IsNullOrEmpty(i.Description) ? "" : $" · {i.Description}"),
                Used = i.Used,
            });
        }
        PeerDiags.Clear();
        foreach (var p in d.Peers)
        {
            var parts = new List<string>();
            if (p.Addr != null) parts.Add(p.Addr);
            if (p.Direction != null) parts.Add(p.Direction == "outbound" ? "我方发起" : "对方发起");
            if (p.RttMs is uint r) parts.Add($"{r} ms");
            if (p.ConnectedForMs is long cf) parts.Add($"已连接 {TimeSpan.FromMilliseconds(cf):hh\\:mm\\:ss}");
            if (p.DialFailures > 0) parts.Add($"连接失败 {p.DialFailures} 次");
            if (!string.IsNullOrEmpty(p.LastError)) parts.Add($"最近错误：{p.LastError}");
            if (p.NextRetryInMs is long nr) parts.Add($"{nr / 1000.0:0.#}s 后重试");
            if (p.Candidates.Count > 0) parts.Add("候选地址 " + string.Join(", ", p.Candidates));
            if (p.Sources.Count > 0) parts.Add("来源 " + string.Join("/", p.Sources));
            PeerDiags.Add(new PeerDiagItem
            {
                Title = $"{p.Name}  ({p.Fingerprint})",
                Detail = string.Join(" · ", parts),
                Connected = p.State == ConnState.Connected,
                StateText = p.State switch { ConnState.Connected => "已连接", ConnState.Connecting => "连接中", _ => "离线" },
            });
        }
        ManualPeers.Clear();
        foreach (var m in d.ManualPeers) ManualPeers.Add(m);
        var ds = d.Discovery;
        DiscoveryText = !ds.Enabled ? "自动发现已关闭（仅手动添加 / 记住的地址）" :
            $"UDP {ds.Port} {(ds.Listening ? "监听中" : "未监听")}{(string.IsNullOrEmpty(ds.BindError) ? "" : " · " + ds.BindError)} · 已发送 {ds.AnnouncesSent} · 已收到 {ds.AnnouncesReceived}" +
            (ds.SendErrors > 0 ? $" · 发送错误 {ds.SendErrors}（{ds.LastSendError}）" : "");
        PortsText = $"{d.TcpListen}{(string.IsNullOrEmpty(d.TcpError) ? "" : " · " + d.TcpError)} · IPC {d.IpcEndpoint}";
        if (!skipFirewall)
        {
            var f = d.Firewall;
            FirewallOk = !f.Supported || (f.TcpRule && f.UdpRule && !f.RulePathMismatch);
            PublicNetwork = f.PublicNetwork;
            FirewallSummary = !f.Supported ? "非 Windows：请自行放行端口" :
                f.TcpRule && f.UdpRule ? (f.RulePathMismatch ? "规则存在但路径不匹配" : $"已放行 (配置文件: {f.RuleProfiles})") : "未配置放行规则";
            FirewallAdvice = f.Advice;
            ProfilesText = f.Profiles.Count == 0 ? "" : string.Join("；", f.Profiles.Select(p => $"{p.Name} ({p.Interface}): {CategoryText(p.Category)}"));
        }
        DiagnosticsJson = JsonSerializer.Serialize(d, new JsonSerializerOptions(Json.Options) { WriteIndented = true, Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping });
    }

    private static string CategoryText(string c) => c switch
    {
        "Public" => "公用网络 ⚠",
        "Private" => "专用网络",
        "DomainAuthenticated" => "域网络",
        _ => c,
    };

    [RelayCommand]
    private async Task FirewallSetupAsync()
    {
        ShowStatus("请在弹出的管理员确认 (UAC) 窗口中点击“是”…");
        await SafeCall(x => x.FirewallSetupAsync(IncludePublic), "防火墙规则已添加");
        await RefreshDiagnosticsAsync(false);
    }

    [RelayCommand]
    private void CopyDiagnostics()
    {
        _platform.CopyToClipboard(DiagnosticsJson);
        ShowStatus("诊断信息已复制到剪贴板");
    }

    public Task RemoveManualPeerAsync(string addr) => SafeCall(async x => { await x.RemoveManualPeerAsync(addr); await RefreshDiagnosticsAsync(true); });
}
