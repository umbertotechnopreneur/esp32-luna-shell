using Microsoft.UI.Xaml;

namespace OpenAIUsage.Windows;

public static class Program
{
    [STAThread]
    public static int Main(string[] args)
    {
        if (args.FirstOrDefault() == "--memory-guard") return WindowsMemoryGuard.Run(args);
        try
        {
            uint packageNameLength = 0;
            const int noPackage = 15700;
            if (GetCurrentPackageFullName(ref packageNameLength, 0) == noPackage)
                SetCurrentProcessExplicitAppUserModelID("biz.umbertogiacobbi.OpenAIUsage");
            using var guard = WindowsMemoryGuard.Start();
            WinRT.ComWrappersSupport.InitializeComWrappers();
            Application.Start(initialization =>
            {
                SynchronizationContext.SetSynchronizationContext(new Microsoft.UI.Dispatching.DispatcherQueueSynchronizationContext(Microsoft.UI.Dispatching.DispatcherQueue.GetForCurrentThread()));
                _ = new App();
            });
            return 0;
        }
        catch (Exception ex)
        {
            Diagnostics.Record("startup.failed", ex.GetType().Name + ": " + ex.Message);
            WindowsMemoryGuard.ShowNotice("OpenAI Usage could not start with memory protection.\nDetails are in the local diagnostic logs.");
            return 1;
        }
    }
    [System.Runtime.InteropServices.DllImport("shell32.dll",CharSet=System.Runtime.InteropServices.CharSet.Unicode)]
    private static extern int SetCurrentProcessExplicitAppUserModelID(string appId);

    [System.Runtime.InteropServices.DllImport("kernel32.dll", ExactSpelling = true)]
    private static extern int GetCurrentPackageFullName(ref uint packageFullNameLength, nint packageFullName);
}
