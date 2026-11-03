#include "public_api.h"
#include <winhttp.h>
#include "../vendor/picojson/picojson.h"
namespace companion {
namespace {
struct Internet {HINTERNET h=nullptr;~Internet(){if(h)WinHttpCloseHandle(h);}};
struct Reply {DWORD status=0,retry=60;std::string body;};
Reply Get(const std::wstring& path){
    Reply out;Internet session{WinHttpOpen(L"TruckCompanionPanel/0.1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
    if(!session.h)return out;WinHttpSetTimeouts(session.h,2500,2500,2500,2500);
    Internet connection{WinHttpConnect(session.h,L"api.truckersmp.com",INTERNET_DEFAULT_HTTPS_PORT,0)};if(!connection.h)return out;
    Internet request{WinHttpOpenRequest(connection.h,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE)};if(!request.h)return out;
    DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(request.h,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof policy);
    if(!WinHttpSendRequest(request.h,L"Accept: application/json\r\n",DWORD(-1),nullptr,0,0,0)||!WinHttpReceiveResponse(request.h,nullptr))return out;
    DWORD size=sizeof out.status;WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&out.status,&size,nullptr);
    if(out.status==429){wchar_t value[128]{};size=sizeof value;
        if(WinHttpQueryHeaders(request.h,WINHTTP_QUERY_CUSTOM,L"Retry-After",value,&size,nullptr)){
            wchar_t* end=nullptr;auto seconds=wcstoul(value,&end,10);
            if(end!=value&&!*end)out.retry=(std::min)(3600ul,(std::max)(60ul,seconds));
            else{SYSTEMTIME time{};if(WinHttpTimeToSystemTime(value,&time)){FILETIME then{},now{};SystemTimeToFileTime(&time,&then);GetSystemTimeAsFileTime(&now);
                ULARGE_INTEGER a{},b{};a.LowPart=then.dwLowDateTime;a.HighPart=then.dwHighDateTime;b.LowPart=now.dwLowDateTime;b.HighPart=now.dwHighDateTime;
                if(a.QuadPart>b.QuadPart)out.retry=static_cast<DWORD>((std::min)(3600ull,(std::max)(60ull,(a.QuadPart-b.QuadPart)/10000000)));}}}}
    if(out.status!=200)return out;
    char buffer[8192];const auto start=GetTickCount64();
    for(;;){DWORD count=0;if(!WinHttpReadData(request.h,buffer,sizeof buffer,&count)){out.status=0;out.body.clear();break;}if(!count)break;
        if(out.body.size()+count>1024*1024||GetTickCount64()-start>10000){out.status=0;out.body.clear();break;}out.body.append(buffer,count);}
    return out;
}
const picojson::value& Field(const picojson::value& v,const char* name){static const picojson::value empty;if(!v.is<picojson::object>())return empty;auto i=v.get<picojson::object>().find(name);return i==v.get<picojson::object>().end()?empty:i->second;}
std::uint64_t Integer(const picojson::value& v){
    if(v.is<double>()){auto n=v.get<double>();return std::isfinite(n)&&n>=0&&n<=9007199254740991.0&&std::floor(n)==n?static_cast<std::uint64_t>(n):0;}
    if(v.is<std::string>()){const auto& s=v.get<std::string>();if(s.empty()||s.size()>20||s.find_first_not_of("0123456789")!=std::string::npos)return 0;try{return std::stoull(s);}catch(...){}}return 0;
}
std::wstring Text(const picojson::value& v){if(!v.is<std::string>())return {};const auto& s=v.get<std::string>();return s.size()<=1024?Wide(s):L"";}
bool Good(const Reply& r,picojson::value& v){if(r.status!=200||!picojson::parse(v,r.body).empty()||!v.is<picojson::object>())return false;
    // The servers endpoint returns "false" as a string; accept only explicit success.
    const auto& error=Field(v,"error");return (error.is<bool>()&&!error.get<bool>())||(error.is<std::string>()&&error.get<std::string>()=="false");}
}
PublicApi::PublicApi():thread_([this]{Run();}){}
PublicApi::~PublicApi(){{std::lock_guard<std::mutex> guard(mutex_);stop_=true;}wake_.notify_all();if(thread_.joinable())thread_.join();}
void PublicApi::Request(std::uint64_t id,bool priority){
    if(!id)return;std::lock_guard<std::mutex> guard(mutex_);auto it=profiles_.find(id);
    if(it==profiles_.end()){
        if(profiles_.size()>=512){auto oldest=profiles_.end();for(auto i=profiles_.begin();i!=profiles_.end();++i)if(!i->second.queued&&(oldest==profiles_.end()||i->second.tick<oldest->second.tick))oldest=i;
            if(oldest==profiles_.end())return;profiles_.erase(oldest);}
        it=profiles_.emplace(id,Profile{}).first;it->second.id=id;
    }
    auto& p=it->second;
    if(p.queued){if(priority){auto queued=std::find(queue_.begin(),queue_.end(),id);if(queued!=queue_.end()){queue_.erase(queued);queue_.push_front(id);}}return;}
    if(GetTickCount64()<p.next||queue_.size()>=256)return;
    p.queued=true;if(p.status!=ProfileStatus::Ready)p.status=ProfileStatus::Pending;
    if(priority)queue_.push_front(id);else queue_.push_back(id);wake_.notify_all();
}
Profile PublicApi::Get(std::uint64_t id){std::lock_guard<std::mutex> guard(mutex_);auto i=profiles_.find(id);return i==profiles_.end()?Profile{}:i->second;}
std::vector<Server> PublicApi::Servers(std::wstring& status,std::uint64_t& tick){std::lock_guard<std::mutex> guard(mutex_);status=serverMessage_;tick=serverTick_;return servers_;}
void PublicApi::Run(){
    std::uint64_t nextRequest=0,nextServers=0;
    for(;;){
        std::uint64_t id=0;bool server=false;
        {std::unique_lock<std::mutex> lock(mutex_);wake_.wait_for(lock,std::chrono::milliseconds(200));if(stop_)return;
            const auto now=GetTickCount64();if(now<nextRequest)continue;
            if(now>=nextServers){server=true;nextServers=now+60000;}
            else if(!queue_.empty()){id=queue_.front();queue_.pop_front();}else continue;
            nextRequest=now+3000;
        }
        try{
            const auto reply=::companion::Get(server?L"/v2/servers":L"/v2/player/"+std::to_wstring(id));picojson::value data;
            const bool good=Good(reply,data);const auto now=GetTickCount64();
            std::lock_guard<std::mutex> guard(mutex_);if(stop_)return;
            if(reply.status==429)nextRequest=now+reply.retry*1000ull;
            if(server){
                const auto& rows=Field(data,"response");
                if(good&&rows.is<picojson::array>()){
                    servers_.clear();for(const auto& row:rows.get<picojson::array>()){
                        if(servers_.size()>=200)break;Server s;s.id=Integer(Field(row,"id"));s.name=Text(Field(row,"name"));s.players=static_cast<unsigned>((std::min)(Integer(Field(row,"players")),1000000ull));
                        s.capacity=static_cast<unsigned>((std::min)(Integer(Field(row,"maxplayers")),1000000ull));s.queue=static_cast<unsigned>((std::min)(Integer(Field(row,"queue")),1000000ull));
                        const auto& online=Field(row,"online");s.online=online.is<bool>()&&online.get<bool>();if(s.id&&!s.name.empty())servers_.push_back(s);
                    }serverTick_=now;serverMessage_=L"服务器公开资料已更新";
                }else if(reply.status==429)serverMessage_=L"服务器查询限频，稍后重试";
                else if(reply.status==200)serverMessage_=L"服务器接口返回错误或不兼容数据；已有资料可能过期";
                else if(!reply.status)serverMessage_=L"服务器请求未完成，请检查网络；已有资料可能过期";
                else serverMessage_=L"服务器查询失败（HTTP "+std::to_wstring(reply.status)+L"）；已有资料可能过期";
            }else{
                auto& p=profiles_[id];p.id=id;p.queued=false;p.tick=now;p.next=now+60000;
                const auto& row=Field(data,"response");
                const auto& vtc=Field(row,"vtc");const auto& in=Field(vtc,"inVTC");
                const auto name=Text(Field(row,"name"));const auto vtcName=Text(Field(vtc,"name"));const auto vtcId=Integer(Field(vtc,"id"));
                const bool validVtc=in.is<bool>()&&(!in.get<bool>()||(vtcId&&!vtcName.empty()));
                if(good&&Integer(Field(row,"id"))==id&&!name.empty()&&validVtc){
                    p.status=ProfileStatus::Ready;p.name=name;p.vtc=in.get<bool>()?vtcId:0;p.vtcName=p.vtc?vtcName:L"未加入 VTC";
                    p.message=L"公开资料已缓存";p.next=now+15*60*1000;
                }else{p.status=reply.status==404?ProfileStatus::Missing:ProfileStatus::Error;
                    p.message=reply.status==429?L"API 限频，等待重试":p.status==ProfileStatus::Missing?L"未找到该账号的公开资料":L"公开资料暂不可用";
                    if(reply.status==429)p.next=nextRequest;}
            }
        }catch(...){std::lock_guard<std::mutex> guard(mutex_);if(id){auto& p=profiles_[id];p.queued=false;p.status=ProfileStatus::Error;p.message=L"资料读取失败";p.next=GetTickCount64()+60000;}else serverMessage_=L"服务器资料读取失败";}
    }
}
}
