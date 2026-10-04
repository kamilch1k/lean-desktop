// LeanBar: event-driven native Win32 taskbar, no framework or web runtime.
#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <psapi.h>
#include <commctrl.h>
#include <algorithm>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>

constexpr wchar_t CLASS_NAME[] = L"LeanBar.Window";
constexpr UINT REFRESH = WM_APP + 1;
constexpr UINT WRITE_STATUS = WM_APP + 2;
constexpr UINT CLOCK_TIMER = 1, UPDATE_TIMER = 2;
constexpr int ID_SEARCH = 10, ID_MORE = 11, ID_CLOCK = 12, ID_SOUND = 13, ID_BLUETOOTH = 14, ID_NETWORK = 15, ID_FIRST = 100;
struct Recovery { HWND taskbar; BOOL showTaskbar, areaChanged, restartExplorer; RECT before, applied; };
struct Task { HWND window, button; std::wstring title; HICON icon; };
struct Layout { int count, width; bool overflow; };
static HWND bar, searchButton, moreButton, clockButton, soundButton, bluetoothButton, networkButton;
static HFONT font;
static HINSTANCE instance;
static std::vector<Task> tasks;
static std::vector<HWINEVENTHOOK> hooks;
static HWND active;
static bool replaceMode, demoMode, desktopMode, desktopPreview, companionMode, takeoverMode, queued, closing, fullscreen, positioning;
static UINT dpi = 96, taskbarCreated, showDesktopMessage, overviewMessage;
static int barHeight = 36;
static HANDLE mapping, guardProcess;
static Recovery* recovery;
static std::wstring baseDir, quickSearch;
#include "Desktop.h"
static unsigned refreshCount, paintCount;
static bool winDNative,appsHotkey;

