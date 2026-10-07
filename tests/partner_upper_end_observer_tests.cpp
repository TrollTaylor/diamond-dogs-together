// Detached fake-memory and forwarding fixtures only. No game/native calls.
#include "mgscoop/partner_upper_end_observer.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <thread>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
namespace e = mgscoop::partner_upper_end_observer;
unsigned checks = 0;
void Check(bool ok, const char* why) { ++checks; if (!ok) { std::fprintf(stderr, "FAIL %u: %s\n", checks, why); std::exit(1); } }
bool Is(const char* actual, const char* expected) { return actual && std::strcmp(actual, expected) == 0; }
constexpr std::uintptr_t base = 0x140000000, P = 0x200000, Q = 0x201000, S = 0x202000, block = 0x203000;
constexpr std::uintptr_t ps = 0x204000, cs = 0x205000, entry = 0x206000, controllers = 0x207000;
constexpr std::uintptr_t buffer = controllers + 0x20, records = buffer + 0x20, lower = 0x208000, upper = 0x209000;
constexpr std::uintptr_t bank = 0x20a000, descriptors = 0x20b000, lower_table = 0x20c000, upper_table = 0x20d000;
struct Memory {
    std::map<std::uintptr_t, std::uint8_t> bytes;
    unsigned reads = 0; std::uintptr_t refuse = 0; bool clear_on_read = false;
    void Fill(std::uintptr_t at, std::size_t n) { for (std::size_t i = 0; i < n; ++i) bytes[at + i] = 0; }
    template<class T> void Put(std::uintptr_t at, const T& value) {
        const auto* raw = reinterpret_cast<const std::uint8_t*>(&value);
        for (std::size_t i = 0; i < sizeof(value); ++i) bytes[at + i] = raw[i];
    }
    void Ptr(std::uintptr_t at, std::uintptr_t value) { Put(at, value); }
    void U32(std::uintptr_t at, std::uint32_t value) { Put(at, value); }
    void U8(std::uintptr_t at, std::uint8_t value) { Put(at, value); }
    void Relative(std::uintptr_t at, std::uintptr_t target) { Put(at, static_cast<std::int32_t>(target - at)); }
    Memory() {
        Put(base + 0x2c01372, std::uint16_t(30050)); Ptr(base + 0x2a82c30, P);
        Fill(P, 0x390); Fill(Q, 0x200); Fill(S, 0x50); Fill(block, 0x6a8); Fill(ps, 0x410);
        Fill(cs, 0x18); Fill(entry, 0xc0); Fill(controllers, 0xe0); Fill(lower, 0x200); Fill(upper, 0x200);
        Fill(bank, 0x30); Fill(descriptors, 4 * 0x38); Fill(lower_table, 10 * 0x48); Fill(upper_table, 122 * 0x48);
        Ptr(P, base + 0x2295490); U32(P + 0x204, 1); U32(P + 0x208, 2); U32(P + 0x20c, 2); Ptr(P + 0x60, Q);
        Ptr(Q, base + 0x23ad970); Ptr(Q + 0x1e8, S); U32(S + 0x40, 2); Ptr(S, 0x240040); U8(0x240040, 3);
        Ptr(P + 0x70, 0x240000); Ptr(P + 0x78, 0x240010); Ptr(P + 0x90, 0x240020);
        Put(0x240000, std::uint16_t(0)); Put(0x240002, std::uint16_t(1));
        U8(0x240010, 0); U8(0x240011, 1); U8(0x240020, 3); U8(0x240021, 3);
        Ptr(P + 0x378, block); Ptr(P + 0x58, block); Ptr(P + 0x50, 0x240030); Ptr(0x240030, block);
        Ptr(block, base + 0x2295210); Ptr(block + 0x138, P); U32(block + 8, 1); Ptr(block + 0x78, ps);
        Ptr(ps, base + 0x23b09f0); Ptr(ps + 0x408, block); Ptr(ps + 0x238, cs);
        Ptr(cs, base + 0x22e6070); U32(cs + 8, 1); Ptr(cs + 0x10, entry);
        Ptr(entry + 0x78, controllers); Ptr(entry + 0x90, controllers); Ptr(entry + 0x98, buffer);
        U32(entry + 0xac, 1); U32(entry + 0xb0, 4); U32(buffer + 0xc, 0x28); Ptr(buffer + 0x10, records);
        Ptr(controllers, lower); Ptr(controllers + 16, upper);
        Ptr(lower, base + 0x250ba50); Ptr(upper, base + 0x250ba50); Ptr(lower + 0x1b8, bank); Ptr(upper + 0x1b8, bank);
        U32(upper + 0xd4, 2); U32(bank + 0x1c, 4); Relative(bank + 0x20, descriptors);
        U32(descriptors + 8, 10); Relative(descriptors + 12, lower_table);
        U32(descriptors + 2 * 0x38 + 8, 122); Relative(descriptors + 2 * 0x38 + 12, upper_table);
        for (unsigned i = 0; i < 122; ++i) {
            const auto node = upper_table + i * 0x48, code = std::uintptr_t(0x220000 + i * 8);
            Relative(node + 0x18, code); Put(code, std::uint64_t(0x1000 + i));
        }
        for (unsigned i = 0; i < 10; ++i) {
            const auto node = lower_table + i * 0x48, code = std::uintptr_t(0x221000 + i * 8);
            Relative(node + 0x18, code); Put(code, std::uint64_t(0x2000 + i));
        }
        Put(0x220000 + 103 * 8, e::kEnd); Put(0x220000 + 65 * 8, std::uint64_t(0x91a5b8ac42f8));
        Put(0x220000 + 116 * 8, std::uint64_t(0xb8a0bf169f98));
        U8(upper_table + 65 * 0x48 + 0x14, 6); Relative(upper_table + 65 * 0x48 + 0x24, upper_table + 116 * 0x48);
        Ptr(lower + 0x1a0, lower_table + 2 * 0x48); Ptr(upper + 0x1a0, upper_table + 20 * 0x48);
    }
    static bool Read(void* context, std::uintptr_t at, void* out, std::size_t n) noexcept {
        auto& m = *static_cast<Memory*>(context); ++m.reads; SetLastError(901);
        if (m.clear_on_read) { m.clear_on_read = false; e::Clear(); }
        if (m.refuse && at <= m.refuse && m.refuse - at < n) return false;
        auto* raw = static_cast<std::uint8_t*>(out);
        for (std::size_t i = 0; i < n; ++i) {
            const auto found = m.bytes.find(at + i); if (found == m.bytes.end()) return false; raw[i] = found->second;
        }
        return true;
    }
    e::Reader Reader() { return {this, Read}; }
};
e::Owner Owner(std::uint64_t generation = 7) { return {generation, 8, 30050, GetCurrentThreadId(), 873, 1, 1}; }
e::Submission Submission(bool slot190 = false, std::uint32_t thread = GetCurrentThreadId()) {
    return {cs, base + (slot190 ? 0x105d39c : 0x105d3b3), 0, 2, thread, 0x7fc01234, 0, e::kEnd, slot190};
}
std::vector<e::Observation> Drain() {
    std::vector<e::Observation> result; e::Observation record{};
    while (e::Drain(record)) result.push_back(record); return result;
}
void ResolverAndPolicy() {
    Memory memory; e::Snapshot snapshot{};
    Check(e::ReadSnapshot(memory.Reader(), base, 30050, snapshot), "exact body0/CS/controller graph ancestor resolves by read only");
    Check(snapshot.identity.block == block && snapshot.identity.cs == cs && snapshot.identity.entry == entry
        && snapshot.identity.upper == upper && snapshot.identity.lower == lower && snapshot.end_node.index == 103
        && snapshot.end_node.code == e::kEnd && snapshot.fire_selector.index == 65 && snapshot.fire_selector.child_index == 116,
        "End and Fire selector-child ancestry are retained, with independent lower graph");
    const e::Lease lease{Owner(), snapshot.identity, 1000}; const auto submission = Submission();
    Check(!e::SubmissionRefusal(lease, submission, snapshot, 1000, 7, base), "exact natural local End170 qualifies");
    Check(!e::SubmissionRefusal(lease, Submission(true), snapshot, 1250, 7, base), "exact natural End190 qualifies at lease boundary");
    Check(!e::SubmissionRefusal(lease, Submission(false, 999), snapshot, 1000, 7, base), "natural worker need not equal publisher thread");
    Check(Is(e::SubmissionRefusal(lease, submission, snapshot, 1251, 7, base), "expired"), "expired lease denied");
    Check(Is(e::SubmissionRefusal(lease, submission, snapshot, 999, 7, base), "expired"), "backwards clock denied");
    Check(Is(e::SubmissionRefusal(lease, submission, snapshot, 1000, 8, base), "generation"), "different generation denied");
    for (unsigned field = 0; field < 7; ++field) {
        auto q = submission;
        if (field == 0) ++q.cs; if (field == 1) q.body_id = 1; if (field == 2) q.channel = 0;
        if (field == 3) q.thread = 0; if (field == 4) ++q.state; if (field == 5) ++q.caller; if (field == 6) q.option = 1;
        Check(e::SubmissionRefusal(lease, q, snapshot, 1000, 7, base) != nullptr, "only actual natural local End tuple is observed");
    }
    auto wrong_slot = submission; wrong_slot.slot190 = true;
    Check(Is(e::SubmissionRefusal(lease, wrong_slot, snapshot, 1000, 7, base), "caller"), "170 callsite cannot authorize190 method");
    auto changed = snapshot; ++changed.identity.upper;
    Check(Is(e::SubmissionRefusal(lease, submission, changed, 1000, 7, base), "identity"), "controller changed within allocated body denied");
    auto malformed = lease; malformed.identity.block = 0;
    Check(Is(e::SubmissionRefusal(malformed, submission, snapshot, 1000, 7, base), "lease"), "missing ancestor denied");
    const e::Update update{entry, base + 0xaf0381, 999};
    Check(!e::UpdateRefusal(lease, update, snapshot, 1000, 7, base), "exact body0 natural worker phase qualifies for observation API");
    auto foreign = update; ++foreign.entry;
    Check(Is(e::UpdateRefusal(lease, foreign, snapshot, 1000, 7, base), "entry"), "ghost/foreign entry cannot reuse local lease");
    foreign = update; ++foreign.caller;
    Check(Is(e::UpdateRefusal(lease, foreign, snapshot, 1000, 7, base), "caller"), "phase return address exact");
    changed = snapshot; changed.entry_flags = 3;
    Check(Is(e::UpdateRefusal(lease, update, changed, 1000, 7, base), "phase"), "natural update inactive branch denied");

    for (unsigned field = 0; field < 25; ++field) {
        Memory bad;
        if (field == 0) bad.U32(P + 0x204, 0x101);
        if (field == 1) bad.Ptr(P + 0xa8, 1);
        if (field == 2) bad.U32(P + 0x208, 3);
        if (field == 3) bad.U32(P + 0x214, 1);
        if (field == 4) bad.Ptr(P + 0x58, block + 1);
        if (field == 5) bad.Ptr(block + 0x138, P + 1);
        if (field == 6) bad.Ptr(ps + 0x408, block + 1);
        if (field == 7) bad.U32(cs + 0xc, 1);
        if (field == 8) bad.U32(entry + 0xb0, 2);
        if (field == 9) bad.U8(buffer + 8, 1);
        if (field == 10) bad.U32(buffer + 0xc, 0x40);
        if (field == 11) bad.Ptr(buffer + 0x10, records + 1);
        if (field == 12) bad.Ptr(controllers + 16, lower);
        if (field == 13) bad.Ptr(upper, base + 0x250ba51);
        if (field == 14) bad.U32(upper + 0xd4, 0);
        if (field == 15) bad.U32(bank + 0x1c, 3);
        if (field == 16) bad.U32(descriptors + 2 * 0x38 + 8, 121);
        if (field == 17) bad.Put(0x220000 + 103 * 8, std::uint64_t(0));
        if (field == 18) bad.Relative(upper_table + 65 * 0x48 + 0x24, upper_table + 117 * 0x48);
        if (field == 19) bad.Ptr(upper + 0x1a0, upper_table + 1);
        if (field == 20) bad.Ptr(lower + 0x1a0, upper_table);
        if (field == 21) bad.U8(0x240021, 0);
        if (field == 22) bad.refuse = records + 2 * 0x28;
        if (field == 23) bad.U32(P + 0x20c, 1);
        if (field == 24) bad.U8(0x240040, 2);
        e::Snapshot output{}; output.end_node.code = 123;
        Check(!e::ReadSnapshot(bad.Reader(), base, 30050, output) && output.end_node.code == 123,
              "bad ancestor/capacity/graph/read is refused atomically");
    }
}
template<class Original> std::uintptr_t Forward170(const e::Submission& s, float blend, std::uint64_t now, Original original) {
    // Exact proposed root wrapper boundary, without manual Consumer TLS.
    const DWORD before = GetLastError(); const auto ticket = e::BeforeSubmission(s, now); SetLastError(before);
    const auto result = original(reinterpret_cast<void*>(s.cs), s.body_id, s.channel, s.state, blend, s.option);
    const DWORD after = GetLastError(); e::AfterSubmission(ticket, result, now + 1); SetLastError(after); return result;
}
template<class Original> std::uintptr_t Forward190(const e::Submission& s, float blend, std::uint64_t now, Original original) {
    const DWORD before = GetLastError(); const auto ticket = e::BeforeSubmission(s, now); SetLastError(before);
    const auto result = original(reinterpret_cast<void*>(s.cs), s.body_id, s.channel, s.state, blend);
    const DWORD after = GetLastError(); e::AfterSubmission(ticket, result, now + 1); SetLastError(after); return result;
}
float Float(std::uint32_t bits) { float value = 0; std::memcpy(&value, &bits, sizeof(value)); return value; }
std::uint32_t Bits(float value) { std::uint32_t bits = 0; std::memcpy(&bits, &value, sizeof(bits)); return bits; }
bool Configure(Memory& m) { return e::Configure(base, {true,true,true,true}, m.Reader()); }
void RuntimeAndForwarding() {
    Memory m;
    if (!e::Compiled()) {
        const auto reads = m.reads;
        Check(!Configure(m) && !e::PublishOwner(Owner(), GetCurrentThreadId(), 1000)
            && !e::BeforeSubmission(Submission(), 1000).qualified && Drain().empty() && m.reads == reads,
            "default OFF performs no reads, native calls, observations or publication");
        return;
    }
    Check(Configure(m) && e::PublishOwner(Owner(), GetCurrentThreadId(), 1000), "natural owner publishes exact read-only lease");
    Check(!e::PublishOwner(Owner(), GetCurrentThreadId() + 1, 1000), "worker cannot publish owner's lease");
    for (unsigned field = 0; field < 8; ++field) {
        auto wrong = Submission();
        if (field == 0) ++wrong.cs; if (field == 1) wrong.body_id = 1; if (field == 2) wrong.channel = 0;
        if (field == 3) wrong.thread = 0; if (field == 4) ++wrong.state; if (field == 5) ++wrong.caller;
        if (field == 6) wrong.option = 1; if (field == 7) wrong.slot190 = true;
        const auto reads = m.reads;
        Check(!e::BeforeSubmission(wrong, 1000).qualified && m.reads == reads && Drain().empty(),
              "unrelated CS submissions are cheaply refused before any snapshot read");
    }
    const auto s = Submission(); unsigned forwarded = 0;
    SetLastError(111);
    const auto result = Forward170(s, Float(s.blend_bits), 1000,
        [&](void* p, std::uint32_t id, std::uint32_t channel, std::uint64_t state, float blend, std::uint32_t option) {
            ++forwarded;
            Check(GetLastError() == 111 && p == reinterpret_cast<void*>(cs) && id == 0 && channel == 2 && state == e::kEnd
                && Bits(blend) == s.blend_bits && option == 0, "170 forwards every natural argument and incoming LastError exactly once");
            m.U8(records + 2 * 0x28, 0x77); SetLastError(222); return UINTPTR_MAX;
        });
    Check(result == UINTPTR_MAX && GetLastError() == 222 && forwarded == 1, "170 raw RAX and original LastError are unchanged after observer");
    auto observed = Drain();
    Check(observed.size() == 2 && observed[0].phase == e::Phase::submission_before && observed[1].phase == e::Phase::submission_after
        && observed[0].serial == observed[1].serial && observed[1].original_rax == result
        && observed[1].snapshot.upper.record[0] == 0x77 && observed[0].snapshot.lower.record == observed[1].snapshot.lower.record,
        "paired End snapshots preserve original RAX and lower request record without inferring completion");
    auto q = Submission(true); q.blend_bits = 0x80000000;
    SetLastError(111);
    const auto r190 = Forward190(q, Float(q.blend_bits), 1002,
        [&](void* p, std::uint32_t id, std::uint32_t channel, std::uint64_t state, float blend) {
            ++forwarded;
            Check(GetLastError() == 111 && p == reinterpret_cast<void*>(cs) && id == 0 && channel == 2 && state == e::kEnd
                && Bits(blend) == 0x80000000, "190 retains its exact method/arguments including negative-zero blend");
            SetLastError(333); return UINTPTR_MAX - 1;
        });
    Check(r190 == UINTPTR_MAX - 1 && GetLastError() == 333 && forwarded == 2, "190 raw RAX and original LastError are unchanged");
    Check(Drain().size() == 2, "190 natural End has independent before/after observation");
    q = s; q.body_id = 1; q.cs = 0; q.channel = UINT32_MAX; q.option = UINT32_MAX; q.state = UINT64_MAX;
    SetLastError(111);
    const auto foreign = Forward170(q, Float(0x7fc01234), 1004,
        [&](void* p, std::uint32_t id, std::uint32_t channel, std::uint64_t state, float blend, std::uint32_t option) {
            ++forwarded;
            Check(GetLastError() == 111 && p == nullptr && id == 1 && channel == UINT32_MAX && state == UINT64_MAX
                && Bits(blend) == 0x7fc01234 && option == UINT32_MAX, "refused foreign170 preserves all argument bits");
            SetLastError(444); return std::uintptr_t(0x123456789abcdef0ull);
        });
    Check(foreign == 0x123456789abcdef0ull && GetLastError() == 444 && Drain().empty(), "foreign submission forwards with no observation or coupling");
    const auto update = e::BeforeUpdate({entry, base + 0xaf0381, 999}, 1005);
    Check(update.qualified, "uninstalled natural-phase API observes exact worker entry independently of submission TLS");
    m.Ptr(upper + 0x1a0, upper_table + 116 * 0x48); m.U8(upper + 0x17f, 1); m.U8(upper + 0x180, 1); m.U8(upper + 0x182, 1);
    e::AfterUpdate(update, 0x9876, 1006); observed = Drain();
    Check(observed.size() == 2 && observed[1].phase == e::Phase::update_after && observed[1].correlated_end != 0
        && observed[1].snapshot.upper.current.index == 116 && observed[1].snapshot.fire_selector.child_index == 116
        && observed[1].snapshot.upper.flags[2] == 1 && observed[1].snapshot.upper.flags[3] == 1
        && observed[1].snapshot.upper.flags[5] == 1 && observed[1].original_rax == 0x9876,
        "terminal/transition bytes and selector-child identity remain diagnostic data only");
    const auto expired = e::BeforeSubmission(s, 1251);
    Check(!expired.qualified && e::PublishOwner(Owner(), GetCurrentThreadId(), 1251)
        && e::BeforeSubmission(s, 1251).qualified, "only natural owner refresh renews bounded lease");
    Drain();
    const auto old = e::BeforeSubmission(s, 1252); e::Clear();
    Check(!e::PublishOwner(Owner(), GetCurrentThreadId(), 1252), "clear requires a new generation, not stale owner re-publication");
    Check(e::PublishOwner(Owner(8), GetCurrentThreadId(), 1252), "new lifecycle publishes a fresh owner generation");
    e::AfterSubmission(old, 1, 1253);
    Check(Drain().empty(), "invalidated old callback cannot report into new generation");
    auto unauthorized = e::Authorization{true,true,true,false};
    Check(!e::Configure(base, unauthorized, m.Reader()) && !e::PublishOwner(Owner(9), GetCurrentThreadId(), 1254),
          "failed exact pins cannot reuse an earlier configured permission");
}
void ReentrancyWorkerAndCap() {
    if (!e::Compiled()) return;
    Memory m; Check(Configure(m) && e::PublishOwner(Owner(), GetCurrentThreadId(), 1000), "reentrant fixture configured");
    auto s = Submission(); const auto q = Submission(true);
    SetLastError(111);
    Forward170(s, Float(s.blend_bits), 1000,
        [&](void*, std::uint32_t, std::uint32_t, std::uint64_t, float, std::uint32_t) {
            Check(GetLastError() == 111, "reentrant outer native receives original LastError");
            SetLastError(555);
            const auto r = Forward190(q, Float(q.blend_bits), 1001,
                [&](void*, std::uint32_t id, std::uint32_t channel, std::uint64_t state, float) {
                    Check(GetLastError() == 555 && id == 0 && channel == 2 && state == e::kEnd, "nested natural forwarding remains unchanged");
                    SetLastError(666); return std::uintptr_t(77);
                });
            Check(r == 77 && GetLastError() == 666, "nested observer preserves native return/LastError");
            SetLastError(777); return std::uintptr_t(88);
        });
    auto observed = Drain();
    Check(GetLastError() == 777 && observed.size() == 4 && observed[0].serial != observed[1].serial
        && observed[0].serial == observed[3].serial && observed[1].serial == observed[2].serial,
        "reentrant natural End submissions keep separate paired identities without manual TLS");

    std::atomic<bool> entered{false}, release{false}; bool worker_ok = false;
    std::thread worker([&] {
        const auto natural = Submission(false, GetCurrentThreadId()); SetLastError(111);
        const auto r = Forward170(natural, Float(natural.blend_bits), 1010,
            [&](void*, std::uint32_t id, std::uint32_t channel, std::uint64_t state, float, std::uint32_t option) {
                entered.store(true, std::memory_order_release);
                while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
                worker_ok = GetLastError() == 111 && id == 0 && channel == 2 && state == e::kEnd && option == 0;
                SetLastError(888); return std::uintptr_t(99);
            });
        worker_ok = worker_ok && r == 99 && GetLastError() == 888;
    });
    while (!entered.load(std::memory_order_acquire)) std::this_thread::yield();
    e::Clear(); release.store(true, std::memory_order_release); worker.join();
    Check(worker_ok && Drain().empty(), "concurrent clear suppresses worker observations while original forwarding still completes unchanged");
    Check(Configure(m), "race fixture configured"); m.clear_on_read = true;
    Check(!e::PublishOwner(Owner(), GetCurrentThreadId(), 2000) && !e::BeforeSubmission(s, 2000).qualified,
          "invalidation during read-only publication cannot reactivate stale authority");
}
void StartupWindowAndReservedPairs() {
    if (!e::Compiled()) return;
    Memory m; Check(Configure(m), "startup fixture configured");
    const e::Update update{entry, base + 0xaf0381, 999};
    // Thousands of actual-shaped owner updates before aim release must neither
    // read on the hook path nor consume one byte of the End correlation budget.
    for (unsigned i = 0; i < 6000; ++i) {
        const auto now = std::uint64_t(1000 + i * 17);
        Check(e::PublishOwner(Owner(), GetCurrentThreadId(), now), "long startup retains natural lease");
        const auto reads = m.reads;
        Check(!e::BeforeUpdate(update, now).qualified && m.reads == reads, "no End means no update reads or records");
    }
    Check(Drain().empty(), "startup did not exhaust or emit diagnostic budget");
    const std::uint64_t start = 104000;
    Check(e::PublishOwner(Owner(), GetCurrentThreadId(), start), "first real aim-release owner refresh");
    const auto end = e::BeforeSubmission(Submission(), start);
    Check(end.qualified, "first End remains observable after arbitrary startup duration");
    e::AfterSubmission(end, 42, start + 1);
    e::AfterSubmission(end, 99, start + 1);
    Check(Drain().size() == 2, "duplicate completion cannot emit an extra member of an End pair");
    for (unsigned field = 0; field < 3; ++field) {
        auto wrong = update;
        if (field == 0) ++wrong.entry; if (field == 1) ++wrong.caller; if (field == 2) wrong.thread = 0;
        const auto reads = m.reads;
        Check(!e::BeforeUpdate(wrong, start + 1).qualified && m.reads == reads,
              "foreign phase tuples perform no snapshot reads");
    }
    unsigned phase_records = 0;
    for (unsigned i = 0; i < e::kUpdatePairCap; ++i) {
        const auto now = start + (i < 2 ? i : 1 + (i - 1) * e::kUpdateIntervalMs);
        Check(e::PublishOwner(Owner(), GetCurrentThreadId(), now), "owner refresh never re-arms phase cap");
        const auto ticket = e::BeforeUpdate(update, now);
        Check(ticket.qualified && ticket.correlated_end == end.serial, "bounded updates retain exact End event identity");
        e::AfterUpdate(ticket, 50 + i, now + 1);
        e::AfterUpdate(ticket, 500, now + 1);
        const auto pair = Drain(); phase_records += static_cast<unsigned>(pair.size());
        Check(pair.size() == 2 && pair[0].serial == pair[1].serial && pair[1].correlated_end == end.serial,
              "each sampled update forwards as one distinct paired observation");
        if (i >= 1) {
            const auto reads = m.reads;
            Check(!e::BeforeUpdate(update, now + 1).qualified && m.reads == reads,
                  "cadence and budget refusals are cheap before any reader");
        }
    }
    Check(phase_records == 56, "phase traffic uses only its 28 reserved pairs");
    Check(e::PublishOwner(Owner(), GetCurrentThreadId(), start + 2001), "fresh lease does not extend End correlation window");
    const auto reads = m.reads;
    Check(!e::BeforeUpdate(update, start + 2001).qualified && m.reads == reads, "expired End window refuses reads");
    unsigned submission_records = 2;
    for (unsigned i = 1; i < e::kSubmissionPairCap; ++i) {
        const auto ticket = e::BeforeSubmission(Submission(), start + 2001 + i);
        Check(ticket.qualified, "phase traffic preserves capacity for ensuing natural End pairs");
        e::AfterSubmission(ticket, i, start + 2002 + i);
        submission_records += static_cast<unsigned>(Drain().size());
    }
    Check(submission_records + phase_records == e::kObservationCap, "64-record lifetime budget survives drain and refresh");
    const auto capped_reads = m.reads;
    Check(!e::BeforeSubmission(Submission(), start + 2010).qualified && !e::BeforeUpdate(update, start + 2010).qualified
        && m.reads == capped_reads, "exhausted admission remains bounded with no read work");
}
void WindowExpiryWithoutExhaustion() {
    if (!e::Compiled()) return;
    Memory m; Check(Configure(m) && e::PublishOwner(Owner(), GetCurrentThreadId(), 1000), "uncapped window fixture configured");
    const auto end = e::BeforeSubmission(Submission(), 1000); e::AfterSubmission(end, 1, 1001); Drain();
    const e::Update update{entry, base + 0xaf0381, 999};
    Check(e::PublishOwner(Owner(), GetCurrentThreadId(), 3000), "fresh lease at exact window endpoint");
    const auto boundary = e::BeforeUpdate(update, 3000);
    Check(boundary.qualified && boundary.correlated_end == end.serial, "2s endpoint remains observable with unused phase budget");
    e::AfterUpdate(boundary, 2, 3001); Drain();
    Check(e::PublishOwner(Owner(), GetCurrentThreadId(), 3001), "fresh lease one ms after window");
    const auto reads = m.reads;
    Check(!e::BeforeUpdate(update, 3001).qualified && m.reads == reads,
          "window expiry independently refuses reads despite fresh lease and 27 unspent phase pairs");
    const auto next = e::BeforeSubmission(Submission(true), 3001); e::AfterSubmission(next, 3, 3002); Drain();
    const auto renewed = e::BeforeUpdate(update, 3002);
    Check(next.qualified && renewed.qualified && next.serial != end.serial && renewed.correlated_end == next.serial,
          "new natural End opens its own window without inheriting prior event identity");
    e::AfterUpdate(renewed, 4, 3003);
    Check(Drain().size() == 2, "renewed window records one update pair");
}
int main() {
    ResolverAndPolicy(); RuntimeAndForwarding(); ReentrancyWorkerAndCap(); StartupWindowAndReservedPairs(); WindowExpiryWithoutExhaustion();
    std::printf("%u natural upper End observer checks passed; compiled=%u; detached fake reads, zero game/native calls or mutations\n", checks, e::Compiled() ? 1 : 0);
}
