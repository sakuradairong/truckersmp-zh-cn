#pragma once
#include "settings.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <map>

namespace companion {
using Microsoft::WRL::ComPtr;
inline D2D1_COLOR_F Color(unsigned rgb,float alpha=1.f) {
    return D2D1::ColorF(rgb,alpha);
}
constexpr unsigned Background=0x151a22,Surface=0x202734,Border=0x313c4c;
constexpr unsigned Foreground=0xe9eef7,Muted=0x9daec4,Accent=0xffb15c,Green=0x70d9b0;
class Canvas {
    ComPtr<ID2D1Factory> factory_;
    ComPtr<IDWriteFactory> text_;
    ComPtr<ID2D1DCRenderTarget> target_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    std::map<unsigned,ComPtr<IDWriteTextFormat>> formats_;
    HDC dc_=nullptr;HBITMAP bitmap_=nullptr;HGDIOBJ original_=nullptr;
    int width_=0,height_=0;
    void DropTarget(){brush_.Reset();target_.Reset();}
public:
    ~Canvas(){if(dc_){SelectObject(dc_,original_);if(bitmap_)DeleteObject(bitmap_);DeleteDC(dc_);}}
    bool Begin(int width,int height,UINT dpi) {
        if(!factory_&&FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory_.GetAddressOf())))return false;
        if(!text_&&FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(text_.GetAddressOf()))))return false;
        if(!dc_){dc_=CreateCompatibleDC(nullptr);if(!dc_)return false;original_=GetCurrentObject(dc_,OBJ_BITMAP);}
        if(width_!=width||height_!=height||!bitmap_){
            BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
            info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            void* pixels=nullptr;auto b=CreateDIBSection(dc_,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(!b)return false;
            SelectObject(dc_,b);if(bitmap_)DeleteObject(bitmap_);bitmap_=b;width_=width;height_=height;
        }
        if(!target_){
            auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
            if(FAILED(factory_->CreateDCRenderTarget(&props,target_.GetAddressOf())))return false;
            if(FAILED(target_->CreateSolidColorBrush(Color(Foreground),brush_.GetAddressOf()))){DropTarget();return false;}
            target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        }
        RECT rect{0,0,width,height};if(FAILED(target_->BindDC(dc_,&rect))){DropTarget();return false;}
        target_->SetDpi(static_cast<float>(dpi),static_cast<float>(dpi));target_->BeginDraw();target_->Clear(D2D1::ColorF(0,0.f));return true;
    }
    void Box(D2D1_RECT_F r,unsigned color,float radius=8.f,float alpha=1.f) {
        brush_->SetColor(Color(color,alpha));target_->FillRoundedRectangle(D2D1::RoundedRect(r,radius,radius),brush_.Get());
    }
    void Line(float x,float y,float end,unsigned color=Border) {
        brush_->SetColor(Color(color));target_->DrawLine(D2D1::Point2F(x,y),D2D1::Point2F(end,y),brush_.Get(),1.f);
    }
    void Text(const std::wstring& value,D2D1_RECT_F rect,float size=14,unsigned color=Foreground,bool bold=false) {
        if(value.empty()||rect.right<=rect.left||rect.bottom<=rect.top)return;
        unsigned key=static_cast<unsigned>(size*10)*2+(bold?1:0);auto& format=formats_[key];
        if(!format){
            if(FAILED(text_->CreateTextFormat(L"Microsoft YaHei UI",nullptr,bold?DWRITE_FONT_WEIGHT_SEMI_BOLD:DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"zh-CN",format.GetAddressOf())))return;
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        ComPtr<IDWriteTextLayout> layout;
        if(FAILED(text_->CreateTextLayout(value.c_str(),static_cast<UINT32>(value.size()),format.Get(),rect.right-rect.left,rect.bottom-rect.top,layout.GetAddressOf())))return;
        DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0};ComPtr<IDWriteInlineObject> ellipsis;
        if(SUCCEEDED(text_->CreateEllipsisTrimmingSign(format.Get(),ellipsis.GetAddressOf())))layout->SetTrimming(&trim,ellipsis.Get());
        brush_->SetColor(Color(color));target_->DrawTextLayout(D2D1::Point2F(rect.left,rect.top),layout.Get(),brush_.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    bool End(HWND window,BYTE opacity) {
        if(FAILED(target_->EndDraw())){DropTarget();return false;}
        POINT source{};SIZE size{width_,height_};BLENDFUNCTION blend{AC_SRC_OVER,0,opacity,AC_SRC_ALPHA};
        return UpdateLayeredWindow(window,nullptr,nullptr,&size,dc_,&source,0,&blend,ULW_ALPHA)!=FALSE;
    }
};
}
