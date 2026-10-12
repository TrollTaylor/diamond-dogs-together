#pragma once
#include "mgscoop/native_pose_dual_parts.hpp"
#include "native_pose_parts_diagnostic.hpp"
#include "native_pose_carry_participants.hpp"
#include "native_pose_birth.hpp"
namespace mgscoop::dual_parts_core_policy {
using Context=partner_input_hook::NativePoseDualPartsContext;
// The Closed job marker/context is retained history. Only actual currently
// opened/owned writer domains can conflict with the real even shared ticket.
inline bool SharedDomains(bool bodyOpened,bool bodyWriterOwned,bool localWriterOpened) noexcept {
 return !bodyOpened&&!bodyWriterOwned&&!localWriterOpened;
}
inline bool BirthsCurrent(const native_pose_carry_participants::Pair& pair,const native_pose_birth::Receipt& a,
 const native_pose_birth::Receipt& b,std::uint64_t bound0,std::uint64_t bound1) noexcept {
 return bound0&&bound1&&bound0!=bound1&&a.serial==bound0&&b.serial==bound1&&
 native_pose_birth::Current(a,{pair.player,pair.body[0].block,pair.body[0].state,pair.body[0].cs,pair.body[0].entry})&&
 native_pose_birth::Current(b,{pair.player,pair.body[1].block,pair.body[1].state,pair.body[1].cs,pair.body[1].entry});
}
inline bool NoWork(const native_pose_parts_diagnostic::Witness& w) noexcept {
 return w.count==2&&w.realized==2&&w.local==0&&w.selected==0&&w.actualId[0]==0&&w.actualId[1]==1&&
 w.pool[0]<16&&w.pool[1]<16&&w.pool[0]!=w.pool[1]&&w.blockMatched[0]&&w.blockMatched[1]&&w.parts[0]==3&&w.parts[1]==3&&
 ((w.mask180|w.mask184)&3u)==0&&(w.flags&1u)&&!(w.flags&0x100u)&&(w.flags&0x6000u)==0x2000u;
}
// Values originate only from the real shared getter. This final bounded shape
// check does not manufacture a ticket, native caller or immutable sleep cycle.
inline bool ContextValid(const Context& c) noexcept {
 if(!c.image||!c.player||c.player<0x20||c.go!=c.player-0x20||!c.data||!c.state||!c.vars||
 !c.generation||!c.sceneEpoch||!c.thread||!c.sequence||(c.sequence&1u)||c.callerRva!=0x9c2a2a||
 !c.sharedSerial||!c.carrySerial||c.purposes!=3||c.pending||c.dirty||!c.local0ProfileCurrent||
 !(c.flags&1u)||(c.flags&0x100u)||(c.flags&0x6000u)!=0x2000u||(c.profileRefresh&1u))return false;
 for(unsigned k=0;k<2;++k)if(!c.block[k]||!c.ps[k]||!c.modelCS[k]||!c.modelEntry[k]||!c.birthSerial[k]||c.pool[k]>=16||c.status[k]!=3)return false;
 return c.block[0]!=c.block[1]&&c.ps[0]!=c.ps[1]&&c.modelCS[0]!=c.modelCS[1]&&c.modelEntry[0]!=c.modelEntry[1]&&
 c.birthSerial[0]!=c.birthSerial[1]&&c.pool[0]!=c.pool[1];
}
}
