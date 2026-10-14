#pragma once
// Pure ownership guards for native reticle observation and explicit friendly classification.
#include <cstdint>
namespace mgscoop::native_ui {
struct Raw {
    std::uintptr_t p{}, ui{}, ui_vtable{}, primary_vtable{}, owner{}, cam{}, cam_vtable{}, hud{}, hud_vtable{}, setter{};
    std::uint32_t count{}, local{}, category{}, cam_flags{};
    std::uint16_t ids[2]{}, hit{0xffff};
    std::uint64_t ray[2]{};
};
struct Expected {std::uintptr_t ui_vtable, primary_vtable, cam_vtable, code_begin, code_end;};
inline bool Valid(const Raw& r, const Expected& e) noexcept {
    return r.p && r.ui >= 0x20 && r.cam && r.hud && r.owner == r.p && r.count == 2 && r.local == 0 &&
        r.ids[0] != 0xffff && r.ids[1] != 0xffff && r.ids[0] != r.ids[1] &&
        r.ui_vtable == e.ui_vtable && r.primary_vtable == e.primary_vtable && r.cam_vtable == e.cam_vtable &&
        r.hud_vtable && r.setter >= e.code_begin && r.setter < e.code_end && r.category <= 2;
}
inline std::uint16_t RayHit(const Raw& r) noexcept {
    if (!(r.cam_flags & (1u << 29))) return 0xffff;
    const auto h = r.ray[(r.cam_flags & (1u << 28)) ? 0 : 1];
    return (h >> 63) && ((h >> 59) & 15u) == 1 ? static_cast<std::uint16_t>(h) : 0xffff;
}
struct Classification {
    std::uintptr_t caller{}, primary{}, ui{}, ui_vtable{}, primary_vtable{}, owner{}, p{}, p_vtable{}, p_ui{};
    std::uint32_t flags{}, count{}, local{};
    std::uint16_t ids[2]{0xffff,0xffff}, candidate{0xffff};
    int category{};
};
struct ClassificationExpected {std::uintptr_t caller, ui_vtable, primary_vtable, p_vtable;};
inline bool OwnedChain(const Classification& s,const ClassificationExpected& e) noexcept {
    return s.primary && s.ui==s.primary+0x20 && s.p && s.owner==s.p && s.p_ui==s.ui &&
        s.ui_vtable==e.ui_vtable && s.primary_vtable==e.primary_vtable && s.p_vtable==e.p_vtable &&
        (s.flags&1) && !(s.flags&0x100) && s.count==2 && s.local==0 && s.ids[0]==0 && s.ids[1]==1;
}
inline bool OwnedClassification(const Classification& s,const ClassificationExpected& e) noexcept {
    return s.caller==e.caller && OwnedChain(s,e);
}
inline bool OwnedTeammate(const Classification& s,const ClassificationExpected& e) noexcept {
    return OwnedClassification(s,e) && s.candidate==s.ids[1];
}
inline int FriendlyCategory(const Classification& s,const ClassificationExpected& e,bool filter) noexcept {
    return filter && OwnedTeammate(s,e) && s.category==1 ? 2 : s.category;
}
// One already-validated instance-1 job, on the natural owner thread. The
// queue hook must additionally verify the current player/UI chain on every call.
struct QuietHint {
    Classification owner{};
    std::uint32_t thread{}, owner_thread{}, priority{};
    std::uint16_t job_id{0xffff}, hint{};
    std::uint8_t flag{}, enable{}, extra6{}, extra7{};
    bool armed{};
};
inline bool OwnedQuietHint(const QuietHint& q,const ClassificationExpected& e) noexcept {
    return q.armed && q.thread && q.thread==q.owner_thread && q.job_id==1 &&
        OwnedClassification(q.owner,e) && q.hint==0x78 && q.priority==2 &&
        q.flag==0 && q.enable<=1 && q.extra6==0 && q.extra7==0;
}
inline bool SuppressQuietHint(const QuietHint& q,const ClassificationExpected& e,bool filter) noexcept {
    return filter && OwnedQuietHint(q,e);
}
// The shared UI prompt queues have no owner gate, so instance 1's own prompts
// (the sleep struggle guide among them) reach the local HUD. Any request queued
// from inside the validated instance-1 job is the partner's, whatever its caller.
inline bool OwnedPartnerPrompt(const QuietHint& q,const ClassificationExpected& e) noexcept {
    return q.armed && q.thread && q.thread==q.owner_thread && q.job_id==1 && OwnedChain(q.owner,e);
}
inline bool SuppressPartnerPrompt(const QuietHint& q,const ClassificationExpected& e,bool filter) noexcept {
    return filter && OwnedPartnerPrompt(q,e);
}
struct CensusStamp {
    std::uint16_t hit{0xffff}, ray{0xffff};
    std::uint32_t category{}, cam_flags{}, assist{};
    std::int32_t gain{};
    std::uint32_t hint{}, hint_flags{};
    std::uint8_t hint_active{}, hint_priority{};
};
inline bool SameCensus(const CensusStamp& a,const CensusStamp& b) noexcept {
    return a.hit==b.hit && a.ray==b.ray && a.category==b.category && a.cam_flags==b.cam_flags && a.assist==b.assist && a.gain==b.gain && a.hint==b.hint && a.hint_flags==b.hint_flags &&
        a.hint_active==b.hint_active && a.hint_priority==b.hint_priority;
}
} // namespace mgscoop::native_ui
