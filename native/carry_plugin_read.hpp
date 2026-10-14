#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include "carry_plugin_contract.hpp"
namespace mgscoop::carry_plugin_read {
// Pure reader only. This module calls no engine method and writes no game
// memory. Its target is the shoulder-carry plugin, not the gun target.
struct Scope {
    bool exactGate=false,admitted=false,ready=false,armed=false,batch=false;
    std::uintptr_t image=0,player=0,data=0,state=0;
    std::uint32_t mission=0,ownedMission=0,generation=0,ownedGeneration=0,thread=0,ownerThread=0;
    std::uint16_t epoch=0,ownedEpoch=0;
    std::uint32_t sequenceBefore=0,sequenceAfter=0;
    std::uint64_t now=0,poll=0;
};
inline const char* ScopeRefusal(const Scope& s) noexcept {
    if(!s.exactGate)return "plugin_gate";
    if(!s.admitted||!s.ready||!s.armed)return "scope";
    if(!s.thread||s.thread!=s.ownerThread)return "owner_thread";
    if((s.mission!=30010&&s.mission!=30020&&s.mission!=30050)||!s.epoch||!s.generation||s.mission!=s.ownedMission||s.epoch!=s.ownedEpoch||s.generation!=s.ownedGeneration)return "scene";
    if(!s.poll||s.now<s.poll||s.now-s.poll>750)return "scope_expired";
    if(s.batch)return "batch";
    if((s.sequenceBefore&1u)||s.sequenceBefore!=s.sequenceAfter)return "input_window";
    if(!s.image||!s.player||!s.data||!s.state)return "identity";
    return nullptr;
}
struct Body {
    std::uintptr_t block=0,ps=0,node=0,primary=0,records=0,record=0,psRecord=0;
    std::uint32_t id=0,psFlags=0;
    std::uint16_t target=0xffff,previous=0xffff,carrier=0xffff;
    std::uint16_t flags[4]{};
    std::uint8_t state[5]{};
    bool nodeActive=false;
};
struct Observation {
    std::uintptr_t player=0,data=0,state=0,ids=0,pool=0,parts=0,blocks=0,blockTable=0,selector=0;
    Body body[2];
    std::uintptr_t selectorOwner=0;
    std::uint16_t selectorTarget=0xffff;
    std::uint32_t playerFlags=0,cachedKind=0,currentKind=0,cachedFlags=0,currentFlags=0;
};
struct BodyReply {
    std::uint16_t target=0xffff,previous=0xffff,carrier=0xffff,flags[4]{};
    std::uint8_t state[5]{};
    std::uint32_t psFlags=0;
    bool nodeActive=false;
};
struct Reply {
    bool ready=false,selectorOwnerMatches=false,selectorAccepts=false;
    BodyReply body[2];
    std::uint16_t selectorTarget=0xffff;
    std::uint32_t cachedKind=0,currentKind=0,cachedFlags=0,currentFlags=0;
    const char* reason="disabled";
};
inline bool Aligned(std::uintptr_t p,unsigned n) noexcept {return p&&!(p&(n-1));}
inline bool Range(std::uintptr_t p,std::size_t n) noexcept {return p&&n&&p<=std::numeric_limits<std::uintptr_t>::max()-n;}
template<class Reader,class T> bool Get(Reader& r,std::uintptr_t p,T& out) noexcept {
    return Range(p,sizeof(T))&&r(p,&out,sizeof(T));
}
// Exact-gate helper uses only bounded reader calls. Invoke once after the
// coordinator's main exact-PE gate and before setting Scope.exactGate.
template<class Reader> bool CodeGate(Reader& r,std::uintptr_t base,std::size_t span,std::uintptr_t& mismatch) noexcept {
    mismatch=0;if(!Range(base,span))return false;
    unsigned char buffer[8192]{};
    for(const auto& site:carry_plugin_contract::sites){
        if(site.rva>span||site.size>span-site.rva||site.size>sizeof(buffer)||!r(base+site.rva,buffer,site.size)){mismatch=site.rva;return false;}
        for(std::size_t j=0;j<site.size;++j)if(buffer[j]!=site.bytes[j]){mismatch=site.rva;return false;}
    }
    for(const auto& slot:carry_plugin_contract::slots){
        const auto off=slot.vtable_rva+slot.offset;std::uintptr_t value=0;
        if(off>span||sizeof(value)>span-off||!Get(r,base+off,value)||value!=base+slot.target_rva){mismatch=off;return false;}
    }return true;
}
inline bool Same(const Observation& a,const Observation& b) noexcept {
    if(a.player!=b.player||a.data!=b.data||a.state!=b.state||a.ids!=b.ids||a.pool!=b.pool||a.parts!=b.parts||a.blocks!=b.blocks||a.blockTable!=b.blockTable||a.selector!=b.selector||a.selectorOwner!=b.selectorOwner||a.selectorTarget!=b.selectorTarget||a.playerFlags!=b.playerFlags||a.cachedKind!=b.cachedKind||a.currentKind!=b.currentKind||a.cachedFlags!=b.cachedFlags||a.currentFlags!=b.currentFlags)return false;
    for(unsigned k=0;k<2;++k){const auto& x=a.body[k];const auto& y=b.body[k];
        if(x.block!=y.block||x.ps!=y.ps||x.node!=y.node||x.primary!=y.primary||x.records!=y.records||x.record!=y.record||x.psRecord!=y.psRecord||x.id!=y.id||x.psFlags!=y.psFlags||x.target!=y.target||x.previous!=y.previous||x.carrier!=y.carrier||x.nodeActive!=y.nodeActive)return false;
        for(unsigned j=0;j<4;++j)if(x.flags[j]!=y.flags[j])return false;
        for(unsigned j=0;j<5;++j)if(x.state[j]!=y.state[j])return false;
    }return true;
}
// Scope comes only from the coordinator's current, exact-gated paired offline
// ReadState and fresh downed snapshot; never from Lua arguments. The caller
// must sample WindowSeq/InBatch again after this read and refuse changes.
template<class Reader> bool ReadOnce(Reader& r,const Scope& s,Observation& out,const char** refusal=nullptr) noexcept {
    const auto stage=[&](const char* value) noexcept {if(refusal)*refusal=value;};stage("scope");
    out={};if(ScopeRefusal(s)||!Range(s.image,0x3000000)||!Range(s.player,0x388)||!Range(s.data,0x1f0)||!Range(s.state,0x48))return false;
    stage("player");out.player=s.player;out.data=s.data;out.state=s.state;
    std::uintptr_t vt=0,q=0,st=0,sync=0;std::uint32_t count=0,realized=0,local=0;
    if(!Get(r,s.player,vt)||vt!=s.image+0x2295490||!Get(r,s.player+0x60,q)||q!=s.data||!Get(r,q,vt)||vt!=s.image+0x23ad970||!Get(r,q+0x1e8,st)||st!=s.state||!Get(r,st+0x40,count)||count!=2||!Get(r,s.player+0xa8,sync)||sync||!Get(r,s.player+0x204,out.playerFlags)||!(out.playerFlags&1u)||(out.playerFlags&0x100u)||!Get(r,s.player+0x208,count)||count!=2||!Get(r,s.player+0x20c,realized)||realized!=2||!Get(r,s.player+0x214,local)||local)return false;
    stage("arrays");if(!Get(r,s.player+0x70,out.ids)||!Aligned(out.ids,2)||!Get(r,s.player+0x78,out.pool)||!out.pool||!Get(r,s.player+0x90,out.parts)||!out.parts||!Get(r,s.player+0x378,out.blocks)||!Aligned(out.blocks,8)||!Get(r,s.player+0x50,out.blockTable)||!Aligned(out.blockTable,8))return false;
    if(!Range(out.ids,4)||!Range(out.pool,2)||!Range(out.parts,2)||!Range(out.blocks,16*0x6a8)||!Range(out.blockTable,16*8))return false;
    std::uint8_t pools[2]{};
    for(unsigned k=0;k<2;++k){stage(k?"instance1":"instance0");auto& b=out.body[k];std::uint16_t id=0;std::uint8_t part=0;
        if(!Get(r,out.ids+k*2,id)||id!=k||!Get(r,out.pool+k,pools[k])||pools[k]>=16||!Get(r,out.parts+k,part)||part!=3)return false;
        b.id=id;b.block=out.blocks+std::uintptr_t(pools[k])*0x6a8;
        stage(k?"block_ps1":"block_ps0");std::uintptr_t block2=0,owner=0;std::uint32_t base=0,one=0;
        if(!Get(r,out.blockTable+pools[k]*8,block2)||block2!=b.block||!Get(r,b.block,vt)||vt!=s.image+0x2295210||!Get(r,b.block+0x138,owner)||owner!=s.player||!Get(r,b.block+8,one)||one!=1||!Get(r,b.block+0xc,base)||base!=id||!Get(r,b.block+0x78,b.ps)||!Aligned(b.ps,8)||!Range(b.ps,0x410)||!Get(r,b.ps,vt)||(vt!=s.image+0x23b09f0&&vt!=s.image+0x23b0c70)||!Get(r,b.ps+0x408,owner)||owner!=b.block||!Get(r,b.ps+8,base)||base!=id)return false;
        stage(k?"carry_node1":"carry_node0");if(!Get(r,b.block+0x320,b.node)||!Aligned(b.node,8)||!Range(b.node,0x40)||!Get(r,b.node,vt)||vt!=s.image+0x2306628||!Get(r,b.node+0x20,one)||one!=1||!Get(r,b.node+0x24,base)||base!=id||!Get(r,b.node+0x2c,one)||one!=0x32)return false;
        stage(k?"carry_primary1":"carry_primary0");if(!Get(r,b.block+0xb8,b.primary)||!Aligned(b.primary,8)||!Range(b.primary,0x78)||!Get(r,b.primary,vt)||vt!=s.image+0x23b9ac0||!Get(r,b.primary+0x10,vt)||vt!=s.image+0x23b9b18||!Get(r,b.primary+0x30,vt)||vt!=s.image+0x23b9b50||!Get(r,b.primary+8,owner)||owner!=b.block||!Get(r,b.primary+0x38,owner)||owner!=b.node||!Get(r,b.block+0x548,owner)||owner!=b.primary+0x30||!Get(r,b.node+0x38,owner)||owner!=b.primary+0x30||!Get(r,b.primary+0x68,b.records)||!Aligned(b.records,16)||!Range(b.records,0x6c0))return false;
        b.record=b.records; // node's base id == this one-body block's id
        stage(k?"carry_bitmap1":"carry_bitmap0");std::uintptr_t bitmap=0;std::uint32_t bits=0;
        if(!Get(r,b.node+8,bitmap)||!Aligned(bitmap,4)||!Get(r,bitmap,bits))return false;
        b.nodeActive=(bits&1u)!=0;
        stage(k?"carry_record1":"carry_record0");if(!Get(r,b.record+0x78,b.target)||!Get(r,b.record+0x7c,b.previous)||!Get(r,b.record+0x7a,b.carrier)||!Range(b.record+0x380,sizeof(b.state))||!r(b.record+0x380,b.state,sizeof(b.state))||!Range(b.record+0x6a2,sizeof(b.flags))||!r(b.record+0x6a2,b.flags,sizeof(b.flags))||!Get(r,b.ps+0x280,b.psRecord)||!Aligned(b.psRecord,4)||!Get(r,b.psRecord,b.psFlags))return false;
    }
    stage("aliases");if(pools[0]==pools[1]||out.body[0].node==out.body[1].node||out.body[0].primary==out.body[1].primary||out.body[0].records==out.body[1].records)return false;
    stage("selector");if(!Get(r,s.player+0xd0,out.selector)||!Aligned(out.selector,8)||!Range(out.selector,0x3f30)||!Get(r,out.selector,vt)||vt!=s.image+0x22e8f00||!Get(r,out.selector+0x10,st)||st!=s.player||!Get(r,out.selector+0xa60,out.selectorOwner)||!Get(r,out.selector+0xaa0,out.selectorTarget)||!Get(r,out.selector+0xa6c,out.cachedKind)||!Get(r,out.selector+0x1268,out.currentKind)||!Get(r,out.selector+0xae4,out.cachedFlags)||!Get(r,out.selector+0x1288,out.currentFlags))return false;
    stage("ready");return true;
}
inline bool SelectorAccepts(const Observation& a) noexcept {
    return a.selectorOwner==a.body[0].primary&&a.cachedKind==a.currentKind&&(a.currentFlags&0x20u)&&!(a.cachedFlags&0x6000u)&&(a.playerFlags&0x400u);
}
// A native carrier flag plus the plugin target still proves only the carrier
// side, never the target body's attach/camera/control state. No attached bit
// is exported and these reads must never unlock downed controls.
template<class Reader> bool ReadPair(Reader& r,const Scope& s,Reply& out) noexcept {
    out={};if(const char* why=ScopeRefusal(s)){out.reason=why;return false;}
    Observation a{},b{};const char* why="plugin_chain";if(!ReadOnce(r,s,a,&why)||!ReadOnce(r,s,b,&why)){out.reason=why;return false;}
    if(!Same(a,b)){out.reason="state_changed";return false;}
    for(unsigned k=0;k<2;++k){const auto& x=a.body[k];auto& y=out.body[k];y.target=x.target;y.previous=x.previous;y.carrier=x.carrier;y.nodeActive=x.nodeActive;y.psFlags=x.psFlags;for(unsigned j=0;j<4;++j)y.flags[j]=x.flags[j];for(unsigned j=0;j<5;++j)y.state[j]=x.state[j];}
    out.selectorOwnerMatches=a.selectorOwner==a.body[0].primary;out.selectorAccepts=SelectorAccepts(a);out.selectorTarget=a.selectorTarget;
    out.cachedKind=a.cachedKind;out.currentKind=a.currentKind;out.cachedFlags=a.cachedFlags;out.currentFlags=a.currentFlags;
    out.ready=true;out.reason="ready";return true;
}
} // namespace mgscoop::carry_plugin_read
