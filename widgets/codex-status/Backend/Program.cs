using OpenAIUsage;
using System.Text.Json;

// One bounded JSON request per invocation; stdout contains only the public widget schema.
try
{
    var storage = new Storage();
    string command = args.FirstOrDefault() ?? "snapshot";
    if (command == "settings")
        Console.WriteLine(JsonSerializer.Serialize(storage.LoadSettings(), JsonOptions.Compact));
    else if (command == "save-settings")
    {
        var value = JsonSerializer.Deserialize<WidgetSettings>(await Console.In.ReadToEndAsync(), JsonOptions.Default)
            ?? throw new ArgumentException("Settings are required.");
        storage.SaveSettings(value);
        Console.WriteLine("{\"ok\":true}");
    }
    else if (command == "snapshot")
    {
        var state = await new UsageService(storage).RefreshAsync(storage.LoadSettings());
        Console.WriteLine(JsonSerializer.Serialize(state, JsonOptions.Compact));
        if (!state.Connected) Environment.ExitCode = 2;
    }
    else throw new ArgumentException("Commands: snapshot, settings, save-settings");
}
catch (Exception ex) when (ex is IOException or ArgumentException or JsonException or UnauthorizedAccessException or TimeZoneNotFoundException)
{
    Console.WriteLine(JsonSerializer.Serialize(new { error = ex.Message }, JsonOptions.Compact));
    Environment.ExitCode = 1;
}
