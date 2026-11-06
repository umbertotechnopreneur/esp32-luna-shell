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
    else if (command == "cody-status")
    {
        var status = await CodyBridgeClient.ReadStatusAsync(storage.LoadSettings().CodyBridge);
        Console.WriteLine(JsonSerializer.Serialize(status, JsonOptions.Compact));
    }
    else if (command == "cody-control")
    {
        // The short-lived GTK backend delegates every device operation to the one CCP owner.
        // It never opens a device socket or borrows account-read success as source evidence.
        var input = await ReadCodyRequestAsync();
        using var request = JsonDocument.Parse(input);
        var operation = request.RootElement.GetProperty("operation").GetString()
            ?? throw new ArgumentException("A Cody control operation is required.");
        var arguments = request.RootElement.GetProperty("arguments");
        var result = await CodyBridgeClient.CallAsync(storage.LoadSettings().CodyBridge, operation, arguments);
        Console.WriteLine(result.GetRawText());
    }
    else throw new ArgumentException("Commands: snapshot, settings, save-settings, cody-status, cody-control");
}
catch (Exception ex) when (ex is IOException or ArgumentException or JsonException or UnauthorizedAccessException or
    TimeZoneNotFoundException or KeyNotFoundException or InvalidOperationException or FormatException or OverflowException or System.ComponentModel.Win32Exception)
{
    Console.WriteLine(JsonSerializer.Serialize(new { error = ex.Message }, JsonOptions.Compact));
    Environment.ExitCode = 1;
}

// No parameters: read one bounded control request from redirected stdin.
// Exception: Throws ArgumentException before retaining an oversized request.
static async Task<string> ReadCodyRequestAsync()
{
    var buffer = new char[16385];
    int length = 0;
    while (length < buffer.Length)
    {
        int received = await Console.In.ReadAsync(buffer.AsMemory(length));
        if (received == 0) return new string(buffer, 0, length);
        length += received;
    }
    throw new ArgumentException("The Cody control request is too large.");
}
