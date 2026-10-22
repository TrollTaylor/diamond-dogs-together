// Original MGS Co-op native puppet: gestures on body 1 (partner_gesture.hpp,
// docs/native-puppet.md "Gestures").
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <intrin.h>
#include <strsafe.h>
#include <atomic>
#include <cstring>
#include <MinHook.h>
#include "mgscoop/partner_gesture.hpp"
#include "mgscoop/partner_upper_phase_observer.hpp"

namespace mgscoop::partner_gesture {
namespace {
constexpr std::uintptr_t kMax = 0x7fffffffffff;
constexpr std::size_t kSpan = 0xa01d000;
// Body layout (partner_player_contract, partner_upper_phase_observer::ReadGhostSnapshot).
constexpr std::uintptr_t kPlayerSlot = 0x2a82c30, kPVtable = 0x2295490, kQVtable = 0x23ad970, kBlockVtable = 0x2295210,
                         kPsVtable = 0x23b09f0, kPsVtableAlt = 0x23b0c70;
constexpr std::uintptr_t kCallSite = 0xaf037c;  // e8 rel32 -> 0xB6C410, next ip 0xAF0381
constexpr std::size_t kRecord = 0x28;           // request record stride (buffer+0xC)

struct Identity {
    std::uintptr_t P, Q;
    std::uintptr_t cs[2], entry[2], graphs[2], buffer[2], records[2], upper[2];
};
struct Nodes {
    bool ok;
    std::uint64_t current, queued;
};
struct Active {
    bool on;
    std::uint32_t seq;
    pol::Kind kind;
    pol::Method method;
    std::uint64_t node, submit_ms;
    std::uintptr_t entry, cs;
};
// Single owner: the body-1 update holding g_busy.
struct HookState {
    Active active;
    std::uint32_t handled_seq, epoch;
    bool epoch_have, uncertain;
};
struct Ticket {
    bool submitted;
    std::uint32_t seq;
    pol::Kind kind;
    pol::Method method;
    std::uint64_t node;
    std::uintptr_t upper;
};
struct Digest {
    std::uint16_t masks[3];
    std::uint8_t record[kRecord];
};

std::uintptr_t g_image = 0;
detail::Calls g_calls{};
OriginalFn g_original = nullptr;
std::atomic<std::uintptr_t> g_entry{0};
std::atomic<bool> g_installed{false}, g_faulted{false};
std::atomic<bool> g_pub_active{false}, g_pub_uncertain{false};
std::atomic<std::uint32_t> g_pub_seq{0}, g_pub_epoch{0};
std::atomic<std::uint32_t> g_body_calls{0}, g_submits{0}, g_ends{0}, g_refusals{0};
std::atomic_flag g_busy = ATOMIC_FLAG_INIT;
volatile LONG g_install_state = 0;
const char* g_install_result = "not_installed";
SRWLOCK g_lock = SRWLOCK_INIT;
// Under g_lock.
Command g_cmd{};
bool g_cmd_have = false;
Receipt g_ring[kReceipts]{};
unsigned g_count = 0;
std::uint32_t g_dropped = 0;
HookState g_hook{};

int AccessFilter(DWORD code) noexcept {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}
bool Raw(std::uintptr_t at, void* out, std::size_t size) noexcept {
    if (!at || at > kMax || !size || size > kMax - at) return false;
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(at), size);
        return true;
    } __except (AccessFilter(GetExceptionCode())) {
        return false;
    }
}
template <class T>
bool Rd(std::uintptr_t at, T& out) noexcept { return Raw(at, &out, sizeof(T)); }
bool Rel(std::uintptr_t anchor, std::int32_t rel, std::uintptr_t& out) noexcept {
    const auto value = static_cast<std::int64_t>(anchor) + rel;
    if (!anchor || anchor > kMax || value <= 0 || static_cast<std::uint64_t>(value) > kMax) return false;
    out = static_cast<std::uintptr_t>(value);
    return true;
}

