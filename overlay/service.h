#pragma once
#include "../panel/settings.h"
#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <optional>

namespace overlay {
enum class ProfileState { Pending,Ready,Missing,Error };
struct Profile {
    ProfileState state=ProfileState::Pending;
    std::uint64_t id=0,vtc=0,tick=0,next=0;
    std::string name,vtcName,message="等待查询";
};
struct Server {std::uint64_t id=0;std::string name;unsigned players=0,capacity=0,queue=0;bool online=false;};
struct ServiceView {
    bool ready=false;
    companion::Settings settings;
    std::map<std::uint64_t,Profile> profiles;
    std::vector<Server> servers;
    std::shared_ptr<const std::vector<unsigned char>> font;
    std::string message,serverMessage="等待服务器资料";
    std::uint64_t serverTick=0,revision=0,savedSerial=0;
};
// The game thread only uses try-lock operations. A worker owns a DLL reference
// until its bounded synchronous WinHTTP call returns and cleanup is finished.
class Service final {
    SRWLOCK requestsLock_=SRWLOCK_INIT,viewLock_=SRWLOCK_INIT;
    std::deque<std::pair<std::uint64_t,bool>> requests_;
    std::optional<std::pair<companion::Settings,std::uint64_t>> save_;
    std::optional<std::wstring> link_;
    std::shared_ptr<const ServiceView> view_;
    std::atomic<bool> stopping_{false};
    HANDLE stop_=nullptr;
    std::uint64_t generation_=0;
    void Run();
    void Publish(ServiceView& data);
    static DWORD WINAPI Thread(void* data);
public:
    Service();~Service();
    static std::shared_ptr<Service> Start();
    void Stop() noexcept;
    bool Request(std::uint64_t id,bool priority=false);
    bool Save(const companion::Settings& settings,std::uint64_t serial);
    bool OpenProfile(std::uint64_t id);
    bool OpenAuthor();
    std::shared_ptr<const ServiceView> Read();
};
inline bool Current(const Profile& profile) {
    return profile.state==ProfileState::Ready&&companion::Fresh(GetTickCount64(),profile.tick,15*60*1000);
}
}
