// Execute the fingerprinted native gate in a private allocation, substituting
// only its external network services. No MGSV process or Steam account involved.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "mgscoop/player_ghost_patch.hpp"
#include "mgscoop/player_team_patch.hpp"
#include "player_ghost_contract.hpp"
#include "player_index_contract.hpp"
#include "targeting_contract.hpp"

namespace {
unsigned checks=0;
void Check(bool ok,const char* message) {
    ++checks;
    if(!ok) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
void Log(const char* s) noexcept { std::fputs(s, stderr); }
std::array<std::uintptr_t,16> quark{};
std::array<std::uintptr_t,9> service_table{};
std::array<std::uintptr_t,6> session_table{};
std::uintptr_t service=0,session=0;
std::uint32_t native_count=2;
bool native_host=false;
void* GetQuark() { return quark.data(); }
void* GetSession(void*) { return &session; }
std::uint32_t GetCount(void*) { return native_count; }
bool GetHost(void*) { return native_host; }
template<class T> void Put(unsigned char* destination,T value) { std::memcpy(destination,&value,sizeof(value)); }
}
int main() {
    using namespace mgscoop::player_ghost;
    constexpr std::size_t span=0x1e04000;
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,span,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    Check(image!=nullptr,"fixture allocation");
    auto* body=image+body_rva;
    std::memcpy(body,expected_body,sizeof(expected_body));
    std::memcpy(image+defense_rva,defense_body,sizeof(defense_body));
    std::memcpy(image+offense_rva,offense_body,sizeof(offense_body));
    std::memcpy(image+mgscoop::player_index::defense_rva,mgscoop::player_index::defense_body,sizeof(mgscoop::player_index::defense_body));
    for(const auto& b:mgscoop::targeting_contract::bodies) {
        std::memcpy(image+b.rva,b.bytes,b.size);
    }
    // Current gate calls the Quark accessor at B+BFF050 twice.
    // Its stand-in tail-jumps to our typed service provider.
    unsigned char jump[]={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
    Put(jump+2,reinterpret_cast<std::uintptr_t>(&GetQuark));
    std::memcpy(image+0xbff050,jump,sizeof(jump));
    quark[0x78/8]=reinterpret_cast<std::uintptr_t>(&service);
    service=reinterpret_cast<std::uintptr_t>(service_table.data());
    service_table[0x40/8]=reinterpret_cast<std::uintptr_t>(&GetSession);
    session=reinterpret_cast<std::uintptr_t>(session_table.data());
    session_table[0x18/8]=reinterpret_cast<std::uintptr_t>(&GetCount);
    session_table[0x28/8]=reinterpret_cast<std::uintptr_t>(&GetHost);
    DWORD old=0;
    Check(VirtualProtect(image,span,PAGE_EXECUTE_READ,&old)!=0,"fixture executable");
    Check(FlushInstructionCache(GetCurrentProcess(),image,span)!=0,"fixture instruction cache");
    using Gate=unsigned char (*)(void*,unsigned char);
    auto gate=reinterpret_cast<Gate>(body);
    std::array<unsigned char,0x500> player{};
    auto matrix=[&](bool patched) {
        for(std::uint32_t count: {1u,2u,4u}) for(std::uint32_t net: {1u,2u,3u})
        for(std::uint32_t online: {0u,0x100u}) for(unsigned char flag: std::array<unsigned char,3>{0,1,2})
        for(std::uint16_t mission: std::array<std::uint16_t,3>{30050,50050,10033}) {
            Put(player.data()+0x228,count); Put(player.data()+0x224,online);
            Put(player.data()+0x4ec,mission); native_count=net;
            const bool expected=count==2 && net==2 && online!=0 && flag==0 && (patched || mission==50050);
            Check((gate(player.data(),flag)!=0)==expected,patched ? "patched native branch behavior":"original native branch behavior");
        }
    };
    matrix(false);
    Check(!Install(nullptr,span,Log),"null base rejected");
    Check(!Install(image,body_rva+sizeof(expected_body)-1,Log),"truncated span rejected");
    Check(!Install(image,span,nullptr),"null logger rejected");
    Check(VirtualProtect(body,sizeof(expected_body),PAGE_EXECUTE_READWRITE,&old)!=0,"fixture mutation protection");
    body[branch_offset]=0x66; body[branch_offset+1]=0x90;
    Check(!Install(image,span,Log),"unowned existing patch rejected");
    std::memcpy(body,expected_body,sizeof(expected_body));
    body[0x35]^=1;
    Check(!Install(image,span,Log),"different prerequisite instruction rejected");
    std::memcpy(body,expected_body,sizeof(expected_body));
    Check(VirtualProtect(body,sizeof(expected_body),old,&old)!=0,"restore fixture page");
    SetLastError(0x1234);
    Check(Install(image,span,Log),"install native mission bypass");
    Check(GetLastError()==0x1234,"last error preserved");
    for(std::size_t n=0;n<sizeof(expected_body);++n) {
        const auto expected=n==branch_offset ? 0x66 : n==branch_offset+1 ? 0x90 : expected_body[n];
        Check(body[n]==expected,"only selected branch changed");
    }
    MEMORY_BASIC_INFORMATION memory{};
    Check(VirtualQuery(body,&memory,sizeof(memory))!=0 && memory.Protect==PAGE_EXECUTE_READ,"page protection restored");
    matrix(true);
    Check(Install(image,span,Log),"owned patch idempotent");
    Check(VirtualProtect(body,sizeof(expected_body),PAGE_EXECUTE_READWRITE,&old)!=0,"test owned mutation");
    body[0x35]^=1;
    Check(!Install(image,span,Log),"later prerequisite corruption rejected");
    using Team=bool (*)();
    const auto defense=reinterpret_cast<Team>(image+defense_rva);
    const auto offense=reinterpret_cast<Team>(image+offense_rva);
    for(bool host: {false,true}) {
        native_host=host;
        Check(defense()==!host && offense()==host,"original team predicates reflect native host authority");
    }
    Check(!mgscoop::player_team::Install(image,offense_rva,Log),"team rejects incomplete span");
    Check(VirtualProtect(image+defense_rva,sizeof(defense_body),PAGE_EXECUTE_READWRITE,&old)!=0,"team fixture mutation protection");
    image[defense_rva+0x1e]^=1;
    Check(!mgscoop::player_team::Install(image,span,Log),"team rejects changed negation");
    image[defense_rva+0x1e]^=1;
    Check(VirtualProtect(image+defense_rva,sizeof(defense_body),old,&old)!=0,"restore team fixture protection");
    Check(mgscoop::player_team::Install(image,span,Log),"install paired team hooks");
    for(bool enabled: {false,true,false,true,false}) {
        mgscoop::player_team::SetActive(enabled);
        for(bool host: {false,true}) {
            native_host=host;
            Check(defense()==(enabled ? false : !host),"defense hook forwards outside coop");
            Check(offense()==(enabled ? true : host),"offense hook forwards outside coop");
            Check(GetHost(nullptr)==host,"session host authority preserved");
        }
    }
    // Production hooks and their fixture code remain mapped until process exit.
    std::printf("%u checks passed; 324 actual native ghost cases plus paired team hook lifecycle; no remote player simulated\n",checks);
}

