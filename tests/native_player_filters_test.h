// Native ownership fixtures execute only in this isolated test process.
#include "native_player_fixture.h"
namespace playerfilterfixture {
template<class T,std::size_t N,std::size_t M>
T Function(const unsigned char (&bytes)[N],const PlayerFixtureCall (&calls)[M]) {
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(code!=nullptr,"player native fixture allocation");std::memcpy(code,bytes,N);
    unsigned stub=static_cast<unsigned>((N+15)&~15);
    for(const auto& call:calls) {
        void* fn=reinterpret_cast<void*>(FixtureError);
        if(call.target==0x54720)fn=reinterpret_cast<void*>(FixtureFree);
        if(call.target==0x81c50)fn=reinterpret_cast<void*>(playerfilters::moveString);
        if(call.target==0x82a90)fn=reinterpret_cast<void*>(DestroyNarrow);
        code[stub]=0x48;code[stub+1]=0xb8;std::memcpy(code+stub+2,&fn,8);
        code[stub+10]=0xff;code[stub+11]=0xe0;
        const auto displacement=static_cast<std::int32_t>(stub-call.offset-5);
        std::memcpy(code+call.offset+1,&displacement,4);stub+=16;
    }
    DWORD old;Check(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old)!=0,"player native fixture executable");
    FlushInstructionCache(GetCurrentProcess(),code,4096);return reinterpret_cast<T>(code);
}
struct List {
    alignas(8) unsigned char panel[0x1b0]{};
    std::vector<std::array<unsigned char,0x98>> rows;
    explicit List(unsigned size):rows(size) {Fill(size);}
    unsigned char* Model(){return panel+0x118;}
    unsigned char* Begin(){return rows.empty()?nullptr:rows.front().data();}
    std::size_t Size() {
        return (reinterpret_cast<std::uintptr_t>(PointerAt(Model(),0x70))-
                reinterpret_cast<std::uintptr_t>(Begin()))/0x98;
    }
    void Clear() {
        for(std::size_t n=0;n<Size();++n)for(unsigned offset:{0x20u,0x40u,0x60u})
            DestroyNarrow(*reinterpret_cast<NativeString*>(rows[n].data()+offset));
        *reinterpret_cast<void**>(Model()+0x70)=Begin();
    }
    void Fill(unsigned count) {
        if(PointerAt(Model(),0x68))Clear();
        Check(count<=rows.size(),"fixture capacity sufficient");
        for(unsigned i=0;i<count;++i) {
            auto* row=rows[i].data();std::memset(row,0,0x98);
            *reinterpret_cast<unsigned*>(row+8)=i;
            row[0x1e]=(i&1)!=0;row[0x1f]=(i&2)!=0;row[0x1c]=(i&4)!=0;
            for(unsigned offset:{0x20u,0x40u,0x60u}) {
                const std::string value="long native player payload "+std::to_string(i)+"/"+std::to_string(offset);
                MakeNarrow(*reinterpret_cast<NativeString*>(row+offset),value.c_str());
            }
        }
        *reinterpret_cast<void**>(Model()+0x68)=Begin();
        auto* end=count?Begin()+count*0x98:Begin();
        *reinterpret_cast<void**>(Model()+0x70)=end;
        *reinterpret_cast<void**>(Model()+0x78)=rows.empty()?nullptr:Begin()+rows.size()*0x98;
    }
    ~List(){Clear();}
};
List* current=nullptr;
unsigned resets=0,dirty=0,builds=0,releaseCalls=0;
std::string argument,display;
std::vector<std::int64_t> pushed;
alignas(8) unsigned char references[16]{};
void Rebuild(void*){++builds;current->Fill(static_cast<unsigned>(current->rows.size()));}
bool Refresh(void* model){playerfilters::HookRebuild(model);return true;}
void Dirty(void*){++dirty;}
void Reset(void*){++resets;}
void* Context(void*){return current;}
playerfilters::SharedPanel* Panel(playerfilters::SharedPanel* out) {
    out->object=current?current->panel:nullptr;out->counter=current?references:nullptr;
    if(out->counter)InterlockedIncrement(reinterpret_cast<volatile LONG*>(references+8));
    return out;
}
const char* String(void*,int index,std::size_t*){Check(index==1,"filter argument index");return argument.c_str();}
void Integer(void*,std::int64_t value){pushed.push_back(value);}
void Variant(void*,const char* value){display=value;}
void Count(void*,void*){display="original nearby count";}
void OriginalRelease(void*){++releaseCalls;}
void Noop(void*){}
int top=3,registerCalls=0;
std::vector<std::string> bindings;
void Register(){++registerCalls;}
void* LuaState(){return reinterpret_cast<void*>(1);}
int Top(void*){return top;}
void SetTop(void*,int n){top=n;}
void GetField(void*,int index,const char* name){
    Check((index==-10002&&!std::strcmp(name,"PlayerPanel"))||
          (index==-1&&!std::strcmp(name,"ZHRefreshFilterTabs")),"scoped player Lua table access");++top;
}
void Closure(void*,playerfilters::LuaCall callback,int n){Check(callback&&n==0,"Lua C closure has no upvalues");++top;}
void Field(void*,int index,const char* name){Check(index==4,"Lua binding absolute table index");bindings.emplace_back(name);--top;}
unsigned notifications=0;
int Protected(void*,int args,int results,int handler) {
    Check(args==0&&results==0&&handler==0,"document notification is a protected Lua call");
    ++notifications;
    if(current)playerfilters::HookRefresh(current->Model());
    return 0;
}

