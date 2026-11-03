#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vector>
#include "protocol.h"
namespace companion {
std::wstring PipeName(DWORD pid);
std::vector<DWORD> Games();
HWND GameWindow(DWORD pid);
bool Fetch(DWORD pid,Snapshot& snapshot);
class PipeServer {
    HANDLE pipe_=INVALID_HANDLE_VALUE,event_=nullptr;
    OVERLAPPED op_{};
    enum Phase { Connecting,Reading,Writing } phase_=Connecting;
    bool pending_=false;
    ULONGLONG deadline_=0;
    Query query_{};
    Snapshot reply_{};
    void(*copy_)(Snapshot&)=nullptr;
    void Connect();
    void Read();
    void Write();
public:
    bool Start(void(*copy)(Snapshot&));
    void Step();
    void Stop();
    ~PipeServer(){Stop();}
};
}
