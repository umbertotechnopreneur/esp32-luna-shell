using Windows.UI.Notifications;
using Windows.UI.Notifications.Management;

namespace OpenAIUsage.Windows;

// Carries application identity only; notification contents are deliberately not read.
public sealed record WindowsNotificationApplication(string Id, string Name);

public sealed class WindowsNotificationSource : IDisposable
{
    private readonly UserNotificationListener listener = UserNotificationListener.Current;
    private readonly object gate = new();
    private HashSet<string> enabledApplications = new(StringComparer.OrdinalIgnoreCase);
    private bool subscribed;

    public event Action<WindowsNotificationApplication>? NotificationAdded;
    public event Action<Exception>? ListenerFailed;

    // No parameters: attach only when access has already been granted; never display a consent prompt.
    // Exception: Propagates Windows capability and package identity failures.
    public bool StartIfAllowed()
    {
        if (listener.GetAccessStatus() != UserNotificationListenerAccessStatus.Allowed) return false;
        if (!subscribed)
        {
            listener.NotificationChanged += OnNotificationChanged;
            subscribed = true;
        }
        return true;
    }

    // No parameters: the caller must invoke this from the UI thread after the user requests access.
    // Exception: Propagates Windows capability, package identity and permission failures.
    public async Task<UserNotificationListenerAccessStatus> RequestAccessAsync()
    {
        var status = await listener.RequestAccessAsync();
        if (status == UserNotificationListenerAccessStatus.Allowed) StartIfAllowed();
        return status;
    }

    // No parameters: returns applications represented in current Windows notifications.
    // Exception: Propagates Windows listener errors; an empty result does not imply access was granted.
    public async Task<IReadOnlyList<WindowsNotificationApplication>> AvailableApplicationsAsync()
    {
        if (listener.GetAccessStatus() != UserNotificationListenerAccessStatus.Allowed)
            throw new InvalidOperationException("Windows notification access is not enabled.");
        var notifications = await listener.GetNotificationsAsync(NotificationKinds.Toast);
        return notifications.Select(item => new WindowsNotificationApplication(
                item.AppInfo.AppUserModelId, item.AppInfo.DisplayInfo.DisplayName))
            .Where(item => !string.IsNullOrWhiteSpace(item.Id))
            .DistinctBy(item => item.Id, StringComparer.OrdinalIgnoreCase)
            .OrderBy(item => item.Name, StringComparer.CurrentCultureIgnoreCase).ToArray();
    }

    // applicationIds: Explicit application identities selected by the user; an empty set disables forwarding.
    // Exception: Throws ArgumentNullException if the selection is absent.
    public void SetEnabledApplications(IEnumerable<string> applicationIds)
    {
        ArgumentNullException.ThrowIfNull(applicationIds);
        var selected = new HashSet<string>(applicationIds.Where(id => !string.IsNullOrWhiteSpace(id)),
            StringComparer.OrdinalIgnoreCase);
        lock (gate) enabledApplications = selected;
    }

    // sender: The Windows listener reporting an added or removed notification.
    // args: Notification change and its Windows-assigned identifier.
    // Exception: Listener and subscriber failures are reported through ListenerFailed.
    private void OnNotificationChanged(UserNotificationListener sender, UserNotificationChangedEventArgs args)
    {
        if (args.ChangeKind != UserNotificationChangedKind.Added) return;
        try
        {
            if (sender.GetAccessStatus() != UserNotificationListenerAccessStatus.Allowed) return;
            var notification = sender.GetNotification(args.UserNotificationId);
            if (notification is null) return;
            var app = notification.AppInfo;
            lock (gate)
                if (!enabledApplications.Contains(app.AppUserModelId)) return;
            NotificationAdded?.Invoke(new WindowsNotificationApplication(app.AppUserModelId, app.DisplayInfo.DisplayName));
        }
        catch (Exception error) { ListenerFailed?.Invoke(error); }
    }

    // No parameters: stops observing Windows notifications without changing the user's permission.
    public void Dispose()
    {
        if (!subscribed) return;
        listener.NotificationChanged -= OnNotificationChanged;
        subscribed = false;
    }
}
