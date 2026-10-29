using System.Collections.Concurrent;
using System.IO.Pipes;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using LanMessenger.Core.Models;

namespace LanMessenger.Core.Ipc;

public sealed class DaemonException : Exception
{
    public int Code { get; }
    public DaemonException(int code, string message) : base(message) { Code = code; }
}

public static class Endpoint
{
    /// <summary>Same rule as lan-core ipc::default_endpoint().</summary>
    public static string Default()
    {
        var user = Environment.GetEnvironmentVariable("USERNAME")
                   ?? Environment.GetEnvironmentVariable("USER")
                   ?? Environment.UserName;
        var sb = new StringBuilder();
        foreach (var c in user ?? "user")
            sb.Append(char.IsAsciiLetterOrDigit(c) || c is '-' or '_' or '.' ? c : '_');
        var tag = sb.Length == 0 ? "user" : sb.ToString();
        if (OperatingSystem.IsWindows())
            return $@"\\.\pipe\lan-secure-messenger-{tag}";
        var dir = Environment.GetEnvironmentVariable("XDG_RUNTIME_DIR");
        if (string.IsNullOrEmpty(dir) || !Directory.Exists(dir)) dir = "/tmp";
        return $"{dir}/lan-secure-messenger-{tag}.sock";
    }
}

/// <summary>
/// One connection to lan-daemon: newline-delimited JSON-RPC 2.0 requests + pushed events.
/// Thread-safe; events are raised on a background thread.
/// </summary>
public sealed class DaemonConnection : IAsyncDisposable
{
    private readonly Stream _stream;
    private readonly StreamReader _reader;
    private readonly SemaphoreSlim _writeLock = new(1, 1);
    private readonly ConcurrentDictionary<long, TaskCompletionSource<JsonElement>> _pending = new();
    private readonly CancellationTokenSource _cts = new();
    private long _nextId;
    private Task? _readLoop;
    private int _closed;

    public event Action<DaemonEvent>? EventReceived;
    public event Action<Exception?>? Disconnected;
    public string EndpointName { get; }
    public bool IsConnected => _closed == 0;

    private DaemonConnection(Stream s, string endpoint)
    {
        _stream = s;
        EndpointName = endpoint;
        _reader = new StreamReader(s, new UTF8Encoding(false), false, 64 * 1024, leaveOpen: true);
    }

