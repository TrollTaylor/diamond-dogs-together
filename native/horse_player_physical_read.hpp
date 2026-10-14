#pragma once
// Pure census of the stock movement-matrix destination. No calls or writes.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include "carry_plugin_read.hpp"
#include "horse_player_physical_contract.hpp"

namespace mgscoop::horse_player_physical_read {
namespace cr=carry_plugin_read;
struct Body {
    std::uintptr_t cs=0,entry=0,entity=0,components=0,component=0,physicalInterface=0;
    std::uintptr_t transform=0,ownerTransform=0,physicalArray=0;
    std::uint32_t componentCount=0,tag=0,flags=0,componentType=0;
    float matrix[16]{};
};
struct Observation {cr::Observation carry{};Body body[2];};
template<class Reader> bool CodeGate(Reader& r,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch) noexcept {
    mismatch=0;if(!cr::Range(image,span))return false;unsigned char buffer[8192]{};
    for(const auto& site:horse_player_physical_contract::sites){
        if(site.rva>span||site.size>span-site.rva||site.size>sizeof(buffer)||!r(image+site.rva,buffer,site.size)){mismatch=site.rva;return false;}
        for(std::size_t j=0;j<site.size;++j)if(buffer[j]!=site.bytes[j]){mismatch=site.rva;return false;}
    }
    for(const auto& slot:horse_player_physical_contract::slots){std::uintptr_t value=0;
        if(slot.rva>span||sizeof(value)>span-slot.rva||!cr::Get(r,image+slot.rva,value)||value!=image+slot.target){mismatch=slot.rva;return false;}}
    return true;
}
template<class Reader> bool ReadOnce(Reader& r,const cr::Scope& s,Observation& out,const char** refusal=nullptr) noexcept {
    out={};if(!cr::ReadOnce(r,s,out.carry,refusal))return false;
    const auto fail=[&](const char* why) noexcept {if(refusal)*refusal=why;return false;};
    for(unsigned id=0;id<2;++id){const auto& c=out.carry.body[id];auto& b=out.body[id];
        std::uintptr_t vt=0,modelEntries=0,p=0;std::uint32_t count=0,base=0;
        if(!cr::Get(r,c.ps+0x238,b.cs)||!cr::Aligned(b.cs,8)||!cr::Get(r,b.cs,vt)||vt!=s.image+0x22e6070||
           !cr::Get(r,c.block+0x10,p)||p!=b.cs||!cr::Get(r,b.cs+8,count)||count!=1||
           !cr::Get(r,b.cs+0xc,base)||base!=id||!cr::Get(r,b.cs+0x10,modelEntries)||!cr::Aligned(modelEntries,16))return fail(id?"physical_model1":"physical_model0");
        b.entry=modelEntries;if(!cr::Range(b.entry,0xc0)||!cr::Get(r,b.entry+0x60,b.transform)||!cr::Aligned(b.transform,16)||
           !cr::Get(r,b.entry+0x70,b.entity)||!cr::Aligned(b.entity,8)||!cr::Range(b.entity,0x38))return fail(id?"physical_entity1":"physical_entity0");
        if(!cr::Get(r,b.entity+0x28,b.componentCount)||!b.componentCount||b.componentCount>128||
           !cr::Get(r,b.entity+0x30,b.components)||!cr::Aligned(b.components,8)||
           !cr::Range(b.components,std::size_t(b.componentCount)*8))return fail(id?"physical_list1":"physical_list0");
        for(std::uint32_t k=0;k<b.componentCount;++k){std::uintptr_t component=0;std::uint32_t tag=0;
            if(!cr::Get(r,b.components+k*8,component)||!cr::Aligned(component,8)||!cr::Range(component,0x14)||
               !cr::Get(r,component+0x10,tag))return fail("physical_component_unreadable");
            if(tag==0x7f1402f4u){if(b.component)return fail("physical_tag_duplicate");b.component=component;b.tag=tag;}
        }
        if(!b.component||!cr::Range(b.component,0xf0)||!cr::Get(r,b.component,vt))return fail(id?"physical_class1":"physical_class0");
        b.componentType=vt>=s.image&&vt-s.image<=UINT32_MAX?static_cast<std::uint32_t>(vt-s.image):UINT32_MAX;
        if(vt!=s.image+0x24c97f8)return fail(id?"physical_class1":"physical_class0");
        b.physicalInterface=b.component+0x80;
        if(!cr::Get(r,b.physicalInterface,vt)||vt!=s.image+0x24c9830||!cr::Get(r,b.component+0x68,b.ownerTransform)||
           b.ownerTransform!=b.transform)return fail(id?"physical_owner1":"physical_owner0");
        if(!cr::Get(r,c.ps+0x270,b.physicalArray)||!cr::Aligned(b.physicalArray,8)||!cr::Get(r,b.physicalArray,p)||
           p!=b.physicalInterface)return fail(id?"physical_slot1":"physical_slot0");
        if(!cr::Get(r,b.component+0xd0,b.flags)||!r(b.physicalInterface+0x10,b.matrix,sizeof(b.matrix)))return fail("physical_matrix_unreadable");
        for(float value:b.matrix)if(!std::isfinite(value)||std::fabs(value)>200000.0f)return fail("physical_matrix_invalid");
    }
    const auto& a=out.body[0];const auto& b=out.body[1];
    if(a.cs==b.cs||a.entry==b.entry||a.entity==b.entity||a.component==b.component||a.transform==b.transform||
       a.physicalArray==b.physicalArray||a.components==b.components)return fail("physical_body_alias");
    if(refusal)*refusal="physical_observed";return true;
}
inline bool SameBody(const Body& a,const Body& b) noexcept {
    if(a.cs!=b.cs||a.entry!=b.entry||a.entity!=b.entity||a.components!=b.components||a.component!=b.component||
       a.physicalInterface!=b.physicalInterface||a.transform!=b.transform||a.ownerTransform!=b.ownerTransform||a.physicalArray!=b.physicalArray||
       a.componentCount!=b.componentCount||a.tag!=b.tag||a.flags!=b.flags||a.componentType!=b.componentType)return false;
    for(unsigned k=0;k<16;++k)if(a.matrix[k]!=b.matrix[k])return false;return true;
}
template<class Reader> bool Observe(Reader& r,const cr::Scope& s,Observation& out,const char** refusal=nullptr) noexcept {
    out={};Observation a{},b{};
    if(!ReadOnce(r,s,a,refusal)){out=a;return false;}
    if(!ReadOnce(r,s,b,refusal)){out=b;return false;}
    if(!cr::Same(a.carry,b.carry)||!SameBody(a.body[0],b.body[0])||!SameBody(a.body[1],b.body[1])){
        if(refusal)*refusal="physical_changed";return false;}
    out=b;return true;
}
inline bool Line(const Observation& o,bool ready,const char* reason,char* out,std::size_t capacity) noexcept {
    if(!out||!capacity)return false;
    const int n=std::snprintf(out,capacity,"charPhysical;ready=%u;b0=%u,%x,%x;b1=%u,%x,%x;reason=%s",ready?1u:0u,
        o.body[0].componentCount,o.body[0].componentType,o.body[0].flags,
        o.body[1].componentCount,o.body[1].componentType,o.body[1].flags,reason?reason:"unknown");
    return n>=0&&static_cast<std::size_t>(n)<capacity;
}
}
