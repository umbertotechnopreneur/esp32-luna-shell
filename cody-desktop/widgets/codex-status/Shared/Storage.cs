using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace OpenAIUsage;

public sealed class Storage
{
    public string DirectoryPath { get; }
    public string SettingsPath => Path.Combine(DirectoryPath, "settings.json");
    public Exception? SettingsReadFailure { get; private set; }
    public Storage(string? directory = null)
    {
        DirectoryPath = directory ?? (OperatingSystem.IsWindows()
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "CodyCompanion")
            : Path.Combine(Environment.GetEnvironmentVariable("XDG_STATE_HOME") ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".local", "state"), "cody-companion"));
        Directory.CreateDirectory(DirectoryPath);
        if (!OperatingSystem.IsWindows()) File.SetUnixFileMode(DirectoryPath, UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);
    }

    // No parameters: load the current configuration; defaults represent only an absent file.
    // Exception: Retains and propagates read, JSON and validation failures without repairing the file.
    public WidgetSettings LoadSettings()
    {
        try
        {
            RejectSettingsLink();
            WidgetSettings settings;
            try
            {
                settings = JsonSerializer.Deserialize<WidgetSettings>(File.ReadAllText(SettingsPath), JsonOptions.Default)
                    ?? throw new InvalidDataException("Widget settings must be a JSON object, not null.");
            }
            catch (FileNotFoundException)
            {
                // A link with an absent target is an invalid existing entry, not first use.
                RejectSettingsLink();
                if (!Directory.Exists(DirectoryPath))
                    throw new DirectoryNotFoundException("The widget settings directory is unavailable.");
                settings = new();
            }
            settings.Validate();
            SettingsReadFailure = null;
            return settings;
        }
        catch (Exception error) when (error is IOException or InvalidDataException or UnauthorizedAccessException or JsonException or
            ArgumentException or TimeZoneNotFoundException or InvalidTimeZoneException)
        {
            SettingsReadFailure = error;
            throw;
        }
    }

    // No parameters: inspect the settings directory entry without resolving a symbolic-link target.
    // Exception: Rejects links, including dangling links, before any read or replacement.
    private void RejectSettingsLink()
    {
        if (new FileInfo(SettingsPath).LinkTarget is not null)
            throw new IOException("Widget settings must be a regular file, not a symbolic link.");
    }

    // settings: Complete user-selected configuration; no invalid value is repaired implicitly.
    // Exception: Refuses writes after a failed read and propagates validation or atomic-write failures.
    public void SaveSettings(WidgetSettings settings)
    {
        if (SettingsReadFailure is { } failure)
            throw new IOException("Settings could not be loaded. Restore settings.json and reload before saving changes.", failure);
        // Short-lived backend callers must also validate the existing file before replacing it.
        _ = LoadSettings();
        ArgumentNullException.ThrowIfNull(settings);
        settings.Validate();
        RejectSettingsLink();
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
