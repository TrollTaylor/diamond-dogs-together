// DirectMotion overlay lever (partner_dm_overlay): the catalogue, its reply
// codes and the Lua OVERLAY table, the remote-act map, and the machine on
// deterministic timelines (suspend, patch, play gate, confirm, stop, release,
// map start/switch/exit, refusals). Pure fixtures; no game, no Win32.
// Usage: partner_dm_overlay_tests [lua/coop/partner_sync.lua]
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "mgscoop/partner_dm_overlay.hpp"

namespace {
namespace pdo = mgscoop::partner_dm_overlay;
using mgscoop::PartnerNativeOverlay;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}
bool Is(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }
constexpr std::uint16_t kDm = 139;

void TestCatalogue(const char* lua_path) {
    Check(pdo::Count() >= 19 && pdo::Count() <= pdo::kMaxEntries, "catalogue size fits the 5-bit reply index");
    // The map candidates (critic 6 / P2.10), verified graph-0 codes.
    const struct { const char* name; std::uint64_t code; } verified[] = {
        {"stateDamageBigFront", 0x3db7bdedaea7ull}, {"stateDamageBigBack", 0xad926744b0e3ull}, {"stateDamageBigLeft", 0x1888402f2d17ull},
        {"stateDamageBigRight", 0x074254c360e9ull}, {"stateDead", 0xdad5759b0203ull}, {"stateCBoxIdle", 0x039c85aa4598ull},
        {"stateCBoxMove", 0x6e06f122e3b3ull}, {"stateEludeIdle", 0x1cf4b93bd4fcull}, {"stateClimbIdle", 0x2928469590b4ull},
        {"stateClimbMoveFront", 0x489a8f233d63ull}, {"stateClimbMoveBack", 0x66d0af83390eull}};
    for (const auto& v : verified) {
        const std::uint8_t i = pdo::Find(v.name, std::strlen(v.name));
        Check(i != pdo::kNone && pdo::At(i)->code == v.code && !pdo::At(i)->path && Is(pdo::At(i)->lua, v.name), v.name);
    }
    // The mount research probes (docs/mounts.md), appended at 22..24, looping, never mapped.
    const struct { const char* name; std::uint64_t code; std::uint8_t index; } mount[] = {
        {"stateRideOnIdle", 0xf241f3bcf60bull, 22}, {"stateGallop", 0xce7e8409eb61ull, 23}, {"stateRideWalkerWalk", 0x1b8b642289d5ull, 24}};
    for (const auto& v : mount) {
        const std::uint8_t i = pdo::Find(v.name, std::strlen(v.name));
        Check(i == v.index && pdo::At(i)->code == v.code && !pdo::At(i)->path && pdo::At(i)->loop && Is(pdo::At(i)->lua, v.name), v.name);
        for (std::uint16_t act = 0; act < 256; ++act)
            if (pdo::MapAct(act, 0) == i || pdo::MapAct(act, 1000) == i || pdo::MapAct(act, -1000) == i) Check(false, "no act maps to a mount probe");
    }
    Check(pdo::Find("stateGunHold", 12) == pdo::kNone && pdo::Find("statethrowholdend", 17) == pdo::kNone &&
              pdo::Find("stateDead ", 10) == pdo::kNone && pdo::Find("", 0) == pdo::kNone && pdo::Find(nullptr, 3) == pdo::kNone,
          "graph-2 states and near misses are not in the catalogue");
    for (std::uint8_t i = 0; i < pdo::Count(); ++i) {
        const pdo::Entry* e = pdo::At(i);
        Check(e && e->name && e->lua && e->code, "entry fields");
        Check(pdo::Find(e->name, std::strlen(e->name)) == i, "names are unique");
        for (const bool loop : {false, true}) {
            const std::uint8_t code = pdo::ReplyCode(i, loop);
            std::uint8_t index = 0xEE;
            bool l = !loop;
            Check(code >= 64 && code <= 127 && pdo::DecodeReply(code, index, l) && index == i && l == loop, "reply code round trip");
        }
        if (e->path) Check(e->lua[0] == '/' && pdo::ExpectedRec18(i) == 0x81582ffa24b9ull && (e->code >> 51) == 0x1F8Au, "clip path entry");
        else Check(e->lua[0] != '/' && pdo::ExpectedRec18(i) == e->code && e->code < (1ull << 48), "state entry");
    }
    std::uint8_t index = 0;
    bool loop = false;
    Check(!pdo::DecodeReply(63, index, loop) && !pdo::DecodeReply(128, index, loop) &&
              !pdo::DecodeReply(static_cast<std::uint8_t>(64 + pdo::Count()), index, loop) && pdo::ReplyCode(pdo::Count(), false) == 0,
          "reply codes outside the catalogue refused");
    // partner_sync.lua OVERLAY: the same entries in the same order.
    if (!lua_path) return;
    std::ifstream file(lua_path, std::ios::binary);
    Check(file.good(), "partner_sync.lua readable");
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    const std::size_t at = text.find("local OVERLAY = {[0] = ");
    Check(at != std::string::npos, "OVERLAY table present");
    const std::size_t end = text.find('}', at);
    Check(end != std::string::npos, "OVERLAY table closed");
    const std::string gmp = "/Assets/tpp/motion/SI_game/fani/bodies/snap/";
    const std::string dmp = gmp + "snapnon/";
    std::vector<std::string> entries;
    std::string body = text.substr(at + 23, end - (at + 23));
    std::size_t pos = 0;
    while (pos < body.size()) {
        std::size_t comma = body.find(',', pos);
        if (comma == std::string::npos) comma = body.size();
        std::string item = body.substr(pos, comma - pos);
        pos = comma + 1;
        // trim
        const std::size_t a = item.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) continue;
        item = item.substr(a, item.find_last_not_of(" \t\r\n") - a + 1);
        std::string prefix;
        if (item.rfind("GMP .. ", 0) == 0) { prefix = gmp; item = item.substr(7); }
        else if (item.rfind("DMP .. ", 0) == 0) { prefix = dmp; item = item.substr(7); }
        Check(item.size() >= 2 && item.front() == '"' && item.back() == '"', "OVERLAY items are string literals");
        entries.push_back(prefix + item.substr(1, item.size() - 2));
    }
    Check(entries.size() == pdo::Count(), "OVERLAY has every catalogue entry");
    for (std::uint8_t i = 0; i < pdo::Count() && i < entries.size(); ++i) Check(entries[i] == pdo::At(i)->lua, "OVERLAY[i] == catalogue lua");
}

