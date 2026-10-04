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
    int folder=-1; for(int i=0;i<static_cast<int>(desktopItems.size());++i) if(desktopItems[i].name==L"Projects") folder=i;
    OpenDesktopItem(folder); check(desktopFolder==fixtures+L"\\Projects","folder opens inside Lean Desktop");
    check(desktopItems.size()==1,"folder contents load"); DesktopUp(); check(desktopFolder==fixtures,"Up navigation");
    DestroyDesktop();
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
