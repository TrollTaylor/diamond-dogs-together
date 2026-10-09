// Original MGS Co-op aim parity: the aim-target mirror (docs/native-puppet.md
// "Facing"; partner_aim_point.hpp). Contract: native/partner_player_contract.hpp
// ("Aim parity", tools/partner_aim_sites.py).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xmmintrin.h>
#include <strsafe.h>
#include <cstdarg>
#include <cstring>
#include "mgscoop/partner_aim_point.hpp"
#include "mgscoop/lab_config.hpp"
#include "mgscoop/partner_aim.hpp"
#include "partner_player_contract.hpp"

namespace mgscoop::partner_aim_point {
namespace {
namespace c = mgscoop::partner_player_contract;
namespace pa = mgscoop::partner_aim;
static_assert(c::q_aim_stride == 16 && c::q_aim_rec_stride == 0x30 && c::q_aim_rec_prev == 0x10 && c::q_aim_rec_timers == 0x24 &&
                  c::q_aim_rec_timers_end == 0x30,
              "the aim slot and record layout (Q vt+0x18 0xFE0080, vt+0x28 0xFDEDD0)");

volatile LONG64 g_base = 0;
LogFn g_log = nullptr;

// The publication (seqlock: odd while written).
volatile LONG g_seq = 0;
Publication g_pub{};

// Hook-thread state.
bool g_applied = false;
std::uintptr_t g_p = 0, g_p_vt = 0, g_q = 0, g_arr = 0, g_rec = 0;
alignas(16) unsigned char g_orig_q10[16]{};
alignas(16) unsigned char g_orig_rec[c::q_aim_rec_stride]{};
float g_last[3]{};
std::uint32_t g_epoch = 0;
bool g_over_logged = false;
pa::Reason g_logged_reason = pa::Reason::none;
bool g_logged_any = false;
// Event lines (installed, refused, restored, restore_dropped) per mission
// epoch: a chain that flips between refused and ok every frame must not flood
// the log at frame rate. The last line says the rest are suppressed.
constexpr unsigned kLineCap = 64;
unsigned g_lines = 0;

// Any thread.
volatile LONG g_forget = 0, g_forget_epoch = 0, g_blocked = 0, g_shutdown = 0, g_override = -1;
volatile LONG g_latched = 0, g_applied_pub = 0, g_want_pub = 0;
// Statistics (atomics; the driver reads them).
volatile LONG g_state = 0, g_reason = 0, g_writes = 0, g_refused = 0, g_holds = 0, g_over = 0, g_restores = 0, g_drops = 0;
volatile LONG g_seen_ok = 0, g_written_ok = 0;
volatile LONG g_seen[3] = {0, 0, 0}, g_written[3] = {0, 0, 0};  // float bits

template <class T>
T Load(std::uintptr_t a) noexcept { T v{}; std::memcpy(&v, reinterpret_cast<const void*>(a), sizeof(v)); return v; }
LONG Bits(float v) noexcept { LONG b = 0; std::memcpy(&b, &v, 4); return b; }
float FromBits(LONG b) noexcept { float v = 0.0f; std::memcpy(&v, &b, 4); return v; }
std::uintptr_t Base() noexcept { return static_cast<std::uintptr_t>(g_base); }

void LogLine(const char* format, ...) noexcept {
    if (!g_log) return;
    char line[256]{};
    va_list args;
    va_start(args, format);
    const HRESULT hr = StringCchVPrintfA(line, ARRAYSIZE(line), format, args);
    va_end(args);
    if (SUCCEEDED(hr)) g_log(line);
}
int Milli(float v) noexcept { return v == v && v < 2e6f && v > -2e6f ? static_cast<int>(v * 1000.0f) : 0; }

bool ReadPub(Publication& out) noexcept {
    for (int tries = 0; tries < 2; ++tries) {
        const LONG s1 = g_seq;
        if (s1 & 1) continue;
        MemoryBarrier();
        std::memcpy(&out, const_cast<const Publication*>(&g_pub), sizeof(out));
        MemoryBarrier();
        if (g_seq == s1) return true;
    }
    return false;
}

void SetState(State state, pa::Reason reason) noexcept {
    InterlockedExchange(&g_state, static_cast<LONG>(state));
    InterlockedExchange(&g_reason, static_cast<LONG>(reason));
}
// One event line of this epoch's budget: false once spent; `note` marks the last.
bool TakeLine(const char*& note) noexcept {
    note = "";
    if (g_lines >= kLineCap) return false;
    if (++g_lines == kLineCap) note = "; further=suppressed";
    return true;
}
// A refusal (counted; logged once per change of reason, within the budget).
void Refuse(pa::Reason reason) noexcept {
    InterlockedIncrement(&g_refused);
    SetState(State::refused, reason);
    if (!g_logged_any || g_logged_reason != reason) {
        g_logged_any = true;
        g_logged_reason = reason;
        const char* note = "";
        if (TakeLine(note)) LogLine("partner_aim=refused; reason=%s; applied=%u%s\r\n", pa::ReasonName(reason), g_applied ? 1u : 0u, note);
    }
}
void Hold(pa::Reason reason) noexcept {
    InterlockedIncrement(&g_holds);
    SetState(State::hold, reason);
}
void Drop() noexcept {
    if (g_applied) InterlockedIncrement(&g_drops);
    g_applied = false;
    g_p = g_p_vt = g_q = g_arr = g_rec = 0;
    InterlockedExchange(&g_applied_pub, 0);
}
void ForgetPending() noexcept {
    if (!InterlockedExchange(&g_forget, 0)) return;
    Drop();
    g_over_logged = false;
    g_logged_any = false;
    g_lines = 0;
    InterlockedExchange(&g_latched, 0);
    InterlockedExchange(&g_blocked, InterlockedCompareExchange(&g_forget_epoch, 0, 0));
}
int AccessFilter(DWORD code) noexcept {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}
void Latch() noexcept {
    Drop();
    if (InterlockedExchange(&g_latched, 1) == 0) LogLine("partner_aim=fault; aim_mirror=disabled\r\n");
    SetState(State::latched, pa::Reason::fault);
}

// The slot chain of P (inside the caller's __try): flags, owner, Q class and
// its synchronizer slots, the count, the array and the record (16-aligned).
pa::Reason Chain(std::uintptr_t P, std::uintptr_t& q, std::uintptr_t& arr, std::uintptr_t& rec) noexcept {
    q = arr = rec = 0;
    const std::uintptr_t base = Base();
    if (Load<std::uint32_t>(P + c::p_flags) & (c::flag_online | c::flag_alt_dispatch)) return pa::Reason::online;
    if (Load<std::uint32_t>(P + c::p_local) == 1u || Load<std::uint32_t>(P + c::p_local_id) == 1u) return pa::Reason::owner;
    q = Load<std::uintptr_t>(P + c::p_q);
    if (!q) return pa::Reason::q_vtable;
    const std::uintptr_t vt = Load<std::uintptr_t>(q);
    if (vt != base + c::q_vtable_rva) return pa::Reason::q_vtable;
    if (Load<std::uintptr_t>(vt + c::q_vt_aim_push) != base + c::q_aim_push_rva ||
        Load<std::uintptr_t>(vt + c::q_vt_aim_lerp) != base + c::q_aim_lerp_rva ||
        Load<std::uintptr_t>(vt + c::q_vt_aim_snap) != base + c::q_aim_snap_rva)
        return pa::Reason::slots;
    const std::uintptr_t s = Load<std::uintptr_t>(q + c::q_s);
    if (!s || Load<std::uint32_t>(s + c::s_count) <= 1u) return pa::Reason::count;
    arr = Load<std::uintptr_t>(q + c::q_aim);
    if (!arr || (arr & 15u)) return pa::Reason::array;
    rec = Load<std::uintptr_t>(q + c::q_aim_rec);
    if (!rec || (rec & 15u)) return pa::Reason::record;
    return pa::Reason::none;
}

// xyz of a 16-byte vector at `at` replaced by p (w kept): one aligned store.
void StoreXyz(std::uintptr_t at, const float (&p)[3]) noexcept {
    float v[4]{};
    std::memcpy(v, reinterpret_cast<const void*>(at), sizeof(v));
    v[0] = p[0]; v[1] = p[1]; v[2] = p[2];
    _mm_store_ps(reinterpret_cast<float*>(at), _mm_loadu_ps(v));
}
bool SameXyz(std::uintptr_t at, const float (&p)[3]) noexcept {
    float v[3]{};
    std::memcpy(v, reinterpret_cast<const void*>(at), sizeof(v));
    return std::memcmp(v, p, sizeof(v)) == 0;
}
void Seen(std::uintptr_t q10) noexcept {
    float v[3]{};
    std::memcpy(v, reinterpret_cast<const void*>(q10), sizeof(v));
    for (unsigned i = 0; i < 3; ++i) InterlockedExchange(&g_seen[i], Bits(v[i]));
    InterlockedExchange(&g_seen_ok, 1);
}

// Restore what the mirror wrote (hook thread, no batch in flight). Fields
// the game rewrote since our last write keep the game's value. A chain that
// no longer holds drops the state without writing and says why (capped).
void Restore() noexcept {
    if (!g_applied) return;
    if (g_shutdown) { Drop(); return; }
    bool restored = false;
    const char* dropped = nullptr;
    __try {
        const std::uintptr_t base = Base();
        std::uintptr_t q = 0, arr = 0, rec = 0;
        const bool slot_ok = Load<std::uintptr_t>(base + c::player_slot_rva) == g_p && Load<std::uintptr_t>(g_p) == g_p_vt;
        const pa::Reason why = slot_ok ? Chain(g_p, q, arr, rec) : pa::Reason::chain;
        if (why != pa::Reason::none || q != g_q || arr != g_arr || rec != g_rec) {
            SetState(State::refused, pa::Reason::chain);
            dropped = !slot_ok ? "player" : why != pa::Reason::none ? pa::ReasonName(why) : "object";
        } else {
            const std::uintptr_t q10 = arr + c::q_aim_stride;  // id 1
            const std::uintptr_t r = rec + c::q_aim_rec_stride;
            Seen(q10);
            if (SameXyz(q10, g_last)) std::memcpy(reinterpret_cast<void*>(q10), g_orig_q10, sizeof(g_orig_q10));
            if (SameXyz(r, g_last)) std::memcpy(reinterpret_cast<void*>(r), g_orig_rec, 0x10);
            if (SameXyz(r + c::q_aim_rec_prev, g_last))
                std::memcpy(reinterpret_cast<void*>(r + c::q_aim_rec_prev), g_orig_rec + c::q_aim_rec_prev, 0x10);
            unsigned char zero[c::q_aim_rec_timers_end - c::q_aim_rec_timers]{};
            if (std::memcmp(reinterpret_cast<const void*>(r + c::q_aim_rec_timers), zero, sizeof(zero)) == 0)
                std::memcpy(reinterpret_cast<void*>(r + c::q_aim_rec_timers), g_orig_rec + c::q_aim_rec_timers, sizeof(zero));
            restored = true;
        }
    } __except (AccessFilter(GetExceptionCode())) {
        Latch();
        return;
    }
    if (restored) {
        InterlockedIncrement(&g_restores);
        g_applied = false;
        InterlockedExchange(&g_applied_pub, 0);
        SetState(State::off, pa::Reason::off);
        const char* note = "";
        if (TakeLine(note)) LogLine("partner_aim=restored; restores=%ld%s\r\n", static_cast<long>(g_restores), note);
    } else {
        Drop();
        const char* note = "";
        if (dropped && TakeLine(note))
            LogLine("partner_aim=restore_dropped; reason=chain; changed=%s; written=0%s\r\n", dropped, note);
    }
}
} // namespace

const char* StateName(State state) noexcept {
    switch (state) {
    case State::wait: return "wait";
    case State::ok: return "ok";
    case State::hold: return "hold";
    case State::refused: return "refused";
    case State::latched: return "latched";
    default: return "off";
    }
}

void Bind(const void* base, LogFn log) noexcept {
    g_log = log;
    InterlockedExchange64(&g_base, static_cast<LONG64>(reinterpret_cast<std::uintptr_t>(base)));
}

void Publish(const Publication& publication) noexcept {
    InterlockedIncrement(&g_seq);  // odd
    MemoryBarrier();
    std::memcpy(const_cast<Publication*>(&g_pub), &publication, sizeof(publication));
    MemoryBarrier();
    InterlockedIncrement(&g_seq);  // even
    InterlockedExchange(&g_want_pub, publication.want ? 1 : 0);
}

void Step(std::uintptr_t P, std::uint64_t now_ms) noexcept {
    ForgetPending();
    if (g_shutdown) { SetState(State::off, pa::Reason::shutdown); return; }
    Publication pub{};
    if (!ReadPub(pub)) { Hold(pa::Reason::torn); return; }
    if (pub.epoch != g_epoch) {
        // A new epoch: whatever the old state referred to is not touched.
        Drop();
        g_epoch = pub.epoch;
        g_over_logged = false;
        g_lines = 0;
    }
    const LONG blocked = g_blocked;
    if (blocked && pub.epoch == static_cast<std::uint32_t>(blocked)) { Restore(); SetState(State::off, pa::Reason::epoch); return; }
    if (!pub.want) {
        Restore();
        SetState(State::off, static_cast<pa::Reason>(pub.reason ? pub.reason : static_cast<std::uint8_t>(pa::Reason::off)));
        return;
    }
    if (g_latched) { SetState(State::latched, pa::Reason::fault); return; }
    if (!pub.valid) {
        // No body or look this tick: the slot keeps its last value.
        if (g_applied) Hold(static_cast<pa::Reason>(pub.reason));
        else SetState(State::wait, static_cast<pa::Reason>(pub.reason));
        return;
    }
    if (now_ms > pub.ms + pa::kStaleMs) { Hold(pa::Reason::stale); return; }
    if (!pa::CoordOk(pub.p)) { Refuse(pa::Reason::range); return; }
    pa::Reason why = pa::Reason::none;
    bool wrote = false, overwritten = false;
    float before[3]{};
    __try {
        std::uintptr_t q = 0, arr = 0, rec = 0;
        why = Chain(P, q, arr, rec);
        if (why == pa::Reason::none) {
            const std::uintptr_t q10 = arr + c::q_aim_stride;  // id 1 only; instance 0's slot is never touched
            const std::uintptr_t r = rec + c::q_aim_rec_stride;
            // A different object than the one the originals came from: never restored into.
            if (g_applied && (P != g_p || q != g_q || arr != g_arr || rec != g_rec)) Drop();
            Seen(q10);
            if (!g_applied) {
                std::memcpy(g_orig_q10, reinterpret_cast<const void*>(q10), sizeof(g_orig_q10));
                std::memcpy(g_orig_rec, reinterpret_cast<const void*>(r), sizeof(g_orig_rec));
                g_applied = true;
                g_p = P;
                g_p_vt = Load<std::uintptr_t>(P);
                g_q = q;
                g_arr = arr;
                g_rec = rec;
                InterlockedExchange(&g_applied_pub, 1);
                const char* note = "";
                if (TakeLine(note))
                    LogLine("partner_aim=installed; q10_orig_mm=%d,%d,%d; target_mm=%d,%d,%d; src=%s%s\r\n",
                            Milli(Load<float>(q10)), Milli(Load<float>(q10 + 4)), Milli(Load<float>(q10 + 8)), Milli(pub.p[0]),
                            Milli(pub.p[1]), Milli(pub.p[2]), pa::SourceName(static_cast<pa::Source>(pub.src)), note);
            } else if (!SameXyz(q10, g_last)) {
                overwritten = true;
                std::memcpy(before, g_last, sizeof(before));
            }
            StoreXyz(q10, pub.p);
            StoreXyz(r, pub.p);
            StoreXyz(r + c::q_aim_rec_prev, pub.p);
            std::memset(reinterpret_cast<void*>(r + c::q_aim_rec_timers), 0, c::q_aim_rec_timers_end - c::q_aim_rec_timers);
            std::memcpy(g_last, pub.p, sizeof(g_last));
            wrote = true;
        }
    } __except (AccessFilter(GetExceptionCode())) {
        Latch();
        return;
    }
    if (why != pa::Reason::none) {
        // A chain that no longer holds: the old object is not written again.
        Drop();
        Refuse(why);
        return;
    }
    if (overwritten) {
        InterlockedIncrement(&g_over);
        if (!g_over_logged) {
            g_over_logged = true;
            LogLine("partner_aim=overwritten; q10_mm=%d,%d,%d; last_mm=%d,%d,%d\r\n", Milli(FromBits(g_seen[0])), Milli(FromBits(g_seen[1])),
                    Milli(FromBits(g_seen[2])), Milli(before[0]), Milli(before[1]), Milli(before[2]));
        }
    }
    if (wrote) {
        InterlockedIncrement(&g_writes);
        for (unsigned i = 0; i < 3; ++i) InterlockedExchange(&g_written[i], Bits(pub.p[i]));
        InterlockedExchange(&g_written_ok, 1);
        SetState(State::ok, pa::Reason::none);
        g_logged_any = false;
    }
}

bool IdleDue(bool native_armed) noexcept {
    if (g_forget) return true;
    return g_applied_pub && (!native_armed || !g_want_pub || g_shutdown);
}

void Idle() noexcept {
    ForgetPending();
    Restore();
}

void Forget(std::uint32_t epoch) noexcept {
    InterlockedExchange(&g_forget_epoch, static_cast<LONG>(epoch));
    InterlockedExchange(&g_forget, 1);
}

void Shutdown() noexcept { InterlockedExchange(&g_shutdown, 1); }

void SetOverride(int value) noexcept { InterlockedExchange(&g_override, value); }
int Override() noexcept { return static_cast<int>(g_override); }

bool HandleCommand(const char* text, char* result, std::size_t capacity, bool& ok) noexcept {
    ok = false;
    if (!pa::IsCommand(text)) return false;
    pa::ParsedCommand cmd{};
    if (!pa::ParseCommand(text, cmd)) {
        if (result && capacity) StringCchCopyA(result, capacity, "aimpt=refused; reason=args; use=off|look|point|lab|status");
        return true;
    }
    if (cmd.kind == pa::Command::set) SetOverride(static_cast<int>(cmd.value));
    else if (cmd.kind == pa::Command::clear) SetOverride(-1);
    const int value = Override();
    if (result && capacity)
        StringCchPrintfA(result, capacity, "aimpt=%s; override=%s; state=%s; reason=%s; writes=%ld; overwritten=%ld",
                         value < 0 ? "lab" : PartnerNativeAimPtName(static_cast<PartnerNativeAimPt>(value)), value < 0 ? "none" : "set",
                         StateName(static_cast<State>(g_state)), pa::ReasonName(static_cast<pa::Reason>(g_reason)),
                         static_cast<long>(g_writes), static_cast<long>(g_over));
    ok = true;
    return true;
}

void ReadStats(Stats& out) noexcept {
    out = Stats{};
    out.applied = g_applied_pub != 0;
    out.latched = g_latched != 0;
    out.state = static_cast<std::uint8_t>(g_state);
    out.reason = static_cast<std::uint8_t>(g_reason);
    out.writes = static_cast<std::uint32_t>(g_writes);
    out.refused = static_cast<std::uint32_t>(g_refused);
    out.holds = static_cast<std::uint32_t>(g_holds);
    out.overwritten = static_cast<std::uint32_t>(g_over);
    out.restores = static_cast<std::uint32_t>(g_restores);
    out.drops = static_cast<std::uint32_t>(g_drops);
    out.seen_ok = g_seen_ok != 0;
    out.written_ok = g_written_ok != 0;
    for (unsigned i = 0; i < 3; ++i) {
        out.q10_seen[i] = FromBits(g_seen[i]);
        out.written[i] = FromBits(g_written[i]);
    }
    out.override_value = static_cast<int>(g_override);
}

namespace detail {
std::uint64_t NowMs() noexcept {
    LARGE_INTEGER f{}, cc{};
    if (!QueryPerformanceFrequency(&f) || f.QuadPart <= 0 || !QueryPerformanceCounter(&cc)) return GetTickCount64();
    const auto fq = static_cast<std::uint64_t>(f.QuadPart), cq = static_cast<std::uint64_t>(cc.QuadPart);
    return (cq / fq) * 1000 + ((cq % fq) * 1000) / fq;
}
void ResetForTest() noexcept {
    g_applied = false;
    g_p = g_p_vt = g_q = g_arr = g_rec = 0;
    g_epoch = 0;
    g_over_logged = g_logged_any = false;
    g_lines = 0;
    g_pub = Publication{};
    g_seq = 0;
    g_forget = g_forget_epoch = g_blocked = g_shutdown = g_latched = g_applied_pub = g_want_pub = 0;
    g_override = -1;
    g_state = g_reason = g_writes = g_refused = g_holds = g_over = g_restores = g_drops = 0;
    g_seen_ok = g_written_ok = 0;
}
} // namespace detail

} // namespace mgscoop::partner_aim_point
