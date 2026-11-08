using LanMessenger.Core.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace LanMessenger.Views;

public sealed partial class ProfilePage : Page
{
    public MainViewModel VM => App.VM;
    public ProfilePage() { InitializeComponent(); }
    private void Avatar_Click(object sender, RoutedEventArgs e) => _ = P0Dialogs.ShowMyProfileAsync(XamlRoot);
    private void Qr_Click(object sender, RoutedEventArgs e) => _ = UxDialogs.MyQrAsync(XamlRoot);
}
