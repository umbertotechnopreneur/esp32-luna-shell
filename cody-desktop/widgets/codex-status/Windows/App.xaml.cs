using Microsoft.UI.Xaml;

namespace OpenAIUsage.Windows;

public partial class App : Application
{
    private Window? window;
    public App()
    {
        InitializeComponent();
        UnhandledException += (_, error) =>
        {
            Diagnostics.Record("ui.unhandled", error.Exception.GetType().Name + ": " + error.Exception.Message);
            try
            {
                var directory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "OpenAIUsage");
                Directory.CreateDirectory(directory);
                File.WriteAllText(Path.Combine(directory, "startup-error.log"), error.Exception.ToString());
            }
            catch (IOException) { }
        };
    }
    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        window = new MainWindow();
        window.Activate();
    }
}
