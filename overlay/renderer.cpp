#include "renderer.h"
#include "imgui_impl_dx11.h"
#include <imm.h>

namespace overlay {
void Renderer::UpdateWindow(){
    DWORD pid=0;if(window_)GetWindowThreadProcessId(window_,&pid);
    if(!IsWindow(window_)||pid!=GetCurrentProcessId()){
        struct Find{HWND window=nullptr;long long area=0;} found;
        EnumWindows([](HWND h,LPARAM data)->BOOL{DWORD id=0;GetWindowThreadProcessId(h,&id);if(id==GetCurrentProcessId()&&IsWindowVisible(h)&&!GetWindow(h,GW_OWNER)){
            RECT r{};GetClientRect(h,&r);auto size=static_cast<long long>(r.right)*r.bottom;auto& f=*reinterpret_cast<Find*>(data);if(size>f.area){f.window=h;f.area=size;}}return TRUE;},reinterpret_cast<LPARAM>(&found));
        window_=found.window;
    }
    RECT rect{};if(window_&&GetClientRect(window_,&rect)){clientWidth_=static_cast<float>(rect.right);clientHeight_=static_cast<float>(rect.bottom);}
    if(context_){ImGui::GetMainViewport()->PlatformHandle=window_;ImGui::GetMainViewport()->PlatformHandleRaw=window_;}
}
bool Renderer::Focused() const {DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);return window_&&IsWindow(window_)&&!IsIconic(window_)&&pid==GetCurrentProcessId();}
ImVec2 Renderer::Mouse(int x,int y) const {return {clientWidth_>0?x*width_/clientWidth_:static_cast<float>(x),clientHeight_>0?y*height_/clientHeight_:static_cast<float>(y)};}
void Renderer::Ime(ImGuiContext*,ImGuiViewport*,ImGuiPlatformImeData* data){
    auto* self=static_cast<Renderer*>(ImGui::GetIO().BackendPlatformUserData);
    if(!self||!self->imeEditing_||!data->WantVisible||!self->Focused()||self->width_<=0||self->height_<=0)return;
    if(auto context=ImmGetContext(self->window_)){
        if(self->imeWindow_!=self->window_||self->imeContext_!=context){
            self->RestoreIme();self->imeWindow_=self->window_;self->imeContext_=context;
            self->savedComposition_=ImmGetCompositionWindow(context,&self->previousComposition_)!=FALSE;
            self->savedCandidate_=ImmGetCandidateWindow(context,0,&self->previousCandidate_)!=FALSE;
        }
        POINT p{static_cast<LONG>(data->InputPos.x*self->clientWidth_/self->width_),static_cast<LONG>(data->InputPos.y*self->clientHeight_/self->height_)};
        COMPOSITIONFORM composition{};composition.dwStyle=CFS_FORCE_POSITION;composition.ptCurrentPos=p;ImmSetCompositionWindow(context,&composition);
        CANDIDATEFORM candidate{};candidate.dwStyle=CFS_CANDIDATEPOS;candidate.ptCurrentPos={p.x,p.y+static_cast<LONG>(data->InputLineHeight*self->clientHeight_/self->height_)};
        ImmSetCandidateWindow(context,&candidate);ImmReleaseContext(self->window_,context);
    }
}
void Renderer::RestoreIme(){
    if(imeWindow_&&IsWindow(imeWindow_)){if(auto context=ImmGetContext(imeWindow_)){
        if(context==imeContext_){if(savedComposition_)ImmSetCompositionWindow(context,&previousComposition_);if(savedCandidate_)ImmSetCandidateWindow(context,&previousCandidate_);}
        ImmReleaseContext(imeWindow_,context);
    }}
    imeWindow_=nullptr;imeContext_=nullptr;savedComposition_=savedCandidate_=false;
}
bool Renderer::Begin(ID3D11Device* device,std::shared_ptr<const std::vector<unsigned char>> font,int fontSize){
    if(!device)return false;
    if(device_.Get()!=device){Reset();device_=device;device->GetImmediateContext(immediate_.GetAddressOf());}
    if(!immediate_||FAILED(device->GetDeviceRemovedReason())){Reset();return false;}
    // Borrow the current target for this callback only. Never retain a backbuffer
    // across ResizeBuffers, and never call Present or replace the game's target.
    ComPtr<ID3D11RenderTargetView> target;immediate_->OMGetRenderTargets(1,target.GetAddressOf(),nullptr);if(!target)return false;
    ComPtr<ID3D11Resource> resource;target->GetResource(resource.GetAddressOf());ComPtr<ID3D11Texture2D> texture;
    if(FAILED(resource.As(&texture)))return false;D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    if(desc.Width<64||desc.Height<64||desc.Width>32768||desc.Height>32768)return false;
    width_=static_cast<float>(desc.Width);height_=static_cast<float>(desc.Height);
    if(!context_){
        // CreateContext preserves a previously current context. Initialize only
        // our own context, and restore the caller even if font/backend setup throws.
        ContextScope restore(ImGui::GetCurrentContext());
        context_=ImGui::CreateContext();if(!context_)return false;++generation_;
        ImGui::SetCurrentContext(context_);
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.BackendPlatformName="TruckersMP SDK";io.BackendPlatformUserData=this;
        io.ConfigFlags=ImGuiConfigFlags_None;io.ConfigInputTrickleEventQueue=true;
        fontData_=std::move(font);ImFontConfig config;config.FontDataOwnedByAtlas=false;config.OversampleH=1;config.OversampleV=1;
        if(fontData_&&!fontData_->empty())io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(fontData_->data()),static_cast<int>(fontData_->size()),static_cast<float>(fontSize),&config);
        else io.Fonts->AddFontDefault();
        ImGui::StyleColorsDark();auto& style=ImGui::GetStyle();
        style.WindowRounding=3;style.FrameRounding=2;style.ChildRounding=2;style.PopupRounding=3;style.GrabRounding=2;style.ScrollbarRounding=2;
        style.WindowPadding={10,8};style.ItemSpacing={6,5};style.FramePadding={7,4};style.CellPadding={5,3};style.ScrollbarSize=11;
        style.WindowBorderSize=1;style.FrameBorderSize=0;style.ChildBorderSize=1;
        auto& colors=style.Colors;
        colors[ImGuiCol_Text]={.94f,.94f,.94f,1};colors[ImGuiCol_TextDisabled]={.65f,.65f,.65f,1};
        colors[ImGuiCol_WindowBg]={.10f,.10f,.10f,1};colors[ImGuiCol_ChildBg]={0,0,0,0};colors[ImGuiCol_PopupBg]={.13f,.13f,.13f,.98f};
        colors[ImGuiCol_TitleBg]={.14f,.14f,.14f,1};colors[ImGuiCol_TitleBgActive]={.19f,.19f,.19f,1};colors[ImGuiCol_Border]={.40f,.40f,.40f,.5f};
        colors[ImGuiCol_FrameBg]={.23f,.23f,.23f,.95f};colors[ImGuiCol_FrameBgHovered]={.29f,.29f,.29f,1};colors[ImGuiCol_FrameBgActive]={.32f,.32f,.32f,1};
        colors[ImGuiCol_Button]={.25f,.25f,.25f,.95f};colors[ImGuiCol_ButtonHovered]={.36f,.36f,.36f,1};colors[ImGuiCol_ButtonActive]={.65f,.15f,.18f,1};
        colors[ImGuiCol_Header]={.61f,.14f,.17f,.85f};colors[ImGuiCol_HeaderHovered]={.69f,.19f,.22f,.9f};colors[ImGuiCol_HeaderActive]={.76f,.20f,.23f,1};
        colors[ImGuiCol_CheckMark]={.94f,.28f,.31f,1};colors[ImGuiCol_SliderGrab]={.82f,.23f,.26f,1};colors[ImGuiCol_SliderGrabActive]={.96f,.32f,.35f,1};
        colors[ImGuiCol_Separator]={.38f,.38f,.38f,.65f};colors[ImGuiCol_SeparatorHovered]=colors[ImGuiCol_HeaderHovered];colors[ImGuiCol_SeparatorActive]=colors[ImGuiCol_HeaderActive];
        colors[ImGuiCol_TableHeaderBg]={.22f,.22f,.22f,.95f};colors[ImGuiCol_TableBorderLight]={.37f,.37f,.37f,.35f};
        colors[ImGuiCol_TableRowBg]={0,0,0,0};colors[ImGuiCol_TableRowBgAlt]={1,1,1,.035f};colors[ImGuiCol_TextSelectedBg]={.67f,.18f,.21f,.65f};
        colors[ImGuiCol_ResizeGrip]={.6f,.6f,.6f,.2f};colors[ImGuiCol_ResizeGripHovered]=colors[ImGuiCol_HeaderHovered];colors[ImGuiCol_ResizeGripActive]=colors[ImGuiCol_HeaderActive];
        ImGui::GetPlatformIO().Platform_SetImeDataFn=&Renderer::Ime;
        bool initialized=ImGui_ImplDX11_Init(device_.Get(),immediate_.Get());
        if(!initialized){Reset();return false;}
    }
    ContextScope context(context_);UpdateWindow();if(!window_||IsIconic(window_))return false;
    auto now=GetTickCount64();auto& io=ImGui::GetIO();io.DisplaySize={width_,height_};io.DisplayFramebufferScale={1,1};io.DeltaTime=last_?std::clamp((now-last_)/1000.f,.001f,.2f):1.f/60;last_=now;
    ImGui::GetStyle().FontSizeBase=static_cast<float>(fontSize);ImGui_ImplDX11_NewFrame();ImGui::NewFrame();frame_=true;return true;
}
void Renderer::End(){if(!context_||!frame_)return;ContextScope scope(context_);ImGui::Render();frame_=false;ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());}
void Renderer::Reset(){
    imeEditing_=false;
    RestoreIme();
    if(context_){auto* previous=ImGui::GetCurrentContext();if(previous==context_)previous=nullptr;ImGui::SetCurrentContext(context_);
        if(frame_)ImGui::EndFrame();frame_=false;
        auto& io=ImGui::GetIO();
        if(io.BackendRendererUserData)ImGui_ImplDX11_Shutdown();
        // We implement the platform backend ourselves; DX11 shutdown only
        // detaches the renderer. Release our platform state before DestroyContext.
        ImGui::GetPlatformIO().Platform_SetImeDataFn=nullptr;
        auto* viewport=ImGui::GetMainViewport();
        viewport->PlatformHandle=nullptr;viewport->PlatformHandleRaw=nullptr;
        io.BackendPlatformUserData=nullptr;io.BackendPlatformName=nullptr;
        ImGui::DestroyContext(context_);context_=nullptr;ImGui::SetCurrentContext(previous);}
    immediate_.Reset();device_.Reset();fontData_.reset();window_=nullptr;width_=height_=clientWidth_=clientHeight_=0;last_=0;imeEditing_=false;
}
}
