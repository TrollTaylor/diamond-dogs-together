// Original MGS Co-op seated vehicle rider: the four reversible code dwords of
// partner_native_ride=seat (docs/anim-round1-seat.md) and the seated state table.
#include "mgscoop/partner_ride_seat.hpp"
#include <cstring>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include "mgscoop/near_code.hpp"
#endif
#include "partner_player_contract.hpp"

namespace mgscoop::partner_ride_seat {
namespace {
namespace c = mgscoop::partner_player_contract;

constexpr Site kSiteTable[kSites] = {
    {"seat", c::ride_seat_rva, c::ride_seat_old, c::ride_seat_old},
    {"cam_play", c::ride_cam_play_rva, c::ride_cam_play_old, c::ride_cam_play_new},
    {"cam_plain", c::ride_cam_plain_rva, c::ride_cam_plain_old, c::ride_cam_plain_new},
    {"cam_end", c::ride_cam_end_rva, c::ride_cam_end_old, c::ride_cam_end_new},
};
static_assert(c::ride_seat_old == (c::ride_seat_base | (2u << 24)), "the retail seat immediate is 2");

std::uint32_t ReadDword(const unsigned char* base, std::uintptr_t rva) noexcept {
    std::uint32_t value = 0;
    std::memcpy(&value, base + rva, sizeof(value));
    return value;
}

const c::Site* ContractSite(std::uintptr_t rva) noexcept {
    for (const auto& site : c::code_sites)
        if (site.rva == rva) return &site;
    return nullptr;
}

// Static "block_0x..." reasons (one per contract block, no formatting).
const char* BlockReason(std::uintptr_t rva) noexcept {
    switch (rva) {
    case 0x1198770: return "block_0x1198770";
    case 0x11987b5: return "block_0x11987b5";
    case 0x11987e5: return "block_0x11987e5";
    case 0x1198834: return "block_0x1198834";
    case 0x1198870: return "block_0x1198870";
    case 0x1198cc0: return "block_0x1198cc0";
    case 0x1198cff: return "block_0x1198cff";
    case 0x1198d22: return "block_0x1198d22";
    case 0x1198d62: return "block_0x1198d62";
    case 0x1198d9d: return "block_0x1198d9d";
    case 0x1198db5: return "block_0x1198db5";
    case 0x1198dbc: return "block_0x1198dbc";
    case 0x11992c3: return "block_0x11992c3";
    case 0x1199508: return "block_0x1199508";
    default: return "block_other";
    }
}

// The site dword covering byte `at` (absolute rva), if any.
bool Masked(std::uintptr_t at) noexcept {
    for (const auto& site : kSiteTable)
        if (at >= site.rva && at < site.rva + 4) return true;
    return false;
}

#ifdef _WIN32
volatile LONG g_armed = 0;
volatile LONG g_seat = 0xFF;

void Emit(LogFn log, const char* line) noexcept { if (log) log(line); }
void EmitFormat(LogFn log, const char* format, const char* a, const char* b) noexcept {
    char line[160]{};
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), format, a, b))) Emit(log, line);
}
#endif
} // namespace

const Site& SiteFor(SiteId id) noexcept {
    const auto index = static_cast<unsigned>(id);
    return kSiteTable[index < kSites ? index : 0];
}

std::uint32_t SeatDword(std::uint8_t seat) noexcept { return c::ride_seat_base | (static_cast<std::uint32_t>(seat & 3u) << 24); }

std::uint8_t AnchorSeat(std::uint8_t seat) noexcept { return seat; }

const char* VerifyImage(const unsigned char* base, std::size_t span) noexcept {
    if (!base) return "base";
    for (const std::uintptr_t rva : c::ride_seat_blocks) {
        const c::Site* site = ContractSite(rva);
        if (!site) return "contract";
        if (site->rva > span || site->size > span - site->rva) return "span";
        for (std::size_t i = 0; i < site->size; ++i) {
            if (Masked(site->rva + i)) continue;
            if (base[site->rva + i] != site->bytes[i]) return BlockReason(rva);
        }
    }
    // Every site dword lies inside a verified block (its expected value is the original).
    for (const auto& s : kSiteTable) {
        bool covered = false;
        for (const std::uintptr_t rva : c::ride_seat_blocks) {
            const c::Site* site = ContractSite(rva);
            if (site && s.rva >= site->rva && s.rva + 4 <= site->rva + site->size) covered = true;
        }
        if (!covered) return "contract";
    }
    return nullptr;
}

