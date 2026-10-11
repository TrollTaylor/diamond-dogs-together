#pragma once
#include "carry_queued_abort.hpp"
namespace mgscoop::carry_cold_rollback {
// Pure branch exclusions and nominations. No native call or write. A successful
// branch check does not qualify the other callbacks inside native End/exit.
struct RecordRead {
 std::uintptr_t record=0,vtable=0,resource248=0,resource250=0,resource258=0;
 std::uint32_t resourceKind=0,playerFlags=0;
 std::uint16_t flags6A6=0;
 std::uint8_t dropFlags27D=0;
 std::uintptr_t closureCallback=0;
 std::int32_t closureThisAdjust=0;
};
inline bool ColdResources(const RecordRead& a) noexcept {
 return !a.resourceKind&&!a.resource248&&!a.resource250&&!a.resource258;
}
inline bool DropBranchesExcluded(const RecordRead& a) noexcept {
 // P+204 bit100 admits the exit's new1800 producers. If absent, neither
 // producer runs; old1000 is the only remaining path to set27D bits1/2.
 return (a.playerFlags&1u)&&!(a.playerFlags&0x100u)&&!(a.flags6A6&0x1000u)&&!(a.dropFlags27D&3u);
}
struct Scope {
 carry_queued_abort::CarrierEndScope end{};
 RecordRead initial{},current{};
 bool exactColdGate=false,typedColdReads=false,coherentColdReads=false;
 bool deferredAndNativeMutationQuiesced=false;
};
enum class Reason { identity,scope,resources,drop,closure,end_unqualified,nominate };
inline Reason Check(std::uintptr_t image,std::size_t span,const Scope& s) noexcept {
 if(!s.exactColdGate||!s.typedColdReads||!s.coherentColdReads||!s.deferredAndNativeMutationQuiesced)return Reason::scope;
 if(!carry_request_retirement::Valid(s.end.fresh.expected)||!carry_request_retirement::Valid(s.end.fresh.current)||!carry_request_retirement::Same(s.end.fresh.expected,s.end.fresh.current)||!carry_request_payload::ImageSpan(image,span)||0x23b9468>span||sizeof(std::uintptr_t)>span-0x23b9468||0x113bba0>=span||s.initial.record!=s.end.fresh.expected.lease.carrierRecord||s.current.record!=s.initial.record||s.initial.vtable!=image+0x23b9468||s.current.vtable!=s.initial.vtable)return Reason::identity;
 if(!ColdResources(s.initial)||!ColdResources(s.current))return Reason::resources;
 if(!DropBranchesExcluded(s.initial)||!DropBranchesExcluded(s.current))return Reason::drop;
 if(s.current.closureCallback!=image+0x113bba0||s.current.closureThisAdjust)return Reason::closure;
 if(!s.end.fullyQualifiedEnd)return Reason::end_unqualified;
 return Reason::nominate;
}
inline bool Nominate(std::uintptr_t image,std::size_t span,const Scope& s,carry_queued_abort::Token& token) noexcept {
 // Full ABI, typed transitive callbacks, scheduled exit and mandatory pose/UI
 // window qualification are independent requirements; this never supplies them.
 return Check(image,span,s)==Reason::nominate&&carry_queued_abort::NominateCarrierEnd(image,span,s.end,token);
}
} // namespace mgscoop::carry_cold_rollback
