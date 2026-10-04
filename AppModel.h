#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <cmath>
#include "WindowNames.h"

namespace Apps {
struct Process {
    DWORD pid=0,parent=0; ULONGLONG created=0,ticks=0,ram=0;
    std::wstring path; double cpu=0; bool cpuKnown=false,ramKnown=false,protectedProcess=true;
};
struct Window { HWND hwnd{}; DWORD pid=0; ULONGLONG created=0; std::wstring title; bool hung=false; };
struct Group {
    std::wstring key,name,path; std::vector<Process> processes; std::vector<Window> windows;
    ULONGLONG ram=0; double cpu=0; bool completeCpu=true,completeRam=true; HICON icon{};
};
struct Snapshot { std::vector<Group> groups; ULONGLONG elapsedMs=0; std::wstring error; };
static ULONGLONG Number(FILETIME t){return (static_cast<ULONGLONG>(t.dwHighDateTime)<<32)|t.dwLowDateTime;}
static std::wstring Directory(const std::wstring& path){auto pos=path.find_last_of(L"\\/");return pos==std::wstring::npos?L"":LowerName(path.substr(0,pos+1));}
static bool Under(const std::wstring& path,const std::wstring& directory){return !directory.empty() && LowerName(path).compare(0,directory.size(),directory)==0;}
static bool ShellProcess(const std::wstring& path){
    auto file=LowerName(path.substr(path.find_last_of(L"\\/")+1));
    return file==L"leanbar.exe" || file==L"quicksearch.companion.exe" || file==L"leanapps.exe";
}
static std::vector<BYTE> Owner(HANDLE process){
    HANDLE token{};if(!OpenProcessToken(process,TOKEN_QUERY,&token))return {};
    DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);std::vector<BYTE> data(size),sid;
    if(size && GetTokenInformation(token,TokenUser,data.data(),size,&size)){
        auto value=reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid;sid.resize(GetLengthSid(value));CopySid(static_cast<DWORD>(sid.size()),sid.data(),value);
    }CloseHandle(token);return sid;
}
static bool SameOwner(HANDLE process,const std::vector<BYTE>& owner){auto actual=Owner(process);return !owner.empty() && !actual.empty() && EqualSid(actual.data(),const_cast<BYTE*>(owner.data()));}
static bool Identity(HANDLE handle,const Process& p){
    FILETIME created{},exit{},kernel{},user{};wchar_t path[32768]{};DWORD size=32768;
    return GetProcessId(handle)==p.pid && GetProcessTimes(handle,&created,&exit,&kernel,&user) && Number(created)==p.created &&
        QueryFullProcessImageName(handle,0,path,&size) && !_wcsicmp(path,p.path.c_str()) && WaitForSingleObject(handle,0)==WAIT_TIMEOUT;
}
// Each action uses a held handle and the creation timestamp, never just a reused PID.
static HANDLE Verified(const Process& p,bool terminate,const std::vector<BYTE>& owner){
    if(ShellProcess(p.path) || p.pid==GetCurrentProcessId())return nullptr;
    HANDLE handle=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE|(terminate?PROCESS_TERMINATE:0),FALSE,p.pid);
    BOOL critical=TRUE;
    if(!handle)return nullptr;
    bool valid=Identity(handle,p) && SameOwner(handle,owner) && IsProcessCritical(handle,&critical) && !critical;
    if(terminate){wchar_t windows[MAX_PATH]{};GetWindowsDirectory(windows,MAX_PATH);valid=valid && !p.protectedProcess && !Under(p.path,LowerName(std::wstring(windows)+L"\\"));}
    if(!valid){CloseHandle(handle);return nullptr;}return handle;
}
static bool CloseWindow(const Window& window,const Process& process,const std::vector<BYTE>& owner){
    HANDLE handle=Verified(process,false,owner);if(!handle)return false;
    DWORD pid=0;GetWindowThreadProcessId(window.hwnd,&pid);
    bool ok=pid==window.pid && pid==process.pid && window.created==process.created && PostMessage(window.hwnd,WM_CLOSE,0,0)!=FALSE;
    CloseHandle(handle);return ok;
}
static bool EndProcess(const Process& p,const std::vector<BYTE>& owner){
    HANDLE handle=Verified(p,true,owner);if(!handle)return false;
    bool result=TerminateProcess(handle,1)!=FALSE;CloseHandle(handle);return result;
}
static BOOL CALLBACK ReadWindow(HWND w,LPARAM data){
    if(!IsWindowVisible(w))return TRUE;
    auto ex=GetWindowLongPtr(w,GWL_EXSTYLE),style=GetWindowLongPtr(w,GWL_STYLE);
    if((style&WS_CHILD) || (ex&(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE)) || (GetWindow(w,GW_OWNER) && !(ex&WS_EX_APPWINDOW)))return TRUE;
    wchar_t cls[128]{},title[1024]{};GetClassName(w,cls,128);
    if(!wcscmp(cls,L"Progman") || !wcscmp(cls,L"WorkerW") || !wcscmp(cls,L"Shell_TrayWnd") || !wcsncmp(cls,L"LeanBar.",8) || !wcsncmp(cls,L"LeanApps",8))return TRUE;
    DWORD cloaked=0;DwmGetWindowAttribute(w,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));
    if(cloaked || !GetWindowText(w,title,1024))return TRUE;
    DWORD pid=0;GetWindowThreadProcessId(w,&pid);
    reinterpret_cast<std::vector<Window>*>(data)->push_back({w,pid,0,title,IsHungAppWindow(w)!=FALSE});return TRUE;
}
// An app is an executable path, not a filename. Only windowless helpers inside
// its installation directory can inherit a parent's group. A separately opened
// app, shell, or child from another directory stays independent.
static std::vector<Group> GroupProcesses(const std::vector<Process>& processes,const std::vector<Window>& windows){
    std::map<DWORD,const Process*> byPid;std::set<std::wstring> windowPaths;
    for(auto& p:processes)byPid[p.pid]=&p;
    for(auto& w:windows){auto p=byPid.find(w.pid);if(p!=byPid.end())windowPaths.insert(LowerName(p->second->path));}
    std::map<std::wstring,Group> grouped;std::map<DWORD,std::wstring> keys;
    for(auto& p:processes){
        const Process* root=&p;std::set<DWORD> visited{p.pid};
        if(!windowPaths.count(LowerName(p.path)) && !p.protectedProcess){
            for(unsigned depth=0;depth<64;++depth){
                auto parent=byPid.find(root->parent);
                if(parent==byPid.end() || !visited.insert(parent->first).second)break;
                auto next=parent->second;
                if(!next->created || next->created>root->created || next->protectedProcess || !Under(root->path,Directory(next->path)))break;
                root=next;if(windowPaths.count(LowerName(root->path)))break;
            }
        }
        auto key=LowerName(root->path);keys[p.pid]=key;
        auto& g=grouped[key];g.key=key;g.path=root->path;g.name=WindowAppName(root->path);
        // File managers are separate apps here; each retains its own process group.
        if(g.name==L"File explorers")g.name=LowerName(root->path).find(L"explorer++.exe")!=std::wstring::npos?L"Explorer++":L"File Explorer";
        g.processes.push_back(p);g.ram+=p.ram;g.cpu+=p.cpu;g.completeRam=g.completeRam && p.ramKnown;g.completeCpu=g.completeCpu && p.cpuKnown;
    }
    for(auto w:windows){auto key=keys.find(w.pid);if(key==keys.end())continue;w.created=byPid[w.pid]->created;grouped[key->second].windows.push_back(std::move(w));}
    std::vector<Group> result;for(auto& pair:grouped)result.push_back(std::move(pair.second));return result;
}
struct Sampler {
    struct Previous {ULONGLONG created,ticks,at;};
    std::map<DWORD,Previous> previous;std::map<std::wstring,HICON> icons;
    std::vector<BYTE> owner=Owner(GetCurrentProcess());DWORD session=0;std::wstring windowsDirectory;
    Sampler(){ProcessIdToSessionId(GetCurrentProcessId(),&session);wchar_t path[MAX_PATH]{};GetWindowsDirectory(path,MAX_PATH);windowsDirectory=LowerName(std::wstring(path)+L"\\");}
    ~Sampler(){for(auto& item:icons)DestroyIcon(item.second);}
    Snapshot Read(){
        auto start=GetTickCount64();Snapshot result;std::vector<Process> processes;std::map<DWORD,Previous> next;
        std::vector<Window> windows;EnumWindows(ReadWindow,reinterpret_cast<LPARAM>(&windows));
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
        if(snapshot==INVALID_HANDLE_VALUE){result.error=L"Windows could not list processes. Refresh to retry.";return result;}
        PROCESSENTRY32 entry{sizeof(entry)};DWORD cores=std::max<DWORD>(1,GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
        if(Process32First(snapshot,&entry))do{
            DWORD currentSession=0;if(!entry.th32ProcessID || entry.th32ProcessID==GetCurrentProcessId() || !ProcessIdToSessionId(entry.th32ProcessID,&currentSession) || currentSession!=session)continue;
            HANDLE handle=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID);if(!handle)continue;
            wchar_t path[32768]{};DWORD length=32768;Process p;p.pid=entry.th32ProcessID;p.parent=entry.th32ParentProcessID;
            if(!QueryFullProcessImageName(handle,0,path,&length) || !SameOwner(handle,owner)){CloseHandle(handle);continue;}p.path=path;
            FILETIME created{},exit{},kernel{},user{};BOOL critical=TRUE;
            p.protectedProcess=ShellProcess(p.path) || Under(p.path,windowsDirectory) || !IsProcessCritical(handle,&critical) || critical;
            if(GetProcessTimes(handle,&created,&exit,&kernel,&user)){
                p.created=Number(created);p.ticks=Number(kernel)+Number(user);auto now=GetTickCount64();auto old=previous.find(p.pid);
                if(old!=previous.end() && old->second.created==p.created && now>old->second.at && p.ticks>=old->second.ticks){
                    p.cpu=std::clamp((p.ticks-old->second.ticks)*100.0/((now-old->second.at)*10000.0*cores),0.0,100.0);p.cpuKnown=true;
                }next[p.pid]={p.created,p.ticks,now};
            }
            PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
            p.ramKnown=GetProcessMemoryInfo(handle,reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))!=FALSE;p.ram=memory.WorkingSetSize;
            CloseHandle(handle);processes.push_back(std::move(p));
        }while(Process32Next(snapshot,&entry));CloseHandle(snapshot);previous=std::move(next);
        result.groups=GroupProcesses(processes,windows);
        // Services and Windows background infrastructure belong in the full Task Manager.
        result.groups.erase(std::remove_if(result.groups.begin(),result.groups.end(),[&](auto& g){return g.windows.empty() && Under(g.path,windowsDirectory);}),result.groups.end());
        for(auto& g:result.groups){
            auto icon=icons.find(g.key);
            if(icon==icons.end() && !g.windows.empty()){
                DWORD_PTR value=0;SendMessageTimeout(g.windows[0].hwnd,WM_GETICON,ICON_SMALL2,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,20,&value);
                HICON source=reinterpret_cast<HICON>(value);if(!source)source=reinterpret_cast<HICON>(GetClassLongPtr(g.windows[0].hwnd,GCLP_HICON));
                if(source)icon=icons.emplace(g.key,CopyIcon(source)).first;
            }if(icon!=icons.end())g.icon=icon->second;
        }
        result.elapsedMs=GetTickCount64()-start;return result;
    }
};
}
