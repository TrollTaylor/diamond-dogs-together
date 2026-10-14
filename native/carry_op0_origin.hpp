#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "carry_owned_handshake.hpp"
namespace mgscoop::carry_op0_origin {
inline constexpr std::uintptr_t senderReturn=0x113c06e,op1Return=0x11286c6,serviceRva=0x2c38008,serviceVt=0x2305e70;
// Sender initializes word8, dwordC and pointer10; reserved wordA is padding
// and is never consumed by stock receiver. It must not become an admission gate.
inline bool NaturalSource(std::uintptr_t image,std::uintptr_t caller,std::uintptr_t service,std::uint32_t target,
 const carry_owned_handshake::MessagePrefix& m,std::uintptr_t carrierRecord)noexcept{
 return image&&caller==image+senderReturn&&service==image+serviceRva&&target==1&&m.hash==carry_request_payload::kHandshakeHash&&
 m.carrier==0&&!m.operation&&m.carrierRecord&&m.carrierRecord==carrierRecord;
}
// The domain is derived by the installed wrapper from the actual writer or
// its current tracked OwnsRead ticket. A cached submission is never a domain.
enum class Domain : std::uint8_t {none,owned,shared};
enum class Route : std::uint8_t {stock,origin,refuse};
inline Route DispatchRoute(Domain domain,bool carryHeld,bool confirmed,bool exactSender,std::uint32_t actual)noexcept{
 if(domain==Domain::none)return carryHeld?Route::refuse:Route::stock;
 if(domain==Domain::owned)return carryHeld?Route::refuse:Route::stock;
 if(!carryHeld)return Route::stock;
 if(confirmed&&exactSender&&actual==1)return Route::origin;
 return actual<=1?Route::refuse:Route::stock;
}
inline Route RecipientRoute(Domain domain,bool carryHeld,bool exactOrigin,std::uint32_t actual)noexcept{
 if(!carryHeld)return Route::stock;
 return domain==Domain::shared&&exactOrigin&&actual==1?Route::origin:Route::refuse;
}
// Full producer113C03C stores only BYTE record80, then113C061 BYTE85.
// Neighbor81 is native progress and must not be accidentally read as ID high byte.
template<class R>bool ReadCarrierTag(R& r,std::uintptr_t record,std::uint8_t& actual,std::uint8_t& offline)noexcept{
 actual=0xff;offline=0;return record&&record<=UINTPTR_MAX-0x86&&r(record+0x80,&actual,1)&&r(record+0x85,&offline,1);
}
inline bool CopyOnly(const unsigned char* before,const unsigned char* after,const unsigned char* actualMatrix,std::int32_t result)noexcept{
 return before&&after&&actualMatrix&&result==0&&!std::memcmp(before,after,0x1d0)&&
 !std::memcmp(after+0x1d0,actualMatrix,64)&&!std::memcmp(before+0x210,after+0x210,0x6c0-0x210);
}
struct Intent {std::uint64_t submission=0,intent=0,at=0,carry=0;std::uint32_t generation=0,epoch=0;std::uint8_t prior=0;bool accepted=false,queued=false;};
inline bool Reserve(Intent& value,std::uint64_t submission,std::uint64_t nonce,std::uint64_t now,std::uint64_t carry,
 std::uint32_t generation,std::uint32_t epoch,std::uint8_t prior)noexcept{
 if(value.accepted||value.queued||!submission||!nonce||!now||!carry||!generation||!epoch)return false;
 value={submission,nonce,now,carry,generation,epoch,prior,true,false};return true;
}
// Native sender may retry while awaiting the reciprocal ack. Preserve one
// immutable accepted intent; successful repeat copy-out never requeues it.
inline bool SameCycle(const Intent& value,std::uint64_t submission,std::uint64_t carry,std::uint32_t generation,std::uint32_t epoch)noexcept{
 return value.accepted&&value.intent&&value.submission==submission&&value.carry==carry&&value.generation==generation&&value.epoch==epoch;
}
inline bool Current(const Intent& value,std::uint64_t submission,std::uint64_t carry,std::uint32_t generation,std::uint32_t epoch,std::uint64_t now)noexcept{
 return value.accepted&&!value.queued&&value.intent&&value.submission==submission&&value.carry==carry&&value.generation==generation&&value.epoch==epoch&&
 now>=value.at&&now-value.at<=750;
}
} // namespace mgscoop::carry_op0_origin
