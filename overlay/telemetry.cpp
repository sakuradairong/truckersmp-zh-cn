#include "telemetry.h"
namespace overlay {
namespace {
SRWLOCK ownLock=SRWLOCK_INIT;
companion::Own own,scsData;
bool scsActive=false,registered[6]{},events[7]{};
scs_telemetry_unregister_from_channel_t removeChannel=nullptr;
scs_telemetry_unregister_from_event_t removeEvent=nullptr;
const char* channels[]={"truck.speed","truck.fuel.amount","truck.engine.gear","truck.navigation.distance","truck.navigation.speed.limit","truck.engine.rpm"};
void Publish(){scsData.active=1;scsData.tick=GetTickCount64();if(TryAcquireSRWLockExclusive(&ownLock)){own=scsData;ReleaseSRWLockExclusive(&ownLock);}}
SCSAPI_VOID Channel(const scs_string_t,const scs_u32_t,const scs_value_t* value,const scs_context_t context){
    unsigned i=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(context));if(i>=6)return;auto bit=1u<<i;scsData.fields&=~bit;if(!value)return;
    if(i==2){if(value->type==SCS_VALUE_TYPE_s32&&value->value_s32.value>=-128&&value->value_s32.value<=128){scsData.gear=value->value_s32.value;scsData.fields|=bit;}return;}
    if(value->type!=SCS_VALUE_TYPE_float||!std::isfinite(value->value_float.value))return;float v=value->value_float.value;
    switch(i){case 0:if(std::abs(v)>1000.f/3.6f)return;scsData.kmh=std::abs(v)*3.6f;break;
        case 1:if(v<0)return;scsData.fuel=v;break;case 3:if(v<0)return;scsData.navigationKm=v/1000.f;break;
        case 4:if(v<0||v>1000.f/3.6f)return;scsData.limitKmh=v*3.6f;break;case 5:if(v<0)return;scsData.rpm=v;break;default:return;}
    scsData.fields|=bit;
}
template<std::size_t N>bool Attribute(char(&text)[N],const scs_named_value_t* values,const char* name){
    if(!values)return false;for(unsigned i=0;i<256&&values[i].name;++i){const auto& v=values[i];
        if(v.index==SCS_U32_NIL&&!std::strcmp(v.name,name)&&v.value.type==SCS_VALUE_TYPE_string&&v.value.value_string.value){
            auto n=strnlen_s(v.value.value_string.value,4097);if(n>4096)return false;companion::CopyText(text,std::string_view(v.value.value_string.value,n));return true;}}
    return false;
}
SCSAPI_VOID ScsEvent(scs_event_t event,const void* value,scs_context_t){
    if(event==SCS_TELEMETRY_EVENT_started)scsData.paused=0;
    if(event==SCS_TELEMETRY_EVENT_paused)scsData.paused=1;
    if(event==SCS_TELEMETRY_EVENT_configuration){auto* c=static_cast<const scs_telemetry_configuration_t*>(value);
        if(c&&c->id&&!std::strcmp(c->id,"job")){scsData.jobFields=0;scsData.cargo[0]=scsData.from[0]=scsData.to[0]=0;
            if(Attribute(scsData.cargo,c->attributes,"cargo"))scsData.jobFields|=1;
            if(Attribute(scsData.from,c->attributes,"source.city"))scsData.jobFields|=2;
            if(Attribute(scsData.to,c->attributes,"destination.city"))scsData.jobFields|=4;}}
    if(event==SCS_TELEMETRY_EVENT_frame_end||event==SCS_TELEMETRY_EVENT_started||event==SCS_TELEMETRY_EVENT_paused)Publish();
}
}
scs_result_t StartScs(scs_u32_t version,const scs_telemetry_init_params_t* params){
    if(version!=SCS_TELEMETRY_VERSION_1_00&&version!=SCS_TELEMETRY_VERSION_1_01)return SCS_RESULT_unsupported;
    if(!params)return SCS_RESULT_invalid_parameter;if(scsActive)return SCS_RESULT_ok;
    auto* p=static_cast<const scs_telemetry_init_params_v100_t*>(params);
    if(!p->register_for_channel||!p->register_for_event||!p->unregister_from_channel||!p->unregister_from_event)return SCS_RESULT_invalid_parameter;
    scsData={};scsActive=true;removeChannel=p->unregister_from_channel;removeEvent=p->unregister_from_event;
    for(unsigned i=0;i<6;++i)registered[i]=p->register_for_channel(channels[i],SCS_U32_NIL,i==2?SCS_VALUE_TYPE_s32:SCS_VALUE_TYPE_float,SCS_TELEMETRY_CHANNEL_FLAG_each_frame|SCS_TELEMETRY_CHANNEL_FLAG_no_value,Channel,reinterpret_cast<scs_context_t>(static_cast<std::uintptr_t>(i)))==SCS_RESULT_ok;
    for(auto e:{SCS_TELEMETRY_EVENT_frame_end,SCS_TELEMETRY_EVENT_started,SCS_TELEMETRY_EVENT_paused,SCS_TELEMETRY_EVENT_configuration})events[e]=p->register_for_event(e,ScsEvent,nullptr)==SCS_RESULT_ok;
    if(!registered[0]||!events[2]||!events[3]||!events[4]){StopScs();return SCS_RESULT_generic_error;}return SCS_RESULT_ok;
}
void StopScs(){if(!scsActive)return;scsActive=false;for(unsigned i=0;i<6;++i)if(registered[i]){removeChannel(channels[i],SCS_U32_NIL,i==2?SCS_VALUE_TYPE_s32:SCS_VALUE_TYPE_float);registered[i]=false;}
    for(unsigned e=1;e<7;++e)if(events[e]){removeEvent(e);events[e]=false;}
    AcquireSRWLockExclusive(&ownLock);own={};ReleaseSRWLockExclusive(&ownLock);
}
companion::Own ReadOwn(){companion::Own result;if(TryAcquireSRWLockShared(&ownLock)){result=own;ReleaseSRWLockShared(&ownLock);}return result;}
void Players::Reset(){players_.clear();waiting_.clear();world_={};world_.active=1;world_.tick=GetTickCount64();world_.epoch=++epoch_;cursor_=-1;online_=false;dirty_=true;next_=nextRefresh_=0;}
void Players::Enter(const TruckersMP::Player& player){
    auto id=player.GetPlayerID();if(!id||*id<0)return;auto account=player.GetAccountID().value_or(0);
    auto found=players_.find(*id);if(found!=players_.end()){if(found->second.account&&account&&account!=found->second.account){found->second={};found->second.id=*id;found->second.account=account;}return;}
    if(players_.size()>=512){if(waiting_.size()<8192)waiting_.insert(*id);return;}
    auto& value=players_[*id];value.id=*id;value.account=account;waiting_.erase(*id);
}
void Players::StreamIn(const TruckersMP::Player& p){Enter(p);}
void Players::StreamOut(const TruckersMP::Player& p){
    if(auto id=p.GetPlayerID()){players_.erase(*id);waiting_.erase(*id);}
    // If the departing handle is already invalid, a debounced public snapshot
    // reconciles membership. No stored handle is used to query a vanished entity.
    dirty_=true;
}
void Players::Sample(companion::Player& out){
    const int id=out.id;auto handle=session_.Player().GetPlayerByID(id);companion::Player value;value.id=id;
    if(!handle){out=value;dirty_=true;return;}const auto& p=*handle;
    if(p.GetPlayerID().value_or(-1)!=id){out=value;dirty_=true;return;}
    if(auto v=p.GetAccountID()){value.account=*v;value.fields|=companion::Account;}
    if(auto v=p.GetUsername()){companion::CopyText(value.name,*v);value.fields|=companion::Name;}
    if(auto v=p.GetTagText()){companion::CopyText(value.tag,*v);value.fields|=companion::Tag;}
    if(auto v=p.GetNetworkLatency()){value.ping=*v;value.fields|=companion::Ping;}
    if(auto v=p.GetDistanceFromLocalPlayer();v&&std::isfinite(*v)&&*v>=0){value.distance=*v;value.fields|=companion::Distance;}
    if(p.IsTeamMember().value_or(false))value.roles|=1;if(p.IsGameModerator().value_or(false))value.roles|=2;
    if(auto vehicle=p.GetVehicle())if(auto velocity=vehicle->GetLinearVelocity()){
        const auto& v=*velocity;float speed=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z)*3.6f;
        if(std::isfinite(speed)&&speed>=0&&speed<=1000){value.kmh=speed;value.fields|=companion::Speed;}}
    value.tick=GetTickCount64();out=value;
}
void Players::Update(){
    auto now=GetTickCount64();if(now<next_)return;next_=now+100;
    bool connected=session_.Network().IsConnected().value_or(false);
    if(!connected&&online_)Reset();
    world_={};world_.active=1;world_.connected=connected;world_.tick=now;world_.epoch=epoch_;
    world_.account=session_.Account().GetAccountID().value_or(0);
    if(auto version=session_.Core().ClientVersion())companion::CopyText(world_.clientVersion,*version);
    if(!connected)return;
    if(!online_){online_=true;dirty_=true;world_.epoch=++epoch_;}
    world_.server=session_.Network().GetGameServerID().value_or(0);
    if(dirty_&&now>=nextRefresh_){
        nextRefresh_=now+5000;
        if(auto all=session_.Player().GetAllPlayers()){
            std::set<int> seen;unsigned scanned=0;
            for(const auto& p:*all){if(scanned++>=8704)break;if(auto id=p.GetPlayerID();id&&*id>=0){seen.insert(*id);Enter(p);}}
            for(auto i=players_.begin();i!=players_.end();)if(!seen.count(i->first))i=players_.erase(i);else ++i;
            for(auto i=waiting_.begin();i!=waiting_.end();)if(!seen.count(*i))i=waiting_.erase(i);else ++i;
            dirty_=false;
        }
    }
    while(players_.size()<512&&!waiting_.empty()){int id=*waiting_.begin();waiting_.erase(waiting_.begin());if(auto p=session_.Player().GetPlayerByID(id))Enter(*p);}
    if(!players_.empty()){
        auto it=players_.upper_bound(cursor_);if(it==players_.end())it=players_.begin();auto count=std::min<std::size_t>(32,players_.size());
        for(std::size_t n=0;n<count;++n){Sample(it->second);cursor_=it->first;if(++it==players_.end())it=players_.begin();}
    }
    std::vector<companion::Player> values;values.reserve(players_.size());
    const auto publishTick=GetTickCount64();world_.tick=publishTick;
    for(const auto& row:players_)if(companion::Fresh(publishTick,row.second.tick))values.push_back(row.second);
    std::stable_sort(values.begin(),values.end(),[](const auto& a,const auto& b){bool x=(a.fields&companion::Distance)!=0,y=(b.fields&companion::Distance)!=0;return x!=y?x:x&&a.distance!=b.distance?a.distance<b.distance:a.id<b.id;});
    world_.total=static_cast<unsigned>(players_.size()+waiting_.size());world_.count=static_cast<unsigned>(std::min<std::size_t>(values.size(),companion::MaxPlayers));world_.overflow=world_.total-world_.count;
    std::copy_n(values.begin(),world_.count,world_.players);
}
}
