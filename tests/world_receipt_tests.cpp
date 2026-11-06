// World sync receipts and the lab world.trigger grammar
// (docs/world-sync-protocol.md "Receipts", "world.trigger"): exact
// classification, strict integers, reply widths, the LOG sanitizer.
#include "mgscoop/world_receipt.hpp"
#include "mgscoop/coop_reason_text.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
namespace world = mgscoop::world;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}
world::Receipt Kind(const std::string& s) { return world::Classify(s.data(), s.size(), s.c_str()); }
world::TxStatus Tx(const std::string& args, std::uint8_t& code, std::int64_t (&v)[world::kMaxValues], unsigned& n) {
    const std::string s = std::string(world::tx_prefix) + args;
    return world::ParseTx(s.data(), s.size(), code, v, n);
}
world::TxStatus Tx(const std::string& args) {
    std::uint8_t code = 0;
    std::int64_t v[world::kMaxValues]{};
    unsigned n = 0;
    return Tx(args, code, v, n);
}
bool Trigger(const std::string& text, world::Trigger& out, std::string& canonical, std::string& reason) {
    char buffer[300]{};
    const char* why = nullptr;
    const bool ok = world::ParseTrigger(text.data(), text.size(), out, buffer, sizeof(buffer), why);
    canonical = buffer;
    reason = why ? why : "";
    return ok;
}

void Classification() {
    Check(Kind("return 'MGS_COOP_WORLD_CFG_V1' -- 30050 0") == world::Receipt::cfg, "cfg");
    Check(Kind("return 'MGS_COOP_WORLD_TX_V1' -- 1 30050 1 2 3 4") == world::Receipt::tx, "tx");
    Check(Kind("return 'MGS_COOP_WORLD_RX_V1' -- 3") == world::Receipt::rx, "rx");
    Check(Kind("return 'MGS_COOP_WORLD_LOG_V1' -- event=started") == world::Receipt::log, "log");
    Check(Kind("return 'MGS_COOP_WORLD_WARP_V1' -- 1 1116") == world::Receipt::warp, "warp");
    Check(Kind("return 'MGS_COOP_DEBUG_WARP_V1' -- 1 1116 0 0 0 0") == world::Receipt::none, "the host menu warp is not a world receipt");
    Check(Kind("return 'MGS_COOP_WORLD_CFG_V1'") == world::Receipt::none, "a prefix without args");
    Check(Kind("return 'MGS_COOP_WORLD_XX_V1' -- 1") == world::Receipt::none, "unknown receipt");
    Check(Kind("return 'MGS_COOP_PARTNER_V1' -- 1") == world::Receipt::none, "partner receipts are not world");
    const std::string ok = "return 'MGS_COOP_WORLD_RX_V1' -- 3";
    Check(world::Classify(ok.data(), ok.size(), "return 'MGS_COOP_WORLD_RX_V1' -- 1") == world::Receipt::none, "name != source");
    Check(world::Classify(ok.data(), ok.size(), (ok + "x").c_str()) == world::Receipt::none, "a longer name");
    std::string nul = ok;
    nul[10] = '\0';
    Check(world::Classify(nul.data(), nul.size(), nul.c_str()) == world::Receipt::none, "NUL inside");
    std::string big = "return 'MGS_COOP_WORLD_LOG_V1' -- " + std::string(240 - 34, 'a');
    Check(big.size() == 240 && Kind(big) == world::Receipt::log, "240 bytes");
    big += "a";
    Check(Kind(big) == world::Receipt::none, "241 bytes refused");
    Check(world::Classify(nullptr, 3, "abc") == world::Receipt::none && world::Classify("abc", 3, nullptr) == world::Receipt::none,
          "null inputs");
    // Enemy follow: PEER, the partner's remote pose (exact text, no arguments).
    Check(Kind("return 'MGS_COOP_WORLD_PEER_V1'") == world::Receipt::peer, "peer");
    Check(Kind("return 'MGS_COOP_WORLD_PEER_V1' -- 1") == world::Receipt::none && Kind("return 'MGS_COOP_WORLD_PEER_V1' ") ==
          world::Receipt::none, "peer takes no arguments");
    char peer[64];
    Check(world::FormatPeerReply(true, 12.3456f, -2001.5f, 120, peer, sizeof(peer)) && std::strcmp(peer, "return 1,12346,-2001500,120") == 0,
          "peer reply: x and z in mm and the age");
    Check(world::FormatPeerReply(false, 1.0f, 1.0f, 0, peer, sizeof(peer)) && std::strcmp(peer, "return 0") == 0, "unknown: return 0");
    Check(world::FormatPeerReply(true, 1.0f, 1.0f, 2001, peer, sizeof(peer)) && std::strcmp(peer, "return 0") == 0, "older than 2 s: 0");
    Check(world::FormatPeerReply(true, NAN, 1.0f, 0, peer, sizeof(peer)) && std::strcmp(peer, "return 0") == 0 &&
          world::FormatPeerReply(true, 100001.0f, 1.0f, 0, peer, sizeof(peer)) && std::strcmp(peer, "return 0") == 0, "bounds");
    char tiny_peer[6];
    Check(!world::FormatPeerReply(true, 1.0f, 1.0f, 0, tiny_peer, sizeof(tiny_peer)), "capacity");
}

