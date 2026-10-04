#pragma once
#include "WindowNames.h"

// One fullscreen GDI canvas with independent app panels. No capture or animation.
struct OverviewItem { HWND window{}; DWORD pid{}; std::wstring title,group; HICON icon{}; RECT card{}; };
struct OverviewGroup {
    std::wstring name; RECT bounds{},view{}; int first=0,count=0,columns=1,height=0,scroll=0,page=0;
};
static HWND overviewWindow{},overviewPrevious{};
static std::vector<OverviewItem> overviewItems;
static std::vector<OverviewGroup> overviewGroups;
static HFONT overviewFont{},overviewHeading{};
static int overviewSelected=0,overviewPage=0,overviewPages=1,overviewWheel=0,overviewDrag=-1,overviewDragOffset=0;
static UINT overviewDpi=96;
static bool overviewClosing=false;
static int O(int n){return MulDiv(n,overviewDpi,96);}
static RECT OverviewViewport(){
    RECT r{};GetClientRect(overviewWindow,&r);r.top=O(86);r.bottom=std::max(r.top,r.bottom-O(44));return r;
}
static void ReleaseOverviewItem(OverviewItem& item){if(item.icon)DestroyIcon(item.icon);item.icon=nullptr;}
static int OverviewLimit(const OverviewGroup& g){return std::max(0,g.height-static_cast<int>(g.view.bottom-g.view.top));}
static int SelectedOverviewGroup(){
    for(int i=0;i<static_cast<int>(overviewGroups.size());++i){
        const auto& g=overviewGroups[i];if(overviewSelected>=g.first && overviewSelected<g.first+g.count)return i;
    }return -1;
}
static RECT OverviewThumb(const OverviewGroup& g){
    int height=g.view.bottom-g.view.top,thumb=std::min(height,std::max(O(28),MulDiv(height,height,std::max(1,g.height))));
    int y=g.view.top+(OverviewLimit(g)?MulDiv(g.scroll,height-thumb,OverviewLimit(g)):0);
    return {g.bounds.right-O(10),y,g.bounds.right-O(4),y+thumb};
}
static void ScrollOverviewGroup(int index,int position){
    if(index<0 || index>=static_cast<int>(overviewGroups.size()))return;
    auto& g=overviewGroups[index];g.scroll=std::clamp(position,0,OverviewLimit(g));InvalidateRect(overviewWindow,&g.bounds,FALSE);
}
static void LayoutOverview(){
    if(!overviewWindow || overviewGroups.empty())return;
    RECT screen=OverviewViewport();const int gap=O(16),pad=O(24);
    int width=std::max(1,static_cast<int>(screen.right)-2*pad),height=std::max(1,static_cast<int>(screen.bottom-screen.top)-O(8));
    // Whole app panels fit on a board. Exceptionally many app types get another
    // board page instead of a scrollbar for the entire desktop.
    int cols=std::max(1,std::min(static_cast<int>(overviewGroups.size()),(width+gap)/(O(350)+gap)));
    int rows=std::max(1,std::min((static_cast<int>(overviewGroups.size())+cols-1)/cols,(height+gap)/(O(280)+gap)));
    int capacity=cols*rows;overviewPages=(static_cast<int>(overviewGroups.size())+capacity-1)/capacity;
    overviewPage=std::clamp(overviewPage,0,overviewPages-1);
    int panelWidth=(width-(cols-1)*gap)/cols,panelHeight=(height-(rows-1)*gap)/rows;
    for(int i=0;i<static_cast<int>(overviewGroups.size());++i){
        auto& g=overviewGroups[i];int slot=i%capacity,x=pad+(slot%cols)*(panelWidth+gap),y=screen.top+(slot/cols)*(panelHeight+gap);
        g.page=i/capacity;g.bounds={x,y,x+panelWidth,y+panelHeight};g.view={x+O(10),y+O(42),x+panelWidth-O(16),y+panelHeight-O(10)};
        int available=std::max(1,static_cast<int>(g.view.right-g.view.left)),cardGap=O(8),cardHeight=O(176);
        g.columns=std::max(1,(available+cardGap)/(O(178)+cardGap));
        int itemRows=(g.count+g.columns-1)/g.columns;
        int fitHeight=(g.view.bottom-g.view.top-(itemRows-1)*cardGap)/itemRows;
        if(fitHeight>=O(156))cardHeight=std::min(cardHeight,fitHeight); // Fit modest groups while keeping icons and text large.
        int cardWidth=(available-(g.columns-1)*cardGap)/g.columns;
        for(int j=0;j<g.count;++j){
            int left=g.view.left+(j%g.columns)*(cardWidth+cardGap),top=(j/g.columns)*(cardHeight+cardGap);
            overviewItems[g.first+j].card={left,top,left+cardWidth,top+cardHeight};
        }
        g.height=((g.count+g.columns-1)/g.columns)*(cardHeight+cardGap)-cardGap;
        g.scroll=std::clamp(g.scroll,0,OverviewLimit(g));
    }
    InvalidateRect(overviewWindow,nullptr,FALSE);
}
static void SelectOverview(int index){
    if(overviewItems.empty())return;
    overviewSelected=std::clamp(index,0,static_cast<int>(overviewItems.size())-1);
    int group=SelectedOverviewGroup();if(group<0)return;auto& g=overviewGroups[group];overviewPage=g.page;
    RECT r=overviewItems[overviewSelected].card;int scroll=g.scroll,height=g.view.bottom-g.view.top;
    if(r.top<scroll)scroll=r.top;else if(r.bottom>scroll+height)scroll=r.bottom-height;
    ScrollOverviewGroup(group,scroll);InvalidateRect(overviewWindow,nullptr,FALSE);
}
static void ChangeOverviewPage(int step){
    overviewPage=std::clamp(overviewPage+step,0,overviewPages-1);
    for(const auto& g:overviewGroups)if(g.page==overviewPage){overviewSelected=g.first;break;}
    SelectOverview(overviewSelected);
}
static void RefreshOverview(const std::vector<HWND>& live){
    if(!overviewWindow)return;
    if(overviewDrag>=0){overviewDrag=-1;ReleaseCapture();}
    HWND selected=overviewItems.empty()?overviewPrevious:overviewItems[overviewSelected].window;
    std::vector<OverviewItem> next;next.reserve(live.size());
    for(HWND w:live){
        if(!IsWindow(w) || w==overviewWindow)continue;
        DWORD pid=0;GetWindowThreadProcessId(w,&pid);
        auto old=std::find_if(overviewItems.begin(),overviewItems.end(),[=](const auto& item){return item.window==w && item.pid==pid;});
        OverviewItem item;
        if(old!=overviewItems.end()){item=std::move(*old);old->icon=nullptr;}
        else{
            item.window=w;item.pid=pid;item.icon=CopyWindowIcon(w,true);
            wchar_t cls[128]{};GetClassName(w,cls,128);item.group=WindowAppName(WindowProcessPath(pid),cls);
        }
        wchar_t title[1024]{};GetWindowText(w,title,1024);item.title=title;next.push_back(std::move(item));
    }
    for(auto& item:overviewItems)ReleaseOverviewItem(item);overviewItems=std::move(next);
    std::stable_sort(overviewItems.begin(),overviewItems.end(),[](const auto& a,const auto& b){
        int group=_wcsicmp(a.group.c_str(),b.group.c_str());return group?group<0:_wcsicmp(a.title.c_str(),b.title.c_str())<0;
    });
    auto previous=std::move(overviewGroups);overviewGroups.clear();overviewSelected=0;
    for(int first=0;first<static_cast<int>(overviewItems.size());){
        int end=first+1;while(end<static_cast<int>(overviewItems.size()) && overviewItems[end].group==overviewItems[first].group)++end;
        OverviewGroup group;group.name=overviewItems[first].group;group.first=first;group.count=end-first;
        for(const auto& old:previous)if(old.name==group.name)group.scroll=old.scroll;
        overviewGroups.push_back(std::move(group));first=end;
    }
    for(int i=0;i<static_cast<int>(overviewItems.size());++i)if(overviewItems[i].window==selected)overviewSelected=i;
    LayoutOverview();InvalidateRect(overviewWindow,nullptr,FALSE);
}
static void CloseOverview(bool restore,HWND target=nullptr){
    if(!overviewWindow || overviewClosing)return;
    overviewClosing=true;HWND previous=overviewPrevious;DestroyWindow(overviewWindow);overviewClosing=false;
    HWND next=target?target:(restore?previous:nullptr);
    if(next && IsWindow(next)){if(IsIconic(next))ShowWindowAsync(next,SW_RESTORE);SetForegroundWindow(next);}
}
static void CommitOverview(){if(!overviewItems.empty())CloseOverview(false,overviewItems[overviewSelected].window);}
static void PaintOverview(HDC dc){
    RECT r{};GetClientRect(overviewWindow,&r);
    SetDCBrushColor(dc,RGB(20,24,29));FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(238,243,248));SelectObject(dc,overviewHeading);
    RECT title{O(24),O(14),r.right-O(70),O(49)};DrawText(dc,L"Open windows",-1,&title,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
    SelectObject(dc,overviewFont);SetTextColor(dc,RGB(171,187,201));
    RECT hint{O(24),O(51),r.right-O(70),O(77)};DrawText(dc,L"Grouped by app  |  Click or Enter to switch  |  Esc to close",-1,&hint,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
    RECT close{r.right-O(62),O(17),r.right-O(20),O(59)};DrawText(dc,L"X",-1,&close,DT_SINGLELINE|DT_CENTER|DT_VCENTER);
    for(const auto& g:overviewGroups){
        if(g.page!=overviewPage)continue;
        SetDCBrushColor(dc,RGB(29,35,42));FillRect(dc,&g.bounds,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        RECT heading=g.bounds;heading.left+=O(12);heading.right-=O(12);heading.bottom=heading.top+O(38);SetTextColor(dc,RGB(160,218,206));
        std::wstring text=g.name+L"  ("+std::to_wstring(g.count)+L")";DrawText(dc,text.c_str(),-1,&heading,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
        int saved=SaveDC(dc);IntersectClipRect(dc,g.view.left,g.view.top,g.view.right,g.view.bottom);
        for(int i=g.first;i<g.first+g.count;++i){
            const auto& item=overviewItems[i];RECT card=item.card,clipped{};OffsetRect(&card,0,g.view.top-g.scroll);
            if(!IntersectRect(&clipped,&card,&g.view))continue;
            SetDCBrushColor(dc,i==overviewSelected?RGB(39,63,72):RGB(38,45,54));FillRect(dc,&card,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            if(i==overviewSelected){SetDCBrushColor(dc,RGB(101,220,193));FrameRect(dc,&card,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));}
            int center=(card.left+card.right)/2;RECT tile{center-O(40),card.top+O(10),center+O(40),card.top+O(90)};
            SetDCBrushColor(dc,RGB(78,89,103));FillRect(dc,&tile,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            if(item.icon)DrawIconEx(dc,tile.left+O(8),tile.top+O(8),item.icon,O(64),O(64),0,nullptr,DI_NORMAL);
            RECT label{card.left+O(9),card.top+O(102),card.right-O(9),card.bottom-O(7)};SetTextColor(dc,RGB(238,243,248));
            DrawText(dc,item.title.c_str(),-1,&label,DT_WORDBREAK|DT_CENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
        }
        RestoreDC(dc,saved);
        if(OverviewLimit(g)>0){RECT thumb=OverviewThumb(g);SetDCBrushColor(dc,RGB(113,139,150));FillRect(dc,&thumb,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));}
    }
    if(overviewItems.empty()){RECT view=OverviewViewport();SetTextColor(dc,RGB(190,201,212));DrawText(dc,L"No open application windows",-1,&view,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
    SetTextColor(dc,RGB(171,187,201));RECT footer{O(24),r.bottom-O(38),r.right-O(220),r.bottom-O(8)};
    std::wstring text=std::to_wstring(overviewItems.size())+L" windows  |  Arrows / Tab to select  |  Scroll inside a group";
    DrawText(dc,text.c_str(),-1,&footer,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    if(overviewPages>1){RECT pages{r.right-O(216),r.bottom-O(38),r.right-O(20),r.bottom-O(8)};text=L"<  "+std::to_wstring(overviewPage+1)+L" / "+std::to_wstring(overviewPages)+L"  >";DrawText(dc,text.c_str(),-1,&pages,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
}
static LRESULT CALLBACK OverviewProcedure(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:overviewWindow=w;return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);PaintOverview(dc);EndPaint(w,&ps);return 0;}
    case WM_PRINTCLIENT:PaintOverview(reinterpret_cast<HDC>(wp));return 0;
    case WM_SIZE:LayoutOverview();return 0;
    case WM_ACTIVATE:if(LOWORD(wp)==WA_INACTIVE)CloseOverview(false);return 0;
    case WM_DISPLAYCHANGE:case WM_DPICHANGED:CloseOverview(true);return 0;
    case WM_KEYDOWN:{
        int g=SelectedOverviewGroup(),columns=g>=0?overviewGroups[g].columns:1;
        if(wp==VK_ESCAPE)CloseOverview(true);else if(wp==VK_RETURN || wp==VK_SPACE)CommitOverview();
        else if(wp==VK_LEFT)SelectOverview(overviewSelected-1);else if(wp==VK_RIGHT)SelectOverview(overviewSelected+1);
        else if(wp==VK_UP)SelectOverview(overviewSelected-columns);else if(wp==VK_DOWN)SelectOverview(overviewSelected+columns);
        else if(wp==VK_HOME)SelectOverview(0);else if(wp==VK_END)SelectOverview(static_cast<int>(overviewItems.size())-1);
        else if(wp==VK_TAB && !overviewItems.empty())SelectOverview((overviewSelected+((GetKeyState(VK_SHIFT)&0x8000)?-1:1)+static_cast<int>(overviewItems.size()))%static_cast<int>(overviewItems.size()));
        else if(wp==VK_PRIOR || wp==VK_NEXT)ChangeOverviewPage(wp==VK_NEXT?1:-1);return 0;
    }
    case WM_MOUSEWHEEL:{
        POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(w,&p);
        for(int i=0;i<static_cast<int>(overviewGroups.size());++i)if(overviewGroups[i].page==overviewPage && PtInRect(&overviewGroups[i].bounds,p)){
            overviewWheel+=GET_WHEEL_DELTA_WPARAM(wp);ScrollOverviewGroup(i,overviewGroups[i].scroll-(overviewWheel/WHEEL_DELTA)*O(96));overviewWheel%=WHEEL_DELTA;break;
        }return 0;
    }
    case WM_LBUTTONDOWN:{
        POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        for(int i=0;i<static_cast<int>(overviewGroups.size());++i){auto& g=overviewGroups[i];RECT thumb=OverviewThumb(g);InflateRect(&thumb,O(3),0);
            if(g.page==overviewPage && OverviewLimit(g)>0 && PtInRect(&thumb,p)){overviewDrag=i;overviewDragOffset=p.y-thumb.top;SetCapture(w);break;}}
        return 0;
    }
    case WM_MOUSEMOVE:
        if(overviewDrag>=0){auto& g=overviewGroups[overviewDrag];RECT thumb=OverviewThumb(g);int travel=g.view.bottom-g.view.top-(thumb.bottom-thumb.top);
            ScrollOverviewGroup(overviewDrag,MulDiv(GET_Y_LPARAM(lp)-g.view.top-overviewDragOffset,OverviewLimit(g),std::max(1,travel)));}
        return 0;
    case WM_CAPTURECHANGED:overviewDrag=-1;return 0;
    case WM_LBUTTONUP:{
        if(overviewDrag>=0){overviewDrag=-1;ReleaseCapture();return 0;}
        POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};RECT r{};GetClientRect(w,&r);
        if(p.x>=r.right-O(70) && p.y<O(70)){CloseOverview(true);return 0;}
        if(overviewPages>1 && p.y>=r.bottom-O(44) && p.x>=r.right-O(216)){ChangeOverviewPage(p.x<r.right-O(118)?-1:1);return 0;}
        for(const auto& g:overviewGroups)if(g.page==overviewPage && PtInRect(&g.view,p)){
            p.y+=g.scroll-g.view.top;
            for(int i=g.first;i<g.first+g.count;++i)if(PtInRect(&overviewItems[i].card,p)){overviewSelected=i;CommitOverview();return 0;}
            break;
        }return 0;
    }
    case WM_CLOSE:CloseOverview(true);return 0;
    case WM_DESTROY:
        if(GetCapture()==w)ReleaseCapture();overviewDrag=-1;
        for(auto& item:overviewItems)ReleaseOverviewItem(item);overviewItems.clear();overviewGroups.clear();
        if(overviewFont)DeleteObject(overviewFont);if(overviewHeading)DeleteObject(overviewHeading);
        overviewFont=overviewHeading=nullptr;overviewWindow=nullptr;return 0;
    }return DefWindowProc(w,msg,wp,lp);
}
static void OpenOverview(const std::vector<HWND>& windows){
    if(overviewWindow)return;
    overviewPrevious=GetForegroundWindow();overviewPage=overviewWheel=overviewSelected=0;overviewPages=1;overviewDpi=dpi;
    overviewFont=CreateFont(-O(17),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    overviewHeading=CreateFont(-O(25),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    WNDCLASS cls{};cls.hInstance=instance;cls.lpfnWndProc=OverviewProcedure;cls.lpszClassName=L"LeanBar.Overview";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClass(&cls);
    RECT area=Primary().rcMonitor;
    overviewWindow=CreateWindowEx(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,cls.lpszClassName,L"Open windows",WS_POPUP,area.left,area.top,area.right-area.left,area.bottom-area.top,nullptr,nullptr,instance,nullptr);
    if(!overviewWindow){DeleteObject(overviewFont);DeleteObject(overviewHeading);overviewFont=overviewHeading=nullptr;return;}
    BOOL disable=TRUE;DwmSetWindowAttribute(overviewWindow,DWMWA_TRANSITIONS_FORCEDISABLED,&disable,sizeof(disable));
    RefreshOverview(windows);SelectOverview(overviewSelected);ShowWindow(overviewWindow,SW_SHOW);SetForegroundWindow(overviewWindow);SetFocus(overviewWindow);
}
static void ToggleOverview(){if(overviewWindow)CloseOverview(true);else{std::vector<HWND> live;EnumWindows(Collect,reinterpret_cast<LPARAM>(&live));OpenOverview(live);}}
