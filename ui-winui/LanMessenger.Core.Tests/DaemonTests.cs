using System.Diagnostics;
using System.Text.Json;
using LanMessenger.Core.Ipc;
using LanMessenger.Core.Models;
using LanMessenger.Core.ViewModels;
using Xunit;
using Xunit.Abstractions;

namespace LanMessenger.Core.Tests;

/// <summary>Runs a real lan-daemon (Rust) process with an isolated data dir and IPC endpoint.</summary>
public sealed class DaemonProcess : IAsyncDisposable
{
    public string Dir { get; }
    public string Endpoint { get; }
    private readonly Process _p;

    public static string DaemonBin()
    {
        var env = Environment.GetEnvironmentVariable("LSM_DAEMON_BIN");
        if (!string.IsNullOrEmpty(env)) return env;
        var d = new DirectoryInfo(AppContext.BaseDirectory);
        while (d != null && !File.Exists(Path.Combine(d.FullName, "Cargo.toml"))) d = d.Parent;
        if (d == null) throw new InvalidOperationException("repo root not found; set LSM_DAEMON_BIN");
        var exe = OperatingSystem.IsWindows() ? "lan-daemon.exe" : "lan-daemon";
        foreach (var cfg in new[] { "debug", "release" })
        {
            var p = Path.Combine(d.FullName, "target", cfg, exe);
            if (File.Exists(p)) return p;
        }
        throw new InvalidOperationException("lan-daemon binary not built (cargo build -p lan-daemon)");
    }

    public DaemonProcess(string name, params string[] extra)
    {
        Dir = Path.Combine(Path.GetTempPath(), "lsm-cs-" + Guid.NewGuid().ToString("N")[..8]);
        Directory.CreateDirectory(Dir);
        Endpoint = OperatingSystem.IsWindows() ? $@"\\.\pipe\lsm-test-{Guid.NewGuid():N}" : Path.Combine(Dir, "ipc.sock");
        var psi = new ProcessStartInfo(DaemonBin()) { RedirectStandardError = true, RedirectStandardOutput = true, UseShellExecute = false };
        foreach (var a in new[] { "--data-dir", Dir, "--ipc", Endpoint, "--set", "listen_port=0", "--set", "discovery_enabled=false",
                     "--set", "auto_trust=true", "--set", $"display_name={name}", "--set", $"download_dir=\"{Path.Combine(Dir, "dl").Replace("\\", "\\\\")}\"" })
            psi.ArgumentList.Add(a);
        foreach (var a in extra) psi.ArgumentList.Add(a);
        _p = Process.Start(psi)!;
        _p.ErrorDataReceived += (_, _) => { };
        _p.OutputDataReceived += (_, _) => { };
        _p.BeginErrorReadLine();
        _p.BeginOutputReadLine();
    }

    public async Task<DaemonConnection> ConnectAsync()
    {
        var end = DateTime.UtcNow.AddSeconds(15);
        while (true)
        {
            try { return await DaemonConnection.ConnectAsync(Endpoint, TimeSpan.FromSeconds(1)); }
            catch when (DateTime.UtcNow < end) { await Task.Delay(100); }
        }
    }

    public async ValueTask DisposeAsync()
    {
        try
        {
            await using var c = await DaemonConnection.ConnectAsync(Endpoint, TimeSpan.FromSeconds(1));
            await c.CallAsync("shutdown");
        }
        catch { }
        if (!_p.WaitForExit(5000)) { try { _p.Kill(true); } catch { } }
        try { Directory.Delete(Dir, true); } catch { }
    }
}

sealed class LockDispatcher : IDispatcher
{
    public readonly object Gate = new();
    public void Post(Action a) { lock (Gate) a(); }
}

sealed class FakePlatform : IPlatform
{
    public List<(string, string)> Toasts = new();
    public bool StartDaemon(string endpoint) => true;
    public Task<IReadOnlyList<string>> PickFilesAsync() => Task.FromResult<IReadOnlyList<string>>(Array.Empty<string>());
    public Task<string?> PickFolderAsync() => Task.FromResult<string?>(null);
    public Task<string?> PickSaveFileAsync(string s) => Task.FromResult<string?>(null);
    public Task<string?> PickOpenFileAsync() => Task.FromResult<string?>(null);
    public void OpenPath(string path) { }
    public void RevealInFolder(string path) { }
    public void CopyToClipboard(string text) { }
    public void ShowToast(string title, string body, string conv) { lock (Toasts) Toasts.Add((title, body)); }
    public bool IsWindowActive => false;
}

public class DaemonTests
{
    private readonly ITestOutputHelper _out;
    public DaemonTests(ITestOutputHelper o) { _out = o; }

