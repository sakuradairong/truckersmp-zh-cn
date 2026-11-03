// Official SDK entry points only. No client image inspection or game rendering.
#include "transport.h"
#include "scssdk_telemetry.h"
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#ifdef PANEL_WITH_TMP_SDK
#include <TruckersMP/TruckersMP.hxx>
#endif

namespace {
using namespace companion;
SRWLOCK stateLock=SRWLOCK_INIT;
Snapshot state;
std::atomic<bool> scsActive{false},tmpActive{false};
std::mutex lifetime;
HANDLE worker=nullptr,stopEvent=nullptr;
unsigned references=0;
std::unique_ptr<PipeServer> service;
void Copy(Snapshot& out){
    AcquireSRWLockShared(&stateLock);out=state;ReleaseSRWLockShared(&stateLock);
    out.own.active=scsActive.load()?1:0;out.world.active=tmpActive.load()?1:0;
    if(!out.own.active)out.own={};if(!out.world.active)out.world={};
#ifdef PANEL_WITH_TMP_SDK
    out.capabilities=Scs|Tmp;
#else
    out.capabilities=Scs;
#endif
}
DWORD WINAPI Worker(void* module){
    try{while(WaitForSingleObject(stopEvent,10)==WAIT_TIMEOUT)service->Step();}catch(...){}
    service.reset();
    FreeLibraryAndExitThread(static_cast<HMODULE>(module),0);
}
bool Acquire(){
    std::lock_guard<std::mutex> guard(lifetime);
    if(references){if(WaitForSingleObject(worker,0)!=WAIT_TIMEOUT)return false;++references;return true;}
    if(worker){if(WaitForSingleObject(worker,0)!=WAIT_OBJECT_0)return false;CloseHandle(worker);worker=nullptr;CloseHandle(stopEvent);stopEvent=nullptr;}
    // Create the pipe before reporting successful initialization to either host.
    try{service=std::make_unique<PipeServer>();if(!service->Start(Copy)){service.reset();return false;}}
    catch(...){service.reset();return false;}
    stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!stopEvent){service.reset();return false;}
    HMODULE self=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&Worker),&self)){CloseHandle(stopEvent);stopEvent=nullptr;service.reset();return false;}
    worker=CreateThread(nullptr,0,Worker,self,0,nullptr);
    if(!worker){FreeLibrary(self);CloseHandle(stopEvent);stopEvent=nullptr;service.reset();return false;}
    references=1;return true;
}
void Release(){
    std::lock_guard<std::mutex> guard(lifetime);
    if(!references||--references)return;SetEvent(stopEvent);
    // The worker owns a module reference if cancellation needs longer.
    if(WaitForSingleObject(worker,2000)==WAIT_OBJECT_0){CloseHandle(worker);worker=nullptr;CloseHandle(stopEvent);stopEvent=nullptr;}
}
Own scsData;
scs_telemetry_unregister_from_channel_t removeChannel=nullptr;
scs_telemetry_unregister_from_event_t removeEvent=nullptr;
const char* channels[]={"truck.speed","truck.fuel.amount","truck.engine.gear","truck.navigation.distance","truck.navigation.speed.limit","truck.engine.rpm"};
bool registered[6]{};bool events[7]{};
void PublishOwn(){scsData.tick=GetTickCount64();scsData.active=1;
    if(TryAcquireSRWLockExclusive(&stateLock)){state.own=scsData;ReleaseSRWLockExclusive(&stateLock);}}
