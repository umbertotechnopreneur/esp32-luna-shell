using OpenAIUsage;
using System.Text.Json;

int passed = 0;
void Check(bool condition, string name) { if (!condition) throw new Exception("FAIL: " + name); Console.WriteLine("PASS: " + name); passed++; }
void Near(double? actual, double expected, string name) => Check(actual.HasValue && Math.Abs(actual.Value - expected) < .00001, name);
// action: Operation that must reject its input or preserve its original IO failure.
// name: Human-readable contract reported by the existing test harness.
// Exception: Throws if the expected failure does not occur; unexpected failures propagate.
TException Reject<TException>(Action action, string name) where TException : Exception
{
    try { action(); }
    catch (TException error) { Check(true, name); return error; }
    throw new Exception("FAIL: " + name);
}

// directory: Disposable test-owned directory, never the user's preferences.
// Exception: Throws on a failed settings preservation or recovery contract.
void CheckSettingsFailures(string directory)
{
    var settingsStore = new Storage(directory);
    var initial = settingsStore.LoadSettings();
    Check(initial.CodyBridge is not null && !File.Exists(settingsStore.SettingsPath), "Only an absent settings file supplies defaults without writing");
    var valid = new WidgetSettings
    {
        TimeZoneId = "UTC", WindowOpacityPercent = 75, AlwaysOnTop = true, SelectedWindow = "codex:10080",
        CodyBridge = new() { PythonExecutable = "test-python", ControlScriptPath = Path.Combine(directory, "ccp_server.py") },
        WindowsNotificationsEnabled = true, WindowsNotificationDeviceId = "abcdef012345",
        WindowsNotificationApplicationIds = ["Example.Application"]
    };
    settingsStore.SaveSettings(valid);
    string retained = File.ReadAllText(settingsStore.SettingsPath);
    var loaded = settingsStore.LoadSettings();
    Check(loaded.WindowsNotificationsEnabled && loaded.WindowsNotificationDeviceId == valid.WindowsNotificationDeviceId &&
        loaded.WindowsNotificationApplicationIds.SequenceEqual(valid.WindowsNotificationApplicationIds) &&
        loaded.CodyBridge.PythonExecutable == valid.CodyBridge.PythonExecutable && loaded.WindowOpacityPercent == 75,
        "Nested bridge and notification settings round trip without changing values");
    var invalidDocuments = new Dictionary<string, string>
    {
        ["null document"] = "null",
        ["truncated document"] = "{",
        ["null bridge"] = "{\"codyBridge\":null}",
        ["null bridge executable"] = "{\"codyBridge\":{\"pythonExecutable\":null}}",
        ["null bridge launcher"] = "{\"codyBridge\":{\"controlScriptPath\":null}}",
        ["null notification applications"] = "{\"windowsNotificationApplicationIds\":null}",
        ["null notification application"] = "{\"windowsNotificationApplicationIds\":[null]}",
        ["duplicate notification application"] = "{\"windowsNotificationApplicationIds\":[\"Example.App\",\"example.app\"]}",
        ["null notification device"] = "{\"windowsNotificationDeviceId\":null}",
        ["invalid notification device"] = "{\"windowsNotificationDeviceId\":\"not-a-device\"}",
        ["invalid opacity"] = "{\"windowOpacityPercent\":5}",
        ["incomplete forwarding"] = "{\"windowsNotificationsEnabled\":true}"
    };
    foreach (var (name, document) in invalidDocuments)
    {
        File.WriteAllText(settingsStore.SettingsPath, document);
        var reader = new Storage(directory);
        var failure = Reject<Exception>(() => reader.LoadSettings(), "Reject " + name);
        Check(failure is JsonException or ArgumentException or InvalidDataException, name + " reports a configuration error, not a null dereference");
        var blocked = Reject<IOException>(() => reader.SaveSettings(valid), name + " blocks subsequent writes");
        Check(ReferenceEquals(blocked.InnerException, failure) && File.ReadAllText(reader.SettingsPath) == document,
            name + " preserves the original failure and saved bytes");
        var shortLived = new Storage(directory);
        Reject<Exception>(() => shortLived.SaveSettings(valid), name + " cannot be overwritten by a fresh backend");
        Check(File.ReadAllText(reader.SettingsPath) == document, name + " remains unchanged after a fresh backend save");
        File.WriteAllText(reader.SettingsPath, retained);
        Reject<IOException>(() => reader.SaveSettings(valid), name + " requires an explicit successful reload before writing");
        _ = reader.LoadSettings();
        reader.SaveSettings(valid);
        Check(reader.SettingsReadFailure is null && File.ReadAllText(reader.SettingsPath) == retained,
            name + " recovers after an explicit reload of repaired settings");
    }
    var afterRead = new Storage(directory);
    _ = afterRead.LoadSettings();
    File.WriteAllText(afterRead.SettingsPath, "{");
    Reject<JsonException>(() => afterRead.SaveSettings(valid), "Save detects settings damaged since the previous load");
    Check(File.ReadAllText(afterRead.SettingsPath) == "{", "A failed pre-save read preserves the damaged file");
    File.WriteAllText(afterRead.SettingsPath, retained);
    _ = afterRead.LoadSettings();
    Reject<ArgumentException>(() => afterRead.SaveSettings(new() { CodyBridge = null! }), "Invalid in-memory bridge cannot be saved");
    Check(File.ReadAllText(afterRead.SettingsPath) == retained, "Invalid proposed settings preserve the last valid file");
    File.Delete(afterRead.SettingsPath);
    Directory.CreateDirectory(afterRead.SettingsPath);
    var ioFailure = Reject<Exception>(() => afterRead.LoadSettings(), "A settings path that is a directory is a read failure");
    Check(ioFailure is IOException or UnauthorizedAccessException, "Filesystem failures retain their IO cause");
    Reject<IOException>(() => afterRead.SaveSettings(valid), "A filesystem read failure blocks settings replacement");
    Check(Directory.Exists(afterRead.SettingsPath), "An inaccessible settings path is not replaced with defaults");
    Directory.Delete(afterRead.SettingsPath);
    _ = afterRead.LoadSettings();
    afterRead.SaveSettings(valid);
    Check(File.ReadAllText(afterRead.SettingsPath) == retained, "Explicitly removing the invalid path permits first-use settings after reload");
    string linkTarget = Path.Combine(directory, "linked-settings.json");
    File.WriteAllText(linkTarget, retained);
    File.Delete(afterRead.SettingsPath);
    bool linksSupported = false;
    try
    {
        File.CreateSymbolicLink(afterRead.SettingsPath, linkTarget);
        linksSupported = true;
    }
    catch (Exception error) when (error is IOException or UnauthorizedAccessException or PlatformNotSupportedException)
    {
        Console.WriteLine("SKIP: Settings symbolic-link checks; the test volume or current account cannot create links (" + error.GetType().Name + ").");
    }
    try
    {
        if (linksSupported)
        {
            Reject<IOException>(() => new Storage(directory).LoadSettings(), "Settings reads reject a symbolic link even with a valid target");
            Reject<IOException>(() => new Storage(directory).SaveSettings(valid), "Settings writes do not replace a symbolic link");
            Check(new FileInfo(afterRead.SettingsPath).LinkTarget is not null && File.ReadAllText(linkTarget) == retained,
                "Rejected linked settings preserve both the directory entry and target bytes");
            File.Delete(linkTarget);
            var dangling = new Storage(directory);
            Reject<IOException>(() => dangling.LoadSettings(), "Dangling settings links are not treated as absent files");
            Reject<IOException>(() => dangling.SaveSettings(valid), "A dangling-link read failure blocks subsequent writes");
            Reject<IOException>(() => new Storage(directory).SaveSettings(valid), "A new backend cannot replace a dangling settings link");
            Check(new FileInfo(afterRead.SettingsPath).LinkTarget is not null && !File.Exists(linkTarget),
                "Rejected dangling settings preserve the link without creating its target");
        }
    }
    finally
    {
        File.Delete(afterRead.SettingsPath);
        File.Delete(linkTarget);
        File.WriteAllText(afterRead.SettingsPath, retained);
    }
}
Check(!WindowsMemoryGuard.IsOverLimit(999_999_999) && !WindowsMemoryGuard.IsOverLimit(1_000_000_000), "Memory guard accepts up to exactly one decimal GB");
Check(WindowsMemoryGuard.IsOverLimit(1_000_000_001), "Memory guard trips above one decimal GB");
Check(WindowsMemoryGuard.Run(["--memory-guard", "invalid"]) == 2, "Invalid watchdog invocation cannot target another process");
Check(CodexExecutable.Resolve("/explicit/custom/codex") == "/explicit/custom/codex", "Explicit Codex path is preserved");
Check(Analytics.Compact(1000) == "1 K", "Token kilo uses decimal thousands");
Check(Analytics.Compact(1_000_000) == "1 M", "Token mega suffix");
Check(Analytics.Compact(1_000_000_000) == "1 G", "Token giga suffix instead of B");
Check(Analytics.Compact(1_000_000_000_000) == "1 T", "Token tera suffix");
Check(Analytics.Compact(999_999_999) == "1 G", "Rounding promotes the next unit");
Check(Analytics.Compact(0) == "0" && Analytics.Compact(double.NaN) == "—", "Zero and invalid token quantities");
var now = new DateTimeOffset(2026, 11, 2, 0, 0, 0, TimeSpan.Zero);
var days = new[] { 14d, 17, 22, 16, 20, 19, 18 }.Select((used, i) => new QuotaDay($"2026-10-{26+i:00}", used, 1)).ToArray();
var forecast = Analytics.Predict(42, days, now.AddHours(62).ToUnixTimeSeconds(), now);
Near(forecast.Average, 18, "Seven-day arithmetic average");
Near(forecast.DaysLeft, 56d/24, "42 percentage points at 18 per day lasts 56 hours");
Check(forecast.Detail.Contains("6 hours"), "Exhaustion warning before reset");
Check(Analytics.Predict(42, days, now.AddHours(1).ToUnixTimeSeconds(), now).Detail.Contains("until reset"), "Reset precedes hypothetical exhaustion");
Check(Analytics.Predict(0, days, null, now).Headline == "Allowance exhausted", "Zero allowance");
Check(Analytics.Predict(42, [], null, now).Average is null, "No history is not zero consumption");
Check(Analytics.Predict(42, [new("2026-11-01", 0, 1)], null, now).DaysLeft is null, "Zero consumption has no finite forecast");
Check(Analytics.Predict(42, days.Take(2).ToArray(), null, now).Detail.Contains("partial"), "Incomplete seven-day window is disclosed");
Check(Analytics.Predict(42, days, now.AddMinutes(-1).ToUnixTimeSeconds(), now).DaysLeft is null, "Expired reset invalidates forecast");

