// Original MGS Co-op F1 vehicles (docs/native-puppet.md "Vehicle boarding"):
// the board block and passengers both ways. Two rel32 call sites of the stock
// ride action are retargeted (partner_input_hook::detail::RetargetCall, the
// partner_defer H2 pattern) to wrappers that run the pure partner_board_policy
// decision on the policy word the driver tick publishes:
//   site A 0x12458AC: the ride pre-query's nearest free seat (4: no ride icon)
//   site B 0x1251479: phase 1's driver-seat search (4: the stock no-seat exit)
// The wrappers only read u16 unit+0x78 and call two read-only getters (SEH);
// they never write game memory. Counters are logged by the driver tick.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <atomic>
#include <cstring>
#include "mgscoop/partner_board.hpp"
#include "mgscoop/near_code.hpp"
#include "mgscoop/partner_input_hook.hpp"
#include "partner_player_contract.hpp"

namespace mgscoop::partner_board {
namespace {
namespace c = mgscoop::partner_player_contract;
namespace hk = mgscoop::partner_input_hook;

static_assert(c::board_unit_riders == kUnitRiders && c::board_unit_index == kUnitIndex, "unit offsets");
static_assert(c::board_p_units == kPUnits && c::board_q_vehicle == kQVehicle && c::q_act == kQAct, "player offsets");
static_assert(c::board_act_passenger == kActPassenger, "passenger act");

// --- Process state (the DLL is pinned; nothing is torn down) ------------------
volatile LONG g_install_tried = 0;
volatile LONG g_installed = 0;
const char* g_refusal = nullptr;
std::uintptr_t g_base = 0;
LogFn g_log = nullptr;
NowFn g_now = nullptr;
void* g_stub_a = nullptr;
void* g_stub_b = nullptr;
detail::NearestFn g_nearest = nullptr;
detail::DriverFn g_driver = nullptr;
detail::TypeFn g_type = nullptr;
detail::CountFn g_count = nullptr;

std::atomic<std::uint64_t> g_policy{0};
// M6 seat 1: the live options (PackOpts; 0 until the remap skip is in) and the phase-1 writer.
std::atomic<std::uint8_t> g_opts{0};
detail::LimitFn g_limit = nullptr;
std::atomic<std::uint32_t> g_seat_one{0};
// M6 seat races: on, the want (vehicle | ms << 32), the deny (vehicle | until << 32), the deny hits.
std::atomic<bool> g_race{false};
std::atomic<std::uint64_t> g_want{kVehicleUnknown};
std::atomic<std::uint64_t> g_deny{kVehicleUnknown};
std::atomic<std::uint32_t> g_race_hits{0};
std::atomic<std::uint32_t> g_calls[2]{}, g_block[2]{}, g_steer[2]{}, g_faults{0};
std::atomic<std::uint32_t> g_last_seat[2]{};
std::atomic<std::uint32_t> g_last_vehicle{kVehicleUnknown}, g_last_type{0xFF}, g_last_count{0xFF};
std::atomic<const char*> g_last_reason{"none"};

void LogFormat(const char* format, ...) noexcept {
    if (!g_log) return;
    char line[256]{};
    va_list arguments;
    va_start(arguments, format);
    const HRESULT result = StringCchVPrintfA(line, ARRAYSIZE(line), format, arguments);
    va_end(arguments);
    if (SUCCEEDED(result)) g_log(line);
}

bool IsAccessFault(DWORD code) noexcept { return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR; }
int ReadFilter(DWORD code) noexcept { return IsAccessFault(code) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH; }

// u16 unit+0x78 (the record index) under SEH.
bool ReadIndex(void* unit, std::uint16_t& out) noexcept {
    if (!unit) return false;
    __try {
        out = *reinterpret_cast<const volatile std::uint16_t*>(static_cast<const unsigned char*>(unit) + kUnitIndex);
        return true;
    } __except (ReadFilter(GetExceptionCode())) {
        return false;
    }
}
// The two read-only getters (GetVehicleType 0x55E7F0, the seat count 0xA27CC0) under SEH.
bool ReadFacts(void* unit, std::uint8_t& type, int& count) noexcept {
    if (!g_type || !g_count) return false;
    __try {
        type = g_type(unit);
        count = g_count(unit);
        return true;
    } __except (ReadFilter(GetExceptionCode())) {
        return false;
    }
}

struct Pick {
    Verdict verdict;
    std::uint8_t first, last;
    std::uint16_t vehicle;
    const char* reason;
};
// The decision both wrappers share. Policy off: no read at all.
Pick Choose(void* unit) noexcept {
    const Policy p = Unpack(g_policy.load(std::memory_order_acquire));
    Pick k{Verdict::stock, 0, 0, p.vehicle, "off"};
    if (p.mode == Mode::off) return k;
    const std::uint32_t now = g_now ? static_cast<std::uint32_t>(g_now()) : 0u;
    // The record index is read only for a live peer-driver policy (DecideVehicle
    // decides lease and seat before it looks at the index).
    std::uint16_t index = 0xFFFF;
    bool index_ok = true;
    if (LeaseLive(p.until_ms, now) && p.peer_seat == 0) {
        index_ok = ReadIndex(unit, index);
        if (!index_ok) g_faults.fetch_add(1, std::memory_order_relaxed);
    }
    k.verdict = DecideVehicle(p, now, index_ok, index, k.reason);
    if (k.verdict != Verdict::seats) return k;
    std::uint8_t type = 0xFF;
    int count = 0;
    const bool facts = ReadFacts(unit, type, count);
    if (!facts) g_faults.fetch_add(1, std::memory_order_relaxed);
    g_last_type.store(facts ? type : 0xFFu, std::memory_order_relaxed);
    g_last_count.store(facts && count >= 0 && count < 0xFF ? static_cast<std::uint32_t>(count) : 0xFFu, std::memory_order_relaxed);
    const Range r = DecideSeats(p, facts, type, count, UnpackOpts(g_opts.load(std::memory_order_acquire)));
    k.reason = r.reason;
    if (!r.ok) {
        k.verdict = Verdict::block;
        return k;
    }
    k.first = r.first;
    k.last = r.last;
    return k;
}

void Note(unsigned site, const Pick& k, std::uint8_t result) noexcept {
    g_calls[site].fetch_add(1, std::memory_order_relaxed);
    if (k.verdict == Verdict::stock) return;
    g_last_vehicle.store(k.vehicle, std::memory_order_relaxed);
    g_last_reason.store(k.reason, std::memory_order_relaxed);
    if (k.verdict == Verdict::block || result == kNoSeat) {
        g_block[site].fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_last_seat[site].store(result, std::memory_order_relaxed);
    g_steer[site].fetch_add(1, std::memory_order_relaxed);
}

// Site A 0x12458AC: the pre-query 0x1245820's nearest free seat (its own range).
// The caller's last error is restored before the original runs (our reads and
// getters never leak into it); Note only touches atomics.
// M6 seat races: the stock verdict's want (site A) and deny (both sites). The index read is the
// wrappers' own SEH read; an unreadable index leaves the stock result.
// The record's origin (unit+0x20, three floats) under SEH.
bool ReadUnitPos(void* unit, float (&out)[3]) noexcept {
    if (!unit) return false;
    __try {
        const auto* p = reinterpret_cast<const volatile float*>(static_cast<const unsigned char*>(unit) + kUnitPos);
        out[0] = p[0];
        out[1] = p[1];
        out[2] = p[2];
        return true;
    } __except (ReadFilter(GetExceptionCode())) {
        return false;
    }
}
std::uint8_t RaceStep(void* unit, const float* pos, Verdict verdict, std::uint8_t result, bool record) noexcept {
    if (!g_race.load(std::memory_order_acquire) || verdict != Verdict::stock || result == kNoSeat) return result;
    std::uint16_t index = 0xFFFF;
    if (!ReadIndex(unit, index) || index >= 0x200) return result;
    const auto vehicle = static_cast<std::uint16_t>(kVehicleBase | index);
    const std::uint32_t now = g_now ? static_cast<std::uint32_t>(g_now()) : 0u;
    float at[3]{};
    if (record && RaceWantSeat(verdict, result) && ReadUnitPos(unit, at) && RaceNear(pos, at))
        g_want.store(static_cast<std::uint64_t>(vehicle) | static_cast<std::uint64_t>(now) << 32, std::memory_order_release);
    const std::uint64_t deny = g_deny.load(std::memory_order_acquire);
    if (RaceDenied(static_cast<std::uint16_t>(deny & 0xFFFFu), static_cast<std::uint32_t>(deny >> 32), now, vehicle)) {
        g_race_hits.fetch_add(1, std::memory_order_relaxed);
        return kNoSeat;
    }
    return result;
}
std::uint8_t SiteA(void* unit, const float* pos, std::uint8_t first, std::uint8_t last) noexcept {
    const DWORD error = GetLastError();
    const Pick k = Choose(unit);
    SetLastError(error);
    std::uint8_t result = kNoSeat;
    if (k.verdict == Verdict::stock) result = g_nearest(unit, pos, first, last);
    else if (k.verdict == Verdict::seats) result = g_nearest(unit, pos, k.first, k.last);
    result = RaceStep(unit, pos, k.verdict, result, true);
    Note(0, k, result);
    return result;
}
// Site B 0x1251479: phase 1 0x1251200's SearchNearestRideSeatForDriver. With seat 1
// live (M6) every call also sets the two phase-1 immediates that run after it in the
// same call: 1 for a steered seat 1, else stock 2. A failed write never seats the
// viewer in seat 1 under the stock driver mapping: seat 1 then answers 4 (counted).
std::uint8_t SiteB(void* unit, const float* pos) noexcept {
    const DWORD error = GetLastError();
    const Pick k = Choose(unit);
    SetLastError(error);
    std::uint8_t result = kNoSeat;
    if (k.verdict == Verdict::stock) result = g_driver(unit, pos);
    else if (k.verdict == Verdict::seats) result = g_nearest(unit, pos, k.first, k.last);
    result = RaceStep(unit, pos, k.verdict, result, false);
    if (g_opts.load(std::memory_order_acquire) && g_limit) {
        const std::uint8_t limit = PhaseOneLimit(k.verdict, result, true);
        const DWORD before = GetLastError();
        const bool written = g_limit(limit);
        SetLastError(before);
        if (!written) {
            g_faults.fetch_add(1, std::memory_order_relaxed);
            if (result == kFrontSeat) result = kNoSeat;
        } else if (limit == kPhaseOneSeatOne) {
            g_seat_one.fetch_add(1, std::memory_order_relaxed);
        }
    }
    Note(1, k, result);
    return result;
}

// The live writer: both phase-1 dwords (`3C imm8 ..`) with `limit`, only over bytes that are
// the stock instruction with either immediate (anything else: false, nothing written).
bool SetPhaseOneLimitLive(std::uint8_t limit) noexcept {
    if (!g_base) return false;
    const std::uintptr_t sites[2] = {c::board_seat1_pass_rva, c::board_seat1_phase_rva};
    const std::uint32_t originals[2] = {c::board_seat1_pass_orig, c::board_seat1_phase_orig};
    for (unsigned i = 0; i < 2; ++i) {
        auto* at = reinterpret_cast<unsigned char*>(g_base + sites[i]);
        std::uint32_t current = 0;
        __try {
            std::memcpy(&current, at, sizeof(current));
        } __except (ReadFilter(GetExceptionCode())) {
            return false;
        }
        const std::uint8_t imm = static_cast<std::uint8_t>(current >> 8);
        if (PhaseOneDword(current, kPhaseOneStock) != originals[i] || (imm != kPhaseOneStock && imm != kPhaseOneSeatOne)) return false;
        const std::uint32_t want = PhaseOneDword(originals[i], limit);
        if (current == want) continue;
        const char* why = near_code::PatchDword(at, current, want);
        if (why && std::strcmp(why, "protect_restore") != 0) return false;
    }
    return true;
}

const c::Site* SiteAt(std::uintptr_t rva) noexcept {
    for (const c::Site& s : c::code_sites)
        if (s.rva == rva) return &s;
    return nullptr;
}
// The rel32 operand of a retargeted call inside a block is ignored (offset of
// the operand inside the block, or SIZE_MAX).
std::size_t MaskedOperand(std::uintptr_t block) noexcept {
    if (block == c::board_a_block_rva) return c::board_a_site + 1 - c::board_a_block_rva;
    if (block == c::board_b_block_rva) return c::board_b_site + 1 - c::board_b_block_rva;
    return static_cast<std::size_t>(-1);
}
// M6: the bytes site B and the seat-one install own: the two phase-1 immediates (stock 2 or
// our 1) and the remap's opcode (stock jne 0x75 or our jmp 0xEB). Review R5: only those two
// values verify; anything else at an owned byte is a foreign change. -1: not an owned byte.
int SeatOneByteOk(std::uintptr_t rva, unsigned char b) noexcept {
    if (rva == c::board_seat1_pass_rva + 1 || rva == c::board_seat1_phase_rva + 1) return b == kPhaseOneStock || b == kPhaseOneSeatOne;
    if (rva == c::board_remap_rva + 3) return b == 0x75 || b == 0xEB;
    return -1;
}

bool SiteLive(std::uintptr_t base, std::uintptr_t site, std::uintptr_t next_ip, const void* stub) noexcept {
    if (!stub) return false;
    std::int32_t rel = 0;
    if (!near_code::Rel32(base + next_ip, reinterpret_cast<std::uintptr_t>(stub), rel)) return false;
    const auto* code = reinterpret_cast<const unsigned char*>(base + site);
    std::int32_t live = 0;
    std::memcpy(&live, code + 1, 4);
    return code[0] == 0xE8 && live == rel;
}

// Puts site A's original rel32 back (B failed). nullptr on success.
const char* RevertA(std::uintptr_t base, void* stub) noexcept {
    std::int32_t rel = 0;
    if (!near_code::Rel32(base + c::board_a_next_ip, reinterpret_cast<std::uintptr_t>(stub), rel)) return "range";
    std::uint32_t current = 0;
    std::memcpy(&current, &rel, 4);
    return near_code::PatchDword(reinterpret_cast<void*>(base + c::board_a_site + 1), current, c::board_a_rel32_orig);
}
} // namespace

namespace detail {
const char* VerifyImage(const void* base, std::size_t span, std::uintptr_t& mismatch_rva) noexcept {
    mismatch_rva = 0;
    const auto* image = static_cast<const unsigned char*>(base);
    if (!image) return "base";
    for (const std::uintptr_t rva : c::board_blocks) {
        mismatch_rva = rva;
        const c::Site* site = SiteAt(rva);
        if (!site) return "contract";
        if (site->rva > span || site->size > span - site->rva) return "span";
        const std::size_t skip = MaskedOperand(rva);
        bool same = true;
        __try {
            for (std::size_t i = 0; i < site->size && same; ++i) {
                if (i >= skip && i < skip + 4) continue;
                const int owned = SeatOneByteOk(site->rva + i, image[site->rva + i]);
                same = owned >= 0 ? owned == 1 : image[site->rva + i] == site->bytes[i];
            }
        } __except (ReadFilter(GetExceptionCode())) {
            return "read";
        }
        if (!same) return "bytes";
    }
    mismatch_rva = 0;
    return nullptr;
}

void BindSeatOneForTest(SeatOpts opts, LimitFn limit) noexcept {
    g_opts.store(PackOpts(opts), std::memory_order_release);
    g_limit = limit;
}
void BindForTest(NearestFn nearest, DriverFn driver, TypeFn type, CountFn count, NowFn now) noexcept {
    g_nearest = nearest;
    g_driver = driver;
    g_type = type;
    g_count = count;
    g_now = now;
}
std::uint8_t SiteAForTest(void* unit, const float* pos, std::uint8_t first, std::uint8_t last) noexcept {
    return SiteA(unit, pos, first, last);
}
std::uint8_t SiteBForTest(void* unit, const float* pos) noexcept { return SiteB(unit, pos); }
void ResetForTest() noexcept {
    g_install_tried = g_installed = 0;
    g_refusal = nullptr;
    g_base = 0;
    g_log = nullptr;
    g_now = nullptr;
    g_stub_a = g_stub_b = nullptr;
    g_nearest = nullptr;
    g_driver = nullptr;
    g_type = nullptr;
    g_count = nullptr;
    g_policy.store(0);
    g_opts.store(0);
    g_limit = nullptr;
    g_seat_one.store(0);
    g_race.store(false);
    g_want.store(kVehicleUnknown);
    g_deny.store(kVehicleUnknown);
    g_race_hits.store(0);
    for (unsigned i = 0; i < 2; ++i) {
        g_calls[i].store(0);
        g_block[i].store(0);
        g_steer[i].store(0);
        g_last_seat[i].store(0);
    }
    g_faults.store(0);
    g_last_vehicle.store(kVehicleUnknown);
    g_last_type.store(0xFF);
    g_last_count.store(0xFF);
    g_last_reason.store("none");
}
} // namespace detail

namespace {
// M6: the seat-1 blocks against the image (the owned bytes accepted in either state), then the
// remap jne -> jmp. nullptr when seat 1 is live.
const char* InstallSeatOne(std::size_t span) noexcept {
    const auto* image = reinterpret_cast<const unsigned char*>(g_base);
    for (const std::uintptr_t rva : c::board_seat1_blocks) {
        const c::Site* site = SiteAt(rva);
        if (!site) return "contract";
        if (site->rva > span || site->size > span - site->rva) return "span";
        bool same = true;
        __try {
            for (std::size_t i = 0; i < site->size && same; ++i) {
                const std::uintptr_t at = site->rva + i;
                const unsigned char b = image[at];
                const int owned = SeatOneByteOk(at, b);
                same = owned >= 0 ? owned == 1 : b == site->bytes[i];
            }
        } __except (ReadFilter(GetExceptionCode())) {
            return "read";
        }
        if (!same) return "bytes";
    }
    auto* remap = reinterpret_cast<unsigned char*>(g_base + c::board_remap_rva);
    std::uint32_t current = 0;
    std::memcpy(&current, remap, sizeof(current));
    if (current == c::board_remap_skip) return nullptr;
    if (current != c::board_remap_orig) return "remap";
    const char* why = near_code::PatchDword(remap, c::board_remap_orig, c::board_remap_skip);
    if (why && std::strcmp(why, "protect_restore") != 0) return "remap_write";
    return nullptr;
}
} // namespace

const char* Install(const void* base, std::size_t span, NowFn now, LogFn log, SeatOpts opts) noexcept {
    if (InterlockedCompareExchange(&g_install_tried, 1, 0) != 0) return g_installed ? nullptr : g_refusal;
    const DWORD error = GetLastError();
    g_base = reinterpret_cast<std::uintptr_t>(base);
    g_log = log;
    g_now = now;
    std::uintptr_t mismatch = 0;
    if (const char* why = detail::VerifyImage(base, span, mismatch)) {
        g_refusal = why;
        LogFormat("partner_board=refused; reason=%s; rva=0x%llx; installed_before=none\r\n", why, static_cast<unsigned long long>(mismatch));
        SetLastError(error);
        return g_refusal;
    }
    g_nearest = reinterpret_cast<detail::NearestFn>(g_base + c::board_nearest_rva);
    g_driver = reinterpret_cast<detail::DriverFn>(g_base + c::board_driver_rva);
    g_type = reinterpret_cast<detail::TypeFn>(g_base + c::board_type_rva);
    g_count = reinterpret_cast<detail::CountFn>(g_base + c::board_count_rva);
    bool warn = false;
    void* stub_a = nullptr;
    const char* a = hk::detail::RetargetCall(g_base + c::board_a_site, g_base + c::board_a_next_ip, c::board_a_rel32_orig,
                                             reinterpret_cast<const void*>(&SiteA), stub_a);
    if (a && std::strcmp(a, "protect_restore") != 0) {
        g_refusal = std::strcmp(a, "bytes") == 0 ? "a_bytes" : std::strcmp(a, "allocate_near") == 0 ? "a_allocate_near"
                  : std::strcmp(a, "range") == 0 ? "a_range" : std::strcmp(a, "protect") == 0 ? "a_protect"
                  : std::strcmp(a, "cache_line") == 0 ? "a_cache_line" : "a_exchange";
        LogFormat("partner_board=refused; reason=%s; at=a; site=0x%llx; installed_before=none\r\n", g_refusal,
                  static_cast<unsigned long long>(c::board_a_site));
        SetLastError(error);
        return g_refusal;
    }
    warn = warn || a != nullptr;
    void* stub_b = nullptr;
    const char* b = hk::detail::RetargetCall(g_base + c::board_b_site, g_base + c::board_b_next_ip, c::board_b_rel32_orig,
                                             reinterpret_cast<const void*>(&SiteB), stub_b);
    if (b && std::strcmp(b, "protect_restore") != 0) {
        // B failed: A goes back to the stock call (its stub stays allocated, never freed).
        const char* reverted = RevertA(g_base, stub_a);
        const bool a_back = !reverted || std::strcmp(reverted, "protect_restore") == 0;
        g_refusal = a_back ? "b_failed" : "b_revert";
        // A left live passes through: the driver never publishes a policy without the install.
        if (!a_back) g_stub_a = stub_a;
        LogFormat("partner_board=refused; reason=%s; at=b; why=%s; a_reverted=%u; site=0x%llx\r\n", g_refusal, b, a_back ? 1u : 0u,
                  static_cast<unsigned long long>(c::board_b_site));
        SetLastError(error);
        return g_refusal;
    }
    warn = warn || b != nullptr;
    g_stub_a = stub_a;
    g_stub_b = stub_b;
    InterlockedExchange(&g_installed, 1);
    LogFormat("partner_board=installed; a=0x%llx; b=0x%llx; stub_a=0x%llx; stub_b=0x%llx; blocks=%u; warn=%s\r\n",
              static_cast<unsigned long long>(c::board_a_site), static_cast<unsigned long long>(c::board_b_site),
              static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(stub_a)),
              static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(stub_b)),
              static_cast<unsigned>(sizeof(c::board_blocks) / sizeof(c::board_blocks[0])), warn ? "protect_restore" : "none");
    if (SeatOne(opts)) {
        // The phase-1 immediates start stock (2); site B owns them from here.
        const char* why = InstallSeatOne(span);
        if (!why) {
            g_limit = &SetPhaseOneLimitLive;
            g_opts.store(PackOpts(opts), std::memory_order_release);
        }
        LogFormat("partner_board=seat_one; result=%s; reason=%s; front=%u; cab=%u; remap=0x%llx; sites=0x%llx,0x%llx\r\n",
                  why ? "refused" : "ok", why ? why : "none", opts.front ? 1u : 0u, opts.cab ? 1u : 0u,
                  static_cast<unsigned long long>(c::board_remap_rva), static_cast<unsigned long long>(c::board_seat1_pass_rva),
                  static_cast<unsigned long long>(c::board_seat1_phase_rva));
    }
    SetLastError(error);
    return nullptr;
}

