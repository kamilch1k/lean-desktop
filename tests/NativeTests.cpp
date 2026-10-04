#define wWinMain LeanBarApplicationMain
#include "../LeanBar.cpp"
#undef wWinMain
#include <gdiplus.h>
#include <iostream>

static void Pump(DWORD duration) {
    DWORD until = GetTickCount()+duration; MSG msg;
    while (GetTickCount()<until) {
        while(PeekMessage(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        MsgWaitForMultipleObjects(0,nullptr,FALSE,20,QS_ALLINPUT);
    }
}
static bool Render(HWND window, const std::wstring& path) {
    RECT r; GetWindowRect(window,&r); int width=r.right-r.left,height=r.bottom-r.top;
    HDC dc=GetDC(window), memory=CreateCompatibleDC(dc); HBITMAP bitmap=CreateCompatibleBitmap(dc,width,height);
    HGDIOBJ old=SelectObject(memory,bitmap); bool ok=PrintWindow(window,memory,0)!=FALSE;
    SelectObject(memory,old); DeleteDC(memory); ReleaseDC(window,dc);
    ULONG_PTR token; Gdiplus::GdiplusStartupInput start; Gdiplus::GdiplusStartup(&token,&start,nullptr);
    { Gdiplus::Bitmap image(bitmap,nullptr); CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
      ok=ok && image.Save(path.c_str(),&png,nullptr)==Gdiplus::Ok; }
    Gdiplus::GdiplusShutdown(token); DeleteObject(bitmap); return ok;
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=3) return 10;
    instance=GetModuleHandle(nullptr); SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    std::wstring out=argv[1],fixtures=argv[2]; int failures=0;
    auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<"\n"; if(!ok)++failures;};
    check(CreateDesktopView(true),"native desktop creation");
    desktopFolder=fixtures; RefreshDesktop();
    for(int i=0;i<100 && desktopIconsResolved<desktopItems.size();++i) Pump(50);
    check(desktopItems.size()==5,"desktop lists fixture entries and excludes hidden files");
    check(desktopItems[0].directory,"folders sort first");
    check(ListView_GetItemCount(desktopList)==5,"ListView contains all fixture entries");
    check(desktopIconsResolved==desktopItems.size(),"background icon resolution completes");
    int shortcutImage=-1;
    for(int i=0;i<static_cast<int>(desktopItems.size());++i) if(desktopItems[i].name==L"Notepad.lnk") {LVITEM item{};item.mask=LVIF_IMAGE;item.iItem=i;ListView_GetItem(desktopList,&item);shortcutImage=item.iImage;}
    check(shortcutImage>=0 && shortcutImage!=fileIcon,"application shortcut uses resolved app icon");
    SetWindowText(desktopPath,L"Sample files and shortcuts");
    check(Render(desktopWindow,out+L"\\desktop-preview.png"),"render native desktop");
    check(desktopWatch && !desktopWatch->changes.empty(),"filesystem notifications subscribed");
    ListView_SetItemState(desktopList,1,LVIS_SELECTED,LVIS_SELECTED);
    auto selectedPath=desktopItems[1].path;
    auto hasName=[](const wchar_t* name){return std::any_of(desktopItems.begin(),desktopItems.end(),[name](const auto& item){return item.name==name;});};
    {std::wofstream file(fixtures+L"\\Auto refresh.txt");file<<L"temporary test fixture";}
    Pump(650);check(hasName(L"Auto refresh.txt"),"created file appears automatically");
    int selectedIndex=ListView_GetNextItem(desktopList,-1,LVNI_SELECTED);
    check(selectedIndex>=0 && desktopItems[selectedIndex].path==selectedPath,"automatic refresh preserves selection");
    MoveFile((fixtures+L"\\Auto refresh.txt").c_str(),(fixtures+L"\\Renamed.txt").c_str());
    Pump(650);check(hasName(L"Renamed.txt") && !hasName(L"Auto refresh.txt"),"renamed file updates automatically");
    SetFileAttributes((fixtures+L"\\Renamed.txt").c_str(),FILE_ATTRIBUTE_HIDDEN);
    Pump(650);check(!hasName(L"Renamed.txt"),"hidden attribute updates automatically");
    SetFileAttributes((fixtures+L"\\Renamed.txt").c_str(),FILE_ATTRIBUTE_NORMAL);
    Pump(650);check(hasName(L"Renamed.txt"),"unhidden file returns automatically");
    DeleteFile((fixtures+L"\\Renamed.txt").c_str());Pump(650);check(!hasName(L"Renamed.txt"),"deleted file disappears automatically");
    int folder=-1; for(int i=0;i<static_cast<int>(desktopItems.size());++i) if(desktopItems[i].name==L"Projects") folder=i;
    OpenDesktopItem(folder); check(desktopFolder==fixtures+L"\\Projects","folder opens inside Lean Desktop");
    check(desktopItems.size()==1,"folder contents load"); DesktopUp(); check(desktopFolder==fixtures,"Up navigation");
    DestroyDesktop();
    check(!desktopWatch,"desktop watcher released on close");
    check(WindowAppName(L"C:\\apps\\RobloxStudioBeta.exe")==L"Roblox Studio" && WindowAppName(L"C:\\apps\\RobloxPlayerBeta.exe")==L"Roblox","Studio and player have separate groups");
    check(WindowAppName(L"C:\\Windows\\explorer.exe",L"CabinetWClass")==L"File explorers","file explorer grouping");
    check(ExactProcessTitle(12,{{12,L"Project Guns - Roblox Studio"},{13,L"Other project"}})==L"Project Guns - Roblox Studio","mixer title matches exact process");
    check(ExactProcessTitle(12,{{12,L"Tab one"},{12,L"Tab two"}}).empty() && ExactProcessTitle(14,{{12,L"Tab one"}}).empty(),"ambiguous or child-process audio does not guess a title");
    std::vector<HWND> overviewFixtures;
    for (int i=0;i<12;++i) {
        auto title=(i<8?L"Project ":i<11?L"Folder ":L"Document ")+std::to_wstring(i+1);
        HWND window=CreateWindow(L"STATIC",title.c_str(),WS_OVERLAPPEDWINDOW,40,40,360,220,nullptr,nullptr,instance,nullptr);
        ShowWindow(window,SW_SHOWNOACTIVATE);overviewFixtures.push_back(window);
    }
    OpenOverview(overviewFixtures);
    check(overviewWindow && overviewItems.size()==12,"overview includes each fixture window");
    RECT overviewBounds{};GetWindowRect(overviewWindow,&overviewBounds);auto monitor=Primary().rcMonitor;
    check(EqualRect(&overviewBounds,&monitor)!=FALSE,"overview covers the full monitor");
    check(!(GetWindowLongPtr(overviewWindow,GWL_STYLE)&(WS_HSCROLL|WS_VSCROLL)),"overview canvas has no global scrollbar");
    for(auto& item:overviewItems) item.group=item.title.find(L"Project")==0?L"Roblox Studio":item.title.find(L"Folder")==0?L"File explorers":L"Editor";
    RefreshOverview(overviewFixtures);
    SetWindowPos(overviewWindow,nullptr,40,40,1100,760,SWP_NOZORDER|SWP_NOACTIVATE);
    check(overviewGroups.size()==3 && overviewGroups[0].name==L"Editor" && overviewGroups[2].name==L"Roblox Studio","overview sorts and separates application groups");
    check(!TaskWindow(overviewWindow),"overview excludes itself from task switching");
    check(overviewGroups[2].iconSize==O(32) && overviewGroups[1].iconSize==O(48) && overviewGroups[0].iconSize==O(64),"crowded groups shrink icons independently; sparse groups stay large");
    SelectOverview(11);check(overviewGroups[2].scroll>0 && overviewGroups[0].scroll==0,"only selected app group scrolls to its last card");
    SendMessage(overviewWindow,WM_KEYDOWN,VK_HOME,0);check(overviewSelected==0 && overviewGroups[0].scroll==0,"Home returns to first card");
    std::vector<HWND> few{overviewFixtures[0],overviewFixtures[8],overviewFixtures[11]};RefreshOverview(few);
    check(overviewGroups.size()==3 && std::all_of(overviewGroups.begin(),overviewGroups.end(),[](const auto& group){return OverviewLimit(group)==0;}),"small app groups have no scrolling");
    check(std::all_of(overviewGroups.begin(),overviewGroups.end(),[](const auto& group){return group.iconSize==O(64);}),"icons grow back after windows close");
    // Restore test identities for the removed fixtures, then verify title updates.
    RefreshOverview(overviewFixtures);
    for(auto& item:overviewItems)item.group=item.title.find(L"Project")==0?L"Roblox Studio":item.title.find(L"Folder")==0?L"File explorers":L"Editor";
    RefreshOverview(overviewFixtures);
    SetWindowText(overviewFixtures[0],L"Project renamed - Roblox Studio");RefreshOverview(overviewFixtures);
    check(std::any_of(overviewItems.begin(),overviewItems.end(),[](const auto& item){return item.title==L"Project renamed - Roblox Studio";}),"overview updates renamed window titles");
    Pump(50);check(Render(overviewWindow,out+L"\\overview-preview.png"),"render grouped icon overview with fixture titles");
    CloseOverview(false);check(!overviewWindow && overviewItems.empty() && !overviewFont,"overview releases windows, icons, and fonts on close");
    for(HWND window:overviewFixtures)DestroyWindow(window);
    WNDCLASS cls{};cls.hInstance=instance;cls.lpfnWndProc=DefWindowProc;cls.lpszClassName=L"LeanBar.RecoveryFixture";RegisterClass(&cls);
    HWND fixture=CreateWindow(cls.lpszClassName,L"Recovery fixture",WS_OVERLAPPEDWINDOW|WS_VISIBLE,40,40,180,80,nullptr,nullptr,instance,nullptr);
    std::wstring exe=out+L"\\LeanBar.exe", command=L"\""+exe+L"\" --test-recovery-child "+std::to_wstring(reinterpret_cast<UINT_PTR>(fixture));
    STARTUPINFO si{sizeof(si)};PROCESS_INFORMATION pi{};
    bool spawned=CreateProcess(exe.c_str(),&command[0],nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi)!=FALSE;
    check(spawned,"recovery child starts");
    if(spawned){
        CloseHandle(pi.hThread); DWORD until=GetTickCount()+10000;
        while(WaitForSingleObject(pi.hProcess,0)==WAIT_TIMEOUT && GetTickCount()<until) Pump(20);
        DWORD code=0;GetExitCodeProcess(pi.hProcess,&code);check(code==99,"child exits without normal restoration");
        Pump(500);check(IsWindowVisible(fixture)!=FALSE,"watchdog restores hidden fixture after child exit");CloseHandle(pi.hProcess);
    }
    DestroyWindow(fixture);CoUninitialize(); std::cout<<"Failures: "<<failures<<"\n";return failures?1:0;
}
