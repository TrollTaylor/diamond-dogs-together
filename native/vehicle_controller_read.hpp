#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include "vehicle_controller_contract.hpp"
namespace mgscoop::vehicle_controller_read {
// Pure reads. Scope must be sampled from the coordinator's currently admitted
// paired copied-save scene, exact PE/partner gate, natural thread, current
// ReadState P/Q/S and fresh downed owner generation. No Lua addresses or IDs.
struct Scope {
 bool exactGate=false,admitted=false,ready=false,armed=false,batch=false;
 std::uintptr_t image=0,player=0,data=0,state=0;
 std::uint32_t mission=0,ownedMission=0,generation=0,ownedGeneration=0,thread=0,ownerThread=0;
 std::uint16_t epoch=0,ownedEpoch=0;
 std::uint32_t sequenceBefore=0,sequenceAfter=0;
 std::uint64_t now=0,poll=0;
};
inline bool Range(std::uintptr_t p,std::size_t n) noexcept {return p&&n&&p<=std::numeric_limits<std::uintptr_t>::max()-n;}
inline bool Aligned(std::uintptr_t p,unsigned n) noexcept {return p&&!(p&(n-1));}
template<class Reader,class T> bool Get(Reader& r,std::uintptr_t p,T& out) noexcept {return Range(p,sizeof(T))&&r(p,&out,sizeof(T));}
inline const char* ScopeRefusal(const Scope& s) noexcept {
 if(!s.exactGate)return "vehicle_gate";
 if(!s.admitted||!s.ready||!s.armed)return "scope";
 if(!s.thread||s.thread!=s.ownerThread)return "owner_thread";
 if((s.mission!=30010&&s.mission!=30020&&s.mission!=30050)||!s.epoch||!s.generation||s.mission!=s.ownedMission||s.epoch!=s.ownedEpoch||s.generation!=s.ownedGeneration)return "scene";
 if(!s.poll||s.now<s.poll||s.now-s.poll>750)return "scope_expired";
 if(s.batch)return "batch";
 if((s.sequenceBefore&1u)||s.sequenceBefore!=s.sequenceAfter)return "input_window";
 if(!Range(s.image,0x3000000)||!Range(s.player,0x388)||!Range(s.data,0x1f0)||!Range(s.state,0x48))return "identity";
 return nullptr;
}
inline bool SameScope(const Scope& a,const Scope& b) noexcept {
 return !ScopeRefusal(b)&&a.image==b.image&&a.player==b.player&&a.data==b.data&&a.state==b.state&&a.mission==b.mission&&a.ownedMission==b.ownedMission&&a.epoch==b.epoch&&a.ownedEpoch==b.ownedEpoch&&a.generation==b.generation&&a.ownedGeneration==b.ownedGeneration&&a.thread==b.thread&&a.ownerThread==b.ownerThread&&a.sequenceAfter==b.sequenceBefore&&a.sequenceAfter==b.sequenceAfter;
}
template<class Reader> bool CodeGate(Reader& r,std::uintptr_t base,std::size_t span,std::uintptr_t& mismatch) noexcept {
 mismatch=0;if(!Range(base,span))return false;unsigned char bytes[8192]{};
 for(const auto& s:vehicle_controller_contract::sites){
  if(s.rva>span||s.size>span-s.rva||s.size>sizeof(bytes)||!r(base+s.rva,bytes,s.size)||std::memcmp(bytes,s.bytes,s.size)){mismatch=s.rva;return false;}
 }
 for(const auto& s:vehicle_controller_contract::slots){const auto off=s.vtable_rva+s.offset;std::uintptr_t v=0;
  if(off>span||sizeof(v)>span-off||!Get(r,base+off,v)||v!=base+s.target_rva){mismatch=off;return false;}
 }return true;
}
struct Body {
 std::uintptr_t block=0,primary=0,node=0,storage=0,header=0,record=0,bitmap=0;
 std::uint32_t bitmapValue=0,id=0;std::uint8_t pool=0,part=0;
 unsigned char raw[0x48]{}; // record40..4B and50..87;4C..4F padding is not read
};
struct Observation {
 std::uintptr_t player=0,data=0,state=0,ids=0,pool=0,parts=0,blocks=0,blockTable=0;
 std::uint32_t flags=0;Body body[2];
};
struct BodyReply {
 std::uint16_t actualId=0xffff,vehicle=0xffff,candidate=0xffff;
 std::uint8_t phase=0,nextPhase=0,packedEntryOrSeat=0,entryOrSeat=0;
 bool nodeActive=false;
};
struct Reply {
 bool ready=false;std::uint32_t generation=0;BodyReply body[2];const char* reason="disabled";
 // No occupancy/attachment/camera/control claim. entryOrSeat is record57 low
 // nibble: phase1 values0/1 are entry sides; settled driver phase3 is0.
};
inline std::uint16_t Word(const unsigned char* p) noexcept {std::uint16_t v=0;std::memcpy(&v,p,sizeof(v));return v;}
inline std::uintptr_t Pointer(const unsigned char* p) noexcept {std::uintptr_t v=0;std::memcpy(&v,p,sizeof(v));return v;}
inline std::int32_t Adjustment(const unsigned char* p) noexcept {std::int32_t v=0;std::memcpy(&v,p,sizeof(v));return v;}
inline bool VehicleWord(std::uint16_t v) noexcept {return v==0xffff||(v&0xfe00u)==0x6400u;}
// All record reads require exact final type, current body/node ownership and
// allocator header: stride240, count1, storage270, records=storage+30. Those
// dimensions are derived from1244CB0/12447E0 for a one-body node, not guessed.
template<class Reader> bool ReadOnce(Reader& r,const Scope& s,Observation& out,const char** failure=nullptr) noexcept {
 out={};if(failure)*failure=nullptr;
 const auto fail=[failure](const char* why) noexcept {if(failure)*failure=why;return false;};
 if(const char* why=ScopeRefusal(s))return fail(why);out.player=s.player;out.data=s.data;out.state=s.state;
 std::uintptr_t v=0,q=0,st=0,sync=0;std::uint32_t n=0,realized=0,local=0,selected=0;
 if(!Get(r,s.player,v)||v!=s.image+0x2295490||!Get(r,s.player+0x60,q)||q!=s.data||!Get(r,q,v)||v!=s.image+0x23ad970||!Get(r,q+0x1e8,st)||st!=s.state||!Get(r,st+0x40,n)||n!=2)return fail("player_identity");
 if(!Get(r,s.player+0xa8,sync)||sync||!Get(r,s.player+0x204,out.flags)||!(out.flags&1u)||(out.flags&0x100u)||!Get(r,s.player+0x208,n)||n!=2||!Get(r,s.player+0x20c,realized)||realized!=2||!Get(r,s.player+0x214,local)||local||!Get(r,s.player+0x218,selected)||selected)return fail("player_provider_flags");
 if(!Get(r,s.player+0x70,out.ids)||!Aligned(out.ids,2)||!Get(r,s.player+0x78,out.pool)||!out.pool||!Get(r,s.player+0x90,out.parts)||!out.parts||!Get(r,s.player+0x378,out.blocks)||!Aligned(out.blocks,8)||!Get(r,s.player+0x50,out.blockTable)||!Aligned(out.blockTable,8)||!Range(out.ids,4)||!Range(out.pool,2)||!Range(out.parts,2)||!Range(out.blocks,16*0x6a8)||!Range(out.blockTable,16*8))return fail("body_metadata");
 for(unsigned k=0;k<2;++k){auto& b=out.body[k];std::uint16_t id=0;
  if(!Get(r,out.ids+k*2,id)||id!=k||!Get(r,out.pool+k,b.pool)||b.pool>=16||!Get(r,out.parts+k,b.part)||b.part!=3)return fail(k?"body1_metadata":"body0_metadata");
  b.id=id;b.block=out.blocks+std::uintptr_t(b.pool)*0x6a8;
  std::uintptr_t other=0,owner=0;std::uint32_t one=0,base=0,row=0;
  if(!Get(r,out.blockTable+b.pool*8,other)||other!=b.block||!Get(r,b.block,v)||v!=s.image+0x2295210||!Get(r,b.block+0x138,owner)||owner!=s.player||!Get(r,b.block+8,one)||one!=1||!Get(r,b.block+0xc,base)||base!=id||!Get(r,b.block+0x128,b.primary)||!Aligned(b.primary,8)||!Range(b.primary,0x88))return fail(k?"body1_block_owner":"body0_block_owner");
  if(!Get(r,b.primary,v)||v!=s.image+0x23cbea8||!Get(r,b.primary+0x10,v)||v!=s.image+0x23cbee0||!Get(r,b.primary+0x30,v)||v!=s.image+0x23cbf10||!Get(r,b.primary+0x48,v)||v!=s.image+0x23cbf50||!Get(r,b.primary+8,owner)||owner!=b.block)return fail(k?"body1_controller_owner":"body0_controller_owner");
  if(!Get(r,b.primary+0x38,b.node)||!Aligned(b.node,8)||!Range(b.node,0x40)||!Get(r,b.node,v)||v!=s.image+0x2306628||!Get(r,b.node+0x20,one)||one!=1||!Get(r,b.node+0x24,base)||base!=id||!Get(r,b.node+0x2c,row)||row!=0x23||!Get(r,b.node+0x38,owner)||owner!=b.primary+0x30)return fail(k?"body1_node_owner":"body0_node_owner");
  std::uintptr_t allocation=0,used=0;std::uint16_t alignment=0,allocationFlags=0;std::uint32_t tag=0;
  if(!Get(r,b.primary+0x50,b.storage)||!Aligned(b.storage,16)||!Range(b.storage,0x270)||!Get(r,b.primary+0x58,b.header)||b.header!=b.storage||!Get(r,b.primary+0x60,allocation)||allocation!=0x270||!Get(r,b.primary+0x68,used)||used!=allocation||!Get(r,b.primary+0x70,alignment)||alignment!=16||!Get(r,b.primary+0x72,allocationFlags)||!(allocationFlags&1u)||!Get(r,b.primary+0x74,tag)||tag!=0x1c0048||!Get(r,b.primary+0x78,b.record)||b.record!=b.storage+0x30)return fail(k?"body1_allocation_holder":"body0_allocation_holder");
  std::uintptr_t stride=0,count=0;
  if(!Get(r,b.header,v)||v!=s.image+0x645940||!Get(r,b.header+0x10,other)||other!=b.record||!Get(r,b.header+0x18,stride)||stride!=0x240||!Get(r,b.header+0x20,count)||count!=1)return fail(k?"body1_allocation_header":"body0_allocation_header");
  if(!Get(r,b.node+8,b.bitmap)||!Aligned(b.bitmap,4)||!Get(r,b.bitmap,b.bitmapValue))return fail(k?"body1_active_bitmap":"body0_active_bitmap");
  // Native member-pointer dispatch uses signed32+48; the upper32 at+4C
  // is copied from unspecified stack padding. Never read or compare it.
  if(!Range(b.record+0x40,sizeof(b.raw))||!r(b.record+0x40,b.raw,0x0c)||!r(b.record+0x50,b.raw+0x10,0x38))return fail(k?"body1_record_read":"body0_record_read");
  const unsigned phase=b.raw[0x14],next=b.raw[0x15];const auto callback=Pointer(b.raw);const auto adjust=Adjustment(b.raw+8);
  if(phase>7||next>7||adjust||((callback!=s.image+vehicle_controller_contract::phase_callbacks[phase])&&!(phase==0&&callback==0)))return fail(k?"body1_phase_callback":"body0_phase_callback");
  if(!VehicleWord(Word(b.raw+0x44))||!VehicleWord(Word(b.raw+0x46)))return fail(k?"body1_target_class":"body0_target_class");
 }
 const auto& a=out.body[0];const auto& b=out.body[1];
 if(a.pool==b.pool||a.primary==b.primary||a.node==b.node||a.storage==b.storage||a.record==b.record||a.bitmap==b.bitmap)return fail("body_alias");return true;
}
inline bool Same(const Observation& a,const Observation& b) noexcept {
 if(a.player!=b.player||a.data!=b.data||a.state!=b.state||a.ids!=b.ids||a.pool!=b.pool||a.parts!=b.parts||a.blocks!=b.blocks||a.blockTable!=b.blockTable||a.flags!=b.flags)return false;
 for(unsigned k=0;k<2;++k){const auto& x=a.body[k];const auto& y=b.body[k];
  if(x.block!=y.block||x.primary!=y.primary||x.node!=y.node||x.storage!=y.storage||x.header!=y.header||x.record!=y.record||x.bitmap!=y.bitmap||x.bitmapValue!=y.bitmapValue||x.id!=y.id||x.pool!=y.pool||x.part!=y.part||std::memcmp(x.raw,y.raw,0x0c)||std::memcmp(x.raw+0x10,y.raw+0x10,0x38))return false;
 }return true;
}
// Sample supplies fresh read-only native ownership/window state BEFORE,
// BETWEEN and AFTER the two reads. It may not call any gameplay engine method.
// Reader must be a bounded fault-refusing memory reader (e.g. ReadProcessMemory).
template<class Reader,class Sampler> bool Observe(Reader& r,Sampler& sample,Reply& out) noexcept {
 out={};const auto s=sample();if(const char* why=ScopeRefusal(s)){out.reason=why;return false;}
 Observation a{},b{};const char* failure=nullptr;if(!ReadOnce(r,s,a,&failure)){out.reason=failure?failure:"vehicle_chain";return false;}
 const auto middle=sample();if(!SameScope(s,middle)){out.reason="scope_changed";return false;}
 if(!ReadOnce(r,middle,b,&failure)){out.reason=failure?failure:"vehicle_chain";return false;}
 const auto final=sample();if(!SameScope(s,final)){out.reason="scope_changed";return false;}
 if(!Same(a,b)){out.reason="state_changed";return false;}
 for(unsigned k=0;k<2;++k){const auto& x=b.body[k];auto& y=out.body[k];y.actualId=static_cast<std::uint16_t>(x.id);y.phase=x.raw[0x14];y.nextPhase=x.raw[0x15];y.packedEntryOrSeat=x.raw[0x17];y.entryOrSeat=static_cast<std::uint8_t>(y.packedEntryOrSeat&15u);y.vehicle=Word(x.raw+0x44);y.candidate=Word(x.raw+0x46);y.nodeActive=(x.bitmapValue&1u)!=0;}
 out.generation=s.generation;out.ready=true;out.reason="vehicle_observed";return true;
}
} // namespace mgscoop::vehicle_controller_read
