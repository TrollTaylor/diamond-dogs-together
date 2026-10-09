#pragma once
// Native puppet job deferral (docs/native-puppet.md 4): block 1's per-block
// job (the player GameObject's primary vtable slot +0x50, qword 0x2295420 ->
// 0x9C1B90) is deferred out of the parallel batch by a slot wrapper W50 (H1)
// and run on the main thread right after that batch is joined, from a wrapper
// of the only call of the batch function (0xBF4F20, call site 0xBF4D55, H2).
// The drain recreates the context an inline-run job has: it pushes the game's
// own TLS scratch scope (0x34470 / 0x34550, as 0xBF4F20 does around its batch)
// and takes the entry read lock exactly as the job body does; inside, the
// input hook's window swaps in instance 1's pad (RunJobWindow).
//
// Threads. W50 runs on job workers, on the main thread when a queued job runs
// inline during the join, and on any thread that calls GO vt+0x50 for P: it
// uses atomics only (no logging, allocation, locks or Win32 calls other than
// GetCurrentThreadId) and defers only when called by the job body (return
// address 0xBF4CEE) inside an active main-thread batch, for sub 0 of the
// published identity's block 1. WrapBatch runs on whatever thread runs the
// ticker; only the hook (main) thread marks batches active and drains.
// Batches are counted, not flagged: a batch nested in another hook-thread
// batch (a job run inline during the join that ticks again) or any batch on
// another thread never defers (nested / foreign), and only the outermost
// hook-thread batch drains, after its join, once no batch is in flight.
// Pending records: claim (0 -> 2), write, publish (1); the owning WrapBatch
// consumes with 1 -> 0 after the join. MissionStart/disarm never touch them;
// a batch that unwinds (an exception through the join) drops its record
// (counted as torn) so it is never drained in a later frame.
//
// Patches are never removed (the DLL is pinned); disarmed wrappers forward.
#include <cstddef>
#include <cstdint>

