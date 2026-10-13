#pragma once
#include "mgscoop/native_pose_carry_handshake.hpp"
#include "mgscoop/native_pose_carry_queue.hpp"
namespace mgscoop::partner_input_hook {
// Current execution comes only from job/queue actual selected1 odd writer.
// submission is the immutable successful op0 seal; its old shared ticket,
// thread and sequence are metadata and never current execution authority.
struct NativePoseDeferredCarryContext {
 std::uintptr_t image=0,carrierRecord=0,recipientRecord=0;
 NativePoseJobContext job{};
 NativePoseCarryQueueContext queue{};
 NativePoseSubmittedCarryContext submission{};
 std::uint64_t intentNonce=0,intentAt=0,birth0=0,birth1=0;
 std::uint8_t priorSequence=0;
 bool copyReserved=false; // true only during the exact current core copy TLS
};
// Requires actual Sleep+Carry holds, whole/current dual models+birthed owner,
// neutral owned PP, full current native rest5/Q84/life2/positivehealth,
// exact accepted op0, unqueued intent<=750ms and unchanged recipient priorseq.
// No callable/pointer permission and no old shared scope projection.
bool ReadNativePoseDeferredCarryRequest(NativePoseDeferredCarryContext&) noexcept;
using NativePoseDeferredCarryValidator=bool(*)(const sleep_partner_test::CarryLeaseProof&) noexcept;
bool BindNativePoseDeferredCarryValidator(NativePoseDeferredCarryValidator) noexcept;
// Actual installed FE00D0 setter/full code/IAT+Q/origin receipts only.
// Does not authorize a copied request without fresh per-call getter+validator.
bool NativePoseDeferredCarryHooksLive() noexcept;
// Worker-only retained-original copy. False preflight makes no native write;
// after reservation any refusal faults privately and keeps every held owner.
bool CopyNativePoseDeferredCarryRequest() noexcept;
} // namespace mgscoop::partner_input_hook
