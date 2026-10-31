using LanMessenger.Core.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace LanMessenger.Views;

public sealed partial class SettingsPage : Page
{
    public MainViewModel VM => App.VM;

    private bool _loadingTheme;

    public SettingsPage()
    {
        InitializeComponent();
        Loaded += (_, __) => {
            if (VM.RefreshStorageCommand.CanExecute(null)) _ = VM.RefreshStorageCommand.ExecuteAsync(null);
            if (NotifyStatusText != null)
                NotifyStatusText.Text = Services.Toasts.Available
                    ? "系统通知：可用（Windows App SDK）"
                    : "系统通知：不可用 — 将尝试托盘气泡；请检查系统通知权限 / 打包身份";
        };
        CloseToTraySwitch.IsOn = Services.UiPrefs.Current.CloseToTray;
        _loadingTheme = true;
        ThemeChoice.SelectedIndex = (int)LanMessenger.Services.ThemeService.Mode;
        _loadingTheme = false;
    }

    private void ThemeChoice_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_loadingTheme || ThemeChoice.SelectedIndex < 0) return;
        var mode = (LanMessenger.Services.AppThemeMode)ThemeChoice.SelectedIndex;
        if (mode == LanMessenger.Services.ThemeService.Mode) return;
        LanMessenger.Services.ThemeService.SetMode(mode);
    }

    private async Task<string?> AskPasswordAsync(string title, bool confirm)
    {
        var pw = new PasswordBox { Header = "密码（至少 8 位）" };
        var pw2 = new PasswordBox { Header = "再次输入密码" };
        var panel = new StackPanel { Spacing = 8, MinWidth = 320 };
        panel.Children.Add(pw);
        if (confirm) panel.Children.Add(pw2);
        var dlg = new ContentDialog
        {
            XamlRoot = XamlRoot,
            Title = title,
            Content = panel,
            PrimaryButtonText = "确定",
            CloseButtonText = "取消",
            DefaultButton = ContentDialogButton.Primary,
        };
        if (await LanMessenger.Services.ThemeService.Prepare(dlg).ShowAsync() != ContentDialogResult.Primary) return null;
        if (pw.Password.Length < 8) { VM.ShowStatus("密码至少 8 位", true); return null; }
        if (confirm && pw.Password != pw2.Password) { VM.ShowStatus("两次输入的密码不一致", true); return null; }
        return pw.Password;
    }

    private async void Backup_Click(object sender, RoutedEventArgs e)
    {
        var path = await App.Platform.PickSaveFileAsync($"lan-messenger-{DateTime.Now:yyyyMMdd-HHmm}.lsmbak");
        if (path == null) return;
        var pw = await AskPasswordAsync("设置备份密码", true);
        if (pw == null) return;
        await VM.BackupAsync(path, pw);
    }

    private async void Restore_Click(object sender, RoutedEventArgs e)
    {
        var path = await App.Platform.PickOpenFileAsync();
        if (path == null) return;
        var confirm = new ContentDialog
        {
            XamlRoot = XamlRoot,
            Title = "从备份恢复？",
            Content = "当前的身份、联系人、群组和聊天记录将被备份中的内容替换。",
            PrimaryButtonText = "继续",
            CloseButtonText = "取消",
            DefaultButton = ContentDialogButton.Close,
        };
        if (await LanMessenger.Services.ThemeService.Prepare(confirm).ShowAsync() != ContentDialogResult.Primary) return;
        var pw = await AskPasswordAsync("输入备份密码", false);
        if (pw == null) return;
        await VM.RestoreAsync(path, pw);
    }

    /// <summary>Enter in a relay field saves (and connects); Esc leaves the field without saving.</summary>
    private void Relay_KeyDown(object sender, Microsoft.UI.Xaml.Input.KeyRoutedEventArgs e)
    {
        if (e.Key == Windows.System.VirtualKey.Enter)
        {
            e.Handled = true;
            if (sender is PasswordBox pb) VM.EditRelayPassphrase = pb.Password;
            if (VM.SaveRelayCommand.CanExecute(null)) VM.SaveRelayCommand.Execute(null);
        }
        else if (e.Key == Windows.System.VirtualKey.Escape)
        {
            e.Handled = true;
            VM.EditRelayPassphrase = "";
            RelaySaveButton.Focus(FocusState.Keyboard);
        }
    }

    /// <summary>For the UI smoke: bring the relay card into view.</summary>
    public void ShowRelayCardForSmoke() => RelayCard.StartBringIntoView(new BringIntoViewOptions { AnimationDesired = false, VerticalAlignmentRatio = 0.0 });

    private void CloseToTray_Toggled(object sender, RoutedEventArgs e)
    {
        if (Services.UiPrefs.Current.CloseToTray == CloseToTraySwitch.IsOn) return;
        Services.UiPrefs.Current.CloseToTray = CloseToTraySwitch.IsOn; Services.UiPrefs.Current.Save();
    }
}
