#pragma once
#include <cstdint>
namespace mgscoop::partner_input_hook {
// Cold worker-only diagnostics, never permission to acquire/submit/drop.
// Native pointers and callable addresses are not exported.
struct NativePoseCarryColdStatus {
 std::uint32_t sequence=0,local=0,selected=0,routeType=0,registryType=0,routeMode=0,routeFlags=0,routeCount=0;
 std::uint64_t routePending=0;
 std::uint32_t messageType=0,messageKey=0,messageEnabled=0,word=0xffff;
 bool routeReady=false,routeNoWork=false,messageReady=false,varsReady=false;
 bool varsDomain=false,messageDomain=false,routeDomain=false,localSliceActive=false;
 bool profileReady=false,profileCurrent=false;
 std::uint32_t profileReason=0,profileType=0,profileParts=0,profileCamo=0,profileAux=0,profileFace=0,profileRefresh=0;
 std::uint32_t currentType=0,currentParts=0,currentCamo=0,currentEncoded=0,currentAux=0,currentFace=0,profileSelector=0;
};
// Requires actual copied-save worker/current thread/mandatory healthy scope,
// no native Sleep/Carry hold or pendingclear, and its own real shared ticket.
// Two coherent snapshots and unchanged current birth epoch/Arm are checked.
// It reports cold observations only. localSliceActive isfalse outside a slice.
bool ReadNativePoseCarryColdStatus(NativePoseCarryColdStatus&) noexcept;
}