long start = now.AddDays(-1).ToUnixTimeSeconds(), end = now.ToUnixTimeSeconds();
var samples = Enumerable.Range(0, 1441).Select(i => new Sample(start+i*60, i*18d/1440, end+86400, "pro")).ToArray();
var daily = Analytics.DailyQuota(samples, new(2026, 11, 2), TimeZoneInfo.Utc, "pro");
Near(daily[^1].Used, 18, "Continuous full-day quota measurement");
Check(daily.Take(6).All(d => d.Used is null), "Unobserved days remain missing");
var gap = samples.Where((_, i) => i < 100 || i > 110).ToArray();
Check(Analytics.DailyQuota(gap, new(2026, 11, 2), TimeZoneInfo.Utc, "pro")[^1].Used is null, "Long monitoring gap invalidates daily quota");
var resetCrossing = samples.Select((s, i) => i < 700 ? s : s with { Reset = end+2*86400, Used = s.Used-8 }).ToArray();
Check(Analytics.DailyQuota(resetCrossing, new(2026, 11, 2), TimeZoneInfo.Utc, "pro")[^1].Used is null, "Reset crossing is not negative usage");
Check(Analytics.DailyQuota(samples, new(2026, 11, 2), TimeZoneInfo.Utc, "plus")[^1].Used is null, "Plan changes invalidate incomparable quota samples");
var dstZone = TimeZoneInfo.FindSystemTimeZoneById(OperatingSystem.IsWindows() ? "Eastern Standard Time" : "America/New_York");
var dstStart = new DateTimeOffset(2026, 11, 1, 4, 0, 0, TimeSpan.Zero).ToUnixTimeSeconds();
var dstSamples = Enumerable.Range(0, 1501).Select(i => new Sample(dstStart+i*60, i*20d/1500, dstStart+3*86400, "pro"));
Near(Analytics.DailyQuota(dstSamples, new(2026, 11, 2), dstZone, "pro")[^1].Used, 20, "25-hour daylight-saving calendar day");

