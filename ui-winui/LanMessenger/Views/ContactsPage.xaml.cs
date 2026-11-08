using LanMessenger.Core.ViewModels;
using Microsoft.UI.Xaml.Controls;

namespace LanMessenger.Views;

public sealed partial class ContactsPage : Page
{
    public MainViewModel VM => App.VM;
    public ContactsPage() { InitializeComponent(); }

    private void Peer_Click(object sender, ItemClickEventArgs e)
    {
        if (e.ClickedItem is not PeerItem p) return;
        App.MainWin?.OpenConversation("dm:" + p.Fingerprint);
    }
}
