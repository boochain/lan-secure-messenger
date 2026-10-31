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
    [ObservableProperty] private bool _pinned;           // v0.6.1 P2: pinned chat (sorted first)
    [ObservableProperty] private string _draft = "";
    [ObservableProperty] private bool _hasDraft;
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
        Pinned = c.Pinned; Draft = c.Draft.Replace('\n', ' '); HasDraft = c.Draft.Trim().Length > 0;
        Archived = c.Archived; Folders = c.Folders ?? new(); TtlSecs = c.TtlSecs; HasTtl = c.TtlSecs > 0;
        OnPropertyChanged(nameof(ShowOfflineDot));
        OnPropertyChanged(nameof(ShowOnlineDot));
    }
}

public partial class ConversationItem
{
    public static bool IsImageName(string n) => Path.GetExtension(n ?? "").ToLowerInvariant() is ".png" or ".jpg" or ".jpeg" or ".gif" or ".bmp" or ".webp";
    partial void OnOnlineChanged(bool value)
    {
        OnPropertyChanged(nameof(ShowOnlineDot));
        OnPropertyChanged(nameof(ShowOfflineDot));
    }
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
    [ObservableProperty] private bool _isRead;
    [ObservableProperty] private int _tick;              // 0 none, 1 sent/queued, 2 delivered, 3 read, 4 failed
    [ObservableProperty] private bool _isFileCard;
    [ObservableProperty] private string _fileExt = "";
    [ObservableProperty] private bool _isPdf;
    // v0.6.1 P1
    public static string MyFp = "";
    [ObservableProperty] private string _rawBody = "";
    [ObservableProperty] private bool _isRecalled;
    [ObservableProperty] private bool _isEdited;
    [ObservableProperty] private bool _hasQuote;
    [ObservableProperty] private string _quoteText = "";
    [ObservableProperty] private bool _hasReactions;
    [ObservableProperty] private string _reactionsText = "";
    [ObservableProperty] private string _myReaction = "";
    [ObservableProperty] private bool _isVoice;
    [ObservableProperty] private bool _isVoiceCard;
    [ObservableProperty] private string _voiceText = "";
    public bool CanModify => IsOutgoing && IsText && !IsRecalled && DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() - TsMs < 24L * 3600 * 1000;
    /// <summary>-1 unless the file is named lsm-voice-&lt;N&gt;s-….</summary>
    public static int VoiceSecs(string name)
    {
        var b = Path.GetFileName(name ?? "");
        if (!b.StartsWith("lsm-voice-")) return -1;
        int i = 10, v = 0; while (i < b.Length && char.IsDigit(b[i])) { v = v * 10 + (b[i] - '0'); i++; }
        return v;
    }
    public bool HasDate => DateLabel.Length > 0;
    partial void OnDateLabelChanged(string value) => OnPropertyChanged(nameof(HasDate));
    public long TsMs { get; private set; }

