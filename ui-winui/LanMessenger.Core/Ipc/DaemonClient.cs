using System.Text.Json;
using LanMessenger.Core.Models;

namespace LanMessenger.Core.Ipc;

/// <summary>Typed wrapper over the JSON-RPC methods in docs/ipc.md.</summary>
public sealed class DaemonClient
{
    public DaemonConnection Connection { get; }
    public DaemonClient(DaemonConnection c) { Connection = c; }

    private Task<JsonElement> Call(string m, object? p = null) => Connection.CallAsync(m, p);
    private Task<T> Call<T>(string m, object? p = null) => Connection.CallAsync<T>(m, p);

    public Task<JsonElement> HelloAsync() => Call("hello", new { client = "LanMessenger", version = 1 });
    public Task SubscribeAsync() => Call("subscribe");
    public Task<Snapshot> GetSnapshotAsync() => Call<Snapshot>("get_snapshot");
    public Task RefreshPeersAsync() => Call("refresh_peers");
    public Task<JsonElement> GetStorageAsync() => Call("get_storage");
    public Task<JsonElement> CleanupStorageAsync(bool failedPartials = true, bool orphans = true, bool finishedTransfers = false)
        => Call("cleanup_storage", new { failed_partials = failedPartials, orphans, finished_transfers = finishedTransfers });
    public Task<List<Message>> GetMessagesAsync(string conversation, int limit = 200, long? beforeTs = null)
        => Call<List<Message>>("get_messages", new { conversation, limit, before_ts = beforeTs });
    public Task<Message> SendTextAsync(string conversation, string body) => Call<Message>("send_text", new { conversation, body });
    public Task<Transfer> SendFileAsync(string conversation, string path) => Call<Transfer>("send_file", new { conversation, path });
    public Task CancelTransferAsync(string id) => Call("cancel_transfer", new { id });
    public Task<Transfer> RetryTransferAsync(string id) => Call<Transfer>("retry_transfer", new { id });
    public Task AcceptTransferAsync(string id) => Call("accept_transfer", new { id });
    public Task RejectTransferAsync(string id) => Call("reject_transfer", new { id });
    public Task AddPeerAsync(string address) => Call("add_peer", new { address });
    public Task RemoveManualPeerAsync(string address) => Call("remove_manual_peer", new { address });
    public Task<Peer> SetTrustAsync(string fingerprint, Trust trust) => Call<Peer>("set_trust", new { fingerprint, trust });
    public Task ForgetPeerAsync(string fingerprint) => Call("forget_peer", new { fingerprint });
    public Task<Me> SetDisplayNameAsync(string name) => Call<Me>("set_display_name", new { name });
    public Task<Me> CompleteFirstRunAsync(string? name) => Call<Me>("complete_first_run", new { name });
    public Task SetMutedAsync(string conversation, bool muted) => Call("set_muted", new { conversation, muted });
    public Task MarkReadAsync(string conversation) => Call("mark_read", new { conversation });
    /// <summary>Delete one message on this device only (the other side keeps its copy).</summary>
    public Task DeleteMessageAsync(string id) => Call("delete_message", new { id });
    public Task ClearConversationAsync(string conversation) => Call("clear_conversation", new { conversation });
    public Task<Group> CreateGroupAsync(string name, IEnumerable<string> members) => Call<Group>("create_group", new { name, members = members.ToArray() });
    public Task<Group> InviteToGroupAsync(string groupId, IEnumerable<string> members) => Call<Group>("invite_to_group", new { group_id = groupId, members = members.ToArray() });
    public Task RemoveGroupMemberAsync(string groupId, string fingerprint) => Call("remove_group_member", new { group_id = groupId, fingerprint });
    public Task LeaveGroupAsync(string groupId) => Call("leave_group", new { group_id = groupId });
    public Task<List<Message>> SearchAsync(string query, int limit = 100) => Call<List<Message>>("search", new { query, limit });
    public Task BackupAsync(string path, string password) => Call("backup", new { path, password });
    public Task AccountRegisterAsync(string user, string password) => Call("account_register", new { user, password });
    public Task AccountLoginAsync(string user, string password) => Call("account_login", new { user, password });
    public Task AccountBackupPutAsync(string user, string password) => Call("account_backup_put", new { user, password });
    public Task AccountBackupGetAsync(string user, string password) => Call("account_backup_get", new { user, password });
    public Task<JsonElement> RestoreAsync(string path, string password) => Call("restore", new { path, password });
    public async Task<string> PinOfferAsync() => (await Call("pin_offer")).GetProperty("pin").GetString() ?? "";
    public async Task<bool> PinPairAsync(string fingerprint, string pin) => (await Call("pin_pair", new { fingerprint, pin })).GetProperty("ok").GetBoolean();
    public async Task<bool> UpdateSettingsAsync(object patch)
    {
        var r = await Call("update_settings", new { settings = patch });
        return r.TryGetProperty("restart_required", out var x) && x.GetBoolean();
    }
    public Task<Diagnostics> GetDiagnosticsAsync(bool skipFirewall = false) => Call<Diagnostics>("get_diagnostics", new { skip_firewall = skipFirewall });
    public Task<FirewallStatus> FirewallStatusAsync() => Call<FirewallStatus>("firewall_status");
    public Task FirewallSetupAsync(bool includePublic) => Call("firewall_setup", new { include_public = includePublic });
    public Task ShutdownAsync() => Call("shutdown");
}
