using System.Globalization;

namespace OpenAIUsage;

public sealed class UsageService(Storage storage)
{
    public async Task<WidgetState> RefreshAsync(WidgetSettings settings, CancellationToken cancellationToken = default)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(35));
        try
        {
            settings.Validate();
            await using var client = new CodexClient(settings.CodexPath);
            var usage = await client.ReadAsync(timeout.Token);
            return Build(usage, settings, DateTimeOffset.UtcNow);
        }
        catch (OperationCanceledException) { return new() { Status = "Codex timed out. Check connection and refresh." }; }
        catch (Exception ex) when (ex is KeyNotFoundException or InvalidOperationException or FormatException or OverflowException)
        {
            return new() { Status = "Unexpected Codex response. Update Codex and refresh; no account values were inferred." };
        }
        catch (Exception ex) when (ex is IOException or System.ComponentModel.Win32Exception or ArgumentException or System.Text.Json.JsonException or TimeZoneNotFoundException or InvalidTimeZoneException)
        {
            return new() { Status = ex is System.ComponentModel.Win32Exception ? "Codex executable not found. Set its path in Settings and sign in with 'codex login'." : ex.Message };
        }
    }

    public WidgetState Build(AccountUsage usage, WidgetSettings settings, DateTimeOffset now)
    {
        var zone = TimeZoneInfo.FindSystemTimeZoneById(settings.TimeZoneId);
        var today = DateOnly.FromDateTime(TimeZoneInfo.ConvertTime(now, zone).DateTime);
        // The API exposes calendar labels without a timezone. Exclude both today's local and UTC
        // date rather than presenting a potentially still-growing current bucket as a full day.
        var utcToday = DateOnly.FromDateTime(now.UtcDateTime);
        var tokenToday = today < utcToday ? today : utcToday;
        var selected = usage.Windows.FirstOrDefault(w => w.Key == settings.SelectedWindow)
            ?? usage.Windows.FirstOrDefault(w => w.Key.StartsWith("codex:") && w.Minutes == 10080)
            ?? usage.Windows.OrderByDescending(w => w.Minutes).FirstOrDefault();
        var state = new WidgetState
        {
            Connected = true, UpdatedAt = now, Status = "Live · Codex account", Plan = usage.Plan,
            Windows = usage.Windows, SelectedWindow = selected?.Key,
            CreditsLabel = usage.UnlimitedCredits ? "Unlimited" : usage.Credits?.ToString("N2", CultureInfo.InvariantCulture) ?? "Unavailable",
            TokenDays = usage.Tokens.Where(d => string.CompareOrdinal(d.Date, tokenToday.AddDays(-14).ToString("yyyy-MM-dd")) >= 0 && string.CompareOrdinal(d.Date, tokenToday.ToString("yyyy-MM-dd")) < 0).ToList(),
            TokenEndDate = tokenToday.AddDays(-1).ToString("yyyy-MM-dd"),
            AccessNote = usage.SpendBlocked ? "Account spending control reached." : usage.OrdinaryUsageAllowed == false ? "Included usage is currently blocked by the service." : ""
        };
        if (!usage.UnlimitedCredits && usage.Credits is { } credits && settings.CreditUnitPrice is { } price)
        {
            try { state.CreditValueLabel = $"≈ {(credits * price).ToString("N2", CultureInfo.InvariantCulture)} {settings.Currency.ToUpperInvariant()}"; }
            catch (OverflowException) { state.CreditValueLabel = "Purchase rate is too large"; }
        }
        var currentTokens = Analytics.TokenSummary(usage.Tokens, tokenToday.AddDays(-7));
        state.TokensLabel = currentTokens.Total;
        state.TokenAverageLabel = currentTokens.Average;
        state.PreviousTokensLabel = Analytics.TokenSummary(usage.Tokens, tokenToday.AddDays(-14)).Total;
        state.TokenNote = usage.TokenError ?? $"OpenAI daily buckets through {state.TokenEndDate}. Current local/UTC dates excluded; the service does not specify a bucket timezone. Tokens do not determine quota percentage.";
        var todayBucket = usage.Tokens.FirstOrDefault(d => d.Date == today.ToString("yyyy-MM-dd"));
        state.TodayTokensLabel = todayBucket?.CompactTokens ?? "—";
        state.TodayTokensNote = $"OpenAI bucket dated {today:yyyy-MM-dd}. Service timezone and publication delay may differ from your local day.";
        foreach (var window in usage.Windows)
        {
            List<Sample> history;
            try { history = storage.Record(usage.Identity, window, usage.Plan, now.ToUnixTimeSeconds()); }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or System.Text.Json.JsonException)
            {
                state.Status = "Live · Local history unavailable (file busy, corrupt or not writable)";
                history = [];
            }
            if (window.Key != selected?.Key) continue;
            state.AllowanceLabel = window.Label + " allowance remaining";
            state.Remaining = 100 - window.Used;
            state.ResetsAt = window.ResetsAt;
            if (window.ResetsAt is { } reset)
                state.ResetLabel = TimeZoneInfo.ConvertTime(DateTimeOffset.FromUnixTimeSeconds(reset), zone).ToString("ddd dd MMM yyyy · HH:mm:ss zzz", CultureInfo.InvariantCulture) + " · " + zone.Id;
            state.QuotaDays = Analytics.DailyQuota(history, today, zone, usage.Plan);
            var forecast = Analytics.Predict(state.Remaining.Value, state.QuotaDays, window.ResetsAt, now);
            state.AverageLabel = forecast.Average is { } average ? $"{average:0.##} pp / day" : "Collecting quota history";
            state.ForecastLabel = forecast.Headline;
            state.ForecastDetail = forecast.Detail;
            state.CoverageLabel = $"{forecast.CompleteDays} of 7 complete days";
        }
        return state;
    }
}
