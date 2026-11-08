using System.Diagnostics;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace OpenAIUsage;

public sealed class CodyBridgeSettings
{
    public string PythonExecutable { get; set; } = OperatingSystem.IsWindows() ? "python" : "python3";
    public string ControlScriptPath { get; set; } = "";

    // No parameters: validate saved configuration without starting a process.
    // Exception: Throws ArgumentException for malformed configuration.
    public void Validate()
    {
        if (string.IsNullOrWhiteSpace(PythonExecutable) || PythonExecutable.Length > 4096 || PythonExecutable.Contains('\0'))
            throw new ArgumentException("Choose a Python executable for the Cody companion service.");
        if (ControlScriptPath is null || ControlScriptPath.Length > 4096 || ControlScriptPath.Contains('\0'))
            throw new ArgumentException("The Cody service launcher path is invalid.");
        if (ControlScriptPath.Length > 0 && !Path.IsPathFullyQualified(ControlScriptPath))
            throw new ArgumentException("Choose the full path to the Cody service launcher.");
    }
}

[JsonConverter(typeof(JsonStringEnumConverter<CodySourceProvider>))]
public enum CodySourceProvider { Unavailable, Local, Codex }

[JsonConverter(typeof(JsonStringEnumConverter<CodySourceState>))]
public enum CodySourceState { Idle, Working, Attention, Unavailable }

public sealed record CodySourceStatus(CodySourceProvider Provider, string Identity, CodySourceState State,
    long? AgeMilliseconds, string Label, string Detail);
public sealed record CodyDeviceStatus(string DeviceId, bool Connected, bool Simulated, CodySourceStatus ProviderSource);
public sealed record CodyBridgeStatus(bool Running, string Version, bool Persistent, CodySourceStatus Source,
    IReadOnlyList<CodyDeviceStatus> Devices);

public static class CodyBridgeClient
{
    private const int MaximumResponseCharacters = 65536;

