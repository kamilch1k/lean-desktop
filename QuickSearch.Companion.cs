// QuickSearch companion: launcher, large Alt+Tab switcher, and screenshots.
// LeanBar owns the desktop/taskbar. The keyboard hook has its own message thread
// so icon extraction, application enumeration, and screenshot UI cannot stall it.
// Build through build.cmd. --toggle opens the running launcher.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

class Row
{
    public string Text;
    public Action Go;
    public override string ToString() { return Text; }
}

class QuickSearch : Form
{
    delegate IntPtr HookProc(int nCode, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] static extern IntPtr SetWindowsHookEx(int idHook, HookProc fn, IntPtr hMod, uint threadId);
    [DllImport("user32.dll")] static extern bool UnhookWindowsHookEx(IntPtr hhk);
    [DllImport("user32.dll")] static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] static extern short GetAsyncKeyState(int vk);
    [DllImport("user32.dll")] static extern uint RegisterWindowMessage(string name);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] static extern bool AllowSetForegroundWindow(uint processId);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")] static extern bool RegisterHotKey(IntPtr hWnd, int id, uint mods, uint vk);
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr FindWindow(string className, string title);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr GetProp(IntPtr window, string name);
    [DllImport("user32.dll")] static extern bool SetProcessDPIAware();
    [DllImport("kernel32.dll")] static extern IntPtr GetModuleHandle(string name);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumWindowsProc fn, IntPtr lParam);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] static extern bool IsIconic(IntPtr hWnd);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr hWnd, int cmd);
    [DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr hWnd, uint cmd);
    [DllImport("user32.dll")] static extern int GetWindowLong(IntPtr hWnd, int index);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr hWnd, System.Text.StringBuilder s, int max);
    [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr hWnd, int attr, out int value, int size);
    const int GWL_EXSTYLE = -20, WS_EX_TOOLWINDOW = 0x80, WS_EX_APPWINDOW = 0x40000, DWMWA_CLOAKED = 14, SW_RESTORE = 9;
    const uint GW_OWNER = 4;
    const int WH_KEYBOARD_LL = 13, WM_KEYDOWN = 0x100, WM_SYSKEYDOWN = 0x104, WM_HOTKEY = 0x312;
    const int VK_LWIN = 0x5B, VK_RWIN = 0x5C, VK_SHIFT = 0x10, VK_CONTROL = 0x11, VK_MENU = 0x12, VK_LMENU = 0xA4, VK_RMENU = 0xA5,
              VK_TAB = 0x09, VK_ESCAPE = 0x1B, VK_SNAPSHOT = 0x2C, VK_S = 0x53, LLKHF_INJECTED = 0x10;
    static readonly uint ToggleMsg = RegisterWindowMessage("QuickSearch.Toggle");   // sent by "--toggle" to the running copy
    const byte VK_MASK = 0xE8;   // unassigned key: makes Windows think Win was used in a combo, so Start stays shut
    const uint KEYEVENTF_KEYUP = 2, MOD_ALT = 1, MOD_NOREPEAT = 0x4000;
    const string AppsFolder = "shell:::{4234d49b-0245-4df3-b780-3893943456e1}";

    static readonly string Dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "QuickSearch");
    static readonly string AppsFile = Path.Combine(Dir, "apps.txt");  // name \t id; shown instantly, refreshed in background
    static readonly string MruFile = Path.Combine(Dir, "mru.txt");    // launches \t id
    static readonly string HwndFile = Path.Combine(Dir, "lean-hwnd.txt");  // running copy's window, for --toggle
    static readonly string Everything = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), @"Everything\Everything.exe");
    static readonly string StartupLink = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Startup), "QuickSearch-LeanBar.lnk");
    static readonly string[] Favorites = { "Claude", "ChatGPT", "Google Chrome", "Visual Studio Code", "Blender", "Roblox Studio", "Terminal", "File Explorer" };
    static QuickSearch instance;   // keeps the hidden form alive
    static HookProc hookProc;      // keeps the hook delegate alive (GC'd delegate = crash)

    List<string[]> apps;           // { name, AppsFolder id }
    List<KeyValuePair<IntPtr, string>> windows = new List<KeyValuePair<IntPtr, string>>();   // open windows, most recent first
    Dictionary<string, int> mru = new Dictionary<string, int>();
    DateTime loadedAt = DateTime.MinValue;
    IntPtr hook;
    Thread keyboardThread;
    bool winDown, winUsed, winSDone, switching;
    bool desktopKeyDown, overviewKeyDown;
    Switcher switcher;
    TextBox box = new TextBox();
    ListBox list = new ListBox();
    NotifyIcon tray;
    TaskBar bar = null;

    QuickSearch()
    {
        Directory.CreateDirectory(Dir);
        apps = ReadPairs(AppsFile);
        foreach (var p in ReadPairs(MruFile)) { int n; if (int.TryParse(p[0], out n)) mru[p[1]] = n; }

        float k;
        using (var g = Graphics.FromHwnd(IntPtr.Zero)) k = g.DpiX / 96f;
        Text = "Search";
        FormBorderStyle = FormBorderStyle.FixedToolWindow;   // no visual styles anywhere: classic 3D controls
        ShowInTaskbar = false;
        TopMost = true;
        StartPosition = FormStartPosition.Manual;
        Font = new Font("Tahoma", 10f);
        ClientSize = new Size((int)(560 * k), (int)(320 * k));
        list.Dock = DockStyle.Fill;
        list.IntegralHeight = false;
        box.Dock = DockStyle.Top;
        Controls.Add(list);
        Controls.Add(box);
        box.TextChanged += (s, e) => UpdateList();
        box.KeyDown += OnKey;
        list.KeyDown += (s, e) => { if (e.KeyCode == Keys.Enter) Launch(); else if (e.KeyCode == Keys.Escape) Hide(); };
        list.DoubleClick += (s, e) => Launch();
        Deactivate += (s, e) => Hide();
        FormClosing += (s, e) => { if (e.CloseReason == CloseReason.UserClosing) { e.Cancel = true; Hide(); } };

        CreateHandle();
        try { File.WriteAllText(HwndFile, Handle.ToInt64().ToString()); } catch { }
        switcher = new Switcher(k);
        hookProc = OnKeyboard;
        string keys = "Win";
        if (!StartKeyboardThread())
        {
            keys = RegisterHotKey(Handle, 1, MOD_ALT | MOD_NOREPEAT, (uint)Keys.Space) ? "Alt+Space" : "double-click here";
        }
        var menu = new ContextMenu();
        menu.MenuItems.Add("Exit", (s, e) => Quit());
        tray = new NotifyIcon { Icon = SystemIcons.Application, Text = "QuickSearch: " + keys, ContextMenu = menu, Visible = true };
        tray.DoubleClick += (s, e) => Toggle();
        // LeanBar companion: the native process owns the taskbar.

        RefreshApps();
    }

    void Quit()
    {
        UnhookWindowsHookEx(hook);
        if (bar != null) bar.Release();
        tray.Visible = false;
        Application.Exit();
    }

    bool Rehook()
    {
        if (hook != IntPtr.Zero) UnhookWindowsHookEx(hook);
        winDown = Held(VK_LWIN) || Held(VK_RWIN);
        winUsed = winDown;
        winSDone = desktopKeyDown = overviewKeyDown = switching = false;
        BeginInvoke(new Action(switcher.Cancel));
        hook = SetWindowsHookEx(WH_KEYBOARD_LL, hookProc, GetModuleHandle(null), 0);
        return hook != IntPtr.Zero;
    }

    bool StartKeyboardThread()
    {
        var ready = new ManualResetEvent(false);
        bool installed = false;
        keyboardThread = new Thread(() =>
        {
            installed = Rehook();
            ready.Set();
            // A dedicated message pump keeps low-level callbacks independent of UI work.
            using (var repair = new System.Windows.Forms.Timer { Interval = 5 * 60 * 1000 })
            {
                repair.Tick += (s, e) => { if (!Held(VK_MENU) && !Held(VK_LWIN) && !Held(VK_RWIN)) Rehook(); };
                repair.Start();
                Application.Run();
            }
            if (hook != IntPtr.Zero) UnhookWindowsHookEx(hook);
        });
        keyboardThread.IsBackground = true;
        keyboardThread.Name = "QuickSearch keyboard";
        keyboardThread.SetApartmentState(ApartmentState.STA);
        keyboardThread.Start();
        ready.WaitOne();
        ready.Dispose();
        return installed;
    }

    // An unassigned key tap between a modifier's press and release: Windows then treats Win/Alt as "used in a combo",
    // so no Start menu on Win-up and no app menu-bar activation on Alt-up.
    static void Mask()
    {
        keybd_event(VK_MASK, 0, 0, UIntPtr.Zero);
        keybd_event(VK_MASK, 0, KEYEVENTF_KEYUP, UIntPtr.Zero);
    }

    static bool Held(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

    // All global keys in one hook: Win (alone) / Win+S / Ctrl+Esc = QuickSearch, Alt+Tab = switcher,
    // Win+Shift+S / PrtScn = screenshot. Everything else passes straight through.
    IntPtr OnKeyboard(int nCode, IntPtr wParam, IntPtr lParam)
    {
        if (nCode >= 0 && (Marshal.ReadInt32(lParam, 8) & LLKHF_INJECTED) == 0)
        {
            int vk = Marshal.ReadInt32(lParam);
            int msg = wParam.ToInt32();
            bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
            // Lost key-up events (lock screen, UAC, hook reinstallation) must not
            // leave Win marked down forever and silently disable Alt+Tab.
            if (vk != VK_LWIN && vk != VK_RWIN && !Held(VK_LWIN) && !Held(VK_RWIN)) winDown = false;
            if (vk != VK_TAB && !Held(VK_TAB)) overviewKeyDown = false;
            if (vk == 0x44 && !down && desktopKeyDown) { desktopKeyDown = false; return (IntPtr)1; }
            if (vk == VK_TAB && !down && overviewKeyDown) { overviewKeyDown = false; return (IntPtr)1; }
            if (vk == VK_TAB && down && winDown && !Held(VK_CONTROL) && !Held(VK_MENU))
            {
                IntPtr nativeBar = FindWindow("LeanBar.Window", null);
                if (nativeBar != IntPtr.Zero)
                {
                    winUsed = true;
                    if (!overviewKeyDown)
                    {
                        overviewKeyDown = true; Mask();
                        uint pid; GetWindowThreadProcessId(nativeBar, out pid); AllowSetForegroundWindow(pid);
                        PostMessage(nativeBar, RegisterWindowMessage("LeanBar.Overview"), IntPtr.Zero, IntPtr.Zero);
                    }
                    return (IntPtr)1;
                }
            }
            if ((vk == VK_LMENU || vk == VK_RMENU) && !down && switching)   // Alt released: go to the picked window
            {
                switching = false;
                BeginInvoke(new Action(switcher.Commit));
            }
            if (vk == VK_TAB && (Marshal.ReadInt32(lParam, 8) & 0x20) != 0 && !winDown && !Held(VK_CONTROL))
            {
                if (down)
                {
                    int step = Held(VK_SHIFT) ? -1 : 1;
                    if (!switching) { switching = true; Mask(); BeginInvoke(new Action(() => switcher.Open(step))); }
                    else BeginInvoke(new Action(() => switcher.Step(step)));
                }
                return (IntPtr)1;   // the system never sees Alt+Tab
            }
            if (switching && vk == VK_ESCAPE)
            {
                if (down) { switching = false; BeginInvoke(new Action(switcher.Cancel)); }
                return (IntPtr)1;
            }
            if (vk == VK_SNAPSHOT)   // PrtScn
            {
                if (!down) BeginInvoke(new Action(Snip.Take));
                return (IntPtr)1;
            }
            if (vk == VK_LWIN || vk == VK_RWIN)
            {
                if (down) { if (!winDown) { winDown = true; winUsed = false; winSDone = false; } }
                else if (winDown)
                {
                    winDown = false;
                    if (!winUsed)
                    {
                        // swallow the real Win-up and replay it after a mask key, so Explorer never opens Start
                        keybd_event(VK_MASK, 0, 0, UIntPtr.Zero);
                        keybd_event(VK_MASK, 0, KEYEVENTF_KEYUP, UIntPtr.Zero);
                        keybd_event((byte)vk, 0, KEYEVENTF_KEYUP, UIntPtr.Zero);
                        BeginInvoke(new Action(Toggle));
                        return (IntPtr)1;
                    }
                }
            }
            else if (winDown && down)
            {
                winUsed = true;   // Win+E, Win+L, Win+D, ... : leave them alone
                if (vk == 0x44 && !Held(VK_CONTROL) && !Held(VK_SHIFT) && !Held(VK_MENU))
                {
                    IntPtr desktopBar = FindWindow("LeanBar.Window", null);
                    if (desktopBar != IntPtr.Zero && GetProp(desktopBar, "LeanBar.WinD") == IntPtr.Zero)
                    {
                        if (!desktopKeyDown) { desktopKeyDown = true; Mask(); PostMessage(desktopBar, RegisterWindowMessage("LeanBar.ShowDesktop"), IntPtr.Zero, IntPtr.Zero); }
                        return (IntPtr)1;
                    }
                }
                if (vk == VK_S)
                {
                    // Win+S = QuickSearch, Win+Shift+S = screenshot; swallowed so Windows never sees them, once per press
                    if (!winSDone)
                    {
                        winSDone = true;
                        Mask();
                        BeginInvoke(Held(VK_SHIFT) ? new Action(Snip.Take) : new Action(Toggle));
                    }
                    return (IntPtr)1;
                }
            }
            else if (down && vk == VK_ESCAPE && Held(VK_CONTROL) && !Held(VK_SHIFT))
            {
                BeginInvoke(new Action(Toggle));   // Ctrl+Esc would open Start; Ctrl+Shift+Esc (Task Manager) is untouched
                return (IntPtr)1;
            }
        }
        return CallNextHookEx(hook, nCode, wParam, lParam);
    }

    protected override void WndProc(ref Message m)
    {
        if (m.Msg == WM_HOTKEY || m.Msg == (int)ToggleMsg) Toggle();
        base.WndProc(ref m);
    }

    void Toggle()
    {
        if (Visible) { Hide(); return; }
        if ((DateTime.Now - loadedAt).TotalMinutes > 5) RefreshApps();
        windows = OpenWindows(Handle);
        box.Text = "";
        UpdateList();
        var wa = Screen.FromPoint(Cursor.Position).WorkingArea;
        Location = new Point(wa.Left + (wa.Width - Width) / 2, wa.Top + wa.Height / 5);
        Show();
        Activate();
        SetForegroundWindow(Handle);
        box.Focus();
    }

    // Reading the Start menu's app list is slow (seconds when RAM is tight), so it never blocks the UI.
    void RefreshApps()
    {
        loadedAt = DateTime.Now;
        var t = new Thread(() =>
        {
            var found = LoadApps();
            if (found.Count == 0) return;
            try { File.WriteAllLines(AppsFile, found.Select(a => a[0] + "\t" + a[1])); } catch { }
            BeginInvoke(new Action(() => { apps = found; if (Visible) UpdateList(); }));
        });
        t.SetApartmentState(ApartmentState.STA);   // shell COM needs STA
        t.IsBackground = true;
        t.Start();
    }

    static List<string[]> LoadApps()
    {
        var found = new List<string[]>();
        try
        {
            dynamic shell = Activator.CreateInstance(Type.GetTypeFromProgID("Shell.Application"));
            dynamic items = shell.NameSpace(AppsFolder).Items();
            int n = items.Count;
            for (int i = 0; i < n; i++)
            {
                dynamic item = items.Item(i);
                found.Add(new[] { ((string)item.Name).Replace('\t', ' '), (string)item.Path });
            }
        }
        catch { }
        return found;
    }

    static string ReadText(string file) { try { return File.ReadAllText(file).Trim(); } catch { return ""; } }

    static List<string[]> ReadPairs(string file)
    {
        try { return File.ReadAllLines(file).Select(l => l.Split('\t')).Where(p => p.Length == 2).ToList(); }
        catch { return new List<string[]>(); }
    }

    // Higher is better, -1 = no match: prefix > word start > substring > initials ("vsc" -> Visual Studio Code).
    static int Score(string name, string q)
    {
        string n = name.ToLowerInvariant();
        int len = Math.Min(n.Length, 49);   // shorter names win ties inside a tier
        if (n.StartsWith(q, StringComparison.Ordinal)) return 300 - len;
        if (n.Contains(" " + q)) return 200 - len;
        if (n.Contains(q)) return 100 - len;
        int k = 0;
        foreach (char c in n) if (k < q.Length && c == q[k]) k++;
        return k == q.Length ? 50 - len : -1;
    }

    // Same windows the taskbar / Alt+Tab would show, in Z-order (most recently used first).
    static List<KeyValuePair<IntPtr, string>> OpenWindows(IntPtr self)
    {
        var found = new List<KeyValuePair<IntPtr, string>>();
        EnumWindows((h, l) =>
        {
            if (h == self || !IsWindowVisible(h)) return true;
            int ex = GetWindowLong(h, GWL_EXSTYLE);
            if ((ex & WS_EX_TOOLWINDOW) != 0) return true;
            if (GetWindow(h, GW_OWNER) != IntPtr.Zero && (ex & WS_EX_APPWINDOW) == 0) return true;   // dialogs/popups of other windows
            int cloaked;
            if (DwmGetWindowAttribute(h, DWMWA_CLOAKED, out cloaked, 4) == 0 && cloaked != 0) return true;   // suspended Store apps etc.
            var title = new System.Text.StringBuilder(256);
            if (GetWindowText(h, title, title.Capacity) > 0) found.Add(new KeyValuePair<IntPtr, string>(h, title.ToString()));
            return true;
        }, IntPtr.Zero);
        return found;
    }

    static void SwitchTo(IntPtr h)
    {
        if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
        SetForegroundWindow(h);
    }

    [DllImport("user32.dll")] static extern IntPtr SendMessageTimeout(IntPtr h, uint msg, IntPtr w, IntPtr l, uint flags, uint ms, out IntPtr result);
    [DllImport("user32.dll", EntryPoint = "GetClassLongPtrW")] static extern IntPtr GetClassLongPtr(IntPtr h, int index);
    [DllImport("user32.dll")] static extern bool DrawIconEx(IntPtr hdc, int x, int y, IntPtr icon, int w, int h, int step, IntPtr brush, int flags);

    // The window's small icon (shared, never destroyed by us). 50 ms cap so a hung app can't stall the UI.
    static IntPtr WindowIcon(IntPtr w, bool large = false)
    {
        IntPtr icon = IntPtr.Zero;
        if (large) SendMessageTimeout(w, 0x7F, (IntPtr)1, IntPtr.Zero, 2, 50, out icon); // ICON_BIG
        if (large && icon == IntPtr.Zero) icon = GetClassLongPtr(w, -14);
        if (icon == IntPtr.Zero) SendMessageTimeout(w, 0x7F, (IntPtr)2, IntPtr.Zero, 2, 50, out icon);   // ICON_SMALL2
        if (icon == IntPtr.Zero) SendMessageTimeout(w, 0x7F, IntPtr.Zero, IntPtr.Zero, 2, 50, out icon);   // ICON_SMALL
        if (icon == IntPtr.Zero) icon = GetClassLongPtr(w, -34);   // GCLP_HICONSM
        if (icon == IntPtr.Zero) icon = GetClassLongPtr(w, -14);   // GCLP_HICON
        return icon;
    }

    static void DrawIcon(Graphics g, IntPtr icon, int x, int y, int size)
    {
        if (icon == IntPtr.Zero) return;
        IntPtr hdc = g.GetHdc();
        DrawIconEx(hdc, x, y, icon, size, size, 0, IntPtr.Zero, 3);   // DI_NORMAL
        g.ReleaseHdc(hdc);
    }

    int Uses(string id) { int n; mru.TryGetValue(id, out n); return n; }
    static bool IsFavorite(string name) { return Favorites.Any(f => name.StartsWith(f, StringComparison.OrdinalIgnoreCase)); }

    // Often-launched apps climb up to one score tier; favorites win ties.
    int Boost(string[] a) { return Math.Min(Uses(a[1]), 10) * 10 + (IsFavorite(a[0]) ? 5 : 0); }

    void UpdateList()
    {
        string q = box.Text.Trim();
        string ql = q.ToLowerInvariant();
        // open windows first: with no taskbar this is the window switcher (empty box = all, typing filters)
        var rows = windows
            .Select(w => new { w, s = q.Length == 0 ? 0 : Score(w.Value, ql) })
            .Where(x => x.s >= 0)
            .OrderByDescending(x => x.s)   // stable: keeps most-recent-first order on ties
            .Take(10)
            .Select(x => new Row { Text = "» " + x.w.Value, Go = () => SwitchTo(x.w.Key) })
            .ToList();
        rows.AddRange(apps
            .Select(a => new { a, s = q.Length == 0 ? 0 : Score(a[0], ql) })
            .Where(x => x.s >= 0 && (q.Length > 0 || Uses(x.a[1]) > 0 || IsFavorite(x.a[0])))   // empty box: your usual apps
            .OrderByDescending(x => x.s + Boost(x.a))
            .ThenBy(x => x.a[0])
            .Take(12)
            .Select(x => AppRow(x.a)));
        if (q.Length > 0)
        {
            rows.Add(new Row { Text = "> Run  " + q, Go = () => Run(q) });
            rows.Add(new Row
            {
                Text = "> Find files  " + q,
                Go = File.Exists(Everything)
                    ? (Action)(() => Process.Start(Everything, "-search \"" + q + "\""))
                    : () => Explorer("search-ms:query=" + Uri.EscapeDataString(q))   // ShellExecute on search-ms: fails ("cannot find the file")
            });
        }
        list.BeginUpdate();
        list.Items.Clear();
        list.Items.AddRange(rows.ToArray());
        if (list.Items.Count > 0) list.SelectedIndex = 0;
        list.EndUpdate();
    }

    Row AppRow(string[] a)
    {
        return new Row
        {
            Text = a[0],
            Go = () =>
            {
                mru[a[1]] = Uses(a[1]) + 1;
                try { File.WriteAllLines(MruFile, mru.Select(p => p.Value + "\t" + p.Key)); } catch { }
                string target = "shell:AppsFolder\\" + a[1];   // same launch path Start uses
                try { Process.Start(target); }
                catch (System.ComponentModel.Win32Exception) { Explorer(target); }
            }
        };
    }

    // Explorer opens whatever Start/Win+R can (AppsFolder ids, search-ms:, folders). Started with CreateProcess,
    // not ShellExecute, so "No application is associated..." can't happen here even when ShellExecute is failing.
    static void Explorer(string target)
    {
        string exe = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "explorer.exe");
        Process.Start(new ProcessStartInfo(exe, "\"" + target + "\"") { UseShellExecute = false });
    }

    // Like Win+R: "cmd", "notepad C:\x.txt", "https://...", "ms-settings:", folders.
    static void Run(string q)
    {
        try { Process.Start(q); }
        catch (System.ComponentModel.Win32Exception)
        {
            int sp = q.IndexOf(' ');
            if (sp < 0) throw;
            Process.Start(q.Substring(0, sp), q.Substring(sp + 1));
        }
    }

    void OnKey(object sender, KeyEventArgs e)
    {
        if (e.KeyCode == Keys.Escape) Hide();
        else if (e.KeyCode == Keys.Enter) Launch();
        else if (e.KeyCode == Keys.Down || e.KeyCode == Keys.Up)
        {
            int i = list.SelectedIndex + (e.KeyCode == Keys.Down ? 1 : -1);
            if (i >= 0 && i < list.Items.Count) list.SelectedIndex = i;
        }
        else return;
        e.Handled = e.SuppressKeyPress = true;
    }

    void Launch()
    {
        var row = (list.SelectedItem ?? (list.Items.Count > 0 ? list.Items[0] : null)) as Row;
        if (row == null) return;
        try { row.Go(); }   // before Hide(): while we're still foreground, Windows lets us hand focus to the target
        catch (Exception ex) { MessageBox.Show(ex.Message, "QuickSearch"); }
        Hide();
    }

    // ---- Win98-style taskbar. Only shown while Explorer isn't running (QuickSearch as the whole shell). ----
    // Event-driven (WinEvent hooks), so it costs nothing until a window opens, closes, renames or takes focus.
    class TaskBar : Form
    {
        delegate void WinEventProc(IntPtr hook, uint evt, IntPtr hwnd, int idObject, int idChild, uint thread, uint time);
        [DllImport("user32.dll")] static extern IntPtr SetWinEventHook(uint min, uint max, IntPtr mod, WinEventProc fn, uint pid, uint tid, uint flags);
        [DllImport("user32.dll")] static extern bool SystemParametersInfo(uint action, uint param, ref RECT rect, uint flags);
        [DllImport("user32.dll")] static extern IntPtr FindWindow(string cls, string title);
        [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
        [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
        [StructLayout(LayoutKind.Sequential)] struct RECT { public int Left, Top, Right, Bottom; }
        const uint SPI_SETWORKAREA = 0x2F, SPIF_SENDCHANGE = 2, WM_CLOSE = 0x10;
        const int GWL_STYLE = -16, WS_CHILD = 0x40000000, SW_MINIMIZE = 6;
        static WinEventProc eventProc;   // keeps the hook delegate alive

        readonly QuickSearch search;
        readonly List<IntPtr> order = new List<IntPtr>();   // stable button order (oldest first), like the real taskbar
        readonly Dictionary<IntPtr, string> titles = new Dictionary<IntPtr, string>();
        readonly Dictionary<IntPtr, IntPtr> icons = new Dictionary<IntPtr, IntPtr>();
        readonly System.Windows.Forms.Timer settle = new System.Windows.Forms.Timer { Interval = 60 };   // coalesces event bursts
        readonly System.Windows.Forms.Timer clock = new System.Windows.Forms.Timer();
        readonly Font bold;
        readonly float k;
        readonly int barH, startW, clockW, maxButtonW;
        IntPtr active;
        bool workAreaSet;
        string drawn = "";

        public TaskBar(QuickSearch search, float k)
        {
            this.search = search;
            this.k = k;
            FormBorderStyle = FormBorderStyle.None;
            ShowInTaskbar = false;
            StartPosition = FormStartPosition.Manual;
            BackColor = SystemColors.Control;
            DoubleBuffered = true;
            Font = new Font("Tahoma", 8.25f);
            bold = new Font(Font, FontStyle.Bold);
            barH = (int)(28 * k); startW = (int)(56 * k); clockW = (int)(60 * k); maxButtonW = (int)(160 * k);
            Place();
            eventProc = OnWinEvent;
            // foreground, minimize start/end, destroy/show/hide, name change, cloak/uncloak
            foreach (var r in new[] { new[] { 0x3u, 0x3u }, new[] { 0x16u, 0x17u }, new[] { 0x8001u, 0x8003u }, new[] { 0x800Cu, 0x800Cu }, new[] { 0x8017u, 0x8018u } })
                SetWinEventHook(r[0], r[1], IntPtr.Zero, eventProc, 0, 0, 0);   // WINEVENT_OUTOFCONTEXT
            settle.Tick += (s, e) => { settle.Stop(); UpdateNow(); };
            clock.Tick += (s, e) => { clock.Interval = MsToNextMinute(); Invalidate(ClockRect); };
            clock.Interval = MsToNextMinute();
            clock.Start();
            Microsoft.Win32.SystemEvents.DisplaySettingsChanged += (s, e) => { workAreaSet = false; Place(); UpdateNow(); };
        }

        protected override CreateParams CreateParams
        {
            get { var cp = base.CreateParams; cp.ExStyle |= 0x80 | 0x08000000 | 0x8; return cp; }   // tool window, never takes focus, topmost
        }
        protected override bool ShowWithoutActivation { get { return true; } }

        static int MsToNextMinute() { var n = DateTime.Now; return Math.Max(1000, 60000 - n.Second * 1000 - n.Millisecond); }

        void Place()
        {
            var b = Screen.PrimaryScreen.Bounds;
            Bounds = new Rectangle(b.Left, b.Bottom - barH, b.Width, barH);
        }

        // Maximized windows stop above the bar. Session-only (not saved), like any taskbar.
        void SetWorkArea(bool reserve)
        {
            if (workAreaSet == reserve) return;
            var b = Screen.PrimaryScreen.Bounds;
            var r = new RECT { Left = b.Left, Top = b.Top, Right = b.Right, Bottom = b.Bottom - (reserve ? barH : 0) };
            SystemParametersInfo(SPI_SETWORKAREA, 0, ref r, SPIF_SENDCHANGE);
            workAreaSet = reserve;
        }

        public void Release() { if (FindWindow("Shell_TrayWnd", null) == IntPtr.Zero) SetWorkArea(false); }

        void OnWinEvent(IntPtr hook, uint evt, IntPtr hwnd, int idObject, int idChild, uint thread, uint time)
        {
            if (idObject != 0 || idChild != 0 || hwnd == IntPtr.Zero) return;                       // whole windows only
            if (evt != 0x3 && (GetWindowLong(hwnd, GWL_STYLE) & WS_CHILD) != 0) return;            // not controls inside them
            if (!settle.Enabled) settle.Start();
        }

        public void UpdateNow()
        {
            if (FindWindow("Shell_TrayWnd", null) != IntPtr.Zero)   // Explorer is the shell: its taskbar rules, stay out of the way
            {
                if (Visible) Hide();
                workAreaSet = false;   // Explorer manages the work area itself
                return;
            }
            var live = OpenWindows(Handle);
            order.RemoveAll(w => !live.Any(x => x.Key == w));
            foreach (var w in live)
            {
                if (!order.Contains(w.Key)) order.Add(w.Key);
                titles[w.Key] = w.Value;
                if (!icons.ContainsKey(w.Key)) icons[w.Key] = WindowIcon(w.Key);
            }
            foreach (var gone in icons.Keys.Where(w => !order.Contains(w)).ToList()) { icons.Remove(gone); titles.Remove(gone); }
            active = GetForegroundWindow();
            SetWorkArea(true);
            if (IsFullscreen(active)) { if (Visible) Hide(); return; }   // games/videos get the whole screen
            if (!Visible) Show();
            string now = string.Join("|", order.Select(w => w + ":" + titles[w])) + "#" + active;
            if (now != drawn) { drawn = now; Invalidate(); }   // repaint only when something visible changed
        }

        bool IsFullscreen(IntPtr w)
        {
            RECT r;
            if (w == IntPtr.Zero || w == Handle || w == search.Handle || !GetWindowRect(w, out r)) return false;
            var b = Screen.PrimaryScreen.Bounds;
            return r.Left <= b.Left && r.Top <= b.Top && r.Right >= b.Right && r.Bottom >= b.Bottom;
        }

        Rectangle StartRect { get { return new Rectangle(2, 4, startW, barH - 6); } }
        Rectangle ClockRect { get { return new Rectangle(Width - clockW - 2, 4, clockW, barH - 6); } }
        Rectangle ButtonRect(int i)
        {
            int x0 = StartRect.Right + 4, x1 = ClockRect.Left - 4;
            int w = Math.Min(maxButtonW, (x1 - x0) / Math.Max(1, order.Count));
            return new Rectangle(x0 + i * w, 4, w - 3, barH - 6);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            var center = TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine;
            ControlPaint.DrawBorder3D(g, ClientRectangle, Border3DStyle.Raised, Border3DSide.Top);
            ControlPaint.DrawButton(g, StartRect, ButtonState.Normal);
            TextRenderer.DrawText(g, "Start", bold, StartRect, SystemColors.ControlText, center);
            int iconSize = (int)(16 * k);
            for (int i = 0; i < order.Count; i++)
            {
                var r = ButtonRect(i);
                bool on = order[i] == active;
                ControlPaint.DrawButton(g, r, on ? ButtonState.Pushed : ButtonState.Normal);
                int pad = on ? 1 : 0;   // pushed buttons shift their content 1px, like Win98
                var text = new Rectangle(r.X + 4 + pad, r.Y + pad, r.Width - 8, r.Height);
                IntPtr icon;
                if (icons.TryGetValue(order[i], out icon) && icon != IntPtr.Zero)
                {
                    DrawIcon(g, icon, text.X, r.Y + (r.Height - iconSize) / 2 + pad, iconSize);
                    text.X += iconSize + 3;
                    text.Width -= iconSize + 3;
                }
                string title;
                titles.TryGetValue(order[i], out title);
                TextRenderer.DrawText(g, title, on ? bold : Font, text, SystemColors.ControlText,
                    TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPrefix);
            }
            ControlPaint.DrawBorder3D(g, ClockRect, Border3DStyle.SunkenOuter);
            TextRenderer.DrawText(g, DateTime.Now.ToString("HH:mm"), Font, ClockRect, SystemColors.ControlText, center);
        }

        protected override void OnMouseUp(MouseEventArgs e)
        {
            if (StartRect.Contains(e.Location))
            {
                if (e.Button == MouseButtons.Left) search.Toggle();
                else ShowMenu(e.Location,
                    new MenuItem("Task Manager", (s, a) => Process.Start("taskmgr.exe")),
                    new MenuItem("Bring back Explorer", (s, a) => Process.Start("explorer.exe")),   // bar hides itself once Explorer's taskbar is up
                    new MenuItem("-"),
                    new MenuItem("Exit QuickSearch", (s, a) => search.Quit()));
                return;
            }
            for (int i = 0; i < order.Count; i++)
            {
                if (!ButtonRect(i).Contains(e.Location)) continue;
                IntPtr w = order[i];
                if (e.Button == MouseButtons.Right)
                    ShowMenu(e.Location,
                        new MenuItem("Minimize", (s, a) => ShowWindow(w, SW_MINIMIZE)),
                        new MenuItem("Close", (s, a) => PostMessage(w, WM_CLOSE, IntPtr.Zero, IntPtr.Zero)));
                else if (w == GetForegroundWindow() && !IsIconic(w)) ShowWindow(w, SW_MINIMIZE);   // click the active one = minimize
                else SwitchTo(w);
                return;
            }
        }

        void ShowMenu(Point at, params MenuItem[] items)
        {
            SetForegroundWindow(Handle);   // popup menus only close on click-away if their owner is foreground
            new ContextMenu(items).Show(this, at);
        }
    }

    // ---- Alt+Tab: a small pre-built list of open windows. Tab / Shift+Tab move, releasing Alt switches, Esc cancels. ----
    class Switcher : Form
    {
        const int MaxRows = 14;
        readonly float k;
        readonly int rowH, pad;
        List<KeyValuePair<IntPtr, string>> items = new List<KeyValuePair<IntPtr, string>>();
        List<IntPtr> itemIcons = new List<IntPtr>();
        int sel, visibleRows = MaxRows;

        public Switcher(float k)
        {
            this.k = k;
            FormBorderStyle = FormBorderStyle.None;
            ShowInTaskbar = false;
            StartPosition = FormStartPosition.Manual;
            BackColor = Color.FromArgb(24, 28, 34);
            DoubleBuffered = true;
            Font = new Font("Segoe UI", 12f);
            rowH = (int)(72 * k);
            pad = (int)(12 * k);
            CreateHandle();   // built once up front, so opening it costs nothing
        }

        protected override CreateParams CreateParams
        {
            get { var cp = base.CreateParams; cp.ExStyle |= 0x80 | 0x08000000 | 0x8; return cp; }   // tool window, never takes focus, topmost
        }
        protected override bool ShowWithoutActivation { get { return true; } }

        public void Open(int step)
        {
            items = OpenWindows(IntPtr.Zero);   // Z-order: [0] is the window you're in now
            if (items.Count == 0) return;
            itemIcons = items.Select(x => WindowIcon(x.Key, true)).ToList();
            sel = step > 0 ? Math.Min(1, items.Count - 1) : items.Count - 1;   // like Windows: first Tab = previous window
            var b = Screen.PrimaryScreen.WorkingArea;
            visibleRows = Math.Max(1, Math.Min(MaxRows, (b.Height - 2 * pad) / rowH));
            int w = Math.Min((int)(720 * k), b.Width - 2 * pad), hgt = Math.Min(items.Count, visibleRows) * rowH + 2 * pad;
            Bounds = new Rectangle(b.Left + (b.Width - w) / 2, b.Top + (b.Height - hgt) / 2, w, hgt);
            Show();
            Invalidate();
        }

        public void Step(int d)
        {
            if (!Visible || items.Count == 0) return;
            sel = (sel + d + items.Count) % items.Count;
            Invalidate();
        }

        public void Commit()
        {
            if (!Visible) return;
            Hide();
            if (sel < items.Count) SwitchTo(items[sel].Key);   // Alt was just pressed, so Windows allows the focus change
        }

        public void Cancel() { Hide(); }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            using (var edge = new Pen(Color.FromArgb(80, 92, 104))) g.DrawRectangle(edge, 0, 0, Width - 1, Height - 1);
            int shown = Math.Min(items.Count, visibleRows), first = Math.Max(0, sel - shown + 1), iconSize = (int)(48 * k);
            using (var selected = new SolidBrush(Color.FromArgb(40, 67, 79)))
            using (var tile = new SolidBrush(Color.FromArgb(82, 91, 103)))
            using (var accent = new SolidBrush(Color.FromArgb(99, 222, 194)))
            for (int i = 0; i < shown; i++)
            {
                int idx = first + i;
                bool on = idx == sel;
                var r = new Rectangle(pad, pad + i * rowH, Width - 2 * pad, rowH);
                if (on) { g.FillRectangle(selected, r); g.FillRectangle(accent, r.X, r.Y, Math.Max(2, (int)(3 * k)), r.Height); }
                int inset = (int)(12 * k), tilePad = (int)(5 * k);
                var iconRect = new Rectangle(r.X + inset, r.Y + (rowH - iconSize) / 2, iconSize, iconSize);
                g.FillRectangle(tile, Rectangle.Inflate(iconRect, tilePad, tilePad));
                DrawIcon(g, itemIcons[idx], iconRect.X, iconRect.Y, iconSize);
                int textX = iconRect.Right + (int)(20 * k);
                TextRenderer.DrawText(g, items[idx].Value, Font, new Rectangle(textX, r.Y, r.Right - textX - inset, r.Height),
                    Color.FromArgb(239, 243, 247),
                    TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPrefix);
            }
        }
    }

    // ---- Screenshot: Win+Shift+S / PrtScn freeze the screen; drag a box, or click for the whole monitor. ----
    // Goes to the clipboard and Pictures\Screenshots. Esc or right-click cancels.
    class Snip : Form
    {
        readonly Bitmap shot;
        Point start, cur;
        Rectangle lastSel;
        bool dragging;

        public static void Take()
        {
            var vs = SystemInformation.VirtualScreen;
            var bmp = new Bitmap(vs.Width, vs.Height, System.Drawing.Imaging.PixelFormat.Format32bppRgb);
            using (var g = Graphics.FromImage(bmp)) g.CopyFromScreen(vs.Location, Point.Empty, vs.Size);
            var f = new Snip(bmp, vs);
            f.Show();
            f.Activate();
            SetForegroundWindow(f.Handle);
        }

        Snip(Bitmap shot, Rectangle vs)
        {
            this.shot = shot;
            FormBorderStyle = FormBorderStyle.None;
            ShowInTaskbar = false;
            StartPosition = FormStartPosition.Manual;
            TopMost = true;
            DoubleBuffered = true;
            Cursor = Cursors.Cross;
            Bounds = vs;
        }

        Rectangle Selection
        {
            get { return Rectangle.FromLTRB(Math.Min(start.X, cur.X), Math.Min(start.Y, cur.Y), Math.Max(start.X, cur.X), Math.Max(start.Y, cur.Y)); }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.DrawImageUnscaled(shot, 0, 0);   // clipped to the invalidated area, so dragging stays cheap
            using (var dim = new SolidBrush(Color.FromArgb(120, 0, 0, 0)))
            {
                var all = ClientRectangle;
                if (!dragging) { g.FillRectangle(dim, all); return; }
                var s = Selection;
                g.FillRectangle(dim, Rectangle.FromLTRB(all.Left, all.Top, all.Right, s.Top));
                g.FillRectangle(dim, Rectangle.FromLTRB(all.Left, s.Bottom, all.Right, all.Bottom));
                g.FillRectangle(dim, Rectangle.FromLTRB(all.Left, s.Top, s.Left, s.Bottom));
                g.FillRectangle(dim, Rectangle.FromLTRB(s.Right, s.Top, all.Right, s.Bottom));
                g.DrawRectangle(Pens.White, s.X, s.Y, Math.Max(0, s.Width - 1), Math.Max(0, s.Height - 1));
            }
        }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            if (e.Button == MouseButtons.Right) { Close(); return; }
            start = cur = e.Location;
            dragging = true;
            lastSel = Rectangle.Empty;
            Invalidate();
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            if (!dragging) return;
            cur = e.Location;
            var s = Selection;
            var dirty = lastSel.IsEmpty ? s : Rectangle.Union(lastSel, s);
            dirty.Inflate(2, 2);
            Invalidate(dirty);   // repaint only what the box touched
            lastSel = s;
        }

        protected override void OnMouseUp(MouseEventArgs e)
        {
            if (!dragging) return;
            dragging = false;
            var s = Selection;
            if (s.Width < 3 || s.Height < 3)   // a click: the whole monitor under the cursor
            {
                var m = Screen.FromPoint(PointToScreen(e.Location)).Bounds;
                s = new Rectangle(m.X - Left, m.Y - Top, m.Width, m.Height);
            }
            s.Intersect(new Rectangle(Point.Empty, shot.Size));
            Hide();
            if (s.Width > 0 && s.Height > 0)
            {
                using (var crop = shot.Clone(s, shot.PixelFormat))
                {
                    try { Clipboard.SetImage(crop); } catch { }   // clipboard briefly locked by another app: the file still gets saved
                    string dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyPictures), "Screenshots");
                    Directory.CreateDirectory(dir);
                    crop.Save(Path.Combine(dir, "Screenshot " + DateTime.Now.ToString("yyyy-MM-dd HHmmss") + ".png"), System.Drawing.Imaging.ImageFormat.Png);
                }
            }
            Close();
        }

        protected override void OnKeyDown(KeyEventArgs e) { if (e.KeyCode == Keys.Escape) Close(); }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            shot.Dispose();
            base.OnFormClosed(e);
        }
    }

    static void SelfTest()
    {
        Check(Score("Visual Studio Code", "vis") > Score("Microsoft Visual Studio", "vis"), "prefix beats word start");
        Check(Score("Microsoft Visual Studio", "vis") > Score("Television", "vis"), "word start beats substring");
        Check(Score("Television", "vis") > Score("Visual Studio Code", "vsc"), "substring beats initials");
        Check(Score("Visual Studio Code", "vsc") >= 0, "initials match");
        Check(Score("Notepad", "xyz") < 0, "no match");
        Check(IsFavorite("Blender 5.2") && !IsFavorite("Calculator"), "favorites by name prefix");
        var sw = Stopwatch.StartNew();
        var found = LoadApps();
        Console.WriteLine("apps: " + found.Count + " in " + sw.ElapsedMilliseconds + " ms");
        Check(found.Count > 20, "start menu app list loads");
        Console.WriteLine("favorites found: " + string.Join(", ", found.Where(a => IsFavorite(a[0])).Select(a => a[0])));
        using (var b = new Bitmap(8, 8))
        using (var g = Graphics.FromImage(b)) g.CopyFromScreen(0, 0, 0, 0, b.Size);   // what the screenshot tool relies on
        Console.WriteLine("screen capture ok");
        var open = OpenWindows(IntPtr.Zero);
        Console.WriteLine("open windows: " + string.Join(" | ", open.Select(w => w.Value)));
        Check(open.Count > 0, "open windows listed");
        Console.WriteLine("selftest ok");
    }

    static void Check(bool ok, string what)
    {
        if (ok) return;
        Console.WriteLine("FAIL: " + what);
        Environment.Exit(1);
    }

    [STAThread]
    static void Main(string[] args)
    {
        string arg = args.Length > 0 ? args[0] : "";
        if (arg == "--selftest") { SelfTest(); return; }
        if (arg == "--uninstall") { File.Delete(StartupLink); return; }
        if (arg == "--install")
        {
            dynamic sh = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell"));
            dynamic lnk = sh.CreateShortcut(StartupLink);
            lnk.TargetPath = Application.ExecutablePath;
            lnk.Save();
            Process.Start(Application.ExecutablePath);
            return;
        }
        SetProcessDPIAware();
        bool first;
        using (new Mutex(true, "QuickSearch.LeanBar.single", out first))
        {
            if (!first)   // already running: "--toggle" pokes that copy, anything else just exits
            {
                if (arg == "--toggle")
                {
                    AllowSetForegroundWindow(0xFFFFFFFF);   // we got the click, let the running copy take focus
                    long h;
                    if (long.TryParse(ReadText(HwndFile), out h)) PostMessage((IntPtr)h, ToggleMsg, IntPtr.Zero, IntPtr.Zero);
                }
                return;
            }
            instance = new QuickSearch();
            if (arg == "--toggle") instance.BeginInvoke(new Action(instance.Toggle));
            Application.Run();
        }
    }
}
