#pragma once
#include "carry_request_retirement.hpp"
namespace mgscoop::carry_queued_abort {
// Pure nominations only. No game reads, calls, writes, native End permission,
// or release of the incapacitation lock. This only covers an unconsumed kind15.
using Identity=carry_request_retirement::Identity;
using NeutralRequest=carry_request_retirement::NeutralRequest;
struct Scope {
 Identity expected{},current{};
 carry_request_payload::RecipientRequest ours{},observed{};
 std::uint8_t priorSequence=0,committedSequence=0,windowBody=0xff;
 bool exactGate=false,serializedNaturalWindow=false,typedAllocation=false,coherentNativeStates=false;
 bool deferredQueueQuiesced=false,controlsRetained=false;
 bool recipientActive=false,recipientEndPending=false,carrierActive=false,carrierEndPending=false;
 std::uint8_t recipientCurrent=0,recipientNext=0;
 std::uintptr_t recipientCarrierRecord=0,recipientResource=0;
 std::uint16_t recipientCarrier=0xffff,carrierOwner=0xffff,carrierTarget=0xffff,carrierPrevious=0xffff;
 std::uint8_t carrierCurrent=0,carrierNext=0,carrierProgress=0,carrierAck=0,carrierCancel=0,carrierOfflineOwned=0,carrierVariant=0;
};
enum class Reason { scope,identity,payload,consumed,carrier,duplicate,plan };
struct Token {
 Identity identity{};
 NeutralRequest neutral{};
 bool planned=false,confirmed=false,endNominated=false;
};
inline bool NativeIdle(const Scope& s) noexcept {
 return !s.recipientActive&&!s.recipientEndPending&&!s.recipientCurrent&&!s.recipientNext&&!s.recipientCarrierRecord&&!s.recipientResource&&s.recipientCarrier==0xffff&&s.committedSequence==s.priorSequence;
}
inline bool ExactCarrier(const Scope& s) noexcept {
 // Narrow forced-A handshake closure only. Established attachment/drop phases
 // require the separately qualified reciprocal End/release path.
 return s.carrierActive&&!s.carrierEndPending&&s.carrierCurrent==1&&s.carrierNext==1&&s.carrierProgress>=2&&s.carrierProgress<=3&&!s.carrierAck&&!s.carrierCancel&&s.carrierOfflineOwned==1&&s.carrierVariant==0xa&&s.carrierOwner==s.expected.lease.carrier&&s.carrierTarget==s.expected.lease.recipient&&s.carrierPrevious==s.expected.lease.recipient;
}
inline Reason Validate(std::uintptr_t image,std::size_t span,const Scope& s,bool requirePacket,std::uint8_t actualBody) noexcept {
 if(!s.exactGate||!s.serializedNaturalWindow||!s.typedAllocation||!s.coherentNativeStates||!s.deferredQueueQuiesced||!s.controlsRetained)return Reason::scope;
 if(s.windowBody!=actualBody)return Reason::scope;
 if(!carry_request_retirement::Valid(s.expected)||!carry_request_retirement::Valid(s.current)||!carry_request_retirement::Same(s.expected,s.current))return Reason::identity;
 if(!carry_request_payload::ImageSpan(image,span)||carry_request_retirement::kNeutralVtableRva>=span||sizeof(std::uintptr_t)>span-carry_request_retirement::kNeutralVtableRva)return Reason::payload;
 if(requirePacket){
  carry_request_retirement::Scope p{};p.expected=s.expected;p.ours=s.ours;p.observed=s.observed;
  if(!carry_request_retirement::Canonical(image,span,p)||s.ours.sequence!=carry_request_payload::NextSequence(s.priorSequence)||s.ours.sequence==s.committedSequence)return Reason::payload;
 }
 if(!NativeIdle(s))return Reason::consumed;
 if(!ExactCarrier(s))return Reason::carrier;
 return Reason::plan;
}
inline Reason Plan(std::uintptr_t image,std::size_t span,const Scope& s,Token& token,NeutralRequest& out) noexcept {
 out={};if(token.planned||token.confirmed||token.endNominated)return Reason::duplicate;
 const auto r=Validate(image,span,s,true,s.expected.lease.recipient);if(r!=Reason::plan)return r;
 out.vtable=image+carry_request_retirement::kNeutralVtableRva;
 token.identity=s.current;token.neutral=out;token.planned=true;return Reason::plan;
}
// Confirm only a fresh 16-byte readback: Q setter copies 16 bytes for kind0,
// leaving its unused trailing record bytes unchanged. Never require tail zero.
inline bool Confirm(std::uintptr_t image,std::size_t span,const Scope& fresh,const NeutralRequest& readback,Token& token) noexcept {
 if(!token.planned||token.confirmed||token.endNominated||!carry_request_retirement::Same(token.identity,fresh.current)||Validate(image,span,fresh,false,fresh.expected.lease.recipient)!=Reason::plan||std::memcmp(&readback,&token.neutral,sizeof(readback)))return false;
 token.confirmed=true;return true;
}
struct CarrierEndScope {
 Scope fresh{};
 NeutralRequest readback{};
 bool serializedCarrierWindow=false,fullyQualifiedEnd=false;
};
inline bool NominateCarrierEnd(std::uintptr_t image,std::size_t span,const CarrierEndScope& s,Token& token) noexcept {
 // This nomination cannot qualify the callback/transitive ABI. The coordinator
 // must separately prove those and keep controls locked through scheduled exit.
 if(!token.planned||!token.confirmed||token.endNominated||!s.serializedCarrierWindow||!s.fullyQualifiedEnd||!carry_request_retirement::Same(token.identity,s.fresh.current)||Validate(image,span,s.fresh,false,s.fresh.expected.lease.carrier)!=Reason::plan||std::memcmp(&s.readback,&token.neutral,sizeof(s.readback)))return false;
 token.endNominated=true;return true;
}
inline void Forget(Token& token) noexcept {token={};}
} // namespace mgscoop::carry_queued_abort
