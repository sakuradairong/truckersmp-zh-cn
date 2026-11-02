// Included inside the adapter namespace, after the shared memory helpers.
// All mutations are confined to the native UI projection and selection state.
namespace playerfilters {
using Mode=playerfilterlogic::Mode;
using LuaCall=int(*)(void*);
using Register=void(*)();
using LuaState=void*(*)();
using LuaGetTop=int(*)(void*);
using LuaSetTop=void(*)(void*,int);
using LuaGetField=void(*)(void*,int,const char*);
using LuaPushClosure=void(*)(void*,LuaCall,int);
using LuaSetField=void(*)(void*,int,const char*);
using LuaString=const char*(*)(void*,int,std::size_t*);
using LuaInteger=void(*)(void*,std::int64_t);
using LuaPushString=void(*)(void*,const char*);
using LuaProtectedCall=int(*)(void*,int,int,int);
using MoveString=void*(*)(void*,void*);
using EraseRows=void*(*)(void*,void*,void*,void*);
using Refresh=bool(*)(void*);
using CountGetter=void(*)(void*,void*);
using VariantString=void(*)(void*,const char*);
struct SharedPanel {void* object=nullptr;void* counter=nullptr;};
using GetPanel=SharedPanel*(*)(SharedPanel*);
using GetContext=void*(*)(void*);

Build originalRebuild=nullptr,originalRelease=nullptr;
Register originalRegister=nullptr;
LuaState luaState=nullptr;
CountGetter originalCount=nullptr;
LuaGetTop luaGetTop=nullptr;
LuaSetTop luaSetTop=nullptr;
LuaGetField luaGetField=nullptr;
LuaPushClosure luaPushClosure=nullptr;
LuaSetField luaSetField=nullptr;
LuaString luaString=nullptr;
LuaInteger luaInteger=nullptr;
LuaPushString luaPushString=nullptr;
MoveString moveString=nullptr;
EraseRows eraseRows=nullptr;
Refresh refresh=nullptr;
Refresh originalRefresh=nullptr;
LuaProtectedCall luaProtectedCall=nullptr;
Build dirtyAll=nullptr,resetSelection=nullptr;
GetPanel getPanel=nullptr;
GetContext getContext=nullptr;
VariantString variantString=nullptr;
std::atomic<bool> active{false};
struct State {
    void* owner=nullptr;
    Mode mode=Mode::All;
    playerfilterlogic::Counts counts{};
    bool valid=false;
};
State state;
void* boundLua=nullptr;
void* notifiedOwner=nullptr;
ULONGLONG lastNotification=0;
thread_local bool notifying=false;
void ForgetDocument();
bool Ready(){return active.load()&&!stopped.load();}
void Adopt(void* panel){if(state.owner!=panel){state={};state.owner=panel;}}
void Disable(){active=false;state={};playerprofile::Snapshot(0,GetTickCount64());Log("inactive: player filters layout guard; original player list retained");}

bool Range(void* model,unsigned char*& begin,std::size_t& count) {
    if(!Readable(model,0x88))return false;
    auto* p=static_cast<unsigned char*>(model);
    const auto first=reinterpret_cast<std::uintptr_t>(PointerAt(p,0x68));
    const auto last=reinterpret_cast<std::uintptr_t>(PointerAt(p,0x70));
    const auto limit=reinterpret_cast<std::uintptr_t>(PointerAt(p,0x78));
    if(last<first||limit<last||(last-first)%playerfilterlogic::kRowSize||
       (limit-first)%playerfilterlogic::kRowSize)return false;
    count=(last-first)/playerfilterlogic::kRowSize;
    begin=reinterpret_cast<unsigned char*>(first);
    return count==0||Readable(begin,last-first);
}
void MoveRow(unsigned char* destination,unsigned char* source) {
    std::memcpy(destination,source,0x20);
    for(unsigned offset:{0x20u,0x40u,0x60u})moveString(destination+offset,source+offset);
    std::memcpy(destination+0x80,source+0x80,0x18);
}
bool Apply(void* model) {
    unsigned char* begin=nullptr;std::size_t count=0;
    if(!Range(model,begin,count))return false;
    auto* panel=static_cast<unsigned char*>(model)-0x118;
    if(!Readable(panel,0x1a0))return false;
    Adopt(panel);
    state.counts=playerfilterlogic::Compact(begin,count,state.mode,MoveRow,
        [model](unsigned char* first,unsigned char* last) {
            void* result=nullptr;
            eraseRows(static_cast<unsigned char*>(model)+0x68,&result,first,last);
        });
    state.valid=true;
    return true;
}
void HookRebuild(void* model) {
    originalRebuild(model);
    if(Ready()&&!Apply(model))Disable();
}
void HookRelease(void* panel) {
    if(state.owner==panel){ForgetDocument();state={};notifiedOwner=nullptr;playerprofile::Snapshot(0,GetTickCount64());}
    originalRelease(panel);
}
void HookCount(void* closure,void* value) {
    if(Ready()&&state.valid&&Readable(closure,16)&&PointerAt(closure,8)==state.owner) {
        char text[64];
        std::snprintf(text,sizeof text,"%zu / %zu",state.counts.matched,state.counts.candidates);
        variantString(value,text);
    } else originalCount(closure,value);
}
void Release(SharedPanel& panel) {
    if(!panel.counter)return;
    auto* counter=static_cast<unsigned char*>(panel.counter);
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(counter+8))==0) {
        auto** table=*reinterpret_cast<void***>(counter);
        reinterpret_cast<Build>(table[0])(counter);
        if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(counter+12))==0)
            reinterpret_cast<Build>(table[1])(counter);
    }
    panel={};
}
int LuaReady(void* lua){luaInteger(lua,Ready()?1:0);return 1;}
int LuaMode(void* lua){luaInteger(lua,Ready()?static_cast<unsigned>(state.mode):0);return 1;}
int LuaClearProfile(void*){playerprofile::Snapshot(0,GetTickCount64());return 0;}
int LuaLocalProfile(void* lua) {
    playerprofile::Local local;SharedPanel panel;
    if(Ready())getPanel(&panel);
    const bool found=panel.object&&Readable(panel.object,0x1a0)&&
        playerprofile::ReadLocal(static_cast<unsigned char*>(panel.object)+0x118,local);
    Release(panel); // Do not retain native ownership across Lua stack allocation.
    const auto vtc=playerprofile::Snapshot(found?local.account:0,GetTickCount64());
    if(!found){luaInteger(lua,0);return 1;}
    luaInteger(lua,local.account);luaInteger(lua,local.server);luaPushString(lua,local.name);
    luaInteger(lua,vtc.status);luaPushString(lua,vtc.name);luaInteger(lua,vtc.vtc);return 6;
}
int LuaSetFilter(void* lua) {
    // Validate the Lua argument before acquiring a native shared reference.
    const char* text=luaString(lua,1,nullptr);Mode mode;
    if(!Ready()||!playerfilterlogic::ParseMode(text,mode)){luaInteger(lua,0);return 1;}
    SharedPanel panel;getPanel(&panel);
    bool accepted=false;
    if(panel.object&&Readable(panel.object,0x1a0)) {
        auto* object=static_cast<unsigned char*>(panel.object);
        Adopt(object);
        const bool changed=state.mode!=mode;
        state.mode=mode;state.valid=false;
        if(changed) {
            void* context=getContext(object);
            if(context)resetSelection(context);
        }
        refresh(object+0x118);
        dirtyAll(object+0x50);
        dirtyAll(object+0x48);
        accepted=Ready()&&state.valid;
    }
    Release(panel);
    luaInteger(lua,accepted?1:0);return 1;
}
void BindLua(void* lua) {
    if(!lua)return;
    const int top=luaGetTop(lua);
    luaGetField(lua,-10002,"PlayerPanel");
    const int table=luaGetTop(lua);
    struct Binding {const char* name;LuaCall call;};
    for(const auto& binding:{Binding{"FiltersReady",LuaReady},Binding{"GetFilter",LuaMode},
                             Binding{"SetFilter",LuaSetFilter},Binding{"GetLocalProfile",LuaLocalProfile},
                             Binding{"ClearLocalProfile",LuaClearProfile}}) {
        luaPushClosure(lua,binding.call,0);
        luaSetField(lua,table,binding.name);
    }
    luaSetTop(lua,top);
    boundLua=lua;
}
void ForgetDocument() {
    if(!Ready()||!boundLua)return;
    const int top=luaGetTop(boundLua);
    luaGetField(boundLua,-10002,"PlayerPanel");
    const int table=luaGetTop(boundLua);
    luaInteger(boundLua,0);
    luaSetField(boundLua,table,"ZHRefreshFilterTabs");
    luaSetTop(boundLua,top);
}
void HookRegister() {
    originalRegister();
    if(Ready()){BindLua(luaState());notifiedOwner=nullptr;}
}
bool HookRefresh(void* model) {
    // This runs on the native UI thread, including when the SCS module attached
    // after the document was created. Never touch the Lua VM from Worker().
    void* lua=Ready()?luaState():nullptr;
    if(lua&&lua!=boundLua)BindLua(lua);
    const bool changed=originalRefresh(model);
    const auto now=GetTickCount64();
    if(lua&&Ready()&&state.valid&&(notifiedOwner!=state.owner||now-lastNotification>=1000)&&!notifying) {
        Scope<bool> scope(notifying,true);
        const int top=luaGetTop(lua);
        luaGetField(lua,-10002,"PlayerPanel");
        luaGetField(lua,-1,"ZHRefreshFilterTabs");
        // Missing/old scripts are a normal fallback. pcall catches nil and
        // script errors; restore the exact stack regardless of its result.
        const int result=luaProtectedCall(lua,0,0,0);
        luaSetTop(lua,top);
        if(result==0){notifiedOwner=state.owner;lastNotification=now;}
    }
    return changed;
}
bool Verify() {
#ifndef TMPZH_TEST_FIXTURE
    return (compatibility.capabilities&4)!=0;
#else
    if(!Readable(core,4096))return false;
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(core);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<0||dos->e_lfanew>2048)return false;
    auto* pe=reinterpret_cast<IMAGE_NT_HEADERS64*>(core+dos->e_lfanew);
    if(pe->Signature!=IMAGE_NT_SIGNATURE||pe->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64)return false;
    const auto imageSize=pe->OptionalHeader.SizeOfImage;
    for(const auto& g:playerfiltersabi::kGuards)
        if(g.rva>=imageSize||g.size>imageSize-g.rva||!Readable(core+g.rva,g.size)||
           std::memcmp(core+g.rva,g.bytes,g.size))return false;
    return true;
