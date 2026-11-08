using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using System.Diagnostics;
using System.Text.Json;

namespace OpenAIUsage.Windows;

public sealed class CodyControlPanel : UserControl
{
    private readonly StackPanel layout = new() { Spacing = 14 };
    private UIElementCollection Children => layout.Children;
    private readonly Storage storage;
    private readonly WidgetSettings settings;
    private readonly CancellationTokenSource lifetime;
    private readonly DispatcherTimer poll = new() { Interval = TimeSpan.FromSeconds(5) };
    private readonly List<Button> commands = [];
    private readonly TextBlock source = Label("Source unavailable", 22);
    private readonly TextBlock status = Label("Choose the companion service launcher, then refresh.");
    private readonly TextBlock result = Label("");
    private readonly ComboBox devices = new() { Header = "Physical Cody", HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly TextBox interpreter;
    private readonly TextBox launcher;
    private readonly TextBox pairing = new() { Header = "Six-character code shown on Cody", MaxLength = 6 };
    private readonly TextBox preferredName = new() { Header = "Preferred name", MaxLength = 24 };
    private readonly ComboBox timezone = new() { Header = "Device time zone", HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly CheckBox quiet = new() { Content = "Quiet hours" };
    private readonly TextBox quietStart = new() { Header = "Start (HH:MM)", MaxLength = 5 };
    private readonly TextBox quietEnd = new() { Header = "End (HH:MM)", MaxLength = 5 };
    private bool busy;
    private bool clockLoaded;
    private bool disposed;
    private CodyBridgeStatus? lastStatus;
    private long statusReceivedAt;

    // storage: Existing local settings owner; account observations remain independent.
    // settings: Shared widget settings, updated only after an explicit save.
    // cancellationToken: Widget lifetime; closing the panel cancels only its control requests.
    public CodyControlPanel(Storage storage, WidgetSettings settings, CancellationToken cancellationToken)
    {
        this.storage = storage;
        this.settings = settings;
        lifetime = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        Content = layout;
        interpreter = new() { Header = "Python executable", Text = settings.CodyBridge.PythonExecutable };
        launcher = new() { Header = "Companion service launcher (ccp_server.py)", Text = settings.CodyBridge.ControlScriptPath };
        Children.Add(source);
        Children.Add(status);
        Children.Add(Label("Cody follows connected Codex sessions and works locally when Codex is unavailable."));
        Children.Add(Row(Command("Refresh", RefreshStatusAsync), Command("Start companion", () => ControlAsync("bridge/start", new { })),
            Command("Stop companion", () => ControlAsync("bridge/stop", new { }))));
        Children.Add(Label("Keep this widget open or hidden to tray while Codex is closed. Exiting the widget can stop its companion service."));

        var setup = new StackPanel { Spacing = 10 };
        setup.Children.Add(interpreter);
        setup.Children.Add(launcher);
        setup.Children.Add(Command("Save launcher", SaveLauncherAsync));
        Children.Add(new Expander { Header = "Companion service setup", Content = setup,
            IsExpanded = string.IsNullOrWhiteSpace(settings.CodyBridge.ControlScriptPath), HorizontalAlignment = HorizontalAlignment.Stretch });

        Children.Add(devices);
        // Selection is an exact retained identity. Never select a different device after refresh.
        devices.SelectionChanged += (_, _) => { clockLoaded = false; preferredName.Text = ""; timezone.SelectedIndex = -1; RenderSourceStatus(); };
        Children.Add(Row(Command("Find Cody", () => ControlAsync("device/discover", new { include_simulator = false })),
            Command("Read preferences", ReadPreferencesAsync), Command("Normal screen", () => DeviceControlAsync("device/normal"))));
        Children.Add(pairing);
        Children.Add(Command("Pair this code", PairAsync));
        Children.Add(preferredName);
        Children.Add(Command("Save name", () => DeviceControlAsync("device/set-name", new() { ["preferred_name"] = preferredName.Text })));
        foreach (var zone in new[] { "UTC", "Europe/Rome", "Asia/Ho_Chi_Minh" }) timezone.Items.Add(zone);
        Children.Add(timezone);
        Children.Add(quiet);
        Children.Add(quietStart);
        Children.Add(quietEnd);
        Children.Add(Command("Save clock preferences", SaveClockAsync));

        Children.Add(result);
        poll.Tick += async (_, _) => { RenderSourceStatus(); await RunAsync(RefreshStatusAsync); };
        Loaded += async (_, _) => { poll.Start(); await RunAsync(RefreshStatusAsync); };
        Unloaded += (_, _) => { disposed = true; poll.Stop(); lifetime.Cancel(); lifetime.Dispose(); };
    }

    // text: Visible native caption.
    // size: Font size in device-independent pixels.
    private static TextBlock Label(string text, double size = 13) => new()
        { Text = text, FontSize = size, TextWrapping = TextWrapping.Wrap };

    // buttons: Parallel actions shown without changing their independent operation semantics.
    private static StackPanel Row(params Button[] buttons)
    {
        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        foreach (var button in buttons) row.Children.Add(button);
        return row;
    }

    // caption: Explicit user action, including every mutation.
    // action: Single operation; failures remain visible and are never retried.
    private Button Command(string caption, Func<Task> action)
    {
        var button = new Button { Content = caption };
        button.Click += async (_, _) => await RunAsync(action);
        commands.Add(button);
        return button;
    }

    // action: Operation serialized against the panel's refresh owner.
    // No exceptions: Show original control failures; cancellation leaves write completion unconfirmed.
    private async Task RunAsync(Func<Task> action)
    {
        if (busy || disposed) return;
        busy = true;
        foreach (var button in commands) button.IsEnabled = false;
        try { await action(); }
        catch (OperationCanceledException) { if (!disposed) result.Text = "Operation interrupted; refresh before repeating a change."; }
        catch (Exception error) when (error is IOException or ArgumentException or JsonException or InvalidOperationException or
            KeyNotFoundException or UnauthorizedAccessException or FormatException or OverflowException or System.ComponentModel.Win32Exception)
        {
            if (!disposed) result.Text = error.Message;
        }
        finally
        {
            busy = false;
            if (!disposed) foreach (var button in commands) button.IsEnabled = true;
        }
    }

    // No parameters: refresh only the actual bridge projection, without starting it.
    // Exception: Clears stale connection claims before propagating a failed read.
    private async Task RefreshStatusAsync()
    {
        var selected = (devices.SelectedItem as ComboBoxItem)?.Tag as string;
        CodyBridgeStatus observed;
        try { observed = await CodyBridgeClient.ReadStatusAsync(settings.CodyBridge, lifetime.Token); }
        catch
        {
            if (!disposed) { lastStatus = null; source.Text = "Source unavailable"; status.Text = "Companion status could not be read."; devices.Items.Clear(); clockLoaded = false; }
            throw;
        }
        if (disposed) return;
        lastStatus = observed;
        statusReceivedAt = Stopwatch.GetTimestamp();
        var physical = observed.Devices.Where(device => !device.Simulated).ToList();
        var current = devices.Items.OfType<ComboBoxItem>().Select(item => item.Tag as string).ToList();
        if (!current.SequenceEqual(physical.Select(device => device.DeviceId)))
        {
            devices.Items.Clear();
            foreach (var device in physical) devices.Items.Add(new ComboBoxItem { Tag = device.DeviceId });
            devices.SelectedItem = devices.Items.OfType<ComboBoxItem>().FirstOrDefault(item => (string?)item.Tag == selected);
        }
        for (int index = 0; index < physical.Count; index++)
            ((ComboBoxItem)devices.Items[index]).Content = physical[index].DeviceId + (physical[index].Connected ? " · Connected" : " · Unavailable");
        RenderSourceStatus();
    }

    // No parameters: select source metadata by exact device identity, never by list position.
    // No exceptions: A missing selection displays the clearly labeled service projection only.
    private void RenderSourceStatus()
    {
        if (lastStatus is not { } observed) return;
        var identity = (devices.SelectedItem as ComboBoxItem)?.Tag as string;
        var selected = observed.Devices.FirstOrDefault(device => device.DeviceId == identity);
        var current = selected?.ProviderSource ?? observed.Source;
        long? sourceAge = current.AgeMilliseconds is { } receivedAge
            ? receivedAge + (long)Stopwatch.GetElapsedTime(statusReceivedAt).TotalMilliseconds : null;
        var provider = current.Provider == CodySourceProvider.Local ? "Local companion" : current.Provider.ToString();
        bool stale = sourceAge is >= 120000;
        bool missingAge = current.Provider == CodySourceProvider.Codex && sourceAge is null;
        var stateText = stale ? "STALE" : missingAge ? "UNAVAILABLE" : current.State.ToString().ToLowerInvariant();
        source.Text = observed.Running ? provider + " · " + stateText : "Companion service stopped";
        status.Text = observed.Running
            ? (selected is null ? "Service source. " : "Source for Cody " + selected.DeviceId + ". ") + current.Detail + " " +
                (sourceAge is { } age ? $"Observed {age / 1000}s ago." : "No source timestamp available.")
            : "Start the companion service to connect Cody.";
    }

    // No parameters: save only launcher preferences, preserving every account setting.
    // Exception: Propagates validation/persistence failure without changing active configuration.
    private async Task SaveLauncherAsync()
    {
        var next = new CodyBridgeSettings { PythonExecutable = interpreter.Text.Trim(), ControlScriptPath = launcher.Text.Trim() };
        next.Validate();
        var previous = settings.CodyBridge;
        settings.CodyBridge = next;
        try { storage.SaveSettings(settings); }
        catch { settings.CodyBridge = previous; throw; }
        result.Text = "Companion launcher saved.";
        await RefreshStatusAsync();
    }

    // operation: Explicit bridge or discovery operation.
    // arguments: Public command payload, never a raw protocol packet.
    // Exception: Propagates the original control error; refresh is a read, not a retry.
    private async Task ControlAsync(string operation, object arguments)
    {
        var response = await CodyBridgeClient.CallAsync(settings.CodyBridge, operation, arguments, lifetime.Token);
        if (disposed) return;
        result.Text = DescribeResult(response);
        await RefreshStatusAsync();
    }

    // No parameters: require the explicitly selected physical identity.
    // Exception: Throws when no exact device was selected.
    private string SelectedDevice() => (devices.SelectedItem as ComboBoxItem)?.Tag as string
        ?? throw new ArgumentException("Select the intended physical Cody first.");

    // operation: Existing authenticated device command.
    // arguments: Only user-edited fields; omitted preferences remain owned by Cody.
    // Exception: Preserves rejected and unconfirmed outcomes; never replays a mutation.
    private async Task DeviceControlAsync(string operation, Dictionary<string, object>? arguments = null)
    {
        var payload = arguments ?? new();
        payload["device_id"] = SelectedDevice();
        await ControlAsync(operation, payload);
    }

    // No parameters: consume only the display code entered for this explicit pairing action.
    // Exception: The service rejects missing, ambiguous or expired offers without replacement permission.
    private async Task PairAsync()
    {
        var code = pairing.Text.Trim();
        if (code.Length != 6) throw new ArgumentException("Enter the six-character code currently shown on Cody.");
        pairing.Text = "";
        await ControlAsync("device/pair", new { code });
    }

    // No parameters: read preferences for one frozen selected identity.
    // Exception: Rejects unavailable acknowledgements instead of populating invented defaults.
    private async Task ReadPreferencesAsync()
    {
        var identity = SelectedDevice();
        clockLoaded = false;
        var name = await CodyBridgeClient.CallAsync(settings.CodyBridge, "device/read-name", new { device_id = identity }, lifetime.Token);
        RequireApplied(name);
        var clock = await CodyBridgeClient.CallAsync(settings.CodyBridge, "device/read-clock", new { device_id = identity }, lifetime.Token);
        RequireApplied(clock);
        if (disposed || SelectedDevice() != identity) return;
        preferredName.Text = name.GetProperty("preferred_name").GetString() ?? "";
        timezone.SelectedItem = clock.GetProperty("timezone").GetString();
        quiet.IsChecked = clock.GetProperty("quiet_hours_enabled").GetBoolean();
        quietStart.Text = clock.GetProperty("quiet_hours_start").GetString() ?? "";
        quietEnd.Text = clock.GetProperty("quiet_hours_end").GetString() ?? "";
        clockLoaded = true;
        result.Text = "Preferences read from Cody.";
    }

    // No parameters: apply the explicitly edited complete clock form after a successful read.
    // Exception: Requires current selected-device preferences rather than guessing quiet hours.
    private async Task SaveClockAsync()
    {
        if (!clockLoaded || timezone.SelectedItem is not string zone)
            throw new ArgumentException("Read this Cody's preferences before editing its clock.");
        await DeviceControlAsync("device/configure-clock", new()
        {
            ["timezone"] = zone, ["quiet_hours_enabled"] = quiet.IsChecked == true,
            ["quiet_hours_start"] = quietStart.Text.Trim(), ["quiet_hours_end"] = quietEnd.Text.Trim()
        });
    }

    // response: Native authenticated operation outcome returned unchanged by the service.
    // Exception: Requires an applied ACK with error zero before accepting readback values.
    private static void RequireApplied(JsonElement response)
    {
        if (!response.TryGetProperty("status", out var status) || status.GetString() != "applied" ||
            !response.TryGetProperty("error", out var error) || !error.TryGetInt32(out var code) || code != 0)
            throw new IOException(DescribeResult(response));
    }

    // response: Public service result; output contains outcome metadata only, never a token or request payload.
    private static string DescribeResult(JsonElement response)
    {
        if (response.TryGetProperty("status", out var status))
        {
            var value = status.GetString();
            var failure = response.TryGetProperty("error", out var error) ? error.ToString() : "";
            if (value == "applied" && failure == "0") return "Cody acknowledged the change. Physical behavior has not been verified.";
            return "Cody result: " + value + (failure.Length > 0 ? " · " + failure : "") +
                (value is "ambiguous" or "unconfirmed" ? ". Read status before repeating the action." : "");
        }
        if (response.TryGetProperty("count", out var count)) return "Pairing offers found: " + count + ". Use the code on the intended physical Cody.";
        if (response.TryGetProperty("device_id", out var identity)) return "Pairing confirmed for Cody " + identity + ".";
        return "Companion service request completed. Refresh shows the current observed state.";
    }
}