// Body 0 and body 1, one id per block (offline), each with its own CS and entry.
const char* Resolve(std::uintptr_t image, Identity& id) noexcept {
    id = Identity{};
    std::uintptr_t vt = 0, sync = 0, ids_at = 0, pools_at = 0, blocks = 0, table = 0;
    std::uint32_t flags = 0, count = 0, local = 0;
    std::uint16_t ids[2]{};
    std::uint8_t pool[2]{};
    if (!image || image > kMax - kSpan || !Rd(image + kPlayerSlot, id.P) || !id.P || !Rd(id.P, vt) || vt != image + kPVtable) return "player";
    if (!Rd(id.P + 0x204, flags) || !(flags & 1u) || (flags & 0x100u) || !Rd(id.P + 0xa8, sync) || sync) return "online";
    if (!Rd(id.P + 0x208, count) || count != 2 || !Rd(id.P + 0x214, local) || local != 0) return "count";
    if (!Rd(id.P + 0x60, id.Q) || !id.Q || !Rd(id.Q, vt) || vt != image + kQVtable) return "q";
    if (!Rd(id.P + 0x70, ids_at) || !Raw(ids_at, ids, sizeof(ids)) || ids[0] != 0 || ids[1] != 1) return "ids";
    if (!Rd(id.P + 0x78, pools_at) || !Raw(pools_at, pool, sizeof(pool)) || pool[0] > 1 || pool[1] > 1 || pool[0] == pool[1]) return "pool";
    if (!Rd(id.P + 0x378, blocks) || !blocks || blocks > kMax - 2 * 0x6a8 || !Rd(id.P + 0x50, table) || !table) return "blocks";
    for (std::uint32_t k = 0; k < 2; ++k) {
        const std::uintptr_t blk = blocks + static_cast<std::uintptr_t>(pool[k]) * 0x6a8;
        std::uintptr_t slot = 0, owner = 0, ps = 0, cs = 0, entries = 0, lower = 0;
        std::uint32_t n = 0, base = 0, ps_id = 0, channels = 0, stride = 0;
        std::uint8_t extras = 0;
        if (!Rd(table + static_cast<std::uintptr_t>(pool[k]) * 8, slot) || slot != blk || !Rd(blk, vt) || vt != image + kBlockVtable ||
            !Rd(blk + 0x138, owner) || owner != id.P || !Rd(blk + 8, n) || n != 1 || !Rd(blk + 0xc, base) || base != k) return "block";
        if (!Rd(blk + 0x78, ps) || !Rd(ps, vt) || (vt != image + kPsVtable && vt != image + kPsVtableAlt) || !Rd(ps + 8, ps_id) ||
            ps_id != k || !Rd(ps + 0x408, owner) || owner != blk) return "ps";
        if (!Rd(ps + 0x238, cs) || !Rd(cs, vt) || vt != image + kCsVtableRva || !Rd(cs + 8, n) || n != 1 || !Rd(cs + 0xc, base) ||
            base != k || !Rd(cs + 0x10, entries) || !entries) return "cs";
        id.cs[k] = cs;
        id.entry[k] = entries;  // the block's only id: index 0
        if (!Rd(entries + 0x90, id.graphs[k]) || !id.graphs[k] || id.graphs[k] > kMax - 0x20 || !Rd(entries + 0x98, id.buffer[k]) ||
            id.buffer[k] != id.graphs[k] + 0x20 || !Rd(entries + 0xb0, channels) || channels != 4) return "entry";
        if (!Rd(id.buffer[k] + 8, extras) || extras || !Rd(id.buffer[k] + 0xc, stride) || stride != kRecord ||
            !Rd(id.buffer[k] + 0x10, id.records[k]) || id.records[k] != id.buffer[k] + 0x20) return "buffer";
        if (!Rd(id.graphs[k], lower) || !lower || !Rd(lower, vt) || vt != image + kGraphVtableRva || !Rd(id.graphs[k] + 0x10, id.upper[k]) ||
            !id.upper[k] || id.upper[k] == lower || !Rd(id.upper[k], vt) || vt != image + kGraphVtableRva) return "graph";
    }
    if (id.cs[0] == id.cs[1] || id.entry[0] == id.entry[1] || id.buffer[0] == id.buffer[1]) return "shared";
    return nullptr;
}