void Cfg() {
    std::uint32_t mission = 0;
    std::uint8_t phase = 9;
    const auto parse = [&](const std::string& args) {
        const std::string s = std::string(world::cfg_prefix) + args;
        return world::ParseCfg(s.data(), s.size(), mission, phase);
    };
    Check(parse("30050 0") && mission == 30050 && phase == 0, "query");
    Check(parse("30010 1") && mission == 30010 && phase == 1, "start");
    Check(parse("0 1") && mission == 0, "mission 0");
    Check(!parse("30050 2") && !parse("30050") && !parse("30050  1") && !parse("030050 1") && !parse("100000 1") && !parse("30050 1 ") &&
              !parse("-1 1") && !parse("30050 -0") && !parse("3005.0 1") && !parse("+30050 1"),
          "strict CFG");
    world::CfgReply r{3, 2, 1, 1, 65535, 15, 1, 4294967295u};
    char out[world::reply_capacity];
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,4294967295,0,0,0,0,0,0,0,0,0,0,0") == 0,
          "CFG first seven fields preserved, unsigned session appended, then remote mission, remote level, wait, handoff, lock and census");
    r.link = 0; r.session = 0;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,0,65535,15,1,0,0,0,0,0,0,0,0,0,0,0,0") == 0, "disconnected CFG session zero");
    r.link = 1; r.session = 7; r.remote_mission = 30010; r.remote_level = 3; r.wait = 9;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,0,0,0,0,0,0,0,0") == 0,
          "the partner's mission, its world level and the raw pair-wait code are r[9..11]");
    // Save handoff (join-fix phase 2 P2-1): r[12] the handoff code, r[13] the travel lock.
    r.handoff = 3; r.lock = 1;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,0,0,0,0,0,0") == 0,
          "the handoff code and the travel lock are r[12..13]");
    // WS4 step 0 (lab key partner_placed_census): r[14] the attacker census flag.
    r.census = 1;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,1,0,0,0,0,0") == 0,
          "the WS4 attacker census flag is r[14]");
    // 0.13.6 online-vehicle-gone (lab key world_vehicle_gone, lab and play): r[15] the VEHICLE_GONE module.
    r.vehicle_gone = 1;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,1,1,0,0,0,0") == 0,
          "the VEHICLE_GONE module flag is r[15]");
    // M4 (lab key world_vehicle_state): r[15] is the vehicle modules bitmask, bit 2 VEHICLE_STATE.
    r.vehicle_gone = 2;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,1,2,0,0,0,0") == 0,
          "VEHICLE_STATE alone is r[15] = 2");
    r.vehicle_gone = 3;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,1,3,0,0,0,0") == 0,
          "both vehicle modules are r[15] = 3");
    // M4 review: bit 4 the wreck parity key (world_vehicle_wreck); world_link::HandleCfg composes r[15] with
    // world::VehicleModules (gone, state, wreck).
    Check(world::VehicleModules(false, false, false) == 0 && world::VehicleModules(true, false, false) == 1 &&
              world::VehicleModules(false, true, false) == 2 && world::VehicleModules(true, true, false) == 3 &&
              world::VehicleModules(false, false, true) == 4 && world::VehicleModules(true, true, true) == 7,
          "r[15] bits: 1 VEHICLE_GONE, 2 VEHICLE_STATE, 4 the wreck parity");
    r.vehicle_gone = world::VehicleModules(true, true, true);
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,1,7,0,0,0,0") == 0,
          "both vehicle modules and the wreck key are r[15] = 7");
    // The user's vehicle decisions of 2026-10-29: bit 8 D12 world_vehicle_break, bit 16 D14 world_vehicle_credit, bit 32
    // D11 world_loadout_block (lab and play); the three older bits keep their meaning.
    Check(world::VehicleModules(false, false, false, true, false, false) == 8 &&
              world::VehicleModules(false, false, false, false, true, false) == 16 &&
              world::VehicleModules(false, false, false, false, false, true) == 32 &&
              world::VehicleModules(true, true, false, true, true, true) == 59 && world::VehicleModules(true, true, true, true, true, true) == 63,
          "r[15] bits: 8 the guest's break request, 16 the receiver's Fulton credit, 32 the guest's LOADOUT block");
    r.vehicle_gone = world::VehicleModules(true, true, true, true, true, true);
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,1,63,0,0,0,0") == 0,
          "every vehicle key is r[15] = 63");
    // The vehicle fixes of 2026-10-29: bit 64 D12 world_vehicle_crew, bit 128 world_vehicle_damage (lab and play).
    Check(world::VehicleModules(false, false, false, false, false, false, true, false) == 64 &&
              world::VehicleModules(false, false, false, false, false, false, false, true) == 128 &&
              world::VehicleModules(true, true, false, true, false, false, true, true) == 203,
          "r[15] bits: 64 the crewed vehicle wrecked at once, 128 the guest's vehicle damage report");
    r.vehicle_gone = world::VehicleModules(true, true, true, true, true, true, true, true);
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,1,255,0,0,0,0") == 0,
          "every vehicle key, the fixes too, is r[15] = 255");
    r.vehicle_gone = world::VehicleModules(true, true, true);
    // M1 host-owned companion (lab key companion_host_owned, lab and play): r[16] the companion module.
    r.companion = 1;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,1,7,1,0,0,0") == 0,
          "the host-owned companion module flag is r[16]");
    // M3 sortie vehicle (lab key world_relief, lab and play): r[17] the relief module.
    r.relief = 1;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,1,7,1,1,0,0") == 0,
          "the sortie vehicle module flag is r[17]");
    // r1101 fulton (lab key fulton_sync, lab and play): r[18] the Fulton lift and FULTON 0x46 (world_enemy.lua W.fulton).
    r.fulton = 1;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,1,7,1,1,1,0") == 0,
          "the Fulton sync flag is r[18]");
    // Review fixes of 2026-11-01: r[18] carries the fulton bits as is (2 fulton_early, 4 fulton_look).
    r.fulton = 7;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,1,7,1,1,7,0") == 0,
          "the fulton bits are r[18]");
    // r1101 Prove: bit 8 fulton_one_save (the receiver's soldier credit skipped) rides r[18] as is too.
    r.fulton = 15;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,1,7,1,1,15,0") == 0,
          "the fulton bits with fulton_one_save are r[18]");
    // r1101 land: bit 16 fulton_guest_vehicle (the guest's lift of a host's vehicle Fulton) rides r[18] as is too.
    r.fulton = 31;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,1,7,1,1,31,0") == 0,
          "the fulton bits with fulton_guest_vehicle are r[18]");
    // Enemy follow (lab key soldier_follow, lab and play; docs/world-sync-design.md 7.8): r[19] the follow scan and place-first.
    r.follow = 1;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,1,7,1,1,31,1") == 0,
          "the enemy follow flag is r[19]");
    // The enemy sync plan's M3..M7 (soldier_owner .. soldier_puppet; docs/world-sync-design.md 7.9+): r[19] bits 2..32.
    r.follow = 63;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,1,7,1,1,31,63") == 0,
          "the enemy sync bits ride r[19] as is");
    // M7 step 1 (soldier_perceive): its bit survives the configured bits (run 96216259: the old 0x7E mask dropped it).
    // The wp/enemy-gaps merge: place_lift keeps bit 128 (its first key), soldier_perceive moves to bit 256, so r[19]
    // carries 9 bits (0..511).
    Check(world::EnemySyncBits(0x1FF) == 0x1FE && world::EnemySyncBits(256) == 256 && world::EnemySyncBits(128) == 128 &&
              world::EnemySyncBits(64 | 1) == 64 && world::EnemySyncBits(0x200 | 256) == 0x300 &&
              world::EnemySyncBits(0x200) == 0x200 && world::EnemySyncBits(0xFFF) == 0xFFE && world::EnemySyncBits(0x1000) == 0,
          "the enemy sync bits keep 2..256, never bit 1");
    r.follow = 256 | 128 | 1;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,1,7,1,1,31,385") == 0,
          "place_lift (128) and soldier_perceive (256) ride r[19] side by side");
    r.remote_mission = 4294967295u; r.remote_level = 255; r.wait = 255; r.handoff = 255; r.lock = 255; r.census = 255; r.vehicle_gone = 255; r.companion = 255;
    r.relief = 255; r.fulton = 255; r.follow = 511;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,4294967295,255,255,255,255,255,255,255,255,255,511") == 0 &&
              std::strlen(out) < world::reply_capacity,
          "the widest CFG reply fits");
    r.handoff = 0; r.lock = 0; r.census = 0; r.vehicle_gone = 0; r.companion = 0; r.relief = 0; r.fulton = 0;
    char tiny[16];
    Check(!world::FormatCfgReply(r, tiny, sizeof(tiny)) && tiny[0] == 0, "CFG insufficient buffer produces no partial reply");
}

