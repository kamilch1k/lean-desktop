#pragma once
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <devicetopology.h>
#include <ks.h>
#include <ksmedia.h>
#include <ksproxy.h>
#include <condition_variable>
#include <winver.h>
#include <wrl/client.h>
#include "WindowNames.h"
using Microsoft::WRL::ComPtr;

struct AudioOutput { std::wstring id, name; DWORD state = 0; bool bluetooth = false; std::wstring container; };
struct AudioRow { std::wstring name, appName; ComPtr<ISimpleAudioVolume> volume; HWND slider{}, mute{}, value{}, titleLabel{}; HICON icon{}; DWORD pid=0; AudioSessionState state=AudioSessionStateInactive; };
static std::vector<AudioOutput> outputs;
static std::vector<AudioRow> audioRows;
static ComPtr<IAudioEndpointVolume> master;
static HWND outputCombo{}, masterSlider{}, masterMute{}, masterValue{};
static std::wstring selectedOutput;
static std::wstring demoDefaultOutput=L"demo-speakers";
static HWND audioViewport{};
static std::vector<Placed> audioPlacements;
static int audioScroll=0,audioHeight=0;
static std::vector<HWINEVENTHOOK> audioTitleHooks;
static const GUID AudioContext={0x5788b309,0x68ac,0x4d0d,{0xb8,0x8a,0x99,0x2c,0x3b,0xb5,0x69,0x04}};
static void ScrollAudio(){
    if(!audioViewport)return;RECT r{};GetClientRect(audioViewport,&r);audioScroll=std::clamp(audioScroll,0,std::max(0,S(audioHeight)-static_cast<int>(r.bottom)));
    SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS,0,S(audioHeight),static_cast<UINT>(r.bottom),audioScroll,0};SetScrollInfo(audioViewport,SB_VERT,&si,TRUE);
    for(auto& c:audioPlacements)MoveWindow(c.hwnd,S(c.x),S(c.y)-audioScroll,S(c.w),S(c.h),TRUE);
}
static void LayoutAudio(){
    if(!audioViewport)return;RECT r{};GetClientRect(content,&r);MoveWindow(audioViewport,0,S(249),r.right,std::max(S(40),static_cast<int>(r.bottom)-S(254)),TRUE);ScrollAudio();
}
static HWND AudioAdd(const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id=0){
    HWND control=CreateWindowEx(0,cls,text,WS_CHILD|WS_VISIBLE|(wcscmp(cls,L"STATIC")?WS_TABSTOP:0)|style,S(x),S(y)-audioScroll,S(w),S(h),audioViewport,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),appInstance,nullptr);
    SendMessage(control,WM_SETFONT,reinterpret_cast<WPARAM>(uiFont),TRUE);audioPlacements.push_back({control,x,y,w,h});return control;
}
static void ClearAudio(){
    for(auto hook:audioTitleHooks)UnhookWinEvent(hook);audioTitleHooks.clear();
    KillTimer(mainWindow,9);
    audioPlacements.clear();if(audioViewport)DestroyWindow(audioViewport);audioViewport=nullptr;
    for(auto& row:audioRows)if(row.icon)DestroyIcon(row.icon);audioRows.clear();master.Reset();
}
static void AudioSlider(HWND slider);
static void HandleCommand(int id,int notification);
static LRESULT CALLBACK AudioListProcedure(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_COMMAND:HandleCommand(LOWORD(wp),HIWORD(wp));return 0;
    case WM_HSCROLL:AudioSlider(reinterpret_cast<HWND>(lp));return 0;
    case WM_VSCROLL:{int command=LOWORD(wp);if(command==SB_LINEUP)audioScroll-=S(40);if(command==SB_LINEDOWN)audioScroll+=S(40);if(command==SB_PAGEUP)audioScroll-=S(220);if(command==SB_PAGEDOWN)audioScroll+=S(220);if(command==SB_THUMBTRACK || command==SB_THUMBPOSITION){SCROLLINFO si{sizeof(si),SIF_TRACKPOS};GetScrollInfo(w,SB_VERT,&si);audioScroll=si.nTrackPos;}ScrollAudio();return 0;}
    case WM_MOUSEWHEEL:audioScroll-=GET_WHEEL_DELTA_WPARAM(wp)*S(70)/WHEEL_DELTA;ScrollAudio();return 0;
    case WM_DRAWITEM:{auto d=reinterpret_cast<DRAWITEMSTRUCT*>(lp);int i=static_cast<int>(d->CtlID)-3000;if(i>=0 && i<static_cast<int>(audioRows.size())){SetDCBrushColor(d->hDC,RGB(76,87,98));FillRect(d->hDC,&d->rcItem,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));DrawIconEx(d->hDC,S(4),S(4),audioRows[i].icon,S(32),S(32),0,nullptr,DI_NORMAL);return TRUE;}break;}
    case WM_CTLCOLORSTATIC:{HDC dc=reinterpret_cast<HDC>(wp);SetBkColor(dc,RGB(238,240,242));SetTextColor(dc,RGB(25,31,38));return reinterpret_cast<LRESULT>(background);}
    }return DefWindowProc(w,msg,wp,lp);
}
static std::wstring ProcessPath(DWORD pid){
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);wchar_t path[32768]{};DWORD size=32768;
    if(process){QueryFullProcessImageName(process,0,path,&size);CloseHandle(process);}return path;
}
static HICON SessionIcon(DWORD pid){
    SHFILEINFO info{};auto path=ProcessPath(pid);
    if(!path.empty() && SHGetFileInfo(path.c_str(),0,&info,sizeof(info),SHGFI_ICON|SHGFI_LARGEICON))return info.hIcon;
    return CopyIcon(LoadIcon(nullptr,pid?IDI_APPLICATION:IDI_INFORMATION));
}
static std::wstring AppDescription(const std::wstring& path){
    DWORD unused=0;DWORD size=GetFileVersionInfoSize(path.c_str(),&unused);if(!size || size>1024*1024)return L"";
    std::vector<BYTE> data(size);if(!GetFileVersionInfo(path.c_str(),0,size,data.data()))return L"";
    struct Translation{WORD language,codepage;};Translation* languages=nullptr;UINT length=0;
    if(!VerQueryValue(data.data(),L"\\VarFileInfo\\Translation",reinterpret_cast<void**>(&languages),&length) || length<sizeof(Translation))return L"";
    wchar_t query[80]{};swprintf_s(query,L"\\StringFileInfo\\%04x%04x\\FileDescription",languages[0].language,languages[0].codepage);
    wchar_t* value=nullptr;if(VerQueryValue(data.data(),query,reinterpret_cast<void**>(&value),&length) && value)return value;return L"";
}