void TestMap() {
    const auto name = [](std::uint8_t i) { return i == pdo::kNone ? "none" : pdo::At(i)->name; };
    Check(Is(name(pdo::MapAct(29, 0)), "stateDamageBigFront") && Is(name(pdo::MapAct(75, 0)), "stateDamageBigFront") &&
              Is(name(pdo::MapAct(76, 0)), "stateDamageBigFront"), "damage 29/75/76");
    Check(Is(name(pdo::MapAct(40, 0)), "stateDead") && Is(name(pdo::MapAct(41, 0)), "stateDead"), "death 40/41");
    Check(Is(name(pdo::MapAct(77, 0)), "stateCBoxIdle") && Is(name(pdo::MapAct(78, 0)), "stateCBoxMove"), "box 77/78");
    Check(Is(name(pdo::MapAct(54, 0)), "stateEludeIdle") && Is(name(pdo::MapAct(55, 0)), "stateEludeIdle") &&
              Is(name(pdo::MapAct(56, 0)), "stateEludeIdle"), "ledge hang 54..56");
    Check(Is(name(pdo::MapAct(23, 0)), "stateClimbIdle") && Is(name(pdo::MapAct(23, 400)), "stateClimbMoveFront") &&
              Is(name(pdo::MapAct(23, -400)), "stateClimbMoveBack") && Is(name(pdo::MapAct(21, 0)), "stateClimbIdle") &&
              Is(name(pdo::MapAct(21, 200)), "stateClimbIdle"), "ladder 21/23 by vertical speed");
    for (const int act : {0, 1, 2, 3, 5, 6, 11, 25, 26, 27, 39, 42, 43, 57, 58, 109, 111, 131, 139})
        Check(pdo::MapAct(static_cast<std::uint16_t>(act), 0) == pdo::kNone, "locomotion, carry, sleep, ride, DM: unmapped");
}

