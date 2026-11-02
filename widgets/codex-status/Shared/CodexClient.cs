using System.Diagnostics;
using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace OpenAIUsage;

// Only account reads are issued. This client never starts a turn, redeems credits or sends messages.
public sealed class CodexClient : IAsyncDisposable
{
    private readonly Process process;
    private readonly Task stderrDrain;
    private int requestId;

    public CodexClient(string executable)
    {
        var start = new ProcessStartInfo(CodexExecutable.Resolve(executable))
        {
            RedirectStandardInput = true, RedirectStandardOutput = true, RedirectStandardError = true,
            UseShellExecute = false, CreateNoWindow = true,
            StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8
        };
        start.ArgumentList.Add("app-server");
        start.ArgumentList.Add("--stdio");
        process = Process.Start(start) ?? throw new IOException("Could not start Codex.");
        stderrDrain = DrainAsync(process.StandardError);
    }

    private static async Task DrainAsync(StreamReader reader)
    {
        while (await reader.ReadLineAsync() is not null) { }
    }

    private async Task<JsonElement> CallAsync(string method, object? parameters, CancellationToken ct)
    {
        var id = ++requestId;
        await process.StandardInput.WriteLineAsync(JsonSerializer.Serialize(new { id, method, @params = parameters }));
        await process.StandardInput.FlushAsync(ct);
        while (true)
        {
            var line = await process.StandardOutput.ReadLineAsync(ct);
            if (line is null) throw new IOException("Codex closed its connection. Check the executable and sign-in.");
            using var doc = JsonDocument.Parse(line);
            var root = doc.RootElement;
            if (root.TryGetProperty("method", out _) && root.TryGetProperty("id", out var incoming))
            {
                await process.StandardInput.WriteLineAsync(JsonSerializer.Serialize(new
                {
                    id = incoming.Clone(), error = new { code = -32601, message = "Read-only usage client" }
                }));
                await process.StandardInput.FlushAsync(ct);
                continue;
            }
            if (!root.TryGetProperty("id", out var responseId) || !responseId.TryGetInt32(out var number) || number != id) continue;
            if (root.TryGetProperty("error", out var error))
            {
                var code = error.TryGetProperty("code", out var ec) ? ec.ToString() : "unknown";
                // Do not surface raw server diagnostics: they can contain private account context.
                throw new IOException($"Codex {method} failed ({code}). Check sign-in, network and CLI version.");
            }
            return root.GetProperty("result").Clone();
        }
    }

    public async Task<AccountUsage> ReadAsync(CancellationToken ct)
    {
        await CallAsync("initialize", new { clientInfo = new { name = "openai_usage_widget", version = "0.1.0" } }, ct);
        await process.StandardInput.WriteLineAsync("{\"method\":\"initialized\"}");
        await process.StandardInput.FlushAsync(ct);
        var account = await CallAsync("account/read", new { refreshToken = false }, ct);
        var a = account.GetProperty("account");
        if (a.ValueKind == JsonValueKind.Null) throw new IOException("Sign in with ChatGPT using 'codex login', then refresh.");
        if (Text(a, "type") != "chatgpt") throw new IOException("ChatGPT sign-in is required. API-key billing does not expose this allowance.");
        var limits = await CallAsync("account/rateLimits/read", null, ct);
        JsonElement? usage = null;
        string? tokenError = null;
        try { usage = await CallAsync("account/usage/read", null, ct); }
        catch (IOException) { tokenError = "Token history unavailable from this Codex version or account."; }
        var check = await CallAsync("account/read", new { refreshToken = false }, ct);
        if (check.GetProperty("account").GetRawText() != a.GetRawText())
            throw new IOException("Account changed while refreshing. Refresh again.");
        return Parse(a, limits, usage, tokenError);
    }

