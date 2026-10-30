// Isolated process; never loads core_ets2mp.dll or opens the game process.
#include "../src/settings_zh.cpp"
#include "native_assign_fixture.h"
#include "native_font_fixture.h"
#include <cstdlib>
#include <string>
#include <vector>
#include <array>
#include <new>

namespace {
unsigned checks=0, allocated=0, released=0;
void Check(bool ok,const char* message) {++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
void* FixtureAllocate(std::size_t n) {++allocated;return HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,n);}
void FixtureFree(void* p,unsigned) {if(p){++released;Check(HeapFree(GetProcessHeap(),0,p)!=0,"fixture native free");}}
void* FixtureCopy(void* to,const void* from,std::size_t n) {return std::memmove(to,from,n);}
[[noreturn]] void FixtureError() {std::fputs("native assign error branch reached\n",stderr);std::abort();}
Assign MakeAssignFixture() {
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(code!=nullptr,"fixture allocation");
    std::memcpy(code,kAssignCode,sizeof kAssignCode);
    unsigned stub=static_cast<unsigned>((sizeof kAssignCode+15)&~15);
    for(const auto& call:kAssignCalls) {
        void* fn=reinterpret_cast<void*>(&FixtureError);
        if(call.target==0x9bb3f0)fn=reinterpret_cast<void*>(&FixtureCopy);
        if(call.target==0x82680)fn=reinterpret_cast<void*>(&FixtureAllocate);
        if(call.target==0x54720)fn=reinterpret_cast<void*>(&FixtureFree);
        code[stub]=0x48;code[stub+1]=0xb8;std::memcpy(code+stub+2,&fn,8);code[stub+10]=0xff;code[stub+11]=0xe0;
        Check(code[call.offset]==0xe8,"fixture relative call encoding");
        const auto displacement=static_cast<std::int32_t>(stub-call.offset-5);
        std::memcpy(code+call.offset+1,&displacement,4);stub+=16;
    }
    DWORD old;Check(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old)!=0,"fixture execute protection");
    FlushInstructionCache(GetCurrentProcess(),code,4096);
    return reinterpret_cast<Assign>(code);
}
void EmptyWide(NativeString& s) {s={};s.capacity=7;}
template<class T,std::size_t N> T FontFixture(const unsigned char (&bytes)[N]) {
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(code!=nullptr,"native font fixture allocation");std::memcpy(code,bytes,N);
    DWORD old;Check(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old)!=0,"native font fixture protection");
    FlushInstructionCache(GetCurrentProcess(),code,4096);return reinterpret_cast<T>(code);
}
void DestroyWide(NativeString& s) {
    if(s.capacity>7){void* p=nullptr;std::memcpy(&p,s.storage,8);FixtureFree(p,0);}EmptyWide(s);
}
void MakeNarrow(NativeString& s,const char* value) {
    s={};s.size=std::strlen(value);s.capacity=s.size>15?s.size:15;
    if(s.size>15) {void* p=FixtureAllocate(s.size+1);std::memcpy(p,value,s.size+1);std::memcpy(s.storage,&p,8);}
    else std::memcpy(s.storage,value,s.size+1);
}
void DestroyNarrow(NativeString& s) {
    if(s.capacity>15){void* p=nullptr;std::memcpy(&p,s.storage,8);FixtureFree(p,0);}s={};
}
struct Text {
    NativeString wide{},narrow{};
    explicit Text(const char* en) {
        EmptyWide(wide);MakeNarrow(narrow,en);
        std::wstring w;for(const char* p=en;*p;++p)w+=static_cast<unsigned char>(*p);
        nativeAssign(&wide,w.c_str(),w.size());
    }
    ~Text(){DestroyWide(wide);DestroyNarrow(narrow);}
};
void* MockConstructor(void* p,const void* source){return new(p) Text(static_cast<const char*>(source));}
void MockDestroy(void* p){static_cast<Text*>(p)->~Text();}
void MockSetLabel(void* control,const void* source,bool resize) {
    Check(!resize,"existing control bounds retained");
    auto* target=static_cast<Text*>(DisplayText(control));const auto* value=static_cast<const Text*>(source);
    Check(target!=nullptr,"existing native Label interface");
    nativeAssign(&target->wide,Wide(&value->wide),value->wide.size);
    DestroyNarrow(target->narrow);MakeNarrow(target->narrow,Narrow(&value->narrow));
}
struct FakeControl {
    alignas(8) unsigned char data[0x228]{},child[0x190]{};
    NativeFont font{};std::array<void*,3> textHead{};
    std::array<void*,3> head{};
    std::vector<std::array<void*,3>> nodes;
    FakeControl(const char* text,bool concreteLabel=true,const char* name="") {
        void* vt=core+(concreteLabel?kLabelVtable:0xdf01e0);
        *reinterpret_cast<void**>(data)=vt;*reinterpret_cast<void**>(data+0x150)=child;
        *reinterpret_cast<void**>(child)=core+kTextVtable;
        EmptyWide(font.name);nativeAssign(&font.name,L"OpenSans.ttf",12);font.size=14;
        *reinterpret_cast<void**>(child+0x168)=&font;
        textHead[0]=textHead.data();textHead[1]=textHead.data();
        *reinterpret_cast<void**>(child+0x180)=textHead.data();
        new(child+0x128) Text(text);
        MakeNarrow(*reinterpret_cast<NativeString*>(data+0xd0),name);
        SetChildren({});
    }
    void SetChildrenVector(const std::vector<void*>& values) {
        nodes.resize(values.size());unsigned i=0;
        for(void* value:values){nodes[i][2]=value;++i;}
        for(i=0;i<nodes.size();++i) {
            nodes[i][0]=i+1<nodes.size()?nodes[i+1].data():head.data();
            nodes[i][1]=i?nodes[i-1].data():head.data();
        }
        head[0]=nodes.empty()?head.data():nodes.front().data();head[1]=nodes.empty()?head.data():nodes.back().data();
        *reinterpret_cast<void**>(data+0x58)=head.data();*reinterpret_cast<std::size_t*>(data+0x60)=nodes.size();
    }
    void SetChildren(std::initializer_list<void*> values) {SetChildrenVector(std::vector<void*>(values));}
    const wchar_t* Value(){return Wide(child+0x128);}
    ~FakeControl(){reinterpret_cast<Text*>(child+0x128)->~Text();DestroyWide(font.name);DestroyNarrow(*reinterpret_cast<NativeString*>(data+0xd0));}
};
void CheckPair(const Pair& pair,unsigned kind) {
    Text text(pair.en);NativeString before=text.narrow;
    const auto r=TranslateText(&text,pair,kind);
    Check(r,"production wide translation accepted");
    Check(std::wcscmp(Wide(&text.wide),pair.zh)==0,"native assign exact UTF-16 result");
    Check(!std::memcmp(&before,&text.narrow,sizeof before)&&NarrowIs(&text.narrow,pair.en),"original narrow identifier retained");
    Check(!TranslateText(&text,pair,kind),"translation is idempotent");
}
void FillGuards(unsigned char* image) {
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image);dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x80;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(image+0x80);nt->Signature=IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;nt->OptionalHeader.SizeOfImage=kImageSize;
    for(const auto& g:kGuards)std::memcpy(image+g.rva,g.bytes,g.size);
    *reinterpret_cast<void**>(image+kComboVtable+0x10)=image+0x13e860;
    *reinterpret_cast<void**>(image+kLabelVtable+0x558)=image+0x336bc0;
    *reinterpret_cast<void**>(image+0xdf01e0+0x558)=image+0x336bc0;
    for(const auto& l:kLiterals)std::memcpy(image+l.source,l.text.en,std::strlen(l.text.en)+1);
}
void StubBuild(void*){}
void TestNativeFontMeasurement() {
    using Measure=void*(*)(void*,void*,void*,void*);
    auto measure=FontFixture<Measure>(kMeasureCode);
    std::vector<unsigned char> atlas(0x12+0xffff*0x16);
    const auto glyph=[&](unsigned cp,unsigned advance) {
        auto* p=atlas.data()+0x12+cp*0x16;*p=1;
        const auto width=static_cast<unsigned short>(advance);std::memcpy(p+0x12,&width,2);
    };
    glyph('?',7);glyph(0x8bbe,14);glyph(0x7f6e,14);
    NativeFont font{};font.data=atlas.data();font.size=14;
    NativeString text;EmptyWide(text);nativeAssign(&text,L"设置",2);
    struct {int width,height;} result{};
    Check(measure(nullptr,&result,&font,&text)==&result&&result.width==28,
          "exact native metrics body reads both Chinese BMP glyphs");
    atlas[0x12+0x8bbe*0x16]=0;
    measure(nullptr,&result,&font,&text);
    Check(result.width==21,"exact native missing-glyph branch substitutes question mark");
    DestroyWide(text);
}
int StubRender(void*,void*,int x,int y,int width) {Check(x==0x131&&y==10&&width==0xf0,"render parameters forwarded");return 30;}
void TestScopesAndDescriptor() {
    const Row& row=kRows[3]; // Job sharing privacy, with a tooltip and enum.
    Text label(row.label.en),tip(row.tooltip.en);
    alignas(8) unsigned char object[0xe0]{};
    std::memcpy(object+8,&label,64);std::memcpy(object+0x48,&tip,64);
    // Move ownership to our descriptor, leaving inert Text destructors.
    EmptyWide(label.wide);label.narrow={};EmptyWide(tip.wide);tip.narrow={};
    std::memset(object+0x88,0xa7,sizeof object-0x88);
    std::array<unsigned char,0x58> binding{};std::memcpy(binding.data(),object+0x88,binding.size());
    pageScope=1;TranslateDescriptor(object);
    Check(WideIsEnglish(object+8,row.label.en),"wrong page leaves label unchanged");
    pageScope=0;TranslateDescriptor(object);
    Check(std::wcscmp(Wide(object+8),row.label.zh)==0,"pre-existing native descriptor translated on render");
    Check(std::wcscmp(Wide(object+0x48),row.tooltip.zh)==0,"descriptor tooltip translated");
    Check(!std::memcmp(binding.data(),object+0x88,binding.size()),"bindings and values untouched");
    Check(NarrowIs(object+0x28,row.label.en)&&NarrowIs(object+0x68,row.tooltip.en),"descriptor narrow fields unchanged");

    const Options* group=nullptr;for(const auto& g:kOptions)if(!std::strcmp(g.label,row.label.en))group=&g;
    Check(group&&group->count==3,"privacy option group");
    std::vector<NativeString> items(group->count);
    for(unsigned i=0;i<group->count;++i)MakeNarrow(items[i],group->pairs[i].en);
    void* vtable=core+kComboVtable;std::memcpy(object,&vtable,8);
    NativeString* first=items.data();NativeString* last=first+items.size();
    std::memcpy(object+0xa0,&first,8);std::memcpy(object+0xa8,&last,8);
    Check(OptionsFor(object)==group,"complete enum vector accepted");
    const auto before=items;
    optionScope=group;
    for(unsigned i=0;i<group->count;++i) {
        auto* pair=ConversionText(&items[i],0x13e9a0);
        Check(pair==group->pairs+i,"option routing preserves order and identity");
        Text value(pair->en);Check(TranslateWide(&value.wide,*pair,3),"option wide string translated");
        Check(NarrowIs(&value.narrow,pair->en),"option original text retained");
    }
    Check(!std::memcmp(before.data(),items.data(),items.size()*sizeof(NativeString)),"source enum vector unchanged");
    Check(!ConversionText(first,0x13ee20),"device/string combo path excluded");
    Check(!ConversionText(first,0x13f0e0),"dynamic device path excluded");
    optionScope=nullptr;Check(!ConversionText(first,0x13e9a0),"unscoped static-looking text excluded");
    std::swap(items[0],items[1]);Check(!OptionsFor(object),"reordered or unexpected options rejected");std::swap(items[0],items[1]);
    *reinterpret_cast<void**>(object)=nullptr;Check(!OptionsFor(object),"foreign descriptor vtable rejected");
    originalLabel=&StubRender;
    Check(HookLabel(object,nullptr,0x131,10,0xf0)==30,"native row height retained");
    originalWindow=&StubBuild;originalPage=&StubBuild;
    windowScope=false;HookWindow(nullptr);Check(!windowScope,"window scope restored");
    pageScope=-1;HookPage(object);Check(pageScope==-1,"page scope restored");
    for(auto& item:items)DestroyNarrow(item);
    DestroyWide(*reinterpret_cast<NativeString*>(object+8));DestroyNarrow(*reinterpret_cast<NativeString*>(object+0x28));
    DestroyWide(*reinterpret_cast<NativeString*>(object+0x48));DestroyNarrow(*reinterpret_cast<NativeString*>(object+0x68));
}
void TestAlreadyCreatedWindow() {
    originalCString=&MockConstructor;nativeDestroyText=&MockDestroy;nativeSetLabel=&MockSetLabel;originalShow=&StubBuild;
    const Row& row=kRows[3];
    FakeControl window(""),title("Settings"),footer("Cancel",false,"settings_cancel"),tab("General",false);
    FakeControl content(""),label(row.label.en),editable(row.label.en,false),tooltip(row.tooltip.en);
    FakeControl combo("Public",false),menu("",false),inner("",false);
    FakeControl privateItem("Private",false),friendsItem("Steam friends only",false),publicItem("Public",false);
    FakeControl staffContent(""),staffLabel(row.label.en);
    window.SetChildren({footer.data,content.data});
    *reinterpret_cast<void**>(window.data+0x1b0)=title.data;
    content.SetChildren({label.data,editable.data,combo.data});
    *reinterpret_cast<void**>(combo.data+0x80)=tooltip.data;
    *reinterpret_cast<void**>(combo.data+0x200)=menu.data;
    *reinterpret_cast<void**>(combo.data+0x208)=publicItem.data;
    *reinterpret_cast<void**>(menu.data+0x70)=inner.data;
    inner.SetChildren({privateItem.data,friendsItem.data,publicItem.data});
    staffContent.SetChildren({staffLabel.data});
    alignas(8) unsigned char descriptor[0xc0]{};
    *reinterpret_cast<void**>(descriptor)=core+kComboVtable;
    new(descriptor+8)Text(row.label.en);new(descriptor+0x48)Text(row.tooltip.en);
    const char* originals[]={"Private","Steam friends only","Public"};NativeString options[3];
    for(unsigned i=0;i<3;++i)MakeNarrow(options[i],originals[i]);
    *reinterpret_cast<NativeString**>(descriptor+0xa0)=options;
    *reinterpret_cast<NativeString**>(descriptor+0xa8)=options+3;
    *reinterpret_cast<void**>(descriptor+0xb8)=combo.data;
    const auto originalOptions=std::array<NativeString,3>{options[0],options[1],options[2]};
    void* rows[]={descriptor};
    alignas(8) unsigned char pages[0x68*2]{};
    MakeNarrow(*reinterpret_cast<NativeString*>(pages+0x20),"General");
    MakeNarrow(*reinterpret_cast<NativeString*>(pages+0x68+0x20),"Staff");
    *reinterpret_cast<void**>(pages+0x40)=tab.data;*reinterpret_cast<void**>(pages+0x48)=content.data;
    *reinterpret_cast<void**>(pages+0x50)=rows;*reinterpret_cast<void**>(pages+0x58)=rows+1;
    *reinterpret_cast<void**>(pages+0x68+0x48)=staffContent.data;
    alignas(8) unsigned char settings[0x48]{};
    *reinterpret_cast<void**>(settings+0x28)=window.data;
    *reinterpret_cast<void**>(settings+0x38)=pages;*reinterpret_cast<void**>(settings+0x40)=pages+sizeof pages;
    pageScope=-1;windowScope=false;HookShow(settings);
    Check(!std::wcscmp(label.Value(),row.label.zh),"already-created static label refreshed");
    Check(!std::wcscmp(tooltip.Value(),row.tooltip.zh),"already-created tooltip refreshed");
    Check(!std::wcscmp(title.Value(),kWindow.zh)&&!std::wcscmp(tab.Value(),kPages[0].zh),"already-created window and tab refreshed");
    Check(!std::wcscmp(footer.Value(),kLiterals[0].text.zh),"owned footer refreshed by internal name");
    Check(NarrowIs(footer.data+0xd0,"settings_cancel"),"footer action identifier retained");
    Check(WideIsEnglish(editable.child+0x128,row.label.en),"editable user text excluded even with equal English content");
    Check(WideIsEnglish(staffLabel.child+0x128,row.label.en),"unrequested Staff page excluded");
    Check(PointerAt(label.child,0x168)!=&label.font,"translated label gets compact native font");
    Check(PointerAt(editable.child,0x168)==&editable.font&&PointerAt(staffLabel.child,0x168)==&staffLabel.font,
          "editable fields and excluded pages retain original fonts");
    Check(WideIsChinese(&static_cast<NativeFont*>(PointerAt(label.child,0x168))->name,kSettingsFont),
          "native font name resolves to the dedicated compact asset");
    Check(label.child[0x179]==1,"exact native setter marks font metrics dirty");
    FakeControl scaled(row.label.en);scaled.font.size=21;scaled.font.bold=1;
    Check(TranslateDisplay(scaled.data,row.label,0),"scaled bold Settings label translated");
    const auto* scaledFont=static_cast<NativeFont*>(PointerAt(scaled.child,0x168));
    Check(scaledFont->size==21&&scaledFont->bold==1&&WideIsChinese(&scaledFont->name,kSettingsBoldFont),
          "font binding preserves scaled size and bold style");
    FakeControl prior(row.label.en);nativeAssign(prior.child+0x128,row.label.zh,std::wcslen(row.label.zh));
    Check(TranslateDisplay(prior.data,row.label,0)&&PointerAt(prior.child,0x168)!=&prior.font,
          "already-translated constructor text still receives compact font");
    Check(!std::wcscmp(privateItem.Value(),L"仅自己")&&!std::wcscmp(friendsItem.Value(),L"仅 Steam 好友")&&!std::wcscmp(publicItem.Value(),L"公开"),"already-created menu items refreshed in order");
    Check(!std::wcscmp(combo.Value(),L"公开"),"selected option caption refreshed");
    Check(PointerAt(combo.data,0x208)==publicItem.data,"selected native item pointer retained");
    Check(!std::memcmp(originalOptions.data(),options,sizeof options),"original enum vector retained after late refresh");
    Check(NarrowIs(descriptor+0x28,row.label.en),"native descriptor original identifier retained");
    Check(pageScope==-1&&!windowScope,"late refresh restores caller scopes");
    auto previous=counts[0].load();HookShow(settings);Check(counts[0].load()==previous,"already-created refresh is idempotent");
    for(auto& option:options)DestroyNarrow(option);
    reinterpret_cast<Text*>(descriptor+8)->~Text();reinterpret_cast<Text*>(descriptor+0x48)->~Text();
    DestroyNarrow(*reinterpret_cast<NativeString*>(pages+0x20));DestroyNarrow(*reinterpret_cast<NativeString*>(pages+0x68+0x20));
}
void* insertedItem=nullptr;
const void* expectedDisplay=nullptr;
const void* expectedIdentifier=nullptr;
void* StubFreightInsert(void*,const void* display,const void* identifier) {
    Check(display==expectedDisplay&&identifier==expectedIdentifier,"freight display and identifier forwarded unchanged");
    return insertedItem;
}
void TestFreightFonts() {
    alignas(8) unsigned char owner[0xc8]{};
    *reinterpret_cast<void**>(owner)=core+kFreightVtable;
    FakeControl* controls[7]{};
    for(unsigned i=0;i<7;++i) {
        controls[i]=new FakeControl("Dynamic cargo",false,kFreightNames[i]);
        *reinterpret_cast<void**>(owner+0x30+i*8)=controls[i]->data;
    }
    FakeControl menu("",false),inner("",false),oldItem("Old city",false),newItem("New city",false);
    *reinterpret_cast<void**>(controls[0]->data+0x1e0)=menu.data;
    *reinterpret_cast<void**>(menu.data+0x70)=inner.data;
    std::vector<FakeControl*> oldCities;
    std::vector<void*> menuItems{oldItem.data};
    for(unsigned i=0;i<300;++i) {
        auto* item=new FakeControl("Existing city",false);oldCities.push_back(item);menuItems.push_back(item->data);
    }
    inner.SetChildrenVector(menuItems);
    originalFreightUpdate=&StubBuild;originalFreightDestroy=&StubBuild;
    originalFreightComboItem=&StubFreightInsert;originalFreightAutocompleteItem=&StubFreightInsert;
    HookFreightUpdate(owner);
    for(auto* control:controls) {
        auto* font=static_cast<NativeFont*>(PointerAt(control->child,0x168));
        Check(font!=&control->font&&WideIsChinese(&font->name,kFreightFont),"seven owned dynamic controls bind compact freight font");
        Check(WideIsEnglish(control->child+0x128,"Dynamic cargo"),"freight font binding never changes display text");
    }
    Check(PointerAt(oldItem.child,0x168)!=&oldItem.font,"pre-existing autocomplete popup row gets compact font");
    Check(PointerAt(oldCities.back()->child,0x168)!=&oldCities.back()->font,
          "late-loaded freight lists larger than 256 rows are fully bound");
    Text display("New city"),identifier("internal_cargo_id");
    expectedDisplay=&display;expectedIdentifier=&identifier;insertedItem=newItem.data;
    Check(HookFreightAutocompleteItem(controls[0]->data,&display,&identifier)==newItem.data,"autocomplete insertion return retained");
    Check(PointerAt(newItem.child,0x168)!=&newItem.font&&NarrowIs(&identifier.narrow,"internal_cargo_id"),
          "new autocomplete row bound without changing internal identifier");
    FakeControl foreign("Foreign",false,"trailers"),foreignItem("Foreign item",false);
    insertedItem=foreignItem.data;HookFreightComboItem(foreign.data,&display,&identifier);
    Check(PointerAt(foreignItem.child,0x168)==&foreignItem.font,"same-name control outside registered freight window excluded");
    FakeControl comboItem("Trailer",false);insertedItem=comboItem.data;
    HookFreightComboItem(controls[5]->data,&display,&identifier);
    Check(PointerAt(comboItem.child,0x168)!=&comboItem.font,"new combo row bound");
    auto* selected=comboItem.data;*reinterpret_cast<void**>(controls[5]->data+0x208)=selected;
    HookFreightUpdate(owner);
    Check(PointerAt(controls[5]->data,0x208)==selected,"selected freight item identity retained across update");
    HookFreightDestroy(owner);
    Check(freightOwner==nullptr&&!FreightRegistered(controls[0]->data),"window destructor invalidates freight registrations");
    for(auto* control:controls)delete control;
    for(auto* item:oldCities)delete item;
}
} // namespace

