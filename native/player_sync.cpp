#include "mgscoop/player_sync.hpp"

#include <cstring>
#include <limits>

namespace mgscoop::sync {
namespace {

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "player_sync requires IEEE-754 binary32 floats");

bool bounded_coord(float value) noexcept {
    std::uint32_t bits = 0, limit_bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const float limit = max_coord_component;
    std::memcpy(&limit_bits, &limit, sizeof(limit_bits));
    // Reject NaN (exponent all 1s + non-zero mantissa) and values exceeding limit
    return (bits & 0x7fffffffu) <= limit_bits;
}

bool bounded_rot(float value) noexcept {
    std::uint32_t bits = 0, limit_bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const float limit = 6.28318530718f * 2.0f; // 4*pi
    std::memcpy(&limit_bits, &limit, sizeof(limit_bits));
    return (bits & 0x7fffffffu) <= limit_bits;
}

bool valid_transform(const PlayerTransformPacket& t) noexcept {
    constexpr unsigned known_actions = ActionFlag::Aiming | ActionFlag::Firing | ActionFlag::Sprinting |
                                       ActionFlag::Reloading | ActionFlag::Walking | ActionFlag::Running;
    return bounded_coord(t.x) && bounded_coord(t.y) && bounded_coord(t.z) &&
           bounded_rot(t.rot_y) && t.stance <= static_cast<std::uint8_t>(Stance::Crawl) &&
           (t.action_flags & ~known_actions) == 0;
}

void put(std::uint8_t* out, std::uint32_t value, unsigned width = 4) noexcept {
    for(unsigned i=0;i<width;++i) out[i]=static_cast<std::uint8_t>(value>>(i*8));
}
std::uint32_t get(const std::uint8_t* in, unsigned width = 4) noexcept {
    std::uint32_t value=0;
    for(unsigned i=0;i<width;++i) value|=static_cast<std::uint32_t>(in[i])<<(i*8);
    return value;
}
void put_float(std::uint8_t* out, float value) noexcept {
    std::uint32_t bits=0; std::memcpy(&bits,&value,sizeof(bits)); put(out,bits);
}
float get_float(const std::uint8_t* in) noexcept {
    const auto bits=get(in); float value=0; std::memcpy(&value,&bits,sizeof(value)); return value;
}

} // namespace

std::optional<wire::Packet> encode_transform(std::uint64_t session,
                                            std::uint32_t epoch,
                                            const PlayerTransformPacket& transform) {
    if (!valid_transform(transform) || session == 0 || epoch == 0) return std::nullopt;

    wire::Packet packet;
    packet.kind = wire::Kind::snapshot;
    packet.session = session;
    packet.epoch = epoch;
    packet.sequence = transform.sequence;
    packet.payload.resize(transform_packet_size);
    auto* bytes=packet.payload.data();
    put(bytes,transform.sequence);
    put_float(bytes+4,transform.x); put_float(bytes+8,transform.y); put_float(bytes+12,transform.z);
    put_float(bytes+16,transform.rot_y); put(bytes+20,transform.equip_id,2);
    bytes[22]=transform.stance; bytes[23]=transform.action_flags; put(bytes+24,transform.tick_ms);

    return packet;
}

std::optional<PlayerTransformPacket> decode_transform(const wire::Packet& packet) {
    if (packet.kind != wire::Kind::snapshot || packet.session == 0 || packet.epoch == 0 ||
        packet.payload.size() != transform_packet_size) {
        return std::nullopt;
    }

    PlayerTransformPacket transform{};
    const auto* bytes=packet.payload.data();
    transform.sequence=get(bytes);
    transform.x=get_float(bytes+4); transform.y=get_float(bytes+8); transform.z=get_float(bytes+12);
    transform.rot_y=get_float(bytes+16); transform.equip_id=static_cast<std::uint16_t>(get(bytes+20,2));
    transform.stance=bytes[22]; transform.action_flags=bytes[23]; transform.tick_ms=get(bytes+24);

    if (!valid_transform(transform) || transform.sequence != packet.sequence) return std::nullopt;

    return transform;
}