SeatOpts LiveSeatOpts() noexcept { return UnpackOpts(g_opts.load(std::memory_order_acquire)); }

void SetRace(bool on) noexcept { g_race.store(on, std::memory_order_release); }
bool LiveRace() noexcept { return g_race.load(std::memory_order_acquire); }
void RaceDeny(std::uint16_t vehicle, std::uint32_t ms) noexcept {
    if (vehicle == kVehicleUnknown || !ms || (vehicle >> 9) != kVehicleType) {
        g_deny.store(kVehicleUnknown, std::memory_order_release);
        return;
    }
    const std::uint32_t now = g_now ? static_cast<std::uint32_t>(g_now()) : 0u;
    const std::uint32_t until = now + (ms < kRaceDenyMaxMs ? ms : kRaceDenyMaxMs);
    g_deny.store(static_cast<std::uint64_t>(vehicle) | static_cast<std::uint64_t>(until) << 32, std::memory_order_release);
}
void ReadRace(RaceState& out) noexcept {
    out = RaceState{};
    out.on = g_race.load(std::memory_order_acquire);
    const std::uint32_t now = g_now ? static_cast<std::uint32_t>(g_now()) : 0u;
    const std::uint64_t want = g_want.load(std::memory_order_acquire);
    out.want = static_cast<std::uint16_t>(want & 0xFFFFu);
    out.want_age_ms = out.want == kVehicleUnknown ? 0xFFFFFFFFu : now - static_cast<std::uint32_t>(want >> 32);
    const std::uint64_t deny = g_deny.load(std::memory_order_acquire);
    const auto dv = static_cast<std::uint16_t>(deny & 0xFFFFu);
    const auto du = static_cast<std::uint32_t>(deny >> 32);
    const bool live = dv != kVehicleUnknown && static_cast<std::uint32_t>(du - now) <= kRaceDenyMaxMs;
    out.deny = live ? dv : kVehicleUnknown;
    out.deny_left_ms = live ? du - now : 0;
    out.hits = g_race_hits.load(std::memory_order_relaxed);
}

