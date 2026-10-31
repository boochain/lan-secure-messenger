using CommunityToolkit.Mvvm.ComponentModel;
using LanMessenger.Core.Models;

namespace LanMessenger.Core.ViewModels;

public partial class PeerItem : ObservableObject
{
    public string Fingerprint { get; }
    public PeerItem(Peer p) { Fingerprint = p.Fingerprint; Update(p); }

    [ObservableProperty] private string _name = "";
    [ObservableProperty] private bool _online;
    [ObservableProperty] private bool _connecting;
    [ObservableProperty] private string _stateText = "";
    [ObservableProperty] private Trust _trust;
    [ObservableProperty] private string _trustText = "";
    [ObservableProperty] private bool _isTrusted;
    [ObservableProperty] private bool _isBlocked;
    [ObservableProperty] private string _addr = "";
    [ObservableProperty] private string _detail = "";
    [ObservableProperty] private string _initial = "?";
    [ObservableProperty] private bool _isSelected;
    [ObservableProperty] private bool _viaRelay;        // online through the relay server
    public bool Offline => !Online;

    public void Update(Peer p)
    {
        Name = p.Name;
        Online = p.Online;
        ViaRelay = p.Online && p.ViaRelay;
        Connecting = p.State == ConnState.Connecting;
        StateText = p.State switch
        {
            ConnState.Connected => p.RttMs is uint r ? $"在线 · {r} ms" : "在线",
            ConnState.Connecting => "正在连接…",
            _ => p.LastSeenMs > 0 ? $"离线 · 最后在线 {Fmt.Time(p.LastSeenMs)}" : "离线",
        };
        Trust = p.Trust;
        IsTrusted = p.Trust == Trust.Trusted;
        IsBlocked = p.Trust == Trust.Blocked;
        TrustText = p.Trust switch { Trust.Trusted => "已信任", Trust.Blocked => "已屏蔽", _ => "未验证" };
        Addr = p.Addr ?? "";
        Detail = $"{p.Fingerprint}  {Addr}".Trim();
        Initial = Fmt.Initial(p.Name);
        OnPropertyChanged(nameof(Offline));
    }
}

public partial class ConversationItem : ObservableObject
{
    public string Id { get; }
    public bool IsGroup { get; }
    public string PeerFingerprint => IsGroup ? "" : Id.StartsWith("dm:") ? Id[3..] : "";
    public string GroupId => IsGroup ? Id[6..] : "";

    public ConversationItem(Conversation c)
    {
        Id = c.Id;
        IsGroup = c.Kind == "group";
        Update(c);
    }

    [ObservableProperty] private string _title = "";
    [ObservableProperty] private string _lastBody = "";
    [ObservableProperty] private string _timeText = "";
    [ObservableProperty] private long _lastTsMs;
    [ObservableProperty] private uint _unread;
    [ObservableProperty] private bool _hasUnread;
    [ObservableProperty] private string _unreadText = "";
    [ObservableProperty] private bool _online;
    [ObservableProperty] private bool _muted;
    [ObservableProperty] private string _initial = "?";
    [ObservableProperty] private string _lastFrom = "";
    [ObservableProperty] private string _preview = "";
    [ObservableProperty] private bool _previewIsFile;
    [ObservableProperty] private bool _previewIsImage;
    [ObservableProperty] private int _lastTick;          // 0 none, 1 sent/queued, 2 delivered, 3 read, 4 failed
    [ObservableProperty] private bool _isTrusted;        // set by the view model from the peer list
    [ObservableProperty] private bool _viaServer;        // online and the live link goes through the relay server (Peer.via_relay)
    public bool IsDm => !IsGroup;
    public bool ShowOfflineDot => false;                 // WhatsApp/Telegram: only online is marked
    public bool ShowOnlineDot => IsDm && Online;
    public string AvatarKey => IsGroup ? GroupId : PeerFingerprint;
    public int AvatarIndex => Fmt.AvatarIndex(AvatarKey);

    public void Update(Conversation c)
    {
        Title = c.Title;
        LastBody = c.LastBody;
        LastTsMs = c.LastTsMs;
        TimeText = Fmt.ListTime(c.LastTsMs);
        LastFrom = IsGroup && c.LastTsMs > 0 && c.LastKind != MsgKind.System && !string.IsNullOrEmpty(c.LastSender) ? (c.LastOut ? "我" : c.LastSender) + ": " : "";
        PreviewIsFile = c.LastKind == MsgKind.File;
        PreviewIsImage = PreviewIsFile && IsImageName(c.LastFile);
        Preview = PreviewIsImage ? "照片" : PreviewIsFile && c.LastFile.Length > 0 ? c.LastFile : c.LastBody.Replace('\n', ' ');
        LastTick = !c.LastOut || c.LastStatus == null ? 0 : c.LastStatus switch
        {
            MsgStatus.Read => 3, MsgStatus.Delivered => 2, MsgStatus.Failed => 4, _ => 1,
        };
        Unread = c.Unread;
        HasUnread = c.Unread > 0;
        UnreadText = c.Unread > 99 ? "99+" : c.Unread.ToString();
        Online = c.Online;
        Muted = c.Muted;
        Initial = Fmt.Initials(c.Title);
        OnPropertyChanged(nameof(ShowOfflineDot));
        OnPropertyChanged(nameof(ShowOnlineDot));
    }
}

