// Original MGS Co-op F3: the traversal start of body 1 through the stock
// action icon (docs/native-puppet.md "Traversal icon"; partner_icon.hpp).
// No code byte and no image vtable slot is ever written: the module only
// keeps a copy of the padop vtable that partner_input_hook swaps into P's
// padop object for instance 1's drain windows.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <cstring>
#include "mgscoop/partner_icon.hpp"
#include "mgscoop/link_crypto.hpp"
#include "partner_player_contract.hpp"

namespace mgscoop::partner_icon {
namespace {
namespace c = mgscoop::partner_player_contract;
using detail::ForceFn;
using detail::IsCompletedFn;
using PrioFn = std::uint8_t (*)(void* plugin, std::uint32_t idx, std::uint32_t kt);
using ExecFn = bool (*)(void* plugin, std::uint32_t idx, std::uint32_t kt, void* res);

template <class T> T Load(std::uintptr_t at) noexcept {
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(at), sizeof(T));
    return value;
}
template <class T> void Store(std::uintptr_t at, T value) noexcept { std::memcpy(reinterpret_cast<void*>(at), &value, sizeof(T)); }

// The copy: [0] the image's vt[-1] (kept identical), [1..96] the slots.
alignas(16) std::uintptr_t g_copy[1 + kPadopSlots];
volatile LONG g_built = 0;
volatile LONG g_ready = 0;
std::uintptr_t g_base = 0;
IsCompletedFn g_completed = nullptr;
ForceFn g_force = nullptr;
std::uintptr_t g_exec[kKinds] = {};
LogFn g_log = nullptr;

// The drain scope. Begin/End run on the drain thread only; any thread reads
// g_active and g_tid, and only the scoped thread reads the rest.
volatile LONG g_active = 0;
volatile LONG g_tid = 0;
std::uintptr_t g_P = 0, g_blk1 = 0;
std::uint8_t g_kind = 0;
std::uint32_t g_seq = 0;
volatile LONG g_granted_seq = 0;
volatile LONG g_uncertain = 0, g_faulted = 0;
// Counters (process totals).
volatile LONG g_swaps = 0, g_swap_refused = 0, g_windows = 0, g_queries = 0, g_passed = 0, g_denied = 0;
volatile LONG g_exec_ok = 0, g_exec_fail = 0, g_granted = 0, g_force_swallowed = 0, g_force_passed = 0;
volatile LONG g_viewer_changed = 0, g_faults = 0, g_last_kind = 0, g_last_deny = 0;

bool InScope() noexcept { return g_active != 0 && static_cast<DWORD>(g_tid) == GetCurrentThreadId(); }

void Log(const char* line) noexcept {
    if (g_log) g_log(line);
}

// FNV-1a over a byte range (the viewer digest).
std::uint64_t Fnv(std::uint64_t h, std::uintptr_t at, std::size_t n) noexcept {
    const auto* p = reinterpret_cast<const unsigned char*>(at);
    for (std::size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 0x100000001B3ull;
    }
    return h;
}

// The viewer's own state Exec must never change: padop's key-type slots and
// icon input states (shared with the viewer; Exec writes neither), and the
// viewer's lower/upper act words Q+0x18[local] / Q+0x20[local].
bool ViewerDigest(std::uintptr_t padop, std::uintptr_t P, std::uint64_t& out) noexcept {
    __try {
        std::uint64_t h = 0xCBF29CE484222325ull;
        h = Fnv(h, padop + kSlots, kKeyTypes * kSlotStride);
        h = Fnv(h, padop + kInputs, kKeyTypes * kInputStride);
        const std::uint32_t local = Load<std::uint32_t>(P + c::p_local);
        const std::uintptr_t Q = Load<std::uintptr_t>(P + c::p_q);
        if (Q && local < 2) {
            const std::uintptr_t acts = Load<std::uintptr_t>(Q + c::q_act);
            const std::uintptr_t upper = Load<std::uintptr_t>(Q + c::q_upper);
            if (acts) h = Fnv(h, acts + local * 2u, 2);
            if (upper) h = Fnv(h, upper + local, 1);
        }
        out = h;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// What a body-1 query needs, read under SEH.
struct QueryRead {
    std::uintptr_t block, vt, prio, exec, owner;
    std::uint32_t p_flags;
};
bool ReadBlock(std::uintptr_t plugin, std::uintptr_t& block) noexcept {
    __try {
        block = Load<std::uintptr_t>(plugin + kPluginBlock);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool ReadQuery(std::uintptr_t padop, std::uintptr_t plugin, std::uintptr_t P, QueryRead& q) noexcept {
    __try {
        q.vt = Load<std::uintptr_t>(plugin);
        q.prio = Load<std::uintptr_t>(q.vt);
        q.exec = Load<std::uintptr_t>(q.vt + 8);
        q.owner = Load<std::uintptr_t>(padop + kPadopOwner);
        q.p_flags = Load<std::uint32_t>(P + kPFlags);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Priority, then Exec for idx 1 / kt 0, with the shared ray-cache entry saved
// and restored around Exec (also when Exec faults after the save).
enum class ExecResult : std::uint8_t { ok, fail, priority, fault };
ExecResult CallExec(std::uintptr_t padop, std::uintptr_t plugin, std::uintptr_t prio_fn, std::uintptr_t exec_fn, void* res) noexcept {
    unsigned char cache[kRayCacheSize];
    bool saved = false;
    __try {
        const std::uint8_t prio = reinterpret_cast<PrioFn>(prio_fn)(reinterpret_cast<void*>(plugin), 1u, 0u);
        if (!prio) return ExecResult::priority;
        std::memcpy(cache, reinterpret_cast<const void*>(padop + kRayCache), sizeof(cache));
        saved = true;
        const bool ok = reinterpret_cast<ExecFn>(exec_fn)(reinterpret_cast<void*>(plugin), 1u, 0u, res);
        std::memcpy(reinterpret_cast<void*>(padop + kRayCache), cache, sizeof(cache));
        return ok ? ExecResult::ok : ExecResult::fail;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (saved) {
            __try {
                std::memcpy(reinterpret_cast<void*>(padop + kRayCache), cache, sizeof(cache));
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        return ExecResult::fault;
    }
}

bool Refuse(Deny why) noexcept {
    InterlockedIncrement(&g_denied);
    InterlockedExchange(&g_last_deny, static_cast<LONG>(why));
    return false;
}

// Copy slot +0x28.
bool IconCompletedBody1(void* padop, void* plugin, std::int32_t kt, void* res) noexcept {
    const IsCompletedFn original = g_completed;
    if (!InScope()) {
        InterlockedIncrement(&g_passed);
        return original(padop, plugin, kt, res);
    }
    const auto pad = reinterpret_cast<std::uintptr_t>(padop);
    const auto plug = reinterpret_cast<std::uintptr_t>(plugin);
    std::uintptr_t block = 0;
    if (!plug || !ReadBlock(plug, block) || RouteQuery(true, true, block, g_blk1) != Route::body1) {
        InterlockedIncrement(&g_passed);
        return original(padop, plugin, kt, res);
    }
    // From here: body 1's own plugin. Stock answers false (2.2 of the note);
    // only the requested plugin's query is answered, once per request.
    InterlockedIncrement(&g_queries);
    const Kind kind = static_cast<Kind>(g_kind);
    QueryRead q{};
    const bool read = ReadQuery(pad, plug, g_P, q);
    if (!read) {
        InterlockedIncrement(&g_faults);
        InterlockedExchange(&g_faulted, 1);
        return Refuse(Deny::fault);
    }
    GrantIn in{};
    in.kt = kt;
    in.kind = kind;
    in.seq = g_seq;
    in.granted_seq = static_cast<std::uint32_t>(g_granted_seq);
    in.latched = g_uncertain != 0 || g_faulted != 0;
    in.exec_match = static_cast<unsigned>(kind) < kKinds && kind != Kind::none && q.exec == g_exec[static_cast<unsigned>(kind)];
    in.owner_ok = q.owner == g_P;
    in.p_flag = (q.p_flags & kPFlagIcons) != 0;
    in.res_aligned = res && (reinterpret_cast<std::uintptr_t>(res) & 0xFu) == 0;
    const Deny why = GrantCheck(in);
    if (why != Deny::none) return Refuse(why);
    std::uint64_t before = 0, after = 0;
    const bool digest_before = ViewerDigest(pad, g_P, before);
    const ExecResult r = CallExec(pad, plug, q.prio, q.exec, res);
    const bool digest_after = ViewerDigest(pad, g_P, after);
    InterlockedExchange(&g_last_kind, static_cast<LONG>(kind));
    if (!digest_before || !digest_after || before != after) {
        // A surprise on the viewer's side: no grant, no more requests this mission.
        InterlockedIncrement(&g_viewer_changed);
        InterlockedExchange(&g_uncertain, 1);
        if (r == ExecResult::ok) InterlockedIncrement(&g_exec_ok);
        return Refuse(Deny::latched);
    }
    switch (r) {
    case ExecResult::ok:
        InterlockedIncrement(&g_exec_ok);
        InterlockedIncrement(&g_granted);
        InterlockedExchange(&g_granted_seq, static_cast<LONG>(g_seq));
        return true;  // the stock poll now sets its work state and wakes the plugin
    case ExecResult::fail:
        InterlockedIncrement(&g_exec_fail);
        return Refuse(Deny::exec);
    case ExecResult::priority: return Refuse(Deny::priority);
    default:
        InterlockedIncrement(&g_faults);
        InterlockedExchange(&g_faulted, 1);
        return Refuse(Deny::fault);
    }
}

// Copy slot +0x50: body 1's step-on poll calls it right after a granted
// completion (0x14129C129); stock would set the viewer's input-state bit 7
// (padop+0x1260) and copy the viewer's slot result into a discarded local.
void IconForceBody1(void* padop, std::int32_t kt, bool flag, void* res) noexcept {
    if (ForceSwallowed(g_active != 0, static_cast<DWORD>(g_tid) == GetCurrentThreadId())) {
        InterlockedIncrement(&g_force_swallowed);
        return;
    }
    InterlockedIncrement(&g_force_passed);
    g_force(padop, kt, flag, res);
}

bool SlotsLive(std::uintptr_t base) noexcept {
    __try {
        return Load<std::uintptr_t>(base + kPadopVtableRva + kSlotCompleted) == base + kCompletedRva &&
               Load<std::uintptr_t>(base + kPadopVtableRva + kSlotForce) == base + kForceRva;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool PinsMatch(std::uintptr_t base) noexcept {
    __try {
        for (const auto& pin : c::icon_bodies) {
            std::uint8_t digest[link::kSha256Size]{};
            link::Sha256Digest(reinterpret_cast<const void*>(base + pin.rva), pin.size, digest);
            if (std::memcmp(digest, pin.sha256, sizeof(digest)) != 0) return false;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void FillCopy(std::uintptr_t base) noexcept {
    std::memcpy(g_copy, reinterpret_cast<const void*>(base + kPadopVtableRva - 8), sizeof(g_copy));
    g_copy[1 + kSlotCompleted / 8] = reinterpret_cast<std::uintptr_t>(&IconCompletedBody1);
    g_copy[1 + kSlotForce / 8] = reinterpret_cast<std::uintptr_t>(&IconForceBody1);
}
bool SafeFill(std::uintptr_t base) noexcept {
    __try {
        FillCopy(base);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
} // namespace

const char* KindName(Kind kind) noexcept {
    switch (kind) {
    case Kind::ladder: return "ladder";
    case Kind::pipe: return "pipe";
    case Kind::climb: return "climb";
    case Kind::step_on: return "step_on";
    default: return "none";
    }
}

const char* DenyName(Deny deny) noexcept {
    using D = Deny;
    switch (deny) {
    case D::kt: return "kt";
    case D::no_request: return "no_request";
    case D::latched: return "latched";
    case D::granted: return "granted";
    case D::kind: return "kind";
    case D::owner: return "owner";
    case D::p_flag: return "p_flag";
    case D::align: return "align";
    case D::priority: return "priority";
    case D::exec: return "exec";
    case D::fault: return "fault";
    default: return "none";
    }
}

void CensusAdd(Census& s, std::uintptr_t p0, std::uintptr_t p1, std::uintptr_t exec0_rva, std::uintptr_t exec1_rva,
               std::uintptr_t block_of_p0, std::uintptr_t block_of_p1, std::uintptr_t blk0, std::uintptr_t blk1) noexcept {
    if (!p0) ++s.null0;
    if (!p1) ++s.null1;
    if (p0 && p1) {
        ++s.plugins;
        if (p0 != p1) ++s.distinct;
    }
    if (p0) {
        const Kind k = KindOfExec(exec0_rva);
        if (k != Kind::none) {
            s.found0 |= 1u << static_cast<unsigned>(k);
            if (block_of_p0 == blk0) s.blk_ok0 |= 1u << static_cast<unsigned>(k);
        }
    }
    if (p1) {
        const Kind k = KindOfExec(exec1_rva);
        if (k != Kind::none) {
            s.found1 |= 1u << static_cast<unsigned>(k);
            if (block_of_p1 == blk1) s.blk_ok1 |= 1u << static_cast<unsigned>(k);
        }
    }
}

const char* CensusVerdict(const Census& s) noexcept {
    if (!s.read) return "unread";
    if (s.plugins == 0) return "empty";
    if (s.distinct != s.plugins) return "shared";
    if (s.found0 != kAllKinds) return "missing0";
    if (s.found1 != kAllKinds) return "missing1";
    if (s.blk_ok0 != kAllKinds) return "blk0";
    if (s.blk_ok1 != kAllKinds) return "blk1";
    return "ok";
}

const char* Build(const void* base, LogFn log) noexcept {
    if (InterlockedCompareExchange(&g_built, 1, 0) != 0) return g_ready ? nullptr : "disabled";
    const DWORD error = GetLastError();
    g_log = log;
    const auto b = reinterpret_cast<std::uintptr_t>(base);
    const char* why = nullptr;
    if (!b) why = "base";
    else if (!SlotsLive(b)) why = "slots";
    else if (!PinsMatch(b)) why = "pins";
    else if (!SafeFill(b)) why = "fault";
    if (!why) {
        g_base = b;
        g_completed = reinterpret_cast<IsCompletedFn>(b + kCompletedRva);
        g_force = reinterpret_cast<ForceFn>(b + kForceRva);
        for (unsigned k = 0; k < kKinds; ++k) g_exec[k] = kExecRva[k] ? b + kExecRva[k] : 0;
        InterlockedExchange(&g_ready, 1);
    }
    char line[200]{};
    if (why) StringCchPrintfA(line, ARRAYSIZE(line), "partner_icon=refused; reason=%s\r\n", why);
    else StringCchPrintfA(line, ARRAYSIZE(line),
                          "partner_icon=built; vtable=0x22e8f00; slots=%u; completed=0xafd870; force=0xb02480; pins=%u\r\n",
                          static_cast<unsigned>(kPadopSlots), static_cast<unsigned>(ARRAYSIZE(c::icon_bodies)));
    Log(line);
    SetLastError(error);
    return why;
}

bool Ready() noexcept { return g_ready != 0; }

std::uintptr_t CopyTable() noexcept { return g_ready ? reinterpret_cast<std::uintptr_t>(&g_copy[1]) : 0; }

void NoteSwap(bool installed) noexcept { InterlockedIncrement(installed ? &g_swaps : &g_swap_refused); }

void Begin(std::uintptr_t P, std::uintptr_t blk1, std::uint8_t kind, std::uint32_t seq) noexcept {
    g_P = P;
    g_blk1 = blk1;
    g_kind = kind;
    g_seq = seq;
    InterlockedExchange(&g_tid, static_cast<LONG>(GetCurrentThreadId()));
    InterlockedIncrement(&g_windows);
    InterlockedExchange(&g_active, 1);
}

void End() noexcept {
    InterlockedExchange(&g_active, 0);
    InterlockedExchange(&g_tid, 0);
}

void ResetMission() noexcept {
    InterlockedExchange(&g_uncertain, 0);
    InterlockedExchange(&g_faulted, 0);
}

void ReadStats(Stats& out) noexcept {
    out = Stats{};
    out.ready = g_ready != 0;
    out.swaps = static_cast<std::uint32_t>(g_swaps);
    out.swap_refused = static_cast<std::uint32_t>(g_swap_refused);
    out.windows = static_cast<std::uint32_t>(g_windows);
    out.queries = static_cast<std::uint32_t>(g_queries);
    out.passed = static_cast<std::uint32_t>(g_passed);
    out.denied = static_cast<std::uint32_t>(g_denied);
    out.exec_ok = static_cast<std::uint32_t>(g_exec_ok);
    out.exec_fail = static_cast<std::uint32_t>(g_exec_fail);
    out.granted = static_cast<std::uint32_t>(g_granted);
    out.force_swallowed = static_cast<std::uint32_t>(g_force_swallowed);
    out.force_passed = static_cast<std::uint32_t>(g_force_passed);
    out.viewer_changed = static_cast<std::uint32_t>(g_viewer_changed);
    out.faults = static_cast<std::uint32_t>(g_faults);
    out.last_kind = static_cast<std::uint8_t>(g_last_kind);
    out.last_deny = static_cast<std::uint8_t>(g_last_deny);
    out.granted_seq = static_cast<std::uint32_t>(g_granted_seq);
    out.latched = g_uncertain != 0 || g_faulted != 0;
}

void ReadCensus(const void* base, std::uintptr_t blk0, std::uintptr_t blk1, Census& out) noexcept {
    out = Census{};
    const auto b = reinterpret_cast<std::uintptr_t>(base);
    if (!b || !blk0 || !blk1) return;
    __try {
        const std::uintptr_t ps0 = Load<std::uintptr_t>(blk0 + kBlockPs), ps1 = Load<std::uintptr_t>(blk1 + kBlockPs);
        if (!ps0 || !ps1) return;
        for (unsigned k = 0; k < kPlugins; ++k) {
            const std::uintptr_t p0 = Load<std::uintptr_t>(ps0 + kPsPlugins + 8u * k);
            const std::uintptr_t p1 = Load<std::uintptr_t>(ps1 + kPsPlugins + 8u * k);
            const std::uintptr_t e0 = p0 ? Load<std::uintptr_t>(Load<std::uintptr_t>(p0) + 8) - b : 0;
            const std::uintptr_t e1 = p1 ? Load<std::uintptr_t>(Load<std::uintptr_t>(p1) + 8) - b : 0;
            const std::uintptr_t b0 = p0 ? Load<std::uintptr_t>(p0 + kPluginBlock) : 0;
            const std::uintptr_t b1 = p1 ? Load<std::uintptr_t>(p1 + kPluginBlock) : 0;
            CensusAdd(out, p0, p1, e0, e1, b0, b1, blk0, blk1);
        }
        out.read = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out.read = false;
    }
}

namespace detail {
void BindForTest(const void* base, IsCompletedFn completed, ForceFn force, const std::uintptr_t (&exec)[kKinds]) noexcept {
    const auto b = reinterpret_cast<std::uintptr_t>(base);
    FillCopy(b);
    g_base = b;
    g_completed = completed;
    g_force = force;
    for (unsigned k = 0; k < kKinds; ++k) g_exec[k] = exec[k];
    InterlockedExchange(&g_built, 1);
    InterlockedExchange(&g_ready, 1);
}
void ResetForTest() noexcept {
    InterlockedExchange(&g_built, 0);
    InterlockedExchange(&g_ready, 0);
    End();
    g_P = g_blk1 = 0;
    g_kind = 0;
    g_seq = 0;
    InterlockedExchange(&g_granted_seq, 0);
    ResetMission();
}
std::uintptr_t CompletedSlot() noexcept { return reinterpret_cast<std::uintptr_t>(&IconCompletedBody1); }
std::uintptr_t ForceSlot() noexcept { return reinterpret_cast<std::uintptr_t>(&IconForceBody1); }
} // namespace detail

} // namespace mgscoop::partner_icon
