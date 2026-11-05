// Enemy sync M7 FM1 probe P1 (native/include/mgscoop/soldier_act.hpp): the pure census helpers. No MGSV process.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "mgscoop/soldier_act.hpp"
#include "mgscoop/soldier_authority.hpp"
#include "soldier_act_contract.hpp"

namespace {
namespace sa = mgscoop::soldier_act;
namespace c = mgscoop::soldier_act_contract;
unsigned checks = 0;
void Check(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
bool Near(float a, float b) { return std::fabs(a - b) < 0.006f; }
} // namespace

int main() {
    // The engine's packed position (0x136f810 / 0x13704a0): 22/20/22 bits of cm around the offsets.
    {
        const float p[3] = {-1234.56f, 270.11f, 2084.42f};
        float back[3]{};
        sa::DecodePacked(sa::EncodePacked(p), back);
        Check(Near(back[0], p[0]) && Near(back[1], p[1]) && Near(back[2], p[2]), "packed position round trip (1 cm)");
        const float zero[3] = {0, 0, 0};
        Check(sa::EncodePacked(zero) == ((0x200000ull << 42) | (0x80000ull << 22) | 0x200000ull), "packed zero is the offsets");
        const float huge[3] = {1e9f, -1e9f, 1e9f};
        sa::DecodePacked(sa::EncodePacked(huge), back);
        Check(Near(back[0], 20971.51f) && Near(back[1], -5242.88f) && Near(back[2], 20971.51f), "packed position clamps");
    }
    // The fixed fields of a packed request come from 0x13704a0's byte writes.
    {
        unsigned char w[0x70]{};
        w[0x60] = 1; w[0x61] = 2; w[0x63] = 0x0d; w[0x64] = 3; w[0x65] = 2; w[0x66] = 1; w[0x67] = 3; w[0x68] = 1; w[0x6e] = 0x24;
        const float d[3] = {10.0f, 2.5f, -7.25f};
        const std::uint64_t v = sa::EncodePacked(d);
        std::memcpy(w + 0x40, &v, sizeof(v));
        const sa::Fixed f = sa::DecodeFixed(w);
        Check(f.path == 1 && f.dest_mode == 2 && f.kind_a == 0x0d && f.kind_b == 3 && f.gait == 2 && f.b66 == 1 && f.posture == 3 &&
                  f.face_mode == 1 && f.weapon == 0x24, "fixed fields at +0x60..+0x6e");
        Check(Near(f.dest[0], 10.0f) && Near(f.dest[1], 2.5f) && Near(f.dest[2], -7.25f), "destination at +0x40");
    }
    // The catch-up kinds of 0x136f810's switch.
    {
        const unsigned moving[] = {0x01, 0x0b, 0x0c, 0x0d, 0x11, 0x14, 0x15, 0x16, 0x18, 0x1a, 0x1f};
        unsigned n = 0;
        for (unsigned k = 0; k < 256; ++k) n += sa::MovingKind(static_cast<std::uint8_t>(k)) ? 1u : 0u;
        Check(n == sizeof(moving) / sizeof(moving[0]), "eleven moving kinds");
        for (const unsigned k : moving) Check(sa::MovingKind(static_cast<std::uint8_t>(k)), "a catch-up kind is moving");
        Check(!sa::MovingKind(0) && !sa::MovingKind(2) && !sa::MovingKind(0x0e), "idle kinds are not moving");
    }
    // Bands of 25 m, the last one beyond 250 m.
    Check(sa::Band(0.0f) == 0 && sa::Band(24.9f) == 0 && sa::Band(25.0f) == 1 && sa::Band(249.0f) == 9, "25 m bands");
    Check(sa::Band(250.0f) == sa::kBands - 1 && sa::Band(9999.0f) == sa::kBands - 1 && sa::Band(-1.0f) == sa::kBands - 1, "far band");
    // Hex.
    {
        const unsigned char b[3] = {0x00, 0xab, 0x7f};
        char out[7]{};
        Check(sa::Hex(b, 3, out, sizeof(out)) && std::strcmp(out, "00ab7f") == 0, "hex");
        Check(!sa::Hex(b, 3, out, 6), "hex refuses a short buffer");
    }
    // The engage bins: Defend hits on the local player per shooter (soldier_authority), taken and cleared.
    {
        namespace au = mgscoop::soldier_authority;
        static std::uint16_t hits[au::kSoldiers];
        au::TakeShooterHits(hits);
        au::NoteDefense(0, static_cast<std::uint16_t>((2u << 9) | 15u));
        au::NoteDefense(0, static_cast<std::uint16_t>((2u << 9) | 15u));
        au::NoteDefense(0, static_cast<std::uint16_t>((2u << 9) | 246u));
        au::NoteDefense(1, static_cast<std::uint16_t>((2u << 9) | 15u));   // body 1: not a local-player hit
        au::NoteDefense(0, static_cast<std::uint16_t>((1u << 9) | 3u));    // not a soldier
        Check(au::TakeShooterHits(hits) == 3 && hits[15] == 2 && hits[246] == 1 && hits[3] == 0, "shooter hits on body 0");
        Check(au::TakeShooterHits(hits) == 0 && hits[15] == 0, "taking clears");
    }
    // The contract's layout constants match the decompile the census reads by.
    Check(c::record_stride == 0x1e0 && c::fob_this == 0x20 && c::m_soldier == 0x50 && c::m_codec == 0x58, "M layout");
    Check(c::s_netpos == 0xe0 && c::net_stride == 0x18 && c::net_count == 0x10 && c::net_flags == 0x14, "network position table");
    Check(c::s_flags == 0x110 && c::flag_stride == 0x28 && c::flag_dword == 8 && c::flag_puppet_bit == 27, "SetPuppet bit");
    Check(c::r_flags == 0x19e && c::r_order == 0x1b0 && c::r_order_flags == 0x1d2 && c::wire_request == 0x70 && c::wire_order == 0x18,
          "record offsets");
    std::printf("soldier_act_tests: %u checks passed\n", checks);
    return 0;
}
