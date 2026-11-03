#pragma once
#include "settings.h"
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
namespace companion {
enum class ProfileStatus { Empty,Pending,Ready,Missing,Error };
struct Profile {ProfileStatus status=ProfileStatus::Empty;std::uint64_t id=0,vtc=0,tick=0,next=0;std::wstring name,vtcName,message;bool queued=false;};
struct Server {std::uint64_t id=0;std::wstring name;unsigned players=0,queue=0,capacity=0;bool online=false;};
class PublicApi {
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_=false;
    std::deque<std::uint64_t> queue_;
    std::map<std::uint64_t,Profile> profiles_;
    std::vector<Server> servers_;
    std::wstring serverMessage_=L"服务器资料等待查询";
    std::uint64_t serverTick_=0;
    // The worker starts only after all state it reads has been constructed.
    std::thread thread_;
    void Run();
public:
    PublicApi();~PublicApi();
    void Request(std::uint64_t id,bool priority=false);
    Profile Get(std::uint64_t id);
    std::vector<Server> Servers(std::wstring& status,std::uint64_t& tick);
};
}
