#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

namespace companion {
constexpr std::uint32_t Magic=0x31504354,Version=1,MaxPlayers=256;
enum Capability : std::uint32_t { Scs=1,Tmp=2 };
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
struct Snapshot {
    std::uint32_t magic=Magic,version=Version,bytes=sizeof(Snapshot),pid=0;
    std::uint32_t capabilities=Scs,reserved=0;
    std::uint64_t tick=0;
    Own own{};
    World world{};
};
struct Query {
    std::uint32_t magic=Magic,version=Version,bytes=sizeof(Query),pid=0,client=0,reserved=0;
};
static_assert(std::is_trivially_copyable_v<Snapshot> && sizeof(Snapshot)<100000);
template<std::size_t N>inline void CopyText(char(&out)[N],std::string_view text) {
    auto n=(std::min)(text.size(),N-1);
    while(n<text.size()&&n&&(static_cast<unsigned char>(text[n])&0xc0)==0x80)--n;
    std::memset(out,0,N);std::memcpy(out,text.data(),n);
}
template<std::size_t N>inline bool Terminated(const char(&s)[N]) {return std::memchr(s,0,N)!=nullptr;}
inline bool Fresh(std::uint64_t now,std::uint64_t tick,std::uint64_t maxAge=3000) {return tick&&now>=tick&&now-tick<=maxAge;}
inline bool Valid(const Snapshot& s,std::uint32_t pid,std::uint64_t now) {
    if(s.magic!=Magic||s.version!=Version||s.bytes!=sizeof s||s.pid!=pid||s.reserved||(s.capabilities&~3u)||!Fresh(now,s.tick,5000))return false;
    const auto& o=s.own;const auto& w=s.world;
    if(o.active>1||o.paused>1||(o.fields&~63u)||(o.jobFields&~7u)||!Terminated(o.cargo)||!Terminated(o.from)||!Terminated(o.to))return false;
    if(!std::isfinite(o.kmh)||!std::isfinite(o.fuel)||!std::isfinite(o.navigationKm)||!std::isfinite(o.limitKmh)||!std::isfinite(o.rpm))return false;
    if(o.kmh<0||o.kmh>1000||o.fuel<0||o.navigationKm<0||o.limitKmh<0)return false;
    if(w.active>1||w.connected>1||w.count>MaxPlayers||w.reserved||w.total<w.count||!Terminated(w.clientVersion))return false;
    for(unsigned i=0;i<w.count;++i){const auto& p=w.players[i];
        if(p.id<0||(p.fields&~63u)||(p.roles&~3u)||!Terminated(p.name)||!Terminated(p.tag)||
           !std::isfinite(p.distance)||p.distance<0||!std::isfinite(p.kmh)||p.kmh<0||p.kmh>1000)return false;
    }return true;
}
}
