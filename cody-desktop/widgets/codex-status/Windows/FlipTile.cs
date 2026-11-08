using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Animation;
using Microsoft.UI.Xaml.Shapes;

namespace OpenAIUsage.Windows;

public sealed class FlipTile : StackPanel
{
    private readonly TextBlock number;
    private readonly Border face;
    private readonly TextBlock caption;
    private readonly Grid overlays = new();
    private Storyboard? running;
    private string current = "--";
    public FlipTile(string label)
    {
        Spacing = 7;
        HorizontalAlignment = HorizontalAlignment.Stretch;
        var surface = new Grid();
        surface.Background = new LinearGradientBrush
        {
            StartPoint = new(0, 0), EndPoint = new(0, 1),
            GradientStops = { new() { Color = ColorHelper.FromArgb(255, 34, 37, 40), Offset = 0 }, new() { Color = ColorHelper.FromArgb(255, 12, 14, 16), Offset = 1 } }
        };
        number = new TextBlock { Text = current, FontSize = 57, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
            Foreground = new SolidColorBrush(Colors.FloralWhite), HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Center, FontFamily = new FontFamily("Consolas") };
        surface.Children.Add(number);
        surface.Children.Add(new Rectangle { Height = 2, Fill = new SolidColorBrush(Colors.Black), VerticalAlignment = VerticalAlignment.Center, IsHitTestVisible = false });
        foreach (var side in new[] { HorizontalAlignment.Left, HorizontalAlignment.Right })
            surface.Children.Add(new Border { Width = 4, Height = 14, Background = new SolidColorBrush(Colors.DimGray), HorizontalAlignment = side, VerticalAlignment = VerticalAlignment.Center });
        surface.Children.Add(overlays);
        face = new Border { Child = surface, Height = 100, CornerRadius = new(7), BorderThickness = new(1), BorderBrush = new SolidColorBrush(Colors.Black) };
        Children.Add(face);
        caption=new TextBlock { Text = label, FontSize = 10, CharacterSpacing = 100, Opacity = .7, HorizontalAlignment = HorizontalAlignment.Center };
        Children.Add(caption);
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(this, label);
    }

    public void SetCompact(bool compact) { caption.FontSize=compact?23:10; caption.CharacterSpacing=compact?0:100; Spacing=compact?5:7; }

    public void Set(string value, bool animate)
    {
        if (value == current) return;
        var previous = current;
        current = value;
        number.Text = value;
        running?.Stop();
        overlays.Children.Clear();
        if (!animate || face.ActualWidth <= 0 || previous == "--") return;
        // The old top rotates into the hinge, then the new bottom unfolds from it.
        var oldBottom = Half(previous, false);
        var oldTop = Half(previous, true);
        var newBottom = Half(value, false);
        var upperProjection = new PlaneProjection { CenterOfRotationY = 1 };
        var lowerProjection = new PlaneProjection { CenterOfRotationY = 0, RotationX = 90 };
        oldTop.Projection = upperProjection;
        newBottom.Projection = lowerProjection;
        overlays.Children.Add(oldBottom); overlays.Children.Add(oldTop); overlays.Children.Add(newBottom);
        var upper = new DoubleAnimation { From = 0, To = -90, Duration = new(TimeSpan.FromMilliseconds(100)), EnableDependentAnimation = true };
        var lower = new DoubleAnimation { From = 90, To = 0, BeginTime = TimeSpan.FromMilliseconds(100), Duration = new(TimeSpan.FromMilliseconds(100)), EnableDependentAnimation = true };
        Storyboard.SetTarget(upper, upperProjection); Storyboard.SetTargetProperty(upper, "RotationX");
        Storyboard.SetTarget(lower, lowerProjection); Storyboard.SetTargetProperty(lower, "RotationX");
        var storyboard = new Storyboard();
        storyboard.Children.Add(upper); storyboard.Children.Add(lower);
        storyboard.Completed += (_, _) => overlays.Children.Clear();
        running = storyboard;
        storyboard.Begin();
    }

    private Grid Half(string value, bool top)
    {
        double width = Math.Max(1, face.ActualWidth - 2);
        var half = new Grid { Width = width, Height = 49, VerticalAlignment = top ? VerticalAlignment.Top : VerticalAlignment.Bottom,
            Clip = new RectangleGeometry { Rect = new(0, 0, width, 49) } };
        var full = new Grid { Width = width, Height = 98, VerticalAlignment = VerticalAlignment.Top,
            Background = new SolidColorBrush(top ? ColorHelper.FromArgb(255, 31, 34, 37) : ColorHelper.FromArgb(255, 16, 18, 20)),
            RenderTransform = new TranslateTransform { Y = top ? 0 : -49 } };
        full.Children.Add(new TextBlock { Text = value, FontSize = 57, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
            Foreground = new SolidColorBrush(Colors.FloralWhite), HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Center, FontFamily = new FontFamily("Consolas") });
        half.Children.Add(full);
        return half;
    }
}
