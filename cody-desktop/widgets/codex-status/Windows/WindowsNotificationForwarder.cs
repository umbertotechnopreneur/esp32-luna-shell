using Windows.UI.Notifications.Management;

namespace OpenAIUsage.Windows;

// Owned by the widget, not by the short-lived settings window.
public sealed class WindowsNotificationForwarder : IDisposable
{
    private readonly WindowsNotificationSource source = new();
    private readonly object gate = new();
    private readonly Queue<(string Device, CodyBridgeSettings Bridge, long Generation)> pending = new();
    private readonly CancellationTokenSource lifetime = new();
    private string device = "";
    private CodyBridgeSettings bridge = new();
    private long generation;
    private bool working;
    private bool enabled;
    public string? LastError { get; private set; }
    public event Action<string>? Failed;
    public event Action? Configured;

    // No parameters: attach callbacks for the lifetime of the widget.
    public WindowsNotificationForwarder()
    {
        source.NotificationAdded += OnNotification;
        source.ListenerFailed += error => Report("Windows notification listener: " + error.Message);
    }

    // No parameters: request Windows consent on the UI thread in response to the user's action.
    // Exception: Propagates Windows capability, package identity and permission errors.
    public Task<UserNotificationListenerAccessStatus> RequestAccessAsync() => source.RequestAccessAsync();

    // No parameters: list currently visible Windows notification sources.
    // Exception: Propagates Windows access errors.
    public Task<IReadOnlyList<WindowsNotificationApplication>> AvailableApplicationsAsync() => source.AvailableApplicationsAsync();

    // settings: Saved, user-selected source identities and exact physical Cody destination.
    // Exception: Propagates listener and configuration failures.
    public void Apply(WidgetSettings settings)
    {
        ArgumentNullException.ThrowIfNull(settings);
        bool shouldEnable = settings.WindowsNotificationsEnabled;
        if (shouldEnable && (string.IsNullOrWhiteSpace(settings.WindowsNotificationDeviceId) ||
            settings.WindowsNotificationApplicationIds is null || settings.WindowsNotificationApplicationIds.Count == 0))
            throw new ArgumentException("Choose a physical Cody and at least one application.");
        if (shouldEnable && (string.IsNullOrWhiteSpace(settings.CodyBridge.ControlScriptPath) || !File.Exists(settings.CodyBridge.ControlScriptPath)))
            throw new IOException("Choose the installed companion service launcher before enabling forwarding.");
        if (shouldEnable && !source.StartIfAllowed())
            throw new InvalidOperationException("Windows notification access is not enabled. Open Cody Setup to request access.");
        source.SetEnabledApplications([]);
        lock (gate)
        {
            generation++;
            pending.Clear();
            device = settings.WindowsNotificationDeviceId;
            bridge = new CodyBridgeSettings { PythonExecutable = settings.CodyBridge.PythonExecutable, ControlScriptPath = settings.CodyBridge.ControlScriptPath };
            enabled = shouldEnable;
            LastError = null;
        }
        source.SetEnabledApplications(shouldEnable ? settings.WindowsNotificationApplicationIds : []);
        Configured?.Invoke();
    }

    // application: Identity matched locally; only a generic event is forwarded to the device.
    private void OnNotification(WindowsNotificationApplication application)
    {
        lock (gate)
        {
            if (!enabled) return;
            if (pending.Count >= 32) { Report("Windows notification queue is full; an alert was not forwarded."); return; }
            pending.Enqueue((device, bridge, generation));
            if (working) return;
            working = true;
        }
        _ = DrainAsync();
    }

    // No parameters: sequentially admit each distinct event once and never retry ambiguous sends.
    private async Task DrainAsync()
    {
        while (true)
        {
            (string Device, CodyBridgeSettings Bridge, long Generation) item;
            lock (gate)
            {
                if (!enabled || pending.Count == 0) { working = false; return; }
                item = pending.Dequeue();
                if (item.Generation != generation) continue;
            }
            try
            {
                var result = await CodyBridgeClient.CallAsync(item.Bridge, "device/notify-system",
                    new { device_id = item.Device, @event = "notification" }, lifetime.Token);
                if (!result.TryGetProperty("request_id", out _) || !result.TryGetProperty("state", out _))
                    throw new IOException("Cody did not confirm notification admission.");
            }
            catch (OperationCanceledException) when (lifetime.IsCancellationRequested) { return; }
            catch (Exception error) { Report("Cody notification: " + error.Message); }
        }
    }

    // message: User-visible failure; no notification content or app name is logged.
    private void Report(string message)
    {
        lock (gate) LastError = message;
        Failed?.Invoke(message);
    }

    // No parameters: stop forwarding when the widget exits.
    public void Dispose()
    {
        lock (gate) { enabled = false; pending.Clear(); generation++; }
        source.Dispose();
        lifetime.Cancel();
        lifetime.Dispose();
    }
}
