using System;
using System.IO;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using LanMessenger.ViewModels;

namespace LanMessenger.Views;

public sealed partial class RecoveryPage : Page
{
    public MainViewModel VM => App.VM;
    public static bool ForceConfirm { get; set; }
    string? _key;

    public RecoveryPage()
    {
        InitializeComponent();
        Loaded += OnLoaded;
    }

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        _key = await VM.RevealRecoveryKeyAsync();
        if (string.IsNullOrEmpty(_key) && !string.IsNullOrEmpty(Environment.GetEnvironmentVariable("LSM_UI_SMOKE")))
            _key = "RK1-TEST-XXXX-XXXX-XXXX";
        var ok = !string.IsNullOrEmpty(_key);
        KeyText.Text = ok ? _key! : "还没有恢复密钥（请先开启账号模式）";
        CopyBtn.IsEnabled = ExportBtn.IsEnabled = ok;
        AckBox.Visibility = (ForceConfirm || ok) ? Visibility.Visible : Visibility.Collapsed;
        ConfirmBtn.Content = ForceConfirm ? "我已抄好" : "完成";
        UpdateButtons();
    }

    private void UpdateButtons()
    {
        var acked = AckBox.IsChecked == true;
        ConfirmBtn.IsEnabled = !ForceConfirm || acked;
        BackBtn.IsEnabled = !ForceConfirm || acked;
    }

    private void Ack_Click(object sender, RoutedEventArgs e) => UpdateButtons();

    private void Copy_Click(object sender, RoutedEventArgs e)
    {
        if (string.IsNullOrEmpty(_key)) return;
        App.Platform.CopyToClipboard(_key);
        VM.ShowStatus("已复制恢复密钥");
    }

    private async void Export_Click(object sender, RoutedEventArgs e)
    {
        if (string.IsNullOrEmpty(_key)) return;
        var path = await App.Platform.PickSaveFileAsync("recovery-key.txt");
        if (string.IsNullOrEmpty(path)) return;
        try
        {
            await File.WriteAllTextAsync(path, _key);
            VM.ShowStatus("已导出恢复密钥");
        }
        catch (Exception ex) { VM.ShowStatus("导出失败：" + ex.Message, true); }
    }

    private void Leave()
    {
        ForceConfirm = false;
        App.MainWin?.NavigateTo("settings");
    }

    private void Done_Click(object sender, RoutedEventArgs e)
    {
        if (ForceConfirm && AckBox.IsChecked != true) { VM.ShowStatus("请先确认已抄下恢复密钥", true); return; }
        Leave();
    }

    private void Back_Click(object sender, RoutedEventArgs e)
    {
        if (ForceConfirm && AckBox.IsChecked != true) { VM.ShowStatus("请先确认已抄下恢复密钥", true); return; }
        Leave();
    }
}
