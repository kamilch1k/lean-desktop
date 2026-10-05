using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;

class LaunchFixture
{
    [STAThread] static void Main(string[] args)
    {
        File.WriteAllLines(args[0], new[] { args[1], Environment.CurrentDirectory });
    }
}

class AppLaunchTest
{
    delegate bool EnumWindow(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumWindow callback, IntPtr data);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")] static extern int GetWindowTextLength(IntPtr window);
    [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr window, int attribute, out int value, int size);
    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access, bool inherit, uint process);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    static extern int GetApplicationUserModelId(IntPtr process, ref uint length, System.Text.StringBuilder id);
    static void Check(bool ok, string what)
    {
        if (!ok) throw new Exception(what);
        Console.WriteLine("PASS: " + what);
    }
    static object Call(Type type, string name, params object[] args)
    {
        try { return type.GetMethod(name, BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static).Invoke(null, args); }
        catch (TargetInvocationException error) { throw error.InnerException; }
    }
    static bool MatchesApp(uint process, string appId)
    {
        IntPtr handle = OpenProcess(0x1000, false, process);
        if (handle == IntPtr.Zero) return false;
        try
        {
            uint length = 512;
            var id = new System.Text.StringBuilder((int)length);
            return GetApplicationUserModelId(handle, ref length, id) == 0 && id.ToString() == appId;
        }
        finally { CloseHandle(handle); }
    }
    static uint WindowProcess(uint process, string appId)
    {
        uint found = 0;
        EnumWindows((window, data) => {
            uint pid; int cloaked;
            GetWindowThreadProcessId(window, out pid);
            if (IsWindowVisible(window) && GetWindowTextLength(window) > 0
                && (DwmGetWindowAttribute(window, 14, out cloaked, 4) != 0 || cloaked == 0)
                && (pid == process || MatchesApp(pid, appId))) found = pid;
            return found == 0;
        }, IntPtr.Zero);
        return found;
    }
    [STAThread] static int Main(string[] args)
    {
        try
        {
            var assembly = Assembly.LoadFrom(Path.GetFullPath(args[0]));
            var launcher = assembly.GetType("AppLauncher", true);
            if (args.Length == 3 && args[1] == "--app")
            {
                // Opt-in live validation leaves the requested app open. Routine
                // tests only start our disposable fixture, never the user's apps.
                var apps = (List<string[]>)Call(assembly.GetType("QuickSearch", true), "LoadApps");
                var app = apps.Single(a => string.Equals(a[0], args[2], StringComparison.OrdinalIgnoreCase));
                var before = Process.GetProcesses().Select(p => { using (p) return p.Id; }).ToArray();
                uint process = (uint)Call(launcher, "Start", app[1]);
                Check(process != 0, "activation returned an app process");
                var timer = Stopwatch.StartNew();
                uint windowProcess;
                // Terminal can forward a second activation to its first process
                // and exit the activation PID. Match that window's exact AUMID.
                while ((windowProcess = WindowProcess(process, app[1])) == 0 && timer.ElapsedMilliseconds < 20000) Thread.Sleep(100);
                Check(windowProcess != 0, app[0] + " has a visible window; PID " + windowProcess
                    + "; new window process: " + !before.Contains((int)windowProcess));
                return 0;
            }
            Check((bool)Call(launcher, "IsPackagedApp", "Microsoft.WindowsTerminal_8wekyb3d8bbwe!App"), "packaged app detection");
            Check(!(bool)Call(launcher, "IsPackagedApp", @"C:\Test_folder!App.exe"), "exclamation in executable path stays a desktop app");
            Check(!(bool)Call(launcher, "IsPackagedApp", "Chrome"), "desktop app ID detection");
            string root = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Launch fixture ! " + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(root);
            try
            {
                string fixture = Path.Combine(root, "Test_app!Launch.exe");
                File.Copy(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "LaunchFixture.exe"), fixture);
                string report = Path.Combine(root, "result.txt"), shortcut = Path.Combine(root, "Shortcut with spaces.lnk");
                dynamic shell = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell"));
                dynamic link = shell.CreateShortcut(shortcut);
                try
                {
                    link.TargetPath = fixture;
                    link.Arguments = "\"" + report + "\" \"argument with spaces\"";
                    link.WorkingDirectory = root;
                    link.Save();
                }
                finally { Marshal.ReleaseComObject(link); Marshal.ReleaseComObject(shell); }
                uint process = (uint)Call(launcher, "StartShellItem", shortcut);
                if (process != 0)
                {
                    try { using (var child = Process.GetProcessById((int)process)) Check(child.WaitForExit(5000), "fixture exits"); }
                    catch (ArgumentException) { } // Already exited normally.
                }
                var timer = Stopwatch.StartNew();
                while (!File.Exists(report) && timer.ElapsedMilliseconds < 5000) Thread.Sleep(50);
                Check(File.Exists(report), "cold shortcut launch ran its executable");
                var result = File.ReadAllLines(report);
                Check(result[0] == "argument with spaces", "shortcut arguments preserved");
                Check(result[1] == root, "shortcut working directory preserved");
                bool failed = false;
                try { Call(launcher, "StartShellItem", Path.Combine(root, "missing.exe")); }
                catch (Exception) { failed = true; }
                Check(failed, "missing application reports an error");
                failed = false;
                try { Call(launcher, "Start", "QuickSearch.MissingApp_0000000000000!App"); }
                catch (COMException) { failed = true; }
                Check(failed, "unregistered packaged application reports an error");
            }
            finally { Directory.Delete(root, true); }
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
