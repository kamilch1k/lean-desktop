#pragma once
#include <wlanapi.h>
#include <iphlpapi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Radios.h>
using namespace winrt::Windows::Devices::Enumeration;
using namespace winrt::Windows::Devices::Radios;
struct BtDevice { DeviceInformation info{nullptr}; std::wstring name; bool paired=false,connected=false;std::wstring container,aepContainer; };
static std::vector<BtDevice> bluetoothDevices;
static HWND bluetoothList{}, wifiList{}, wifiAdapterCombo{};
static std::vector<AudioOutput> bluetoothOutputs;
static HWND bluetoothRadioLabel{};
static std::wstring bluetoothRadioStatus=L"Checking Bluetooth...";
static HANDLE wlan{};
static std::vector<WLAN_INTERFACE_INFO> wifiAdapters;
struct WifiNetwork { std::wstring name,profile; DOT11_SSID ssid{}; DOT11_AUTH_ALGORITHM auth{}; DOT11_CIPHER_ALGORITHM cipher{}; bool connected=false,secure=false; ULONG signal=0; };
static std::vector<WifiNetwork> wifiNetworks;
static int adapterIndex=0;

static std::wstring Wide(const char* text,int length) {
    int count=MultiByteToWideChar(CP_UTF8,0,text,length,nullptr,0);std::wstring out(count,L' ');
    if(count) MultiByteToWideChar(CP_UTF8,0,text,length,&out[0],count);return out;
}
static std::wstring XmlEscape(const std::wstring& text) {
    std::wstring result;for(wchar_t c:text) switch(c){case L'&':result+=L"&amp;";break;case L'<':result+=L"&lt;";break;case L'>':result+=L"&gt;";break;case L'\"':result+=L"&quot;";break;case L'\'':result+=L"&apos;";break;default:result+=c;}return result;
}
static std::wstring WifiProfile(const WifiNetwork& n,const std::wstring& password) {
    std::wstring hex;const wchar_t* digits=L"0123456789ABCDEF";
    for(ULONG i=0;i<n.ssid.uSSIDLength;++i){hex+=digits[n.ssid.ucSSID[i]>>4];hex+=digits[n.ssid.ucSSID[i]&15];}
    std::wstring auth=n.secure?(n.auth==DOT11_AUTH_ALGO_WPA3_SAE?L"WPA3SAE":L"WPA2PSK"):L"open";
    std::wstring xml=L"<?xml version=\"1.0\"?><WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\"><name>"+XmlEscape(n.name)+L"</name><SSIDConfig><SSID><hex>"+hex+L"</hex></SSID></SSIDConfig><connectionType>ESS</connectionType><connectionMode>auto</connectionMode><MSM><security><authEncryption><authentication>"+auth+L"</authentication><encryption>"+(n.secure?L"AES":L"none")+L"</encryption><useOneX>false</useOneX></authEncryption>";
    if(n.secure) xml+=L"<sharedKey><keyType>passPhrase</keyType><protected>false</protected><keyMaterial>"+XmlEscape(password)+L"</keyMaterial></sharedKey>";
    return xml+L"</security></MSM></WLANProfile>";
}
static HWND MakeList(int id,int y,int height,const std::vector<std::pair<std::wstring,int>>& columns) {
    HWND list=Add(WC_LISTVIEW,L"",LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_BORDER,16,y,532,height,id);
    ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
    int i=0;for(auto& c:columns){LVCOLUMN column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=const_cast<LPWSTR>(c.first.c_str());column.cx=S(c.second);ListView_InsertColumn(list,i++,&column);}return list;
}
static void ListRow(HWND list,int i,const std::wstring& name,const std::wstring& detail) {
    LVITEM item{};item.mask=LVIF_TEXT;item.iItem=i;item.pszText=const_cast<LPWSTR>(name.c_str());ListView_InsertItem(list,&item);
    ListView_SetItemText(list,i,1,const_cast<LPWSTR>(detail.c_str()));
}
static bool DeviceFlag(const DeviceInformation& d,const wchar_t* property) {
    try{auto value=d.Properties().TryLookup(property);return value && winrt::unbox_value<bool>(value);}catch(...){return false;}
}
static void UpdateBluetoothDeviceButtons(){
    int i=bluetoothList?ListView_GetNextItem(bluetoothList,-1,LVNI_SELECTED):-1;
    bool selected=i>=0 && i<static_cast<int>(bluetoothDevices.size());
    EnableWindow(GetDlgItem(content,214),selected && !bluetoothDevices[i].paired);
    EnableWindow(GetDlgItem(content,215),selected && bluetoothDevices[i].paired);
}
static std::wstring DeviceContainer(const DeviceInformation& d,const wchar_t* property){
    try{auto value=d.Properties().TryLookup(property);return value?ContainerKey(winrt::unbox_value<winrt::guid>(value)):L"";}catch(...){return L"";}
}
static std::vector<AudioOutput> DeviceAudio(const BtDevice& device,const std::vector<AudioOutput>& all){
    std::vector<AudioOutput> found;
    for(const auto& output:all)if(!output.container.empty() && (output.container==device.container || output.container==device.aepContainer))found.push_back(output);
    return found;
}
static int PreferredBluetoothOutput(const std::vector<AudioOutput>& audio,bool activeOnly=false){
    auto current=CurrentOutput();int chosen=-1;
    for(int i=0;i<static_cast<int>(audio.size());++i){auto& a=audio[i];if(a.state&DEVICE_STATE_DISABLED || (activeOnly && a.state!=DEVICE_STATE_ACTIVE))continue;
        if(chosen<0 || (a.state==DEVICE_STATE_ACTIVE && audio[chosen].state!=DEVICE_STATE_ACTIVE) || a.id==current)chosen=i;
        if(a.id==current && a.state==DEVICE_STATE_ACTIVE)break;
    }return chosen;
}
static void FillBluetoothList() {
    if(!bluetoothList)return;ListView_DeleteAllItems(bluetoothList);auto current=CurrentOutput();
    for(int i=0;i<static_cast<int>(bluetoothDevices.size());++i) {auto& d=bluetoothDevices[i];auto audio=DeviceAudio(d,bluetoothOutputs);int active=PreferredBluetoothOutput(audio,true);
        std::wstring state=!d.paired?L"Ready to pair":active>=0?(audio[active].id==current?L"Connected · sound output":L"Connected"):
            !audio.empty()?L"Paired · disconnected":d.connected?L"Connected":L"Paired";
        ListRow(bluetoothList,i,d.name,state);
    }
    UpdateBluetoothDeviceButtons();
}
static std::vector<BtDevice> CollectBluetoothDevices(bool discover) {
        std::vector<BtDevice> found;
        const wchar_t* query=L"System.Devices.Aep.ProtocolId:=\"{e0cbf06c-cd8b-4647-bb8a-263b43f0f974}\" OR System.Devices.Aep.ProtocolId:=\"{bb7bb05e-5972-42b5-94fc-76eaa7084d49}\"";
        auto props=winrt::single_threaded_vector<winrt::hstring>({L"System.Devices.Aep.IsConnected",L"System.Devices.ContainerId",L"System.Devices.Aep.ContainerId"});
        auto collect=[&](const DeviceInformation& d){if(d.Name().empty())return;found.push_back({d,d.Name().c_str(),d.Pairing().IsPaired(),DeviceFlag(d,L"System.Devices.Aep.IsConnected"),DeviceContainer(d,L"System.Devices.ContainerId"),DeviceContainer(d,L"System.Devices.Aep.ContainerId")});};
        {
            struct ScanState {std::mutex lock;std::map<std::wstring,DeviceInformation> devices;};
            auto state=std::make_shared<ScanState>();
            auto watcher=DeviceInformation::CreateWatcher(query,props,DeviceInformationKind::AssociationEndpoint);
            auto added=watcher.Added([state](auto const&,DeviceInformation const& d){try{std::lock_guard<std::mutex> guard(state->lock);state->devices.insert_or_assign(d.Id().c_str(),d);}catch(...){}});
            auto updated=watcher.Updated([state](auto const&,DeviceInformationUpdate const& update){try{std::lock_guard<std::mutex> guard(state->lock);auto it=state->devices.find(update.Id().c_str());if(it!=state->devices.end())it->second.Update(update);}catch(...){}});
            watcher.Start();Sleep(discover?8000:3000);watcher.Stop();
            watcher.Added(added);watcher.Updated(updated);
            std::lock_guard<std::mutex> guard(state->lock);for(auto& d:state->devices)collect(d.second);
        }
        if(!discover)found.erase(std::remove_if(found.begin(),found.end(),[](auto& d){return !d.paired;}),found.end());
        return found;
}
static void ReadBluetooth(bool discover) {
    RunAsync([discover]{
        auto found=CollectBluetoothDevices(discover);
        bool hasRadio=false,on=false;
        for(auto r:Radio::GetRadiosAsync().get())if(r.Kind()==RadioKind::Bluetooth){hasRadio=true;on=on || r.State()==RadioState::On;}
        std::wstring radios=!hasRadio?L"No Bluetooth adapter found":on?L"Bluetooth is on":L"Bluetooth is off";
        Deliver([found=std::move(found),radios]() mutable {bluetoothDevices=std::move(found);bluetoothRadioStatus=radios;if(page==1){FillBluetoothList();SetWindowText(bluetoothRadioLabel,radios.c_str());}});
        return discover?L"Scan complete. Select a device, then choose Pair.":L"Right-click your headphones and choose Connect.";
    },discover?L"Scanning for eight seconds. Put the new device in pairing mode...":L"Reading Bluetooth devices...");
}
static void BuildBluetooth(bool scan) {
    Section(L"Bluetooth",6);
    bluetoothRadioLabel=Add(L"STATIC",demo?L"Bluetooth is on":bluetoothRadioStatus.c_str(),0,16,38,532,24);
    Add(L"BUTTON",L"Turn on",0,16,70,110,30,210);Add(L"BUTTON",L"Turn off",0,138,70,110,30,211);Add(L"BUTTON",L"Find new devices",0,260,70,174,30,212);
    Section(L"Devices",116);
    bluetoothList=MakeList(213,150,300,{{L"Device",300},{L"Status",209}});
    bluetoothOutputs=demo?DemoOutputs():ReadOutputs(true);
    if(demo)bluetoothDevices={{nullptr,L"Wireless headphones",true,false,L"demo-wireless-container",L""},{nullptr,L"Headphones",true,true,L"demo-headphones-container",L""},{nullptr,L"Bluetooth mouse",true,true,L"demo-mouse-container",L""}};
    FillBluetoothList();
    Add(L"BUTTON",L"Pair selected",0,16,462,145,30,214);Add(L"BUTTON",L"Forget selected",0,174,462,145,30,215);
    Add(L"STATIC",L"Right-click headphones → Connect. This also selects them for sound.\nThe same menu has Disconnect and Use for sound.",0,16,511,532,50);
    ContentHeight(568);UpdateBluetoothDeviceButtons();
    Status(L"Right-click a device for its actions. Shift+F10 works with the selected device too.");
    if(!demo && scan)ReadBluetooth(false);
}
static void PairBluetoothDevice(const BtDevice& d,bool forget) {
    if(demo){Status(L"Preview only. No device changed.");return;}
    if(forget && MessageBox(mainWindow,(L"Forget "+d.name+L"? You will need to pair it again.").c_str(),L"Forget device",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
    RunAsync([d,forget]{
        if(forget){auto r=d.info.Pairing().UnpairAsync().get();return r.Status()==DeviceUnpairingResultStatus::Unpaired || r.Status()==DeviceUnpairingResultStatus::AlreadyUnpaired?L"Device forgotten. Refresh to update the list.":L"Windows could not unpair this device (status "+std::to_wstring(static_cast<int>(r.Status()))+L").";}
        auto custom=d.info.Pairing().Custom();
        auto token=custom.PairingRequested([](auto const&,DevicePairingRequestedEventArgs const& args){
            if(args.PairingKind()==DevicePairingKinds::ConfirmOnly)args.Accept();
            else if(args.PairingKind()==DevicePairingKinds::ConfirmPinMatch){if(MessageBox(mainWindow,(L"Does this code match the one on your device?\n\n"+std::wstring(args.Pin().c_str())).c_str(),L"Bluetooth pairing",MB_YESNO|MB_ICONQUESTION)==IDYES)args.Accept();}
            else if(args.PairingKind()==DevicePairingKinds::DisplayPin){MessageBox(mainWindow,(L"Enter this code on your device:\n\n"+std::wstring(args.Pin().c_str())).c_str(),L"Bluetooth pairing",MB_OK);args.Accept();}
            else if(args.PairingKind()==DevicePairingKinds::ProvidePin){std::wstring pin;if(AskText(L"Bluetooth PIN",L"Enter the PIN for this device:",pin,false))args.Accept(pin);}
        });
        auto result=custom.PairAsync(DevicePairingKinds::ConfirmOnly|DevicePairingKinds::ConfirmPinMatch|DevicePairingKinds::DisplayPin|DevicePairingKinds::ProvidePin).get();custom.PairingRequested(token);
        return result.Status()==DevicePairingResultStatus::Paired || result.Status()==DevicePairingResultStatus::AlreadyPaired?L"Device paired. Refresh, then connect its audio if needed.":L"Pairing did not complete (status "+std::to_wstring(static_cast<int>(result.Status()))+L").";
    },forget?L"Forgetting device...":L"Pairing device...");
}
static void PairBluetooth(bool forget){
    int i=ListView_GetNextItem(bluetoothList,-1,LVNI_SELECTED);
    if(i<0 || i>=static_cast<int>(bluetoothDevices.size())){Status(L"Select a Bluetooth device first.");return;}PairBluetoothDevice(bluetoothDevices[i],forget);
}
static HMENU BluetoothDeviceMenu(const BtDevice& device,const std::vector<AudioOutput>& audio){
    HMENU menu=CreatePopupMenu();int active=PreferredBluetoothOutput(audio,true);
    bool connect=device.paired && std::any_of(audio.begin(),audio.end(),[](auto& a){return a.bluetooth && !(a.state&DEVICE_STATE_DISABLED);});
    bool disconnect=std::any_of(audio.begin(),audio.end(),[](auto& a){return a.bluetooth && a.state==DEVICE_STATE_ACTIVE;});
    auto add=[&](UINT id,const wchar_t* text,bool enabled){AppendMenu(menu,MF_STRING|(!busy && enabled?MF_ENABLED:MF_GRAYED),id,text);};
    add(4001,L"Connect",connect || active>=0);add(4002,L"Disconnect",disconnect);add(4003,L"Use for sound",active>=0);
    AppendMenu(menu,MF_SEPARATOR,0,nullptr);add(4010,L"Pair",!device.paired);add(4011,L"Forget device",device.paired);return menu;
}
static void BluetoothDeviceAction(const BtDevice& device,UINT command){
    if(command==4010 || command==4011){PairBluetoothDevice(device,command==4011);return;}
    if(command<4001 || command>4003)return;
    if(demo){Status(L"Preview: "+device.name+L" selected. No real device changed.");return;}
    RunAsync([device,command]() -> std::wstring {
        // Re-resolve this physical device, never the current selection or a name match.
        auto audio=DeviceAudio(device,ReadOutputs(true));int active=PreferredBluetoothOutput(audio,true);
        if(command==4002){
            int disconnected=0;for(auto& a:audio)if(a.bluetooth && a.state==DEVICE_STATE_ACTIVE){BluetoothAudioRequest(a.id,false);++disconnected;}
            auto after=DeviceAudio(device,ReadOutputs(true));return PreferredBluetoothOutput(after,true)<0?device.name+L" disconnected.":disconnected?L"Disconnect requested. Refresh to check the device.":L"No supported connected audio output was found for this device.";
        }
        if(active<0 && command==4001){
            auto candidate=std::find_if(audio.begin(),audio.end(),[](auto& a){return a.bluetooth && !(a.state&DEVICE_STATE_DISABLED);});
            if(candidate==audio.end())return L"No supported audio connection was found for these headphones. Pair them first, then Refresh.";
            auto result=BluetoothAudioRequest(candidate->id,true);audio=DeviceAudio(device,ReadOutputs(true));active=PreferredBluetoothOutput(audio,true);if(active<0)return result;
        }
        if(active<0)return L"These headphones are not connected. Choose Connect first.";
        HRESULT hr=MakeDefaultOutput(audio[active].id);
        if(FAILED(hr))return device.name+L" is connected, but Windows could not select it for sound: "+ErrorText(hr);
        if(DefaultOutput()!=audio[active].id)return device.name+L" is connected. Windows has not switched the sound output yet.";
        return L"Connected and playing through: "+device.name;
    },command==4002?L"Disconnecting "+device.name+L"...":L"Connecting / selecting "+device.name+L"...",true);
}
static void ShowBluetoothMenu(POINT point){
    int index=-1;
    if(point.x==-1 && point.y==-1){index=ListView_GetNextItem(bluetoothList,-1,LVNI_SELECTED);RECT row{};if(index>=0 && ListView_GetItemRect(bluetoothList,index,&row,LVIR_BOUNDS)){point={row.left+S(20),row.bottom};ClientToScreen(bluetoothList,&point);}}
    else {POINT local=point;ScreenToClient(bluetoothList,&local);LVHITTESTINFO hit{};hit.pt=local;index=ListView_HitTest(bluetoothList,&hit);}
    if(index<0 || index>=static_cast<int>(bluetoothDevices.size()))return;
    ListView_SetItemState(bluetoothList,index,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    auto device=bluetoothDevices[index];auto audio=DeviceAudio(device,bluetoothOutputs);HMENU menu=BluetoothDeviceMenu(device,audio);
    UINT command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,mainWindow,nullptr);DestroyMenu(menu);
    if(command)BluetoothDeviceAction(device,command);
}
static std::wstring SetBluetoothRadio(bool on) {
    auto access=Radio::RequestAccessAsync().get();
    if(access!=RadioAccessStatus::Allowed)return L"Windows denied permission to change Bluetooth radio state.";
    bool found=false;
    for(auto radio:Radio::GetRadiosAsync().get())if(radio.Kind()==RadioKind::Bluetooth){found=true;if(radio.SetStateAsync(on?RadioState::On:RadioState::Off).get()!=RadioAccessStatus::Allowed)return L"Windows could not change the Bluetooth radio state.";}
    std::wstring state=found?(on?L"Bluetooth is on":L"Bluetooth is off"):L"No Bluetooth adapter found";
    Deliver([state]{bluetoothRadioStatus=state;if(page==1 && bluetoothRadioLabel)SetWindowText(bluetoothRadioLabel,state.c_str());});
    return state;
}
static void CALLBACK WifiChanged(PWLAN_NOTIFICATION_DATA data,PVOID) {
    if(data->NotificationSource!=WLAN_NOTIFICATION_SOURCE_ACM)return;
    if((data->NotificationCode==wlan_notification_acm_connection_attempt_fail || data->NotificationCode==wlan_notification_acm_connection_complete) && data->dwDataSize>=sizeof(WLAN_CONNECTION_NOTIFICATION_DATA)){
        auto result=static_cast<WLAN_CONNECTION_NOTIFICATION_DATA*>(data->pData);
        if(result->wlanReasonCode!=WLAN_REASON_CODE_SUCCESS){PostMessage(mainWindow,WM_APP+5,result->wlanReasonCode,0);return;}
    }
    PostMessage(mainWindow,WM_APP+4,data->NotificationCode,0);
}
static void OpenWifi() {
    if(wlan)return;DWORD version=0;DWORD error=WlanOpenHandle(2,nullptr,&version,&wlan);if(error)throw HRESULT_FROM_WIN32(error);
    WlanRegisterNotification(wlan,WLAN_NOTIFICATION_SOURCE_ACM,TRUE,WifiChanged,nullptr,nullptr,nullptr);
}
static void BuildNetwork() {
    Add(L"STATIC",L"Wi-Fi adapter",0,16,12,520,22);
    wifiAdapterCombo=Add(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,16,39,530,200,310);
    Add(L"BUTTON",L"Scan",0,16,78,100,30,311);Add(L"BUTTON",L"Wi-Fi on",0,128,78,100,30,312);Add(L"BUTTON",L"Wi-Fi off",0,240,78,100,30,313);
    wifiList=MakeList(314,122,266,{{L"Network",320},{L"State / signal",190}});
    Add(L"BUTTON",L"Connect",0,16,402,130,30,315);Add(L"BUTTON",L"Disconnect",0,158,402,130,30,316);
    Add(L"STATIC",L"Saved networks reconnect using Windows credentials. New home networks prompt for a password here.",0,16,452,530,48);
    ContentHeight(515);wifiNetworks.clear();wifiAdapters.clear();
    if(demo){SendMessage(wifiAdapterCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Wireless adapter"));SendMessage(wifiAdapterCombo,CB_SETCURSEL,0,0);ListRow(wifiList,0,L"Home Wi-Fi",L"Connected - 95%");ListRow(wifiList,1,L"Studio",L"Saved - 80%");return;}
    OpenWifi();PWLAN_INTERFACE_INFO_LIST interfaces=nullptr;DWORD error=WlanEnumInterfaces(wlan,nullptr,&interfaces);if(error)throw HRESULT_FROM_WIN32(error);
    for(DWORD i=0;i<interfaces->dwNumberOfItems;++i)wifiAdapters.push_back(interfaces->InterfaceInfo[i]);WlanFreeMemory(interfaces);
    for(auto& adapter:wifiAdapters)SendMessage(wifiAdapterCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(adapter.strInterfaceDescription));
    if(wifiAdapters.empty()){Status(L"No Wi-Fi adapter is available. Wired networking continues independently of Explorer.");return;}
    adapterIndex=std::min(adapterIndex,static_cast<int>(wifiAdapters.size())-1);SendMessage(wifiAdapterCombo,CB_SETCURSEL,adapterIndex,0);
    auto& adapter=wifiAdapters[adapterIndex];PWLAN_AVAILABLE_NETWORK_LIST networks=nullptr;
    error=WlanGetAvailableNetworkList(wlan,&adapter.InterfaceGuid,WLAN_AVAILABLE_NETWORK_INCLUDE_ALL_MANUAL_HIDDEN_PROFILES,nullptr,&networks);
    if(!error){
        for(DWORD i=0;i<networks->dwNumberOfItems;++i){auto& n=networks->Network[i];
            if(n.dot11Ssid.uSSIDLength==0)continue;
            std::wstring name=Wide(reinterpret_cast<const char*>(n.dot11Ssid.ucSSID),n.dot11Ssid.uSSIDLength);
            wifiNetworks.push_back({name,n.strProfileName,n.dot11Ssid,n.dot11DefaultAuthAlgorithm,n.dot11DefaultCipherAlgorithm,(n.dwFlags&WLAN_AVAILABLE_NETWORK_CONNECTED)!=0,n.bSecurityEnabled!=FALSE,n.wlanSignalQuality});
        }WlanFreeMemory(networks);
    }else{
        PWLAN_PROFILE_INFO_LIST profiles=nullptr;
        if(!WlanGetProfileList(wlan,&adapter.InterfaceGuid,nullptr,&profiles)){for(DWORD i=0;i<profiles->dwNumberOfItems;++i){WifiNetwork n;n.name=n.profile=profiles->ProfileInfo[i].strProfileName;wifiNetworks.push_back(n);}WlanFreeMemory(profiles);}
    }
    for(int i=0;i<static_cast<int>(wifiNetworks.size());++i){auto& n=wifiNetworks[i];std::wstring state=n.connected?L"Connected":n.profile.empty()?L"Available":L"Saved";if(!error)state+=L" - "+std::to_wstring(n.signal)+L"%";ListRow(wifiList,i,n.name,state);}
    Status(error==ERROR_ACCESS_DENIED?L"Windows blocks nearby Wi-Fi scans without location permission. Saved networks are still listed.":error?L"Nearby scan unavailable: "+ErrorText(HRESULT_FROM_WIN32(error)):L"Select a network to connect. Current connections change only when you use a control.");
}
static void ConnectWifi() {
    int index=ListView_GetNextItem(wifiList,-1,LVNI_SELECTED);
    if(index<0 || index>=static_cast<int>(wifiNetworks.size()) || wifiAdapters.empty()){Status(L"Select a network first.");return;}
    auto n=wifiNetworks[index];auto guid=wifiAdapters[adapterIndex].InterfaceGuid;DWORD error=0;
    if(n.profile.empty()){
        if(n.secure && (n.cipher!=DOT11_CIPHER_ALGO_CCMP || (n.auth!=DOT11_AUTH_ALGO_RSNA_PSK && n.auth!=DOT11_AUTH_ALGO_WPA3_SAE))){Status(L"This network needs a preconfigured enterprise or legacy-security profile.");return;}
        std::wstring password;
        if(n.secure && !AskText(L"Connect to "+n.name,L"Wi-Fi password (saved securely by Windows):",password,true))return;
        if(n.secure && (password.size()<8 || password.size()>63)){Status(L"Use an 8-63 character Wi-Fi passphrase.");return;}
        std::wstring profile=WifiProfile(n,password);DWORD reason=0;
        error=WlanSetProfile(wlan,&guid,WLAN_PROFILE_USER,profile.c_str(),nullptr,FALSE,nullptr,&reason);
        if(!password.empty())SecureZeroMemory(&password[0],password.size()*sizeof(wchar_t));
        SecureZeroMemory(&profile[0],profile.size()*sizeof(wchar_t));
        if(error){wchar_t why[1024]{};WlanReasonCodeToString(reason,1024,why,nullptr);Status(L"Could not save this Wi-Fi profile: "+std::wstring(why)+L" "+ErrorText(HRESULT_FROM_WIN32(error)));return;}
        n.profile=n.name;
    }
    WLAN_CONNECTION_PARAMETERS params{};params.wlanConnectionMode=wlan_connection_mode_profile;params.strProfile=n.profile.c_str();params.dot11BssType=dot11_BSS_type_infrastructure;
    error=WlanConnect(wlan,&guid,&params,nullptr);Status(error?L"Could not connect: "+ErrorText(HRESULT_FROM_WIN32(error)):L"Connecting... Windows will report the result.");
}
static void WifiRadio(bool on) {
    if(wifiAdapters.empty())return;
    auto guid=wifiAdapters[adapterIndex].InterfaceGuid;DWORD size=0;WLAN_RADIO_STATE* state=nullptr;
    DWORD error=WlanQueryInterface(wlan,&guid,wlan_intf_opcode_radio_state,nullptr,&size,reinterpret_cast<void**>(&state),nullptr);
    if(!error){for(DWORD i=0;i<state->dwNumberOfPhys;++i){WLAN_PHY_RADIO_STATE phy=state->PhyRadioState[i];phy.dot11SoftwareRadioState=on?dot11_radio_state_on:dot11_radio_state_off;error=WlanSetInterface(wlan,&guid,wlan_intf_opcode_radio_state,sizeof(phy),&phy,nullptr);if(error)break;}WlanFreeMemory(state);}
    Status(error?L"Could not change Wi-Fi state: "+ErrorText(HRESULT_FROM_WIN32(error)):(on?L"Wi-Fi enabled.":L"Wi-Fi disabled."));
}
