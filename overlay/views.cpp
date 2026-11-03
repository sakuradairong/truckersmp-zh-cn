#include "runtime.h"
#include <cstdio>

namespace overlay {
namespace {
std::string Clean(const char* value){return companion::Utf8(companion::Wide(value));}
std::string Num(float value,unsigned precision=0){return companion::Utf8(companion::Number(value,precision));}
std::wstring Lower(std::wstring value){if(!value.empty())CharLowerBuffW(value.data(),static_cast<DWORD>(value.size()));return value;}
void Label(const std::string& value){ImGui::TextUnformatted(value.c_str());}
void Dim(const std::string& value){ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.68f,.68f,.68f,1));Label(value);ImGui::PopStyleColor();}
void Accent(const std::string& value){ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.49f,.50f,1));Label(value);ImGui::PopStyleColor();}
void Fit(const std::string& value,float width){
    width=std::max(1.f,width);auto p=ImGui::GetCursorScreenPos();float height=ImGui::GetTextLineHeight();
    auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(p,{p.x+width,p.y+height},true);
    draw->AddText(p,ImGui::GetColorU32(ImGuiCol_Text),value.c_str());draw->PopClipRect();ImGui::Dummy({width,height});
    if(ImGui::IsItemHovered()&&ImGui::CalcTextSize(value.c_str()).x>width)ImGui::SetTooltip("%s",value.c_str());
}
bool Nav(const char* label,bool active,float width){
    if(active){ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.66f,.15f,.18f,1));ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(.76f,.21f,.24f,1));}
    bool pressed=ImGui::Button(label,{width,0});if(active)ImGui::PopStyleColor(2);return pressed;
}
std::string Status(const Profile* p){if(!p)return "等待公开资料";if(p->state==ProfileState::Ready&&!Current(*p))return "缓存已过期，等待更新";return p->message;}
bool LiveOwn(const companion::Own& own){return own.active&&companion::Fresh(GetTickCount64(),own.tick);}
}
bool Runtime::WorldLive() const {const auto& w=players_.Read();return w.active&&w.connected&&companion::Fresh(GetTickCount64(),w.tick);}
std::uint64_t Runtime::Account() const {return settings_.account?settings_.account:players_.Read().account;}
const Profile* Runtime::ProfileFor(std::uint64_t id) const {if(!data_)return nullptr;auto i=data_->profiles.find(id);return i==data_->profiles.end()?nullptr:&i->second;}
void Runtime::Request(std::uint64_t id,bool priority){if(allowRequests_&&id)service_->Request(id,priority);}
void Runtime::Draft(){draft_=settings_;sprintf_s(accountText_,"%llu",static_cast<unsigned long long>(draft_.account));settingsMessage_.clear();}
void Runtime::Draw(){
    const float width=renderer_.Width(),height=renderer_.Height();const float scale=std::clamp(height/1080.f,.8f,1.8f);
    const float desiredWidth=std::min(settings_.width*scale,width-16),desiredHeight=std::min(settings_.height*scale,height-16);
    ImGuiCond position=ImGuiCond_FirstUseEver;
    if(width!=lastWidth_||height!=lastHeight_){position=ImGuiCond_Always;lastWidth_=width;lastHeight_=height;}
    const float x=settings_.x==INT_MIN?width-desiredWidth-16:settings_.x*width/10000.f;
    const float y=settings_.y==INT_MIN?20:settings_.y*height/10000.f;
    ImGui::SetNextWindowPos({std::clamp(x,0.f,std::max(0.f,width-desiredWidth)),std::clamp(y,0.f,std::max(0.f,height-desiredHeight))},position);
    ImGui::SetNextWindowSize({desiredWidth,desiredHeight},position);
    ImGui::SetNextWindowSizeConstraints({std::min(std::max(430.f*scale,ImGui::GetFontSize()*24),width-16),std::min(360.f*scale,height-16)},{std::min(900.f*scale,width-16),std::min(1200.f*scale,height-16)});
    ImGui::SetNextWindowBgAlpha(settings_.opacity/255.f);
    ImGuiWindowFlags flags=ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse;
    if(!editing_)flags|=ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoFocusOnAppearing;
    bool open=shown_;
    if(ImGui::Begin("Truck Companion###TruckCompanionOverlay",&open,flags)){
        boundsMin_=ImGui::GetWindowPos();auto size=ImGui::GetWindowSize();boundsMax_={boundsMin_.x+size.x,boundsMin_.y+size.y};
        if(editing_&&!ImGui::IsMouseDown(ImGuiMouseButton_Left)){
            int nx=static_cast<int>(std::clamp(boundsMin_.x/width,0.f,1.f)*10000),ny=static_cast<int>(std::clamp(boundsMin_.y/height,0.f,1.f)*10000);
            int nw=std::clamp(static_cast<int>(std::round(size.x/scale)),430,900),nh=std::clamp(static_cast<int>(std::round(size.y/scale)),360,1200);
            if(settings_.x!=nx||settings_.y!=ny||settings_.width!=nw||settings_.height!=nh){settings_.x=nx;settings_.y=ny;settings_.width=nw;settings_.height=nh;MarkDirty();}
        }
        const auto& world=players_.Read();const Server* server=nullptr;
        if(WorldLive()&&data_)for(const auto& s:data_->servers)if(s.id==world.server){server=&s;break;}
        const std::string count=WorldLive()?"已加载 "+std::to_string(world.total):"未连接";
        float gap=ImGui::GetStyle().ItemSpacing.x,countWidth=ImGui::CalcTextSize(count.c_str()).x;
        Fit(server?server->name:WorldLive()?"服务器 #"+std::to_string(world.server):"TruckersMP",ImGui::GetContentRegionAvail().x-countWidth-gap);ImGui::SameLine();Dim(count);
        Request(Account());auto profile=ProfileFor(Account());
        std::string name=profile&&Current(*profile)?profile->name:Account()?"TMP "+std::to_string(Account()):"等待个人资料";
        ImGui::AlignTextToFramePadding();
        bool ownLive=LiveOwn(own_);std::string speed=ownLive&&!own_.paused&&(own_.fields&companion::OwnSpeed)?Num(own_.kmh):"—";
        const std::string speedText=speed+" km/h"+(ownLive&&own_.paused?" · 暂停":"");
        Fit(name,ImGui::GetContentRegionAvail().x-ImGui::CalcTextSize(speedText.c_str()).x-gap);ImGui::SameLine();Label(speedText);
        ImGui::Separator();
        const float footer=ImGui::GetFrameHeightWithSpacing()+ImGui::GetTextLineHeightWithSpacing()+ImGui::GetStyle().ItemSpacing.y*2+1;
        if(ImGui::BeginChild("Content",{0,std::max(1.f,ImGui::GetContentRegionAvail().y-footer)},ImGuiChildFlags_None)){
            ImGui::PushID(page_);
            switch(page_){case 0:Nearby();break;case 1:Truck();break;case 2:Freight();break;case 3:ProfilePage();break;case 4:SettingsPage();break;default:page_=0;break;}
            ImGui::PopID();
        }ImGui::EndChild();
        ImGui::Separator();
        ImGui::BeginDisabled(!editing_);
        const char* pages[]={"附近","本车","货运","资料","设置"};
        const float driveWidth=ImGui::CalcTextSize("返回驾驶").x+ImGui::GetStyle().FramePadding.x*2;
        const float navWidth=std::max(1.f,(ImGui::GetContentRegionAvail().x-driveWidth-gap*5)/5);
        ImGui::PushID("PageNavigation");
        for(int i=0;i<5;++i){if(i)ImGui::SameLine();ImGui::PushID(i);if(Nav(pages[i],page_==i,navWidth)){if(i==4&&page_!=4)Draft();page_=i;}ImGui::PopID();}
        ImGui::SameLine();if(ImGui::Button("返回驾驶",{driveWidth,0}))requestMode_=true;
        ImGui::PopID();
        ImGui::EndDisabled();
        std::string hint=std::string(editing_?"右键 / ESC 返回驾驶 · ":"右键操作 · ")+companion::Utf8(companion::KeyText(settings_.visible))+" 显示/隐藏";
        if(!message_.empty())hint+=" · "+message_;else if(data_&&!data_->message.empty())hint+=" · "+data_->message;
        ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.68f,.68f,.68f,1));Fit(hint,ImGui::GetContentRegionAvail().x);ImGui::PopStyleColor();
    }
    ImGui::End();shown_=open;
}
void Runtime::Nearby(){
    const char* filters[]={"全部","关注","同车队","团队成员"};const char* orders[]={"距离 ↑","车速 ↓","延迟 ↑"};
    float gap=ImGui::GetStyle().ItemSpacing.x,sortWidth=ImGui::GetFontSize()*6.5f;
    ImGui::SetNextItemWidth(std::max(1.f,ImGui::GetContentRegionAvail().x-sortWidth-gap));ImGui::InputTextWithHint("##Search","搜索玩家 / ID / 标签",search_,sizeof search_);
    ImGui::SameLine();ImGui::SetNextItemWidth(sortWidth);ImGui::Combo("##Sort",&sort_,orders,3);
    float filterWidth=(ImGui::GetContentRegionAvail().x-gap*3)/4;
    // Visible filter labels may also appear in selected-player actions.
    ImGui::PushID("NearbyFilters");
    for(int i=0;i<4;++i){if(i)ImGui::SameLine();ImGui::PushID(i);if(Nav(filters[i],filter_==i,filterWidth))filter_=i;ImGui::PopID();}
    ImGui::PopID();
    if(ImGui::IsItemHovered())ImGui::SetTooltip("团队成员指 TruckersMP 工作人员；同车队按正式 VTC 匹配。");
    const auto& world=players_.Read();std::vector<const companion::Player*> rows;unsigned stale=0,pending=0;
    auto mine=ProfileFor(Account());if(filter_==2)Request(Account(),true);
    const auto search=Lower(companion::Wide(search_));
    if(WorldLive())for(unsigned n=0;n<world.count;++n){const auto& p=world.players[n];
        if(world.account&&p.account==world.account)continue;
        if(!companion::Fresh(GetTickCount64(),p.tick)){++stale;continue;}
        if(!search.empty()&&Lower(companion::Wide(p.name)+L" "+companion::Wide(p.tag)+L" "+std::to_wstring(p.id)+L" "+std::to_wstring(p.account)).find(search)==std::wstring::npos)continue;
        if(filter_==1&&!settings_.favorites.count(p.account))continue;if(filter_==3&&!p.roles)continue;
        if(filter_==2){auto other=ProfileFor(p.account);Request(p.account);
            if(!mine||!other||!Current(*mine)||!Current(*other)){++pending;continue;}if(!mine->vtc||mine->vtc!=other->vtc)continue;}
        rows.push_back(&p);
    }
    std::stable_sort(rows.begin(),rows.end(),[this](const auto* a,const auto* b){unsigned field=sort_==0?companion::Distance:sort_==1?companion::Speed:companion::Ping;bool validA=(a->fields&field)!=0,validB=(b->fields&field)!=0;
        if(validA!=validB)return validA;if(validA){if(sort_==0&&a->distance!=b->distance)return a->distance<b->distance;if(sort_==1&&a->kmh!=b->kmh)return a->kmh>b->kmh;if(sort_==2&&a->ping!=b->ping)return a->ping<b->ping;}return a->id<b->id;});
    std::string summary=std::to_string(rows.size())+" 人符合";
    if(pending)summary+=" · VTC 待查 "+std::to_string(pending);
    if(WorldLive()&&world.overflow)summary+=" · 容量外/待采样 "+std::to_string(world.overflow);
    if(stale)summary+=" · 过期 "+std::to_string(stale);
    if(rows.empty())summary+=!WorldLive()?" · 等待联机数据":filter_==2&&mine&&Current(*mine)&&!mine->vtc?" · 当前账号未加入 VTC":pending?" · 正在查询车队":" · 没有匹配玩家";
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.68f,.68f,.68f,1));Fit(summary,ImGui::GetContentRegionAvail().x);ImGui::PopStyleColor();
    const float selectedSpace=ImGui::GetTextLineHeightWithSpacing()*2+ImGui::GetFrameHeightWithSpacing()+ImGui::GetStyle().ItemSpacing.y*2+1;
    const float tableHeight=std::max(ImGui::GetTextLineHeightWithSpacing()*3,ImGui::GetContentRegionAvail().y-selectedSpace);
    const float tableWidth=std::max(ImGui::GetContentRegionAvail().x-12,ImGui::GetFontSize()*27);
    if(ImGui::BeginTable("Nearby",5,ImGuiTableFlags_ScrollY|ImGuiTableFlags_ScrollX|ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_SizingFixedFit,{0,tableHeight},tableWidth)){
        float unit=ImGui::GetFontSize();
        ImGui::TableSetupColumn("ID",ImGuiTableColumnFlags_WidthFixed,unit*2.6f);ImGui::TableSetupColumn("玩家",ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("车速",ImGuiTableColumnFlags_WidthFixed,unit*4.8f);ImGui::TableSetupColumn("延迟",ImGuiTableColumnFlags_WidthFixed,unit*3.8f);
        ImGui::TableSetupColumn("距离",ImGuiTableColumnFlags_WidthFixed,unit*4.1f);ImGui::TableSetupScrollFreeze(0,1);ImGui::TableHeadersRow();
        ImGuiListClipper clipper;clipper.Begin(static_cast<int>(rows.size()));
        while(clipper.Step())for(int n=clipper.DisplayStart;n<clipper.DisplayEnd;++n){const auto& p=*rows[static_cast<std::size_t>(n)];
            ImGui::PushID(p.id);ImGui::TableNextRow();ImGui::TableNextColumn();
            bool selected=selected_==p.id&&selectedAccount_==p.account;
            auto name=Clean(p.name);if(name.empty())name="未提供昵称";
            auto label=std::string(settings_.favorites.count(p.account)?"★ ":"")+name;
            if(ImGui::Selectable(std::to_string(p.id).c_str(),selected,ImGuiSelectableFlags_SpanAllColumns|ImGuiSelectableFlags_AllowOverlap)){selected_=p.id;selectedAccount_=p.account;service_->Request(p.account,true);}
            ImGui::TableNextColumn();Label(label);
            if(ImGui::IsItemHovered()){ImGui::BeginTooltip();Label(Clean(p.name));Dim("TMP "+std::to_string(p.account));Dim(Clean(p.tag));if(p.roles)Dim("TruckersMP 工作人员");ImGui::EndTooltip();}
            ImGui::TableNextColumn();Label((p.fields&companion::Speed)?Num(p.kmh)+" km/h":"—");
            ImGui::TableNextColumn();Label((p.fields&companion::Ping)?std::to_string(p.ping)+" ms":"—");
            ImGui::TableNextColumn();Label((p.fields&companion::Distance)?Num(p.distance)+" m":"—");ImGui::PopID();
        }
        ImGui::EndTable();
    }
    Selected();
}
void Runtime::Selected(){
    ImGui::Separator();const companion::Player* player=nullptr;const auto& world=players_.Read();
    if(WorldLive())for(unsigned i=0;i<world.count;++i){const auto& p=world.players[i];if(p.id==selected_&&p.account==selectedAccount_&&companion::Fresh(GetTickCount64(),p.tick)){player=&p;break;}}
    if(!player){Dim("右键进入操作，再点击玩家查看车队和关注。");Dim("附近为本机已加载范围；关注保存在本机。");return;}
    auto profile=ProfileFor(player->account);Request(player->account,true);Fit(Clean(player->name)+" · TMP "+std::to_string(player->account),ImGui::GetContentRegionAvail().x);
    Fit(profile&&Current(*profile)?profile->vtcName:Status(profile),ImGui::GetContentRegionAvail().x);
    if(player->account){bool favorite=settings_.favorites.count(player->account)!=0;
        ImGui::PushID("SelectedPlayerActions");
        const auto accountId=std::to_string(player->account);ImGui::PushID(accountId.c_str());
        // Keep the toggle identity stable when its visible label changes.
        if(ImGui::Button(favorite?"取消关注###ToggleFavorite":"关注###ToggleFavorite")){if(favorite)settings_.favorites.erase(player->account);else if(settings_.favorites.size()<512)settings_.favorites.insert(player->account);else message_="关注列表上限为 512 人。";MarkDirty();}
        ImGui::SameLine();if(ImGui::Button("公开主页###OpenProfile"))service_->OpenProfile(player->account);
        ImGui::PopID();ImGui::PopID();
    }
}
void Runtime::Truck(){
    bool live=LiveOwn(own_);auto metric=[&](const char* label,const std::string& value){Dim(label);ImGui::PushFont(nullptr,ImGui::GetStyle().FontSizeBase*1.5f);Label(value);ImGui::PopFont();ImGui::Spacing();};
    if(ImGui::BeginTable("TruckMetrics",2,ImGuiTableFlags_SizingStretchSame)){
        ImGui::TableNextColumn();metric("剩余燃油",live&&(own_.fields&companion::Fuel)?Num(own_.fuel,1)+" L":"—");
        ImGui::TableNextColumn();metric("档位",live&&(own_.fields&companion::Gear)?(own_.gear==0?"N":own_.gear<0?"R"+std::to_string(-own_.gear):"D"+std::to_string(own_.gear)):"—");
        ImGui::TableNextColumn();metric("导航剩余距离",live&&(own_.fields&companion::Navigation)?Num(own_.navigationKm,1)+" km":"—");
        ImGui::TableNextColumn();metric("发动机转速",live&&(own_.fields&companion::Rpm)?Num(own_.rpm)+" rpm":"—");
        ImGui::TableNextColumn();metric("道路限速",live&&(own_.fields&companion::Limit)?Num(own_.limitKmh)+" km/h":"—");ImGui::EndTable();
    }
    ImGui::Spacing();ImGui::TextWrapped("暂停时保留最近的非速度指标；当前运输任务见底部“货运”。");
}
void Runtime::Freight(){
    bool live=LiveOwn(own_);Label("当前运输任务");ImGui::Separator();
    Accent(live&&(own_.jobFields&1)?Clean(own_.cargo):"暂无当前货物资料");
    if(ImGui::BeginTable("JobRoute",2,ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_RowBg)){
        ImGui::TableNextColumn();Dim("起点城市");ImGui::TableNextColumn();Label(live&&(own_.jobFields&2)?Clean(own_.from):"—");
        ImGui::TableNextColumn();Dim("终点城市");ImGui::TableNextColumn();Label(live&&(own_.jobFields&4)?Clean(own_.to):"—");
        ImGui::TableNextColumn();Dim("导航剩余距离");ImGui::TableNextColumn();Label(live&&(own_.fields&companion::Navigation)?Num(own_.navigationKm,1)+" km":"—");ImGui::EndTable();
    }
    ImGui::Spacing();Dim("货物和城市名称跟随游戏语言。");ImGui::Separator();
    Label("自选货运 / 到场接取");
    ImGui::TextWrapped("当前版本只展示任务。创建任务及到货场直接接取，请使用 TMP 原版货运调度器。");
    ImGui::Spacing();ImGui::TextWrapped("本面板的自选货运仍在研究中：公开接口尚未提供生成或接取任务功能，暂时无法代为操作原版调度器。");
}
void Runtime::ProfilePage(){
    auto id=Account();Request(id,true);auto p=ProfileFor(id);
    Dim(settings_.account?"个人资料 · 手动指定账号":"个人资料 · 当前玩家");
    ImGui::PushFont(nullptr,ImGui::GetStyle().FontSizeBase*1.6f);Label(id?(p&&Current(*p)?p->name:"TMP "+std::to_string(id)):"等待账号资料");ImGui::PopFont();
    Dim(id?"TMP ID  "+std::to_string(id):"可在设置中手动填写 TMP ID");
    Accent(p&&Current(*p)?p->vtcName:Status(p));
    if(p&&Current(*p))Dim("公开资料缓存 "+std::to_string((GetTickCount64()-p->tick)/1000)+" 秒前更新");
    if(id&&ImGui::Button("打开玩家公开主页"))service_->OpenProfile(id);
    ImGui::Separator();Dim("当前服务器 · 公开 API");const Server* server=nullptr;
    if(WorldLive()&&data_)for(const auto& s:data_->servers)if(s.id==players_.Read().server){server=&s;break;}
    Label(server?server->name:WorldLive()?"服务器 #"+std::to_string(players_.Read().server):"尚未获取当前服务器");
    if(server)Label(std::to_string(server->players)+" / "+std::to_string(server->capacity)+" 在线 · 排队 "+std::to_string(server->queue));
    if(data_){Dim(data_->serverMessage);if(data_->serverTick)Dim("缓存 "+std::to_string((GetTickCount64()-data_->serverTick)/1000)+" 秒前更新");}
    ImGui::Spacing();Dim("VTC 约缓存 15 分钟，服务器约每分钟更新。");
    ImGui::Separator();Dim("Truck Companion Overlay 0.2.4 · RainySY");if(ImGui::Button("作者 GitHub"))service_->OpenAuthor();
}
void Runtime::SettingsPage(){
    Label("面板设置");Dim("仅修改本面板；不会改动游戏或 TMP 配置。");
    ImGui::BeginDisabled(!editing_);
    ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##Account","TMP ID，0 表示自动读取",accountText_,sizeof accountText_,ImGuiInputTextFlags_CharsDecimal);
    ImGui::SliderInt("背景不透明度",&draft_.opacity,130,255);ImGui::SliderInt("字号",&draft_.font,12,20);
    ImGui::SliderInt("宽度",&draft_.width,430,900);ImGui::SliderInt("高度",&draft_.height,360,1200);
    if(ImGui::Button("紧凑布局 620 × 480")){draft_.width=620;draft_.height=480;}
    Dim("布局和字号在应用后生效，透明度和快捷键保留。");
    auto binding=[&](const char* label,const companion::Hotkey& key,int target,const char* id){
        Dim(label);const auto text=companion::Utf8(companion::KeyText(key))+" · 点击录制###"+id;
        if(ImGui::Button(text.c_str(),{-1,0})){BeginKeyCapture(target);ImGui::OpenPopup("录制快捷键###KeyCapture");}
    };
    binding("显示 / 隐藏面板",draft_.visible,1,"RecordVisible");
    binding("操作 / 驾驶备用键（也可直接右键切换）",draft_.edit,2,"RecordEdit");
    ImGui::TextWrapped("支持单键（如 F10、Insert）或 Ctrl / Shift / Alt 组合。请避开游戏和其他面板已使用的按键。");
    if(ImGui::BeginPopupModal("录制快捷键###KeyCapture",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        if(!recordingKey_)ImGui::CloseCurrentPopup();
        else{
            Label(recordingKey_==1?"录制：显示 / 隐藏面板":"录制：操作 / 驾驶备用键");
            Dim("请按下新按键或组合键；ESC 取消。");
            Dim("录制期间原快捷键暂停；完成后点击“应用并保存”。");
            if(!recordingError_.empty())ImGui::TextWrapped("%s",recordingError_.c_str());
            if(ImGui::Button("取消录制")){EndKeyCapture();settingsMessage_="已取消录制，原快捷键保留。";ImGui::CloseCurrentPopup();}
        }
        ImGui::EndPopup();
    }
    if(!settingsMessage_.empty())ImGui::TextWrapped("%s",settingsMessage_.c_str());
    if(ImGui::Button("应用并保存")){
        settingsMessage_.clear();
        if(!companion::ValidHotkey(draft_.visible,true)||!companion::ValidHotkey(draft_.edit,true))settingsMessage_="快捷键不可用，请重新录制。";
        else if(draft_.visible.key==draft_.edit.key&&draft_.visible.modifiers==draft_.edit.modifiers)settingsMessage_="两个操作不能使用同一个快捷键。";
        std::string id=accountText_;if(id.empty()||id.size()>20||id.find_first_not_of("0123456789")!=std::string::npos)settingsMessage_="TMP ID 只能填写数字，0 表示自动读取。";
        else try{draft_.account=std::stoull(id);}catch(...){settingsMessage_="TMP ID 超出可用范围。";}
        if(settingsMessage_.empty()){draft_.x=settings_.x;draft_.y=settings_.y;settings_=draft_;lastWidth_=lastHeight_=0;MarkDirty();message_="设置已提交后台保存。";page_=0;}
    }
    ImGui::SameLine();if(ImGui::Button("恢复默认快捷键")){draft_.visible={MOD_CONTROL|MOD_SHIFT,'V'};draft_.edit={MOD_CONTROL|MOD_SHIFT,'E'};settingsMessage_="默认键已填入；点击“应用并保存”后生效。";}
    if(ImGui::Button("重置面板位置")){settings_.x=settings_.y=INT_MIN;lastWidth_=lastHeight_=0;MarkDirty();}
    ImGui::EndDisabled();
    ImGui::Spacing();Dim(data_&&data_->savedSerial>=saveSerial_&&!dirty_?"当前设置已保存":"设置等待后台保存");
    ImGui::TextWrapped("输入文字时暂停面板快捷键，避免误触；右键或 ESC 仍可返回驾驶。支持中文输入和系统候选框。");
}
}
