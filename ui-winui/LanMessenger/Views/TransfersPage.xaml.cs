using LanMessenger.Core.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace LanMessenger.Views;

public sealed partial class TransfersPage : Page
{
    public MainViewModel VM => App.VM;

    public TransfersPage() { InitializeComponent(); }

    private static TransferItem? ItemOf(object sender) => (sender as FrameworkElement)?.DataContext as TransferItem;

    private void Cancel_Click(object sender, RoutedEventArgs e) => VM.CancelTransferCommand.Execute(ItemOf(sender));
    private void Retry_Click(object sender, RoutedEventArgs e) => VM.RetryTransferCommand.Execute(ItemOf(sender));
    private void Open_Click(object sender, RoutedEventArgs e) => VM.OpenTransferCommand.Execute(ItemOf(sender));
    private void Reveal_Click(object sender, RoutedEventArgs e) => VM.RevealTransferCommand.Execute(ItemOf(sender));
}
