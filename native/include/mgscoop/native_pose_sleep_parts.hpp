#pragma once
#include <cstdint>
namespace mgscoop::partner_input_hook {
// Actual shared, even, selected0 appearance-poll receipt. No job/window or
// mutation permission is projected from this internal read-only interface.
struct NativePoseSleepPartsContext {
 std::uintptr_t image=0,player=0,go=0,data=0,state=0,block=0,ps=0,modelCS=0,modelEntry=0;
 std::uint32_t generation=0,sceneEpoch=0,thread=0,sequence=0,callerRva=0;
 std::uint64_t birthSerial=0,sharedSerial=0;
 std::uint32_t flags=0,remotePending=0,remoteStatus=0,remotePool=0;
};
using NativePosePartsPollValidator=bool(*)() noexcept;
bool RegisterNativePosePartsPollValidator(NativePosePartsPollValidator) noexcept;
bool ReadNativePoseSleepPartsPoll(NativePoseSleepPartsContext&) noexcept;
}
