#pragma once
#include "sleep_body0_lease.hpp"
#include "sleep_plugin_read.hpp"
namespace mgscoop::sleep_body0_rest {
namespace sl=sleep_body0_lease;namespace sp=sleep_plugin_read;
struct Descriptor{std::uintptr_t ps=0,records=0;std::uint32_t base=0,flags=0;};
inline bool Same(const Descriptor& a,const Descriptor& b)noexcept{return a.ps==b.ps&&a.records==b.records&&a.base==b.base&&a.flags==b.flags;}
template<class R>bool ReadDescriptor(R& r,const sp::Scope& s,const sp::Body& b,Descriptor& out)noexcept{
 out={};if(sp::ScopeRefusal(s)||b.id||!b.ps||!b.block||!sp::Range(b.ps,0x410))return false;
 std::uintptr_t live=0,vt=0,owner=0,block=0;std::uint32_t local=0,selected=0;
 if(!sp::Get(r,s.image+0x2a82c30,live)||live!=s.player||!sp::Get(r,s.player+0x214,local)||local||
 !sp::Get(r,s.player+0x218,selected)||selected||!sp::Get(r,b.ps,vt)||(vt!=s.image+0x23b09f0&&vt!=s.image+0x23b0c70)||
 !sp::Get(r,b.ps+0x408,block)||block!=b.block||!sp::Get(r,block+0x138,owner)||owner!=s.player||
 !sp::Get(r,block+0x78,owner)||owner!=b.ps||!sp::Get(r,b.ps+8,out.base)||out.base||
 !sp::Get(r,b.ps+0x280,out.records)||!sp::Range(out.records,0x20)||!sp::Get(r,out.records+0x14,out.flags))return false;
 out.ps=b.ps;return true;
}
inline bool RestHash(std::uint64_t hash)noexcept{return hash==0xd7654fb0a3f2ull||hash==0xc31867300b57ull;}
inline bool SameSample(const sl::Sample& a,const sl::Sample& b,bool exchanged)noexcept{
 return sl::Same(a.owner,b.owner)&&sl::Bits(a.health)==sl::Bits(b.health)&&sl::Bits(a.maximumHealth)==sl::Bits(b.maximumHealth)&&
 sl::Bits(a.maximumStun)==sl::Bits(b.maximumStun)&&sl::Bits(b.stun)==(exchanged?sl::Bits(0.f):sl::Bits(a.stun))&&
 a.lifeState==b.lifeState&&a.mode==b.mode&&a.requested==b.requested&&a.action==b.action&&a.stance==b.stance&&a.cause==b.cause&&
 a.qAction==b.qAction&&a.nodeActive==b.nodeActive&&a.graphCurrent==b.graphCurrent&&a.graphSleepCandidate==b.graphSleepCandidate;
}
inline const char* PendingDeadline(std::uint64_t started,std::uint64_t now)noexcept{
 if(!started)return nullptr;if(now<started)return "sleep_rest_body0_clock";
 return now-started>sl::kAcquireMs?"sleep_rest_body0_zero_timeout":nullptr;
}
struct ZeroCheckpoint{bool valid=false,clearPending=false;};
inline ZeroCheckpoint ObserveZero(bool exactSequence,bool retainedNaturalZero)noexcept{
 return {exactSequence||retainedNaturalZero,exactSequence};
}
inline sl::Plan Maintain(const sl::Scope& s,const sl::Sample& a,const sl::Lease& l,std::uint64_t hash,const Descriptor& d,
 bool controlTick,bool canonical,bool wake,bool carryBlocked)noexcept{
 sl::Plan p{};p.reason="sleep_rest_body0_refused";
 if(!controlTick||!canonical||wake||carryBlocked){p.reason="sleep_rest_body0_phase";return p;}
 if(const auto why=sl::ContextRefusal(s,a)){p.reason=why;return p;}
 if(!s.admitted||!s.pairedOffline){p.reason="sleep_rest_body0_admission";return p;}
 if(l.phase!=sl::Phase::active||!l.nativeEntered||l.wakeAuthorized||l.wakeAt||!sl::Same(l.owner,a.owner)||!sl::SameMaximum(l,a)||
 !l.seededAt||s.now<l.seededAt||!l.lastSeen||s.now<l.lastSeen||s.now-l.lastSeen>sl::kFreshMs){p.reason="sleep_rest_body0_lease";return p;}
 if(a.mode!=5||a.requested||a.lifeState!=2||!a.nodeActive||a.action!=0x19||a.qAction!=0x84||a.stance<1||a.stance>3||
 !a.graphCurrent||!a.graphSleepCandidate||!RestHash(hash)||a.stun>=a.maximumStun){p.reason="sleep_rest_body0_native";return p;}
 if(d.ps!=a.owner.ps||d.base||!d.records||!sp::Range(d.records,0x20)||(d.flags&(1u<<18))){p.reason="sleep_rest_body0_descriptor";return p;}
 // An already-zero sample needs no write permission; its real observation
 // receipt is checked by the adapter without creating or renewing a deadline.
 if(a.stun==0.f){p.valid=true;p.reason="sleep_rest_body0_already_zero";return p;}
 // Readonly V10 retention cannot authorize a nonzero scalar write.
 if(!s.analogCapQualified){p.reason="sleep_rest_body0_zero_pending";return p;}
 p.valid=true;p.count=1;p.changes[0]={0x2c,sl::Bits(a.stun),sl::Bits(0.f)};
 p.reason="sleep_rest_body0_stun_cas";return p;
}
} // namespace mgscoop::sleep_body0_rest
