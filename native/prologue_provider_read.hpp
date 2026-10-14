#pragma once
// A census of the global sequence-work manager and its retained providers.
// No provider getter/slot, native method, buffer write or mode switch is called.
// A successful observation supplies no per-player or movement authority.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include "prologue_provider_contract.hpp"
namespace mgscoop::prologue_provider_read {
namespace contract=prologue_provider_contract;
struct Scope {
    std::uintptr_t image=0;
    std::size_t imageSpan=0;
    bool exactGate=false,currentPair=false,fresh=false;
};
struct Provider {
    std::uintptr_t pointer=0,vtable=0,slot0=0,slot8=0;
    std::uint32_t vtableRva=0,slot0Rva=0,slot8Rva=0;
};
struct Observation {
    std::uintptr_t manager=0,vtable=0,selected=0;
    // These are aliases: 2C1BF20+158 == 2C1C078, not separate mode fields.
    std::uint32_t selector=0;
    Provider provider[3]{};
};
inline bool Range(std::uintptr_t p,std::size_t n) noexcept {
    return p&&n&&p<=std::numeric_limits<std::uintptr_t>::max()-n;
}
inline bool Aligned(std::uintptr_t p) noexcept {return p&&!(p&7u);}
template<class Reader,class T>bool Get(Reader& r,std::uintptr_t p,T& value)noexcept {
    return Range(p,sizeof(T))&&r(p,&value,sizeof(T));
}
inline bool ImageRange(std::uintptr_t p,std::uintptr_t image,std::size_t span,std::size_t n)noexcept {
    return p>=image&&p-image<=span&&n<=span-(p-image);
}
inline bool ReadonlyTable(std::uintptr_t rva,std::size_t n)noexcept {
    return rva>=contract::tableBegin&&rva<=contract::tableEnd&&n<=contract::tableEnd-rva;
}
inline bool Executable(std::uintptr_t rva)noexcept {
    for(const auto& s:contract::execute)if(rva>=s.begin&&rva<s.end)return true;
    return false;
}
template<class Reader>bool CodeGate(Reader& r,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch)noexcept {
    mismatch=0;if(!Range(image,span)||span!=contract::imageSpan)return false;
    unsigned char bytes[256]{};
    for(const auto& s:contract::sites){
        if(s.rva>span||s.size>span-s.rva){mismatch=s.rva;return false;}
        for(std::size_t k=0;k<s.size;){const auto n=(s.size-k<sizeof(bytes))?s.size-k:sizeof(bytes);
            if(!r(image+s.rva+k,bytes,n)){mismatch=s.rva;return false;}
            for(std::size_t j=0;j<n;++j)if(bytes[j]!=s.bytes[k+j]){mismatch=s.rva;return false;}k+=n;}
    }
    for(const auto& s:contract::slots){std::uintptr_t target=0;
        if(s.rva>span||sizeof(target)>span-s.rva||!Get(r,image+s.rva,target)||target!=image+s.target){mismatch=s.rva;return false;}}
    return true;
}
template<class Reader>bool ReadOnce(Reader& r,const Scope& s,Observation& out,const char*& why)noexcept {
    out={};const auto fail=[&](const char* v)noexcept{why=v;return false;};
    if(!s.exactGate)return fail("seq_gate");
    if(!s.currentPair||!s.fresh)return fail("seq_scope");
    if(!Range(s.image,s.imageSpan)||s.imageSpan!=contract::imageSpan)return fail("seq_image");
    out.manager=s.image+contract::manager;
    if(!Get(r,out.manager,out.vtable)||out.vtable!=s.image+contract::managerVtable)return fail("seq_manager");
    if(!Get(r,s.image+contract::selector,out.selector))return fail("seq_selector");
    for(unsigned k=0;k<3;++k){auto& p=out.provider[k];
        if(!Get(r,out.manager+0xcc0+k*8,p.pointer))return fail("seq_provider");
        if(!p.pointer)continue;
        if(!Aligned(p.pointer)||!Get(r,p.pointer,p.vtable)||!Aligned(p.vtable)||
            !ImageRange(p.vtable,s.image,s.imageSpan,16)||!ReadonlyTable(p.vtable-s.image,16))return fail("seq_table");
        p.vtableRva=static_cast<std::uint32_t>(p.vtable-s.image);
        if(!Get(r,p.vtable,p.slot0)||!Get(r,p.vtable+8,p.slot8)||
            !ImageRange(p.slot0,s.image,s.imageSpan,1)||!ImageRange(p.slot8,s.image,s.imageSpan,1)||
            !Executable(p.slot0-s.image)||!Executable(p.slot8-s.image))return fail("seq_slots");
        p.slot0Rva=static_cast<std::uint32_t>(p.slot0-s.image);p.slot8Rva=static_cast<std::uint32_t>(p.slot8-s.image);
    }
    if(out.selector<3)out.selected=out.provider[out.selector].pointer;
    why=out.selected?"seq_observed":"seq_no_selected";return true;
}
inline bool Same(const Observation& a,const Observation& b)noexcept {
    if(a.manager!=b.manager||a.vtable!=b.vtable||a.selector!=b.selector||a.selected!=b.selected)return false;
    for(unsigned k=0;k<3;++k){const auto& p=a.provider[k];const auto& q=b.provider[k];
        if(p.pointer!=q.pointer||p.vtable!=q.vtable||p.slot0!=q.slot0||p.slot8!=q.slot8||
            p.vtableRva!=q.vtableRva||p.slot0Rva!=q.slot0Rva||p.slot8Rva!=q.slot8Rva)return false;}
    return true;
}
template<class Reader>bool Observe(Reader& r,const Scope& s,Observation& out,const char*& why)noexcept {
    Observation a{},b{};out={};
    if(!ReadOnce(r,s,a,why)){out=a;return false;}
    if(!ReadOnce(r,s,b,why)){out=b;return false;}
    if(!Same(a,b)){why="seq_changed";return false;}out=b;return true;
}
inline bool Line(const Observation& a,bool ready,const char* why,char* text,std::size_t capacity)noexcept {
    const int n=std::snprintf(text,capacity,
        "seqProv;ok=%u;s=%x;p0=%x,%x,%x;p1=%x,%x,%x;p2=%x,%x,%x;why=%s",ready?1u:0u,a.selector,
        a.provider[0].vtableRva,a.provider[0].slot0Rva,a.provider[0].slot8Rva,
        a.provider[1].vtableRva,a.provider[1].slot0Rva,a.provider[1].slot8Rva,
        a.provider[2].vtableRva,a.provider[2].slot0Rva,a.provider[2].slot8Rva,why?why:"unknown");
    return n>=0&&static_cast<std::size_t>(n)<capacity&&n<=160;
}
}
