#pragma once
#include "mgscoop/native_pose_carry_handshake.hpp"
namespace mgscoop::partner_input_hook {
// Actual natural carrier0 op2 after genuine accepted op0, before deferred kind15.
// Execution derives only from this current even OwnsRead ticket/sender TLS;
// prior original op0 submission is immutable metadata, never projected scope.
struct NativePoseCarryOp2Context {
 std::uintptr_t image=0,player=0,go=0,data=0,carrierRecord=0,message=0,result=0,service=0;
 std::uintptr_t block[2]{},state[2]{},modelCS[2]{},modelEntry[2]{};
 NativePoseSubmittedCarryContext submission{};
 std::uint64_t intentNonce=0,intentAt=0,birth0=0,birth1=0,readSerial=0;
 std::uint32_t thread=0,sequence=0,actual=1;
 bool batch=false;
};
bool ReadNativePoseCarryOp2Origin(NativePoseCarryOp2Context&)noexcept;
using NativePoseCarryOp2Validator=bool(*)(const sleep_partner_test::CarryLeaseProof&)noexcept;
bool BindNativePoseCarryOp2Validator(NativePoseCarryOp2Validator)noexcept;
using NativePoseCarryOp2TreeValidator=bool(*)(const NativePoseCarryOp2Context&)noexcept;
bool BindNativePoseCarryOp2TreeValidator(NativePoseCarryOp2TreeValidator)noexcept;
} // namespace mgscoop::partner_input_hook
