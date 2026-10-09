// Original MGS Co-op native puppet: equipment manifest codec (docs/native-puppet.md 6.2).
#include "mgscoop/partner_equip_wire.hpp"
#include <cstring>

namespace mgscoop::partner_equip_wire {
namespace {
void Put16(std::uint8_t* out, std::uint16_t v) noexcept {
    out[0] = static_cast<std::uint8_t>(v);
    out[1] = static_cast<std::uint8_t>(v >> 8);
}
void Put32(std::uint8_t* out, std::uint32_t v) noexcept {
    for (unsigned i = 0; i < 4; ++i) out[i] = static_cast<std::uint8_t>(v >> (8 * i));
}
std::uint16_t Get16(const std::uint8_t* in) noexcept { return static_cast<std::uint16_t>(in[0] | (in[1] << 8)); }
std::uint32_t Get32(const std::uint8_t* in) noexcept {
    return static_cast<std::uint32_t>(in[0]) | (static_cast<std::uint32_t>(in[1]) << 8) | (static_cast<std::uint32_t>(in[2]) << 16) |
           (static_cast<std::uint32_t>(in[3]) << 24);
}
bool ValidInv(std::uint8_t inv) noexcept { return inv <= kInvSlotMax || inv == kInvSlotUnknown; }
} // namespace

bool SameSlots(const Manifest& a, const Manifest& b) noexcept {
    for (std::size_t i = 0; i < kSlots; ++i) {
        const Slot& x = a.slots[i];
        const Slot& y = b.slots[i];
        if (x.base_id != y.base_id || x.alias != y.alias || x.type != y.type || std::memcmp(x.recipe, y.recipe, kRecipe) != 0) return false;
    }
    return true;
}

std::size_t Encode(const Manifest& m, std::uint8_t (&out)[kSize]) noexcept {
    std::memset(out, 0, sizeof(out));
    if (m.revision == 0 || m.epoch == 0 || !ValidInv(m.inv_slot)) return 0;
    for (const Slot& s : m.slots)
        if (s.type > kMaxGunType) return 0;
    out[0] = kMagic;
    out[1] = kVersion;
    Put16(out + 2, m.revision);
    Put32(out + 4, m.epoch);
    out[8] = m.inv_slot;
    out[9] = static_cast<std::uint8_t>(kSlots);
    for (std::size_t i = 0; i < kSlots; ++i) {
        std::uint8_t* s = out + kHeader + i * kSlotSize;
        Put16(s, m.slots[i].base_id);
        Put16(s + 2, m.slots[i].alias);
        std::memcpy(s + 4, m.slots[i].recipe, kRecipe);
        out[kTypes + i] = m.slots[i].type;
    }
    return kSize;
}

const char* ResultName(Result r) noexcept {
    switch (r) {
    case Result::ok: return "ok";
    case Result::size: return "size";
    case Result::magic: return "magic";
    case Result::version: return "version";
    case Result::revision: return "revision";
    case Result::epoch: return "epoch";
    case Result::stale_epoch: return "stale_epoch";
    case Result::inv_slot: return "inv_slot";
    case Result::slot_count: return "slot_count";
    case Result::type: return "type";
    default: return "reserved";
    }
}

Result Decode(const std::uint8_t* data, std::size_t size, std::uint32_t current_epoch, Manifest& out) noexcept {
    if (!data || (size != kSizeV1 && size != kSize)) return Result::size;
    if (data[0] != kMagic) return Result::magic;
    if (data[1] != kVersion1 && data[1] != kVersion) return Result::version;
    // Each version has exactly one layout.
    if ((data[1] == kVersion1) != (size == kSizeV1)) return Result::size;
    Manifest m{};
    m.version = data[1];
    m.revision = Get16(data + 2);
    if (m.revision == 0) return Result::revision;
    m.epoch = Get32(data + 4);
    if (m.epoch == 0) return Result::epoch;
    if (current_epoch && m.epoch != current_epoch) return Result::stale_epoch;
    m.inv_slot = data[8];
    if (!ValidInv(m.inv_slot)) return Result::inv_slot;
    if (data[9] != kSlots) return Result::slot_count;
    if (data[10] || data[11]) return Result::reserved;
    for (std::size_t i = 0; i < kSlots; ++i) {
        const std::uint8_t* s = data + kHeader + i * kSlotSize;
        m.slots[i].base_id = Get16(s);
        m.slots[i].alias = Get16(s + 2);
        std::memcpy(m.slots[i].recipe, s + 4, kRecipe);
    }
    if (m.version == kVersion) {
        if (data[kTypes + kSlots]) return Result::reserved;
        for (std::size_t i = 0; i < kSlots; ++i) {
            if (data[kTypes + i] > kMaxGunType) return Result::type;
            m.slots[i].type = data[kTypes + i];
        }
    }
    out = m;
    return Result::ok;
}

bool Accept(Store& store, const std::uint8_t* data, std::size_t size, std::uint32_t current_epoch) noexcept {
    Manifest m{};
    const Result r = Decode(data, size, current_epoch, m);
    if (r == Result::stale_epoch) { ++store.stale; return false; }
    if (r != Result::ok) { ++store.invalid; return false; }
    // A duplicate repeats the stored message: the same revision and epoch AND the same content. A
    // 0.12.0 sender restarts at revision 1 every mission (its sender was per mission) while this
    // receiver keeps the session's manifest across missions (MissionStart seeds it): its new
    // loadout under the old revision number must replace the stored one, never be dropped.
    if (store.have && store.manifest.revision == m.revision && store.manifest.epoch == m.epoch &&
        store.manifest.version == m.version && store.manifest.inv_slot == m.inv_slot && SameSlots(store.manifest, m)) {
        ++store.duplicates;
        return false;
    }
    store.have = true;
    store.manifest = m;
    ++store.received;
    return true;
}

std::uint16_t BaseFromTables(const std::uint16_t* packed, std::size_t count, const std::uint8_t* receivers, std::size_t rows,
                             std::uint8_t type, std::uint8_t receiver) noexcept {
    if (!packed || !receivers || !GunType(type) || !receiver) return 0;
    for (std::size_t e = 1; e < count; ++e) {
        const std::uint16_t p = packed[e];
        if ((p & 0x3Fu) != type) continue;
        const std::size_t row = p >> 6;
        if (row >= 1 && row <= rows && receivers[row - 1] == receiver) return TableIndexToId(static_cast<std::uint32_t>(e));
    }
    return 0;
}

} // namespace mgscoop::partner_equip_wire
