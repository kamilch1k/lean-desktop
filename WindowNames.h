#pragma once
#include <cwctype>

// Shared identity helpers. Titles are matched only to an exact process ID;
// a browser audio subprocess cannot safely identify the tab producing sound.
static std::wstring LowerName(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return value;
}
static inline std::wstring WindowProcessPath(DWORD pid) {
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    wchar_t path[32768]{}; DWORD size = 32768;
    if (p) { QueryFullProcessImageName(p, 0, path, &size); CloseHandle(p); }
    return path;
}
static std::wstring WindowAppName(const std::wstring& path, const std::wstring& cls = L"") {
    auto name = path.substr(path.find_last_of(L"\\/") + 1), lower = LowerName(name);
    if (lower == L"robloxstudiobeta.exe" || lower == L"robloxstudio.exe") return L"Roblox Studio";
    if (lower == L"robloxplayerbeta.exe" || lower == L"robloxplayer.exe") return L"Roblox";
    if (cls == L"CabinetWClass" || cls == L"ExploreWClass" || lower == L"explorer.exe" || lower == L"explorer++.exe" || lower == L"winfile.exe") return L"File explorers";
    if (lower == L"chrome.exe") return L"Google Chrome";
    if (lower == L"msedge.exe") return L"Microsoft Edge";
    if (lower == L"firefox.exe") return L"Firefox";
    if (lower == L"code.exe") return L"Visual Studio Code";
    if (lower == L"chatgpt.exe") return LowerName(path).find(L"openai.codex") != std::wstring::npos ? L"Codex" : L"ChatGPT";
    if (lower == L"claude.exe") return L"Claude";
    if (lower == L"discord.exe") return L"Discord";
    if (lower == L"blender.exe") return L"Blender";
    if (lower == L"leancontrols.exe") return L"Lean Controls";
    if (name.empty()) return cls.empty() ? L"Other apps" : cls;
    if (lower.size() > 4 && lower.substr(lower.size() - 4) == L".exe") name.resize(name.size() - 4);
    return name;
}
struct ProcessWindowTitle { DWORD pid; std::wstring title; };
static inline BOOL CALLBACK ReadProcessWindowTitle(HWND w, LPARAM data) {
    if (!IsWindowVisible(w) || (GetWindowLongPtr(w, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) || GetWindow(w, GW_OWNER)) return TRUE;
    wchar_t title[1024]{}; if (!GetWindowText(w, title, 1024)) return TRUE;
    DWORD pid = 0; GetWindowThreadProcessId(w, &pid);
    reinterpret_cast<std::vector<ProcessWindowTitle>*>(data)->push_back({pid, title});
    return TRUE;
}
static inline std::wstring ExactProcessTitle(DWORD pid, const std::vector<ProcessWindowTitle>& titles) {
    std::wstring result;
    for (const auto& item : titles) if (pid && item.pid == pid) {
        if (!result.empty() && result != item.title) return L""; // Multiple documents: do not attribute audio to just one.
        result = item.title;
    }
    return result;
}
