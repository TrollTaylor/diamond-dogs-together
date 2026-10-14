#pragma once
// Pure current Horse cargo/controller census. No native methods or writes.
#include <cstdio>
#include <cmath>
#include "carry_plugin_read.hpp"
#include "mgscoop/buddy_capacity_census.hpp"
#include "horse_cargo_native_v2_contract.hpp"
#include "horse_native_service_bridge.hpp"
namespace mgscoop::horse_cargo_native_read {
namespace cr=carry_plugin_read;
using Census=buddy_capacity_census::Census;
struct Observation {
    std::uintptr_t family=0,kind=0,realized=0,controllerArray=0,service=0,primary=0,receiver=0,descriptor=0,records=0,record=0,sdRecord=0,unit=0,cs=0;
    horse_native_service_bridge::Observation bridge{};
    std::uint32_t groupStride=0,nativeIndex=UINT32_MAX,nativeBase=0,nativeCount=0,primaryType=0,interfaceType=0;
    std::uint16_t gameId=0xffff,logicalIndex=0xffff,cargo=0xffff,sdCargo=0xffff,driver=0xffff;
    std::uint8_t occupied=0,anchorSkip=0,driverOccupied=0,phase=0;
    float matrix[16]{};bool matrixFinite=false,anchorFresh=false;
};
template<class Reader> bool CodeGate(Reader& r,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch) noexcept {
    mismatch=0;if(!cr::Range(image,span))return false;unsigned char bytes[256]{};
    for(const auto& site:horse_cargo_native_v2_contract::sites){
        if(site.rva>span||site.size>span-site.rva){mismatch=site.rva;return false;}
        for(std::size_t k=0;k<site.size;){const auto n=site.size-k<sizeof(bytes)?site.size-k:sizeof(bytes);
            if(!r(image+site.rva+k,bytes,n)){mismatch=site.rva;return false;}
            for(std::size_t j=0;j<n;++j)if(bytes[j]!=site.bytes[k+j]){mismatch=site.rva;return false;}k+=n;}
    }
    for(const auto& slot:horse_cargo_native_v2_contract::slots){std::uintptr_t value=0;
        if(slot.rva>span||sizeof(value)>span-slot.rva||!cr::Get(r,image+slot.rva,value)||value!=image+slot.target){mismatch=slot.rva;return false;}}
    return true;
}
// Root must obtain Census from freshly admitted current Horse owner and hold
// its mission/epoch/body/model identities across BOTH calls. This reader is
// not an admission substitute, and never grants native invocation authority.
template<class Reader> bool ReadOnce(Reader& r,std::uintptr_t image,const Census& census,std::uint16_t gameId,
    Observation& out,const char*& why) noexcept {
    out={};out.gameId=gameId;out.family=census.receiver;out.kind=census.kind;out.realized=census.realized_indices;
    const auto fail=[&](const char* reason) noexcept {why=reason;return false;};
    std::uintptr_t vt=0,p=0;std::uint32_t n=0;std::uint16_t category=0;
    if(!image||census.class_index!=13||census.count<1||census.count>2||census.realized_count<1||census.realized_count>census.count||
       (gameId>>9)!=13||!cr::Aligned(out.family,8)||out.family<0x20||!cr::Range(out.family-0x20,0x200)||
       !cr::Get(r,out.family,vt)||(vt!=image+0x23624e0&&vt!=image+0x2362650))return fail("horse_family");
    const auto familyType=vt;
    if(!cr::Get(r,out.family-0x20,vt)||(familyType==image+0x23624e0?vt!=image+0x23624a8:vt!=image+0x2362620)||
       !cr::Get(r,out.family+8,category)||category!=13||!cr::Get(r,out.family+0xc,n)||n!=census.count||
       !cr::Get(r,out.family+0x18,p)||p!=out.kind||!cr::Get(r,out.family+0x68,p)||p!=census.component||
       !cr::Get(r,census.component,vt)||vt!=image+0x2336cf0||!cr::Get(r,census.component-0x20,vt)||vt!=image+0x2367b80||
       !cr::Get(r,census.component+0x10,n)||n!=census.count||!cr::Get(r,census.component+8,p)||p!=census.records||
       !cr::Get(r,out.kind+0x44,n)||n!=census.realized_count||!cr::Get(r,out.kind+0x28,p)||p!=out.realized||
       !cr::Aligned(out.realized,2)||!cr::Range(out.realized,std::size_t(census.realized_count)*2))return fail("horse_family_changed");
    out.logicalIndex=static_cast<std::uint16_t>(gameId&0x1ffu);
    if(out.logicalIndex>=census.count||!census.actors[out.logicalIndex].realized||census.actors[out.logicalIndex].game_id!=gameId)
        return fail("horse_unrealized");
    unsigned seen=0,matches=0;
    for(unsigned k=0;k<census.realized_count;++k){std::uint16_t actor=0xffff;
        if(!cr::Get(r,out.realized+k*2,actor)||actor>=census.count||(seen&(1u<<actor)))return fail("horse_realized_map");
        seen|=1u<<actor;if(actor==out.logicalIndex){out.nativeIndex=k;++matches;}
    }
    if(matches!=1||seen!=census.realized_mask)return fail("horse_native_index");
    // Replicate E4C4D0's actual realized-ordinal selector, never low-bit id.
    if(!cr::Get(r,out.family+0xf0,out.groupStride)||!out.groupStride||out.groupStride>census.count||
       !cr::Get(r,out.family+0x110,out.controllerArray)||!cr::Aligned(out.controllerArray,8)||
       !cr::Range(out.controllerArray,((census.realized_count+out.groupStride-1)/out.groupStride)*8u)||
       !cr::Get(r,out.controllerArray+(out.nativeIndex/out.groupStride)*8,out.service)||!cr::Aligned(out.service,8))return fail("horse_controller_selector");
    if(!horse_native_service_bridge::ReadOnce(r,image,out.family,out.service,out.nativeIndex,census.realized_count,out.bridge,why))return false;
    out.receiver=out.bridge.receiver;out.primary=out.bridge.primary;
    if(!cr::Get(r,out.receiver,vt))return fail("horse_native_interface");
    out.interfaceType=vt>=image&&vt-image<=UINT32_MAX?static_cast<std::uint32_t>(vt-image):UINT32_MAX;
    out.primary=out.receiver-0x20;
    if(!cr::Range(out.primary,0xc8)||!cr::Get(r,out.primary,vt))return fail("horse_native_type");
    out.primaryType=vt>=image&&vt-image<=UINT32_MAX?static_cast<std::uint32_t>(vt-image):UINT32_MAX;
    if(!((out.primaryType==0x2363c48&&out.interfaceType==0x2363c80)||(out.primaryType==0x2363b60&&out.interfaceType==0x2363b90)))
        return fail("horse_native_type");
    if(!cr::Get(r,out.primary+0x70,p)||p!=out.family||!cr::Get(r,out.primary+0x78,out.descriptor)||!cr::Aligned(out.descriptor,8)||
       !cr::Range(out.descriptor,0x60)||!cr::Get(r,out.primary+0x98,out.nativeBase)||!cr::Get(r,out.descriptor+0x54,n)||n!=out.nativeBase||
       !cr::Get(r,out.descriptor+0x58,out.nativeCount)||!out.nativeCount||out.nativeCount>census.realized_count||
       out.nativeBase>=census.realized_count||out.nativeIndex<out.nativeBase||out.nativeIndex-out.nativeBase>=out.nativeCount||
       !cr::Get(r,out.descriptor+0x10,out.cs)||!cr::Aligned(out.cs,8)||!cr::Get(r,out.primary+0x80,p)||p!=out.cs||
       !cr::Get(r,out.primary+0xc0,out.records)||!cr::Aligned(out.records,16)||!cr::Range(out.records,std::size_t(out.nativeCount)*0x440))
        return fail("horse_native_owner");
    out.record=out.records+std::size_t(out.nativeIndex-out.nativeBase)*0x440;
    out.sdRecord=census.records+std::size_t(out.logicalIndex)*0x2f0;out.unit=out.record+0x70;
    if(census.actors[out.logicalIndex].record!=out.sdRecord||!cr::Get(r,out.record,vt)||vt!=image+0x2337a88||
       !cr::Get(r,out.record+0x268,p)||p!=out.sdRecord||!cr::Get(r,out.record+0x284,n)||n!=out.logicalIndex||
       !cr::Get(r,out.sdRecord,p)||p!=out.unit||census.actors[out.logicalIndex].ride_link!=out.unit||
       !cr::Get(r,out.unit,vt)||vt!=image+0x2363af8)return fail("horse_unit_link");
    if(!cr::Get(r,out.unit+0x164,out.cargo)||!cr::Get(r,out.sdRecord+0x10,out.sdCargo)||
       !cr::Get(r,out.sdRecord+0x2de,out.occupied)||!cr::Get(r,out.sdRecord+0x2ec,out.anchorSkip)||
       !cr::Get(r,out.sdRecord+0xe,out.driver)||!cr::Get(r,out.sdRecord+0x2dd,out.driverOccupied)||
       !cr::Get(r,out.record+0x260,out.phase)||!r(out.unit+0x90,out.matrix,sizeof(out.matrix)))return fail("horse_cargo_unreadable");
    if(out.occupied>1||out.driverOccupied>1||out.anchorSkip>1||out.cargo!=out.sdCargo||
       (out.occupied?(out.cargo==0xffff):(out.cargo!=0xffff)))return fail("horse_cargo_incoherent");
    out.matrixFinite=true;for(float value:out.matrix)if(!std::isfinite(value)||std::fabs(value)>200000.0f)out.matrixFinite=false;
    if(!out.matrixFinite)return fail("horse_cargo_matrix");
    // Original E54440 post-admission production is a separate future receipt.
    out.anchorFresh=false;why="horse_cargo_observed";return true;
}
inline bool Same(const Observation& a,const Observation& b) noexcept {
    if(a.family!=b.family||a.kind!=b.kind||a.realized!=b.realized||a.controllerArray!=b.controllerArray||a.service!=b.service||!horse_native_service_bridge::Same(a.bridge,b.bridge)||a.primary!=b.primary||a.receiver!=b.receiver||
       a.descriptor!=b.descriptor||a.records!=b.records||a.record!=b.record||a.sdRecord!=b.sdRecord||a.unit!=b.unit||a.cs!=b.cs||
       a.groupStride!=b.groupStride||a.nativeIndex!=b.nativeIndex||a.nativeBase!=b.nativeBase||a.nativeCount!=b.nativeCount||
       a.primaryType!=b.primaryType||a.interfaceType!=b.interfaceType||a.gameId!=b.gameId||a.logicalIndex!=b.logicalIndex||a.cargo!=b.cargo||
       a.sdCargo!=b.sdCargo||a.driver!=b.driver||a.occupied!=b.occupied||a.anchorSkip!=b.anchorSkip||a.driverOccupied!=b.driverOccupied||
       a.phase!=b.phase||a.matrixFinite!=b.matrixFinite||a.anchorFresh!=b.anchorFresh)return false;
    for(unsigned k=0;k<16;++k)if(a.matrix[k]!=b.matrix[k])return false;return true;
}
template<class Reader> bool Observe(Reader& r,std::uintptr_t image,const Census& current,std::uint16_t id,Observation& out,const char*& why) noexcept {
    out={};Observation a{},b{};
    if(!ReadOnce(r,image,current,id,a,why)){out=a;return false;}
    if(!ReadOnce(r,image,current,id,b,why)){out=b;return false;}
    if(!Same(a,b)){why="horse_cargo_changed";return false;}out=b;return true;
}
// Source closure includes current service/topology and both actor unit links,
// and deliberately excludes continuously changing positions. A plain Observe
// cannot replace Root's current scene/birth/parts/owner admission.
inline bool SameSource(const Census& a,const Census& b) noexcept {
    if(a.outer!=b.outer||a.receiver!=b.receiver||a.component!=b.component||a.records!=b.records||a.kind!=b.kind||a.positions!=b.positions||
       a.realized_indices!=b.realized_indices||a.controller!=b.controller||a.pack_hash!=b.pack_hash||a.class_index!=b.class_index||
       a.declared_total!=b.declared_total||a.declared_realized!=b.declared_realized||a.count!=b.count||
       a.realized_count!=b.realized_count||a.realized_mask!=b.realized_mask)return false;
    for(unsigned k=0;k<2;++k)if(a.actors[k].record!=b.actors[k].record||a.actors[k].ride_link!=b.actors[k].ride_link||
        a.actors[k].game_id!=b.actors[k].game_id||a.actors[k].realized!=b.actors[k].realized||a.actors[k].rider!=b.actors[k].rider||
        a.actors[k].pending_rider!=b.actors[k].pending_rider||a.actors[k].occupied!=b.actors[k].occupied)return false;
    return true;
}
// source() must obtain the ORIGINAL pure ReadPlayPose/ReadCurrentCensus under
// Root's own current scene/parts/owner gates. It performs no engine call.
template<class Reader,class Source> bool ObserveCurrent(Reader& r,std::uintptr_t image,Source&& source,std::uint16_t id,
    Observation& out,const char*& why) noexcept {
    Census before{},after{};out={};out.gameId=id;
    if(!source(before,why))return false;
    if(!Observe(r,image,before,id,out,why))return false;
    if(!source(after,why)||!SameSource(before,after)){why="horse_source_changed";return false;}
    Observation final{};if(!ReadOnce(r,image,after,id,final,why))return false;
    if(!Same(out,final)){why="horse_native_changed_after_source";return false;}
    out=final;why="horse_cargo_observed";return true;
}
inline bool Line(const Observation& o,bool ready,const char* reason,char* out,std::size_t capacity) noexcept {
    if(!out||!capacity)return false;
    const int n=std::snprintf(out,capacity,"horseCargo;ready=%u;id=%u;type=%x,%x;idx=%u,%u,%u;cargo=%u,%u,%u;skip=%u;phase=%u;mat=%u;fresh=%u;why=%s",
      ready?1u:0u,unsigned(o.gameId),o.primaryType,o.interfaceType,o.nativeIndex,o.nativeBase,o.nativeCount,unsigned(o.cargo),unsigned(o.sdCargo),
      unsigned(o.occupied),unsigned(o.anchorSkip),unsigned(o.phase),o.matrixFinite?1u:0u,o.anchorFresh?1u:0u,reason?reason:"unknown");
    return n>=0&&static_cast<std::size_t>(n)<capacity;
}
}
