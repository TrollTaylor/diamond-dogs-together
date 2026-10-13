#pragma once
#include "mgscoop/sleep_partner_test.hpp"
namespace mgscoop::partner_input_hook {
// Read only inside the actual installed11268B0 op0 hook's own real shared
// ticket. No old exclusive Scope is projected into this natural tick.
struct NativePoseSubmittedCarryContext {
 std::uintptr_t image=0,player=0,go=0,data=0,block1=0,state1=0,modelCS1=0,modelEntry1=0;
 std::uint32_t armGeneration=0,armEpoch=0,thread=0,sequence=0,handlerRva=0;
 std::uint64_t birth1=0,carrierBirth0=0,carrySerial=0,sharedSerial=0,submitNonce=0,submittedAt=0;
 bool sleepHeld=false,carryHeld=false;
 sleep_partner_test::CarryLeaseProof submittedSleep{};
};
bool ReadNativePoseSubmittedCarryHandshake(NativePoseSubmittedCarryContext&) noexcept;
// Immutable passive binding before worker opt-in. A registered validator is
// not call permission; it must requery this actual current hook receipt.
using NativePoseSubmittedCarryValidator=bool(*)(const sleep_partner_test::CarryLeaseProof&) noexcept;
bool BindNativePoseSubmittedCarryValidator(NativePoseSubmittedCarryValidator) noexcept;
bool NativePoseCarrySenderHooksLive() noexcept; // actual installed two-hook receipts/fullcode only

} // namespace mgscoop::partner_input_hook
