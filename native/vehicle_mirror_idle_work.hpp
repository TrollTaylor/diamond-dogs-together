#pragma once
#include "vehicle_mirror_state.hpp"
#include "mgscoop/native_pose_vehicle.hpp"
namespace mgscoop::vehicle_mirror_idle_work {
// Derived only under the module's actual core writer/shared read domain.
// This decides whether there is work to inspect; it grants no native call.
struct Duty {
 vehicle_mirror_state::Phase phase=vehicle_mirror_state::Phase::idle;
 std::uint64_t purpose=0,leasePurpose=0,posePurpose=0;std::uint32_t poseMount=0;
 bool clearing=false,retained=false,exitPending=false,releaseRequested=false,reply=false,invalid=false;
};
inline bool FrameRequired(const Duty& d,bool ownedCoreVehicle,bool mailboxRead,
                          const vehicle_mirror_state::Inbox& mailbox,std::uint64_t now) noexcept {
 namespace s=vehicle_mirror_state;
 if(ownedCoreVehicle||d.phase!=s::Phase::idle||d.purpose||d.leasePurpose||d.posePurpose||d.poseMount||
  d.clearing||d.retained||d.exitPending||d.releaseRequested||d.reply||d.invalid)return true;
 if(!mailboxRead)return true; // busy is not proof of absence
 if(!mailbox.received)return mailbox.sequence||mailbox.receipt.mount;
 if(!s::Fresh(now,mailbox.received))return false; // cold expired source cannot start a new mount
 if(mailbox.receipt.event==s::Event::mounted)return true;
 // A current valid exit with no retained duty has nothing left to retire.
 // Unknown/malformed fresh state always takes the existing full path.
 return mailbox.receipt.event!=s::Event::exit||!s::Valid(mailbox.receipt)||!s::Valid(mailbox.link)||
  mailbox.receipt.role!=mailbox.link.peerRole||mailbox.receipt.mission!=mailbox.link.mission||
  mailbox.receipt.epoch!=mailbox.link.epoch||mailbox.sequence!=mailbox.receipt.sequence;
}
// This reader is the compiled actual core ownership API in production. It
// receives the real shared ticket (or genuine writer), never GuardBinding.
template<class ReadCurrent>bool ReadRetained(ReadCurrent& read,const partner_input_hook::PoseSharedLease* ticket,
 const partner_input_hook::NativePoseVehicleLease& expected,std::uint16_t vehicle,std::uint32_t thread,
 partner_input_hook::NativePoseVehicleOwnership& out) noexcept {
 namespace ph=partner_input_hook;out={};ph::NativePoseVehicleOwnership a{},b{};
 if(!expected.serial||!expected.generation||!expected.sceneEpoch||!thread||
  !read(a,ticket)||!a.image||!a.player||!a.go||!a.data||!a.state||!a.block[1]||!a.birth[0]||!a.birth[1]||!a.birthLifecycle||
  a.lease.serial!=expected.serial||a.lease.generation!=expected.generation||
  a.lease.sceneEpoch!=expected.sceneEpoch||a.vehicle!=vehicle||a.purposes!=4u||a.pending||a.thread!=thread||
  !read(b,ticket)||b.lease.serial!=a.lease.serial||b.lease.generation!=a.lease.generation||b.lease.sceneEpoch!=a.lease.sceneEpoch||
  b.image!=a.image||b.go!=a.go||b.thread!=a.thread||b.player!=a.player||b.data!=a.data||b.state!=a.state||b.block[1]!=a.block[1]||b.pool[1]!=a.pool[1]||
  b.birth[0]!=a.birth[0]||b.birth[1]!=a.birth[1]||b.birthLifecycle!=a.birthLifecycle||b.sequence!=a.sequence||
  b.phase[1]!=a.phase[1]||b.nextPhase[1]!=a.nextPhase[1]||b.currentVehicle[1]!=a.currentVehicle[1]||
  b.nativeBinding[1]!=a.nativeBinding[1]||b.seat[1]!=a.seat[1]||b.purposes!=a.purposes||b.pending!=a.pending)return false;
 out=b;return true;
}
}