// A tiny game: the arbiter clears native one tick after the suspend, the DM
// patch lands when requested, a play is consumed (q18[1] = 139) one tick
// later, a stop ends it one tick later; the remote acts are scripted.
struct Game {
    pdo::State s{};
    pdo::Params p{};
    pdo::In in{};
    pdo::Out out{};
    std::uint64_t now = 100000;
    bool native_active = true;
    bool suspended_prev = false;
    bool consume = true, stops = true;
    bool stuck = false;  // the arbiter never leaves
    bool play_pending = false, stop_pending = false;
    unsigned plays = 0, stops_sent = 0, patches = 0, clears = 0;
    std::vector<std::string> events;
    std::uint8_t last_code = 0;
    Game() {
        pdo::Reset(s);
        in.mode = PartnerNativeOverlay::debug;
        in.map_idle = true;
        in.dm_ok = in.cam_ok = true;
        in.q18[0] = 1;
        in.q18[1] = 1;
        in.body_valid = true;
        in.body_act = 1;
        in.body_present = true;
    }
    pdo::Out Step(std::uint32_t dt = 33) {
        now += dt;
        // The game reacts to the previous step.
        if (suspended_prev && !stuck) native_active = false;
        if (play_pending) {
            play_pending = false;
            if (consume) {
                in.q18[1] = kDm;
                in.rec18 = pdo::ExpectedRec18(s.index);
            } else {
                in.g388 = 4;
            }
        }
        if (stop_pending) {
            stop_pending = false;
            if (stops) in.q18[1] = 1;
        }
        in.now_ms = now;
        in.native_clear = !native_active;
        out = pdo::Step(s, in, p);
        in.debug = pdo::Request{};
        suspended_prev = out.suspend;
        if (!out.suspend && !pdo::Active(s)) native_active = true;  // the arbiter re-enters
        if (out.patch) { in.patched = true; ++patches; }
        if (out.play) { ++plays; play_pending = true; last_code = out.reply_code; }
        if (out.stop) { ++stops_sent; stop_pending = true; }
        if (out.clear_pending) { ++clears; in.g388 = 0; }
        if (out.event) events.push_back(std::string(out.event) + ":" + (out.reason ? out.reason : "-"));
        return out;
    }
    void Run(std::uint32_t ms) {
        const std::uint64_t end = now + ms;
        while (now < end) Step();
    }
    bool Saw(const char* what) const {
        for (const auto& e : events)
            if (e == what) return true;
        return false;
    }
    void Debug(const char* name, bool loop, std::uint32_t ms) {
        in.debug.start = true;
        in.debug.index = pdo::Find(name, std::strlen(name));
        in.debug.loop = loop;
        in.debug.ms = ms;
    }
};

void TestDebugFlow() {
    Game g;
    g.Debug("stateEludeIdle", true, 2000);
    g.Step();
    Check(Is(g.out.event, "request") && g.out.suspend && g.out.owns_dm && !g.out.play && g.out.source == pdo::Source::debug,
          "request: suspend first, own the DM, no play while native is active");
    g.Step();
    Check(g.out.patch && !g.out.play && g.s.phase == pdo::Phase::patch, "native cleared: patch");
    g.Step();
    const std::uint8_t elude = pdo::Find("stateEludeIdle", 14);
    Check(g.out.play && g.last_code == pdo::ReplyCode(elude, true) && g.last_code == 64 + 32 + elude && Is(g.out.event, "play"),
          "patched and idle: DM_PLAY with the overlay reply code");
    g.Step();
    Check(Is(g.out.event, "playing") && g.out.code_match && g.s.phase == pdo::Phase::playing, "confirmed (q18[1] == 139, rec+0x18 matches)");
    g.Run(1500);
    Check(g.s.phase == pdo::Phase::playing && g.stops_sent == 0, "plays for the requested ms");
    for (int i = 0; i < 40 && !g.stops_sent; ++i) g.Step();
    Check(g.stops_sent == 1 && g.Saw("stop:timeout") && g.s.phase == pdo::Phase::stop_sent, "timeout: DM_STOP");
    g.Step();
    Check(g.Saw("released:timeout") && g.s.phase == pdo::Phase::idle && g.out.owns_dm && !g.out.suspend,
          "released after q18[1] left 139; the DM stays the overlay's for the finishing tick");
    g.Step();
    Check(!g.out.owns_dm && !g.out.suspend && g.native_active, "back to native");
    Check(g.plays == 1 && g.patches == 1 && g.clears == 0, "one play, one patch");
}