// On-screen world notes: "<code> <a> <b>", code 1..15, a and b u32, no sign.
void Note() {
    Check(Kind("return 'MGS_COOP_WORLD_NOTE_V1' -- 1 40010 1") == world::Receipt::note, "note");
    const std::string ok = "return 'MGS_COOP_WORLD_NOTE_V1' -- 2 30010 0";
    Check(world::Classify(ok.data(), ok.size(), "return 'MGS_COOP_WORLD_NOTE_V1' -- 2 30010 1") == world::Receipt::none,
          "note name != source");
    Check(Kind("return 'MGS_COOP_WORLD_NOTE_V1' -- ") == world::Receipt::none, "a note prefix without args");
    std::uint8_t code = 0;
    std::uint32_t a = 0, b = 0;
    const auto parse = [&](const char* tail) {
        const std::string s = std::string(world::note_prefix) + tail;
        return world::ParseNote(s.data(), s.size(), code, a, b);
    };
    Check(parse("1 40010 1") && code == 1 && a == 40010 && b == 1, "PROGRESS story start");
    Check(parse("4 0 0") && code == 4 && a == 0 && b == 0, "zeros");
    Check(parse("15 4294967295 4294967295") && code == 15 && a == 4294967295u && b == 4294967295u, "bounds");
    code = 9; a = 9; b = 9;
    Check(!parse("0 1 1") && !parse("16 1 1") && !parse("-1 1 1") && !parse("01 1 1") && !parse("1  1 1") && !parse("1 1") &&
              !parse("1 1 1 ") && !parse("1 4294967296 0") && !parse("1 0 4294967296") && !parse("1 -1 0") && !parse("1 0 -1") &&
              !parse("1 1 1 1") && !parse("+1 1 1") && !parse("1 1.0 1") && !parse(" 1 1 1") && !parse(""),
          "strict NOTE grammar");
    Check(code == 9 && a == 9 && b == 9, "outputs untouched on failure");
    // fb1031 travel-1: note 14 (TRAVEL) is shown and logged with its values like 1..13; fb1031 mbgo: 15 (MISSION_END)
    // too; 18 ARMS (Friendly fire) and 19 HINT (arrival-prevent) as well. 16 and 17 are posted by native only.
    Check(world::NoteShown(1) && world::NoteShown(13) && world::NoteShown(14) && world::NoteShown(15) && !world::NoteShown(0) &&
              !world::NoteShown(16) && !world::NoteShown(17) && world::NoteShown(18) && world::NoteShown(19) && world::NoteShown(20) &&
              world::NoteShown(21) && !world::NoteShown(22) && !world::NoteShown(23),
          "HandleNote shows notes 1..15 (14 TRAVEL, 15 MISSION_END), 18, 19, 20 (VSPAWN) and 21 (SUPPLY), never 0, 16, 17, 22 or 23");
    Check(parse("15 3 0") && code == 15 && a == 3 && b == 0, "NOTE 15 parses");
    Check(parse("19 30050 1") && code == 19 && a == 30050 && b == 1 && !parse("17 1 0") && !parse("23 1 1"), "NOTE 19 parses, 17 and 23 do not");
    // Debug menu Spawn vehicle (lua/coop/debug_vspawn.lua): note 20 a=1 spawned for you | 2 your partner spawned | 3 refused.
    Check(parse("20 3 8") && code == 20 && a == 3 && b == 8, "NOTE 20 (VSPAWN) parses");
    {
        namespace text = mgscoop::coop_text;
        char s[256]{};
        Check(text::FormatWorldNote(20, 1, 1, 0, 0, s, sizeof(s)) && std::strstr(s, "jeep") && std::strstr(s, "in front of you"),
              "note 20 a=1 b=1: a jeep in front of you");
        Check(text::FormatWorldNote(20, 2, 2, 0, 0, s, sizeof(s)) && std::strstr(s, "Your partner") && std::strstr(s, "truck"),
              "note 20 a=2 b=2: your partner spawned a truck");
        Check(text::FormatWorldNote(20, 3, 1, 0, 0, s, sizeof(s)) && std::strstr(s, "not available yet"), "note 20 refusal 1: key off");
        Check(text::FormatWorldNote(20, 3, 2, 0, 0, s, sizeof(s)) && std::strstr(s, "free roam"), "note 20 refusal 2: free roam only");
        // Review r1101: 9 the partner's copy did not take it, 10 a player on the spot; 11 is the first without a sentence.
        for (std::uint32_t why = 3; why <= 10; ++why)
            Check(text::FormatWorldNote(20, 3, why, 0, 0, s, sizeof(s)) && std::strlen(s) > 10, "note 20 refusals 3..10 have a sentence");
        Check(!text::FormatWorldNote(20, 1, 3, 0, 0, s, sizeof(s)) && !text::FormatWorldNote(20, 3, 11, 0, 0, s, sizeof(s)) &&
                  !text::FormatWorldNote(20, 4, 1, 0, 0, s, sizeof(s)),
              "note 20 other values: no toast");
    }
    const std::string other = "return 'MGS_COOP_WORLD_WARP_V1' -- 1 2 3";
    Check(!world::ParseNote(other.data(), other.size(), code, a, b) && !world::ParseNote(nullptr, 3, code, a, b), "other receipts");
}

