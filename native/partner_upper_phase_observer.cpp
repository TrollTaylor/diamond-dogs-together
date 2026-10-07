// Strictly observe-only natural update probe; shared H3 playback stays unchanged.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "mgscoop/partner_upper_phase_observer.hpp"
#include "mgscoop/partner_upper_manual_idle.hpp"
#include <atomic>
#include <cstring>
namespace mgscoop::partner_upper_phase_observer {
namespace {
constexpr std::uintptr_t kMaxAddress = 0x7fffffffffff;
bool Read(Reader r, std::uintptr_t address, void* out, std::size_t size) noexcept {
    return r.read && address && size && address <= kMaxAddress && size <= kMaxAddress - address
        && r.read(r.context, address, out, size);
}
template<class T> bool At(Reader r, std::uintptr_t address, std::size_t offset, T& out) noexcept {
    return address && address <= kMaxAddress && offset <= kMaxAddress - address && Read(r, address + offset, &out, sizeof(out));
}
bool Relative(std::uintptr_t anchor, std::int32_t rel, std::uintptr_t& out) noexcept {
    const auto value = static_cast<std::int64_t>(anchor) + rel;
    if (!anchor || anchor > kMaxAddress || value <= 0 || static_cast<std::uint64_t>(value) > kMaxAddress) return false;
    out = static_cast<std::uintptr_t>(value); return true;
}
bool Graph(Reader r, std::uintptr_t controller, std::uint32_t channel, std::uintptr_t& bank,
           std::uintptr_t& table, std::uint32_t& count) noexcept {
    std::uint32_t index = 0, graph_count = 0; std::int32_t relative = 0; std::uintptr_t descriptors = 0;
    if (!At(r, controller, 0x1b8, bank) || !At(r, controller, 0xd4, index) || index != channel
        || !At(r, bank, 0x1c, graph_count) || graph_count != 4 || !At(r, bank, 0x20, relative)
        || !Relative(bank + 0x20, relative, descriptors) || descriptors > kMaxAddress - 4 * 0x38) return false;
    const auto descriptor = descriptors + channel * 0x38 + 8;
    if (!At(r, descriptor, 0, count) || (channel == 2 ? count != 122 : !count || count > 4096)
        || !At(r, descriptor, 4, relative) || !Relative(descriptor + 4, relative, table)
        || table > kMaxAddress - static_cast<std::uintptr_t>(count) * 0x48) return false;
    return true;
}
bool NodeAt(Reader r, std::uintptr_t pointer, std::uintptr_t table, std::uint32_t count, end::Node& out) noexcept {
    out = {}; out.pointer = pointer;
    if (!pointer) return true;
    if (pointer < table || pointer - table >= static_cast<std::uintptr_t>(count) * 0x48 || (pointer - table) % 0x48) return false;
    out.index = static_cast<std::uint32_t>((pointer - table) / 0x48);
    std::int32_t relative = 0; std::uintptr_t code = 0;
    if (!At(r, pointer, 0x14, out.type) || !At(r, pointer, 0x18, relative) || !relative
        || relative < -0x400000 || relative > 0x400000 || !Relative(pointer + 0x18, relative, code)
        || !At(r, code, 0, out.code)) return false;
    if (out.type == 6) {
        if (!At(r, pointer, 0x24, relative) || !Relative(pointer + 0x24, relative, out.child)
            || out.child < table || out.child - table >= static_cast<std::uintptr_t>(count) * 0x48 || (out.child - table) % 0x48) return false;
        out.child_index = static_cast<std::uint32_t>((out.child - table) / 0x48);
    }
    return true;
}
bool ChannelAt(Reader r, std::uintptr_t controller, std::uintptr_t table, std::uint32_t count,
               std::uintptr_t record, end::Channel& out) noexcept {
    std::uintptr_t current = 0, queued = 0, alternate = 0;
    if (!At(r, controller, 0x1a0, current) || !At(r, controller, 0x1a8, queued) || !At(r, controller, 0x1b0, alternate)
        || !NodeAt(r, current, table, count, out.current) || !NodeAt(r, queued, table, count, out.queued)
        || !NodeAt(r, alternate, table, count, out.alternate) || !Read(r, record, out.record.data(), out.record.size())) return false;
    constexpr unsigned offsets[] = {0x17d,0x17e,0x17f,0x180,0x181,0x182,0x185,0x186,0x187,0x188};
    for (unsigned i = 0; i < out.flags.size(); ++i) if (!At(r, controller, offsets[i], out.flags[i])) return false;
    return true;
}
bool Mission(std::uint32_t value) noexcept { return value == 30010 || value == 30020 || value == 30050; }
} // namespace
bool ReadGhostSnapshot(Reader r, std::uintptr_t base, std::uint32_t mission, Snapshot& out) noexcept {
    if (!base || base > kMaxAddress - 0xa01d000 || !Mission(mission)) return false;
    Snapshot s{}; auto& i = s.identity;
    std::uint16_t actual_mission = 0; std::uintptr_t vt = 0, sync = 0, ids = 0, pools = 0, parts = 0, blocks = 0, block_table = 0, exists = 0;
    std::uint32_t count = 0, local = 0, flags = 0, base_id = 0;
    std::uint16_t body_ids[2]{}; std::uint8_t pool[2]{}, part[2]{};
    if (!At(r, base, 0x2c01372, actual_mission) || actual_mission != mission || !At(r, base, 0x2a82c30, i.P)
        || !At(r, i.P, 0, vt) || vt != base + 0x2295490 || !At(r, i.P, 0x204, flags) || !(flags & 1) || (flags & 0x100)
        || !At(r, i.P, 0xa8, sync) || sync || !At(r, i.P, 0x208, count) || count != 2
        || !At(r, i.P, 0x20c, count) || count != 2 || !At(r, i.P, 0x214, local) || local
        || !At(r, i.P, 0x60, i.Q) || !At(r, i.Q, 0, vt) || vt != base + 0x23ad970 || !At(r, i.Q, 0x1e8, i.S)
        || !At(r, i.S, 0x40, count) || count != 2 || !At(r, i.S, 0, exists)
        || !At(r, exists, 0, part[0]) || (part[0] & 3) != 3
        || !At(r, i.P, 0x70, ids) || !At(r, i.P, 0x78, pools) || !At(r, i.P, 0x90, parts)
        || !Read(r, ids, body_ids, sizeof(body_ids)) || body_ids[0] != 0 || body_ids[1] != 1
        || !Read(r, pools, pool, sizeof(pool)) || pool[0] > 1 || pool[1] > 1 || pool[0] == pool[1]
        || !Read(r, parts, part, sizeof(part)) || part[0] != 3 || part[1] != 3 || !At(r, i.P, 0x378, blocks)
        || blocks > kMaxAddress - pool[1] * 0x6a8
        || !At(r, i.P, 0x50, block_table)) return false;
    i.block = blocks + pool[1] * 0x6a8;
    if (!At(r, block_table, pool[1] * 8, vt) || vt != i.block
        || !At(r, i.block, 0, vt) || vt != base + 0x2295210 || !At(r, i.block, 0x138, vt) || vt != i.P
        || !At(r, i.block, 8, count) || !count || count > 2 || !At(r, i.block, 0xc, base_id) || base_id > 1 || 1 - base_id >= count
        || !At(r, i.block, 0x78, i.ps) || !At(r, i.ps, 0, vt) || (vt != base + 0x23b09f0 && vt != base + 0x23b0c70)
        || !At(r, i.ps, 8, local) || local != base_id || !At(r, i.ps, 0x408, vt) || vt != i.block
        || !At(r, i.ps, 0x238, i.cs) || !At(r, i.cs, 0, vt) || vt != base + 0x22e6070
        || !At(r, i.cs, 8, count) || !count || count > 2 || !At(r, i.cs, 0xc, base_id) || base_id > 1 || 1 - base_id >= count || !At(r, i.cs, 0x10, i.entries)
        || !i.entries || i.entries > kMaxAddress - (1 - base_id) * 0xc0) return false;
    i.entry = i.entries + (1 - base_id) * 0xc0; // allocated body1, validated CS id range
    std::uint32_t channels = 0, stride = 0; std::uint8_t extras = 0;
    if (!At(r, i.entry, 0x78, i.allocation) || !i.allocation || !At(r, i.entry, 0x90, i.controllers) || i.controllers < i.allocation
        || !At(r, i.entry, 0x98, i.buffer) || i.controllers > kMaxAddress - 0x20 || i.buffer != i.controllers + 0x20
        || !At(r, i.entry, 0xb0, channels) || channels != 4 || !At(r, i.entry, 0xac, s.entry_flags)
        || !At(r, i.buffer, 8, extras) || extras || !At(r, i.buffer, 0xc, stride) || stride != 0x28
        || !At(r, i.buffer, 0x10, i.records) || i.buffer > kMaxAddress - 0x20 || i.records != i.buffer + 0x20 || i.records > kMaxAddress - 4 * 0x28
        || !At(r, i.controllers, 0, i.lower) || !At(r, i.controllers, 16, i.upper) || i.lower == i.upper
        || !At(r, i.lower, 0, vt) || vt != base + 0x250ba50 || !At(r, i.upper, 0, vt) || vt != base + 0x250ba50) return false;
    std::uintptr_t lower_bank = 0;
    if (!Graph(r, i.lower, 0, lower_bank, i.lower_table, i.lower_count) || !Graph(r, i.upper, 2, i.bank, i.upper_table, i.upper_count)
        || lower_bank != i.bank) return false;
    if (!NodeAt(r, i.upper_table + 103 * 0x48, i.upper_table, i.upper_count, s.end_node)
        || s.end_node.type != 0 || s.end_node.code != end::kEnd
        || !NodeAt(r, i.upper_table + 65 * 0x48, i.upper_table, i.upper_count, s.fire_selector)
        || s.fire_selector.type != 6 || s.fire_selector.code != 0x91a5b8ac42f8 || s.fire_selector.child_index != 116
        || !ChannelAt(r, i.lower, i.lower_table, i.lower_count, i.records, s.lower)
        || !ChannelAt(r, i.upper, i.upper_table, i.upper_count, i.records + 2 * 0x28, s.upper)
        || !Read(r, i.buffer, s.masks.data(), sizeof(s.masks))) return false;
    out = s; return true;
}
namespace {
struct Core {
    std::atomic_flag lock = ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> active{0}, lifecycle{0};
    bool configured = false; Reader reader{}; std::uintptr_t image = 0; Lease lease{};
    unsigned pairs = 0, queued = 0, drained = 0;
    std::uint64_t serial = 0, last_generation = 0, last_sample_ms = 0;
    std::array<Observation, kPairCap * 2> observations{};
    std::array<std::uint64_t, kPairCap> serials{};
    std::array<bool, kPairCap> after_done{};
} core;
struct TryLock {
    bool held = !core.lock.test_and_set(std::memory_order_acquire);
    ~TryLock() { if (held) core.lock.clear(std::memory_order_release); }
};
struct Context { Lease lease{}; Reader reader{}; std::uintptr_t image = 0; std::uint64_t lifecycle = 0; };
bool Fresh(const Lease& lease, std::uint64_t now) noexcept {
    return now >= lease.refreshed_ms && now - lease.refreshed_ms <= kLeaseMs;
}
bool Cadence(std::uint64_t now) noexcept {
    return core.pairs < 2 || (now >= core.last_sample_ms && now - core.last_sample_ms >= kSampleIntervalMs);
}
bool AdmitAfter(Context& out, const Ticket& ticket, std::uint64_t now) noexcept {
    const TryLock guard;
    if (!guard.held || !core.configured || !Fresh(ticket.lease, now)
        || ticket.lease.owner.generation != core.active.load(std::memory_order_acquire)
        || ticket.lifecycle != core.lifecycle.load(std::memory_order_acquire)
        || ticket.pair >= core.pairs || core.serials[ticket.pair] != ticket.serial || core.after_done[ticket.pair]
        || !end::SameIdentity(core.lease.identity, ticket.lease.identity)) return false;
    out = {core.lease, core.reader, core.image, core.lifecycle.load(std::memory_order_acquire)}; return true;
}
bool Admit(Context& out, const Update& q, std::uint64_t now) noexcept {
    const TryLock guard;
    if (!guard.held || !core.configured || !core.active.load(std::memory_order_acquire)
        || !Fresh(core.lease, now) || core.pairs >= kPairCap || !Cadence(now)
        || q.entry != core.lease.identity.entry || q.caller != core.image + 0xaf0381 || !q.thread) return false;
    out = {core.lease, core.reader, core.image, core.lifecycle.load(std::memory_order_acquire)}; return true;
}
bool Valid(const Context& ctx, const Snapshot& snapshot, const Update& update, std::uint64_t now) noexcept {
    return Fresh(ctx.lease, now) && ctx.lease.owner.generation == core.active.load(std::memory_order_acquire)
        && ctx.lifecycle == core.lifecycle.load(std::memory_order_acquire)
        && end::SameIdentity(ctx.lease.identity, snapshot.identity)
        && update.entry == ctx.lease.identity.entry && update.caller == ctx.image + 0xaf0381 && update.thread
        && (snapshot.entry_flags & 1) && !(snapshot.entry_flags & 2);
}
void ResetBudget() noexcept {
    core.pairs = 0; core.queued = 0; core.drained = 0; core.last_sample_ms = 0;
    core.serials.fill(0); core.after_done.fill(false);
}
void Record(const Ticket& t, const Snapshot& s, std::uintptr_t rax, std::uint64_t now, bool after) noexcept {
    if (core.queued >= kPairCap * 2) return;
    auto& out = core.observations[core.queued++];
    out = {}; out.after = after; out.serial = t.serial; out.lifecycle = t.lifecycle; out.now_ms = now;
    out.owner = t.lease.owner; out.update = t.update; out.snapshot = s; out.original_rax = rax;
}
} // namespace
bool Compiled() noexcept {
#if defined(MGS_LOCAL_UPPER_PHASE_OBSERVE) && MGS_LOCAL_UPPER_PHASE_OBSERVE
    return true;
#else
    return false;
#endif
}
bool Configure(std::uintptr_t image, Authorization auth, Reader reader) noexcept {
    if (Installed()) return false; // Never rebind a live forwarding hook to another image/reader.
    Clear();
    const TryLock guard;
    if (!guard.held) return false;
    core.configured = false; core.reader = {}; core.image = 0;
    if (!Compiled() || !image || image > kMaxAddress - 0xa01d000 || !auth.offline || !auth.copied_saves
        || !auth.exact_executable || !auth.exact_code_pins || !reader.read) return false;
    core.configured = true; core.reader = reader; core.image = image; core.last_generation = 0; return true;
}
bool PublishGhostOwner(const Owner& owner, std::uint32_t thread, std::uint64_t now) noexcept {
    // Mission{} starts at body_epoch 0. The nonzero observation generation,
    // immutable epoch/identity lease and lifecycle invalidate stale callbacks.
    if (!Compiled() || !owner.generation || !Mission(owner.mission)
        || !owner.owner_thread || thread != owner.owner_thread || owner.stance > 2 || (owner.flags & ~0x3fu)) return false;
    Context ctx{};
    { const TryLock guard; if (!guard.held || !core.configured) return false;
      ctx.reader = core.reader; ctx.image = core.image; ctx.lifecycle = core.lifecycle.load(std::memory_order_acquire); }
    Snapshot s{}; if (!ReadGhostSnapshot(ctx.reader, ctx.image, owner.mission, s)) return false;
    const TryLock guard;
    if (!guard.held || ctx.lifecycle != core.lifecycle.load(std::memory_order_acquire)) return false;
    const auto active = core.active.load(std::memory_order_acquire);
    if (owner.generation < core.last_generation || (!active && owner.generation == core.last_generation)) return false;
    if (active && owner.generation == active && (owner.body_epoch != core.lease.owner.body_epoch
        || owner.mission != core.lease.owner.mission || owner.owner_thread != core.lease.owner.owner_thread
        || !end::SameIdentity(core.lease.identity, s.identity) || now < core.lease.refreshed_ms)) return false;
    if (active && owner.generation != active) { ResetBudget(); core.lifecycle.fetch_add(1, std::memory_order_acq_rel); }
    if (!active) ResetBudget();
    const auto lifecycle = ctx.lifecycle + (active && owner.generation != active ? 1 : 0);
    core.lease = {owner, s.identity, now}; core.last_generation = owner.generation;
    core.active.store(owner.generation, std::memory_order_release);
    if (lifecycle != core.lifecycle.load(std::memory_order_acquire)) { core.active.store(0, std::memory_order_release); return false; }
    return true;
}
Ticket BeforeUpdate(const Update& update, std::uint64_t now) noexcept {
    Ticket out{}; Context ctx{}; Snapshot s{};
    if (!Compiled() || !Admit(ctx, update, now) || !ReadGhostSnapshot(ctx.reader, ctx.image, ctx.lease.owner.mission, s)
        || !Valid(ctx, s, update, now)) return out;
    const TryLock guard;
    if (!guard.held || ctx.lifecycle != core.lifecycle.load(std::memory_order_acquire)
        || ctx.lease.owner.generation != core.active.load(std::memory_order_acquire) || core.pairs >= kPairCap || !Cadence(now)) return out;
    out.qualified = true; out.pair = core.pairs++; out.serial = ++core.serial; if (!out.serial) out.serial = ++core.serial;
    out.lifecycle = ctx.lifecycle; out.lease = ctx.lease; out.update = update;
    core.serials[out.pair] = out.serial; core.last_sample_ms = now;
    Record(out, s, 0, now, false); return out;
}
void AfterUpdate(const Ticket& ticket, std::uintptr_t rax, std::uint64_t now) noexcept {
    if (!Compiled() || !ticket.qualified) return;
    Context ctx{}; Snapshot s{};
    if (!AdmitAfter(ctx, ticket, now) || !ReadGhostSnapshot(ctx.reader, ctx.image, ticket.lease.owner.mission, s)
        || !Valid({ticket.lease,ctx.reader,ctx.image,ticket.lifecycle}, s, ticket.update, now)) return;
    const TryLock guard;
    if (!guard.held || ticket.lifecycle != core.lifecycle.load(std::memory_order_acquire)
        || ticket.lease.owner.generation != core.active.load(std::memory_order_acquire) || ticket.pair >= core.pairs
        || core.serials[ticket.pair] != ticket.serial || core.after_done[ticket.pair]) return;
    core.after_done[ticket.pair] = true; Record(ticket, s, rax, now, true);
}
bool Drain(Observation& out) noexcept {
    const TryLock guard;
    if (!guard.held) return false;
    while (core.drained < core.queued) {
        const auto& next = core.observations[core.drained++];
        if (next.lifecycle == core.lifecycle.load(std::memory_order_acquire)
            && next.owner.generation == core.active.load(std::memory_order_acquire)) { out = next; return true; }
    }
    return false;
}
void Clear() noexcept {
    core.lifecycle.fetch_add(1, std::memory_order_acq_rel); core.active.store(0, std::memory_order_release);
    const TryLock guard;
    if (guard.held) { core.lease = {}; ResetBudget(); }
}
std::uintptr_t ForwardUpdate(OriginalFn original, void* entry, std::uintptr_t caller,
                             std::uint32_t thread, std::uint64_t now) noexcept {
    // Only the original natural call executes. Observe both independently
    // scoped leases; no local permission can authorize the remote body.
    const DWORD before = GetLastError();
    const Update q{reinterpret_cast<std::uintptr_t>(entry),caller,thread};
    const auto local = end::BeforeUpdate(q, now); const auto ghost = partner_upper_phase_observer::BeforeUpdate(q, now);
    SetLastError(before);
    const auto rax = original(entry);
    const DWORD after = GetLastError();
    const auto done = GetTickCount64();
    end::AfterUpdate(local, rax, done); AfterUpdate(ghost, rax, done);
    SetLastError(after); return rax;
}
} // namespace mgscoop::partner_upper_phase_observer

