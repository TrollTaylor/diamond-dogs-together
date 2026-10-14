#pragma once
// SCRATCH ONLY. Unregistered and off by default. The coordinator supplies the
// actual natural callback/lifetime validator; no Lua scope can substitute it.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include "horse_player_cargo_api.hpp"
#include "horse_physical_profile_read.hpp"
namespace mgscoop::horse_owned_cargo_prototype {
using Identity=horse_player_cargo_api::Identity;
using Phase=horse_player_cargo_api::Phase;
struct alignas(8) HorseMessage {
    std::uint64_t hash=0x5167c60042b5ull;
    std::uint16_t body=0xffff,reserved=0;
    std::uint32_t kind=5;
    std::uint64_t unused=0;
    std::uintptr_t unit=0;
    std::uint8_t variant=0,padding[7]{};
};
static_assert(sizeof(HorseMessage)==40&&offsetof(HorseMessage,kind)==0xc&&offsetof(HorseMessage,unit)==0x18&&offsetof(HorseMessage,variant)==0x20);
// E59E20 receives PRIMARY+20 in RCX, actual native index in EDX, signed result
// pointer in R8 and the packet in R9. It synchronously dispatches current
// natural event7 and copies the result back before returning. No retained
// packet pointer is observed in the inspected exact receiver/event graph.
using HorseReceiver=void(*)(void*,std::uint32_t,std::int32_t*,HorseMessage*);
using PhysicalOriginal=void(*)(void*,void*); //1AD8250 PRIMARY RCX, poseContext RDX.
struct Slot {
    std::uintptr_t controller=0,receiver=0,record=0,sdRecord=0,unit=0;
    std::uint16_t gameId=0xffff,nativeIndex=0xffff,body=0xffff,sdBody=0xffff;
    std::uint8_t sdCargo=0;
    float cargoMatrix[16]{};
};
struct Snapshot {
    Identity identity{};
    horse_player_physical_read::Body physical{};
    horse_physical_profile_read::Profile profile{};
    Slot horse{};
    bool carryRetired=false,maskHeld=false,residentGraph=false,physicalReleased=false;
};
struct RawWindow {
    std::uintptr_t self=0,poseContext=0,caller=0;
    std::uint32_t thread=0;
};
// Capture must validate the actual current1AD8250 callback origin, complete
// pinned caller+pose-context ownership, body/model retention and same scene.
// Horse invokes the ORIGINAL typed current receiver after fresh same-slot
// validation. Apply copies all4 original cargo columns only in this validated
// natural component callback before its original; it does not setD0 bit1.
// Release stops copies and confirms the original physical/camera/collider
// consumer has relinquished cargo, without unlocking incapacity controls.
struct Backend {
    void* context=nullptr;
    bool(*capture)(void*,const RawWindow&,const Identity&,Snapshot&,const char*&)=nullptr;
    bool(*retireCarry)(void*,const Snapshot&,const char*&)=nullptr;
    bool(*horseOriginal)(void*,const Snapshot&,HorseMessage&,std::int32_t&,const char*&)=nullptr;
    bool(*applyCurrentMatrix)(void*,const RawWindow&,const Snapshot&,const float(&)[16],const char*&)=nullptr;
    bool(*releaseCurrentPhysical)(void*,const RawWindow&,const Snapshot&,const char*&)=nullptr;
};
inline bool SameIdentity(const Identity& a,const Identity& b) noexcept {
    return a.mission==b.mission&&a.generation==b.generation&&a.bodyBirth==b.bodyBirth&&a.horseBirth==b.horseBirth&&
      a.epoch==b.epoch&&a.body==b.body&&a.horse==b.horse&&a.grant==b.grant;
}
inline bool SameSlot(const Slot& current,const Slot& owned) noexcept {
    return current.controller==owned.controller&&current.receiver==owned.receiver&&current.record==owned.record&&
      current.sdRecord==owned.sdRecord&&current.unit==owned.unit&&current.gameId==owned.gameId&&current.nativeIndex==owned.nativeIndex;
}
inline bool OwnedSlot(const Snapshot& s,const Slot& owned) noexcept {
    return SameSlot(s.horse,owned)&&s.horse.body==s.identity.body&&s.horse.sdBody==s.identity.body&&s.horse.sdCargo==1;
}
inline bool FreeSlot(const Slot& slot) noexcept {
    return slot.body==0xffff&&slot.sdBody==0xffff&&!slot.sdCargo;
}
inline bool MatrixFinite(const float(&matrix)[16]) noexcept {
    for(float value:matrix)if(!std::isfinite(value)||std::fabs(value)>200000.0f)return false;
    return true;
}
class Controller {
    Identity identity_{};Slot owned_{};Phase phase_=Phase::idle;
    bool enabled_=false,cancel_=false,retireRequested_=false,replyAccepted_=false;
    std::uint16_t readbackWait_=0;const char* reason_="prototype_disabled";
public:
    // Enabling this detached state machine alone supplies no native permission.
    // Main has no registration/activation path for this scratch prototype.
    void SetEnabledForQualifiedWorker(bool enabled) noexcept {enabled_=enabled;}
    Phase phase() const noexcept{return phase_;}
    const char* reason() const noexcept{return reason_;}
    bool HoldIncapacity() const noexcept{return phase_!=Phase::idle&&phase_!=Phase::retired&&phase_!=Phase::refused;}
    bool QueueLoad(const Identity& id) noexcept {
        if(!enabled_){reason_="prototype_disabled";return false;}
        if(phase_!=Phase::idle&&phase_!=Phase::retired&&phase_!=Phase::refused){reason_="cargo_busy";return false;}
        if(!id.mission||!id.epoch||!id.generation||!id.bodyBirth||!id.horseBirth||!id.grant||id.body>1||
           (id.horse>>9)!=13){reason_="cargo_identity";return false;}
        identity_=id;owned_={};cancel_=retireRequested_=replyAccepted_=false;readbackWait_=0;
        phase_=Phase::awaiting_player_retirement;reason_="carry_retirement_pending";return true;
    }
    void QueueDetach(const Identity& id) noexcept {if(SameIdentity(id,identity_))cancel_=true;}
    // One bounded native action per component callback. Missing callback or
    // changed identity never grants calls, copies, cleanup or wake permission.
    void BeforeOriginal(const RawWindow& raw,const Backend& backend) noexcept {
        if(!enabled_||phase_==Phase::idle||phase_==Phase::retired||phase_==Phase::refused)return;
        Snapshot s{};const char* why="natural_window_unqualified";
        if(!raw.self||!raw.poseContext||!raw.caller||!raw.thread||!backend.capture||
           !backend.capture(backend.context,raw,identity_,s,why)||!SameIdentity(s.identity,identity_)){
            reason_=why;return;}
        if(s.physical.component!=raw.self||s.profile.component!=raw.self||!s.maskHeld||!s.residentGraph){reason_="player_binding_changed";return;}
        if(cancel_&&(phase_==Phase::awaiting_player_retirement||phase_==Phase::admitting_horse)){
            if(!s.carryRetired){
                if(!retireRequested_&&backend.retireCarry)retireRequested_=backend.retireCarry(backend.context,s,why);
                reason_="cancel_retirement_pending";return;
            }
            phase_=Phase::retired;reason_="cancelled_before_horse";return;
        }
        if(phase_==Phase::awaiting_player_retirement){
            if(!s.carryRetired){
                if(!retireRequested_&&backend.retireCarry)retireRequested_=backend.retireCarry(backend.context,s,why);
                reason_=retireRequested_?"carry_retirement_readback_pending":why;return;
            }
            phase_=Phase::admitting_horse;reason_="horse_admission_pending";return;
        }
        if(phase_==Phase::admitting_horse){
            if(!s.carryRetired||!s.horse.controller||!s.horse.receiver||!s.horse.record||!s.horse.sdRecord||
               !s.horse.unit||s.horse.gameId!=identity_.horse||s.horse.nativeIndex==0xffff||!FreeSlot(s.horse)){
                reason_="horse_cargo_not_free";return;
            }
            if(!horse_physical_profile_read::MatrixCompatible(s.profile)){reason_="physical_profile_pending";return;}
            HorseMessage message{};message.body=identity_.body;message.kind=1;std::int32_t result=INT32_MIN;
            if(!backend.horseOriginal||!backend.horseOriginal(backend.context,s,message,result,why)){reason_=why;return;}
            // Admission is provisional until a fresh native slot observation
            // proves actual unit164/Sd10/2DE; reply0 alone never proves it.
            // The backend return means ORIGINAL WAS CALLED, not success. Even
            // an unexpected reply requires fresh native slot readback before
            // release or another admission, because event7 may have committed.
            replyAccepted_=result>=0&&message.unit==s.horse.unit;
            owned_=s.horse;readbackWait_=0;phase_=Phase::binding_physical;
            reason_=replyAccepted_?"horse_slot_readback_pending":"horse_reply_cleanup_pending";return;
        }
        if(phase_==Phase::binding_physical||phase_==Phase::loaded){
            if(!SameSlot(s.horse,owned_)){reason_="horse_slot_changed_no_calls";return;}
            if(!OwnedSlot(s,owned_)){
                if(FreeSlot(s.horse)&&(!replyAccepted_||cancel_||readbackWait_>=240)){
                    phase_=Phase::awaiting_physical_release;reason_="horse_admission_refused";return;
                }
                if(readbackWait_<240)++readbackWait_;
                if(readbackWait_>=240){cancel_=true;reason_="horse_readback_timeout_cleanup_pending";}
                else reason_="horse_slot_readback_pending";
                return;
            }
            if(!replyAccepted_)cancel_=true;
            if(cancel_){phase_=Phase::detaching_horse;reason_="horse_detach_pending";return;}
            if(!s.carryRetired||!horse_physical_profile_read::MatrixCompatible(s.profile)||!MatrixFinite(s.horse.cargoMatrix)){
                reason_="physical_profile_pending";return;
            }
            if(!backend.applyCurrentMatrix||!backend.applyCurrentMatrix(backend.context,raw,s,s.horse.cargoMatrix,why)){reason_=why;return;}
            phase_=Phase::loaded;reason_="native_matrix_applied_pose_unverified";return;
        }
        if(phase_==Phase::detaching_horse){
            if(!OwnedSlot(s,owned_)){reason_="owned_horse_cleanup_changed";return;}
            HorseMessage message{};message.body=identity_.body;message.kind=2;std::int32_t result=INT32_MIN;
            if(!backend.horseOriginal||!backend.horseOriginal(backend.context,s,message,result,why)||result<0){reason_=why;return;}
            phase_=Phase::awaiting_physical_release;reason_="horse_release_readback_pending";return;
        }
        if(phase_==Phase::awaiting_physical_release){
            if(!SameSlot(s.horse,owned_)||!FreeSlot(s.horse)){reason_="horse_release_pending";return;}
            if(!s.physicalReleased){if(backend.releaseCurrentPhysical)backend.releaseCurrentPhysical(backend.context,raw,s,why);reason_=why;return;}
            if(!s.carryRetired){reason_="player_retirement_pending";return;}
            phase_=Phase::retired;reason_="native_cargo_retired";
        }
    }
};
}
