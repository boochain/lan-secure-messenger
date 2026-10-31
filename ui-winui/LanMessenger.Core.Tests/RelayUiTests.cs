using System.Text.Json;
using LanMessenger.Core.Models;
using LanMessenger.Core.ViewModels;
using Xunit;

namespace LanMessenger.Core.Tests;

/// <summary>Relay client UI rules (docs/ipc.md "Relay client API").</summary>
public class RelayUiTests
{
    static T Parse<T>(string json) => JsonSerializer.Deserialize<T>(json, Json.Options)!;

    [Fact]
    public void PeerViaRelayOnlyWhileOnline()
    {
        var p = Parse<Peer>("""{"fingerprint":"AAAA","name":"Carol","online":true,"via_relay":true,"state":"connected","trust":"trusted"}""");
        Assert.True(new PeerItem(p).ViaRelay);
        p.Online = false;
        Assert.False(new PeerItem(p).ViaRelay);
    }

    [Fact]
    public void SettingsAndStatusCarryNoPassphrase()
    {
        var s = Parse<Settings>("""{"relay_enabled":true,"relay_host":"r.example","relay_port":29443,"relay_has_passphrase":true}""");
        Assert.True(s.RelayEnabled); Assert.Equal("r.example", s.RelayHost); Assert.Equal(29443, s.RelayPort); Assert.True(s.RelayHasPassphrase);
        var r = Parse<RelayStatus>("""{"enabled":true,"state":"auth_failed","detail":"x","server":"r:1","peers":0,"relayed_links":0,"has_passphrase":true}""");
        Assert.Equal("auth_failed", r.State); Assert.True(r.HasPassphrase);
    }

    [Theory]
    [InlineData("off", "未启用")]
    [InlineData("connecting", "正在连接中转服务器…")]
    [InlineData("online", "已连接中转服务器")]
    [InlineData("auth_failed", "中转服务器拒绝连接")]
    [InlineData("error", "无法连接中转服务器")]
    public void RelayStateCopy(string state, string want) => Assert.Equal(want, MainViewModel.RelayStateLabel(state));

    [Fact]
    public void PausedAndResumedTransferText()
    {
        var t = new Transfer { Id = "t", Name = "a.bin", PeerName = "Carol", Direction = Direction.In, Size = 1000, Transferred = 400,
                               State = TransferState.Offered, Error = "连接中断，重连后自动续传" };
        var i = new TransferItem(t);
        Assert.True(i.IsPaused); Assert.Equal("连接中断，重连后自动续传", i.StateText); Assert.False(i.IsError);
        t.State = TransferState.Active; t.Error = ""; t.ResumedFrom = 400; i.Update(t);
        Assert.False(i.IsPaused); Assert.StartsWith("续传中 · 从 40%", i.StateText);
        t.State = TransferState.Offered; t.ResumedFrom = 0; i.Update(t);
        Assert.Equal("等待接收", i.StateText);
    }
}
