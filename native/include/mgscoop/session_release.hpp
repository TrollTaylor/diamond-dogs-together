#pragma once
#include <cstddef>
#include <cstdint>

namespace mgscoop {
// The game can destroy the session inside its original OnTerminate before our
// explicit close wrapper runs. Inspect only surviving singleton slots; never
// dereference the former session. Two reads reject partial/changed teardown,
// but are not an atomic snapshot. Caller owns the serialized mission boundary.
template<class ReadPointer>
bool SessionSlotsReleased(std::uintptr_t base, std::size_t span,
                          std::uintptr_t owned_root, ReadPointer read) noexcept {
    constexpr std::uintptr_t root_rva=0x2bef840, main_rva=0x2b9dfb0;
    if(!base || !owned_root || span<root_rva+sizeof(std::uintptr_t)) return false;
    for(unsigned pass=0;pass<2;++pass) {
        std::uintptr_t root=0,session=0,manager=0,main=0,root_after=0;
        if(!read(base+root_rva,root) || root!=owned_root ||
           !read(root+0xcc8,session) || session ||
           !read(root+0xd00,manager) || manager ||
           !read(base+main_rva,main) || main ||
           !read(base+root_rva,root_after) || root_after!=root) return false;
    }
    return true;
}
}