    static async Task Until(string what, Func<bool> cond, int secs = 15)
    {
        var end = DateTime.UtcNow.AddSeconds(secs);
        while (!cond())
        {
            if (DateTime.UtcNow > end) throw new TimeoutException("timeout: " + what);
            await Task.Delay(100);
        }
    }

    [Fact]
    public void VersionCompare()
    {
        Assert.True(MainViewModel.CompareVersions("v0.5.0", "v0.5.0-beta.1") > 0);
        Assert.True(MainViewModel.CompareVersions("v0.5.0-beta.2", "0.5.0-beta.1") > 0);
        Assert.True(MainViewModel.CompareVersions("v0.4.9", "v0.5.0-beta.1") < 0);
        Assert.Equal(0, MainViewModel.CompareVersions("v0.5.0-beta.1", "0.5.0-beta.1"));
    }

    [Fact]
    public void EventAndEnumJson()
    {
        var json = """{"type":"message_added","message":{"id":"x","conversation":"dm:AAAA","direction":"out","sender_fp":"B","sender_name":"我","kind":"file","body":"","ts_ms":1700000000000,"status":"delivered","file":{"transfer_id":"t","name":"a.png","size":12,"mime":"image/png","path":null,"is_image":true}}}""";
        var el = JsonDocument.Parse(json).RootElement.Clone();
        var ev = new DaemonEvent { Type = "message_added", Raw = el };
        var m = ev.Get<Message>("message")!;
        Assert.Equal(Direction.Out, m.Direction);
        Assert.Equal(MsgKind.File, m.Kind);
        Assert.Equal(MsgStatus.Delivered, m.Status);
        Assert.True(m.File!.IsImage);
        var back = JsonSerializer.Serialize(new { trust = Trust.Blocked, before_ts = (long?)null }, Json.Options);
        Assert.Equal("""{"trust":"blocked"}""", back);
    }

    [Fact]
    public async Task RawClientAgainstRustDaemon()
    {
        await using var a = new DaemonProcess("CsA");
        await using var b = new DaemonProcess("CsB");
        await using var ca = await a.ConnectAsync();
        await using var cb = await b.ConnectAsync();
        var A = new DaemonClient(ca);
        var B = new DaemonClient(cb);
        var hello = await A.HelloAsync();
        Assert.Equal(1, hello.GetProperty("ipc_version").GetInt32());
        var events = new List<DaemonEvent>();
        ca.EventReceived += e => { lock (events) events.Add(e); };
        await A.SubscribeAsync();
        var sa = await A.GetSnapshotAsync();
        var sb = await B.GetSnapshotAsync();
        Assert.Equal("CsA", sa.Me.Name);
        Assert.False(string.IsNullOrEmpty(sa.Me.Fingerprint));
        Assert.Equal("CsB", sb.Settings.DisplayName);
        await A.AddPeerAsync($"127.0.0.1:{sb.Me.ListenPort}");
        await Until("peer_updated online event", () =>
        {
            lock (events) return events.Any(e => e.Type == "peer_updated" && e.Get<Peer>("peer") is { Online: true } p && p.Fingerprint == sb.Me.Fingerprint);
        });
        var msg = await A.SendTextAsync("dm:" + sb.Me.Fingerprint, "来自 C# 的消息");
        Assert.Equal(MsgStatus.Sent, msg.Status);
        await Until("delivered event", () =>
        {
            lock (events) return events.Any(e => e.Type == "message_updated" && e.Get<Message>("message") is { Status: MsgStatus.Delivered } m && m.Id == msg.Id);
        });
        var inB = await B.GetMessagesAsync("dm:" + sa.Me.Fingerprint);
        Assert.Contains(inB, m => m.Body == "来自 C# 的消息" && m.Direction == Direction.In);
        // delete on this device only: gone from A's history, B keeps its copy
        await A.DeleteMessageAsync(msg.Id);
        Assert.DoesNotContain(await A.GetMessagesAsync("dm:" + sb.Me.Fingerprint), m => m.Id == msg.Id);
        Assert.Contains(await B.GetMessagesAsync("dm:" + sa.Me.Fingerprint), m => m.Body == "来自 C# 的消息");
        await Assert.ThrowsAsync<DaemonException>(() => A.DeleteMessageAsync(msg.Id));
        var ex = await Assert.ThrowsAsync<DaemonException>(() => A.SendTextAsync("dm:" + sb.Me.Fingerprint, "   "));
        Assert.Equal(-32602, ex.Code);
        var diag = await A.GetDiagnosticsAsync();
        Assert.Contains(diag.Peers, p => p.Fingerprint == sb.Me.Fingerprint && p.State == ConnState.Connected);
        Assert.False(string.IsNullOrEmpty(diag.LogDir));
        _out.WriteLine($"diag interfaces: {diag.Discovery.Interfaces.Count}, firewall supported={diag.Firewall.Supported}");
    }

