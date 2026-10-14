#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "carry_request_payload.hpp"
namespace mgscoop::carry_deferred_copy {
// The ledger protects one actual completed op0. It does not grant execution
// scope, sleep authority or model lifetime; the real core getter supplies those.
enum class Phase : std::uint8_t {empty,reserved,queued};
struct Key {std::uint64_t submit=0,intent=0,carry=0,at=0;std::uint32_t generation=0,epoch=0;std::uint8_t prior=0;};
inline bool Same(const Key& a,const Key& b)noexcept{return a.submit==b.submit&&a.intent==b.intent&&a.carry==b.carry&&a.at==b.at&&a.generation==b.generation&&a.epoch==b.epoch&&a.prior==b.prior;}
inline bool Fresh(const Key& k,std::uint64_t submittedAt,std::uint64_t now)noexcept{return k.submit&&k.intent&&k.carry&&k.generation&&k.epoch&&submittedAt&&k.at>=submittedAt&&k.at-submittedAt<=750&&now>=k.at&&now-k.at<=750;}
struct Ledger {Phase phase=Phase::empty;Key key{};unsigned char before[0x80]{};carry_request_payload::RecipientRequest payload{};};
inline bool Build(std::uintptr_t image,std::uint8_t prior,carry_request_payload::RecipientRequest& out)noexcept{
 if(!carry_request_payload::BuildRecipient(image,0xa01d000,0,1,prior,out))return false;
 // The changed sequence authorizes the single initial offline entry. Never
 // force/restart it on subsequent ticks and never write record12 ourselves.
 out.force=0;return true;
}
inline bool Reserve(Ledger& l,const Key& k,std::uint64_t submittedAt,std::uint64_t now,std::uintptr_t image,const unsigned char* bytes)noexcept{
 if(l.phase!=Phase::empty||!bytes||!Fresh(k,submittedAt,now))return false;
 carry_request_payload::RecipientRequest payload{};if(!Build(image,k.prior,payload))return false;
 l.key=k;std::memcpy(l.before,bytes,sizeof(l.before));l.payload=payload;l.phase=Phase::reserved;return true;
}
inline bool IsBefore(const Ledger& l,const unsigned char* bytes)noexcept{return bytes&&!std::memcmp(l.before,bytes,0x80);}
inline bool IsAfter(const Ledger& l,const unsigned char* bytes)noexcept{return bytes&&!std::memcmp(l.before,bytes,0x40)&&!std::memcmp(bytes+0x40,&l.payload,32)&&!std::memcmp(l.before+0x60,bytes+0x60,32);}
inline bool CurrentReserved(const Ledger& l,const Key& k,bool exactTls,const unsigned char* bytes)noexcept{return l.phase==Phase::reserved&&exactTls&&Same(l.key,k)&&(IsBefore(l,bytes)||IsAfter(l,bytes));}
inline bool Commit(Ledger& l,const Key& k,std::uintptr_t returned,std::uintptr_t queued,const unsigned char* bytes)noexcept{
 if(l.phase!=Phase::reserved||!Same(l.key,k)||!queued||queued>UINTPTR_MAX-0x80||returned!=queued+0x40||!IsAfter(l,bytes))return false;
 l.phase=Phase::queued;return true;
}
inline bool CanClear(const Ledger& l,bool actualRetired,bool neutralQueue,bool cleanPublication)noexcept{return l.phase==Phase::queued&&actualRetired&&neutralQueue&&cleanPublication;}
} // namespace mgscoop::carry_deferred_copy
