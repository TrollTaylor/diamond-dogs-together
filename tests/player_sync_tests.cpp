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

    std::printf("%u checks passed; player_sync transform codec verified\n", checks);
    return 0;
}