// The save handoff report (join-fix phase 2 P2-2): exactly 6 integers with
// per-op ranges.
void Save() {
    Check(Kind("return 'MGS_COOP_WORLD_SAVE_V1' -- 1 40050 1 2 0 0") == world::Receipt::save, "SAVE_V1 classifies");
    const std::string ok = "return 'MGS_COOP_WORLD_SAVE_V1' -- 1 40050 1 2 0 0";
    Check(world::Classify(ok.data(), ok.size(), "return 'MGS_COOP_WORLD_SAVE_V1' -- 1 40050 1 2 0 1") == world::Receipt::none,
          "save name != source");
    Check(Kind("return 'MGS_COOP_WORLD_SAVE_V1' -- ") == world::Receipt::none, "a save prefix without args");
    std::uint8_t op = 0;
    std::uint32_t a = 0, b = 0, c = 0, d = 0, e = 0;
    const auto parse = [&](const char* tail) {
        const std::string s = std::string(world::save_prefix) + tail;
        return world::ParseSave(s.data(), s.size(), op, a, b, c, d, e);
    };
    Check(parse("1 40050 1 2 0 0") && op == 1 && a == 40050 && b == 1 && c == 2 && d == 0 && e == 0, "REPORT at the title");
    Check(parse("2 30010 1 2 0 0") && op == 2 && a == 30010 && b == 1 && c == 2, "CONTINUE into 30010");
    Check(parse("3 1 0 0 0 0") && op == 3 && a == 1 && b == 0, "DECLINE second continue");
    Check(parse("3 2 0 0 0 0") && op == 3 && a == 2, "DECLINE f9");
    Check(parse("1 0 0 0 1 1") && op == 1 && a == 0 && d == 1 && e == 1, "REPORT mission 0, saving, in game");
    Check(parse("1 99999 0 1 0 1") && a == 99999 && c == 1, "REPORT bounds");
    op = 9; a = b = c = d = e = 9;
    Check(!parse("1 40050 1 2 0") && !parse("1 40050 1 2 0 0 0"), "5 or 7 tokens");
    Check(!parse("01 40050 1 2 0 0") && !parse("1 040050 1 2 0 0") && !parse("1 40050 1 2 0 00"), "leading zeros");
    Check(!parse("-1 40050 1 2 0 0") && !parse("1 -1 1 2 0 0") && !parse("1 40050 -0 2 0 0") && !parse("+1 40050 1 2 0 0"), "signs");
    Check(!parse("4 0 0 0 0 0") && !parse("0 0 0 0 0 0"), "op 4 and op 0");
    Check(!parse("1 40050 1 3 0 0") && !parse("2 30010 1 3 0 0"), "story 3");
    Check(!parse("1 100000 1 2 0 0") && !parse("2 100000 1 2 0 0") && !parse("2 0 1 2 0 0"), "mission range");
    Check(!parse("1 40050  1 2 0 0") && !parse("1 40050 1 2 0 0 ") && !parse(" 1 40050 1 2 0 0") && !parse(""), "spacing");
    Check(!parse("1 40050 2 2 0 0") && !parse("1 40050 1 2 2 0") && !parse("1 40050 1 2 0 2"), "flags 0|1");
    Check(!parse("2 30010 1 2 1 0") && !parse("2 30010 1 2 0 1") && !parse("2 30010 2 2 0 0"), "CONTINUE d=e=0, title 0|1");
    Check(!parse("3 0 0 0 0 0") && !parse("3 3 0 0 0 0") && !parse("3 1 1 0 0 0") && !parse("3 1 0 0 0 1"), "DECLINE reason 1|2, zeros");
    Check(!parse("1 4294967296 1 2 0 0") && !parse("1 40050 1 2 0 0x"), "overflow and junk");
    Check(op == 9 && a == 9 && b == 9 && c == 9 && d == 9 && e == 9, "outputs untouched on failure");
    const std::string other = "return 'MGS_COOP_WORLD_NOTE_V1' -- 1 2 3";
    Check(!world::ParseSave(other.data(), other.size(), op, a, b, c, d, e) && !world::ParseSave(nullptr, 3, op, a, b, c, d, e),
          "other receipts");
}

