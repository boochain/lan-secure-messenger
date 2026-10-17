using System.Text.Json;

namespace LanMessenger.Services;

/// <summary>Per-user window preferences (config/window.json): window placement, close-to-tray, first-time tray notice.</summary>
public sealed class UiPrefs
{
    public int X { get; set; } = int.MinValue;
    public int Y { get; set; } = int.MinValue;
    public int W { get; set; }
    public int H { get; set; }
    public bool Maximized { get; set; }
    public bool CloseToTray { get; set; } = true;
    public bool TrayNoticeShown { get; set; }

    public static UiPrefs Current { get; } = Load();

    private static string FilePath
    {
        get
        {
            var baseDir = Environment.GetEnvironmentVariable("LSM_DATA_DIR");
            var d = string.IsNullOrWhiteSpace(baseDir)
                ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "FREC86", "lan-secure-messenger", "config")
                : Path.Combine(baseDir, "config");
            try { Directory.CreateDirectory(d); } catch { }
            return Path.Combine(d, "window.json");
        }
    }

    private static UiPrefs Load()
    {
        try { if (File.Exists(FilePath)) return JsonSerializer.Deserialize<UiPrefs>(File.ReadAllText(FilePath)) ?? new UiPrefs(); }
        catch (Exception e) { CrashLog.Write("uiprefs load", e); }
        return new UiPrefs();
    }

    public void Save()
    {
        try { var tmp = FilePath + ".tmp"; File.WriteAllText(tmp, JsonSerializer.Serialize(this)); File.Move(tmp, FilePath, true); }
        catch (Exception e) { CrashLog.Write("uiprefs save", e); }
    }
}