namespace {
void put64(std::uint8_t* out, std::uint64_t value) noexcept {
    for (unsigned i = 0; i < 8; ++i) out[i] = static_cast<std::uint8_t>(value >> (i * 8));
}
std::uint64_t get64(const std::uint8_t* in) noexcept {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(in[i]) << (i * 8);
    return value;
}
// The wire::encode header layout (protocol.cpp), written into a fixed buffer.
void put_header(std::uint8_t* out, wire::Kind kind, std::uint64_t session, std::uint32_t epoch,
                std::uint32_t sequence, std::uint32_t length) noexcept {
    out[0] = 'M'; out[1] = 'G'; out[2] = 'S'; out[3] = 'C';
    put(out + 4, wire::version, 2);
    put(out + 6, static_cast<std::uint16_t>(kind), 2);
    put64(out + 8, session);
    put(out + 16, epoch);
    put(out + 20, sequence);
    put(out + 24, length);
}
bool header_matches(const std::uint8_t* data, wire::Kind kind, std::uint32_t length) noexcept {
    return data[0] == 'M' && data[1] == 'G' && data[2] == 'S' && data[3] == 'C' &&
           get(data + 4, 2) == wire::version && get(data + 6, 2) == static_cast<std::uint16_t>(kind) &&
           get(data + 24) == length;
}
} // namespace

std::size_t encode_transform_frame(std::uint64_t session, std::uint32_t epoch, const PlayerTransformPacket& transform,
                                   std::uint8_t (&out)[transform_frame_size]) noexcept {
    std::memset(out, 0, sizeof(out));
    if (!valid_transform(transform) || session == 0 || epoch == 0) return 0;
    put_header(out, wire::Kind::snapshot, session, epoch, transform.sequence, transform_packet_size);
    auto* bytes = out + wire::header_size;
    put(bytes, transform.sequence);
    put_float(bytes + 4, transform.x); put_float(bytes + 8, transform.y); put_float(bytes + 12, transform.z);
    put_float(bytes + 16, transform.rot_y); put(bytes + 20, transform.equip_id, 2);
    bytes[22] = transform.stance; bytes[23] = transform.action_flags; put(bytes + 24, transform.tick_ms);
    return transform_frame_size;
}

bool decode_transform_frame(const std::uint8_t* data, std::size_t size, std::uint64_t& session, std::uint32_t& epoch,
                            PlayerTransformPacket& out) noexcept {
    if (!data || size != transform_frame_size || !header_matches(data, wire::Kind::snapshot, transform_packet_size))
        return false;
    const std::uint64_t frame_session = get64(data + 8);
    const std::uint32_t frame_epoch = get(data + 16);
    const std::uint32_t frame_sequence = get(data + 20);
    if (frame_session == 0 || frame_epoch == 0) return false;
    const auto* bytes = data + wire::header_size;
    PlayerTransformPacket transform{};
    transform.sequence = get(bytes);
    transform.x = get_float(bytes + 4); transform.y = get_float(bytes + 8); transform.z = get_float(bytes + 12);
    transform.rot_y = get_float(bytes + 16); transform.equip_id = static_cast<std::uint16_t>(get(bytes + 20, 2));
    transform.stance = bytes[22]; transform.action_flags = bytes[23]; transform.tick_ms = get(bytes + 24);
    if (!valid_transform(transform) || transform.sequence != frame_sequence) return false;
    session = frame_session;
    epoch = frame_epoch;
    out = transform;
    return true;
}

std::size_t encode_keepalive_frame(std::uint64_t session, std::uint32_t epoch,
                                   std::uint8_t (&out)[keepalive_frame_size]) noexcept {
    std::memset(out, 0, sizeof(out));
    if (session == 0 || epoch == 0) return 0;
    put_header(out, wire::Kind::event, session, epoch, 0, 0);
    return keepalive_frame_size;
}

bool decode_keepalive_frame(const std::uint8_t* data, std::size_t size, std::uint64_t& session) noexcept {
    if (!data || size != keepalive_frame_size || !header_matches(data, wire::Kind::event, 0)) return false;
    const std::uint64_t frame_session = get64(data + 8);
    if (frame_session == 0 || get(data + 16) == 0 || get(data + 20) != 0) return false;
    session = frame_session;
    return true;
}