SiteState StateOf(const unsigned char* base, SiteId id) noexcept {
    if (!base) return SiteState::other;
    const Site& site = SiteFor(id);
    const std::uint32_t value = ReadDword(base, site.rva);
    if (value == site.original) return SiteState::original;
    if (id == SiteId::seat) return (value & 0x00FFFFFFu) == c::ride_seat_base && (value >> 24) <= 3 ? SiteState::patched : SiteState::other;
    return value == site.patched ? SiteState::patched : SiteState::other;
}

#ifdef _WIN32
bool Arm(void* base, std::size_t span, std::uint8_t seat, LogFn log) noexcept {
    auto* image = static_cast<unsigned char*>(base);
    if (!image || seat > 3) { Emit(log, "partner_ride_seat_patch=refused; reason=args\r\n"); return false; }
    if (const char* why = VerifyImage(image, span)) {
        EmitFormat(log, "partner_ride_seat_patch=refused; reason=%s%s\r\n", why, "");
        return false;
    }
    for (unsigned i = 0; i < kSites; ++i) {
        if (StateOf(image, static_cast<SiteId>(i)) == SiteState::other) {
            EmitFormat(log, "partner_ride_seat_patch=refused; reason=site_%s%s\r\n", kSiteTable[i].name, "");
            return false;
        }
    }
    // Seat first (a later anchor reads it), the End skip last; a failure
    // part way puts back exactly what this call wrote.
    std::uint32_t written_from[kSites]{};
    bool written[kSites]{};
    for (unsigned i = 0; i < kSites; ++i) {
        const Site& s = kSiteTable[i];
        const std::uint32_t current = ReadDword(image, s.rva);
        const std::uint32_t want = i == 0 ? SeatDword(seat) : s.patched;
        if (current == want) continue;
        if (const char* why = near_code::PatchDword(image + s.rva, current, want)) {
            for (unsigned k = i; k-- > 0;)
                if (written[k]) near_code::PatchDword(image + kSiteTable[k].rva, k == 0 ? SeatDword(seat) : kSiteTable[k].patched, written_from[k]);
            EmitFormat(log, "partner_ride_seat_patch=refused; reason=patch_%s_%s\r\n", s.name, why);
            return false;
        }
        written[i] = true;
        written_from[i] = current;
    }
    InterlockedExchange(&g_seat, seat);
    InterlockedExchange(&g_armed, 1);
    char line[192]{};
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
            "partner_ride_seat_patch=applied; seat=%u; sites=0x119883c,0x1198d56,0x1198d81,0x11992e9; seat_dword=0x%08x\r\n",
            unsigned(seat), SeatDword(seat)))) Emit(log, line);
    return true;
}

void Revert(void* base, std::size_t span, LogFn log, const char* reason) noexcept {
    auto* image = static_cast<unsigned char*>(base);
    (void)span;
    if (!image) return;
    bool wrote = false;
    // The End skip first, the seat last (the reverse of Arm).
    for (unsigned i = kSites; i-- > 0;) {
        const Site& s = kSiteTable[i];
        const SiteState state = StateOf(image, static_cast<SiteId>(i));
        if (state != SiteState::patched) continue;
        const std::uint32_t current = ReadDword(image, s.rva);
        if (current == s.original) continue;
        if (const char* why = near_code::PatchDword(image + s.rva, current, s.original)) {
            EmitFormat(log, "partner_ride_seat_patch=refused; reason=revert_%s_%s\r\n", s.name, why);
            return;
        }
        wrote = true;
    }
    for (unsigned i = 0; i < kSites; ++i) {
        if (StateOf(image, static_cast<SiteId>(i)) != SiteState::original) {
            EmitFormat(log, "partner_ride_seat_patch=refused; reason=revert_site_%s%s\r\n", kSiteTable[i].name, "");
            return;
        }
    }
    const bool was = InterlockedExchange(&g_armed, 0) != 0;
    InterlockedExchange(&g_seat, 0xFF);
    if (wrote || was) EmitFormat(log, "partner_ride_seat_patch=reverted; reason=%s%s\r\n", reason ? reason : "unknown", "");
}

bool Armed() noexcept { return g_armed != 0; }
std::uint8_t ArmedSeat() noexcept { return g_armed ? static_cast<std::uint8_t>(g_seat) : 0xFF; }
#endif

