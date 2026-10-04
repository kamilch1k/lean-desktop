// Minimal desktop and folder navigation. No thumbnails, indexer, or polling.
struct DesktopItem { std::wstring name, path; bool directory, shortcut; };
static HWND desktopWindow, desktopList, desktopPath, desktopHome, desktopUp, desktopRefresh;
static HIMAGELIST desktopImages;
static HFONT desktopFont;
static std::wstring desktopFolder;
static std::vector<DesktopItem> desktopItems;
static bool desktopIsPreview, desktopTruncated;
static int folderIcon, fileIcon, shortcutIcon;
static HWND desktopPrevious;
static bool desktopRaised;
static unsigned iconGeneration;
static unsigned desktopIconsResolved;
static constexpr UINT DESK_ICONS = WM_APP + 50;
static constexpr UINT DESK_CHANGED = WM_APP + 51, DESK_REFRESH_TIMER = 51;
struct DesktopWatch {
    HANDLE stop = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    std::vector<HANDLE> changes;
    std::vector<std::wstring> paths;
    ~DesktopWatch() { for (HANDLE h : changes) FindCloseChangeNotification(h); if (stop) CloseHandle(stop); }
};
static std::shared_ptr<DesktopWatch> desktopWatch;
static std::wstring desktopListedFolder;
static void WatchDesktopFolders(const std::vector<std::wstring>& paths) {
    if (desktopWatch && desktopWatch->paths == paths) return;
    if (desktopWatch) SetEvent(desktopWatch->stop);
    desktopWatch.reset();
    auto watch = std::make_shared<DesktopWatch>(); watch->paths = paths;
    if (!watch->stop) return;
    for (const auto& path : paths) {
        HANDLE change = FindFirstChangeNotification(path.c_str(), FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_LAST_WRITE);
        if (change != INVALID_HANDLE_VALUE) watch->changes.push_back(change);
    }
    if (watch->changes.empty()) return;
    desktopWatch = watch; HWND target = desktopWindow;
    std::thread([watch, target] {
        std::vector<HANDLE> handles{watch->stop}; handles.insert(handles.end(), watch->changes.begin(), watch->changes.end());
        for (;;) {
            DWORD signal = WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, INFINITE);
            if (signal == WAIT_OBJECT_0 || signal == WAIT_FAILED || signal >= WAIT_OBJECT_0 + handles.size()) break;
            HANDLE changed = handles[signal-WAIT_OBJECT_0];
            if (!FindNextChangeNotification(changed)) break;
            PostMessage(target, DESK_CHANGED, 0, 0);
        }
    }).detach(); // Stop event wakes the worker; shared ownership protects its handles.
}
struct IconJob { std::wstring path; int index; unsigned generation; };
struct IconResult { int index, image; unsigned generation; };
struct IconLoader {
    std::mutex mutex; std::condition_variable wake;
    std::deque<IconJob> jobs; std::vector<IconResult> results;
    HWND window; bool stop = false;
};
static std::shared_ptr<IconLoader> iconLoader;
static void StartIconLoader() {
    auto loader = std::make_shared<IconLoader>(); loader->window = desktopWindow; iconLoader = loader;
    std::thread([loader] {
        CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        for (;;) {
            IconJob job;
            { std::unique_lock<std::mutex> lock(loader->mutex); loader->wake.wait(lock,[&]{return loader->stop || !loader->jobs.empty();});
              if(loader->stop) break; job=std::move(loader->jobs.front()); loader->jobs.pop_front(); }
            SHFILEINFO info{};
            // Shell's cached icon indices: .lnk targets, executables, URL IconFile, and file associations.
            // Slow icon handlers stay off the UI thread; no favicon downloads are implemented here.
            if(SHGetFileInfo(job.path.c_str(),0,&info,sizeof(info),SHGFI_SYSICONINDEX|SHGFI_LARGEICON)) {
                std::lock_guard<std::mutex> lock(loader->mutex);
                if(!loader->stop) { loader->results.push_back({job.index,info.iIcon,job.generation}); PostMessage(loader->window,DESK_ICONS,0,0); }
            }
        }
        CoUninitialize();
    }).detach(); // shared state remains valid until the current shell-icon call returns
}
static void QueueDesktopIcons() {
    ++iconGeneration;
    if(!iconLoader) return;
    { std::lock_guard<std::mutex> lock(iconLoader->mutex); iconLoader->jobs.clear(); iconLoader->results.clear();
      for(int i=0;i<static_cast<int>(desktopItems.size());++i) iconLoader->jobs.push_back({desktopItems[i].path,i,iconGeneration}); }
    iconLoader->wake.notify_one();
}
static constexpr int DESK_LIST = 500, DESK_HOME = 501, DESK_UP = 502, DESK_REFRESH = 503;
static void ReadDesktopFolder(const std::wstring& folder) {
    WIN32_FIND_DATA f{};
    HANDLE h = FindFirstFile((folder + L"\\*").c_str(), &f);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!wcscmp(f.cFileName, L".") || !wcscmp(f.cFileName, L"..") || (f.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))) continue;
        if (desktopItems.size() >= 4096) { desktopTruncated = true; break; }
        std::wstring name = f.cFileName;
        bool shortcut = name.size() >= 4 && !_wcsicmp(name.c_str() + name.size() - 4, L".lnk");
        desktopItems.push_back({name, folder + L"\\" + name, (f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, shortcut});
    } while (FindNextFile(h, &f));
    FindClose(h);
}
static void RefreshDesktop() {
    KillTimer(desktopWindow, DESK_REFRESH_TIMER);
    std::vector<std::wstring> selected;
    POINT origin{}; ListView_GetOrigin(desktopList, &origin);
    if (desktopListedFolder == desktopFolder) for (int i=0;i<static_cast<int>(desktopItems.size());++i)
        if (ListView_GetItemState(desktopList,i,LVIS_SELECTED)&LVIS_SELECTED) selected.push_back(desktopItems[i].path);
    desktopListedFolder = desktopFolder;
    std::vector<std::wstring> folders;
    if (desktopFolder.empty()) {
        for (const auto& id : {FOLDERID_Desktop,FOLDERID_PublicDesktop}) {
            PWSTR path=nullptr; if (SUCCEEDED(SHGetKnownFolderPath(id,0,nullptr,&path))) { folders.emplace_back(path); CoTaskMemFree(path); }
        }
    } else folders.push_back(desktopFolder);
    WatchDesktopFolders(folders); // Subscribe before enumerating so new changes are not missed.
    desktopItems.clear(); desktopTruncated = false; desktopIconsResolved=0;
    for (const auto& folder : folders) ReadDesktopFolder(folder);
    std::sort(desktopItems.begin(), desktopItems.end(), [](const DesktopItem& a, const DesktopItem& b) {
        return a.directory != b.directory ? a.directory > b.directory : _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    SendMessage(desktopList, WM_SETREDRAW, FALSE, 0); ListView_DeleteAllItems(desktopList);
    for (int i=0; i<static_cast<int>(desktopItems.size()); ++i) {
        auto& item = desktopItems[i]; LVITEM lv{}; lv.mask = LVIF_TEXT | LVIF_IMAGE; lv.iItem = i;
        lv.pszText = const_cast<wchar_t*>(item.name.c_str()); lv.iImage = item.directory ? folderIcon : (item.shortcut ? shortcutIcon : fileIcon);
        ListView_InsertItem(desktopList, &lv);
        if (std::find(selected.begin(),selected.end(),item.path)!=selected.end()) ListView_SetItemState(desktopList,i,LVIS_SELECTED,LVIS_SELECTED);
    }
    POINT now{}; ListView_GetOrigin(desktopList,&now); ListView_Scroll(desktopList,origin.x-now.x,origin.y-now.y);
    SendMessage(desktopList, WM_SETREDRAW, TRUE, 0); InvalidateRect(desktopList, nullptr, TRUE);
    std::wstring label = desktopFolder.empty() ? L"Desktop" : desktopFolder;
    label += L"  |  " + std::to_wstring(desktopItems.size()) + L" items";
    if (desktopTruncated) label += L" (first 4096 shown)";
    SetWindowText(desktopPath, label.c_str()); EnableWindow(desktopUp, !desktopFolder.empty());
    QueueDesktopIcons();
}
static void OpenDesktopItem(int index) {
    if (index < 0 || index >= static_cast<int>(desktopItems.size())) return;
    auto item = desktopItems[index];
    if (item.shortcut) {
        IShellLink* link = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
            IPersistFile* file = nullptr;
            if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
                if (SUCCEEDED(file->Load(item.path.c_str(), STGM_READ))) {
                    wchar_t target[32768]{}, expanded[32768]{};
                    if (SUCCEEDED(link->GetPath(target, 32768, nullptr, SLGP_RAWPATH))) {
                        ExpandEnvironmentStrings(target, expanded, 32768);
                        DWORD attributes = GetFileAttributes(expanded);
                        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) { item.path = expanded; item.directory = true; }
                    }
                }
                file->Release();
            }
            link->Release();
        }
    }
    if (item.directory) { desktopFolder = item.path; RefreshDesktop(); }
    else {
        auto result = reinterpret_cast<INT_PTR>(ShellExecute(desktopWindow, L"open", item.path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (result <= 32) MessageBox(desktopWindow, L"Windows could not open this file or shortcut. Its application may be missing or require Explorer.", L"Lean Desktop", MB_ICONERROR);
    }
}
static void DesktopUp() {
    if (desktopFolder.empty()) return;
    auto p = desktopFolder.find_last_of(L"\\/");
    if (p == std::wstring::npos || p < 3) desktopFolder.clear(); else desktopFolder.resize(p);
    RefreshDesktop();
}
static void PositionDesktop() {
    if (!desktopWindow || desktopIsPreview) return;
    MONITORINFO mi{sizeof(mi)}; GetMonitorInfo(MonitorFromPoint({0,0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    SetWindowPos(desktopWindow, HWND_BOTTOM, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right-mi.rcMonitor.left,
        mi.rcMonitor.bottom-mi.rcMonitor.top, SWP_NOACTIVATE);
}
static void ShowLeanDesktop() {
    if (!desktopWindow) return;
    if(desktopRaised && GetForegroundWindow()==desktopWindow) {
        desktopRaised=false;
        SetWindowPos(desktopWindow,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        if(IsWindow(desktopPrevious) && desktopPrevious!=bar) SetForegroundWindow(desktopPrevious);
        return;
    }
    desktopPrevious=GetForegroundWindow();desktopRaised=true;
    ShowWindow(desktopWindow, SW_RESTORE); SetWindowPos(desktopWindow, HWND_TOP, 0,0,0,0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(desktopWindow); SetFocus(desktopList);
}
static void DestroyDesktop() {
    if (desktopWatch) { SetEvent(desktopWatch->stop); desktopWatch.reset(); }
    if (desktopWindow) KillTimer(desktopWindow,DESK_REFRESH_TIMER);
    if(iconLoader) { {std::lock_guard<std::mutex> lock(iconLoader->mutex);iconLoader->stop=true;iconLoader->jobs.clear();iconLoader->results.clear();} iconLoader->wake.notify_one();iconLoader.reset(); }
    if (desktopWindow) DestroyWindow(desktopWindow); desktopWindow = nullptr;
    // The system owns this shared image list; never destroy or add images to it.
    desktopImages = nullptr;
    if (desktopFont) DeleteObject(desktopFont); desktopFont = nullptr;
}
static void LayoutDesktop() {
    RECT rc; GetClientRect(desktopWindow, &rc);
    UINT d = GetDpiForWindow(desktopWindow); int row = MulDiv(40,d,96), button = MulDiv(82,d,96), pad = MulDiv(5,d,96);
    for (int i=0;i<3;++i) MoveWindow(i==0 ? desktopHome : i==1 ? desktopUp : desktopRefresh, pad+i*(button+pad), pad, button, row-2*pad, TRUE);
    MoveWindow(desktopPath, 3*(button+pad)+pad, pad, std::max(1, static_cast<int>(rc.right)-3*(button+pad)-2*pad), row-2*pad, TRUE);
    int bottom = desktopIsPreview ? 0 : MulDiv(36,d,96);
    MoveWindow(desktopList, 0, row, rc.right, std::max(1,static_cast<int>(rc.bottom)-row-bottom), TRUE);
}
static LRESULT CALLBACK DesktopProcedure(HWND w, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
        case WM_CREATE: desktopWindow = w; return 0;
        case DESK_CHANGED: SetTimer(w,DESK_REFRESH_TIMER,250,nullptr); return 0;
        case WM_TIMER: if(wp==DESK_REFRESH_TIMER) RefreshDesktop(); return 0;
        case WM_SIZE: if (desktopList) LayoutDesktop(); return 0;
        case WM_DPICHANGED: {
            auto r = reinterpret_cast<RECT*>(lp); SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER | SWP_NOACTIVATE); return 0;
        }
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE && !desktopIsPreview && reinterpret_cast<HWND>(lp) != bar) {
                desktopRaised=false;
                SetWindowPos(w,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            }
            return 0;
        case DESK_ICONS: {
            if(!iconLoader) return 0;
            std::vector<IconResult> results;
            {std::lock_guard<std::mutex> lock(iconLoader->mutex);results.swap(iconLoader->results);}
            for(auto& result:results) if(result.generation==iconGeneration && result.index < static_cast<int>(desktopItems.size())) {
                LVITEM item{};item.mask=LVIF_IMAGE;item.iItem=result.index;item.iImage=result.image;ListView_SetItem(desktopList,&item);++desktopIconsResolved;
            }
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp)==DESK_HOME) { desktopFolder.clear(); RefreshDesktop(); }
            if (LOWORD(wp)==DESK_UP) DesktopUp();
            if (LOWORD(wp)==DESK_REFRESH) RefreshDesktop();
            return 0;
        case WM_NOTIFY: {
            auto header = reinterpret_cast<NMHDR*>(lp);
            if (header->hwndFrom == desktopList) {
                if (header->code == NM_DBLCLK) OpenDesktopItem(reinterpret_cast<NMITEMACTIVATE*>(lp)->iItem);
                if (header->code == LVN_KEYDOWN) {
                    WORD key = reinterpret_cast<NMLVKEYDOWN*>(lp)->wVKey;
                    if (key==VK_RETURN) OpenDesktopItem(ListView_GetNextItem(desktopList,-1,LVNI_SELECTED));
                    if (key==VK_F5) RefreshDesktop();
                    if (key==VK_BACK) DesktopUp();
                }
            }
            return 0;
        }
        case WM_CONTEXTMENU: {
            POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; if (p.x==-1) GetCursorPos(&p);
            HMENU menu = CreatePopupMenu(); AppendMenu(menu,MF_STRING,1,L"Open selected item"); AppendMenu(menu,MF_STRING,2,L"Refresh\tF5");
            AppendMenu(menu,MF_STRING,3,L"Back to Desktop"); AppendMenu(menu,MF_SEPARATOR,0,nullptr);
            AppendMenu(menu,MF_STRING,4,L"Close Lean Desktop and taskbar");
            int choice = TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,w,nullptr); DestroyMenu(menu);
            if(choice==1) OpenDesktopItem(ListView_GetNextItem(desktopList,-1,LVNI_SELECTED));
            if(choice==2) RefreshDesktop(); if(choice==3) { desktopFolder.clear(); RefreshDesktop(); }
            if(choice==4) PostMessage(bar,WM_CLOSE,0,0); return 0;
        }
        case WM_CLOSE: PostMessage(bar,WM_CLOSE,0,0); return 0;
    }
    return DefWindowProc(w,message,wp,lp);
}
static bool CreateDesktopView(bool preview) {
    desktopIsPreview = preview;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&controls);
    WNDCLASS cls{}; cls.hInstance = instance; cls.lpfnWndProc = DesktopProcedure; cls.lpszClassName = L"LeanBar.Desktop";
    cls.hCursor = LoadCursor(nullptr,IDC_ARROW); cls.hbrBackground = GetSysColorBrush(COLOR_BTNFACE); RegisterClass(&cls);
    desktopWindow = CreateWindowEx(preview ? WS_EX_APPWINDOW : WS_EX_TOOLWINDOW,L"LeanBar.Desktop",L"Lean Desktop",(preview ? WS_OVERLAPPEDWINDOW : WS_POPUP)|WS_CLIPCHILDREN,
        120,120,1000,620,nullptr,nullptr,instance,nullptr);
    if (!desktopWindow) return false;
    desktopFont = CreateFont(-MulDiv(13,GetDpiForWindow(desktopWindow),96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    desktopHome = CreateWindow(L"BUTTON",L"Desktop",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,0,0,desktopWindow,reinterpret_cast<HMENU>(static_cast<INT_PTR>(DESK_HOME)),instance,nullptr);
    desktopUp = CreateWindow(L"BUTTON",L"Up",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,0,0,desktopWindow,reinterpret_cast<HMENU>(static_cast<INT_PTR>(DESK_UP)),instance,nullptr);
    desktopRefresh = CreateWindow(L"BUTTON",L"Refresh",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,0,0,desktopWindow,reinterpret_cast<HMENU>(static_cast<INT_PTR>(DESK_REFRESH)),instance,nullptr);
    desktopPath = CreateWindow(L"STATIC",L"Desktop",WS_CHILD|WS_VISIBLE|SS_CENTERIMAGE|SS_PATHELLIPSIS,0,0,0,0,desktopWindow,nullptr,instance,nullptr);
    desktopList = CreateWindowEx(0,WC_LISTVIEW,L"Desktop files and shortcuts",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LVS_ICON|LVS_AUTOARRANGE|LVS_SHOWSELALWAYS|LVS_SHAREIMAGELISTS,
        0,0,0,0,desktopWindow,reinterpret_cast<HMENU>(static_cast<INT_PTR>(DESK_LIST)),instance,nullptr);
    ListView_SetBkColor(desktopList,RGB(15,67,70)); ListView_SetTextBkColor(desktopList,CLR_NONE); ListView_SetTextColor(desktopList,RGB(244,249,248));
    ListView_SetExtendedListViewStyle(desktopList,LVS_EX_DOUBLEBUFFER);
    SHFILEINFO generic{};
    desktopImages=reinterpret_cast<HIMAGELIST>(SHGetFileInfo(L"folder",FILE_ATTRIBUTE_DIRECTORY,&generic,sizeof(generic),SHGFI_USEFILEATTRIBUTES|SHGFI_SYSICONINDEX|SHGFI_LARGEICON));folderIcon=generic.iIcon;
    SHGetFileInfo(L"file",FILE_ATTRIBUTE_NORMAL,&generic,sizeof(generic),SHGFI_USEFILEATTRIBUTES|SHGFI_SYSICONINDEX|SHGFI_LARGEICON);fileIcon=generic.iIcon;
    SHGetFileInfo(L"file.lnk",FILE_ATTRIBUTE_NORMAL,&generic,sizeof(generic),SHGFI_USEFILEATTRIBUTES|SHGFI_SYSICONINDEX|SHGFI_LARGEICON);shortcutIcon=generic.iIcon;
    ListView_SetImageList(desktopList,desktopImages,LVSIL_NORMAL);
    ListView_SetIconSpacing(desktopList,MulDiv(116,GetDpiForWindow(desktopWindow),96),MulDiv(88,GetDpiForWindow(desktopWindow),96));
    for (HWND c : {desktopHome,desktopUp,desktopRefresh,desktopPath,desktopList}) SendMessage(c,WM_SETFONT,reinterpret_cast<WPARAM>(desktopFont),TRUE);
    StartIconLoader(); RefreshDesktop(); PositionDesktop(); LayoutDesktop(); ShowWindow(desktopWindow,SW_SHOWNOACTIVATE); return true;
}
