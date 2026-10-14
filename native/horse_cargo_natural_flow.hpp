#pragma once
// Pure observed natural sender/slot/recipient relation; no native invocation,
// loader, global messages, pointer fabrication, transform or mutation API.
#include "native_pose_horse_flow.hpp"
#include "horse_cargo_native_read.hpp"
#include "horse_cargo_flow_contract.hpp"
#include <cstdio>
namespace mgscoop::horse_cargo_natural_flow {
namespace hk=partner_input_hook;namespace cr=carry_plugin_read;namespace hn=horse_cargo_native_read;
struct Packet {std::uint64_t hash=0;std::uint16_t body=0xffff;std::uint32_t kind=0;std::uintptr_t unit=0;std::uint8_t variant=0;};
struct Carrier {
 std::uintptr_t record=0,returnedUnit=0,boundUnit=0;std::uint16_t victim=0xffff,previous=0xffff,horse=0xffff,boundHorse=0xffff,flags=0;
 std::uint8_t actual=0xff,phase=0xff,prior=0xff;
};
struct Recipient {std::uintptr_t record=0,resource=0,carrierRecord=0;std::uint16_t carrier=0xffff;std::uint8_t phase=0,next=0;};
struct Sample {hk::NativeHorseFlowOwner owner{};hn::Observation horse{};Packet packet{};Carrier carrier{};Recipient player[2]{};};
struct Result {
 bool closed=false,slotChanged=false,slotMatches=false,freeAfter=false,playerTarget=false,oldCarryLink=false;
 std::uint32_t kind=0,caller=0;std::uint16_t victim=0xffff,horse=0xffff,before=0xffff,after=0xffff;
 std::uint8_t carrierPhase=0xff,recipientPhase=0;std::int32_t reply=-1;
 const char* reason="horse_flow_never";
};
inline bool SameOwner(const hk::NativeHorseFlowOwner& a,const hk::NativeHorseFlowOwner& b) noexcept {
 const auto& x=a.births;const auto& y=b.births;
 if(!a.at||b.at<a.at||b.at-a.at>750||!a.sharedSerial||a.sharedSerial!=b.sharedSerial||!x.image||!x.player||!x.data||!x.state||
    x.image!=y.image||x.player!=y.player||x.data!=y.data||x.state!=y.state||
    !x.lifecycle||x.lifecycle!=y.lifecycle||!x.generation||x.generation!=y.generation||!x.sceneEpoch||x.sceneEpoch!=y.sceneEpoch||
    !x.thread||x.thread!=y.thread||(x.sequence&1u)||x.sequence!=y.sequence)return false;
 for(unsigned k=0;k<2;++k)if(!x.birth[k]||!x.block[k]||!x.ps[k]||!x.cs[k]||!x.entry[k]||
    x.birth[k]!=y.birth[k]||x.block[k]!=y.block[k]||x.ps[k]!=y.ps[k]||
    x.cs[k]!=y.cs[k]||x.entry[k]!=y.entry[k]||!a.carrierRecord[k]||a.carrierRecord[k]!=b.carrierRecord[k]||
    !a.recipientRecord[k]||a.recipientRecord[k]!=b.recipientRecord[k])return false;
 return x.birth[0]!=x.birth[1]&&x.block[0]!=x.block[1]&&x.ps[0]!=x.ps[1]&&x.cs[0]!=x.cs[1]&&x.entry[0]!=x.entry[1]&&
  a.carrierRecord[0]!=a.carrierRecord[1]&&a.recipientRecord[0]!=a.recipientRecord[1];
}
inline bool SameHorse(const hn::Observation& a,const hn::Observation& b) noexcept {
 return a.family==b.family&&a.kind==b.kind&&a.realized==b.realized&&a.controllerArray==b.controllerArray&&a.service==b.service&&
  horse_native_service_bridge::Same(a.bridge,b.bridge)&&a.primary==b.primary&&a.receiver==b.receiver&&a.descriptor==b.descriptor&&
  a.records==b.records&&a.record==b.record&&a.sdRecord==b.sdRecord&&a.unit==b.unit&&a.cs==b.cs&&a.gameId==b.gameId&&
  a.logicalIndex==b.logicalIndex&&a.nativeIndex==b.nativeIndex&&a.nativeBase==b.nativeBase&&a.nativeCount==b.nativeCount;
}
template<class Reader>bool CodeGate(Reader& read,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch) noexcept {
 mismatch=0;if(!cr::Range(image,span))return false;unsigned char bytes[256]{};
 for(const auto& p:horse_cargo_flow_contract::sites){if(p.rva>span||p.size>span-p.rva){mismatch=p.rva;return false;}
  for(std::size_t j=0;j<p.size;){const auto n=p.size-j<sizeof(bytes)?p.size-j:sizeof(bytes);
   if(!read(image+p.rva+j,bytes,n)||std::memcmp(bytes,p.bytes+j,n)){mismatch=p.rva;return false;}j+=n;}}
 return hn::CodeGate(read,image,span,mismatch);
}
template<class Reader>bool ReadPacket(Reader& read,std::uintptr_t at,Packet& p) noexcept {
 p={};return cr::Get(read,at,p.hash)&&cr::Get(read,at+8,p.body)&&cr::Get(read,at+12,p.kind)&&
  cr::Get(read,at+24,p.unit)&&cr::Get(read,at+32,p.variant);
}
template<class Reader>bool ReadPlayer(Reader& read,const hk::NativeHorseFlowOwner& owner,Carrier& c,Recipient (&players)[2]) noexcept {
 c={};c.record=owner.carrierRecord[0];
 if(!cr::Range(c.record,0x6c0)||!cr::Get(read,c.record+0x78,c.victim)||!cr::Get(read,c.record+0x7c,c.previous)||
    !cr::Get(read,c.record+0x80,c.actual)||!cr::Get(read,c.record+0x81,c.phase)||!cr::Get(read,c.record+0x82,c.prior)||
    !cr::Get(read,c.record+0x538,c.horse)||!cr::Get(read,c.record+0x548,c.returnedUnit)||
    !cr::Get(read,c.record+0x258,c.boundUnit)||!cr::Get(read,c.record+0x236,c.boundHorse)||!cr::Get(read,c.record+0x6a2,c.flags))return false;
 for(unsigned k=0;k<2;++k){auto& p=players[k];p={};p.record=owner.recipientRecord[k];
  if(!cr::Range(p.record,0x60)||!cr::Get(read,p.record+0x10,p.phase)||!cr::Get(read,p.record+0x11,p.next)||
     !cr::Get(read,p.record+0x20,p.resource)||!cr::Get(read,p.record+0x28,p.carrierRecord)||!cr::Get(read,p.record+0x30,p.carrier))return false;
 }
 return true;
}
inline bool NaturalSource(const Sample& a,const hk::NativeHorseFlowCall& call) noexcept {
 const auto image=a.owner.births.image;
 if(!SameOwner(a.owner,a.owner)||call.service!=image+0x2c38008||call.target!=a.horse.gameId||(call.target>>9)!=13||
    a.packet.hash!=0x5167c60042b5ull||a.packet.body!=a.carrier.victim||a.carrier.actual!=0||a.carrier.horse!=call.target||
    a.packet.unit||a.packet.variant)return false;
 return a.packet.kind==1?call.caller==image+0x1136f57&&a.carrier.phase==0x17:
  a.packet.kind==2&&call.caller==image+0x113a814&&a.carrier.phase==7;
}
inline Result Complete(const Sample& before,const Sample& after,const hk::NativeHorseFlowCall& call,std::int32_t reply) noexcept {
 Result result{};result.kind=before.packet.kind;result.victim=before.packet.body;result.horse=before.horse.gameId;
 result.before=before.horse.cargo;result.after=after.horse.cargo;result.reply=reply;result.carrierPhase=after.carrier.phase;
 result.caller=call.caller>=before.owner.births.image&&call.caller-before.owner.births.image<=UINT32_MAX?
  static_cast<std::uint32_t>(call.caller-before.owner.births.image):0;
 result.reason="horse_flow_source";if(!NaturalSource(before,call))return result;
 result.reason="horse_flow_owner_changed";if(!SameOwner(before.owner,after.owner)||!SameHorse(before.horse,after.horse))return result;
 result.reason="horse_flow_packet_changed";
 if(call.returned!=call.result||before.packet.hash!=after.packet.hash||before.packet.body!=after.packet.body||
    before.packet.kind!=after.packet.kind||before.packet.variant!=after.packet.variant)return result;
 result.closed=true;result.slotChanged=before.horse.cargo!=after.horse.cargo||before.horse.occupied!=after.horse.occupied;
 result.slotMatches=after.horse.cargo==before.packet.body&&after.horse.sdCargo==before.packet.body&&after.horse.occupied==1;
 result.freeAfter=after.horse.cargo==0xffff&&after.horse.sdCargo==0xffff&&!after.horse.occupied;
 result.playerTarget=before.packet.body<2;
 if(result.playerTarget){const auto& p=after.player[before.packet.body];result.recipientPhase=p.phase;
  result.oldCarryLink=p.resource||p.carrierRecord||p.carrier!=0xffff||p.phase||p.next;}
 if(result.kind==1){
  if(reply==0&&result.slotMatches&&after.packet.unit==after.horse.unit){
   result.reason=before.horse.cargo==0xffff&&!before.horse.occupied&&result.slotChanged?"horse_flow_slot_committed":
    before.horse.cargo==before.packet.body&&before.horse.occupied==1?"horse_flow_slot_existing":"horse_flow_slot_delta";
  }else result.reason="horse_flow_no_commit";
 }else result.reason=reply==0&&result.freeAfter?(before.horse.occupied?"horse_flow_slot_retired":"horse_flow_slot_already_free"):"horse_flow_unload_unsettled";
 // A slot commit and finite anchor are never a loaded-player/follow/camera,
 // collision, graph, retirement, lifetime or native writer receipt.
 return result;
}
inline bool Line(const Result& r,unsigned calls,char* out,std::size_t capacity) noexcept {
 if(!out||!capacity)return false;
 const int n=std::snprintf(out,capacity,"horseFlow;read=%u;n=%u;call=%x;k=%u;h=%u;body=%u;slot=%u,%u;match=%u;free=%u;p=%u,%u,%u;reply=%d;why=%s",
  r.closed?1u:0u,calls,r.caller,r.kind,unsigned(r.horse),unsigned(r.victim),unsigned(r.before),unsigned(r.after),
  r.slotMatches?1u:0u,r.freeAfter?1u:0u,unsigned(r.carrierPhase),unsigned(r.recipientPhase),r.oldCarryLink?1u:0u,r.reply,r.reason);
 return n>=0&&static_cast<std::size_t>(n)<capacity;
}
}