public partial class ConversationItem
{
    public static bool IsImageName(string n) => Path.GetExtension(n ?? "").ToLowerInvariant() is ".png" or ".jpg" or ".jpeg" or ".gif" or ".bmp" or ".webp";
}

public partial class MessageItem : ObservableObject
{
    public string Id { get; }
    public bool IsOutgoing { get; }
    public bool IsIncoming => !IsOutgoing && !IsSystem;
    public bool IsSystem { get; }
    public bool IsGroupConversation { get; }
    public string Conversation { get; }

    public MessageItem(Message m)
    {
        Id = m.Id;
        Conversation = m.Conversation;
        IsOutgoing = m.Direction == Direction.Out;
        IsSystem = m.Kind == MsgKind.System;
        IsGroupConversation = m.Conversation.StartsWith("group:");
        Update(m);
    }

    [ObservableProperty] private string _body = "";
    [ObservableProperty] private string _senderName = "";
    [ObservableProperty] private bool _showSender;
    [ObservableProperty] private string _timeText = "";
    [ObservableProperty] private string _statusText = "";
    [ObservableProperty] private bool _isFailed;
    [ObservableProperty] private bool _isFile;
    [ObservableProperty] private bool _isText;
    [ObservableProperty] private bool _isImage;
    [ObservableProperty] private string _fileName = "";
    [ObservableProperty] private string _fileSizeText = "";
    [ObservableProperty] private string _filePath = "";
    [ObservableProperty] private bool _hasFilePath;
    [ObservableProperty] private string _transferId = "";
    // redesign: grouping, date separators, receipts (Grouping.Apply keeps these current)
    [ObservableProperty] private bool _isFirstInGroup = true;
    [ObservableProperty] private bool _isLastInGroup = true;
    [ObservableProperty] private string _dateLabel = "";
    [ObservableProperty] private bool _showE2E;
    [ObservableProperty] private string _clockText = "";
    [ObservableProperty] private int _tick;              // 0 none, 1 sent/queued, 2 delivered, 3 read, 4 failed
    [ObservableProperty] private bool _isFileCard;
    [ObservableProperty] private string _fileExt = "";
    [ObservableProperty] private bool _isPdf;
    public bool HasDate => DateLabel.Length > 0;
    partial void OnDateLabelChanged(string value) => OnPropertyChanged(nameof(HasDate));
    public long TsMs { get; private set; }

    public void Update(Message m)
    {
        TsMs = m.TsMs;
        Body = m.Body;
        SenderName = m.SenderName;
        ShowSender = IsGroupConversation && m.Direction == Direction.In && m.Kind != MsgKind.System && IsFirstInGroup;
        TimeText = Fmt.Time(m.TsMs);
        ClockText = Fmt.Clock(m.TsMs);
        Tick = !IsOutgoing ? 0 : m.Status switch { MsgStatus.Read => 3, MsgStatus.Delivered => 2, MsgStatus.Failed or MsgStatus.Rejected => 4, _ => 1 };
        IsFailed = m.Status is MsgStatus.Failed or MsgStatus.Rejected;
        StatusText = !IsOutgoing ? "" : m.Status switch
        {
            MsgStatus.Queued => "等待对方上线",
            MsgStatus.Sent => "已发送",
            MsgStatus.Delivered => "已送达",
            MsgStatus.Read => "已读",
            MsgStatus.Failed => "发送失败",
            MsgStatus.Rejected => "已拒收",
            _ => "",
        };
        IsFile = m.Kind == MsgKind.File && m.File != null;
        IsText = m.Kind == MsgKind.Text;
        if (m.File != null)
        {
            FileName = m.File.Name;
            FileSizeText = Fmt.Bytes(m.File.Size);
            FilePath = m.File.Path ?? "";
            HasFilePath = !string.IsNullOrEmpty(m.File.Path) && File.Exists(m.File.Path);
            IsImage = m.File.IsImage && HasFilePath;
            TransferId = m.File.TransferId;
            FileExt = Path.GetExtension(m.File.Name).TrimStart('.').ToUpperInvariant();
            IsPdf = FileExt == "PDF";
        }
        IsFileCard = IsFile && !IsImage;
    }
}

public partial class TransferItem : ObservableObject
{
    public string Id { get; }
    public TransferItem(Transfer t) { Id = t.Id; Update(t); }

