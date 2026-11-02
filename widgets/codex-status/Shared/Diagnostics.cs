using System.Text.Json;

namespace OpenAIUsage;

public static class Diagnostics
{
    private static readonly object Sync = new();
    public static string DirectoryPath => Path.Combine(new Storage().DirectoryPath, "logs");
    public static void Record(string kind, string message, long? bytes = null)
    {
        try
        {
            lock (Sync)
            {
                Directory.CreateDirectory(DirectoryPath);
                var path = Path.Combine(DirectoryPath, "app.jsonl");
                if (File.Exists(path) && new FileInfo(path).Length > 512 * 1024) File.Move(path, path + ".1", true);
                var line = JsonSerializer.Serialize(new { atUtc = DateTimeOffset.UtcNow, kind,
                    message = message.Length > 600 ? message[..600] : message, bytes }, JsonOptions.Compact);
                File.AppendAllText(path, line + Environment.NewLine);
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
    }
}
