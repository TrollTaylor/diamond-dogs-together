#pragma once
#include "mgscoop/native_pose_carry_deferred.hpp"
namespace mgscoop::partner_input_hook {
// Actual current natural11285D0->BF3A80 op1 under owned selected1 odd job.
// The prior submission ticket/thread remains metadata; never current scope.
struct NativePoseQueuedCarryContext {
 std::uintptr_t image=0,carrierRecord=0,recipientRecord=0,message=0,result=0,service=0;
 NativePoseJobContext job{};NativePoseCarryQueueContext queue{};
 NativePoseSubmittedCarryContext submission{};
 std::uint64_t intentNonce=0,intentAt=0,birth0=0,birth1=0;
 std::uint8_t priorSequence=0,current=0,next=0;
 std::uint16_t nativeAction=0;
 bool acknowledged=false; // only actual completed retained native reply
};
// Exact owned natural op1 source, queued ledger, full kind15force0 copy,
// actual active recipient1 state1/2/3, positive-health/life2 +sleepmode5,
// current typed model/graph, target0 and P238==retained current body0CS.
// This is neither physical-attachment nor permission for general dispatch.
bool ReadNativePoseQueuedCarryHandshake(NativePoseQueuedCarryContext&)noexcept;
using NativePoseQueuedCarryValidator=bool(*)(const sleep_partner_test::CarryLeaseProof&)noexcept;
bool BindNativePoseQueuedCarryValidator(NativePoseQueuedCarryValidator)noexcept;
// Tree gate must derive the actual current typed Player tree and pin every
// visited handler. Missing/unknown handler fails; it may not trust scope flags.
using NativePoseCarryTreeValidator=bool(*)(const NativePoseQueuedCarryContext&)noexcept;
bool BindNativePoseCarryTreeValidator(NativePoseCarryTreeValidator)noexcept;
} // namespace mgscoop::partner_input_hook
