#pragma once
#include <cstddef>
#include <cstdint>
#include "mgscoop/native_pose_window.hpp"
namespace mgscoop::sleep_partner_test {
// Private/copied-save experiment only. This interface makes no production
// downed/network promise. Target must be accepted fresh native/Lua peer state.
struct Target {
 bool pairedReady=false,fieldReady=false,demo=false,stopping=false,asleep=false;
 std::uint32_t mission=0,peerGeneration=0,armGeneration=0,armEpoch=0;
 std::uint16_t worldEpoch=0;std::uint64_t poll=0;
};
using LogFn=void(*)(const char*) noexcept;
bool CodeGate(const void* image,std::size_t span,std::uintptr_t& mismatch) noexcept;
bool Configure(const void* image,std::size_t span,bool exactRetailAndPartnerGate,
               const partner_input_hook::NativePoseWorkerBindings&,LogFn) noexcept;
void BindLifetimeQuery(bool(*actualWholePlayerDomainLive)() noexcept) noexcept;
void BindCarryHeldQuery(bool(*actualCarryRecipientHeld)() noexcept) noexcept;
bool PublishTarget(const Target&) noexcept; // fresh natural owner only, no retail call
bool Enable(bool) noexcept; // disable queues wake, never discards lease
bool RequestWake() noexcept; // queues; strict current odd-window CAS only
bool Before(const partner_input_hook::NativePoseJobContext&) noexcept;
bool After(const partner_input_hook::NativePoseJobContext&) noexcept;
void Closed(const partner_input_hook::NativePoseJobContext&,bool cleanupComplete) noexcept;
bool AllowPartsPoll() noexcept; // actual shared/even getter + fresh retained body1 read; no mutation/lease refresh
bool SuppressForcedStance() noexcept; // root/driver uses while preparing/leased; existing normal source behavior otherwise
struct Status {
 bool configured=false,enabled=false,active=false,held=false,wakePending=false,clearPending=false;
 std::uint32_t phase=0,mode=0,lifeState=0,qAction=0,stance=0,computedButtons=0,zero=0;
 std::uint32_t peerGeneration=0,window=0,requests=0;
 std::uint64_t birthSerial=0;const char* reason="off";
 bool stanceObserved=false;std::uint32_t psPacked=0,qStance=0,psAction=0;std::uint64_t stanceAt=0;
};
Status GetStatus() noexcept; // bounded cache, never raw pointers
}
