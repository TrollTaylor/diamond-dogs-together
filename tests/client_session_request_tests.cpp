#include "mgscoop/client_session_request.hpp"
#include <cstdio>
#include <cstdlib>
#include <array>
#include <string>

namespace {
unsigned checks=0;
void Check(bool value) { ++checks; if(!value) { std::fprintf(stderr,"failed check %u\n",checks); std::exit(1); } }
}
int main() {
    using namespace mgscoop;
    // Synthetic IDs used only in memory; this test never loads Steam or MGSV.
    constexpr std::uint64_t id=0x011000010000002aULL;
    const auto text=std::to_string(id);
    Check(ParseClientHostIdentity(text)==id);
    Check(ParseClientHostIdentity(text+"\n")==id);
    Check(ParseClientHostIdentity(text+"\r\n")==id);
    for(const auto& bad:std::array<std::string,11>{{"", "0", "18446744073709551615", "-"+text, "+"+text,
        " "+text,text+" ",text+"\n\n",text+"junk",std::to_string(0x0110000100000000ULL),std::to_string(0x017000010000002aULL)}})
        Check(ParseClientHostIdentity(bad)==0);
    for(std::size_t i=0;i<text.size();++i) { auto bad=text; bad[i]='x'; Check(ParseClientHostIdentity(bad)==0); }
    ClientConnectInfoPrefix info{};
    std::memset(info.reserved,0xa7,sizeof(info.reserved));
    info.host=1234;
    const auto before=info;
    int target=0; unsigned calls=0;
    auto refused=[&](void* actual,ClientConnectInfoPrefix* actual_info)->char {
        ++calls; Check(actual==&target && actual_info==&info); Check(info.host==id && info.state==0);
        Check(std::memcmp(info.reserved,before.reserved,sizeof(info.reserved))==0); return 0;
    };
    Check(!SubmitClientRequest(&target,info,id,refused));
    Check(calls==1 && std::memcmp(&info,&before,sizeof(info))==0);
    Check(!SubmitClientRequest(nullptr,info,id,refused));
    Check(!SubmitClientRequest(&target,info,0,refused));
    info.state=2;
    Check(!SubmitClientRequest(&target,info,id,refused));
    Check(calls==1 && info.host==before.host && info.state==2);
    info.state=0;
    Check(SubmitClientRequest(&target,info,id,[&](void* actual,ClientConnectInfoPrefix* actual_info)->char {
        ++calls; Check(actual==&target && actual_info==&info && actual_info->host==id); return 1;
    }));
    Check(calls==2 && info.host==id && info.state==0);
    Check(std::memcmp(info.reserved,before.reserved,sizeof(info.reserved))==0);
    std::printf("%u client identity, borrowed-argument, refusal rollback and field preservation checks; no network calls\n",checks);
}
