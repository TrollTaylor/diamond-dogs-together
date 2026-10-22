// Original MGS Co-op native puppet: gestures, pure part (partner_gesture_policy.hpp).
#include "mgscoop/partner_gesture_policy.hpp"

namespace mgscoop::partner_gesture_policy {
namespace {
void Put16(std::uint8_t* out, std::uint16_t v) noexcept { out[0] = static_cast<std::uint8_t>(v); out[1] = static_cast<std::uint8_t>(v >> 8); }
void Put32(std::uint8_t* out, std::uint32_t v) noexcept {
    for (unsigned i = 0; i < 4; ++i) out[i] = static_cast<std::uint8_t>(v >> (8 * i));
}
std::uint16_t Get16(const std::uint8_t* in) noexcept { return static_cast<std::uint16_t>(in[0] | (in[1] << 8)); }
std::uint32_t Get32(const std::uint8_t* in) noexcept {
    return static_cast<std::uint32_t>(in[0]) | (static_cast<std::uint32_t>(in[1]) << 8) | (static_cast<std::uint32_t>(in[2]) << 16) |
           (static_cast<std::uint32_t>(in[3]) << 24);
}
bool KindValid(Kind kind) noexcept { return kind != Kind::none && static_cast<std::uint8_t>(kind) <= kKindMax; }
bool SignLike(Kind kind) noexcept { return kind == Kind::sign_go || kind == Kind::sign_stop || kind == Kind::knock; }
} // namespace

Kind KindForUpper(std::uint8_t upper) noexcept {
    switch (upper) {
    case kUpperPickup: return Kind::pickup;
    case kUpperKnock: return Kind::knock;
    case kUpperSignGo: return Kind::sign_go;
    case kUpperSignStop: return Kind::sign_stop;
    default: return Kind::none;
    }
}

const char* KindName(Kind kind) noexcept {
    switch (kind) {
    case Kind::pickup: return "pickup";
    case Kind::sign_go: return "sign_go";
    case Kind::sign_stop: return "sign_stop";
    case Kind::knock: return "knock";
    default: return "none";
    }
}

const char* MethodName(Method method) noexcept {
    switch (method) {
    case Method::set_current: return "set";
    case Method::reserve_reset: return "reserve";
    default: return "none";
    }
}

Choice NodeFor(Kind kind, std::uint8_t stance, bool moving, bool hand) noexcept {
    Choice c{0, Method::none, 0.0f, nullptr};
    switch (kind) {
    case Kind::pickup:
        // FUN_14120E520, low item, flag 0: stand idle statePickUpStandRight,
        // stand moving statePickUpStandLeft, squat statePickUpSquatRight.
        c.method = Method::set_current;
        c.blend = kPickupBlend;
        if (stance == 1) c.node = moving ? 0x82E84B26E4CEULL : 0x1E904603EB7DULL;
        else if (stance == 2) c.node = 0xFF77B695370AULL;
        else { c.method = Method::none; c.blend = 0.0f; c.refusal = "stance"; }
        return c;
    case Kind::sign_go:
        // StateHandSignGo 0x11E6A40: stand / squat / crawl / other.
        c.method = Method::reserve_reset;
        c.blend = kSignBlend;
        c.node = stance == 1 ? 0xE74E11BABA4BULL : stance == 2 ? 0x8881431FFF42ULL : stance == 3 ? 0x5FE632112A9AULL : 0x2153458C564CULL;
        return c;
    case Kind::sign_stop:
        // StateHandSignStop 0x11E6C30.
        c.method = Method::reserve_reset;
        c.blend = kSignBlend;
        c.node = stance == 1 ? 0xB98C80A7DDAEULL : stance == 2 ? 0x78CB18630AF1ULL : stance == 3 ? 0x825F953B5437ULL : 0x724E8FE0F814ULL;
        return c;
    case Kind::knock:
        // StateKnock 0x11E7210 event 2 (flag 0 rows): the second value of each
        // pair is taken when Q+0x58[id] (the hand) is non-zero.
        c.method = Method::reserve_reset;
        c.blend = kSignBlend;
        if (stance == 1) c.node = hand ? 0x7B2D4F836A92ULL : 0xD64283446A67ULL;
        else if (stance == 2) c.node = hand ? 0xD08C8B213CABULL : 0x7BC06465D606ULL;
        else if (stance == 3) c.node = hand ? 0x0430147D9D34ULL : 0xC1143CAC83B1ULL;
        else c.node = hand ? 0x1EC34B25A344ULL : 0x174A0F341288ULL;
        return c;
    default:
        c.refusal = "kind";
        return c;
    }
}

bool Encode(const Packet& p, std::uint8_t (&out)[kSize]) noexcept {
    for (auto& b : out) b = 0;
    if (!KindValid(p.kind) || p.stance > 3 || p.speed > 3 || (p.flags & ~Flag::Known) || !p.seq) return false;
    out[0] = kMagic;
    out[1] = kVersion;
    out[2] = static_cast<std::uint8_t>(p.kind);
    out[3] = p.stance;
    out[4] = p.speed;
    out[5] = p.flags;
    Put16(out + 6, p.seq);
    Put32(out + 8, p.sender_ms);
    return true;
}

bool Decode(const std::uint8_t* data, std::size_t size, Packet& out) noexcept {
    if (!data || size != kSize || data[0] != kMagic || data[1] != kVersion) return false;
    Packet p{};
    p.kind = static_cast<Kind>(data[2]);
    p.stance = data[3];
    p.speed = data[4];
    p.flags = data[5];
    p.seq = Get16(data + 6);
    p.sender_ms = Get32(data + 8);
    if (!KindValid(p.kind) || p.stance > 3 || p.speed > 3 || (p.flags & ~Flag::Known) || !p.seq) return false;
    out = p;
    return true;
}

SenderOut Observe(Sender& s, const SenderIn& in) noexcept {
    SenderOut out{false, Packet{}};
    if (!in.enabled || !in.valid || !in.epoch) {
        s.ready = false;
        return out;
    }
    if (!s.ready || s.epoch != in.epoch) {
        s.ready = true;
        s.epoch = in.epoch;
        s.last_upper = in.upper;
        return out;
    }
    const std::uint8_t before = s.last_upper;
    s.last_upper = in.upper;
    const Kind kind = KindForUpper(in.upper);
    if (kind == Kind::none || in.upper == before) return out;
    if (!++s.seq) s.seq = 1;
    ++s.edges;
    out.send = true;
    out.packet.kind = kind;
    out.packet.stance = in.stance <= 3 ? in.stance : 0;
    out.packet.speed = in.speed <= 3 ? in.speed : 3;
    out.packet.flags = in.speed ? Flag::Moving : 0;
    out.packet.seq = s.seq;
    out.packet.sender_ms = static_cast<std::uint32_t>(in.now_ms);
    return out;
}

bool Newer(std::uint16_t seq, std::uint16_t prior) noexcept {
    const std::uint16_t d = static_cast<std::uint16_t>(seq - prior);
    return d != 0 && d < 0x8000u;
}

bool Accept(Store& store, const std::uint8_t* data, std::size_t size, std::uint32_t epoch, std::uint64_t now_ms) noexcept {
    Packet p{};
    if (!Decode(data, size, p)) { ++store.invalid; return false; }
    if (store.have && store.epoch == epoch && !Newer(p.seq, store.packet.seq)) { ++store.duplicates; return false; }
    store.have = true;
    store.packet = p;
    store.epoch = epoch;
    store.received_ms = now_ms;
    ++store.received;
    return true;
}

bool BodyReady(std::uint16_t act, std::uint8_t q28, std::uint8_t upper) noexcept {
    return act >= 1 && act <= 6 && q28 == 0 && upper == 0;
}

const char* Gate(const GateIn& in) noexcept {
    if (!in.enabled) return "key_off";
    if (in.uncertain) return "uncertain";
    if (!in.native) return "not_native";
    if (!in.realized) return "unrealized";
    if (in.block) return in.block;
    if (!in.remote_live) return "remote_stale";
    if (!in.installed) return "not_installed";
    if (in.busy) return "busy";
    if (!in.body_ok) return "body_unread";
    if (in.body_act == 0x4D) return "box";
    if (in.body_act < 1 || in.body_act > 6) return "body_act";
    if (in.body_q28) return "body_q28";
    if (in.body_upper) return "body_upper";
    return nullptr;
}

const char* ViaName(Via via) noexcept {
    switch (via) {
    case Via::end: return "end";
    case Via::timeout: return "timeout";
    case Via::override_: return "override";
    default: return "none";
    }
}

EndOut End(const EndIn& in) noexcept {
    // Identity first: a different body or controller is never written.
    if (!in.identity) return {Via::override_, "identity", false};
    if (!in.lease) return {Via::override_, "lease", in.ours};
    if (!in.ours) {
        if (in.elapsed_ms < kNodeGraceMs) return {Via::none, nullptr, false};
        return {Via::override_, "node", false};
    }
    if (in.clip_end && in.elapsed_ms >= kMinPlayMs) return {Via::end, "clip", true};
    if (SignLike(in.kind) && in.remote_known && in.remote_speed > 1) return {Via::end, "speed", true};
    if (in.elapsed_ms >= kEndDeadlineMs) return {Via::timeout, "deadline", true};
    return {Via::none, nullptr, false};
}

} // namespace mgscoop::partner_gesture_policy
