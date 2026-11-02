#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/relocation_resolver.h"
#include "relocation_profile.h"
#include <cstdio>

namespace {
unsigned checks=0;
void Check(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
void Put(tmpcompat::Image& image,unsigned at,std::uint64_t value,unsigned size){std::memcpy(image.bytes.data()+at,&value,size);}
const char* fixture=R"PROFILE({"schema":2,"abi":1,"aliases":{},"required":[4096,8192,16384,24576],
"required_groups":{"1":[4096],"2":[4096],"4":[8192]},"literals":[{"source":16384,"caller":4105}],
"guards":[{"rva":4096,"size":17,"name":"settings","group":3},{"rva":8192,"size":5,"name":"players","group":4}],
"nodes":[{"old":4096,"size":17,"raw":"488b4118e800000000488d1500000000c3","mask":"ffffffffff00000000ffffff00000000ff","refs":[
{"off":5,"width":4,"mode":1,"next":9,"target":8192},{"off":12,"width":4,"mode":1,"next":16,"target":16384}]},
{"old":8192,"size":5,"raw":"488b4920c3","mask":"ffffffffff","refs":[]}],
"tables":[{"old":24576,"refs":[{"slot":0,"target":8192},{"slot":8,"target":4096}]}],
"contracts":{"16384":{"kind":2,"size":6,"raw":"68656c6c6f00","mask":"ffffffffffff","alignment":0,"refs":[]}}})PROFILE";
tmpcompat::Image Make(unsigned a=0x1000,unsigned b=0x2000,unsigned text=0x4000,unsigned table=0x6000){
    tmpcompat::Image image;image.bytes.resize(0x10000);image.base=0x140000000;
    image.sections={{0x1000,0x2800,1},{0x4000,0xc000,2}};
    auto first=tmpcompat::Hex("488b4118e800000000488d1500000000c3"),second=tmpcompat::Hex("488b4920c3");
    std::memcpy(image.bytes.data()+a,first.data(),first.size());std::memcpy(image.bytes.data()+b,second.data(),second.size());
    Put(image,a+5,static_cast<std::uint32_t>(b-a-9),4);Put(image,a+12,static_cast<std::uint32_t>(text-a-16),4);
    std::memcpy(image.bytes.data()+text,"hello",6);Put(image,table,image.base+b,8);Put(image,table+8,image.base+a,8);return image;
}
tmpcompat::Result Run(const tmpcompat::Image& image,const char* rules=fixture){return tmpcompat::Resolver(image,rules).Run();}
tmpcompat::Image Rebase(const tmpcompat::Image& disk,std::uint64_t base){
    auto image=disk;image.base=base;
    auto dos=disk.Read<IMAGE_DOS_HEADER>(0);auto nt=disk.Read<IMAGE_NT_HEADERS64>(dos.e_lfanew);
    auto dir=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    for(unsigned pos=0;pos<dir.Size;){
        auto block=disk.Read<IMAGE_BASE_RELOCATION>(dir.VirtualAddress+pos);
        Check(block.SizeOfBlock>=sizeof block&&block.SizeOfBlock<=dir.Size-pos,"relocation block bounds");
        for(unsigned off=sizeof block;off<block.SizeOfBlock;off+=2){
            auto entry=disk.Read<unsigned short>(dir.VirtualAddress+pos+off);
            if(entry>>12==IMAGE_REL_BASED_ABSOLUTE)continue;
            Check(entry>>12==IMAGE_REL_BASED_DIR64,"x64 pointer relocation");
            auto at=block.VirtualAddress+(entry&0xfff);
            Put(image,at,disk.Read<std::uint64_t>(at)+base-disk.base,8);
        }
        pos+=block.SizeOfBlock;
    }
    return image;
}
void RebaseFixture(const wchar_t* path){
    auto disk=tmpcompat::Image::File(path);
    Check(Run(disk,tmpcompat::Profile().c_str()).capabilities==7,"real baseline features");
    for(auto base:{0x7ff8e59d0000ull,0x7fff12340000ull,0x170000000ull}){
        auto image=Rebase(disk,base);
        auto result=Run(image,tmpcompat::Profile().c_str());
        Check(result.capabilities==7,"ASLR must preserve Settings, freight and players");
        const auto table=result.Find(0xdf2aa0);Check(table!=0,"ASLR must resolve native text vtable");
        // ASLR normalization must not turn the pointer into unchecked wildcard bytes.
        Put(image,table,image.base+result.Find(0x337530)+1,8);
        Check((Run(image,tmpcompat::Profile().c_str()).capabilities&3)==0,"wrong relocated vtable target rejected");
    }
    std::puts("PASS: real client ASLR rebasing at three bases; wrong pointer rejected; no client code executed");
}
void Unit(){
    auto original=Make();auto first=Run(original);Check(first.capabilities==7&&!first.moved,"baseline fixture");
    auto moved=Make(0x3000,0x1800,0xb000,0x8000);auto result=Run(moved);
    Check(result.capabilities==7&&result.moved,"nonuniform code/data movement");
    Check(result.Find(0x1000)==0x3000&&result.Find(0x2000)==0x1800&&result.Find(0x4000)==0xb000&&result.Find(0x6000)==0x8000,"all consumers receive relocated addresses");
    Check(result.Find(0x1009)==0x3009,"return-address selector moves with its caller");
    auto rebased=moved;rebased.base+=0x17000000;Put(rebased,0x8000,rebased.base+0x1800,8);Put(rebased,0x8008,rebased.base+0x3000,8);
    Check(Run(rebased).capabilities==7,"loaded base normalization");
    auto wrong=moved;wrong.bytes[0x3003]=0x19;Check(Run(wrong).capabilities==4,"field drift disables Settings/freight but preserves independent TAB");
    wrong=moved;Put(wrong,0x3005,static_cast<std::uint32_t>(0x1801-0x3000-9),4);Check(Run(wrong).capabilities==4,"wrong call target rejected");
    wrong=moved;wrong.bytes[0xb000]='x';Check(Run(wrong).capabilities==4,"changed constant rejected");
    wrong=moved;Put(wrong,0x8000,wrong.base+0x1801,8);Check(Run(wrong).capabilities==4,"changed vtable rejected");
    wrong=moved;wrong.bytes[0x1803]=0x21;Check(Run(wrong).capabilities==0,"shared dependency failure propagates");
    wrong=moved;std::memcpy(wrong.bytes.data()+0x2000,wrong.bytes.data()+0x3000,17);
    Put(wrong,0x2005,static_cast<std::uint32_t>(0x1800-0x2000-9),4);Put(wrong,0x200c,static_cast<std::uint32_t>(0xb000-0x2000-16),4);
    Check(Run(wrong).capabilities==7,"vtable disambiguates duplicated instruction body");
    picojson::value ambiguous;picojson::parse(ambiguous,fixture);ambiguous.get<picojson::object>()["tables"]=picojson::value(picojson::array{});
    Check(Run(wrong,ambiguous.serialize().c_str()).capabilities==4,"unresolved duplicate fails closed");
    picojson::value typed;picojson::parse(typed,fixture);
    auto& textContract=typed.get<picojson::object>()["contracts"].get<picojson::object>()["16384"].get<picojson::object>();
    textContract["alignment_modulus"]=picojson::value(1.0);
    Check(Run(Make(0x1000,0x2000,0x4004),typed.serialize().c_str()).capabilities==7,"literal pooling may change modulo-eight residue");
    picojson::value tls;picojson::parse(tls,fixture);
    picojson::value tlsNode;picojson::parse(tlsNode,R"({"old":8192,"size":10,"raw":"b820000000488b4920c3","mask":"ff00000000ffffffffff","refs":[{"off":1,"width":4,"mode":3,"next":8,"target":4026531872,"terminal_kind":5}]})");
    tls.get<picojson::object>()["nodes"].get<picojson::array>()[1]=tlsNode;
    auto tlsImage=Make();tlsImage.tlsSize=128;auto tlsBytes=tmpcompat::Hex("b820000000488b4920c3");std::memcpy(tlsImage.bytes.data()+0x2000,tlsBytes.data(),tlsBytes.size());
    Check(Run(tlsImage,tls.serialize().c_str()).capabilities==7,"typed TLS operand");
    Put(tlsImage,0x2001,64,4);Check(Run(tlsImage,tls.serialize().c_str()).capabilities==7,"TLS offset movement accepted");
    Put(tlsImage,0x2001,124,4);Check(Run(tlsImage,tls.serialize().c_str()).capabilities==0,"TLS extent overflow rejected");
    picojson::value variants;picojson::parse(variants,fixture);auto variant=variants;
    auto& variantNodes=variant.get<picojson::object>()["nodes"].get<picojson::array>();
    variantNodes[0].get<picojson::object>()["raw"]=picojson::value(std::string("488b4119e800000000488d1500000000c3"));
    variantNodes[1].get<picojson::object>()["raw"]=picojson::value(std::string("488b4921c3"));
    variants.get<picojson::object>()["variants"]=picojson::value(picojson::array{variant});
    auto variantImage=Make();variantImage.bytes[0x1003]=0x19;variantImage.bytes[0x2003]=0x21;
    Check(Run(variantImage,variants.serialize().c_str()).capabilities==7,"reviewed complete variant");
    variantImage.bytes[0x2003]=0x20;
    Check(Run(variantImage,variants.serialize().c_str()).capabilities!=7,"partial profiles must never be combined");
    auto raw=original.bytes;auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(raw.data());dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x80;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(raw.data()+0x80);nt->Signature=IMAGE_NT_SIGNATURE;nt->FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
    nt->FileHeader.NumberOfSections=2;nt->FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;nt->OptionalHeader.ImageBase=original.base;nt->OptionalHeader.SizeOfImage=0x10000;nt->OptionalHeader.SizeOfHeaders=0x600;
    auto* sections=reinterpret_cast<IMAGE_SECTION_HEADER*>(raw.data()+0x80+sizeof(IMAGE_NT_HEADERS64));
    for(unsigned i=0;i<2;++i){sections[i].VirtualAddress=original.sections[i].at;sections[i].Misc.VirtualSize=original.sections[i].size;
        sections[i].SizeOfRawData=original.sections[i].size;sections[i].PointerToRawData=original.sections[i].at;
        sections[i].Characteristics=IMAGE_SCN_MEM_READ|(i?0:IMAGE_SCN_MEM_EXECUTE);}
    Check(Run(tmpcompat::Image::Parse(raw.data(),raw.size(),false)).capabilities==7,"file PE mapping");
    void* memory=VirtualAlloc(nullptr,raw.size(),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);Check(memory!=nullptr,"isolated image allocation");
    std::memcpy(memory,raw.data(),raw.size());auto base=reinterpret_cast<std::uint64_t>(memory);
    std::uint64_t value=base+0x2000;std::memcpy(static_cast<unsigned char*>(memory)+0x6000,&value,8);value=base+0x1000;std::memcpy(static_cast<unsigned char*>(memory)+0x6008,&value,8);
    auto snapshot=tmpcompat::Image::Module(static_cast<HMODULE>(memory));Check(snapshot.base==base&&Run(snapshot).capabilities==7,"loaded image snapshot with small headers and gaps");
    VirtualFree(memory,0,MEM_RELEASE);
    bool rejected=false;try{tmpcompat::Image::Parse(raw.data(),0x100,false);}catch(const std::exception&){rejected=true;}Check(rejected,"truncated PE rejected");
    sections[1].VirtualAddress=0x1000;rejected=false;try{tmpcompat::Image::Parse(raw.data(),raw.size(),false);}catch(const std::exception&){rejected=true;}Check(rejected,"overlapping sections rejected");
    std::printf("PASS: %u native relocation checks; no client code executed\n",checks);
}
}

int wmain(int argc,wchar_t** argv){
    try{
        if(argc==3&&!std::wcscmp(argv[1],L"--rebase-fixture")){RebaseFixture(argv[2]);return 0;}
        if(argc<2||!std::wcscmp(argv[1],L"--selftest")){
            Unit();if(argc>=3){FILE* file=nullptr;_wfopen_s(&file,argv[2],L"wb");Check(file!=nullptr,"report output");
                std::fprintf(file,"{\"passed\":true,\"checks\":%u,\"client_code_executed\":false}\n",checks);Check(std::fclose(file)==0,"report flush");}return 0;}
        auto begin=std::chrono::steady_clock::now();
        auto image=tmpcompat::Image::File(argv[1]);
        auto result=tmpcompat::Resolver(image,tmpcompat::Profile().c_str()).Run();
        auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();
        std::printf("capabilities=%u moved=%u addresses=%zu elapsed_ms=%lld detail=%s\n",result.capabilities,result.moved?1:0,result.addresses.size(),elapsed,result.detail.c_str());
        if(argc>2&&result.capabilities!=static_cast<unsigned>(_wtoi(argv[2])))return 1;
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"compatibility rejected: %s\n",e.what());return 1;}
}
