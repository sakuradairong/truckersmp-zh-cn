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
thread_local bool notifying=false;
void ForgetDocument();
bool Ready(){return active.load()&&enabled.load()&&!stopped.load();}
void Adopt(void* panel){if(state.owner!=panel){state={};state.owner=panel;}}
void Disable(){active=false;state={};Log("inactive: player filters layout guard; original player list retained");}

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
    if(state.owner==panel){ForgetDocument();state={};notifiedOwner=nullptr;}
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
                             Binding{"SetFilter",LuaSetFilter}}) {
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
    if(lua&&Ready()&&state.valid&&notifiedOwner!=state.owner&&!notifying) {
        Scope<bool> scope(notifying,true);
        const int top=luaGetTop(lua);
        luaGetField(lua,-10002,"PlayerPanel");
        luaGetField(lua,-1,"ZHRefreshFilterTabs");
        // Missing/old scripts are a normal fallback. pcall catches nil and
        // script errors; restore the exact stack regardless of its result.
        const int result=luaProtectedCall(lua,0,0,0);
        luaSetTop(lua,top);
        if(result==0)notifiedOwner=state.owner;
    }
    return changed;
}
bool Verify() {
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
}
void Attach() {
    if(!Verify()){Log("inactive: player filters code/ABI mismatch; original player list retained");return;}
    luaGetTop=reinterpret_cast<LuaGetTop>(core+0x729b10);
    luaState=reinterpret_cast<LuaState>(core+0x8fad10);
    luaSetTop=reinterpret_cast<LuaSetTop>(core+0x72a9e0);
    luaGetField=reinterpret_cast<LuaGetField>(core+0x729940);
    luaPushClosure=reinterpret_cast<LuaPushClosure>(core+0x72a0c0);
    luaSetField=reinterpret_cast<LuaSetField>(core+0x72a770);
    luaString=reinterpret_cast<LuaString>(core+0x728c90);
    luaInteger=reinterpret_cast<LuaInteger>(core+0x72a1f0);
    luaProtectedCall=reinterpret_cast<LuaProtectedCall>(core+0x729fc0);
    moveString=reinterpret_cast<MoveString>(core+0x81c50);
    eraseRows=reinterpret_cast<EraseRows>(core+0x1d3010);
    refresh=HookRefresh;
    dirtyAll=reinterpret_cast<Build>(core+0x7f5e70);
    getPanel=reinterpret_cast<GetPanel>(core+0x1cef50);
    getContext=reinterpret_cast<GetContext>(core+0x1cbc50);
    resetSelection=reinterpret_cast<Build>(core+0x1b8780);
    variantString=reinterpret_cast<VariantString>(core+0x805480);
    struct Hook {unsigned rva;void* replacement;void** original;};
    const Hook hooks[]={
        {0x1cdc10,reinterpret_cast<void*>(HookRebuild),reinterpret_cast<void**>(&originalRebuild)},
        {0x1cb500,reinterpret_cast<void*>(HookRelease),reinterpret_cast<void**>(&originalRelease)},
        {0x1d66c0,reinterpret_cast<void*>(HookCount),reinterpret_cast<void**>(&originalCount)},
        {0x1cd970,reinterpret_cast<void*>(HookRefresh),reinterpret_cast<void**>(&originalRefresh)},
        {0x1cf710,reinterpret_cast<void*>(HookRegister),reinterpret_cast<void**>(&originalRegister)}};
    unsigned created=0;
    for(const auto& hook:hooks) {
        if(MH_CreateHook(core+hook.rva,hook.replacement,hook.original)!=MH_OK)break;
        ++created;
    }
    if(created!=sizeof hooks/sizeof *hooks) {
        for(unsigned i=0;i<created;++i)MH_RemoveHook(core+hooks[i].rva);
        Log("inactive: player filters hook preparation failed");return;
    }
    // Activate only these hooks; Settings uses its own compatibility boundary.
    active=true;
    for(const auto& hook:hooks)if(MH_EnableHook(core+hook.rva)!=MH_OK) {
        active=false;
        for(const auto& undo:hooks)MH_DisableHook(core+undo.rva);
        Log("inactive: player filters activation failed");return;
    }
    Log("ready: four player filter tabs; complete UI projection before native sorting/viewport; live validation outstanding");
}
}
