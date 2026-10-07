#pragma once
// Portable action data only. No game addresses, hooks, allocation or playback.
// GUN_FIRE state rises identify observed burst onsets, not physical bullets.
// RELOAD input rises identify requests, not executed/completed reloads.
#include "mgscoop/protocol.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace mgscoop::partner_actions {
enum class FireSource : std::uint8_t { none, observed_fire_state_rise, verified_executed_shot };
enum class ReloadSource : std::uint8_t { none, requested_reload_input_rise, verified_executed_reload_start };
enum class EventKind : std::uint8_t { fire = 1, reload = 2 };
constexpr std::size_t kHistory = 8;
constexpr std::size_t kPayloadSize = 36 + 12 * kHistory;
constexpr std::size_t kFrameSize = wire::header_size + kPayloadSize;
constexpr std::uint32_t kEventAgeMs = 500;
constexpr std::uint64_t kNetworkAgeMs = 350;
constexpr std::uint8_t kGunFlags = 0x0b;
struct Event { std::uint32_t token = 0, tick_ms = 0; EventKind kind = EventKind::fire; };
struct Payload {
    FireSource fire_source = FireSource::none;
    ReloadSource reload_source = ReloadSource::none;
    std::uint8_t stance = 0, flags = 0;
    std::uint16_t equip = 0;
    std::uint32_t mission = 0, generation = 0, tick_ms = 0;
    std::uint32_t fire_count = 0, reload_count = 0, cursor = 0;
    std::uint8_t count = 0;
    std::array<Event, kHistory> history{}; // oldest to newest, unused slots zero on wire
};
struct Frame { std::uint64_t session = 0; std::uint32_t epoch = 0, sequence = 0; Payload action{}; };
inline bool Newer(std::uint32_t value, std::uint32_t prior) noexcept {
    const auto d = value - prior; return d != 0 && d < 0x80000000u;
}
inline bool Valid(const Payload& p) noexcept {
    if (!p.mission || !p.generation || !p.equip || p.stance > 2 || (p.flags & ~kGunFlags)
        || static_cast<unsigned>(p.fire_source) > 2 || static_cast<unsigned>(p.reload_source) > 2
        || p.count > kHistory || p.cursor != p.fire_count + p.reload_count) return false;
    if ((p.fire_source == FireSource::none && p.fire_count)
        || (p.reload_source == ReloadSource::none && p.reload_count)) return false;
    if (!p.count && (p.cursor || p.fire_count || p.reload_count)) return false;
    for (unsigned i = 0; i < p.count; ++i) {
        const auto& e = p.history[i];
        if ((e.kind != EventKind::fire && e.kind != EventKind::reload)
            || (e.kind == EventKind::fire && p.fire_source == FireSource::none)
            || (e.kind == EventKind::reload && p.reload_source == ReloadSource::none)
            || e.token != p.cursor - (p.count - 1 - i) || p.tick_ms - e.tick_ms >= 0x80000000u
            || (i && e.tick_ms - p.history[i - 1].tick_ms >= 0x80000000u)) return false;
    }
    return true;
}
namespace detail {
inline void Put(std::uint8_t* out, std::uint64_t value, unsigned n) noexcept {
    for (unsigned i = 0; i < n; ++i) out[i] = static_cast<std::uint8_t>(value >> (8 * i));
}
inline std::uint64_t Get(const std::uint8_t* in, unsigned n) noexcept {
    std::uint64_t value = 0; for (unsigned i = 0; i < n; ++i) value |= std::uint64_t(in[i]) << (8 * i); return value;
}
}
// Separate kind=event companion. Existing exact 56/64-byte snapshot formats
// are unchanged. Old peers can drop this frame and still consume movement.
inline std::size_t Encode(const Frame& f, std::uint8_t (&out)[kFrameSize]) noexcept {
    for (auto& b : out) b = 0;
    if (!f.session || !f.epoch || !Valid(f.action)) return 0;
    auto put = [&](std::size_t off, std::uint64_t value, unsigned n) { detail::Put(out + off, value, n); };
    out[0] = 'M'; out[1] = 'G'; out[2] = 'S'; out[3] = 'C';
    put(4, wire::version, 2); put(6, static_cast<unsigned>(wire::Kind::event), 2);
    put(8, f.session, 8); put(16, f.epoch, 4); put(20, f.sequence, 4); put(24, kPayloadSize, 4);
    const auto& p = f.action;
    out[28] = 0x47; out[29] = 1; out[30] = static_cast<std::uint8_t>(p.fire_source);
    out[31] = static_cast<std::uint8_t>(p.reload_source); out[32] = p.stance; out[33] = p.flags;
    put(34, p.equip, 2); put(36, p.mission, 4); put(40, p.generation, 4); put(44, p.tick_ms, 4);
    put(48, p.fire_count, 4); put(52, p.reload_count, 4); put(56, p.cursor, 4); out[60] = p.count;
    for (unsigned i = 0; i < p.count; ++i) {
        const auto off = 64 + 12 * i; put(off, p.history[i].token, 4); put(off + 4, p.history[i].tick_ms, 4);
        out[off + 8] = static_cast<std::uint8_t>(p.history[i].kind);
    }
    return kFrameSize;
}
// Atomic output on failure; strict length, all reserved/unused bytes, enums,
// counters, ordered event identities, frame kind and nonzero session/epoch.
inline bool Decode(const std::uint8_t* data, std::size_t size, Frame& out) noexcept {
    if (!data || size != kFrameSize || data[0] != 'M' || data[1] != 'G' || data[2] != 'S' || data[3] != 'C') return false;
    auto get = [&](std::size_t off, unsigned n) { return detail::Get(data + off, n); };
    if (get(4, 2) != wire::version || get(6, 2) != static_cast<unsigned>(wire::Kind::event)
        || get(24, 4) != kPayloadSize || data[28] != 0x47 || data[29] != 1 || data[61] || data[62] || data[63]) return false;
    Frame f{}; f.session = get(8, 8); f.epoch = static_cast<std::uint32_t>(get(16, 4));
    f.sequence = static_cast<std::uint32_t>(get(20, 4)); auto& p = f.action;
    p.fire_source = static_cast<FireSource>(data[30]); p.reload_source = static_cast<ReloadSource>(data[31]);
    p.stance = data[32]; p.flags = data[33]; p.equip = static_cast<std::uint16_t>(get(34, 2));
    p.mission = static_cast<std::uint32_t>(get(36, 4)); p.generation = static_cast<std::uint32_t>(get(40, 4));
    p.tick_ms = static_cast<std::uint32_t>(get(44, 4)); p.fire_count = static_cast<std::uint32_t>(get(48, 4));
    p.reload_count = static_cast<std::uint32_t>(get(52, 4)); p.cursor = static_cast<std::uint32_t>(get(56, 4)); p.count = data[60];
    if (p.count > kHistory) return false;
    for (unsigned i = 0; i < kHistory; ++i) {
        const auto off = 64 + 12 * i;
        if (i >= p.count) { for (unsigned j = 0; j < 12; ++j) if (data[off + j]) return false; }
        else {
            if (data[off + 9] || data[off + 10] || data[off + 11]) return false;
            p.history[i] = {static_cast<std::uint32_t>(get(off, 4)), static_cast<std::uint32_t>(get(off + 4, 4)), static_cast<EventKind>(data[off + 8])};
        }
    }
    if (!f.session || !f.epoch || !Valid(p)) return false;
    out = f; return true;
}
// Source-owned natural Lua receipt, not a network parser or request authorizer.
// v1 explicitly labels raw state rises/input edges; it cannot assert physical
// shots. Accept exactly 10 + 3*count unsigned decimal fields, atomically.
inline bool DecodeReceipt(const char* text, std::size_t size, Payload& out) noexcept {
    constexpr char prefix[] = "return 'MGS_COOP_PARTNER_ACTIONS_V1' -- ";
    constexpr std::size_t prefix_size = sizeof(prefix) - 1;
    if (!text || size <= prefix_size || size > 512) return false;
    for (std::size_t i = 0; i < prefix_size; ++i) if (text[i] != prefix[i]) return false;
    std::size_t at = prefix_size;
    auto number = [&](std::uint32_t& n) {
        if (at >= size || text[at] < '0' || text[at] > '9') return false;
        n = 0;
        do {
            const unsigned d = unsigned(text[at++] - '0');
            if (n > (0xffffffffu - d) / 10) return false;
            n = n * 10 + d;
        } while (at < size && text[at] >= '0' && text[at] <= '9');
        if (at < size) { if (text[at] != ' ') return false; ++at; if (at == size) return false; }
        return true;
    };
    std::array<std::uint32_t, 10> v{};
    for (auto& field : v) if (!number(field)) return false;
    if (v[2] > 65535 || v[3] > 2 || v[5] > 255 || v[9] > kHistory) return false;
    Payload p{}; p.fire_source = FireSource::observed_fire_state_rise; p.reload_source = ReloadSource::requested_reload_input_rise;
    p.generation = v[0]; p.mission = v[1]; p.equip = static_cast<std::uint16_t>(v[2]); p.stance = static_cast<std::uint8_t>(v[3]);
    p.tick_ms = v[4]; p.flags = static_cast<std::uint8_t>(v[5]); p.fire_count = v[6]; p.reload_count = v[7]; p.cursor = v[8]; p.count = static_cast<std::uint8_t>(v[9]);
    for (unsigned i = 0; i < p.count; ++i) {
        std::uint32_t token = 0, tick = 0, kind = 0;
        if (!number(token) || !number(tick) || !number(kind) || kind < 1 || kind > 2) return false;
        p.history[i] = {token, tick, static_cast<EventKind>(kind)};
    }
    if (at != size || !Valid(p)) return false;
    out = p; return true;
}

