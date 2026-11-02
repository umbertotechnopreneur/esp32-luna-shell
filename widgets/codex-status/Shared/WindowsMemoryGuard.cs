using System.ComponentModel;
using System.Diagnostics;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace OpenAIUsage;

// A UI-independent process owns a Windows job containing the widget and its future children.
// Reference behavior reviewed in Thermalright VibeWare's WindowsMemoryGuard; no device code is used.
public static class WindowsMemoryGuard
{
    public const ulong LimitBytes = 1_000_000_000;
    private const uint KillOnClose = 0x2000;
    private const int ExtendedLimits = 9;
    public static bool IsOverLimit(ulong bytes) => bytes > LimitBytes;

    public static IDisposable Start()
    {
        using var current = Process.GetCurrentProcess();
        var nonce = Guid.NewGuid().ToString("N");
        var pipe = new NamedPipeServerStream("OpenAIUsage.Guard." + nonce, PipeDirection.InOut, 1,
            PipeTransmissionMode.Byte, PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);
        var start = new ProcessStartInfo(Environment.ProcessPath!) { UseShellExecute = false, CreateNoWindow = true };
        start.ArgumentList.Add("--memory-guard"); start.ArgumentList.Add(current.Id.ToString());
        start.ArgumentList.Add(current.StartTime.ToUniversalTime().Ticks.ToString()); start.ArgumentList.Add(nonce);
        using var guard = Process.Start(start) ?? throw new IOException("Memory guard did not start.");
        try
        {
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(10));
            pipe.WaitForConnectionAsync(deadline.Token).GetAwaiter().GetResult();
            if (!GetNamedPipeClientProcessId(pipe.SafePipeHandle, out uint peer) || peer != guard.Id) throw new IOException("Memory guard identity check failed.");
            var ready = new byte[1];
            pipe.ReadExactlyAsync(ready, deadline.Token).AsTask().GetAwaiter().GetResult();
            if (ready[0] != 1) throw new IOException("Memory guard could not protect this process.");
            Diagnostics.Record("guard.ready", "Separate watchdog active; aggregate private-memory limit 1 GB.");
            return pipe;
        }
        catch { pipe.Dispose(); throw; }
    }

    public static int Run(string[] args)
    {
        if (args.Length != 4 || !int.TryParse(args[1], out int pid) || !long.TryParse(args[2], out long started) || !Guid.TryParseExact(args[3], "N", out _)) return 2;
        bool protectedParent = false;
        try
        {
            using var pipe = new NamedPipeClientStream(".", "OpenAIUsage.Guard." + args[3], PipeDirection.InOut, PipeOptions.CurrentUserOnly);
            pipe.Connect(10000);
            if (!GetNamedPipeServerProcessId(pipe.SafePipeHandle, out uint peer) || peer != pid) return 2;
            using var parent = Process.GetProcessById(pid);
            _ = parent.Handle;
            if (parent.StartTime.ToUniversalTime().Ticks != started || !string.Equals(parent.MainModule?.FileName, Environment.ProcessPath, StringComparison.OrdinalIgnoreCase)) return 2;
            using var job = CreateJobObject(IntPtr.Zero, null);
            var config = new JobLimits { Basic = new BasicLimits { Flags = KillOnClose } };
            uint size = (uint)Marshal.SizeOf<JobLimits>();
            if (job.IsInvalid || !SetInformationJobObject(job, ExtendedLimits, ref config, size) || !AssignProcessToJobObject(job, parent.SafeHandle)) throw new Win32Exception(Marshal.GetLastWin32Error());
            protectedParent = true;
            parent.Refresh();
            if (!QueryInformationJobObject(job, ExtendedLimits, out var first, size, IntPtr.Zero)) throw new Win32Exception(Marshal.GetLastWin32Error());
            ulong privateBefore = (ulong)parent.PrivateMemorySize64;
            ulong prior = privateBefore - Math.Min(privateBefore, first.PeakJobMemoryUsed.ToUInt64());
            pipe.WriteByte(1); pipe.Flush();
            long heartbeat = 0;
            while (!parent.WaitForExit(500))
            {
                if (!QueryInformationJobObject(job, ExtendedLimits, out var usage, size, IntPtr.Zero)) throw new Win32Exception(Marshal.GetLastWin32Error());
                ulong peak = usage.PeakJobMemoryUsed.ToUInt64() + prior;
                if (++heartbeat % 20 == 0)
                {
                    try { File.WriteAllText(Path.Combine(new Storage().DirectoryPath, "watchdog.json"), System.Text.Json.JsonSerializer.Serialize(new { atUtc = DateTimeOffset.UtcNow, pid, peakBytes = peak, limitBytes = LimitBytes })); }
                    catch (IOException) { }
                }
                if (!IsOverLimit(peak)) continue;
                Diagnostics.Record("guard.memory-limit", "The widget and its children were stopped after exceeding 1 GB.", (long)peak);
                if (!TerminateJobObject(job, 137)) throw new Win32Exception(Marshal.GetLastWin32Error());
                ShowNotice("OpenAI Usage exceeded its 1 GB memory limit.\nThe widget and its child processes have been stopped.\nDetails are in the local diagnostic logs.");
                return 137;
            }
            return 0; // Closing the job also terminates leftover children from this widget only.
        }
        catch (Exception ex)
        {
            Diagnostics.Record("guard.failure", ex.GetType().Name + ": " + ex.Message);
            if (protectedParent) ShowNotice("Memory protection stopped working.\nOpenAI Usage and its child processes have been closed.\nSee the local diagnostic logs.");
            // The guarded job's handle closes during unwinding, stopping its members on failure.
            return 1;
        }
    }

    public static void ShowNotice(string message) => MessageBox(IntPtr.Zero, message, "OpenAI Usage", 0x30 | 0x10000);
    [StructLayout(LayoutKind.Sequential)] private struct BasicLimits { public long ProcessTime, JobTime; public uint Flags; public UIntPtr MinWorkingSet, MaxWorkingSet; public uint ActiveProcessLimit; public UIntPtr Affinity; public uint PriorityClass, SchedulingClass; }
    [StructLayout(LayoutKind.Sequential)] private struct IoCounters { public ulong ReadOps, WriteOps, OtherOps, ReadBytes, WriteBytes, OtherBytes; }
    [StructLayout(LayoutKind.Sequential)] private struct JobLimits { public BasicLimits Basic; public IoCounters Io; public UIntPtr ProcessMemoryLimit, JobMemoryLimit, PeakProcessMemoryUsed, PeakJobMemoryUsed; }
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] private static extern SafeFileHandle CreateJobObject(IntPtr attributes, string? name);
    [DllImport("kernel32.dll", SetLastError=true)] private static extern bool AssignProcessToJobObject(SafeFileHandle job, SafeProcessHandle process);
    [DllImport("kernel32.dll", SetLastError=true)] private static extern bool SetInformationJobObject(SafeFileHandle job, int type, ref JobLimits info, uint length);
    [DllImport("kernel32.dll", SetLastError=true)] private static extern bool QueryInformationJobObject(SafeFileHandle job, int type, out JobLimits info, uint length, IntPtr returned);
    [DllImport("kernel32.dll", SetLastError=true)] private static extern bool TerminateJobObject(SafeFileHandle job, uint code);
    [DllImport("kernel32.dll", SetLastError=true)] private static extern bool GetNamedPipeClientProcessId(SafePipeHandle pipe, out uint pid);
    [DllImport("kernel32.dll", SetLastError=true)] private static extern bool GetNamedPipeServerProcessId(SafePipeHandle pipe, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int MessageBox(IntPtr window, string message, string title, uint type);
}