bool NodeCode(std::uintptr_t node, std::uintptr_t table, std::uint32_t count, std::uint64_t& out) noexcept {
    out = 0;
    if (!node) return true;
    if (node < table || node - table >= static_cast<std::uintptr_t>(count) * 0x48 || (node - table) % 0x48) return false;
    std::int32_t rel = 0;
    std::uintptr_t at = 0;
    return Rd(node + 0x18, rel) && rel && rel >= -0x400000 && rel <= 0x400000 && Rel(node + 0x18, rel, at) && Rd(at, out);
}
// Graph 2's current and queued node codes (the bounded ChannelAt walk).
Nodes UpperNodes(std::uintptr_t upper) noexcept {
    Nodes n{};
    std::uintptr_t bank = 0, descriptors = 0, table = 0, current = 0, queued = 0;
    std::uint32_t index = 0, graphs = 0, count = 0;
    std::int32_t rel = 0;
    if (!Rd(upper + 0x1b8, bank) || !bank || !Rd(upper + 0xd4, index) || index != pol::kGraphUpper || !Rd(bank + 0x1c, graphs) || graphs != 4 ||
        !Rd(bank + 0x20, rel) || !Rel(bank + 0x20, rel, descriptors) || descriptors > kMax - 4 * 0x38) return n;
    const std::uintptr_t descriptor = descriptors + pol::kGraphUpper * 0x38 + 8;
    if (!Rd(descriptor, count) || !count || count > 4096 || !Rd(descriptor + 4, rel) || !Rel(descriptor + 4, rel, table) ||
        table > kMax - static_cast<std::uintptr_t>(count) * 0x48) return n;
    if (!Rd(upper + 0x1a0, current) || !Rd(upper + 0x1a8, queued) || !NodeCode(current, table, count, n.current) ||
        !NodeCode(queued, table, count, n.queued)) return n;
    n.ok = true;
    return n;
}

bool SlotsOk(std::uintptr_t image) noexcept {
    for (const Slot& s : kSlots) {
        std::uintptr_t target = 0;
        if (!Rd(image + s.vtable_rva + s.offset, target) || target != image + s.target_rva) return false;
    }
    return true;
}
// vt+0x2B8 dereferences *(entry+0x70)+0x10 and its layer pointer without a null check.
bool PlayEndReady(std::uintptr_t entry) noexcept {
    std::uintptr_t a = 0, b = 0, c = 0;
    return Rd(entry + 0x70, a) && a && Rd(a + 0x10, b) && b && Rd(b + 0xf8 + pol::kEndLayer * 8, c) && c;
}
bool BodyArrays(const Identity& id, std::uint16_t& act, std::uint8_t& upper, std::uint8_t& q28) noexcept {
    std::uintptr_t p18 = 0, p20 = 0, p28 = 0;
    return Rd(id.Q + 0x18, p18) && p18 && Rd(p18 + 2, act) && Rd(id.Q + 0x20, p20) && p20 && Rd(p20 + 1, upper) && Rd(id.Q + 0x28, p28) &&
           p28 && Rd(p28 + 1, q28);
}
// Body k's request masks and one graph's request record.
bool ReadDigest(const Identity& id, std::uint32_t k, std::uint32_t graph, Digest& out) noexcept {
    return Raw(id.buffer[k], out.masks, sizeof(out.masks)) && Raw(id.records[k] + graph * kRecord, out.record, sizeof(out.record));
}
bool SameDigest(const Digest& a, const Digest& b) noexcept { return std::memcmp(&a, &b, sizeof(Digest)) == 0; }
// Body 1 after our request: only the graph-2 mask bit may move, graph 0's record stays.
bool Isolated(const Digest& a, const Digest& b) noexcept {
    for (unsigned i = 0; i < 3; ++i)
        if (((a.masks[i] ^ b.masks[i]) & ~static_cast<std::uint16_t>(1u << pol::kGraphUpper)) != 0) return false;
    return std::memcmp(a.record, b.record, sizeof(a.record)) == 0;
}