bool valid_anim(const AnimState& anim) noexcept {
    if ((anim.flags & ~AnimFlag::Valid) != 0 || anim.speed > 3) return false;
    // A valid extension carries a real stance (1 stand, 2 squat, 3 crawl); an
    // invalid one carries no stance or speed at all.
    if (anim.flags & AnimFlag::Valid) return anim.stance >= 1 && anim.stance <= 3;
    return anim.stance == 0 && anim.speed == 0;
}

std::size_t encode_transform_anim_frame(std::uint64_t session, std::uint32_t epoch, const PlayerTransformPacket& transform,
                                        const AnimState& anim, std::uint8_t (&out)[transform_anim_frame_size]) noexcept {
    std::memset(out, 0, sizeof(out));
    if (!valid_transform(transform) || !valid_anim(anim) || session == 0 || epoch == 0) return 0;
    put_header(out, wire::Kind::snapshot, session, epoch, transform.sequence,
               static_cast<std::uint32_t>(transform_packet_size + anim_extension_size));
    auto* bytes = out + wire::header_size;
    put(bytes, transform.sequence);
    put_float(bytes + 4, transform.x); put_float(bytes + 8, transform.y); put_float(bytes + 12, transform.z);
    put_float(bytes + 16, transform.rot_y); put(bytes + 20, transform.equip_id, 2);
    bytes[22] = transform.stance; bytes[23] = transform.action_flags; put(bytes + 24, transform.tick_ms);
    auto* ext = bytes + transform_packet_size;
    ext[0] = anim_extension_magic; ext[1] = anim_extension_version; put(ext + 2, anim.act, 2);
    ext[4] = anim.stance; ext[5] = anim.speed; ext[6] = anim.flags; ext[7] = 0;
    return transform_anim_frame_size;
}

bool decode_partner_transform_frame(const std::uint8_t* data, std::size_t size, std::uint64_t& session, std::uint32_t& epoch,
                                    PlayerTransformPacket& out, AnimState& anim, bool& has_anim) noexcept {
    if (!data) return false;
    if (size == transform_frame_size) {
        PlayerTransformPacket transform{};
        if (!decode_transform_frame(data, size, session, epoch, transform)) return false;
        out = transform;
        anim = AnimState{};
        has_anim = false;
        return true;
    }
    if (size != transform_anim_frame_size ||
        !header_matches(data, wire::Kind::snapshot, static_cast<std::uint32_t>(transform_packet_size + anim_extension_size)))
        return false;
    const std::uint64_t frame_session = get64(data + 8);
    const std::uint32_t frame_epoch = get(data + 16);
    const std::uint32_t frame_sequence = get(data + 20);
    if (frame_session == 0 || frame_epoch == 0) return false;
    const auto* bytes = data + wire::header_size;
    PlayerTransformPacket transform{};
    transform.sequence = get(bytes);
    transform.x = get_float(bytes + 4); transform.y = get_float(bytes + 8); transform.z = get_float(bytes + 12);
    transform.rot_y = get_float(bytes + 16); transform.equip_id = static_cast<std::uint16_t>(get(bytes + 20, 2));
    transform.stance = bytes[22]; transform.action_flags = bytes[23]; transform.tick_ms = get(bytes + 24);
    if (!valid_transform(transform) || transform.sequence != frame_sequence) return false;
    const auto* ext = bytes + transform_packet_size;
    if (ext[0] != anim_extension_magic || ext[1] != anim_extension_version || ext[7] != 0) return false;
    AnimState decoded{};
    decoded.act = static_cast<std::uint16_t>(get(ext + 2, 2));
    decoded.stance = ext[4]; decoded.speed = ext[5]; decoded.flags = ext[6];
    if (!valid_anim(decoded)) return false;
    session = frame_session;
    epoch = frame_epoch;
    out = transform;
    anim = decoded;
    has_anim = true;
    return true;
}

std::size_t encode_look_frame(std::uint64_t session, std::uint32_t epoch, const LookPacket& look,
                              std::uint8_t (&out)[look_frame_size]) noexcept {
    std::memset(out, 0, sizeof(out));
    if (session == 0 || epoch == 0 || look.revision == 0 || (look.flags & ~LookFlag::PartsLoaded) != 0) return 0;
    put_header(out, wire::Kind::event, session, epoch, look.revision, static_cast<std::uint32_t>(look_packet_size));
    auto* bytes = out + wire::header_size;
    bytes[0] = look_magic; bytes[1] = look_version;
    bytes[2] = look.type; bytes[3] = look.parts; bytes[4] = look.camo; bytes[5] = look.hand;
    put(bytes + 6, look.face, 2);
    bytes[8] = look.face_equip; bytes[9] = look.flags;
    put(bytes + 10, look.revision, 2);
    return look_frame_size;
}