    [ObservableProperty] private string _name = "";
    [ObservableProperty] private string _peerName = "";
    [ObservableProperty] private string _directionText = "";
    [ObservableProperty] private bool _isOutgoing;
    [ObservableProperty] private double _progress;
    [ObservableProperty] private string _progressText = "";
    [ObservableProperty] private string _stateText = "";
    [ObservableProperty] private bool _canCancel;
    [ObservableProperty] private bool _canRetry;
    [ObservableProperty] private bool _canOpen;
    [ObservableProperty] private bool _isActive;
    [ObservableProperty] private bool _isError;
    [ObservableProperty] private string _path = "";
    [ObservableProperty] private string _error = "";
    [ObservableProperty] private string _conversation = "";
    [ObservableProperty] private bool _isPaused;          // 连接中断，重连后自动续传
    [ObservableProperty] private string _resumedText = ""; // 从 40%（48 MB）处续传
    public long StartedMs { get; private set; }

    public void Update(Transfer t)
    {
        StartedMs = t.StartedMs;
        Name = t.Name;
        PeerName = t.PeerName;
        Conversation = t.Conversation;
        IsOutgoing = t.Direction == Direction.Out;
        DirectionText = IsOutgoing ? $"发送给 {t.PeerName}" : $"来自 {t.PeerName}";
        Progress = t.Size == 0 ? (t.State == TransferState.Done ? 100 : 0) : Math.Min(100.0, t.Transferred * 100.0 / t.Size);
        var speed = t.State == TransferState.Active && t.BytesPerSec > 0 ? $" · {Fmt.Bytes(t.BytesPerSec)}/s" : "";
        ProgressText = $"{Fmt.Bytes(t.Transferred)} / {Fmt.Bytes(t.Size)}{speed}";
        // link lost mid-transfer: the daemon keeps it "offered" with this reason and resumes by itself
        IsPaused = t.State == TransferState.Offered && t.Error.Length > 0;
        var pct = t.Size == 0 ? 0 : (int)(t.ResumedFrom * 100 / t.Size);
        ResumedText = t.ResumedFrom > 0 ? $"从 {pct}%（{Fmt.Bytes(t.ResumedFrom)}）处续传" : "";
        StateText = t.State switch
        {
            TransferState.Offered when IsPaused => t.Error,
            TransferState.Offered => IsOutgoing ? "等待对方接收" : "等待接收",
            TransferState.Active => "传输中",
            TransferState.Verifying => "校验中",
            TransferState.Done => "已完成",
            TransferState.Failed => "失败",
            TransferState.Cancelled => "已取消",
            TransferState.Rejected => "已拒收",
            _ => "",
        };
        if (ResumedText.Length > 0 && t.State is TransferState.Active or TransferState.Verifying or TransferState.Done) StateText += " · " + ResumedText;
        IsActive = t.State is TransferState.Active or TransferState.Offered or TransferState.Verifying;
        CanCancel = IsActive;
        CanRetry = t.Retryable;
        Path = t.Path ?? "";
        CanOpen = t.State == TransferState.Done && !string.IsNullOrEmpty(t.Path);
        Error = t.Error;
        IsError = !string.IsNullOrEmpty(t.Error) && t.State is TransferState.Failed or TransferState.Rejected or TransferState.Cancelled;
    }
}

public partial class GroupMemberItem : ObservableObject
{
    public string Fingerprint { get; init; } = "";
    [ObservableProperty] private string _name = "";
    [ObservableProperty] private bool _online;
    [ObservableProperty] private bool _canRemove;
}

public partial class GroupItem : ObservableObject
{
    public string Id { get; }
    public GroupItem(Group g) { Id = g.Id; Update(g); }

    [ObservableProperty] private string _name = "";
    [ObservableProperty] private string _membersText = "";
    [ObservableProperty] private bool _isCreator;
    [ObservableProperty] private string _epochText = "";
    [ObservableProperty] private bool _muted;
    public System.Collections.ObjectModel.ObservableCollection<GroupMemberItem> Members { get; } = new();

    public void Update(Group g)
    {
        Name = g.Name;
        IsCreator = g.IsCreator;
        var online = g.Members.Count(m => m.Online);
        MembersText = $"{g.Members.Count} 位成员 · {online} 在线";
        EpochText = $"密钥版本 {g.KeyEpoch}";
        Muted = g.Muted;
        Members.Clear();
        foreach (var m in g.Members)
            Members.Add(new GroupMemberItem { Fingerprint = m.Fingerprint, Name = m.Name, Online = m.Online, CanRemove = g.IsCreator && m.Fingerprint != g.CreatorFp });
    }
}

public sealed class InterfaceItem
{
    public string Title { get; init; } = "";
    public string Detail { get; init; } = "";
    public bool Used { get; init; }
    public string UsedText => Used ? "用于发现" : "已跳过";
}

public sealed class PeerDiagItem
{
    public string Title { get; init; } = "";
    public string Detail { get; init; } = "";
    public bool Connected { get; init; }
    public string StateText { get; init; } = "";
}

public sealed class PickPeer : ObservableObject
{
    public string Fingerprint { get; init; } = "";
    public string Name { get; init; } = "";
    private bool _selected;
    public bool Selected { get => _selected; set => SetProperty(ref _selected, value); }
}
