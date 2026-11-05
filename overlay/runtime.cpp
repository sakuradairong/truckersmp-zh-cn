#include "runtime.h"

namespace overlay {
namespace {
ImGuiKey GuiKey(unsigned key){
    if(key>='A'&&key<='Z')return static_cast<ImGuiKey>(ImGuiKey_A+key-'A');
    if(key>='0'&&key<='9')return static_cast<ImGuiKey>(ImGuiKey_0+key-'0');
    if(key>=VK_F1&&key<=VK_F24)return static_cast<ImGuiKey>(ImGuiKey_F1+key-VK_F1);
    if(key>=VK_NUMPAD0&&key<=VK_NUMPAD9)return static_cast<ImGuiKey>(ImGuiKey_Keypad0+key-VK_NUMPAD0);
    switch(key){
        case VK_TAB:return ImGuiKey_Tab;case VK_LEFT:return ImGuiKey_LeftArrow;case VK_RIGHT:return ImGuiKey_RightArrow;case VK_UP:return ImGuiKey_UpArrow;case VK_DOWN:return ImGuiKey_DownArrow;
        case VK_PRIOR:return ImGuiKey_PageUp;case VK_NEXT:return ImGuiKey_PageDown;case VK_HOME:return ImGuiKey_Home;case VK_END:return ImGuiKey_End;
        case VK_INSERT:return ImGuiKey_Insert;case VK_DELETE:return ImGuiKey_Delete;case VK_BACK:return ImGuiKey_Backspace;case VK_SPACE:return ImGuiKey_Space;case VK_RETURN:return ImGuiKey_Enter;case VK_ESCAPE:return ImGuiKey_Escape;
        case VK_LCONTROL:return ImGuiKey_LeftCtrl;case VK_RCONTROL:return ImGuiKey_RightCtrl;case VK_LSHIFT:return ImGuiKey_LeftShift;case VK_RSHIFT:return ImGuiKey_RightShift;
        case VK_LMENU:return ImGuiKey_LeftAlt;case VK_RMENU:return ImGuiKey_RightAlt;case VK_LWIN:return ImGuiKey_LeftSuper;case VK_RWIN:return ImGuiKey_RightSuper;
        case VK_OEM_1:return ImGuiKey_Semicolon;case VK_OEM_PLUS:return ImGuiKey_Equal;case VK_OEM_COMMA:return ImGuiKey_Comma;case VK_OEM_MINUS:return ImGuiKey_Minus;case VK_OEM_PERIOD:return ImGuiKey_Period;
        case VK_OEM_2:return ImGuiKey_Slash;case VK_OEM_3:return ImGuiKey_GraveAccent;case VK_OEM_4:return ImGuiKey_LeftBracket;case VK_OEM_5:return ImGuiKey_Backslash;case VK_OEM_6:return ImGuiKey_RightBracket;case VK_OEM_7:return ImGuiKey_Apostrophe;
        case VK_CAPITAL:return ImGuiKey_CapsLock;case VK_SCROLL:return ImGuiKey_ScrollLock;case VK_NUMLOCK:return ImGuiKey_NumLock;case VK_SNAPSHOT:return ImGuiKey_PrintScreen;case VK_PAUSE:return ImGuiKey_Pause;
        case VK_DECIMAL:return ImGuiKey_KeypadDecimal;case VK_DIVIDE:return ImGuiKey_KeypadDivide;case VK_MULTIPLY:return ImGuiKey_KeypadMultiply;case VK_SUBTRACT:return ImGuiKey_KeypadSubtract;case VK_ADD:return ImGuiKey_KeypadAdd;
        default:return ImGuiKey_None;
    }
}
unsigned VirtualKey(const TruckersMP::InputKeyEvent& event){
    unsigned key=event.GetKey();if(key==VK_SHIFT){auto code=MapVirtualKeyW(event.GetFlags()&255,MAPVK_VSC_TO_VK_EX);return code==VK_RSHIFT?VK_RSHIFT:VK_LSHIFT;}
    if(key==VK_CONTROL)return (event.GetFlags()&0x100)?VK_RCONTROL:VK_LCONTROL;
    if(key==VK_MENU)return (event.GetFlags()&0x100)?VK_RMENU:VK_LMENU;return key;
}
bool GameForeground(){DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);return pid==GetCurrentProcessId();}
bool ModifierKey(unsigned key){return key==VK_LCONTROL||key==VK_RCONTROL||key==VK_LSHIFT||key==VK_RSHIFT||key==VK_LMENU||key==VK_RMENU||key==VK_LWIN||key==VK_RWIN;}
}
bool Runtime::Start(){
    service_=Service::Start();if(!service_)return false;players_.Reset();
    session_.Network().OnConnected.Register([this]{try{if(!stopped_){players_.Reset();freight_.SessionChanged(session_);freightTarget_={};}}catch(...){}});
    session_.Network().OnDisconnected.Register([this]{try{if(!stopped_){players_.Reset();freight_.SessionChanged(session_);freightTarget_={};}}catch(...){}});
    session_.Player().OnStreamIn.Register([this](auto& event){try{if(!stopped_)players_.StreamIn(event.GetPlayer());}catch(...){}});
    session_.Player().OnStreamOut.Register([this](auto& event){try{if(!stopped_)players_.StreamOut(event.GetPlayer());}catch(...){}});
    session_.Input().OnKey.Register([this](auto& event){try{if(!stopped_)Key(event);}catch(...){LeaveEdit();}});
    session_.Input().OnChar.Register([this](auto& event){try{if(!stopped_)Character(event);}catch(...){LeaveEdit();}});
    session_.Input().OnMouseMove.Register([this](auto& event){try{if(!stopped_)MouseMove(event);}catch(...){LeaveEdit();}});
    session_.Input().OnMouseButton.Register([this](auto& event){try{if(!stopped_)MouseButton(event);}catch(...){LeaveEdit();}});
    session_.Input().OnMouseWheel.Register([this](auto& event){try{if(!stopped_)MouseWheel(event);}catch(...){LeaveEdit();}});
    session_.Render().OnPostRender.Register([this]{if(stopped_)return;try{Frame();}catch(...){
        try{freight_.Stop(session_);}catch(...){}
        LeaveEdit();renderer_.Reset();message_="绘图已暂停，未完成的货运跟踪已停止；可按显示热键重新打开。";shown_=false;freightNotice_=false;
    }});
    session_.Core().LogMessage(TruckersMP::LogLevel::Info,NativeFreight::Included()?"Truck Companion Overlay 0.2.11 initialized; optional local native freight adapter included, runtime acceptance not verified.":"Truck Companion Overlay 0.2.11 initialized; public SDK build without native freight execution.");
    return true;
}
unsigned Runtime::Modifiers() const {
    unsigned modifiers=0;if(keys_[VK_LCONTROL]||keys_[VK_RCONTROL])modifiers|=MOD_CONTROL;if(keys_[VK_LSHIFT]||keys_[VK_RSHIFT])modifiers|=MOD_SHIFT;if(keys_[VK_LMENU]||keys_[VK_RMENU])modifiers|=MOD_ALT;
    if(keys_[VK_LWIN]||keys_[VK_RWIN])modifiers|=MOD_WIN;return modifiers;
}
bool Runtime::Matches(const companion::Hotkey& key,unsigned pressed) const {return key.key==pressed&&key.modifiers==Modifiers();}
void Runtime::BeginKeyCapture(int target){
    recordingKey_=target;recordingError_.clear();settingsMessage_.clear();
    if(renderer_.Context()){ContextScope scope(renderer_.Context());ImGui::GetIO().ClearInputKeys();}
}
void Runtime::EndKeyCapture(){
    if(recordingKey_)suppressKeysUntilRelease_=std::any_of(keys_.begin(),keys_.end(),[](bool down){return down;});
    recordingKey_=0;recordingError_.clear();
    if(renderer_.Context()){ContextScope scope(renderer_.Context());ImGui::GetIO().ClearInputKeys();}
}
void Runtime::CaptureKey(unsigned key,bool edge){
    if(!edge)return;
    if(key==VK_ESCAPE){EndKeyCapture();settingsMessage_="已取消录制，原快捷键保留。";return;}
    if(ModifierKey(key))return;
    companion::Hotkey next{Modifiers(),key};
    if(!companion::ValidHotkey(next,true)){recordingError_="请选择字母、数字、F1–F24 或导航键；不支持 Windows 键和系统保留组合。";return;}
    const auto other=recordingKey_==1?draft_.edit:draft_.visible;
    if(next.key==other.key&&next.modifiers==other.modifiers){recordingError_="两个操作不能使用同一个快捷键，请重新按键。";return;}
    if(recordingKey_==1)draft_.visible=next;else draft_.edit=next;
    EndKeyCapture();settingsMessage_="已录制；点击“应用并保存”后生效。";
}
void Runtime::Key(TruckersMP::InputKeyEvent& event){
    if(!GameForeground()){LeaveEdit();keys_.fill(false);rightToggleHeld_=suppressKeysUntilRelease_=false;return;}
    if(!ready_){keys_.fill(false);return;}
    unsigned key=VirtualKey(event);if(key>=keys_.size())return;bool edge=event.GetDown()&&!keys_[key];keys_[key]=event.GetDown();
    if(recordingKey_){CaptureKey(key,edge);return;}
    if(suppressKeysUntilRelease_){suppressKeysUntilRelease_=std::any_of(keys_.begin(),keys_.end(),[](bool down){return down;});return;}
    if(edge&&key==VK_ESCAPE&&editing_){LeaveEdit();return;}
    bool typing=false;if(editing_&&renderer_.Context()){ContextScope scope(renderer_.Context());typing=ImGui::GetIO().WantTextInput;}
    if(!typing&&edge&&Matches(settings_.visible,key)){shown_=!shown_;freightNotice_=false;if(!shown_)LeaveEdit();return;}
    if(!typing&&edge&&Matches(settings_.edit,key)){freightNotice_=false;if(editing_)LeaveEdit();else{shown_=true;EnterEdit();}return;}
    if(!editing_||!renderer_.Context())return;ContextScope scope(renderer_.Context());auto& io=ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl,keys_[VK_LCONTROL]||keys_[VK_RCONTROL]);io.AddKeyEvent(ImGuiMod_Shift,keys_[VK_LSHIFT]||keys_[VK_RSHIFT]);io.AddKeyEvent(ImGuiMod_Alt,keys_[VK_LMENU]||keys_[VK_RMENU]);io.AddKeyEvent(ImGuiMod_Super,keys_[VK_LWIN]||keys_[VK_RWIN]);
    auto mapped=GuiKey(key);if(key==VK_RETURN&&(event.GetFlags()&0x100))mapped=ImGuiKey_KeypadEnter;
    if(mapped!=ImGuiKey_None)io.AddKeyEvent(mapped,event.GetDown());
}
void Runtime::Character(TruckersMP::InputCharEvent& event){
    if(!editing_||!renderer_.Context()||recordingKey_||suppressKeysUntilRelease_)return;auto c=event.GetCharacter();if(c<32||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return;
    ContextScope scope(renderer_.Context());ImGui::GetIO().AddInputCharacter(c);
}
void Runtime::MouseMove(TruckersMP::InputMouseMoveEvent& event){if(!editing_||!renderer_.Context())return;ContextScope scope(renderer_.Context());auto p=renderer_.Mouse(event.GetX(),event.GetY());ImGui::GetIO().AddMousePosEvent(p.x,p.y);}
void Runtime::MouseButton(TruckersMP::InputMouseButtonEvent& event){
    if(event.GetButton()==TruckersMP::MouseButton::Right){
        // Consume the complete click, including release after leaving edit mode.
        if(rightToggleHeld_){event.SetBlock(true);if(!event.GetDown())rightToggleHeld_=false;return;}
        if(event.GetDown()&&!event.GetBlock()&&ready_&&shown_&&renderer_.Focused()){
            bool changed=editing_;if(editing_)LeaveEdit();else changed=EnterEdit();
            if(changed){rightToggleHeld_=true;event.SetBlock(true);}return;
        }
    }
    if(!editing_||!renderer_.Context())return;int button=-1;switch(event.GetButton()){case TruckersMP::MouseButton::Left:button=0;break;case TruckersMP::MouseButton::Right:button=1;break;case TruckersMP::MouseButton::Middle:button=2;break;default:return;}
    if(event.GetBlock()&&!captured_[button])return;
    ContextScope scope(renderer_.Context());auto& io=ImGui::GetIO();auto p=io.MousePos;
    bool inside=p.x>=boundsMin_.x&&p.x<=boundsMax_.x&&p.y>=boundsMin_.y&&p.y<=boundsMax_.y;
    if(inside||io.WantCaptureMouse||captured_[button]){event.SetBlock(true);captured_[button]=event.GetDown();}
    io.AddMouseButtonEvent(button,event.GetDown());
}
void Runtime::MouseWheel(TruckersMP::InputMouseWheelEvent& event){if(!editing_||!renderer_.Context())return;ContextScope scope(renderer_.Context());ImGui::GetIO().AddMouseWheelEvent(0,static_cast<float>(event.GetDelta()));}
bool Runtime::EnterEdit(){
    if(editing_)return true;if(!renderer_.Context()||!renderer_.Focused()){message_="等待游戏窗口和绘图就绪后，再进入编辑。";return false;}
    const auto mouse=session_.Input().IsGameMouseLocked(),keyboard=session_.Input().IsGameKeyboardLocked(),cursor=session_.Input().IsMouseVisible();
    if(!mouse||!keyboard||!cursor||*mouse||*keyboard||*cursor){message_="请先关闭占用输入的游戏/TMP 界面，再右键操作。";return false;}
    using TruckersMP::Result;
    if(session_.Input().IncreaseMouseRef()!=Result::Ok){message_="暂时无法取得鼠标。";return false;}cursor_=true;
    if(session_.Input().SetGameMouseLocked(true)!=Result::Ok){LeaveEdit();return false;}mouseLock_=true;
    if(session_.Input().SetGameKeyboardLocked(true)!=Result::Ok){LeaveEdit();return false;}keyLock_=true;
    editing_=true;renderer_.ImeEditing(true);ContextScope scope(renderer_.Context());auto& io=ImGui::GetIO();io.ClearInputKeys();io.ClearInputMouse();io.AddFocusEvent(true);message_.clear();return true;
}
void Runtime::LeaveEdit(){
    EndKeyCapture();
    editing_=false;requestMode_=false;renderer_.ImeEditing(false);captured_.fill(false);
    using TruckersMP::Result;
    if(keyLock_&&session_.Input().SetGameKeyboardLocked(false)==Result::Ok)keyLock_=false;
    if(mouseLock_&&session_.Input().SetGameMouseLocked(false)==Result::Ok)mouseLock_=false;
    if(cursor_&&session_.Input().DecreaseMouseRef()==Result::Ok)cursor_=false;
    if(renderer_.Context()){ContextScope scope(renderer_.Context());auto& io=ImGui::GetIO();io.ClearInputKeys();io.ClearInputMouse();}
}
void Runtime::MarkDirty(){dirty_=true;++saveSerial_;}
void Runtime::Frame(){
    players_.Update();own_=ReadOwn();job_=ReadJob();auto now=GetTickCount64();
    if(players_.Read().epoch!=epoch_){epoch_=players_.Read().epoch;selected_=-1;selectedAccount_=0;freightTarget_={};}
    freight_.Update(session_,players_.Read(),job_,shown_&&editing_&&page_==2&&freightMode_==0&&renderer_.Focused());
    if(now>=nextRead_){nextRead_=now+200;if(auto data=service_->Read())data_=std::move(data);
        if(!ready_&&data_&&data_->ready){settings_=data_->settings;ready_=true;Draft();}}
    if(!ready_)return;
    if(dirty_&&now>=nextSave_){nextSave_=now+300;if(service_->Save(settings_,saveSerial_))dirty_=false;}
    if(!editing_&&(cursor_||mouseLock_||keyLock_))LeaveEdit();
    if(renderer_.Context()&&!renderer_.Focused()){LeaveEdit();keys_.fill(false);rightToggleHeld_=suppressKeysUntilRelease_=false;}
    if(!shown_&&!freightNotice_)return;
    if(session_.Render().GetRendererID()!=TruckersMP::RendererID::DirectX11){
        if(!rendererWarning_){session_.Core().LogMessage(TruckersMP::LogLevel::Warning,"Overlay requires the official SDK DirectX11 renderer; drawing is suspended.");rendererWarning_=true;}
        LeaveEdit();renderer_.Reset();return;
    }rendererWarning_=false;
    auto device=session_.Render().GetDeviceHandle().value_or(0);
    if(!renderer_.Begin(reinterpret_cast<ID3D11Device*>(device),data_->font,settings_.font)){LeaveEdit();return;}
    if(renderer_.Generation()!=rendererGeneration_){rendererGeneration_=renderer_.Generation();if(editing_)LeaveEdit();lastWidth_=lastHeight_=0;}
    {
        ContextScope scope(renderer_.Context());bool focus=renderer_.Focused();ImGui::GetIO().AddFocusEvent(focus);
        if(!focus){LeaveEdit();keys_.fill(false);rightToggleHeld_=suppressKeysUntilRelease_=false;}
        allowRequests_=now>=nextOffer_;if(allowRequests_)nextOffer_=now+250;
        if(shown_)Draw();else FreightNotice();
    }
    renderer_.End();
    if(freight_.EditorQueued()&&!freight_.Queued()){
        freight_.ProcessEditor(session_,players_.Read(),ReadJob(),shown_&&editing_&&page_==2&&freightMode_==0&&renderer_.Focused());
        message_=freight_.Message();
    }
    if(freight_.Queued()){
        LeaveEdit();
        const auto mouse=session_.Input().IsGameMouseLocked(),keyboard=session_.Input().IsGameKeyboardLocked(),cursor=session_.Input().IsMouseVisible();
        const bool released=renderer_.Focused()&&!cursor_&&!mouseLock_&&!keyLock_&&mouse&&keyboard&&cursor&&!*mouse&&!*keyboard&&!*cursor;
        freight_.Execute(session_,players_.Read(),ReadJob(),released);
        message_=freight_.Message();
        if(freight_.Phase()==CopyPhase::AwaitingObservation){shown_=false;freightNotice_=true;}
    }
    if(!shown_)LeaveEdit();else if(requestMode_){requestMode_=false;if(editing_)LeaveEdit();else EnterEdit();}
}
void Runtime::Shutdown() noexcept {
    if(stopped_)return;stopped_=true;
    try{freight_.Stop(session_);}catch(...){}
    try{LeaveEdit();if(cursor_||mouseLock_||keyLock_)session_.Core().LogMessage(TruckersMP::LogLevel::Warning,"Input release returned an error during overlay shutdown.");}catch(...){}
    try{if(service_&&dirty_)service_->Save(settings_,++saveSerial_);}catch(...){}
    if(service_)service_->Stop();renderer_.Reset();data_.reset();service_.reset();
}
}