    // settings: User-selected interpreter and public CCP control entrypoint.
    // operation: One documented control operation, never a shell command.
    // arguments: Bounded operation payload; secrets remain on redirected stdin.
    // cancellationToken: Caller lifetime; cancelling a write does not prove it was unapplied.
    // Exception: Throws the original process, transport or protocol failure; mutations are never retried.
    public static async Task<JsonElement> CallAsync(CodyBridgeSettings settings, string operation,
        object arguments, CancellationToken cancellationToken = default)
    {
        settings.Validate();
        if (string.IsNullOrWhiteSpace(settings.ControlScriptPath) || !File.Exists(settings.ControlScriptPath))
            throw new IOException("Choose the installed Cody service launcher before connecting.");
        if (operation is not ("bridge/status" or "bridge/start" or "bridge/stop" or
            "device/discover" or "device/pair" or "device/read-name" or "device/set-name" or
            "device/read-clock" or "device/configure-clock" or "device/normal"))
            throw new ArgumentException("Unsupported Cody control operation.");
        var request = JsonSerializer.Serialize(arguments, JsonOptions.Compact);
        if (Encoding.UTF8.GetByteCount(request) > 16384)
            throw new ArgumentException("The Cody control request exceeds its size limit.");
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        var start = new ProcessStartInfo(settings.PythonExecutable)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8,
            WorkingDirectory = Path.GetDirectoryName(settings.ControlScriptPath)!
        };
        start.ArgumentList.Add("-B");
        start.ArgumentList.Add(settings.ControlScriptPath);
        start.ArgumentList.Add("--control");
        start.ArgumentList.Add(operation);
        using var process = Process.Start(start) ?? throw new IOException("Could not start the Cody service control client.");
        var outputTask = ReadBoundedAsync(process.StandardOutput, timeout.Token);
        var errorTask = DrainAsync(process.StandardError, timeout.Token);
        try
        {
            await process.StandardInput.WriteAsync(request.AsMemory(), timeout.Token);
            process.StandardInput.Close();
            var output = await outputTask;
            await process.WaitForExitAsync(timeout.Token);
            await errorTask;
            using var document = JsonDocument.Parse(output);
            var envelope = document.RootElement;
            if (envelope.TryGetProperty("error", out var error))
            {
                var message = error.TryGetProperty("message", out var detail) && detail.ValueKind == JsonValueKind.String
                    ? detail.GetString() : "The Cody service rejected the request.";
                throw new IOException(message);
            }
            if (process.ExitCode != 0 || !envelope.TryGetProperty("result", out var result))
                throw new IOException("The Cody service returned an invalid control response.");
            if (result.ValueKind == JsonValueKind.Object && result.TryGetProperty("isError", out var rejected) && rejected.GetBoolean())
            {
                if (result.TryGetProperty("result", out var native) && native.ValueKind == JsonValueKind.Object &&
                    native.TryGetProperty("status", out _)) return native.Clone();
                throw new IOException("The Cody device rejected the request without a usable acknowledgement.");
            }
            return result.Clone();
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            throw new IOException("Cody service control timed out. Refresh status before repeating a change; its result is unconfirmed.");
        }
        finally
        {
            // Only the short-lived control client belongs to this invocation. The CCP owner
            // has an independent lifecycle; never kill its process tree after an ambiguous write.
            timeout.Cancel();
            try { if (!process.HasExited) process.Kill(); }
            catch (InvalidOperationException) { }
            catch (System.ComponentModel.Win32Exception error)
            {
                Diagnostics.Record("cody.control.cleanup", "Could not stop the short-lived control client; native error " + error.NativeErrorCode + ".");
            }
            await ObserveCompletionAsync(outputTask);
            await ObserveCompletionAsync(errorTask);
        }
    }

    // settings: Saved public control launcher configuration, independent from account sign-in.
    // cancellationToken: Cancels this read only; status never starts the companion owner.
    // Exception: Propagates process, JSON or strict schema failures instead of inferring connectivity.
    public static async Task<CodyBridgeStatus> ReadStatusAsync(CodyBridgeSettings settings,
        CancellationToken cancellationToken = default)
    {
        var result = await CallAsync(settings, "bridge/status", new { }, cancellationToken);
        return ParseStatus(result);
    }

    // result: Current owner response; its source age is never replaced by account polling time.
    // Exception: Throws InvalidDataException or JSON errors for unknown or malformed identities.
    public static CodyBridgeStatus ParseStatus(JsonElement result)
    {
        var bridge = result.GetProperty("bridge");
        var source = ParseSource(result.GetProperty("source"));
        var devices = new List<CodyDeviceStatus>();
        var identities = new HashSet<string>(StringComparer.Ordinal);
        foreach (var device in result.GetProperty("devices").EnumerateArray())
        {
            var identity = RequiredText(device, "device_id", 12);
            if (devices.Count >= 64 || identity.Length != 12 ||
                !identity.All(character => character is >= '0' and <= '9' or >= 'a' and <= 'f') || !identities.Add(identity))
                throw new InvalidDataException("Cody reported an invalid or duplicate device identity.");
            devices.Add(new(identity, device.GetProperty("connected").GetBoolean(), device.GetProperty("simulated").GetBoolean(),
                ParseSource(device.GetProperty("provider_source"))));
        }
        return new(bridge.GetProperty("running").GetBoolean(), RequiredText(bridge, "version", 64),
            bridge.GetProperty("persistent").GetBoolean(), source, devices);
    }

    // source: One owner projection, optionally bound to an exact physical device identity.
    // Exception: Rejects unsupported providers/states or invalid source ages without inferring activity.
    private static CodySourceStatus ParseSource(JsonElement source)
    {
        var provider = source.GetProperty("provider").GetString() switch
        {
            "local" => CodySourceProvider.Local,
            "codex" => CodySourceProvider.Codex,
            "unavailable" => CodySourceProvider.Unavailable,
            _ => throw new InvalidDataException("Cody reported an unknown source provider.")
        };
        var state = source.GetProperty("state").GetString() switch
        {
            "idle" => CodySourceState.Idle,
            "working" => CodySourceState.Working,
            "attention" => CodySourceState.Attention,
            "unavailable" => CodySourceState.Unavailable,
            _ => throw new InvalidDataException("Cody reported an unknown source state.")
        };
        long? age = source.TryGetProperty("age_ms", out var ageValue) && ageValue.ValueKind != JsonValueKind.Null
            ? ageValue.GetInt64() : null;
        if (age is < 0 or > uint.MaxValue) throw new InvalidDataException("Cody reported an invalid source age.");
        return new(provider, RequiredText(source, "identity", 255), state, age,
            RequiredText(source, "label", 31), RequiredText(source, "detail", 95));
    }

    // value: One schema object returned by the public service.
    // key: Required string property name.
    // maximumLength: Maximum admitted UTF-16 length for this bounded display value.
    // Exception: Throws InvalidDataException for missing, oversized or control-bearing text.
    private static string RequiredText(JsonElement value, string key, int maximumLength)
    {
        var text = value.GetProperty(key).GetString();
        if (text is null || text.Length > maximumLength || text.Any(char.IsControl))
            throw new InvalidDataException("Cody returned an invalid " + key + " value.");
        return text;
    }

    // reader: Redirected stdout for one bounded control response.
    // cancellationToken: Stops an unfinished helper read.
    // Exception: Throws InvalidDataException when the response exceeds its hard bound.
    private static async Task<string> ReadBoundedAsync(StreamReader reader, CancellationToken cancellationToken)
    {
        var output = new StringBuilder();
        var buffer = new char[2048];
        int read;
        while ((read = await reader.ReadAsync(buffer.AsMemory(), cancellationToken)) > 0)
        {
            if (output.Length + read > MaximumResponseCharacters)
                throw new InvalidDataException("The Cody control response exceeds its size limit.");
            output.Append(buffer, 0, read);
        }
        return output.ToString();
    }

    // reader: Helper diagnostics consumed without persisting or echoing private payloads.
    // cancellationToken: Stops the drain when its owning invocation ends.
    // Exception: Propagates redirected-stream failures to the owning call.
    private static async Task DrainAsync(StreamReader reader, CancellationToken cancellationToken)
    {
        var buffer = new char[2048];
        while (await reader.ReadAsync(buffer.AsMemory(), cancellationToken) > 0) { }
    }

    // operation: A redirected-stream task already accounted for by its owning call.
    // No exceptions: Observe cancellation/stream failures while preserving the primary result.
    private static async Task ObserveCompletionAsync(Task operation)
    {
        try { await operation; }
        catch (Exception error) when (error is OperationCanceledException or IOException or ObjectDisposedException) { }
    }
}
