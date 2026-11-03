#pragma once
#include "transport.h"
#include <condition_variable>
#include <mutex>
#include <thread>
namespace companion {
struct Connection {Snapshot data;DWORD pid=0;bool available=false;unsigned games=0;};
class DataClient {
    std::mutex mutex_;std::condition_variable wake_;bool stop_=false;
    Connection current_;std::thread thread_;
    void Run() {
        std::vector<DWORD> candidates;ULONGLONG discover=0;
        for(;;){
            {std::unique_lock<std::mutex> lock(mutex_);wake_.wait_for(lock,std::chrono::milliseconds(200));if(stop_)return;}
            try{
                if(GetTickCount64()>=discover){candidates=Games();if(candidates.size()>4)candidates.resize(4);discover=GetTickCount64()+1500;}
                DWORD front=0;GetWindowThreadProcessId(GetForegroundWindow(),&front);
                auto preferred=std::find(candidates.begin(),candidates.end(),front);if(preferred!=candidates.end())std::iter_swap(candidates.begin(),preferred);
                Connection next;next.games=static_cast<unsigned>(candidates.size());if(!candidates.empty())next.pid=candidates.front();
                for(auto pid:candidates)if(Fetch(pid,next.data)){next.pid=pid;next.available=true;break;}
                std::lock_guard<std::mutex> lock(mutex_);current_=next;
            }catch(...){std::lock_guard<std::mutex> lock(mutex_);current_={};}
        }
    }
public:
    DataClient():thread_([this]{Run();}){}
    ~DataClient(){{std::lock_guard<std::mutex> lock(mutex_);stop_=true;}wake_.notify_all();if(thread_.joinable())thread_.join();}
    Connection Read(){std::lock_guard<std::mutex> lock(mutex_);return current_;}
};
}
