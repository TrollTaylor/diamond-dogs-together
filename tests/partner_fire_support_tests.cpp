#include <initializer_list>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "mgscoop/partner_fire_filter.hpp"
#include "partner_player_contract.hpp"
namespace ff=mgscoop::partner_fire_filter;
namespace c=mgscoop::partner_player_contract;
unsigned checks=0,forwarded=0,preview_writes=0;
void Check(bool ok,const char* label) {++checks;if(!ok){std::fprintf(stderr,"failed: %s\n",label);std::exit(1);}}
void* Original(void* self,const void* request) noexcept {
    ++forwarded;
    std::uint16_t flags=0;std::memcpy(&flags,static_cast<const unsigned char*>(request)+c::thrown_aux,2);
    if(flags&1) ++preview_writes;
    return self;
}
struct Image {
    std::size_t size=c::thrown_slot_live_rva+0x1000;
    unsigned char* data=static_cast<unsigned char*>(VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Image() {
        Check(data!=nullptr,"synthetic image allocation");
        for(const auto& s:c::code_sites) if(s.rva==c::thrown_request_rva) std::memcpy(data+s.rva,s.bytes,s.size);
        const auto target=reinterpret_cast<std::uintptr_t>(data)+c::thrown_request_rva;
        std::memcpy(data+c::thrown_slot_live_rva+c::thrown_slot,&target,8);
        std::memcpy(data+c::thrown_slot_base_rva+c::thrown_slot,&target,8);
    }
    ~Image(){VirtualFree(data,0,MEM_RELEASE);}
    ff::detail::ProjectileFn Hook() const {ff::detail::ProjectileFn fn=nullptr;std::memcpy(&fn,data+c::thrown_slot_live_rva+c::thrown_slot,8);return fn;}
};
void* Request(ff::detail::ProjectileFn fn,std::uint16_t owner,std::uint16_t flags) {
    std::array<unsigned char,0x50> request{};
    std::memcpy(request.data()+c::req_owner,&owner,2);std::memcpy(request.data()+c::thrown_aux,&flags,2);
    const std::uint16_t equip=354;std::memcpy(request.data()+c::thrown_id,&equip,2);
    return fn(reinterpret_cast<void*>(0x1234),request.data());
}
int main() {
    ff::detail::ResetForTest();
    Image image;
    Check(!ff::ThrownArmed() && !ff::ThrownPatchLive(image.data),"default off");
    ff::detail::SetSlotOriginalForTest(2,Original);
    Check(ff::InstallThrown(image.data,nullptr)==nullptr && ff::ThrownPatchLive(image.data),"independent entry install without census");
    Check(Request(image.Hook(),1,1)==reinterpret_cast<void*>(0x1234) && forwarded==1 && preview_writes==1,"unarmed forwards unchanged");
    Check(!ff::ArmThrown(0) && !ff::ArmThrown(0x401) && !ff::ArmThrown(0xFFFF),"local and soldier owners cannot arm");
    Check(ff::ArmThrown(1) && ff::ThrownArmed(1),"partner id1 armed");
    Check(Request(image.Hook(),1,0)==nullptr && forwarded==1,"real partner throw dropped before original");
    Check(Request(image.Hook(),1,1)==nullptr && forwarded==1 && preview_writes==1,"partner preview dropped before shared write");
    ff::SetCensus(false);
    Check(Request(image.Hook(),1,1)==nullptr && forwarded==1,"filter remains live with census off and no drain window");
    for(auto owner:{0u,2u,0x201u,0x401u,0xFE01u,0xFFFFu})
        Check(Request(image.Hook(),static_cast<std::uint16_t>(owner),1)==reinterpret_cast<void*>(0x1234),"full u16 nonpartner owner forwarded");
    ff::Stats stats{};ff::ReadStats(image.data,stats);
    Check(stats.thrown_installed && stats.thrown_live && stats.thrown_armed && stats.thrown_owner==1,"filter status independent of bullet filter");
    Check(stats.thrown_dropped==3 && stats.thrown_preview_dropped==2 && stats.thrown_passed==7 && stats.thrown_unread==0,"drop/preview/pass counters");
    ff::SetCensus(true);std::uint32_t cursor=0,lost=0;ff::CensusEntry entry{};
    Check(Request(image.Hook(),1,1)==nullptr && ff::DrainCensus(cursor,&entry,1,lost)==1 && entry.kind==ff::CensusKind::thrown && entry.dropped && entry.owner==1,
          "census records thrown drop");
    // Later census installation recognizes the existing thrown swap.
    ff::Install(image.data,nullptr);ff::CensusTotals totals{};ff::ReadCensusTotals(totals);
    Check(totals.slot_installed[2] && totals.slot_live[2] && !totals.slot_refusal[2],"late census preserves entry filter");
    ff::detail::ResetForTest();ff::detail::SetSlotOriginalForTest(2,nullptr);
    Image bad;bad.data[c::thrown_request_rva]^=1;
    Check(ff::InstallThrown(bad.data,nullptr)!=nullptr && !ff::ThrownPatchLive(bad.data),"expected-byte mismatch refuses patch");
    ff::detail::ResetForTest();
    std::printf("partner_fire_support_tests: %u checks (synthetic slots; no gameplay evidence)\n",checks);
}
