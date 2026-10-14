#pragma once
// Pure current Player/Horse relationship census. No engine calls, writes,
// requests, hook installation, attachment success or native-call authority.
#include "horse_player_cargo_bridge_read.hpp"
#include "horse_cargo_native_read.hpp"
#include "mgscoop/native_pose_window.hpp"
#include "horse_cargo_census_contract.hpp"
namespace mgscoop::horse_cargo_pair_read {
namespace cr=carry_plugin_read;namespace pc=horse_player_cargo_bridge_read;
namespace hn=horse_cargo_native_read;namespace hk=partner_input_hook;
struct Current {
 cr::Scope carry{};hk::NativePoseBirthCensus births{};hn::Census horse{};
 std::uintptr_t buddySelection=0;std::uint8_t activeBuddy=0;std::uint32_t loadingGeneration=0;
};
struct Relation {
 bool slot=false,horseMatrix=false,oldCarryLink=false;
};
struct Observation {
 pc::Observation player{};hn::Observation horse{};Relation body[2]{};
 std::uint64_t birth[2]{};std::uint32_t window=0;
};
template<class Reader>bool CodeGate(Reader& r,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch) noexcept {
 mismatch=0;unsigned char bytes[256]{};
 if(!cr::Range(image,span))return false;
 for(const auto& p:horse_cargo_census_contract::sites){
  if(p.rva>span||p.size>span-p.rva){mismatch=p.rva;return false;}
  for(std::size_t j=0;j<p.size;){const auto n=p.size-j<sizeof(bytes)?p.size-j:sizeof(bytes);
   if(!r(image+p.rva+j,bytes,n)||std::memcmp(bytes,p.bytes+j,n)){mismatch=p.rva;return false;}j+=n;
  }
 }
 return cr::CodeGate(r,image,span,mismatch)&&carry_recipient_read::CodeGate(r,image,span,mismatch)&&
  horse_player_physical_read::CodeGate(r,image,span,mismatch)&&horse_physical_profile_read::CodeGate(r,image,span,mismatch)&&
  pc::CodeGate(r,image,span,mismatch)&&hn::CodeGate(r,image,span,mismatch);
}
inline const char* Refusal(const Current& c) noexcept {
 if(const char* why=cr::ScopeRefusal(c.carry))return why;
 const auto& b=c.births;const auto& s=c.carry;
 if(b.image!=s.image||b.player!=s.player||b.data!=s.data||b.state!=s.state||b.thread!=s.thread||
    b.sequence!=s.sequenceBefore||!b.lifecycle||!b.generation||!b.sceneEpoch)return "cargo_birth_scope";
 if(!b.birth[0]||!b.birth[1]||b.birth[0]==b.birth[1])return "cargo_birth_identity";
 for(unsigned k=0;k<2;++k)if(!b.block[k]||!b.ps[k]||!b.cs[k]||!b.entry[k])return "cargo_birth_identity";
 if(!c.buddySelection||c.activeBuddy!=1||!c.loadingGeneration)return "cargo_buddy_selection";
 return nullptr;
}
inline bool SameCurrent(const Current& a,const Current& b) noexcept {
 if(Refusal(a)||Refusal(b))return false;
 const auto& x=a.carry;const auto& y=b.carry;
 if(x.image!=y.image||x.player!=y.player||x.data!=y.data||x.state!=y.state||
    x.mission!=y.mission||x.ownedMission!=y.ownedMission||x.epoch!=y.epoch||x.ownedEpoch!=y.ownedEpoch||
    x.generation!=y.generation||x.ownedGeneration!=y.ownedGeneration||x.thread!=y.thread||x.ownerThread!=y.ownerThread||
    x.sequenceBefore!=y.sequenceBefore||x.sequenceAfter!=y.sequenceAfter||
    a.buddySelection!=b.buddySelection||a.activeBuddy!=b.activeBuddy||a.loadingGeneration!=b.loadingGeneration||!hn::SameSource(a.horse,b.horse))return false;
 const auto& p=a.births;const auto& q=b.births;
 if(p.image!=q.image||p.player!=q.player||p.data!=q.data||p.state!=q.state||p.lifecycle!=q.lifecycle||
    p.generation!=q.generation||p.sceneEpoch!=q.sceneEpoch||p.thread!=q.thread||p.sequence!=q.sequence)return false;
 for(unsigned k=0;k<2;++k)if(p.birth[k]!=q.birth[k]||p.block[k]!=q.block[k]||p.ps[k]!=q.ps[k]||p.cs[k]!=q.cs[k]||p.entry[k]!=q.entry[k])return false;
 return true;
}
template<class Reader>bool ReadOnce(Reader& r,const Current& c,std::uint16_t horseId,Observation& out,const char*& why) noexcept {
 out={};why=Refusal(c);if(why)return false;
 if(!pc::ReadOnce(r,c.carry,out.player,why)||!hn::ReadOnce(r,c.carry.image,c.horse,horseId,out.horse,why))return false;
 for(unsigned k=0;k<2;++k){const auto& p=out.player;const auto& births=c.births;
  if(births.block[k]!=p.recipients.carrier.body[k].block||births.ps[k]!=p.recipients.carrier.body[k].ps||
     births.cs[k]!=p.physical.body[k].cs||births.entry[k]!=p.physical.body[k].entry){why="cargo_birth_current";return false;}
  auto& b=out.body[k];const auto& rec=p.recipients.recipient[k];
  b.slot=out.horse.occupied==1&&out.horse.cargo==k&&out.horse.sdCargo==k;
  b.horseMatrix=b.slot&&out.horse.matrixFinite&&
   !std::memcmp(out.horse.matrix,p.physical.body[k].matrix,sizeof(out.horse.matrix));
  b.oldCarryLink=rec.active||rec.endPending||rec.resource||rec.carrierRecord||rec.carrier!=0xffff;
  out.birth[k]=births.birth[k];
 }
 out.window=c.carry.sequenceBefore;why="cargo_pair_observed";return true;
}
inline bool Same(const Observation& a,const Observation& b) noexcept {
 if(!pc::Same(a.player,b.player)||!hn::Same(a.horse,b.horse)||a.window!=b.window)return false;
 for(unsigned k=0;k<2;++k)if(a.birth[k]!=b.birth[k]||a.body[k].slot!=b.body[k].slot||
  a.body[k].horseMatrix!=b.body[k].horseMatrix||a.body[k].oldCarryLink!=b.body[k].oldCarryLink)return false;
 return true;
}
// source must capture a fresh genuine private carry/scene/generation/buddy
// source plus cold birth receipt. Fresh WindowSeq/InBatch and current scope
// are required at each capture, including the final one after all reads.
template<class Reader,class Source>bool Observe(Reader& r,Source&& source,std::uint16_t horseId,Observation& out,const char*& why) noexcept {
 out={};Current first{},middle{},last{};Observation a{},b{};
 if(!source(first,why)||!ReadOnce(r,first,horseId,a,why)){out=a;return false;}
 if(!source(middle,why))return false;
 if(!SameCurrent(first,middle)){why="cargo_owner_changed";return false;}
 if(!ReadOnce(r,middle,horseId,b,why)){out=b;return false;}
 if(!Same(a,b)){why="cargo_pair_changed";return false;}
 if(!source(last,why))return false;
 if(!SameCurrent(middle,last)){why="cargo_owner_changed_final";return false;}
 out=b;why="cargo_pair_observed";return true;
}
inline bool Line(const Observation& a,bool ready,const char* why,char* out,std::size_t capacity) noexcept {
 if(!out||!capacity)return false;
 // bN = action, recipient phase, reciprocal carry, original carrier-bone
 // matrix equality, actual horse slot, horse/physical matrix equality,
 // old carry link. None of these is a native loaded/follow/writer receipt.
 const auto& p=a.player;const auto& x=a.body[0];const auto& y=a.body[1];
 const int n=std::snprintf(out,capacity,"cargoPair;ok=%u;h=%u,%u,%u;b0=%x,%u,%u,%u,%u,%u,%u;b1=%x,%u,%u,%u,%u,%u,%u;win=%u;why=%s",ready?1u:0u,
  unsigned(a.horse.gameId),unsigned(a.horse.cargo),unsigned(a.horse.occupied),
  p.body[0].action,unsigned(p.recipients.recipient[0].current),p.body[0].currentCarry?1u:0u,p.body[0].matrixMatches?1u:0u,x.slot?1u:0u,x.horseMatrix?1u:0u,x.oldCarryLink?1u:0u,
  p.body[1].action,unsigned(p.recipients.recipient[1].current),p.body[1].currentCarry?1u:0u,p.body[1].matrixMatches?1u:0u,y.slot?1u:0u,y.horseMatrix?1u:0u,y.oldCarryLink?1u:0u,
  a.window,why?why:"unknown");
 return n>=0&&static_cast<std::size_t>(n)<capacity;
}
}