#if defined(MGS_LOCAL_UPPER_PHASE_OBSERVE) && MGS_LOCAL_UPPER_PHASE_OBSERVE && !defined(MGS_UPPER_PHASE_TESTING)
#include <bcrypt.h>
#include <intrin.h>
#include <MinHook.h>
namespace mgscoop::partner_upper_phase_observer {
namespace {
OriginalFn original_update = nullptr;
std::atomic<bool> installed{false};
bool PinMatches(std::uintptr_t image, const end::Pin& pin) noexcept {
    std::uint8_t bytes[0x200]{}, digest[32]{}; SIZE_T copied = 0;
    if (pin.size > sizeof(bytes) || !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(image + pin.rva), bytes, pin.size, &copied)
        || copied != pin.size) return false;
    BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    if (ok) ok = BCryptHashData(hash, bytes, static_cast<ULONG>(pin.size), 0) >= 0;
    if (ok) ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash); if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    constexpr char alphabet[] = "0123456789abcdef"; char text[65]{};
    for (unsigned n = 0; n < 32; ++n) { text[n * 2] = alphabet[digest[n] >> 4]; text[n * 2 + 1] = alphabet[digest[n] & 15]; }
    return ok && !std::memcmp(text, pin.sha256, 64);
}
std::uintptr_t Detour(void* entry) noexcept {
    const DWORD before = GetLastError();
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto thread = GetCurrentThreadId(); const auto now = GetTickCount64();
    SetLastError(before);
    return partner_upper_manual_idle::ForwardUpdate(original_update, entry, caller, thread, now);
}
} // namespace
bool Installed() noexcept { return installed.load(std::memory_order_acquire); }
bool Install(void* image_base, std::size_t span) noexcept {
    const auto image = reinterpret_cast<std::uintptr_t>(image_base);
    { const TryLock guard; if (!guard.held || !core.configured || image != core.image || span != 0xa01d000) return false; }
    if (Installed()) return true;
    for (const auto& pin : kPins) if (!PinMatches(image, pin)) return false;
    const auto target = reinterpret_cast<void*>(image + 0xb6c410);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&Detour), reinterpret_cast<void**>(&original_update)) != MH_OK) return false;
    if (MH_EnableHook(target) != MH_OK) { MH_RemoveHook(target); original_update = nullptr; return false; }
    installed.store(true, std::memory_order_release); return true;
}
} // namespace mgscoop::partner_upper_phase_observer
#else
namespace mgscoop::partner_upper_phase_observer {
bool Install(void*, std::size_t) noexcept { return false; }
bool Installed() noexcept { return false; }
} // namespace mgscoop::partner_upper_phase_observer
#endif