// Every game call runs under SEH: false on an exception.
bool CallExist(std::uintptr_t cs, std::uint64_t node, bool& out) noexcept {
    static const std::uint64_t kNoTags[2] = {0, 0};
    __try {
        out = g_calls.exist(reinterpret_cast<void*>(cs), 1, pol::kGraphUpper, node, kNoTags);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool CallSubmit(pol::Method method, std::uintptr_t cs, std::uint64_t node, float blend) noexcept {
    __try {
        if (method == pol::Method::reserve_reset) g_calls.reserve(reinterpret_cast<void*>(cs), 1, pol::kGraphUpper, node, blend, 0);
        else g_calls.set(reinterpret_cast<void*>(cs), 1, pol::kGraphUpper, node, blend, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool CallEnd(pol::Kind kind, std::uintptr_t cs, bool& out) noexcept {
    __try {
        out = kind == pol::Kind::pickup ? g_calls.motion_end(reinterpret_cast<void*>(cs), 1, pol::kGraphUpper)
                                        : g_calls.play_end(reinterpret_cast<void*>(cs), 1, pol::kEndLayer);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Push(const Receipt& r) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (g_count < kReceipts) g_ring[g_count++] = r;
    else ++g_dropped;
    ReleaseSRWLockExclusive(&g_lock);
}
Receipt FromActive(const Active& a, Stage stage) noexcept {
    Receipt r{};
    r.stage = stage;
    r.seq = a.seq;
    r.kind = a.kind;
    r.method = a.method;
    r.node = a.node;
    return r;
}
Receipt FromRequest(const Request& q, Stage stage) noexcept {
    Receipt r{};
    r.stage = stage;
    r.seq = q.seq;
    r.kind = q.kind;
    r.method = q.method;
    r.node = q.node;
    return r;
}
void Publish(const HookState& h) noexcept {
    g_pub_active.store(h.active.on, std::memory_order_release);
    g_pub_seq.store(h.active.on ? h.active.seq : 0, std::memory_order_release);
    g_pub_uncertain.store(h.uncertain, std::memory_order_release);
    g_pub_epoch.store(h.epoch, std::memory_order_release);
}
void Fault(HookState& h, Receipt r, const char* why) noexcept {
    g_faulted.store(true, std::memory_order_release);
    h.active = Active{};
    r.stage = Stage::fault;
    r.reason = why;
    Push(r);
}

// Ends a running gesture (before the original update).
void TickActive(HookState& h, const Command& cmd, bool lease, const char* why, const Identity& id, std::uint64_t now) noexcept {
    const Active& a = h.active;
    const bool same = !why && id.entry[1] == a.entry && id.cs[1] == a.cs;
    const Nodes nodes = same ? UpperNodes(id.upper[1]) : Nodes{};
    const bool ours = nodes.ok && (nodes.current == a.node || nodes.queued == a.node);
    const std::uint64_t elapsed = now >= a.submit_ms ? now - a.submit_ms : 0;
    bool clip = false;
    if (same && ours && lease && elapsed >= pol::kMinPlayMs && SlotsOk(g_image) && (a.kind == pol::Kind::pickup || PlayEndReady(id.entry[1])) &&
        !CallEnd(a.kind, id.cs[1], clip)) {
        Fault(h, FromActive(a, Stage::fault), "end_call");
        return;
    }
    const pol::EndOut e = pol::End({a.kind, lease, same, ours, clip, cmd.remote_known, cmd.remote_speed, elapsed});
    if (e.via == pol::Via::none) return;
    Receipt r = FromActive(a, Stage::ended);
    r.via = e.via;
    r.cause = e.cause;
    r.current = nodes.current;
    r.queued = nodes.queued;
    r.ms = elapsed > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(elapsed);
    if (e.null_return && same && ours && !h.uncertain && SlotsOk(g_image)) {
        Digest v0{}, v1{}, b0{}, b1{};
        if (!ReadDigest(id, 0, pol::kGraphUpper, v0) || !ReadDigest(id, 1, 0, b0)) {
            r.reason = "digest";
        } else {
            if (!CallSubmit(pol::Method::set_current, id.cs[1], pol::kStateNull, pol::kNullBlend)) {
                Fault(h, r, "null_call");
                return;
            }
            r.null_sent = true;
            const char* surprise = !ReadDigest(id, 0, pol::kGraphUpper, v1) || !ReadDigest(id, 1, 0, b1) ? "digest_after"
                                 : !SameDigest(v0, v1)                                                     ? "viewer_changed"
                                 : !Isolated(b0, b1)                                                       ? "body1_foreign"
                                                                                                           : nullptr;
            if (surprise) {
                h.uncertain = true;
                Receipt u = FromActive(a, Stage::uncertain);
                u.reason = surprise;
                Push(u);
            }
        }
    }
    ++g_ends;
    Push(r);
    h.active = Active{};
}

Ticket Before(std::uint64_t now) noexcept {
    Ticket t{};
    Command cmd{};
    bool have = false;
    AcquireSRWLockShared(&g_lock);
    cmd = g_cmd;
    have = g_cmd_have;
    ReleaseSRWLockShared(&g_lock);
    HookState& h = g_hook;
    if (have && (!h.epoch_have || cmd.epoch != h.epoch)) {
        // A new mission epoch: no write to an old body; the latch starts clear.
        if (h.active.on) {
            Receipt r = FromActive(h.active, Stage::ended);
            r.via = pol::Via::override_;
            r.cause = "epoch";
            Push(r);
            ++g_ends;
        }
        h.active = Active{};
        h.epoch = cmd.epoch;
        h.epoch_have = true;
        h.uncertain = false;
    }
    const bool lease = have && cmd.enabled && now >= cmd.now_ms && now - cmd.now_ms <= pol::kLeaseMs;
    Identity id{};
    const char* why = Resolve(g_image, id);
    if (h.active.on) {
        TickActive(h, cmd, lease, why, id, now);
        if (g_faulted.load(std::memory_order_acquire)) { Publish(h); return t; }
    }
    const Request& q = cmd.request;
    if (!have || !q.seq || q.seq == h.handled_seq || (h.active.on && q.seq == h.active.seq)) { Publish(h); return t; }
    h.handled_seq = q.seq;
    Receipt r = FromRequest(q, Stage::refused);
    const std::uint64_t age = now >= q.issued_ms ? now - q.issued_ms : 0;
    r.ms = age > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(age);
    std::uint16_t act = 0;
    std::uint8_t upper = 0, q28 = 0;
    const bool arrays = !why && BodyArrays(id, act, upper, q28);
    r.body_act = act;
    r.body_upper = upper;
    r.body_q28 = q28;
    bool exist = false;
    if (h.active.on) r.reason = "busy";
    else if (!lease) r.reason = "lease";
    else if (h.uncertain) r.reason = "uncertain";
    else if (age > pol::kRequestMs) r.reason = "stale";
    else if (why) r.reason = why;
    else if (!arrays) r.reason = "body_unread";
    else if (act == 0x4D) r.reason = "box";
    else if (!pol::BodyReady(act, q28, upper)) r.reason = act < 1 || act > 6 ? "body_act" : q28 ? "body_q28" : "body_upper";
    else if (q.method != pol::Method::set_current && q.method != pol::Method::reserve_reset) r.reason = "method";
    else if (!SlotsOk(g_image)) r.reason = "slots";
    else if (!CallExist(id.cs[1], q.node, exist)) { Fault(h, r, "exist_call"); Publish(h); return t; }
    else if (!exist) r.reason = "no_node";
    if (r.reason) {
        const Nodes nodes = !why ? UpperNodes(id.upper[1]) : Nodes{};
        r.current = nodes.current;
        r.queued = nodes.queued;
        ++g_refusals;
        Push(r);
        Publish(h);
        return t;
    }
    r.exist = true;
    Digest v0{}, v1{}, b0{}, b1{};
    if (!ReadDigest(id, 0, pol::kGraphUpper, v0) || !ReadDigest(id, 1, 0, b0)) {
        r.reason = "digest";
        ++g_refusals;
        Push(r);
        Publish(h);
        return t;
    }
    if (!CallSubmit(q.method, id.cs[1], q.node, q.blend)) { Fault(h, r, "submit_call"); Publish(h); return t; }
    const char* surprise = !ReadDigest(id, 0, pol::kGraphUpper, v1) || !ReadDigest(id, 1, 0, b1) ? "digest_after"
                         : !SameDigest(v0, v1)                                                     ? "viewer_changed"
                         : !Isolated(b0, b1)                                                       ? "body1_foreign"
                                                                                                   : nullptr;
    if (surprise) {
        // The request went out, but something we cannot attribute moved: no
        // further write this epoch (not even the stateNull return).
        h.uncertain = true;
        Receipt u = FromRequest(q, Stage::uncertain);
        u.reason = surprise;
        u.exist = true;
        Push(u);
        Publish(h);
        return t;
    }
    ++g_submits;
    r.stage = Stage::submit;
    Push(r);
    h.active = Active{true, q.seq, q.kind, q.method, q.node, now, id.entry[1], id.cs[1]};
    t = Ticket{true, q.seq, q.kind, q.method, q.node, id.upper[1]};
    Publish(h);
    return t;
}

void After(const Ticket& t) noexcept {
    const Nodes nodes = UpperNodes(t.upper);
    Receipt r{};
    r.stage = Stage::applied;
    r.seq = t.seq;
    r.kind = t.kind;
    r.method = t.method;
    r.node = t.node;
    r.exist = true;
    r.current = nodes.current;
    r.queued = nodes.queued;
    r.ok = nodes.ok && (nodes.current == t.node || nodes.queued == t.node);
    Push(r);
}

std::uintptr_t Detour(void* entry) noexcept {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const std::uintptr_t want = g_entry.load(std::memory_order_acquire);
    if (!want || reinterpret_cast<std::uintptr_t>(entry) != want) return g_original(entry);
    const DWORD error = GetLastError();
    const std::uint64_t now = GetTickCount64();
    SetLastError(error);
    return ForwardUpdate(g_original, entry, caller, now);
}

bool PinMatches(std::uintptr_t image, const Pin& pin) noexcept {
    std::uint8_t bytes[0x200]{}, digest[32]{};
    SIZE_T copied = 0;
    if (pin.size > sizeof(bytes) || !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(image + pin.rva), bytes, pin.size, &copied) ||
        copied != pin.size) return false;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    if (ok) ok = BCryptHashData(hash, bytes, static_cast<ULONG>(pin.size), 0) >= 0;
    if (ok) ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    constexpr char alphabet[] = "0123456789abcdef";
    char text[65]{};
    for (unsigned n = 0; n < 32; ++n) {
        text[n * 2] = alphabet[digest[n] >> 4];
        text[n * 2 + 1] = alphabet[digest[n] & 15];
    }
    return ok && std::memcmp(text, pin.sha256, 64) == 0;
}
void BindProduction(std::uintptr_t image) noexcept {
    g_calls.exist = reinterpret_cast<decltype(g_calls.exist)>(image + 0xaf1f10);
    g_calls.set = reinterpret_cast<decltype(g_calls.set)>(image + 0xaf2ba0);
    g_calls.reserve = reinterpret_cast<decltype(g_calls.reserve)>(image + 0xaf2710);
    g_calls.motion_end = reinterpret_cast<decltype(g_calls.motion_end)>(image + 0xaf1f70);
    g_calls.play_end = reinterpret_cast<decltype(g_calls.play_end)>(image + 0xaf2110);
}
} // namespace

const char* StageName(Stage stage) noexcept {
    switch (stage) {
    case Stage::submit: return "submit";
    case Stage::refused: return "refused";
    case Stage::applied: return "applied";
    case Stage::ended: return "ended";
    case Stage::uncertain: return "uncertain";
    default: return "fault";
    }
}

const char* Install(const void* base, std::size_t span, LogFn log) noexcept {
    if (InterlockedCompareExchange(&g_install_state, 1, 0) != 0) return g_installed.load(std::memory_order_acquire) ? nullptr : g_install_result;
    const DWORD error = GetLastError();
    const auto image = reinterpret_cast<std::uintptr_t>(base);
    const char* why = nullptr;
    std::uintptr_t mismatch = 0;
    if (!image || span != kSpan || image > kMax - kSpan) why = "image";
    else if (partner_upper_phase_observer::Installed()) why = "phase_observer";  // the diagnostic build hooks 0xB6C410 itself
    if (!why) {
        for (const Pin& pin : kPins) {
            if (!PinMatches(image, pin)) { why = "pins"; mismatch = pin.rva; break; }
        }
    }
    if (!why && !SlotsOk(image)) why = "slots";
    if (!why) {
        // The one caller this hook serves: e8 rel32 at 0xAF037C to 0xB6C410.
        std::uint8_t call[5]{};
        std::int32_t rel = 0;
        if (!Raw(image + kCallSite, call, sizeof(call)) || call[0] != 0xe8) why = "caller";
        else {
            std::memcpy(&rel, call + 1, 4);
            if (static_cast<std::int64_t>(kCallerRva) + rel != static_cast<std::int64_t>(kUpdateRva)) why = "caller";
        }
    }
    HMODULE pinned = nullptr;
    if (!why) {
        const MH_STATUS init = MH_Initialize();
        if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) why = "minhook";
    }
    if (!why && !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&Install),
                                    &pinned)) why = "module";
    if (!why) {
        g_image = image;
        BindProduction(image);
        void* target = reinterpret_cast<void*>(image + kUpdateRva);
        if (MH_CreateHook(target, reinterpret_cast<void*>(&Detour), reinterpret_cast<void**>(&g_original)) != MH_OK) why = "hook";
        else if (MH_EnableHook(target) != MH_OK) {
            MH_RemoveHook(target);
            g_original = nullptr;
            why = "enable";
        }
    }
    g_install_result = why;
    if (!why) g_installed.store(true, std::memory_order_release);
    if (log) {
        char line[224]{};
        if (!why)
            StringCchPrintfA(line, ARRAYSIZE(line), "partner_gesture=installed; hook=0xb6c410; caller=0xaf0381; pins=%u; slots=%u\r\n",
                             static_cast<unsigned>(sizeof(kPins) / sizeof(kPins[0])), static_cast<unsigned>(sizeof(kSlots) / sizeof(kSlots[0])));
        else
            StringCchPrintfA(line, ARRAYSIZE(line), "partner_gesture=install_refused; reason=%s; rva=0x%llx\r\n", why,
                             static_cast<unsigned long long>(mismatch));
        log(line);
    }
    SetLastError(error);
    return why;
}

bool Installed() noexcept { return g_installed.load(std::memory_order_acquire); }

bool PatchLive() noexcept {
    if (!g_installed.load(std::memory_order_acquire) || !g_original || !g_image) return false;
    std::uint8_t first = 0;
    return Rd(g_image + kUpdateRva, first) && first == 0xE9;
}

void Step(const Command& command, Status& out) noexcept {
    out = Status{};
    std::uintptr_t entry = 0;
    if (!g_installed.load(std::memory_order_acquire)) out.resolve = "not_installed";
    else if (g_faulted.load(std::memory_order_acquire)) out.resolve = "faulted";
    else {
        Identity id{};
        out.resolve = Resolve(g_image, id);
        if (!out.resolve) entry = id.entry[1];
    }
    g_entry.store(entry, std::memory_order_release);
    AcquireSRWLockExclusive(&g_lock);
    g_cmd = command;
    g_cmd_have = true;
    out.count = g_count;
    for (unsigned i = 0; i < g_count; ++i) out.receipts[i] = g_ring[i];
    g_count = 0;
    out.dropped = g_dropped;
    g_dropped = 0;
    ReleaseSRWLockExclusive(&g_lock);
    out.installed = g_installed.load(std::memory_order_acquire);
    out.live = PatchLive();
    out.faulted = g_faulted.load(std::memory_order_acquire);
    out.active = g_pub_active.load(std::memory_order_acquire);
    out.active_seq = g_pub_seq.load(std::memory_order_acquire);
    out.uncertain = g_pub_uncertain.load(std::memory_order_acquire);
    out.epoch = g_pub_epoch.load(std::memory_order_acquire);
    out.body_calls = g_body_calls.load(std::memory_order_relaxed);
    out.submits = g_submits.load(std::memory_order_relaxed);
    out.ends = g_ends.load(std::memory_order_relaxed);
    out.refusals = g_refusals.load(std::memory_order_relaxed);
}

std::uintptr_t ForwardUpdate(OriginalFn original, void* entry, std::uintptr_t caller, std::uint64_t now_ms) noexcept {
    const DWORD error = GetLastError();
    const std::uintptr_t want = g_entry.load(std::memory_order_acquire);
    if (!original) return 0;
    if (!g_installed.load(std::memory_order_acquire) || g_faulted.load(std::memory_order_acquire) || !want ||
        reinterpret_cast<std::uintptr_t>(entry) != want || caller != g_image + kCallerRva || g_busy.test_and_set(std::memory_order_acquire)) {
        SetLastError(error);
        return original(entry);
    }
    ++g_body_calls;
    const Ticket ticket = Before(now_ms);
    SetLastError(error);
    const std::uintptr_t rax = original(entry);
    const DWORD after = GetLastError();
    if (ticket.submitted) After(ticket);
    g_busy.clear(std::memory_order_release);
    SetLastError(after);
    return rax;
}

namespace detail {
void BindForTest(std::uintptr_t image, const Calls& calls) noexcept {
    g_image = image;
    g_calls = calls;
    g_installed.store(true, std::memory_order_release);
}
void ResetForTest() noexcept {
    g_entry.store(0);
    g_installed.store(false);
    g_faulted.store(false);
    g_pub_active.store(false);
    g_pub_uncertain.store(false);
    g_pub_seq.store(0);
    g_pub_epoch.store(0);
    g_body_calls.store(0);
    g_submits.store(0);
    g_ends.store(0);
    g_refusals.store(0);
    g_busy.clear();
    AcquireSRWLockExclusive(&g_lock);
    g_cmd = Command{};
    g_cmd_have = false;
    g_count = 0;
    g_dropped = 0;
    ReleaseSRWLockExclusive(&g_lock);
    g_hook = HookState{};
    g_image = 0;
    g_calls = Calls{};
}
const char* ResolveForTest(std::uintptr_t image, std::uintptr_t& entry1) noexcept {
    Identity id{};
    const char* why = Resolve(image, id);
    entry1 = why ? 0 : id.entry[1];
    return why;
}
} // namespace detail

} // namespace mgscoop::partner_gesture