SCSAPI_VOID Channel(const scs_string_t, const scs_u32_t,const scs_value_t* value,const scs_context_t context){
    const auto index=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(context));if(index>=6)return;
    const auto bit=1u<<index;scsData.fields&=~bit;if(!value)return;
    if(index==2){if(value->type==SCS_VALUE_TYPE_s32){scsData.gear=value->value_s32.value;scsData.fields|=bit;}return;}
    if(value->type!=SCS_VALUE_TYPE_float||!std::isfinite(value->value_float.value))return;
    const float v=value->value_float.value;
    switch(index){case 0:if(std::abs(v)>1000.f/3.6f)return;scsData.kmh=std::abs(v)*3.6f;break;
        case 1:if(v<0)return;scsData.fuel=v;break;case 3:if(v<0)return;scsData.navigationKm=v/1000.f;break;
        case 4:if(v<0||v>1000.f/3.6f)return;scsData.limitKmh=v*3.6f;break;case 5:if(v<0)return;scsData.rpm=v;break;default:return;}
    scsData.fields|=bit;
}
template<std::size_t N>bool Attribute(char(&out)[N],const scs_named_value_t* values,const char* key){
    if(!values)return false;for(unsigned i=0;i<256&&values[i].name;++i){const auto& a=values[i];
        if(a.index==SCS_U32_NIL&&!std::strcmp(a.name,key)&&a.value.type==SCS_VALUE_TYPE_string&&a.value.value_string.value){
            const auto* s=a.value.value_string.value;const auto n=strnlen_s(s,4097);if(n>4096)return false;CopyText(out,std::string_view(s,n));return true;}}
    return false;
}
SCSAPI_VOID ScsEvent(const scs_event_t event,const void* info,const scs_context_t){
    if(event==SCS_TELEMETRY_EVENT_paused)scsData.paused=1;
    else if(event==SCS_TELEMETRY_EVENT_started)scsData.paused=0;
    else if(event==SCS_TELEMETRY_EVENT_configuration){
        const auto* c=static_cast<const scs_telemetry_configuration_t*>(info);
        if(c&&c->id&&!std::strcmp(c->id,"job")){
            scsData.jobFields=0;std::memset(scsData.cargo,0,sizeof scsData.cargo);std::memset(scsData.from,0,sizeof scsData.from);std::memset(scsData.to,0,sizeof scsData.to);
            if(Attribute(scsData.cargo,c->attributes,"cargo"))scsData.jobFields|=1;
            if(Attribute(scsData.from,c->attributes,"source.city"))scsData.jobFields|=2;
            if(Attribute(scsData.to,c->attributes,"destination.city"))scsData.jobFields|=4;
        }
    }
    if(event==SCS_TELEMETRY_EVENT_frame_end||event==SCS_TELEMETRY_EVENT_paused||event==SCS_TELEMETRY_EVENT_started)PublishOwn();
}
#ifdef PANEL_WITH_TMP_SDK
using namespace TruckersMP;
std::unique_ptr<Session> session;
struct Entry {TruckersMP::Player handle;companion::Player value;};
std::map<std::uint64_t,Entry> players;
std::map<std::uint64_t,TruckersMP::Player> waiting;
std::uint64_t epoch=0,cursor=0;
bool online=false,seed=true;
ULONGLONG nextSample=0,nextSeed=0;
void ReadPlayer(Entry& entry){
    const auto& p=entry.handle;companion::Player v;v.id=p.GetPlayerID().value_or(-1);if(v.id<0){entry.value=v;return;}
    if(auto x=p.GetUsername()){CopyText(v.name,*x);v.fields|=Name;}
    if(auto x=p.GetAccountID()){v.account=*x;v.fields|=Account;}
    if(auto x=p.GetTagText()){CopyText(v.tag,*x);v.fields|=Tag;}
    if(auto x=p.GetNetworkLatency()){v.ping=*x;v.fields|=Ping;}
    if(auto x=p.GetDistanceFromLocalPlayer();x&&std::isfinite(*x)&&*x>=0){v.distance=*x;v.fields|=Distance;}
    if(p.IsTeamMember().value_or(false))v.roles|=1;if(p.IsGameModerator().value_or(false))v.roles|=2;
    if(auto vehicle=p.GetVehicle())if(auto velocity=vehicle->GetLinearVelocity()){
        const auto& x=*velocity;const float speed=std::sqrt(x.x*x.x+x.y*x.y+x.z*x.z)*3.6f;
        if(std::isfinite(speed)&&speed>=0&&speed<=1000){v.kmh=speed;v.fields|=Speed;}}
    v.tick=GetTickCount64();entry.value=v;
}
void Enter(TruckersMP::Player p){
    const auto key=p.RawHandle().m_value;if(!key||players.count(key))return;
    if(players.size()>=512){if(waiting.size()<8192)waiting.emplace(key,p);return;}
    waiting.erase(key);
    Entry e;e.handle=p;players.emplace(key,std::move(e));
}
void ResetWorld(){
    players.clear();waiting.clear();cursor=0;online=false;seed=true;nextSeed=0;++epoch;
    if(TryAcquireSRWLockExclusive(&stateLock)){state.world={};state.world.active=1;state.world.epoch=epoch;state.world.tick=GetTickCount64();ReleaseSRWLockExclusive(&stateLock);}
}
void Sample(){
    const auto now=GetTickCount64();if(!session||now<nextSample)return;nextSample=now+100;
    const bool connected=session->Network().IsConnected().value_or(false);
    if(!connected&&online)ResetWorld();
    World out;out.active=1;out.connected=connected;out.tick=now;out.epoch=epoch;
    if(auto x=session->Core().ClientVersion())CopyText(out.clientVersion,*x);
    out.account=session->Account().GetAccountID().value_or(0);
    if(connected){
        if(!online){online=true;seed=true;++epoch;out.epoch=epoch;}
        out.server=session->Network().GetGameServerID().value_or(0);
        if(seed&&now>=nextSeed){nextSeed=now+5000;if(auto all=session->Player().GetAllPlayers()){seed=false;for(const auto& p:*all)Enter(p);}}
        // Promote still-streamed handles as capacity becomes available; no full-world polling.
        while(players.size()<512&&!waiting.empty()){auto p=waiting.begin()->second;waiting.erase(waiting.begin());Enter(p);}
        if(!players.empty()){
            auto it=players.upper_bound(cursor);if(it==players.end())it=players.begin();
            const auto budget=(std::min)(std::size_t(32),players.size());
            for(std::size_t n=0;n<budget;++n){ReadPlayer(it->second);cursor=it->first;if(++it==players.end())it=players.begin();}
        }
        std::vector<companion::Player> values;values.reserve(players.size());
        for(const auto& row:players)if(row.second.value.id>=0)values.push_back(row.second.value);
        std::stable_sort(values.begin(),values.end(),[](const auto& a,const auto& b){
            const bool ak=(a.fields&Distance)!=0,bk=(b.fields&Distance)!=0;
            return ak!=bk?ak:ak&&a.distance!=b.distance?a.distance<b.distance:a.id<b.id;});
        out.total=static_cast<unsigned>(players.size()+waiting.size());out.count=static_cast<unsigned>((std::min)(values.size(),std::size_t(MaxPlayers)));
        out.overflow=out.total-out.count;std::copy_n(values.begin(),out.count,out.players);
    }
    if(TryAcquireSRWLockExclusive(&stateLock)){state.world=out;ReleaseSRWLockExclusive(&stateLock);}
}
#endif
}

SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version,const scs_telemetry_init_params_t* params){
    if(version!=SCS_TELEMETRY_VERSION_1_00&&version!=SCS_TELEMETRY_VERSION_1_01)return SCS_RESULT_unsupported;
    if(!params)return SCS_RESULT_invalid_parameter;if(scsActive.load())return SCS_RESULT_ok;
    const auto* p=static_cast<const scs_telemetry_init_params_v100_t*>(params);
    if(!p->register_for_channel||!p->register_for_event||!p->unregister_from_channel||!p->unregister_from_event)return SCS_RESULT_invalid_parameter;
    if(!Acquire())return SCS_RESULT_generic_error;
    removeChannel=p->unregister_from_channel;removeEvent=p->unregister_from_event;scsData={};scsActive=true;
    for(unsigned i=0;i<6;++i)registered[i]=p->register_for_channel(channels[i],SCS_U32_NIL,i==2?SCS_VALUE_TYPE_s32:SCS_VALUE_TYPE_float,
        SCS_TELEMETRY_CHANNEL_FLAG_each_frame|SCS_TELEMETRY_CHANNEL_FLAG_no_value,Channel,reinterpret_cast<scs_context_t>(static_cast<std::uintptr_t>(i)))==SCS_RESULT_ok;
    for(auto e:{SCS_TELEMETRY_EVENT_frame_end,SCS_TELEMETRY_EVENT_paused,SCS_TELEMETRY_EVENT_started,SCS_TELEMETRY_EVENT_configuration})events[e]=p->register_for_event(e,ScsEvent,nullptr)==SCS_RESULT_ok;
    if(!registered[0]||!events[SCS_TELEMETRY_EVENT_frame_end]||!events[SCS_TELEMETRY_EVENT_paused]||!events[SCS_TELEMETRY_EVENT_started]){scs_telemetry_shutdown();return SCS_RESULT_generic_error;}
    return SCS_RESULT_ok;
}
SCSAPI_VOID scs_telemetry_shutdown(){
    if(!scsActive.exchange(false))return;
    for(unsigned i=0;i<6;++i)if(registered[i]){removeChannel(channels[i],SCS_U32_NIL,i==2?SCS_VALUE_TYPE_s32:SCS_VALUE_TYPE_float);registered[i]=false;}
    for(unsigned e=1;e<7;++e)if(events[e]){removeEvent(e);events[e]=false;}
    Release();
}
#ifdef PANEL_WITH_TMP_SDK
TMP_EXPORT bool TMP_API truckersmp_init(const TruckersMP_Host* host,TruckersMP_PluginDesc* desc){
    if(!host||!desc||host->m_size<sizeof *host||desc->m_size<sizeof *desc||session||!host->m_ctx||!host->DeclareVersion||!host->GetApi||!host->Free)return false;
    bool acquired=false;
    try{
        PluginInfo info;info.m_name="Truck Companion data";info.m_author="RainySY";info.m_version="0.1.0";
        info.m_description="Read-only public SDK snapshots for a separate desktop window; no game rendering or private intents.";FillPluginDesc(desc,info);
        session=Session::Create(host);if(!session)return false;
        if(!session->Player().IsAvailable()||!session->Network().IsAvailable()||!session->Render().IsAvailable()||!session->Account().IsAvailable()||!session->Vehicle().IsAvailable()){session.reset();return false;}
        if(!Acquire()){session.reset();return false;}acquired=true;
        ResetWorld();tmpActive=true;nextSample=0;
        session->Network().OnConnected.Register([]{try{ResetWorld();}catch(...){}});
        session->Network().OnDisconnected.Register([]{try{ResetWorld();}catch(...){}});
        session->Player().OnStreamIn.Register([](PlayerStreamInEvent& e){try{Enter(e.GetPlayer());}catch(...){}});
        session->Player().OnStreamOut.Register([](PlayerStreamOutEvent& e){try{const auto key=e.GetPlayer().RawHandle().m_value;players.erase(key);waiting.erase(key);}catch(...){}});
        session->Render().OnPostRender.Register([]{try{Sample();}catch(...){}});
        return true;
    }catch(...){tmpActive=false;session.reset();if(acquired)Release();return false;}
}
TMP_EXPORT void TMP_API truckersmp_shutdown(){
    if(!tmpActive.exchange(false))return;session.reset();players.clear();waiting.clear();online=false;seed=true;Release();
}
#endif