void TestDebugStopAndRefusals() {
    {
        Game g;
        g.Debug("stateCBoxIdle", true, 10000);
        g.Run(200);
        Check(g.s.phase == pdo::Phase::playing, "playing");
        g.in.debug.stop = true;
        g.Step();
        Check(g.out.stop && Is(g.out.event, "stop") && Is(g.out.reason, "debug"), "overlay_stop: DM_STOP");
        g.Run(100);
        Check(g.Saw("released:debug") && g.s.phase == pdo::Phase::idle, "released");
        g.in.debug.stop = true;
        g.Step();
        Check(Is(g.out.event, "refused") && Is(g.out.reason, "not_active"), "stop while idle: refused");
    }
    {
        Game g;
        g.in.mode = PartnerNativeOverlay::off;
        g.Debug("stateDead", false, 1000);
        g.Step();
        Check(Is(g.out.event, "refused") && Is(g.out.reason, "setting_off") && !g.out.suspend && !pdo::Active(g.s), "setting off: refused");
        g.in.mode = PartnerNativeOverlay::debug;
        g.in.debug.start = true;
        g.in.debug.index = 200;
        g.Step();
        Check(Is(g.out.reason, "unknown_state"), "unknown index refused");
        g.in.blocked = "ride";
        g.Debug("stateDead", false, 1000);
        g.Step();
        Check(Is(g.out.event, "refused") && Is(g.out.reason, "ride") && !pdo::Active(g.s), "blocked: refused with the driver's reason");
        g.in.blocked = nullptr;
        g.Debug("stateDead", false, 1000);
        g.Step();
        g.Debug("stateCBoxIdle", true, 1000);
        g.Step();
        Check(Is(g.out.event, "refused") && Is(g.out.reason, "busy") && g.s.index == pdo::Find("stateDead", 9), "a second request while busy: refused");
    }
}

void TestPlayGate() {
    Game g;
    g.in.cam1f0 = 0x8800000000006400ull;  // the viewer drives a vehicle
    g.in.cam200 = 1;
    g.Debug("stateDamageBigFront", false, 3000);
    g.Run(500);
    Check(g.plays == 0 && g.Saw("deferred:cam_connect"), "camera connect: refused (deferred) without a camera-skip module");
    g.in.cam_skip = true;  // the hook point (track V's camera skips)
    g.Step();
    Check(g.plays == 1, "camera-skip armed: plays");
    Game h;
    h.in.q18[0] = kDm;  // the local player in DM
    h.Debug("stateDead", false, 600);
    h.Run(300);
    Check(h.plays == 0 && h.Saw("deferred:host_dm"), "local DM: deferred");
    h.in.q18[0] = 1;
    h.in.g388 = 0x10;
    h.Run(100);
    Check(h.plays == 0 && h.Saw("deferred:pending"), "a pending global request: deferred");
    h.in.g388 = 0;
    h.in.reply_busy = true;
    h.Run(100);
    Check(h.plays == 0 && h.Saw("deferred:reply"), "a Warp/look op in this reply: deferred");
    h.Run(400);
    Check(h.plays == 0 && h.Saw("abort:reply") && !pdo::Active(h.s), "deferred past the request's ms: abort");
}