    public static async Task<DaemonConnection> ConnectAsync(string endpoint, TimeSpan timeout, CancellationToken ct = default)
    {
        Stream stream;
        if (endpoint.StartsWith(@"\\.\pipe\", StringComparison.OrdinalIgnoreCase))
        {
            var name = endpoint.Substring(@"\\.\pipe\".Length);
            var pipe = new NamedPipeClientStream(".", name, PipeDirection.InOut, PipeOptions.Asynchronous);
            await pipe.ConnectAsync((int)timeout.TotalMilliseconds, ct).ConfigureAwait(false);
            stream = pipe;
        }
        else
        {
            var sock = new Socket(AddressFamily.Unix, SocketType.Stream, ProtocolType.Unspecified);
            using var tcs = CancellationTokenSource.CreateLinkedTokenSource(ct);
            tcs.CancelAfter(timeout);
            try
            {
                await sock.ConnectAsync(new UnixDomainSocketEndPoint(endpoint), tcs.Token).ConfigureAwait(false);
            }
            catch
            {
                sock.Dispose();
                throw;
            }
            stream = new NetworkStream(sock, ownsSocket: true);
        }
        var c = new DaemonConnection(stream, endpoint);
        c._readLoop = Task.Run(c.ReadLoopAsync);
        return c;
    }

    private async Task ReadLoopAsync()
    {
        Exception? error = null;
        try
        {
            while (!_cts.IsCancellationRequested)
            {
                var line = await _reader.ReadLineAsync(_cts.Token).ConfigureAwait(false);
                if (line == null) break;
                if (line.Length == 0) continue;
                JsonElement root;
                try
                {
                    using var doc = JsonDocument.Parse(line);
                    root = doc.RootElement.Clone();
                }
                catch (JsonException)
                {
                    continue;
                }
                if (root.TryGetProperty("method", out var m))
                {
                    if (m.GetString() == "event" && root.TryGetProperty("params", out var p))
                    {
                        var type = p.TryGetProperty("type", out var t) ? t.GetString() ?? "" : "";
                        try { EventReceived?.Invoke(new DaemonEvent { Type = type, Raw = p }); }
                        catch { /* never let a UI handler kill the read loop */ }
                    }
                    continue;
                }
                if (!root.TryGetProperty("id", out var idEl) || idEl.ValueKind != JsonValueKind.Number) continue;
                if (!_pending.TryRemove(idEl.GetInt64(), out var tcs)) continue;
                if (root.TryGetProperty("error", out var err))
                {
                    var code = err.TryGetProperty("code", out var c) && c.ValueKind == JsonValueKind.Number ? c.GetInt32() : -32000;
                    var msg = err.TryGetProperty("message", out var mm) ? mm.GetString() ?? "error" : "error";
                    tcs.TrySetException(new DaemonException(code, msg));
                }
                else
                {
                    tcs.TrySetResult(root.TryGetProperty("result", out var r) ? r : default);
                }
            }
        }
        catch (OperationCanceledException) { }
        catch (Exception e) { error = e; }
        Close(error);
    }

    private void Close(Exception? error)
    {
        if (Interlocked.Exchange(ref _closed, 1) != 0) return;
        foreach (var kv in _pending)
            kv.Value.TrySetException(new IOException("与后台服务的连接已断开", error));
        _pending.Clear();
        try { Disconnected?.Invoke(error); } catch { }
    }

    public async Task<JsonElement> CallAsync(string method, object? parameters = null, CancellationToken ct = default, TimeSpan? timeout = null)
    {
        if (_closed != 0) throw new IOException("与后台服务的连接已断开");
        var id = Interlocked.Increment(ref _nextId);
        var tcs = new TaskCompletionSource<JsonElement>(TaskCreationOptions.RunContinuationsAsynchronously);
        _pending[id] = tcs;
        var req = new Dictionary<string, object?>
        {
            ["jsonrpc"] = "2.0",
            ["id"] = id,
            ["method"] = method,
            ["params"] = parameters ?? new Dictionary<string, object?>(),
        };
        var bytes = JsonSerializer.SerializeToUtf8Bytes(req, Json.Options);
        await _writeLock.WaitAsync(ct).ConfigureAwait(false);
        try
        {
            await _stream.WriteAsync(bytes, ct).ConfigureAwait(false);
            await _stream.WriteAsync(new byte[] { (byte)'\n' }, ct).ConfigureAwait(false);
            await _stream.FlushAsync(ct).ConfigureAwait(false);
        }
        catch (Exception e)
        {
            _pending.TryRemove(id, out _);
            Close(e);
            throw new IOException("与后台服务的连接已断开", e);
        }
        finally
        {
            _writeLock.Release();
        }
        var limit = timeout ?? TimeSpan.FromSeconds(method is "firewall_setup" ? 180 : 30);
        using var reg = ct.Register(() => tcs.TrySetCanceled());
        var done = await Task.WhenAny(tcs.Task, Task.Delay(limit, CancellationToken.None)).ConfigureAwait(false);
        if (done != tcs.Task)
        {
            _pending.TryRemove(id, out _);
            throw new TimeoutException($"后台服务响应超时 ({method})");
        }
        return await tcs.Task.ConfigureAwait(false);
    }

    public async Task<T> CallAsync<T>(string method, object? parameters = null, CancellationToken ct = default)
    {
        var r = await CallAsync(method, parameters, ct).ConfigureAwait(false);
        return Json.To<T>(r) ?? throw new DaemonException(-32000, $"empty result for {method}");
    }

    public async ValueTask DisposeAsync()
    {
        _cts.Cancel();
        try { _stream.Dispose(); } catch { }
        if (_readLoop != null)
        {
            try { await _readLoop.ConfigureAwait(false); } catch { }
        }
        Close(null);
    }
}
