#include "mgscoop/partner_upper_end_observer.hpp"
#include <atomic>
#include <cstring>
namespace mgscoop::partner_upper_end_observer {
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
        || !Relative(bank + 0x20, relative, descriptors)) return false;
    const auto descriptor = descriptors + channel * 0x38 + 8;
    if (!At(r, descriptor, 0, count) || (channel == 2 ? count != 122 : !count || count > 4096)
        || !At(r, descriptor, 4, relative) || !Relative(descriptor + 4, relative, table)
        || table > kMaxAddress - static_cast<std::uintptr_t>(count) * 0x48) return false;
    return true;
}
bool NodeAt(Reader r, std::uintptr_t pointer, std::uintptr_t table, std::uint32_t count, Node& out) noexcept {
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
               std::uintptr_t record, Channel& out) noexcept {
    std::uintptr_t current = 0, queued = 0, alternate = 0;
    if (!At(r, controller, 0x1a0, current) || !At(r, controller, 0x1a8, queued) || !At(r, controller, 0x1b0, alternate)
        || !NodeAt(r, current, table, count, out.current) || !NodeAt(r, queued, table, count, out.queued)
        || !NodeAt(r, alternate, table, count, out.alternate) || !Read(r, record, out.record.data(), out.record.size())) return false;
    constexpr unsigned offsets[] = {0x17d,0x17e,0x17f,0x180,0x181,0x182,0x185,0x186,0x187,0x188};
    for (unsigned i = 0; i < out.flags.size(); ++i) if (!At(r, controller, offsets[i], out.flags[i])) return false;
    return true;
}
bool Mission(std::uint32_t value) noexcept { return value == 30010 || value == 30020 || value == 30050; }
const char* LeaseRefusal(const Lease& l, const Snapshot& s, std::uint64_t now, std::uint64_t generation) noexcept {
    if (!l.owner.generation || l.owner.generation != generation) return "generation";
    if (!Mission(l.owner.mission) || !l.owner.owner_thread) return "owner";
    const auto& i = l.identity;
    if (!i.P || !i.Q || !i.S || !i.block || !i.ps || !i.cs || !i.entries || i.entry != i.entries || !i.allocation
        || !i.controllers || !i.buffer || !i.records || !i.lower || !i.upper || i.lower == i.upper || !i.bank
        || !i.lower_table || !i.upper_table || !i.lower_count || i.lower_count > 4096 || i.upper_count != 122) return "lease";
    if (now < l.refreshed_ms || now - l.refreshed_ms > kLeaseMs) return "expired";
    return SameIdentity(l.identity, s.identity) ? nullptr : "identity";
}
struct Core {
    std::atomic_flag lock = ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> active{0}, lifecycle{0};
    bool configured = false; std::uintptr_t image = 0; Reader reader{}; Lease lease{};
    std::array<Observation, kObservationCap> records{};
    unsigned count = 0, emitted = 0; std::uint64_t next = 0, end = 0, last_generation = 0;
    unsigned pairs = 0, submissions = 0, updates = 0, end_updates = 0;
    std::uint64_t end_ms = 0, update_ms = 0;
    std::array<std::uint64_t, kObservationCap / 2> pair_serial{};
    std::array<bool, kObservationCap / 2> after_done{};
} core;
struct TryLock {
    bool held = !core.lock.test_and_set(std::memory_order_acquire);
    ~TryLock() { if (held) core.lock.clear(std::memory_order_release); }
};
struct Context {
    Lease lease{}; Reader reader{}; std::uintptr_t image = 0;
    std::uint64_t lifecycle = 0, end = 0;
};
bool Copy(Context& out) noexcept {
    const TryLock guard;
    if (!guard.held || !core.configured || !core.active.load(std::memory_order_acquire)) return false;
    out = {core.lease, core.reader, core.image, core.lifecycle.load(std::memory_order_acquire), core.end}; return true;
}
bool Fresh(const Lease& lease, std::uint64_t now) noexcept {
    return now >= lease.refreshed_ms && now - lease.refreshed_ms <= kLeaseMs;
}
bool UpdateWindow(std::uint64_t now) noexcept {
    return core.end && now >= core.end_ms && now - core.end_ms <= kEndWindowMs
        && (core.end_updates < 2 || (now >= core.update_ms && now - core.update_ms >= kUpdateIntervalMs));
}
bool Admit(Context& out, bool update, std::uint64_t now) noexcept {
    const TryLock guard;
    if (!guard.held || !core.configured || !core.active.load(std::memory_order_acquire)
        || !Fresh(core.lease, now) || core.pairs >= kObservationCap / 2
        || (update ? core.updates >= kUpdatePairCap || !UpdateWindow(now) : core.submissions >= kSubmissionPairCap)) return false;
    out = {core.lease, core.reader, core.image, core.lifecycle.load(std::memory_order_acquire), core.end}; return true;
}
bool SubmissionTuple(const Context& ctx, const Submission& q) noexcept {
    return q.cs == ctx.lease.identity.cs && q.body_id == 0 && q.channel == 2 && q.thread && q.state == kEnd
        && q.caller == ctx.image + (q.slot190 ? 0x105d39c : 0x105d3b3) && (q.slot190 || !q.option);
}
bool UpdateTuple(const Context& ctx, const Update& q) noexcept {
    return q.entry == ctx.lease.identity.entry && q.caller == ctx.image + 0xaf0381 && q.thread;
}
void ResetBudget() noexcept {
    core.count = 0; core.emitted = 0; core.end = 0; core.end_ms = 0; core.update_ms = 0;
    core.pairs = 0; core.submissions = 0; core.updates = 0; core.end_updates = 0;
    core.pair_serial.fill(0); core.after_done.fill(false);
}
void Claim(Ticket& out) noexcept {
    out.qualified = true; out.serial = ++core.next; if (!out.serial) out.serial = ++core.next;
    out.pair = core.pairs++; core.pair_serial[out.pair] = out.serial;
}
bool Current(const Ticket& t) noexcept {
    return t.qualified && t.lease.owner.generation == core.active.load(std::memory_order_acquire)
        && t.lifecycle == core.lifecycle.load(std::memory_order_acquire) && SameIdentity(t.lease.identity, core.lease.identity)
        && t.pair < core.pairs && core.pair_serial[t.pair] == t.serial && !core.after_done[t.pair];
}
void Record(const Ticket& t, Phase phase, const Snapshot& snapshot, std::uintptr_t rax, std::uint64_t now) noexcept {
    if (core.count >= kObservationCap || core.emitted >= kObservationCap) return;
    auto& out = core.records[core.count++];
    out = {}; out.phase = phase; out.serial = t.serial; out.correlated_end = t.correlated_end;
    out.lifecycle = t.lifecycle; out.now_ms = now; out.generation = t.lease.owner.generation; out.body_epoch = t.lease.owner.body_epoch;
    out.owner = t.lease.owner; out.submission = t.submission; out.update = t.update; out.snapshot = snapshot; out.original_rax = rax;
    ++core.emitted;
}
} // namespace
bool SameIdentity(const Identity& a, const Identity& b) noexcept {
    return a.P == b.P && a.Q == b.Q && a.S == b.S && a.block == b.block && a.ps == b.ps && a.cs == b.cs
        && a.entries == b.entries && a.entry == b.entry && a.allocation == b.allocation && a.controllers == b.controllers
        && a.buffer == b.buffer && a.records == b.records && a.lower == b.lower && a.upper == b.upper
        && a.bank == b.bank && a.lower_table == b.lower_table && a.upper_table == b.upper_table
        && a.lower_count == b.lower_count && a.upper_count == b.upper_count;
}
bool ReadSnapshot(Reader r, std::uintptr_t base, std::uint32_t mission, Snapshot& out) noexcept {
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
        || blocks > kMaxAddress - pool[0] * 0x6a8 || !At(r, i.P, 0x58, i.block) || i.block != blocks + pool[0] * 0x6a8
        || !At(r, i.P, 0x50, block_table) || !At(r, block_table, pool[0] * 8, vt) || vt != i.block
        || !At(r, i.block, 0, vt) || vt != base + 0x2295210 || !At(r, i.block, 0x138, vt) || vt != i.P
        || !At(r, i.block, 8, count) || !count || count > 2 || !At(r, i.block, 0xc, base_id) || base_id
        || !At(r, i.block, 0x78, i.ps) || !At(r, i.ps, 0, vt) || (vt != base + 0x23b09f0 && vt != base + 0x23b0c70)
        || !At(r, i.ps, 8, base_id) || base_id || !At(r, i.ps, 0x408, vt) || vt != i.block
        || !At(r, i.ps, 0x238, i.cs) || !At(r, i.cs, 0, vt) || vt != base + 0x22e6070
        || !At(r, i.cs, 8, count) || !count || count > 2 || !At(r, i.cs, 0xc, base_id) || base_id || !At(r, i.cs, 0x10, i.entries)) return false;
    i.entry = i.entries; // exact inherited body 0, CS base id 0
    std::uint32_t channels = 0, stride = 0; std::uint8_t extras = 0;
    if (!At(r, i.entry, 0x78, i.allocation) || !i.allocation || !At(r, i.entry, 0x90, i.controllers) || i.controllers < i.allocation
        || !At(r, i.entry, 0x98, i.buffer) || i.controllers > kMaxAddress - 0x20 || i.buffer != i.controllers + 0x20
        || !At(r, i.entry, 0xb0, channels) || channels != 4 || !At(r, i.entry, 0xac, s.entry_flags)
        || !At(r, i.buffer, 8, extras) || extras || !At(r, i.buffer, 0xc, stride) || stride != 0x28
        || !At(r, i.buffer, 0x10, i.records) || i.records != i.buffer + 0x20 || i.records > kMaxAddress - 4 * 0x28
        || !At(r, i.controllers, 0, i.lower) || !At(r, i.controllers, 16, i.upper) || i.lower == i.upper
        || !At(r, i.lower, 0, vt) || vt != base + 0x250ba50 || !At(r, i.upper, 0, vt) || vt != base + 0x250ba50) return false;
    std::uintptr_t lower_bank = 0;
    if (!Graph(r, i.lower, 0, lower_bank, i.lower_table, i.lower_count) || !Graph(r, i.upper, 2, i.bank, i.upper_table, i.upper_count)
        || lower_bank != i.bank) return false;
    if (!NodeAt(r, i.upper_table + 103 * 0x48, i.upper_table, i.upper_count, s.end_node)
        || s.end_node.type != 0 || s.end_node.code != kEnd
        || !NodeAt(r, i.upper_table + 65 * 0x48, i.upper_table, i.upper_count, s.fire_selector)
        || s.fire_selector.type != 6 || s.fire_selector.code != 0x91a5b8ac42f8 || s.fire_selector.child_index != 116
        || !ChannelAt(r, i.lower, i.lower_table, i.lower_count, i.records, s.lower)
        || !ChannelAt(r, i.upper, i.upper_table, i.upper_count, i.records + 2 * 0x28, s.upper)
        || !Read(r, i.buffer, s.masks.data(), sizeof(s.masks))) return false;
    out = s; return true;
}
const char* SubmissionRefusal(const Lease& l, const Submission& q, const Snapshot& s, std::uint64_t now,
                              std::uint64_t generation, std::uintptr_t base) noexcept {
    if (const char* why = LeaseRefusal(l, s, now, generation)) return why;
    if (!base || base > kMaxAddress - 0xa01d000) return "image";
    if (q.cs != l.identity.cs) return "cs";
    if (q.body_id != 0) return "body";
    if (q.channel != 2) return "channel";
    if (!q.thread) return "thread";
    if (q.state != kEnd) return "state";
    if (q.caller != base + (q.slot190 ? 0x105d39c : 0x105d3b3)) return "caller";
    if (!q.slot190 && q.option) return "option";
    return nullptr;
}
const char* UpdateRefusal(const Lease& l, const Update& q, const Snapshot& s, std::uint64_t now,
                          std::uint64_t generation, std::uintptr_t base) noexcept {
    if (const char* why = LeaseRefusal(l, s, now, generation)) return why;
    if (!base || base > kMaxAddress - 0xa01d000) return "image";
    if (q.entry != l.identity.entry) return "entry";
    if (q.caller != base + 0xaf0381) return "caller";
    if (!q.thread) return "thread";
    if (!(s.entry_flags & 1) || (s.entry_flags & 2)) return "phase";
    return nullptr;
}
bool Compiled() noexcept {
#if defined(MGS_LOCAL_UPPER_END_OBSERVE) && MGS_LOCAL_UPPER_END_OBSERVE
    return true;
#else
    return false;
#endif
}
bool Configure(std::uintptr_t image, Authorization auth, Reader reader) noexcept {
    Clear();
    { const TryLock guard; if (!guard.held) return false; core.configured = false; core.reader = {}; core.image = 0; }
    if (!Compiled() || !image || image > kMaxAddress - 0xa01d000 || !auth.offline || !auth.copied_saves
        || !auth.exact_executable || !auth.exact_code_pins || !reader.read) return false;
    const TryLock guard;
    if (!guard.held) return false;
    core.configured = true; core.image = image; core.reader = reader; core.last_generation = 0; return true;
}
bool PublishOwner(const Owner& owner, std::uint32_t thread, std::uint64_t now) noexcept {
    if (!Compiled() || !owner.generation || !Mission(owner.mission) || !owner.owner_thread || thread != owner.owner_thread
        || owner.stance > 2 || (owner.flags & ~0x3fu)) return false;
    Reader reader{}; std::uintptr_t image = 0; const auto lifecycle = core.lifecycle.load(std::memory_order_acquire);
    { const TryLock guard; if (!guard.held || !core.configured) return false; reader = core.reader; image = core.image; }
    Snapshot snapshot{}; if (!ReadSnapshot(reader, image, owner.mission, snapshot)) return false;
    const TryLock guard;
    if (!guard.held || lifecycle != core.lifecycle.load(std::memory_order_acquire)) return false;
    const auto active = core.active.load(std::memory_order_acquire);
    if (owner.generation < core.last_generation || (!active && owner.generation == core.last_generation)) return false;
    if (active && owner.generation == core.lease.owner.generation && (owner.body_epoch != core.lease.owner.body_epoch
        || owner.mission != core.lease.owner.mission || owner.owner_thread != core.lease.owner.owner_thread
        || !SameIdentity(snapshot.identity, core.lease.identity) || now < core.lease.refreshed_ms)) return false;
    if (active && owner.generation != core.lease.owner.generation) {
        ResetBudget(); core.lifecycle.fetch_add(1, std::memory_order_acq_rel);
    }
    if (!active) ResetBudget(); // Clear can invalidate authority while its nonblocking lock is busy.
    core.lease = {owner, snapshot.identity, now}; core.last_generation = owner.generation;
    core.active.store(owner.generation, std::memory_order_release);
    const auto expected_lifecycle = lifecycle + (active && owner.generation != active ? 1 : 0);
    if (expected_lifecycle != core.lifecycle.load(std::memory_order_acquire)) {
        core.active.store(0, std::memory_order_release); return false;
    }
    return true;
}
Ticket BeforeSubmission(const Submission& submission, std::uint64_t now) noexcept {
    Ticket out{}; Context ctx{}; Snapshot snapshot{};
    if (!Compiled() || !Admit(ctx, false, now) || !SubmissionTuple(ctx, submission)
        || !ReadSnapshot(ctx.reader, ctx.image, ctx.lease.owner.mission, snapshot)
        || SubmissionRefusal(ctx.lease, submission, snapshot, now, core.active.load(std::memory_order_acquire), ctx.image)) return out;
    const TryLock guard;
    if (!guard.held || ctx.lifecycle != core.lifecycle.load(std::memory_order_acquire)
        || ctx.lease.owner.generation != core.active.load(std::memory_order_acquire)
        || core.pairs >= kObservationCap / 2 || core.submissions >= kSubmissionPairCap) return out;
    Claim(out); ++core.submissions;
    out.correlated_end = out.serial; out.lifecycle = ctx.lifecycle; out.lease = ctx.lease; out.submission = submission;
    core.end = out.serial; core.end_ms = now; core.end_updates = 0; core.update_ms = 0;
    Record(out, Phase::submission_before, snapshot, 0, now); return out;
}
void AfterSubmission(const Ticket& ticket, std::uintptr_t rax, std::uint64_t now) noexcept {
    if (!Compiled() || !ticket.qualified || ticket.phase != Phase::submission_before) return;
    Context ctx{}; Snapshot snapshot{};
    if (!Copy(ctx) || !ReadSnapshot(ctx.reader, ctx.image, ticket.lease.owner.mission, snapshot)
        || SubmissionRefusal(ticket.lease, ticket.submission, snapshot, now, core.active.load(std::memory_order_acquire), ctx.image)) return;
    const TryLock guard;
    if (guard.held && Current(ticket)) {
        core.after_done[ticket.pair] = true; Record(ticket, Phase::submission_after, snapshot, rax, now);
    }
}
Ticket BeforeUpdate(const Update& update, std::uint64_t now) noexcept {
    Ticket out{}; Context ctx{}; Snapshot snapshot{};
    if (!Compiled() || !Admit(ctx, true, now) || !UpdateTuple(ctx, update)
        || !ReadSnapshot(ctx.reader, ctx.image, ctx.lease.owner.mission, snapshot)
        || UpdateRefusal(ctx.lease, update, snapshot, now, core.active.load(std::memory_order_acquire), ctx.image)) return out;
    const TryLock guard;
    if (!guard.held || ctx.lifecycle != core.lifecycle.load(std::memory_order_acquire)
        || ctx.lease.owner.generation != core.active.load(std::memory_order_acquire)
        || ctx.end != core.end || core.pairs >= kObservationCap / 2 || core.updates >= kUpdatePairCap || !UpdateWindow(now)) return out;
    Claim(out); ++core.updates; ++core.end_updates; core.update_ms = now; out.phase = Phase::update_before;
    out.correlated_end = core.end; out.lifecycle = ctx.lifecycle; out.lease = ctx.lease; out.update = update;
    Record(out, Phase::update_before, snapshot, 0, now); return out;
}
void AfterUpdate(const Ticket& ticket, std::uintptr_t rax, std::uint64_t now) noexcept {
    if (!Compiled() || !ticket.qualified || ticket.phase != Phase::update_before) return;
    Context ctx{}; Snapshot snapshot{};
    if (!Copy(ctx) || !ReadSnapshot(ctx.reader, ctx.image, ticket.lease.owner.mission, snapshot)
        || UpdateRefusal(ticket.lease, ticket.update, snapshot, now, core.active.load(std::memory_order_acquire), ctx.image)) return;
    const TryLock guard;
    if (guard.held && Current(ticket)) {
        core.after_done[ticket.pair] = true; Record(ticket, Phase::update_after, snapshot, rax, now);
    }
}
bool Drain(Observation& out) noexcept {
    const TryLock guard;
    if (!guard.held) return false;
    while (core.count && (core.records[0].lifecycle != core.lifecycle.load(std::memory_order_acquire)
        || core.records[0].generation != core.active.load(std::memory_order_acquire))) {
        for (unsigned i = 1; i < core.count; ++i) core.records[i - 1] = core.records[i]; --core.count;
    }
    if (!core.count) return false;
    out = core.records[0]; for (unsigned i = 1; i < core.count; ++i) core.records[i - 1] = core.records[i]; --core.count; return true;
}
void Clear() noexcept {
    core.lifecycle.fetch_add(1, std::memory_order_acq_rel); core.active.store(0, std::memory_order_release);
    const TryLock guard;
    if (guard.held) { core.lease = {}; ResetBudget(); }
}
} // namespace mgscoop::partner_upper_end_observer
