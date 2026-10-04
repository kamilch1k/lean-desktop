// On-demand app groups, resource totals, and window actions. No service or driver.
#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <thread>
#include <memory>
#include <fstream>
#include "AppModel.h"

static HWND mainWindow,heading,summary,searchLabel,searchBox,backgroundBox,refreshButton,appList,detailTitle,detailTabs,detailList,statusLabel;
static HWND switchButton,closeWindowButton,closeAppButton,endAppButton,taskManagerButton;
static HINSTANCE instance;static HFONT font,titleFont;static HBRUSH background;static UINT dpi=96;
static HIMAGELIST images;static std::map<std::wstring,int> imageIndices;
static Apps::Sampler sampler;static Apps::Snapshot snapshot;static std::thread worker;
static std::vector<size_t> rows;static std::wstring selectedKey;static HWND selectedWindow;
static bool loading=false,updating=false,modal=false,demo=false,paused=false;
static int sortColumn=0;static bool descending=false;static ULONGLONG actionUntil=0;
static int S(int n){return MulDiv(n,dpi,96);}
static std::wstring Text(HWND w){int n=GetWindowTextLength(w);std::wstring value(n+1,L'\0');GetWindowText(w,&value[0],n+1);value.resize(n);return value;}
static std::wstring Decimal(double value){wchar_t out[64];swprintf_s(out,L"%.1f",value);return out;}
static std::wstring Memory(ULONGLONG bytes,bool complete){return (complete?L"":L"~")+Decimal(bytes/1048576.0);}
static std::wstring Cpu(double cpu,bool complete){return complete?Decimal(cpu)+L"%":L"...";}
static void Status(const std::wstring& message){SetWindowText(statusLabel,message.c_str());actionUntil=GetTickCount64()+10000;}
static Apps::Group* Selected(){for(auto& g:snapshot.groups)if(g.key==selectedKey)return &g;return nullptr;}
static const Apps::Process* WindowProcess(const Apps::Group& group,const Apps::Window& window){for(auto& p:group.processes)if(p.pid==window.pid && p.created==window.created)return &p;return nullptr;}
static void Put(HWND list,int row,int column,const std::wstring& value){ListView_SetItemText(list,row,column,const_cast<wchar_t*>(value.c_str()));}
static void Row(HWND list,int row,const std::wstring& text,int icon=-1){LVITEM item{};item.mask=LVIF_TEXT|(icon>=0?LVIF_IMAGE:0);item.iItem=row;item.pszText=const_cast<wchar_t*>(text.c_str());item.iImage=icon;ListView_InsertItem(list,&item);}
static void Columns(HWND list,const std::vector<std::pair<std::wstring,int>>& columns){
    while(ListView_DeleteColumn(list,0)){}int i=0;
    for(auto& c:columns){LVCOLUMN col{};col.mask=LVCF_TEXT|LVCF_WIDTH;col.pszText=const_cast<wchar_t*>(c.first.c_str());col.cx=S(c.second);ListView_InsertColumn(list,i++,&col);}
}
static void Details(){
    static std::wstring displayedKey;static int displayedTab=-1;
    auto group=Selected();int tab=TabCtrl_GetCurSel(detailTabs);bool windows=tab==0;
    bool preserve=displayedKey==selectedKey && displayedTab==tab;int top=preserve?ListView_GetTopIndex(detailList):0;DWORD selectedPid=0;
    int selected=ListView_GetNextItem(detailList,-1,LVNI_SELECTED);
    if(preserve && !windows && selected>=0){wchar_t pid[32]{};ListView_GetItemText(detailList,selected,1,pid,32);selectedPid=wcstoul(pid,nullptr,10);}
    displayedKey=selectedKey;displayedTab=tab;updating=true;SetWindowRedraw(detailList,FALSE);ListView_DeleteAllItems(detailList);
    if(windows)Columns(detailList,{{L"Window / document",570},{L"PID",80},{L"State",150}});
    else Columns(detailList,{{L"Process",200},{L"PID",75},{L"RAM MiB",95},{L"CPU",75},{L"Executable path",500}});
    bool canClose=false,canEnd=false,canWindow=false;
    if(group){
        SetWindowText(detailTitle,(group->name+L"  —  "+group->path).c_str());
        if(windows){
            for(int i=0;i<static_cast<int>(group->windows.size());++i){auto& w=group->windows[i];Row(detailList,i,w.title);Put(detailList,i,1,std::to_wstring(w.pid));Put(detailList,i,2,w.hung?L"Not responding":L"Open");
                if(w.hwnd==selectedWindow){ListView_SetItemState(detailList,i,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);auto p=WindowProcess(*group,w);canWindow=p && !Apps::ShellProcess(p->path);}
            }
        }else{
            for(int i=0;i<static_cast<int>(group->processes.size());++i){auto& p=group->processes[i];Row(detailList,i,p.path.substr(p.path.find_last_of(L"\\/")+1));Put(detailList,i,1,std::to_wstring(p.pid));Put(detailList,i,2,Memory(p.ram,p.ramKnown));Put(detailList,i,3,Cpu(p.cpu,p.cpuKnown));Put(detailList,i,4,p.path);if(p.pid==selectedPid)ListView_SetItemState(detailList,i,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);}
        }
        canClose=!group->windows.empty() && !Apps::ShellProcess(group->path);
        canEnd=std::any_of(group->processes.begin(),group->processes.end(),[](auto& p){return !p.protectedProcess;});
    }else SetWindowText(detailTitle,L"Select an app to see its windows and processes");
    EnableWindow(switchButton,windows && canWindow);EnableWindow(closeWindowButton,windows && canWindow);EnableWindow(closeAppButton,canClose);EnableWindow(endAppButton,canEnd);
    if(top>0){RECT row{};if(ListView_GetItemRect(detailList,0,&row,LVIR_BOUNDS))ListView_Scroll(detailList,0,top*(row.bottom-row.top));}
    SetWindowRedraw(detailList,TRUE);InvalidateRect(detailList,nullptr,TRUE);updating=false;
}
static bool Matches(const Apps::Group& g,const std::wstring& query){
    if(query.empty() || LowerName(g.name+L" "+g.path).find(query)!=std::wstring::npos)return true;
    for(auto& w:g.windows)if(LowerName(w.title).find(query)!=std::wstring::npos)return true;
    for(auto& p:g.processes)if(std::to_wstring(p.pid).find(query)!=std::wstring::npos || LowerName(p.path).find(query)!=std::wstring::npos)return true;
    return false;
}
static void Fill(){
    std::wstring query=LowerName(Text(searchBox));bool includeBackground=Button_GetCheck(backgroundBox)==BST_CHECKED;
    rows.clear();for(size_t i=0;i<snapshot.groups.size();++i){auto& g=snapshot.groups[i];if((includeBackground || !g.windows.empty()) && Matches(g,query))rows.push_back(i);}
    std::sort(rows.begin(),rows.end(),[](size_t a,size_t b){auto& x=snapshot.groups[a];auto& y=snapshot.groups[b];int comparison=0;
        if(sortColumn==1)comparison=x.ram<y.ram?-1:x.ram>y.ram?1:0;
        else if(sortColumn==2)comparison=x.cpu<y.cpu?-1:x.cpu>y.cpu?1:0;
        else if(sortColumn==3)comparison=x.windows.size()<y.windows.size()?-1:x.windows.size()>y.windows.size()?1:0;
        else if(sortColumn==4)comparison=x.processes.size()<y.processes.size()?-1:x.processes.size()>y.processes.size()?1:0;
        if(!comparison)comparison=LowerName(x.name+x.key).compare(LowerName(y.name+y.key));return descending?comparison>0:comparison<0;
    });
    int top=ListView_GetTopIndex(appList);updating=true;SetWindowRedraw(appList,FALSE);ListView_DeleteAllItems(appList);bool found=false;size_t processCount=0;
    for(int i=0;i<static_cast<int>(rows.size());++i){auto& g=snapshot.groups[rows[i]];int icon=0;
        if(g.icon){auto cached=imageIndices.find(g.key);if(cached==imageIndices.end())cached=imageIndices.emplace(g.key,ImageList_AddIcon(images,g.icon)).first;icon=cached->second;}
        Row(appList,i,g.name,icon);Put(appList,i,1,Memory(g.ram,g.completeRam));Put(appList,i,2,Cpu(g.cpu,g.completeCpu));Put(appList,i,3,std::to_wstring(g.windows.size()));Put(appList,i,4,std::to_wstring(g.processes.size()));
        bool hung=std::any_of(g.windows.begin(),g.windows.end(),[](auto& w){return w.hung;});
        Put(appList,i,5,hung?L"Not responding":g.windows.empty()?L"Background":L"Running");processCount+=g.processes.size();
        if(g.key==selectedKey){found=true;ListView_SetItemState(appList,i,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);}
    }
    if(!found){selectedKey.clear();selectedWindow=nullptr;}
    if(top>0 && !rows.empty()){RECT row{};if(ListView_GetItemRect(appList,0,&row,LVIR_BOUNDS))ListView_Scroll(appList,0,top*(row.bottom-row.top));}
    SetWindowRedraw(appList,TRUE);InvalidateRect(appList,nullptr,TRUE);updating=false;Details();
    SetWindowText(summary,(std::to_wstring(rows.size())+L" apps  ·  "+std::to_wstring(processCount)+L" processes  ·  "+(paused?L"Paused":L"Updates every 2 seconds while visible")).c_str());
    if(GetTickCount64()>actionUntil)SetWindowText(statusLabel,snapshot.error.empty()?L"RAM is the sum of working sets (shared pages may count twice). CPU is a share of the whole PC.":snapshot.error.c_str());
}
static void Sample(){
    if(demo || loading || paused || modal || IsIconic(mainWindow))return;
    if(worker.joinable())worker.join();loading=true;HWND target=mainWindow;
    worker=std::thread([target]{auto result=std::make_unique<Apps::Snapshot>();try{*result=sampler.Read();}catch(...){result->error=L"App information could not refresh.";}
        if(PostMessage(target,WM_APP+1,0,reinterpret_cast<LPARAM>(result.get())))result.release();});
}
static void AppAction(int action){
    auto selected=Selected();if(!selected)return;auto group=*selected;
    if(demo){Status(L"Preview only. No real app was closed.");return;}
    if(action==10 || action==11){
        for(auto& w:group.windows)if(w.hwnd==selectedWindow){auto p=WindowProcess(group,w);if(!p)return;
            if(action==10){HANDLE handle=Apps::Verified(*p,false,sampler.owner);DWORD pid=0;GetWindowThreadProcessId(w.hwnd,&pid);if(handle && pid==p->pid){if(IsIconic(w.hwnd))ShowWindowAsync(w.hwnd,SW_RESTORE);SetForegroundWindow(w.hwnd);}if(handle)CloseHandle(handle);}
            else Status(Apps::CloseWindow(w,*p,sampler.owner)?L"Close requested. The app can prompt to save your work.":L"This window exited or Windows did not allow it to close.");
            return;
        }return;
    }
    if(action==12){int requested=0;for(auto& w:group.windows){auto p=WindowProcess(group,w);if(p && Apps::CloseWindow(w,*p,sampler.owner))++requested;}
        Status(L"Close requested for "+std::to_wstring(requested)+L" window(s). Save prompts may remain; some apps stay in the background.");return;
    }
    if(action==13){
        int eligible=0;for(auto& p:group.processes)if(!p.protectedProcess)++eligible;if(!eligible)return;
        std::wstring prompt=L"Force end "+group.name+L"?\n\nThis stops up to "+std::to_wstring(eligible)+L" processes in the selected app group, including its other open windows. Unsaved work can be lost.\n\nTry Close app first if it is responding.";
        modal=true;int choice=MessageBox(mainWindow,prompt.c_str(),L"Force end app",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2);modal=false;
        if(choice!=IDYES)return;
        int stopped=0;for(auto& p:group.processes)if(!p.protectedProcess && Apps::EndProcess(p,sampler.owner))++stopped;
        Status(L"Stopped "+std::to_wstring(stopped)+L" of "+std::to_wstring(eligible)+L" selected processes. Exited or inaccessible processes were skipped.");Sample();
    }
}
static HWND Add(const wchar_t* cls,const wchar_t* text,DWORD style,int id=0){
    HWND control=CreateWindowEx(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,mainWindow,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);SendMessage(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return control;
}
static void Layout(){
    RECT r{};GetClientRect(mainWindow,&r);int width=r.right,height=r.bottom,pad=S(16),listTop=S(116),listHeight=std::max(S(145),(height-S(200))/2);
    MoveWindow(heading,pad,S(12),width-S(200),S(32),TRUE);MoveWindow(summary,pad,S(48),width-2*pad,S(23),TRUE);
    MoveWindow(searchLabel,pad,S(84),S(54),S(24),TRUE);MoveWindow(searchBox,pad+S(58),S(80),std::max(S(155),width-S(528)),S(27),TRUE);MoveWindow(backgroundBox,width-S(440),S(80),S(225),S(27),TRUE);MoveWindow(refreshButton,width-S(204),S(79),S(88),S(29),TRUE);
    MoveWindow(GetDlgItem(mainWindow,5),width-S(108),S(79),S(92),S(29),TRUE);
    MoveWindow(appList,pad,listTop,width-2*pad,listHeight,TRUE);int bottom=listTop+listHeight;
    MoveWindow(detailTitle,pad,bottom+S(10),width-2*pad,S(25),TRUE);MoveWindow(detailTabs,pad,bottom+S(40),width-2*pad,S(28),TRUE);
    MoveWindow(detailList,pad,bottom+S(72),width-2*pad,std::max(S(65),height-bottom-S(154)),TRUE);
    int y=height-S(68);MoveWindow(switchButton,pad,y,S(108),S(30),TRUE);MoveWindow(closeWindowButton,pad+S(118),y,S(126),S(30),TRUE);MoveWindow(closeAppButton,pad+S(254),y,S(114),S(30),TRUE);MoveWindow(endAppButton,pad+S(378),y,S(135),S(30),TRUE);MoveWindow(taskManagerButton,width-S(188),y,S(172),S(30),TRUE);
    MoveWindow(statusLabel,pad,height-S(30),width-2*pad,S(24),TRUE);
    ListView_SetColumnWidth(appList,0,std::max(S(180),width-S(570)));ListView_SetColumnWidth(appList,5,S(145));
}
static LRESULT CALLBACK Procedure(HWND w,UINT message,WPARAM wp,LPARAM lp){
    switch(message){
    case WM_CREATE:mainWindow=w;return 0;
    case WM_SIZE:if(appList){Layout();if(wp!=SIZE_MINIMIZED)Sample();}return 0;
    case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={S(820),S(630)};return 0;
    case WM_TIMER:if(wp==1)Sample();if(wp==2){KillTimer(w,2);Fill();}return 0;
    case WM_APP+1:{std::unique_ptr<Apps::Snapshot> result(reinterpret_cast<Apps::Snapshot*>(lp));loading=false;if(!modal){snapshot=std::move(*result);Fill();}return 0;}
    case WM_COMMAND:{int id=LOWORD(wp);
        if(id==2 && HIWORD(wp)==EN_CHANGE)SetTimer(w,2,120,nullptr);
        else if(id==3)Fill();else if(id==4){actionUntil=0;Sample();}
        else if(id==5){paused=!paused;SetWindowText(GetDlgItem(w,5),paused?L"Resume":L"Pause");Fill();if(!paused)Sample();}
        else if(id>=10 && id<=13)AppAction(id);
        else if(id==14)ShellExecute(w,L"open",L"taskmgr.exe",nullptr,nullptr,SW_SHOWNORMAL);
        return 0;}
    case WM_NOTIFY:{auto n=reinterpret_cast<NMHDR*>(lp);if(updating)return 0;
        if(n->hwndFrom==appList && n->code==LVN_COLUMNCLICK){int col=reinterpret_cast<NMLISTVIEW*>(lp)->iSubItem;if(col<=4){descending=sortColumn==col?!descending:col!=0;sortColumn=col;Fill();}}
        if(n->hwndFrom==appList && n->code==LVN_ITEMCHANGED){int i=ListView_GetNextItem(appList,-1,LVNI_SELECTED);auto key=i>=0 && i<static_cast<int>(rows.size())?snapshot.groups[rows[i]].key:L"";if(key!=selectedKey){selectedKey=key;selectedWindow=nullptr;Details();}}
        if(n->hwndFrom==detailTabs && n->code==TCN_SELCHANGE)Details();
        if(n->hwndFrom==detailList && n->code==LVN_ITEMCHANGED && TabCtrl_GetCurSel(detailTabs)==0){auto g=Selected();int i=ListView_GetNextItem(detailList,-1,LVNI_SELECTED);bool valid=g && i>=0 && i<static_cast<int>(g->windows.size());selectedWindow=valid?g->windows[i].hwnd:nullptr;EnableWindow(switchButton,valid);EnableWindow(closeWindowButton,valid && !Apps::ShellProcess(g->path));}
        if(n->hwndFrom==detailList && n->code==NM_DBLCLK && TabCtrl_GetCurSel(detailTabs)==0)AppAction(10);
        return 0;}
    case WM_CTLCOLORSTATIC:{auto dc=reinterpret_cast<HDC>(wp);SetBkColor(dc,RGB(238,240,242));SetTextColor(dc,RGB(25,31,38));return reinterpret_cast<LRESULT>(background);}
    case WM_CLOSE:DestroyWindow(w);return 0;
    case WM_DESTROY:KillTimer(w,1);KillTimer(w,2);PostQuitMessage(0);return 0;
    }return DefWindowProc(w,message,wp,lp);
}
static void Demo(){
    auto add=[](const wchar_t* name,const wchar_t* path,ULONGLONG mb,double cpu,std::vector<std::wstring> titles,int count){
        Apps::Group g;g.name=name;g.path=path;g.key=LowerName(path);g.ram=mb*1048576;g.cpu=cpu;g.icon=LoadIcon(nullptr,IDI_APPLICATION);
        for(int i=0;i<count;++i){Apps::Process p;p.pid=4200+static_cast<DWORD>(snapshot.groups.size()*100+i);p.path=path;p.created=1;p.protectedProcess=false;p.ram=g.ram/count;p.ramKnown=p.cpuKnown=true;p.cpu=cpu/count;g.processes.push_back(p);}
        for(size_t i=0;i<titles.size();++i)g.windows.push_back({reinterpret_cast<HWND>(100+snapshot.groups.size()*10+i),g.processes[0].pid,1,titles[i],false});snapshot.groups.push_back(g);
    };
    add(L"Roblox Studio",L"C:\\Apps\\Roblox\\RobloxStudioBeta.exe",2140,6.3,{L"Kalashok guns — Roblox Studio",L"Forest arena — Roblox Studio",L"Test place — Roblox Studio"},4);
    add(L"Google Chrome",L"C:\\Apps\\Chrome\\chrome.exe",930,1.2,{L"Documentation — Google Chrome",L"Music — Google Chrome"},14);
    add(L"Discord",L"C:\\Apps\\Discord\\Discord.exe",380,.4,{L"Friends — Discord"},5);
    add(L"Blender",L"C:\\Apps\\Blender\\blender.exe",1480,12.8,{L"Workshop.blend — Blender"},1);
    add(L"File Explorer",L"C:\\Windows\\explorer.exe",148,0,{L"Downloads",L"Projects"},1);
    selectedKey=snapshot.groups[0].key;selectedWindow=snapshot.groups[0].windows[0].hwnd;
}
static bool Render(const std::wstring& path){
    RECT r{};GetWindowRect(mainWindow,&r);HDC dc=GetDC(mainWindow),mem=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,r.right-r.left,r.bottom-r.top);auto old=SelectObject(mem,bitmap);
    bool ok=PrintWindow(mainWindow,mem,0)!=FALSE;SelectObject(mem,old);DeleteDC(mem);ReleaseDC(mainWindow,dc);
    ULONG_PTR token{};Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&token,&input,nullptr);
    {Gdiplus::Bitmap picture(bitmap,nullptr);CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};ok=ok && picture.Save(path.c_str(),&png,nullptr)==Gdiplus::Ok;}
    Gdiplus::GdiplusShutdown(token);DeleteObject(bitmap);return ok;
}
static bool TestPreview(){
    auto key=selectedKey;auto window=selectedWindow;bool ok=IsWindowEnabled(closeWindowButton)!=FALSE;
    sortColumn=1;descending=true;Fill();ok=ok && !rows.empty() && snapshot.groups[rows[0]].key==key && selectedKey==key && selectedWindow==window;
    SetWindowText(searchBox,L"Kalashok");Fill();ok=ok && rows.size()==1 && selectedKey==key;
    SetWindowText(searchBox,L"");sortColumn=0;descending=false;Fill();
    TabCtrl_SetCurSel(detailTabs,1);Details();ok=ok && ListView_GetItemCount(detailList)==4 && !IsWindowEnabled(closeWindowButton);
    ListView_SetItemState(detailList,2,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);Fill();ok=ok && ListView_GetNextItem(detailList,-1,LVNI_SELECTED)==2;
    TabCtrl_SetCurSel(detailTabs,0);Details();ok=ok && ListView_GetItemCount(detailList)==3 && IsWindowEnabled(closeWindowButton);
    auto groups=snapshot.groups;snapshot.groups.erase(snapshot.groups.begin());Fill();ok=ok && selectedKey.empty() && !IsWindowEnabled(closeAppButton) && !IsWindowEnabled(endAppButton);
    snapshot.groups=std::move(groups);selectedKey=key;selectedWindow=window;Fill();return ok;
}
int WINAPI wWinMain(HINSTANCE h,HINSTANCE,PWSTR,int){
    instance=h;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argc=0;auto argv=CommandLineToArgvW(GetCommandLine(),&argc);std::wstring render;
    for(int i=1;i<argc;++i){if(!wcscmp(argv[i],L"--demo"))demo=true;if(!wcscmp(argv[i],L"--render") && i+1<argc)render=argv[++i];}LocalFree(argv);
    HWND existing=FindWindow(L"LeanApps.Window",nullptr);if(!demo && existing){ShowWindowAsync(existing,SW_RESTORE);SetForegroundWindow(existing);return 0;}
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_LISTVIEW_CLASSES|ICC_TAB_CLASSES};InitCommonControlsEx(&common);
    background=CreateSolidBrush(RGB(238,240,242));WNDCLASS cls{};cls.hInstance=h;cls.lpfnWndProc=Procedure;cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hIcon=LoadIcon(nullptr,IDI_APPLICATION);cls.hbrBackground=background;cls.lpszClassName=demo?L"LeanApps.Preview":L"LeanApps.Window";RegisterClass(&cls);
    mainWindow=CreateWindowEx(WS_EX_CONTROLPARENT,cls.lpszClassName,L"Lean Apps",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,990,740,nullptr,nullptr,h,nullptr);if(!mainWindow)return 1;dpi=GetDpiForWindow(mainWindow);
    font=CreateFont(-S(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    titleFont=CreateFont(-S(23),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    heading=Add(L"STATIC",L"Apps",0);SendMessage(heading,WM_SETFONT,reinterpret_cast<WPARAM>(titleFont),TRUE);summary=Add(L"STATIC",L"Reading your apps...",SS_ENDELLIPSIS);
    searchLabel=Add(L"STATIC",L"Search",0);searchBox=Add(L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,2);SendMessage(searchBox,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Apps, window titles, or PID"));
    backgroundBox=Add(L"BUTTON",L"Include background apps",WS_TABSTOP|BS_AUTOCHECKBOX,3);refreshButton=Add(L"BUTTON",L"Refresh",WS_TABSTOP,4);Add(L"BUTTON",L"Pause",WS_TABSTOP,5);
    appList=Add(WC_LISTVIEW,L"",WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,6);ListView_SetExtendedListViewStyle(appList,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_INFOTIP);
    images=ImageList_Create(S(28),S(28),ILC_COLOR32|ILC_MASK,16,8);ImageList_AddIcon(images,LoadIcon(nullptr,IDI_APPLICATION));ListView_SetImageList(appList,images,LVSIL_SMALL);
    Columns(appList,{{L"Application",350},{L"RAM MiB",100},{L"CPU",75},{L"Windows",75},{L"Processes",95},{L"Status",145}});
    detailTitle=Add(L"STATIC",L"Select an app to see its windows and processes",SS_ENDELLIPSIS|SS_NOPREFIX);
    detailTabs=Add(WC_TABCONTROL,L"",WS_TABSTOP|TCS_BUTTONS,7);int i=0;for(auto label:{L"Windows",L"Processes"}){TCITEM tab{};tab.mask=TCIF_TEXT;tab.pszText=const_cast<wchar_t*>(label);TabCtrl_InsertItem(detailTabs,i++,&tab);}
    detailList=Add(WC_LISTVIEW,L"",WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,8);ListView_SetExtendedListViewStyle(detailList,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_INFOTIP);
    switchButton=Add(L"BUTTON",L"Switch to",WS_TABSTOP,10);closeWindowButton=Add(L"BUTTON",L"Close window",WS_TABSTOP,11);closeAppButton=Add(L"BUTTON",L"Close app",WS_TABSTOP,12);endAppButton=Add(L"BUTTON",L"Force end app",WS_TABSTOP,13);taskManagerButton=Add(L"BUTTON",L"Windows Task Manager",WS_TABSTOP,14);statusLabel=Add(L"STATIC",L"",SS_ENDELLIPSIS);
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfo(MonitorFromWindow(mainWindow,MONITOR_DEFAULTTONEAREST),&monitor);int width=std::min<int>(S(990),monitor.rcWork.right-monitor.rcWork.left),height=std::min<int>(S(740),monitor.rcWork.bottom-monitor.rcWork.top);
    SetWindowPos(mainWindow,nullptr,monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-width)/2,monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-height)/2,width,height,SWP_NOZORDER);Layout();
    if(demo)Demo();Fill();ShowWindow(mainWindow,render.empty()?SW_SHOW:SW_SHOWNOACTIVATE);UpdateWindow(mainWindow);SetTimer(mainWindow,1,2000,nullptr);Sample();
    if(!render.empty()){bool ok=demo && TestPreview() && Render(render);DestroyWindow(mainWindow);if(worker.joinable())worker.join();return ok?0:1;}
    MSG msg{};while(GetMessage(&msg,nullptr,0,0)>0){if(!IsDialogMessage(mainWindow,&msg)){TranslateMessage(&msg);DispatchMessage(&msg);}}
    if(worker.joinable())worker.join();
    while(PeekMessage(&msg,nullptr,WM_APP+1,WM_APP+1,PM_REMOVE))delete reinterpret_cast<Apps::Snapshot*>(msg.lParam);
    ImageList_Destroy(images);DeleteObject(font);DeleteObject(titleFont);DeleteObject(background);return 0;
}
