using Microsoft.UI;
using Microsoft.UI.Input;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using System.Runtime.InteropServices;
using Tools.Widgets;
using Windows.ApplicationModel;
using Windows.UI.ViewManagement;

namespace OpenAIUsage.Windows;

public sealed class MainWindow : Window
{
    private const string StartupTaskId = "OpenAIUsageStartup";
    private static readonly SolidColorBrush Accent=new(), PrimaryText=new(), SecondaryText=new(), WarningText=new();
    private static readonly FontFamily MetricFont=new("Cascadia Mono, Consolas");
    private static readonly LinearGradientBrush ValueGradient=new() { StartPoint=new(0,0), EndPoint=new(1,.25), GradientStops={new(),new(){Offset=1}} };
    private readonly Storage storage=new();
    private WidgetSettings settings;
    private WidgetState state=new();
    private readonly Grid root=new() { Background=new SolidColorBrush(Colors.Transparent) };
    private readonly Grid strip=new() { Width=819, ColumnSpacing=16, Margin=new(14,8,14,8) };
    private readonly StackPanel actions=new() { Orientation=Orientation.Horizontal, Spacing=0, VerticalAlignment=VerticalAlignment.Center };
    private readonly TextBlock allowance=Text("Weekly limit",15), remaining=Text("—",26,true), todayTokens=Text("—",20,true), credits=Text("—",20,true);
    private readonly FlipTile[] tiles=[new("DAYS"),new("HOURS"),new("MINUTES"),new("SECONDS")];
    private readonly ToggleButtonCompat pin=new();
    private readonly Button connectionError=Button("\uE7BA","Connection settings");
    private readonly UISettings systemSettings=new();
    private readonly DispatcherTimer clock=new(){Interval=TimeSpan.FromSeconds(1)}, refresh=new(){Interval=TimeSpan.FromSeconds(60)};
    private readonly CancellationTokenSource lifetime=new();
    private readonly SemaphoreSlim dialogGate=new(1,1);
    private InputNonClientPointerSource? nonClientInput;
    private TrayIcon? tray;
    private Window? activeDialog;
    private bool busy, closed, allowClose, closingPrompt;
    private DateTimeOffset lastResetRefresh=DateTimeOffset.MinValue;