static int Scale(int n) { return MulDiv(n, dpi, 96); }
static bool SameRect(const RECT& a, const RECT& b) { return EqualRect(&a, &b) != FALSE; }
static bool SetSessionWorkArea(const RECT& area) {
    RECT value=area;
    if(!SystemParametersInfo(SPI_SETWORKAREA,0,&value,0))return false;
    // SPIF_SENDCHANGE synchronously waits on other apps (for example a busy
    // Blender). Notify them asynchronously so startup and recovery stay usable.
    SendNotifyMessage(HWND_BROADCAST,WM_SETTINGCHANGE,SPI_SETWORKAREA,0);
    return true;
}
static MONITORINFO Primary() {
    MONITORINFO mi{ sizeof(mi) }; GetMonitorInfo(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi); return mi;
}
static Layout ComputeLayout(int space, int count, int minWidth, int maxWidth, int moreWidth) {
    if (count <= 0 || space <= 0) return {0, 0, count > 0};
    bool overflow = count > space / minWidth;
    int capacity = std::max(0, (space - (overflow ? moreWidth : 0)) / minWidth);
    int visible = std::min(count, capacity);
    return {visible, visible ? std::min(maxWidth, (space - (overflow ? moreWidth : 0)) / visible) : 0, overflow};
}
static bool TaskWindow(HWND w) {
    if (!IsWindowVisible(w) || w == bar) return false;
    LONG_PTR ex = GetWindowLongPtr(w, GWL_EXSTYLE), style = GetWindowLongPtr(w, GWL_STYLE);
    if (style & WS_CHILD || ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) return false;
    if (GetWindow(w, GW_OWNER) && !(ex & WS_EX_APPWINDOW)) return false;
    wchar_t cls[128]{}; GetClassName(w, cls, 128);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Shell_SecondaryTrayWnd")) return false;
    DWORD cloaked = 0; DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    return !cloaked && GetWindowTextLength(w) > 0;
}
static BOOL CALLBACK Collect(HWND w, LPARAM data) {
    if (TaskWindow(w)) reinterpret_cast<std::vector<HWND>*>(data)->push_back(w);
    return TRUE;
}
static HICON CopyWindowIcon(HWND w, bool large = false) {
    DWORD_PTR result = 0;
    SendMessageTimeout(w, WM_GETICON, large ? ICON_BIG : ICON_SMALL2, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 20, &result);
    HICON icon = reinterpret_cast<HICON>(result);
    if (!icon) icon = reinterpret_cast<HICON>(GetClassLongPtr(w, large ? GCLP_HICON : GCLP_HICONSM));
    if (!icon) icon = LoadIcon(nullptr, IDI_APPLICATION);
    return CopyIcon(icon);
}
#include "Overview.h"
static HWND Button(int id, const wchar_t* title) {
    HWND w = CreateWindowEx(0, L"BUTTON", title, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, bar, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    SendMessage(w, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    return w;
}
static void SetFontForDpi() {
    HFONT old = font;
    font = CreateFont(-Scale(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    for (HWND w : {searchButton, moreButton, clockButton,soundButton,bluetoothButton,networkButton}) if (w) SendMessage(w, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    for (auto& t : tasks) SendMessage(t.button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    if (old) DeleteObject(old);
}
static void LayoutButtons() {
    RECT rc; GetClientRect(bar, &rc);
    int pad = Scale(4), gap = Scale(4), left = Scale(116), clockWidth = Scale(76), moreWidth = Scale(40);
    int h = std::max(1, static_cast<int>(rc.bottom) - pad * 2), right = rc.right - clockWidth - pad;
    MoveWindow(searchButton, pad, pad, left - pad - gap, h, TRUE);
    MoveWindow(clockButton, right, pad, clockWidth, h, TRUE);
    right-=Scale(62);MoveWindow(networkButton,right,pad,Scale(58),h,TRUE);
    right-=Scale(82);MoveWindow(bluetoothButton,right,pad,Scale(78),h,TRUE);
    right-=Scale(66);MoveWindow(soundButton,right,pad,Scale(62),h,TRUE);
    auto layout = ComputeLayout(right - gap - left, static_cast<int>(tasks.size()), Scale(90), Scale(204), moreWidth);
    for (int i = 0; i < static_cast<int>(tasks.size()); ++i) {
        auto& t = tasks[i]; SetWindowLongPtr(t.button, GWLP_ID, ID_FIRST + i);
        if (i < layout.count) { MoveWindow(t.button, left + i * layout.width, pad, layout.width - gap, h, TRUE); ShowWindow(t.button, SW_SHOWNA); }
        else ShowWindow(t.button, SW_HIDE);
    }
    MoveWindow(moreButton, right - moreWidth - gap, pad, moreWidth, h, TRUE);
    ShowWindow(moreButton, layout.overflow ? SW_SHOWNA : SW_HIDE);
}
static bool IsFullscreen(HWND w) {
    if (!w || w == bar || IsIconic(w) || !TaskWindow(w)) return false;
    if (MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST) != MonitorFromPoint({0,0}, MONITOR_DEFAULTTOPRIMARY)) return false;
    RECT r{}; if (!GetWindowRect(w, &r)) return false;
    auto m = Primary().rcMonitor;
    // Maximized decorated windows can extend past the screen. They are not fullscreen.
    if ((GetWindowLongPtr(w, GWL_STYLE) & WS_CAPTION) == WS_CAPTION) return false;
    return r.left <= m.left && r.top <= m.top && r.right >= m.right && r.bottom >= m.bottom;
}
static void Restore(Recovery* r) {
    if (!r) return;
    if (r->areaChanged) {
        RECT now{}; SystemParametersInfo(SPI_GETWORKAREA, 0, &now, 0);
        if (SameRect(now, r->applied)) SetSessionWorkArea(r->before);
        r->areaChanged = FALSE;
    }
    if (r->showTaskbar && IsWindow(r->taskbar)) {
        wchar_t cls[64]{}; GetClassName(r->taskbar, cls, 64);
        if (!wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"LeanBar.RecoveryFixture")) ShowWindowAsync(r->taskbar, SW_SHOWNA);
    }
    r->showTaskbar = FALSE;
    if (r->restartExplorer && !FindWindow(L"Shell_TrayWnd", nullptr)) {
        wchar_t windows[MAX_PATH]{}; GetWindowsDirectory(windows,MAX_PATH);
        std::wstring exe = std::wstring(windows) + L"\\explorer.exe";
        STARTUPINFO si{sizeof(si)}; PROCESS_INFORMATION pi{};
        if (CreateProcess(exe.c_str(),nullptr,nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    }
    r->restartExplorer = FALSE;
}
static bool StartGuard() {
    std::wstring name = L"Local\\LeanBar.Recovery." + std::to_wstring(GetCurrentProcessId());
    mapping = CreateFileMapping(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Recovery), name.c_str());
    if (!mapping) return false;
    recovery = static_cast<Recovery*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Recovery)));
    if (!recovery) return false;
    *recovery = {};
    HANDLE ready = CreateEvent(nullptr, TRUE, FALSE, (name + L".Ready").c_str());
    if (!ready) return false;
    wchar_t exe[MAX_PATH]; GetModuleFileName(nullptr, exe, MAX_PATH);
    std::wstring command = L"\"" + std::wstring(exe) + L"\" --guard " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFO si{sizeof(si)}; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    bool ok = CreateProcess(exe, &command[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != FALSE;
    if (ok) { guardProcess = pi.hProcess; CloseHandle(pi.hThread); ok = WaitForSingleObject(ready, 5000) == WAIT_OBJECT_0; }
    CloseHandle(ready); return ok;
}
static int Guard(DWORD pid) {
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!parent) return 2;
    std::wstring name = L"Local\\LeanBar.Recovery." + std::to_wstring(pid);
    HANDLE map = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    auto r = map ? static_cast<Recovery*>(MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Recovery))) : nullptr;
    HANDLE ready = OpenEvent(EVENT_MODIFY_STATE, FALSE, (name + L".Ready").c_str());
    if (!r || !ready) { CloseHandle(parent); if (map) CloseHandle(map); return 3; }
    SetEvent(ready); CloseHandle(ready);
    WaitForSingleObject(parent, INFINITE); // zero polling, no CPU wakeups
    Restore(r); UnmapViewOfFile(r); CloseHandle(map); CloseHandle(parent); return 0;
}
static void PositionBar() {
    if (positioning || closing) return;
    positioning = true;
    auto mi = Primary(); RECT pos = mi.rcWork;
    dpi = GetDpiForWindow(bar); barHeight = Scale(36);
    if (demoMode) {
        int width = std::min(Scale(1160), static_cast<int>(pos.right - pos.left));
        SetWindowPos(bar, HWND_TOPMOST, pos.left + (pos.right-pos.left-width)/2, pos.top + Scale(90), width, barHeight + Scale(40), SWP_NOACTIVATE);
        LayoutButtons(); positioning = false; return;
    }
    HWND shell = FindWindow(L"Shell_TrayWnd", nullptr);
    if (replaceMode && shell) {
        // Reuse Explorer's already reserved bottom band; never rewrite its appbar state.
        RECT tray{}; GetWindowRect(shell, &tray);
        pos = mi.rcMonitor;
        int band = mi.rcMonitor.bottom - mi.rcWork.bottom;
        if (band >= Scale(24) && band <= Scale(120)) barHeight = band;
        if (recovery->taskbar != shell) {
            if (recovery->areaChanged) Restore(recovery);
            recovery->taskbar = shell; recovery->showTaskbar = IsWindowVisible(shell);
        }
        ShowWindowAsync(shell, SW_HIDE);
    } else if (replaceMode && !shell) {
        if (!recovery->areaChanged) {
            SystemParametersInfo(SPI_GETWORKAREA, 0, &recovery->before, 0);
            recovery->applied = recovery->before;
            recovery->applied.bottom = mi.rcMonitor.bottom - barHeight;
            recovery->areaChanged = TRUE; // guard knows intent before the mutation
            if(!SetSessionWorkArea(recovery->applied))recovery->areaChanged=FALSE;
        }
        pos = mi.rcMonitor;
    }
    SetWindowPos(bar, HWND_TOPMOST, pos.left, pos.bottom - barHeight, pos.right - pos.left, barHeight, SWP_NOACTIVATE);
    LayoutButtons(); positioning = false;
}
static void Refresh() {
    queued = false; KillTimer(bar, UPDATE_TIMER); ++refreshCount;
    std::vector<HWND> live; EnumWindows(Collect, reinterpret_cast<LPARAM>(&live));
    bool changed = false;
    for (auto it = tasks.begin(); it != tasks.end();) {
        if (std::find(live.begin(), live.end(), it->window) == live.end()) {
            DestroyWindow(it->button); if (it->icon) DestroyIcon(it->icon); it = tasks.erase(it); changed = true;
        } else ++it;
    }
    for (HWND w : live) {
        wchar_t title[512]{}; GetWindowText(w, title, 512);
        auto found = std::find_if(tasks.begin(), tasks.end(), [w](const Task& t){return t.window == w;});
        if (found == tasks.end()) { tasks.push_back({w, Button(ID_FIRST + static_cast<int>(tasks.size()), title), title, CopyWindowIcon(w)}); changed = true; }
        else if (found->title != title) { found->title = title; SetWindowText(found->button, title); InvalidateRect(found->button, nullptr, FALSE); }
    }
    HWND foreground = GetForegroundWindow();
    if (active != foreground) {
        HWND old = active; active = foreground;
        for (auto& t : tasks) if (t.window == old || t.window == active) InvalidateRect(t.button, nullptr, FALSE);
    }
    if (changed) LayoutButtons();
    bool full = !demoMode && IsFullscreen(foreground);
    if (full != fullscreen) { fullscreen = full; ShowWindow(bar, full ? SW_HIDE : SW_SHOWNA); }
    if (overviewWindow) RefreshOverview(live);
}
static void CALLBACK WindowEvent(HWINEVENTHOOK, DWORD event, HWND w, LONG object, LONG child, DWORD, DWORD) {
    if (!bar || closing || !w || object != OBJID_WINDOW || child != CHILDID_SELF || w == bar) return;
    if (GetWindowLongPtr(w, GWL_STYLE) & WS_CHILD) return;
    if (event == EVENT_OBJECT_LOCATIONCHANGE && w != GetForegroundWindow()) return;
    // Explorer can make its bar visible again after a Win key or display change.
    if (replaceMode && recovery && ((w == recovery->taskbar && event == EVENT_OBJECT_DESTROY) ||
        (w == FindWindow(L"Shell_TrayWnd", nullptr) && event == EVENT_OBJECT_SHOW))) {
        PostMessage(bar, REFRESH, 1, 0);
    }
    if (!queued) { queued = true; SetTimer(bar, UPDATE_TIMER, 80, nullptr); }
}
static void Open(const wchar_t* target, const wchar_t* args = nullptr) {
    auto result = reinterpret_cast<INT_PTR>(ShellExecute(bar, L"open", target, args, nullptr, SW_SHOWNORMAL));
    if (result <= 32) MessageBox(bar, L"Windows could not open this item. Check its path in LeanBar.ini.", L"LeanBar", MB_ICONERROR);
}
static void Controls(const wchar_t* page) {
    std::wstring path=baseDir+L"\\LeanControls.exe";
    if(GetFileAttributes(path.c_str())==INVALID_FILE_ATTRIBUTES){MessageBox(bar,L"LeanControls.exe is missing. Rebuild or reinstall the full package.",L"Lean Controls",MB_ICONERROR);return;}
    Open(path.c_str(),page);
}
static void AppManager(){
    auto path=baseDir+L"\\LeanApps.exe";
    if(GetFileAttributes(path.c_str())==INVALID_FILE_ATTRIBUTES){MessageBox(bar,L"LeanApps.exe is missing. Rebuild or reinstall the full package.",L"Lean Apps",MB_ICONERROR);return;}
    Open(path.c_str());
}
static void Search() {
    wchar_t local[MAX_PATH]{}; SHGetFolderPath(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, local);
    std::wifstream file(std::wstring(local) + (companionMode ? L"\\QuickSearch\\lean-hwnd.txt" : L"\\QuickSearch\\hwnd.txt"));
    long long h = 0; file >> h;
    HWND target = reinterpret_cast<HWND>(h); DWORD pid = 0;
    GetWindowThreadProcessId(target, &pid);
    if (IsWindow(target)) {
        // Do not send to a stale/recycled handle unless its process is QuickSearch.
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        wchar_t path[32768]{}; DWORD size = 32768;
        bool matches = process && QueryFullProcessImageName(process, 0, path, &size) &&
            (_wcsicmp(path, quickSearch.c_str()) == 0 || wcsstr(path, L"\\QuickSearch.exe") != nullptr);
        if (process) CloseHandle(process);
        if (matches) { AllowSetForegroundWindow(pid); PostMessage(target, RegisterWindowMessage(L"QuickSearch.Toggle"), 0, 0); return; }
    }
    if (GetFileAttributes(quickSearch.c_str()) != INVALID_FILE_ATTRIBUTES) Open(quickSearch.c_str(), L"--toggle");
    else MessageBox(bar, L"Set QuickSearchPath in LeanBar.ini to your QuickSearch.exe, then restart LeanBar.", L"QuickSearch not found", MB_ICONINFORMATION);
}
static void Activate(HWND w) {
    if (!IsWindow(w)) return;
    if (GetForegroundWindow() == w && !IsIconic(w)) ShowWindowAsync(w, SW_MINIMIZE);
    else { if (IsIconic(w)) ShowWindowAsync(w, SW_RESTORE); SetForegroundWindow(w); }
}
static void Menu(POINT at, HWND target = nullptr, bool all = false) {
    HMENU menu = CreatePopupMenu();
    std::vector<HWND> targets;
    if (all) {
        for (auto& t : tasks) {
            std::wstring title = t.title; size_t offset = 0;
            while ((offset = title.find(L'&', offset)) != std::wstring::npos) { title.insert(offset, 1, L'&'); offset += 2; }
            targets.push_back(t.window); AppendMenu(menu, MF_STRING, 1000 + targets.size(), title.c_str());
        }
    } else if (target) {
        AppendMenu(menu, MF_STRING, 1, L"Activate / restore"); AppendMenu(menu, MF_STRING, 2, L"Minimize");
        AppendMenu(menu, MF_STRING, 3, L"Close window");
    } else {
        AppendMenu(menu, MF_STRING, 10, L"QuickSearch"); AppendMenu(menu, MF_STRING, 20, L"App manager\tCtrl+Alt+Esc");AppendMenu(menu, MF_STRING, 11, L"Windows Task Manager\tCtrl+Shift+Esc");
        AppendMenu(menu, MF_STRING, 12, L"Volume mixer"); AppendMenu(menu, MF_STRING, 13, L"Network connections");
        AppendMenu(menu, MF_STRING, 18, L"Bluetooth devices");
        AppendMenu(menu, MF_STRING, 14, L"Date and time settings");
        if (desktopWindow) AppendMenu(menu, MF_STRING, 17, L"Show desktop\tWin+D");
        AppendMenu(menu, MF_STRING, 19, L"Window overview\tWin+Tab");
        AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenu(menu, MF_STRING, 15, L"About LeanBar");
        AppendMenu(menu, MF_STRING, 16, L"Exit and restore taskbar\tCtrl+Alt+F12");
    }
    SetForegroundWindow(bar);
    UINT id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, at.x, at.y, 0, bar, nullptr);
    DestroyMenu(menu); PostMessage(bar, WM_NULL, 0, 0);
    if (all && id > 1000 && id <= 1000 + targets.size()) Activate(targets[id - 1001]);
    else if (target) {
        if (id == 1) { if (IsIconic(target)) ShowWindowAsync(target, SW_RESTORE); SetForegroundWindow(target); }
        if (id == 2) ShowWindowAsync(target, SW_MINIMIZE);
        if (id == 3) PostMessage(target, WM_CLOSE, 0, 0);
    } else switch (id) {
        case 10: Search(); break;
        case 11: Open(L"taskmgr.exe"); break;
        case 12: Controls(L"--sound"); break;
        case 13: Controls(L"--network"); break;
        case 14: Open(L"control.exe", L"timedate.cpl"); break;
        case 15: MessageBox(bar, L"LeanBar 1.0\nNative Windows taskbar + QuickSearch\n\nClick an app to switch; click again to minimize.\nRight-click an app for window actions.\nCtrl+Alt+F12 restores the original taskbar and exits.\n\nPrimary monitor only. No notification tray hosting.\nExplorer remains running in replacement mode.\nNo startup, registry, security or service changes.", L"LeanBar", MB_OK); break;
        case 16: PostMessage(bar, WM_CLOSE, 0, 0); break;
        case 17: ShowLeanDesktop(); break;
        case 18: Controls(L"--bluetooth"); break;
        case 19: ToggleOverview(); break;
        case 20: AppManager(); break;
    }
}
static void PaintButton(const DRAWITEMSTRUCT* d) {
    ++paintCount;
    bool selected = false; Task* task = nullptr;
    if (d->CtlID >= ID_FIRST && d->CtlID < ID_FIRST + tasks.size()) { task = &tasks[d->CtlID - ID_FIRST]; selected = task->window == active; }
    HDC dc = d->hDC; RECT r = d->rcItem;
    COLORREF bg = selected ? RGB(35, 65, 75) : (d->itemState & ODS_SELECTED ? RGB(52, 56, 62) : RGB(31, 34, 39));
    SetDCBrushColor(dc, bg); FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    if (selected) { RECT line = r; line.top = line.bottom - Scale(2); SetDCBrushColor(dc, RGB(97, 213, 190)); FillRect(dc, &line, static_cast<HBRUSH>(GetStockObject(DC_BRUSH))); }
    SelectObject(dc, font); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(229, 233, 236));
    r.left += Scale(9); r.right -= Scale(8);
    if (task && task->icon) { int s = Scale(16); DrawIconEx(dc, r.left, (r.bottom - s)/2, task->icon, s, s, 0, nullptr, DI_NORMAL); r.left += s + Scale(7); }
    wchar_t text[512]{}; GetWindowText(d->hwndItem, text, 512);
    DrawText(dc, text, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | (task ? DT_LEFT : DT_CENTER));
    if (d->itemState & ODS_FOCUS) DrawFocusRect(dc, &r);
}
static void Clock() {
    SYSTEMTIME st; GetLocalTime(&st); wchar_t text[64]; GetTimeFormat(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, text, 64);
    SetWindowText(clockButton, text);
    SetTimer(bar, CLOCK_TIMER, std::max(1000u, 60000u - st.wSecond * 1000u - st.wMilliseconds), nullptr);
}
static void WriteStatus() {
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb=sizeof(memory); GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
    RECT area{},bounds{}; SystemParametersInfo(SPI_GETWORKAREA,0,&area,0); GetWindowRect(bar,&bounds);
    std::ofstream out(baseDir+L"\\status.json");
    out << "{\n  \"pid\": " << GetCurrentProcessId() << ",\n  \"desktop\": " << (desktopWindow?"true":"false")
        << ",\n  \"desktopVisible\": " << (desktopWindow && IsWindowVisible(desktopWindow)?"true":"false")
        << ",\n  \"taskbarVisible\": " << (IsWindowVisible(bar)?"true":"false")
        << ",\n  \"explorerTaskbarPresent\": " << (FindWindow(L"Shell_TrayWnd",nullptr)?"true":"false")
        << ",\n  \"taskCount\": " << tasks.size() << ",\n  \"desktopItems\": " << desktopItems.size()
        << ",\n  \"iconsResolved\": " << desktopIconsResolved << ",\n  \"winDRegistered\": " << (winDNative?"true":"false")
        << ",\n  \"appManagerHotkeyRegistered\": " << (appsHotkey?"true":"false")
        << ",\n  \"overviewOpen\": " << (overviewWindow?"true":"false") << ",\n  \"overviewWindows\": " << overviewItems.size()
        << ",\n  \"eventHooks\": " << hooks.size() << ",\n  \"refreshCount\": " << refreshCount
        << ",\n  \"workingSetBytes\": " << memory.WorkingSetSize << ",\n  \"privateBytes\": " << memory.PrivateUsage
        << ",\n  \"workAreaBottom\": " << area.bottom << ",\n  \"barTop\": " << bounds.top << "\n}\n";
}
static bool BeginTakeover() {
    DWORD value=1,bytes=sizeof(value);
    if(RegGetValue(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",L"AutoRestartShell",RRF_RT_REG_DWORD,nullptr,&value,&bytes)!=ERROR_SUCCESS || value!=0) {
        MessageBox(bar,L"Explorer restart-on-crash is enabled. LeanBar left Explorer running. Restore the configuration from the installer before using takeover mode.",L"LeanBar",MB_ICONINFORMATION);return false;
    }
    if(GetFileAttributes(quickSearch.c_str())==INVALID_FILE_ATTRIBUTES) return false;
    std::wstring cmd=L"\""+quickSearch+L"\""; STARTUPINFO si{sizeof(si)}; PROCESS_INFORMATION pi{};
    si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
    if(!CreateProcess(quickSearch.c_str(),&cmd[0],nullptr,nullptr,FALSE,0,nullptr,baseDir.c_str(),&si,&pi)) return false;
    CloseHandle(pi.hThread); WaitForInputIdle(pi.hProcess,5000); CloseHandle(pi.hProcess);
    wchar_t local[MAX_PATH]{};SHGetFolderPath(nullptr,CSIDL_LOCAL_APPDATA,nullptr,0,local);
    bool ready=false;
    for(int i=0;i<50 && !ready;++i) {
        std::wifstream f(std::wstring(local)+L"\\QuickSearch\\lean-hwnd.txt");long long h=0;f>>h;
        HWND q=reinterpret_cast<HWND>(h);DWORD pid=0;GetWindowThreadProcessId(q,&pid);
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);wchar_t path[32768]{};DWORD length=32768;
        ready=process && IsWindow(q) && QueryFullProcessImageName(process,0,path,&length) && !_wcsicmp(path,quickSearch.c_str());
        if(process) CloseHandle(process);if(!ready) Sleep(100);
    }
    if(!ready) return false;
    HWND shell=FindWindow(L"Shell_TrayWnd",nullptr);
    if(shell) {
        DWORD pid=0;GetWindowThreadProcessId(shell,&pid);HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_TERMINATE|SYNCHRONIZE,FALSE,pid);
        wchar_t path[MAX_PATH]{},windows[MAX_PATH]{};DWORD length=MAX_PATH;GetWindowsDirectory(windows,MAX_PATH);
        std::wstring expected=std::wstring(windows)+L"\\explorer.exe";
        if(!process || !QueryFullProcessImageName(process,0,path,&length) || _wcsicmp(path,expected.c_str())) {if(process)CloseHandle(process);return false;}
        bool stopped=TerminateProcess(process,0)!=FALSE;
        if(stopped) stopped=WaitForSingleObject(process,5000)==WAIT_OBJECT_0;CloseHandle(process);
        if(!stopped) return false;
    }
    if(desktopWindow) { winDNative=RegisterHotKey(bar,3,MOD_WIN|MOD_NOREPEAT,'D')!=FALSE; if(winDNative) SetProp(bar,L"LeanBar.WinD",reinterpret_cast<HANDLE>(1)); }
    PositionDesktop();PositionBar();Refresh();WriteStatus();return true;
}
static LRESULT CALLBACK Procedure(HWND w, UINT message, WPARAM wp, LPARAM lp) {
    if (message == showDesktopMessage) { ShowLeanDesktop(); return 0; }
    if (message == overviewMessage) { ToggleOverview(); return 0; }
    if (message == taskbarCreated) { PostMessage(w, REFRESH, 1, 0); return 0; }
    switch (message) {
        case WM_CREATE: bar = w; return 0;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(w, &ps); SetDCBrushColor(dc, RGB(21, 24, 28)); FillRect(dc, &ps.rcPaint, static_cast<HBRUSH>(GetStockObject(DC_BRUSH))); EndPaint(w, &ps); return 0; }
        case WM_DRAWITEM: PaintButton(reinterpret_cast<DRAWITEMSTRUCT*>(lp)); return TRUE;
        case WM_SIZE: if (searchButton) LayoutButtons(); return 0;
        case WM_DISPLAYCHANGE: PositionDesktop(); PostMessage(w, REFRESH, 1, 0); return 0;
        case WM_DPICHANGED: dpi = HIWORD(wp); SetFontForDpi(); PostMessage(w, REFRESH, 1, 0); return 0;
        case WM_SETTINGCHANGE: if (!positioning) PostMessage(w, REFRESH, 1, 0); return 0;
        case REFRESH: if (wp) PositionBar(); Refresh(); return 0;
        case WRITE_STATUS: WriteStatus(); return 0;
        case WM_TIMER:
            if (wp == CLOCK_TIMER) Clock();
            if (wp == UPDATE_TIMER) Refresh();
            return 0;
        case WM_HOTKEY: if (wp == 1) DestroyWindow(w); if (wp == 2 || wp == 3) ShowLeanDesktop();if(wp==4)AppManager();return 0;
        case WM_CONTEXTMENU: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (p.x == -1 && p.y == -1) { RECT rc; GetWindowRect(w, &rc); p = {rc.left + Scale(16), rc.top}; }
            HWND target = nullptr; for (auto& t : tasks) if (t.button == reinterpret_cast<HWND>(wp)) target = t.window;
            Menu(p, target); return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == ID_SEARCH) Search();
            else if(id==ID_SOUND)Controls(L"--sound");
            else if(id==ID_BLUETOOTH)Controls(L"--bluetooth");
            else if(id==ID_NETWORK)Controls(L"--network");
            else if (id == ID_MORE || id == ID_CLOCK) { RECT r; GetWindowRect(id == ID_MORE ? moreButton : clockButton, &r); Menu({r.right, r.top}, nullptr, id == ID_MORE); }
            else if (id >= ID_FIRST && id < ID_FIRST + static_cast<int>(tasks.size())) Activate(tasks[id-ID_FIRST].window);
            return 0;
        }
        case WM_QUERYENDSESSION: return TRUE;
        case WM_ENDSESSION: if (wp) { closing = true; Restore(recovery); } return 0;
        case WM_CLOSE: DestroyWindow(w); return 0;
        case WM_DESTROY:
            closing = true;
            CloseOverview(false);
            for (auto h : hooks) UnhookWinEvent(h); hooks.clear();
            UnregisterHotKey(w, 1); UnregisterHotKey(w, 2); UnregisterHotKey(w,3);UnregisterHotKey(w,4); DestroyDesktop(); Restore(recovery); PostQuitMessage(0); return 0;
    }
    return DefWindowProc(w, message, wp, lp);
}