    public void Update(Message m)
    {
        TsMs = m.TsMs;
        RawBody = m.Body;
        IsRecalled = m.Recalled; IsEdited = m.Edited && !m.Recalled;
        IsPinned = m.Pinned; IsStarred = m.Starred; MentionsMe = m.MentionsMe;
        UpdatePoll(m);
        TagsText = m.Recalled ? "" : string.Concat(m.Forwarded ? "↪ 转发  " : "", m.Pinned ? "📌  " : "", m.Starred ? "⭐  " : "", m.MentionsMe ? "@我" : "").Trim(); HasTags = TagsText.Length > 0;
        Body = m.Recalled ? (m.Direction == Direction.Out ? "你撤回了一条消息" : "对方撤回了一条消息") : m.Edited ? m.Body + "（已编辑）" : m.Body;
        HasQuote = !string.IsNullOrEmpty(m.ReplyTo) && !m.Recalled; QuoteText = m.Quote.Length > 0 ? m.Quote : "原消息";
        var groups = m.Recalled ? new List<string>() : m.Reactions.Where(r => r.Emoji.Length > 0).GroupBy(r => r.Emoji).Select(g => $"{g.Key} {g.Count()}").ToList();
        ReactionsText = string.Join("  ", groups); HasReactions = groups.Count > 0;
        MyReaction = m.Reactions.FirstOrDefault(r => r.Fp == MyFp)?.Emoji ?? "";
        SenderName = m.SenderName;
        ShowSender = IsGroupConversation && m.Direction == Direction.In && m.Kind != MsgKind.System && IsFirstInGroup;
        TimeText = Fmt.Time(m.TsMs);
        ClockText = Fmt.Clock(m.TsMs);
        Tick = !IsOutgoing ? 0 : m.Status switch { MsgStatus.Read => 3, MsgStatus.Delivered => 2, MsgStatus.Failed or MsgStatus.Rejected => 4, _ => 1 };
        IsRead = Tick == 3;
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
        var vs = m.File != null ? VoiceSecs(m.File.Name) : -1;
        IsVoice = vs >= 0; VoiceText = IsVoice ? $"语音 {vs}″" + (HasFilePath ? "" : " · 接收中…") : "";
        IsVoiceCard = IsFile && IsVoice;
        IsFileCard = IsFile && !IsImage && !IsVoice;
        OnPropertyChanged(nameof(CanModify));
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
        // Sender must not show own/peer receive speeds — only meaningful progress (#2).
        // Receiver may show local write throughput.
        var speed = "";
        var eta = "";
        if (!IsOutgoing && t.State == TransferState.Active && t.BytesPerSec > 0)
        {
            speed = $" · {Fmt.Bytes(t.BytesPerSec)}/s";
            if (t.Size > t.Transferred)
            {
                var sec = (t.Size - t.Transferred) / t.BytesPerSec;
                eta = sec >= 3600 ? $" · 剩余 {sec / 3600}h{sec % 3600 / 60:00}m" : sec >= 60 ? $" · 剩余 {sec / 60}m{sec % 60:00}s" : $" · 剩余 {sec}s";
            }
        }
        var pctNow = t.Size == 0 ? 0 : (int)(t.Transferred * 100 / t.Size);
        ProgressText = $"{pctNow}% · {Fmt.Bytes(t.Transferred)} / {Fmt.Bytes(t.Size)}{speed}{eta}";
        // link lost mid-transfer: the daemon keeps it "offered" with this reason and resumes by itself
        IsPaused = t.State == TransferState.Offered && t.Error.Length > 0;
        var pct = t.Size == 0 ? 0 : (int)(t.ResumedFrom * 100 / t.Size);
        ResumedText = t.ResumedFrom > 0 ? $"从 {pct}%（{Fmt.Bytes(t.ResumedFrom)}）处续传" : "";
        StateText = t.State switch
        {
            TransferState.Offered when IsPaused => t.Error,
            TransferState.Offered => IsOutgoing ? "等待对方接收" : "等待接收",
            TransferState.Active => t.ResumedFrom > 0 ? "续传中" : "传输中",
            TransferState.Verifying => "校验中",
            TransferState.Done => "已完成",
            TransferState.Failed => t.Error.Length > 0 ? "失败：" + t.Error : "失败",
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

public partial class MessageItem
{
    // v0.6.1 P2
    [ObservableProperty] private bool _isPinned;
    [ObservableProperty] private bool _isStarred;
    [ObservableProperty] private bool _mentionsMe;
    [ObservableProperty] private string _tagsText = "";
    [ObservableProperty] private bool _hasTags;
    [ObservableProperty] private bool _isPicked;   // multi-select
}

// v0.6.2
public partial class ConversationItem
{
    [ObservableProperty] private bool _archived;
    [ObservableProperty] private uint _ttlSecs;
    [ObservableProperty] private bool _hasTtl;
    public List<string> Folders { get; set; } = new();
}

public partial class PollOptionItem : ObservableObject
{
    public int Index { get; init; }
    public string Text { get; init; } = "";
    [ObservableProperty] private int _count;
    [ObservableProperty] private bool _mine;
    [ObservableProperty] private double _percent;
    [ObservableProperty] private string _label = "";
    public MessageItem? Owner { get; init; }
}

public partial class MessageItem
{
    [ObservableProperty] private bool _isPoll;
    [ObservableProperty] private bool _pollClosed;
    [ObservableProperty] private bool _pollMulti;
    [ObservableProperty] private string _pollSummary = "";
    [ObservableProperty] private bool _canClosePoll;
    [ObservableProperty] private bool _isExpiring;
    [ObservableProperty] private string _expiresText = "";
    public long ExpiresMs { get; private set; }
    public System.Collections.ObjectModel.ObservableCollection<PollOptionItem> PollOptions { get; } = new();
    public HashSet<int> MyChoices { get; } = new();

    private void UpdatePoll(Message m)
    {
        ExpiresMs = m.ExpiresMs; IsExpiring = m.ExpiresMs > 0;
        ExpiresText = IsExpiring ? "⏱ " + DateTimeOffset.FromUnixTimeMilliseconds(m.ExpiresMs).ToLocalTime().ToString("MM-dd HH:mm:ss") + " 删除" : "";
        var p = m.Poll; IsPoll = p != null && !m.Recalled;
        if (p == null) { PollOptions.Clear(); return; }
        PollClosed = p.Closed; PollMulti = p.Multi; CanClosePoll = IsOutgoing && !p.Closed;
        var voters = p.Votes.Count(v => v.Choices.Count > 0);
        MyChoices.Clear(); foreach (var c in p.Votes.FirstOrDefault(v => v.Fp == MyFp)?.Choices ?? new()) MyChoices.Add(c);
        if (PollOptions.Count != p.Options.Count) { PollOptions.Clear(); for (int i = 0; i < p.Options.Count; i++) PollOptions.Add(new PollOptionItem { Index = i, Text = p.Options[i], Owner = this }); }
        for (int i = 0; i < p.Options.Count; i++)
        {
            var o = PollOptions[i]; var n = p.Votes.Count(v => v.Choices.Contains(i));
            o.Count = n; o.Mine = MyChoices.Contains(i); o.Percent = voters == 0 ? 0 : 100.0 * n / voters;
            o.Label = (o.Mine ? "✓ " : "") + o.Text + $"  ·  {n} 票";
        }
        PollSummary = (p.Multi ? "多选投票" : "单选投票") + $" · {voters} 人参与" + (p.Closed ? " · 已结束" : "");
    }
}
