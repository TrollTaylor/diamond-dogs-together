#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include "sleep_plugin_contract.hpp"
namespace mgscoop::sleep_plugin_read {
// Pure diagnostic reader: no engine methods, game memory writes, input, mode
// requests, or recovery. ExactGate additionally requires the coordinator's
// full executable SHA/PE and natural owner qualification.
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
 std::uintptr_t block=0,ps=0,node=0,primary=0,interface=0,records=0,bitmap=0,actionBytes=0,lifeRecord=0;
 std::uintptr_t callback=0,adjustment=0;
 std::uint32_t id=0,nodeState=0,nodeKind=0,recordFlags=0,subtype=0,lifeState=0;
 std::uint16_t flags=0,qAction=0;
 std::uint8_t mode=0,requested=0,counter=0,action=0;
 bool nodeActive=false,callbackInitialized=false;
};
struct Observation {
 std::uintptr_t player=0,data=0,state=0,ids=0,pool=0,parts=0,blocks=0,blockTable=0,lifeRecords=0,qActions=0;
 std::uint32_t playerFlags=0;
 Body body[2];
};
struct BodyReply {
 std::uint32_t nodeState=0,nodeKind=0,recordFlags=0,subtype=0,lifeState=0;
 std::uint16_t flags=0,qAction=0;
 std::uint8_t mode=0,requested=0,counter=0,action=0;
 bool nodeActive=false,callbackInitialized=false;
};
struct Reply {
 bool ready=false;
 BodyReply body[2];
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
    for(const auto& site:sleep_plugin_contract::sites){
        if(site.rva>span||site.size>span-site.rva||site.size>sizeof(buffer)||!r(base+site.rva,buffer,site.size)){mismatch=site.rva;return false;}
        for(std::size_t j=0;j<site.size;++j)if(buffer[j]!=site.bytes[j]){mismatch=site.rva;return false;}
    }
    for(const auto& slot:sleep_plugin_contract::slots){
        const auto off=slot.vtable_rva+slot.offset;std::uintptr_t value=0;
        if(off>span||sizeof(value)>span-off||!Get(r,base+off,value)||value!=base+slot.target_rva){mismatch=off;return false;}
    }return true;
}

