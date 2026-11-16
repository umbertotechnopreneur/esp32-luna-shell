using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace OpenAIUsage.Windows;

// Native editor for the monitor's bounded control API. It owns no monitoring worker.
public sealed class CodyRulesPanel : UserControl
{
    private readonly Func<string, object, Task<JsonElement>> invoke;
    private readonly StackPanel content = new() { Spacing = 10 };
    private readonly ComboBox devices = new() { Header = "Physical Cody" };
    private readonly ComboBox metrics = new() { Header = "Metric" };
    private readonly ListView list = new() { Height = 130, SelectionMode = ListViewSelectionMode.Single };
    private readonly TextBox title = new() { Header = "Alert name", MaxLength = 120 };
    private readonly ComboBox condition = new() { Header = "Condition" };
    private readonly ComboBox severity = new() { Header = "Severity" };
    private readonly TextBox threshold = new() { Header = "Threshold" };
    private readonly TextBox duration = new() { Header = "Duration in seconds", Text = "0" };
    private readonly TextBox repeat = new() { Header = "Repeat interval in seconds (optional)" };
    private readonly TextBox reset = new() { Header = "Reset threshold (optional)" };
    private readonly CheckBox enabled = new() { Content = "Enabled", IsChecked = true };
    private readonly TextBlock result = new() { TextWrapping = TextWrapping.Wrap };
    private List<Rule> rules = [];
    private bool busy;

    private sealed class Rule
    {
        public Rule() { }
        [JsonPropertyName("id")] public string Id { get; set; } = "";
        [JsonPropertyName("title")] public string Title { get; set; } = "";
        [JsonPropertyName("metric_id")] public string MetricId { get; set; } = "";
        [JsonPropertyName("condition")] public string Condition { get; set; } = "below";
        [JsonPropertyName("threshold")] public double Threshold { get; set; }
        [JsonPropertyName("severity")] public string Severity { get; set; } = "information";
        [JsonPropertyName("duration_seconds")] public double Duration { get; set; }
        [JsonPropertyName("repeat_seconds")] public double? Repeat { get; set; }
        [JsonPropertyName("reset_threshold")] public double? Reset { get; set; }
        [JsonPropertyName("enabled")] public bool Enabled { get; set; } = true;
        public override string ToString() => $"{Title} · {MetricId} · {Severity}";
    }

    // invoke: Existing bounded broker-control function returning the result object.
    // Exception: The caller receives construction errors from native UI controls.
    public CodyRulesPanel(Func<string, object, Task<JsonElement>> invoke)
    {
        this.invoke = invoke ?? throw new ArgumentNullException(nameof(invoke));
        condition.ItemsSource = new[] { "below", "above", "changed_to_below", "changed_to_above", "increased" };
        severity.ItemsSource = new[] { "information", "warning", "critical" };
        condition.SelectedIndex = 0;
        severity.SelectedIndex = 0;
        content.Children.Add(new TextBlock { Text = "PC alerts", FontSize = 22 });
        content.Children.Add(new TextBlock { Text = "Select a paired physical Cody, save, start the monitor and refresh to discover metrics. Refresh discards unsaved drafts. Severity delivery requires compatible Cody firmware; delivery errors appear below.", TextWrapping = TextWrapping.Wrap });
        content.Children.Add(devices);
        content.Children.Add(list);
        list.SelectionChanged += (_, _) => ShowSelected();
        content.Children.Add(title);
        content.Children.Add(metrics);
        content.Children.Add(condition);
        content.Children.Add(threshold);
        content.Children.Add(severity);
        content.Children.Add(duration);
        content.Children.Add(repeat);
        content.Children.Add(reset);
        condition.SelectionChanged += (_, _) => UpdateConditionFields();
        content.Children.Add(enabled);
        var actions = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        actions.Children.Add(Button("New", () => { ClearEditor(); return Task.CompletedTask; }));
        actions.Children.Add(Button("Apply rule", () => { ApplyRule(); return Task.CompletedTask; }));
        actions.Children.Add(Button("Duplicate", () => { DuplicateRule(); return Task.CompletedTask; }));
        actions.Children.Add(Button("Delete", () => { DeleteRule(); return Task.CompletedTask; }));
        content.Children.Add(actions);
        var controls = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        controls.Children.Add(Button("Refresh", RefreshAsync));
        controls.Children.Add(Button("Save", SaveAsync));
        controls.Children.Add(Button("Start", () => OperationAsync("monitor/start")));
        controls.Children.Add(Button("Stop", () => OperationAsync("monitor/stop")));
        content.Children.Add(controls);
        content.Children.Add(result);
        Content = new ScrollViewer { Content = content, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled };
        Loaded += async (_, _) => await RunAsync(RefreshAsync);
    }

