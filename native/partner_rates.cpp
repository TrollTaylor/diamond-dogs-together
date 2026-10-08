// Original MGS Co-op native rates mirror (pure; docs/native-puppet.md "Rates").
#include "mgscoop/partner_rates.hpp"
#include <cmath>
#include <cstring>

namespace mgscoop::partner_rates {
namespace {
std::uint32_t Bits(float value) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}
bool Same(float a, float b) noexcept { return Bits(a) == Bits(b); }
// The mirrored fields in Wire order.
constexpr Field kMirrored[3] = {kDash, kCrawl, kCrawlFactor};
void Set(Plan& plan, const Record& current, Field field, float value) noexcept {
    plan.value[field] = value;
    if (!Same(current.f[field], value)) plan.mask = static_cast<std::uint8_t>(plan.mask | (1u << field));
}
// The products the game derives (0x9CB7E1, 0x9CB803, 0x10BD1C8, 0x11403FD).
void Products(Plan& plan, const Record& current, float dash, float crawl, float factor) noexcept {
    Set(plan, current, kDashRate, current.f[kDashFactor] * dash);
    Set(plan, current, kCrawlRate, crawl * factor);
}
} // namespace

bool Finite(const Record& record) noexcept {
    for (float v : record.f)
        if (!std::isfinite(v)) return false;
    return true;
}

bool ValidWire(const Wire& w) noexcept {
    const std::uint16_t v[3] = {w.dash, w.crawl, w.crawl_factor};
    for (std::uint16_t x : v)
        if (x < kMinMilli || x > kMaxMilli) return false;
    return true;
}

bool ToWire(const Record& record, Wire& out) noexcept {
    out = Wire{};
    std::uint16_t v[3]{};
    for (int i = 0; i < 3; ++i) {
        const float f = record.f[kMirrored[i]];
        if (!std::isfinite(f)) return false;
        const double milli = std::round(static_cast<double>(f) * 1000.0);
        if (milli < kMinMilli || milli > kMaxMilli) return false;
        v[i] = static_cast<std::uint16_t>(milli);
    }
    out = Wire{v[0], v[1], v[2]};
    return true;
}

float FromMilli(std::uint16_t milli) noexcept { return static_cast<float>(milli) / 1000.0f; }

const char* MirrorName(Mirror mirror) noexcept {
    switch (mirror) {
    case Mirror::wait: return "wait";
    case Mirror::ok: return "ok";
    case Mirror::latched: return "latched";
    default: return "off";
    }
}

const char* ReasonName(Reason reason) noexcept {
    switch (reason) {
    case Reason::no_remote: return "no_remote";
    case Reason::no_record: return "no_record";
    case Reason::sp_vtable: return "sp_vtable";
    case Reason::rec_vtable: return "rec_vtable";
    case Reason::bounds: return "bounds";
    case Reason::nonfinite: return "nonfinite";
    case Reason::range: return "range";
    case Reason::fault: return "fault";
    default: return "none";
    }
}

void Reset(MirrorState& state) noexcept { state = MirrorState{}; }

void Latch(MirrorState& state, Reason reason) noexcept {
    state.latched = true;
    state.applied = false;
    state.reason = reason;
}

Plan Apply(MirrorState& state, const Record& current, const Wire& remote) noexcept {
    Plan plan{};
    if (state.latched) return plan;
    if (!Finite(current)) { Latch(state, Reason::nonfinite); return plan; }
    if (!ValidWire(remote)) {
        // Back to the game's values first, then latched.
        plan = Restore(state, current);
        Latch(state, Reason::range);
        return plan;
    }
    const float want[3] = {FromMilli(remote.dash), FromMilli(remote.crawl), FromMilli(remote.crawl_factor)};
    if (!state.applied) {
        for (int i = 0; i < 3; ++i) state.orig[i] = current.f[kMirrored[i]];
        state.applied = true;
    } else {
        // The game rewrote a field since our last write (instance setup,
        // crawl factor events): that value is its new original.
        for (int i = 0; i < 3; ++i) {
            if (!Same(current.f[kMirrored[i]], state.written[i])) {
                state.orig[i] = current.f[kMirrored[i]];
                plan.reapplied = true;
            }
        }
        if (plan.reapplied) ++state.reapplied;
    }
    for (int i = 0; i < 3; ++i) {
        Set(plan, current, kMirrored[i], want[i]);
        state.written[i] = want[i];
    }
    Products(plan, current, want[0], want[1], want[2]);
    state.reason = Reason::none;
    if (plan.mask) ++state.writes;
    return plan;
}

Plan Restore(MirrorState& state, const Record& current) noexcept {
    Plan plan{};
    if (!state.applied) return plan;
    state.applied = false;
    if (!Finite(current)) { Latch(state, Reason::nonfinite); return plan; }
    float value[3]{};
    for (int i = 0; i < 3; ++i) {
        const float now = current.f[kMirrored[i]];
        value[i] = Same(now, state.written[i]) ? state.orig[i] : now;  // a field the game rewrote keeps the game's value
        Set(plan, current, kMirrored[i], value[i]);
    }
    Products(plan, current, value[0], value[1], value[2]);
    ++state.restores;
    return plan;
}

Record After(const Record& current, const Plan& plan) noexcept {
    Record out = current;
    for (int i = 0; i < kFields; ++i)
        if (plan.mask & (1u << i)) out.f[i] = plan.value[i];
    return out;
}

} // namespace mgscoop::partner_rates