static int SelfTest() {
    std::wofstream out(baseDir + L"\\selftest.txt"); int failures = 0;
    auto check = [&](bool ok, const wchar_t* name) { out << (ok ? L"PASS " : L"FAIL ") << name << L"\n"; if (!ok) ++failures; };
    for (int space : {0, 1, 60, 180, 800, 3840}) for (int count : {0, 1, 2, 10, 50, 200}) {
        auto l = ComputeLayout(space, count, 90, 204, 40);
        check(l.count >= 0 && l.count <= count && l.width >= 0 && l.count*l.width <= space && (!l.count || l.width >= 90), L"layout bounds");
    }
    HWND normal = CreateWindow(L"STATIC", L"LeanBar test window", WS_OVERLAPPEDWINDOW, 40,40,300,180,nullptr,nullptr,instance,nullptr);
    ShowWindow(normal, SW_SHOWNOACTIVATE); check(TaskWindow(normal), L"normal window included");
    ShowWindow(normal, SW_HIDE); check(!TaskWindow(normal), L"hidden window excluded");
    SetWindowLongPtr(normal, GWL_EXSTYLE, WS_EX_TOOLWINDOW); ShowWindow(normal, SW_SHOWNOACTIVATE); check(!TaskWindow(normal), L"tool window excluded");
    SetWindowLongPtr(normal, GWL_EXSTYLE, WS_EX_APPWINDOW); check(TaskWindow(normal), L"app window included");
    HWND child = CreateWindow(L"STATIC", L"child", WS_CHILD | WS_VISIBLE, 0,0,40,40,normal,nullptr,instance,nullptr); check(!TaskWindow(child), L"child excluded");
    HWND owned = CreateWindow(L"STATIC", L"owned", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50,50,100,100,normal,nullptr,instance,nullptr); check(!TaskWindow(owned), L"owned popup excluded");
    DestroyWindow(owned); DestroyWindow(normal); check(!TaskWindow(normal), L"destroyed window excluded");
    out << L"Failures: " << failures << L"\n"; return failures ? 1 : 0;
}
int WINAPI wWinMain(HINSTANCE h, HINSTANCE, PWSTR, int) {
    instance = h; SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t exe[MAX_PATH]; GetModuleFileName(nullptr, exe, MAX_PATH); baseDir = exe; baseDir.resize(baseDir.find_last_of(L"\\/"));
    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLine(), &argc);
    if (argc >= 3 && !wcscmp(argv[1], L"--guard")) { DWORD pid = wcstoul(argv[2], nullptr, 10); LocalFree(argv); return Guard(pid); }
    // Used only by the isolated recovery test with a fixture window owned by the test runner.
    if (argc >= 3 && !wcscmp(argv[1], L"--test-recovery-child")) {
        HWND fixture = reinterpret_cast<HWND>(_wcstoui64(argv[2], nullptr, 10)); LocalFree(argv);
        wchar_t cls[64]{}; GetClassName(fixture, cls, 64);
        if (wcscmp(cls, L"LeanBar.RecoveryFixture") || !StartGuard()) return 3;
        recovery->taskbar = fixture; recovery->showTaskbar = TRUE;
        ShowWindow(fixture, SW_HIDE); ExitProcess(99);
    }
    bool selftest = false, restore = false, status = false, overview = false;
    for (int i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--replace")) replaceMode = true;
        if (!wcscmp(argv[i], L"--demo")) demoMode = true;
        if (!wcscmp(argv[i], L"--desktop")) desktopMode = true;
        if (!wcscmp(argv[i], L"--desktop-preview")) desktopPreview = true;
        if (!wcscmp(argv[i], L"--companion")) companionMode = true;
        if (!wcscmp(argv[i], L"--takeover")) takeoverMode = desktopMode = companionMode = true;
        if (!wcscmp(argv[i], L"--status")) status = true;
        if (!wcscmp(argv[i], L"--overview")) overview = true;
        if (!wcscmp(argv[i], L"--selftest")) selftest = true;
        if (!wcscmp(argv[i], L"--restore")) restore = true;
    }
    if (demoMode) replaceMode = false;
    if (desktopMode) replaceMode = true;
    LocalFree(argv);
    if (selftest) return SelfTest();
    HWND existing = FindWindow(CLASS_NAME, nullptr);
    if (overview) { if(existing) { DWORD pid=0; GetWindowThreadProcessId(existing,&pid); AllowSetForegroundWindow(pid); PostMessage(existing,RegisterWindowMessage(L"LeanBar.Overview"),0,0); } return existing?0:1; }
    if (status) { if(existing) PostMessage(existing,WRITE_STATUS,0,0); return existing?0:1; }
    if (restore) {
        if (existing) PostMessage(existing, WM_CLOSE, 0, 0);
        else { HWND tray = FindWindow(L"Shell_TrayWnd", nullptr); if (tray) ShowWindow(tray, SW_SHOWNA); }
        return 0;
    }
    HANDLE singleton = CreateMutex(nullptr, FALSE, L"Local\\LeanBar.Single");
    if (!singleton || GetLastError() == ERROR_ALREADY_EXISTS) { if (existing) SetForegroundWindow(existing); if (singleton) CloseHandle(singleton); return 0; }
    wchar_t documents[MAX_PATH]{}; SHGetFolderPath(nullptr, CSIDL_PERSONAL, nullptr, 0, documents);
    std::wstring bundledSearch = baseDir + L"\\QuickSearch.Companion.exe";
    std::wstring defaultSearch = GetFileAttributes(bundledSearch.c_str()) != INVALID_FILE_ATTRIBUTES
        ? bundledSearch : std::wstring(documents) + L"\\QuickSearch\\QuickSearch.exe";
    wchar_t setting[32768]{};
    GetPrivateProfileString(L"LeanBar", L"QuickSearchPath", defaultSearch.c_str(), setting, 32768, (baseDir + L"\\LeanBar.ini").c_str());
    quickSearch = setting;
    if (!_wcsicmp(quickSearch.c_str(), bundledSearch.c_str())) companionMode = true;
    if (companionMode) quickSearch = baseDir + L"\\QuickSearch.Companion.exe";
    if (desktopMode && !takeoverMode && FindWindow(L"Shell_TrayWnd", nullptr)) {
        MessageBox(nullptr, L"Explorer is currently providing your desktop. Use --desktop-preview to try Lean Desktop without stopping Explorer. See README.txt for the reversible shell trial.", L"Lean Desktop", MB_ICONINFORMATION); return 1;
    }
    if (replaceMode && !StartGuard()) { MessageBox(nullptr, L"Could not start taskbar recovery guard. No desktop changes were made.", L"LeanBar", MB_ICONERROR); return 1; }
    if (desktopMode && recovery) recovery->restartExplorer = TRUE;
    taskbarCreated = RegisterWindowMessage(L"TaskbarCreated");
    showDesktopMessage = RegisterWindowMessage(L"LeanBar.ShowDesktop");
    overviewMessage = RegisterWindowMessage(L"LeanBar.Overview");
    WNDCLASS cls{}; cls.hInstance = h; cls.lpfnWndProc = Procedure; cls.lpszClassName = CLASS_NAME; cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClass(&cls);
    bar = CreateWindowEx(demoMode ? WS_EX_APPWINDOW : WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, CLASS_NAME, L"LeanBar", (demoMode ? WS_OVERLAPPEDWINDOW : WS_POPUP) | WS_CLIPCHILDREN, 0,0,800,36,nullptr,nullptr,h,nullptr);
    if (!bar) return 2;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if ((desktopMode || desktopPreview) && !CreateDesktopView(desktopPreview)) {
        Restore(recovery); MessageBox(bar,L"Could not create Lean Desktop. Windows has been restored.",L"LeanBar",MB_ICONERROR); return 2;
    }
    dpi = GetDpiForWindow(bar); SetFontForDpi();
    searchButton = Button(ID_SEARCH, L"QuickSearch"); moreButton = Button(ID_MORE, L"..."); clockButton = Button(ID_CLOCK, L"");
    soundButton=Button(ID_SOUND,L"Sound");bluetoothButton=Button(ID_BLUETOOTH,L"Bluetooth");networkButton=Button(ID_NETWORK,L"Wi-Fi");
    PositionBar(); Refresh(); Clock();
    const DWORD ranges[][2] = {{EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND}, {EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND},
        {EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE}, {EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE},
        {EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED}, {EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE}};
    for (auto& r : ranges) {
        auto hook = SetWinEventHook(r[0], r[1], nullptr, WindowEvent, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        if (hook) hooks.push_back(hook);
    }
    if (hooks.size() != 6) { MessageBox(bar, L"Could not subscribe to window events. Restoring your taskbar.", L"LeanBar", MB_ICONERROR); DestroyWindow(bar); }
    else {
        RegisterHotKey(bar, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F12);
        appsHotkey=RegisterHotKey(bar,4,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,VK_ESCAPE)!=FALSE;
        if (desktopWindow) RegisterHotKey(bar, 2, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'D');
        ShowWindow(bar, fullscreen ? SW_HIDE : SW_SHOWNOACTIVATE);
        if(takeoverMode && !BeginTakeover()) {
            MessageBox(bar,L"The replacement could not start completely. Restoring Explorer. No sign-out or restart is needed.",L"LeanBar",MB_ICONERROR);DestroyWindow(bar);
        }
    }
    MSG msg; while (GetMessage(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessage(&msg); }
    for (auto& t : tasks) if (t.icon) DestroyIcon(t.icon);
    if (font) DeleteObject(font);
    if (recovery) UnmapViewOfFile(recovery); if (mapping) CloseHandle(mapping); if (guardProcess) CloseHandle(guardProcess); CloseHandle(singleton);
    CoUninitialize(); return 0;
}