#endif
}
void Attach() {
    if(stopped.load())return;
    if(!Verify()){Log("inactive: player filters code/ABI mismatch; original player list retained");return;}
    luaGetTop=reinterpret_cast<LuaGetTop>(core+Rva(0x729e90));
    luaState=reinterpret_cast<LuaState>(core+Rva(0x8fb080));
    luaSetTop=reinterpret_cast<LuaSetTop>(core+Rva(0x72ad60));
    luaGetField=reinterpret_cast<LuaGetField>(core+Rva(0x729cc0));
    luaPushClosure=reinterpret_cast<LuaPushClosure>(core+Rva(0x72a440));
    luaSetField=reinterpret_cast<LuaSetField>(core+Rva(0x72aaf0));
    luaString=reinterpret_cast<LuaString>(core+Rva(0x729010));
    luaInteger=reinterpret_cast<LuaInteger>(core+Rva(0x72a570));
    luaPushString=reinterpret_cast<LuaPushString>(core+Rva(0x72a6d0));
    luaProtectedCall=reinterpret_cast<LuaProtectedCall>(core+Rva(0x72a340));
    moveString=reinterpret_cast<MoveString>(core+Rva(0x81c90));
    eraseRows=reinterpret_cast<EraseRows>(core+Rva(0x1d3190));
    refresh=HookRefresh;
    dirtyAll=reinterpret_cast<Build>(core+Rva(0x7f61f0));
    getPanel=reinterpret_cast<GetPanel>(core+Rva(0x1cf0d0));
    getContext=reinterpret_cast<GetContext>(core+Rva(0x1cbdd0));
    resetSelection=reinterpret_cast<Build>(core+Rva(0x1b8900));
    variantString=reinterpret_cast<VariantString>(core+Rva(0x805800));
    struct Hook {unsigned rva;void* replacement;void** original;};
    const Hook hooks[]={
        {0x1cdd90,reinterpret_cast<void*>(HookRebuild),reinterpret_cast<void**>(&originalRebuild)},
        {0x1cb680,reinterpret_cast<void*>(HookRelease),reinterpret_cast<void**>(&originalRelease)},
        {0x1d6860,reinterpret_cast<void*>(HookCount),reinterpret_cast<void**>(&originalCount)},
        {0x1cdaf0,reinterpret_cast<void*>(HookRefresh),reinterpret_cast<void**>(&originalRefresh)},
        {0x1cf890,reinterpret_cast<void*>(HookRegister),reinterpret_cast<void**>(&originalRegister)}};
    unsigned created=0;
    for(const auto& hook:hooks) {
        if(stopped.load()||!HookUnchanged(hook.rva)||MH_CreateHook(core+Rva(hook.rva),hook.replacement,hook.original)!=MH_OK)break;
        ++created;
    }
    if(created!=sizeof hooks/sizeof *hooks) {
        for(unsigned i=0;i<created;++i)MH_RemoveHook(core+Rva(hooks[i].rva));
        Log("inactive: player filters hook preparation failed");return;
    }
    // Activate only these hooks; Settings uses its own compatibility boundary.
    for(const auto& hook:hooks)if(stopped.load()||MH_EnableHook(core+Rva(hook.rva))!=MH_OK) {
        active=false;
        for(const auto& undo:hooks)MH_DisableHook(core+Rva(undo.rva));
        Log("inactive: player filters activation failed");return;
    }
    active=true;
    Log("ready: four player filter tabs; complete UI projection before native sorting/viewport; live validation outstanding");
}
}
