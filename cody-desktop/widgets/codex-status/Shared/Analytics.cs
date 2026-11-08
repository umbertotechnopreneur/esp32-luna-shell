using System.Globalization;

namespace OpenAIUsage;

public static class Analytics
{
    public const int MaximumSampleGapSeconds = 180;

    // Samples describe window percentages, not token balances. Reset crossings are deliberately gaps.
    public static List<QuotaDay> DailyQuota(IEnumerable<Sample> source, DateOnly today, TimeZoneInfo zone, string plan)
    {
        var samples = source.OrderBy(s => s.At).DistinctBy(s => s.At).ToArray();
        var result = new List<QuotaDay>();
        for (int offset = -7; offset < 0; offset++)
        {
            var date = today.AddDays(offset);
            long start = DayBoundary(date, zone), end = DayBoundary(date.AddDays(1), zone);
            double covered = 0, used = 0;
            for (int i = 1; i < samples.Length; i++)
            {
                var a = samples[i - 1]; var b = samples[i];
                if (b.At <= start || a.At >= end) continue;
                long elapsed = b.At - a.At;
                if (elapsed <= 0 || elapsed > MaximumSampleGapSeconds || a.Reset is null || a.Reset != b.Reset ||
                    a.Plan != plan || b.Plan != plan || b.Used < a.Used || b.At > a.Reset) continue;
                long overlap = Math.Max(0, Math.Min(end, b.At) - Math.Max(start, a.At));
                covered += overlap;
                used += (b.Used - a.Used) * overlap / elapsed;
            }
            double coverage = Math.Clamp(covered / (end - start), 0, 1);
            result.Add(new(date.ToString("yyyy-MM-dd"), coverage >= 0.999999 ? used : null, coverage));
        }
        return result;
    }

    private static long DayBoundary(DateOnly date, TimeZoneInfo zone)
    {
        var local = date.ToDateTime(TimeOnly.MinValue, DateTimeKind.Unspecified);
        // Some time zones advance at midnight. Use the first valid instant of that calendar day.
        while (zone.IsInvalidTime(local)) local = local.AddMinutes(1);
        return new DateTimeOffset(TimeZoneInfo.ConvertTimeToUtc(local, zone)).ToUnixTimeSeconds();
    }

    public static Forecast Predict(double remaining, IReadOnlyList<QuotaDay> days, long? reset, DateTimeOffset now)
    {
        var complete = days.Where(d => d.Used.HasValue).ToArray();
        double? average = complete.Length == 0 ? null : complete.Average(d => d.Used!.Value);
        if (remaining <= 0) return new(average, 0, complete.Length, "Allowance exhausted", "Wait for the reset or check available credits in Codex.");
        if (reset is { } old && old <= now.ToUnixTimeSeconds())
            return new(average, null, complete.Length, "Waiting for reset data", "Refresh to confirm the new allowance.");
        if (average is null) return new(null, null, 0, "Quota forecast is learning", "Token history comes from OpenAI. Remaining-time estimates need locally observed quota consumption.");
        if (average <= 0) return new(0, null, complete.Length, "No observed consumption", "No exhaustion estimate while the measured daily average is zero.");
        var duration = remaining / average.Value;
        string detail = complete.Length == 7 ? "Based on the last 7 complete days." : $"Based on {complete.Length} complete day(s) in the last 7; partial estimate.";
        if (reset is { } r)
        {
            double toReset = (r - now.ToUnixTimeSeconds()) / 86400d;
            detail += duration < toReset
                ? $" May run out {Duration(toReset - duration)} before reset."
                : " At this pace, the allowance should last until reset.";
        }
        return new(average, duration, complete.Length, Duration(duration), detail);
    }

    public static string Duration(double days)
    {
        if (!double.IsFinite(days) || days > 36500) return "Over 100 years at this pace";
        long minutes = Math.Max(0, (long)Math.Floor(days * 1440));
        if (minutes < 1) return "Less than a minute";
        if (minutes < 60) return $"{minutes} minutes";
        if (minutes < 1440) return $"{minutes / 60} hours {minutes % 60} minutes";
        return $"{minutes / 1440} days {minutes % 1440 / 60} hours";
    }

    public static (string Total, string Average) TokenSummary(IEnumerable<TokenDay> source, DateOnly start)
    {
        var byDate = source.ToDictionary(d => d.Date);
        var values = Enumerable.Range(0, 7).Select(i => byDate.GetValueOrDefault(start.AddDays(i).ToString("yyyy-MM-dd"))?.Tokens).ToArray();
        if (values.Any(v => v is null)) return ($"Unavailable ({values.Count(v => v.HasValue)}/7 days)", "Insufficient token history");
        double total = values.Sum(v => (double)v!.Value);
        return (Compact(total), Compact(total / 7) + " / day");
    }

    public static string Compact(double value)
    {
        if (!double.IsFinite(value) || value < 0) return "—";
        string[] units = ["", "K", "M", "G", "T", "P", "E"];
        int unit = 0;
        while (value >= 1000 && unit < units.Length - 1) { value /= 1000; unit++; }
        if (Math.Round(value, 2) >= 1000 && unit < units.Length - 1) { value /= 1000; unit++; }
        return value.ToString(unit == 0 ? "0" : "0.##", CultureInfo.CurrentCulture) + (unit == 0 ? "" : " " + units[unit]);
    }
}
