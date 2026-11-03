#pragma once
#include "../panel/transport.h"
#include "scssdk_telemetry.h"
#include <TruckersMP/TruckersMP.hxx>
#include <map>
#include <set>
namespace overlay {
scs_result_t StartScs(scs_u32_t version,const scs_telemetry_init_params_t* params);
void StopScs();
companion::Own ReadOwn();
class Players {
    TruckersMP::Session& session_;
    std::map<int,companion::Player> players_;
    std::set<int> waiting_;
    companion::World world_;
    std::uint64_t next_=0,nextRefresh_=0,epoch_=0;
    int cursor_=-1;
    bool online_=false,dirty_=true;
    void Enter(const TruckersMP::Player& player);
    void Sample(companion::Player& out);
public:
    explicit Players(TruckersMP::Session& session):session_(session){}
    void Reset();
    void StreamIn(const TruckersMP::Player& player);
    void StreamOut(const TruckersMP::Player& player);
    void Update();
    const companion::World& Read() const {return world_;}
};
}
