using System.Text.Json;

namespace OpenAIUsage;

public record QuotaWindow(string Key, string Label, double Used, int Minutes, long? ResetsAt);
public record TokenDay(string Date, long? Tokens)
{
    public string CompactTokens => Tokens is { } count ? Analytics.Compact(count) : "—";
}
public record AccountUsage(string Identity, string Plan, List<QuotaWindow> Windows,
    decimal? Credits, bool UnlimitedCredits, bool? OrdinaryUsageAllowed, bool SpendBlocked,
    List<TokenDay> Tokens, string? TokenError);
public record Sample(long At, double Used, long? Reset, string Plan);
public record QuotaDay(string Date, double? Used, double Coverage);
public record Forecast(double? Average, double? DaysLeft, int CompleteDays, string Headline, string Detail);

public sealed class WidgetSettings
{
    public string CodexPath { get; set; } = "codex";
    public string? SelectedWindow { get; set; }
    public decimal? CreditUnitPrice { get; set; }
    public string Currency { get; set; } = "USD";
    public string TimeZoneId { get; set; } = TimeZoneInfo.Local.Id;
    public bool ReduceMotion { get; set; }
    public bool AlwaysOnTop { get; set; }
    public CodyBridgeSettings CodyBridge { get; set; } = new();
    // No parameters: validates stored account and companion configuration without contacting either service.
    // Exception: Throws ArgumentException or time-zone exceptions for invalid persisted values.
    public void Validate()
    {
        if (string.IsNullOrWhiteSpace(CodexPath)) throw new ArgumentException("Choose a Codex executable.");
        if (CreditUnitPrice is < 0) throw new ArgumentException("Purchase price cannot be negative.");
        if (Currency.Length != 3 || !Currency.All(char.IsAsciiLetter)) throw new ArgumentException("Use a three-letter currency code.");
        _ = TimeZoneInfo.FindSystemTimeZoneById(TimeZoneId);
        if (CodyBridge is null) throw new ArgumentException("Cody companion settings are invalid.");
        CodyBridge.Validate();
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
    public string CreditsLabel { get; set; } = "Unavailable";
    public string CreditValueLabel { get; set; } = "Set purchase rate in Settings";
    public string CreditNote { get; set; } = "Purchase-cost estimate, not cash value";
    public string AccessNote { get; set; } = "";
    public string AverageLabel { get; set; } = "Collecting quota history";
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
