using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Graphics;
using Windows.System;

namespace LanMessenger.Views;

/// <summary>OS-level picture window. Not a layer inside the main window.</summary>
public sealed class ImageViewerWindow : Window
{
    public ScrollViewer Scroll { get; }
    public Image Picture { get; }
    public BitmapImage? Bitmap { get; }

    public ImageViewerWindow(string path, MainWindow? main)
    {
        Title = "图片";
        var black = new SolidColorBrush(Colors.Black);
        var white = new SolidColorBrush(Colors.White);
        Bitmap = new BitmapImage(new Uri(path));
        Picture = new Image { Source = Bitmap, Stretch = Stretch.Uniform };
        Scroll = new ScrollViewer
        {
            Content = Picture,
            Tag = "lsm-content",
            ZoomMode = ZoomMode.Enabled,
            MinZoomFactor = 0.25f,
            MaxZoomFactor = 8,
            HorizontalScrollMode = ScrollMode.Enabled,
            VerticalScrollMode = ScrollMode.Enabled,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Auto,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            Background = black,
        };
        Picture.DoubleTapped += (_, ev) =>
        {
            ev.Handled = true;
            if (Scroll.ZoomFactor > 1.05f || Scroll.ZoomFactor < 0.95f) Scroll.ChangeView(null, null, 1f);
            else Scroll.ChangeView(null, null, Math.Min(8f, OneToOne()));
        };
        var bar = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Center, Spacing = 8, Padding = new Thickness(8), Background = black };
        Button Mk(string t, Action a)
        {
            var b = new Button { Content = t };
            b.Click += (_, _) => a();
            return b;
        }
        bar.Children.Add(Mk("适应", () => Scroll.ChangeView(0, 0, 1f)));
        bar.Children.Add(Mk("1:1", () => Scroll.ChangeView(null, null, Math.Min(8f, OneToOne()))));
        bar.Children.Add(Mk("+", () => Scroll.ChangeView(null, null, Math.Min(8f, Scroll.ZoomFactor * 1.25f))));
        bar.Children.Add(Mk("−", () => Scroll.ChangeView(null, null, Math.Max(0.25f, Scroll.ZoomFactor / 1.25f))));
        bar.Children.Add(Mk("关闭", Close));
        bar.Children.Add(new TextBlock
        {
            Text = "滚轮缩放  ·  拖动平移  ·  双击切换适应/1:1  ·  +/- 缩放  ·  Esc 关闭",
            Foreground = white,
            VerticalAlignment = VerticalAlignment.Center,
            Margin = new Thickness(12, 0, 0, 0),
        });
        var drag = new Grid { Background = black, Height = 48 };
        drag.Children.Add(new TextBlock { Text = "图片", Foreground = white, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(16, 0, 0, 0) });
        var root = new Grid { Background = black, IsTabStop = true };
        if (main?.Content is FrameworkElement host) root.RequestedTheme = host.ActualTheme;
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(48) });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        Grid.SetRow(drag, 0);
        Grid.SetRow(Scroll, 1);
        Grid.SetRow(bar, 2);
        root.Children.Add(drag);
        root.Children.Add(Scroll);
        root.Children.Add(bar);
        root.KeyDown += (_, ev) =>
        {
            if (ev.Key == VirtualKey.Escape) { Close(); ev.Handled = true; }
            else if (ev.Key == VirtualKey.Add || ev.Key == (VirtualKey)187) { Scroll.ChangeView(null, null, Math.Min(8f, Scroll.ZoomFactor * 1.25f)); ev.Handled = true; }
            else if (ev.Key == VirtualKey.Subtract || ev.Key == (VirtualKey)189) { Scroll.ChangeView(null, null, Math.Max(0.25f, Scroll.ZoomFactor / 1.25f)); ev.Handled = true; }
        };
        Content = root;
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(drag);
        try
        {
            var tb = AppWindow.TitleBar;
            tb.BackgroundColor = Colors.Black;
            tb.ForegroundColor = Colors.White;
            tb.InactiveBackgroundColor = Colors.Black;
            tb.InactiveForegroundColor = Colors.White;
            tb.ButtonBackgroundColor = Colors.Black;
            tb.ButtonForegroundColor = Colors.White;
            tb.ButtonHoverBackgroundColor = Colors.Black;
            tb.ButtonHoverForegroundColor = Colors.White;
            tb.ButtonPressedBackgroundColor = Colors.Black;
            tb.ButtonPressedForegroundColor = Colors.White;
            tb.ButtonInactiveBackgroundColor = Colors.Black;
            tb.ButtonInactiveForegroundColor = Colors.White;
            tb.PreferredHeightOption = TitleBarHeightOption.Tall;
        }
        catch { }
        try
        {
            if (main != null)
            {
                var p = main.AppWindow.Position;
                var s = main.AppWindow.Size;
                AppWindow.MoveAndResize(new RectInt32(p.X + 48, p.Y + 48, Math.Max(640, s.Width - 96), Math.Max(480, s.Height - 96)));
            }
        }
        catch { }
        Activated += (_, e) => { if (e.WindowActivationState != WindowActivationState.Deactivated) root.Focus(FocusState.Programmatic); };
    }

    float OneToOne()
    {
        if (Picture.ActualWidth > 1 && Bitmap?.PixelWidth > 0)
            return Math.Max(1f, (float)(Bitmap.PixelWidth / Picture.ActualWidth));
        return 2.5f;
    }
}
