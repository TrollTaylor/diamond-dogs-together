// MGS_COOP_HORSE_DRIVE_V1 rrec / getoff live reads and the one write (native/include/mgscoop/horse_ride.hpp; contract
// native/horse_ride_contract.hpp).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstring>
#include "mgscoop/horse_ride.hpp"
#include "horse_ride_contract.hpp"

namespace mgscoop::horse_ride {
namespace {
namespace c = horse_ride_contract;
template <class T>
bool Get(std::uintptr_t address, T& value) noexcept {
    SIZE_T read = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), &value, sizeof(value), &read) &&
           read == sizeof(value);
}
bool Inside(std::uintptr_t image, std::size_t span, std::uintptr_t v) noexcept { return v >= image && v - image < span; }
// The stock get-off's three byte stores (ride state 5 at 0x141236BA3 / 0x141236BB4 / 0x141236BB8), SEH-guarded.
bool Store(std::uintptr_t sub, std::uintptr_t rec) noexcept {
    __try {
        if (sub) *reinterpret_cast<volatile std::uint8_t*>(sub + c::sub_select) = c::getoff_select;
        *reinterpret_cast<volatile std::uint8_t*>(rec + c::rec_next) = c::getoff_state;
        *reinterpret_cast<volatile std::uint8_t*>(rec + c::rec_again) = 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}

const char* PinsReason(std::uintptr_t image, std::size_t span) noexcept {
    if (!image || span < 0x3000000) return "pins";
    unsigned char buf[0x100];
    for (const auto& s : c::sites) {
        if (s.size > sizeof(buf) || !Inside(image, span, image + s.rva + s.size)) return "pins";
        SIZE_T read = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(image + s.rva), buf, s.size, &read) || read != s.size ||
            std::memcmp(buf, s.bytes, s.size) != 0)
            return "pins";
    }
    for (const auto& s : c::slots) {
        std::uintptr_t target = 0;
        if (!Get(image + s.vtable_rva + s.offset, target) || target != image + s.target_rva) return "pins";
    }
    return nullptr;
}

std::uintptr_t FindPlugin(std::uintptr_t image, std::uintptr_t block, std::uintptr_t player) noexcept {
    if (!image || !block || !player) return 0;
    for (std::size_t i = 0; i < c::block_action_slots; ++i) {
        std::uintptr_t p = 0, vt = 0;
        if (!Get(block + c::block_actions + 8 * i, p) || !p || (p & 7u) || !Get(p, vt)) continue;
        std::uintptr_t obj = 0;
        if (vt == image + c::ride_vtable) obj = p;
        else if (vt == image + c::ride_vtable_10) obj = p - 0x10;
        else if (vt == image + c::ride_vtable_30) obj = p - 0x30;
        else continue;
        std::uintptr_t ovt = 0, owner = 0, owner_player = 0;
        if (!Get(obj, ovt) || ovt != image + c::ride_vtable || !Get(obj + c::obj_block, owner) || owner != block ||
            !Get(owner + c::block_player, owner_player) || owner_player != player)
            continue;
        return obj;
    }
    return 0;
}

const char* Read(std::uintptr_t plugin, std::uint32_t id, std::uint32_t id_count, Rec& out) noexcept {
    out = Rec{};
    out.plugin = plugin;
    std::uintptr_t node = 0, records = 0;
    std::int32_t base = 0;
    if (!plugin || !Get(plugin + c::obj_node, node) || !node || !Get(node + c::node_base_id, base) || !Get(plugin + c::obj_records, records) ||
        !records)
        return "node";
    if (static_cast<std::int64_t>(id) < base || static_cast<std::int64_t>(id) - base >= static_cast<std::int64_t>(id_count) || id_count > 16)
        return "index";
    const std::uintptr_t rec = records + static_cast<std::uintptr_t>(static_cast<std::int64_t>(id) - base) * c::record_size;
    std::uintptr_t sub = 0;
    if (!Get(rec + c::rec_state, out.state) || !Get(rec + c::rec_next, out.next) || !Get(rec + c::rec_again, out.again) ||
        !Get(rec + c::rec_sub, sub))
        return "record";
    out.rec = rec;
    out.sub = sub;
    if (sub && (!Get(sub + c::sub_rider, out.rider) || !Get(sub + c::sub_select, out.select) || !Get(sub + c::sub_forced, out.forced)))
        out.sub = 0;
    out.ok = true;
    return nullptr;
}

const char* ForceGetOff(std::uintptr_t plugin, std::uint32_t id, std::uint32_t id_count, Rec& r) noexcept {
    if (const char* why = Read(plugin, id, id_count, r)) return why;
    if (const char* why = GetOffRefusal(r)) return why;
    // sub+0x82 only when the sub record names this rider (as the stock check at 0x141236B9C).
    const std::uintptr_t sub = r.sub && r.rider == static_cast<std::uint16_t>(id & 0x1ffu) ? r.sub : 0;
    if (!Store(sub, r.rec)) return "write";
    return nullptr;
}

} // namespace mgscoop::horse_ride