void TestUnconsumedAndLost() {
    {
        Game g;
        g.consume = false;
        g.Debug("stateClimbIdle", true, 8000);
        g.Run(2600 + 200);
        Check(g.Saw("unconsumed:-") && g.clears == 1 && !pdo::Active(g.s), "unconsumed after confirm_ms: cleared, released");
    }
    {
        // A loop the game ends is replayed once, then given up.
        Game g;
        g.Debug("stateEludeIdle", true, 20000);
        g.Run(200);
        Check(g.s.phase == pdo::Phase::playing, "playing");
        g.in.q18[1] = 1;
        g.Step();
        Check(Is(g.out.event, "lost") && Is(g.out.reason, "replay") && g.s.phase == pdo::Phase::patch, "lost: replay");
        g.Run(200);
        Check(g.plays == 2 && g.s.phase == pdo::Phase::playing, "replayed");
        g.in.q18[1] = 1;
        g.Step();
        Check(Is(g.out.event, "lost") && Is(g.out.reason, "ended") && !pdo::Active(g.s), "second loss: released");
    }
    {
        // A one-shot ends by itself: finished.
        Game g;
        g.Debug("stateDamageBigFront", false, 3000);
        g.Run(200);
        g.in.q18[1] = 1;
        g.Step();
        Check(Is(g.out.event, "finished") && Is(g.out.reason, "ended") && g.stops_sent == 0, "one-shot finished on its own");
    }
    {
        // A stop the game does not confirm: one retry, then give up.
        Game g;
        g.stops = false;
        g.Debug("stateCBoxIdle", true, 300);
        g.Run(400);
        g.Run(2200);
        Check(g.stops_sent == 2 && g.Saw("stop_retry:timeout") && g.Saw("stop_timeout:timeout") && !pdo::Active(g.s), "stop retry, then timeout");
    }
    {
        // The local player's DM blocks our (global) stop.
        Game g;
        g.Debug("stateCBoxIdle", true, 300);
        g.Run(250);
        g.in.q18[0] = kDm;
        g.Run(200);
        Check(g.stops_sent == 0 && g.Saw("deferred:host_dm"), "no global stop over the local DM");
        g.in.q18[0] = 1;
        g.Step();
        Check(g.out.stop, "stop once the local DM ended");
    }
    {
        // Blocked mid-play (a carry): stop.
        Game g;
        g.Debug("stateCBoxIdle", true, 30000);
        g.Run(200);
        g.in.blocked = "carry";
        g.Step();
        Check(g.out.stop && Is(g.out.reason, "carry"), "blocked while playing: stop");
    }
    {
        // The body went away (a ride hide unrealized it, q18[1] may read stale 139): no global stop, abort.
        Game g;
        g.Debug("stateCBoxIdle", true, 30000);
        g.Run(200);
        g.in.blocked = "ride";
        g.in.body_present = false;
        g.Step();
        Check(!g.out.stop && Is(g.out.event, "abort") && Is(g.out.reason, "ride") && !pdo::Active(g.s) && g.stops_sent == 0,
              "body gone while playing: abort without DM_STOP");
    }
}

