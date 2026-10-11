#pragma once
#include <cstdint>
namespace mgscoop::teammate_targeting {
struct Scope {
    std::uintptr_t caller{},camera{},camera_vtable{},secondary_vtable{},owner{},player{},player_vtable{},player_camera{},id_out{},position_out{};
    std::uint32_t flags{},count{},local{};
    std::uint16_t ids[2]{0xffff,0xffff},candidate{0xffff};
};
struct Expected {std::uintptr_t caller,camera_vtable,secondary_vtable,player_vtable;};
inline bool Owned(const Scope& s,const Expected& e) noexcept {
    return s.camera && s.player && s.owner==s.player && s.caller==e.caller &&
        s.camera_vtable==e.camera_vtable && s.secondary_vtable==e.secondary_vtable && s.player_vtable==e.player_vtable &&
        s.player_camera==s.camera+0x20 && s.id_out==s.camera+0x3dc && s.position_out==s.camera+0x3e0 &&
        (s.flags&1) && !(s.flags&0x100) && s.count==2 && s.local==0 && s.ids[0]==0 && s.ids[1]==1;
}
inline bool Teammate(const Scope& s,const Expected& e) noexcept {return Owned(s,e) && s.candidate==s.ids[1];}
} // namespace mgscoop::teammate_targeting
