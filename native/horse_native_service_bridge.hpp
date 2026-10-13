#pragma once
// Readonly correction to the earlier mistaken model-service/receiver identity.
// E4C4D0 returns 22EAF70. Original B11E40/BF6870 then bridge through seven
// retained resource rows; no getter, QueryInterface, kind5 or native call here.
#include "carry_plugin_read.hpp"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "horse_native_service_bridge_contract.hpp"
namespace mgscoop::horse_native_service_bridge {
namespace cr=carry_plugin_read;
struct Observation {
    std::uintptr_t service=0,modelCS=0,primary=0,receiver=0,row=0,holder=0;
    std::uintptr_t rows[7]{},holders[7]{},payloads[7]{},receivers[7]{};
    std::uint32_t rowTypes[7]{},payloadTypes[7]{},receiverTypes[7]{},serviceType=0,nativeBase=0,nativeCount=0;
    unsigned selected=7,scanned=7;
};
template<class Reader> bool CodeGate(Reader& r,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch)noexcept {
    mismatch=0;unsigned char bytes[256]{};
    for(const auto& s:horse_native_service_bridge_contract::sites){if(s.rva>span||s.size>span-s.rva){mismatch=s.rva;return false;}
        for(std::size_t k=0;k<s.size;){const auto n=(s.size-k<sizeof(bytes))?s.size-k:sizeof(bytes);if(!r(image+s.rva+k,bytes,n)){mismatch=s.rva;return false;}
            for(std::size_t j=0;j<n;++j)if(bytes[j]!=s.bytes[k+j]){mismatch=s.rva;return false;}k+=n;}}
    for(const auto& s:horse_native_service_bridge_contract::slots){std::uintptr_t target=0;if(s.rva>span||sizeof(target)>span-s.rva||
        !cr::Get(r,image+s.rva,target)||target!=image+s.target){mismatch=s.rva;return false;}}
    return true;
}
template<class Reader> bool ReadOnce(Reader& r,std::uintptr_t image,std::uintptr_t family,std::uintptr_t service,
    std::uint32_t nativeIndex,std::uint32_t realizedCount,Observation& out,const char*& why) noexcept {
    out={};out.service=service;const auto fail=[&](const char* reason)noexcept{why=reason;return false;};
    std::uintptr_t vt=0,p=0;
    if(!cr::Aligned(service,8)||!cr::Range(service,0x108)||!cr::Get(r,service,vt)||vt!=image+0x22eaf70||
        !cr::Get(r,service+0xa8,p)||p!=family||!cr::Get(r,service+0x10,out.modelCS)||!cr::Aligned(out.modelCS,8)||
        !cr::Get(r,service+0x54,out.nativeBase)||!cr::Get(r,service+0x58,out.nativeCount)||!out.nativeCount||
        out.nativeCount>realizedCount||nativeIndex<out.nativeBase||nativeIndex-out.nativeBase>=out.nativeCount)return fail("horse_service_current");
    out.serviceType=0x22eaf70;unsigned matches=0;
    for(unsigned k=0;k<7;++k){
        out.scanned=k;
        if(!cr::Get(r,service+0xb0+k*8,out.rows[k]))return fail("horse_service_rows");
        if(!out.rows[k])continue;
        if(!cr::Aligned(out.rows[k],8)||!cr::Range(out.rows[k],0x70)||!cr::Get(r,out.rows[k],vt))return fail("horse_service_resource_row");
        out.rowTypes[k]=vt>=image&&vt-image<=UINT32_MAX?static_cast<std::uint32_t>(vt-image):UINT32_MAX;
        if(vt!=image+0x2306628||
            !cr::Get(r,out.rows[k]+0x30,out.holders[k])||!cr::Aligned(out.holders[k],8)||!cr::Range(out.holders[k],0x18)||
            !cr::Get(r,out.holders[k]+0x10,out.payloads[k])||!cr::Aligned(out.payloads[k],8)||
            !cr::Get(r,out.payloads[k],vt)||!cr::Get(r,out.rows[k]+0x38,out.receivers[k]))return fail("horse_service_resource_row");
        out.payloadTypes[k]=vt>=image&&vt-image<=UINT32_MAX?static_cast<std::uint32_t>(vt-image):UINT32_MAX;
        if(out.receivers[k]&&cr::Aligned(out.receivers[k],8)&&cr::Get(r,out.receivers[k],p))
            out.receiverTypes[k]=p>=image&&p-image<=UINT32_MAX?static_cast<std::uint32_t>(p-image):UINT32_MAX;
        if(out.payloadTypes[k]!=0x2363c48&&out.payloadTypes[k]!=0x2363b60)continue;
        const auto primary=out.payloads[k],receiver=out.receivers[k];
        if(receiver!=primary+0x20||!cr::Range(primary,0xc8)||
            !((out.payloadTypes[k]==0x2363c48&&out.receiverTypes[k]==0x2363c80)||
              (out.payloadTypes[k]==0x2363b60&&out.receiverTypes[k]==0x2363b90))||
            !cr::Get(r,receiver+8,p)||p!=out.rows[k]||!cr::Get(r,primary+0x70,p)||p!=family||
            !cr::Get(r,primary+0x80,p)||p!=out.modelCS)return fail("horse_service_motion_owner");
        std::uint32_t base=0,count=0;std::uintptr_t descriptor=0;
        if(!cr::Get(r,primary+0x98,base)||base!=out.nativeBase||!cr::Get(r,primary+0x78,descriptor)||!cr::Aligned(descriptor,8)||
            !cr::Range(descriptor,0x60)||!cr::Get(r,descriptor+0x54,base)||base!=out.nativeBase||
            !cr::Get(r,descriptor+0x58,count)||count!=out.nativeCount||!cr::Get(r,descriptor+0x10,p)||p!=out.modelCS)return fail("horse_service_motion_descriptor");
        ++matches;out.selected=k;out.primary=primary;out.receiver=receiver;out.row=out.rows[k];out.holder=out.holders[k];
    }
    if(matches!=1)return fail(matches?"horse_service_motion_duplicate":"horse_service_motion_missing");
    why="horse_service_motion_observed";return true;
}
inline bool Same(const Observation& a,const Observation& b)noexcept {
    return a.service==b.service&&a.modelCS==b.modelCS&&a.primary==b.primary&&a.receiver==b.receiver&&a.row==b.row&&a.holder==b.holder&&
        a.serviceType==b.serviceType&&a.nativeBase==b.nativeBase&&a.nativeCount==b.nativeCount&&a.selected==b.selected&&a.scanned==b.scanned&&
        std::memcmp(a.rows,b.rows,sizeof(a.rows))==0&&std::memcmp(a.holders,b.holders,sizeof(a.holders))==0&&
        std::memcmp(a.payloads,b.payloads,sizeof(a.payloads))==0&&std::memcmp(a.receivers,b.receivers,sizeof(a.receivers))==0&&
        std::memcmp(a.rowTypes,b.rowTypes,sizeof(a.rowTypes))==0&&std::memcmp(a.payloadTypes,b.payloadTypes,sizeof(a.payloadTypes))==0&&std::memcmp(a.receiverTypes,b.receiverTypes,sizeof(a.receiverTypes))==0;
}
template<class Reader>bool Observe(Reader& r,std::uintptr_t image,std::uintptr_t family,std::uintptr_t service,
    std::uint32_t nativeIndex,std::uint32_t realizedCount,Observation& out,const char*& why)noexcept {
    Observation a{},b{};out={};if(!ReadOnce(r,image,family,service,nativeIndex,realizedCount,a,why)){out=a;return false;}
    if(!ReadOnce(r,image,family,service,nativeIndex,realizedCount,b,why)){out=b;return false;}
    if(!Same(a,b)){why="horse_service_motion_changed";return false;}out=b;return true;
}
inline bool Line(const Observation& a,bool ready,const char* why,char* text,std::size_t capacity)noexcept {
    const int n=std::snprintf(text,capacity,"horseSvc;ok=%u;type=%x;row=%u,%u,%x;native=%x,%x;idx=%u,%u;why=%s",ready?1u:0u,
        a.serviceType,a.selected,a.scanned,a.scanned<7?a.rowTypes[a.scanned]:0,a.selected<7?a.payloadTypes[a.selected]:0,a.selected<7?a.receiverTypes[a.selected]:0,
        a.nativeBase,a.nativeCount,why?why:"unknown");return n>=0&&static_cast<std::size_t>(n)<capacity;
}
inline bool CandidatesLine(const Observation& a,char* text,std::size_t capacity)noexcept {
    const int n=std::snprintf(text,capacity,"horseSvcRows;p=%x,%x,%x,%x,%x,%x,%x;r=%x,%x,%x,%x,%x,%x,%x",
        a.payloadTypes[0],a.payloadTypes[1],a.payloadTypes[2],a.payloadTypes[3],a.payloadTypes[4],a.payloadTypes[5],a.payloadTypes[6],
        a.receiverTypes[0],a.receiverTypes[1],a.receiverTypes[2],a.receiverTypes[3],a.receiverTypes[4],a.receiverTypes[5],a.receiverTypes[6]);
    return n>=0&&static_cast<std::size_t>(n)<capacity;
}
}