    public MainWindow()
    {
        try { settings=storage.LoadSettings(); }
        catch(Exception error) { settings=new(); Diagnostics.Record("settings.failed",error.Message); }
        Title="OpenAI Usage";
        AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory,"Branding","app-icon.ico"));
        if(AppWindow.Presenter is OverlappedPresenter presenter)
        {
            presenter.SetBorderAndTitleBar(true,false);
            presenter.IsMinimizable=false; presenter.IsMaximizable=false; presenter.IsResizable=false;
        }
        if(WidgetAcrylicBackdrop.IsSupported)SystemBackdrop=new WidgetAcrylicBackdrop();
        ApplySystemTheme();
        systemSettings.ColorValuesChanged+=ThemeChanged;
        strip.ColumnDefinitions.Add(new(){Width=new(28)});
        strip.ColumnDefinitions.Add(new(){Width=new(1,GridUnitType.Star)});
        strip.ColumnDefinitions.Add(new(){Width=GridLength.Auto});
        var logo=Logo(28); logo.VerticalAlignment=VerticalAlignment.Center; strip.Children.Add(logo);
        var data=new Grid { ColumnSpacing=18, VerticalAlignment=VerticalAlignment.Center };
        data.ColumnDefinitions.Add(new(){Width=new(1,GridUnitType.Star)}); data.ColumnDefinitions.Add(new(){Width=new(200)});
        var summary=new Grid { ColumnSpacing=16, VerticalAlignment=VerticalAlignment.Center };
        for(int i=0;i<3;i++)summary.ColumnDefinitions.Add(new(){Width=new(1,GridUnitType.Star)});
        var quota=new StackPanel { Spacing=7 }; allowance.Foreground=SecondaryText; quota.Children.Add(allowance); quota.Children.Add(remaining); summary.Children.Add(quota);
        var today=Metric("Today tokens",todayTokens); Grid.SetColumn(today,1); summary.Children.Add(today);
        var credit=Metric("Remaining credits",credits); Grid.SetColumn(credit,2); summary.Children.Add(credit);
        data.Children.Add(summary);
        var tileRow=new Grid { ColumnSpacing=8, Width=400 };
        for(int i=0;i<4;i++) { tiles[i].SetCompact(true); tileRow.ColumnDefinitions.Add(new(){Width=new(1,GridUnitType.Star)}); Grid.SetColumn(tiles[i],i); tileRow.Children.Add(tiles[i]); }
        var clockView=new Viewbox { Child=tileRow, Stretch=Stretch.Uniform, MaxWidth=200 };
        Grid.SetColumn(clockView,1); data.Children.Add(clockView);
        Grid.SetColumn(data,1); strip.Children.Add(data);
        connectionError.Visibility=Visibility.Collapsed; connectionError.Click+=async(_,_)=>await ShowSettingsAsync(); actions.Children.Add(connectionError);
        pin.Content=new FontIcon { Glyph="\uE718", FontSize=15, Foreground=PrimaryText };
        pin.MinWidth=26; pin.Padding=new(5); pin.Background=new SolidColorBrush(Colors.Transparent); pin.BorderThickness=new(0);
        foreach(var key in new[]{"ToggleButtonBackground","ToggleButtonBackgroundPointerOver","ToggleButtonBackgroundPressed","ToggleButtonBackgroundChecked","ToggleButtonBackgroundCheckedPointerOver","ToggleButtonBackgroundCheckedPressed"})pin.Resources[key]=new SolidColorBrush(Colors.Transparent);
        ToolTipService.SetToolTip(pin,"Always on top"); Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(pin,"Always on top");
        pin.IsChecked=settings.AlwaysOnTop; pin.Click+=(_,_)=>{settings.AlwaysOnTop=pin.IsChecked==true;ApplyTopmost();SaveSettings();UpdatePinVisual();}; actions.Children.Add(pin);
        var preferences=Button("\uE713","Settings"); preferences.Click+=async(_,_)=>await ShowSettingsAsync(); actions.Children.Add(preferences);
        var about=Button("\uE946","About OpenAI Usage"); about.Click+=async(_,_)=>await ShowAboutAsync(); actions.Children.Add(about);
        var close=Button("\uE8BB","Close"); close.Click+=(_,_)=>Close(); actions.Children.Add(close);
        Grid.SetColumn(actions,2); strip.Children.Add(actions);
        var view=new Viewbox { Child=strip, Stretch=Stretch.Uniform, VerticalAlignment=VerticalAlignment.Center };
        root.Children.Add(view); Content=root; ApplyTopmost(); UpdatePinVisual();
        root.SizeChanged+=(_,_)=>UpdateDragRegions(); actions.SizeChanged+=(_,_)=>UpdateDragRegions(); view.SizeChanged+=(_,_)=>UpdateDragRegions();
        clock.Tick+=(_,_)=>Tick(); refresh.Tick+=async(_,_)=>await RefreshAsync();
        root.Loaded+=async(_,_)=>
        {
            root.XamlRoot.Changed+=(_,_)=>UpdateDragRegions();
            double dpi=root.XamlRoot.RasterizationScale; AppWindow.Resize(new((int)(604*dpi),(int)(66*dpi)));
            try { tray=new TrayIcon(Path.Combine(AppContext.BaseDirectory,"Branding","app-icon.ico"),ShowWindow,
                ()=>{ShowWindow();_=ShowAboutAsync();},()=>{ShowWindow();_=ShowSettingsAsync();},
                ()=>OpenLink("https://umbertogiacobbi.biz"),()=>OpenLink("https://umbertogiacobbi.biz/vibeware/manifesto"),ExitApplication);
                Diagnostics.Record("tray.ready","Native notification icon registered."); }
            catch(Exception error) { Diagnostics.Record("tray.unavailable",error.Message); }
            ApplyTaskbarVisibility();
            clock.Start(); refresh.Start(); await RefreshAsync();
            DispatcherQueue.TryEnqueue(()=>{root.UpdateLayout();UpdateDragRegions();LogLayout();});
        };
        AppWindow.Closing+=AskToClose;
        Closed+=(_,_)=>{closed=true;activeDialog?.Close();tray?.Dispose();clock.Stop();refresh.Stop();lifetime.Cancel();systemSettings.ColorValuesChanged-=ThemeChanged;};
    }

    private async Task RefreshAsync()
    {
        if(busy || closed)return;
        busy=true;
        try
        {
            var next=await new UsageService(storage).RefreshAsync(settings,lifetime.Token);
            if(closed)return;
            if(state.Connected!=next.Connected || state.Status!=next.Status)
            {
                Diagnostics.Record(next.Connected?"connection.ready":"connection.failed",next.Status);
                if(!next.Connected)tray?.NotifyError(next.Status);
            }
            state=next;
            remaining.Text=state.Remaining is {} n?$"{n:0.#}%":"—";
            allowance.Text=state.Windows.FirstOrDefault(w=>w.Key==state.SelectedWindow)?.Minutes is 10080?"Weekly limit":"Allowance limit";
            todayTokens.Text=state.TodayTokensLabel; credits.Text=state.CreditsLabel;
            ToolTipService.SetToolTip(todayTokens,state.TodayTokensNote); ToolTipService.SetToolTip(credits,"Remaining credits: "+state.CreditsLabel);
            connectionError.Visibility=state.Connected?Visibility.Collapsed:Visibility.Visible; ToolTipService.SetToolTip(connectionError,state.Status);
            ApplyMetricColors(); Tick();
        }
        finally { busy=false; }
    }
    private void Tick()
    {
        long? seconds=state.ResetsAt is {} end?Math.Max(0,end-DateTimeOffset.UtcNow.ToUnixTimeSeconds()):null;
        string[] values=seconds is {} s?[$"{s/86400:00}",$"{s/3600%24:00}",$"{s/60%60:00}",$"{s%60:00}"]:["--","--","--","--"];
        for(int i=0;i<4;i++)tiles[i].Set(values[i],systemSettings.AnimationsEnabled);
        ToolTipService.SetToolTip(remaining,allowance.Text+": "+remaining.Text+" · "+state.ResetLabel);
        bool elapsed=state.ResetsAt is {} reset && reset<=DateTimeOffset.UtcNow.ToUnixTimeSeconds();
        if(elapsed && !busy && state.Connected && DateTimeOffset.UtcNow-lastResetRefresh>TimeSpan.FromMinutes(1)) {lastResetRefresh=DateTimeOffset.UtcNow;_=RefreshAsync();}
    }
    private void ApplyTopmost() {if(AppWindow.Presenter is OverlappedPresenter p)p.IsAlwaysOnTop=settings.AlwaysOnTop;}
    private void ApplyTaskbarVisibility() => AppWindow.IsShownInSwitchers = settings.ShowInTaskbar || tray?.IsAvailable != true;
    private void UpdatePinVisual() {if(pin.Content is FontIcon glyph)glyph.Foreground=pin.IsChecked==true?Accent:PrimaryText;}
    private void ShowWindow() {AppWindow.Show();if(activeDialog is not null)activeDialog.Activate();else Activate();}
    private void ExitApplication() {allowClose=true;Close();}
    private static void OpenLink(string url)
    {
        try {System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(url){UseShellExecute=true});}
        catch(Exception error){Diagnostics.Record("link.failed",error.GetType().Name);}
    }
    private void UpdateDragRegions()
    {
        if(closed || !root.IsLoaded || actions.ActualWidth<=0 || root.ActualWidth<=0)return;
        var bounds=actions.TransformToVisual(root).TransformBounds(new(0,0,actions.ActualWidth,actions.ActualHeight));
        double dpi=root.XamlRoot.RasterizationScale;
        int width=(int)Math.Round(root.ActualWidth*dpi),height=(int)Math.Round(root.ActualHeight*dpi),right=Math.Clamp((int)Math.Floor(bounds.X*dpi)-8,0,(int)Math.Round(root.ActualWidth*dpi));
        nonClientInput??=InputNonClientPointerSource.GetForWindowId(AppWindow.Id);
        nonClientInput.SetRegionRects(NonClientRegionKind.Caption,[new(0,0,right,height)]);
        nonClientInput.SetRegionRects(NonClientRegionKind.Passthrough,[new(right,0,width-right,height)]);
    }
    private void LogLayout()
    {
        var captions=nonClientInput?.GetRegionRects(NonClientRegionKind.Caption);
        Diagnostics.Record("window.layout",$"compactOnly=True; client={root.ActualWidth:0}x{root.ActualHeight:0}; toolbarActions={actions.Children.OfType<Control>().Count(c=>c.Visibility==Visibility.Visible)}; captionRegions={captions?.Length??0}; metricsClipped={remaining.IsTextTrimmed || todayTokens.IsTextTrimmed || credits.IsTextTrimmed}; acrylicSupported={WidgetAcrylicBackdrop.IsSupported}");
    }
    private async void AskToClose(AppWindow sender,AppWindowClosingEventArgs args)
    {
        if(allowClose)return;
        args.Cancel=true; if(closingPrompt || dialogGate.CurrentCount==0)return; closingPrompt=true;
        try
        {
            var choice=await ShowDialogAsync("OpenAI Usage · Close",
                Text(tray?.IsAvailable==true?"Keep monitoring in the notification area, or exit the app.":"The notification area is unavailable. You can exit or keep the widget open.",15),
                closeButtonText:"Cancel",primaryButtonText:"Hide to tray",secondaryButtonText:"Exit",isPrimaryEnabled:tray?.IsAvailable==true,width:500,height:280);
            if(choice==ContentDialogResult.Secondary)ExitApplication();
            else if(choice==ContentDialogResult.Primary && tray?.IsAvailable==true)AppWindow.Hide();
        }
        catch(Exception error){Diagnostics.Record("close.prompt.failed",error.Message);}
        finally{closingPrompt=false;}
    }
    private async Task<ContentDialogResult> ShowDialogAsync(string title,UIElement content,
        string closeButtonText="Close",string? primaryButtonText=null,string? secondaryButtonText=null,
        bool isPrimaryEnabled=true,Func<bool>? onPrimary=null,double width=500,double height=590,bool about=false)
    {
        if(!dialogGate.Wait(0))return ContentDialogResult.None;
        var host=new Window {Title=title};
        if(!about && WidgetAcrylicBackdrop.IsSupported)host.SystemBackdrop=new WidgetAcrylicBackdrop();
        var surface=new Grid {RequestedTheme=about?ElementTheme.Light:root.ActualTheme,Background=about?WidgetAbout.Paper():root.Background};
        surface.RowDefinitions.Add(new(){Height=new(about?0:44)});surface.RowDefinitions.Add(new(){Height=new(1,GridUnitType.Star)});surface.RowDefinitions.Add(new(){Height=GridLength.Auto});
        var titleBar=new StackPanel {Orientation=Orientation.Horizontal,Spacing=10,Margin=new(20,0,60,0),VerticalAlignment=VerticalAlignment.Center};
        if(!about){titleBar.Children.Add(Logo(22));titleBar.Children.Add(Text(title,16,true));surface.Children.Add(titleBar);}
        var scroll=new ScrollViewer {Content=content,Margin=about?new(24,24,24,12):new(24,12,24,16),HorizontalScrollBarVisibility=ScrollBarVisibility.Disabled,VerticalScrollBarVisibility=ScrollBarVisibility.Auto};
        if(about)scroll.HorizontalContentAlignment=HorizontalAlignment.Stretch;
        Grid.SetRow(scroll,1);surface.Children.Add(scroll);
        var buttons=new StackPanel {Orientation=Orientation.Horizontal,Spacing=10,HorizontalAlignment=HorizontalAlignment.Right,Margin=new(24,0,24,20)};
        Grid.SetRow(buttons,2);surface.Children.Add(buttons);
        var loaded=new TaskCompletionSource();var result=new TaskCompletionSource<ContentDialogResult>();
        surface.Loaded+=(_,_)=>loaded.TrySetResult();
        bool dismissing=false;
        void Complete(ContentDialogResult choice)=>result.TrySetResult(choice);
        if(about)surface.KeyDown+=(_,e)=>{if(e.Key==global::Windows.System.VirtualKey.Escape){e.Handled=true;Complete(ContentDialogResult.None);}};
        Button Command(string label,ContentDialogResult choice,bool enabled=true)
        {
            var button=new Button {Content=label,MinWidth=100,IsEnabled=enabled};
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(button,label);
            button.Click+=(_,_)=>{if(choice!=ContentDialogResult.Primary || onPrimary is null || onPrimary())Complete(choice);};
            return button;
        }
        if(primaryButtonText is not null)buttons.Children.Add(Command(primaryButtonText,ContentDialogResult.Primary,isPrimaryEnabled));
        if(secondaryButtonText is not null)buttons.Children.Add(Command(secondaryButtonText,ContentDialogResult.Secondary));
        buttons.Children.Add(Command(closeButtonText,ContentDialogResult.None));
        host.Content=surface;activeDialog=host;
        host.AppWindow.Closing+=(_,e)=>{if(!dismissing){e.Cancel=true;Complete(ContentDialogResult.None);loaded.TrySetResult();}};
        nint owner=WinRT.Interop.WindowNative.GetWindowHandle(this);
        try
        {
            host.AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory,"Branding","app-icon.ico"));
            if(!about)
            {
                host.ExtendsContentIntoTitleBar=true;host.SetTitleBar(titleBar);
                host.AppWindow.TitleBar.ButtonBackgroundColor=Colors.Transparent;
                host.AppWindow.TitleBar.ButtonInactiveBackgroundColor=Colors.Transparent;
                host.AppWindow.TitleBar.ButtonForegroundColor=PrimaryText.Color;
                host.AppWindow.TitleBar.ButtonInactiveForegroundColor=SecondaryText.Color;
            }
            // An owned utility window stays above its widget without creating another taskbar entry.
            host.AppWindow.IsShownInSwitchers=false;
            SetWindowLongPtr(WinRT.Interop.WindowNative.GetWindowHandle(host),-8,owner);
            double dpi=root.XamlRoot.RasterizationScale;var area=DisplayArea.GetFromWindowId(AppWindow.Id,DisplayAreaFallback.Primary).WorkArea;
            int w=(int)Math.Min(width*dpi,area.Width*.94),h=(int)Math.Min(height*dpi,area.Height*.94);
            host.AppWindow.MoveAndResize(new(area.X+(area.Width-w)/2,area.Y+(area.Height-h)/2,w,h));
            if(host.AppWindow.Presenter is OverlappedPresenter p){p.IsMinimizable=false;p.IsMaximizable=false;p.IsResizable=about;p.IsAlwaysOnTop=settings.AlwaysOnTop;}
            host.Activate();await loaded.Task;
            if(result.Task.IsCompleted)return await result.Task;
            EnableWindow(owner,false);
            if(Microsoft.UI.Xaml.Input.FocusManager.FindFirstFocusableElement(content) is Control first)first.Focus(FocusState.Programmatic);
            if(!about)Diagnostics.Record("window.preferences","Flat acrylic utility window opened; switchers=False; owned=True.");
            return await result.Task;
        }
        finally
        {
            EnableWindow(owner,true);activeDialog=null;dismissing=true;host.Close();dialogGate.Release();
            if(!closed)Activate();
        }
    }
    private async Task ShowAboutAsync()
    {
        ShowWindow();
        var initiative = new Image
        {
            Width = 64, Height = 64, Stretch = Stretch.Uniform,
            Source = new BitmapImage(new Uri(Path.Combine(AppContext.BaseDirectory, "Branding", "VibeWare.png")))
        };
        var details = new Expander
        {
            Header = "Technical details",
            HorizontalAlignment = HorizontalAlignment.Center,
            Content = new TextBlock
            {
                Text = $"Separate watchdog · 1 GB process-tree limit\nDiagnostic logs: {Diagnostics.DirectoryPath}",
                FontFamily = MetricFont, TextWrapping = TextWrapping.Wrap
            }
        };
        var content = WidgetAbout.Create(Logo(100), initiative, "OpenAI Usage",
            $"Version {typeof(App).Assembly.GetName().Version?.ToString(3) ?? "unknown"}",
            "I built OpenAI Usage to see my Codex allowance, tokens and credits at a glance.",
            "https://github.com/umbertotechnopreneur/Tools/tree/main/Widgets/OpenAI",
            "I provide this software independently and as-is, without warranty.", details);
        await ShowDialogAsync("About OpenAI Usage", content, width:640, height:620, about:true);
    }
    private static Image Logo(double size)=>new(){Source=new BitmapImage(new Uri(Path.Combine(AppContext.BaseDirectory,"Branding","app-icon-512.png"))),Width=size,Height=size,HorizontalAlignment=HorizontalAlignment.Left};
    private void ThemeChanged(UISettings sender,object args)=>DispatcherQueue.TryEnqueue(ApplySystemTheme);
    private void ApplySystemTheme()
    {
        var background=systemSettings.GetColorValue(UIColorType.Background);
        bool dark=background.R*299+background.G*587+background.B*114<128000;
        root.RequestedTheme=dark?ElementTheme.Dark:ElementTheme.Light;
        root.Background=new SolidColorBrush(WidgetAcrylicBackdrop.IsSupported?Colors.Transparent:dark?ColorHelper.FromArgb(255,24,30,37):ColorHelper.FromArgb(255,245,249,252));
        var accent=systemSettings.GetColorValue(UIColorType.Accent); Accent.Color=accent;
        PrimaryText.Color=dark?ColorHelper.FromArgb(255,235,241,249):ColorHelper.FromArgb(255,23,39,62);
        SecondaryText.Color=dark?ColorHelper.FromArgb(255,194,205,221):ColorHelper.FromArgb(255,69,87,111);
        WarningText.Color=dark?ColorHelper.FromArgb(255,255,203,107):ColorHelper.FromArgb(255,148,82,0);
        static global::Windows.UI.Color Blend(global::Windows.UI.Color a,global::Windows.UI.Color b,double amount)=>ColorHelper.FromArgb(255,(byte)(a.R*(1-amount)+b.R*amount),(byte)(a.G*(1-amount)+b.G*amount),(byte)(a.B*(1-amount)+b.B*amount));
        ValueGradient.GradientStops[0].Color=dark?Blend(Colors.White,accent,.12):ColorHelper.FromArgb(255,18,24,34);
        ValueGradient.GradientStops[1].Color=dark?Blend(Colors.White,accent,.38):Blend(Colors.Black,accent,.58);
        ApplyMetricColors();UpdatePinVisual();
        if(activeDialog is { ExtendsContentIntoTitleBar: true, Content: FrameworkElement dialogRoot })
        {
            dialogRoot.RequestedTheme=root.RequestedTheme;
            activeDialog.AppWindow.TitleBar.ButtonForegroundColor=PrimaryText.Color;
            activeDialog.AppWindow.TitleBar.ButtonInactiveForegroundColor=SecondaryText.Color;
        }
    }
    private void ApplyMetricColors()
    {
        foreach(var metric in new[]{remaining,todayTokens,credits}){metric.FontFamily=MetricFont;metric.Foreground=ValueGradient;metric.TextWrapping=TextWrapping.NoWrap;metric.TextTrimming=TextTrimming.CharacterEllipsis;}
        remaining.Foreground=state.Remaining is <=10?WarningText:ValueGradient;
    }
    private void SaveSettings(){try{storage.SaveSettings(settings);}catch(Exception error){Diagnostics.Record("settings.failed",error.Message);tray?.NotifyError("Could not save settings.");}}

    private static async Task AddStartupPreferenceAsync(StackPanel content)
    {
        var startup = new CheckBox { Content = "Start with Windows", IsEnabled = false };
        var hint = Text("Available with the MSIX installation.", 12);
        content.Children.Add(startup);
        content.Children.Add(hint);

        StartupTask task;
        try { task = await StartupTask.GetAsync(StartupTaskId); }
        catch (Exception error)
        {
            Diagnostics.Record("startup.unavailable", error.GetType().Name);
            return;
        }

        void ShowState(StartupTaskState state)
        {
            startup.IsChecked = state is StartupTaskState.Enabled or StartupTaskState.EnabledByPolicy;
            startup.IsEnabled = state is StartupTaskState.Enabled or StartupTaskState.Disabled;
            hint.Text = state switch
            {
                StartupTaskState.DisabledByUser => "Re-enable OpenAI Usage in Windows Settings > Apps > Startup.",
                StartupTaskState.DisabledByPolicy or StartupTaskState.EnabledByPolicy => "Managed by Windows policy.",
                _ => "Applies immediately when changed."
            };
        }

        ShowState(task.State);
        startup.Click += async (_, _) =>
        {
            startup.IsEnabled = false;
            var state = task.State;
            try
            {
                if (startup.IsChecked == true && state == StartupTaskState.Disabled)
                    state = await task.RequestEnableAsync();
                else if (startup.IsChecked == false && state == StartupTaskState.Enabled)
                {
                    task.Disable();
                    state = task.State;
                }
                ShowState(state);
            }
            catch (Exception error)
            {
                ShowState(state);
                hint.Text = "Could not change the Windows startup setting.";
                Diagnostics.Record("startup.failed", error.GetType().Name);
            }
        };
    }

    private async Task ShowSettingsAsync()
    {
        ShowWindow();
        var executable = new TextBox { Header = "Codex executable", Text = settings.CodexPath };
        var timezone = new TextBox { Header = "Time zone", Text = settings.TimeZoneId };
        var taskbar = new CheckBox { Content = "Show in taskbar", IsChecked = settings.ShowInTaskbar };
        var content = new StackPanel { Spacing = 12, MinWidth = 380 };
        content.Children.Add(executable);
        content.Children.Add(timezone);
        content.Children.Add(Text("Uses your existing Codex ChatGPT sign-in.", 12));
        content.Children.Add(taskbar);
        await AddStartupPreferenceAsync(content);
        var error = Text("", 12); error.Foreground = new SolidColorBrush(Colors.Orange); content.Children.Add(error);
        bool Save()
        {
            try
            {
                var next = new WidgetSettings
                {
                    CodexPath = executable.Text.Trim(),
                    TimeZoneId = timezone.Text.Trim(),
                    AlwaysOnTop = settings.AlwaysOnTop,
                    ShowInTaskbar = taskbar.IsChecked == true,
                    SelectedWindow = settings.SelectedWindow
                };
                storage.SaveSettings(next);
                settings = next;
                return true;
            }
            catch (Exception ex) { error.Text = ex.Message;return false; }
        }
        if (await ShowDialogAsync("OpenAI Usage · Preferences", content, closeButtonText: "Cancel", primaryButtonText: "Save", onPrimary: Save) == ContentDialogResult.Primary)
        {
            ApplyTaskbarVisibility();
            await RefreshAsync();
        }
    }

    private static TextBlock Text(string text,double size,bool bold=false)=>new(){Text=text,FontSize=size,TextWrapping=TextWrapping.Wrap,FontWeight=bold?Microsoft.UI.Text.FontWeights.SemiBold:Microsoft.UI.Text.FontWeights.Normal,Foreground=PrimaryText};
    private static StackPanel Metric(string label,TextBlock number){var p=new StackPanel {Spacing=7};var heading=Text(label,15);heading.Foreground=SecondaryText;heading.TextWrapping=TextWrapping.NoWrap;p.Children.Add(heading);p.Children.Add(number);return p;}
    private static Button Button(string glyph,string tooltip){var b=new Button {Content=new FontIcon {Glyph=glyph,FontSize=16,Foreground=PrimaryText},Style=(Style)Application.Current.Resources["SubtleButtonStyle"],Background=new SolidColorBrush(Colors.Transparent),BorderThickness=new(0),Padding=new(5),MinWidth=26};ToolTipService.SetToolTip(b,tooltip);Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(b,tooltip);return b;}
    [DllImport("user32.dll",EntryPoint="SetWindowLongPtrW")]private static extern nint SetWindowLongPtr(nint window,int index,nint value);
    [DllImport("user32.dll")]private static extern bool EnableWindow(nint window,bool enabled);
    private sealed class ToggleButtonCompat:Microsoft.UI.Xaml.Controls.Primitives.ToggleButton{}
}
