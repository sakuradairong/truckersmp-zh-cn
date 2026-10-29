// Presentation-only adapter for the reviewed TMP 0.7.9.2 Settings window.
// SCS loads this independent DLL. It has no TruckersMP SDK entry points.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <intrin.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include "MinHook.h"
#include "scssdk_telemetry.h"
#include "settings_profile.h"

namespace {
using namespace settingszh;
// Opaque MSVC release string layout. Never construct/free these with our CRT.
struct NativeString { unsigned char storage[16]; std::size_t size, capacity; };
static_assert(sizeof(NativeString)==32 && offsetof(NativeString,size)==16);
using Construct = void* (*)(void*,const void*);
using Render = int (*)(void*,void*,int,int,int);
using Build = void (*)(void*);
using Assign = void* (*)(void*,const wchar_t*,std::size_t);
using SetLabel = void (*)(void*,const void*,bool);
using DestroyText = void (*)(void*);
Construct originalCString=nullptr, originalString=nullptr, originalConvert=nullptr;
Render originalLabel=nullptr, originalCombo=nullptr;
Build originalWindow=nullptr, originalPage=nullptr;
Build originalShow=nullptr;
Assign nativeAssign=nullptr;
SetLabel nativeSetLabel=nullptr;
DestroyText nativeDestroyText=nullptr;
unsigned char* core=nullptr;
std::atomic<bool> enabled{false}, initialized{false}, stopped{false};
std::atomic<unsigned> counts[5]{}; // label, tooltip, tab, option, window/footer
HANDLE stopEvent=nullptr, workerThread=nullptr;
wchar_t logPath[MAX_PATH]{};
SRWLOCK logLock=SRWLOCK_INIT;
thread_local int pageScope=-1;
thread_local bool windowScope=false;
thread_local const Options* optionScope=nullptr;
thread_local bool assigning=false;
template<class T> struct Scope {
    T& target; T previous;
    Scope(T& t,T value):target(t),previous(t){target=value;}
    ~Scope(){target=previous;}
};

void Log(const char* message) {
    if(!*logPath)return;
    AcquireSRWLockExclusive(&logLock);
    HANDLE f=CreateFileW(logPath,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f!=INVALID_HANDLE_VALUE) {
        SYSTEMTIME t;GetLocalTime(&t);char line[640];DWORD wrote;
        int n=std::snprintf(line,sizeof line,"%04u-%02u-%02u %02u:%02u:%02u [%lu] %s\r\n",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId(),message);
        if(n>0&&n<static_cast<int>(sizeof line))WriteFile(f,line,static_cast<DWORD>(n),&wrote,nullptr);
        CloseHandle(f);
    }
    ReleaseSRWLockExclusive(&logLock);
}
bool Readable(const void* pointer,std::size_t size) {
    auto p=reinterpret_cast<std::uintptr_t>(pointer);
    if(!p||size>SIZE_MAX-p)return false;
    const auto end=p+size;
    while(p<end) {
        MEMORY_BASIC_INFORMATION m{};
        if(!VirtualQuery(reinterpret_cast<void*>(p),&m,sizeof m)||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const DWORD f=m.Protect&0xff;
        if(f!=PAGE_READONLY&&f!=PAGE_READWRITE&&f!=PAGE_WRITECOPY&&f!=PAGE_EXECUTE_READ&&f!=PAGE_EXECUTE_READWRITE&&f!=PAGE_EXECUTE_WRITECOPY)return false;
        auto next=reinterpret_cast<std::uintptr_t>(m.BaseAddress)+m.RegionSize;
        if(next<=p)return false;
        p=next;
    }
    return true;
}
const char* Narrow(const void* value) {
    if(!Readable(value,sizeof(NativeString)))return nullptr;
    const auto* s=static_cast<const NativeString*>(value);
    if(s->size>512||s->size>s->capacity||s->capacity>0x100000)return nullptr;
    const char* data=reinterpret_cast<const char*>(s->storage);
    if(s->capacity>15)std::memcpy(&data,s->storage,sizeof data);
    if(!Readable(data,s->size+1)||data[s->size])return nullptr;
    return data;
}
bool NarrowIs(const void* value,const char* text) {
    const char* data=Narrow(value);
    return data&&text&&static_cast<const NativeString*>(value)->size==std::strlen(text)&&!std::memcmp(data,text,std::strlen(text));
}
const wchar_t* Wide(const void* value) {
    if(!Readable(value,sizeof(NativeString)))return nullptr;
    const auto* s=static_cast<const NativeString*>(value);
    if(s->size>512||s->size>s->capacity||s->capacity>0x100000)return nullptr;
    const wchar_t* data=reinterpret_cast<const wchar_t*>(s->storage);
    if(s->capacity>7)std::memcpy(&data,s->storage,sizeof data);
    if(!Readable(data,(s->size+1)*sizeof(wchar_t))||data[s->size])return nullptr;
    return data;
}
bool WideIsEnglish(const void* value,const char* en) {
    const wchar_t* data=Wide(value);
    if(!data||!en||static_cast<const NativeString*>(value)->size!=std::strlen(en))return false;
    for(std::size_t i=0;en[i];++i)if(data[i]!=static_cast<unsigned char>(en[i]))return false;
    return true;
}
bool WideIsChinese(const void* value,const wchar_t* zh) {
    const wchar_t* data=Wide(value);
    return data&&zh&&static_cast<const NativeString*>(value)->size==std::wcslen(zh)&&!std::wcscmp(data,zh);
}
bool TranslateWide(void* value,const Pair& text,unsigned kind) {
    if(!enabled.load()||assigning||!nativeAssign||!text.en||!text.zh||!WideIsEnglish(value,text.en))return false;
    Scope<bool> recursion(assigning,true);
    // This is the client's assign function, including its allocator/deallocator.
    // Only the wide display string changes; the narrow identifier is retained.
    nativeAssign(value,text.zh,std::wcslen(text.zh));
    if(counts[kind].fetch_add(1)==0) {
        constexpr const char* messages[]={"observed: first setting label translated","observed: first tooltip translated",
            "observed: first page title translated","observed: first static option translated","observed: first window/footer caption translated"};
        Log(messages[kind]);
    }
    return true;
}
bool TranslateText(void* object,const Pair& text,unsigned kind) {
    return text.en&&Readable(object,64)&&NarrowIs(static_cast<unsigned char*>(object)+32,text.en)&&TranslateWide(object,text,kind);
}
int PageIndex(const void* object) {
    if(!enabled.load()||!Readable(object,0x68))return -1;
    for(unsigned i=0;i<sizeof kPages/sizeof *kPages;++i)
        if(NarrowIs(static_cast<const unsigned char*>(object)+0x20,kPages[i].en))return static_cast<int>(i);
    return -1;
}
void TranslateDescriptor(void* object) {
    if(pageScope<0||!enabled.load()||!Readable(object,0x88))return;
    auto* p=static_cast<unsigned char*>(object);
    for(const auto& row:kRows) {
        if(row.page!=static_cast<unsigned>(pageScope)||!NarrowIs(p+0x28,row.label.en))continue;
        TranslateText(p+8,row.label,0);
        if(row.tooltip.en)TranslateText(p+0x48,row.tooltip,1);
        return;
    }
}
const Options* OptionsFor(void* object) {
    if(pageScope<0||!enabled.load()||!Readable(object,0xb8))return nullptr;
    auto* p=static_cast<unsigned char*>(object);
    if(*reinterpret_cast<void**>(p)!=core+kComboVtable)return nullptr;
    for(const auto& group:kOptions) {
        if(group.page!=static_cast<unsigned>(pageScope)||!NarrowIs(p+0x28,group.label))continue;
        const NativeString *first=nullptr,*last=nullptr;
        std::memcpy(&first,p+0xa0,sizeof first);std::memcpy(&last,p+0xa8,sizeof last);
        const auto start=reinterpret_cast<std::uintptr_t>(first), end=reinterpret_cast<std::uintptr_t>(last);
        if(end<start||end-start!=group.count*sizeof(NativeString)||!Readable(first,end-start))return nullptr;
        for(unsigned i=0;i<group.count;++i)if(!NarrowIs(first+i,group.pairs[i].en))return nullptr;
        return &group;
    }
    return nullptr;
}
void TranslateLiteral(void* object,const void* source,std::uintptr_t caller) {
    if(!windowScope||!enabled.load())return;
    for(const auto& item:kLiterals)
        if(caller==item.caller&&source==core+item.source) {TranslateText(object,item.text,4);return;}
}
const Pair* ConversionText(const void* source,std::uintptr_t caller) {
    if(!enabled.load())return nullptr;
    if(windowScope&&caller==0x32f2de&&NarrowIs(source,kWindow.en))return &kWindow;
    if(optionScope&&caller==0x13e9a0)
        for(unsigned i=0;i<optionScope->count;++i)if(NarrowIs(source,optionScope->pairs[i].en))return optionScope->pairs+i;
    return nullptr;
}

// Refresh on the native UI thread when Settings opens. The SCS loader can run
// after TMP has already constructed this window, so constructor hooks alone
// cannot cover existing controls. No values, bindings, child lists or names change.
struct TemporaryText {
    alignas(8) unsigned char data[64];
    explicit TemporaryText(const char* en){originalCString(data,en);}
    ~TemporaryText(){nativeDestroyText(data);}
};
void* PointerAt(void* object,std::size_t offset) {
    if(!Readable(object,offset+sizeof(void*)))return nullptr;
    void* result=nullptr;std::memcpy(&result,static_cast<unsigned char*>(object)+offset,sizeof result);return result;
}
void* DisplayText(void* control) {
    if(!Readable(control,0x158))return nullptr;
    void* table=PointerAt(control,0);
    if(!Readable(table,0x560)||PointerAt(table,0x558)!=core+0x336bc0)return nullptr;
    void* child=PointerAt(control,0x150);
    return Readable(child,0x168)?static_cast<unsigned char*>(child)+0x128:nullptr;
}
bool DisplayIs(void* control,const Pair& pair) {
    void* text=DisplayText(control);
    return text&&(WideIsEnglish(text,pair.en)||WideIsChinese(text,pair.zh));
}
bool TranslateDisplay(void* control,const Pair& pair,unsigned kind) {
    void* text=DisplayText(control);
    if(!text||!WideIsEnglish(text,pair.en))return false;
    TemporaryText translated(pair.en);
    if(!TranslateText(translated.data,pair,kind))return false;
    // false keeps the outer control's bounds; the client's setter updates text metrics.
    nativeSetLabel(control,translated.data,false);
    return true;
}
struct ChildList {void* head;std::size_t size;};
bool Children(void* control,void** output,unsigned& count,bool semantic=false) {
    count=0;
    for(unsigned depth=0;semantic&&depth<8;++depth) {
        void* inner=PointerAt(control,0x70);
        if(!inner)break;
        if(inner==control)return false;
        control=inner;
        if(depth==7)return false;
    }
    if(!Readable(control,0x68))return false;
    ChildList list{};std::memcpy(&list,static_cast<unsigned char*>(control)+0x58,sizeof list);
    if(list.size>256||!Readable(list.head,24))return false;
    void* node=PointerAt(list.head,0);
    for(std::size_t i=0;i<list.size;++i) {
        if(node==list.head||!Readable(node,24))return false;
        output[count++]=PointerAt(node,16);node=PointerAt(node,0);
    }
    return node==list.head;
}
void RefreshPageTree(void* content,unsigned page) {
    void* pending[1024]{};unsigned used=1,visited=0;pending[0]=content;
    while(used&&visited++<1024) {
        void* control=pending[--used];
        if(!Readable(control,0xe8))continue;
        // Only the concrete static Label class is eligible. TextBox, MenuItem,
        // device selectors and user-owned text use different concrete vtables.
        if(PointerAt(control,0)==core+kLabelVtable)
            for(const auto& row:kRows)if(row.page==page) {
                TranslateDisplay(control,row.label,0);
                if(row.tooltip.en)TranslateDisplay(control,row.tooltip,1);
            }
        void* tooltip=PointerAt(control,0x80);
        if(PointerAt(tooltip,0)==core+kLabelVtable)
            for(const auto& row:kRows)if(row.page==page&&row.tooltip.en)TranslateDisplay(tooltip,row.tooltip,1);
        void* children[256];unsigned count=0;
        if(Children(control,children,count))for(unsigned i=0;i<count&&used<1024;++i)pending[used++]=children[i];
    }
}
void RefreshOptions(void* descriptor) {
    const Options* group=OptionsFor(descriptor);
    if(!group)return;
    void* combo=PointerAt(descriptor,0xb8);
    void* menu=PointerAt(combo,0x200);
    void* items[256];unsigned count=0;
    if(!Children(menu,items,count,true)||count!=group->count)return;
    for(unsigned i=0;i<count;++i)if(!DisplayIs(items[i],group->pairs[i]))return;
    for(unsigned i=0;i<count;++i)TranslateDisplay(items[i],group->pairs[i],3);
    for(unsigned i=0;i<count;++i)if(DisplayIs(combo,group->pairs[i])) {TranslateDisplay(combo,group->pairs[i],3);break;}
}
void RefreshFooter(void* window) {
    void* pending[1024]{};unsigned used=1,visited=0;pending[0]=window;
    while(used&&visited++<1024) {
        void* control=pending[--used];
        if(!Readable(control,0xf0))continue;
        for(unsigned i=0;i<sizeof kLiterals/sizeof *kLiterals;++i)
            if(NarrowIs(static_cast<unsigned char*>(control)+0xd0,kFooterNames[i]))TranslateDisplay(control,kLiterals[i].text,4);
        void* children[256];unsigned count=0;
        if(Children(control,children,count))for(unsigned i=0;i<count&&used<1024;++i)pending[used++]=children[i];
    }
}
void RefreshExisting(void* settings) {
    if(!enabled.load()||!Readable(settings,0x48))return;
    void* window=PointerAt(settings,0x28);
    void* begin=PointerAt(settings,0x38);void* end=PointerAt(settings,0x40);
    auto start=reinterpret_cast<std::uintptr_t>(begin), finish=reinterpret_cast<std::uintptr_t>(end);
    if(finish<start||(finish-start)%0x68||(finish-start)/0x68>16||!Readable(begin,finish-start))return;
    TranslateDisplay(PointerAt(window,0x1b0),kWindow,4);
    RefreshFooter(window);
    for(auto address=start;address<finish;address+=0x68) {
        auto* page=reinterpret_cast<unsigned char*>(address);int index=PageIndex(page);
        if(index<0)continue;
        Scope<int> scope(pageScope,index);
        TranslateDisplay(PointerAt(page,0x40),kPages[index],2);
        RefreshPageTree(PointerAt(page,0x48),static_cast<unsigned>(index));
        void* first=PointerAt(page,0x50);void* last=PointerAt(page,0x58);
        auto a=reinterpret_cast<std::uintptr_t>(first), z=reinterpret_cast<std::uintptr_t>(last);
        if(z<a||(z-a)%8||(z-a)/8>128||!Readable(first,z-a))continue;
        for(auto p=a;p<z;p+=8) {
            void* descriptor=nullptr;std::memcpy(&descriptor,reinterpret_cast<void*>(p),8);
            TranslateDescriptor(descriptor);RefreshOptions(descriptor);
        }
    }
}
std::uintptr_t Caller(void* address) {return reinterpret_cast<std::uintptr_t>(address)-reinterpret_cast<std::uintptr_t>(core);}

// Keep SEH in leaf wrappers with no C++ objects needing unwinding.
void SafeDescriptor(void* p) {
    __try {TranslateDescriptor(p);} __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {enabled=false;Log("inactive: descriptor access guard; forwarding original calls");}
}
const Options* SafeOptions(void* p) {
    __try {return OptionsFor(p);} __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {enabled=false;return nullptr;}
}
int SafePage(void* p) {
    __try {return PageIndex(p);} __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {enabled=false;return -1;}
}
void SafeRefresh(void* p) {
    __try {RefreshExisting(p);} __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {enabled=false;Log("inactive: existing-window access guard; original UI retained");}
}
void HookShow(void* p) {originalShow(p);SafeRefresh(p);}
void HookWindow(void* p) {Scope<bool> scope(windowScope,enabled.load());originalWindow(p);}
void HookPage(void* p) {Scope<int> scope(pageScope,SafePage(p));originalPage(p);}
int HookLabel(void* p,void* parent,int x,int y,int width) {SafeDescriptor(p);return originalLabel(p,parent,x,y,width);}
int HookCombo(void* p,void* parent,int x,int y,int width) {Scope<const Options*> scope(optionScope,SafeOptions(p));return originalCombo(p,parent,x,y,width);}
void* HookCString(void* object,const void* source) {
    auto caller=Caller(_ReturnAddress());
    void* result=originalCString(object,source);
    TranslateLiteral(result,source,caller);
    return result;
}
void* HookString(void* object,const void* source) {
    auto caller=Caller(_ReturnAddress());
    void* result=originalString(object,source);
    if(enabled.load()&&caller==0x13de5a&&pageScope>=0)TranslateText(result,kPages[pageScope],2);
    return result;
}
void* HookConvert(void* object,const void* source) {
    auto caller=Caller(_ReturnAddress());
    const Pair* text=ConversionText(source,caller);
    void* result=originalConvert(object,source);
    if(text)TranslateWide(result,*text,caller==0x13e9a0?3:4);
    return result;
}

bool HashMatches(HMODULE module) {
    wchar_t path[MAX_PATH];DWORD n=GetModuleFileNameW(module,path,MAX_PATH);
    if(!n||n>=MAX_PATH)return false;
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(f==INVALID_HANDLE_VALUE)return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok)ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    unsigned char buffer[32768],digest[32]{};DWORD count;char hex[65]{};
    while(ok) {
        if(!ReadFile(f,buffer,sizeof buffer,&count,nullptr)){ok=false;break;}
        if(!count)break;
        ok=BCryptHashData(hash,buffer,count,0)>=0;
    }
    if(ok)ok=BCryptFinishHash(hash,digest,sizeof digest,0)>=0;
    if(hash)BCryptDestroyHash(hash);
    if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    CloseHandle(f);
    for(unsigned i=0;i<32;++i)std::snprintf(hex+i*2,3,"%02x",digest[i]);
    return ok&&!std::strcmp(hex,kCoreHash);
}
bool VerifyImage() {
    if(!Readable(core,4096))return false;
    const auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(core);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<0||dos->e_lfanew>3072)return false;
    const auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(core+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt->OptionalHeader.SizeOfImage!=kImageSize)return false;
    for(const auto& g:kGuards)if(!Readable(core+g.rva,g.size)||std::memcmp(core+g.rva,g.bytes,g.size))return false;
    if(!Readable(core+kComboVtable,0x18)||*reinterpret_cast<void**>(core+kComboVtable+0x10)!=core+0x13e860)return false;
    if(!Readable(core+kLabelVtable,0x560)||PointerAt(core+kLabelVtable,0x558)!=core+0x336bc0)return false;
    for(const auto& l:kLiterals)if(!Readable(core+l.source,std::strlen(l.text.en)+1)||std::strcmp(reinterpret_cast<char*>(core+l.source),l.text.en))return false;
    return true;
}
void Attach(HMODULE module) {
    if(!HashMatches(module)){Log("inactive: unsupported TMP file hash");return;}
    core=reinterpret_cast<unsigned char*>(module);
    if(!VerifyImage()){Log("inactive: loaded TMP code/ABI guard mismatch");return;}
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(module),&pinned))return;
    nativeAssign=reinterpret_cast<Assign>(core+0x81fe0);
    nativeSetLabel=reinterpret_cast<SetLabel>(core+0x336bc0);
    nativeDestroyText=reinterpret_cast<DestroyText>(core+0xef630);
    if(MH_Initialize()!=MH_OK){Log("inactive: private MinHook initialization failed");return;}
    struct HookSpec {unsigned rva;void* detour;void** original;};
    const HookSpec hooks[]={
        {0x13d110,reinterpret_cast<void*>(&HookWindow),reinterpret_cast<void**>(&originalWindow)},
        {0x13de20,reinterpret_cast<void*>(&HookPage),reinterpret_cast<void**>(&originalPage)},
        {0x13e130,reinterpret_cast<void*>(&HookLabel),reinterpret_cast<void**>(&originalLabel)},
        {0x13e860,reinterpret_cast<void*>(&HookCombo),reinterpret_cast<void**>(&originalCombo)},
        {0xf0310,reinterpret_cast<void*>(&HookCString),reinterpret_cast<void**>(&originalCString)},
        {0xef4f0,reinterpret_cast<void*>(&HookString),reinterpret_cast<void**>(&originalString)},
        {0xf0210,reinterpret_cast<void*>(&HookConvert),reinterpret_cast<void**>(&originalConvert)}
        ,{0x13da10,reinterpret_cast<void*>(&HookShow),reinterpret_cast<void**>(&originalShow)}
    };
    unsigned created=0;
    for(const auto& hook:hooks) {
        if(MH_CreateHook(core+hook.rva,hook.detour,hook.original)!=MH_OK)break;
        ++created;
    }
    if(created!=sizeof hooks/sizeof *hooks||stopped.load()) {
        for(unsigned i=0;i<created;++i)MH_RemoveHook(core+hooks[i].rva);
        Log("inactive: incomplete hook preparation; no translation enabled");return;
    }
    for(const auto& hook:hooks)if(MH_QueueEnableHook(core+hook.rva)!=MH_OK){Log("inactive: hook queue failed");return;}
    if(MH_ApplyQueued()!=MH_OK){Log("inactive: hook activation failed; original forwarding retained");return;}
    enabled=true;
    if(stopped.load()){enabled=false;return;}
    Log("ready: experimental Settings zh-CN 0.1.0; pinned TMP 0.7.9.2; five pages; native wide strings; no TMP SDK");
    Log("ready means hooks armed only; in-game loading, appearance and selection still require observation");
}
DWORD WINAPI Worker(void*) {
    const auto start=GetTickCount64();
    while(WaitForSingleObject(stopEvent,100)==WAIT_TIMEOUT) {
        if(HMODULE module=GetModuleHandleW(L"core_ets2mp.dll")){Attach(module);return 0;}
        if(GetTickCount64()-start>120000){Log("inactive: no TMP core; no hooks installed");return 0;}
    }
    return 0;
}
} // namespace

SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version,const scs_telemetry_init_params_t* const params) {
    if(version!=SCS_TELEMETRY_VERSION_1_00&&version!=SCS_TELEMETRY_VERSION_1_01)return SCS_RESULT_unsupported;
    if(!params)return SCS_RESULT_invalid_parameter;
    const auto* p=static_cast<const scs_telemetry_init_params_v100_t*>(params);
    if(!p->common.game_id||std::strcmp(p->common.game_id,"eut2"))return SCS_RESULT_unsupported;
    wchar_t host[MAX_PATH];DWORD length=GetModuleFileNameW(nullptr,host,MAX_PATH);
    const wchar_t* slash=std::wcsrchr(host,L'\\');
    if(!length||length>=MAX_PATH||_wcsicmp(slash?slash+1:host,L"eurotrucks2.exe"))return SCS_RESULT_unsupported;
    if(initialized.exchange(true))return SCS_RESULT_ok;
    HMODULE self=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&scs_telemetry_init),&self))return SCS_RESULT_generic_error;
    DWORD n=GetModuleFileNameW(self,logPath,MAX_PATH);
    if(!n||n>=MAX_PATH)return SCS_RESULT_generic_error;
    auto* pos=std::wcsrchr(logPath,L'\\');
    if(!pos)return SCS_RESULT_generic_error;
    if(wcscpy_s(pos+1,MAX_PATH-(pos+1-logPath),L"TmpSettingsZh.log"))return SCS_RESULT_generic_error;
    stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!stopEvent)return SCS_RESULT_generic_error;
    workerThread=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);
    if(!workerThread){CloseHandle(stopEvent);stopEvent=nullptr;return SCS_RESULT_generic_error;}
    Log("initialized: SCS loader only; waiting for pinned TMP; existing settings values are not changed");
    return SCS_RESULT_ok;
}
SCSAPI_VOID scs_telemetry_shutdown() {
    stopped=true;enabled=false;
    if(stopEvent)SetEvent(stopEvent);
    if(workerThread&&WaitForSingleObject(workerThread,1000)==WAIT_OBJECT_0) {
        CloseHandle(workerThread);workerThread=nullptr;CloseHandle(stopEvent);stopEvent=nullptr;
    }
    char line[200];std::snprintf(line,sizeof line,"shutdown: labels=%u tooltips=%u tabs=%u options=%u window_footer=%u; pinned forwarding remains until process exit",
        counts[0].load(),counts[1].load(),counts[2].load(),counts[3].load(),counts[4].load());Log(line);
}
