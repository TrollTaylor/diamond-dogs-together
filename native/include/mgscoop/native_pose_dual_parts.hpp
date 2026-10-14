#pragma once
#include <cstdint>
namespace mgscoop::partner_input_hook {
// Internal current receipt from the real t_parts shared ticket. No caller
// supplies a Scope, cached Active flag, selected-body override or writer proof.
struct NativePoseDualPartsContext {
 std::uintptr_t image=0,player=0,go=0,data=0,state=0,vars=0;
 std::uintptr_t block[2]{},ps[2]{},modelCS[2]{},modelEntry[2]{};
 std::uint32_t generation=0,sceneEpoch=0,thread=0,sequence=0,callerRva=0,purposes=0,pending=0;
 std::uint64_t birthSerial[2]{},sharedSerial=0,carrySerial=0;
 std::uint32_t flags=0,dirty=0,pool[2]{},status[2]{};
 // True is established by the exact typed desired/current local0 reader only.
 // Module validators must independently obtain this real receipt; equality
 // and this field alone never authorize native calls or lifetime release.
 bool local0ProfileCurrent=false;
 std::uint8_t profileType=0,profileParts=0,profileCamo=0,profileEncoded=0,profileAux=0;
 std::uint16_t profileFace=0,profileSelector=0;
 std::uint32_t profileRefresh=0;
};
bool ReadNativePoseDualPartsPoll(NativePoseDualPartsContext&) noexcept;
inline bool SameNativePoseDualPartsPoll(const NativePoseDualPartsContext& a,const NativePoseDualPartsContext& b) noexcept {
 if(a.image!=b.image||a.player!=b.player||a.go!=b.go||a.data!=b.data||a.state!=b.state||a.vars!=b.vars||
 a.generation!=b.generation||a.sceneEpoch!=b.sceneEpoch||a.thread!=b.thread||a.sequence!=b.sequence||a.callerRva!=b.callerRva||
 a.purposes!=b.purposes||a.pending!=b.pending||a.sharedSerial!=b.sharedSerial||a.carrySerial!=b.carrySerial||
 a.flags!=b.flags||a.dirty!=b.dirty||a.local0ProfileCurrent!=b.local0ProfileCurrent||
 a.profileType!=b.profileType||a.profileParts!=b.profileParts||a.profileCamo!=b.profileCamo||a.profileEncoded!=b.profileEncoded||a.profileAux!=b.profileAux||
 a.profileFace!=b.profileFace||a.profileSelector!=b.profileSelector||a.profileRefresh!=b.profileRefresh)return false;
 for(unsigned k=0;k<2;++k)if(a.block[k]!=b.block[k]||a.ps[k]!=b.ps[k]||a.modelCS[k]!=b.modelCS[k]||a.modelEntry[k]!=b.modelEntry[k]||
 a.birthSerial[k]!=b.birthSerial[k]||a.pool[k]!=b.pool[k]||a.status[k]!=b.status[k])return false;
 return true;
}
// Root registers one immutable composed parts validator before opt-in through
// the existing RegisterNativePosePartsPollValidator. Sleep-only route is kept.
// A separate actual Sleep/carry module cycle provider remains mandatory;
// this core receipt does not grant a prepare/submit/bound/retired label.
}
