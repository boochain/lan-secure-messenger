namespace LanMessenger.Services;

/// <summary>UI-side crash / error log next to the daemon logs.</summary>
public static class CrashLog
{
    public static string Dir
    {
        get
        {
            var d = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "FREC86", "lan-secure-messenger", "logs");
            try { Directory.CreateDirectory(d); } catch { }
            return d;
        }
    }

    public static void Write(string what, Exception? e)
    {
        try
        {
            File.AppendAllText(Path.Combine(Dir, "ui.log"), $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] {what}: {e}\r\n");
        }
        catch { }
    }
}
