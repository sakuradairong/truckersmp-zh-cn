#include "runtime.h"
namespace {
// Explicit SDK ownership: never call host APIs from CRT/DllMain destruction.
TruckersMP::Session* session=nullptr;
overlay::Runtime* runtime=nullptr;
}
SCSAPI_RESULT scs_telemetry_init(scs_u32_t version,const scs_telemetry_init_params_t* params){try{return overlay::StartScs(version,params);}catch(...){overlay::StopScs();return SCS_RESULT_generic_error;}}
SCSAPI_VOID scs_telemetry_shutdown(){overlay::StopScs();}
TMP_EXPORT bool TMP_API truckersmp_init(const TruckersMP_Host* host,TruckersMP_PluginDesc* desc){
    if(!host||!desc||host->m_size<sizeof *host||desc->m_size<sizeof *desc||!host->m_ctx||!host->DeclareVersion||!host->GetApi||!host->Free||session)return false;
    std::unique_ptr<TruckersMP::Session> nextSession;std::unique_ptr<overlay::Runtime> nextRuntime;
    try{
        TruckersMP::PluginInfo info;info.m_name="Truck Companion Overlay";info.m_author="RainySY";info.m_version="0.2.2";
        info.m_description="Self-drawn DX11 companion UI through the public Render/Input SDK; no private client hooks or resource overrides.";TruckersMP::FillPluginDesc(desc,info);
        nextSession=TruckersMP::Session::Create(host);if(!nextSession)return false;
        if(!nextSession->Render().IsAvailable()||!nextSession->Input().IsAvailable()||!nextSession->Player().IsAvailable()||!nextSession->Vehicle().IsAvailable()||!nextSession->Network().IsAvailable()||!nextSession->Account().IsAvailable())return false;
        nextRuntime=std::make_unique<overlay::Runtime>(*nextSession);if(!nextRuntime->Start()){nextRuntime->Shutdown();nextSession.reset();nextRuntime.reset();return false;}
        session=nextSession.release();runtime=nextRuntime.release();return true;
    }catch(...){if(nextRuntime)nextRuntime->Shutdown();nextSession.reset();nextRuntime.reset();return false;}
}
TMP_EXPORT void TMP_API truckersmp_shutdown(){
    auto* oldRuntime=runtime;auto* oldSession=session;runtime=nullptr;session=nullptr;
    if(oldRuntime)oldRuntime->Shutdown();delete oldSession;delete oldRuntime;
}