namespace {
// StrCode64 (low 48 bits) of each state name; every one is present in
// TppPlayer2_layers.mog (critic_work/mogcheck.py). The names are what Lua sends.
constexpr SeatStates kLight[4] = {
    {"stateVehicleFrontLeftLoop", "stateVehicleGetOnFrontLeft", "stateVehicleGetOffFrontLeft", 0x489157d7d94full, 0xdfb7fc6173d0ull,
     0x3b9161fab2a7ull},
    {"stateVehicleFrontRightLoop", "stateVehicleGetOnFrontRight", "stateVehicleGetOffFrontRight", 0x4470e2406e9dull, 0x00f3b0882fd1ull,
     0xe59bbf7182faull},
    {"stateVehicleBackLeftLoop", "stateVehicleGetOnBackLeft", "stateVehicleGetOffBackLeft", 0xc40b38a1a985ull, 0xfe95b58c6bb0ull,
     0xb94bf30ab6fcull},
    {"stateVehicleBackRightLoop", "stateVehicleGetOnBackRight", "stateVehicleGetOffBackRight", 0x4b8ed7f43128ull, 0x2b9bc4ee90aaull,
     0x97cd2a9b170cull},
};
// Truck seat 1: the MOG has no stateTruckFrontRightLoop (0xd2765cdf5be0 absent).
constexpr SeatStates kTruck[2] = {
    {"stateTruckFrontLeftLoop", "stateTruckGetOnFrontLeft", "stateTruckGetOffFrontLeft", 0x9df6a2daa633ull, 0x946f1acadba5ull,
     0xedcae893d0cfull},
    {"stateVehicleFrontRightLoop", "stateTruckGetOnFrontRight", "stateTruckGetOffFrontRight", 0x4470e2406e9dull, 0x8a991a913969ull,
     0x0db1c7e93c7bull},
};
constexpr std::uint64_t kCode48 = 0xFFFFFFFFFFFFull;
} // namespace

const SeatStates* StatesFor(SeatClass cls, std::uint8_t seat) noexcept {
    if (cls == SeatClass::light) return seat < 4 ? &kLight[seat] : nullptr;
    if (cls == SeatClass::truck) return seat < 2 ? &kTruck[seat] : nullptr;
    return nullptr;
}

bool IsSeatState(std::uint64_t code, std::uint8_t seat) noexcept {
    const std::uint64_t c48 = code & kCode48;
    if (!c48 || c48 == kForbiddenRearSeatState) return false;
    constexpr SeatClass kClasses[] = {SeatClass::light, SeatClass::truck};
    for (const SeatClass cls : kClasses) {
        const SeatStates* s = StatesFor(cls, seat);
        if (s && (c48 == s->loop_code || c48 == s->get_on_code || c48 == s->get_off_code)) return true;
    }
    return false;
}

static_assert(kCnpPposC == c::dm_cnp_ppos_c && kEntryFlagTarget == c::dm_entry_flag_target && kEntryFlagState == c::dm_entry_flag_state &&
                  kEntryMax == c::dm_entry_max && kEntryState == c::dm_entry_state && kEntryFlags == c::dm_entry_flags &&
                  kEntryTarget == c::dm_entry_target && kEntryCnp == c::dm_entry_cnp && kEntryCur == c::dm_entry_cur,
              "the DM record entry layout");

TargetPlan PlanSeatTarget(const EntryView& v, std::uint16_t vehicle, std::uint8_t seat, std::uint8_t entries) noexcept {
    TargetPlan plan{nullptr, false, entries};
    const auto refuse = [&](const char* why) noexcept { plan.refusal = why; return plan; };
    if (!entries || entries > 3) return refuse("entries");
    if (vehicle == 0xFFFF || (vehicle >> 9) != 50 || seat > 3) return refuse("vehicle");
    if (v.cur >= entries) return refuse("cur");
    if (!IsSeatState(v.rec18, seat)) return refuse("state");
    bool named = true, open = true;
    for (unsigned i = 0; i < entries; ++i) {
        if ((v.cnp[i] & kCode48) != kCnpPposC) return refuse("cnp");
        if (!(v.flags[i] & kEntryFlagState) || !IsSeatState(v.state[i], seat)) return refuse("entry_state");
        named = named && v.ids[i] == vehicle && (v.flags[i] & kEntryFlagTarget);
        open = open && v.ids[i] == 0xFFFF && !(v.flags[i] & kEntryFlagTarget);
    }
    if (named) return plan;
    if (!open) return refuse("foreign_target");
    plan.write = true;
    return plan;
}

} // namespace mgscoop::partner_ride_seat
