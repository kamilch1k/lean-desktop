// On-demand native controls. No Explorer, web runtime, background service or telemetry.
#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <memory>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <gdiplus.h>
#include <winrt/base.h>
static HWND mainWindow{}, content{}, statusLabel{}, tabs{}, refreshButton{};
static HFONT uiFont{};
static HINSTANCE appInstance{};
static int page=0,scrollOffset=0,contentHeight=0;
static UINT controlDpi=96;
static bool demo=false,busy=false;
static HBRUSH background{};
struct Placed {HWND hwnd;int x,y,w,h;};
static std::vector<Placed> controls;
static int S(int n){return MulDiv(n,controlDpi,96);}
static void Check(HRESULT hr){if(FAILED(hr))throw hr;}
static std::wstring ErrorText(HRESULT hr){
    wchar_t* message=nullptr;FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,hr,0,reinterpret_cast<LPWSTR>(&message),0,nullptr);
    std::wstring result=message?message:L"Windows error";if(message)LocalFree(message);
    wchar_t code[32];swprintf_s(code,L" (0x%08X)",static_cast<unsigned>(hr));return result+code;
}
static void Status(const std::wstring& text){SetWindowText(statusLabel,text.c_str());}
static void Deliver(std::function<void()> action){
    auto value=new std::function<void()>(std::move(action));
    if(!PostMessage(mainWindow,WM_APP+2,0,reinterpret_cast<LPARAM>(value)))delete value;
}
static void RunAsync(std::function<std::wstring()> action,const std::wstring& text){
    if(busy){Status(L"Wait for the current operation to finish.");return;}
    busy=true;EnableWindow(refreshButton,FALSE);Status(text);
    std::thread([action=std::move(action)]{
        std::wstring result;
        try{winrt::init_apartment(winrt::apartment_type::multi_threaded);result=action();}
        catch(const winrt::hresult_error& e){result=ErrorText(e.code());}
        catch(HRESULT hr){result=ErrorText(hr);}
        catch(...){result=L"The operation could not complete.";}
        Deliver([result]{busy=false;EnableWindow(refreshButton,TRUE);Status(result);});
        winrt::uninit_apartment();
    }).detach();
}
static HWND Add(const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id=0){
    HWND control=CreateWindowEx(0,cls,text,WS_CHILD|WS_VISIBLE|(wcscmp(cls,L"STATIC")?WS_TABSTOP:0)|style,S(x),S(y)-scrollOffset,S(w),S(h),content,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),appInstance,nullptr);
    SendMessage(control,WM_SETFONT,reinterpret_cast<WPARAM>(uiFont),TRUE);controls.push_back({control,x,y,w,h});return control;
}
static void ScrollLayout(){
    RECT rc{};GetClientRect(content,&rc);int limit=std::max(0,S(contentHeight)-static_cast<int>(rc.bottom));scrollOffset=std::clamp(scrollOffset,0,limit);
    SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS,0,S(contentHeight),static_cast<UINT>(rc.bottom),scrollOffset,0};SetScrollInfo(content,SB_VERT,&si,TRUE);
    for(auto& c:controls)MoveWindow(c.hwnd,S(c.x),S(c.y)-scrollOffset,S(c.w),S(c.h),TRUE);
}
static void ContentHeight(int height){contentHeight=height;ScrollLayout();}
struct InputPrompt{HWND window{},edit{};std::wstring title,label,value;bool secret=false,accepted=false;};
static LRESULT CALLBACK InputProcedure(HWND w,UINT m,WPARAM wp,LPARAM lp){
    auto p=reinterpret_cast<InputPrompt*>(GetWindowLongPtr(w,GWLP_USERDATA));
    if(m==WM_NCCREATE){p=static_cast<InputPrompt*>(reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);SetWindowLongPtr(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));}
    switch(m){
    case WM_CREATE:{
        HWND label=CreateWindow(L"STATIC",p->label.c_str(),WS_CHILD|WS_VISIBLE,16,14,430,44,w,nullptr,appInstance,nullptr);
        p->edit=CreateWindowEx(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL|(p->secret?ES_PASSWORD:0),16,62,430,28,w,reinterpret_cast<HMENU>(10),appInstance,nullptr);
        HWND ok=CreateWindow(L"BUTTON",L"OK",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,250,110,90,30,w,reinterpret_cast<HMENU>(IDOK),appInstance,nullptr);
        HWND cancel=CreateWindow(L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP,354,110,90,30,w,reinterpret_cast<HMENU>(IDCANCEL),appInstance,nullptr);
        for(HWND c:{label,p->edit,ok,cancel})SendMessage(c,WM_SETFONT,reinterpret_cast<WPARAM>(uiFont),TRUE);
        SendMessage(p->edit,EM_SETLIMITTEXT,256,0);return 0;}
    case WM_COMMAND:
        if(LOWORD(wp)==IDOK){wchar_t value[257]{};GetWindowText(p->edit,value,257);p->value=value;SecureZeroMemory(value,sizeof(value));p->accepted=true;DestroyWindow(w);return 0;}
        if(LOWORD(wp)==IDCANCEL){DestroyWindow(w);return 0;}break;
    case WM_CLOSE:DestroyWindow(w);return 0;
    }return DefWindowProc(w,m,wp,lp);
}
static bool AskText(const std::wstring& title,const std::wstring& label,std::wstring& result,bool secret){
    InputPrompt prompt;prompt.title=title;prompt.label=label;prompt.secret=secret;
    WNDCLASS cls{};cls.lpfnWndProc=InputProcedure;cls.hInstance=appInstance;cls.lpszClassName=L"LeanControls.Input";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);RegisterClass(&cls);
    RECT parent{};GetWindowRect(mainWindow,&parent);
    prompt.window=CreateWindowEx(WS_EX_DLGMODALFRAME,cls.lpszClassName,title.c_str(),WS_CAPTION|WS_SYSMENU|WS_POPUP,parent.left+40,parent.top+100,480,190,mainWindow,nullptr,appInstance,&prompt);
    if(!prompt.window)return false;
    EnableWindow(mainWindow,FALSE);ShowWindow(prompt.window,SW_SHOW);SetForegroundWindow(prompt.window);SetFocus(prompt.edit);
    MSG msg{};while(IsWindow(prompt.window) && GetMessage(&msg,nullptr,0,0)>0){if(!IsDialogMessage(prompt.window,&msg)){TranslateMessage(&msg);DispatchMessage(&msg);}}
    EnableWindow(mainWindow,TRUE);SetForegroundWindow(mainWindow);result=std::move(prompt.value);return prompt.accepted;
}
#include "ControlsAudio.h"
#include "ControlsWireless.h"

