#pragma once
// SCRATCH ONLY. Original pure local ride storage diagnostic. No hook, engine
// call, write, input or mount permission. Root retains actual scene authority.
#include "mgscoop/buddy_capacity_census.hpp"
namespace mgscoop::buddy_ride_read {
using Reader=buddy_capacity_census::Reader;
using Authorization=buddy_capacity_census::Authorization;
using Pin=buddy_capacity_census::Pin;
inline constexpr Pin kPins[]={
 {0x9d6ae0,4792,"4156f2db71c761ad8a356cd1be7367c517d7447ba58130d0ca15fe5a4ee2289e"},
 {0x9d2510,51,"594ddb813412050c6eff5f133146a9c94a4fe7ce98a2b5cc6764e8fbce3db6ed"},
 {0x1e02920,33,"724877d59b738d0c59475cc8fa89019c6a9b0092ce8b50e7ca8aca1fbde2e69b"},
 {0x53c9d0,61,"76b3456decae18e4ec286d3498381a46d48cd166d47c85a3630682c2d44a4aff"},
 {0x53ca40,57,"210a78952e7f3c7a2539e8557c84609e32cde4bb344fb84873f53272d7c5ab7a"},
 {0x53c850,182,"272a9645646ab79a3ed383e9263433ab97810112b4323de52e99eb0f46b32ce5"},
 {0x1fe8850,62,"1edc093da6d5d726621646f221d101f4e774b35b4e36e1f8655f35cc28dd35e9"},
 {0x53cb10,175,"93d589b937a3520cb5d4fa50b67d5be043ca7ff253e2201ed4e7d38f39cccd57"},
};
struct Owner {
 std::uintptr_t P{};std::uint32_t mission{},generation{},thread{},buddy_cycle{};
 std::uint64_t epoch{},refreshed_ms{};
 std::uint8_t active_kind{}; // exact native HORSE1 or WALKER4; not wire kind
 bool field_ready{},demo_playing{},terminating{};
};
struct Snapshot {
 std::uintptr_t info{};std::uint32_t bank{},local_index{};
 std::uint16_t ride_id{0xffff};bool current{},mounted{},permit_action{};
};
namespace detail {
inline bool Read(Reader r,std::uintptr_t p,void* out,std::size_t n) noexcept {
 constexpr std::uintptr_t last=0x7fffffffffff;
 return r.read&&p>=0x10000&&n&&p<=last&&n<=last-p&&r.read(r.context,p,out,n);
}
template<class T>inline bool At(Reader r,std::uintptr_t p,std::size_t off,T& out) noexcept {
 constexpr std::uintptr_t last=0x7fffffffffff;
 return p&&p<=last&&off<=last-p&&Read(r,p+off,&out,sizeof(out));
}
struct Identity {std::uintptr_t selector{},controller{};std::uint32_t bank{};};
inline bool Current(Reader r,std::uintptr_t base,const Owner& o,Identity& identity) noexcept {
 std::uintptr_t p{},vt{};std::uint16_t mission{};
 std::uint32_t flags{},count{},local{};std::uint8_t active{},pending{};
 return At(r,base,0x2a82c30,p)&&p==o.P&&At(r,base,0x2c01372,mission)&&mission==o.mission&&
 At(r,p,0,vt)&&vt==base+0x2295490&&At(r,p,0x204,flags)&&(flags&1u)&&!(flags&0x100u)&&
 At(r,p,0x208,count)&&count==2&&At(r,p,0x20c,count)&&count==2&&At(r,p,0x214,local)&&local==0&&
 At(r,base,0x2c28930,identity.selector)&&At(r,identity.selector,0x10,identity.controller)&&
 At(r,identity.controller,0x175,active)&&active==o.active_kind&&
 At(r,identity.controller,0x17f,pending)&&pending==0&&
 At(r,base,0x2bd3fd4,local)&&local==0&&At(r,base,0x2bd3fd0,identity.bank)&&identity.bank<=1;
}
}
inline bool Compiled() noexcept {
#if defined(MGS_BACKGROUND_WORKER) && MGS_BACKGROUND_WORKER && defined(MGS_LOCAL_BUDDY_CAPACITY_TEST) && MGS_LOCAL_BUDDY_CAPACITY_TEST
 return true;
#else
 return false;
#endif
}
inline const char* ReadCurrent(Reader r,std::uintptr_t base,Authorization auth,
 const Owner& o,std::uint32_t actual_thread,std::uint64_t now,Snapshot& out) noexcept {
 if(!Compiled()||!auth.private_worker||!auth.offline||!auth.copied_saves||!auth.candidate_catalog||
 !auth.exact_executable||!auth.exact_code_pins||!r.read||!base||base>0x7fffffffffff-0x2c389a0)return "admission";
 if(!o.P||!o.generation||!o.thread||actual_thread!=o.thread||!o.buddy_cycle||!o.epoch||
 !o.field_ready||o.demo_playing||o.terminating||(o.active_kind!=1&&o.active_kind!=4)||
 (o.mission!=30010&&o.mission!=30020&&o.mission!=30050)||now<o.refreshed_ms||now-o.refreshed_ms>250)return "scope";
 detail::Identity before{},after{};if(!detail::Current(r,base,o,before))return "owner";
 Snapshot value{};value.bank=before.bank;value.info=base+0x2bd3ff0+std::uintptr_t(before.bank)*0xe00;
 std::uintptr_t vt{};if(!detail::At(r,value.info,0,vt)||vt!=base+0x2190b28||
 !detail::At(r,value.info,0xb6,value.ride_id))return "record";
 // No mount inference: even a stable exact ride ID must be compared with the
 // separately typed actor's current natural rider/occupied read and frames.
 if(!detail::Current(r,base,o,after)||before.selector!=after.selector||before.controller!=after.controller||before.bank!=after.bank||
 !detail::At(r,value.info,0,vt)||vt!=base+0x2190b28)return "identity_changed";
 std::uint16_t again{};if(!detail::At(r,value.info,0xb6,again)||again!=value.ride_id)return "ride_changed";
 value.current=true;out=value;return nullptr;
}
}
