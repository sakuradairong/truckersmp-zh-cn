#pragma once
#include "transport.h"
#include <filesystem>
#include <set>
namespace companion {
std::wstring Wide(const std::string& text);
std::string Utf8(const std::wstring& text);
std::wstring Number(float value,unsigned precision=0);
struct Hotkey {UINT modifiers=MOD_CONTROL|MOD_SHIFT,key='V';};
bool ParseKey(std::wstring text,Hotkey& result);
std::wstring KeyText(Hotkey key);
struct Settings {
    int x=INT_MIN,y=INT_MIN,width=470,height=700,opacity=235,font=14;
    bool followGame=true;
    std::uint64_t account=0;
    Hotkey visible{},edit{MOD_CONTROL|MOD_SHIFT,'E'};
    std::set<std::uint64_t> favorites;
};
std::filesystem::path SettingsPath(bool overlay=false);
Settings LoadSettings(std::wstring& warning,bool overlay=false);
bool SaveSettings(const Settings& settings,std::wstring& error,bool overlay=false);
}
