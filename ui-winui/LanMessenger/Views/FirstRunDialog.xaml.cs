using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace LanMessenger.Views;

public sealed partial class FirstRunDialog : ContentDialog
{
    public FirstRunDialog()
    {
        InitializeComponent();
        NameBox.Text = string.IsNullOrWhiteSpace(App.VM.MyName) ? Environment.UserName : App.VM.MyName;
        FpText.Text = App.VM.MyFingerprint;
        Loaded += async (_, _) =>
        {
            await App.VM.RefreshDiagnosticsAsync(false);
            UpdateFirewall();
        };
    }

    private void UpdateFirewall()
    {
        // Status only. Long advice already lives in the pinned InfoBar; duplicating it
        // here was clipping the fingerprint row mid-glyph at the scroll viewport bottom.
        FwResult.Text = App.VM.FirewallSummary ?? "";
        if (App.VM.FirewallOk)
        {
            FwInfo.Severity = InfoBarSeverity.Success;
            FwInfo.Message = "防火墙规则已就绪。";
        }
        if (App.VM.PublicNetwork)
        {
            FwInfo.Severity = InfoBarSeverity.Warning;
            FwInfo.Message = "当前网络被 Windows 标记为「公用网络」，默认会阻止局域网通讯。建议在 Windows 设置 → 网络 中改为「专用网络」，或在「诊断」页勾选允许公用网络后重新配置。";
        }
    }

    private async void Firewall_Click(object sender, RoutedEventArgs e)
    {
        FwButton.IsEnabled = false;
        FwRing.IsActive = true;
        try
        {
            await App.VM.FirewallSetupCommand.ExecuteAsync(null);
            UpdateFirewall();
        }
        finally
        {
            FwRing.IsActive = false;
            FwButton.IsEnabled = true;
        }
    }

    private void CopyFp_Click(object sender, RoutedEventArgs e) => App.Platform.CopyToClipboard(FpText.Text);

    private async void Dialog_PrimaryButtonClick(ContentDialog sender, ContentDialogButtonClickEventArgs args)
    {
        var d = args.GetDeferral();
        var mode = AccountModeRadio.IsChecked == true ? "account" : "device";
        try
        {
            await App.VM.CompleteFirstRunAsync(NameBox.Text.Trim(), mode);
            if (mode == "account")
            {
                var rk = await App.VM.RevealRecoveryKeyAsync();
                if (!string.IsNullOrEmpty(rk)) App.VM.ShowStatus("请抄下恢复密钥：" + rk);
            }
        }
        finally { d.Complete(); }
    }
}
