// Read-only local player snapshot plus bounded public VTC lookup on Worker().
// Included inside the adapter namespace after the native string/memory helpers.
namespace playerprofile {
enum Status : unsigned { Loading, Member, NoVtc, Failed, RateLimited };
struct Result {Status status=Loading;unsigned vtc=0;char name[513]{};};
struct Local {unsigned server=0,account=0;char name[513]{};};
struct Ticket {unsigned account=0;std::uint64_t generation=0;};
SRWLOCK cacheLock=SRWLOCK_INIT;
unsigned account=0;
std::uint64_t generation=0,lastSeen=0,nextAttempt=0,nextNetwork=0;
Result cached;
struct Lock {
    Lock(){AcquireSRWLockExclusive(&cacheLock);}
    ~Lock(){ReleaseSRWLockExclusive(&cacheLock);}
};
Result Snapshot(unsigned wanted,std::uint64_t now) {
    Lock lock;
    if(account!=wanted){account=wanted;++generation;cached={};nextAttempt=0;}
    lastSeen=now;return cached;
}
bool Begin(std::uint64_t now,Ticket& ticket) {
    Lock lock;
    if(!account||now<nextAttempt||now<nextNetwork||now-lastSeen>5000)return false;
    ticket={account,generation};nextAttempt=now+60000;nextNetwork=now+5000;
    return true;
}
void Complete(const Ticket& ticket,const Result& result,std::uint64_t now) {
    Lock lock;
    if(account!=ticket.account||generation!=ticket.generation||now-lastSeen>5000)return;
    cached=result;
    nextAttempt=now+(result.status==Member||result.status==NoVtc?300000:
                    result.status==RateLimited?120000:60000);
}
bool ReadLocal(void* model,Local& result) {
    result={};
    if(!Readable(model,0x88))return false;
    const auto* p=static_cast<unsigned char*>(model);
    // The live list is separate from the searched projection (0x68) and history (0x50).
    const auto first=reinterpret_cast<std::uintptr_t>(PointerAt(p,0x38));
    const auto last=reinterpret_cast<std::uintptr_t>(PointerAt(p,0x40));
    const auto limit=reinterpret_cast<std::uintptr_t>(PointerAt(p,0x48));
    if(last<first||limit<last||(last-first)%0x98||(limit-first)%0x98||
       (last-first)/0x98>32768||!first||!Readable(reinterpret_cast<void*>(first),last-first))return false;
    bool found=false;
    for(auto at=first;at<last;at+=0x98) {
        const auto* row=reinterpret_cast<const unsigned char*>(at);
        if(row[0x18]!=1||row[0x19]||row[0x1a])continue; // local, not demo, not left
        if(found){result={};return false;}
        unsigned server=0,id=0;std::memcpy(&server,row+8,4);std::memcpy(&id,row+12,4);
        const char* name=Narrow(row+0x40);
        if(server>65535||!id||id>0x7fffffff||!name||!*name)return false;
        const auto size=reinterpret_cast<const NativeString*>(row+0x40)->size;
        if(std::strlen(name)!=size)return false;
        result.server=server;result.account=id;std::memcpy(result.name,name,size+1);found=true;
    }
    return found;
}
bool BoundedJson(const std::string& text) {
    if(text.empty()||text.size()>65536||text.find('\0')!=std::string::npos)return false;
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0))return false;
    unsigned depth=0;bool quoted=false,escape=false;
    for(unsigned char c:text) {
        if(quoted){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')quoted=false;}
        else if(c=='"')quoted=true;
        else if(c=='{'||c=='['){if(++depth>24)return false;}
        else if(c=='}'||c==']'){if(!depth)return false;--depth;}
    }
    return !quoted&&!depth;
}
bool Id(const picojson::value& value,unsigned& id) {
    if(!value.is<double>())return false;
    const auto number=value.get<double>();
    if(!std::isfinite(number)||number<0||number>0x7fffffff||std::floor(number)!=number)return false;
    id=static_cast<unsigned>(number);return true;
}
Result Parse(const std::string& body,unsigned expected) {
    Result result;result.status=Failed;
    if(!BoundedJson(body))return result;
    picojson::value root;
    std::string errorText;
    auto end=picojson::parse(root,body.begin(),body.end(),&errorText);
    while(end!=body.end()&&(*end==' '||*end=='\r'||*end=='\n'||*end=='\t'))++end;
    if(!errorText.empty()||end!=body.end()||!root.is<picojson::object>())return result;
    const auto& error=root.get("error");const auto& response=root.get("response");
    if(!error.is<bool>()||error.get<bool>()||!response.is<picojson::object>())return result;
    unsigned received=0;
    if(!Id(response.get("id"),received)||received!=expected)return result;
    const auto& vtc=response.get("vtc");
    if(!vtc.is<picojson::object>()||!vtc.get("inVTC").is<bool>())return result;
    if(!vtc.get("inVTC").get<bool>()){result.status=NoVtc;return result;}
    if(!Id(vtc.get("id"),result.vtc)||!result.vtc||!vtc.get("name").is<std::string>())return result;
    const auto& name=vtc.get("name").get<std::string>();
    if(name.empty()||name.size()>512||name.find('\0')!=std::string::npos)return result;
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name.data(),static_cast<int>(name.size()),nullptr,0))return result;
    std::memcpy(result.name,name.c_str(),name.size()+1);result.status=Member;return result;
}
struct Internet {
    HINTERNET handle=nullptr;
    explicit Internet(HINTERNET value):handle(value){}
    ~Internet(){if(handle)WinHttpCloseHandle(handle);}
    Internet(const Internet&)=delete;Internet& operator=(const Internet&)=delete;
};
Result Fetch(unsigned id) {
    Result failure;failure.status=Failed;
    Internet session(WinHttpOpen(L"RainySY-TMPZH/0.3 (public VTC lookup)",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    if(!session.handle)return failure;
    if(!WinHttpSetTimeouts(session.handle,5000,5000,5000,10000))return failure;
    Internet connection(WinHttpConnect(session.handle,L"api.truckersmp.com",INTERNET_DEFAULT_HTTPS_PORT,0));
    if(!connection.handle)return failure;
    wchar_t path[80];swprintf_s(path,L"/v2/player/%u",id);
    Internet request(WinHttpOpenRequest(connection.handle,L"GET",path,nullptr,WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    if(!request.handle)return failure;
    DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if(!WinHttpSetOption(request.handle,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof redirects))return failure;
    DWORD disabled=WINHTTP_DISABLE_COOKIES;
    if(!WinHttpSetOption(request.handle,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof disabled))return failure;
    DWORD auth=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    if(!WinHttpSetOption(request.handle,WINHTTP_OPTION_AUTOLOGON_POLICY,&auth,sizeof auth))return failure;
    if(!WinHttpSendRequest(request.handle,L"Accept: application/json\r\n",static_cast<DWORD>(-1),
                          WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(request.handle,nullptr))return failure;
    DWORD status=0,size=sizeof status;
    if(!WinHttpQueryHeaders(request.handle,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX))return failure;
    if(status==429){failure.status=RateLimited;return failure;}
    if(status!=200)return failure;
    std::string body;char chunk[4096];const auto start=GetTickCount64();
    for(;;) {
        if(stopped.load()||GetTickCount64()-start>15000)return failure;
        DWORD read=0;
        if(!WinHttpReadData(request.handle,chunk,sizeof chunk,&read))return failure;
        if(!read)break;
        if(body.size()+read>65536)return failure;
        body.append(chunk,read);
    }
    return Parse(body,id);
}
void Poll() noexcept {
    Ticket ticket;
    if(stopped.load()||!Begin(GetTickCount64(),ticket))return;
    Result result;result.status=Failed;
    try {result=Fetch(ticket.account);} catch(...) {} // Network/parser failures never escape Worker().
    if(!stopped.load())Complete(ticket,result,GetTickCount64());
}
}
