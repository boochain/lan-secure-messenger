using LanMessenger.Core.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace LanMessenger.Views;

public sealed partial class DiagnosticsPage : Page
{
    public MainViewModel VM => App.VM;

    public DiagnosticsPage()
    {
        InitializeComponent();
        Loaded += async (_, _) => await VM.RefreshDiagnosticsAsync(false);
    }

    private async void RemoveManual_Click(object sender, RoutedEventArgs e)
    {
        if (sender is FrameworkElement { Tag: string addr }) await VM.RemoveManualPeerAsync(addr);
    }
}