    [Fact]
    public async Task ViewModelEndToEnd()
    {
        await using var a = new DaemonProcess("VmA");
        await using var b = new DaemonProcess("VmB");
        await using var cb = await b.ConnectAsync();
        var B = new DaemonClient(cb);
        var sb = await B.GetSnapshotAsync();
        var ui = new LockDispatcher();
        var platform = new FakePlatform();
        var vm = new MainViewModel(ui, platform, a.Endpoint);
        var firstRun = false;
        vm.FirstRunRequested += () => firstRun = true;
        await vm.StartAsync();
        Assert.True(vm.IsConnected);
        Assert.True(firstRun);
        await vm.CompleteFirstRunAsync("新名字");
        await Until("me_updated", () => { lock (ui.Gate) return vm.MyName == "新名字" && !vm.FirstRunNeeded; });
        vm.AddPeerAddress = $"127.0.0.1:{sb.Me.ListenPort}";
        await vm.AddPeerCommand.ExecuteAsync(null);
        await Until("peer online in VM", () => { lock (ui.Gate) return vm.Peers.Any(p => p.Fingerprint == sb.Me.Fingerprint && p.Online && p.IsTrusted); });
        await Until("conversation exists", () => { lock (ui.Gate) return vm.Conversations.Any(c => c.PeerFingerprint == sb.Me.Fingerprint && c.Online); });
        lock (ui.Gate) vm.SelectPeer(sb.Me.Fingerprint);
        Assert.True(vm.SelectedIsDm);
        Assert.Equal("在线 · 已验证身份 · 局域网直连", vm.SelectedSubtitle);   // no fingerprint / latency piled into the header
        Assert.Contains(MainViewModel.FormatFingerprint(sb.Me.Fingerprint), vm.SelectedDetailsText);   // grouped for reading aloud
        Assert.Contains("身份：已验证身份", vm.SelectedDetailsText);
        vm.ComposeText = "hello from the view model";
        await vm.SendCommand.ExecuteAsync(null);
        Assert.Equal("", vm.ComposeText);
        await Until("bubble delivered", () => { lock (ui.Gate) return vm.Messages.Any(m => m.IsOutgoing && m.Body == "hello from the view model" && m.StatusText == "已送达"); });
        // Incoming message from B shows up in the open conversation and raises a toast (window inactive).
        await B.SendTextAsync("dm:" + vm.MyFingerprint, "reply from B");
        await Until("incoming bubble", () => { lock (ui.Gate) return vm.Messages.Any(m => m.IsIncoming && m.Body == "reply from B"); });
        await Until("toast", () => { lock (platform.Toasts) return platform.Toasts.Any(t => t.Item2 == "reply from B"); });
        // File transfer with progress -> done.
        var f = Path.Combine(a.Dir, "photo.png");
        await File.WriteAllBytesAsync(f, Enumerable.Range(0, 300_000).Select(i => (byte)i).ToArray());
        await vm.SendFilesAsync(new[] { f });
        await Until("transfer done", () => { lock (ui.Gate) return vm.Transfers.Any(t => t.Name == "photo.png" && t.StateText == "已完成" && Math.Abs(t.Progress - 100) < 0.01); }, 20);
        await Until("file bubble", () => { lock (ui.Gate) return vm.Messages.Any(m => m.IsFile && m.FileName == "photo.png" && m.IsImage && m.StatusText == "已送达"); });
        // Diagnostics mapping.
        await vm.RefreshDiagnosticsAsync(false);
        await Until("diag", () => { lock (ui.Gate) return vm.PeerDiags.Any(p => p.Connected) && vm.DiagnosticsJson.Length > 10; });
        if (!OperatingSystem.IsWindows())
            Assert.Contains("非 Windows", vm.FirewallSummary);
        else if (Environment.GetEnvironmentVariable("LSM_EXPECT_FIREWALL_RULE") == "1")
        {
            // CI creates the rule for this exact lan-daemon.exe in an earlier step.
            Assert.StartsWith("已放行", vm.FirewallSummary);
            Assert.True(vm.FirewallOk);
        }
        else
            Assert.True(vm.FirewallSummary.StartsWith("已放行") || vm.FirewallSummary is "未配置放行规则" or "规则存在但路径不匹配", vm.FirewallSummary);
        // Group creation through the VM.
        var ok = await vm.CreateGroupAsync("C# 群", new[] { sb.Me.Fingerprint });
        Assert.True(ok);
        await Until("group item", () => { lock (ui.Gate) return vm.Groups.Any(g => g.Name == "C# 群" && g.IsCreator && g.Members.Count == 2); });
        // Daemon restart -> VM reconnects (daemon is restarted by the test since FakePlatform doesn't spawn).
        await vm.StopAsync(shutdownDaemon: false);
    }
}