namespace mgscoop::partner_defer {

using LogFn = void (*)(const char* line) noexcept;
enum class Mode : std::uint8_t { off, census, on };
const char* ModeName(Mode mode) noexcept;

// The job body's parameter (0xBF4C90; caller stack, 0x18 bytes).
struct JobParam {
    std::uint32_t block;   // pool index
    std::uint32_t sub;
    std::uint32_t mode;    // batch pass 0 / 1
    std::uint32_t nbits;   // entry+0xC
    const std::uint8_t* bits;  // entry+0x28
};
static_assert(sizeof(JobParam) == 0x18, "job param layout (0xBF4C90)");

// Installs H2 then H1 once per process (H0 and the wraps are the input
// hook's). `base` is the image base. nullptr on success, otherwise a static
// reason (also logged; sticky): bytes | scope | slot | changed | protect |
// allocate_near | range | alignment | cache_line | exchange. An H1 refusal
// leaves H2 installed as a pass-through. protect_restore = installed (warned).
const char* Install(const void* base, Mode mode, LogFn log) noexcept;
bool H2Installed() noexcept;
bool H1Installed() noexcept;
const char* InstallRefusal() noexcept;  // nullptr unless an install was refused
void SetMode(Mode mode) noexcept;
Mode CurrentMode() noexcept;
// 1 Hz: the H1 slot still holds W50 and the H2 rel32 still reaches our stub.
bool PatchLive(const void* base) noexcept;

// Published by the driver every tick where the input targets resolve,
// independent of arming (census needs block classes without an Arm).
struct Identity {
    std::uintptr_t go;  // P - 0x20
    std::uint8_t pool0, pool1;
};
void PublishIdentity(const Identity& identity) noexcept;
void ClearIdentity() noexcept;
// Publication order: identity, then the enabled Arm, then SetArmed(true);
// leaving: SetArmed(false) first.
void SetArmed(bool armed) noexcept;
// Process shutdown (the game window is closing): disarms for good, from any thread.
void Shutdown() noexcept;
bool Armed() noexcept;
// Disarms and clears the deferral fault (never touches pending or the batch counts).
void MissionStart() noexcept;
// A batch is in flight: on the hook thread (a job running inline during the
// join; nested or not) or on any other thread (block 0's job may be running
// on a worker). Shared PP/CAM fields must not be swapped while it is true.
bool InBatch() noexcept;

// Process-lifetime totals (the driver logs deltas); the *_us_max and
// spin_max maxima are cleared by each drain.
struct Stats {
    bool h1, h2;
    Mode mode;
    bool armed, faulted;
    std::uint32_t fault_phase;  // FaultPhase
    std::uint32_t fault_code;
    std::uint32_t batches, batch_threads_seen, batch_thread, hook_thread;
    std::uint32_t w50_calls, w50_b0_s0, w50_b1_s0, w50_other_s0, w50_subs, w50_inline, w50_worker, ret_other;
    std::uint32_t deferred, dup, pass_reason[16];
    std::uint32_t drains[2], windowed, unwindowed, lost, gate_closed, torn, entry_changed, scope_unbalanced;
    std::uint32_t id1_pass;     // bit m: the last drain of pass m had id 1's bit == m
    std::uint32_t drain_us_max, batch_us_max, spin_max;
    // Hook-thread batches nested in another hook-thread batch, batches on any
    // other thread, and records dropped because their batch unwound (also in torn).
    std::uint32_t nested_batches, foreign_batches, unwound;
};
void DrainStats(Stats& out) noexcept;
enum class FaultPhase : std::uint8_t { none, lost, torn, gate, scope, find, lock, job };
const char* FaultPhaseName(std::uint32_t phase) noexcept;

namespace detail {
// W50's pure decision (unit-tested).
enum class Pass : std::uint8_t { defer, mode, unarmed, faulted, h2, ret_other, pmode, no_batch, nested, foreign, sub, identity, go,
                                  block, count_ };
static_assert(static_cast<unsigned>(Pass::count_) <= 16, "pass_reason has 16 slots");
const char* PassName(Pass pass) noexcept;
struct DecisionIn {
    Mode mode;
    bool armed, faulted, h2_ready;
    std::uintptr_t ret, expected_ret;
    std::uint32_t pmode, sub, block;
    bool batch_active;  // a hook-thread batch of pass pmode is in flight (pmode <= 1)
    bool nested;        // more than one hook-thread batch is in flight
    bool foreign;       // a batch on another thread is in flight
    bool identity;      // an identity is published
    std::uintptr_t go, id_go;
    std::uint8_t id_pool1;
};
Pass DeferDecision(const DecisionIn& in) noexcept;
// The entry of `go` in the group's pointer vector [begin, end): null entries
// skipped, more than kMaxEntries or a misaligned span refused (0). Plain
// reads: the caller holds SEH.
inline constexpr std::size_t kMaxEntries = 4096;
std::uintptr_t FindEntry(std::uintptr_t begin, std::uintptr_t end, std::uintptr_t go) noexcept;
// The 17-byte H2 block at 0xBF4D50 with the rel32 at offsets 6..9 excluded.
bool BatchBlockMatches(const unsigned char* block) noexcept;
// Every contract site H2 (and the scope pair) / H1 rely on, in a mapped
// image: nullptr, or "bytes" / "scope" with the first mismatching RVA.
const char* H2SitesMatch(std::uintptr_t base, std::uintptr_t& mismatch) noexcept;
const char* H1SitesMatch(std::uintptr_t base, std::uintptr_t& mismatch) noexcept;
// A RetargetCall / PatchQword result: installed (nullptr, or protect_restore
// with a warning: the patch IS live) or the refusal reason ("changed" on the
// H1 slot is "slot").
struct Verdict {
    bool installed, warn;
    const char* reason;
};
Verdict ClassifyPatch(const char* why, bool slot) noexcept;

// --- Test seams (fixtures only; production binds the image's functions) -----
using BatchFn = void (*)(void* group, std::uint32_t mode) noexcept;
using JobFn = void (*)(void* go, const JobParam* param) noexcept;
using ScopeCtorFn = void* (*)(void* scope) noexcept;
using ScopeDtorFn = void (*)(void* scope) noexcept;
using ScopeReadFn = std::uintptr_t (*)() noexcept;
using ThreadFn = std::uint32_t (*)() noexcept;
struct Bindings {
    BatchFn batch;
    JobFn job;
    ScopeCtorFn scope_ctor;
    ScopeDtorFn scope_dtor;
    ScopeReadFn scope_read;      // the current TLS scope (telemetry / balance)
    std::uintptr_t job_return;   // W50's expected return address
    ThreadFn hook_thread;        // the input hook's thread (main)
};
// Replaces the bound functions after Install (null members keep theirs).
void BindForTest(const Bindings& bindings) noexcept;
// Clears every process-level flag so a fixture can install again on a new
// synthetic image (never called in the game).
void ResetForTest() noexcept;
// The installed wrappers (WrapBatch, W50) for fixtures.
void* WrapBatchEntry() noexcept;
void* W50Entry() noexcept;
} // namespace detail

} // namespace mgscoop::partner_defer