#include "native_player_filters_test.h"

int Run(int argc,char** argv) {
    nativeAssign=MakeAssignFixture();nativeSetFont=FontFixture<SetFont>(kSetFontCode);enabled=true;
    core=static_cast<unsigned char*>(VirtualAlloc(nullptr,kImageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(core!=nullptr,"fake image allocation");
    Check(!VerifyImage(),"unknown image disabled");FillGuards(core);Check(VerifyImage(),"reviewed code and structure guard accepted");
    auto* imageHeader=reinterpret_cast<IMAGE_NT_HEADERS64*>(core+reinterpret_cast<IMAGE_DOS_HEADER*>(core)->e_lfanew);
    imageHeader->OptionalHeader.SizeOfImage=kImageSize+0x1000;Check(VerifyImage(),"unrelated image size change accepted");
    imageHeader->OptionalHeader.SizeOfImage=0x1000;Check(!VerifyImage(),"guard outside image refused");
    imageHeader->OptionalHeader.SizeOfImage=kImageSize;
    core[kGuards[0].rva]^=1;Check(!VerifyImage(),"modified loaded code disabled");core[kGuards[0].rva]^=1;
    const auto pin=*reinterpret_cast<void**>(core+kComboVtable+0x10);
    *reinterpret_cast<void**>(core+kComboVtable+0x10)=nullptr;Check(!VerifyImage(),"wrong native vtable disabled");
    *reinterpret_cast<void**>(core+kComboVtable+0x10)=pin;
    // Exercise the exact reviewed assign body at the SSO boundary and during growth.
    struct {std::uint64_t left=0x1122334455667788;NativeString s{};std::uint64_t right=0x8877665544332211;} storage;
    EmptyWide(storage.s);
    for(const wchar_t* text:{L"",L"中文",L"一二三四五六七",L"一二三四五六七八",L"需要超过小字符串空间的中文说明，用于验证原生分配、扩容和字符结尾。",L"短"}) {
        nativeAssign(&storage.s,text,std::wcslen(text));
        Check(Wide(&storage.s)&&!std::wcscmp(Wide(&storage.s),text),"reviewed assign SSO/heap/growth/terminator");
        Check(storage.left==0x1122334455667788&&storage.right==0x8877665544332211,"native string boundary canaries");
    }
    DestroyWide(storage.s);
    for(const auto& row:kRows){
        CheckPair(row.label,0);if(row.tooltip.en)CheckPair(row.tooltip,1);
        struct Descriptor {void* vtable;Text label,tooltip;std::uint64_t value;};
        static_assert(offsetof(Descriptor,label)==8&&offsetof(Descriptor,tooltip)==0x48);
        Descriptor d{nullptr,Text(row.label.en),Text(row.tooltip.en?row.tooltip.en:""),0xcafebabefeedbeef};
        pageScope=static_cast<int>(row.page);TranslateDescriptor(&d);
        Check(!std::wcscmp(Wide(&d.label.wide),row.label.zh),"all 52 descriptor labels routed on their page");
        if(row.tooltip.en)Check(!std::wcscmp(Wide(&d.tooltip.wide),row.tooltip.zh),"all 27 descriptor tooltips routed");
        Check(d.value==0xcafebabefeedbeef,"native setting value canary retained");
    }
    pageScope=-1;
    for(const auto& page:kPages) {
        alignas(8) unsigned char object[0x68]{};
        NativeString name;MakeNarrow(name,page.en);std::memcpy(object+0x20,&name,sizeof name);
        Check(PageIndex(object)==&page-kPages,"five native page identifiers recognized");DestroyNarrow(name);
    }
    for(const auto& page:kPages)CheckPair(page,2);
    for(const auto& group:kOptions)for(unsigned i=0;i<group.count;++i)CheckPair(group.pairs[i],3);
    CheckPair(kWindow,4);
    windowScope=true;
    for(const auto& literal:kLiterals) {
        Text t(literal.text.en);TranslateLiteral(&t,core+literal.source,literal.caller);
        Check(!std::wcscmp(Wide(&t.wide),literal.text.zh),"exact footer site translated");
        Text other(literal.text.en);TranslateLiteral(&other,literal.text.en,literal.caller);
        Check(WideIsEnglish(&other.wide,literal.text.en),"user-owned equal text excluded by pointer");
    }
    Text title("Settings");Check(ConversionText(&title.narrow,0x32f2de)==&kWindow,"window title routed");
    windowScope=false;Check(!ConversionText(&title.narrow,0x32f2de),"other windows excluded");
    TestScopesAndDescriptor();
    TestNativeFontMeasurement();
    TestAlreadyCreatedWindow();
    TestFreightFonts();
    playerfilterfixture::Run();
    Text unknown("My Player 公开");Check(!TranslateText(&unknown,kRows[0].label,0),"player-owned text unchanged");
    Text disabled(kRows[0].label.en);enabled=false;Check(!TranslateText(&disabled,kRows[0].label,0),"disabled adapter forwards unchanged");
    enabled=true;
    // MinHook must relocate all reviewed prologues without enabling them.
    DWORD old;Check(VirtualProtect(core,kImageSize,PAGE_EXECUTE_READWRITE,&old)!=0,"fake code protection");
    Check(MH_Initialize()==MH_OK,"isolated MinHook initialized");
    for(unsigned rva:{0x13d110u,0x13de20u,0x13e130u,0x13e860u,0xf0310u,0xef4f0u,0xf0210u,0x13da10u,
                      0xfc300u,0xfc260u,0x3304f0u,0x331700u,0x1cdc10u,0x1cb500u,0x1d66c0u,0x1cf710u,0x1cd970u}) {
        void* original=nullptr;Check(MH_CreateHook(core+rva,reinterpret_cast<void*>(&HookCString),&original)==MH_OK,"reviewed native prologue trampoline");
        Check(original!=nullptr,"trampoline produced");Check(MH_RemoveHook(core+rva)==MH_OK,"unarmed test hook removed");
    }
    MH_Uninitialize();
    Check(scs_telemetry_init(0,nullptr)==SCS_RESULT_unsupported,"unsupported loader API disabled");
    Check(scs_telemetry_init(SCS_TELEMETRY_VERSION_1_01,nullptr)==SCS_RESULT_invalid_parameter,"null loader parameters rejected");
    scs_telemetry_init_params_v100_t params{};params.common.game_id="ats";
    Check(scs_telemetry_init(SCS_TELEMETRY_VERSION_1_01,&params)==SCS_RESULT_unsupported,"ATS excluded");
    params.common.game_id="eut2";
    Check(scs_telemetry_init(SCS_TELEMETRY_VERSION_1_01,&params)==SCS_RESULT_unsupported,"non-game executable excluded");
    if(argc>=2) {
        HMODULE dll=LoadLibraryA(argv[1]);Check(dll!=nullptr,"production DLL load in isolated non-game host");
        auto init=reinterpret_cast<scs_result_t(*)(scs_u32_t,const scs_telemetry_init_params_t*)>(GetProcAddress(dll,"scs_telemetry_init"));
        Check(init&&GetProcAddress(dll,"scs_telemetry_shutdown"),"SCS exports present");
        Check(!GetProcAddress(dll,"truckersmp_init")&&!GetProcAddress(dll,"truckersmp_shutdown"),"no TMP SDK exports");
        Check(init(SCS_TELEMETRY_VERSION_1_01,&params)==SCS_RESULT_unsupported,"production DLL inert outside game");
        FreeLibrary(dll);
    }
    return 0;
}
int main(int argc,char** argv) {
    Run(argc,argv);
    for(auto& slot:settingsFonts)if(slot.used){DestroyWide(slot.font.name);slot.used=false;}
    for(auto& slot:freightFonts)if(slot.used){DestroyWide(slot.font.name);slot.used=false;}
    Check(allocated==released,"all native fixture/string allocations released by the same allocator");
    VirtualFree(core,0,MEM_RELEASE);VirtualFree(reinterpret_cast<void*>(nativeAssign),0,MEM_RELEASE);
    std::printf("PASS: %u checks; Settings/freight, four player filters, native ownership, lifecycle, guards, seventeen trampolines, SCS-only DLL.\n",checks);
    std::printf("No game/TMP process was started or attached; in-game validation remains outstanding.\n");
    if(argc>=3) {
        FILE* report=nullptr;fopen_s(&report,argv[2],"wb");Check(report!=nullptr,"test report output");
        std::fprintf(report,"{\"passed\":true,\"checks\":%u,\"mode\":\"isolated_native_fixture\",\"game_started\":false,\"tmp_core_loaded\":false,\"allocations\":%u,\"releases\":%u,\"live_validation\":\"not_performed\"}\n",checks,allocated,released);
        Check(std::fclose(report)==0,"test report flushed");
    }
    return 0;
}