    // caption: The visible action caption.
    // action: UI operation, invoked once for each click.
    // Exception: Native control creation errors propagate to the caller.
    private Button Button(string caption, Func<Task> action)
    {
        var button = new Button { Content = caption };
        button.Click += async (_, _) => await RunAsync(action);
        return button;
    }

    // action: One explicit operation, never retried after an ambiguous failure.
    // Exception: Displays the original exception to the user.
    private async Task RunAsync(Func<Task> action)
    {
        if (busy) return;
        busy = true;
        try { await action(); }
        catch (Exception error) { result.Text = error.Message; }
        finally { busy = false; }
    }

    // No parameters: reads current rules, available samples and physical devices.
    // Exception: Preserves broker, parsing and validation errors for the UI.
    private async Task RefreshAsync()
    {
        var monitor = await invoke("monitor/status", new { });
        var bridge = await invoke("bridge/status", new { });
        var loaded = JsonSerializer.Deserialize<List<Rule>>(monitor.GetProperty("rules").GetRawText()) ?? [];
        var savedDevice = monitor.GetProperty("device_id");
        var savedId = savedDevice.ValueKind == JsonValueKind.String ? savedDevice.GetString() : null;
        devices.Items.Clear();
        foreach (var item in bridge.GetProperty("devices").EnumerateArray())
        {
            if (item.GetProperty("simulated").GetBoolean()) continue;
            var id = item.GetProperty("device_id").GetString();
            if (string.IsNullOrEmpty(id)) continue;
            devices.Items.Add(id);
            if (id == savedId) devices.SelectedItem = id;
        }
        metrics.Items.Clear();
        foreach (var sample in monitor.GetProperty("samples").EnumerateArray())
        {
            var id = sample.GetProperty("id").GetString();
            if (string.IsNullOrEmpty(id) || MetricItem(id) is not null) continue;
            var label = sample.GetProperty("label").GetString();
            var unit = sample.GetProperty("unit").GetString();
            metrics.Items.Add(new ComboBoxItem { Tag = id, Content = string.IsNullOrEmpty(unit) ? label : $"{label} ({unit})" });
        }
        foreach (var rule in loaded)
            if (MetricItem(rule.MetricId) is null) metrics.Items.Add(new ComboBoxItem { Tag = rule.MetricId, Content = rule.MetricId + " (unavailable)" });
        rules = loaded;
        ReloadList();
        result.Text = monitor.GetProperty("running").GetBoolean() ? "Monitor running." : "Monitor stopped.";
        if (!string.IsNullOrEmpty(savedId) && monitor.TryGetProperty("pc_alert_capability", out var capability))
            result.Text += capability.GetString() == "confirmed"
                ? " Cody severity support confirmed."
                : " Cody severity support not confirmed; Cody may be offline or need an update.";
        if (monitor.TryGetProperty("last_delivery_error", out var error) && error.ValueKind == JsonValueKind.String)
            result.Text += " Delivery: " + error.GetString();
        if (monitor.TryGetProperty("config_error", out var configError) && configError.ValueKind == JsonValueKind.String)
            result.Text += " Settings: " + configError.GetString();
        if (monitor.TryGetProperty("issues", out var issues) && issues.ValueKind == JsonValueKind.Array)
            foreach (var issue in issues.EnumerateArray())
                result.Text += "\n" + issue.GetProperty("source").GetString() + ": " + issue.GetProperty("message").GetString();
    }

    // No parameters: replaces the visible rule list without mutating the broker.
    // Exception: Native list operations may report UI errors.
    private void ReloadList() { list.ItemsSource = null; list.ItemsSource = rules.ToArray(); }

    // id: Exact metric identity, independent of display label.
    // Exception: Does not intentionally throw.
    private ComboBoxItem? MetricItem(string id) => metrics.Items.OfType<ComboBoxItem>().FirstOrDefault(item => (string?)item.Tag == id);

    // No parameters: loads the exact selected rule into the editor.
    // Exception: Does not intentionally throw.
    private void ShowSelected()
    {
        if (list.SelectedItem is not Rule rule) return;
        title.Text = rule.Title; metrics.SelectedItem = MetricItem(rule.MetricId);
        condition.SelectedItem = rule.Condition; severity.SelectedItem = rule.Severity;
        threshold.Text = rule.Threshold.ToString(CultureInfo.InvariantCulture);
        duration.Text = rule.Duration.ToString(CultureInfo.InvariantCulture);
        repeat.Text = rule.Repeat?.ToString(CultureInfo.InvariantCulture) ?? "";
        reset.Text = rule.Reset?.ToString(CultureInfo.InvariantCulture) ?? "";
        enabled.IsChecked = rule.Enabled;
    }

