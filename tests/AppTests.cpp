#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <iostream>
#include "../AppModel.h"
static LRESULT CALLBACK FixtureWindow(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_CLOSE || msg==WM_TIMER){DestroyWindow(w);return 0;}
    if(msg==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProc(w,msg,wp,lp);
}
static int Fixture(const wchar_t* name){
    HANDLE ready=OpenEvent(EVENT_MODIFY_STATE,FALSE,name);if(!ready)return 1;
    WNDCLASS cls{};cls.hInstance=GetModuleHandle(nullptr);cls.lpfnWndProc=FixtureWindow;cls.lpszClassName=L"LeanAppTest.Fixture";RegisterClass(&cls);
    HWND w=CreateWindow(cls.lpszClassName,L"Test-owned document",WS_OVERLAPPEDWINDOW,60,60,320,160,nullptr,nullptr,cls.hInstance,nullptr);
    ShowWindow(w,SW_SHOWNOACTIVATE);SetTimer(w,1,30000,nullptr);SetEvent(ready);CloseHandle(ready);
    MSG m{};while(GetMessage(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessage(&m);}return 0;
}
int wmain(int argc,wchar_t** argv){
    if(argc==3 && !wcscmp(argv[1],L"--fixture"))return Fixture(argv[2]);
    int failures=0;auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<"\n";if(!ok)++failures;};
    auto make=[](DWORD pid,DWORD parent,ULONGLONG created,const wchar_t* path){Apps::Process p;p.pid=pid;p.parent=parent;p.created=created;p.path=path;p.ram=104857600;p.ramKnown=p.cpuKnown=true;p.cpu=2;p.protectedProcess=false;return p;};
    std::vector<Apps::Process> input={make(1,0,10,L"C:\\Apps\\Studio\\RobloxStudioBeta.exe"),make(2,0,11,L"C:\\Apps\\Studio\\RobloxStudioBeta.exe"),make(3,1,12,L"C:\\Apps\\Studio\\helper.exe"),make(4,0,13,L"D:\\Other\\RobloxStudioBeta.exe"),make(5,1,14,L"C:\\Python\\python.exe"),make(6,1,15,L"C:\\Apps\\Studio\\Editor.exe"),make(7,0,100,L"C:\\Apps\\Old\\parent.exe"),make(8,7,20,L"C:\\Apps\\Old\\child.exe")};
    std::vector<Apps::Window> windows={{nullptr,1,10,L"Project A",false},{nullptr,2,11,L"Project B",false},{nullptr,6,15,L"Independent editor",false}};
    auto groups=Apps::GroupProcesses(input,windows);
    auto group=std::find_if(groups.begin(),groups.end(),[](auto& g){return g.path==L"C:\\Apps\\Studio\\RobloxStudioBeta.exe";});
    check(group!=groups.end() && group->processes.size()==3 && group->windows.size()==2,"same app instances and its windowless helper share one group");
    check(group!=groups.end() && group->ram==314572800 && group->cpu==6,"app resource totals include its grouped processes once");
    check(groups.size()==6,"other installations, independently opened apps, unrelated children, and reused parents stay separate");
    check(Apps::ShellProcess(L"C:\\Shell\\LeanBar.exe") && Apps::ShellProcess(L"C:\\Shell\\QuickSearch.Companion.exe"),"shell processes are protected");

    std::vector<PROCESS_INFORMATION> children;
    wchar_t self[32768]{};GetModuleFileName(nullptr,self,32768);
    for(int i=0;i<3;++i){
        std::wstring name=L"Local\\LeanAppsTest."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(i);
        HANDLE ready=CreateEvent(nullptr,TRUE,FALSE,name.c_str());STARTUPINFO start{sizeof(start)};PROCESS_INFORMATION child{};
        std::wstring command=L"\""+std::wstring(self)+L"\" --fixture "+name;
        bool made=CreateProcess(self,&command[0],nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&child)!=FALSE;
        check(made && WaitForSingleObject(ready,3000)==WAIT_OBJECT_0,"test-owned app process starts");CloseHandle(ready);if(made)children.push_back(child);
    }
    if(children.size()==3){
        Apps::Sampler sampler;auto first=sampler.Read();Sleep(100);auto sample=sampler.Read();
        std::vector<Apps::Process> processes;std::vector<Apps::Window> owned;
        for(auto& g:sample.groups){for(auto& p:g.processes)for(auto& c:children)if(p.pid==c.dwProcessId)processes.push_back(p);for(auto& w:g.windows)for(auto& c:children)if(w.pid==c.dwProcessId)owned.push_back(w);}
        check(processes.size()==3 && owned.size()==3,"live sampler finds all fixture app instances and document windows");
        check(std::all_of(processes.begin(),processes.end(),[](auto& p){return p.cpuKnown && p.ramKnown && p.ram>0 && p.cpu>=0 && p.cpu<=100;}),"CPU and memory samples are valid after two reads");
        if(processes.size()==3 && owned.size()==3){
            auto p=processes[0];auto window=std::find_if(owned.begin(),owned.end(),[&](auto& w){return w.pid==p.pid;});
            auto live=[&](DWORD pid){for(auto& c:children)if(c.dwProcessId==pid)return WaitForSingleObject(c.hProcess,0)==WAIT_TIMEOUT;return false;};
            auto stale=p;++stale.created;check(!Apps::EndProcess(stale,sampler.owner) && live(p.pid),"stale creation time cannot terminate a reused PID");
            stale=p;stale.path=L"C:\\wrong.exe";check(!Apps::EndProcess(stale,sampler.owner) && live(p.pid),"executable identity must still match before termination");
            stale=p;stale.protectedProcess=true;check(!Apps::EndProcess(stale,sampler.owner) && live(p.pid),"protected process cannot be force-ended");
            check(Apps::CloseWindow(*window,p,sampler.owner),"normal close posts to the selected fixture window");
            Sleep(150);check(!live(p.pid) && live(processes[1].pid) && live(processes[2].pid),"closing one document leaves other app instances running");
            check(Apps::EndProcess(processes[1],sampler.owner),"force end terminates the verified fixture process");
            Sleep(100);check(!live(processes[1].pid) && live(processes[2].pid),"force end affects only the selected process identity");
        }
        std::cout<<"Sample duration: "<<sample.elapsedMs<<" ms\n";
    }
    // Cleanup is limited to handles returned by the three fixture CreateProcess calls.
    for(auto& c:children){if(WaitForSingleObject(c.hProcess,0)==WAIT_TIMEOUT)TerminateProcess(c.hProcess,0);WaitForSingleObject(c.hProcess,2000);CloseHandle(c.hThread);CloseHandle(c.hProcess);}
    std::cout<<"Failures: "<<failures<<"\n";return failures?1:0;
}
