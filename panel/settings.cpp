#include "settings.h"
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cwctype>
#include "../vendor/picojson/picojson.h"
namespace companion {
std::wstring Wide(const std::string& text){
    if(text.size()>65536)return L"";int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);if(!n)return L"";
    std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),out.data(),n);
    for(auto& c:out)if(c<32)c=L' ';return out;
}
std::string Utf8(const std::wstring& text){int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);if(!n)return {};
    std::string out(n,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),out.data(),n,nullptr,nullptr);return out;}
std::wstring Number(float value,unsigned precision){std::wostringstream out;out<<std::fixed<<std::setprecision(precision)<<value;return out.str();}
bool ParseKey(std::wstring text,Hotkey& result){
    text.erase(std::remove_if(text.begin(),text.end(),[](wchar_t c){return iswspace(c)!=0;}),text.end());
    std::transform(text.begin(),text.end(),text.begin(),[](wchar_t c){return static_cast<wchar_t>(towupper(c));});Hotkey out{0,0};
    std::wstringstream in(text);std::wstring word;
    while(std::getline(in,word,L'+')){
        if(word==L"CTRL")out.modifiers|=MOD_CONTROL;else if(word==L"SHIFT")out.modifiers|=MOD_SHIFT;else if(word==L"ALT")out.modifiers|=MOD_ALT;
        else{if(out.key)return false;if(word.size()==1&&((word[0]>='A'&&word[0]<='Z')||(word[0]>='0'&&word[0]<='9')))out.key=word[0];
            else if(word==L"SPACE")out.key=VK_SPACE;
            else if(word.size()>=2&&word.size()<=3&&word[0]=='F'){wchar_t* end=nullptr;auto v=wcstoul(word.c_str()+1,&end,10);if(*end||v<1||v>12)return false;out.key=VK_F1+static_cast<UINT>(v-1);}else return false;}
    }
    if(!out.key||!out.modifiers)return false;result=out;return true;
}
std::wstring KeyText(Hotkey key){std::wstring out;if(key.modifiers&MOD_CONTROL)out+=L"Ctrl+";if(key.modifiers&MOD_SHIFT)out+=L"Shift+";if(key.modifiers&MOD_ALT)out+=L"Alt+";
    if(key.key==VK_SPACE)out+=L"Space";else if(key.key>=VK_F1&&key.key<=VK_F12)out+=L"F"+std::to_wstring(key.key-VK_F1+1);else out+=static_cast<wchar_t>(key.key);return out;}
std::filesystem::path SettingsPath(bool overlay){PWSTR path=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&path)))throw std::runtime_error("LocalAppData unavailable");
    std::filesystem::path p=path;CoTaskMemFree(path);return p/(overlay?L"TruckCompanionOverlay":L"TruckCompanionPanel")/L"settings.json";}
namespace {
void Plain(std::filesystem::path p){for(;!p.empty();){auto attr=GetFileAttributesW(p.c_str());if(attr!=INVALID_FILE_ATTRIBUTES&&(attr&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Linked settings path refused");auto next=p.parent_path();if(next==p)break;p=next;}}
std::uint64_t Id(const picojson::value& v){if(!v.is<std::string>())return 0;const auto& s=v.get<std::string>();if(s.empty()||s.size()>20||s.find_first_not_of("0123456789")!=std::string::npos)return 0;
    try{return std::stoull(s);}catch(...){return 0;}}
}
Settings LoadSettings(std::wstring& warning,bool overlay){
    Settings s;if(overlay){s.width=620;s.height=480;}try{auto p=SettingsPath(overlay);Plain(p);if(!std::filesystem::exists(p))return s;if(std::filesystem::file_size(p)>65536)throw std::runtime_error("oversized settings");
        std::ifstream in(p,std::ios::binary);std::string data((std::istreambuf_iterator<char>(in)),{});picojson::value v;
        if(!picojson::parse(v,data).empty()||!v.is<picojson::object>())throw std::runtime_error("invalid settings");
        const auto& o=v.get<picojson::object>();auto item=[&](const char* k)->picojson::value{auto i=o.find(k);return i==o.end()?picojson::value():i->second;};
        auto integer=[&](const char* k,int fallback,int low,int high){auto x=item(k);if(!x.is<double>())return fallback;auto n=x.get<double>();return std::isfinite(n)&&n>=low&&n<=high&&std::floor(n)==n?static_cast<int>(n):fallback;};
        s.x=integer("x",INT_MIN,-100000,100000);s.y=integer("y",INT_MIN,-100000,100000);s.width=integer("width",s.width,430,900);s.height=integer("height",s.height,overlay?360:600,1200);
        s.opacity=integer("opacity",235,130,255);s.font=integer("font",14,12,20);if(item("followGame").is<bool>())s.followGame=item("followGame").get<bool>();s.account=Id(item("account"));
        if(item("visibleKey").is<std::string>())ParseKey(Wide(item("visibleKey").get<std::string>()),s.visible);
        if(item("editKey").is<std::string>())ParseKey(Wide(item("editKey").get<std::string>()),s.edit);
        if(s.visible.modifiers==s.edit.modifiers&&s.visible.key==s.edit.key){s.visible={MOD_CONTROL|MOD_SHIFT,'V'};s.edit={MOD_CONTROL|MOD_SHIFT,'E'};}
        const auto favorites=item("favorites");
        if(favorites.is<picojson::array>()){for(const auto& x:favorites.get<picojson::array>()){if(s.favorites.size()>=512)break;if(auto id=Id(x))s.favorites.insert(id);}}
    }catch(...){warning=L"面板配置读取失败，已使用默认值；原文件未修改。";}return s;
}
bool SaveSettings(const Settings& s,std::wstring& error,bool overlay){
    std::filesystem::path temp;HANDLE file=INVALID_HANDLE_VALUE;
    try{auto path=SettingsPath(overlay);Plain(path);std::filesystem::create_directories(path.parent_path());Plain(path);
        picojson::object o;auto number=[&](const char* k,int n){o[k]=picojson::value(double(n));};number("schema",1);number("x",s.x);number("y",s.y);number("width",s.width);number("height",s.height);number("opacity",s.opacity);number("font",s.font);
        o["followGame"]=picojson::value(s.followGame);o["account"]=picojson::value(std::to_string(s.account));o["visibleKey"]=picojson::value(Utf8(KeyText(s.visible)));o["editKey"]=picojson::value(Utf8(KeyText(s.edit)));
        picojson::array a;for(auto id:s.favorites)a.emplace_back(std::to_string(id));o["favorites"]=picojson::value(a);const auto data=picojson::value(o).serialize(true);
        temp=path;temp+=L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
        file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("stage failed");
        DWORD written=0;if(!WriteFile(file,data.data(),static_cast<DWORD>(data.size()),&written,nullptr)||written!=data.size()||!FlushFileBuffers(file))throw std::runtime_error("write failed");CloseHandle(file);file=INVALID_HANDLE_VALUE;
        if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("publish failed");return true;
    }catch(...){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);if(!temp.empty())DeleteFileW(temp.c_str());error=L"无法保存面板设置；请检查本机目录权限。";return false;}
}
}