var observed = Enumerable.Range(0, 121).Select(i => new Sample(end - 7200 + i * 60, i / 120d * 4, end + 86400, "pro")).ToArray();
var observedForecast = Analytics.PredictObserved(48, observed, "pro", end + 86400, now);
Near(observedForecast.DaysLeft, 1, "Partial observed periods produce a rate-based estimate");
Check(observedForecast.Detail.Contains("observed"), "Partial observation is disclosed");
Check(Analytics.PredictObserved(48, observed.Take(30), "pro", end + 86400, now).DaysLeft is null, "Insufficient observation does not produce a forecast");
var broken = observed.Select((sample, i) => i < 60 ? sample : sample with { At = sample.At + 600 }).ToArray();
Check(Analytics.PredictObserved(48, broken, "other", end + 86400, now).DaysLeft is null, "Different plans cannot create an estimate");
Check(Analytics.CompactDuration(1.5) == "1d 12h", "Compact forecast fits the widget");

var tokenDays = Enumerable.Range(0, 7).Select(i => new TokenDay(new DateOnly(2026, 10, 26).AddDays(i).ToString("yyyy-MM-dd"), 100)).ToList();
Check(Analytics.TokenSummary(tokenDays, new(2026, 10, 26)).Total == "700", "Seven-day token total");
Check(Analytics.TokenSummary(tokenDays.Skip(1), new(2026, 10, 26)).Total.Contains("6/7"), "Missing token date is not an implicit zero");

