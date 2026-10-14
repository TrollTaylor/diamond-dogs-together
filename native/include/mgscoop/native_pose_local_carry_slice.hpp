#pragma once
#include "mgscoop/native_pose_window.hpp"
namespace mgscoop::partner_input_hook {
// Read only by the actual separate exclusive carrier slice. These are current
// identities, not caller-manufactured admission flags or cached status.
struct NativePoseLocalCarryContext {
 std::uintptr_t image=0,player=0,go=0,data=0,block1=0,state1=0,modelCS1=0,modelEntry1=0;
 std::uint32_t armGeneration=0,armEpoch=0,thread=0,sequence=0;
 std::uint64_t birth1=0,carrierBirth0=0,sliceNonce=0,carrySerial=0;
 std::uint32_t realLocal=0,selected=0;bool sleepHeld=false;
 NativePoseCarryLease carryLease{};
};
// Returns true only while this thread owns the distinct actual Gate writer,
// both retained participant births/models remain current, P214/P218 are both0,
// previous body1 Closed was clean and all mandatory hooks remain owned.
// It never changes selection, supplies a caller bool override or calls a model.
bool ReadNativePoseLocalCarrySlice(NativePoseLocalCarryContext&) noexcept;
// Exact pre/post receipt equality after the actual installed-domain getter.
// Equality alone never authorizes entering a slice or issuing a request.
inline bool SameNativePoseLocalCarrySlice(const NativePoseLocalCarryContext& a,const NativePoseLocalCarryContext& b) noexcept {
 return a.image==b.image&&a.player==b.player&&a.go==b.go&&a.data==b.data&&a.block1==b.block1&&a.state1==b.state1&&
  a.modelCS1==b.modelCS1&&a.modelEntry1==b.modelEntry1&&a.armGeneration==b.armGeneration&&a.armEpoch==b.armEpoch&&
  a.thread==b.thread&&a.sequence==b.sequence&&a.birth1==b.birth1&&a.carrierBirth0==b.carrierBirth0&&
  a.sliceNonce==b.sliceNonce&&a.carrySerial==b.carrySerial&&a.realLocal==b.realLocal&&a.selected==b.selected&&
  a.sleepHeld==b.sleepHeld&&a.carryLease.serial==b.carryLease.serial&&a.carryLease.generation==b.carryLease.generation&&
  a.carryLease.sceneEpoch==b.carryLease.sceneEpoch;
}
using NativePoseLocalCarryFn=bool(*)(const NativePoseLocalCarryContext&,void*) noexcept;
// Worker-only, exact dual Carry lease, immediately after the clean body1 Closed.
// Callback runs under the separate actual writer with local0 word unshadowed.
// Callback true means it completed without a partial native failure. A no-op
// preflight refusal may return true and report its structured result through
// the argument. Callback false/exception is never treated as clean completion.
// Failed callback/readback/release preserves all participant holds and refuses
// privately. Native Start remains separately typed/reviewed and OFF.
bool RunNativePoseLocalCarrySlice(const NativePoseCarryLease&,NativePoseLocalCarryFn,void*) noexcept;
} // namespace mgscoop::partner_input_hook