bool Installed() noexcept { return g_installed != 0; }
const char* InstallRefusal() noexcept { return g_refusal; }

bool PatchLive(const void* base) noexcept {
    const auto b = reinterpret_cast<std::uintptr_t>(base);
    if (!g_installed || !b) return false;
    return SiteLive(b, c::board_a_site, c::board_a_next_ip, g_stub_a) && SiteLive(b, c::board_b_site, c::board_b_next_ip, g_stub_b);
}

// Review R5: without site B nothing resets the immediates a steered call left at 1, and a stock
// right-door driver would then become a passenger of an empty vehicle. Called from the driver tick
// only while site B is not live (no wrapper call can write them then): back to the stock 2.
bool RestorePhaseOneIfBLost(const void* base) noexcept {
    const auto b = reinterpret_cast<std::uintptr_t>(base);
    if (!g_installed || !b || b != g_base || !g_limit) return false;
    if (SiteLive(b, c::board_b_site, c::board_b_next_ip, g_stub_b)) return false;
    bool changed = false;
    const std::uintptr_t sites[2] = {c::board_seat1_pass_rva, c::board_seat1_phase_rva};
    for (const std::uintptr_t rva : sites) {
        const unsigned char imm = reinterpret_cast<const volatile unsigned char*>(b + rva)[1];
        if (imm == kPhaseOneSeatOne) changed = true;
    }
    if (!changed) return false;
    return SetPhaseOneLimitLive(kPhaseOneStock);
}

void Publish(std::uint64_t policy) noexcept { g_policy.store(policy, std::memory_order_release); }
std::uint64_t Published() noexcept { return g_policy.load(std::memory_order_acquire); }

void ReadStats(Stats& out) noexcept {
    out = Stats{};
    for (unsigned i = 0; i < 2; ++i) {
        out.calls[i] = g_calls[i].load(std::memory_order_relaxed);
        out.block[i] = g_block[i].load(std::memory_order_relaxed);
        out.steer[i] = g_steer[i].load(std::memory_order_relaxed);
        out.last_seat[i] = static_cast<std::uint8_t>(g_last_seat[i].load(std::memory_order_relaxed));
    }
    out.last_vehicle = static_cast<std::uint16_t>(g_last_vehicle.load(std::memory_order_relaxed));
    out.last_type = static_cast<std::uint8_t>(g_last_type.load(std::memory_order_relaxed));
    out.last_count = static_cast<std::uint8_t>(g_last_count.load(std::memory_order_relaxed));
    out.faults = g_faults.load(std::memory_order_relaxed);
    out.last_reason = g_last_reason.load(std::memory_order_relaxed);
    out.seat_one = g_seat_one.load(std::memory_order_relaxed);
}

} // namespace mgscoop::partner_board
