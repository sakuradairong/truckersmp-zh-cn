#include "freight_native.h"
#include <algorithm>
#include <array>
#include <limits>
#include <vector>
#include <cstring>

#ifndef COMPANION_NATIVE_FREIGHT
#define COMPANION_NATIVE_FREIGHT 0
#endif
#if COMPANION_NATIVE_FREIGHT
#include "freight_profile.h"
#endif

namespace overlay {
namespace {
bool Read(std::uintptr_t address,void* out,std::size_t bytes) {
    if(address<0x10000||bytes>std::numeric_limits<std::uintptr_t>::max()-address)return false;
    SIZE_T count=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),out,bytes,&count)&&count==bytes;
}
template<class T> bool Read(std::uintptr_t address,T& out){return Read(address,&out,sizeof out);}
bool Span(std::uintptr_t at,std::size_t size,std::uintptr_t first,std::uintptr_t last){return at>=first&&at<=last&&size<=last-at;}
bool Page(std::uintptr_t address,bool execute) {
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof info)||info.State!=MEM_COMMIT||(info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    const DWORD p=info.Protect&0xff;
    return execute?(p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY):
        (p==PAGE_READONLY||p==PAGE_READWRITE||p==PAGE_WRITECOPY||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY);
}
bool ReadOnlyPage(std::uintptr_t address){
    MEMORY_BASIC_INFORMATION info{};
    return VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof info)&&info.State==MEM_COMMIT&&
        !(info.Protect&(PAGE_GUARD|PAGE_NOACCESS))&&(info.Protect&0xff)==PAGE_READONLY;
}
struct Image {
    std::uintptr_t first=0,last=0;
    std::vector<IMAGE_SECTION_HEADER> sections;
    struct Code{std::uintptr_t address;std::vector<unsigned char> bytes;};
    std::vector<Code> code;
    bool Open(HMODULE module){
        first=reinterpret_cast<std::uintptr_t>(module);IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 pe{};
        if(!Read(first,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<sizeof dos||dos.e_lfanew>0x100000)return false;
        if(!Read(first+dos.e_lfanew,pe)||pe.Signature!=IMAGE_NT_SIGNATURE||pe.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||pe.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return false;
        auto size=pe.OptionalHeader.SizeOfImage;
        if(size<0x10000||size>128*1024*1024||size>std::numeric_limits<std::uintptr_t>::max()-first||!pe.FileHeader.NumberOfSections||pe.FileHeader.NumberOfSections>64)return false;
        last=first+size;
        const auto table=first+dos.e_lfanew+offsetof(IMAGE_NT_HEADERS64,OptionalHeader)+pe.FileHeader.SizeOfOptionalHeader;
        sections.resize(pe.FileHeader.NumberOfSections);
        if(!Span(table,sections.size()*sizeof(IMAGE_SECTION_HEADER),first,last)||!Read(table,sections.data(),sections.size()*sizeof(IMAGE_SECTION_HEADER)))return false;
        for(const auto& s:sections)if(!Span(first+s.VirtualAddress,s.Misc.VirtualSize,first,last))return false;
        for(const auto& s:sections)if((s.Characteristics&IMAGE_SCN_MEM_EXECUTE)&&s.Misc.VirtualSize){
            Code part{first+s.VirtualAddress,std::vector<unsigned char>(s.Misc.VirtualSize)};
            if(!Read(part.address,part.bytes.data(),part.bytes.size()))return false;code.push_back(std::move(part));
        }
        return true;
    }
    bool Section(std::uintptr_t at,std::size_t size,bool executable)const{
        for(const auto& s:sections)if(Span(at,size,first+s.VirtualAddress,first+s.VirtualAddress+s.Misc.VirtualSize))
            return executable?(s.Characteristics&IMAGE_SCN_MEM_EXECUTE)!=0:(s.Characteristics&IMAGE_SCN_MEM_WRITE)!=0&&(s.Characteristics&IMAGE_SCN_MEM_EXECUTE)==0;
        return false;
    }
#if COMPANION_NATIVE_FREIGHT
    bool Region(std::uintptr_t at,std::size_t size,freight_profile::Region region)const{
        for(const auto& s:sections)if(Span(at,size,first+s.VirtualAddress,first+s.VirtualAddress+s.Misc.VirtualSize)){
            if(!(s.Characteristics&IMAGE_SCN_MEM_READ))return false;
            const bool executable=(s.Characteristics&IMAGE_SCN_MEM_EXECUTE)!=0,writable=(s.Characteristics&IMAGE_SCN_MEM_WRITE)!=0;
            return region==freight_profile::Region::Code?executable:
                !executable&&(region==freight_profile::Region::Writable?writable:!writable);
        }
        return false;
    }
    const char* References(const freight_profile::Pattern& pattern,std::uintptr_t candidate)const{
        for(std::size_t i=0;i<pattern.referenceCount;++i){
            const auto& ref=pattern.references[i];
            if(ref.at>pattern.size||ref.length>pattern.size-ref.at||
               !Region(Relative(candidate+ref.at,ref.displacement,ref.length),ref.width,ref.region))return "reference-section-changed";
            if(ref.literalSize){
                std::array<unsigned char,256> bytes{};
                const auto target=Relative(candidate+ref.at,ref.displacement,ref.length);
                if(ref.literalSize>bytes.size()||!Region(target,ref.literalSize,freight_profile::Region::ReadOnly)||
                   !Read(target,bytes.data(),ref.literalSize)||std::memcmp(bytes.data(),ref.literal,ref.literalSize))return "referenced-name-changed";
            }
        }
        return nullptr;
    }
    std::uintptr_t Find(const freight_profile::Pattern& pattern,std::string& diagnostic)const{
        const auto fail=[&](const char* why)->std::uintptr_t{diagnostic+=std::string(pattern.name)+":"+why+"; ";return 0;};
        if(!pattern.size||pattern.size>4096||!pattern.mask[0])return fail("invalid-pattern");
        std::uintptr_t found=0;const char* rejected=nullptr;
        for(const auto& part:code){
            const auto& bytes=part.bytes;if(bytes.size()<pattern.size)continue;
            for(std::size_t at=0;at<=bytes.size()-pattern.size;++at){
                if(bytes[at]!=pattern.bytes[0])continue;
                std::size_t i=1;for(;i<pattern.size;++i)if(pattern.mask[i]&&bytes[at+i]!=pattern.bytes[i])break;
                if(i!=pattern.size)continue;
                if(const auto why=References(pattern,part.address+at)){rejected=why;continue;}
                if(found)return fail("ambiguous"); // Never select the first candidate.
                found=part.address+at;
            }
        }
        if(!found)return fail(rejected?rejected:"missing");
        return found;
    }
#endif
    std::uintptr_t Relative(std::uintptr_t instruction,unsigned displacement,unsigned length)const{
        std::int32_t offset=0;
        if(displacement>length||length-displacement<sizeof offset||!Section(instruction,length,true)||!Read(instruction+displacement,offset))return 0;
        const auto next=instruction+length;
        const auto target=static_cast<std::int64_t>(next)+offset;
        return target>0&&Span(static_cast<std::uintptr_t>(target),1,first,last)?static_cast<std::uintptr_t>(target):0;
    }
};
struct Invocation {std::uintptr_t players,manager,ui,find,request,event,first,last,onlineRoot,managerTable;};
bool OnlineReady(std::uintptr_t global){
    std::uintptr_t root=0,state=0,account=0;int flag=0;
    return Read(global,root)&&root&&Read(root,state)&&state&&Read(state+0x11d8,account)&&account&&Read(account+0x68,flag)&&flag!=0;
}
int ExceptionFilter(unsigned code){return code==EXCEPTION_ACCESS_VIOLATION||code==EXCEPTION_IN_PAGE_ERROR||code==EXCEPTION_ILLEGAL_INSTRUCTION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH;}
// Keep SEH outside C++ objects needing stack unwinding. A fault latches this
// adapter off; it cannot undo side effects or make native calls crash-proof.
NativeFreight::Result Invoke(const Invocation& b,int id,bool online,NativeCallTrace& trace){
    __try {
        trace.stage="copy.objects";
        std::uintptr_t host=0,list=0,manager=0,table=0,method=0,ui=0,context=0;
        if(!Read(b.players,host)||!Read(host+0x18,list)||!list||!Page(list,false)||
           !Read(b.manager,manager)||!manager||!Read(manager,table)||table!=b.managerTable||!Span(table,16,b.first,b.last)||!ReadOnlyPage(table)||
           !Read(table+8,method)||method!=b.request||!Page(method,true)||
           !Read(b.ui,ui)||!Read(ui+8,context)||!context||!Page(context,false))return NativeFreight::Result::StateChanged;
        std::uintptr_t dispatcher=0;
        trace.stage="copy.online-condition";
        if(online&&(!OnlineReady(b.onlineRoot)||!Read(manager+0x20,dispatcher)||!dispatcher))return NativeFreight::Result::StateChanged;
        using Find=void*(__fastcall*)(void*,int);
        trace.stage="copy.player";
        void* player=reinterpret_cast<Find>(b.find)(reinterpret_cast<void*>(list),id);
        int actual=-1;
        if(!player||!Read(reinterpret_cast<std::uintptr_t>(player)+0x0c,actual)||actual!=id)return NativeFreight::Result::PlayerMissing;
        // Slot +8 takes the native player in R8 and the market flag in R9B.
        using Request=void(__fastcall*)(void*,void*,void*,bool);
        alignas(16) unsigned char scratch[32]{};
        trace.stage="copy.request.entered";
        reinterpret_cast<Request>(method)(reinterpret_cast<void*>(manager),scratch,player,online);
        trace.stage="copy.request.returned";
        // Current original UI callers supply IsOnline in DL, NOT CL.
        using Event=void(__fastcall*)(void*,bool);
        trace.stage="copy.ui-event.entered";
        reinterpret_cast<Event>(b.event)(nullptr,online);
        trace.stage="copy.ui-event.returned";
        return NativeFreight::Result::CallReturned;
    } __except(ExceptionFilter(GetExceptionCode())) {trace.exception=GetExceptionCode();return NativeFreight::Result::Fault;}
}
struct EditorBindings {
    std::uintptr_t manager,first,last,generate,reset,selectAuto,selectCombo,checkSet,children,text,owned,route,take,updateGenerate,managerTable,menuItemType;
};
constexpr unsigned menuOffsets[]={0x30,0x38,0x48,0x50,0x60,0x78,0xc0};
constexpr unsigned checkOffsets[]={0x70,0x88,0x98,0xc8,0xd0};
bool Automatic(unsigned field){return field==0||field==2||field==4;}
std::uintptr_t Method(const EditorBindings& b,std::uintptr_t object,unsigned slot){
    std::uintptr_t table=0,fn=0;
    return object&&Read(object,table)&&Span(table,slot+8,b.first,b.last)&&ReadOnlyPage(table)&&Read(table+slot,fn)&&Span(fn,1,b.first,b.last)&&Page(fn,true)?fn:0;
}
#if COMPANION_NATIVE_FREIGHT
bool GetterMatches(const EditorBindings& b,std::uintptr_t method,const freight_profile::Pattern& pattern){
    std::array<unsigned char,32> bytes{};
    if(pattern.referenceCount||pattern.size>bytes.size()||!Span(method,pattern.size,b.first,b.last)||
       !Page(method,true)||!Read(method,bytes.data(),pattern.size))return false;
    for(std::size_t i=0;i<pattern.size;++i)if(pattern.mask[i]&&bytes[i]!=pattern.bytes[i])return false;
    return true;
}
#endif
bool NativeString(std::uintptr_t at,std::string& text){
    struct Value{std::uintptr_t data,unused;std::uint64_t size,capacity;}value{};
    if(!Read(at,value)||value.size>2048||value.capacity<value.size||value.capacity>16*1024*1024)return false;
    if(value.capacity<=15&&value.size>15)return false;
    std::array<char,2049> bytes{};
    if(!Read(value.capacity<=15?at:value.data,bytes.data(),static_cast<std::size_t>(value.size)+1)||bytes[value.size])return false;
    if(value.size&&std::memchr(bytes.data(),0,static_cast<std::size_t>(value.size)))return false;
    text.assign(bytes.data(),static_cast<std::size_t>(value.size));return true;
}
bool Disabled(const EditorBindings& b,std::uintptr_t object,bool& value){
    auto method=Method(b,object,0x3e0);if(!method)return false;
#if COMPANION_NATIVE_FREIGHT
    if(!GetterMatches(b,method,freight_profile::ElementDisabled)&&!GetterMatches(b,method,freight_profile::CheckDisabled))return false;
#endif
    value=reinterpret_cast<bool(__fastcall*)(void*)>(method)(reinterpret_cast<void*>(object));return true;
}
bool ReadToggle(const EditorBindings& b,std::uintptr_t object,FreightToggle& value){
    if(Method(b,object,0x558)!=b.checkSet)return false;
    auto getter=Method(b,object,0x568);bool disabled=true;
    if(!getter||!Disabled(b,object,disabled))return false;
#if COMPANION_NATIVE_FREIGHT
    if(!GetterMatches(b,getter,freight_profile::CheckGet))return false;
#endif
    value.enabled=!disabled;value.checked=reinterpret_cast<bool(__fastcall*)(void*)>(getter)(reinterpret_cast<void*>(object));return true;
}
bool Dialog(const EditorBindings& b,std::uintptr_t& dialog){
    std::uintptr_t manager=0,table=0;unsigned char ready=0;
    return Read(b.manager,manager)&&manager&&Read(manager,table)&&table==b.managerTable&&ReadOnlyPage(table)&&
        Read(manager+0x28,ready)&&ready==1&&Read(manager+0x20,dialog)&&
        Method(b,dialog,8)==b.generate&&Method(b,dialog,0x28)==b.reset;
}
bool ReadMenu(const EditorBindings& b,std::uintptr_t control,bool automatic,FreightMenu& menu){
    if(!Span(b.menuItemType,1,b.first,b.last)||!ReadOnlyPage(b.menuItemType))return false;
    if(Method(b,control,automatic?0x6a0:0x680)!=(automatic?b.selectAuto:b.selectCombo))return false;
    bool disabled=true;if(!Disabled(b,control,disabled))return false;menu.enabled=!disabled;
    std::uintptr_t panel=0;
    if(!Read(control+(automatic?0x1e8:0x208),menu.selected)||!Read(control+(automatic?0x1e0:0x200),panel))return false;
    if(!panel)return !menu.enabled&&!menu.selected;
    if(Method(b,panel,0x38)!=b.children)return false;
    auto list=reinterpret_cast<std::uintptr_t(__fastcall*)(void*)>(b.children)(reinterpret_cast<void*>(panel));
    struct List{std::uintptr_t head;std::uint64_t count;}items{},after{};
    if(!Read(list,items)||!items.head||items.count>2048)return false;
    std::uintptr_t node=0,previous=items.head;
    if(!Read(items.head,node))return false;
    menu.choices.reserve(static_cast<std::size_t>(items.count));
    for(std::uint64_t i=0;i<items.count;++i){
        struct Node{std::uintptr_t next,previous,item;}entry{};
        if(node==items.head||!Read(node,entry)||entry.previous!=previous||!entry.next||!entry.item)return false;
        const auto cast=Method(b,entry.item,0x510);if(!cast)return false;
        // queryType uses pointer identity. A same-spelled string in our DLL
        // never identifies a native MenuItem and would silently drop every row.
        entry.item=reinterpret_cast<std::uintptr_t(__fastcall*)(void*,const char*)>(cast)(reinterpret_cast<void*>(entry.item),reinterpret_cast<const char*>(b.menuItemType));
        if(!entry.item){previous=node;node=entry.next;continue;}
        if(Method(b,entry.item,0x560)!=b.text)return false;
        FreightChoice choice;choice.token=entry.item;
        std::uintptr_t caption=0;
        if(!NativeString(entry.item+0xd0,choice.id)||!Read(entry.item+0x150,caption)||!caption||!NativeString(caption+0x128,choice.label))return false;
        if(!Disabled(b,entry.item,disabled))return false;
        choice.enabled=!disabled&&!choice.label.empty();
        menu.choices.push_back(std::move(choice));previous=node;node=entry.next;
    }
    std::uintptr_t tail=0;
    return (!items.count||!menu.choices.empty())&&node==items.head&&Read(items.head+8,tail)&&tail==previous&&Read(list,after)&&after.head==items.head&&after.count==items.count;
}
bool ReadEditorImpl(const EditorBindings& b,FreightEditorView& out,bool refresh){
    std::uintptr_t dialog=0;if(!Dialog(b,dialog)){out.message="原版货运调度器尚未就绪，可先打开一次原版调度器。";return false;}
    using Refresh=void(__fastcall*)(void*);
    if(refresh){
        reinterpret_cast<Refresh>(b.owned)(reinterpret_cast<void*>(dialog));
        reinterpret_cast<Refresh>(b.route)(reinterpret_cast<void*>(dialog));
        reinterpret_cast<Refresh>(b.take)(reinterpret_cast<void*>(dialog));
        reinterpret_cast<Refresh>(b.updateGenerate)(reinterpret_cast<void*>(dialog));
    }
    for(unsigned i=0;i<out.menus.size();++i){
        std::uintptr_t control=0;
        if(!Read(dialog+menuOffsets[i],control)||!ReadMenu(b,control,Automatic(i),out.menus[i])){out.message="原版选项正在变化或结构不匹配，请刷新后重试。";return false;}
    }
    for(unsigned i=0;i<out.toggles.size();++i){
        std::uintptr_t control=0;
        if(!Read(dialog+checkOffsets[i],control)||!ReadToggle(b,control,out.toggles[i])){out.message="原版接取选项尚不可读，请稍后重新载入。";return false;}
    }
    std::uintptr_t button=0,remote=0,again=0;FreightToggle copied;bool disabled=true;
    if(!Read(dialog+0x90,remote)||!ReadToggle(b,remote,copied)||!Read(dialog+0xe0,button)||!Disabled(b,button,disabled)||!Dialog(b,again)||again!=dialog)return false;
    out.ready=true;out.dialog=dialog;out.tick=GetTickCount64();out.copyTrailer=copied.checked;
    out.canGenerate=!disabled&&!out.copyTrailer;
    for(unsigned i=0;i<out.menus.size();++i){
        if(i==5&&out.toggles[1].checked)continue;
        const auto& menu=out.menus[i];
        if(!menu.selected||std::none_of(menu.choices.begin(),menu.choices.end(),[&](const auto& v){return v.token==menu.selected&&v.enabled;}))out.canGenerate=false;
    }
    out.message=out.copyTrailer?"原版仍在复制挂车模式，请先载入/重置为自选货运。":out.canGenerate?"选项已同步原版；生成时仍会执行原版接取检查。":"请选择完整路线、货物和挂车，等待原版资格就绪。";
    if(!out.copyTrailer&&out.menus[0].choices.empty())out.message="原版暂未提供起点城市，请稍后重新载入选项。";
    return true;
}
bool SameSelection(const FreightEditorView& a,const FreightEditorView& b){
    if(!a.ready||!b.ready||a.dialog!=b.dialog||a.copyTrailer!=b.copyTrailer)return false;
    for(unsigned i=0;i<a.menus.size();++i){
        if(a.menus[i].selected!=b.menus[i].selected)return false;
        if(!a.menus[i].selected)continue;
        const auto find=[](const FreightMenu& menu){return std::find_if(menu.choices.begin(),menu.choices.end(),[&](const auto& c){return c.token==menu.selected;});};
        const auto x=find(a.menus[i]),y=find(b.menus[i]);
        if(x==a.menus[i].choices.end()||y==b.menus[i].choices.end()||x->id!=y->id||x->label!=y->label)return false;
    }
    for(unsigned i=0;i<a.toggles.size();++i)if(a.toggles[i].checked!=b.toggles[i].checked)return false;
    return true;
}
NativeFreight::Result EditImpl(const EditorBindings& b,const FreightEditorAction& action,FreightEditorView& after,NativeCallTrace& trace){
    trace.stage="editor.dialog";
    std::uintptr_t dialog=0;if(!Dialog(b,dialog))return NativeFreight::Result::StateChanged;
    using Action=void(__fastcall*)(void*);
    if(action.kind==EditorActionKind::Refresh){
        trace.stage="editor.reset";
        reinterpret_cast<Action>(b.reset)(reinterpret_cast<void*>(dialog));
        std::uintptr_t remote=0;FreightToggle copied;
        if(!Read(dialog+0x90,remote)||!ReadToggle(b,remote,copied))return NativeFreight::Result::StateChanged;
        if(copied.checked&&copied.enabled)reinterpret_cast<void(__fastcall*)(void*,bool)>(b.checkSet)(reinterpret_cast<void*>(remote),false);
    }else{
        trace.stage="editor.snapshot";
        FreightEditorView current;
        if(!ReadEditorImpl(b,current,true)||!SameSelection(current,action.before))return NativeFreight::Result::StateChanged;
        if(action.kind==EditorActionKind::Select){
            trace.stage="editor.select";
            if(action.field>=current.menus.size())return NativeFreight::Result::Unavailable;
            const auto& menu=current.menus[action.field];
            auto item=std::find_if(menu.choices.begin(),menu.choices.end(),[&](const auto& c){return c.token==action.choice.token&&c.id==action.choice.id&&c.label==action.choice.label&&c.enabled;});
            std::uintptr_t control=0;
            if(!menu.enabled||item==menu.choices.end()||!Read(dialog+menuOffsets[action.field],control))return NativeFreight::Result::StateChanged;
            if(Automatic(action.field))reinterpret_cast<void(__fastcall*)(void*,void*,bool,bool)>(b.selectAuto)(reinterpret_cast<void*>(control),reinterpret_cast<void*>(item->token),true,true);
            else reinterpret_cast<void(__fastcall*)(void*,void*,bool)>(b.selectCombo)(reinterpret_cast<void*>(control),reinterpret_cast<void*>(item->token),true);
        }else if(action.kind==EditorActionKind::Toggle){
            trace.stage="editor.toggle";
            if(action.field>=current.toggles.size()||!current.toggles[action.field].enabled)return NativeFreight::Result::StateChanged;
            std::uintptr_t control=0;if(!Read(dialog+checkOffsets[action.field],control))return NativeFreight::Result::StateChanged;
            reinterpret_cast<void(__fastcall*)(void*,bool)>(b.checkSet)(reinterpret_cast<void*>(control),action.checked);
            if(action.field<=2)reinterpret_cast<Action>(b.reset)(reinterpret_cast<void*>(dialog));
        }else if(action.kind==EditorActionKind::Generate){
            trace.stage="generate.eligibility";
            if(!current.canGenerate)return NativeFreight::Result::StateChanged;
            trace.stage="generate.entered";
            reinterpret_cast<Action>(b.generate)(reinterpret_cast<void*>(dialog));
            trace.stage="generate.returned";
            return NativeFreight::Result::CallReturned;
        }
    }
    trace.stage="editor.readback";
    if(!ReadEditorImpl(b,after,true))return NativeFreight::Result::StateChanged;
    if(action.kind==EditorActionKind::Select&&after.menus[action.field].selected!=action.choice.token)return NativeFreight::Result::StateChanged;
    if(action.kind==EditorActionKind::Toggle&&after.toggles[action.field].checked!=action.checked)return NativeFreight::Result::StateChanged;
    trace.stage="editor.readback-matched";
    return NativeFreight::Result::Edited;
}
// SEH boundary contains no STL locals. All retained values are copied UI data.
bool GuardRead(const EditorBindings& b,FreightEditorView& view,bool refresh,bool& fault){
    __try{return ReadEditorImpl(b,view,refresh);}__except(ExceptionFilter(GetExceptionCode())){fault=true;return false;}
}
NativeFreight::Result GuardEdit(const EditorBindings& b,const FreightEditorAction& action,FreightEditorView& after,NativeCallTrace& trace){
    __try{return EditImpl(b,action,after,trace);}__except(ExceptionFilter(GetExceptionCode())){trace.exception=GetExceptionCode();return NativeFreight::Result::Fault;}
}
}
bool NativeFreight::Included(){return COMPANION_NATIVE_FREIGHT!=0;}
void NativeFreight::SessionChanged(){
    attempted_=available_=editorAvailable_=onlineAvailable_=false;module_=nullptr;
    begin_=end_=playersGlobal_=managerGlobal_=uiGlobal_=findPlayer_=copyRequest_=copyEvent_=managerTable_=0;
    onlineGlobal_=generate_=resetEditor_=selectAuto_=selectCombo_=checkSet_=menuChildren_=textGetter_=0;
    refreshOwned_=refreshRoute_=refreshTake_=refreshGenerate_=menuItemType_=0;
    diagnostic_.clear();editorReason_.clear();onlineReason_.clear();
    reason_=faulted_?"货运适配器异常，已停用至重启":"等待客户端识别";
}
bool NativeFreight::Prepare(std::string_view version){
    if(faulted_)return false;
#if COMPANION_NATIVE_FREIGHT
    const auto current=GetModuleHandleW(L"core_ets2mp.dll");
    if(attempted_&&module_==current)return false;
    if(attempted_)SessionChanged();
    if(!current){reason_="等待 ETS2 客户端模块加载；货运适配暂不可用";return false;}
    attempted_=true;module_=current;
    std::string client;
    for(const auto c:version.substr(0,48))client+=c>=' '&&c<='~'?c:'?';
    diagnostic_="reference="+std::string(freight_profile::ReferenceClientVersion)+" client="+(client.empty()?"unknown":client)+" policy=structure-relocation; ";
    // ClientVersion is diagnostic data. Only the code/data contracts authorize
    // native calls; neither a familiar tag nor an old RVA is a fallback.
    Image image;if(!image.Open(module_)){reason_="无法识别客户端映像";diagnostic_+="invalid-image";return true;}
    const auto find=[&](const freight_profile::Pattern& p){return image.Find(p,diagnostic_);};
    const auto action=find(freight_profile::CopyAction),callback=find(freight_profile::OnlineCallback),constructor=find(freight_profile::ManagerConstructor);
    findPlayer_=find(freight_profile::FindPlayer);copyRequest_=find(freight_profile::CopyRequest);copyEvent_=find(freight_profile::CopyEvent);
    if(!action||!callback||!constructor||!findPlayer_||!copyRequest_||!copyEvent_){reason_="货运入口缺失、不唯一或结构已变化，已停用货运";return true;}
    managerGlobal_=image.Relative(action+0x1f,3,7);playersGlobal_=image.Relative(callback+6,3,7);uiGlobal_=image.Relative(copyEvent_+0x10,3,7);
    managerTable_=image.Relative(constructor+0x32,3,7);std::uintptr_t requestSlot=0;
    if(image.Relative(action+0x3a,1,5)!=copyEvent_||image.Relative(callback+0x20,1,5)!=findPlayer_||image.Relative(callback+0x42,1,5)!=copyEvent_||
       image.Relative(constructor+0x2b,3,7)!=managerGlobal_||!image.Region(managerTable_,16,freight_profile::Region::ReadOnly)||
       !Read(managerTable_+8,requestSlot)||requestSlot!=copyRequest_||
       !image.Section(managerGlobal_,8,false)||!image.Section(playersGlobal_,8,false)||!image.Section(uiGlobal_,8,false)){
        reason_="货运调用关系或对象布局已变化，已停用货运";diagnostic_+="copy-manager-relationship-mismatch; ";return true;
    }
    begin_=image.first;end_=image.last;available_=true;
    const auto online=find(freight_profile::OnlineCapability),onlineAction=find(freight_profile::OnlineAction);
    onlineGlobal_=online?image.Relative(online+0xa,3,7):0;
    onlineAvailable_=onlineAction&&image.Section(onlineGlobal_,8,false)&&image.Relative(onlineAction+0x2a,3,7)==managerGlobal_&&
        image.Relative(onlineAction+0x12,1,5)==image.Relative(action+0xf,1,5);
    if(!onlineAvailable_){onlineReason_="WoT 货运入口或结构已变化，已停用 WoT 复制。";diagnostic_+="wot-disabled; ";}
    generate_=find(freight_profile::EditorGenerate);resetEditor_=find(freight_profile::EditorReset);
    selectAuto_=find(freight_profile::SelectAutocomplete);selectCombo_=find(freight_profile::SelectCombo);
    const auto itemType=find(freight_profile::AutocompleteItemType);
    menuItemType_=itemType?image.Relative(itemType+0x0c,3,7):0;
    checkSet_=find(freight_profile::CheckSet);menuChildren_=find(freight_profile::MenuChildren);textGetter_=find(freight_profile::ElementText);
    refreshOwned_=find(freight_profile::RefreshOwned);refreshRoute_=find(freight_profile::RefreshRoute);refreshTake_=find(freight_profile::RefreshTake);refreshGenerate_=find(freight_profile::RefreshGenerate);
    editorAvailable_=generate_&&resetEditor_&&selectAuto_&&selectCombo_&&checkSet_&&menuChildren_&&textGetter_&&refreshOwned_&&refreshRoute_&&refreshTake_&&refreshGenerate_&&
        image.Region(menuItemType_,sizeof("Button:MenuItem"),freight_profile::Region::ReadOnly);
    if(editorAvailable_&&(image.Relative(resetEditor_+0x19,1,5)!=refreshOwned_||
       image.Relative(selectAuto_+0x32,3,7)!=image.Relative(itemType,3,7))){editorAvailable_=false;diagnostic_+="editor-call-or-type-relationship-mismatch; ";}
    if(!editorAvailable_)editorReason_="自选货运入口或调用关系已变化，已停用自选做货。";
    reason_="货运地址已定位 · 普通复制可用";
    reason_+=editorAvailable_?" · 自选可用":" · 自选停用";
    reason_+=onlineAvailable_?" · WoT 适配可用":" · WoT 适配停用";
    diagnostic_+="copy=ready editor="+std::string(editorAvailable_?"ready":"disabled")+" wot="+(onlineAvailable_?"ready":"disabled");
    return true;
#else
    (void)version;if(attempted_)return false;attempted_=true;
    reason_="此构建未包含本地货运适配描述";diagnostic_="native-freight-not-included";return true;
#endif
}
bool NativeFreight::CanCopyOnline()const{return Available()&&onlineAvailable_&&OnlineReady(onlineGlobal_);}
NativeFreight::Result NativeFreight::SubmitCopy(int id,bool online){
    trace_={"copy.availability",0};
    if(!Available()||id<0||id>=5000)return Result::Unavailable;
    if(online&&!CanCopyOnline())return Result::Unavailable;
    if(module_!=GetModuleHandleW(L"core_ets2mp.dll")||!Page(findPlayer_,true)||!Page(copyRequest_,true)||!Page(copyEvent_,true)){
        available_=false;reason_="客户端模块或入口已变化";return Result::Unavailable;
    }
    Result result=Result::Fault;
    try{result=Invoke({playersGlobal_,managerGlobal_,uiGlobal_,findPlayer_,copyRequest_,copyEvent_,begin_,end_,onlineGlobal_,managerTable_},id,online,trace_);}
    catch(...){result=Result::Fault;}
    if(result==Result::Fault){faulted_=true;available_=false;reason_="货运调用异常，结果未知；已停用至重启";}
    return result;
}
bool NativeFreight::ReadEditor(FreightEditorView& view,bool refresh){
    view={};if(!EditorAvailable()){view.message=Available()?editorReason_:reason_;return false;}
    if(module_!=GetModuleHandleW(L"core_ets2mp.dll")){available_=false;view.message="客户端模块已变化。";return false;}
    bool fault=false,ok=false;
    try{ok=GuardRead({managerGlobal_,begin_,end_,generate_,resetEditor_,selectAuto_,selectCombo_,checkSet_,menuChildren_,textGetter_,refreshOwned_,refreshRoute_,refreshTake_,refreshGenerate_,managerTable_,menuItemType_},view,refresh,fault);}catch(...){fault=true;}
    if(fault){faulted_=true;available_=false;reason_="读取货运状态异常，已停用至重启";view.message=reason_;}
    return ok;
}
NativeFreight::Result NativeFreight::Edit(const FreightEditorAction& action,FreightEditorView& after){
    trace_={"editor.availability",0};
    if(!EditorAvailable()||module_!=GetModuleHandleW(L"core_ets2mp.dll"))return Result::Unavailable;
    Result result=Result::Fault;
    try{result=GuardEdit({managerGlobal_,begin_,end_,generate_,resetEditor_,selectAuto_,selectCombo_,checkSet_,menuChildren_,textGetter_,refreshOwned_,refreshRoute_,refreshTake_,refreshGenerate_,managerTable_,menuItemType_},action,after,trace_);}catch(...){result=Result::Fault;}
    if(result==Result::Fault){faulted_=true;available_=false;reason_="货运操作异常，结果未知；已停用至重启";}
    return result;
}
}