inline bool Same(const Observation& a,const Observation& b) noexcept {
 if(a.player!=b.player||a.data!=b.data||a.state!=b.state||a.ids!=b.ids||a.pool!=b.pool||a.parts!=b.parts||a.blocks!=b.blocks||a.blockTable!=b.blockTable||a.lifeRecords!=b.lifeRecords||a.qActions!=b.qActions||a.playerFlags!=b.playerFlags)return false;
 for(unsigned k=0;k<2;++k){const auto& x=a.body[k];const auto& y=b.body[k];
  if(x.block!=y.block||x.ps!=y.ps||x.node!=y.node||x.primary!=y.primary||x.interface!=y.interface||x.records!=y.records||x.bitmap!=y.bitmap||x.actionBytes!=y.actionBytes||x.lifeRecord!=y.lifeRecord||x.callback!=y.callback||x.adjustment!=y.adjustment||x.id!=y.id||x.nodeState!=y.nodeState||x.nodeKind!=y.nodeKind||x.recordFlags!=y.recordFlags||x.subtype!=y.subtype||x.lifeState!=y.lifeState||x.flags!=y.flags||x.qAction!=y.qAction||x.mode!=y.mode||x.requested!=y.requested||x.counter!=y.counter||x.action!=y.action||x.nodeActive!=y.nodeActive||x.callbackInitialized!=y.callbackInitialized)return false;
 }return true;
}
inline std::uintptr_t CompleteOffset(std::uintptr_t image,std::uintptr_t vt) noexcept {
 if(vt==image+0x23d5c18)return 0;
 if(vt==image+0x23d5c40)return 0x10;
 if(vt==image+0x23d5c70)return 0x30;
 if(vt==image+0x23d5cb0)return 0x48;
 return std::numeric_limits<std::uintptr_t>::max();
}
inline std::uintptr_t ExpectedCallback(std::uintptr_t image,unsigned mode) noexcept {
 constexpr std::uintptr_t table[]={0x645940,0x12ca3b0,0x12c9a80,0x12ca040,0x12cb2b0,0x12caa10,0x12caf50};
 return mode<7?image+table[mode]:0;
}
// These fields are decoded by complete pinned getter/constructor/admission/
// callback bodies. An initialized but inactive plugin is useful evidence;
// no observation itself proves a playing clip or permits a wake or carry.
// Caller resamples its natural WindowSeq/InBatch after ReadPair, refusing any
// owner, generation, or input-window change before publishing the result.
template<class Reader> bool ReadOnce(Reader& r,const Scope& s,Observation& out,const char** refusal=nullptr) noexcept {
 const auto stage=[&](const char* value) noexcept {if(refusal)*refusal=value;};stage("scope");
 out={};if(ScopeRefusal(s)||!Range(s.image,0xa01d000)||!Range(s.player,0x388)||!Range(s.data,0x1f0)||!Range(s.state,0x48))return false;
 stage("player");out.player=s.player;out.data=s.data;out.state=s.state;
 std::uintptr_t vt=0,q=0,st=0,sync=0;std::uint32_t count=0,realized=0,local=0;
 if(!Get(r,s.player,vt)||vt!=s.image+0x2295490||!Get(r,s.player+0x60,q)||q!=s.data||!Get(r,q,vt)||vt!=s.image+0x23ad970||!Get(r,q+0x1e8,st)||st!=s.state||!Get(r,st+0x40,count)||count!=2||!Get(r,s.player+0xa8,sync)||sync||!Get(r,s.player+0x204,out.playerFlags)||!(out.playerFlags&1u)||(out.playerFlags&0x100u)||!Get(r,s.player+0x208,count)||count!=2||!Get(r,s.player+0x20c,realized)||realized!=2||!Get(r,s.player+0x214,local)||local)return false;
 stage("arrays");if(!Get(r,s.player+0x70,out.ids)||!Aligned(out.ids,2)||!Get(r,s.player+0x78,out.pool)||!out.pool||!Get(r,s.player+0x90,out.parts)||!out.parts||!Get(r,s.player+0x378,out.blocks)||!Aligned(out.blocks,8)||!Get(r,s.player+0x50,out.blockTable)||!Aligned(out.blockTable,8)||!Get(r,q+8,out.lifeRecords)||!Aligned(out.lifeRecords,4)||!Get(r,q+0x18,out.qActions)||!Aligned(out.qActions,2))return false;
 if(!Range(out.ids,4)||!Range(out.pool,2)||!Range(out.parts,2)||!Range(out.blocks,16*0x6a8)||!Range(out.blockTable,16*8)||!Range(out.lifeRecords,2*0x4c)||!Range(out.qActions,4))return false;
 std::uint8_t pools[2]{};
 for(unsigned k=0;k<2;++k){stage(k?"instance1":"instance0");auto& b=out.body[k];std::uint16_t id=0;std::uint8_t part=0;
  if(!Get(r,out.ids+k*2,id)||id!=k||!Get(r,out.pool+k,pools[k])||pools[k]>=16||!Get(r,out.parts+k,part)||part!=3)return false;
  b.id=id;b.block=out.blocks+std::uintptr_t(pools[k])*0x6a8;b.lifeRecord=out.lifeRecords+k*0x4c;
  stage(k?"block_ps1":"block_ps0");std::uintptr_t block2=0,owner=0;std::uint32_t base=0,one=0;
  if(!Get(r,out.blockTable+pools[k]*8,block2)||block2!=b.block||!Get(r,b.block,vt)||vt!=s.image+0x2295210||!Get(r,b.block+0x138,owner)||owner!=s.player||!Get(r,b.block+8,one)||one!=1||!Get(r,b.block+0xc,base)||base!=id||!Get(r,b.block+0x78,b.ps)||!Aligned(b.ps,8)||!Range(b.ps,0x510)||!Get(r,b.ps,vt)||(vt!=s.image+0x23b09f0&&vt!=s.image+0x23b0c70)||!Get(r,b.ps+0x408,owner)||owner!=b.block||!Get(r,b.ps+8,base)||base!=id)return false;
  stage(k?"sleep_slot1":"sleep_slot0");if(!Get(r,b.ps+0xd8,b.interface)||!Aligned(b.interface,8)||!Get(r,b.interface,vt))return false;
  stage(k?"sleep_type1":"sleep_type0");const auto adjustment=CompleteOffset(s.image,vt);
  if(adjustment==std::numeric_limits<std::uintptr_t>::max()||b.interface<adjustment)return false;
  b.primary=b.interface-adjustment;
  stage(k?"sleep_primary1":"sleep_primary0");if(!Aligned(b.primary,8)||!Range(b.primary,0x80)||!Get(r,b.primary,vt)||vt!=s.image+0x23d5c18||!Get(r,b.primary+0x10,vt)||vt!=s.image+0x23d5c40||!Get(r,b.primary+0x30,vt)||vt!=s.image+0x23d5c70||!Get(r,b.primary+0x48,vt)||vt!=s.image+0x23d5cb0||!Get(r,b.primary+8,owner)||owner!=b.block)return false;
  stage(k?"sleep_node1":"sleep_node0");if(!Get(r,b.ps+0x500,b.node)||!Aligned(b.node,8)||!Range(b.node,0x70)||!Get(r,b.node,vt)||vt!=s.image+0x2306628||!Get(r,b.node+0x20,one)||one!=1||!Get(r,b.node+0x24,base)||base!=id||!Get(r,b.primary+0x38,owner)||owner!=b.node||!Get(r,b.node+0x38,owner)||owner!=b.primary+0x30||!Get(r,b.node+0x28,b.nodeState)||!Get(r,b.node+0x2c,b.nodeKind))return false;
  stage(k?"sleep_records1":"sleep_records0");if(!Get(r,b.primary+0x78,b.records)||!Aligned(b.records,16)||!Range(b.records,0x40)||!Get(r,b.node+8,b.bitmap)||!Aligned(b.bitmap,4))return false;
  stage(k?"sleep_bitmap1":"sleep_bitmap0");std::uint32_t bits=0;if(!Get(r,b.bitmap,bits))return false;b.nodeActive=(bits&1u)!=0;
  stage(k?"sleep_record1":"sleep_record0");if(!Get(r,b.records,b.callback)||!Get(r,b.records+8,b.adjustment)||b.adjustment||!Get(r,b.records+0x10,b.mode)||b.mode>6||!Get(r,b.records+0x11,b.requested)||b.requested>6||!Get(r,b.records+0x12,b.counter)||!Get(r,b.records+0x14,b.flags)||!Get(r,b.records+0x20,b.subtype)||!Get(r,b.records+0x30,b.recordFlags))return false;
  stage(k?"sleep_callback1":"sleep_callback0");b.callbackInitialized=b.callback!=0;
  if(b.callback!=ExpectedCallback(s.image,b.mode)&&(b.mode||b.callback))return false;
  stage(k?"sleep_action_life1":"sleep_action_life0");if(!Get(r,b.ps+0x2d8,b.actionBytes)||!b.actionBytes||!Get(r,b.actionBytes,b.action)||!Get(r,out.qActions+k*2,b.qAction)||!Get(r,b.lifeRecord+0x34,b.lifeState)||b.lifeState>2)return false;
 }
 stage("aliases");if(pools[0]==pools[1]||out.body[0].ps==out.body[1].ps||out.body[0].node==out.body[1].node||out.body[0].primary==out.body[1].primary||out.body[0].records==out.body[1].records||out.body[0].bitmap==out.body[1].bitmap||out.body[0].actionBytes==out.body[1].actionBytes)return false;
 stage("ready");return true;
}
template<class Reader> bool ReadPair(Reader& r,const Scope& s,Reply& out) noexcept {
 out={};if(const char* why=ScopeRefusal(s)){out.reason=why;return false;}
 Observation a{},b{};const char* why="sleep_chain";if(!ReadOnce(r,s,a,&why)||!ReadOnce(r,s,b,&why)){out.reason=why;return false;}
 if(!Same(a,b)){out.reason="state_changed";return false;}
 for(unsigned k=0;k<2;++k){const auto& x=a.body[k];auto& y=out.body[k];y.nodeState=x.nodeState;y.nodeKind=x.nodeKind;y.recordFlags=x.recordFlags;y.subtype=x.subtype;y.lifeState=x.lifeState;y.flags=x.flags;y.qAction=x.qAction;y.mode=x.mode;y.requested=x.requested;y.counter=x.counter;y.action=x.action;y.nodeActive=x.nodeActive;y.callbackInitialized=x.callbackInitialized;}
 out.ready=true;out.reason="ready";return true;
}
} // namespace mgscoop::sleep_plugin_read
