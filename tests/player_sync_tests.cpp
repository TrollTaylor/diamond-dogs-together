#include "mgscoop/player_sync.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {
unsigned checks = 0;
void Check(bool ok, const char* message) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
}

int main() {
    using namespace mgscoop;
    using namespace mgscoop::sync;

    // 1. Basic round-trip test
    PlayerTransformPacket p1{};
    p1.sequence = 101;
    p1.x = 1250.5f;
    p1.y = 45.2f;
    p1.z = -320.8f;
    p1.rot_y = 1.57f;
    p1.equip_id = 42;
    p1.stance = static_cast<std::uint8_t>(Stance::Crouch);
    p1.action_flags = ActionFlag::Aiming | ActionFlag::Firing;
    p1.tick_ms = 998877;

    auto encoded = encode_transform(0x123456789ABCDEF0ULL, 1, p1);
    Check(encoded.has_value(), "encode valid transform");
    Check(encoded->kind == wire::Kind::snapshot, "packet kind is snapshot");
    Check(encoded->sequence == 101, "packet sequence matches");
    Check(encoded->payload.size() == sizeof(PlayerTransformPacket), "payload size exactly 28 bytes");

    auto decoded = decode_transform(*encoded);
    Check(decoded.has_value(), "decode valid transform");
    Check(decoded->sequence == p1.sequence, "sequence preserved");
    Check(decoded->x == p1.x && decoded->y == p1.y && decoded->z == p1.z, "position preserved");
    Check(decoded->rot_y == p1.rot_y, "rot_y preserved");
    Check(decoded->equip_id == p1.equip_id, "equip_id preserved");
    Check(decoded->stance == p1.stance, "stance preserved");
    Check(decoded->action_flags == p1.action_flags, "action flags preserved");
    Check(decoded->tick_ms == p1.tick_ms, "tick_ms preserved");

    // 2. Rejection of zero session ID
    Check(!encode_transform(0, 1, p1).has_value(), "reject zero session ID");

    // 3. Rejection of NaN / Infinity
    PlayerTransformPacket nan_p = p1;
    nan_p.x = std::numeric_limits<float>::quiet_NaN();
    Check(!encode_transform(0x1234, 1, nan_p).has_value(), "reject NaN x");

    PlayerTransformPacket inf_p = p1;
    inf_p.y = std::numeric_limits<float>::infinity();
    Check(!encode_transform(0x1234, 1, inf_p).has_value(), "reject Infinity y");

    // 4. Rejection of out-of-bounds coords
    PlayerTransformPacket huge_p = p1;
    huge_p.z = 2000000.0f; // exceeds max_coord_component (1,000,000)
    Check(!encode_transform(0x1234, 1, huge_p).has_value(), "reject out of bounds coord");

    // 5. Rejection of invalid packet kind on decode
    wire::Packet bad_kind = *encoded;
    bad_kind.kind = wire::Kind::input;
    Check(!decode_transform(bad_kind).has_value(), "reject non-snapshot packet kind");

    // 6. Rejection of truncated or oversized payload
    wire::Packet truncated = *encoded;
    truncated.payload.pop_back();
    Check(!decode_transform(truncated).has_value(), "reject truncated payload");

    wire::Packet oversized = *encoded;
    oversized.payload.push_back(0);
    Check(!decode_transform(oversized).has_value(), "reject oversized payload");

    // 7. Wire serialization format check
    auto wire_bytes = wire::encode(*encoded);
    Check(!wire_bytes.empty(), "wire encode succeeds");
    auto wire_decoded = wire::decode(wire_bytes.data(), wire_bytes.size());
    Check(wire_decoded.has_value(), "wire decode succeeds");
    auto final_transform = decode_transform(*wire_decoded);
    Check(final_transform.has_value(), "end-to-end wire transform decode succeeds");
    Check(final_transform->sequence == 101, "end-to-end sequence preserved");

    // 8. Fixed-buffer frame codec: byte-identical to wire::encode(encode_transform()).
    {
        const std::uint64_t session = 0x4D4753434F4F5001ULL;
        const PlayerTransformPacket samples[] = {
            p1,
            PlayerTransformPacket{0, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
            PlayerTransformPacket{0xFFFFFFFFu, -999999.0f, 12.5f, 999999.0f, -3.14159265f, 0xFFFF, 2, ActionFlag::Sprinting, 0xFFFFFFFFu},
            PlayerTransformPacket{7, 1.0e-3f, -2.5f, 3.0f, 12.566f, 1, 0, 0x0F, 123},
        };
        for (const auto& sample : samples) {
            for (std::uint32_t epoch : {1u, 30050u, 0xFFFFFFFFu}) {
                std::uint8_t frame[transform_frame_size];
                const auto size = encode_transform_frame(session, epoch, sample, frame);
                Check(size == transform_frame_size, "frame encode succeeds");
                const auto expected = wire::encode(*encode_transform(session, epoch, sample));
                Check(expected.size() == transform_frame_size &&
                      std::memcmp(expected.data(), frame, transform_frame_size) == 0, "frame bytes equal wire::encode");
                const auto via_wire = wire::decode(frame, size);
                Check(via_wire.has_value() && via_wire->session == session && via_wire->epoch == epoch &&
                      via_wire->sequence == sample.sequence, "wire::decode accepts the fixed frame");
                std::uint64_t got_session = 0;
                std::uint32_t got_epoch = 0;
                PlayerTransformPacket got{};
                Check(decode_transform_frame(frame, size, got_session, got_epoch, got), "frame decode succeeds");
                Check(got_session == session && got_epoch == epoch && std::memcmp(&got, &sample, sizeof(got)) == 0,
                      "frame decode round trip");
                const auto reference = decode_transform(*via_wire);
                Check(reference.has_value() && std::memcmp(&*reference, &got, sizeof(got)) == 0, "frame decode equals decode_transform");
            }
        }
        // Refusals mirror encode_transform; the output is zeroed.
        std::uint8_t frame[transform_frame_size];
        std::memset(frame, 0xAB, sizeof(frame));
        Check(encode_transform_frame(0, 1, p1, frame) == 0 && frame[0] == 0 && frame[55] == 0, "zero session refused, zeroed");
        Check(encode_transform_frame(session, 0, p1, frame) == 0, "zero epoch refused");
        Check(encode_transform_frame(session, 1, nan_p, frame) == 0, "NaN refused");
        PlayerTransformPacket bad_stance = p1; bad_stance.stance = 3;
        Check(encode_transform_frame(session, 1, bad_stance, frame) == 0 && !encode_transform(session, 1, bad_stance), "stance refused");
        PlayerTransformPacket bad_rot = p1; bad_rot.rot_y = 13.0f;
        Check(encode_transform_frame(session, 1, bad_rot, frame) == 0, "rotation bound refused");
        // Strict decode.
        Check(encode_transform_frame(session, 5, p1, frame) == transform_frame_size, "re-encode");
        std::uint64_t s = 0; std::uint32_t e = 0; PlayerTransformPacket out{};
        Check(!decode_transform_frame(nullptr, transform_frame_size, s, e, out), "null refused");
        Check(!decode_transform_frame(frame, transform_frame_size - 1, s, e, out), "short refused");
        std::uint8_t longer[transform_frame_size + 1]{};
        std::memcpy(longer, frame, transform_frame_size);
        Check(!decode_transform_frame(longer, sizeof(longer), s, e, out), "long refused");
        const unsigned corrupt_at[] = {0, 3, 4, 6, 24, 20, 28};  // magic, version, kind, length, header seq, payload seq
        for (unsigned at : corrupt_at) {
            std::uint8_t copy[transform_frame_size];
            std::memcpy(copy, frame, sizeof(copy));
            copy[at] ^= 0x01;
            Check(!decode_transform_frame(copy, sizeof(copy), s, e, out), "corrupt header field refused");
        }
        std::uint8_t zero_session[transform_frame_size];
        std::memcpy(zero_session, frame, sizeof(zero_session));
        std::memset(zero_session + 8, 0, 8);
        Check(!decode_transform_frame(zero_session, sizeof(zero_session), s, e, out), "zero session refused on decode");
        std::uint8_t bad_payload[transform_frame_size];
        std::memcpy(bad_payload, frame, sizeof(bad_payload));
        bad_payload[28 + 22] = 9;  // stance
        Check(!decode_transform_frame(bad_payload, sizeof(bad_payload), s, e, out), "invalid payload refused");
        Check(s == 0 && e == 0 && out.sequence == 0, "outputs untouched on refusal");
        // Keepalive frames: header-only, kind event, never a transform.
        std::uint8_t keepalive[keepalive_frame_size];
        Check(encode_keepalive_frame(session, 30050, keepalive) == keepalive_frame_size, "keepalive encode");
        wire::Packet ka{};
        ka.kind = wire::Kind::event; ka.session = session; ka.epoch = 30050; ka.sequence = 0;
        const auto ka_bytes = wire::encode(ka);
        Check(ka_bytes.size() == keepalive_frame_size && std::memcmp(ka_bytes.data(), keepalive, keepalive_frame_size) == 0,
              "keepalive equals wire::encode");
        std::uint64_t ka_session = 0;
        Check(decode_keepalive_frame(keepalive, sizeof(keepalive), ka_session) && ka_session == session, "keepalive decode");
        Check(!decode_transform_frame(keepalive, sizeof(keepalive), s, e, out), "keepalive is not a transform");
        Check(!decode_keepalive_frame(frame, transform_frame_size, ka_session), "transform is not a keepalive");
        Check(encode_keepalive_frame(0, 1, keepalive) == 0 && encode_keepalive_frame(session, 0, keepalive) == 0, "keepalive refusals");
    }

    // 9. Partner wire v2: the 8-byte animation extension (64-byte frames).
    {
        const std::uint64_t session = 0x4D4753434F4F5001ULL;
        AnimState anim{};
        anim.act = 0x0006; anim.stance = 3; anim.speed = 2; anim.flags = AnimFlag::Valid;
        Check(valid_anim(anim) && valid_anim(AnimState{}), "valid anim states");
        std::uint8_t frame[transform_anim_frame_size];
        Check(transform_anim_frame_size == 64 && anim_extension_size == 8, "v2 frame is 64 bytes");
        Check(encode_transform_anim_frame(session, 30050, p1, anim, frame) == transform_anim_frame_size, "v2 encode");
        // The first 56 bytes are a transform frame except the payload length (36).
        std::uint8_t v1[transform_frame_size];
        Check(encode_transform_frame(session, 30050, p1, v1) == transform_frame_size, "v1 reference");
        Check(std::memcmp(frame, v1, 24) == 0 && frame[24] == 36 && frame[25] == 0 &&
              std::memcmp(frame + 28, v1 + 28, 28) == 0, "v2 = v1 header (length 36) + same transform");
        Check(frame[56] == 0x41 && frame[57] == 1 && frame[58] == 6 && frame[59] == 0 && frame[60] == 3 && frame[61] == 2 &&
              frame[62] == 1 && frame[63] == 0, "extension bytes");
        // wire::decode sees a structurally valid snapshot with a 36-byte payload.
        const auto via_wire = wire::decode(frame, sizeof(frame));
        Check(via_wire.has_value() && via_wire->payload.size() == 36 && !decode_transform(*via_wire).has_value(),
              "wire::decode accepts v2; the v1 payload decoder refuses it (old receivers drop it)");
        std::uint64_t s = 0; std::uint32_t e = 0; PlayerTransformPacket got{}; AnimState got_anim{}; bool has = false;
        Check(!decode_transform_frame(frame, sizeof(frame), s, e, got), "v1 decoder refuses a v2 frame");
        Check(decode_partner_transform_frame(frame, sizeof(frame), s, e, got, got_anim, has) && has && s == session && e == 30050 &&
              std::memcmp(&got, &p1, sizeof(got)) == 0 && got_anim.act == 6 && got_anim.stance == 3 && got_anim.speed == 2 &&
              got_anim.flags == AnimFlag::Valid, "v2 decode round trip");
        // v1 frames still decode (no anim).
        got_anim.act = 77; has = true;
        Check(decode_partner_transform_frame(v1, sizeof(v1), s, e, got, got_anim, has) && !has && got_anim.act == 0 && got.sequence == p1.sequence,
              "v1 frame through the v2 decoder: has_anim false, anim zeroed");
        // Refusals.
        AnimState bad = anim; bad.stance = 4;
        Check(!valid_anim(bad) && encode_transform_anim_frame(session, 1, p1, bad, frame) == 0 && frame[0] == 0, "stance 4 refused, zeroed");
        bad = anim; bad.speed = 9;
        Check(encode_transform_anim_frame(session, 1, p1, bad, frame) == 0, "speed 9 refused");
        bad = anim; bad.flags = 0x80;
        Check(encode_transform_anim_frame(session, 1, p1, bad, frame) == 0, "unknown flag refused");
        Check(encode_transform_anim_frame(0, 1, p1, anim, frame) == 0 && encode_transform_anim_frame(session, 0, p1, anim, frame) == 0 &&
              encode_transform_anim_frame(session, 1, nan_p, anim, frame) == 0, "v2 transform/session/epoch refusals");
        Check(encode_transform_anim_frame(session, 30050, p1, anim, frame) == transform_anim_frame_size, "re-encode");
        const unsigned corrupt_at[] = {24, 56, 57, 60, 61, 62, 63, 28 + 22};
        for (unsigned at : corrupt_at) {
            std::uint8_t copy[transform_anim_frame_size];
            std::memcpy(copy, frame, sizeof(copy));
            copy[at] = static_cast<std::uint8_t>(copy[at] ^ 0x0C);
            PlayerTransformPacket untouched{}; AnimState untouched_anim{}; bool untouched_has = false;
            std::uint64_t us = 0; std::uint32_t ue = 0;
            Check(!decode_partner_transform_frame(copy, sizeof(copy), us, ue, untouched, untouched_anim, untouched_has) &&
                  us == 0 && untouched.sequence == 0 && !untouched_has, "corrupt v2 field refused, outputs untouched");
        }
        Check(!decode_partner_transform_frame(frame, sizeof(frame) - 1, s, e, got, got_anim, has) &&
              !decode_partner_transform_frame(nullptr, sizeof(frame), s, e, got, got_anim, has), "v2 size and null refused");
    }

    // 10. Look frames (40 bytes, kind event, sequence = revision).
    {
        const std::uint64_t session = 0x4D4753434F4F5001ULL;
        LookPacket look{6, 27, 116, 0, 0x1234, 7, LookFlag::PartsLoaded, 513};
        std::uint8_t frame[look_frame_size];
        Check(look_frame_size == 40 && encode_look_frame(session, 30050, look, frame) == look_frame_size, "look encode");
        wire::Packet expected{};
        expected.kind = wire::Kind::event; expected.session = session; expected.epoch = 30050; expected.sequence = 513;
        expected.payload = {0x4C, 1, 6, 27, 116, 0, 0x34, 0x12, 7, 1, 0x01, 0x02};
        const auto bytes = wire::encode(expected);
        Check(bytes.size() == look_frame_size && std::memcmp(bytes.data(), frame, look_frame_size) == 0, "look frame equals wire::encode");
        std::uint64_t s = 0; std::uint32_t e = 0; LookPacket got{};
        Check(decode_look_frame(frame, sizeof(frame), s, e, got) && s == session && e == 30050 && got.type == 6 && got.parts == 27 &&
              got.camo == 116 && got.hand == 0 && got.face == 0x1234 && got.face_equip == 7 && got.flags == LookFlag::PartsLoaded &&
              got.revision == 513, "look decode round trip");
        // Not confused with keepalives or transforms.
        std::uint64_t ka = 0; PlayerTransformPacket t{}; AnimState a{}; bool has = false;
        Check(!decode_keepalive_frame(frame, sizeof(frame), ka) && !decode_partner_transform_frame(frame, sizeof(frame), s, e, t, a, has),
              "a look is neither a keepalive nor a transform");
        std::uint8_t keepalive[keepalive_frame_size];
        encode_keepalive_frame(session, 30050, keepalive);
        Check(!decode_look_frame(keepalive, sizeof(keepalive), s, e, got), "a keepalive is not a look");
        // Refusals.
        LookPacket zero_rev = look; zero_rev.revision = 0;
        Check(encode_look_frame(session, 1, zero_rev, frame) == 0 && frame[0] == 0, "revision 0 refused, zeroed");
        LookPacket bad_flags = look; bad_flags.flags = 2;
        Check(encode_look_frame(session, 1, bad_flags, frame) == 0 && encode_look_frame(0, 1, look, frame) == 0 &&
              encode_look_frame(session, 0, look, frame) == 0, "flag/session/epoch refusals");
        Check(encode_look_frame(session, 30050, look, frame) == look_frame_size, "re-encode");
        const unsigned corrupt_at[] = {0, 6, 20, 24, 28, 29, 37, 38};  // magic, kind, header seq, length, look magic/version, flags, revision
        for (unsigned at : corrupt_at) {
            std::uint8_t copy[look_frame_size];
            std::memcpy(copy, frame, sizeof(copy));
            copy[at] = static_cast<std::uint8_t>(copy[at] ^ 0x06);
            LookPacket untouched{};
            Check(!decode_look_frame(copy, sizeof(copy), s, e, untouched) && untouched.revision == 0, "corrupt look field refused");
        }
        Check(!decode_look_frame(frame, sizeof(frame) - 1, s, e, got) && !decode_look_frame(nullptr, sizeof(frame), s, e, got),
              "look size and null refused");
    }

    std::printf("%u checks passed; player_sync transform codec, fixed-buffer frames, v2 anim extension and look frames verified\n", checks);
    return 0;
}
