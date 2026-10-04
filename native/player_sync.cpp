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
    return bounded_coord(t.x) && bounded_coord(t.y) && bounded_coord(t.z) &&
           bounded_rot(t.rot_y) && t.stance <= static_cast<std::uint8_t>(Stance::Crawl) &&
           (t.action_flags & 0xf0u) == 0;
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

} // namespace mgscoop::sync
