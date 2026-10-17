using LanMessenger.Core.Models;
using LanMessenger.Core.ViewModels;
using Xunit;

namespace LanMessenger.Core.Tests;

/// <summary>Shared presentation rules (design/redesign/spec.md §6) — must agree with Android/GTK/XP.</summary>
public class RedesignFmtTests
{
    [Theory]
    [InlineData("Alice Chen", "AC")]
    [InlineData("bob", "B")]
    [InlineData("王工", "王")]
    [InlineData("  ", "?")]
    public void Initials(string name, string want) => Assert.Equal(want, Fmt.Initials(name));

    [Fact]
    public void AvatarIndexIsFnv1a()
    {
        // FNV-1a 32("a") = 0xE40C292C -> % 8 = 4 ; ("") = 0x811C9DC5 -> 5
        Assert.Equal(4, Fmt.AvatarIndex("a"));
        Assert.Equal(5, Fmt.AvatarIndex(""));
    }

    static long Ms(DateTime t) => new DateTimeOffset(t).ToUnixTimeMilliseconds();

    [Fact]
    public void ListTimeAndDateLabels()
    {
        var now = new DateTime(2026, 10, 17, 15, 0, 0, DateTimeKind.Local);
        Assert.Equal("14:05", Fmt.ListTime(Ms(now.AddMinutes(-55)), now));
        Assert.Equal("昨天", Fmt.ListTime(Ms(now.AddDays(-1)), now));
        Assert.Equal("周一", Fmt.ListTime(Ms(new DateTime(2026, 10, 12, 9, 0, 0)), now));
        Assert.Equal("09/01", Fmt.ListTime(Ms(new DateTime(2026, 9, 1, 9, 0, 0)), now));
        Assert.Equal("2025/09/01", Fmt.ListTime(Ms(new DateTime(2025, 9, 1, 9, 0, 0)), now));
        Assert.Equal("今天", Fmt.DateLabel(Ms(now), now));
        Assert.Equal("昨天", Fmt.DateLabel(Ms(now.AddDays(-1)), now));
        Assert.Equal("9月1日", Fmt.DateLabel(Ms(new DateTime(2026, 9, 1)), now));
        Assert.Equal("2025年9月1日", Fmt.DateLabel(Ms(new DateTime(2025, 9, 1)), now));
    }

    static MessageItem M(string id, bool outg, string sender, DateTime t, string conv = "peer:x") => new(new Message
    {
        Id = id, Conversation = conv, Direction = outg ? Direction.Out : Direction.In, Kind = MsgKind.Text,
        SenderName = sender, TsMs = Ms(t), Status = MsgStatus.Delivered, Body = id,
    });

    [Fact]
    public void GroupingBreaksOnSenderDayAndGap()
    {
        var t = new DateTime(2026, 10, 16, 10, 0, 0);
        var l = new List<MessageItem>
        {
            M("a", false, "王工", t), M("b", false, "王工", t.AddMinutes(2)),
            M("c", true, "我", t.AddMinutes(3)),
            M("d", true, "我", t.AddMinutes(9)),             // >5 min gap
            M("e", true, "我", t.AddDays(1)),                // new day
        };
        Grouping.Apply(l);
        Assert.True(l[0].ShowE2E); Assert.False(l[1].ShowE2E);
        Assert.True(l[0].HasDate); Assert.False(l[1].HasDate); Assert.True(l[4].HasDate);
        Assert.True(l[0].IsFirstInGroup); Assert.False(l[0].IsLastInGroup);
        Assert.False(l[1].IsFirstInGroup); Assert.True(l[1].IsLastInGroup);
        Assert.True(l[2].IsFirstInGroup && l[2].IsLastInGroup);
        Assert.True(l[3].IsFirstInGroup && l[3].IsLastInGroup);
        Assert.True(l[4].IsFirstInGroup);
        Assert.Equal(2, l[0].Tick == 0 ? 2 : -1); // incoming: no tick
        Assert.Equal(2, l[2].Tick);              // delivered
    }

    [Fact]
    public void GroupSenderShownOncePerRun()
    {
        var t = new DateTime(2026, 10, 16, 10, 0, 0);
        var l = new List<MessageItem> { M("a", false, "李娜", t, "group:g"), M("b", false, "李娜", t.AddMinutes(1), "group:g"), M("c", false, "王工", t.AddMinutes(1), "group:g") };
        Grouping.Apply(l);
        Assert.True(l[0].ShowSender); Assert.False(l[1].ShowSender); Assert.True(l[2].ShowSender);
    }
}
