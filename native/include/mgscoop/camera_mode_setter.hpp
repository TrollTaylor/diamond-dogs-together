#pragma once
#include <cstdint>
namespace mgscoop::camera_mode_setter {
// Exact C0 setter ABI remains void(void* interface,uint8_t requestedMode).
// Receipt is read-only evidence. It grants no engine invocation or lease.
enum class Kind : std::uint8_t { native,ownedCarry };
struct Receipt {
 Kind kind=Kind::native;
 std::uintptr_t image=0,slot=0,original=0,actual=0;
 std::uint64_t serial=0;
};
using Provider=bool(*)(std::uintptr_t image,std::uintptr_t slot,std::uintptr_t actual,Receipt&) noexcept;
inline bool Valid(const Receipt& r,std::uintptr_t image,std::uintptr_t slot,std::uintptr_t actual) noexcept {
 if(!image||image>UINTPTR_MAX-0xa01d000||r.image!=image||r.slot!=slot||r.actual!=actual||
  r.original!=image+0x11f6520||(slot!=image+0x23c75f8&&slot!=image+0x23c7498))return false;
 if(r.kind==Kind::native)return !r.serial&&actual==r.original;
 return r.kind==Kind::ownedCarry&&r.serial&&actual>=0x10000&&actual!=r.original;
}
inline bool Same(const Receipt& a,const Receipt& b) noexcept {
 return a.kind==b.kind&&a.image==b.image&&a.slot==b.slot&&a.original==b.original&&a.actual==b.actual&&a.serial==b.serial;
}
}
