#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "carry_owned_handshake.hpp"
namespace mgscoop::carry_request_retirement {
// Pure post-retirement packet policy. No game reads, calls, or writes. This
// cannot end an active carrier/recipient phase or replace native cancellation.
inline constexpr std::uintptr_t kNeutralVtableRva=0x23ab570;
struct alignas(8) NeutralRequest {
 std::uintptr_t vtable=0;
 std::uint8_t kind=0,reserved[7]{};
};
static_assert(sizeof(NeutralRequest)==16&&offsetof(NeutralRequest,kind)==8);
struct Identity {
 carry_owned_handshake::Lease lease{};
 std::uintptr_t queue=0,array=0,slot=0;
};
inline bool Valid(const Identity& i) noexcept {
 if(!carry_owned_handshake::ValidLease(i.lease)||!i.queue||!i.array||!i.slot||(i.queue&7u)||(i.array&15u)||(i.slot&15u))return false;
 const auto offset=static_cast<std::uintptr_t>(i.lease.recipient)*0x40u;
 return i.array<=UINTPTR_MAX-offset&&i.slot==i.array+offset&&i.slot<=UINTPTR_MAX-0x40u;
}
inline bool Same(const Identity& a,const Identity& b) noexcept {
 return carry_owned_handshake::SameLease(a.lease,b.lease)&&a.queue==b.queue&&a.array==b.array&&a.slot==b.slot;
}
struct Scope {
 Identity expected{},current{};
 carry_request_payload::RecipientRequest ours{},observed{};
 bool exactGate=false,serializedRecipientWindow=false,typedAllocation=false,coherentNativeStates=false;
 bool recipientActive=false,recipientEndPending=false,carrierActive=false,carrierEndPending=false;
 std::uintptr_t recipientCarrierRecord=0;
 std::uint16_t recipientCarrier=0xffff,carrierOwner=0xffff;
};
enum class Reason { scope,identity,payload,active,plan };
inline bool Canonical(std::uintptr_t image,std::size_t span,const Scope& s) noexcept {
 if(!carry_request_payload::ImageSpan(image,span)||kNeutralVtableRva>=span||sizeof(std::uintptr_t)>span-kNeutralVtableRva)return false;
 const auto& q=s.ours;
 if(q.sequence>0xfa)return false;
 carry_request_payload::RecipientRequest canonical{};
 canonical.vtable=image+carry_request_payload::kRecipientVtableRva;canonical.kind=15;canonical.sequence=q.sequence;canonical.carrier=s.expected.lease.carrier;
 return std::memcmp(&q,&canonical,sizeof(q))==0&&std::memcmp(&q,&s.observed,sizeof(q))==0;
}
inline Reason Plan(std::uintptr_t image,std::size_t span,const Scope& s,NeutralRequest& out) noexcept {
 out={};
 if(!s.exactGate||!s.serializedRecipientWindow||!s.typedAllocation||!s.coherentNativeStates)return Reason::scope;
 if(!Valid(s.expected)||!Valid(s.current)||!Same(s.expected,s.current))return Reason::identity;
 if(!Canonical(image,span,s))return Reason::payload;
 // Never make an active phase lose its queued request or binding. End the
 // actual native phases first. An end-pending bitmap still needs retirement.
 if(s.recipientActive||s.recipientEndPending||s.carrierActive||s.carrierEndPending||s.recipientCarrierRecord||s.recipientCarrier!=0xffff||s.carrierOwner!=0xffff)return Reason::active;
 out.vtable=image+kNeutralVtableRva;return Reason::plan;
}
// The owner calls Plan again immediately before its separately qualified
// typed Q.vt+40 setter and reads back kind0 afterwards. No old snapshot bytes
// or force-byte restoration. Lost leases discard mod bookkeeping only.
} // namespace mgscoop::carry_request_retirement