struct Sample {
    bool enabled = false, eligible = false, raw_fire_active = false, raw_reload_pressed = false;
    std::uint32_t mission = 0, tick_ms = 0;
    std::uint16_t equip = 0;
    std::uint8_t stance = 0, flags = 0;
};
struct Producer { bool ready = false, fire = false, reload = false; std::uint32_t generation = 0; Payload action{}; };
inline void Clear(Producer& p) noexcept { p.ready = p.fire = p.reload = false; p.action = {}; }
inline void Append(Payload& p, EventKind kind, std::uint32_t tick) noexcept {
    if (p.count == kHistory) { for (unsigned i = 1; i < kHistory; ++i) p.history[i - 1] = p.history[i]; --p.count; }
    ++p.cursor; if (kind == EventKind::fire) ++p.fire_count; else ++p.reload_count;
    p.history[p.count++] = {p.cursor, tick, kind};
}
// First/new-context sample establishes a baseline, including already held input.
// Sampling raw state avoids merging edges through the existing 100/333ms flags.
inline bool Observe(Producer& p, const Sample& s) noexcept {
    if (!s.enabled || !s.eligible || !s.mission || !s.equip || s.stance > 2 || (s.flags & ~kGunFlags)) { Clear(p); return false; }
    if (!p.ready || p.action.mission != s.mission || p.action.equip != s.equip || p.action.stance != s.stance
        || s.tick_ms - p.action.tick_ms >= 0x80000000u) {
        Clear(p); if (!++p.generation) ++p.generation; p.ready = true; p.fire = s.raw_fire_active; p.reload = s.raw_reload_pressed;
        p.action.fire_source = FireSource::observed_fire_state_rise; p.action.reload_source = ReloadSource::requested_reload_input_rise;
        p.action.generation = p.generation; p.action.mission = s.mission; p.action.equip = s.equip; p.action.stance = s.stance;
    } else {
        // Deterministic order for simultaneous samples; actual sub-frame order
        // is unknown and is not claimed. Executed callbacks would retain theirs.
        if (s.raw_fire_active && !p.fire) Append(p.action, EventKind::fire, s.tick_ms);
        if (s.raw_reload_pressed && !p.reload) Append(p.action, EventKind::reload, s.tick_ms);
        p.fire = s.raw_fire_active; p.reload = s.raw_reload_pressed;
    }
    p.action.tick_ms = s.tick_ms; p.action.flags = s.flags; return true;
}

