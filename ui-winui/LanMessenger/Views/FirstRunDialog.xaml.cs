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
        FwResult.Text = App.VM.FirewallSummary + (string.IsNullOrEmpty(App.VM.FirewallAdvice) ? "" : "\n" + App.VM.FirewallAdvice);
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
        try { await App.VM.CompleteFirstRunAsync(NameBox.Text.Trim()); }
        finally { d.Complete(); }
    }
}
