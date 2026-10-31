using LanMessenger.Core.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;
using Microsoft.UI.Xaml.Media.Imaging;

namespace LanMessenger;

public sealed class PathToImageConverter : IValueConverter
{
    public object? Convert(object value, Type targetType, object parameter, string language)
    {
        if (value is string p && !string.IsNullOrEmpty(p) && File.Exists(p))
        {
            try { return new BitmapImage { DecodePixelWidth = 480, UriSource = new Uri(p) }; }
            catch { return null; }
        }
        return null;
    }

    public object ConvertBack(object value, Type targetType, object parameter, string language) => throw new NotSupportedException();
}

/// <summary>Helpers for x:Bind function bindings.</summary>
public static class B
{
    public static Visibility Not(bool v) => v ? Visibility.Collapsed : Visibility.Visible;
    public static Visibility Vis(bool v) => v ? Visibility.Visible : Visibility.Collapsed;
    public static Visibility NotEmpty(string? s) => string.IsNullOrEmpty(s) ? Visibility.Collapsed : Visibility.Visible;
    public static Visibility Empty(int count) => count == 0 ? Visibility.Visible : Visibility.Collapsed;
    public static string Str(int v) => v.ToString();
    public static Visibility Is(int v, int want) => v == want ? Visibility.Visible : Visibility.Collapsed;
    public static Visibility Both(bool a, bool b) => a && b ? Visibility.Visible : Visibility.Collapsed;

    // shared avatar palette (same 8 colours and FNV-1a index on every platform; white initials >= 4.5:1 on each)
    // avatar discs: >= 3:1 against the dark list background (#202020/#272727, WCAG 1.4.11) and white initials >= 4.5:1
    private static readonly Windows.UI.Color[] AvatarColors =
    {
        Rgb(0x7C61C8), Rgb(0x2176BC), Rgb(0x108172), Rgb(0xC24D1D), Rgb(0x378139), Rgb(0xC6406F), Rgb(0x727819), Rgb(0xAC5E0A),
    };
    private static readonly Microsoft.UI.Xaml.Media.SolidColorBrush[] AvatarBrushes = AvatarColors.Select(c => new Microsoft.UI.Xaml.Media.SolidColorBrush(c)).ToArray();
    private static Windows.UI.Color Rgb(int v) => Windows.UI.Color.FromArgb(255, (byte)(v >> 16), (byte)(v >> 8), (byte)v);
    public static Microsoft.UI.Xaml.Media.Brush Avatar(int index) => AvatarBrushes[((index % 8) + 8) % 8];
    /// <summary>Receipt ticks: 1 sent ✓, 2 delivered ✓✓, 3 read ✓✓ (accent), 4 failed !</summary>
    public static string Tick(int t) => t switch { 1 => "\u2713", 2 or 3 => "\u2713\u2713", 4 => "!", _ => "" };
    public static bool TickIsRead(int t) => t == 3;
    public static Visibility TickVis(int t) => t == 0 ? Visibility.Collapsed : Visibility.Visible;
    // v0.6.1 P2
    public static string DraftTag(bool hasDraft) => hasDraft ? "[草稿] " : "";
    public static string TickUnlessDraft(bool hasDraft, int t) => hasDraft ? "" : Tick(t);
    public static string UnlessDraft(bool hasDraft, string s) => hasDraft ? "" : s;
    public static string DraftOr(bool hasDraft, string draft, string preview) => hasDraft ? draft.Replace('\n', ' ') : preview;
    public static string Picked(int n) => $"已选 {n} 条";
}

public sealed class MessageTemplateSelector : DataTemplateSelector
{
    public DataTemplate? Incoming { get; set; }
    public DataTemplate? Outgoing { get; set; }
    public DataTemplate? SystemNote { get; set; }

    protected override DataTemplate SelectTemplateCore(object item)
    {
        if (item is MessageItem m)
        {
            if (m.IsSystem) return SystemNote!;
            return m.IsOutgoing ? Outgoing! : Incoming!;
        }
        return Incoming!;
    }

    protected override DataTemplate SelectTemplateCore(object item, DependencyObject container) => SelectTemplateCore(item);
}
