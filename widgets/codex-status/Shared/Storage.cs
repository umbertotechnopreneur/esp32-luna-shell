using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace OpenAIUsage;

public sealed class Storage
{
    public string DirectoryPath { get; }
    public string SettingsPath => Path.Combine(DirectoryPath, "settings.json");
    public Storage(string? directory = null)
    {
        DirectoryPath = directory ?? (OperatingSystem.IsWindows()
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "OpenAIUsage")
            : Path.Combine(Environment.GetEnvironmentVariable("XDG_STATE_HOME") ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".local", "state"), "openai-usage"));
        Directory.CreateDirectory(DirectoryPath);
        if (!OperatingSystem.IsWindows()) File.SetUnixFileMode(DirectoryPath, UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);
    }

    public WidgetSettings LoadSettings()
    {
        if (!File.Exists(SettingsPath)) return new();
        var settings = JsonSerializer.Deserialize<WidgetSettings>(File.ReadAllText(SettingsPath), JsonOptions.Default) ?? new();
        settings.Validate();
        return settings;
    }

    public void SaveSettings(WidgetSettings settings)
    {
        settings.Validate();
        AtomicWrite(SettingsPath, settings);
    }

    public List<Sample> Record(string identity, QuotaWindow window, string plan, long now)
    {
        var key = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(identity + "|" + window.Key + "|" + plan)))[..32];
        string path = Path.Combine(DirectoryPath, key + ".history.json");
        using var locked = new FileStream(Path.Combine(DirectoryPath, key + ".lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
        List<Sample> history = File.Exists(path)
            ? JsonSerializer.Deserialize<List<Sample>>(File.ReadAllText(path), JsonOptions.Default) ?? [] : [];
        history.RemoveAll(s => s.At < now - 16 * 86400 || s.At > now);
        history.RemoveAll(s => s.At == now);
        history.Add(new(now, window.Used, window.ResetsAt, plan));
        AtomicWrite(path, history);
        return history;
    }

    private static void AtomicWrite<T>(string path, T data)
    {
        string temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            File.WriteAllText(temporary, JsonSerializer.Serialize(data, JsonOptions.Default));
            if (!OperatingSystem.IsWindows()) File.SetUnixFileMode(temporary, UnixFileMode.UserRead | UnixFileMode.UserWrite);
            File.Move(temporary, path, true);
        }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }
}