void TxParse() {
    std::uint8_t code = 0;
    std::int64_t v[world::kMaxValues]{};
    unsigned n = 0;
    Check(Tx("1 30050 1 4294967295 10 3", code, v, n) == world::TxStatus::ok && code == 1 && n == 5 && v[2] == 4294967295LL,
          "HELLO with a u32 max hash");
    Check(Tx("16 7 1 5 1 1 0 0 0 -2147483648 2147483647 0", code, v, n) == world::TxStatus::ok && n == 11 && v[8] == -2147483648LL &&
              v[9] == 2147483647LL,
          "i32 extremes");
    Check(Tx("16 7 1 5 1 1 0 0 0 -2147483649 0 0") == world::TxStatus::bad_args, "i32 underflow");
    Check(Tx("1 30050 1 4294967296 10 3") == world::TxStatus::bad_args, "u32 overflow");
    Check(Tx("1 30050 1 -5 10 3") == world::TxStatus::bad_args, "negative u32");
    Check(Tx("1 30050 1 5 10") == world::TxStatus::bad_args && Tx("1 30050 1 5 10 3 0") == world::TxStatus::bad_args, "count per op");
    Check(Tx("1 30050 1 +5 10 3") == world::TxStatus::syntax, "'+'");
    Check(Tx("1 30050 1 05 10 3") == world::TxStatus::syntax, "leading zero");
    Check(Tx("1 30050 1 -0 10 3") == world::TxStatus::syntax, "-0");
    Check(Tx("1 30050 1 5.0 10 3") == world::TxStatus::syntax, "'.'");
    Check(Tx("1 30050 1 5  10 3") == world::TxStatus::syntax && Tx("1 30050 1 5 10 3 ") == world::TxStatus::syntax &&
              Tx(" 1 30050 1 5 10 3") == world::TxStatus::syntax,
          "single spaces, nothing trailing");
    Check(Tx("1 30050 1 12345678901 10 3") == world::TxStatus::syntax, "more than 10 digits");
    Check(Tx("") == world::TxStatus::syntax && Tx("x") == world::TxStatus::syntax, "empty / not a number");
    Check(Tx("29 1") == world::TxStatus::unknown_op && Tx("3 1 3") == world::TxStatus::unknown_op && Tx("113") == world::TxStatus::unknown_op &&
              Tx("0") == world::TxStatus::unknown_op && Tx("256") == world::TxStatus::unknown_op,
          "unknown, native-only and local ops");
    Check(Tx("120 1 2", code, v, n) == world::TxStatus::ok && code == 120 && n == 2, "GOTO_ACK is the one Lua local op");
    Check(Tx("120 3 2") == world::TxStatus::bad_args, "GOTO_ACK step range");
    Check(Tx("23 77 0 5 1 2 3 4 5 0 0 0 0 0 0 0", code, v, n) == world::TxStatus::ok && n == 15, "PHASE_VEC: 15 fields");
    Check(Tx("21 1 86400 2000 1 0 0 0 3 1") == world::TxStatus::bad_args, "clock 86400 refused");
    Check(Tx("21 1 86399 2000 255 0 0 0 3 1") == world::TxStatus::ok, "weather 255 (release)");
    Check(Tx("28 4294967295 65535 -10000000 10000000 0 -360000", code, v, n) == world::TxStatus::ok && n == 6,
          "checkpoint hash, version and bounded signed pose");
    Check(Tx("28 0 1 0 0 0 360001") == world::TxStatus::bad_args && Tx("28 0 1 0 -10000001 0 0") == world::TxStatus::bad_args,
          "checkpoint angle and position bounds");
    char out[world::reply_capacity];
    Check(world::FormatTxReply(world::TxStatus::ok, out, sizeof(out)) && std::strcmp(out, "return 1") == 0, "TX ok reply");
    Check(world::FormatTxReply(world::TxStatus::queue_full, out, sizeof(out)) && std::strcmp(out, "return 0,6") == 0, "TX refusal");
    // The longest TX source (PHASE_VEC with 15 u32 maxima) stays below 256 bytes.
    std::string longest = std::string(world::tx_prefix) + "23 192 0 12";
    for (int i = 0; i < 12; ++i) longest += " 4294967295";
    Check(longest.size() < 256 && longest.size() <= world::max_receipt, "longest TX under 240");
}

