#pragma once
// Native thrown-support admission and stock latch; pure fixture policy.
#include <cstdint>
namespace mgscoop::partner_support_policy {
struct GateIn {
 bool enabled, latched, guard_installed, guard_armed, filter_live, filter_armed;
 bool slot_valid, equip_valid, body_ready, record_ready;
 std::uint8_t slot, type;
 std::uint16_t equip;
 bool live=true,stale=false,gun_valid=true,p218_window=false;
};
constexpr bool Admit(const GateIn& x) noexcept {
 return x.live && !x.stale && x.gun_valid && !x.p218_window && x.enabled && !x.latched && x.guard_installed && x.guard_armed && x.filter_live && x.filter_armed &&
  x.slot_valid && x.slot==3 && x.equip_valid && x.equip>0 && x.equip<0x800 && x.type==12 && x.body_ready && x.record_ready;
}
constexpr std::uint32_t Strip(std::uint32_t mask,bool request,bool key,bool admitted) noexcept {
 if(request)mask|=admitted ? 0x40u : 0x58u;
 if(key || request)mask|=0x8020u; // no Action or CQC in support windows
 return mask;
}
constexpr bool ThrowMode(std::uint8_t mode) noexcept { return mode>=28 && mode<=42; }
struct StockLatch { bool off=false; };
// An unread before/after refuses admission; only actual changed evidence latches.
constexpr bool CheckStock(StockLatch& s,bool before_ok,bool after_ok,std::uint32_t before,std::uint32_t after) noexcept {
 const bool changed=before_ok && after_ok && before!=after;
 if(changed)s.off=true;
 return changed;
}
}
