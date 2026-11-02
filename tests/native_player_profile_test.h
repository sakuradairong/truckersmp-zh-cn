// Synthetic identity and API responses only; no requests in the native test suite.
namespace playerprofilefixture {
std::vector<std::string> strings;
void PushString(void*,const char* text){strings.emplace_back(text);}
void ResetCache() {
    using namespace playerprofile;
    Lock lock;account=0;generation=0;lastSeen=0;nextAttempt=0;nextNetwork=0;cached={};
}
void Run() {
    using namespace playerprofile;
    const std::string valid=R"({"error":false,"response":{"id":42,"name":"ignored API nickname","vtc":{"inVTC":true,"id":9,"name":"\u4e2d\u6587 <VTC> \ud83d\ude9a"}}})";
    const auto member=Parse(valid,42);
    Check(member.status==Member&&member.vtc==9&&!std::strcmp(member.name,"中文 <VTC> 🚚"),"public VTC Unicode, escaped surrogate pair and ID parsed");
    Check(Parse(valid,43).status==Failed,"another account response cannot become current VTC");
    Check(Parse(valid+" {}",42).status==Failed,"trailing JSON rejected");
    Check(Parse(valid+" \r\n",42).status==Member,"JSON whitespace accepted");
    const std::string none=R"({"error":false,"response":{"id":42,"vtc":{"inVTC":false,"id":0,"name":null}}})";
    Check(Parse(none,42).status==NoVtc,"explicit absence of VTC distinguished from errors");
    for(const auto* body:{"null","[]","{","{}",R"({"error":true})",
        R"({"error":false,"response":{"id":42}})",
        R"({"error":false,"response":{"id":"42","vtc":{"inVTC":false}}})",
        R"({"error":false,"response":{"id":42.5,"vtc":{"inVTC":false}}})",
        R"({"error":false,"response":{"id":42,"vtc":{"inVTC":true,"id":0,"name":"bad"}}})",
        R"({"error":false,"response":{"id":42,"vtc":{"inVTC":true,"id":9,"name":"x\u0000y"}}})",
        R"({"error":false,"response":{"id":42,"vtc":{"inVTC":true,"id":9,"name":"\ud800"}}})"})
        Check(Parse(body,42).status==Failed,"malformed or missing API data reports failure, never no VTC");
    Check(!BoundedJson(std::string(65537,' ')),"response bound enforced before parsing");
    Check(!BoundedJson(std::string(25,'[')+std::string(25,']')),"JSON depth bound prevents recursive stack exhaustion");
    Check(!BoundedJson(std::string("\xff",1)),"invalid UTF8 refused");
    ResetCache();Ticket first,second;
    Check(Snapshot(42,1000).status==Loading&&Begin(1000,first),"first live account schedules background lookup");
    Check(!Begin(1001,second),"pending requests cannot multiply per frame");
    Complete(first,member,1100);
    Check(Snapshot(42,2000).status==Member&&!Begin(2000,second),"successful result cached for same player");
    Snapshot(43,3000);Complete(first,member,3100);
    Check(Snapshot(43,4000).status==Loading,"account switch drops late result from previous identity");
    Check(!Begin(4000,second)&&Begin(6000,second),"global request spacing survives account switches");
    Snapshot(0,6100);Snapshot(43,6200);Complete(second,member,6300);
    Check(Snapshot(43,6400).status==Loading,"hide/reconnect generation rejects old same-account responses");
    Check(Begin(11000,second),"active session can retry after cancellation");
    Result limited;limited.status=RateLimited;Complete(second,limited,11100);
    Snapshot(43,120000);Check(!Begin(120000,second),"rate limit waits at least two minutes");
    Snapshot(43,132000);Check(Begin(132000,second),"rate-limited request eventually retries");
    ResetCache();Snapshot(42,100);Check(!Begin(6000,second),"hidden or stale panel cannot keep querying");

    // Live source is deliberately different from the filtered candidate vector.
    playerfilterfixture::List live(4),projection(1);
    auto* model=projection.Model();
    *reinterpret_cast<void**>(model+0x38)=live.Begin();
    *reinterpret_cast<void**>(model+0x40)=live.Begin()+4*0x98;
    *reinterpret_cast<void**>(model+0x48)=live.Begin()+4*0x98;
    auto* row=live.rows[3].data();row[0x18]=1;
    *reinterpret_cast<unsigned*>(row+8)=123;*reinterpret_cast<unsigned*>(row+12)=42;
    Local local;Check(ReadLocal(model,local)&&local.account==42&&local.server==123,"local player found outside filtered viewport and search");
    Check(!std::strcmp(local.name,"long native player payload 3/64"),"nickname read from verified native name field");
    row[0x19]=1;Check(!ReadLocal(model,local),"demo local row excluded");row[0x19]=0;
    row[0x1a]=1;Check(!ReadLocal(model,local),"left/history identity excluded");row[0x1a]=0;
    live.rows[0][0x18]=1;Check(!ReadLocal(model,local),"ambiguous local identity rejected");live.rows[0][0x18]=0;
    *reinterpret_cast<void**>(model+0x40)=live.Begin()+1;Check(!ReadLocal(model,local),"invalid live list range rejected");
    *reinterpret_cast<void**>(model+0x40)=live.Begin()+4*0x98;
    playerfilterfixture::current=&projection;
    playerfilters::luaPushString=PushString;playerfilters::active=true;enabled=true;stopped=false;
    strings.clear();playerfilterfixture::pushed.clear();
    Check(playerfilters::LuaLocalProfile(nullptr)==6,"profile Lua binding returns six explicit fields");
    Check(playerfilterfixture::pushed[0]==42&&playerfilterfixture::pushed[1]==123&&strings[0]=="long native player payload 3/64",
          "Lua account, server ID and nickname remain separate");
    Check(*reinterpret_cast<LONG*>(playerfilterfixture::references+8)==1,"profile snapshot releases native shared owner before Lua calls");
    playerfilters::active=false;Check(playerfilters::LuaLocalProfile(nullptr)==1&&playerfilterfixture::pushed.back()==0,"disabled profile exposes no identity");
    playerfilterfixture::current=nullptr;ResetCache();
}
}
