using System.Text.Json;

namespace OpenAIUsage;

public record QuotaWindow(string Key, string Label, double Used, int Minutes, long? ResetsAt);
public record TokenDay(string Date, long? Tokens)
{
    public string CompactTokens => Tokens is { } count ? Analytics.Compact(count) : "—";
}
public record AccountUsage(string Identity, string Plan, List<QuotaWindow> Windows,
    decimal? Credits, bool UnlimitedCredits, bool? OrdinaryUsageAllowed, bool SpendBlocked,
    List<TokenDay> Tokens, string? TokenError)
{
    public ResetVoucherBalance? ResetVouchers { get; init; }
}
public record ResetVoucherBalance(int AvailableCount, IReadOnlyList<long?> Expirations);
public record ResetVoucherDisplay(string Value, string Detail);
public record Sample(long At, double Used, long? Reset, string Plan);
public record QuotaDay(string Date, double? Used, double Coverage);
public record Forecast(double? Average, double? DaysLeft, int CompleteDays, string Headline, string Detail);
public record ResetDisplay(string Heading, string Value, string Detail, long ResetsAt);

public sealed class WidgetSettings
{
    public string CodexPath { get; set; } = "codex";
    public string? SelectedWindow { get; set; }
    public string TimeZoneId { get; set; } = TimeZoneInfo.Local.Id;
    public bool AlwaysOnTop { get; set; }
    public bool ShowInTaskbar { get; set; } = true;
    public bool BigSize { get; set; }
    public int WindowOpacityPercent { get; set; } = 100;
    public bool Alert50 { get; set; }
    public bool Alert20 { get; set; }
    public bool Alert5 { get; set; }
    public CodyBridgeSettings CodyBridge { get; set; } = new();
    public bool WindowsNotificationsEnabled { get; set; }
    public string WindowsNotificationDeviceId { get; set; } = "";
    public List<string> WindowsNotificationApplicationIds { get; set; } = [];
    // No parameters: validate every persisted value used by the current native and backend consumers.
    // Exception: Rejects invalid or null configuration rather than normalizing or replacing it.
    public void Validate()
    {
        if (string.IsNullOrWhiteSpace(CodexPath) || CodexPath.Length > 4096 || CodexPath.Contains('\0'))
            throw new ArgumentException("Choose a valid Codex executable.");
        if (string.IsNullOrWhiteSpace(TimeZoneId)) throw new ArgumentException("Choose a time zone.");
        _ = TimeZoneInfo.FindSystemTimeZoneById(TimeZoneId);
        if (SelectedWindow is not null && (SelectedWindow.Length == 0 || SelectedWindow.Length > 256 || SelectedWindow.Any(char.IsControl)))
            throw new ArgumentException("The selected allowance window is invalid.");
        if (WindowOpacityPercent is not (25 or 50 or 75 or 100))
            throw new ArgumentException("Window opacity must be 25, 50, 75 or 100 percent.");
        if (CodyBridge is null) throw new ArgumentException("Cody bridge settings must be an object.");
        CodyBridge.Validate();
        if (WindowsNotificationDeviceId is null || (WindowsNotificationDeviceId.Length != 0 &&
            (WindowsNotificationDeviceId.Length != 12 || !WindowsNotificationDeviceId.All(character =>
                character is >= '0' and <= '9' or >= 'a' and <= 'f'))))
            throw new ArgumentException("The notification destination must be an exact Cody device identity.");
        if (WindowsNotificationApplicationIds is null || WindowsNotificationApplicationIds.Any(string.IsNullOrWhiteSpace) ||
            WindowsNotificationApplicationIds.Any(id => id.Any(char.IsControl)) ||
            WindowsNotificationApplicationIds.Distinct(StringComparer.OrdinalIgnoreCase).Count() != WindowsNotificationApplicationIds.Count)
            throw new ArgumentException("Notification application identities must be nonempty, unique strings.");
        if (WindowsNotificationsEnabled && (WindowsNotificationDeviceId.Length == 0 || WindowsNotificationApplicationIds.Count == 0 ||
            CodyBridge.ControlScriptPath.Length == 0))
            throw new ArgumentException("Choose a Cody destination, applications and companion launcher before enabling notifications.");
    }
}

public sealed class WidgetState
{
    public bool Connected { get; set; }
    public DateTimeOffset? UpdatedAt { get; set; }
    public string Status { get; set; } = "Connecting to Codex…";
    public string Plan { get; set; } = "Unavailable";
    public List<QuotaWindow> Windows { get; set; } = [];
    public string? SelectedWindow { get; set; }
    public string AllowanceLabel { get; set; } = "Allowance remaining";
    public double? Remaining { get; set; }
    public long? ResetsAt { get; set; }
    public string ResetLabel { get; set; } = "Reset unavailable";
    public List<ResetDisplay> ResetDisplays { get; set; } = [];
    public int? ResetVoucherCount { get; set; }
    public IReadOnlyList<ResetVoucherDisplay> ResetVoucherExpirations { get; set; } = [];
    public string CreditsLabel { get; set; } = "Unavailable";
    public string AccessNote { get; set; } = "";
    public string AverageLabel { get; set; } = "Collecting quota history";
    public bool ForecastAvailable { get; set; }
    public string ForecastCompact { get; set; } = "ⓘ";
    public string ForecastLabel { get; set; } = "Not enough history";
    public string ForecastDetail { get; set; } = "Keep the widget open to record allowance usage.";
    public string CoverageLabel { get; set; } = "0 of 7 complete days";
    public List<QuotaDay> QuotaDays { get; set; } = [];
    public List<TokenDay> TokenDays { get; set; } = [];
    public string? TokenEndDate { get; set; }
    public string TodayTokensLabel { get; set; } = "—";
    public string TodayTokensNote { get; set; } = "Today's service bucket is not available yet.";
    public string TokensLabel { get; set; } = "Unavailable";
    public string PreviousTokensLabel { get; set; } = "Unavailable";
    public string TokenAverageLabel { get; set; } = "Unavailable";
    public string TokenNote { get; set; } = "Service calendar dates; missing days are not zero.";
}

public static class JsonOptions
{
    public static readonly JsonSerializerOptions Default = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        PropertyNameCaseInsensitive = true,
        WriteIndented = true
    };
    public static readonly JsonSerializerOptions Compact = new(Default) { WriteIndented = false };
}
