#pragma once
#include "service.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <imm.h>
#include "imgui.h"

namespace overlay {
using Microsoft::WRL::ComPtr;
class Renderer {
    ImGuiContext* context_=nullptr;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> immediate_;
    std::shared_ptr<const std::vector<unsigned char>> fontData_;
    HWND window_=nullptr;
    HWND imeWindow_=nullptr;HIMC imeContext_=nullptr;
    COMPOSITIONFORM previousComposition_{};CANDIDATEFORM previousCandidate_{};
    bool savedComposition_=false,savedCandidate_=false;
    float width_=0,height_=0,clientWidth_=0,clientHeight_=0;
    std::uint64_t last_=0;
    std::uint64_t generation_=0;
    bool frame_=false,imeEditing_=false;
    static void Ime(ImGuiContext*,ImGuiViewport*,ImGuiPlatformImeData*);
    void UpdateWindow();
    void RestoreIme();
public:
    ~Renderer(){Reset();}
    bool Begin(ID3D11Device* device,std::shared_ptr<const std::vector<unsigned char>> font,int fontSize);
    void End();
    void Reset();
    ImGuiContext* Context() const {return context_;}
    std::uint64_t Generation() const {return generation_;}
    bool Focused() const;
    void ImeEditing(bool enabled){imeEditing_=enabled;if(!enabled)RestoreIme();}
    ImVec2 Mouse(int x,int y) const;
    float Width() const {return width_;}
    float Height() const {return height_;}
};
struct ContextScope {
    ImGuiContext* previous;
    explicit ContextScope(ImGuiContext* context):previous(ImGui::GetCurrentContext()){ImGui::SetCurrentContext(context);}
    ~ContextScope(){ImGui::SetCurrentContext(previous);}
};
}
