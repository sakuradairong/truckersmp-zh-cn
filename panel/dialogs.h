#pragma once
#include "settings.h"
namespace companion {
struct DialogInput {
    bool search=false;std::wstring query;Settings settings;HFONT font=nullptr;
};
inline std::wstring ControlText(HWND dialog,int id) {
    auto control=GetDlgItem(dialog,id);int count=GetWindowTextLengthW(control);if(count<0||count>1024)return {};
    std::wstring text(static_cast<std::size_t>(count)+1,0);GetWindowTextW(control,text.data(),count+1);text.resize(count);return text;
}
inline bool DialogInteger(HWND dialog,int id,int low,int high,int& value) {
    auto text=ControlText(dialog,id);if(text.empty()||text.find_first_not_of(L"0123456789")!=std::wstring::npos)return false;
    try{auto n=std::stoi(text);if(n<low||n>high)return false;value=n;return true;}catch(...){return false;}
}
inline INT_PTR CALLBACK OptionsProc(HWND dialog,UINT message,WPARAM w,LPARAM l) {
    auto* input=reinterpret_cast<DialogInput*>(GetWindowLongPtrW(dialog,DWLP_USER));
    if(message==WM_INITDIALOG){
        input=reinterpret_cast<DialogInput*>(l);SetWindowLongPtrW(dialog,DWLP_USER,l);
        const auto dpi=GetDpiForWindow(dialog);const auto px=[&](int n){return MulDiv(n,static_cast<int>(dpi),96);};
        input->font=CreateFontW(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        const int height=input->search?180:470;
        RECT size{0,0,px(490),px(height)};AdjustWindowRectExForDpi(&size,WS_POPUP|WS_CAPTION|WS_SYSMENU,0,WS_EX_DLGMODALFRAME,dpi);
        RECT owner{};GetWindowRect(GetParent(dialog),&owner);auto monitor=MonitorFromWindow(GetParent(dialog),MONITOR_DEFAULTTONEAREST);MONITORINFO mi{sizeof mi};GetMonitorInfoW(monitor,&mi);
        const int width=size.right-size.left,h=size.bottom-size.top;
        const int x=(std::max)(mi.rcWork.left,(std::min)(owner.left,mi.rcWork.right-width));
        const int y=(std::max)(mi.rcWork.top,(std::min)(owner.top,mi.rcWork.bottom-h));
        SetWindowPos(dialog,HWND_TOPMOST,x,y,width,h,0);
        auto control=[&](LPCWSTR kind,const std::wstring& text,int id,int cx,int cy,int cw,int ch,DWORD style=0){
            auto child=CreateWindowExW(!wcscmp(kind,L"EDIT")?WS_EX_CLIENTEDGE:0,kind,text.c_str(),WS_CHILD|WS_VISIBLE|style,px(cx),px(cy),px(cw),px(ch),dialog,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
            SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(input->font),TRUE);if(!wcscmp(kind,L"EDIT"))SendMessageW(child,EM_SETLIMITTEXT,256,0);return child;
        };
        if(input->search){
            SetWindowTextW(dialog,L"搜索附近玩家");control(L"STATIC",L"昵称、TMP ID、游戏内 ID 或自定义标签",0,20,20,450,24);
            control(L"EDIT",input->query,201,20,55,450,30,WS_TABSTOP|ES_AUTOHSCROLL);
            control(L"STATIC",L"留空后应用，可清除搜索条件。",0,20,94,450,24);
        }else{
            SetWindowTextW(dialog,L"面板设置");
            auto row=[&](const wchar_t* label,const std::wstring& value,int id,int y){control(L"STATIC",label,0,20,y+4,220,24);control(L"EDIT",value,id,242,y,228,28,WS_TABSTOP|ES_AUTOHSCROLL);};
            row(L"TMP 账号 ID（0 = 自动读取）",std::to_wstring(input->settings.account),201,20);
            row(L"不透明度（130–255）",std::to_wstring(input->settings.opacity),202,60);
            row(L"列表字号（12–20）",std::to_wstring(input->settings.font),203,100);
            row(L"显示 / 隐藏快捷键",KeyText(input->settings.visible),204,140);
            row(L"驾驶 / 编辑快捷键",KeyText(input->settings.edit),205,180);
            row(L"面板宽度（430–900）",std::to_wstring(input->settings.width),206,220);
            row(L"面板高度（600–1200）",std::to_wstring(input->settings.height),207,260);
            auto check=control(L"BUTTON",L"驾驶模式仅在游戏处于前台时显示",208,20,306,450,28,WS_TABSTOP|BS_AUTOCHECKBOX);
            SendMessageW(check,BM_SETCHECK,input->settings.followGame?BST_CHECKED:BST_UNCHECKED,0);
            control(L"STATIC",L"快捷键示例：Ctrl+Shift+V、Alt+F8。",0,20,347,450,24);
            control(L"STATIC",L"仅修改面板自身设置；不改变游戏或 TMP 设置。",0,20,376,450,24);
        }
        control(L"BUTTON",L"应用",IDOK,278,height-48,90,30,WS_TABSTOP|BS_DEFPUSHBUTTON);
        control(L"BUTTON",L"取消",IDCANCEL,380,height-48,90,30,WS_TABSTOP|BS_PUSHBUTTON);
        SetFocus(GetDlgItem(dialog,201));SendDlgItemMessageW(dialog,201,EM_SETSEL,0,-1);return FALSE;
    }
    if(message==WM_COMMAND&&input){
        if(LOWORD(w)==IDCANCEL){EndDialog(dialog,IDCANCEL);return TRUE;}
        if(LOWORD(w)==IDOK){
            if(input->search){input->query=ControlText(dialog,201);EndDialog(dialog,IDOK);return TRUE;}
            auto& s=input->settings;bool valid=DialogInteger(dialog,202,130,255,s.opacity)&&DialogInteger(dialog,203,12,20,s.font)&&
                DialogInteger(dialog,206,430,900,s.width)&&DialogInteger(dialog,207,600,1200,s.height)&&ParseKey(ControlText(dialog,204),s.visible)&&ParseKey(ControlText(dialog,205),s.edit);
            const auto id=ControlText(dialog,201);
            if(id.empty()||id.size()>20||id.find_first_not_of(L"0123456789")!=std::wstring::npos)valid=false;
            else try{s.account=std::stoull(id);}catch(...){valid=false;}
            if(s.visible.key==s.edit.key&&s.visible.modifiers==s.edit.modifiers)valid=false;
            if(!valid){MessageBoxW(dialog,L"请检查数值范围、账号 ID 和快捷键格式。两个快捷键不能相同。",L"设置未应用",MB_OK|MB_ICONINFORMATION);return TRUE;}
            s.followGame=SendDlgItemMessageW(dialog,208,BM_GETCHECK,0,0)==BST_CHECKED;EndDialog(dialog,IDOK);return TRUE;
        }
    }
    if(message==WM_CLOSE){EndDialog(dialog,IDCANCEL);return TRUE;}
    return FALSE;
}
inline bool Options(HWND owner,DialogInput& input) {
    struct EmptyDialog {DLGTEMPLATE header;WORD menu=0,windowClass=0,title=0;};
    EmptyDialog data{};data.header.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;data.header.dwExtendedStyle=WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT;
    data.header.cx=320;data.header.cy=300;
    auto result=DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&data.header,owner,OptionsProc,reinterpret_cast<LPARAM>(&input));
    if(input.font){DeleteObject(input.font);input.font=nullptr;}return result==IDOK;
}
}