void Run() {
    using namespace playerfilters;
    Check(!Verify(),"filters independently refuse unknown image");
    for(const auto& g:playerfiltersabi::kGuards)std::memcpy(core+g.rva,g.bytes,g.size);
    Check(Verify(),"reviewed player ABI accepted");
    core[0x1cdc10]^=1;
    Check(!Verify()&&VerifyImage(),"player ABI drift leaves Settings compatibility intact");core[0x1cdc10]^=1;
    core[0x3000]^=1;Check(Verify(),"unrelated bytes do not gate player filters");core[0x3000]^=1;
    moveString=Function<MoveString>(kPlayerMoveString,kPlayerMoveStringCalls);
    eraseRows=Function<EraseRows>(kPlayerEraseRows,kPlayerEraseRowsCalls);
    active=true;enabled=true;stopped=false;
    Mode parsed=Mode::Staff;
    Check(!playerfilterlogic::ParseMode("nearby",parsed)&&parsed==Mode::Staff,"unknown filter cannot change mode");
    Check(!playerfilterlogic::ParseMode(nullptr,parsed),"null mode refused");
    const char* names[]={"all","friends","company","staff"};
    for(unsigned m=0;m<4;++m) {
        Check(playerfilterlogic::ParseMode(names[m],parsed)&&static_cast<unsigned>(parsed)==m,"four explicit filter modes");
        List list(2048);Adopt(list.panel);state.mode=parsed;
        const std::array<unsigned char,0x180> before=[&](){std::array<unsigned char,0x180> a{};std::memcpy(a.data(),list.panel,a.size());return a;}();
        Check(Apply(list.Model()),"filter complete projection");
        Check(state.counts.candidates==2048&&state.counts.matched==(m?1024u:2048u),"independent category counts across all flag combinations");
        unsigned expected=0;
        for(unsigned n=0;n<list.Size();++n) {
            if(m)while((expected&(m==1?1u:m==2?2u:4u))==0)++expected;
            const auto* row=list.rows[n].data();
            Check(*reinterpret_cast<const unsigned*>(row+8)==expected,"stable order includes matches beyond first viewport");
            for(unsigned offset:{0x20u,0x40u,0x60u}) {
                const std::string value="long native player payload "+std::to_string(expected)+"/"+std::to_string(offset);
                Check(NarrowIs(row+offset,value.c_str()),"all owned native strings survive movement");
            }
            ++expected;
        }
        Check(std::memcmp(before.data(),list.panel,before.size())==0,"source lists and non-projection fields unchanged");
    }
    {
        List empty(0);Adopt(empty.panel);state.mode=Mode::Friends;
        Check(Apply(empty.Model())&&state.counts.matched==0,"empty projection accepted");
        List none(8);for(auto& row:none.rows)row[0x1e]=0;
        Adopt(none.panel);state.mode=Mode::Friends;
        Check(Apply(none.Model())&&none.Size()==0,"zero matches destroys every native payload once");
        List all(8);for(auto& row:all.rows)row[0x1c]=1;
        Adopt(all.panel);state.mode=Mode::Staff;
        Check(Apply(all.Model())&&all.Size()==8,"all matches keep native storage");
        auto* invalid=all.Model();*reinterpret_cast<void**>(invalid+0x70)=all.Begin()+1;
        Check(!Apply(invalid),"misaligned row range refused before mutation");
        *reinterpret_cast<void**>(invalid+0x70)=all.Begin()+8*0x98;
    }
    // Verify native callback lifecycle, rebuilt sources and history/search mode.
    originalRebuild=Rebuild;originalRelease=OriginalRelease;originalCount=Count;
    refresh=Refresh;dirtyAll=Dirty;getPanel=Panel;getContext=Context;resetSelection=Reset;
    luaString=String;luaInteger=Integer;variantString=Variant;
    *reinterpret_cast<LONG*>(references+8)=1;*reinterpret_cast<LONG*>(references+12)=1;
    {
        List list(8);current=&list;state={};argument="friends";
        Check(LuaSetFilter(nullptr)==1&&pushed.back()==1&&list.Size()==4,"Lua mode change rebuilds then filters complete source");
        Check(resets==1&&dirty==2,"mode switch clears selection and dirties both native models");
        argument="company";LuaSetFilter(nullptr);
        Check(list.Size()==4&&*reinterpret_cast<unsigned*>(list.rows[0].data()+8)==2,"second tab restores original candidates including overlap");
        Check(state.counts.candidates==8,"candidates are never narrowed cumulatively");
        alignas(8) void* closure[2]={nullptr,list.panel};HookCount(closure,nullptr);
        Check(display=="4 / 8","header counts use full search result rather than viewport");
        argument="invalid";const auto old=state.mode;LuaSetFilter(nullptr);
        Check(pushed.back()==0&&state.mode==old,"invalid request leaves active mode unchanged");
        LuaMode(nullptr);Check(pushed.back()==2,"Lua mode getter restores active tab");
        argument="all";LuaSetFilter(nullptr);Check(list.Size()==8,"all restores full original list");
        Check(*reinterpret_cast<LONG*>(references+8)==1,"native shared panel reference released for every request");
        HookRelease(list.panel);Check(!state.owner&&releaseCalls==1,"panel release resets side state before native destruction");
    }
    current=nullptr;active=false;argument="friends";LuaSetFilter(nullptr);
    Check(pushed.back()==0,"inactive native feature refuses UI mutation");
    HookCount(nullptr,nullptr);Check(display=="original nearby count","inactive count forwards to original");
    originalRegister=Register;luaState=LuaState;luaGetTop=Top;luaSetTop=SetTop;
    luaGetField=GetField;luaPushClosure=Closure;luaSetField=Field;
    HookRegister();Check(bindings.empty()&&registerCalls==1,"inactive registration preserves original table only");
    active=true;HookRegister();
    Check(top==3&&bindings==std::vector<std::string>{"FiltersReady","GetFilter","SetFilter"},"Lua extension restores stack and original registration");
    Check(registerCalls==2,"original Lua registration always runs");
    {
        List list(8);current=&list;state={};boundLua=nullptr;notifiedOwner=nullptr;
        luaProtectedCall=Protected;originalRefresh=Refresh;
        Check(HookRefresh(list.Model()),"late attachment retains native viewport-change result");
        Check(notifications==1&&top==3&&boundLua==LuaState(),"late UI-thread registration notifies existing document once");
        HookRefresh(list.Model());Check(notifications==1,"native refresh notification cannot recurse or repeat");
        HookRelease(list.panel);Check(!notifiedOwner&&!state.owner,"release drops document callback and side state");
        Check(bindings.back()=="ZHRefreshFilterTabs","released document callback removed before destruction");
        current=nullptr;
    }
    // Independent linear-work bound; move callback has no copy of native owners.
    std::array<unsigned char,0x98*128> raw{};unsigned moves=0,erases=0;
    for(unsigned i=80;i<128;++i)raw[i*0x98+0x1e]=1;
    const auto count=playerfilterlogic::Compact(raw.data(),128,Mode::Friends,
        [&](unsigned char*,unsigned char*){++moves;},[&](unsigned char*,unsigned char*){++erases;});
    Check(count.matched==48&&moves==48&&erases==1,"linear compaction finds offscreen-only matches");
    active=false;state={};
    VirtualFree(reinterpret_cast<void*>(moveString),0,MEM_RELEASE);
    VirtualFree(reinterpret_cast<void*>(eraseRows),0,MEM_RELEASE);
}
}