class AudioNotifications final : public IAudioSessionNotification,public IAudioSessionEvents,public IMMNotificationClient,public IAudioEndpointVolumeCallback {
    std::atomic<ULONG> refs{1};
    void Refresh(){PostMessage(mainWindow,WM_APP+7,0,0);}
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** result) override {
        if(!result)return E_POINTER;*result=nullptr;
        if(iid==__uuidof(IUnknown) || iid==__uuidof(IAudioSessionNotification))*result=static_cast<IAudioSessionNotification*>(this);
        else if(iid==__uuidof(IAudioSessionEvents))*result=static_cast<IAudioSessionEvents*>(this);
        else if(iid==__uuidof(IMMNotificationClient))*result=static_cast<IMMNotificationClient*>(this);
        else if(iid==__uuidof(IAudioEndpointVolumeCallback))*result=static_cast<IAudioEndpointVolumeCallback*>(this);
        else return E_NOINTERFACE;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override{ULONG n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE OnSessionCreated(IAudioSessionControl*) override{Refresh();return S_OK;}
    HRESULT STDMETHODCALLTYPE OnDisplayNameChanged(LPCWSTR,LPCGUID) override{Refresh();return S_OK;}
    HRESULT STDMETHODCALLTYPE OnIconPathChanged(LPCWSTR,LPCGUID) override{Refresh();return S_OK;}
    HRESULT STDMETHODCALLTYPE OnSimpleVolumeChanged(float,BOOL,LPCGUID context) override{if(!context || !IsEqualGUID(*context,AudioContext))PostMessage(mainWindow,WM_APP+8,0,0);return S_OK;}
    HRESULT STDMETHODCALLTYPE OnChannelVolumeChanged(DWORD,float[],DWORD,LPCGUID) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnGroupingParamChanged(LPCGUID,LPCGUID) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnStateChanged(AudioSessionState) override{Refresh();return S_OK;}
    HRESULT STDMETHODCALLTYPE OnSessionDisconnected(AudioSessionDisconnectReason) override{Refresh();return S_OK;}
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR,DWORD) override{Refresh();return S_OK;}
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override{Refresh();return S_OK;}
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override{Refresh();return S_OK;}
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow,ERole,LPCWSTR) override{if(flow==eRender)Refresh();return S_OK;}
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR,const PROPERTYKEY) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA data) override{if(data && !IsEqualGUID(data->guidEventContext,AudioContext))PostMessage(mainWindow,WM_APP+8,0,0);return S_OK;}
};
static std::mutex audioWatchLock;
static std::condition_variable audioWatchWake;
static std::wstring audioWatchId;
static unsigned audioWatchGeneration=0;
static std::atomic<bool> audioWatching{false};
static void SetAudioWatch(const std::wstring& id){
    static bool started=false;
    if(!started && id.empty())return;
    {std::lock_guard<std::mutex> guard(audioWatchLock);audioWatchId=id;++audioWatchGeneration;}
    audioWatchWake.notify_one();
    if(started)return;started=true;
    std::thread([]{
        CoInitializeEx(nullptr,COINIT_MULTITHREADED);unsigned generation=0;
        for(;;){
            std::wstring id;
            {std::unique_lock<std::mutex> guard(audioWatchLock);audioWatchWake.wait(guard,[&]{return generation!=audioWatchGeneration;});generation=audioWatchGeneration;id=audioWatchId;}
            if(id.empty())continue;
            ComPtr<IMMDeviceEnumerator> e;ComPtr<IAudioSessionManager2> manager;ComPtr<IAudioEndpointVolume> volume;
            std::vector<ComPtr<IAudioSessionControl>> sessions;ComPtr<AudioNotifications> events;events.Attach(new AudioNotifications());
            try{
                Check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&e)));
                ComPtr<IMMDevice> d;Check(e->GetDevice(id.c_str(),&d));Check(d->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,&manager));
                ComPtr<IAudioSessionEnumerator> list;Check(manager->GetSessionEnumerator(&list));int count=0;Check(list->GetCount(&count));
                for(int i=0;i<count;++i){ComPtr<IAudioSessionControl> s;if(SUCCEEDED(list->GetSession(i,&s))){s->RegisterAudioSessionNotification(events.Get());sessions.push_back(s);}}
                Check(manager->RegisterSessionNotification(events.Get()));e->RegisterEndpointNotificationCallback(events.Get());audioWatching=true;
                if(SUCCEEDED(d->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,&volume)))volume->RegisterControlChangeNotify(events.Get());
            }catch(...){}
            {std::unique_lock<std::mutex> guard(audioWatchLock);audioWatchWake.wait(guard,[&]{return generation!=audioWatchGeneration;});}
            audioWatching=false;for(auto& s:sessions)s->UnregisterAudioSessionNotification(events.Get());
            if(manager)manager->UnregisterSessionNotification(events.Get());if(volume)volume->UnregisterControlChangeNotify(events.Get());if(e)e->UnregisterEndpointNotificationCallback(events.Get());
        }
    }).detach();
}