void Rx() {
    std::uint8_t mask = 0;
    const auto parse = [&](const char* tail) {
        const std::string s = std::string(world::rx_prefix) + tail;
        return world::ParseRx(s.data(), s.size(), mask);
    };
    Check(parse("1") && mask == 1 && parse("3") && mask == 3, "masks 1 and 3");
    Check(!parse("2") && !parse("0") && !parse("03") && !parse("3 ") && !parse("13"), "other masks refused");
    world::RxReply r{};
    r.empty = false;
    r.queue = 1;
    r.op = 23;
    r.count = 15;
    for (unsigned i = 0; i < 15; ++i) r.v[i] = -2147483647LL - 1;
    char out[world::reply_capacity];
    const std::size_t n = world::FormatRxReply(r, out, sizeof(out));
    Check(n > 0 && n < world::reply_capacity && std::strncmp(out, "return 1,23,-2147483648,", 24) == 0, "17-value reply fits 224");
    world::RxReply empty{};
    empty.empty = true;
    empty.flags = 7;
    empty.qctrl = 2;
    empty.qworld = 300;
    empty.epoch = 9;
    Check(world::FormatRxReply(empty, out, sizeof(out)) && std::strcmp(out, "return 0,0,7,2,300,9") == 0, "empty reply");
    char tiny[16];
    Check(world::FormatRxReply(r, tiny, sizeof(tiny)) == 0 && tiny[0] == 0, "too small: nothing");
}

void Log() {
    char out[world::log_line_capacity];
    const auto line = [&](const std::string& payload) {
        const std::string s = std::string(world::log_prefix) + payload;
        return world::FormatLogLine(s.data(), s.size(), 1234, out, sizeof(out));
    };
    Check(line("event=commit;ent=4294967295;state=5") && std::strcmp(out, "world_lua=event=commit; ent=4294967295; state=5; t_ms=1234\r\n") == 0,
          "';' expanded, t_ms appended");
    Check(line("a b\"c'd") && std::strcmp(out, "world_lua=a_b_c_d; t_ms=1234\r\n") == 0, "sanitized");
    Check(line(std::string(160, 'x')) && std::strlen(out) == 10 + 160 + 13, "160 bytes");
    Check(!line(std::string(161, 'x')) && !line(""), "empty or above 160 refused");
}