bool decode_look_frame(const std::uint8_t* data, std::size_t size, std::uint64_t& session, std::uint32_t& epoch,
                       LookPacket& out) noexcept {
    if (!data || size != look_frame_size || !header_matches(data, wire::Kind::event, static_cast<std::uint32_t>(look_packet_size)))
        return false;
    const std::uint64_t frame_session = get64(data + 8);
    const std::uint32_t frame_epoch = get(data + 16);
    const std::uint32_t frame_sequence = get(data + 20);
    if (frame_session == 0 || frame_epoch == 0) return false;
    const auto* bytes = data + wire::header_size;
    if (bytes[0] != look_magic || bytes[1] != look_version) return false;
    LookPacket look{};
    look.type = bytes[2]; look.parts = bytes[3]; look.camo = bytes[4]; look.hand = bytes[5];
    look.face = static_cast<std::uint16_t>(get(bytes + 6, 2));
    look.face_equip = bytes[8]; look.flags = bytes[9];
    look.revision = static_cast<std::uint16_t>(get(bytes + 10, 2));
    if (look.revision == 0 || frame_sequence != look.revision || (look.flags & ~LookFlag::PartsLoaded) != 0) return false;
    session = frame_session;
    epoch = frame_epoch;
    out = look;
    return true;
}

std::size_t encode_input_frame(std::uint64_t session, std::uint32_t epoch, std::uint32_t sequence,
                               const partner_input::InputPacket& packet, std::uint8_t (&out)[input_frame_size]) noexcept {
    std::memset(out, 0, sizeof(out));
    if (session == 0 || epoch == 0 || sequence == 0 || packet.version != input_version || !partner_input::ValidPacket(packet))
        return 0;
    put_header(out, wire::Kind::event, session, epoch, sequence, static_cast<std::uint32_t>(input_packet_size));
    auto* b = out + wire::header_size;
    b[0] = input_magic; b[1] = input_version; b[2] = packet.flags; b[3] = packet.edge_seq;
    put(b + 4, packet.held); put(b + 8, packet.w4c); put(b + 12, packet.w50); put(b + 16, packet.w54);
    put(b + 20, static_cast<std::uint16_t>(packet.lx), 2); put(b + 22, static_cast<std::uint16_t>(packet.ly), 2);
    put(b + 24, static_cast<std::uint16_t>(packet.rx), 2); put(b + 26, static_cast<std::uint16_t>(packet.ry), 2);
    b[28] = packet.lt; b[29] = packet.rt;
    put(b + 30, packet.lmag, 2); put(b + 32, packet.lforce, 2);
    put(b + 34, static_cast<std::uint16_t>(packet.ldir), 2); put(b + 36, static_cast<std::uint16_t>(packet.cam_cdeg), 2);
    b[38] = packet.edge_rep; b[39] = 0;
    put(b + 40, packet.long12); put(b + 44, packet.tap12);
    return input_frame_size;
}

