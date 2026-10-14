#pragma once
// Pure current census only. No original calls, hook, request, profile change,
// attachment, mode/flag write or lifetime/writer permission exists here.
#include "carry_recipient_read.hpp"
#include "horse_player_physical_read.hpp"
#include "horse_physical_profile_read.hpp"
#include "horse_player_cargo_bridge_contract.hpp"
#include <cmath>
#include <cstdio>
namespace mgscoop::horse_player_cargo_bridge_read {
namespace cr=carry_plugin_read;namespace rr=carry_recipient_read;
namespace hp=horse_player_physical_read;namespace pr=horse_physical_profile_read;
enum class ClipSource : std::uint8_t { other, carrier_load_bone_1d };
// Reproduce the exact complete D3CA70 recognizer, not names inferred from hashes.
inline ClipSource Source(std::uint64_t clip) noexcept {
 switch(clip){case 0x85D76E43B4D2ull:case 0x1481A04BDC75ull:case 0x17181E562E2Bull:
 case 0x2695EBDAF98Dull:case 0x51ADDEBCD428ull:case 0xB217B8AFAE92ull:
 case 0xD7B458464921ull:case 0xF6B9CB46D3EEull:return ClipSource::carrier_load_bone_1d;
 default:return ClipSource::other;}
}
struct Helper {
 std::uintptr_t resource=0,payload=0,iface=0,descriptor=0,records=0,record=0,nativeVariables=0;
 std::uint32_t base=0,state=0;std::uint64_t stride=0,count=0;float values[7]{};std::uint8_t control=0;
};
struct Body {
 Helper helper{};std::uint16_t action=0;std::uint16_t carrier=0xffff;
 ClipSource source=ClipSource::other;bool currentCarry=false,matrixMatches=false;
 float carrierMatrix[16]{};pr::Profile profile{};
};
struct Observation {rr::Observation recipients{};hp::Observation physical{};Body body[2];};
template<class Reader>bool CodeGate(Reader& r,std::uintptr_t image,std::size_t size,std::uintptr_t& mismatch) noexcept {
 mismatch=0;if(!cr::Range(image,size))return false;unsigned char bytes[256]{};
 for(const auto& p:horse_player_cargo_bridge_contract::sites){
  if(p.rva>size||p.size>size-p.rva){mismatch=p.rva;return false;}
  for(std::size_t k=0;k<p.size;){const auto n=(p.size-k<sizeof(bytes))?p.size-k:sizeof(bytes);
   if(!r(image+p.rva+k,bytes,n)){mismatch=p.rva;return false;}
   if(std::memcmp(bytes,p.bytes+k,n)){mismatch=p.rva;return false;}k+=n;
  }
 }
 for(const auto& p:horse_player_cargo_bridge_contract::slots){std::uintptr_t v=0;
  if(p.rva>size||sizeof(v)>size-p.rva||!cr::Get(r,image+p.rva,v)||v!=image+p.target){mismatch=p.rva;return false;}}
 return true;
}
template<class Reader>bool ReadHelper(Reader& r,std::uintptr_t image,unsigned id,const rr::Recipient& rec,Helper& out) noexcept {
 out={};out.iface=rec.helper;if(id>1||!cr::Aligned(out.iface,8)||out.iface<0x20)return false;
 out.payload=out.iface-0x20;std::uintptr_t vt=0,p=0;out.resource=0;
 if(!cr::Range(out.payload,0x70)||!cr::Get(r,out.payload,vt)||vt!=image+0x2339C18||
    !cr::Get(r,out.iface,vt)||vt!=image+0x2339C50)return false;
 // Exact current query resource -> payload -> normalized interface closure.
 if(!cr::Get(r,rec.primary+0x78,out.resource)||!cr::Aligned(out.resource,8)||
    !cr::Get(r,out.resource+0x10,p)||p!=out.payload)return false;
 if(!cr::Get(r,out.payload+0x50,out.records)||!cr::Aligned(out.records,16)||
    !cr::Get(r,out.payload+0x30,out.descriptor)||!cr::Aligned(out.descriptor,8)||
    !cr::Get(r,out.payload+0x68,out.base)||out.base!=id||
    !cr::Get(r,out.descriptor,vt)||vt!=image+0x645940||
    !cr::Get(r,out.descriptor+0x10,p)||p!=out.records||
    !cr::Get(r,out.descriptor+0x18,out.stride)||out.stride!=0x24||
    !cr::Get(r,out.descriptor+0x20,out.count)||out.count!=1||
    !cr::Get(r,out.payload+0x60,out.nativeVariables)||!cr::Aligned(out.nativeVariables,8))return false;
 out.record=out.records;
 if(!r(out.record,out.values,sizeof(out.values))||!cr::Get(r,out.record+0x1c,out.state)||!cr::Get(r,out.record+0x20,out.control))return false;
 for(float v:out.values)if(!std::isfinite(v)||std::fabs(v)>200000.0f)return false;
 return true;
}
inline bool SameHelper(const Helper& a,const Helper& b) noexcept {
 return a.resource==b.resource&&a.payload==b.payload&&a.iface==b.iface&&a.descriptor==b.descriptor&&
 a.records==b.records&&a.record==b.record&&a.nativeVariables==b.nativeVariables&&a.base==b.base&&
 a.stride==b.stride&&a.count==b.count&&a.state==b.state&&a.control==b.control&&!std::memcmp(a.values,b.values,sizeof(a.values));
}
template<class Reader>bool ReadOnce(Reader& r,const cr::Scope& s,Observation& out,const char*& reason) noexcept {
 out={};reason="pc_recipient";if(!rr::ReadOnce(r,s,out.recipients))return false;
 reason="pc_physical";if(!hp::ReadOnce(r,s,out.physical)||!cr::Same(out.recipients.carrier,out.physical.carry))return false;
 std::uintptr_t actions=0;if(!cr::Get(r,s.data+0x18,actions)||!cr::Aligned(actions,2))return false;
 for(unsigned id=0;id<2;++id){auto& b=out.body[id];const auto& rec=out.recipients.recipient[id];
  reason=id?"pc_helper1":"pc_helper0";if(!ReadHelper(r,s.image,id,rec,b.helper))return false;
  reason="pc_profile";if(!pr::ReadOnce(r,s.image,out.physical.body[id],b.profile)||!cr::Get(r,actions+id*2,b.action))return false;
  b.carrier=rec.carrier;b.source=Source(rec.clip);
  // Reads never cast a raw Horse unit to the 6C0 Player carrier frame.
  if(rec.carrierRecord){if(b.carrier>1||b.carrier==id||rec.carrierRecord!=out.recipients.carrier.body[b.carrier].record){reason="pc_carrier_owner";return false;}
   if(!r(rec.carrierRecord+0x110,b.carrierMatrix,sizeof(b.carrierMatrix))){reason="pc_carrier_matrix";return false;}
   for(float v:b.carrierMatrix)if(!std::isfinite(v)||std::fabs(v)>200000.0f){reason="pc_carrier_matrix";return false;}
   b.currentCarry=rr::CoherentBinding(out.recipients,id,b.carrier)&&b.action==0x2B;
   b.matrixMatches=b.source==ClipSource::carrier_load_bone_1d&&
       !std::memcmp(b.carrierMatrix,out.physical.body[id].matrix,sizeof(b.carrierMatrix));
  }
 }
 if(out.body[0].helper.payload==out.body[1].helper.payload||out.body[0].helper.records==out.body[1].helper.records||
    out.body[0].helper.descriptor==out.body[1].helper.descriptor){reason="pc_helper_alias";return false;}
 reason="pc_observed";return true;
}
inline bool Same(const Observation& a,const Observation& b) noexcept {
 if(!cr::Same(a.recipients.carrier,b.recipients.carrier)||!cr::Same(a.physical.carry,b.physical.carry))return false;
 for(unsigned id=0;id<2;++id){const auto& x=a.body[id];const auto& y=b.body[id];
  if(!rr::SameRecipient(a.recipients.recipient[id],b.recipients.recipient[id])||!hp::SameBody(a.physical.body[id],b.physical.body[id])||
     !SameHelper(x.helper,y.helper)||!pr::Same(x.profile,y.profile)||x.action!=y.action||x.carrier!=y.carrier||
     x.source!=y.source||x.currentCarry!=y.currentCarry||x.matrixMatches!=y.matrixMatches||
     std::memcmp(x.carrierMatrix,y.carrierMatrix,sizeof(x.carrierMatrix)))return false;}
 return true;
}
template<class Reader>bool Observe(Reader& r,const cr::Scope& s,Observation& out,const char*& reason) noexcept {
 out={};Observation a{},b{};if(!ReadOnce(r,s,a,reason)){out=a;return false;}
 if(!ReadOnce(r,s,b,reason)){out=b;return false;}if(!Same(a,b)){reason="pc_changed";return false;}out=b;return true;
}
inline bool Line(const Observation& a,bool ready,const char* reason,char* out,std::size_t capacity) noexcept {
 if(!out||!capacity)return false;
 const int n=std::snprintf(out,capacity,"pcBridge;ok=%u;b0=%x,%u,%u,%u;b1=%x,%u,%u,%u;why=%s",ready?1u:0u,
  a.body[0].action,a.body[0].profile.mode,a.body[0].currentCarry?1u:0u,a.body[0].matrixMatches?1u:0u,
  a.body[1].action,a.body[1].profile.mode,a.body[1].currentCarry?1u:0u,a.body[1].matrixMatches?1u:0u,reason?reason:"unknown");
 return n>=0&&static_cast<std::size_t>(n)<capacity;
}
} // namespace mgscoop::horse_player_cargo_bridge_read