    public static AccountUsage Parse(JsonElement account, JsonElement limits, JsonElement? usage, string? tokenError = null)
    {
        string plan = Text(account, "planType") ?? "Unknown";
        string? identity = Text(limits, "accountId") ?? Text(account, "email");
        if (string.IsNullOrEmpty(identity)) throw new IOException("Account identity unavailable; history cannot be safely separated.");
        identity = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(identity)))[..24];
        var buckets = new List<(string, JsonElement)>();
        if (limits.TryGetProperty("rateLimitsByLimitId", out var map) && map.ValueKind == JsonValueKind.Object)
            buckets.AddRange(map.EnumerateObject().Select(p => (p.Name, p.Value)));
        if (buckets.Count == 0 && limits.TryGetProperty("rateLimits", out var single) && single.ValueKind == JsonValueKind.Object)
            buckets.Add((Text(single, "limitId") ?? "codex", single));
        var windows = new List<QuotaWindow>();
        decimal? credits = null;
        bool unlimited = false, blocked = false;
        foreach (var (key, bucket) in buckets.OrderBy(b => b.Item1 == "codex" ? 0 : 1))
        {
            foreach (var name in new[] { "primary", "secondary" })
            {
                if (!bucket.TryGetProperty(name, out var w) || w.ValueKind != JsonValueKind.Object) continue;
                var used = Number(w, "usedPercent");
                var minutes = Number(w, "windowDurationMins");
                if (used is null || minutes is null || minutes <= 0 || minutes > int.MaxValue || used < 0 || !double.IsFinite(used.Value)) continue;
                long? reset = w.TryGetProperty("resetsAt", out var r) && r.TryGetInt64(out var t) && t > 0 && t < 253402300799 ? t : null;
                string duration = minutes == 10080 ? "Weekly" : minutes == 300 ? "5-hour" : $"{minutes / 60:0.#}-hour";
                windows.Add(new($"{key}:{minutes.Value:0}", $"{duration} · {Text(bucket, "limitName") ?? key}",
                    Math.Clamp(used.Value, 0, 100), (int)minutes.Value, reset));
            }
            if (key == "codex" || buckets.Count == 1)
            {
                blocked = Flag(bucket, "spendControlReached") == true;
                if (bucket.TryGetProperty("credits", out var c) && c.ValueKind == JsonValueKind.Object)
                {
                    unlimited = Flag(c, "unlimited") == true;
                    if (c.TryGetProperty("balance", out var balance) && decimal.TryParse(balance.ToString(), NumberStyles.Float, CultureInfo.InvariantCulture, out var amount) && amount >= 0)
                        credits = amount;
                    else if (Flag(c, "hasCredits") == false) credits = 0;
                }
            }
        }
        var days = new List<TokenDay>();
        if (usage is { } u && u.TryGetProperty("dailyUsageBuckets", out var daily) && daily.ValueKind == JsonValueKind.Array)
        {
            foreach (var d in daily.EnumerateArray())
                if (DateOnly.TryParseExact(Text(d, "startDate"), "yyyy-MM-dd", out var date) &&
                    d.TryGetProperty("tokens", out var tokens) && tokens.TryGetInt64(out var n) && n >= 0)
                    days.Add(new(date.ToString("yyyy-MM-dd"), n));
        }
        // Conflicting duplicate dates are unknown, not double counted.
        days = days.GroupBy(d => d.Date).Select(g => new TokenDay(g.Key,
            g.Select(d => d.Tokens).Distinct().Count() == 1 ? g.First().Tokens : null)).OrderBy(d => d.Date).ToList();
        return new(identity, plan, windows, credits, unlimited, Flag(limits, "ordinaryUsageAllowed"), blocked, days, tokenError);
    }

    private static string? Text(JsonElement e, string name) => e.TryGetProperty(name, out var p) && p.ValueKind == JsonValueKind.String ? p.GetString() : null;
    private static double? Number(JsonElement e, string name) => e.TryGetProperty(name, out var p) && p.ValueKind == JsonValueKind.Number && p.TryGetDouble(out var n) ? n : null;
    private static bool? Flag(JsonElement e, string name) => e.TryGetProperty(name, out var p) && p.ValueKind is JsonValueKind.True or JsonValueKind.False ? p.GetBoolean() : null;

    public async ValueTask DisposeAsync()
    {
        try { if (!process.HasExited) process.Kill(entireProcessTree: true); }
        catch (InvalidOperationException) { }
        await process.WaitForExitAsync();
        await stderrDrain;
        process.Dispose();
    }
}