std::size_t encode_input_frame_any(std::uint64_t session, std::uint32_t epoch, std::uint32_t sequence,
                                   const partner_input::InputPacket& packet, std::uint8_t (&out)[input_frame_size_max]) noexcept {
    std::memset(out, 0, sizeof(out));
    if (packet.version == input_version) {
        std::uint8_t v2[input_frame_size]{};
        const std::size_t n = encode_input_frame(session, epoch, sequence, packet, v2);
        std::memcpy(out, v2, n);
        return n;
    }
    const bool v7 = packet.version == input_version_v7;
    const bool v6 = packet.version == input_version_v6 || v7;
    const bool v5 = packet.version == input_version_v5 || v6;
    const bool v4 = packet.version == input_version_v4 || v5;
    if ((packet.version != input_version_v3 && !v4) || session == 0 || epoch == 0 || sequence == 0 ||
        !partner_input::ValidPacket(packet))
        return 0;
    partner_input::InputPacket as2 = packet;
    as2.version = input_version;
    as2.look_pitch = as2.look_yaw = as2.cam270 = as2.cam274 = 0.0f;
    as2.heading_mrad = 0;
    as2.xflags = as2.inv_slot = 0;
    as2.equip_rev = 0;
    as2.rate_dash = as2.rate_crawl = as2.rate_crawl_factor = 0;
    as2.mag = as2.mag_cap = 0;
    as2.gun_equip = 0;
    as2.gun_flags = as2.gun_mode = as2.reload_seq = as2.switch_seq = 0;
    as2.aim_pitch_cdeg = as2.aim_yaw_cdeg = 0;
    as2.place_bits = 0;
    std::uint8_t v2[input_frame_size]{};
    if (!encode_input_frame(session, epoch, sequence, as2, v2)) return 0;
    std::memcpy(out, v2, sizeof(v2));
    put_header(out, wire::Kind::event, session, epoch, sequence,
               static_cast<std::uint32_t>(v7   ? input_packet_size_v7
                                          : v6 ? input_packet_size_v6
                                          : v5 ? input_packet_size_v5
                                          : v4 ? input_packet_size_v4
                                               : input_packet_size_v3));
    auto* b = out + wire::header_size;
    b[1] = v7 ? input_version_v7 : v6 ? input_version_v6 : v5 ? input_version_v5 : v4 ? input_version_v4 : input_version_v3;
    std::uint32_t f = 0;
    std::memcpy(&f, &packet.look_pitch, 4); put(b + 48, f);
    std::memcpy(&f, &packet.look_yaw, 4); put(b + 52, f);
    std::memcpy(&f, &packet.cam270, 4); put(b + 56, f);
    std::memcpy(&f, &packet.cam274, 4); put(b + 60, f);
    put(b + 64, static_cast<std::uint16_t>(packet.heading_mrad), 2);
    b[66] = packet.xflags;
    b[67] = packet.inv_slot;
    put(b + 68, packet.equip_rev, 2);
    b[70] = b[71] = 0;
    if (!v4) return input_frame_size_v3;
    put(b + 72, packet.rate_dash, 2);
    put(b + 74, packet.rate_crawl, 2);
    put(b + 76, packet.rate_crawl_factor, 2);
    put(b + 78, packet.mag, 2);
    put(b + 80, packet.mag_cap, 2);
    put(b + 82, packet.gun_equip, 2);
    if (!v5) return input_frame_size_v4;
    b[84] = packet.gun_flags;
    b[85] = packet.gun_mode;
    b[86] = packet.reload_seq;
    b[87] = packet.switch_seq;
    if (!v6) return input_frame_size_v5;
    put(b + 88, static_cast<std::uint16_t>(packet.aim_pitch_cdeg), 2);
    put(b + 90, static_cast<std::uint16_t>(packet.aim_yaw_cdeg), 2);
    if (!v7) return input_frame_size_v6;
    b[92] = packet.place_bits;
    b[93] = b[94] = b[95] = 0;
    return input_frame_size_v7;
}

std::size_t encode_input_frame_any(std::uint64_t session, std::uint32_t epoch, std::uint32_t sequence,
                                   const partner_input::InputPacket& packet, std::uint8_t (&out)[input_frame_size_v5]) noexcept {
    std::memset(out, 0, sizeof(out));
    if (packet.version > input_version_v5) return 0;
    std::uint8_t full[input_frame_size_max]{};
    const std::size_t n = encode_input_frame_any(session, epoch, sequence, packet, full);
    if (n && n <= sizeof(out)) std::memcpy(out, full, n);
    return n;
}

