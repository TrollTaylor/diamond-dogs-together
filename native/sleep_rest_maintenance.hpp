#pragma once
#include "sleep_partner_controls.hpp"
#include "sleep_partner_owned_read.hpp"
#include "sleep_partner_carry_proof.hpp"
#include "sleep_plugin_read.hpp"
namespace mgscoop::sleep_rest_maintenance {
namespace ro=sleep_partner_owned_read;namespace sl=sleep_partner_lease;namespace ctl=sleep_partner_controls;
struct Descriptor {std::uintptr_t ps=0,records=0;std::uint32_t base=0,flags=0;};
inline bool Same(const Descriptor& a,const Descriptor& b)noexcept {
 return a.ps==b.ps&&a.records==b.records&&a.base==b.base&&a.flags==b.flags;
}
// Exact mode5 consumer indexes PS280 by (actualID-PS8)*32. Existing typed
// Graph/Scope observations must surround this read; the reader cannot write.
template<class R>bool ReadDescriptor(R& r,const ro::Scope& s,const sleep_plugin_read::Body& b,Descriptor& out)noexcept {
 out={};if(ro::ScopeRefusal(s)||b.id!=1||b.ps!=s.ps||b.block!=s.block||!ro::Range(b.ps,0x410))return false;
 std::uintptr_t vt=0,owner=0,block=0,live=0;std::uint32_t local=0,selected=0;
 if(!ro::Get(r,s.image+0x2a82c30,live)||live!=s.player||!ro::Get(r,s.player+0x214,local)||local||
 !ro::Get(r,s.player+0x218,selected)||selected!=1||!ro::Get(r,b.ps,vt)||
 (vt!=s.image+0x23b09f0&&vt!=s.image+0x23b0c70)||!ro::Get(r,b.ps+0x408,block)||block!=s.block||
 !ro::Get(r,block+0x138,owner)||owner!=s.player||!ro::Get(r,block+0x78,owner)||owner!=b.ps||
 !ro::Get(r,b.ps+8,out.base)||out.base!=b.id||!ro::Get(r,b.ps+0x280,out.records)||!ro::Range(out.records,0x20)||
 !ro::Get(r,out.records+0x14,out.flags))return false;
 out.ps=b.ps;return true;
}
// A post-exchange receipt may differ in exactly Stun2C. This is coherence
// only: the actual adapter still rereads typed graph/lifetime/descriptor/scope.
inline bool SameRestSample(const sl::Sample& a,const sl::Sample& b,bool exchanged)noexcept {
 return sl::Same(a.owner,b.owner)&&sl::Bits(a.health)==sl::Bits(b.health)&&sl::Bits(a.maximumHealth)==sl::Bits(b.maximumHealth)&&
 sl::Bits(a.maximumStun)==sl::Bits(b.maximumStun)&&sl::Bits(b.stun)==(exchanged?sl::Bits(0.f):sl::Bits(a.stun))&&
 a.lifeState==b.lifeState&&a.mode==b.mode&&a.requested==b.requested&&a.action==b.action&&a.qAction==b.qAction&&
 a.stance==b.stance&&a.cause==b.cause&&a.nodeActive==b.nodeActive&&a.graphCurrent==b.graphCurrent&&
 a.graphSleepCandidate==b.graphSleepCandidate&&a.graphHash==b.graphHash;
}
inline bool RestHash(std::uint64_t hash)noexcept {return hash==0xd7654fb0a3f2ull||hash==0xc31867300b57ull;}
// Pure conditional scalar plan only. The actual adapter must revalidate its
// current natural owner/window/zero-pad and same descriptor immediately before
// CAS; failure never grants restoration of life/mode/request/action/node/HP.
inline sl::Plan MaintainRest(const sl::Scope& s,const sl::Sample& a,const sl::Lease& l,
 const sleep_partner_test::Target& target,const sleep_partner_test::Target& owned,const Descriptor& d,
 bool before,bool enabled,bool wakeRequested,bool carry)noexcept {
 sl::Plan p{};p.reason="sleep_rest_maintenance_refused";
 if(!before||!enabled||wakeRequested||carry){p.reason="sleep_rest_phase_owner";return p;}
 if(const auto why=sl::Refusal(s,a,false)){p.reason=why;return p;}
 if(!s.admitted||!s.pairedOffline||!ctl::FreshTarget(target,s.now)||!ctl::SameTargetScene(target,owned)||
 !sleep_partner_carry_proof::AcceptedSleep(target)||!sleep_partner_carry_proof::AcceptedSleep(owned)||target.localRole!=owned.localRole||
 target.mission!=a.owner.mission||target.worldEpoch!=a.owner.epoch||target.peerGeneration!=a.owner.generation||
 target.armGeneration!=a.owner.armGeneration||target.armEpoch!=a.owner.armEpoch){p.reason="sleep_rest_canonical";return p;}
 if(l.phase!=sl::Phase::active||!l.nativeEntered||l.wakeAuthorized||l.wakeAt||l.recoveryAt||
 !sl::Same(l.owner,a.owner)||!sl::SameMaximum(l,a)||!l.seededAt||s.now<l.seededAt||!l.lastSeen||
 s.now<l.lastSeen||s.now-l.lastSeen>sl::kFreshMs){p.reason="sleep_rest_lease";return p;}
 if(a.mode!=5||a.requested||a.lifeState!=2||!a.nodeActive||a.action!=0x19||a.qAction!=0x84||
 a.stance<1||a.stance>3||!a.graphCurrent||!a.graphSleepCandidate||!RestHash(a.graphHash)||a.stun>=a.maximumStun){p.reason="sleep_rest_native";return p;}
 if(d.ps!=a.owner.ps||d.base!=1||!d.records||!ro::Range(d.records,0x20)||(d.flags&(1u<<18))){p.reason="sleep_rest_instant_recovery";return p;}
 p.valid=true;if(a.stun!=0.f){p.count=1;p.changes[0]={0x2c,sl::Bits(a.stun),sl::Bits(0.f)};}
 p.reason=p.count?"sleep_rest_stun_cas":"sleep_rest_already_zero";return p;
}
} // namespace mgscoop::sleep_rest_maintenance
