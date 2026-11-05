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
    // Round 6 (docs/world-sync-design.md 7.19): a moving puppet's unchanged request is re-applied every 1 s (runs adecad01 /
    // 80cc0176: with 5 s the walkers lagged 5-12 m, the catch-up ran only then); a still one every 5 s; a first or changed
    // request at once. The owner copy moves when its order-part position is >= 0.3 m from where it was at the last apply.
    // Run fbf3522a: guard 15's puppet kept his own brain's order target (413.5, 267.6, 2133.3; mode 1) under the host's
    // order without a target (wire +0x0e 0) and walked to it, away from the request's destination: such an order now
    // clears the record's target (+0x00..+0x0b, the mode bits of +0x0c), as the owner's record of it is.
    {
        const unsigned char none[0x18] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x1f, 0, 0, 0, 0};
        unsigned char pos[0x18]{};
        std::memcpy(pos, none, sizeof(pos));
        pos[0xe] = 1;
        Check(!sa::OrderHasTarget(none) && sa::OrderHasTarget(pos), "an order names a target in packed modes 1..4 only");
        pos[0xe] = 4;
        Check(sa::OrderHasTarget(pos), "mode 4 (an id) is a target");
        // The guest's record of guard 15 in fbf3522a (census o=): x, y, z floats, +0x0c mode 1, more bytes.
        unsigned char rec[0x20] = {0xec, 0xbe, 0xce, 0x43, 0x31, 0xc6, 0x85, 0x43, 0xae, 0x54, 0x05, 0x45, 0x01, 0, 0, 0,
                                   0, 0, 0, 0, 0x01, 0, 0, 0, 0, 0x01, 0, 0x1b, 0x1f, 0, 0, 0};
        Check(sa::ClearOrderTarget(rec), "a stale target is cleared");
        bool zero = true;
        for (int b = 0; b < 0xc; ++b) zero = zero && rec[b] == 0;
        Check(zero && (rec[0xc] & 7) == 0 && rec[0x14] == 0x01 && rec[0x19] == 0x01 && rec[0x1b] == 0x1b && rec[0x1c] == 0x1f,
              "target and mode cleared, the other bytes kept");
        Check(!sa::ClearOrderTarget(rec), "nothing to clear the second time");
    }
    // Runs fbf3522a / f11fe8a1: a puppet keeps walking his own pre-take leg. Guard 15: the host copy walked to (363.6,
    // 2159.8), this copy's own brain to (414.9, 2132.8); a walker is taken only on the same leg (kActLegM 3 m, flat).
    {
        unsigned char req[0x70]{};
        const float hostDest[3] = {363.64f, 270.96f, 2159.78f};
        const std::uint64_t packed = sa::EncodePacked(hostDest);
        std::memcpy(req + 0x40, &packed, sizeof(packed));
        req[0x63] = 1;  // action A kind 1: a move
        const float other[3] = {414.94f, 268.0f, 2132.75f}, same[3] = {362.0f, 275.0f, 2161.5f}, far3[3] = {367.0f, 271.0f, 2159.78f};
        Check(!sa::LegMatch(req, other, true) && sa::LegMatch(req, same, true) && !sa::LegMatch(req, far3, true),
              "a walker on another leg is not taken; the same leg (within 3 m flat, any height) is");
        Check(!sa::LegMatch(req, same, false), "a walker whose own destination is unknown is not taken");
        req[0x63] = 17;  // not a move (standing at a point): the leg does not matter
        Check(sa::LegMatch(req, other, true) && sa::LegMatch(req, other, false), "a guard who does not move is taken as before");
    }
    {
        Check(sa::kActReapplyMovingMs == 1000 && sa::kActReapplyMs == 5000, "re-apply: 1 s moving, 5 s still");
        Check(sa::ReapplyDue(true, false, false, 0) && sa::ReapplyDue(false, true, false, 0), "re-apply: first / changed at once");
        Check(!sa::ReapplyDue(false, false, true, 999) && sa::ReapplyDue(false, false, true, 1000), "re-apply: moving every 1 s");
        Check(!sa::ReapplyDue(false, false, false, 4999) && sa::ReapplyDue(false, false, false, 5000), "re-apply: still every 5 s");
        const float o0[3] = {10.0f, 2.0f, -5.0f}, o1[3] = {10.2f, 2.0f, -4.8f}, o2[3] = {10.0f, 2.0f, -4.6f}, o3[3] = {10.0f, 2.35f, -5.0f};
        Check(!sa::OwnerMoved(o0, o1) && sa::OwnerMoved(o0, o2) && sa::OwnerMoved(o0, o3), "owner moved: >= 0.3 m (3D)");
        // (4) the phase lever travels in the ACT_V1 p reply (no CFG bit): pl=1 calm / caution only.
        char p[40]{};
        Check(sa::FormatPlayerReply(1, p, sizeof(p)) && std::strcmp(p, "return {p=1,pl=1}") == 0 && sa::FormatPlayerReply(0, p, sizeof(p)) &&
                  std::strcmp(p, "return {p=1,pl=0}") == 0, "the p reply carries the phase limit");
        // (3) the frame line prints the aim-swap counter payload_ids (RemapAimTarget) and the apply accounting.
        sa::Counters k{};
        k.payload_ids = 5; k.due = 9; k.applied = 8; k.reapply_moving = 4; k.frames = 61; k.frame_p95_us = 18950;
        char line[512]{};
        Check(sa::FormatFrameLine(123, k, line, sizeof(line)) && std::strstr(line, "soldier_act_frame; t=123; frames=61; p95_ms=18.95;") &&
                  std::strstr(line, "; payload_ids=5;") && std::strstr(line, "; due=9;") && std::strstr(line, "; reapply=4") &&
                  std::strstr(line, "; applied=8;"), "the frame line prints payload_ids, due and reapply");
        char tiny[16]{};
        Check(!sa::FormatFrameLine(1, k, tiny, sizeof(tiny)), "the frame line refuses a short buffer");
    }
    // Enemy round 8 track C (design 7.23, act_plan): the walk-request start 0x1413725f0 plans a path only for a FOB remote
    // copy (S+0x2a4 bit 2, owner byte != S+0x2a8); offline it plans nothing and a puppet's legs follow his own brain's
    // old path. The hook re-calls it after the engine's own start with a shadow gate object.
    {
        Check(sa::PlanGate(1, 0xffff, 0x0002, true) && sa::PlanGate(1, 0xffff, 0x0042, true),
              "plan: dest mode 1, no handle, the offline start ran (R+0x19e & 3 == 2), applied now");
        Check(!sa::PlanGate(0, 0xffff, 0x0002, true) && !sa::PlanGate(2, 0xffff, 0x0002, true), "plan: modes 0 / 2 not planned");
        Check(!sa::PlanGate(1, 0x0012, 0x0002, true), "plan: a handle already out (FOB life cycle) is left alone");
        Check(!sa::PlanGate(1, 0xffff, 0x0003, true) && !sa::PlanGate(1, 0xffff, 0x0001, true) && !sa::PlanGate(1, 0xffff, 0x0000, true),
              "plan: only after the offline start (low bits 2)");
        Check(!sa::PlanGate(1, 0xffff, 0x0002, false), "plan: only for a request applied this update");
        // The shadow: 0x1413725f0 reads its first argument only at +0x70 (0x141372635) -> S' +0x2a4 bit 2, +0x18 -> +0x18
        // the owner bytes [idx] against +0x2a8 (0x14137264a..0x141372664).
        static sa::Shadow sh{};
        sa::BuildShadow(sh);
        Check(sa::ShadowGatePasses(sh.p, 0) && sa::ShadowGatePasses(sh.p, 246) && sa::ShadowGatePasses(sh.p, sa::kMaxSoldiers - 1),
              "the shadow passes the gate for every guard");
        std::uintptr_t s = 0;
        std::memcpy(&s, sh.p + 0x70, sizeof(s));
        std::uint32_t mode = 0;
        if (s) std::memcpy(&mode, reinterpret_cast<const unsigned char*>(s) + 0x2a4, sizeof(mode));
        Check(s == reinterpret_cast<std::uintptr_t>(sh.s) && mode == 4 && sh.s[0x2a8] == 0, "shadow layout: +0x70 -> S', +0x2a4 = 4, +0x2a8 = 0");
        // A real offline S (census mode=0x782: bit 2 clear) does not pass; an online owner (owner byte == peer) does not.
        alignas(16) static unsigned char p[0x78]{}, so[0x2b0]{}, slots[0x20]{}, owner[sa::kMaxSoldiers]{};
        const std::uintptr_t ps = reinterpret_cast<std::uintptr_t>(so), pslots = reinterpret_cast<std::uintptr_t>(slots),
                             powner = reinterpret_cast<std::uintptr_t>(owner);
        std::memcpy(p + 0x70, &ps, 8);
        std::memcpy(so + 0x18, &pslots, 8);
        std::memcpy(slots + 0x18, &powner, 8);
        const std::uint32_t offline = 0x782, online = 0x786;
        std::memcpy(so + 0x2a4, &offline, 4);
        Check(!sa::ShadowGatePasses(p, 5), "offline (bit 2 clear): the gate refuses (finding 1)");
        std::memcpy(so + 0x2a4, &online, 4);
        so[0x2a8] = 1;
        owner[5] = 1;
        Check(!sa::ShadowGatePasses(p, 5), "online, this peer owns him: the gate refuses");
        owner[5] = 0;
        Check(sa::ShadowGatePasses(p, 5), "online, a remote copy: the gate plans");
        Check(!sa::ShadowGatePasses(nullptr, 5) && !sa::ShadowGatePasses(sh.p, sa::kMaxSoldiers), "no object / idx out of range: refused");
    }
    {
        // The gait catch-up, as the unpack's mode 0 rule (2..5 m: + 1 capped at 3; beyond 5 m: 3), never lowered.
        Check(sa::GaitCatchUp(0.0f, 1) == 1 && sa::GaitCatchUp(1.9f, 1) == 1, "gait: under 2 m ahead unchanged");
        Check(sa::GaitCatchUp(2.5f, 1) == 2 && sa::GaitCatchUp(4.9f, 2) == 3 && sa::GaitCatchUp(3.0f, 3) == 3, "gait: 2..5 m + 1, cap 3");
        Check(sa::GaitCatchUp(5.1f, 0) == 3 && sa::GaitCatchUp(12.0f, 1) == 3, "gait: beyond 5 m 3");
        Check(sa::GaitCatchUp(8.0f, 5) == 5 && sa::GaitCatchUp(3.0f, 4) == 4, "gait: a faster gait is kept");
        Check(sa::GaitCatchUp(-1.0f, 1) == 1 && sa::GaitCatchUp(std::nanf(""), 1) == 1, "gait: not ahead / NaN unchanged");
        const float self[3] = {0.0f, 0.0f, 0.0f}, dest[3] = {20.0f, 0.0f, 0.0f}, ahead[3] = {3.0f, 5.0f, 4.0f}, behind[3] = {-3.0f, 0.0f, 4.0f};
        Check(std::fabs(sa::AheadM(self, ahead, dest) - 5.0f) < 0.01f, "ahead: flat distance when the owner copy is nearer the destination");
        Check(sa::AheadM(self, behind, dest) == 0.0f, "ahead: an owner copy behind is not ahead");
        // The entry yaw: face mode 1 (wire +0x68) -> the facing (wire +0x50, big-endian int16 * pi/32767, 0x142338970).
        unsigned char w[0x70]{};
        w[0x68] = 1;
        w[0x50] = 0x40;
        w[0x51] = 0x00;  // 16384 -> ~pi/2
        Check(std::fabs(sa::EntryYaw(w, 0.25f) - 1.5708f) < 0.001f, "entry yaw: face mode 1 -> the request's facing");
        w[0x50] = 0xc0;  // -16384 -> ~-pi/2
        Check(std::fabs(sa::EntryYaw(w, 0.25f) + 1.5708f) < 0.001f, "entry yaw: negative facing");
        w[0x68] = 2;
        Check(sa::EntryYaw(w, 0.25f) == 0.25f, "entry yaw: other face modes keep this copy's own yaw");
        // A walker is taken only within 10 m (flat) of the owner copy (a take does not open with a visible jump).
        unsigned char req[0x70]{};
        req[0x63] = 1;
        const float a[3] = {0.0f, 0.0f, 0.0f}, near9[3] = {6.0f, 30.0f, 6.7f}, far11[3] = {11.0f, 0.0f, 0.0f};
        Check(sa::TakeNear(req, a, near9, true) && !sa::TakeNear(req, a, far11, true) && !sa::TakeNear(req, a, near9, false),
              "take: a walker within 10 m (flat) of the owner copy only");
        req[0x63] = 17;
        Check(sa::TakeNear(req, a, far11, true) && sa::TakeNear(req, a, far11, false), "take: a guard who does not walk as before");
        // warp15: the unpack's 15 m warp (225.0 squared, 3D) moves the copy.
        const float e0[3] = {0.0f, 0.0f, 0.0f}, e15[3] = {10.0f, 10.0f, 6.0f}, e14[3] = {10.0f, 10.0f, 1.0f};
        const float e15x[3] = {10.0f, 10.0f, 5.0f};  // exactly 15 m: the engine tests 225.0 < d squared
        Check(sa::Warp15(e0, e15) && !sa::Warp15(e0, e14) && !sa::Warp15(e0, e15x), "warp15: beyond 15 m (3D)");
        const float d0[3] = {100.0f, 270.0f, 200.0f}, end1[3] = {100.6f, 270.5f, 200.6f}, end2[3] = {101.0f, 270.0f, 200.8f};
        Check(sa::PlanEndNear(end1, d0) && !sa::PlanEndNear(end2, d0), "plan end: within 1 m (3D) of R+0x80");
        Check(sa::kActTakeNearM == 10.0f && sa::kPlanEndM == 1.0f && sa::kPlanCheckMs == 300, "act_plan constants");
        // The frame line prints the act_plan counters.
        sa::Counters k{};
        k.plan = 7; k.plan_skip = 2; k.warp15 = 1; k.plan_ok = 6; k.plan_far = 1; k.plan_nopath = 0; k.near_wait = 3;
        char line[768]{};
        Check(sa::FormatFrameLine(9, k, line, sizeof(line)) &&
                  std::strstr(line, "; plan=7; plan_skip=2; warp15=1; plan_ok=6; plan_far=1; plan_nopath=0; near_wait=3"),
              "the frame line prints plan / plan_skip / warp15 / plan_ok / plan_far / plan_nopath / near_wait");
    }
    // The planner sites pinned in the contract (round 8 track C).
    Check(c::plan_start_rva == 0x13725f0 && c::plan_dist == 10.0f && c::s_planner == 0x30 && c::planner_follower == 0x20 &&
              c::planner_plan == 0x0 && c::planner_cancel == 0x10 && c::follower_has == 0x90 && c::follower_path == 0xa0 &&
              c::follower_reset == 0xd0 && c::r_handle == 0x19c && c::r_198 == 0x198 && c::r_dest == 0x80 && c::path_points == 0x8 &&
              c::path_count == 0x20,
          "the planner contract");
    // Session 1 (run fcfb3acc: the planner's vtable 0x23a1610; read-only decompile of its vt[0] 0x140fa2430): the per-guard
    // slot *(planner+0x48)+idx*0x10 {u48 type hash, u16 handle, u16 counter, u16 done}; vt[0] refuses (0xffff) a request
    // whose type hash differs from a non-zero slot hash, so a slot his own brain's walk holds is cancelled first (vt[0x10]
    // with that handle, the engine's own cancel), and a slot of ours is cancelled when he is released.
    {
        Check(sa::kMoveHash == 0x2c505bab2efcull, "the request manager's move type hash (0x141372694)");
        Check(sa::PlanSlot(0, 0xffff) == sa::SlotStep::plan && sa::PlanSlot(0x2c505bab2efcull, 0x0012) == sa::SlotStep::plan &&
                  sa::PlanSlot(0xabcd2c505bab2efcull, 0x0003) == sa::SlotStep::plan,
              "slot: free or ours (low 48 bits) -> plan");
        Check(sa::PlanSlot(0x1234567890abull, 0x0007) == sa::SlotStep::cancel_then_plan, "slot: another type with a live handle -> cancel it, then plan");
        Check(sa::PlanSlot(0x1234567890abull, 0xffff) == sa::SlotStep::busy, "slot: another type, no handle -> left alone (busy)");
        Check(sa::ReleaseCancel(0x2c505bab2efcull, 0x0012, 0x0012) && !sa::ReleaseCancel(0x2c505bab2efcull, 0x0012, 0x0013) &&
                  !sa::ReleaseCancel(0x1234567890abull, 0x0012, 0x0012) && !sa::ReleaseCancel(0x2c505bab2efcull, 0xffff, 0xffff),
              "release: cancel only our live handle that R+0x19c still holds");
        sa::Counters k{};
        k.plan_cancel = 4; k.plan_busy = 1; k.plan_none = 2; k.plan_release = 3;
        char line[900]{};
        Check(sa::FormatFrameLine(9, k, line, sizeof(line)) && std::strstr(line, "; plan_cancel=4; plan_busy=1; plan_none=2; plan_release=3"),
              "the frame line prints plan_cancel / plan_busy / plan_none / plan_release");
        Check(c::planner_vtable_rva == 0x23a1610 && c::planner_slots == 0x48 && c::planner_records == 0x40 && c::planner_record_stride == 0xf0 &&
                  c::planner_valid == 0x18,
              "the planner layout");
    }
    // Enemy round 9 track C (act_plan, design 7.26): plan once per leg; the catch-up from 1 m (run 226fc789: 98 plans for
    // guard 18, one per 1.01 s; the puppet trailed the host copy by about 1 s, p50 1.31 m).
    {
        Check(sa::kPlanCatchM == 1.0f && sa::kPlanMoveM == 1.0f, "round 9 C constants: the catch-up from 1 m, a walk's destination held within 1 m");
        Check(sa::GaitCatchUp(1.5f, 1) == 2 && sa::GaitCatchUp(1.01f, 2) == 3 && sa::GaitCatchUp(0.9f, 1) == 1 && sa::GaitCatchUp(1.0f, 1) == 1,
              "gait: the catch-up starts beyond 1 m");
        unsigned char a[0x70]{}, b[0x70]{};
        a[0x61] = b[0x61] = 1;  // dest mode 1: a walk
        a[0x63] = b[0x63] = 1;
        const float d0[3] = {397.66f, 270.22f, 2211.80f}, dn[3] = {397.66f, 270.22f, 2212.50f}, df[3] = {397.66f, 270.22f, 2213.00f};
        const std::uint64_t v0 = sa::EncodePacked(d0), vn = sa::EncodePacked(dn), vf = sa::EncodePacked(df);
        std::memcpy(a + 0x40, &v0, 8);
        std::memcpy(b + 0x40, &v0, 8);
        Check(!sa::RequestChangedFar(a, b), "far change: the same request is not one");
        std::memcpy(b + 0x40, &vn, 8);
        Check(sa::RequestChanged(a, b) && !sa::RequestChangedFar(a, b), "far change: a walk's destination 0.7 m off is held");
        std::memcpy(b + 0x40, &vf, 8);
        Check(sa::RequestChangedFar(a, b), "far change: 1.2 m is a change");
        std::memcpy(b + 0x40, &vn, 8);
        b[0x05] = 3;
        Check(sa::RequestChangedFar(a, b), "far change: another payload byte is a change");
        b[0x05] = 0;
        a[0x61] = b[0x61] = 0;
        Check(sa::RequestChangedFar(a, b), "far change: not a walk -> as RequestChanged (0.7 m is a change)");
        Check(sa::PlanLiveFor(0x0007, 0x0007, 0x2c505bab2efcull, 0x0007, false), "plan live: our handle in R+0x19c and in his slot");
        Check(!sa::PlanLiveFor(0xffff, 0xffff, 0x2c505bab2efcull, 0xffff, false) && !sa::PlanLiveFor(0x0007, 0x0008, 0x2c505bab2efcull, 0x0007, false) &&
                  !sa::PlanLiveFor(0x0007, 0x0007, 0x1234567890abull, 0x0007, false) && !sa::PlanLiveFor(0x0007, 0x0007, 0x2c505bab2efcull, 0x0009, false) &&
                  !sa::PlanLiveFor(0x0007, 0x0007, 0x2c505bab2efcull, 0x0007, true),
              "plan live: none / R+0x19c another / another type / slot another handle / path end far or missing");
        Check(sa::PlanHold(false, false, true, false, 1, 1), "hold: an unchanged walk with a live plan and no gait change");
        Check(!sa::PlanHold(true, false, true, false, 1, 1) && !sa::PlanHold(false, true, true, false, 1, 1) &&
                  !sa::PlanHold(false, false, false, false, 1, 1) && !sa::PlanHold(false, false, true, true, 1, 1) &&
                  !sa::PlanHold(false, false, true, false, 1, 2) && !sa::PlanHold(false, false, true, false, 2, 1),
              "hold: not a first apply, a far change, no live plan, a warp, or a gait change");
        sa::Counters k{};
        k.plan_hold = 12;
        char line[960]{};
        Check(sa::FormatFrameLine(9, k, line, sizeof(line)) && std::strstr(line, "; plan_release=0; plan_hold=12\r\n"),
              "the frame line prints plan_hold");
    }
    // Enemy round 8 track A (quiet_place, docs/world-sync-design.md 7.22): the quiet warp 0x136ece0 moves a copy only beyond
    // 15 m of its slot position; a nearer correction biases the slot 20 m along x first (the function overwrites it).
    {
        const float slot[3] = {363.6f, 270.0f, 2159.8f}, near[3] = {370.0f, 270.0f, 2160.0f}, far1[3] = {413.4f, 268.0f, 2133.6f};
        float out[3]{};
        Check(sa::QuietBias(slot, near, out) && out[0] == near[0] + 20.0f && out[1] == near[1] && out[2] == near[2],
              "quiet bias: a correction within 15 m biases the slot");
        Check(!sa::QuietBias(slot, far1, out), "quiet bias: beyond 15 m the warp moves him as it is");
        const float edge[3] = {363.6f + 15.0f, 270.0f, 2159.8f};
        Check(sa::QuietBias(slot, edge, out), "quiet bias: at 15.0 m (225.0 is not > 225.0) biases");
        char line[256]{};
        Check(sa::FormatQuietLine(1270, 6, 1, 1, 0.12f, -0.03f, line, sizeof(line)) &&
                  std::strcmp(line, "world_place=quiet; go=1270; seq=6; moved=1; bias=1; err_m=0.12; dy_m=-0.03\r\n") == 0,
              "the quiet line: the read-back after the move");
        char tiny[8]{};
        Check(!sa::FormatQuietLine(1270, 6, 1, 1, 0.0f, 0.0f, tiny, sizeof(tiny)), "the quiet line refuses a short buffer");
        Check(c::quiet_warp_rva == 0x136ece0, "the quiet warp is the pinned network-position warp");
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
