#pragma once
// Passive original-dispatch observation only; not a Cargo purpose or writer.
#include "mgscoop/native_pose_window.hpp"
namespace mgscoop::partner_input_hook {
struct NativeHorseFlowOwner {
 NativePoseBirthCensus births{};
 std::uintptr_t carrierRecord[2]{},recipientRecord[2]{};
 std::uint64_t sharedSerial=0,at=0;
};
struct NativeHorseFlowCall {
 std::uintptr_t caller=0,service=0,message=0,result=0,returned=0;
 std::uint32_t target=UINT32_MAX;
};
struct NativeHorseFlowCallbacks {
 void(*before)(const NativeHorseFlowOwner&,const NativeHorseFlowCall&) noexcept=nullptr;
 void(*after)(const NativeHorseFlowOwner*,const NativeHorseFlowCall&) noexcept=nullptr;
 // Raw filtered call metadata only; absence of an owner is explicit and
 // never becomes a current slot/recipient observation.
 void(*refused)(const NativeHorseFlowCall&) noexcept=nullptr;
};
// Core owns BF3A80 already. One immutable process-lifetime passive observer;
// callbacks run only around the untouched stock route under its real ticket.
bool RegisterNativeHorseFlowCallbacks(const NativeHorseFlowCallbacks&) noexcept;
}
