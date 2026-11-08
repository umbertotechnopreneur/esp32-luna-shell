using System.ComponentModel;
using System.Runtime.InteropServices;

namespace OpenAIUsage.Windows;

// Native shell notification icon and Win32 menu. No Windows Forms, WebView or extra UI runtime.
public sealed class TrayIcon : IDisposable
{
    private const uint Callback = 0x8001;
    private readonly WindowProc procedure;
    private readonly string className = "OpenAIUsage.Tray." + Environment.ProcessId;
    private readonly nint window;
    private readonly nint icon;
    private readonly uint taskbarCreated;
    private readonly Action show, about, settings, website, manifesto, exit;
    private bool disposed;
    public bool IsAvailable { get; private set; }

    public TrayIcon(string iconPath, Action show, Action about, Action settings, Action website, Action manifesto, Action exit)
    {
        this.show=show; this.about=about; this.settings=settings; this.website=website; this.manifesto=manifesto; this.exit=exit;
        procedure = OnMessage;
        var type = new WindowClass { Size=(uint)Marshal.SizeOf<WindowClass>(), Procedure=Marshal.GetFunctionPointerForDelegate(procedure),
            Instance=GetModuleHandle(null), ClassName=className };
        if (RegisterClassEx(ref type)==0) throw new Win32Exception(Marshal.GetLastWin32Error());
        window = CreateWindowEx(0,className,"OpenAI Usage tray",0,0,0,0,0,0,0,type.Instance,0);
        if(window==0) throw new Win32Exception(Marshal.GetLastWin32Error());
        icon=LoadImage(0,iconPath,1,0,0,0x10|0x40);
        if(icon==0) { DestroyWindow(window); throw new IOException("Could not load the tray icon."); }
        taskbarCreated=RegisterWindowMessage("TaskbarCreated");
        if(!Add()) { Dispose(); throw new IOException("Windows notification area is unavailable."); }
    }

    private IconData Data() => new() { Size=(uint)Marshal.SizeOf<IconData>(), Window=window, Id=1, Flags=1|2|4,
        CallbackMessage=Callback, Icon=icon, Tip="OpenAI Usage — click to open", Info="", InfoTitle="" };
    private bool Add() { var data=Data(); IsAvailable=ShellNotifyIcon(0,ref data); return IsAvailable; }
    public void NotifyError(string message)
    {
        if(disposed || !IsAvailable)return;
        var data=Data(); data.Flags=0x10; data.InfoTitle="OpenAI Usage · Connection error";
        data.Info=message.Length>250?message[..250]:message; data.InfoFlags=2;
        ShellNotifyIcon(1,ref data);
    }
    private nint OnMessage(nint hwnd,uint message,nint wParam,nint lParam)
    {
        if(message==taskbarCreated && taskbarCreated!=0) { if(!Add())show(); return 0; }
        if(message==Callback)
        {
            if(lParam==0x202 || lParam==0x203) show();
            else if(lParam==0x205) Menu();
            return 0;
        }
        return DefWindowProc(hwnd,message,wParam,lParam);
    }
    private void Menu()
    {
        nint menu=CreatePopupMenu();
        if(menu==0) return;
        try
        {
            AppendMenu(menu,0,1,"Open"); AppendMenu(menu,0x800,0,null);
            AppendMenu(menu,0,3,"About OpenAI Usage"); AppendMenu(menu,0,4,"Settings");
            AppendMenu(menu,0,5,"Website"); AppendMenu(menu,0,6,"VibeWare manifesto");
            AppendMenu(menu,0x800,0,null); AppendMenu(menu,0,7,"Exit");
            GetCursorPos(out var point); SetForegroundWindow(window);
            uint command=TrackPopupMenuEx(menu,0x100|2,point.X,point.Y,window,0);
            PostMessage(window,0,0,0);
            switch(command) { case 1:show();break; case 3:about();break; case 4:settings();break; case 5:website();break; case 6:manifesto();break; case 7:exit();break; }
        }
        catch(Exception error) { Diagnostics.Record("tray.command.failed",error.GetType().Name+": "+error.Message); }
        finally { DestroyMenu(menu); }
    }
    public void Dispose()
    {
        if(disposed)return; disposed=true;
        var data=Data(); ShellNotifyIcon(2,ref data);
        if(window!=0)DestroyWindow(window); if(icon!=0)DestroyIcon(icon);
        UnregisterClass(className,GetModuleHandle(null));
    }
    [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate nint WindowProc(nint hwnd,uint message,nint wParam,nint lParam);
    [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] private struct WindowClass { public uint Size,Style; public nint Procedure; public int ClassExtra,WindowExtra; public nint Instance,Icon,Cursor,Background; public string? MenuName; public string ClassName; public nint SmallIcon; }
    [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] private struct IconData { public uint Size; public nint Window; public uint Id,Flags,CallbackMessage; public nint Icon; [MarshalAs(UnmanagedType.ByValTStr,SizeConst=128)] public string Tip; public uint State,StateMask; [MarshalAs(UnmanagedType.ByValTStr,SizeConst=256)] public string Info; public uint Timeout; [MarshalAs(UnmanagedType.ByValTStr,SizeConst=64)] public string InfoTitle; public uint InfoFlags; public Guid Guid; public nint BalloonIcon; }
    [StructLayout(LayoutKind.Sequential)] private struct Point { public int X,Y; }
    [DllImport("shell32.dll",EntryPoint="Shell_NotifyIconW",CharSet=CharSet.Unicode)] private static extern bool ShellNotifyIcon(uint action,ref IconData data);
    [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)] private static extern ushort RegisterClassEx(ref WindowClass type);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] private static extern bool UnregisterClass(string name,nint instance);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] private static extern nint CreateWindowEx(uint extended,string name,string title,uint style,int x,int y,int width,int height,nint parent,nint menu,nint instance,nint parameter);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] private static extern nint DefWindowProc(nint window,uint message,nint wParam,nint lParam);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] private static extern nint GetModuleHandle(string? name);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] private static extern nint LoadImage(nint instance,string name,uint type,int width,int height,uint flags);
    [DllImport("user32.dll")] private static extern bool DestroyWindow(nint window);
    [DllImport("user32.dll")] private static extern bool DestroyIcon(nint icon);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] private static extern uint RegisterWindowMessage(string name);
    [DllImport("user32.dll")] private static extern nint CreatePopupMenu();
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] private static extern bool AppendMenu(nint menu,uint flags,nuint id,string? text);
    [DllImport("user32.dll")] private static extern uint TrackPopupMenuEx(nint menu,uint flags,int x,int y,nint window,nint parameters);
    [DllImport("user32.dll")] private static extern bool DestroyMenu(nint menu);
    [DllImport("user32.dll")] private static extern bool GetCursorPos(out Point point);
    [DllImport("user32.dll")] private static extern bool SetForegroundWindow(nint window);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] private static extern bool PostMessage(nint window,uint message,nint wParam,nint lParam);
}
