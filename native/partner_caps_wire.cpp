// Original MGS Co-op native puppet online: capability message codec
// (docs/native-puppet.md "Online").
#include "mgscoop/partner_caps_wire.hpp"
#include <cstring>

namespace mgscoop::partner_caps_wire {
namespace {
void Put16(std::uint8_t* out, std::uint16_t v) noexcept {
    out[0] = static_cast<std::uint8_t>(v);
    out[1] = static_cast<std::uint8_t>(v >> 8);
}
void Put32(std::uint8_t* out, std::uint32_t v) noexcept {
    for (unsigned i = 0; i < 4; ++i) out[i] = static_cast<std::uint8_t>(v >> (8 * i));
}
std::uint16_t Get16(const std::uint8_t* in) noexcept { return static_cast<std::uint16_t>(in[0] | (in[1] << 8)); }
std::uint32_t Get32(const std::uint8_t* in) noexcept {
    return static_cast<std::uint32_t>(in[0]) | (static_cast<std::uint32_t>(in[1]) << 8) | (static_cast<std::uint32_t>(in[2]) << 16) |
           (static_cast<std::uint32_t>(in[3]) << 24);
}
// Field ranges: profile 0..2, drive 0..3 (partner_native_net::Drive), mode
// 0..5 (partner_native::Mode), policy 0..1; the reason is any code.
bool FieldsOk(const Caps& c) noexcept {
    return (c.features & ~Feature::Known) == 0 && c.profile <= 2 && c.drive <= 3 && c.mode <= 5 && c.policy <= 1;
}
bool SameContent(const Caps& a, const Caps& b) noexcept {
    return a.features == b.features && a.profile == b.profile && a.drive == b.drive && a.reason == b.reason && a.mode == b.mode &&
           a.policy == b.policy;
}
} // namespace

std::size_t Encode(const Caps& c, std::uint8_t (&out)[kSize]) noexcept {
    std::memset(out, 0, sizeof(out));
    if (c.revision == 0 || c.epoch == 0 || !FieldsOk(c)) return 0;
    out[0] = kMagic;
    out[1] = kVersion;
    Put16(out + 2, c.revision);
    Put32(out + 4, c.epoch);
    out[8] = c.features;
    out[9] = c.profile;
    out[10] = c.drive;
    out[11] = c.reason;
    out[12] = c.mode;
    out[13] = c.policy;
    return kSize;
}

const char* ResultName(Result r) noexcept {
    switch (r) {
    case Result::ok: return "ok";
    case Result::size: return "size";
    case Result::magic: return "magic";
    case Result::version: return "version";
    case Result::revision: return "revision";
    case Result::epoch: return "epoch";
    case Result::stale_epoch: return "stale_epoch";
    case Result::field: return "field";
    case Result::no_link: return "no_link";
    case Result::older: return "older";
    default: return "reserved";
    }
}

Result Decode(const std::uint8_t* data, std::size_t size, std::uint32_t current_epoch, Caps& out) noexcept {
    if (!data || size != kSize) return Result::size;
    if (data[0] != kMagic) return Result::magic;
    if (data[1] != kVersion) return Result::version;
    Caps c{};
    c.revision = Get16(data + 2);
    if (c.revision == 0) return Result::revision;
    c.epoch = Get32(data + 4);
    if (c.epoch == 0) return Result::epoch;
    if (current_epoch && c.epoch != current_epoch) return Result::stale_epoch;
    c.features = data[8];
    c.profile = data[9];
    c.drive = data[10];
    c.reason = data[11];
    c.mode = data[12];
    c.policy = data[13];
    if (!FieldsOk(c)) return Result::field;
    if (data[14] || data[15]) return Result::reserved;
    out = c;
    return Result::ok;
}

bool Accept(Store& store, const std::uint8_t* data, std::size_t size, std::uint32_t link_epoch) noexcept {
    Caps c{};
    // Never the sender's session number against ours (0.13.5: remote=unknown/none after
    // any relaunch of either copy): the decode is unconditional.
    const Result r = Decode(data, size, 0, c);
    if (r != Result::ok) { ++store.invalid; store.last = r; return false; }
    if (!link_epoch) { ++store.stale; store.last = Result::no_link; return false; }
    // An older revision of the same sender session arriving late in the same link session
    // never replaces a newer one (a new link session starts its own sequence).
    if (store.have && store.link_epoch == link_epoch && store.caps.epoch == c.epoch &&
        static_cast<std::uint16_t>(c.revision - store.caps.revision) >= 0x8000u) {
        ++store.stale;
        store.last = Result::older;
        return false;
    }
    store.have = true;
    store.caps = c;
    store.link_epoch = link_epoch;
    store.last = Result::ok;
    ++store.received;
    return true;
}

bool Due(Sender& s, Caps& next, std::uint32_t epoch, std::uint64_t now_ms) noexcept {
    if (!epoch) return false;
    const bool new_epoch = !s.sent_any || s.last.epoch != epoch;
    const bool changed = !s.sent_any || !SameContent(s.last, next);
    const std::uint64_t since = now_ms >= s.sent_ms ? now_ms - s.sent_ms : 0;
    if (!new_epoch && !(changed && since >= kMinGapMs) && since < kHeartbeatMs) return false;
    next.epoch = epoch;
    next.revision = static_cast<std::uint16_t>(s.revision == 0xFFFFu ? 1 : s.revision + 1);
    return true;
}

void Sent(Sender& s, const Caps& caps, std::uint64_t now_ms) noexcept {
    s.sent_any = true;
    s.last = caps;
    s.sent_ms = now_ms;
    s.revision = caps.revision;
    ++s.sent;
}

} // namespace mgscoop::partner_caps_wire