// Peer identity is supplied by the trusted transport, never by the payload.
struct Expected { std::uint64_t peer = 0, session = 0; std::uint32_t epoch = 0, mission = 0; };
struct Context {
    std::uint64_t peer = 0, session = 0;
    std::uint32_t epoch = 0, mission = 0, generation = 0;
    std::uint16_t equip = 0; std::uint8_t stance = 0;
    FireSource fire_source = FireSource::none; ReloadSource reload_source = ReloadSource::none;
};
inline bool Same(const Context& a, const Context& b) noexcept {
    return a.peer == b.peer && a.session == b.session && a.epoch == b.epoch && a.mission == b.mission
        && a.generation == b.generation && a.equip == b.equip && a.stance == b.stance
        && a.fire_source == b.fire_source && a.reload_source == b.reload_source;
}
enum class ReceiptStatus : std::uint8_t { rejected, duplicate, baseline, update, history_gap };
struct Receipt {
    ReceiptStatus status = ReceiptStatus::rejected;
    Context context{}; std::uint32_t sequence = 0, tick_ms = 0; std::uint8_t flags = 0, count = 0, expired = 0;
    std::array<Event, kHistory> events{};
};
struct Receiver {
    bool ready = false; Context context{};
    std::uint32_t sequence = 0, tick_ms = 0, cursor = 0, fire_count = 0, reload_count = 0;
    std::uint64_t received_ms = 0;
};
inline void Reset(Receiver& r) noexcept { r = {}; }
inline bool Expired(const Receiver& r, std::uint64_t now) noexcept {
    return !r.ready || now < r.received_ms || now - r.received_ms > kNetworkAgeMs;
}
inline Receipt Receive(Receiver& r, const Frame& f, std::uint64_t peer, const Expected& want, std::uint64_t now) noexcept {
    Receipt out{}; const auto& p = f.action;
    if (!want.peer || !want.session || !want.epoch || !want.mission || peer != want.peer || f.session != want.session
        || f.epoch != want.epoch || p.mission != want.mission || !Valid(p)) return out;
    Context c{peer, f.session, f.epoch, p.mission, p.generation, p.equip, p.stance, p.fire_source, p.reload_source};
    const bool same_owner = r.ready && r.context.peer == peer && r.context.session == f.session && r.context.epoch == f.epoch;
    if (same_owner && !Newer(f.sequence, r.sequence)) { out.status = ReceiptStatus::duplicate; return out; }
    bool baseline = !r.ready || !same_owner || Expired(r, now);
    if (same_owner && !Same(c, r.context)) {
        if (!Newer(c.generation, r.context.generation)) return out; // changed equipment/stance/source must bump generation
        baseline = true;
    }
    if (!baseline && p.tick_ms - r.tick_ms >= 0x80000000u) return out;
    out.context = c; out.sequence = f.sequence; out.tick_ms = p.tick_ms; out.flags = p.flags;
    if (baseline) out.status = ReceiptStatus::baseline;
    else {
        const auto delta = p.cursor - r.cursor, fire_delta = p.fire_count - r.fire_count, reload_delta = p.reload_count - r.reload_count;
        if (delta >= 0x80000000u || fire_delta >= 0x80000000u || reload_delta >= 0x80000000u
            || std::uint64_t(fire_delta) + reload_delta != delta) return Receipt{};
        if (delta > p.count) out.status = ReceiptStatus::history_gap; // no fabricated missing events
        else {
            unsigned fires = 0, reloads = 0;
            for (unsigned i = p.count - delta; i < p.count; ++i) {
                const auto& e = p.history[i]; if (e.kind == EventKind::fire) ++fires; else ++reloads;
                if (p.tick_ms - e.tick_ms <= kEventAgeMs) out.events[out.count++] = e; else ++out.expired;
            }
            if (fires != fire_delta || reloads != reload_delta) return Receipt{};
            out.status = ReceiptStatus::update;
        }
    }
    r = {true, c, f.sequence, p.tick_ms, p.cursor, p.fire_count, p.reload_count, now}; return out;
}
} // namespace mgscoop::partner_actions