bool decode_input_frame(const std::uint8_t* data, std::size_t size, std::uint64_t& session, std::uint32_t& epoch,
                        std::uint32_t& sequence, partner_input::InputPacket& out) noexcept {
    if (!data) return false;
    const bool v7 = size == input_frame_size_v7;
    const bool v6 = size == input_frame_size_v6 || v7;
    const bool v5 = size == input_frame_size_v5 || v6;
    const bool v4 = size == input_frame_size_v4 || v5;
    const bool v3 = size == input_frame_size_v3 || v4;
    const bool v2 = size == input_frame_size || v3;
    if (!v2 && size != input_frame_size_v1) return false;
    const std::size_t payload = v7   ? input_packet_size_v7
                                : v6 ? input_packet_size_v6
                                : v5 ? input_packet_size_v5
                                : v4 ? input_packet_size_v4
                                : v3 ? input_packet_size_v3
                                : v2 ? input_packet_size
                                     : input_packet_size_v1;
    if (!header_matches(data, wire::Kind::event, static_cast<std::uint32_t>(payload))) return false;
    const std::uint64_t frame_session = get64(data + 8);
    const std::uint32_t frame_epoch = get(data + 16);
    const std::uint32_t frame_sequence = get(data + 20);
    if (frame_session == 0 || frame_epoch == 0 || frame_sequence == 0) return false;
    const auto* b = data + wire::header_size;
    if (b[0] != input_magic ||
        b[1] != (v7 ? input_version_v7 : v6 ? input_version_v6 : v5 ? input_version_v5 : v4 ? input_version_v4 : v3 ? input_version_v3 : v2 ? input_version : 1) ||
        b[39] != 0)
        return false;
    if (!v2 && (b[3] != 0 || b[38] != 0)) return false;
    partner_input::InputPacket p{};
    p.version = v7 ? 7 : v6 ? 6 : v5 ? 5 : v4 ? 4 : v3 ? 3 : v2 ? 2 : 1;
    if (v7) {
        if (b[93] != 0 || b[94] != 0 || b[95] != 0) return false;
        p.place_bits = b[92];
    }
    if (v6) {
        p.aim_pitch_cdeg = static_cast<std::int16_t>(get(b + 88, 2));
        p.aim_yaw_cdeg = static_cast<std::int16_t>(get(b + 90, 2));
    }
    if (v5) {
        p.gun_flags = b[84];
        p.gun_mode = b[85];
        p.reload_seq = b[86];
        p.switch_seq = b[87];
    }
    if (v4) {
        p.gun_equip = static_cast<std::uint16_t>(get(b + 82, 2));
        p.rate_dash = static_cast<std::uint16_t>(get(b + 72, 2));
        p.rate_crawl = static_cast<std::uint16_t>(get(b + 74, 2));
        p.rate_crawl_factor = static_cast<std::uint16_t>(get(b + 76, 2));
        p.mag = static_cast<std::uint16_t>(get(b + 78, 2));
        p.mag_cap = static_cast<std::uint16_t>(get(b + 80, 2));
    }
    p.flags = b[2];
    if (v3) {
        if (b[70] != 0 || b[71] != 0) return false;
        std::uint32_t f = get(b + 48); std::memcpy(&p.look_pitch, &f, 4);
        f = get(b + 52); std::memcpy(&p.look_yaw, &f, 4);
        f = get(b + 56); std::memcpy(&p.cam270, &f, 4);
        f = get(b + 60); std::memcpy(&p.cam274, &f, 4);
        p.heading_mrad = static_cast<std::int16_t>(get(b + 64, 2));
        p.xflags = b[66];
        p.inv_slot = b[67];
        p.equip_rev = static_cast<std::uint16_t>(get(b + 68, 2));
    }
    if (v2) {
        p.edge_seq = b[3];
        p.edge_rep = b[38];
        p.long12 = get(b + 40);
        p.tap12 = get(b + 44);
    }
    p.held = get(b + 4); p.w4c = get(b + 8); p.w50 = get(b + 12); p.w54 = get(b + 16);
    p.lx = static_cast<std::int16_t>(get(b + 20, 2)); p.ly = static_cast<std::int16_t>(get(b + 22, 2));
    p.rx = static_cast<std::int16_t>(get(b + 24, 2)); p.ry = static_cast<std::int16_t>(get(b + 26, 2));
    p.lt = b[28]; p.rt = b[29];
    p.lmag = static_cast<std::uint16_t>(get(b + 30, 2)); p.lforce = static_cast<std::uint16_t>(get(b + 32, 2));
    p.ldir = static_cast<std::int16_t>(get(b + 34, 2)); p.cam_cdeg = static_cast<std::int16_t>(get(b + 36, 2));
    if (!partner_input::ValidPacket(p)) return false;
    session = frame_session;
    epoch = frame_epoch;
    sequence = frame_sequence;
    out = p;
    return true;
}

} // namespace mgscoop::sync
