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
    // FM1, the far mirror: players swap between the copies; soldiers and other ids stay.
    Check(sa::RemapPlayer(0) == 1 && sa::RemapPlayer(1) == 0 && sa::RemapPlayer(2) == 2 && sa::RemapPlayer(0x400 | 15) == (0x400 | 15) &&
              sa::RemapPlayer(0xffff) == 0xffff, "player ids 0 <-> 1 only");
    // U3: the aim action (B kind 1 at wire +0x64) names its target by id in modes 2 and 4 (big-endian u16 at wire +0x20,
    // the mode at +0x35); the players swap, nothing else does.
    {
        unsigned char w[0x70]{};
        w[0x64] = 1;
        w[0x35] = 4;
        Check(sa::RemapAimTarget(w) && w[0x20] == 0 && w[0x21] == 1, "U3: an aim at player 0 aims at player 1 on the other copy");
        Check(sa::RemapAimTarget(w) && w[0x20] == 0 && w[0x21] == 0, "U3: and back");
        w[0x35] = 2;
        w[0x23] = 1;  // the part
        Check(sa::RemapAimTarget(w) && w[0x21] == 1 && w[0x23] == 1, "U3: mode 2 (id + part) swaps the id, keeps the part");
        unsigned char s[0x70]{};
        s[0x64] = 1;
        s[0x35] = 4;
        s[0x20] = 0x04;
        s[0x21] = 0x0f;
        Check(!sa::RemapAimTarget(s) && s[0x20] == 0x04 && s[0x21] == 0x0f, "U3: a soldier target stays");
        unsigned char m[0x70]{};
        m[0x64] = 1;
        m[0x35] = 1;  // a packed position
        Check(!sa::RemapAimTarget(m) && m[0x21] == 0, "U3: mode 1 (a position) is not an id");
        unsigned char k[0x70]{};
        k[0x64] = 2;
        k[0x35] = 4;
        Check(!sa::RemapAimTarget(k) && k[0x21] == 0, "U3: only the aim kind (B kind 1)");
    }
    Check(sa::kCensusRangeM >= 300.0f, "the census covers the far mirror's 300 m range (puppets beyond 250 m logged)");
    // The change hash ignores jitter under 5 cm and 1 degree.
    {
        unsigned char a[0x70]{}, b[0x70]{};
        a[0x63] = b[0x63] = 0x0d;
        const float d0[3] = {100.0f, 20.0f, 300.0f}, d1[3] = {100.03f, 20.0f, 300.02f}, d2[3] = {100.08f, 20.0f, 300.0f};
        const std::uint64_t v0 = sa::EncodePacked(d0), v1 = sa::EncodePacked(d1), v2 = sa::EncodePacked(d2);
        std::memcpy(a + 0x40, &v0, 8);
        std::memcpy(b + 0x40, &v1, 8);
        Check(!sa::RequestChanged(a, b), "a destination 3.6 cm off is jitter");
        std::memcpy(b + 0x40, &v2, 8);
        Check(sa::RequestChanged(a, b), "8 cm is a change");
        std::memcpy(b + 0x40, &v0, 8);
        a[0x68] = b[0x68] = 1;
        const std::uint16_t y0 = 1000, y1 = 1150, y2 = 1200;
        std::memcpy(a + 0x50, &y0, 2);
        std::memcpy(b + 0x50, &y1, 2);
        Check(!sa::RequestChanged(a, b), "a facing 150/65536 off is jitter");
        std::memcpy(b + 0x50, &y2, 2);
        Check(sa::RequestChanged(a, b), "200/65536 (1.1 degree) is a change");
        std::memcpy(b + 0x50, &y0, 2);
        b[0x63] = 0x14;
        Check(sa::RequestChanged(a, b), "another kind is a change");
        b[0x63] = 0x0d;
        b[0x05] = 9;
        Check(sa::RequestChanged(a, b), "another payload byte is a change");
        unsigned char o[0x18]{}, p[0x18]{};
        o[0xe] = p[0xe] = 1;
        std::memcpy(o, &v0, 8);
        std::memcpy(p, &v1, 8);
        Check(!sa::OrderChanged(o, p), "an aim point 3.6 cm off is jitter");
        std::memcpy(p, &v2, 8);
        Check(sa::OrderChanged(o, p), "an aim point 8 cm off is a change");
        o[0xe] = p[0xe] = 2;
        std::memcpy(p, o, 8);
        p[0] ^= 1;
        Check(sa::OrderChanged(o, p), "another target id is a change");
    }
    // The rate cap: a change at most every 100 ms, the same record again after 1 s.
    Check(sa::DueSend(true, 0, 5) && sa::DueSend(false, 0, 5), "the first send is due");
    Check(!sa::DueSend(true, 1000, 1099) && sa::DueSend(true, 1000, 1100), "a change 100 ms after the last");
    Check(!sa::DueSend(false, 1000, 1999) && sa::DueSend(false, 1000, 2000), "unchanged: the 1 s resend");
    // The entry pre-fill: this copy's own position (no 15 m warp), flag bit 0.
    {
        unsigned char e[0x18];
        std::memset(e, 0xcc, sizeof(e));
        const float p[3] = {1.5f, -2.0f, 3.25f};
        sa::PrefillEntry(e, p, 0.5f);
        float back[3]{}, yaw = 0;
        std::memcpy(back, e, sizeof(back));
        std::memcpy(&yaw, e + 0xc, 4);
        Check(back[0] == 1.5f && back[1] == -2.0f && back[2] == 3.25f && yaw == 0.5f && e[0x14] == 1 && e[0x10] == 0 && e[0x17] == 0,
              "entry: xyz, yaw, +0x14 bit 0, the rest zero");
    }
    // The mirror mask (soldier_authority): soldier GameObjectIds only; the drops and local shots counted.
    {
        namespace au = mgscoop::soldier_authority;
        au::ClearMirrored();
        au::SetMirrored(15, true);
        au::SetMirrored(246, true);
        Check(au::Mirrored(static_cast<std::uint16_t>(0x400 | 15)) && au::Mirrored(static_cast<std::uint16_t>(0x400 | 246)) &&
                  !au::Mirrored(15) && !au::Mirrored(1) && !au::Mirrored(static_cast<std::uint16_t>(0x400 | 16)) && au::MirroredCount() == 2,
              "the mirror mask holds soldiers only");
        au::SetMirrored(15, false);
        Check(!au::Mirrored(static_cast<std::uint16_t>(0x400 | 15)) && au::MirroredCount() == 1, "a release clears its bit");
        std::uint32_t f0 = 0, n0 = 0, f1 = 0, n1 = 0;
        au::MirrorDrops(f0, n0);
        au::NoteMirrorDrop(false);
        au::NoteMirrorDrop(true);
        au::NoteMirrorDrop(false);
        au::MirrorDrops(f1, n1);
        const std::uint32_t s0 = au::LocalShots();
        au::NoteLocalShot();
        Check(f1 - f0 == 2 && n1 - n0 == 1 && au::LocalShots() == s0 + 1, "drops and local shots are counted");
        au::ClearMirrored();
        Check(au::MirroredCount() == 0, "cleared");
    }
    // The ACT_V1 reply compiles in Lua: no reserved word as a key (run 963ab276: "or=" did not, so no puppet was set).
    {
        sa::Counters k{};
        k.tx_parts = 3; k.rx = 2; k.applied = 1; k.orders = 7; k.local_shots = 4;
        char out[160]{};
        Check(sa::FormatActReply(true, k, out, sizeof(out)) &&
                  std::strcmp(out, "return {f=1,tx=3,rx=2,ap=1,od=7,sh=4,mi=0,fd=0,fn=0}") == 0, "the ACT_V1 reply");
        static const char* reserved[] = {"and", "break", "do", "else", "elseif", "end", "false", "for", "function", "if", "in",
                                         "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"};
        for (const char* word : reserved) {
            char key1[16]{}, key2[16]{};
            std::snprintf(key1, sizeof(key1), "{%s=", word);
            std::snprintf(key2, sizeof(key2), ",%s=", word);
            Check(!std::strstr(out, key1) && !std::strstr(out, key2), "no Lua reserved word as a reply key");
        }
        char tiny[8]{};
        Check(!sa::FormatActReply(false, k, tiny, sizeof(tiny)), "a short buffer is refused");
        // The m reply carries the owner copy's position (mm) when the mailbox has it (the apart release, run 94a7bf2a).
        const std::int32_t owner[3] = {1500, -2000, 3250};
        Check(sa::FormatActReply(true, k, out, sizeof(out), owner) &&
                  std::strcmp(out, "return {f=1,tx=3,rx=2,ap=1,od=7,sh=4,mi=0,fd=0,fn=0,ox=1500,oy=-2000,oz=3250}") == 0,
              "the m reply with the owner's position");
        std::int32_t none[3]{};
        Check(!sa::MailOwner(sa::kMaxSoldiers, none) && !sa::MailOwner(7, none), "no mailbox, no owner position");
    }
    // The fire-drop mask is a lease (skeptic of FM1, 7b709720): a puppet dropped without ACT_V1 r must not keep his
    // bullets and noise dropped. The hook expires every bit not stamped again within kActFreshMs.
    {
        namespace au = mgscoop::soldier_authority;
        au::ClearMirrored();
        au::SetMirrored(15, true, 1000);
        au::SetMirrored(16, true, 2000);
        Check(au::ExpireMirrored(2400, sa::kActFreshMs) == 0 && au::Mirrored(0x400 | 15) && au::Mirrored(0x400 | 16),
              "mirror lease: fresh stamps stay");
        Check(au::ExpireMirrored(2600, sa::kActFreshMs) == 1 && !au::Mirrored(0x400 | 15) && au::Mirrored(0x400 | 16),
              "mirror lease: a lapsed stamp clears (his fire and noise pass again)");
        au::SetMirrored(16, true, 3000);
        Check(au::ExpireMirrored(4000, sa::kActFreshMs) == 0 && au::Mirrored(0x400 | 16), "mirror lease: a new stamp extends it");
        au::SetMirrored(16, false);
        Check(!au::Mirrored(0x400 | 16) && au::MirroredCount() == 0, "mirror lease: off clears at once");
        au::SetMirrored(17, true, 5000);
        Check(au::ExpireMirrored(4990, sa::kActFreshMs) == 0 && au::Mirrored(0x400 | 17), "mirror lease: a stamp later than now is fresh");
        Check(au::ExpireMirrored(1ull << 40, sa::kActFreshMs) == 1 && au::MirroredCount() == 0, "mirror lease: all lapse");
        au::ClearMirrored();
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
