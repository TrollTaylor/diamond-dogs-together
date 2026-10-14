#pragma once
#include <cmath>
#include <cstring>
#include "sleep_plugin_read.hpp"
#include "sleep_partner_stance_contract.hpp"
namespace mgscoop::sleep_partner_owned_read {
namespace base=sleep_plugin_read;
using base::Body;using base::Aligned;using base::Range;using base::Get;
using base::CodeGate;using base::CompleteOffset;using base::ExpectedCallback;
struct Scope {
 bool exactGate=false,privateWorker=false,copiedSaves=false,required=false,ownedWindow=false;
 bool serialized=false,participantsLive=false,selectedBodyOwned=false,modelReady=false,batch=false,faulted=false;
 bool pairedReady=false,fieldReady=false,demo=false,stopping=false;
 std::uintptr_t image=0,player=0,data=0,state=0,go=0,block=0,ps=0,modelCS=0,modelEntry=0,pp=0,source=0,windowPPVT=0;
 std::uint32_t mission=0,ownedMission=0,generation=0,ownedGeneration=0,armGeneration=0,armEpoch=0;
 std::uint32_t thread=0,ownerThread=0,sequenceBefore=0,sequenceAfter=0;
 std::uint16_t epoch=0,ownedEpoch=0;std::uint64_t now=0,poll=0,birthSerial=0;
};
inline const char* ScopeRefusal(const Scope& s) noexcept {
 if(!s.exactGate||!s.privateWorker||!s.copiedSaves||!s.required||!s.participantsLive||
    !s.pairedReady||!s.fieldReady||s.demo||s.stopping)return "partner_sleep_admission";
 if(!s.thread||s.thread!=s.ownerThread)return "partner_sleep_thread";
 if((s.mission!=30010&&s.mission!=30020&&s.mission!=30050)||!s.epoch||!s.generation||
    s.mission!=s.ownedMission||s.epoch!=s.ownedEpoch||s.generation!=s.ownedGeneration)return "partner_sleep_scene";
 if(!s.poll||s.now<s.poll||s.now-s.poll>250)return "partner_sleep_stale";
 if(!s.ownedWindow||!s.serialized||s.batch||s.faulted||!s.sequenceBefore||!(s.sequenceBefore&1u)||
    s.sequenceBefore!=s.sequenceAfter)return "partner_sleep_window";
 if(!s.selectedBodyOwned||!s.modelReady||!s.birthSerial||!s.armGeneration||!s.armEpoch||
    !Range(s.image,0xa01d000)||!Range(s.player,0x388)||!Range(s.data,0x1f0)||!Range(s.state,0x48)||
    !Range(s.go,0x20)||!Range(s.block,0x140)||!Range(s.ps,0x510)||!Range(s.modelCS,0x70)||
    !Range(s.modelEntry,0xc0)||!Range(s.pp,0x464)||!Range(s.source,0x70)||!Aligned(s.windowPPVT,8))return "partner_sleep_identity";
 return nullptr;
}
inline bool SameScope(const Scope& a,const Scope& b) noexcept {
 return !ScopeRefusal(a)&&!ScopeRefusal(b)&&a.image==b.image&&a.player==b.player&&a.data==b.data&&a.state==b.state&&
 a.go==b.go&&a.block==b.block&&a.ps==b.ps&&a.modelCS==b.modelCS&&a.modelEntry==b.modelEntry&&
 a.pp==b.pp&&a.source==b.source&&a.windowPPVT==b.windowPPVT&&a.birthSerial==b.birthSerial&&
 a.mission==b.mission&&a.epoch==b.epoch&&a.generation==b.generation&&a.armGeneration==b.armGeneration&&
 a.armEpoch==b.armEpoch&&a.thread==b.thread&&a.sequenceBefore==b.sequenceBefore&&a.sequenceAfter==b.sequenceAfter;
}
struct Pad {
 float derived[6]{},axes[6]{};std::uint32_t words[4]{};
 std::uint32_t buttonsMask=0,triggerMask=0,accumulatedButtons=0,computedButtons=0;
};
struct Values {float health=0,maximumHealth=0,stun=0,maximumStun=0;std::uint8_t psStance=0,qStance=0;std::uint32_t psPacked=0;};
struct Observation:base::Observation {Pad pad{};Values values[2]{};std::uint8_t poolIndex[2]{};};
inline bool CoherentTargetStance(const Observation& o) noexcept {return o.values[1].psStance==o.values[1].qStance;}
inline bool Zero(const Pad& p) noexcept {
 for(float v:p.derived)if(!std::isfinite(v))return false;
 for(float v:p.axes)if(!std::isfinite(v)||v!=0.f)return false;
 for(auto v:p.words)if(v)return false;
 return p.derived[0]==0.f&&p.derived[3]==0.f&&p.derived[4]==0.f&&!p.computedButtons;
}
inline bool Same(const Observation& a,const Observation& b) noexcept {
 if(!base::Same(a,b)||a.poolIndex[0]!=b.poolIndex[0]||a.poolIndex[1]!=b.poolIndex[1])return false;
 for(unsigned i=0;i<6;++i)if(a.pad.derived[i]!=b.pad.derived[i]||a.pad.axes[i]!=b.pad.axes[i])return false;
 for(unsigned i=0;i<4;++i)if(a.pad.words[i]!=b.pad.words[i])return false;
 if(a.pad.buttonsMask!=b.pad.buttonsMask||a.pad.triggerMask!=b.pad.triggerMask||
    a.pad.accumulatedButtons!=b.pad.accumulatedButtons||a.pad.computedButtons!=b.pad.computedButtons)return false;
 for(unsigned i=0;i<2;++i){const auto& x=a.values[i];const auto& y=b.values[i];
  if(x.health!=y.health||x.maximumHealth!=y.maximumHealth||x.stun!=y.stun||x.maximumStun!=y.maximumStun||
     x.psStance!=y.psStance||x.qStance!=y.qStance||x.psPacked!=y.psPacked)return false;
 }return true;
}
template<class Reader> bool ReadOnce(Reader& r,const Scope& s,Observation& out,const char** refusal=nullptr) noexcept {
 const auto stage=[&](const char* value) noexcept {if(refusal)*refusal=value;};stage("scope");
 out={};if(ScopeRefusal(s)||!Range(s.image,0xa01d000)||!Range(s.player,0x388)||!Range(s.data,0x1f0)||!Range(s.state,0x48))return false;
 stage("player");out.player=s.player;out.data=s.data;out.state=s.state;
 std::uintptr_t vt=0,q=0,st=0,sync=0,livePlayer=0;std::uint32_t count=0,realized=0,local=0;
 if(!Get(r,s.image+0x2a82c30,livePlayer)||livePlayer!=s.player||!Get(r,s.player,vt)||vt!=s.image+0x2295490||!Get(r,s.player+0x60,q)||q!=s.data||!Get(r,q,vt)||vt!=s.image+0x23ad970||!Get(r,q+0x1e8,st)||st!=s.state||!Get(r,st+0x40,count)||count!=2||!Get(r,s.player+0xa8,sync)||sync||!Get(r,s.player+0x204,out.playerFlags)||!(out.playerFlags&1u)||(out.playerFlags&0x100u)||!Get(r,s.player+0x208,count)||count!=2||!Get(r,s.player+0x20c,realized)||realized!=2||!Get(r,s.player+0x214,local)||local)return false;
 stage("arrays");if(!Get(r,s.player+0x70,out.ids)||!Aligned(out.ids,2)||!Get(r,s.player+0x78,out.pool)||!out.pool||!Get(r,s.player+0x90,out.parts)||!out.parts||!Get(r,s.player+0x378,out.blocks)||!Aligned(out.blocks,8)||!Get(r,s.player+0x50,out.blockTable)||!Aligned(out.blockTable,8)||!Get(r,q+8,out.lifeRecords)||!Aligned(out.lifeRecords,4)||!Get(r,q+0x18,out.qActions)||!Aligned(out.qActions,2))return false;
 if(!Range(out.ids,4)||!Range(out.pool,2)||!Range(out.parts,2)||!Range(out.blocks,16*0x6a8)||!Range(out.blockTable,16*8)||!Range(out.lifeRecords,2*0x4c)||!Range(out.qActions,4))return false;
 std::uint8_t pools[2]{};
 for(unsigned k=0;k<2;++k){stage(k?"instance1":"instance0");auto& b=out.body[k];std::uint16_t id=0;std::uint8_t part=0;
  if(!Get(r,out.ids+k*2,id)||id!=k||!Get(r,out.pool+k,pools[k])||pools[k]>=16||!Get(r,out.parts+k,part)||part!=3)return false;
  out.poolIndex[k]=pools[k];b.id=id;b.block=out.blocks+std::uintptr_t(pools[k])*0x6a8;b.lifeRecord=out.lifeRecords+k*0x4c;
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
 stage("owned_target");
 if(out.body[1].block!=s.block||out.body[1].ps!=s.ps)return false;
 stage("owned_pad");std::uintptr_t got=0;std::uint32_t selected=0;
 if(s.go+0x20!=s.player||!Get(r,s.go,got)||got!=s.image+0x22953d0||
    !Get(r,s.player+0x218,selected)||selected!=1||!Get(r,s.player+0xb0,got)||got!=s.pp||
    !Get(r,s.pp,got)||got!=s.windowPPVT||!Get(r,s.pp+8,got)||got!=s.source||
    !Get(r,s.pp+0x280,got)||got!=s.player||!Get(r,s.source,got)||got!=s.image+0x21534e0)return false;
 if(!r(s.pp+0x10,out.pad.derived,sizeof(out.pad.derived))||
    !r(s.source+0x48,out.pad.words,sizeof(out.pad.words))||!r(s.source+0x58,out.pad.axes,sizeof(out.pad.axes))||
    !Get(r,s.pp+0x28,out.pad.buttonsMask)||!Get(r,s.pp+0x30,out.pad.triggerMask)||!Get(r,s.pp+0x460,out.pad.accumulatedButtons))return false;
 out.pad.computedButtons=(out.pad.words[0]|out.pad.accumulatedButtons)&~out.pad.buttonsMask;
 for(unsigned k=0;k<2;++k){
  auto& value=out.values[k];const auto& b=out.body[k];std::uintptr_t stance=0,qStance=0;
  if(!Get(r,b.lifeRecord+0x24,value.health)||!Get(r,b.lifeRecord+0x28,value.maximumHealth)||
     !Get(r,b.lifeRecord+0x2c,value.stun)||!Get(r,b.lifeRecord+0x30,value.maximumStun)||
     !Get(r,b.ps+sleep_partner_stance_contract::packedOffset,stance)||!Aligned(stance,8)||
     !Range(stance,sleep_partner_stance_contract::stride)||!Get(r,stance,value.psPacked)||
     !Get(r,s.data+0x30,qStance)||!qStance||!Get(r,qStance+k,value.qStance))return false;
  // Typed PS+8 was already proved equal to actual body id, so id-base=0.
  value.psStance=static_cast<std::uint8_t>(value.psPacked&15u);
 }
 stage("aliases");if(pools[0]==pools[1]||out.body[0].ps==out.body[1].ps||out.body[0].node==out.body[1].node||out.body[0].primary==out.body[1].primary||out.body[0].records==out.body[1].records||out.body[0].bitmap==out.body[1].bitmap||out.body[0].actionBytes==out.body[1].actionBytes)return false;
 stage("ready");return true;
}
}
