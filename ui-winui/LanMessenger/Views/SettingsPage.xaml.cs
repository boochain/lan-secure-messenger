using System.Linq;
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
        switch (LanMessenger.Services.ThemeService.Mode)
        {
            case LanMessenger.Services.AppThemeMode.Light: ThemeLightRadio.IsChecked = true; break;
            case LanMessenger.Services.AppThemeMode.Dark: ThemeDarkRadio.IsChecked = true; break;
            default: ThemeSystemRadio.IsChecked = true; break;
        }
        if (VM.TransportMode == "server_lan") TransportBothRadio.IsChecked = true;
        else TransportLanRadio.IsChecked = true;
        _loadingTheme = false;
    }

    private void ThemeRadio_Checked(object sender, RoutedEventArgs e)
    {
        if (_loadingTheme || sender is not RadioButton rb || rb.IsChecked != true) return;
        if (rb.Tag is not string s || !int.TryParse(s, out var i)) return;
        var mode = (LanMessenger.Services.AppThemeMode)i;
        if (mode == LanMessenger.Services.ThemeService.Mode) return;
        LanMessenger.Services.ThemeService.SetMode(mode);
    }

    private void TransportRadio_Checked(object sender, RoutedEventArgs e)
    {
        if (_loadingTheme || sender is not RadioButton rb || rb.IsChecked != true) return;
        if (rb.Tag is not string mode || mode.Length == 0) return;
        if (VM.TransportMode == mode) return;
        VM.TransportMode = mode;
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

    private async void AccountModeOn_Click(object sender, RoutedEventArgs e)
    {
        await VM.SetAccountModeAsync("account");
        RecoveryPage.ForceConfirm = true;
        App.MainWin?.NavigateTo("recovery");
    }
    private async void AccountModeOff_Click(object sender, RoutedEventArgs e) => await VM.SetAccountModeAsync("device");
    private void RevealRk_Click(object sender, RoutedEventArgs e)
    {
        RecoveryPage.ForceConfirm = false;
        App.MainWin?.NavigateTo("recovery");
    }
    private async void RestoreAccount_Click(object sender, RoutedEventArgs e) => await VM.RestoreAccountAsync(RestoreKeyBox.Text);
    private async void ApplyUri_Click(object sender, RoutedEventArgs e) => await VM.AddByLinkAsync(PasteUriBox.Text);
    private async void ScanImage_Click(object sender, RoutedEventArgs e)
    {
        var files = await App.Platform.PickFilesAsync();
        var path = files?.FirstOrDefault();
        if (string.IsNullOrEmpty(path)) return;
        try
        {
            var text = await System.IO.File.ReadAllTextAsync(path);
            var uri = UxDialogs.TakeLsmUri(text);
            if (!string.IsNullOrEmpty(uri))
            {
                await VM.AddByLinkAsync(uri);
                return;
            }
        }
        catch { /* not a text file */ }
        try
        {
            var args = "-q --raw \"" + path + "\"";
            var psi = new System.Diagnostics.ProcessStartInfo("zbarimg", args)
            {
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true
            };
            using var proc = System.Diagnostics.Process.Start(psi);
            var output = proc == null ? "" : await proc.StandardOutput.ReadToEndAsync();
            if (proc != null) await proc.WaitForExitAsync();
            var line = (output ?? "").Replace('\r', '\n').Split('\n')
                .FirstOrDefault(s => s.StartsWith("lsm://", StringComparison.Ordinal));
            if (!string.IsNullOrEmpty(line)) { await VM.AddByLinkAsync(line.Trim()); return; }
        }
        catch { /* zbarimg not installed */ }
        VM.ShowStatus("无法从该文件识别二维码，请将 lsm:// 链接粘贴到上方。桌面扫码以粘贴链接为主；安装 zbar 后可从图片识别。");
    }
    const string TestRelayUri = "lsm://relay?h=ci.example.invalid&p=9443";
    const string TestPairUri = "lsm://pair?r=ci.example.invalid:9443&t=0000000000000000000000000000000000000000000000000000000000000000&k=1111111111111111111111111111111111111111111111111111111111111111";
    static bool Smoke => Environment.GetEnvironmentVariable("LSM_UI_SMOKE") is { Length: > 0 };
    private async void ServerQr_Click(object sender, RoutedEventArgs e)
    {
        var uri = await VM.RelayUriAsync();
        if (string.IsNullOrEmpty(uri)) { if (Smoke) uri = TestRelayUri; else { VM.ShowStatus("请先填写中转服务器"); return; } }
        await UxDialogs.ShowUriQrAsync(XamlRoot, "服务器二维码", uri);
    }
    private async void PairQr_Click(object sender, RoutedEventArgs e)
    {
        var uri = await VM.AccountPairStartAsync();
        if (string.IsNullOrEmpty(uri) || !uri.StartsWith("lsm://")) { if (Smoke) uri = TestPairUri; else { VM.ShowStatus(string.IsNullOrEmpty(uri) ? "无法生成配对码" : uri); return; } }
        await UxDialogs.ShowUriQrAsync(XamlRoot, "配对新设备", uri);
    }
    private void Profile_Click(object sender, RoutedEventArgs e) => _ = P0Dialogs.ShowMyProfileAsync(XamlRoot);
}