using var account = JsonDocument.Parse("""{"type":"chatgpt","email":"fixture@example.test","planType":"pro"}""");
using var limits = JsonDocument.Parse("""
{"accountId":"fixture-account","rateLimitsByLimitId":{"codex":{"primary":{"usedPercent":58,"windowDurationMins":10080,"resetsAt":1794000000},"secondary":{"usedPercent":20,"windowDurationMins":300,"resetsAt":1794000000},"credits":{"unlimited":false,"hasCredits":true,"balance":"1250.25"}}}}
""");
using var usage = JsonDocument.Parse("""{"dailyUsageBuckets":[{"startDate":"2026-11-01","tokens":123},{"startDate":"2026-11-01","tokens":456}]}""");
var parsed = CodexClient.Parse(account.RootElement, limits.RootElement, usage.RootElement);
Check(parsed.Windows.Count == 2 && parsed.Windows[0].Minutes == 10080, "Window duration determines weekly bucket, not primary/secondary name");
Check(parsed.Credits == 1250.25m, "Decimal credit balance preserved");
Check(parsed.Tokens[0].Tokens is null, "Conflicting duplicate service dates are unknown");
Check(!parsed.Identity.Contains("fixture"), "Persisted identity is hashed");
using var absent = JsonDocument.Parse("""{"rateLimits":{"primary":null,"secondary":null}}""");
Check(CodexClient.Parse(account.RootElement, absent.RootElement, null).Windows.Count == 0, "Missing allowance windows do not create fake quota");
using var unlimited = JsonDocument.Parse("""{"rateLimits":{"credits":{"unlimited":true}}}""");
Check(CodexClient.Parse(account.RootElement, unlimited.RootElement, null).UnlimitedCredits, "Unlimited credits represented separately from balance");

