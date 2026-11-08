using Microsoft.UI;
using Microsoft.UI.Composition;
using Microsoft.UI.Composition.SystemBackdrops;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;

namespace OpenAIUsage.Windows;

// Each window owns its controller. System policy still controls fallback/high contrast.
public sealed class WidgetAcrylicBackdrop : SystemBackdrop
{
    public static bool IsSupported => DesktopAcrylicController.IsSupported();
    private DesktopAcrylicController? controller;

    protected override void OnTargetConnected(ICompositionSupportsSystemBackdrop target, XamlRoot xamlRoot)
    {
        base.OnTargetConnected(target, xamlRoot);
        if (!IsSupported) return;
        var configuration = GetDefaultSystemBackdropConfiguration(target, xamlRoot);
        controller = new DesktopAcrylicController();
        controller.SetSystemBackdropConfiguration(configuration);
        ApplyAppearance(configuration.Theme);
        controller.AddSystemBackdropTarget(target);
        Diagnostics.Record("acrylic.ready", "Native desktop acrylic connected; transparent XAML surface.");
    }

    protected override void OnDefaultSystemBackdropConfigurationChanged(ICompositionSupportsSystemBackdrop target, XamlRoot xamlRoot)
    {
        base.OnDefaultSystemBackdropConfigurationChanged(target, xamlRoot);
        ApplyAppearance(GetDefaultSystemBackdropConfiguration(target, xamlRoot).Theme);
    }

    private void ApplyAppearance(SystemBackdropTheme theme)
    {
        if (controller is null) return;
        bool dark = theme == SystemBackdropTheme.Dark;
        controller.TintColor = dark ? ColorHelper.FromArgb(255, 26, 34, 43) : ColorHelper.FromArgb(255, 239, 247, 252);
        controller.FallbackColor = dark ? ColorHelper.FromArgb(255, 26, 34, 43) : ColorHelper.FromArgb(255, 243, 248, 252);
        controller.TintOpacity = dark ? .55f : .6f;
        controller.LuminosityOpacity = .65f;
    }

    protected override void OnTargetDisconnected(ICompositionSupportsSystemBackdrop target)
    {
        controller?.RemoveSystemBackdropTarget(target);
        controller?.Dispose();
        controller = null;
        base.OnTargetDisconnected(target);
    }
}
