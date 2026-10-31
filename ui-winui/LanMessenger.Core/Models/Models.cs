// DTOs mirroring lan-core/src/api.rs (see docs/ipc.md). JSON is snake_case.
using System.Text.Json;
using System.Text.Json.Serialization;

namespace LanMessenger.Core.Models;

public enum Trust { Unknown, Trusted, Blocked }
public enum ConnState { Connected, Connecting, Offline }
public enum Direction { In, Out }
public enum MsgStatus { Queued, Sent, Delivered, Read, Failed, Rejected, Received }
public enum MsgKind { Text, File, System }
public enum TransferState { Offered, Active, Verifying, Done, Failed, Cancelled, Rejected }

public static class Json
{
    public static readonly JsonSerializerOptions Options = Create();

    private static JsonSerializerOptions Create()
    {
        var o = new JsonSerializerOptions
        {
            PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
            DictionaryKeyPolicy = JsonNamingPolicy.SnakeCaseLower,
            DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
            PropertyNameCaseInsensitive = true,
            NumberHandling = JsonNumberHandling.AllowReadingFromString,
        };
        o.Converters.Add(new JsonStringEnumConverter(JsonNamingPolicy.SnakeCaseLower));
        return o;
    }

    public static T? To<T>(JsonElement e) => e.Deserialize<T>(Options);
    public static JsonElement ToElement(object? o) => JsonSerializer.SerializeToElement(o, Options);
}

public sealed class FileRef
{
    public string TransferId { get; set; } = "";
    public string Name { get; set; } = "";
    public ulong Size { get; set; }
    public string Mime { get; set; } = "";
    public string? Path { get; set; }
    public bool IsImage { get; set; }
}

public sealed class Message
{
    public string Id { get; set; } = "";
    public string Conversation { get; set; } = "";
    public Direction Direction { get; set; }
    public string SenderFp { get; set; } = "";
    public string SenderName { get; set; } = "";
    public MsgKind Kind { get; set; }
    public string Body { get; set; } = "";
    public long TsMs { get; set; }
    public MsgStatus Status { get; set; }
    public FileRef? File { get; set; }
}

public sealed class Peer
{
    public string Fingerprint { get; set; } = "";
    public string Name { get; set; } = "";
    public Trust Trust { get; set; }
    public bool Online { get; set; }
    public ConnState State { get; set; }
    public string? Addr { get; set; }
    public string? Direction { get; set; }
    public long LastSeenMs { get; set; }
    public bool Muted { get; set; }
    public uint Unread { get; set; }
    public uint? RttMs { get; set; }
    public string AppVersion { get; set; } = "";
    /// <summary>The live link goes through the relay server (false when offline or direct).</summary>
    public bool ViaRelay { get; set; }
}

public sealed class GroupMember
{
    public string Fingerprint { get; set; } = "";
    public string Name { get; set; } = "";
    public bool Online { get; set; }
}

public sealed class Group
{
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public string CreatorFp { get; set; } = "";
    public bool IsCreator { get; set; }
    public List<GroupMember> Members { get; set; } = new();
    public ulong KeyEpoch { get; set; }
    public bool Muted { get; set; }
    public uint Unread { get; set; }
}

public sealed class Transfer
{
    public string Id { get; set; } = "";
    public string MessageId { get; set; } = "";
    public string Conversation { get; set; } = "";
    public string PeerFp { get; set; } = "";
    public string PeerName { get; set; } = "";
    public Direction Direction { get; set; }
    public string Name { get; set; } = "";
    public ulong Size { get; set; }
    public ulong Transferred { get; set; }
    public TransferState State { get; set; }
    public string Mime { get; set; } = "";
    public string? Path { get; set; }
    public string Error { get; set; } = "";
    public ulong BytesPerSec { get; set; }
    public long StartedMs { get; set; }
    public bool Retryable { get; set; }
    /// <summary>Offset the last (re)start continued from; 0 = from the beginning.</summary>
    public ulong ResumedFrom { get; set; }
}

public sealed class Conversation
{
    public string Id { get; set; } = "";
    public string Kind { get; set; } = "dm";
    public string Title { get; set; } = "";
    public string LastBody { get; set; } = "";
    public long LastTsMs { get; set; }
    public uint Unread { get; set; }
    public bool Muted { get; set; }
    public bool Online { get; set; }
    // last-message details for the chat-list row (newer daemons; defaults when absent)
    public bool LastOut { get; set; }
    public MsgStatus? LastStatus { get; set; }
    public string LastSender { get; set; } = "";
    public MsgKind? LastKind { get; set; }
    public string LastFile { get; set; } = "";
}

public sealed class Me
{
    public string Fingerprint { get; set; } = "";
    public string Name { get; set; } = "";
    public string Version { get; set; } = "";
    public ushort ListenPort { get; set; }
    public ushort DiscoveryPort { get; set; }
    public string IpcEndpoint { get; set; } = "";
    public string DataDir { get; set; } = "";
    public string LogDir { get; set; } = "";
    public string DownloadDir { get; set; } = "";
    public bool FirstRunDone { get; set; }
}