void Triggers() {
    world::Trigger t{};
    std::string canonical, reason;
    Check(Trigger("goto_lz=1 goto=30020 world_probe=race_kill world=3\r\n", t, canonical, reason) && t.has_world && t.world == 3 &&
              t.has_probe && t.probe == mgscoop::WorldProbe::race_kill && t.has_goto && t.goto_target == 30020 && t.has_lz && t.lz == 1,
          "every key");
    Check(canonical == "world=3 world_probe=race_kill goto=30020 goto_lz=1", "canonical order");
    Check(Trigger("\xEF\xBB\xBF  world=0  \n", t, canonical, reason) && t.world == 0 && !t.has_goto && canonical == "world=0",
          "BOM and blanks");
    Check(Trigger("world_probe=off", t, canonical, reason) && t.has_probe && t.probe == mgscoop::WorldProbe::none &&
              canonical == "world_probe=off",
          "off cancels");
    Check(Trigger("goto=30010", t, canonical, reason) && t.goto_target == 30010 && !t.has_lz && canonical == "goto=30010", "goto");
    Check(!Trigger("world=1 world=2", t, canonical, reason) && reason == "repeat", "repeat");
    Check(!Trigger("goto_lz=1", t, canonical, reason) && reason == "token", "goto_lz needs goto");
    Check(!Trigger("", t, canonical, reason) && reason == "empty", "empty");
    Check(!Trigger(" \r\n", t, canonical, reason) && reason == "empty", "blank");
    Check(!Trigger("world=4", t, canonical, reason) && reason == "token", "world 0..3");
    // fb1031 arrival: Mother Base is a lab goto target (the guest follows the host's ACC sortie back in by helicopter).
    Check(Trigger("goto=30050", t, canonical, reason) && t.goto_target == 30050 && canonical == "goto=30050", "goto 30050");
    Check(!Trigger("goto=30030", t, canonical, reason) && reason == "token", "goto targets");
    Check(!Trigger("world_probe=none", t, canonical, reason) && reason == "token", "none is spelled off");
    Check(!Trigger("world_probe=p2", t, canonical, reason) && reason == "token", "unknown probe");
    Check(!Trigger("stage=3", t, canonical, reason) && reason == "token", "partner keys are not world keys");
    Check(!Trigger("world", t, canonical, reason) && reason == "token", "a key without a value");
    std::string pad = "world=1" + std::string(247, ' ') + "\n";
    Check(pad.size() == 255 && Trigger(pad, t, canonical, reason), "255 bytes");
    pad.insert(0, " ");
    Check(!Trigger(pad, t, canonical, reason) && reason == "too_long", "256 bytes refused");
    const char* names[] = {"p1", "p3", "p3b", "p4", "p12", "p17", "race_kill", "remote_area_kill", "kill_near", "sleep_near",
                           "fulton_near", "alert_near", "weather_rain", "clock_jump", "clock_fast",
                           "shared_death", "host_continue", "shared_checkpoint"};
    for (unsigned i = 0; i < 15; ++i) {
        Check(Trigger(std::string("world_probe=") + names[i], t, canonical, reason) && static_cast<unsigned>(t.probe) == i + 1 &&
                  canonical == std::string("world_probe=") + names[i],
              "probe names round trip");
    }
    Check(Trigger("world_probe=guest_restart", t, canonical, reason) && static_cast<unsigned>(t.probe) == 19 &&
              canonical == "world_probe=guest_restart",
          "guest_restart trigger round trips");
    Check(Trigger("world_probe=guest_frozen_heli", t, canonical, reason) && static_cast<unsigned>(t.probe) == 20 &&
              canonical == "world_probe=guest_frozen_heli",
          "guest_frozen_heli trigger round trips");
    Check(Trigger("world_probe=host_target_dead", t, canonical, reason) && static_cast<unsigned>(t.probe) == 21 &&
              canonical == "world_probe=host_target_dead",
          "host_target_dead trigger round trips");
}
// Guest spawn sync: "<seq> <goId>", strict integers, nothing else.
void Warp() {
    std::uint16_t seq = 0;
    std::uint32_t go = 0;
    const auto parse = [&](const char* tail) {
        const std::string s = std::string(world::warp_prefix) + tail;
        return world::ParseWarp(s.data(), s.size(), seq, go);
    };
    Check(parse("1 1116") && seq == 1 && go == 1116, "seq and GameObjectId");
    Check(parse("65535 0") && seq == 65535 && go == 0 && parse("7 65535") && go == 65535, "bounds");
    seq = 9; go = 9;
    Check(!parse("0 1116") && !parse("65536 1") && !parse("1 65536") && !parse("-1 1") && !parse("1 -1"), "out of range");
    Check(!parse("1") && !parse("1 2 3") && !parse("1  2") && !parse("01 2") && !parse("1 02") && !parse("1 2 ") && !parse("1.0 2") &&
              !parse("+1 2") && !parse(""),
          "strict grammar");
    Check(seq == 9 && go == 9, "outputs untouched on failure");
    // Enemy round 8 (quiet_place, docs/world-sync-design.md 7.22): an optional third token 1 asks for the quiet move (the
    // guest's Lua sends it for a correction off its player's screen); the strict two-token parse refuses it.
    bool quiet = true;
    const auto parseQ = [&](const char* tail) {
        const std::string s = std::string(world::warp_prefix) + tail;
        return world::ParseWarp(s.data(), s.size(), seq, go, quiet);
    };
    Check(parseQ("3 1270 1") && seq == 3 && go == 1270 && quiet, "the quiet flag");
    Check(parseQ("4 1271") && seq == 4 && go == 1271 && !quiet, "no flag: the set-transform move");
    Check(!parseQ("4 1271 0") && !parseQ("4 1271 2") && !parseQ("4 1271 1 ") && !parseQ("4 1271  1") && !parseQ("4 1271 1 1") &&
              !parse("4 1271 1"),
          "the quiet flag: only a single 1; the strict parse refuses it");
    seq = 9; go = 9;
    const std::string other = "return 'MGS_COOP_WORLD_TX_V1' -- 1 2";
    Check(!world::ParseWarp(other.data(), other.size(), seq, go) && !world::ParseWarp(nullptr, 3, seq, go), "other receipts");
    // Enemy sync M3: the host's warp "<seq> <goId>" under its own prefix.
    const std::string host = std::string(world::hostwarp_prefix) + "5 1037";
    Check(world::ParseHostWarp(host.data(), host.size(), seq, go) && seq == 5 && go == 1037 &&
              !world::ParseWarp(host.data(), host.size(), seq, go) &&
              world::Classify(host.data(), host.size(), host.c_str()) == world::Receipt::hostwarp,
          "the host warp receipt");
    // Enemy sync M4: DRIVE_V1 "<slot> <goId> <x_mm> <y_mm> <z_mm>".
    std::uint16_t slot = 0;
    std::int32_t pos[3]{};
    const auto drive = [&](const char* tail) {
        const std::string s = std::string(world::drive_prefix) + tail;
        return world::ParseDrive(s.data(), s.size(), slot, go, pos);
    };
    Check(drive("7 1037 410000 -268400 2170000") && slot == 7 && go == 1037 && pos[0] == 410000 && pos[1] == -268400 && pos[2] == 2170000,
          "drive: slot, GameObjectId and position");
    Check(!drive("0 1037 0 0 0") && !drive("513 1037 0 0 0") && !drive("7 65536 0 0 0") && !drive("7 1037 100000001 0 0") &&
              !drive("7 1037 0 0") && !drive("7 1037 0 0 0 1") && !drive("7  1037 0 0 0"),
          "drive: bounds and strict grammar (the soft follow has five fields)");
    std::uint8_t mode = 9;
    const std::string puppet = std::string(world::drive_prefix) + "7 1037 1 2 3 1";
    const std::string soft = std::string(world::drive_prefix) + "7 1037 1 2 3";
    const std::string mirror = std::string(world::drive_prefix) + "7 1037 1 2 3 2";
    const std::string bad = std::string(world::drive_prefix) + "7 1037 1 2 3 3";
    Check(world::ParseDriveMode(puppet.data(), puppet.size(), slot, go, pos, mode) && mode == 1 && pos[2] == 3 &&
              world::ParseDriveMode(soft.data(), soft.size(), slot, go, pos, mode) && mode == 0 &&
              world::ParseDriveMode(mirror.data(), mirror.size(), slot, go, pos, mode) && mode == 2 &&
              !world::ParseDriveMode(bad.data(), bad.size(), slot, go, pos, mode),
          "drive: the mode field (0 soft, 1 puppet, FM1 2 mirror)");
    // FM1, the far mirror (lua/coop/world_mirror.lua): ACT_V1 "<t|m|r|s> <slot> <goId>".
    {
        char op = 0;
        std::uint16_t aslot = 0;
        std::uint32_t ago = 0;
        const auto act = [](const char* rest) { return std::string(world::act_prefix) + rest; };
        const std::string t = act("t 7 1037"), m = act("m 512 1037"), r = act("r 1 0"), s = act("s 0 0"), qq = act("q 9 1040");
        // FM1 (run 3bc7280b): a candidate is only queried (q: freshness, no mailbox, no fire mask); m is for puppets.
        Check(world::ParseAct(qq.data(), qq.size(), op, aslot, ago) && op == 'q' && aslot == 9 && ago == 1040, "act: q <slot> <goId>");
        Check(Kind(t) == world::Receipt::act && Kind(s) == world::Receipt::act, "ACT_V1 is its own receipt");
        Check(world::ParseAct(t.data(), t.size(), op, aslot, ago) && op == 't' && aslot == 7 && ago == 1037 &&
                  world::ParseAct(m.data(), m.size(), op, aslot, ago) && op == 'm' && aslot == 512 &&
                  world::ParseAct(r.data(), r.size(), op, aslot, ago) && op == 'r' && ago == 0 &&
                  world::ParseAct(s.data(), s.size(), op, aslot, ago) && op == 's' && aslot == 0,
              "act: t / m / r with a slot and goId, s 0 0");
        // Enemy round 13 C (D-FM3 (b)): w <slot> <goId>, the puppet's and his owner copy's spots outside the guest's view.
        const std::string wv = act("w 3 1041");
        Check(world::ParseAct(wv.data(), wv.size(), op, aslot, ago) && op == 'w' && aslot == 3 && ago == 1041, "act: w <slot> <goId>");
        const char* bads[] = {"x 7 1037", "t 0 1037", "t 513 1037", "t 7 65536", "t 7", "t 7 1037 1", "s 1 0", "s 0 1", "t  7 1037",
                              "t 7 -1", ""};
        for (const char* b : bads) {
            const std::string x = act(b);
            Check(!world::ParseAct(x.data(), x.size(), op, aslot, ago), "act: a malformed receipt is refused");
        }
        std::int32_t pmm[3]{};
        const std::string p = act("p 421159 -268759 2084588");
        Check(Kind(p) == world::Receipt::act && world::ParseActPlayer(p.data(), p.size(), pmm) && pmm[0] == 421159 && pmm[1] == -268759 &&
                  pmm[2] == 2084588 && !world::ParseAct(p.data(), p.size(), op, aslot, ago), "act: p <x y z mm>, the local player");
        const char* pbads[] = {"p 1 2", "p 1 2 3 4", "p 1 2 100000001", "q 1 2 3", "p  1 2 3"};
        for (const char* b : pbads) {
            const std::string x = act(b);
            Check(!world::ParseActPlayer(x.data(), x.size(), pmm), "act: a malformed p is refused");
        }
    }
}
} // namespace

int main() {
    Classification();
    Cfg();
    TxParse();
    Rx();
    Log();
    Triggers();
    Warp();
    Note();
    Save();
    std::printf("world_receipt_tests: %u checks passed\n", checks);
    return 0;
}