static ComPtr<IMMDeviceEnumerator> AudioEnumerator() {
    ComPtr<IMMDeviceEnumerator> e;
    Check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&e)));
    return e;
}
static std::wstring AudioName(IMMDevice* device) {
    ComPtr<IPropertyStore> props; PROPVARIANT value{};
    std::wstring name=L"Audio device";
    if(SUCCEEDED(device->OpenPropertyStore(STGM_READ,&props)) && SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName,&value))) {
        if(value.vt==VT_LPWSTR) name=value.pwszVal;
        PropVariantClear(&value);
    }
    return name;
}
static std::wstring ContainerKey(const GUID& value){
    if(IsEqualGUID(value,GUID_NULL))return L"";wchar_t text[40]{};StringFromGUID2(value,text,40);return LowerName(text);
}
static std::wstring AudioContainer(IMMDevice* device){
    ComPtr<IPropertyStore> props;PROPVARIANT value{};std::wstring result;
    if(SUCCEEDED(device->OpenPropertyStore(STGM_READ,&props)) && SUCCEEDED(props->GetValue(PKEY_Device_ContainerId,&value)) && value.vt==VT_CLSID && value.puuid)result=ContainerKey(*value.puuid);
    PropVariantClear(&value);return result;
}
static ComPtr<IKsControl> BluetoothControl(IMMDevice* endpoint) {
    ComPtr<IDeviceTopology> topology; ComPtr<IConnector> connector; LPWSTR adapterId=nullptr;
    Check(endpoint->Activate(__uuidof(IDeviceTopology),CLSCTX_ALL,nullptr,&topology));
    Check(topology->GetConnector(0,&connector));
    Check(connector->GetDeviceIdConnectedTo(&adapterId));
    std::wstring id=adapterId; CoTaskMemFree(adapterId);
    auto e=AudioEnumerator(); ComPtr<IMMDevice> adapter; ComPtr<IKsControl> control;
    Check(e->GetDevice(id.c_str(),&adapter));
    Check(adapter->Activate(__uuidof(IKsControl),CLSCTX_ALL,nullptr,&control));
    return control;
}
static bool SupportsBluetooth(IMMDevice* endpoint) {
    try {
        auto ks=BluetoothControl(endpoint); KSPROPERTY property{KSPROPSETID_BtAudio,KSPROPERTY_ONESHOT_RECONNECT,KSPROPERTY_TYPE_BASICSUPPORT};
        ULONG support=0, size=0;
        return SUCCEEDED(ks->KsProperty(&property,sizeof(property),&support,sizeof(support),&size)) && (support & KSPROPERTY_TYPE_GET);
    } catch(...) { return false; }
}
static std::vector<AudioOutput> ReadOutputs(bool detectBluetooth=false) {
    auto e=AudioEnumerator(); ComPtr<IMMDeviceCollection> devices;
    Check(e->EnumAudioEndpoints(eRender,DEVICE_STATEMASK_ALL,&devices));
    UINT count=0; Check(devices->GetCount(&count)); std::vector<AudioOutput> result;
    for(UINT i=0;i<count;++i) {
        ComPtr<IMMDevice> d; LPWSTR id=nullptr; DWORD state=0;
        if(FAILED(devices->Item(i,&d)) || FAILED(d->GetId(&id))) continue;
        d->GetState(&state);
        result.push_back({id,AudioName(d.Get()),state,detectBluetooth && SupportsBluetooth(d.Get()),detectBluetooth?AudioContainer(d.Get()):L""}); CoTaskMemFree(id);
    }
    return result;
}
static std::wstring DefaultOutput() {
    auto e=AudioEnumerator(); ComPtr<IMMDevice> d; LPWSTR id=nullptr;
    Check(e->GetDefaultAudioEndpoint(eRender,eMultimedia,&d)); Check(d->GetId(&id));
    std::wstring result=id; CoTaskMemFree(id); return result;
}
static std::wstring CurrentOutput(){if(demo)return demoDefaultOutput;try{return DefaultOutput();}catch(...){return L"";}}
static std::vector<AudioOutput> DemoOutputs(){return {{L"demo-speakers",L"Speakers",DEVICE_STATE_ACTIVE,false,L"demo-speakers-container"},{L"demo-headphones",L"Headphones",DEVICE_STATE_ACTIVE,true,L"demo-headphones-container"},{L"demo-wireless",L"Wireless headphones",DEVICE_STATE_UNPLUGGED,true,L"demo-wireless-container"}};}
static std::wstring OutputState(const AudioOutput& output,const std::wstring& current){
    if(output.state==DEVICE_STATE_ACTIVE)return output.id==current?L"Playing here":L"Available";
    if(output.state&DEVICE_STATE_DISABLED)return L"Disabled in Windows";
    return L"Disconnected";
}
// Windows has no public API for default-output selection. Keep this optional,
// private COM ABI isolated: failure is reported, never replaced with registry edits.
struct __declspec(uuid("f8679f50-850a-41cf-9c72-430f290290c8")) OutputPolicy : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR,WAVEFORMATEX**)=0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR,INT,WAVEFORMATEX**)=0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR)=0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR,WAVEFORMATEX*,WAVEFORMATEX*)=0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR,INT,INT64*,INT64*)=0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR,INT64*)=0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR,void*)=0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR,void*)=0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR,const PROPERTYKEY&,PROPVARIANT*)=0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR,const PROPERTYKEY&,PROPVARIANT*)=0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR,ERole)=0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR,INT)=0;
};
static HRESULT MakeDefaultOutput(const std::wstring& id) {
    const CLSID cls={0x870af99c,0x171d,0x4f9e,{0xaf,0x0d,0xe6,0x3d,0xf4,0x0c,0x2b,0xc9}};
    ComPtr<OutputPolicy> policy; HRESULT hr=CoCreateInstance(cls,nullptr,CLSCTX_ALL,IID_PPV_ARGS(&policy));
    if(FAILED(hr)) return hr;
    for(ERole role:{eConsole,eMultimedia,eCommunications}) { hr=policy->SetDefaultEndpoint(id.c_str(),role); if(FAILED(hr)) return hr; }
    return S_OK;
}
static void RequestAudioOutput(const std::wstring& id){
    if(id.empty())return;
    auto devices=demo?DemoOutputs():ReadOutputs();
    auto found=std::find_if(devices.begin(),devices.end(),[&](const auto& d){return d.id==id;});
    if(found==devices.end() || found->state!=DEVICE_STATE_ACTIVE){Status(L"This output is unavailable. Connect it first, then choose it again.");return;}
    auto name=found->name;
    if(demo){demoDefaultOutput=id;BuildPage(false);Status(L"Playing through: "+name);return;}
    RunAsync([id,name]{Check(MakeDefaultOutput(id));if(DefaultOutput()!=id)throw E_FAIL;return L"Playing through: "+name+L". Apps with their own output setting may need that changed too.";},L"Switching audio output...",true);
}
static std::wstring SessionName(IAudioSessionControl* control,IAudioSessionControl2* info) {
    if(info->IsSystemSoundsSession()==S_OK) return L"System sounds";
    LPWSTR display=nullptr; std::wstring name;
    if(SUCCEEDED(control->GetDisplayName(&display))) { if(display && display[0]!=L'@') name=display; CoTaskMemFree(display); }
    if(!name.empty()) return name;
    DWORD pid=0; info->GetProcessId(&pid);
    auto path=ProcessPath(pid);name=AppDescription(path);
    if(name.empty() && !path.empty()){name=path;name=name.substr(name.find_last_of(L"\\/")+1);}
    return name.empty()?L"Audio application ("+std::to_wstring(pid)+L")":name;
}
static void SyncAudioTitles(){
    std::vector<ProcessWindowTitle> titles;EnumWindows(ReadProcessWindowTitle,reinterpret_cast<LPARAM>(&titles));
    for(auto& row:audioRows){
        auto title=ExactProcessTitle(row.pid,titles);
        if(title.empty())title=row.appName;
        if(title!=row.name){row.name=title;if(row.titleLabel)SetWindowText(row.titleLabel,title.c_str());}
    }
}
static void CALLBACK AudioWindowEvent(HWINEVENTHOOK,DWORD,HWND window,LONG object,LONG child,DWORD,DWORD){
    if(!window || object!=OBJID_WINDOW || child!=CHILDID_SELF || (GetWindowLongPtr(window,GWL_STYLE)&WS_CHILD))return;
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if(std::any_of(audioRows.begin(),audioRows.end(),[pid](const auto& row){return row.pid && row.pid==pid;}))SetTimer(mainWindow,9,200,nullptr);
}
static void VolumeControls(int id,int y,const std::wstring& label,float level,BOOL muted,HWND& slider,HWND& mute,HWND& value) {
    Add(L"STATIC",label.c_str(),SS_ENDELLIPSIS,16,y,345,24);
    value=Add(L"STATIC",(std::to_wstring(static_cast<int>(level*100+.5f))+L"%").c_str(),SS_RIGHT,374,y,55,24);
    mute=Add(L"BUTTON",L"Mute",BS_AUTOCHECKBOX,448,y,90,24,id+1);
    SendMessage(mute,BM_SETCHECK,muted?BST_CHECKED:BST_UNCHECKED,0);
    slider=Add(TRACKBAR_CLASS,L"",TBS_HORZ|TBS_NOTICKS,16,y+27,530,30,id);
    SendMessage(slider,TBM_SETRANGE,TRUE,MAKELPARAM(0,100)); SendMessage(slider,TBM_SETPOS,TRUE,static_cast<int>(level*100+.5f));
    SendMessage(slider,TBM_SETPAGESIZE,0,5);
}
static void BuildSound() {
    WNDCLASS cls{};cls.lpfnWndProc=AudioListProcedure;cls.hInstance=appInstance;cls.lpszClassName=L"LeanControls.AudioList";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hbrBackground=background;RegisterClass(&cls);
    audioViewport=CreateWindowEx(WS_EX_CONTROLPARENT,cls.lpszClassName,L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_CLIPCHILDREN,0,0,1,1,content,nullptr,appInstance,nullptr);
    outputs=demo?DemoOutputs():ReadOutputs();
    std::wstring current=CurrentOutput(),currentName=L"No active output";
    for(const auto& output:outputs)if(output.id==current)currentName=output.name;
    Section(L"Sound output",6);
    Add(L"STATIC",(L"Playing through: "+currentName).c_str(),SS_ENDELLIPSIS|SS_NOPREFIX,16,36,532,24,112);
    outputCombo=Add(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,16,68,365,250,110);
    HWND useOutput=Add(L"BUTTON",L"Play through this",0,393,67,155,30,111);
    SendMessage(outputCombo,CB_SETDROPPEDWIDTH,S(532),0);
    if(selectedOutput.empty())selectedOutput=current;
    int chosen=-1;
    for(int i=0;i<static_cast<int>(outputs.size());++i) {
        std::wstring name=outputs[i].name+L"  -  "+OutputState(outputs[i],current);
        SendMessage(outputCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));
        if(outputs[i].id==selectedOutput) chosen=i;
    }
    if(chosen<0){for(int i=0;i<static_cast<int>(outputs.size());++i)if(outputs[i].id==current){chosen=i;break;}}
    if(chosen<0 && !outputs.empty()) chosen=0;
    if(chosen<0) {EnableWindow(useOutput,FALSE);selectedOutput.clear();SetAudioWatch(L"");Status(L"No audio output devices are available. Connect an output, then Refresh.");return;}
    SendMessage(outputCombo,CB_SETCURSEL,chosen,0); selectedOutput=outputs[chosen].id;
    bool available=outputs[chosen].state==DEVICE_STATE_ACTIVE,isCurrent=selectedOutput==current;
    EnableWindow(useOutput,available && !isCurrent && !busy);
    const wchar_t* hint=!available?L"Connect or enable this device to use its volume controls.":isCurrent?L"This is your current playback device. Adjust its volume and apps below.":L"Previewing this device's mixer. Click Play through this to send audio here.";
    Add(L"STATIC",hint,SS_NOPREFIX,16,107,532,38);
    float level=available?.65f:0; BOOL muted=FALSE;
    if(!demo && available) {
        ComPtr<IMMDevice> device; auto e=AudioEnumerator(); Check(e->GetDevice(selectedOutput.c_str(),&device));
        Check(device->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,&master));
        master->GetMasterVolumeLevelScalar(&level); master->GetMute(&muted);
        ComPtr<IAudioSessionManager2> manager; ComPtr<IAudioSessionEnumerator> sessions;
        Check(device->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,&manager));
        Check(manager->GetSessionEnumerator(&sessions)); int count=0; sessions->GetCount(&count);
        for(int i=0;i<count;++i) {
            ComPtr<IAudioSessionControl> control; ComPtr<IAudioSessionControl2> info; ComPtr<ISimpleAudioVolume> volume; AudioSessionState state;
            if(FAILED(sessions->GetSession(i,&control)) || FAILED(control.As(&info)) || FAILED(control.As(&volume))) continue;
            if(FAILED(control->GetState(&state)) || state==AudioSessionStateExpired) continue;
            DWORD pid=0;info->GetProcessId(&pid);
            AudioRow row;row.name=SessionName(control.Get(),info.Get());row.appName=row.name;row.volume=volume;row.pid=pid;row.state=state;row.icon=SessionIcon(pid);audioRows.push_back(std::move(row));
        }
    } else if(demo && available)for(const wchar_t* name:{L"Google Chrome",L"Discord",L"Music",L"Game",L"Google Chrome",L"System sounds",L"Video player",L"Voice chat",L"Browser - separate session",L"Other audio"}){AudioRow row;row.name=name;row.pid=4242;row.state=AudioSessionStateActive;row.icon=CopyIcon(LoadIcon(nullptr,IDI_APPLICATION));audioRows.push_back(std::move(row));}
    if(!demo)SyncAudioTitles();
    VolumeControls(120,154,L"Volume on this device",level,muted,masterSlider,masterMute,masterValue);
    EnableWindow(masterSlider,available);EnableWindow(masterMute,available);
    Section(L"App volumes on this device",216);
    int y=8;
    for(int i=0;i<static_cast<int>(audioRows.size());++i) {
        auto& row=audioRows[i];level=.5f;muted=FALSE;
        if(row.volume){row.volume->GetMasterVolume(&level);row.volume->GetMute(&muted);}
        AudioAdd(L"STATIC",L"",SS_OWNERDRAW,16,y+4,40,40,3000+i);
        row.titleLabel=AudioAdd(L"STATIC",row.name.c_str(),SS_ENDELLIPSIS|SS_NOPREFIX,68,y,290,42);
        std::wstring detail=(row.state==AudioSessionStateActive?L"Playing":L"Idle")+std::wstring(L"  |  ")+(row.appName.empty()?row.name:row.appName);
        if(row.pid)detail+=L"  |  PID "+std::to_wstring(row.pid);
        AudioAdd(L"STATIC",detail.c_str(),SS_ENDELLIPSIS|SS_NOPREFIX,68,y+43,478,23);
        row.value=AudioAdd(L"STATIC",(std::to_wstring(static_cast<int>(level*100+.5f))+L"%").c_str(),SS_RIGHT,374,y,55,24);
        row.mute=AudioAdd(L"BUTTON",L"Mute",BS_AUTOCHECKBOX,448,y,90,24,1001+2*i);SendMessage(row.mute,BM_SETCHECK,muted?BST_CHECKED:BST_UNCHECKED,0);
        row.slider=AudioAdd(TRACKBAR_CLASS,L"",TBS_HORZ|TBS_NOTICKS,68,y+69,478,30,1000+2*i);SendMessage(row.slider,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));SendMessage(row.slider,TBM_SETPOS,TRUE,static_cast<int>(level*100+.5f));SendMessage(row.slider,TBM_SETPAGESIZE,0,5);
        y+=119;
    }
    if(audioRows.empty()) AudioAdd(L"STATIC",available?L"No apps are using this output yet. Start audio in an app to see it here.":L"This device is disconnected or disabled.",0,16,y,530,44);
    audioHeight=y+50;ContentHeight(0);LayoutAudio();
    if(!demo){
        SetAudioWatch(available?selectedOutput:current);
        for(auto range:std::vector<std::pair<DWORD,DWORD>>{{EVENT_OBJECT_NAMECHANGE,EVENT_OBJECT_NAMECHANGE},{EVENT_OBJECT_DESTROY,EVENT_OBJECT_HIDE}}){
            auto hook=SetWinEventHook(range.first,range.second,nullptr,AudioWindowEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);if(hook)audioTitleHooks.push_back(hook);
        }
    }
    Status(L"Live audio sessions. Scroll for more apps. Browsers may combine tabs into one session.");
}
static void AudioSlider(HWND slider) {
    int value=static_cast<int>(SendMessage(slider,TBM_GETPOS,0,0)); float scalar=value/100.0f; HRESULT hr=S_OK; HWND label=nullptr;
    if(slider==masterSlider) {if(master)hr=master->SetMasterVolumeLevelScalar(scalar,&AudioContext);label=masterValue;}
    else for(auto& row:audioRows) if(row.slider==slider) {if(row.volume)hr=row.volume->SetMasterVolume(scalar,&AudioContext);label=row.value;break;}
    if(label) SetWindowText(label,(std::to_wstring(value)+L"%").c_str());
    if(FAILED(hr)) Status(L"Could not set volume: "+ErrorText(hr));
}
static void SyncAudioLevels(){
    auto update=[](HWND slider,HWND label,HWND mute,float level,BOOL muted){if(!slider)return;int n=static_cast<int>(level*100+.5f);SendMessage(slider,TBM_SETPOS,TRUE,n);SetWindowText(label,(std::to_wstring(n)+L"%").c_str());SendMessage(mute,BM_SETCHECK,muted?BST_CHECKED:BST_UNCHECKED,0);};
    float level=0;BOOL muted=FALSE;
    if(master && SUCCEEDED(master->GetMasterVolumeLevelScalar(&level)) && SUCCEEDED(master->GetMute(&muted)))update(masterSlider,masterValue,masterMute,level,muted);
    for(auto& row:audioRows)if(row.volume && SUCCEEDED(row.volume->GetMasterVolume(&level)) && SUCCEEDED(row.volume->GetMute(&muted)))update(row.slider,row.value,row.mute,level,muted);
}
static std::wstring BluetoothAudioRequest(std::wstring id,bool connect) {
    auto e=AudioEnumerator(); ComPtr<IMMDevice> d; Check(e->GetDevice(id.c_str(),&d));
    auto ks=BluetoothControl(d.Get()); ULONG size=0;
    KSPROPERTY property{KSPROPSETID_BtAudio,static_cast<ULONG>(connect?KSPROPERTY_ONESHOT_RECONNECT:KSPROPERTY_ONESHOT_DISCONNECT),KSPROPERTY_TYPE_GET};
    Check(ks->KsProperty(&property,sizeof(property),nullptr,0,&size));
    for(int i=0;i<20;++i) {
        DWORD state=0;d->GetState(&state);
        if((connect && state==DEVICE_STATE_ACTIVE) || (!connect && state!=DEVICE_STATE_ACTIVE)) return connect?L"Headphones connected.":L"Headphones disconnected.";
        Sleep(500);
    }
    return connect?L"Connection requested, but audio is not connected yet. Turn on the headphones and release their connection to another device.":L"Disconnect requested. Refresh to check the current state.";
}