    // No parameters: prepares an unsaved rule without changing other rules.
    // Exception: Does not intentionally throw.
    private void ClearEditor()
    {
        list.SelectedItem = null; title.Text = ""; metrics.SelectedItem = null;
        threshold.Text = ""; duration.Text = "0"; repeat.Text = ""; reset.Text = "";
        condition.SelectedIndex = 0; severity.SelectedIndex = 0; enabled.IsChecked = true;
    }

    // No parameters: event-count rules do not use thresholds or timing fields.
    // Exception: Does not intentionally throw.
    private void UpdateConditionFields()
    {
        var numeric = (string?)condition.SelectedItem != "increased";
        threshold.IsEnabled = numeric; duration.IsEnabled = numeric; repeat.IsEnabled = numeric; reset.IsEnabled = numeric;
    }

    // value: Numeric text entered by the user.
    // name: Human-readable field name for validation errors.
    // Exception: Throws when the number is missing, non-finite or malformed.
    private static double Number(string value, string name)
    {
        if (!double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var number) || !double.IsFinite(number))
            throw new ArgumentException(name + " must be a finite number using a decimal point.");
        return number;
    }

    // No parameters: updates the draft only; Save publishes the complete ruleset.
    // Exception: Reports invalid or missing form values.
    private void ApplyRule()
    {
        if (string.IsNullOrWhiteSpace(title.Text) || metrics.SelectedItem is not ComboBoxItem metricItem || metricItem.Tag is not string metric ||
            condition.SelectedItem is not string comparison || severity.SelectedItem is not string level)
            throw new ArgumentException("Choose a name, metric, condition and severity.");
        var previous = list.SelectedItem as Rule;
        var isEvent = comparison == "increased";
        var draft = new Rule { Id = previous?.Id ?? Guid.NewGuid().ToString("N"), Title = title.Text.Trim(),
            MetricId = metric, Condition = comparison, Severity = level, Threshold = isEvent ? 0 : Number(threshold.Text, "Threshold"),
            Duration = isEvent ? 0 : Number(duration.Text, "Duration"), Repeat = isEvent || string.IsNullOrWhiteSpace(repeat.Text) ? null : Number(repeat.Text, "Repeat"),
            Reset = isEvent || string.IsNullOrWhiteSpace(reset.Text) ? null : Number(reset.Text, "Reset threshold"), Enabled = enabled.IsChecked == true };
        if (previous is null) rules.Add(draft); else rules[rules.IndexOf(previous)] = draft;
        ReloadList(); list.SelectedItem = draft; result.Text = "Draft updated. Select Save to apply.";
    }

    // No parameters: removes the selected rule from the local draft.
    // Exception: Reports when no rule is selected.
    private void DeleteRule()
    {
        if (list.SelectedItem is not Rule rule) throw new ArgumentException("Select a rule first.");
        rules.Remove(rule); ReloadList(); ClearEditor(); result.Text = "Draft updated. Select Save to apply.";
    }

    // No parameters: copies the selected rule with a new stable identity.
    // Exception: Reports when no rule is selected.
    private void DuplicateRule()
    {
        if (list.SelectedItem is not Rule rule) throw new ArgumentException("Select a rule first.");
        var copy = new Rule { Id = Guid.NewGuid().ToString("N"), Title = rule.Title + " copy",
            MetricId = rule.MetricId, Condition = rule.Condition, Threshold = rule.Threshold, Severity = rule.Severity,
            Duration = rule.Duration, Repeat = rule.Repeat, Reset = rule.Reset, Enabled = rule.Enabled };
        rules.Add(copy); ReloadList(); list.SelectedItem = copy; result.Text = "Draft updated. Select Save to apply.";
    }

    // No parameters: submits one explicit full configuration with a selected physical Cody.
    // Exception: Preserves broker validation or persistence failures.
    private async Task SaveAsync()
    {
        if (devices.SelectedItem is not string device) throw new ArgumentException("Select a physical Cody first.");
        await invoke("monitor/configure", new { device_id = device, rules });
        result.Text = "Alert configuration saved.";
    }

    // operation: Start or stop chosen explicitly by the user.
    // Exception: Preserves the backend operation failure.
    private async Task OperationAsync(string operation)
    {
        await invoke(operation, new { });
        result.Text = operation == "monitor/start" ? "Monitor started." : "Monitor stopped.";
    }
}
