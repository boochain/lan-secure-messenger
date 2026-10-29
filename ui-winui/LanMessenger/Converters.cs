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

    // shared avatar palette (same 8 colours and FNV-1a index on every platform; white initials >= 4.5:1 on each)
    private static readonly Windows.UI.Color[] AvatarColors =
    {
        Rgb(0x6D4FC2), Rgb(0x1F6FB2), Rgb(0x0F7A6C), Rgb(0xB4471B), Rgb(0x2E6B2F), Rgb(0x9C2F55), Rgb(0x5A5F14), Rgb(0x8A4B08),
    };
    private static readonly Microsoft.UI.Xaml.Media.SolidColorBrush[] AvatarBrushes = AvatarColors.Select(c => new Microsoft.UI.Xaml.Media.SolidColorBrush(c)).ToArray();
    private static Windows.UI.Color Rgb(int v) => Windows.UI.Color.FromArgb(255, (byte)(v >> 16), (byte)(v >> 8), (byte)v);
    public static Microsoft.UI.Xaml.Media.Brush Avatar(int index) => AvatarBrushes[((index % 8) + 8) % 8];
    /// <summary>Receipt ticks: 1 sent ✓, 2 delivered ✓✓, 3 read ✓✓ (accent), 4 failed !</summary>
    public static string Tick(int t) => t switch { 1 => "\u2713", 2 or 3 => "\u2713\u2713", 4 => "!", _ => "" };
    public static Visibility TickVis(int t) => t == 0 ? Visibility.Collapsed : Visibility.Visible;
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
