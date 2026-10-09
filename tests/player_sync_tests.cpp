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

    // The same pose byte carries six known action bits. Walk/run must survive
    // every live codec, including mixed gun flags; bits 6/7 remain reserved.
    // Exercise all 256 values so a future producer/codec mismatch is visible.
    {
        constexpr std::uint64_t session = 0x4D4753434F4F5001ULL;
        std::uint8_t legacy[transform_frame_size]{}, extended[transform_anim_frame_size]{};
        const AnimState anim{3, 2, 0, AnimFlag::Valid};
        Check(encode_transform_frame(session, 3, p1, legacy) == sizeof(legacy) &&
              encode_transform_anim_frame(session, 3, p1, anim, extended) == sizeof(extended),
              "reference legacy and animated action frames retain their sizes");
        for (unsigned flags = 0; flags <= 255; ++flags) {
            PlayerTransformPacket sample = p1;
            sample.action_flags = static_cast<std::uint8_t>(flags);
            std::uint8_t plain[transform_frame_size]{}, rich[transform_anim_frame_size]{};
            std::uint64_t got_session = 0;
            std::uint32_t got_epoch = 0;
            PlayerTransformPacket got{};
            AnimState got_anim{};
            bool has_anim = false;
            if (flags <= 0x3f) {
                const auto packet = encode_transform(session, 3, sample);
                Check(packet.has_value(), "all known locomotion/gun action combinations encode");
                const auto parsed = decode_transform(*packet);
                Check(parsed.has_value() && parsed->action_flags == flags && parsed->equip_id == p1.equip_id,
                      "object codec round trips exact new and legacy action combinations");
                Check(encode_transform_frame(session, 3, sample, plain) == sizeof(plain) &&
                      decode_transform_frame(plain, sizeof(plain), got_session, got_epoch, got) &&
                      got.action_flags == flags && got_session == session && got_epoch == 3,
                      "fixed legacy codec accepts six known bits without changing framing");
                Check(decode_partner_transform_frame(plain, sizeof(plain), got_session, got_epoch, got, got_anim, has_anim) &&
                      !has_anim && got.action_flags == flags,
                      "partner codec preserves locomotion and gun flags from legacy-sized frames");
                Check(encode_transform_anim_frame(session, 3, sample, anim, rich) == sizeof(rich) &&
                      decode_partner_transform_frame(rich, sizeof(rich), got_session, got_epoch, got, got_anim, has_anim) &&
                      has_anim && got.action_flags == flags && got_anim.act == 3 && got_anim.stance == 2,
                      "live animated partner codec preserves combined action flags and raw animation");
            } else {
                Check(!encode_transform(session, 3, sample), "reserved action bits refuse object encoding");
                Check(encode_transform_frame(session, 3, sample, plain) == 0 && plain[0] == 0 && plain[sizeof(plain) - 1] == 0 &&
                      encode_transform_anim_frame(session, 3, sample, anim, rich) == 0 && rich[0] == 0 && rich[sizeof(rich) - 1] == 0,
                      "reserved action bits refuse and zero both fixed encoders");
                auto bad = *encoded;
                bad.payload[23] = sample.action_flags;
                Check(!decode_transform(bad), "reserved received action bits refuse object decoding");
                std::memcpy(plain, legacy, sizeof(plain));
                std::memcpy(rich, extended, sizeof(rich));
                plain[wire::header_size + 23] = rich[wire::header_size + 23] = sample.action_flags;
                Check(!decode_transform_frame(plain, sizeof(plain), got_session, got_epoch, got) &&
                      !decode_partner_transform_frame(plain, sizeof(plain), got_session, got_epoch, got, got_anim, has_anim) &&
                      !decode_partner_transform_frame(rich, sizeof(rich), got_session, got_epoch, got, got_anim, has_anim) &&
                      got_session == 0 && got_epoch == 0 && got.sequence == 0 && got_anim.act == 0 && !has_anim,
                      "reserved bits refuse legacy and animated receive paths without publishing output");
            }
        }
    }

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
        bad = anim; bad.stance = 0;
        Check(!valid_anim(bad) && encode_transform_anim_frame(session, 1, p1, bad, frame) == 0, "valid flag with stance 0 refused");
        bad = anim; bad.flags = 0;
        Check(!valid_anim(bad), "stance/speed without the valid flag refused");
        bad = AnimState{}; bad.speed = 2;
        Check(!valid_anim(bad), "speed without the valid flag refused");
        bad = AnimState{}; bad.act = 0x0079;
        Check(valid_anim(bad), "act alone is structural only (LocomotionAct gates the writes)");
        {
            // A received Valid extension with stance 0 is refused on decode.
            Check(encode_transform_anim_frame(session, 30050, p1, anim, frame) == transform_anim_frame_size, "re-encode for stance 0");
            std::uint8_t copy[transform_anim_frame_size];
            std::memcpy(copy, frame, sizeof(copy));
            copy[60] = 0;
            PlayerTransformPacket untouched{}; AnimState untouched_anim{}; bool untouched_has = false;
            std::uint64_t us = 0; std::uint32_t ue = 0;
            Check(!decode_partner_transform_frame(copy, sizeof(copy), us, ue, untouched, untouched_anim, untouched_has) && !untouched_has,
                  "decode refuses valid flag with stance 0");
            copy[60] = 3; copy[62] = 0;
            Check(!decode_partner_transform_frame(copy, sizeof(copy), us, ue, untouched, untouched_anim, untouched_has) && !untouched_has,
                  "decode refuses a stance without the valid flag");
        }
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

    // 9. Input frame (round 3 v1 decode, round 4 v2 encode/decode).
    {
        namespace pi = mgscoop::partner_input;
        pi::InputPacket pkt{};
        pkt.version = 2;
        pkt.flags = pi::Flag::Valid;
        pkt.held = 0x12345678u; pkt.w4c = 0xAAu; pkt.w50 = 0xBBu; pkt.w54 = 0xCCu;
        pkt.lx = 1000; pkt.ly = -2000; pkt.rx = 3000; pkt.ry = -4000;
        pkt.lt = 10; pkt.rt = 250; pkt.lmag = 50000; pkt.lforce = 40000; pkt.ldir = 12345; pkt.cam_cdeg = -9000;
        pkt.edge_seq = 7; pkt.edge_rep = 1; pkt.long12 = 0x2u; pkt.tap12 = 0x200000u;
        std::uint8_t frame[input_frame_size]{};
        Check(encode_input_frame(0x55, 7, 42, pkt, frame) == input_frame_size && input_frame_size == 76, "input frame v2 76 bytes");
        Check(frame[wire::header_size + 1] == 2 && frame[wire::header_size + 3] == 7 && frame[wire::header_size + 38] == 1,
              "v2 version, edge_seq, edge_rep bytes");
        std::uint64_t s = 0; std::uint32_t e = 0, seq = 0;
        pi::InputPacket got{};
        Check(decode_input_frame(frame, sizeof(frame), s, e, seq, got) && s == 0x55 && e == 7 && seq == 42, "input decode header");
        Check(got.version == 2 && got.held == pkt.held && got.w4c == pkt.w4c && got.w50 == pkt.w50 && got.w54 == pkt.w54 &&
              got.lx == pkt.lx && got.ly == pkt.ly && got.rx == pkt.rx && got.ry == pkt.ry && got.lt == pkt.lt && got.rt == pkt.rt &&
              got.lmag == pkt.lmag && got.lforce == pkt.lforce && got.ldir == pkt.ldir && got.cam_cdeg == pkt.cam_cdeg &&
              got.edge_seq == 7 && got.edge_rep == 1 && got.long12 == 0x2u && got.tap12 == 0x200000u, "input v2 fields round trip");
        // Zero session/epoch/sequence refused; a v1 packet is never encoded.
        Check(encode_input_frame(0, 7, 42, pkt, frame) == 0 && encode_input_frame(0x55, 0, 42, pkt, frame) == 0 &&
              encode_input_frame(0x55, 7, 0, pkt, frame) == 0, "zero session/epoch/seq refused");
        pi::InputPacket v1 = pkt; v1.version = 1;
        Check(encode_input_frame(0x55, 7, 42, v1, frame) == 0, "encode writes v2 only");
        // Without an edge set the edge words must be zero.
        pi::InputPacket noset = pkt; noset.edge_seq = 0; noset.edge_rep = 0;
        Check(encode_input_frame(0x55, 7, 42, noset, frame) == 0, "edge words without edge_seq refused");
        noset.w50 = noset.w54 = noset.tap12 = 0;
        Check(encode_input_frame(0x55, 7, 42, noset, frame) == input_frame_size, "no edge set, zero edge words: ok");
        // Online play: up to kEdgeRepeatsMax (8) repeats per edge set (partner_input_edge_repeats).
        pi::InputPacket rep3 = pkt; rep3.edge_rep = 3;
        Check(encode_input_frame(0x55, 7, 42, rep3, frame) == input_frame_size, "edge_rep 3 accepted (online repeats)");
        rep3.edge_rep = pi::kEdgeRepeatsMax;
        Check(encode_input_frame(0x55, 7, 42, rep3, frame) == input_frame_size, "edge_rep 8 accepted");
        rep3.edge_rep = pi::kEdgeRepeatsMax + 1;
        Check(encode_input_frame(0x55, 7, 42, rep3, frame) == 0, "edge_rep > 8 refused");
        // Strict decode: size, magic, version, kind, length, flag bits, reserved, ranges.
        Check(encode_input_frame(0x55, 7, 42, pkt, frame) == input_frame_size, "re-encode");
        Check(!decode_input_frame(frame, sizeof(frame) - 1, s, e, seq, got), "input size");
        std::uint8_t tweak[input_frame_size];
        std::memcpy(tweak, frame, sizeof(tweak)); tweak[wire::header_size] = 0x48;
        Check(!decode_input_frame(tweak, sizeof(tweak), s, e, seq, got), "input magic");
        std::memcpy(tweak, frame, sizeof(tweak)); tweak[wire::header_size + 1] = 1;
        Check(!decode_input_frame(tweak, sizeof(tweak), s, e, seq, got), "input version must match the size");
        std::memcpy(tweak, frame, sizeof(tweak)); tweak[6] = static_cast<std::uint8_t>(wire::Kind::snapshot);
        Check(!decode_input_frame(tweak, sizeof(tweak), s, e, seq, got), "input kind");
        std::memcpy(tweak, frame, sizeof(tweak)); tweak[24] = 47;
        Check(!decode_input_frame(tweak, sizeof(tweak), s, e, seq, got), "input length");
        std::memcpy(tweak, frame, sizeof(tweak)); tweak[20] = 0; tweak[21] = 0; tweak[22] = 0; tweak[23] = 0;  // seq 0
        Check(!decode_input_frame(tweak, sizeof(tweak), s, e, seq, got), "input seq 0");
        std::memcpy(tweak, frame, sizeof(tweak)); tweak[wire::header_size + 2] = 0x08;
        Check(!decode_input_frame(tweak, sizeof(tweak), s, e, seq, got), "input flag bits");
        std::memcpy(tweak, frame, sizeof(tweak)); tweak[wire::header_size + 39] = 1;
        Check(!decode_input_frame(tweak, sizeof(tweak), s, e, seq, got), "input reserved byte");
        std::memcpy(tweak, frame, sizeof(tweak)); tweak[wire::header_size + 38] = 3;
        Check(decode_input_frame(tweak, sizeof(tweak), s, e, seq, got) && got.edge_rep == 3, "edge_rep 3 decodes (online repeats)");
        std::memcpy(tweak, frame, sizeof(tweak)); tweak[wire::header_size + 38] = 9;
        Check(!decode_input_frame(tweak, sizeof(tweak), s, e, seq, got), "edge_rep 9 refused on decode");
        // -32768 axis refused.
        std::memcpy(tweak, frame, sizeof(tweak)); tweak[wire::header_size + 20] = 0x00; tweak[wire::header_size + 21] = 0x80;
        Check(!decode_input_frame(tweak, sizeof(tweak), s, e, seq, got), "axis -32768 refused");
        // ldir out of range.
        pi::InputPacket bad = pkt; bad.ldir = 31417;
        Check(encode_input_frame(0x55, 7, 42, bad, frame) == 0, "ldir > 31416 refused by encode");
        bad = pkt; bad.cam_cdeg = 18000;
        Check(encode_input_frame(0x55, 7, 42, bad, frame) == 0, "cam out of range refused by encode");
        // A v1 frame (68 bytes, from a round 3 build) still decodes: version 1,
        // the latched w50/w54 kept, no edge sequence or 12-frame words.
        Check(encode_input_frame(0x55, 7, 42, pkt, frame) == input_frame_size, "re-encode for the v1 copy");
        std::uint8_t old[input_frame_size_v1]{};
        std::memcpy(old, frame, wire::header_size);
        old[24] = static_cast<std::uint8_t>(input_packet_size_v1);
        old[25] = old[26] = old[27] = 0;
        std::uint8_t* ob = old + wire::header_size;
        const std::uint8_t* nb = frame + wire::header_size;
        std::memcpy(ob, nb, 38);
        ob[1] = 1; ob[3] = 0; ob[38] = 0; ob[39] = 0;
        Check(decode_input_frame(old, sizeof(old), s, e, seq, got) && got.version == 1 && got.w50 == 0xBBu && got.w54 == 0xCCu &&
              got.edge_seq == 0 && got.long12 == 0 && got.tap12 == 0 && got.held == pkt.held, "v1 frame decoded");
        std::uint8_t old2[input_frame_size_v1];
        std::memcpy(old2, old, sizeof(old2)); old2[wire::header_size + 3] = 1;
        Check(!decode_input_frame(old2, sizeof(old2), s, e, seq, got), "v1 reserved byte 3 refused");
        std::memcpy(old2, old, sizeof(old2)); old2[wire::header_size + 1] = 2;
        Check(!decode_input_frame(old2, sizeof(old2), s, e, seq, got), "v1 size with version 2 refused");
    }

    // 10. Input frame v3 (native puppet): 100 bytes, look/camera/heading fields.
    {
        namespace pi = mgscoop::partner_input;
        pi::InputPacket pkt{};
        pkt.version = 2;
        pkt.flags = pi::Flag::Valid;
        pkt.held = 0x8u; pkt.w4c = 0x8u; pkt.lx = 100; pkt.ly = -32767; pkt.lmag = 65535; pkt.ldir = 31416; pkt.cam_cdeg = 4500;
        pi::V3Source src{};
        src.look_valid = true; src.look_pitch = -0.25f; src.look_yaw = 2.5f;
        src.cam_valid = true; src.cam270 = 1.5f; src.cam274 = -0.75f;
        src.heading_valid = true; src.heading_rad = 3.5;  // wraps to 3.5 - 2 pi
        src.aiming = true; src.inv_slot = 2; src.equip_rev = 9;
        pi::InputPacket v3 = pkt;
        pi::Fill3(v3, src);
        Check(v3.version == 3 && v3.xflags == 0x0F && v3.heading_mrad == static_cast<std::int16_t>(std::lround((3.5 - 2 * 3.14159265358979323846) * 1000)) &&
              v3.inv_slot == 2 && v3.equip_rev == 9 && pi::ValidPacket(v3), "Fill3 builds a valid v3 packet");
        std::uint8_t frame[input_frame_size_v4]{};
        Check(input_frame_size_v3 == 100 && encode_input_frame_any(0x55, 7, 42, v3, frame) == 100, "v3 encodes 100 bytes");
        Check(frame[wire::header_size + 1] == 3 && frame[24] == 72 && frame[wire::header_size + 66] == 0x0F &&
              frame[wire::header_size + 67] == 2 && frame[wire::header_size + 68] == 9, "v3 version, payload length, xflags, inv, rev");
        std::uint64_t s = 0; std::uint32_t e = 0, seq = 0;
        pi::InputPacket got{};
        Check(decode_input_frame(frame, input_frame_size_v3, s, e, seq, got) && got.version == 3 && got.look_pitch == -0.25f &&
              got.look_yaw == 2.5f && got.cam270 == 1.5f && got.cam274 == -0.75f && got.heading_mrad == v3.heading_mrad &&
              got.xflags == 0x0F && got.inv_slot == 2 && got.equip_rev == 9 && got.held == 0x8u && got.ly == -32767 && seq == 42,
              "v3 round trip");
        // v2 via the generic encoder; a legacy sender still encodes v2.
        std::uint8_t any[input_frame_size_v4]{};
        Check(encode_input_frame_any(0x55, 7, 43, pkt, any) == input_frame_size && decode_input_frame(any, input_frame_size, s, e, seq, got) &&
              got.version == 2 && got.xflags == 0 && got.look_pitch == 0.0f, "legacy sender still encodes v2");
        std::uint8_t v2frame[input_frame_size]{};
        Check(encode_input_frame(0x55, 7, 42, v3, v2frame) == 0, "the v2 encoder refuses a v3 packet");
        // Refusals.
        auto refused = [&](void (*edit)(pi::InputPacket&)) {
            pi::InputPacket b = v3;
            edit(b);
            return !pi::ValidPacket(b) && encode_input_frame_any(0x55, 7, 42, b, frame) == 0;
        };
        Check(refused([](pi::InputPacket& b) { b.look_pitch = std::nanf(""); }), "non-finite pitch refused");
        Check(refused([](pi::InputPacket& b) { b.look_pitch = 1.61f; }), "|pitch| > 1.6 refused");
        Check(refused([](pi::InputPacket& b) { b.look_yaw = -3.2f; }), "|yaw| > pi refused");
        Check(refused([](pi::InputPacket& b) { b.cam270 = 65.0f; }), "|cam270| > 64 refused");
        Check(refused([](pi::InputPacket& b) { b.cam274 = std::numeric_limits<float>::infinity(); }), "infinite cam274 refused");
        Check(refused([](pi::InputPacket& b) { b.heading_mrad = 3143; }), "heading > 3142 refused");
        Check(refused([](pi::InputPacket& b) { b.xflags = 0x20; }), "unknown xflags refused");
        Check(refused([](pi::InputPacket& b) { b.xflags |= pi::XFlag::Mag; b.mag = 1; b.mag_cap = 5; }), "the v4 magazine in a v3 packet refused");
        Check(refused([](pi::InputPacket& b) { b.xflags &= static_cast<std::uint8_t>(~pi::XFlag::Look); }), "look fields without their xflag refused");
        Check(refused([](pi::InputPacket& b) { b.inv_slot = 16; }), "inv_slot 16 refused");
        Check(refused([](pi::InputPacket& b) { b.version = 2; }), "v3 fields in a v2 packet refused");
        pi::InputPacket unk = v3;
        unk.inv_slot = 0xFF;
        Check(pi::ValidPacket(unk), "inv_slot unknown accepted");
        Check(encode_input_frame_any(0x55, 7, 42, v3, frame) == 100, "re-encode");
        std::uint8_t tweak[input_frame_size_v3];
        std::memcpy(tweak, frame, sizeof(tweak)); tweak[wire::header_size + 70] = 1;
        Check(!decode_input_frame(tweak, sizeof(tweak), s, e, seq, got), "v3 reserved byte 70 refused");
        std::memcpy(tweak, frame, sizeof(tweak)); tweak[wire::header_size + 71] = 1;
        Check(!decode_input_frame(tweak, sizeof(tweak), s, e, seq, got), "v3 reserved byte 71 refused");
        std::memcpy(tweak, frame, sizeof(tweak)); tweak[wire::header_size + 1] = 2;
        Check(!decode_input_frame(tweak, sizeof(tweak), s, e, seq, got), "100-byte frame with version 2 refused");
        std::memcpy(tweak, frame, sizeof(tweak)); tweak[wire::header_size + 66] = 0x1F;
        Check(!decode_input_frame(tweak, sizeof(tweak), s, e, seq, got), "unknown xflags refused on decode");
        std::memcpy(tweak, frame, sizeof(tweak)); tweak[wire::header_size + 51] = 0x7F; tweak[wire::header_size + 50] = 0xC0;
        Check(!decode_input_frame(tweak, sizeof(tweak), s, e, seq, got), "NaN pitch refused on decode");
        // Fill3 drops invalid groups instead of sending them.
        pi::V3Source badsrc = src;
        badsrc.look_pitch = 5.0f; badsrc.cam_valid = false; badsrc.inv_slot = 40;
        pi::InputPacket f = pkt;
        pi::Fill3(f, badsrc);
        Check(pi::ValidPacket(f) && !(f.xflags & pi::XFlag::Look) && !(f.xflags & pi::XFlag::Cam) && f.look_pitch == 0.0f &&
              f.inv_slot == 0xFF, "Fill3 clears invalid groups");
    }

    // 11. Input frame v4 (native rates and weapons): 112 bytes, v3 plus the sender's
    // rates (xflags Rates, 72/74/76) and magazine (xflags Mag, 78/80). Here: rates.
    {
        namespace pi = mgscoop::partner_input;
        pi::InputPacket pkt{};
        pkt.version = 2;
        pkt.flags = pi::Flag::Valid;
        pkt.held = 0x40004u; pkt.w4c = 0x40004u; pkt.ly = 32767; pkt.lmag = 65535;
        pi::V4Source src{};
        src.v3.heading_valid = true; src.v3.heading_rad = 0.5; src.v3.inv_slot = 0xFF;
        src.rates_valid = true; src.rate_dash = 1200; src.rate_crawl = 1000; src.rate_crawl_factor = 1200;
        pi::InputPacket v4 = pkt;
        pi::Fill4(v4, src);
        Check(v4.version == 4 && (v4.xflags & pi::XFlag::Rates) && (v4.xflags & pi::XFlag::Heading) && !(v4.xflags & pi::XFlag::Mag) &&
              v4.rate_dash == 1200 && v4.rate_crawl == 1000 && v4.rate_crawl_factor == 1200 && v4.mag == 0 && v4.mag_cap == 0 &&
              pi::ValidPacket(v4), "Fill4 builds a valid v4 packet (rates, no magazine)");
        std::uint8_t frame[input_frame_size_v4]{};
        Check(input_frame_size_v4 == 112 && encode_input_frame_any(0x55, 7, 50, v4, frame) == 112, "v4 encodes 112 bytes");
        Check(frame[wire::header_size + 1] == 4 && frame[24] == 84 && frame[wire::header_size + 66] == (pi::XFlag::Rates | pi::XFlag::Heading) &&
              frame[wire::header_size + 72] == 0xB0 && frame[wire::header_size + 73] == 0x04 && frame[wire::header_size + 74] == 0xE8 &&
              frame[wire::header_size + 76] == 0xB0 && frame[wire::header_size + 78] == 0 && frame[wire::header_size + 80] == 0 &&
              frame[wire::header_size + 82] == 0 && frame[wire::header_size + 83] == 0,
              "v4 version, payload length 84, xflags with Rates, rates little-endian at 72/74/76, no magazine");
        std::uint64_t s = 0; std::uint32_t e = 0, seq = 0;
        pi::InputPacket got{};
        Check(decode_input_frame(frame, input_frame_size_v4, s, e, seq, got) && got.version == 4 && got.rate_dash == 1200 &&
              got.rate_crawl == 1000 && got.rate_crawl_factor == 1200 && (got.xflags & pi::XFlag::Rates) && got.held == 0x40004u &&
              got.heading_mrad == v4.heading_mrad && got.mag == 0 && !(got.xflags & pi::XFlag::Mag) && seq == 50,
              "v4 round trip (EVADE held, rates)");
        // Without readable rates (or partner_native_rates=off): v4 with no Rates flag and zero fields.
        pi::V4Source no_rates = src;
        no_rates.rates_valid = false;
        pi::InputPacket none = pkt;
        pi::Fill4(none, no_rates);
        Check(none.version == 4 && !(none.xflags & pi::XFlag::Rates) && none.rate_dash == 0 && pi::ValidPacket(none) &&
              encode_input_frame_any(0x55, 7, 51, none, frame) == 112 && decode_input_frame(frame, 112, s, e, seq, got) &&
              got.rate_dash == 0 && !(got.xflags & pi::XFlag::Rates), "v4 without rates");
        pi::V4Source big = src;
        big.rate_dash = 2001;
        pi::InputPacket out_of_range = pkt;
        pi::Fill4(out_of_range, big);
        Check(!(out_of_range.xflags & pi::XFlag::Rates) && out_of_range.rate_dash == 0 && out_of_range.rate_crawl == 0,
              "Fill4 drops out-of-range rates");
        auto refused4 = [&](void (*edit)(pi::InputPacket&)) {
            pi::InputPacket b = v4;
            edit(b);
            std::uint8_t f4[input_frame_size_v4]{};
            return !pi::ValidPacket(b) && encode_input_frame_any(0x55, 7, 52, b, f4) == 0;
        };
        Check(refused4([](pi::InputPacket& b) { b.rate_dash = 499; }), "rate below 0.5 refused");
        Check(refused4([](pi::InputPacket& b) { b.rate_crawl_factor = 2001; }), "rate above 2.0 refused");
        Check(refused4([](pi::InputPacket& b) { b.xflags &= static_cast<std::uint8_t>(~pi::XFlag::Rates); }), "rates without their xflag refused");
        Check(refused4([](pi::InputPacket& b) { b.version = 3; }), "rates in a v3 packet refused");
        Check(refused4([](pi::InputPacket& b) { b.xflags |= pi::XFlag::Equip; }), "xflags Equip without an equip id refused");
        Check(refused4([](pi::InputPacket& b) { b.gun_equip = 46; }), "a gun equip without xflags Equip refused");
        Check(refused4([](pi::InputPacket& b) { b.xflags |= pi::XFlag::Mag; }), "xflags Mag without a capacity refused");
        std::uint8_t tweak[input_frame_size_v4];
        Check(encode_input_frame_any(0x55, 7, 50, v4, frame) == 112, "re-encode v4");
        std::memcpy(tweak, frame, sizeof(tweak)); tweak[wire::header_size + 82] = 1;
        Check(!decode_input_frame(tweak, sizeof(tweak), s, e, seq, got), "v4 gun equip (byte 82) without xflags Equip refused");
        std::memcpy(tweak, frame, sizeof(tweak)); tweak[wire::header_size + 83] = 1;
        Check(!decode_input_frame(tweak, sizeof(tweak), s, e, seq, got), "v4 gun equip (byte 83) without xflags Equip refused");
        std::memcpy(tweak, frame, sizeof(tweak)); tweak[wire::header_size + 1] = 3;
        Check(!decode_input_frame(tweak, sizeof(tweak), s, e, seq, got), "112-byte frame with version 3 refused");
        std::memcpy(tweak, frame, sizeof(tweak)); tweak[wire::header_size + 72] = 0xF3; tweak[wire::header_size + 73] = 0x01;
        Check(!decode_input_frame(tweak, sizeof(tweak), s, e, seq, got), "rate 499 refused on decode");
        std::memcpy(tweak, frame, sizeof(tweak));
        tweak[wire::header_size + 66] = static_cast<std::uint8_t>(pi::XFlag::Equip | pi::XFlag::Rates | pi::XFlag::Heading);
        Check(!decode_input_frame(tweak, sizeof(tweak), s, e, seq, got), "xflags Equip without an equip id refused on decode");
        // The gamepad flag and the sender's gun equip round-trip (bytes 82..83).
        {
            pi::InputPacket g = v4;
            g.xflags = static_cast<std::uint8_t>(g.xflags | pi::XFlag::Gamepad | pi::XFlag::Equip);
            g.gun_equip = 0x1234;
            std::uint8_t gf[input_frame_size_v4]{};
            pi::InputPacket gg{};
            Check(pi::ValidPacket(g) && encode_input_frame_any(0x55, 7, 53, g, gf) == 112 && gf[wire::header_size + 82] == 0x34 &&
                      gf[wire::header_size + 83] == 0x12 && decode_input_frame(gf, sizeof(gf), s, e, seq, gg) && gg.gun_equip == 0x1234 &&
                      (gg.xflags & pi::XFlag::Gamepad) && (gg.xflags & pi::XFlag::Equip),
                  "v4 gamepad flag and gun equip round trip");
        }
        Check(!decode_input_frame(frame, input_frame_size_v3, s, e, seq, got), "a v4 frame cut to 100 bytes is refused");
        // The single-feature v4 drafts (104: magazine only, 108: rates only) never decode.
        Check(!decode_input_frame(frame, 104, s, e, seq, got) && !decode_input_frame(frame, 108, s, e, seq, got),
              "104- and 108-byte frames are refused");
    }
    // 12. Input frame v4 (native weapons): the sender's magazine (xflags Mag, mag at 78, cap at 80).
    {
        namespace pi = mgscoop::partner_input;
        pi::InputPacket pkt{};
        pkt.version = 2;
        pkt.flags = pi::Flag::Valid;
        pkt.held = 0x18u; pkt.w4c = 0x18u; pkt.lmag = 100; pkt.cam_cdeg = -18000;
        pi::V4Source src{};
        src.v3.look_valid = true; src.v3.look_pitch = 0.5f; src.v3.look_yaw = -1.0f;
        src.v3.aiming = true; src.v3.inv_slot = 0xFF; src.v3.equip_rev = 3;
        src.mag_valid = true; src.mag = 4; src.mag_cap = 5;
        pi::InputPacket v4 = pkt;
        pi::Fill4(v4, src);
        Check(v4.version == 4 && (v4.xflags & pi::XFlag::Mag) && (v4.xflags & pi::XFlag::Look) && !(v4.xflags & pi::XFlag::Rates) &&
              v4.mag == 4 && v4.mag_cap == 5 && v4.rate_dash == 0 && v4.equip_rev == 3 && pi::ValidPacket(v4),
              "Fill4 builds a valid v4 packet (magazine, no rates)");
        std::uint8_t frame[input_frame_size_v4]{};
        Check(encode_input_frame_any(0x55, 7, 50, v4, frame) == 112, "v4 with a magazine encodes 112 bytes");
        Check(frame[wire::header_size + 1] == 4 && frame[24] == 84 && frame[wire::header_size + 72] == 0 && frame[wire::header_size + 76] == 0 &&
              frame[wire::header_size + 78] == 4 && frame[wire::header_size + 80] == 5, "v4 version, payload length 84, mag at 78, cap at 80");
        std::uint64_t s = 0; std::uint32_t e = 0, seq = 0;
        pi::InputPacket got{};
        Check(decode_input_frame(frame, sizeof(frame), s, e, seq, got) && got.version == 4 && got.mag == 4 && got.mag_cap == 5 &&
              (got.xflags & pi::XFlag::Mag) && got.look_pitch == 0.5f && got.equip_rev == 3 && got.held == 0x18u && seq == 50,
              "v4 round trip (magazine)");
        Check(!decode_input_frame(frame, input_frame_size_v3, s, e, seq, got), "a v4 frame cut to 100 bytes is refused");
        std::uint8_t tweak[input_frame_size_v4];
        std::memcpy(tweak, frame, sizeof(tweak)); tweak[wire::header_size + 78] = 6;
        Check(!decode_input_frame(tweak, sizeof(tweak), s, e, seq, got), "mag > cap refused on decode");
        std::memcpy(tweak, frame, sizeof(tweak)); tweak[wire::header_size + 80] = 0; tweak[wire::header_size + 78] = 0;
        Check(!decode_input_frame(tweak, sizeof(tweak), s, e, seq, got), "Mag with capacity 0 refused");
        std::memcpy(tweak, frame, sizeof(tweak)); tweak[wire::header_size + 66] &= static_cast<std::uint8_t>(~pi::XFlag::Mag);
        Check(!decode_input_frame(tweak, sizeof(tweak), s, e, seq, got), "magazine fields without xflags Mag refused");
        std::memcpy(tweak, frame, sizeof(tweak)); tweak[wire::header_size + 1] = 3;
        Check(!decode_input_frame(tweak, sizeof(tweak), s, e, seq, got), "112-byte frame with version 3 refused");
        // An invalid magazine is not sent (Mag clear, both fields 0), and capacity 999 is the limit.
        pi::V4Source bad = src;
        bad.mag = 6;
        pi::InputPacket f = pkt;
        pi::Fill4(f, bad);
        Check(pi::ValidPacket(f) && f.version == 4 && !(f.xflags & pi::XFlag::Mag) && f.mag == 0 && f.mag_cap == 0, "Fill4 drops mag > cap");
        bad = src; bad.mag_cap = 1000; bad.mag = 1;
        f = pkt; pi::Fill4(f, bad);
        Check(!(f.xflags & pi::XFlag::Mag), "Fill4 drops capacity > 999");
        bad = src; bad.mag_valid = false;
        f = pkt; pi::Fill4(f, bad);
        Check(pi::ValidPacket(f) && !(f.xflags & pi::XFlag::Mag) && encode_input_frame_any(0x55, 7, 51, f, frame) == 112 &&
              decode_input_frame(frame, sizeof(frame), s, e, seq, got) && got.version == 4 && got.mag == 0 && !(got.xflags & pi::XFlag::Mag),
              "a v4 frame without a magazine round-trips");
        pi::InputPacket v2 = pkt;
        v2.mag = 1;
        Check(!pi::ValidPacket(v2), "magazine fields in a v2 packet refused");
        pi::InputPacket v3 = v4;
        v3.version = 3;
        Check(!pi::ValidPacket(v3), "the magazine in a v3 packet refused");
        // Both groups in one frame (a native sender with a gun and readable rates).
        pi::V4Source both = src;
        both.rates_valid = true; both.rate_dash = 1100; both.rate_crawl = 900; both.rate_crawl_factor = 1500;
        pi::InputPacket b4 = pkt;
        pi::Fill4(b4, both);
        Check(pi::ValidPacket(b4) && (b4.xflags & pi::XFlag::Rates) && (b4.xflags & pi::XFlag::Mag) &&
              encode_input_frame_any(0x55, 7, 53, b4, frame) == 112 && frame[wire::header_size + 66] == b4.xflags &&
              decode_input_frame(frame, sizeof(frame), s, e, seq, got) && got.rate_dash == 1100 && got.rate_crawl == 900 &&
              got.rate_crawl_factor == 1500 && got.mag == 4 && got.mag_cap == 5 && got.look_yaw == -1.0f && got.equip_rev == 3,
              "v4 carries the rates and the magazine together");
        // Fill3 (a v3 sender) clears both v4 groups.
        pi::InputPacket back = b4;
        pi::Fill3(back, both.v3);
        Check(back.version == 3 && back.rate_dash == 0 && back.mag == 0 && !(back.xflags & (pi::XFlag::Rates | pi::XFlag::Mag)) &&
              pi::ValidPacket(back), "Fill3 leaves no v4 field");
    }

    std::printf("%u checks passed; player_sync transform codec, fixed-buffer frames, v2 anim extension, look and input frames verified\n", checks);
    return 0;
}