static void BuildPage(){
    SetWindowRedraw(content,FALSE);
    ClearAudio();if(page!=0){audioScroll=0;SetAudioWatch(L"");}
    for(auto& c:controls)DestroyWindow(c.hwnd);controls.clear();
    masterSlider=masterMute=masterValue=outputCombo=nullptr;
    bluetoothList=bluetoothAudioCombo=wifiList=wifiAdapterCombo=nullptr;
    scrollOffset=0;contentHeight=520;
    try{if(page==0)BuildSound();else if(page==1)BuildBluetooth();else BuildNetwork();}
    catch(const winrt::hresult_error& e){Status(ErrorText(e.code()));}
    catch(HRESULT hr){Status(ErrorText(hr));}
    catch(...){Status(L"This panel could not load. Refresh to retry.");}
    ScrollLayout();SetWindowRedraw(content,TRUE);RedrawWindow(content,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
}
static void HandleCommand(int id,int notification){
    if(demo)return;
    if(id==110 && notification==CBN_SELCHANGE){int i=static_cast<int>(SendMessage(outputCombo,CB_GETCURSEL,0,0));if(i>=0 && i<static_cast<int>(outputs.size()))selectedOutput=outputs[i].id;BuildPage();}
    else if(id==111){HRESULT hr=MakeDefaultOutput(selectedOutput);Status(FAILED(hr)?L"Could not change the default output: "+ErrorText(hr):L"Default output changed for playback and calls.");}
    else if(id==121){HRESULT hr=master?master->SetMute(SendMessage(masterMute,BM_GETCHECK,0,0)==BST_CHECKED,&AudioContext):E_FAIL;if(FAILED(hr))Status(ErrorText(hr));}
    else if(id>=1001 && (id-1001)%2==0){int i=(id-1001)/2;if(i<static_cast<int>(audioRows.size())){auto& row=audioRows[i];if(row.volume){HRESULT hr=row.volume->SetMute(SendMessage(row.mute,BM_GETCHECK,0,0)==BST_CHECKED,&AudioContext);if(FAILED(hr))Status(ErrorText(hr));}}}
    else if(id==210 || id==211)RunAsync([id]{return SetBluetoothRadio(id==210);},L"Changing Bluetooth radio state...");
    else if(id==212)ReadBluetooth(true);
    else if(id==214 || id==215)PairBluetooth(id==215);
    else if(id==217 || id==218){int i=static_cast<int>(SendMessage(bluetoothAudioCombo,CB_GETCURSEL,0,0));if(i>=0 && i<static_cast<int>(bluetoothOutputs.size())){auto endpoint=bluetoothOutputs[i].id;RunAsync([endpoint,id]{return BluetoothAudioRequest(endpoint,id==217);},id==217?L"Connecting headphones...":L"Disconnecting headphones...");}else Status(L"No supported Bluetooth audio endpoint. Pair your headphones, then refresh.");}
    else if(id==310 && notification==CBN_SELCHANGE){adapterIndex=static_cast<int>(SendMessage(wifiAdapterCombo,CB_GETCURSEL,0,0));BuildPage();}
    else if(id==311 && !wifiAdapters.empty()){DWORD error=WlanScan(wlan,&wifiAdapters[adapterIndex].InterfaceGuid,nullptr,nullptr,nullptr);Status(error?L"Scan unavailable: "+ErrorText(HRESULT_FROM_WIN32(error)):L"Scanning nearby networks...");}
    else if(id==312 || id==313)WifiRadio(id==312);
    else if(id==315)ConnectWifi();
    else if(id==316 && !wifiAdapters.empty()){DWORD error=WlanDisconnect(wlan,&wifiAdapters[adapterIndex].InterfaceGuid,nullptr);Status(error?ErrorText(HRESULT_FROM_WIN32(error)):L"Disconnect requested.");}
}
static LRESULT CALLBACK ContentProcedure(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_COMMAND:try{HandleCommand(LOWORD(wp),HIWORD(wp));}catch(HRESULT hr){Status(ErrorText(hr));}catch(const winrt::hresult_error& e){Status(ErrorText(e.code()));}return 0;
    case WM_HSCROLL:AudioSlider(reinterpret_cast<HWND>(lp));return 0;
    case WM_VSCROLL:{int command=LOWORD(wp);if(command==SB_LINEUP)scrollOffset-=S(32);if(command==SB_LINEDOWN)scrollOffset+=S(32);if(command==SB_PAGEUP)scrollOffset-=S(220);if(command==SB_PAGEDOWN)scrollOffset+=S(220);if(command==SB_THUMBTRACK || command==SB_THUMBPOSITION){SCROLLINFO si{sizeof(si),SIF_TRACKPOS};GetScrollInfo(w,SB_VERT,&si);scrollOffset=si.nTrackPos;}ScrollLayout();return 0;}
    case WM_MOUSEWHEEL:scrollOffset-=GET_WHEEL_DELTA_WPARAM(wp)*S(64)/WHEEL_DELTA;ScrollLayout();return 0;
    case WM_CTLCOLORSTATIC:{HDC dc=reinterpret_cast<HDC>(wp);SetBkColor(dc,RGB(238,240,242));SetTextColor(dc,RGB(25,31,38));return reinterpret_cast<LRESULT>(background);}
    }return DefWindowProc(w,msg,wp,lp);
}
static void LayoutMain(){
    RECT r{};GetClientRect(mainWindow,&r);MoveWindow(tabs,S(10),S(10),r.right-S(126),S(32),TRUE);MoveWindow(refreshButton,r.right-S(106),S(10),S(96),S(30),TRUE);
    MoveWindow(content,S(10),S(52),r.right-S(20),std::max(S(120),static_cast<int>(r.bottom)-S(120)),TRUE);MoveWindow(statusLabel,S(16),r.bottom-S(59),r.right-S(32),S(50),TRUE);ScrollLayout();LayoutAudio();
}
static LRESULT CALLBACK MainProcedure(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:mainWindow=w;return 0;
    case WM_SIZE:if(content)LayoutMain();return 0;
    case WM_GETMINMAXINFO:{auto mm=reinterpret_cast<MINMAXINFO*>(lp);mm->ptMinTrackSize={S(595),S(390)};return 0;}
    case WM_COMMAND:if(LOWORD(wp)==1 && !busy)BuildPage();return 0;
    case WM_NOTIFY:if(reinterpret_cast<NMHDR*>(lp)->hwndFrom==tabs && reinterpret_cast<NMHDR*>(lp)->code==TCN_SELCHANGE){page=TabCtrl_GetCurSel(tabs);BuildPage();}return 0;
    case WM_APP+2:{std::unique_ptr<std::function<void()>> action(reinterpret_cast<std::function<void()>*>(lp));(*action)();return 0;}
    case WM_APP+3:if(wp<=2){page=static_cast<int>(wp);TabCtrl_SetCurSel(tabs,page);BuildPage();ShowWindow(w,SW_RESTORE);SetForegroundWindow(w);}return 0;
    case WM_APP+4:if(page==2 && (wp==wlan_notification_acm_scan_complete || wp==wlan_notification_acm_connection_complete || wp==wlan_notification_acm_disconnected)){BuildPage();}return 0;
    case WM_APP+5:{wchar_t reason[1024]{};WlanReasonCodeToString(static_cast<DWORD>(wp),1024,reason,nullptr);Status(L"Connection failed: "+std::wstring(reason));return 0;}
    case WM_APP+7:if(page==0)SetTimer(w,7,200,nullptr);return 0;
    case WM_APP+8:if(page==0 && !GetCapture())SyncAudioLevels();return 0;
    case WM_TIMER:if(wp==7){if(GetCapture())return 0;KillTimer(w,7);if(page==0)BuildPage();}return 0;
    case WM_CLOSE:DestroyWindow(w);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }return DefWindowProc(w,msg,wp,lp);
}
static bool RenderOwnWindow(const std::wstring& path){
    RECT r{};GetWindowRect(mainWindow,&r);HDC dc=GetDC(mainWindow),mem=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,r.right-r.left,r.bottom-r.top);HGDIOBJ old=SelectObject(mem,bitmap);
    bool ok=PrintWindow(mainWindow,mem,0)!=FALSE;SelectObject(mem,old);DeleteDC(mem);ReleaseDC(mainWindow,dc);
    ULONG_PTR token{};Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&token,&input,nullptr);
    {Gdiplus::Bitmap image(bitmap,nullptr);CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};ok=ok && image.Save(path.c_str(),&png,nullptr)==Gdiplus::Ok;}
    Gdiplus::GdiplusShutdown(token);DeleteObject(bitmap);return ok;
}
static void PumpControls(DWORD milliseconds){
    DWORD until=GetTickCount()+milliseconds;MSG pending{};
    while(GetTickCount()<until){while(PeekMessage(&pending,nullptr,0,0,PM_REMOVE)){TranslateMessage(&pending);DispatchMessage(&pending);}MsgWaitForMultipleObjects(0,nullptr,FALSE,15,QS_ALLINPUT);}
}
static int TestLiveSound(const std::wstring& path){
    std::wofstream report(path);report<<std::unitbuf;int failures=0;
    auto check=[&](bool ok,const wchar_t* name){report<<(ok?L"PASS ":L"FAIL ")<<name<<L"\n";if(!ok)++failures;};
    try{
        for(int i=0;i<40 && !audioWatching;++i)PumpControls(50);
        check(audioWatching,L"audio notification subscription active");check(master!=nullptr,L"master volume interface loaded");
        auto e=AudioEnumerator();ComPtr<IMMDevice> d;Check(e->GetDevice(selectedOutput.c_str(),&d));ComPtr<IAudioClient> client;Check(d->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,&client));
        WAVEFORMATEX* format=nullptr;Check(client->GetMixFormat(&format));GUID session{};CoCreateGuid(&session);
        HRESULT hr=client->Initialize(AUDCLNT_SHAREMODE_SHARED,0,1000000,0,format,&session);CoTaskMemFree(format);Check(hr);
        ComPtr<ISimpleAudioVolume> volume;Check(client->GetService(IID_PPV_ARGS(&volume)));
        for(int i=0;i<40 && std::none_of(audioRows.begin(),audioRows.end(),[](auto& row){return row.pid==GetCurrentProcessId();});++i)PumpControls(50);
        auto findRow=[]()->AudioRow*{for(auto& row:audioRows)if(row.pid==GetCurrentProcessId())return &row;return nullptr;};
        check(findRow()!=nullptr,L"new audio session appears without Refresh");
        for(int i=0;i<40 && !audioWatching;++i)PumpControls(50);
        float before=1;BOOL wasMuted=FALSE;volume->GetMasterVolume(&before);volume->GetMute(&wasMuted);
        Check(volume->SetMasterVolume(.37f,nullptr));Check(volume->SetMute(TRUE,nullptr));PumpControls(400);
        auto row=findRow();check(row && SendMessage(row->slider,TBM_GETPOS,0,0)==37,L"external session-volume event updates slider");
        check(row && SendMessage(row->mute,BM_GETCHECK,0,0)==BST_CHECKED,L"external session-mute event updates checkbox");
        if(row){SendMessage(row->slider,TBM_SETPOS,TRUE,43);AudioSlider(row->slider);float level=0;volume->GetMasterVolume(&level);check(level>.429f && level<.431f,L"slider changes only the dedicated test session");}
        volume->SetMasterVolume(before,nullptr);volume->SetMute(wasMuted,nullptr);
        check(std::all_of(audioRows.begin(),audioRows.end(),[](auto& r){return r.icon!=nullptr;}),L"audio sessions have icons");
    }catch(HRESULT hr){report<<L"FAIL "<<ErrorText(hr)<<L"\n";++failures;}
    report<<L"Failures: "<<failures<<L"\nNo other app volume or device connection was changed.\n";return failures?1:0;
}
static int Probe(const std::wstring& path){
    std::wofstream report(path);report<<std::unitbuf;int failures=0;
    try{auto devices=ReadOutputs(true);report<<L"Audio endpoints: "<<devices.size()<<L"\nBluetooth audio controls: "<<std::count_if(devices.begin(),devices.end(),[](auto& d){return d.bluetooth;})<<L"\n";
        auto e=AudioEnumerator();ComPtr<IMMDevice> d;Check(e->GetDefaultAudioEndpoint(eRender,eMultimedia,&d));ComPtr<IAudioSessionManager2> manager;ComPtr<IAudioSessionEnumerator> sessions;Check(d->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,&manager));Check(manager->GetSessionEnumerator(&sessions));int count=0;sessions->GetCount(&count);report<<L"Current audio sessions: "<<count<<L"\n";
        // A dedicated session GUID confines the round-trip test to this process.
        GUID guid{};CoCreateGuid(&guid);ComPtr<ISimpleAudioVolume> testVolume;Check(manager->GetSimpleAudioVolume(&guid,0,&testVolume));float before=0;testVolume->GetMasterVolume(&before);Check(testVolume->SetMasterVolume(.37f,nullptr));float after=0;Check(testVolume->GetMasterVolume(&after));Check(testVolume->SetMasterVolume(before,nullptr));if(after<.369f || after>.371f)throw E_FAIL;report<<L"PASS private-session volume round trip (no other app changed)\n";
    }catch(HRESULT hr){report<<L"Audio probe: "<<ErrorText(hr)<<L"\n";++failures;}
    try{OpenWifi();PWLAN_INTERFACE_INFO_LIST list=nullptr;DWORD error=WlanEnumInterfaces(wlan,nullptr,&list);if(error)throw HRESULT_FROM_WIN32(error);report<<L"Wi-Fi adapters: "<<list->dwNumberOfItems<<L"\n";
        for(DWORD i=0;i<list->dwNumberOfItems;++i){PWLAN_AVAILABLE_NETWORK_LIST networks=nullptr;error=WlanGetAvailableNetworkList(wlan,&list->InterfaceInfo[i].InterfaceGuid,0,nullptr,&networks);report<<L"Wi-Fi list status: "<<error;if(!error){report<<L", networks: "<<networks->dwNumberOfItems;WlanFreeMemory(networks);}report<<L"\n";}WlanFreeMemory(list);
    }catch(HRESULT hr){report<<L"Wi-Fi probe: "<<ErrorText(hr)<<L"\n";}
    std::thread bt([&report]{
        try{winrt::init_apartment(winrt::apartment_type::multi_threaded);auto radios=Radio::GetRadiosAsync().get();int count=0;for(auto r:radios)if(r.Kind()==RadioKind::Bluetooth)++count;report<<L"Bluetooth radios: "<<count<<L"\n";
            auto devices=CollectBluetoothDevices(false);report<<L"Paired Bluetooth devices: "<<devices.size()<<L"\n";
        }catch(const winrt::hresult_error& e){report<<L"Bluetooth probe: "<<ErrorText(e.code())<<L"\n";}winrt::uninit_apartment();
    });bt.join();
    WifiNetwork sample;sample.name=L"A&B <home>";sample.secure=true;sample.auth=DOT11_AUTH_ALGO_RSNA_PSK;sample.ssid.uSSIDLength=3;sample.ssid.ucSSID[0]='A';sample.ssid.ucSSID[1]='&';sample.ssid.ucSSID[2]='B';auto xml=WifiProfile(sample,L"pass<&word");bool escaped=xml.find(L"A&amp;B &lt;home&gt;")!=std::wstring::npos && xml.find(L"pass&lt;&amp;word")!=std::wstring::npos && xml.find(L"412642")!=std::wstring::npos;report<<(escaped?L"PASS":L"FAIL")<<L" Wi-Fi XML escaping and raw SSID bytes\n";if(!escaped)++failures;
    report<<L"No pairing, radio, network, or default-output settings changed.\n";return failures?1:0;
}
int WINAPI wWinMain(HINSTANCE h,HINSTANCE,PWSTR,int){
    appInstance=h;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);winrt::init_apartment(winrt::apartment_type::single_threaded);
    int argc=0;auto argv=CommandLineToArgvW(GetCommandLine(),&argc);std::wstring render,probe,uiTest;
    for(int i=1;i<argc;++i){if(!wcscmp(argv[i],L"--bluetooth"))page=1;else if(!wcscmp(argv[i],L"--network"))page=2;else if(!wcscmp(argv[i],L"--demo"))demo=true;else if(!wcscmp(argv[i],L"--render") && i+1<argc)render=argv[++i];else if(!wcscmp(argv[i],L"--probe") && i+1<argc)probe=argv[++i];else if(!wcscmp(argv[i],L"--test-live-sound") && i+1<argc)uiTest=argv[++i];}LocalFree(argv);
    if(!probe.empty())return Probe(probe);
    const wchar_t* name=!uiTest.empty()?L"LeanControls.SoundTest":demo?L"LeanControls.Demo":L"LeanControls.Window";HWND existing=FindWindow(name,nullptr);
    if(existing){AllowSetForegroundWindow(ASFW_ANY);PostMessage(existing,WM_APP+3,page,0);return 0;}
    INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_WIN95_CLASSES|ICC_BAR_CLASSES|ICC_TAB_CLASSES|ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&ic);
    background=CreateSolidBrush(RGB(238,240,242));
    WNDCLASS cls{};cls.hInstance=h;cls.lpfnWndProc=MainProcedure;cls.lpszClassName=name;cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hbrBackground=background;cls.hIcon=LoadIcon(nullptr,IDI_APPLICATION);RegisterClass(&cls);
    mainWindow=CreateWindowEx(0,name,L"Lean Controls",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,620,730,nullptr,nullptr,h,nullptr);
    if(!mainWindow)return 1;controlDpi=GetDpiForWindow(mainWindow);
    uiFont=CreateFont(-S(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    cls.lpfnWndProc=ContentProcedure;cls.lpszClassName=L"LeanControls.Content";RegisterClass(&cls);
    content=CreateWindowEx(WS_EX_CONTROLPARENT,cls.lpszClassName,L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_CLIPCHILDREN,0,0,1,1,mainWindow,nullptr,h,nullptr);
    tabs=CreateWindow(WC_TABCONTROL,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|TCS_BUTTONS,0,0,1,1,mainWindow,nullptr,h,nullptr);
    for(auto text:{L"Sound",L"Bluetooth",L"Network"}){TCITEM item{};item.mask=TCIF_TEXT;item.pszText=const_cast<LPWSTR>(text);TabCtrl_InsertItem(tabs,TabCtrl_GetItemCount(tabs),&item);}TabCtrl_SetCurSel(tabs,page);
    refreshButton=CreateWindow(L"BUTTON",L"Refresh",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,1,1,mainWindow,reinterpret_cast<HMENU>(1),h,nullptr);
    statusLabel=CreateWindow(L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,1,1,mainWindow,nullptr,h,nullptr);
    for(HWND w:{tabs,refreshButton,statusLabel})SendMessage(w,WM_SETFONT,reinterpret_cast<WPARAM>(uiFont),TRUE);
    SetWindowPos(mainWindow,nullptr,0,0,S(620),S(730),SWP_NOMOVE|SWP_NOZORDER);LayoutMain();BuildPage();ShowWindow(mainWindow,SW_SHOW);UpdateWindow(mainWindow);
    if(!uiTest.empty()){int result=TestLiveSound(uiTest);DestroyWindow(mainWindow);ExitProcess(result);}
    if(!render.empty()) {
        ShowWindow(mainWindow,SW_SHOWNOACTIVATE);
        DWORD until=GetTickCount()+150;MSG pending{};
        while(GetTickCount()<until){while(PeekMessage(&pending,nullptr,0,0,PM_REMOVE)){TranslateMessage(&pending);DispatchMessage(&pending);}MsgWaitForMultipleObjects(0,nullptr,FALSE,20,QS_ALLINPUT);}
        bool layoutOk=true;
        if(demo && page==0 && !audioRows.empty()){
            RECT masterBefore{},masterAfter{},rowBefore{},rowAfter{};GetWindowRect(masterSlider,&masterBefore);GetWindowRect(audioRows[0].slider,&rowBefore);
            audioScroll=S(100);ScrollAudio();GetWindowRect(masterSlider,&masterAfter);GetWindowRect(audioRows[0].slider,&rowAfter);
            layoutOk=EqualRect(&masterBefore,&masterAfter) && rowAfter.top<rowBefore.top && audioScroll>0;
            audioScroll=0;ScrollAudio();
        }
        bool ok=RenderOwnWindow(render);DestroyWindow(mainWindow);return ok && layoutOk?0:1;
    }
    MSG msg{};while(GetMessage(&msg,nullptr,0,0)>0){if(!IsDialogMessage(mainWindow,&msg)){TranslateMessage(&msg);DispatchMessage(&msg);}}
    // Exit the process even if discovery was still pending; no resident helper remains.
    if(wlan){WlanRegisterNotification(wlan,WLAN_NOTIFICATION_SOURCE_NONE,TRUE,nullptr,nullptr,nullptr,nullptr);WlanCloseHandle(wlan,nullptr);}
    ExitProcess(0);
}
