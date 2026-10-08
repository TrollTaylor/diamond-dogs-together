#pragma once
// Native rates (docs/native-puppet.md "Rates"), pure part: instance 1's
// per-id movement rate record mirrors the remote's. No Win32 calls and no
// game memory (mgs_coop_core); partner_player reads the records,
// partner_input_hook writes instance 1's inside the drain window.
//
// The locomotion speed of a body is base(0x109F7E0) * rate(0x109F690), and
// the rate is rec+0x44 while crawling (stance 2), rec+0x50 while dashing
// (move class 3) and 1.0 otherwise. rec is the body's own record of
// sp = *(PS+0x260): rec = *(sp+0x10) + (id - *(u32)(sp+8))*0x88. The instance
// setup fills +0x48 (dash) and +0x3C (crawl) from the provider *(P+0x130)
// vt+0x28(id, 1|2) and derives +0x50 = +0x48 * +0x4C, +0x44 = +0x3C * +0x40;
// +0x4C (per-frame dash factor) and +0x40 (crawl factor, provider type 0x17
// or 1.0) change while playing. The provider answers per id (equipment and
// staff skills of the local player), so instance 1 kept 1.0 while the remote
// ran at about 1.2: hidden runs a7823/63199 measured sprint and crawl at
// 0.835 of the remote's speed, everything else at 1.00.
//
// The mirror writes the remote's +0x48, +0x3C and +0x40 into instance 1's
// record and recomputes +0x50 and +0x44 with the game's own formulas (the
// dash factor +0x4C stays the body's own). A field the game rewrote since our
// last write is taken as its new original and written again (reapplied).
// Restore writes the originals back (fields the game rewrote keep the game's
// value) and recomputes the products.
#include <cstdint>

namespace mgscoop::partner_rates {

// rec+0x3C..+0x50 in record order.
enum Field : std::uint8_t { kCrawl = 0, kCrawlFactor = 1, kCrawlRate = 2, kDash = 3, kDashFactor = 4, kDashRate = 5, kFields = 6 };
inline constexpr std::uint32_t kFirstOffset = 0x3C;
struct Record {
    float f[kFields];
};
bool Finite(const Record& record) noexcept;

// The three mirrored inputs on the wire (input frame v4), milli units.
inline constexpr std::uint16_t kMinMilli = 500, kMaxMilli = 2000;
struct Wire {
    std::uint16_t dash, crawl, crawl_factor;  // +0x48, +0x3C, +0x40 x 1000
};
bool ValidWire(const Wire& wire) noexcept;
// Rounds +0x48/+0x3C/+0x40 to milli; false (out zeroed) when one is not
// finite or outside [kMinMilli, kMaxMilli].
bool ToWire(const Record& record, Wire& out) noexcept;
float FromMilli(std::uint16_t milli) noexcept;

enum class Mirror : std::uint8_t { off, wait, ok, latched };
const char* MirrorName(Mirror mirror) noexcept;
// Why the mirror waits (no_remote, no_record) or latched (the rest).
enum class Reason : std::uint8_t { none, no_remote, no_record, sp_vtable, rec_vtable, bounds, nonfinite, range, fault };
const char* ReasonName(Reason reason) noexcept;

struct MirrorState {
    bool applied;        // the record holds our values; orig holds the game's
    bool latched;        // refused until Reset (mission)
    Reason reason;
    float orig[3];       // the game's own +0x48, +0x3C, +0x40 (restore targets)
    float written[3];    // what the mirror last wrote there
    std::uint32_t writes, reapplied, restores;
};
void Reset(MirrorState& state) noexcept;
// One write plan: bit i of mask = store value[i] at field i (+0x3C + 4i);
// the dash factor +0x4C is never written.
struct Plan {
    std::uint8_t mask;
    float value[kFields];
    bool reapplied;
};
// The drain window's step with the record's current values and the remote's
// wire rates. Refusals latch: nonfinite current values (empty plan), out-of-
// range remote values (the restore plan, when applied).
Plan Apply(MirrorState& state, const Record& current, const Wire& remote) noexcept;
// Back to the game's values (nothing when not applied).
Plan Restore(MirrorState& state, const Record& current) noexcept;
// A refusal by the caller (identity checks, faults): latched, nothing written.
void Latch(MirrorState& state, Reason reason) noexcept;
// The record after a plan (for tests and the caller's bookkeeping).
Record After(const Record& current, const Plan& plan) noexcept;

} // namespace mgscoop::partner_rates
