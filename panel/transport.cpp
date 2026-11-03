#include "transport.h"
#include <sddl.h>
#include <tlhelp32.h>

namespace companion {
std::wstring PipeName(DWORD pid){return L"\\\\.\\pipe\\TruckCompanion.Panel.v1."+std::to_wstring(pid);}
namespace {
struct Handle {HANDLE h;~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
PSECURITY_DESCRIPTOR Security(){
    HANDLE token=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return nullptr;Handle close{token};
    DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);if(!size||size>65536)return nullptr;
    std::vector<unsigned char> bytes(size);if(!GetTokenInformation(token,TokenUser,bytes.data(),size,&size))return nullptr;
    LPWSTR sid=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid,&sid))return nullptr;
    std::wstring text=L"D:P(A;;GA;;;SY)(A;;GA;;;"+std::wstring(sid)+L")";LocalFree(sid);
    PSECURITY_DESCRIPTOR result=nullptr;
    return ConvertStringSecurityDescriptorToSecurityDescriptorW(text.c_str(),SDDL_REVISION_1,&result,nullptr)?result:nullptr;
}
bool Complete(HANDLE file,OVERLAPPED& op,BOOL done,DWORD& count){
    if(done)return true;if(GetLastError()!=ERROR_IO_PENDING)return false;
    if(WaitForSingleObject(op.hEvent,250)==WAIT_OBJECT_0)return GetOverlappedResult(file,&op,&count,FALSE)!=FALSE;
    CancelIoEx(file,&op);GetOverlappedResult(file,&op,&count,TRUE);return false;
}
}
std::vector<DWORD> Games(){
    std::vector<DWORD> result;Handle h{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};if(h.h==INVALID_HANDLE_VALUE)return result;
    PROCESSENTRY32W p{};p.dwSize=sizeof p;
    if(Process32FirstW(h.h,&p))do{if(!_wcsicmp(p.szExeFile,L"eurotrucks2.exe")||!_wcsicmp(p.szExeFile,L"amtrucks.exe"))result.push_back(p.th32ProcessID);}while(Process32NextW(h.h,&p));
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    auto i=std::find(result.begin(),result.end(),foreground);if(i!=result.end())std::iter_swap(result.begin(),i);return result;
}
HWND GameWindow(DWORD pid){
    struct Search{DWORD pid;HWND window=nullptr;long long area=0;} search{pid};
    EnumWindows([](HWND h,LPARAM value)->BOOL{auto& s=*reinterpret_cast<Search*>(value);DWORD p=0;GetWindowThreadProcessId(h,&p);
        if(p==s.pid&&IsWindowVisible(h)&&!GetWindow(h,GW_OWNER)){RECT r{};GetWindowRect(h,&r);auto a=static_cast<long long>(r.right-r.left)*(r.bottom-r.top);if(a>s.area){s.area=a;s.window=h;}}return TRUE;},reinterpret_cast<LPARAM>(&search));
    return search.window;
}
bool Fetch(DWORD pid,Snapshot& out){
    Handle file{CreateFileW(PipeName(pid).c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr)};
    if(file.h==INVALID_HANDLE_VALUE)return false;ULONG actual=0;if(!GetNamedPipeServerProcessId(file.h,&actual)||actual!=pid)return false;
    DWORD mode=PIPE_READMODE_MESSAGE;if(!SetNamedPipeHandleState(file.h,&mode,nullptr,nullptr))return false;
    Handle event{CreateEventW(nullptr,TRUE,FALSE,nullptr)};if(!event.h)return false;
    OVERLAPPED op{};op.hEvent=event.h;DWORD count=0;Query q;q.pid=pid;q.client=GetCurrentProcessId();
    if(!Complete(file.h,op,WriteFile(file.h,&q,sizeof q,&count,&op),count)||count!=sizeof q)return false;
    ResetEvent(event.h);op={};op.hEvent=event.h;Snapshot s;
    if(!Complete(file.h,op,ReadFile(file.h,&s,sizeof s,&count,&op),count)||count!=sizeof s||!Valid(s,pid,GetTickCount64()))return false;
    out=s;return true;
}
bool PipeServer::Start(void(*copy)(Snapshot&)){
    Stop();copy_=copy;auto descriptor=Security();if(!descriptor)return false;SECURITY_ATTRIBUTES attrs{sizeof attrs,descriptor,FALSE};
    pipe_=CreateNamedPipeW(PipeName(GetCurrentProcessId()).c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,sizeof(Snapshot),sizeof(Query),0,&attrs);LocalFree(descriptor);
    if(pipe_==INVALID_HANDLE_VALUE)return false;event_=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!event_){Stop();return false;}Connect();return true;
}
void PipeServer::Stop(){
    if(pipe_!=INVALID_HANDLE_VALUE){CancelIoEx(pipe_,nullptr);if(pending_){DWORD n=0;GetOverlappedResult(pipe_,&op_,&n,TRUE);}CloseHandle(pipe_);pipe_=INVALID_HANDLE_VALUE;}
    if(event_){CloseHandle(event_);event_=nullptr;}pending_=false;
}
void PipeServer::Connect(){
    CancelIoEx(pipe_,nullptr);if(pending_){DWORD n=0;GetOverlappedResult(pipe_,&op_,&n,TRUE);}pending_=false;
    DisconnectNamedPipe(pipe_);ResetEvent(event_);op_={};op_.hEvent=event_;phase_=Connecting;deadline_=0;
    if(ConnectNamedPipe(pipe_,&op_))Read();else{auto e=GetLastError();if(e==ERROR_PIPE_CONNECTED)Read();else if(e==ERROR_IO_PENDING)pending_=true;}
}
void PipeServer::Read(){
    ResetEvent(event_);op_={};op_.hEvent=event_;phase_=Reading;deadline_=GetTickCount64()+750;query_={};DWORD n=0;
    if(ReadFile(pipe_,&query_,sizeof query_,&n,&op_)){pending_=false;if(n==sizeof query_)Write();else Connect();}
    else if(GetLastError()==ERROR_IO_PENDING)pending_=true;else{pending_=false;Connect();}
}
void PipeServer::Write(){
    ULONG pid=0;
    if(query_.magic!=Magic||query_.version!=Version||query_.bytes!=sizeof query_||query_.reserved||query_.pid!=GetCurrentProcessId()||
       !GetNamedPipeClientProcessId(pipe_,&pid)||!pid||pid!=query_.client){Connect();return;}
    reply_={};copy_(reply_);reply_.pid=GetCurrentProcessId();reply_.tick=GetTickCount64();
    ResetEvent(event_);op_={};op_.hEvent=event_;phase_=Writing;deadline_=GetTickCount64()+750;DWORD n=0;
    if(WriteFile(pipe_,&reply_,sizeof reply_,&n,&op_))pending_=false;
    else if(GetLastError()==ERROR_IO_PENDING)pending_=true;else{pending_=false;Connect();}
}
void PipeServer::Step(){
    if(pipe_==INVALID_HANDLE_VALUE)return;if(deadline_&&GetTickCount64()>=deadline_){Connect();return;}
    if(!pending_){if(phase_==Writing){DWORD n=0;if(!PeekNamedPipe(pipe_,nullptr,0,nullptr,&n,nullptr))Connect();}else if(phase_==Connecting)Connect();return;}
    if(WaitForSingleObject(event_,0)!=WAIT_OBJECT_0)return;DWORD n=0;const bool ok=GetOverlappedResult(pipe_,&op_,&n,FALSE)!=FALSE;pending_=false;
    if(!ok){Connect();return;}if(phase_==Connecting)Read();else if(phase_==Reading){if(n==sizeof query_)Write();else Connect();}
}
}
