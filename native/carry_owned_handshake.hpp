#pragma once
#include <cstddef>
#include <cstdint>
#include "carry_request_payload.hpp"
namespace mgscoop::carry_owned_handshake {
// Pure decision/serialization policy. This file does not install a hook, call
// a native function, write an output result, enqueue requests, or detach bodies.
// The caller must prove the complete runtime lease and lifecycle before use.
struct MessagePrefix {
 std::uint64_t hash=0;
 std::uint16_t carrier=0,reserved=0;
 std::uint32_t operation=0;
 std::uintptr_t carrierRecord=0;
};
static_assert(sizeof(MessagePrefix)==24&&offsetof(MessagePrefix,operation)==0xc&&offsetof(MessagePrefix,carrierRecord)==0x10);
struct Lease {
 std::uint32_t mission=0,generation=0,thread=0,transaction=0,revision=0;
 std::uint16_t epoch=0;
 std::uint8_t carrier=0,recipient=0,cause=0;
 std::uintptr_t player=0,carrierRecord=0,recipientPhase=0;
};
inline bool SameLease(const Lease& a,const Lease& b) noexcept {
 return a.mission==b.mission&&a.generation==b.generation&&a.thread==b.thread&&a.transaction==b.transaction&&a.revision==b.revision&&a.epoch==b.epoch&&a.carrier==b.carrier&&a.recipient==b.recipient&&a.cause==b.cause&&a.player==b.player&&a.carrierRecord==b.carrierRecord&&a.recipientPhase==b.recipientPhase;
}
inline bool ValidLease(const Lease& a) noexcept {
 return (a.mission==30010||a.mission==30020||a.mission==30050)&&a.generation&&a.thread&&a.transaction&&a.revision&&a.epoch&&a.carrier<2&&a.recipient<2&&a.carrier!=a.recipient&&(a.cause==1||a.cause==2)&&a.player&&a.carrierRecord&&a.recipientPhase;
}
struct Admission {
 Lease expected{},current{};
 std::uintptr_t actualPhase=0;
 std::uint32_t actualRecipient=0;
 bool fullyQualified=false,serialized=false,hostGrant=false,locked=false,positiveHealth=false,modelQualified=false,requestSlotOwned=false,recipientIdle=false,carrierEntered=false;
 std::uint8_t progress=0,ack=0,cancel=0,offlineOwned=0,variant=0;
 std::uint16_t nativeTarget=0xffff,nativePrevious=0xffff,nativeCarrier=0xffff;
};
enum class Decision { stock,owned_op0 };
inline Decision Evaluate(const Admission& a,const MessagePrefix& m) noexcept {
 // Every unrelated hash/op/phase/body/native record falls through to stock.
 if(!ValidLease(a.expected)||!SameLease(a.expected,a.current)||m.hash!=carry_request_payload::kHandshakeHash||m.operation!=0||m.carrier!=a.expected.carrier||m.carrierRecord!=a.expected.carrierRecord||a.actualPhase!=a.expected.recipientPhase||a.actualRecipient!=a.expected.recipient)return Decision::stock;
 if(!a.fullyQualified||!a.serialized||!a.hostGrant||!a.locked||!a.positiveHealth||!a.modelQualified||!a.requestSlotOwned||!a.recipientIdle||!a.carrierEntered)return Decision::stock;
 if(a.progress!=2||a.ack||a.cancel||a.offlineOwned!=1||a.variant!=0xa||a.nativeTarget!=a.expected.recipient||a.nativePrevious!=a.expected.recipient||a.nativeCarrier!=a.expected.carrier)return Decision::stock;
 return Decision::owned_op0;
}
struct Pending {
 Lease lease{};
 std::uint8_t previousSequence=0;
 bool accepted=false,consumed=false;
};
// Stage only AFTER the exact owned native copy-out succeeds and output result0
// is accepted. This is a mod-memory token, never a retained callable/pointer
// permission. Recipient request is deferred to its own serialized natural job.
inline bool Stage(const Admission& a,const MessagePrefix& m,bool copySucceeded,std::uint8_t prior,Pending& p) noexcept {
 if(p.accepted||p.consumed||!copySucceeded||Evaluate(a,m)!=Decision::owned_op0)return false;
 p.lease=a.current;p.previousSequence=prior;p.accepted=true;return true;
}
inline bool BuildDeferred(Pending& p,const Lease& current,std::uint32_t body,bool nativeTupleStillOwned,bool naturalRecipientWindow,std::uintptr_t image,std::size_t span,carry_request_payload::RecipientRequest& out) noexcept {
 out={};if(!p.accepted||p.consumed||!ValidLease(current)||!SameLease(p.lease,current)||body!=current.recipient||!nativeTupleStillOwned||!naturalRecipientWindow)return false;
 if(!carry_request_payload::BuildRecipient(image,span,current.carrier,current.recipient,p.previousSequence,out))return false;
 // Exact offline consumer accepts a changed sequence without force. This
 // avoids a persistent forced retry; early-abort retirement remains a separate
 // required runtime contract. Do not submit this payload every tick.
 out.force=0;p.consumed=true;return true;
}
inline void Forget(Pending& p) noexcept {p={};}
} // namespace mgscoop::carry_owned_handshake

