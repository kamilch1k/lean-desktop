using System;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

class InputLanguageTest
{
    [DllImport("user32.dll")] static extern IntPtr GetKeyboardLayout(uint thread);
    [DllImport("user32.dll")] static extern int GetKeyboardLayoutList(int count, [Out] IntPtr[] layouts);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr window, uint message, IntPtr wp, IntPtr lp);
    static void Pump() { var timer = Stopwatch.StartNew(); while (timer.ElapsedMilliseconds < 150) { Application.DoEvents(); Thread.Sleep(5); } }
    static void Check(bool pass, string label) { Console.WriteLine((pass ? "PASS " : "FAIL ") + label); if (!pass) throw new Exception(label); }

    [STAThread] static int Main(string[] args)
    {
        var method = Assembly.LoadFrom(args[0]).GetType("QuickSearch").GetMethod("SwitchInputLanguage", BindingFlags.NonPublic | BindingFlags.Static);
        var before = GetKeyboardLayout(0);
        int count = GetKeyboardLayoutList(0, null);
        using (var window = new Form()) // Test-owned hidden window; never sends keystrokes to other apps.
        {
            IntPtr handle = window.Handle;
            try
            {
                Check(!(bool)method.Invoke(null, new object[] { IntPtr.Zero, false }), "no foreground target is a safe no-op");
                if (count < 2)
                {
                    Check(!(bool)method.Invoke(null, new object[] { handle, false }), "one installed layout is a safe no-op");
                    Console.WriteLine("SKIP cycling requires two installed keyboard layouts");
                    return 0;
                }
                for (int i = 0; i < count; ++i)
                {
                    var previous = GetKeyboardLayout(0);
                    Check((bool)method.Invoke(null, new object[] { handle, false }), "forward layout request posted");
                    Pump();
                    Check(GetKeyboardLayout(0) != previous, "test window activates another installed layout");
                }
                Check(GetKeyboardLayout(0) == before, "full cycle returns to the initial layout");
                Check((bool)method.Invoke(null, new object[] { handle, true }), "reverse layout request posted"); Pump();
                Check(GetKeyboardLayout(0) != before, "reverse cycling changes the test window layout");
                Check((bool)method.Invoke(null, new object[] { handle, false }), "forward layout request posted after reverse"); Pump();
                Check(GetKeyboardLayout(0) == before, "forward undoes reverse");
                return 0;
            }
            catch (Exception error) { Console.Error.WriteLine(error); return 1; }
            finally { PostMessage(handle, 0x0050, IntPtr.Zero, before); Pump(); }
        }
    }
}
