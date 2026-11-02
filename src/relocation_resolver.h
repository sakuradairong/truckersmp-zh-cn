// Bounded, read-only PE compatibility checks shared by installer and runtime.
#pragma once
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include "../vendor/picojson/picojson.h"

namespace tmpcompat {
using Json=picojson::value;
using Object=picojson::object;
using Array=picojson::array;
using U=std::uint32_t;
using Pair=std::pair<U,U>;
inline void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
inline const Json& Get(const Json& j,const char* k){Need(j.is<Object>(),"invalid profile object");auto i=j.get<Object>().find(k);Need(i!=j.get<Object>().end(),"missing profile field");return i->second;}
inline bool Has(const Json& j,const char* k){return j.is<Object>()&&j.get<Object>().count(k)!=0;}
inline U Number(const Json& j){Need(j.is<double>()&&j.get<double>()>=0&&j.get<double>()<=0xffffffff&&j.get<double>()==static_cast<U>(j.get<double>()),"invalid profile integer");return static_cast<U>(j.get<double>());}
inline U Num(const Json& j,const char* k){return Number(Get(j,k));}
inline std::string Str(const Json& j,const char* k){const auto& v=Get(j,k);Need(v.is<std::string>(),"invalid profile text");return v.get<std::string>();}
inline const Array& Items(const Json& j,const char* k){const auto& v=Get(j,k);Need(v.is<Array>(),"invalid profile array");return v.get<Array>();}
inline std::vector<unsigned char> Hex(const std::string& text){
    Need(text.size()%2==0&&text.size()<=4000000,"invalid profile bytes");std::vector<unsigned char> out;
    const auto nib=[](char c)->unsigned{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::runtime_error("invalid hex");};
    out.reserve(text.size()/2);for(std::size_t i=0;i<text.size();i+=2)out.push_back(static_cast<unsigned char>(nib(text[i])*16+nib(text[i+1])));return out;
}
inline bool CopyReadable(void* to,const void* from,std::size_t size) noexcept {
    auto at=reinterpret_cast<std::uintptr_t>(from);if(size>UINTPTR_MAX-at)return false;
    const auto end=at+size;
    while(at<end){MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery(reinterpret_cast<void*>(at),&m,sizeof m)||m.State!=MEM_COMMIT||(m.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
        const DWORD p=m.Protect&0xff;if(p!=PAGE_READONLY&&p!=PAGE_READWRITE&&p!=PAGE_WRITECOPY&&p!=PAGE_EXECUTE_READ&&p!=PAGE_EXECUTE_READWRITE&&p!=PAGE_EXECUTE_WRITECOPY)return false;
        auto next=reinterpret_cast<std::uintptr_t>(m.BaseAddress)+m.RegionSize;if(next<=at)return false;at=next;}
    __try{std::memcpy(to,from,size);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
struct Section{U at,size,kind;};
struct Image {
    std::vector<unsigned char> bytes;std::vector<Section> sections;std::uint64_t base=0;
    std::map<U,std::string> imports;bool loaded=false;U tlsSize=0;
    const unsigned char* At(U at,std::size_t size)const{Need(at<=bytes.size()&&size<=bytes.size()-at,"image bounds");return bytes.data()+at;}
    template<class T>T Read(U at)const{T value{};std::memcpy(&value,At(at,sizeof value),sizeof value);return value;}
    U Kind(U at,std::size_t size=1)const{U kind=0;for(const auto& s:sections)if(at>=s.at&&size<=s.size&&at-s.at<=s.size-size){Need(!kind,"overlapping image sections");kind=s.kind;}Need(kind!=0,"target outside sections");return kind;}
    std::string Text(U at,std::size_t limit=512)const{std::string out;for(std::size_t i=0;i<limit;++i){char c=static_cast<char>(*At(at+static_cast<U>(i),1));if(!c)return out;Need(c>=32&&c<127,"invalid image name");out+=c;}throw std::runtime_error("unterminated image name");}
    void ParseImports(const IMAGE_NT_HEADERS64& nt){
        const auto dir=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(!dir.Size)return;
        Need(dir.Size<=1024*1024,"import directory budget");
        bool end=false;
        for(U pos=0;pos+sizeof(IMAGE_IMPORT_DESCRIPTOR)<=dir.Size;pos+=sizeof(IMAGE_IMPORT_DESCRIPTOR)){
            const auto entry=Read<IMAGE_IMPORT_DESCRIPTOR>(dir.VirtualAddress+pos);if(!entry.Name){end=true;break;}
            auto name=Text(entry.Name);for(auto& c:name)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
            const U names=entry.OriginalFirstThunk?entry.OriginalFirstThunk:entry.FirstThunk;Need(!loaded||entry.OriginalFirstThunk,"loaded image lacks import names");
            bool done=false;for(U i=0;i<16384;++i){auto thunk=Read<std::uint64_t>(names+i*8);if(!thunk){done=true;break;}
                std::string symbol;if(thunk&IMAGE_ORDINAL_FLAG64)symbol="#"+std::to_string(thunk&0xffff);
                else{Need(thunk<=0xffffffff,"invalid import RVA");symbol=Text(static_cast<U>(thunk)+2);}
                const U slot=entry.FirstThunk+i*8;Kind(slot,8);imports[slot]=name+"!"+symbol;
            }
            Need(done,"unterminated import table");}
        Need(end,"unterminated import directory");
    }
    bool ImportIs(U slot,const std::string& identity)const{
        auto found=imports.find(slot);if(found==imports.end()||found->second!=identity)return false;if(!loaded)return true;
        const auto split=identity.find('!');Need(split!=std::string::npos,"invalid import identity");
        const auto moduleName=identity.substr(0,split),symbol=identity.substr(split+1);
        Need(!moduleName.empty()&&!symbol.empty()&&moduleName.find_first_of("/\\:")==std::string::npos,"invalid import module name");
        HMODULE module=GetModuleHandleA(moduleName.c_str());bool owned=false;
        if(!module&&(moduleName.rfind("api-ms-win-",0)==0||moduleName.rfind("ext-ms-win-",0)==0)){
            module=LoadLibraryExA(moduleName.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);owned=module!=nullptr;}
        if(!module)return false;
        auto expected=GetProcAddress(module,symbol[0]=='#'?MAKEINTRESOURCEA(std::stoul(symbol.substr(1))):symbol.c_str());
        bool ok=expected&&Read<std::uint64_t>(slot)==reinterpret_cast<std::uint64_t>(expected);if(owned)FreeLibrary(module);return ok;
    }
    static Image Parse(const unsigned char* data,std::size_t length,bool mapped,std::uint64_t address=0){
        Need(length>=4096&&length<=256000000,"invalid image length");
        const auto read=[&](std::size_t at,void* dest,std::size_t n){Need(at<=length&&n<=length-at,"truncated PE");std::memcpy(dest,data+at,n);};
        IMAGE_DOS_HEADER dos{};read(0,&dos,sizeof dos);Need(dos.e_magic==IMAGE_DOS_SIGNATURE&&dos.e_lfanew>=64,"invalid DOS header");
        IMAGE_NT_HEADERS64 nt{};read(dos.e_lfanew,&nt,sizeof nt);
        Need(nt.Signature==IMAGE_NT_SIGNATURE&&nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64&&nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC,"requires x64 PE");
        Need(nt.FileHeader.NumberOfSections>0&&nt.FileHeader.NumberOfSections<=96&&nt.FileHeader.SizeOfOptionalHeader>=sizeof(IMAGE_OPTIONAL_HEADER64),"invalid section header");
        const auto size=nt.OptionalHeader.SizeOfImage,headers=nt.OptionalHeader.SizeOfHeaders;
        Need(headers>=sizeof nt&&headers<=size&&headers<=length&&size<=128000000,"invalid image size");
        Image result;result.base=mapped?address:nt.OptionalHeader.ImageBase;result.loaded=mapped;result.bytes.resize(size);
        read(0,result.bytes.data(),headers);
        U sectionAt=static_cast<U>(dos.e_lfanew)+24+nt.FileHeader.SizeOfOptionalHeader;
        for(unsigned i=0;i<nt.FileHeader.NumberOfSections;++i){IMAGE_SECTION_HEADER s{};read(sectionAt+i*sizeof s,&s,sizeof s);
            U extent=(std::max)(s.Misc.VirtualSize,s.SizeOfRawData);Need(s.VirtualAddress>=headers&&s.VirtualAddress<=size&&extent<=size-s.VirtualAddress,"invalid section bounds");
            for(const auto& previous:result.sections)Need(s.VirtualAddress+extent<=previous.at||s.VirtualAddress>=previous.at+previous.size,"overlapping PE sections");
            U kind=(s.Characteristics&IMAGE_SCN_MEM_EXECUTE)?1:(s.Characteristics&IMAGE_SCN_MEM_WRITE)?3:2;
            result.sections.push_back({s.VirtualAddress,extent,kind});
            read(mapped?s.VirtualAddress:s.PointerToRawData,result.bytes.data()+s.VirtualAddress,mapped?extent:s.SizeOfRawData);}
        result.ParseImports(nt);
        const auto tls=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
        if(tls.Size){Need(tls.Size>=sizeof(IMAGE_TLS_DIRECTORY64),"invalid TLS directory");auto t=result.Read<IMAGE_TLS_DIRECTORY64>(tls.VirtualAddress);
            Need(t.EndAddressOfRawData>=t.StartAddressOfRawData&&t.EndAddressOfRawData-t.StartAddressOfRawData+t.SizeOfZeroFill<0x1000000,"invalid TLS extent");
            result.tlsSize=static_cast<U>(t.EndAddressOfRawData-t.StartAddressOfRawData+t.SizeOfZeroFill);}
        return result;
    }
    static Image File(const wchar_t* path){
        HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Need(file!=INVALID_HANDLE_VALUE,"cannot read core file");
        struct Close{HANDLE h;~Close(){CloseHandle(h);}} close{file};LARGE_INTEGER length{};Need(GetFileSizeEx(file,&length)&&length.QuadPart>=4096&&length.QuadPart<=256000000,"invalid core file size");
        std::vector<unsigned char> raw(static_cast<std::size_t>(length.QuadPart));DWORD got=0;Need(ReadFile(file,raw.data(),static_cast<DWORD>(raw.size()),&got,nullptr)&&got==raw.size(),"incomplete core read");
        return Parse(raw.data(),raw.size(),false);
    }
    static Image Module(HMODULE module){
        unsigned char header[4096];Need(CopyReadable(header,module,sizeof header),"module headers unreadable");
        auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(header);Need(dos->e_magic==IMAGE_DOS_SIGNATURE&&dos->e_lfanew>=64&&dos->e_lfanew<3000,"bad module header");
        auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(header+dos->e_lfanew);U size=nt->OptionalHeader.SizeOfImage;
        Need(size>=4096&&size<=128000000,"bad module size");std::vector<unsigned char> raw(size);
        const U headers=nt->OptionalHeader.SizeOfHeaders;Need(headers>=sizeof(IMAGE_NT_HEADERS64)&&headers<=size&&headers<=1024*1024,"bad module header size");
        Need(CopyReadable(raw.data(),module,headers),"module headers unreadable");
        const U table=static_cast<U>(dos->e_lfanew)+24+nt->FileHeader.SizeOfOptionalHeader;
        Need(nt->FileHeader.NumberOfSections<=96&&table+nt->FileHeader.NumberOfSections*sizeof(IMAGE_SECTION_HEADER)<=headers,"module section table bounds");
        for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){IMAGE_SECTION_HEADER s{};std::memcpy(&s,raw.data()+table+i*sizeof s,sizeof s);
            U extent=(std::max)(s.Misc.VirtualSize,s.SizeOfRawData);Need(s.VirtualAddress<=size&&extent<=size-s.VirtualAddress,"module section bounds");
            bool copied=CopyReadable(raw.data()+s.VirtualAddress,reinterpret_cast<unsigned char*>(module)+s.VirtualAddress,extent);
            Need(copied||(s.Characteristics&IMAGE_SCN_MEM_DISCARDABLE),"required module section unreadable");}
        return Parse(raw.data(),raw.size(),true,reinterpret_cast<std::uint64_t>(module));
    }
};
struct Pattern {Json spec;U old=0,size=0;std::vector<unsigned char> raw,mask;};
struct Candidate {U at;std::vector<Pair> refs;};
struct Result {
    unsigned capabilities=0;bool moved=false;std::map<U,U> addresses;std::string detail;
    U Find(U old)const{auto i=addresses.find(old);return i==addresses.end()?0:i->second;}
};
class Resolver {
    const Image& image;Json profile;std::vector<Pattern> nodes;std::map<U,Pattern> contracts;
    std::map<U,std::vector<Candidate>> candidates;std::map<U,Pair> aliases;
    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();std::size_t work=0;
    void Budget(){Need(++work<4000000&&std::chrono::steady_clock::now()-start<std::chrono::seconds(15),"compatibility search budget exhausted");}
    Pattern Make(const Json& j,U old){Pattern p;p.spec=j;p.old=old;p.size=Num(j,"size");if(Has(j,"raw")){p.raw=Hex(Str(j,"raw"));p.mask=Hex(Str(j,"mask"));Need(p.raw.size()==p.size&&p.mask.size()==p.size,"invalid pattern size");}return p;}
    bool Match(const Pattern& p,U at)const{const auto* raw=image.At(at,p.size);for(U i=0;i<p.size;++i)if((raw[i]&p.mask[i])!=(p.raw[i]&p.mask[i]))return false;return true;}
    static bool Consistent(const Candidate& candidate){std::map<U,U> identities;for(const auto& ref:candidate.refs){auto inserted=identities.emplace(ref.first,ref.second);if(!inserted.second&&inserted.first->second!=ref.second)return false;}return true;}
    U Target(U at,const Json& ref)const{
        U off=Num(ref,"off"),width=Num(ref,"width"),mode=Num(ref,"mode");Need(width==1||width==2||width==4||width==8,"invalid operand width");
        std::uint64_t value=0;std::memcpy(&value,image.At(at+off,width),width);
        if(mode==0){Need(value>=image.base&&value-image.base<=0xffffffff,"pointer outside image");return static_cast<U>(value-image.base);}
        if(mode==1){std::int64_t signedValue=0;if(width==1)signedValue=static_cast<std::int8_t>(value);else if(width==2)signedValue=static_cast<std::int16_t>(value);else if(width==4)signedValue=static_cast<std::int32_t>(value);else throw std::runtime_error("invalid relative operand");
            const auto target=static_cast<std::int64_t>(at)+Num(ref,"next")+signedValue;Need(target>=0&&target<=0xffffffff,"relative target outside image");return static_cast<U>(target);}
        if(mode==3){Need(value<image.tlsSize&&Num(ref,"next")<=image.tlsSize-value,"TLS slot outside block");return 0xf0000000u+static_cast<U>(value);}
        Need(mode==2&&value<=0xffffffff,"invalid image-relative operand");return static_cast<U>(value);
    }
    bool Role(U at,U kind,const Json& ref)const{
        if(!kind)return at==0;
        if(kind==4)return image.ImportIs(at,Str(ref,"terminal_import"));
        if(kind==5)return at>=0xf0000000u&&at-0xf0000000u<image.tlsSize;
        return image.Kind(at)==kind;
    }
    bool Contract(U old,U at,std::vector<Pair>& refs,std::set<Pair>& seen,unsigned depth){
        Budget();Need(depth<12,"dependency recursion budget");auto i=contracts.find(old);if(i==contracts.end())return true;
        if(!seen.insert({old,at}).second)return true;
        const auto& p=i->second;U kind=Num(p.spec,"kind");if(!kind)return at==0;
        if(kind==4)return image.ImportIs(at,Str(p.spec,"name"));
        if(image.Kind(at,p.size)!=kind)return false;
        if(kind!=1){U modulus=Has(p.spec,"alignment_modulus")?Num(p.spec,"alignment_modulus"):8;Need(modulus>0&&modulus<=16,"invalid alignment");if(at%modulus!=Num(p.spec,"alignment"))return false;}
        if(!p.raw.empty()&&!Match(p,at))return false;
        if(Has(p.spec,"zeros"))for(const auto& z:Items(p.spec,"zeros"))if(image.Read<U>(at+Number(z)))return false;
        for(const auto& ref:Items(p.spec,"refs")){
            U target=Target(at,ref),expected=Num(ref,"target");refs.push_back({expected,target});
            if(Has(ref,"terminal_kind")){if(!Role(target,Num(ref,"terminal_kind"),ref))return false;}
            else if(!Contract(expected,target,refs,seen,depth+1))return false;
        }return true;
    }
    bool Own(U target,Pair& result)const{
        if(candidates.count(target)){result={target,0};return true;}
        auto alias=aliases.find(target);if(alias!=aliases.end()){result=alias->second;return true;}
        bool found=false;for(const auto& n:nodes)if(target>=n.old&&target-n.old<n.size&&(!found||n.old>result.first)){result={n.old,target-n.old};found=true;}return found;
    }
    using Domains=std::map<U,std::set<U>>;
    Domains DomainsFor()const{
        Domains result;std::set<U> initialized;
        const auto intersect=[&](U target,const std::set<U>& incoming){if(initialized.insert(target).second)result[target]=incoming;
            else{std::set<U> common;std::set_intersection(result[target].begin(),result[target].end(),incoming.begin(),incoming.end(),std::inserter(common,common.end()));result[target]=std::move(common);}};
        for(const auto& row:candidates){Domains combined;for(const auto& c:row.second)for(const auto& ref:c.refs)combined[ref.first].insert(ref.second);
            for(const auto& row2:combined)intersect(row2.first,row2.second);}
        for(auto& row:result){Pair own;if(Own(row.first,own)){std::set<U> values;for(const auto& c:candidates.at(own.first))values.insert(c.at+own.second);intersect(row.first,values);}}
        return result;
    }
    void Narrow(){
        for(unsigned iteration=0;iteration<512;++iteration){Budget();bool changed=false;
            // A broken caller must not erase an independently proven callee.
            // Reject impossible outgoing edges before propagating inbound identity.
            for(auto& row:candidates){auto& choices=row.second;auto end=std::remove_if(choices.begin(),choices.end(),[&](const Candidate& c){
                for(const auto& ref:c.refs){Pair own;if(Own(ref.first,own)){
                    auto target=candidates.find(own.first);if(target!=candidates.end()&&std::none_of(target->second.begin(),target->second.end(),[&](const Candidate& value){return value.at+own.second==ref.second;}))return true;}}
                return false;});changed=changed||end!=choices.end();choices.erase(end,choices.end());}
            auto allowed=DomainsFor();
            std::map<U,std::vector<Pair>> inbound;for(const auto& a:allowed){Pair own;if(Own(a.first,own))inbound[own.first].push_back({a.first,own.second});}
            for(auto& row:candidates){auto& choices=row.second;auto end=std::remove_if(choices.begin(),choices.end(),[&](const Candidate& c){
                for(const auto& ref:c.refs)if(!allowed.at(ref.first).count(ref.second))return true;
                for(const auto& ref:inbound[row.first])if(!allowed.at(ref.first).count(c.at+ref.second))return true;return false;});
                changed=changed||end!=choices.end();choices.erase(end,choices.end());}
            if(!changed)return;}
        throw std::runtime_error("constraint iteration budget");
    }
    U Resolve(U at,const Domains& domains)const{Pair own;if(Own(at,own)){const auto& c=candidates.at(own.first);return c.size()==1?c[0].at+own.second:0;}
        auto i=domains.find(at);return i!=domains.end()&&i->second.size()==1?*i->second.begin():0;}
public:
    Resolver(const Image& source,const char* text):image(source){Need(picojson::parse(profile,text).empty(),"invalid embedded profile");Need(Num(profile,"schema")==2&&Num(profile,"abi")==1,"unsupported adapter ABI");
        for(const auto& n:Items(profile,"nodes"))nodes.push_back(Make(n,Num(n,"old")));
        std::sort(nodes.begin(),nodes.end(),[](const Pattern& a,const Pattern& b){return a.size>b.size;});
        Need(nodes.size()<=1024,"node budget");for(const auto& c:Get(profile,"contracts").get<Object>())contracts.emplace(static_cast<U>(std::stoul(c.first)),Make(c.second,static_cast<U>(std::stoul(c.first))));
        for(const auto& a:Get(profile,"aliases").get<Object>())aliases[static_cast<U>(std::stoul(a.first))]={Num(a.second,"parent"),Num(a.second,"offset")};}
    Result RunOne(){
        for(const auto& p:nodes){Budget();auto& found=candidates[p.old];std::size_t best=0,count=0,offset=0;
            for(std::size_t i=0;i<=p.mask.size();++i){if(i<p.mask.size()&&p.mask[i]==255)++count;else{if(count>best){best=count;offset=i-count;}count=0;}}
            Need(best>=1,"unbounded instruction signature");
            for(const auto& s:image.sections){if(s.kind!=1||s.size<p.size)continue;const auto* begin=image.At(s.at,s.size);const auto* end=begin+s.size;
                const auto* pos=begin;while((pos=std::search(pos,end,p.raw.data()+offset,p.raw.data()+offset+best))!=end){Budget();auto delta=static_cast<std::size_t>(pos-begin);++pos;
                    if(delta<offset||delta-offset>s.size-p.size)continue;U at=s.at+static_cast<U>(delta-offset);if(!Match(p,at))continue;
                    Candidate candidate{at,{}};bool valid=true;std::set<Pair> seen;
                    try{for(const auto& ref:Items(p.spec,"refs")){U target=Target(at,ref),expected=Num(ref,"target");candidate.refs.push_back({expected,target});
                        Pair own;if(Own(expected,own)){auto known=candidates.find(own.first);if(own.first==p.old){if(target!=at+own.second){valid=false;break;}}
                            else if(known!=candidates.end()&&std::none_of(known->second.begin(),known->second.end(),[&](const Candidate& c){return target==c.at+own.second;})){valid=false;break;}}
                        if(Has(ref,"terminal_kind")?!Role(target,Num(ref,"terminal_kind"),ref):!Contract(expected,target,candidate.refs,seen,0)){valid=false;break;}}}
                    catch(const std::runtime_error&){valid=false;}
                    if(valid&&Consistent(candidate))found.push_back(std::move(candidate));Need(found.size()<=4096,"ambiguous signature budget");}}
        }
        Narrow();
        for(const auto& table:Items(profile,"tables")){U old=Num(table,"old");std::set<U> possible;bool first=true;
            for(const auto& ref:Items(table,"refs")){Pair own;if(!Own(Num(ref,"target"),own))continue;std::set<U> positions;
                for(const auto& c:candidates[own.first]){std::uint64_t pointer=image.base+c.at+own.second;const auto* needle=reinterpret_cast<const unsigned char*>(&pointer);
                    for(const auto& s:image.sections){if(s.kind!=2||s.size<8)continue;const auto* begin=image.At(s.at,s.size);const auto* end=begin+s.size;const auto* pos=begin;
                        while((pos=std::search(pos,end,needle,needle+8))!=end){Budget();auto value=s.at+static_cast<U>(pos-begin);++pos;U slot=Num(ref,"slot");if(value>=slot&&(value-slot)%8==0)positions.insert(value-slot);}}}
                if(first){possible=std::move(positions);first=false;}else{std::set<U> common;std::set_intersection(possible.begin(),possible.end(),positions.begin(),positions.end(),std::inserter(common,common.end()));possible=std::move(common);}}
            auto& found=candidates[old];Need(possible.size()<=4096,"vtable ambiguity budget");for(U at:possible){Candidate c{at,{}};bool valid=true;
                try{for(const auto& ref:Items(table,"refs")){U slot=Num(ref,"slot");image.Kind(at+slot,8);auto value=image.Read<std::uint64_t>(at+slot);Need(value>=image.base&&value-image.base<=0xffffffff,"vtable pointer bounds");c.refs.push_back({Num(ref,"target"),static_cast<U>(value-image.base)});}}
                catch(const std::runtime_error&){valid=false;}if(valid)found.push_back(std::move(c));}}
        Narrow();auto domains=DomainsFor();Result result;std::set<U> wanted;
        for(const auto& n:nodes)wanted.insert(n.old);for(const auto& c:candidates)wanted.insert(c.first);for(const auto& d:domains)wanted.insert(d.first);
        for(const auto& key:Items(profile,"required"))wanted.insert(Number(key));
        for(const auto& l:Items(profile,"literals")){wanted.insert(Num(l,"source"));wanted.insert(Num(l,"caller"));}
        for(U old:wanted)if(U value=Resolve(old,domains)){result.addresses[old]=value;result.moved=result.moved||value!=old;}
        unsigned ok=7;
        for(const auto& guard:Items(profile,"guards"))if(!Resolve(Num(guard,"rva"),domains)){ok&=~Num(guard,"group");if(result.detail.empty())result.detail="unmatched "+Str(guard,"name");}
        for(const auto& l:Items(profile,"literals"))if(!Resolve(Num(l,"source"),domains)||!Resolve(Num(l,"caller"),domains))ok&=~1u;
        for(const auto& table:Items(profile,"tables")){U old=Num(table,"old");unsigned group=(old==0xdccac0)?2:old==0xdd8340?1:3;if(!Resolve(old,domains))ok&=~group;}
        for(const auto& row:Get(profile,"required_groups").get<Object>())for(const auto& key:row.second.get<Array>())
            if(!Resolve(Number(key),domains))ok&=~static_cast<unsigned>(std::stoul(row.first));
        result.capabilities=ok;if(result.detail.empty())result.detail=ok==7?"compatible":"dependency unavailable";return result;
    }
    Result Run(){
        Result result=RunOne();if(result.capabilities==7||!Has(profile,"variants"))return result;
        const auto count=[](unsigned value){unsigned n=0;while(value){n+=value&1;value>>=1;}return n;};
        // Never combine addresses from different builds. One complete profile
        // must establish all dependencies for each returned capability set.
        for(const auto& variant:Items(profile,"variants")){
            Need(!Has(variant,"variants"),"nested compatibility variants");
            const auto encoded=variant.serialize();auto next=Resolver(image,encoded.c_str()).RunOne();
            if(count(next.capabilities)>count(result.capabilities))result=std::move(next);
            if(result.capabilities==7)break;
        }return result;
    }
};
} // namespace tmpcompat
