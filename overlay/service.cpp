#include "service.h"
#include <winhttp.h>
#include <shellapi.h>
#include <objbase.h>
#include <fstream>
#include <set>
#include "../vendor/picojson/picojson.h"

namespace overlay {
namespace {
std::atomic<std::uint64_t> configOwner{0};
SRWLOCK configIo=SRWLOCK_INIT; // Worker threads only; game callbacks never acquire it.
struct ConfigGuard {ConfigGuard(){AcquireSRWLockExclusive(&configIo);}~ConfigGuard(){ReleaseSRWLockExclusive(&configIo);}};
struct Internet {HINTERNET h=nullptr;~Internet(){if(h)WinHttpCloseHandle(h);}};
struct Reply {DWORD status=0,retry=60;std::string body;};
bool Stopped(HANDLE event){return WaitForSingleObject(event,0)==WAIT_OBJECT_0;}
Reply Get(const std::wstring& path,HANDLE stop){
    Reply out;if(Stopped(stop))return out;
    Internet session{WinHttpOpen(L"TruckCompanionOverlay/0.2.4",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
    if(!session.h)return out;WinHttpSetTimeouts(session.h,2000,2000,2000,2000);
    Internet connection{WinHttpConnect(session.h,L"api.truckersmp.com",INTERNET_DEFAULT_HTTPS_PORT,0)};if(!connection.h||Stopped(stop))return out;
    Internet request{WinHttpOpenRequest(connection.h,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE)};if(!request.h)return out;
    DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if(!WinHttpSetOption(request.h,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof redirects))return out;
    if(Stopped(stop)||!WinHttpSendRequest(request.h,L"Accept: application/json\r\n",DWORD(-1),nullptr,0,0,0))return out;
    if(Stopped(stop)||!WinHttpReceiveResponse(request.h,nullptr))return out;
    DWORD size=sizeof out.status;WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&out.status,&size,nullptr);
    if(out.status==429){
        wchar_t value[128]{};size=sizeof value;
        if(WinHttpQueryHeaders(request.h,WINHTTP_QUERY_CUSTOM,L"Retry-After",value,&size,nullptr)){
            wchar_t* end=nullptr;auto n=wcstoul(value,&end,10);
            if(end!=value&&!*end)out.retry=std::clamp(n,60ul,3600ul);
            else {SYSTEMTIME time{};FILETIME deadline{},current{};if(WinHttpTimeToSystemTime(value,&time)&&SystemTimeToFileTime(&time,&deadline)){
                GetSystemTimeAsFileTime(&current);ULARGE_INTEGER a{},b{};a.LowPart=deadline.dwLowDateTime;a.HighPart=deadline.dwHighDateTime;b.LowPart=current.dwLowDateTime;b.HighPart=current.dwHighDateTime;
                if(a.QuadPart>b.QuadPart)out.retry=static_cast<DWORD>(std::clamp((a.QuadPart-b.QuadPart)/10000000,60ull,3600ull));}}
        }
    }
    if(out.status!=200)return out;
    const auto start=GetTickCount64();char buffer[8192];
    for(;;){
        if(Stopped(stop)||GetTickCount64()-start>10000){out.status=0;out.body.clear();break;}
        DWORD count=0;if(!WinHttpReadData(request.h,buffer,sizeof buffer,&count)){out.status=0;out.body.clear();break;}
        if(!count)break;if(out.body.size()+count>1024*1024){out.status=0;out.body.clear();break;}out.body.append(buffer,count);
    }
    return out;
}
const picojson::value& Field(const picojson::value& v,const char* key){static const picojson::value empty;if(!v.is<picojson::object>())return empty;auto i=v.get<picojson::object>().find(key);return i==v.get<picojson::object>().end()?empty:i->second;}
std::uint64_t Id(const picojson::value& v){
    if(v.is<double>()){auto n=v.get<double>();if(std::isfinite(n)&&n>=0&&n<=9007199254740991.&&std::floor(n)==n)return static_cast<std::uint64_t>(n);}
    if(v.is<std::string>()){const auto& s=v.get<std::string>();if(s.empty()||s.size()>20||s.find_first_not_of("0123456789")!=std::string::npos)return 0;try{return std::stoull(s);}catch(...){}}return 0;
}
std::string Text(const picojson::value& v){if(!v.is<std::string>()||v.get<std::string>().size()>1024)return {};return companion::Utf8(companion::Wide(v.get<std::string>()));}
bool Good(const Reply& reply,picojson::value& out){if(reply.status!=200||!picojson::parse(out,reply.body).empty())return false;const auto& error=Field(out,"error");return error.is<bool>()&&!error.get<bool>();}
std::shared_ptr<const std::vector<unsigned char>> Font(){
    wchar_t windows[MAX_PATH]{};if(!GetWindowsDirectoryW(windows,MAX_PATH))return {};
    for(const auto* name:{L"msyh.ttc",L"msyh.ttf",L"simhei.ttf"}){
        std::filesystem::path file=std::filesystem::path(windows)/L"Fonts"/name;
        std::error_code error;auto size=std::filesystem::file_size(file,error);if(error||size<1000||size>40*1024*1024)continue;
        auto data=std::make_shared<std::vector<unsigned char>>(static_cast<std::size_t>(size));std::ifstream input(file,std::ios::binary);
        if(input.read(reinterpret_cast<char*>(data->data()),static_cast<std::streamsize>(size)))return data;
    }return {};
}
struct StartContext {std::shared_ptr<Service> service;HMODULE module=nullptr;};
}
Service::Service(){stop_=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!stop_)throw std::runtime_error("Cannot create stop event");generation_=++configOwner;}
Service::~Service(){if(stop_)CloseHandle(stop_);}
std::shared_ptr<Service> Service::Start(){
    auto service=std::make_shared<Service>();auto context=std::make_unique<StartContext>();context->service=service;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&Service::Thread),&context->module))return {};
    HANDLE thread=CreateThread(nullptr,0,&Service::Thread,context.get(),0,nullptr);
    if(!thread){FreeLibrary(context->module);return {};}context.release();CloseHandle(thread);return service;
}
DWORD WINAPI Service::Thread(void* value){
    std::unique_ptr<StartContext> context(static_cast<StartContext*>(value));auto service=std::move(context->service);auto module=context->module;context.reset();
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try{service->Run();}catch(...){try{ServiceView error;error.ready=true;error.message="后台服务初始化失败；可重新加载插件。";service->Publish(error);}catch(...){}service->Stop();}
    if(SUCCEEDED(com))CoUninitialize();service.reset();FreeLibraryAndExitThread(module,0);
}
void Service::Stop() noexcept {stopping_=true;SetEvent(stop_);}
std::shared_ptr<const ServiceView> Service::Read(){
    if(!TryAcquireSRWLockShared(&viewLock_))return {};auto result=view_;ReleaseSRWLockShared(&viewLock_);return result;
}
void Service::Publish(ServiceView& data){
    ++data.revision;auto fresh=std::make_shared<const ServiceView>(data);
    AcquireSRWLockExclusive(&viewLock_);auto old=std::move(view_);view_=std::move(fresh);ReleaseSRWLockExclusive(&viewLock_);
}
bool Service::Request(std::uint64_t id,bool priority){
    if(!id||stopping_)return false;
    if(auto view=Read()){auto i=view->profiles.find(id);if(i!=view->profiles.end()&&GetTickCount64()<i->second.next&&!(priority&&i->second.state==ProfileState::Pending))return true;}
    if(!TryAcquireSRWLockExclusive(&requestsLock_))return false;
    bool result=false;try{auto i=std::find_if(requests_.begin(),requests_.end(),[id](const auto& row){return row.first==id;});if(i!=requests_.end()){if(priority){requests_.erase(i);requests_.emplace_front(id,true);}result=true;}
        else if(requests_.size()<256){if(priority)requests_.emplace_front(id,true);else requests_.emplace_back(id,false);result=true;}}
    catch(...){ReleaseSRWLockExclusive(&requestsLock_);throw;}ReleaseSRWLockExclusive(&requestsLock_);return result;
}
bool Service::Save(const companion::Settings& settings,std::uint64_t serial){
    if(stopping_||!TryAcquireSRWLockExclusive(&requestsLock_))return false;
    try{save_=std::make_pair(settings,serial);}catch(...){ReleaseSRWLockExclusive(&requestsLock_);throw;}ReleaseSRWLockExclusive(&requestsLock_);return true;
}
bool Service::OpenProfile(std::uint64_t id){
    if(!id||stopping_||!TryAcquireSRWLockExclusive(&requestsLock_))return false;
    try{link_=L"https://truckersmp.com/user/"+std::to_wstring(id);}catch(...){ReleaseSRWLockExclusive(&requestsLock_);throw;}ReleaseSRWLockExclusive(&requestsLock_);return true;
}
bool Service::OpenAuthor(){
    if(stopping_||!TryAcquireSRWLockExclusive(&requestsLock_))return false;
    try{link_=L"https://github.com/sakuradairong";}catch(...){ReleaseSRWLockExclusive(&requestsLock_);throw;}ReleaseSRWLockExclusive(&requestsLock_);return true;
}
void Service::Run(){
    ServiceView data;std::wstring warning;
    {ConfigGuard guard;
        if(!std::filesystem::exists(companion::SettingsPath(true))&&std::filesystem::exists(companion::SettingsPath())){data.settings=companion::LoadSettings(warning);data.settings.x=data.settings.y=INT_MIN;}
        else data.settings=companion::LoadSettings(warning,true);}
    data.message=companion::Utf8(warning);data.font=Font();data.ready=true;
    if(!data.font)data.message="System Chinese font unavailable; install Microsoft YaHei or SimHei.";
    Publish(data);
    std::deque<std::uint64_t> pending;std::set<std::uint64_t> queued;std::uint64_t nextRequest=0,nextServers=0;
    for(;;){
        std::deque<std::pair<std::uint64_t,bool>> incoming;std::optional<std::pair<companion::Settings,std::uint64_t>> save;std::optional<std::wstring> link;
        AcquireSRWLockExclusive(&requestsLock_);incoming.swap(requests_);save.swap(save_);link.swap(link_);ReleaseSRWLockExclusive(&requestsLock_);
        bool changed=false;
        if(save){ConfigGuard guard;if(generation_==configOwner.load()){std::wstring error;if(companion::SaveSettings(save->first,error,true)){data.settings=save->first;data.savedSerial=save->second;data.message="设置已保存";}else data.message=companion::Utf8(error);changed=true;}}
        if(stopping_){if(changed)Publish(data);return;}
        if(link)ShellExecuteW(nullptr,L"open",link->c_str(),nullptr,nullptr,SW_SHOWNORMAL);
        for(const auto& request:incoming){
            auto id=request.first;
            if(queued.count(id)){if(request.second){auto i=std::find(pending.begin(),pending.end(),id);if(i!=pending.end()){pending.erase(i);pending.push_front(id);}}continue;}
            if(queued.size()>=256)continue;auto i=data.profiles.find(id);
            if(i!=data.profiles.end()&&GetTickCount64()<i->second.next)continue;
            if(i==data.profiles.end()&&data.profiles.size()>=512){auto oldest=data.profiles.end();for(auto p=data.profiles.begin();p!=data.profiles.end();++p)if(!queued.count(p->first)&&(oldest==data.profiles.end()||p->second.tick<oldest->second.tick))oldest=p;
                if(oldest==data.profiles.end())continue;data.profiles.erase(oldest);}
            auto& p=data.profiles[id];p.id=id;p.state=ProfileState::Pending;p.message="按限频队列查询中";p.next=GetTickCount64()+60000;
            if(request.second)pending.push_front(id);else pending.push_back(id);queued.insert(id);changed=true;
        }
        if(changed)Publish(data);
        const auto now=GetTickCount64();if(now>=nextRequest&&(now>=nextServers||!pending.empty())){
            const bool server=now>=nextServers;std::uint64_t id=0;
            if(server)nextServers=now+60000;else{id=pending.front();pending.pop_front();}
            auto reply=Get(server?L"/v2/servers":L"/v2/player/"+std::to_wstring(id),stop_);
            if(stopping_)continue;const auto tick=GetTickCount64();nextRequest=tick+(reply.status==429?reply.retry*1000ull:3000ull);
            picojson::value root;const bool good=Good(reply,root);const auto& response=Field(root,"response");
            if(server){
                if(good&&response.is<picojson::array>()){
                    data.servers.clear();for(const auto& row:response.get<picojson::array>()){
                        if(data.servers.size()>=200)break;Server s;s.id=Id(Field(row,"id"));s.name=Text(Field(row,"name"));
                        s.players=static_cast<unsigned>(std::min(Id(Field(row,"players")),1000000ull));s.capacity=static_cast<unsigned>(std::min(Id(Field(row,"maxplayers")),1000000ull));s.queue=static_cast<unsigned>(std::min(Id(Field(row,"queue")),1000000ull));
                        const auto& online=Field(row,"online");s.online=online.is<bool>()&&online.get<bool>();if(s.id&&!s.name.empty())data.servers.push_back(s);
                    }data.serverTick=tick;data.serverMessage="公开服务器资料已更新";
                }else data.serverMessage=reply.status==429?"API 限频，稍后重试":"服务器查询失败；旧资料可能过期";
            }else{
                auto& p=data.profiles[id];queued.erase(id);p.id=id;p.tick=tick;p.next=reply.status==429?nextRequest:tick+60000;
                auto name=Text(Field(response,"name"));const auto& vtc=Field(response,"vtc");const auto& in=Field(vtc,"inVTC");auto vtcId=Id(Field(vtc,"id"));auto vtcName=Text(Field(vtc,"name"));
                if(good&&Id(Field(response,"id"))==id&&!name.empty()&&in.is<bool>()&&(!in.get<bool>()||(vtcId&&!vtcName.empty()))){
                    p.state=ProfileState::Ready;p.name=name;p.vtc=in.get<bool>()?vtcId:0;p.vtcName=p.vtc?vtcName:"未加入 VTC";p.next=tick+15*60*1000;p.message="公开资料已缓存";
                }else{p.state=reply.status==404?ProfileState::Missing:ProfileState::Error;p.message=reply.status==429?"API 限频，等待重试":reply.status==404?"账号不存在":"资料暂不可用";}
            }
            Publish(data);
        }
        WaitForSingleObject(stop_,100);
    }
}
}
