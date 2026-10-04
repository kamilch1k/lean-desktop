#pragma once
#include <wlanapi.h>
#include <iphlpapi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Radios.h>
using namespace winrt::Windows::Devices::Enumeration;
using namespace winrt::Windows::Devices::Radios;
struct BtDevice { DeviceInformation info{nullptr}; std::wstring name; bool paired=false,connected=false; };
static std::vector<BtDevice> bluetoothDevices;
static HWND bluetoothList{}, bluetoothAudioCombo{}, wifiList{}, wifiAdapterCombo{};
static std::vector<AudioOutput> bluetoothOutputs;
static HWND bluetoothRadioLabel{},bluetoothAudioStatus{};
static std::wstring bluetoothRadioStatus=L"Checking Bluetooth...",selectedBluetoothOutput;
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
static void UpdateBluetoothAudioButtons(){
    int i=bluetoothAudioCombo?static_cast<int>(SendMessage(bluetoothAudioCombo,CB_GETCURSEL,0,0)):-1;
    bool selected=i>=0 && i<static_cast<int>(bluetoothOutputs.size());
    bool connected=selected && bluetoothOutputs[i].state==DEVICE_STATE_ACTIVE;
    bool current=selected && bluetoothOutputs[i].id==CurrentOutput();
    EnableWindow(GetDlgItem(content,217),selected && !connected && !(bluetoothOutputs[i].state&DEVICE_STATE_DISABLED));
    EnableWindow(GetDlgItem(content,218),connected);
    EnableWindow(GetDlgItem(content,219),connected && !current && !busy);
    if(selected)selectedBluetoothOutput=bluetoothOutputs[i].id;
    const wchar_t* state=!selected?L"No compatible headphone output found. Pair headphones, then refresh.":current?L"Connected and selected for playback.":connected?L"Connected. Choose Use for sound to make this your playback device.":L"Not connected. Turn on your headphones and choose Connect.";
    if(bluetoothAudioStatus)SetWindowText(bluetoothAudioStatus,state);
}
static void FillBluetoothList() {
    if(!bluetoothList)return;ListView_DeleteAllItems(bluetoothList);
    for(int i=0;i<static_cast<int>(bluetoothDevices.size());++i) {auto& d=bluetoothDevices[i];ListRow(bluetoothList,i,d.name,d.connected?L"Connected":d.paired?L"Paired":L"Ready to pair");}
    UpdateBluetoothDeviceButtons();
}
static std::vector<BtDevice> CollectBluetoothDevices(bool discover) {
        std::vector<BtDevice> found;
        const wchar_t* query=L"System.Devices.Aep.ProtocolId:=\"{e0cbf06c-cd8b-4647-bb8a-263b43f0f974}\" OR System.Devices.Aep.ProtocolId:=\"{bb7bb05e-5972-42b5-94fc-76eaa7084d49}\"";
        auto props=winrt::single_threaded_vector<winrt::hstring>({L"System.Devices.Aep.IsConnected"});
        auto collect=[&](const DeviceInformation& d){if(d.Name().empty())return;found.push_back({d,d.Name().c_str(),d.Pairing().IsPaired(),DeviceFlag(d,L"System.Devices.Aep.IsConnected")});};
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
        return discover?L"Scan complete. Select a device, then choose Pair.":L"Paired devices are listed above. Connect headphones in the Audio section.";
    },discover?L"Scanning for eight seconds. Put the new device in pairing mode...":L"Reading Bluetooth devices...");
}
static void BuildBluetooth(bool scan) {
    Section(L"Bluetooth",6);
    bluetoothRadioLabel=Add(L"STATIC",demo?L"Bluetooth is on":bluetoothRadioStatus.c_str(),0,16,38,532,24);
    Add(L"BUTTON",L"Turn on",0,16,70,110,30,210);Add(L"BUTTON",L"Turn off",0,138,70,110,30,211);Add(L"BUTTON",L"Find new devices",0,260,70,174,30,212);
    Section(L"Devices",116);
    bluetoothList=MakeList(213,150,160,{{L"Device",345},{L"Status",164}});
    if(demo) {ListRow(bluetoothList,0,L"Wireless headphones",L"Paired");ListRow(bluetoothList,1,L"Bluetooth mouse",L"Connected");}
    else FillBluetoothList();
    Add(L"BUTTON",L"Pair selected",0,16,320,145,30,214);Add(L"BUTTON",L"Forget selected",0,174,320,145,30,215);
    Section(L"Headphone audio",366);
    bluetoothAudioCombo=Add(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,16,402,532,230,216);
    bluetoothOutputs.clear();
    for(auto& output:demo?DemoOutputs():ReadOutputs(true))if(output.bluetooth)bluetoothOutputs.push_back(output);
    int chosen=0;auto current=CurrentOutput();
    for(int i=0;i<static_cast<int>(bluetoothOutputs.size());++i){auto& output=bluetoothOutputs[i];std::wstring text=output.name+L"  -  "+OutputState(output,current);SendMessage(bluetoothAudioCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));if(output.id==selectedBluetoothOutput || (selectedBluetoothOutput.empty() && output.id==current))chosen=i;}
    SendMessage(bluetoothAudioCombo,CB_SETCURSEL,chosen,0);
    bluetoothAudioStatus=Add(L"STATIC",L"",0,16,439,532,39);
    Add(L"BUTTON",L"Connect",0,16,488,140,30,217);Add(L"BUTTON",L"Disconnect",0,168,488,140,30,218);Add(L"BUTTON",L"Use for sound",0,320,488,168,30,219);
    Add(L"STATIC",L"Pairing remembers the device. Connecting makes it available. Use for sound selects where Windows plays audio.",0,16,535,532,46);
    ContentHeight(590);UpdateBluetoothDeviceButtons();UpdateBluetoothAudioButtons();
    Status(L"Choose a device above to pair it. Headphone connection and playback controls are below.");
    if(!demo && scan)ReadBluetooth(false);
}
static void PairBluetooth(bool forget) {
    int i=ListView_GetNextItem(bluetoothList,-1,LVNI_SELECTED);
    if(i<0 || i>=static_cast<int>(bluetoothDevices.size())){Status(L"Select a Bluetooth device first.");return;}
    auto d=bluetoothDevices[i];
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
