using LanMessenger.Core.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace LanMessenger.Views;

public sealed partial class FilesPage : Page
{
    public MainViewModel VM => App.VM;
    public FilesPage()
    {
        InitializeComponent();
        Loaded += (_, __) => { if (VM.RefreshStorageCommand.CanExecute(null)) _ = VM.RefreshStorageCommand.ExecuteAsync(null); };
    }
    private static TransferItem? ItemOf(object sender) => (sender as FrameworkElement)?.DataContext as TransferItem;
    private void Open_Click(object sender, RoutedEventArgs e) => VM.OpenTransferCommand.Execute(ItemOf(sender));
    private void Reveal_Click(object sender, RoutedEventArgs e) => VM.RevealTransferCommand.Execute(ItemOf(sender));
}