string temporary = Path.Combine(Path.GetTempPath(), "openai-usage-tests-" + Guid.NewGuid().ToString("N"));
try
{
    var store = new Storage(temporary);
    CheckSettingsFailures(Path.Combine(temporary, "settings-failures"));
    var settings = new WidgetSettings { TimeZoneId = "UTC" };
    store.SaveSettings(settings);
    Check(store.LoadSettings().TimeZoneId == "UTC", "Settings persist atomically");
    var state = new UsageService(store).Build(parsed, settings, now);
    Check(state.CreditsLabel == "1,250", "Credit balance is displayed without decimal places");
    Check(state.ResetDisplays.Count == 2 && state.ResetDisplays[0].Heading == "First reset" && state.ResetDisplays[1].Heading == "Second reset", "Available reset dates are numbered in chronological order");
    Check(state.ResetDisplays[0].Detail.Contains("UTC"), "Reset tooltip includes the selected system time zone");
    var missingReset = parsed with { Windows=[parsed.Windows[0] with { ResetsAt = null }, parsed.Windows[1]] };
    Check(new UsageService(store).Build(missingReset, settings, now).ResetDisplays.Count == 1, "Missing reset timestamps are not invented");
    var expiredReset = parsed with { Windows=[parsed.Windows[0] with { ResetsAt = now.AddSeconds(-1).ToUnixTimeSeconds() }, parsed.Windows[1]] };
    var expiredList = new UsageService(store).Build(expiredReset, settings, now).ResetDisplays;
    Check(expiredList.Count == 2 && expiredList[0].Detail.Contains("Waiting for an updated reset"), "Expired reported resets remain visible while waiting for refresh");
    var extraReset = parsed with { Windows=[..parsed.Windows, new QuotaWindow("codex:600", "10-hour · codex", 5, 600, now.AddHours(3).ToUnixTimeSeconds())] };
    var resetList = new UsageService(store).Build(extraReset, settings, now).ResetDisplays;
    Check(resetList.Count == 3 && resetList[0].Detail.Contains("10-hour") && resetList[2].Heading == "Third reset", "Additional reset dates are retained and ordered by time");
    Check(!new WidgetSettings().BigSize, "Big size is disabled by default");
    settings.BigSize = true;
    store.SaveSettings(settings);
    Check(store.LoadSettings().BigSize, "Big size persists with settings");
    Check(state.TodayTokensLabel == "—", "Missing current-day bucket is not invented as zero");
    var withToday=parsed with { Tokens=[new TokenDay("2026-11-02",1_000_000)] };
    Check(new UsageService(store).Build(withToday,settings,now).TodayTokensLabel=="1 M", "Today's reported bucket is separate from the completed seven-day average");
    settings.TimeZoneId = OperatingSystem.IsWindows() ? "SE Asia Standard Time" : "Asia/Ho_Chi_Minh";
    state = new UsageService(store).Build(parsed, settings, new DateTimeOffset(2026, 11, 1, 21, 0, 0, TimeSpan.Zero));
    Check(state.TokenEndDate == "2026-10-31", "Still-growing current UTC token bucket is excluded even after local midnight");
    var other = store.Record("different-account", parsed.Windows[0], "pro", now.ToUnixTimeSeconds());
    Check(other.Count == 1, "Accounts never share history");
    var duplicate = store.Record("different-account", parsed.Windows[0], "pro", now.ToUnixTimeSeconds());
    Check(duplicate.Count == 1, "Same-timestamp observations do not double count");
}
finally
{
    string checkedPath = Path.GetFullPath(temporary);
    string tempRoot = Path.GetFullPath(Path.GetTempPath());
    if (!checkedPath.StartsWith(tempRoot, StringComparison.OrdinalIgnoreCase) || !Path.GetFileName(checkedPath).StartsWith("openai-usage-tests-", StringComparison.Ordinal)) throw new IOException("Unexpected test cleanup path.");
    Directory.Delete(checkedPath, true);
}
Console.WriteLine($"{passed} checks passed.");
