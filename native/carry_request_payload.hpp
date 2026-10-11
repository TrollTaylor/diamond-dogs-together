#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
namespace mgscoop::carry_request_payload {
// Pure caller-owned payload construction only. This does not authorize native
// invocation, bypass admission, change a Player action, or claim attachment.
// Root must separately qualify exact PE/typed owners, node/lifetime identity,
// serialized window, paired incapacitation and the complete native drop cycle.
inline constexpr std::uintptr_t kRecipientVtableRva=0x23aa950;
inline constexpr std::uint64_t kCarrierMessageHash=0xfaa2aaf9cd28ull;
inline constexpr std::uint64_t kCarrierMessageSecondary=0xdb0d91c47faull;
inline constexpr std::uint64_t kHandshakeHash=0x72bc35bcdbdull;
inline constexpr std::uint64_t kDropLuaHash=0xf6887344966dull;
inline constexpr std::uint32_t kCarrierPsMask=1u<<22;
inline constexpr std::uint32_t kDropPsMask=0x4000u;
struct alignas(8) CarrierTargetMessage {
 std::uint64_t type=0,secondary=0;
 std::uint16_t target=0;
 std::uint8_t reserved[6]{};
};
struct alignas(8) RecipientRequest {
 std::uintptr_t vtable=0;
 std::uint8_t kind=0,reserved08[7]{};
 std::uint8_t substate=0,sequence=0,force=0,reserved13=0;
 std::uint16_t carrier=0,reserved16=0;
 float additional=0;
 std::uint32_t reserved1c=0;
};
static_assert(sizeof(std::uintptr_t)==8,"retail Windows x64 payload");
static_assert(std::is_trivially_copyable_v<CarrierTargetMessage> && sizeof(CarrierTargetMessage)==24);
static_assert(offsetof(CarrierTargetMessage,type)==0 && offsetof(CarrierTargetMessage,secondary)==8 && offsetof(CarrierTargetMessage,target)==0x10);
static_assert(std::is_trivially_copyable_v<RecipientRequest> && sizeof(RecipientRequest)==32);
static_assert(offsetof(RecipientRequest,vtable)==0 && offsetof(RecipientRequest,kind)==8 && offsetof(RecipientRequest,substate)==0x10 && offsetof(RecipientRequest,sequence)==0x11 && offsetof(RecipientRequest,force)==0x12 && offsetof(RecipientRequest,carrier)==0x14 && offsetof(RecipientRequest,additional)==0x18);
inline bool PairedBodyIds(std::uint32_t carrier,std::uint32_t recipient) noexcept {return carrier<2&&recipient<2&&carrier!=recipient;}
inline bool ImageSpan(std::uintptr_t image,std::size_t span) noexcept {
 return image&&!(image&7u)&&span&&image<=std::numeric_limits<std::uintptr_t>::max()-span && kRecipientVtableRva<span && sizeof(std::uintptr_t)<=span-kRecipientVtableRva;
}
// Exact stock sequence producer11269F4..1126A06 increments a byte and rolls
// values above0xFA back to0. This includes the initial0xFF ->0 case.
inline std::uint8_t NextSequence(std::uint8_t previous) noexcept {
 const auto next=static_cast<std::uint8_t>(previous+1u);return next>0xfau?0:next;
}
inline bool BuildCarrierTarget(std::uint32_t carrier,std::uint32_t recipient,CarrierTargetMessage& out) noexcept {
 out={};if(!PairedBodyIds(carrier,recipient))return false;
 out.type=kCarrierMessageHash;out.secondary=kCarrierMessageSecondary;out.target=static_cast<std::uint16_t>(recipient);return true;
}
// This is a single initial recipient request. The force byte must not be
// resubmitted every tick. Offline init11265E0 always selects native substate1;
// setting request.substate to4 is NOT a qualified put-down request.
inline bool BuildRecipient(std::uintptr_t image,std::size_t span,std::uint32_t carrier,std::uint32_t recipient,std::uint8_t previous,RecipientRequest& out) noexcept {
 out={};if(!PairedBodyIds(carrier,recipient)||!ImageSpan(image,span))return false;
 out.vtable=image+kRecipientVtableRva;out.kind=15;out.sequence=NextSequence(previous);out.force=1;out.carrier=static_cast<std::uint16_t>(carrier);return true;
}
// Exact offline init branch only. Caller admission and native references remain
// separate; this predicate is not a runtime eligibility/attachment assertion.
inline bool OfflineInitConsumes(bool offline,std::uint8_t kind,std::uint8_t currentSequence,std::uint8_t requestedSequence,std::uint8_t force) noexcept {
 return offline&&kind==15&&(currentSequence!=requestedSequence||force!=0);
}
// Native target message enum0's state guard11268B0. For the offline-owned
// carrier (+85 !=0), sleeping native actions83/84 are required. A prone
// fallback action1D cannot pass just because the mod says "asleep".
inline bool TargetReceiverAcceptsState(std::uint16_t action,bool carrierOfflineOwned,std::uint8_t dropCurrent,std::uint8_t dropNext) noexcept {
 if(carrierOfflineOwned&&action!=0x83&&action!=0x84)return false;
 return action!=0x2b||(dropCurrent==0&&dropNext==0);
}
inline bool NativeDropCarrierPresent(std::uint32_t psFlags) noexcept {return (psFlags&kCarrierPsMask)!=0;}
} // namespace mgscoop::carry_request_payload