public sealed class Settings
{
    public string DisplayName { get; set; } = "";
    public ushort DiscoveryPort { get; set; }
    public ushort ListenPort { get; set; }
    public bool DiscoveryEnabled { get; set; } = true;
    public bool DiscoveryListen { get; set; } = true;
    public bool MulticastEnabled { get; set; } = true;
    public List<string> AnnounceTargets { get; set; } = new();
    public List<string> IncludeInterfaces { get; set; } = new();
    public List<string> ExcludeInterfaces { get; set; } = new();
    public string BindLanIp { get; set; } = "";
    public string TransportMode { get; set; } = "lan";
    public string AccountUser { get; set; } = "";
    public string DefaultRetain { get; set; } = "ends_only";
    public bool AutoAcceptFiles { get; set; } = true;
    public ulong MaxFileBytes { get; set; }
    public string? DownloadDir { get; set; }
    public bool HistoryEnabled { get; set; } = true;
    public bool FirstRunDone { get; set; }
    public bool AutoTrust { get; set; }
    public bool CheckUpdates { get; set; } = true;
    public ulong AnnounceIntervalSecs { get; set; } = 3;
    // relay server fallback (optional, off by default); the passphrase is write-only over IPC
    public bool RelayEnabled { get; set; }
    public string RelayHost { get; set; } = "";
    public ushort RelayPort { get; set; } = 9443;
    public bool RelayHasPassphrase { get; set; }
}

public sealed class RelayStatus
{
    public bool Enabled { get; set; }
    public string State { get; set; } = "off";   // off | connecting | online | auth_failed | error
    public string Detail { get; set; } = "";
    public string Server { get; set; } = "";
    public uint Peers { get; set; }
    public uint RelayedLinks { get; set; }
    public bool HasPassphrase { get; set; }
}

public sealed class Snapshot
{
    public Me Me { get; set; } = new();
    public List<Peer> Peers { get; set; } = new();
    public List<Group> Groups { get; set; } = new();
    public List<Conversation> Conversations { get; set; } = new();
    public List<Transfer> Transfers { get; set; } = new();
    public Settings Settings { get; set; } = new();
    public RelayStatus Relay { get; set; } = new();
}

public sealed class NetIf
{
    public string Name { get; set; } = "";
    public string FriendlyName { get; set; } = "";
    public string Description { get; set; } = "";
    public string Ip { get; set; } = "";
    public string Netmask { get; set; } = "";
    public string Broadcast { get; set; } = "";
    public bool Up { get; set; }
    public bool Used { get; set; }
    public string Reason { get; set; } = "";
    public bool IsDefault { get; set; }
}

public sealed class DiscoStatus
{
    public bool Enabled { get; set; }
    public ushort Port { get; set; }
    public bool Listening { get; set; }
    public string BindError { get; set; } = "";
    public bool Multicast { get; set; }
    public ulong AnnouncesSent { get; set; }
    public ulong AnnouncesReceived { get; set; }
    public ulong AnnouncesRejected { get; set; }
    public ulong SendErrors { get; set; }
    public string LastSendError { get; set; } = "";
    public List<NetIf> Interfaces { get; set; } = new();
}

public sealed class NetProfile
{
    public string Name { get; set; } = "";
    public string Interface { get; set; } = "";
    public string Category { get; set; } = "";
}

public sealed class FirewallStatus
{
    public bool Supported { get; set; }
    public bool TcpRule { get; set; }
    public bool UdpRule { get; set; }
    public bool RulePathMismatch { get; set; }
    public string RuleProfiles { get; set; } = "";
    public string Exe { get; set; } = "";
    public List<NetProfile> Profiles { get; set; } = new();
    public bool PublicNetwork { get; set; }
    public string Advice { get; set; } = "";
}

public sealed class PeerDiag
{
    public string Fingerprint { get; set; } = "";
    public string Name { get; set; } = "";
    public ConnState State { get; set; }
    public string? Addr { get; set; }
    public string? Direction { get; set; }
    public long? ConnectedForMs { get; set; }
    public long? LastRxMsAgo { get; set; }
    public uint? RttMs { get; set; }
    public uint DialFailures { get; set; }
    public string LastError { get; set; } = "";
    public long? NextRetryInMs { get; set; }
    public List<string> Candidates { get; set; } = new();
    public List<string> Sources { get; set; } = new();
}

public sealed class Diagnostics
{
    public string Version { get; set; } = "";
    public string Os { get; set; } = "";
    public string TcpListen { get; set; } = "";
    public string TcpError { get; set; } = "";
    public DiscoStatus Discovery { get; set; } = new();
    public FirewallStatus Firewall { get; set; } = new();
    public List<PeerDiag> Peers { get; set; } = new();
    public List<string> ManualPeers { get; set; } = new();
    public string LogDir { get; set; } = "";
    public string ConfigDir { get; set; } = "";
    public string IpcEndpoint { get; set; } = "";
}

/// <summary>A pushed daemon event (params of the "event" notification).</summary>
public sealed class DaemonEvent
{
    public string Type { get; init; } = "";
    public JsonElement Raw { get; init; }

    public T? Get<T>(string field) =>
        Raw.TryGetProperty(field, out var v) ? Json.To<T>(v) : default;

    public string GetString(string field) =>
        Raw.TryGetProperty(field, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString() ?? "" : "";
}
