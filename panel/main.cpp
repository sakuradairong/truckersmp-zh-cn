#include "canvas.h"
#include "data_client.h"
#include "dialogs.h"
#include "public_api.h"
#include <shellapi.h>
#include <windowsx.h>
#include <memory>

namespace companion {
constexpr wchar_t WindowClass[]=L"TruckCompanionPanel.Window.v1";
constexpr UINT TrayMessage=WM_APP+1,ShowMessage=WM_APP+2;
struct Hit {D2D1_RECT_F rect;int action;std::uint64_t account=0;int player=-1;};
enum Action {Nearby=1,Truck,Me,Search,Filter,Sort,Mode,OptionsAction,Hide,Quit,Select,Favorite,OpenProfile,Author};
inline std::wstring Lower(std::wstring text){if(!text.empty())CharLowerBuffW(text.data(),static_cast<DWORD>(text.size()));return text;}
inline bool Current(const Profile& profile){return profile.status==ProfileStatus::Ready&&Fresh(GetTickCount64(),profile.tick,15*60*1000);}
inline std::wstring ProfileMessage(const Profile& p){
    if(p.queued)return L"公开资料查询中（按限频队列依次处理）";
    if(p.status==ProfileStatus::Empty)return L"等待公开资料";
    if(p.status==ProfileStatus::Ready&&!Current(p))return L"缓存已过期，等待更新";
    return p.message;
}

class App {
    HWND window_=nullptr;Canvas canvas_;Settings settings_;std::wstring warning_;
    DataClient client_;PublicApi api_;Connection connection_;
    bool editing_=true,wanted_=true,dialog_=false,hotkeys_=false,renderFailed_=false;
    UINT dpi_=96;int page_=Nearby,filter_=0,sort_=0,scroll_=0;
    std::wstring query_;std::vector<Hit> hits_;
    int selected_=-1;std::uint64_t selectedAccount_=0,epoch_=0;DWORD lastPid_=0;
    UINT taskbarCreated_=0;bool tray_=false;
    float W() const {return static_cast<float>(settings_.width);}
    float H() const {return static_cast<float>(settings_.height);}
    float F() const {return static_cast<float>(settings_.font);}
    bool Live() const {return connection_.available&&Fresh(GetTickCount64(),connection_.data.tick);}
    bool WorldLive() const {return Live()&&connection_.data.world.active&&connection_.data.world.connected&&Fresh(GetTickCount64(),connection_.data.world.tick);}
    bool OwnLive() const {const auto& o=connection_.data.own;return Live()&&o.active&&Fresh(GetTickCount64(),o.tick);}
    std::uint64_t AccountId() const {
        if(settings_.account)return settings_.account;
        const auto& w=connection_.data.world;return Live()&&w.active&&Fresh(GetTickCount64(),w.tick)?w.account:0;
    }
    void Text(const std::wstring& value,float x,float y,float width,float height=25,float size=14,unsigned color=Foreground,bool bold=false){canvas_.Text(value,{x,y,x+width,y+height},size,color,bold);}
    void Button(std::wstring text,float x,float y,float width,int action,bool active=false,std::uint64_t account=0,int player=-1){
        D2D1_RECT_F r{x,y,x+width,y+30};canvas_.Box(r,active?0x503d2d:Surface,6);
        Text(text,x+9,y+5,width-18,22,13,active?Accent:Foreground);
        if(editing_)hits_.push_back({r,action,account,player});
    }
    void Save(){std::wstring error;if(!SaveSettings(settings_,error))warning_=error;}
    bool RegisterKeys(const Settings& s){
        UnregisterHotKey(window_,1);UnregisterHotKey(window_,2);
        bool visible=RegisterHotKey(window_,1,s.visible.modifiers|MOD_NOREPEAT,s.visible.key)!=FALSE;
        bool edit=RegisterHotKey(window_,2,s.edit.modifiers|MOD_NOREPEAT,s.edit.key)!=FALSE;
        if(!visible||!edit){UnregisterHotKey(window_,1);UnregisterHotKey(window_,2);return false;}return true;
    }
    void AddTray(){
        NOTIFYICONDATAW data{sizeof data};data.hWnd=window_;data.uID=1;data.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;
        data.uCallbackMessage=TrayMessage;data.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
        wcscpy_s(data.szTip,L"Truck Companion Panel — 双击编辑");tray_=Shell_NotifyIconW(NIM_ADD,&data)!=FALSE;
    }
    void TrayMenu(){
        auto menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,Mode,editing_?L"进入驾驶模式":L"进入编辑模式");
        AppendMenuW(menu,MF_STRING,Hide,wanted_?L"隐藏面板":L"显示面板");AppendMenuW(menu,MF_STRING,OptionsAction,L"面板设置");
        AppendMenuW(menu,MF_STRING,Author,L"关于 / 作者主页");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,Quit,L"退出");
        POINT p{};GetCursorPos(&p);SetForegroundWindow(window_);int action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,window_,nullptr);DestroyMenu(menu);
        if(action)Act({{},action});PostMessageW(window_,WM_NULL,0,0);
    }
    void Place(bool remember=false){
        RECT current{};GetWindowRect(window_,&current);
        POINT point{settings_.x==INT_MIN?current.left:settings_.x,settings_.y==INT_MIN?current.top:settings_.y};
        auto monitor=MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST);MONITORINFO mi{sizeof mi};GetMonitorInfoW(monitor,&mi);
        int width=MulDiv(settings_.width,static_cast<int>(dpi_),96),height=MulDiv(settings_.height,static_cast<int>(dpi_),96);
        int x=settings_.x==INT_MIN?mi.rcWork.right-width-20:settings_.x,y=settings_.y==INT_MIN?mi.rcWork.top+20:settings_.y;
        x=(std::max<int>)(mi.rcWork.left,(std::min<int>)(x,mi.rcWork.right-width));y=(std::max<int>)(mi.rcWork.top,(std::min<int>)(y,mi.rcWork.bottom-height));
        SetWindowPos(window_,HWND_TOPMOST,x,y,width,height,SWP_NOACTIVATE);settings_.x=x;settings_.y=y;if(remember)Save();
    }
    void SetMode(bool edit){
        // A tray entry or both working hotkeys must remain available to recover input.
        if(!edit&&!tray_&&!hotkeys_){warning_=L"托盘和快捷键均不可用，请先在设置中配置快捷键。";return;}
        editing_=edit;LONG_PTR ex=WS_EX_LAYERED|WS_EX_TOPMOST|WS_EX_TOOLWINDOW;
        if(!editing_)ex|=WS_EX_TRANSPARENT|WS_EX_NOACTIVATE;
        SetWindowLongPtrW(window_,GWL_EXSTYLE,ex);SetWindowPos(window_,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_FRAMECHANGED);
        if(editing_){wanted_=true;ShowWindow(window_,SW_SHOWNORMAL);SetForegroundWindow(window_);}else if(connection_.pid){auto game=GameWindow(connection_.pid);if(game)SetForegroundWindow(game);}
        Tick();
    }
    void Edit(bool search){
        if(dialog_)return;SetMode(true);dialog_=true;DialogInput input;input.search=search;input.query=query_;input.settings=settings_;
        if(Options(window_,input)){
            if(search){query_=input.query;scroll_=0;}
            else if(RegisterKeys(input.settings)){hotkeys_=true;settings_=input.settings;warning_.clear();Place();Save();}
            else{hotkeys_=RegisterKeys(settings_);MessageBoxW(window_,L"快捷键被其他程序占用，原设置已保留。请更换组合。",L"未应用设置",MB_OK|MB_ICONINFORMATION);}
        }
        dialog_=false;Tick();
    }
    void Act(const Hit& hit){
        if(dialog_)return;
        switch(hit.action){
            case Nearby:case Truck:case Me:page_=hit.action;scroll_=0;break;
            case Search:Edit(true);break;
            case Filter:filter_=(filter_+1)%4;scroll_=0;break;
            case Sort:sort_=(sort_+1)%3;scroll_=0;break;
            case Mode:SetMode(!editing_);break;
            case OptionsAction:Edit(false);break;
            case Hide:if(tray_||hotkeys_){wanted_=!wanted_;Tick();}else warning_=L"无法隐藏：请先配置可用快捷键。";break;
            case Quit:DestroyWindow(window_);break;
            case Select:selected_=hit.player;selectedAccount_=hit.account;if(hit.account)api_.Request(hit.account,true);break;
            case Favorite:
                if(hit.account){auto i=settings_.favorites.find(hit.account);if(i!=settings_.favorites.end())settings_.favorites.erase(i);
                    else if(settings_.favorites.size()<512)settings_.favorites.insert(hit.account);else warning_=L"关注列表已达 512 人上限。";Save();}break;
            case OpenProfile:if(hit.account)ShellExecuteW(window_,L"open",(L"https://truckersmp.com/user/"+std::to_wstring(hit.account)).c_str(),nullptr,nullptr,SW_SHOWNORMAL);break;
            case Author:ShellExecuteW(window_,L"open",L"https://github.com/sakuradairong",nullptr,nullptr,SW_SHOWNORMAL);break;
        }
        if(IsWindow(window_))Draw();
    }
    void SpeedCard(){
        canvas_.Box({16,126,W()-16,220},Surface,12);
        const auto& o=connection_.data.own;const bool active=OwnLive();
        Text(L"本车速度 · SCS",30,138,200,22,12,Muted);
        Text(active&&!o.paused&&(o.fields&OwnSpeed)?Number(o.kmh):L"—",28,158,150,56,40,Foreground,true);
        Text(L"km/h",155,182,70,24,13,Muted);
        Text(active?(o.paused?L"游戏暂停 / 菜单中":L"遥测已连接"):L"等待本车遥测",W()-210,145,180,25,14,active?Green:Muted);
        Text(active&&(o.fields&Limit)?L"道路限速  "+Number(o.limitKmh)+L" km/h":L"道路限速  —",W()-210,181,180,24,13,Muted);
    }
    std::wstring SourceMessage(){
        if(!connection_.pid)return L"未发现 ETS2 / ATS；面板正在等待游戏";
        if(!Live())return L"未连接采集模块；请检查安装版本和游戏插件日志";
        if(page_==Truck)return OwnLive()?L"本车数据来自 SCS 遥测；任务名称随游戏语言":L"采集已连接，等待 SCS 遥测帧";
        if(!(connection_.data.capabilities&Tmp))return L"当前为 SCS 版；附近玩家需要可选 SDK 采集包";
        if(!connection_.data.world.active)return L"TMP SDK 未激活 / 版本协商失败；本车遥测仍独立运行";
        if(!Fresh(GetTickCount64(),connection_.data.world.tick))return L"TMP 数据已过期，附近列表暂时隐藏";
        if(!connection_.data.world.connected)return L"TMP SDK 已连接；尚未进入联机服务器";
        return L"附近 = 本机已加载范围；速度数据每人有独立时效";
    }
    void NearbyPage(){
        const wchar_t* filters[]={L"全部",L"关注",L"同 VTC",L"工作人员"};const wchar_t* sorts[]={L"距离 ↑",L"速度 ↓",L"延迟 ↑"};
        Button(L"筛选："+std::wstring(filters[filter_]),16,262,150,Filter,filter_!=0);
        Button(L"排序："+std::wstring(sorts[sort_]),176,262,142,Sort);
        Button(query_.empty()?L"搜索玩家…":query_,16,302,W()-32,Search,!query_.empty());
        std::vector<Player> rows;unsigned stale=0,pending=0;auto mine=api_.Get(AccountId());
        if(filter_==2)api_.Request(AccountId(),true);
        if(WorldLive()){
            const auto& world=connection_.data.world;const auto search=Lower(query_);
            for(unsigned i=0;i<world.count;++i){const auto& p=world.players[i];
                if(world.account&&p.account==world.account)continue;
                if(!Fresh(GetTickCount64(),p.tick)){++stale;continue;}
                if(!search.empty()&&Lower(Wide(p.name)+L" "+Wide(p.tag)+L" "+std::to_wstring(p.id)+L" "+std::to_wstring(p.account)).find(search)==std::wstring::npos)continue;
                if(filter_==1&&!settings_.favorites.count(p.account))continue;
                if(filter_==3&&!p.roles)continue;
                if(filter_==2){api_.Request(p.account);auto profile=api_.Get(p.account);
                    if(!Current(profile)||!Current(mine)){++pending;continue;}if(!mine.vtc||profile.vtc!=mine.vtc)continue;}
                rows.push_back(p);
            }
        }
        std::stable_sort(rows.begin(),rows.end(),[&](const Player& a,const Player& b){
            unsigned bit=sort_==0?Distance:sort_==1?Speed:Ping;bool validA=(a.fields&bit)!=0,validB=(b.fields&bit)!=0;
            if(validA!=validB)return validA;
            if(validA){if(sort_==0&&a.distance!=b.distance)return a.distance<b.distance;if(sort_==1&&a.kmh!=b.kmh)return a.kmh>b.kmh;if(sort_==2&&a.ping!=b.ping)return a.ping<b.ping;}
            return a.id<b.id;
        });
        const float rowHeight=(std::max)(60.f,F()*2+26);const int visible=(std::max)(1,static_cast<int>((H()-170-366)/rowHeight));
        scroll_=(std::max)(0,(std::min)(scroll_,static_cast<int>(rows.size())-visible));
        Text(L"玩家 / 范围",22,342,W()-240,22,12,Muted);Text(L"速度",W()-190,342,80,22,12,Muted);Text(L"延迟",W()-100,342,70,22,12,Muted);
        for(int n=0;n<visible&&scroll_+n<static_cast<int>(rows.size());++n){
            const auto& p=rows[static_cast<std::size_t>(scroll_+n)];float y=366+n*rowHeight;D2D1_RECT_F r{16,y,W()-16,y+rowHeight-6};
            const bool chosen=p.id==selected_&&p.account==selectedAccount_;
            canvas_.Box(r,chosen?0x344338:Surface,7);
            if(editing_)hits_.push_back({r,Select,p.account,p.id});
            auto name=Wide(p.name);if(name.empty())name=L"未提供昵称";
            if(settings_.favorites.count(p.account))name=L"★ "+name;
            Text(name,26,y+6,W()-230,F()+7,F(),Foreground,true);
            Text(L"#"+std::to_wstring(p.id)+L" · "+((p.fields&Distance)?Number(p.distance)+L" m":L"距离未知")+(p.roles?L" · 工作人员":L""),26,y+F()+12,W()-230,F()+6,F()-1,Muted);
            Text((p.fields&Speed)?Number(p.kmh)+L" km/h":L"—",W()-190,y+12,85,F()+10,F(),Accent);
            Text((p.fields&Ping)?std::to_wstring(p.ping)+L" ms":L"—",W()-100,y+12,75,F()+10,F(),Muted);
        }
        if(rows.empty()){
            std::wstring reason=!WorldLive()?L"暂无当前附近玩家":filter_==2&&!AccountId()?L"请先在设置中填写自己的 TMP ID":
                filter_==2&&Current(mine)&&!mine.vtc?L"当前账号未加入 VTC":pending?L"正在排队查询 VTC；已确认的结果会逐步出现":L"没有符合条件的玩家";
            Text(reason,26,379,W()-52,34,14,Muted);
        }
        std::wstring count=std::to_wstring(rows.size())+L" 人符合 · 滚轮翻页";
        if(pending)count+=L" · VTC 待查 "+std::to_wstring(pending);
        if(stale)count+=L" · 过期 "+std::to_wstring(stale);
        if(WorldLive()&&connection_.data.world.overflow)count+=L" · 容量外/待采样 "+std::to_wstring(connection_.data.world.overflow);
        Text(count,20,H()-172,W()-40,22,12,Muted);
        SelectedCard();
    }
    void SelectedCard(){
        const float y=H()-146;canvas_.Box({16,y,W()-16,H()-52},Surface,9);
        const Player* player=nullptr;
        if(WorldLive())for(unsigned i=0;i<connection_.data.world.count;++i){const auto& p=connection_.data.world.players[i];if(p.id==selected_&&p.account==selectedAccount_&&Fresh(GetTickCount64(),p.tick)){player=&p;break;}}
        if(!player){Text(L"点击一位玩家查看 VTC，或加入本机关注列表",28,y+18,W()-56,28,13,Muted);Text(L"关注保存在本机，不代表 Steam 好友关系",28,y+50,W()-56,24,12,Muted);return;}
        api_.Request(player->account,true);auto profile=api_.Get(player->account);
        Text(Wide(player->name)+L" · TMP "+std::to_wstring(player->account),28,y+8,W()-200,23,13,Foreground,true);
        Text(Current(profile)?profile.vtcName:ProfileMessage(profile),28,y+35,W()-200,23,12,Muted);
        Text(Wide(player->tag),28,y+62,W()-200,22,12,Muted);
        if(player->account){Button(settings_.favorites.count(player->account)?L"取消关注":L"关注",W()-152,y+9,124,Favorite,false,player->account);
            Button(L"公开主页 ↗",W()-152,y+49,124,OpenProfile,false,player->account);}
    }
    void TruckPage(){
        const auto& o=connection_.data.own;const bool live=OwnLive();
        auto metric=[&](const wchar_t* label,const std::wstring& value,float x,float y,float width){canvas_.Box({x,y,x+width,y+88},Surface,9);Text(label,x+14,y+12,width-28,22,12,Muted);Text(value,x+14,y+39,width-28,36,24,Foreground,true);};
        const float half=(W()-44)/2;
        metric(L"剩余燃油",live&&(o.fields&Fuel)?Number(o.fuel,1)+L" L":L"—",16,264,half);
        metric(L"档位",live&&(o.fields&Gear)?(o.gear==0?L"N":o.gear<0?L"R"+std::to_wstring(-o.gear):L"D"+std::to_wstring(o.gear)):L"—",28+half,264,half);
        metric(L"导航剩余距离",live&&(o.fields&Navigation)?Number(o.navigationKm,1)+L" km":L"—",16,364,half);
        metric(L"发动机转速",live&&(o.fields&Rpm)?Number(o.rpm)+L" rpm":L"—",28+half,364,half);
        canvas_.Box({16,464,W()-16,H()-58},Surface,9);
        Text(L"当前任务",30,477,W()-60,24,14,Foreground,true);
        Text(live&&(o.jobFields&1)?Wide(o.cargo):L"暂无当前货物资料",30,510,W()-60,28,16,Accent);
        if(H()>625)Text(live&&(o.jobFields&6)?Wide(o.from)+L"  →  "+Wide(o.to):L"起点 / 终点未提供",30,547,W()-60,28,14,Muted);
        if(H()>665)Text(L"货物与城市名称由 SCS 提供，跟随游戏语言。",30,584,W()-60,24,12,Muted);
    }
    void ProfilePage(){
        const auto id=AccountId();api_.Request(id,true);auto profile=api_.Get(id);
        canvas_.Box({16,264,W()-16,418},Surface,10);
        Text(settings_.account?L"个人资料 · 手动指定账号":L"个人资料 · 当前 SDK 账号",30,278,W()-60,24,13,Muted);
        Text(id?(Current(profile)?profile.name:L"TMP "+std::to_wstring(id)):L"等待自动读取当前账号",30,309,W()-60,36,22,Foreground,true);
        Text(id?(Current(profile)?profile.vtcName:ProfileMessage(profile)):L"SCS 版可在设置中填写 TMP ID 查询公开资料",30,350,W()-60,25,13,Muted);
        if(id)Button(L"公开主页 ↗",30,380,124,OpenProfile,false,id);
        std::wstring status;std::uint64_t tick=0;auto servers=api_.Servers(status,tick);const auto serverId=WorldLive()?connection_.data.world.server:0;
        const Server* server=nullptr;for(const auto& row:servers)if(row.id==serverId){server=&row;break;}
        canvas_.Box({16,430,W()-16,H()-58},Surface,10);
        Text(L"当前服务器 · 公开 API",30,444,W()-60,24,13,Muted);
        Text(server?server->name:serverId?L"服务器 #"+std::to_wstring(serverId):L"尚未获取当前服务器",30,478,W()-60,32,20,Foreground,true);
        Text(H()>625?(server?(std::to_wstring(server->players)+L" / "+std::to_wstring(server->capacity)+L" 在线 · 排队 "+std::to_wstring(server->queue)):L"进入联机后通过 SDK 自动识别服务器"):status,30,518,W()-60,26,13,Muted);
        if(H()>665)Text(status,30,556,W()-60,24,12,Muted);
        if(H()>700)Text(tick?L"服务器缓存 "+std::to_wstring((GetTickCount64()-tick)/1000)+L" 秒前更新":L"服务器资料尚不可用",30,589,W()-60,23,12,Muted);
        if(H()>735)Text(L"资料缓存 15 分钟；服务器约每分钟更新。",30,622,W()-60,23,12,Muted);
    }
    void Draw(){
        if(!window_||!IsWindowVisible(window_))return;RECT r{};GetClientRect(window_,&r);
        if(!canvas_.Begin(r.right,r.bottom,dpi_)){if(!renderFailed_){renderFailed_=true;MessageBoxW(window_,L"无法初始化独立面板绘图。请检查 Windows 图形组件。",L"Truck Companion Panel",MB_OK|MB_ICONERROR);}return;}
        hits_.clear();canvas_.Box({0,0,W(),H()},Background,14);
        Text(L"TRUCK COMPANION",20,15,W()-170,30,18,Foreground,true);
        Text(L"RainySY  /  独立行车面板",20,47,W()-210,22,12,Muted);
        Button(editing_?L"编辑中":L"驾驶中",W()-158,16,80,Mode,editing_);Button(L"×",W()-66,16,44,Quit);
        Button(L"设置",W()-86,53,64,OptionsAction);
        const float tab=(W()-48)/3;
        Button(L"附近",16,87,tab,Nearby,page_==Nearby);Button(L"本车",24+tab,87,tab,Truck,page_==Truck);Button(L"资料",32+tab*2,87,tab,Me,page_==Me);
        SpeedCard();Text(SourceMessage(),20,233,W()-40,23,12,Muted);
        if(page_==Nearby)NearbyPage();else if(page_==Truck)TruckPage();else ProfilePage();
        canvas_.Line(20,H()-42,W()-20);
        std::wstring footer=warning_.empty()?(editing_?L"拖动顶部移动 · "+KeyText(settings_.edit)+L" 进入驾驶":KeyText(settings_.edit)+L" 编辑 · "+KeyText(settings_.visible)+L" 隐藏"):warning_;
        Text(footer,20,H()-31,W()-40,22,12,warning_.empty()?Muted:Accent);
        if(!canvas_.End(window_,static_cast<BYTE>(settings_.opacity)))renderFailed_=true;
    }
    void Tick(){
        connection_=client_.Read();const auto& world=connection_.data.world;
        if(connection_.pid!=lastPid_||world.epoch!=epoch_){selected_=-1;selectedAccount_=0;scroll_=0;lastPid_=connection_.pid;epoch_=world.epoch;}
        bool show=wanted_;
        if(show&&!editing_&&settings_.followGame&&connection_.pid){DWORD front=0;GetWindowThreadProcessId(GetForegroundWindow(),&front);show=front==connection_.pid;}
        if(show&&!IsWindowVisible(window_))ShowWindow(window_,SW_SHOWNOACTIVATE);else if(!show&&IsWindowVisible(window_))ShowWindow(window_,SW_HIDE);
        Draw();
    }
    LRESULT Message(UINT message,WPARAM w,LPARAM l){
        if(taskbarCreated_&&message==taskbarCreated_){AddTray();if(!tray_&&!hotkeys_)SetMode(true);return 0;}
        switch(message){
            case WM_TIMER:Tick();return 0;
            case WM_PAINT:{PAINTSTRUCT p{};BeginPaint(window_,&p);EndPaint(window_,&p);Draw();return 0;}
            case WM_ERASEBKGND:return 1;
            case WM_HOTKEY:if(!dialog_){if(w==1)Act({{},Hide});if(w==2)SetMode(!editing_);}return 0;
            case ShowMessage:SetMode(true);return 0;
            case TrayMessage:if(l==WM_LBUTTONDBLCLK)SetMode(true);if(l==WM_RBUTTONUP||l==WM_CONTEXTMENU)TrayMenu();return 0;
            case WM_NCHITTEST:{if(!editing_)return HTTRANSPARENT;POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(window_,&p);
                float x=p.x*96.f/dpi_,y=p.y*96.f/dpi_;if(y<78&&x<W()-174)return HTCAPTION;return HTCLIENT;}
            case WM_MOUSEACTIVATE:return editing_?MA_ACTIVATE:MA_NOACTIVATE;
            case WM_LBUTTONUP:{if(!editing_)return 0;float x=GET_X_LPARAM(l)*96.f/dpi_,y=GET_Y_LPARAM(l)*96.f/dpi_;
                for(const auto& hit:hits_)if(x>=hit.rect.left&&x<hit.rect.right&&y>=hit.rect.top&&y<hit.rect.bottom){auto copy=hit;Act(copy);break;}return 0;}
            case WM_MOUSEWHEEL:if(editing_&&page_==Nearby){scroll_+=(GET_WHEEL_DELTA_WPARAM(w)<0?3:-3);Draw();}return 0;
            case WM_KEYDOWN:if(w==VK_ESCAPE&&editing_)SetMode(false);return 0;
            case WM_EXITSIZEMOVE:{RECT r{};GetWindowRect(window_,&r);settings_.x=r.left;settings_.y=r.top;Place(true);return 0;}
            case WM_DPICHANGED:{dpi_=HIWORD(w);auto* rect=reinterpret_cast<RECT*>(l);settings_.x=rect->left;settings_.y=rect->top;Place();Draw();return 0;}
            case WM_DISPLAYCHANGE:Place();Draw();return 0;
            case WM_CLOSE:DestroyWindow(window_);return 0;
            case WM_DESTROY:{KillTimer(window_,1);UnregisterHotKey(window_,1);UnregisterHotKey(window_,2);NOTIFYICONDATAW data{sizeof data};data.hWnd=window_;data.uID=1;Shell_NotifyIconW(NIM_DELETE,&data);PostQuitMessage(0);return 0;}
        }
        return DefWindowProcW(window_,message,w,l);
    }
    static LRESULT CALLBACK Proc(HWND window,UINT message,WPARAM w,LPARAM l){
        auto* app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);app->window_=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
        if(!app)return DefWindowProcW(window,message,w,l);
        try{return app->Message(message,w,l);}catch(...){app->warning_=L"面板处理失败；等待下一次数据更新。";return DefWindowProcW(window,message,w,l);}
    }
