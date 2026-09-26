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
