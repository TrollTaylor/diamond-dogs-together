#pragma once
#include <cstdint>
namespace mgscoop::native_pose_hold_owners {
inline constexpr std::uint32_t sleep=1u,carry=2u,vehicle=4u,all=sleep|carry|vehicle;
struct Owners {std::uint32_t held=0,pending=0;std::uint64_t carrySerial=0,vehicleSerial=0;};
inline bool AddSleep(Owners& o)noexcept {if((o.held&~all)||(o.held&vehicle))return false;o.held|=sleep;o.pending&=~sleep;return true;}
inline bool AddCarry(Owners& o,std::uint64_t serial)noexcept {
 if(!serial||(o.held&~all)||(o.held&vehicle)||((o.held&carry)&&o.carrySerial!=serial))return false;
 o.held|=carry;o.pending&=~carry;o.carrySerial=serial;return true;
}
inline bool ClearSleep(Owners& o)noexcept {if(!(o.held&sleep))return false;o.pending|=sleep;return true;}
inline bool ClearCarry(Owners& o,std::uint64_t serial)noexcept {
 if(!serial||!(o.held&carry)||o.carrySerial!=serial)return false;o.pending|=carry;return true;
}
inline bool AddVehicle(Owners& o,std::uint64_t serial)noexcept {
 if(!serial||o.pending||(o.held&~all)||(o.held&(sleep|carry))||
  ((o.held&vehicle)&&o.vehicleSerial!=serial))return false;
 o.held|=vehicle;o.vehicleSerial=serial;return true;
}
inline bool ClearVehicle(Owners& o,std::uint64_t serial)noexcept {
 if(!serial||o.held!=vehicle||o.vehicleSerial!=serial)return false;o.pending|=vehicle;return true;
}
// Failed Post/C0/window/writer cleanup leaves every participant retained.
// Retirement is requested by its owner before cleanup; it is never inferred
// from a paired down flag or an unrelated consumer's retirement.
inline bool Closed(Owners& o,bool cleanup)noexcept {
 if(!cleanup)return false;const bool hadCarry=(o.held&carry)!=0;
 o.held&=~o.pending;o.pending=0;
 if(!(o.held&carry))o.carrySerial=0;if(!(o.held&vehicle))o.vehicleSerial=0;return hadCarry&&!(o.held&carry);
}
}
