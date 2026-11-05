#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace companion {
constexpr std::uint32_t MaxPlayers=256;
enum PlayerField : std::uint32_t { Name=1,Account=2,Ping=4,Distance=8,Speed=16,Tag=32 };
enum OwnField : std::uint32_t { OwnSpeed=1,Fuel=2,Gear=4,Navigation=8,Limit=16,Rpm=32 };
struct Player {
    std::int32_t id=-1;
    std::uint32_t fields=0,roles=0,ping=0;
    std::uint64_t account=0,tick=0;
    float distance=0,kmh=0;
    char name[96]{},tag[64]{};
};
struct Own {
    std::uint32_t active=0,paused=1,fields=0,jobFields=0;
    std::uint64_t tick=0;
    float kmh=0,fuel=0,navigationKm=0,limitKmh=0,rpm=0;
    std::int32_t gear=0;
    char cargo[96]{},from[64]{},to[64]{};
};
struct World {
    std::uint32_t active=0,connected=0,count=0,total=0,overflow=0,reserved=0;
    std::uint64_t tick=0,epoch=0,account=0,server=0;
    char clientVersion[48]{};
    Player players[MaxPlayers]{};
};
template<std::size_t N>inline void CopyText(char(&out)[N],std::string_view text) {
    auto n=(std::min)(text.size(),N-1);
    while(n<text.size()&&n&&(static_cast<unsigned char>(text[n])&0xc0)==0x80)--n;
    std::memset(out,0,N);std::memcpy(out,text.data(),n);
}
inline bool Fresh(std::uint64_t now,std::uint64_t tick,std::uint64_t maxAge=3000) {return tick&&now>=tick&&now-tick<=maxAge;}
}