void TestMapFlow() {
    Game g;
    g.in.mode = PartnerNativeOverlay::map;
    g.in.remote_valid = true;
    g.in.remote_act = 1;
    g.Run(200);
    Check(!pdo::Active(g.s), "locomotion: nothing");
    // The remote hangs from a ledge (55); body 1 stays standing (ACTION masked).
    g.in.remote_act = 55;
    g.Run(100);
    Check(!pdo::Active(g.s), "held less than map_hold_ms");
    g.Run(100);
    Check(pdo::Active(g.s) && g.s.source == pdo::Source::map && g.s.index == pdo::Find("stateEludeIdle", 14) && g.s.loop, "map start");
    g.Run(300);
    Check(g.s.phase == pdo::Phase::playing && g.plays == 1, "map playing");
    // Ledge hang -> ladder (23, climbing up): switch after the dwell, without native in between.
    g.in.remote_act = 23;
    g.Run(66);
    g.in.remote_y_mm = 0;
    const std::uint8_t climb_up = pdo::Find("stateClimbMoveFront", 19), climb_idle = pdo::Find("stateClimbIdle", 14);
    for (int i = 0; i < 60; ++i) {
        g.in.remote_y_mm += 20;  // 600 mm/s up
        g.Step();
        Check(!g.native_active, "switches never return to native");
    }
    Check(g.Saw("stop:switch") && g.Saw("released:switch") && !g.Saw("released:map_exit"), "switch: stop, released (no exit)");
    Check(g.s.phase == pdo::Phase::playing && g.s.index == climb_up && g.plays >= 2, "climbing up: stateClimbMoveFront");
    // The remote rests on the ladder: back to stateClimbIdle after the dwell.
    g.Run(1200);
    Check(g.s.phase == pdo::Phase::playing && g.s.index == climb_idle && !g.native_active, "resting on the ladder: stateClimbIdle");
    // The remote back on foot: map_exit.
    g.in.remote_act = 1;
    g.Run(400);
    Check(g.Saw("released:map_exit") && !pdo::Active(g.s), "map exit: released");
    g.Run(100);
    Check(g.native_active, "native again");
    // Damage (one-shot): it runs out even after the remote's act ended, and is not replayed in the same episode.
    g.in.remote_act = 29;
    g.Run(300);
    Check(pdo::Active(g.s) && g.s.index == pdo::Find("stateDamageBigFront", 19) && !g.s.loop, "damage: one-shot");
    g.in.remote_act = 1;
    g.Run(400);
    Check(g.s.phase == pdo::Phase::playing, "a one-shot is not cut by map_exit");
    g.in.q18[1] = 1;
    g.Step();
    Check(Is(g.out.event, "finished"), "one-shot finished");
    // The body shows the act natively: no overlay.
    g.Run(1000);
    g.in.remote_act = 77;
    g.in.body_act = 77;
    g.Run(500);
    Check(!pdo::Active(g.s), "body already in the remote's act: no overlay");
    // Debug mode never maps.
    Game d;
    d.in.remote_valid = true;
    d.in.remote_act = 55;
    d.Run(1000);
    Check(!pdo::Active(d.s), "debug mode: no map");
    // A map loop the game keeps ending: replayed once, then not restarted while the remote stays in the act.
    Game l;
    l.in.mode = PartnerNativeOverlay::map;
    l.in.remote_valid = true;
    l.in.remote_act = 77;
    l.Run(600);
    Check(l.s.phase == pdo::Phase::playing && l.plays == 1, "map loop playing");
    l.in.q18[1] = 1;
    l.Step();
    l.Run(200);
    Check(l.s.phase == pdo::Phase::playing && l.plays == 2, "map loop replayed once");
    l.in.q18[1] = 1;
    l.Step();
    Check(Is(l.out.event, "lost") && !pdo::Active(l.s), "map loop lost again: released");
    l.Run(2000);
    Check(!pdo::Active(l.s) && l.plays == 2, "no restart for the same remote episode (no suspend/resume thrash)");
    l.in.remote_act = 1;
    l.Run(400);
    l.in.remote_act = 77;
    l.Run(600);
    Check(pdo::Active(l.s) && l.plays == 3, "a new remote episode starts again");
    // Stale remote while mapped: map_exit.
    Game s;
    s.in.mode = PartnerNativeOverlay::map;
    s.in.remote_valid = true;
    s.in.remote_act = 77;
    s.Run(600);
    Check(s.s.phase == pdo::Phase::playing && s.s.loop, "box idle playing");
    s.in.remote_valid = false;
    s.Run(500);
    Check(s.Saw("released:map_exit"), "remote stale: map exit");
}

void TestSuspendTimeout() {
    Game g;
    g.stuck = true;
    g.Debug("stateDead", false, 10000);
    for (int i = 0; i < 120 && (i == 0 || pdo::Active(g.s)); ++i) g.Step(40);
    Check(g.Saw("abort:native_busy") && g.plays == 0, "native never cleared: abort after suspend_ms");
    Game m;
    m.in.map_idle = false;
    m.Debug("stateDead", false, 10000);
    m.Step();
    Check(m.out.release_map, "dm=map owns a clip: release it first");
    m.Run(200);
    Check(m.plays == 0 && m.patches == 0, "no patch/play while dm=map owns");
    m.in.map_idle = true;
    m.Run(100);
    Check(m.plays == 1, "plays once dm=map released");
}

} // namespace

int main(int argc, char** argv) {
    TestCatalogue(argc > 1 ? argv[1] : nullptr);
    TestMap();
    TestDebugFlow();
    TestDebugStopAndRefusals();
    TestPlayGate();
    TestUnconsumedAndLost();
    TestMapFlow();
    TestSuspendTimeout();
    std::printf("%u partner DM overlay checks passed: catalogue (verified graph-0 codes, clip paths, reply codes, Lua OVERLAY order%s), "
                "remote-act map, debug flow (suspend, patch, play, confirm, timeout stop, release), stop/refusals, play gate (camera "
                "connect, camera-skip hook, host DM, pending, reply ops), unconsumed, lost loop replay, one-shot finish, stop retry, "
                "host-DM stop hold, blocked stop, map (start hold, switch without native, exit, one-shot episode, native body, stale), "
                "suspend timeout, dm=map release\n",
                checks, argc > 1 ? "" : " not checked");
    return 0;
}