public:
    App(){settings_=LoadSettings(warning_);}
    int Run(HINSTANCE instance){
        WNDCLASSEXW cls{sizeof cls};cls.lpfnWndProc=Proc;cls.hInstance=instance;cls.lpszClassName=WindowClass;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
        if(!RegisterClassExW(&cls))return 1;
        window_=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,WindowClass,L"Truck Companion Panel",WS_POPUP,0,0,settings_.width,settings_.height,nullptr,nullptr,instance,this);
        if(!window_)return 1;dpi_=GetDpiForWindow(window_);Place();hotkeys_=RegisterKeys(settings_);if(!hotkeys_)warning_=L"快捷键被占用；请在设置中更换组合。";
        taskbarCreated_=RegisterWindowMessageW(L"TaskbarCreated");AddTray();SetTimer(window_,1,200,nullptr);ShowWindow(window_,SW_SHOWNORMAL);Draw();
        MSG message{};int result=0;while((result=GetMessageW(&message,nullptr,0,0))>0){TranslateMessage(&message);DispatchMessageW(&message);}return result<0?1:0;
    }
};
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE single=CreateMutexW(nullptr,FALSE,L"Local\\TruckCompanionPanel.Instance.v1");if(!single)return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS){auto existing=FindWindowW(companion::WindowClass,nullptr);if(existing)PostMessageW(existing,companion::ShowMessage,0,0);CloseHandle(single);return 0;}
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int result=1;
    try{auto app=std::make_unique<companion::App>();result=app->Run(instance);}catch(...){MessageBoxW(nullptr,L"面板初始化失败；请检查本机运行环境。",L"Truck Companion Panel",MB_OK|MB_ICONERROR);}
    if(SUCCEEDED(com))CoUninitialize();CloseHandle(single);return result;
}
