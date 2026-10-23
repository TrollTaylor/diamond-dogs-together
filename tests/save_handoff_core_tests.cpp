// Join-fix phase 2 save handoff tests (spec track A): two Cores (host and
// guest) joined by an in-memory ordered link that can drop the session, with
// fake save files and a fake glue that executes every Action; the P2-7
// decisions, the P2-8 provenance, the P2-9 documents, the P2-10 exit gate and
// save_handoff_state. Pure; no Win32.
#include "mgscoop/save_handoff_core.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "mgscoop/link_crypto.hpp"
#include "mgscoop/save_handoff_state.hpp"

namespace {
namespace core = mgscoop::save_handoff_core;
namespace wire = mgscoop::save_handoff_wire;
namespace hs = mgscoop::save_handoff_state;
using hs::Toast;
using hs::Token;
using Bytes = std::vector<std::uint8_t>;

unsigned checks = 0;
void Check(bool value, const char* what, int line) {
    ++checks;
    if (!value) {
        std::fprintf(stderr, "failed check %u (line %d): %s\n", checks, line, what);
        std::exit(1);
    }
}
#define CHECK(x) Check((x), #x, __LINE__)

// ---- Fake save files.
struct Files {
    Bytes slot[wire::kSlots];
    bool unexpected = false;
    std::uint64_t total_override = 0;
};
Files MakeFiles(unsigned seed, std::uint32_t base, unsigned mask = 0x3F) {
    Files f;
    for (unsigned s = 0; s < wire::kSlots; ++s) {
        if (!(mask & (1u << s))) continue;
        f.slot[s].resize(base + s * 997);
        for (std::size_t i = 0; i < f.slot[s].size(); ++i)
            f.slot[s][i] = static_cast<std::uint8_t>(seed * 31 + s * 7 + i * 13 + (i >> 8));
    }
    return f;
}
// remote_primary: the guest glue's rule since online 2026-10-22 P1 (slot 4 is an identity too).
core::SetInfo Info(const Files& f, bool remote_primary = false) {
    core::SetInfo info{};
    info.stable = true;
    info.unexpected = f.unexpected;
    for (std::uint8_t s = 0; s < wire::kSlots; ++s) {
        if (f.slot[s].empty()) continue;
        wire::FileEntry& e = info.files[info.n++];
        e.slot = s;
        e.size = static_cast<std::uint32_t>(f.slot[s].size());
        mgscoop::link::Sha256Digest(f.slot[s].data(), f.slot[s].size(), e.sha256);
        info.total += e.size;
    }
    info.ok = info.n > 0 && (!f.slot[0].empty() || !f.slot[1].empty() || (remote_primary && !f.slot[4].empty()));
    if (f.total_override) info.total = f.total_override;
    wire::SetSha256(info.files, info.n, info.set);
    return info;
}

// ---- One side: a Core plus the fake glue and its half of the link.
struct ToastRec {
    std::uint8_t code;
    std::uint32_t a, b;
    std::uint64_t at;
};
struct Side {
    const char* name = "";
    core::Core* core = nullptr;
    Files files, snap_files;
    bool report_on = true;
    core::Report rep{};
    bool hash_req = false, snap_req = false;
    unsigned hashes = 0, snapshots = 0, releases = 0;
    bool snap_ok = true, snap_change = false;
    std::vector<std::string> logs;
    std::vector<ToastRec> toasts;
    hs::View view{};
    unsigned publishes = 0;
    std::map<std::uint32_t, std::array<Bytes, wire::kSlots>> partial, staged;
    std::vector<std::string> ops;
    unsigned write_errors = 0, requests = 0, manifests = 0, rejoins = 0, exits = 0, provenance_writes = 0, deletes = 0;
    std::string manifest_json, request_json, provenance_json;
    bool request_present = false, fail_stage_write = false, window = true;
    std::uint64_t exit_at = 0, request_at = 0;
    std::deque<Bytes> out;
    bool link_up = true;
    unsigned budget_per_tick = ~0u, chunk_budget = ~0u;
    unsigned max_out = 0, max_chunks_tick = 0, chunks_tick = 0, refused_chunks = 0;
    unsigned sends = 0, pump_rate = 1000;
    std::vector<std::pair<wire::Kind, std::uint8_t>> sent;  // kind, data[8]
    bool corrupt_in = false;
    unsigned drop_in = 0;  // message kinds (bit = kind) lost on the way to this side
    bool remote_primary = false;  // the glue hashes slot 4 as an identity (the guest, P1)
};

core::Core g_host_core, g_guest_core;
Side g_host, g_guest;
std::uint64_t g_now = 1000;
std::uint32_t g_session = 1;
bool g_verbose = false;  // --verbose prints the scene transfer's log lines

std::string Hex8(std::uint32_t v) {
    char text[16];
    std::snprintf(text, sizeof(text), "%08X", v);
    return text;
}

void Drain(Side& s) {
    core::Action a;
    bool refusing = false;
    while (s.core->PopAction(a)) {
        switch (a.kind) {
        case core::ActionKind::send: {
            bool ok = s.link_up;
            if (a.message == wire::Kind::chunk) {
                if (refusing || s.chunk_budget == 0) {
                    ok = false;
                    refusing = true;  // the glue refuses the rest of this drain's CHUNKs unsent
                    ++s.refused_chunks;
                } else if (ok) {
                    --s.chunk_budget;
                }
            }
            if (ok) {
                Bytes bytes(a.data, a.data + a.size);
                // ReliableMode::latest replaces the newest still-unsent message, of any kind.
                if (a.latest && !s.out.empty()) s.out.back() = bytes;
                else s.out.push_back(bytes);
                s.sent.emplace_back(a.message, a.data[8]);
                ++s.sends;
                if (a.message == wire::Kind::chunk) ++s.chunks_tick;
                if (a.message == wire::Kind::result)
                    s.ops.push_back(std::string("result:") + wire::ResultStatusName(static_cast<wire::ResultStatus>(a.data[8])));
                s.max_out = std::max<unsigned>(s.max_out, static_cast<unsigned>(s.out.size()));
            }
            s.core->OnSendResult(a, ok, g_now);
            break;
        }
        case core::ActionKind::hash_set:
            s.hash_req = true;
            ++s.hashes;
            break;
        case core::ActionKind::snapshot:
            s.snap_req = true;
            ++s.snapshots;
            break;
        case core::ActionKind::release_snapshot: ++s.releases; break;
        case core::ActionKind::stage_write: {
            const bool ok = !s.fail_stage_write;
            if (ok) {
                Bytes& v = s.partial[a.xfer][a.slot];
                if (v.size() != a.offset) ++s.write_errors;  // a gap or a duplicate
                v.insert(v.end(), a.data, a.data + a.size);
            }
            s.ops.push_back("write");
            s.core->OnStageDone(ok, g_now);
            break;
        }
        case core::ActionKind::write_manifest: {
            char text[core::kManifestJsonMax];
            const std::size_t n = core::FormatManifest(s.core->manifest(), text, sizeof(text));
            s.manifest_json.assign(text, n);
            ++s.manifests;
            s.ops.push_back("manifest");
            s.core->OnStageDone(n != 0, g_now);
            break;
        }
        case core::ActionKind::commit_stage:
            s.staged[a.xfer] = s.partial[a.xfer];
            s.partial.erase(a.xfer);
            s.ops.push_back("commit");
            s.core->OnStageDone(true, g_now);
            break;
        case core::ActionKind::write_request: {
            char text[core::kRequestJsonMax];
            const std::size_t n = core::FormatRequest(s.core->request(), text, sizeof(text));
            s.request_json.assign(text, n);
            ++s.requests;
            s.request_present = n != 0;
            s.request_at = g_now;
            s.ops.push_back("request");
            s.core->OnStageDone(n != 0, g_now);
            break;
        }
        case core::ActionKind::write_rejoin:
            ++s.rejoins;
            s.ops.push_back("rejoin");
            break;
        case core::ActionKind::write_provenance: {
            char text[core::kProvenanceJsonMax];
            const std::size_t n = s.core->provenance().Format(text, sizeof(text));
            s.provenance_json.assign(text, n);
            ++s.provenance_writes;
            break;
        }
        case core::ActionKind::delete_stage:
            // request.json first, then the xfer's known names.
            s.request_present = false;
            s.partial.erase(a.xfer);
            s.staged.erase(a.xfer);
            ++s.deletes;
            s.ops.push_back("delete:" + Hex8(a.xfer));
            s.core->OnStageDone(true, g_now);
            break;
        case core::ActionKind::post_exit:
            ++s.exits;
            s.exit_at = g_now;
            s.ops.push_back("exit");
            s.core->OnExitPosted(s.window, g_now);
            break;
        case core::ActionKind::toast: s.toasts.push_back({a.code, a.a, a.b, g_now}); break;
        case core::ActionKind::log: s.logs.emplace_back(a.text); break;
        case core::ActionKind::publish:
            s.view = a.view;
            ++s.publishes;
            break;
        case core::ActionKind::none: break;
        }
    }
}

void Respond(Side& s) {
    if (s.hash_req) {
        s.hash_req = false;
        s.core->OnSetHashed(Info(s.files, s.remote_primary), g_now);
        Drain(s);
    }
    if (s.snap_req) {
        s.snap_req = false;
        s.snap_files = s.files;
        if (s.snap_change) s.snap_files.slot[0][0] ^= 0xFF;  // changed between the two reads
        core::Snapshot snap{};
        snap.info = Info(s.snap_files);
        for (unsigned i = 0; i < wire::kSlots; ++i) snap.data[i] = s.snap_files.slot[i].empty() ? nullptr : s.snap_files.slot[i].data();
        s.core->OnSnapshotRead(s.snap_ok, s.snap_ok ? &snap : nullptr, g_now);
        Drain(s);
    }
}

void Pump(Side& from, Side& to) {
    for (unsigned n = 0; n < from.pump_rate && !from.out.empty(); ++n) {
        Bytes m = from.out.front();
        from.out.pop_front();
        if (to.corrupt_in && m.size() > wire::kChunkHeaderSize && m[2] == 5) m[wire::kChunkHeaderSize] ^= 0x5A;
        if (to.drop_in & (1u << m[2])) continue;
        to.core->OnPeer(m.data(), m.size(), g_now);
        Drain(to);
    }
}

void Step() {
    g_now += 20;
    for (Side* s : {&g_host, &g_guest}) {
        if (!s->report_on) continue;
        s->rep.at_ms = g_now;
        s->core->OnLua(s->rep, g_now);
        Drain(*s);
    }
    Pump(g_host, g_guest);
    Pump(g_guest, g_host);
    for (Side* s : {&g_host, &g_guest}) {
        s->chunks_tick = 0;
        s->chunk_budget = s->budget_per_tick;
        s->core->Tick(g_now, static_cast<unsigned>(s->out.size()));
        Drain(*s);
        s->max_chunks_tick = std::max(s->max_chunks_tick, s->chunks_tick);
        Respond(*s);
    }
}

bool Run(std::uint64_t ms, bool (*until)() = nullptr) {
    const std::uint64_t end = g_now + ms;
    while (g_now < end) {
        Step();
        if (until && until()) return true;
    }
    return until ? until() : true;
}

void NewSession() {
    g_host.out.clear();
    g_guest.out.clear();
    ++g_session;
    for (Side* s : {&g_host, &g_guest}) {
        s->core->OnSession(g_session, g_now);
        Drain(*s);
    }
}

core::Report Title(std::uint32_t mission, wire::Story story = wire::Story::open) {
    core::Report r{};
    r.mission = mission;
    r.title = true;
    r.story = story;
    return r;
}
core::Report InGame(std::uint32_t mission, bool saving = false) {
    core::Report r{};
    r.mission = mission;
    r.ingame = true;
    r.story = wire::Story::open;
    r.saving = saving;
    return r;
}

void Setup(wire::Mode host_mode, wire::Mode guest_mode, bool build_ok = true, bool rejoin = false) {
    g_now = 1000;
    g_session = 1;
    g_host = Side{};
    g_guest = Side{};
    g_host.name = "host";
    g_guest.name = "guest";
    g_host.core = &g_host_core;
    g_guest.core = &g_guest_core;
    core::Config hc{};
    hc.role = wire::Role::host;
    hc.mode = host_mode;
    g_host_core.Reset(hc);
    core::Config gc{};
    gc.role = wire::Role::guest;
    gc.mode = guest_mode;
    gc.pid = 4242;
    gc.rejoin = rejoin;
    std::snprintf(gc.process_start_utc, sizeof(gc.process_start_utc), "%s", "2026-10-15T10:00:00Z");
    g_guest_core.Reset(gc);
    g_host.files = MakeFiles(1, 3000);
    g_guest.files = MakeFiles(2, 2000);
    g_host.rep = Title(40010);
    g_guest.rep = Title(40050);
    for (Side* s : {&g_host, &g_guest}) {
        Drain(*s);
        s->core->OnSession(g_session, g_now);
        s->core->OnBuildOk(build_ok, build_ok ? "0.13.0+test" : "0.12.6+c07d0a2");
        Drain(*s);
    }
}

void BindHost(std::uint32_t mission) {
    core::Provenance p;
    const core::SetInfo info = Info(g_host.files);
    p.Bind(info.set, mission, core::Source::continue_, "2026-10-15T09:00:00Z");
    char text[core::kProvenanceJsonMax];
    const std::size_t n = p.Format(text, sizeof(text));
    CHECK(n > 0);
    CHECK(g_host.core->LoadProvenance(text, n));
    Drain(g_host);
}

bool HasLog(const Side& s, const char* text) {
    for (const auto& l : s.logs)
        if (l.find(text) != std::string::npos) return true;
    return false;
}
unsigned CountLog(const Side& s, const char* text) {
    unsigned n = 0;
    for (const auto& l : s.logs)
        if (l.find(text) != std::string::npos) ++n;
    return n;
}
unsigned CountToast(const Side& s, Toast code) {
    unsigned n = 0;
    for (const auto& t : s.toasts)
        if (t.code == static_cast<std::uint8_t>(code)) ++n;
    return n;
}
const ToastRec* FirstToast(const Side& s, Toast code) {
    for (const auto& t : s.toasts)
        if (t.code == static_cast<std::uint8_t>(code)) return &t;
    return nullptr;
}
Token TokenOf(const Side& s) { return static_cast<Token>(s.view.token); }
unsigned CountSent(const Side& s, wire::Kind kind) {
    unsigned n = 0;
    for (const auto& k : s.sent)
        if (k.first == kind) ++n;
    return n;
}
std::ptrdiff_t OpIndex(const Side& s, const std::string& op) {
    const auto it = std::find(s.ops.begin(), s.ops.end(), op);
    return it == s.ops.end() ? -1 : it - s.ops.begin();
}
bool StagedEqualsHost() {
    if (g_guest.staged.size() != 1) return false;
    const auto& st = g_guest.staged.begin()->second;
    for (unsigned s = 0; s < wire::kSlots; ++s)
        if (st[s] != g_host.files.slot[s]) return false;
    return true;
}
bool GuestExited() { return g_guest.exits > 0; }
bool GuestStaged() { return !g_guest.staged.empty(); }
bool GuestCountdown() { return TokenOf(g_guest) == Token::countdown; }
bool GuestReceiving() { return TokenOf(g_guest) == Token::receiving && g_guest.view.pct >= 10; }

// ---- (1) + (20): the scene-mode transfer, the countdown and the exit.
void TestTransferScene() {
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30010);
    CHECK(Run(20000, GuestCountdown));
    CHECK(g_guest.exits == 0);
    CHECK(HasLog(g_host, "save_handoff=offer; xfer="));
    CHECK(HasLog(g_host, "; files=6; bytes="));
    CHECK(HasLog(g_host, "; mission=30010"));
    CHECK(HasLog(g_guest, "save_handoff=need; need=1; why=location"));
    CHECK(HasLog(g_guest, "save_handoff=decide; result=want; reason=location"));
    CHECK(HasLog(g_host, "save_handoff=want_rx; xfer="));
    CHECK(HasLog(g_guest, "save_handoff=staged; xfer="));
    CHECK(HasLog(g_guest, "save_handoff=countdown; ms=10000"));
    for (const char* p : {"pct=25", "pct=50", "pct=75", "pct=100"}) CHECK(CountLog(g_guest, p) == 1);
    CHECK(HasLog(g_guest, "save_handoff=state_rx; role=host; phase=2; story=2; mission=40010; bound=30010; need=0; elig=1; set="));
    CHECK(HasLog(g_host, "save_handoff=state_rx; role=guest; phase=2; story=2; mission=40050; bound=0; need=1; elig=0; set="));
    // Staged bytes equal the host's; no gaps or duplicates.
    CHECK(StagedEqualsHost());
    CHECK(g_guest.write_errors == 0);
    CHECK(g_guest.partial.empty());
    // .partial writes, then handoff.json, the rename, RESULT staged, request.json last.
    const auto manifest = OpIndex(g_guest, "manifest"), commit = OpIndex(g_guest, "commit");
    const auto result = OpIndex(g_guest, "result:staged"), request = OpIndex(g_guest, "request");
    CHECK(OpIndex(g_guest, "write") >= 0 && OpIndex(g_guest, "write") < manifest);
    CHECK(manifest < commit && commit < result && result < request);
    CHECK(g_guest.requests == 1 && g_guest.request_present && g_guest.manifests == 1);
    // The documents.
    core::Request q{};
    CHECK(core::ParseRequest(g_guest.request_json.data(), g_guest.request_json.size(), q));
    const core::SetInfo host_info = Info(g_host.files);
    CHECK(q.pid == 4242 && q.mode == wire::Mode::scene && q.rejoin == 0);
    CHECK(std::memcmp(q.set, host_info.set, wire::kShaSize) == 0);
    CHECK(std::strcmp(q.process_start_utc, "2026-10-15T10:00:00Z") == 0);
    CHECK(std::strcmp(q.created_utc, "1970-01-01T00:00:00Z") == 0);
    CHECK(g_guest.staged.begin()->first == q.xfer);
    CHECK(g_guest.request_json.find("\"xfer\":\"" + Hex8(q.xfer) + "\"") != std::string::npos);
    core::Manifest m{};
    CHECK(core::ParseManifest(g_guest.manifest_json.data(), g_guest.manifest_json.size(), m));
    CHECK(m.xfer == q.xfer && m.host_mission == 30010 && m.n == 6 && std::strcmp(m.host_build, "0.13.0+test") == 0);
    CHECK(std::memcmp(m.set, host_info.set, wire::kShaSize) == 0);
    for (unsigned i = 0; i < 6; ++i)
        CHECK(m.files[i].slot == i && m.files[i].size == host_info.files[i].size &&
              std::memcmp(m.files[i].sha256, host_info.files[i].sha256, wire::kShaSize) == 0);
    CHECK(g_guest.manifest_json.find("\"entry\":\"287700/local/TPP_GAME_DATA0\"") != std::string::npos);
    // Views: guest countdown (Lua staged), F9 keep allowed; host partner_staged.
    CHECK(g_guest.view.available && g_guest.view.role == 2 && g_guest.view.lua_code == 3 && g_guest.view.can_keep);
    CHECK(g_guest.view.host_mission == 30010 && g_guest.view.bytes == host_info.total && g_guest.view.pct == 100);
    CHECK(TokenOf(g_host) == Token::partner_staged && g_host.view.lua_code == 0 && !g_host.view.can_keep);
    CHECK(g_host.releases >= 1);  // the snapshot is freed once the guest staged
    // PostExit only after 10 s at the title.
    CHECK(Run(9800, GuestExited) == false);
    CHECK(Run(400, GuestExited));
    CHECK(g_guest.exit_at - g_guest.request_at >= core::kCountdownMs);
    CHECK(g_guest.exit_at - g_guest.request_at < core::kCountdownMs + 100);
    CHECK(OpIndex(g_guest, "result:exiting") >= 0 && OpIndex(g_guest, "result:exiting") > request);
    CHECK(OpIndex(g_guest, "exit") > OpIndex(g_guest, "result:exiting"));
    CHECK(g_guest.rejoins == 0);
    CHECK(HasLog(g_guest, "save_handoff=exit; how=wm_close"));
    CHECK(TokenOf(g_guest) == Token::exiting && g_guest.view.lua_code == 4 && !g_guest.view.can_keep);
    Run(200);
    CHECK(TokenOf(g_host) == Token::partner_restarting);
    CHECK(HasLog(g_host, "save_handoff=result_rx; status=staged"));
    CHECK(HasLog(g_host, "save_handoff=result_rx; status=exiting"));
    // (20) exactly one toast per transition; G2 counts 10..1 once each.
    CHECK(CountToast(g_guest, Toast::g1_receiving) == 1);
    CHECK(FirstToast(g_guest, Toast::g1_receiving)->a == (host_info.total + 1023) / 1024);
    CHECK(CountToast(g_guest, Toast::g2_countdown) == 10);
    unsigned expect = 10;
    for (const auto& t : g_guest.toasts)
        if (t.code == static_cast<std::uint8_t>(Toast::g2_countdown)) CHECK(t.a == expect--);
    CHECK(CountToast(g_guest, Toast::g3_stage_only) == 0 && CountToast(g_guest, Toast::g4_wait_host) == 0);
    CHECK(CountToast(g_guest, Toast::g7_failed) == 0 && CountToast(g_guest, Toast::g9_quit_manually) == 0);
    CHECK(CountToast(g_host, Toast::h1_sending) == 1 && CountToast(g_host, Toast::h2_partner_restarting) == 1);
    CHECK(FirstToast(g_host, Toast::h2_partner_restarting)->a == 30010);
    CHECK(g_host.toasts.size() == 2);
    CHECK(g_guest.toasts.size() == 11);
    // Still alive 20 s after a WM_CLOSE: the glue's timer owns exit_slow and G9
    // (PartnerShutdown stops this core; save_handoff_io_tests ExitSlow). The
    // core logs it only for no_window (TestEdges).
    Run(25000);
    CHECK(CountLog(g_guest, "save_handoff=exit_slow") == 0);
    CHECK(CountToast(g_guest, Toast::g9_quit_manually) == 0);
    CHECK(TokenOf(g_guest) == Token::exiting && g_guest.request_present);  // staleness never cancels an exit
    CHECK(g_guest.exits == 1 && g_guest.requests == 1);
    CHECK(CountLog(g_host, "save_handoff=offer;") == 1);
    const core::Stats hs_stats = g_host.core->stats(), gs = g_guest.core->stats();
    CHECK(hs_stats.bad_msg == 0 && gs.bad_msg == 0 && hs_stats.dropped_actions == 0 && gs.dropped_actions == 0);
    CHECK(hs_stats.offers == 1 && hs_stats.chunks_sent == gs.chunks_received && gs.chunks_received > 0);
    if (g_verbose) {
        for (const Side* s : {&g_host, &g_guest})
            for (const auto& l : s->logs) std::printf("%s: %s\n", s->name, l.c_str());
    }
}

// (1) stage mode: staged and stored, never request.json, never an exit; a new
// session reuses the verified stage (RESULT staged without WANT).
void TestTransferStage() {
    Setup(wire::Mode::stage, wire::Mode::stage);
    BindHost(30010);
    CHECK(Run(20000, GuestStaged));
    Run(60000);
    CHECK(StagedEqualsHost());
    CHECK(g_guest.requests == 0 && g_guest.exits == 0 && !g_guest.request_present);
    CHECK(TokenOf(g_guest) == Token::staged && g_guest.view.lua_code == 3);
    CHECK(CountToast(g_guest, Toast::g3_stage_only) == 1);
    CHECK(!HasLog(g_guest, "save_handoff=countdown"));
    CHECK(TokenOf(g_host) == Token::partner_staged);
    CHECK(CountLog(g_guest, "result=want") == 1);
    NewSession();
    Run(10000);
    CHECK(CountLog(g_host, "save_handoff=offer;") == 2);
    CHECK(HasLog(g_guest, "save_handoff=decide; result=staged; reason=reuse"));
    CHECK(CountLog(g_guest, "result=want") == 1);
    CHECK(CountSent(g_guest, wire::Kind::want) == 1);
    CHECK(TokenOf(g_host) == Token::partner_staged && TokenOf(g_guest) == Token::staged);
    CHECK(g_guest.staged.size() == 1 && g_guest.deletes == 0);
    CHECK(g_guest.requests == 0 && g_guest.exits == 0);
}

// (2) identical sets: no OFFER.
void TestIdenticalSets() {
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_guest.files = g_host.files;
    BindHost(30010);
    Run(20000);
    CHECK(!HasLog(g_host, "save_handoff=offer;"));
    CHECK(HasLog(g_guest, "save_handoff=need; need=0; why=same"));
    CHECK(CountLog(g_guest, "save_handoff=need;") == 1);
    CHECK(TokenOf(g_guest) == Token::same && g_guest.view.lua_code == 5 && !g_guest.view.can_keep);
    CHECK(TokenOf(g_host) == Token::same);
    CHECK(g_guest.toasts.empty() && g_host.toasts.empty() && g_host.snapshots == 0);
}

// (3) same scene (40010 vs bound 30010) needs nothing; policy always does.
void TestSameScene() {
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_guest.rep = Title(40010);
    BindHost(30010);
    Run(20000);
    CHECK(HasLog(g_guest, "save_handoff=need; need=0; why=same_scene"));
    CHECK(!HasLog(g_host, "save_handoff=offer;"));
    CHECK(TokenOf(g_guest) == Token::same);
    Setup(wire::Mode::scene, wire::Mode::always);
    g_guest.rep = Title(40010);
    BindHost(30010);
    CHECK(Run(20000, GuestCountdown));
    CHECK(HasLog(g_guest, "save_handoff=need; need=1; why=always"));
    CHECK(HasLog(g_guest, "save_handoff=decide; result=want; reason=always"));
    core::Request q{};
    CHECK(core::ParseRequest(g_guest.request_json.data(), g_guest.request_json.size(), q) && q.mode == wire::Mode::always);
    CHECK(StagedEqualsHost());
}

// (4) a guest save at the story start needs the host's save even in the same place.
void TestGuestStory() {
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_guest.rep = Title(40010, wire::Story::opening);
    BindHost(30010);
    CHECK(Run(20000, GuestCountdown));
    CHECK(HasLog(g_guest, "save_handoff=need; need=1; why=story"));
    CHECK(StagedEqualsHost());
}

// (5) a host story at the opening or unknown: no OFFER.
void TestHostStory() {
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_host.rep = Title(40010, wire::Story::opening);
    BindHost(30010);
    Run(30000);
    CHECK(!HasLog(g_host, "save_handoff=offer;") && g_host.snapshots == 0);
    CHECK(HasLog(g_guest, "; elig=2; set="));
    CHECK(TokenOf(g_guest) == Token::refused && g_guest.view.why == static_cast<std::uint8_t>(wire::Elig::story_opening));
    CHECK(g_guest.view.lua_code == 6);
    CHECK(CountToast(g_guest, Toast::g5_host_story_opening) == 1 && g_guest.toasts.size() == 1);
    CHECK(TokenOf(g_host) == Token::refused && g_host.view.why == static_cast<std::uint8_t>(wire::Elig::story_opening));
    CHECK(CountToast(g_host, Toast::h4_story_opening) == 1 && g_host.toasts.size() == 1);
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_host.rep = Title(40010, wire::Story::unknown);
    BindHost(30010);
    Run(30000);
    CHECK(!HasLog(g_host, "save_handoff=offer;"));
    CHECK(HasLog(g_guest, "; elig=3; set="));
    CHECK(TokenOf(g_guest) == Token::wait_host && g_guest.view.lua_code == 1 && g_guest.view.can_keep);
    CHECK(g_guest.toasts.empty() && g_host.toasts.empty());
    // A stale host report counts as story_unknown as well.
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30010);
    g_guest.rep = Title(40010);  // same scene: no transfer either way
    g_host.report_on = false;
    Run(1000);
    CHECK(HasLog(g_guest, "; elig=0; set="));  // never hashed: unknown
    g_host.report_on = true;
    Run(5000);
    CHECK(HasLog(g_guest, "; elig=1; set="));
    CHECK(!HasLog(g_guest, "; elig=3; set="));
    g_host.report_on = false;
    Run(10000);
    CHECK(!HasLog(g_host, "save_handoff=offer;"));
    CHECK(HasLog(g_guest, "; elig=3; set="));
}

// (6) unbound host set: continue_unknown; a Continue bind makes it eligible.
void TestUnbound() {
    Setup(wire::Mode::scene, wire::Mode::scene);
    Run(60000);
    CHECK(!HasLog(g_host, "save_handoff=offer;"));
    CHECK(HasLog(g_guest, "; elig=4; set="));
    CHECK(TokenOf(g_guest) == Token::wait_host && g_guest.view.lua_code == 1);
    CHECK(CountToast(g_guest, Toast::g4_wait_host) == 1 && g_guest.toasts.size() == 1);  // (20): once over many heartbeats
    CHECK(TokenOf(g_host) == Token::refused && g_host.view.why == static_cast<std::uint8_t>(wire::Elig::continue_unknown));
    CHECK(CountToast(g_host, Toast::h3_partner_elsewhere) == 1 && g_host.toasts.size() == 1);
    CHECK(g_host.provenance_writes == 0);
    g_host.core->OnContinue(30010, g_now);
    Drain(g_host);
    CHECK(Run(20000, GuestCountdown));
    CHECK(HasLog(g_host, "save_handoff=continue; target=30010"));
    const core::SetInfo info = Info(g_host.files);
    char hex[wire::kShaHexChars + 1];
    wire::FormatShaHex(info.set, hex);
    CHECK(HasLog(g_host, (std::string("save_handoff=provenance; set=") + std::string(hex, 8) + "; mission=30010; source=continue").c_str()));
    CHECK(g_host.provenance_writes == 1);
    core::Provenance p;
    CHECK(p.Parse(g_host.provenance_json.data(), g_host.provenance_json.size()) && p.Count() == 1);
    CHECK(p.At(0).mission == 30010 && p.At(0).source == core::Source::continue_);
    CHECK(StagedEqualsHost());
    // The guest binds nothing without its own Continue or settle.
    CHECK(g_guest.provenance_writes == 0);
}

// (7) a set bound to 10020: continue_unsupported.
void TestUnsupported() {
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(10020);
    Run(30000);
    CHECK(!HasLog(g_host, "save_handoff=offer;"));
    CHECK(HasLog(g_guest, "; bound=10020; need=0; elig=5; set="));
    CHECK(TokenOf(g_guest) == Token::wait_host);
    CHECK(CountToast(g_guest, Toast::g6_host_unsupported) == 1 && FirstToast(g_guest, Toast::g6_host_unsupported)->a == 10020);
    CHECK(CountToast(g_host, Toast::h7_unsupported) == 1 && FirstToast(g_host, Toast::h7_unsupported)->a == 10020);
    CHECK(TokenOf(g_host) == Token::refused && g_host.view.why == static_cast<std::uint8_t>(wire::Elig::continue_unsupported));
}

// (8) settle bind.
std::uint64_t SettleBindTime(std::uint64_t limit, void (*during)(std::uint64_t elapsed)) {
    const std::uint64_t start = g_now;
    while (g_now - start < limit) {
        if (during) during(g_now - start);
        Step();
        if (HasLog(g_host, "source=settle")) return g_now - start;
    }
    return 0;
}
void TestSettle() {
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_host.rep = InGame(30010);
    const std::uint64_t t = SettleBindTime(60000, nullptr);
    CHECK(t >= core::kSettleBindMs && t <= core::kSettleBindMs + 2100);
    CHECK(HasLog(g_host, "; mission=30010; source=settle"));
    CHECK(g_host.provenance_writes == 1);
    Run(60000);
    CHECK(CountLog(g_host, "save_handoff=provenance;") == 1);  // no churn while nothing changes
    // Online 2026-10-22 P3: the window is 10 s (30 s until 0.13.5).
    CHECK(core::kSettleBindMs == 10000);
    // A set change inside the window restarts it.
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_host.rep = InGame(30020);
    const std::uint64_t changed = SettleBindTime(90000, [](std::uint64_t e) {
        if (e == 6000) g_host.files.slot[2][5] ^= 1;
    });
    CHECK(changed >= 6000 + core::kSettleBindMs && changed <= 6000 + core::kSettleBindMs + 4200);
    CHECK(HasLog(g_host, "; mission=30020; source=settle"));
    // saving=1 inside the window restarts it.
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_host.rep = InGame(30050);
    const std::uint64_t saved = SettleBindTime(90000, [](std::uint64_t e) {
        if (e == 5000) g_host.rep.saving = true;
        if (e == 6000) g_host.rep.saving = false;
    });
    CHECK(saved >= 6000 + core::kSettleBindMs && saved <= 6000 + core::kSettleBindMs + 4200);
    // A report gap restarts it too.
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_host.rep = InGame(30010);
    const std::uint64_t gap = SettleBindTime(90000, [](std::uint64_t e) {
        if (e == 3000) g_host.report_on = false;
        if (e == 7000) g_host.report_on = true;
    });
    CHECK(gap >= 7000 + core::kSettleBindMs);
    // A load (Lua reports loading at once, world_handoff X.leaving) ends the
    // window: a set written just before it is never bound to the old mission.
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_host.rep = InGame(30050);
    const std::uint64_t left = SettleBindTime(40000, [](std::uint64_t e) {
        if (e == 8000) g_host.files.slot[0][7] ^= 1;  // SaveGameData(next) right before leaving
        if (e == 8500) g_host.rep = core::Report{};    // loading (not in game, not the title)
    });
    CHECK(left == 0);
    CHECK(!HasLog(g_host, "save_handoff=provenance;"));
    // Never at a title (40050) or in an unsupported mission (10020).
    for (const core::Report& r : {Title(40050), InGame(40050), InGame(10020)}) {
        Setup(wire::Mode::scene, wire::Mode::scene);
        g_host.rep = r;
        CHECK(SettleBindTime(70000, nullptr) == 0);
        CHECK(!HasLog(g_host, "save_handoff=provenance;"));
    }
}

// (9) a new link session at 40 %: the partial is dropped, the new OFFER starts at 0.
void TestSessionReset() {
    Setup(wire::Mode::scene, wire::Mode::stage);
    g_host.files = MakeFiles(1, 12000);
    g_host.pump_rate = 4;
    BindHost(30010);
    CHECK(Run(30000, [] { return TokenOf(g_guest) == Token::receiving && g_guest.view.pct >= 40; }));
    CHECK(g_guest.view.pct < 60);
    CHECK(g_guest.partial.size() == 1);
    const std::uint32_t old_xfer = g_guest.partial.begin()->first;
    NewSession();
    CHECK(HasLog(g_guest, ("save_handoff=abort; why=link_lost; xfer=" + Hex8(old_xfer)).c_str()));
    CHECK(HasLog(g_host, ("save_handoff=abort; why=link_lost; xfer=" + Hex8(old_xfer)).c_str()));
    CHECK(g_guest.partial.empty() && g_guest.deletes == 1);
    CHECK(OpIndex(g_guest, "delete:" + Hex8(old_xfer)) >= 0);
    CHECK(TokenOf(g_guest) != Token::failed && CountToast(g_guest, Toast::g7_failed) == 0);
    g_host.pump_rate = 1000;
    CHECK(Run(30000, GuestStaged));
    CHECK(CountLog(g_host, "save_handoff=offer;") == 2);
    CHECK(g_guest.staged.size() == 1 && g_guest.staged.begin()->first != old_xfer);
    CHECK(StagedEqualsHost());
    CHECK(g_guest.write_errors == 0);
    CHECK(g_host.snapshots == 1);  // the held snapshot served the second OFFER
    CHECK(CountToast(g_guest, Toast::g1_receiving) == 2);
}

// (10) a corrupted chunk: RESULT failed hash_mismatch, no request, G7; at most 3 OFFERs.
void TestCorruptChunk() {
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30010);
    g_guest.corrupt_in = true;
    Run(120000);
    CHECK(CountLog(g_host, "save_handoff=offer;") == 3);
    CHECK(CountLog(g_guest, "save_handoff=failed; why=hash_mismatch") == 3);
    CHECK(CountToast(g_guest, Toast::g7_failed) == 3 && FirstToast(g_guest, Toast::g7_failed)->a == 1);
    CHECK(CountToast(g_host, Toast::h6_failed) == 3 && FirstToast(g_host, Toast::h6_failed)->a == 1);
    CHECK(g_guest.requests == 0 && !g_guest.request_present && g_guest.staged.empty() && g_guest.partial.empty());
    CHECK(g_guest.exits == 0 && g_guest.deletes == 3);
    CHECK(TokenOf(g_guest) == Token::failed && g_guest.view.why == 1 && g_guest.view.lua_code == 6);
    CHECK(TokenOf(g_host) == Token::failed && g_host.view.why == 1);
    unsigned failed_results = 0;
    for (const auto& k : g_guest.sent)
        if (k.first == wire::Kind::result && k.second == static_cast<std::uint8_t>(wire::ResultStatus::failed)) ++failed_results;
    CHECK(failed_results == 3);
    // A new link session allows 3 more.
    NewSession();
    Run(60000);
    CHECK(CountLog(g_host, "save_handoff=offer;") == 6);
}

// (11) a disk error at StageWrite: failed, DeleteStage, no request.
void TestDiskError() {
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30010);
    g_guest.fail_stage_write = true;
    CHECK(Run(20000, [] { return TokenOf(g_guest) == Token::failed; }));
    CHECK(HasLog(g_guest, "save_handoff=failed; why=disk"));
    CHECK(g_guest.deletes == 1 && g_guest.requests == 0 && g_guest.manifests == 0);
    CHECK(FirstToast(g_guest, Toast::g7_failed)->a == static_cast<std::uint32_t>(wire::Fail::disk));
    Run(1000);
    CHECK(FirstToast(g_host, Toast::h6_failed) && FirstToast(g_host, Toast::h6_failed)->a == static_cast<std::uint32_t>(wire::Fail::disk));
    Run(60000);
    CHECK(CountLog(g_host, "save_handoff=offer;") == 3);
    CHECK(g_guest.requests == 0 && g_guest.exits == 0);
}

// (12) a Lua decline while receiving or in the countdown.
void TestDecline() {
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_host.files = MakeFiles(1, 12000);
    g_host.pump_rate = 2;
    BindHost(30010);
    CHECK(Run(30000, GuestReceiving));
    CHECK(g_guest.view.can_keep);
    g_guest.core->OnDecline(core::DeclineSource::second_continue, g_now);
    Drain(g_guest);
    CHECK(HasLog(g_guest, "save_handoff=declined; source=second_continue"));
    CHECK(HasLog(g_guest, "save_handoff=abort; why=user; xfer="));
    CHECK(g_guest.sent.back().first == wire::Kind::abort && g_guest.sent.back().second == static_cast<std::uint8_t>(wire::AbortReason::user));
    CHECK(g_guest.deletes == 1 && g_guest.partial.empty());
    CHECK(TokenOf(g_guest) == Token::kept && g_guest.view.why == static_cast<std::uint8_t>(wire::DeclineReason::user));
    CHECK(g_guest.view.lua_code == 5 && !g_guest.view.can_keep);
    CHECK(CountToast(g_guest, Toast::g8_kept) == 1);
    g_host.pump_rate = 1000;
    Run(60000);
    CHECK(g_guest.exits == 0 && g_guest.requests == 0 && TokenOf(g_guest) == Token::kept);
    CHECK(TokenOf(g_host) == Token::kept && CountToast(g_host, Toast::h5_partner_kept) == 1);
    CHECK(HasLog(g_host, "save_handoff=abort_rx; why=user"));
    CHECK(CountLog(g_host, "save_handoff=offer;") == 1);
    // A later session: the guest no longer reports a need, so no OFFER.
    NewSession();
    Run(30000);
    CHECK(CountLog(g_host, "save_handoff=offer;") == 1 && CountToast(g_guest, Toast::g8_kept) == 1);

    // In the countdown (F9): the request goes first, then the stage; no PostExit.
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30010);
    CHECK(Run(20000, GuestCountdown));
    Run(3000);
    CHECK(g_guest.request_present);
    const std::uint32_t xfer = g_guest.staged.begin()->first;
    hs::ResetForTest();
    hs::RequestKeepOwnSave();
    CHECK(hs::TakeKeepOwnSave());  // the glue takes it once ...
    CHECK(!hs::TakeKeepOwnSave());
    g_guest.core->OnDecline(core::DeclineSource::f9, g_now);  // ... and feeds the core
    Drain(g_guest);
    CHECK(HasLog(g_guest, "save_handoff=declined; source=f9"));
    CHECK(!g_guest.request_present && g_guest.staged.empty());
    CHECK(OpIndex(g_guest, "delete:" + Hex8(xfer)) > OpIndex(g_guest, "request"));
    CHECK(TokenOf(g_guest) == Token::kept && CountToast(g_guest, Toast::g8_kept) == 1);
    Run(30000);
    CHECK(g_guest.exits == 0);
    CHECK(TokenOf(g_host) == Token::kept && CountToast(g_host, Toast::h5_partner_kept) == 1);
    // A wait_host decline: kept, G8, and the guest stops asking.
    Setup(wire::Mode::scene, wire::Mode::scene);
    Run(10000);
    CHECK(TokenOf(g_guest) == Token::wait_host);
    g_guest.core->OnDecline(core::DeclineSource::second_continue, g_now);
    Drain(g_guest);
    CHECK(TokenOf(g_guest) == Token::kept && CountToast(g_guest, Toast::g8_kept) == 1);
    g_host.core->OnContinue(30010, g_now);
    Drain(g_host);
    Run(30000);
    CHECK(!HasLog(g_host, "save_handoff=offer;"));
    CHECK(TokenOf(g_host) != Token::refused);
}

// (13) the title lost for more than 2 s in the countdown: ABORT not_title.
void TestTitleLost() {
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30010);
    CHECK(Run(20000, GuestCountdown));
    Run(3000);
    g_guest.rep = core::Report{};  // loading: not the title, not in game
    g_guest.rep.mission = 30050;
    g_guest.rep.story = wire::Story::open;
    CHECK(!Run(1900, [] { return TokenOf(g_guest) == Token::failed; }));
    CHECK(Run(400, [] { return TokenOf(g_guest) == Token::failed; }));
    CHECK(HasLog(g_guest, "save_handoff=abort; why=not_title; xfer="));
    CHECK(HasLog(g_guest, "save_handoff=failed; why=not_title"));
    CHECK(g_guest.view.why == static_cast<std::uint8_t>(wire::Fail::not_title));
    CHECK(FirstToast(g_guest, Toast::g7_failed)->a == static_cast<std::uint32_t>(wire::Fail::not_title));
    CHECK(!g_guest.request_present && g_guest.staged.empty());
    Run(20000);
    CHECK(g_guest.exits == 0);
    // A brief loss (1 s) is tolerated and the exit still comes after 10 s.
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30010);
    CHECK(Run(20000, GuestCountdown));
    Run(3000);
    g_guest.rep.title = false;
    Run(1000);
    g_guest.rep.title = true;
    CHECK(Run(7000, GuestExited));
    CHECK(g_guest.exit_at - g_guest.request_at >= core::kCountdownMs);
}

// (14) a Lua report older than 1500 ms blocks PostExit.
void TestStaleReport() {
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30010);
    CHECK(Run(20000, GuestCountdown));
    const std::uint64_t start = g_guest.request_at;
    Run(start + 8000 - g_now);
    g_guest.report_on = false;
    CHECK(!Run(start + 10500 - g_now, GuestExited));
    g_guest.report_on = true;
    CHECK(Run(100, GuestExited));
    CHECK(TokenOf(g_guest) == Token::exiting && !HasLog(g_guest, "why=not_title"));
}

// (15) a partner mission or terminating blocks PostExit.
void TestPartnerMission() {
    for (int variant = 0; variant < 2; ++variant) {
        Setup(wire::Mode::scene, wire::Mode::scene);
        BindHost(30010);
        CHECK(Run(20000, GuestCountdown));
        if (variant == 0) g_guest.core->OnPartnerMission(30010, false, g_now);
        else g_guest.core->OnPartnerMission(0, true, g_now);
        Drain(g_guest);
        CHECK(!Run(15000, GuestExited));
        CHECK(TokenOf(g_guest) == Token::countdown);
        g_guest.core->OnPartnerMission(0, false, g_now);
        Drain(g_guest);
        CHECK(Run(100, GuestExited));
    }
}

// (16) OnBuildOk(false): no Send action at all, STATE included.
void TestBuildGate() {
    Setup(wire::Mode::scene, wire::Mode::scene, false);
    BindHost(30010);
    Run(30000);
    CHECK(g_host.sends == 0 && g_guest.sends == 0);
    CHECK(HasLog(g_host, "save_handoff=peer_build; ok=0; build=0.12.6+c07d0a2"));
    CHECK(!HasLog(g_host, "save_handoff=offer;") && g_host.snapshots == 0);
    for (Side* s : {&g_host, &g_guest}) {
        s->core->OnBuildOk(true, "0.13.0+x");
        Drain(*s);
    }
    CHECK(Run(20000, GuestCountdown));
    CHECK(StagedEqualsHost());
    CHECK(HasLog(g_host, "save_handoff=peer_build; ok=1; build=0.13.0+x"));
    // Repeated calls do not log again.
    for (int i = 0; i < 10; ++i) g_host.core->OnBuildOk(true, "0.13.0+x");
    Drain(g_host);
    CHECK(CountLog(g_host, "save_handoff=peer_build;") == 2);
}

// (17) pacing: at most 4 CHUNKs per tick and 16 queued; a refused CHUNK keeps the cursor.
void TestPacing() {
    Setup(wire::Mode::scene, wire::Mode::stage);
    g_host.files = MakeFiles(1, 9000);
    g_host.pump_rate = 2;
    BindHost(30010);
    CHECK(Run(60000, GuestStaged));
    CHECK(g_host.max_chunks_tick == core::kChunksPerTick);
    CHECK(g_host.max_out <= core::kMaxQueued);
    CHECK(StagedEqualsHost() && g_guest.write_errors == 0);
    // 'full': one CHUNK accepted per tick, the rest refused and resent later.
    Setup(wire::Mode::scene, wire::Mode::stage);
    g_host.files = MakeFiles(3, 9000);
    g_host.budget_per_tick = 1;
    BindHost(30010);
    CHECK(Run(120000, GuestStaged));
    CHECK(g_host.refused_chunks > 0);
    CHECK(g_host.max_chunks_tick == 1);
    CHECK(StagedEqualsHost() && g_guest.write_errors == 0);
    CHECK(g_guest.core->stats().bad_msg == 0 && !HasLog(g_guest, "failed"));
    CHECK(!HasLog(g_host, "failed"));  // a slow but steady stream never trips the host's stall
    // STATE never replaces a CHUNK (ReliableMode::latest only after a STATE).
    CHECK(g_host.core->stats().chunks_sent == g_guest.core->stats().chunks_received);
}

// (18) unexpected files and too_large refuse.
void TestRefusedSets() {
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_host.files.unexpected = true;
    BindHost(30010);
    Run(30000);
    CHECK(!HasLog(g_host, "save_handoff=offer;"));
    CHECK(HasLog(g_guest, "; elig=7; set="));
    CHECK(TokenOf(g_guest) == Token::refused && g_guest.view.why == static_cast<std::uint8_t>(wire::Elig::unexpected_files));
    CHECK(TokenOf(g_host) == Token::refused && g_host.view.why == static_cast<std::uint8_t>(wire::Elig::unexpected_files));
    CHECK(g_guest.toasts.empty() && g_host.toasts.empty());
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_host.files.total_override = wire::kMaxTotal + 1;
    BindHost(30010);
    Run(30000);
    CHECK(!HasLog(g_host, "save_handoff=offer;"));
    CHECK(HasLog(g_guest, "; elig=8; set="));
    CHECK(TokenOf(g_guest) == Token::refused && g_guest.view.why == static_cast<std::uint8_t>(wire::Elig::too_large));
}

// Snapshot changed between the two reads: no OFFER from it, counted against the set.
void TestSnapshotChanged() {
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30010);
    g_host.snap_change = true;
    Run(60000);
    CHECK(!HasLog(g_host, "save_handoff=offer;"));
    CHECK(CountLog(g_host, "save_handoff=failed; why=snapshot_changed") == 3);
    CHECK(g_host.snapshots == 3 && g_host.releases == 3);
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30010);
    g_host.snap_ok = false;
    Run(10000);
    CHECK(!HasLog(g_host, "save_handoff=offer;") && g_host.releases == 0);
}

// Timers: OFFER idle 30 s, chunk stall 30 s, RESULT wait 60 s, snapshot kept 300 s, STATE cadence.
void TestTimers() {
    // The WANT never reaches the host: the OFFER expires (timeout) and the guest stalls.
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30010);
    g_host.drop_in = 1u << static_cast<unsigned>(wire::Kind::want);
    CHECK(Run(10000, [] { return TokenOf(g_guest) == Token::receiving; }));
    const std::uint64_t offered = g_now;
    CHECK(!Run(core::kOfferIdleMs - 200, [] { return TokenOf(g_host) == Token::failed; }));
    CHECK(TokenOf(g_host) == Token::offered);
    CHECK(Run(400, [] { return TokenOf(g_host) == Token::failed; }));
    CHECK(g_now - offered >= core::kOfferIdleMs - 40);
    CHECK(HasLog(g_host, "save_handoff=abort; why=stall; xfer="));
    CHECK(HasLog(g_host, "save_handoff=failed; why=timeout"));
    CHECK(g_host.view.why == static_cast<std::uint8_t>(wire::Fail::timeout));
    Run(200);
    CHECK(TokenOf(g_guest) == Token::failed && g_guest.view.why == static_cast<std::uint8_t>(wire::Fail::stall));
    CHECK(g_guest.partial.empty());
    // RESULT never reaches the host: failed timeout after 60 s; the stage survives the late ABORT.
    Setup(wire::Mode::scene, wire::Mode::stage);
    BindHost(30010);
    g_host.drop_in = 1u << static_cast<unsigned>(wire::Kind::result);
    CHECK(Run(20000, GuestStaged));
    const std::uint64_t staged = g_now;
    CHECK(!Run(core::kResultWaitMs - 400, [] { return TokenOf(g_host) == Token::failed; }));
    CHECK(Run(600, [] { return TokenOf(g_host) == Token::failed; }));
    CHECK(g_now - staged >= core::kResultWaitMs - 400);
    CHECK(HasLog(g_host, "save_handoff=failed; why=timeout"));
    Run(1000);
    CHECK(TokenOf(g_guest) == Token::staged && g_guest.staged.size() == 1);
    CHECK(g_guest.core->stats().stale >= 1);
    // No CHUNK for 30 s: the guest gives up (ABORT stall) and drops the partial.
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30010);
    g_guest.drop_in = (1u << static_cast<unsigned>(wire::Kind::chunk)) | (1u << static_cast<unsigned>(wire::Kind::end));
    CHECK(Run(10000, [] { return TokenOf(g_guest) == Token::receiving; }));
    CHECK(!Run(core::kChunkStallMs - 200, [] { return TokenOf(g_guest) == Token::failed; }));
    CHECK(Run(400, [] { return TokenOf(g_guest) == Token::failed; }));
    CHECK(HasLog(g_guest, "save_handoff=abort; why=stall; xfer=") && HasLog(g_guest, "save_handoff=failed; why=stall"));
    CHECK(g_guest.partial.empty() && FirstToast(g_guest, Toast::g7_failed)->a == static_cast<std::uint32_t>(wire::Fail::stall));
    // CHUNKs lost but END arrives: protocol (ABORT protocol), nothing staged.
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30010);
    g_guest.drop_in = 1u << static_cast<unsigned>(wire::Kind::chunk);
    CHECK(Run(10000, [] { return TokenOf(g_guest) == Token::failed; }));
    CHECK(HasLog(g_guest, "save_handoff=abort; why=protocol; xfer=") && g_guest.view.why == static_cast<std::uint8_t>(wire::Fail::protocol));
    CHECK(g_guest.manifests == 0 && g_guest.staged.empty());
    Run(200);
    CHECK(TokenOf(g_host) == Token::failed && g_host.view.why == static_cast<std::uint8_t>(wire::Fail::protocol));
    // The held snapshot is freed kSnapshotKeepMs after it was read when nothing uses it.
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30010);
    g_guest.corrupt_in = true;
    CHECK(Run(30000, [] { return CountLog(g_host, "save_handoff=offer;") == 3 && TokenOf(g_host) == Token::failed; }));
    Run(2000);
    CHECK(g_host.snapshots == 1 && g_host.releases == 0);
    CHECK(Run(core::kSnapshotKeepMs, [] { return g_host.releases == 1; }));
    // STATE: on change at most every 500 ms, otherwise a 5 s heartbeat.
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_guest.files = g_host.files;
    BindHost(30010);
    Run(10000);
    const unsigned before = CountSent(g_host, wire::Kind::state);
    Run(30000);
    const unsigned steady = CountSent(g_host, wire::Kind::state) - before;
    CHECK(steady >= 5 && steady <= 7);
    const unsigned guest_before = CountSent(g_guest, wire::Kind::state);
    for (int i = 0; i < 10; ++i) {  // the guest's mission flips every 100 ms
        g_guest.rep.mission = (i & 1) ? 40010u : 40050u;
        Run(100);
    }
    const unsigned burst = CountSent(g_guest, wire::Kind::state) - guest_before;
    CHECK(burst >= 1 && burst <= 3);
    NewSession();
    const unsigned at_session = CountSent(g_host, wire::Kind::state);
    Run(20);
    CHECK(CountSent(g_host, wire::Kind::state) == at_session + 1);  // at once on a new session
}

// Link loss mid-transfer, shutdown, applied.json, no_window, rejoin, bad messages.
void TestEdges() {
    // The peer closes mid-transfer: both sides fail with link_lost.
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_host.files = MakeFiles(1, 12000);
    g_host.pump_rate = 2;
    BindHost(30010);
    CHECK(Run(30000, GuestReceiving));
    for (Side* s : {&g_host, &g_guest}) {
        s->core->OnPeerClose(g_now);
        Drain(*s);
    }
    CHECK(TokenOf(g_guest) == Token::failed && g_guest.view.why == static_cast<std::uint8_t>(wire::Fail::link_lost));
    CHECK(TokenOf(g_host) == Token::failed && g_host.view.why == static_cast<std::uint8_t>(wire::Fail::link_lost));
    CHECK(g_guest.partial.empty() && g_guest.deletes == 1);
    // Shutdown mid-transfer: ABORT shutdown, the snapshot freed, inert afterwards.
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_host.files = MakeFiles(1, 12000);
    g_host.pump_rate = 2;
    BindHost(30010);
    CHECK(Run(30000, GuestReceiving));
    const unsigned releases = g_host.releases;
    g_host.core->OnShutdown(g_now);
    Drain(g_host);
    CHECK(g_host.sent.back().first == wire::Kind::abort &&
          g_host.sent.back().second == static_cast<std::uint8_t>(wire::AbortReason::shutdown));
    CHECK(g_host.releases == releases + 1);
    const unsigned sends = g_host.sends;
    g_host.pump_rate = 1000;
    Run(5000);
    CHECK(g_host.sends == sends);
    CHECK(TokenOf(g_guest) == Token::failed && g_guest.view.why == static_cast<std::uint8_t>(wire::Fail::shutdown));
    CHECK(HasLog(g_guest, "save_handoff=abort_rx; why=shutdown"));
    // applied.json seen at Configure: token applied and G10.
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_guest.files = g_host.files;
    core::Applied applied{};
    std::memcpy(applied.set, Info(g_host.files).set, wire::kShaSize);
    g_guest.core->OnAppliedSeen(applied, g_now);
    Drain(g_guest);
    CHECK(TokenOf(g_guest) == Token::applied && CountToast(g_guest, Toast::g10_applied) == 1);
    CHECK(HasLog(g_guest, "save_handoff=applied_seen; set="));
    BindHost(30010);
    Run(20000);
    CHECK(TokenOf(g_guest) == Token::applied && g_guest.view.lua_code == 5);
    CHECK(HasLog(g_guest, "save_handoff=need; need=0; why=same"));
    // No window: G9 at once, exit_slow later without a second G9; a steam-wait guest writes link-rejoin.txt first.
    Setup(wire::Mode::scene, wire::Mode::scene, true, true);
    g_guest.window = false;
    BindHost(30010);
    CHECK(Run(30000, GuestExited));
    CHECK(HasLog(g_guest, "save_handoff=exit; how=no_window"));
    CHECK(CountToast(g_guest, Toast::g9_quit_manually) == 1);
    CHECK(OpIndex(g_guest, "rejoin") >= 0 && OpIndex(g_guest, "rejoin") < OpIndex(g_guest, "exit"));
    core::Request q{};
    CHECK(core::ParseRequest(g_guest.request_json.data(), g_guest.request_json.size(), q) && q.rejoin == 1);
    Run(25000);
    CHECK(HasLog(g_guest, "save_handoff=exit_slow") && CountToast(g_guest, Toast::g9_quit_manually) == 1);
    // Bad messages are counted; the log is rate limited to one per 10 s.
    Setup(wire::Mode::scene, wire::Mode::scene);
    const std::uint8_t junk[12] = {0x53, 0x02, 0x03, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    for (int i = 0; i < 5; ++i) g_host.core->OnPeer(junk, sizeof(junk), g_now);
    Drain(g_host);
    CHECK(g_host.core->stats().bad_msg == 5 && g_host.core->stats().last_bad == wire::Decode::version);
    CHECK(CountLog(g_host, "save_handoff=bad_msg; why=version; count=1") == 1 && CountLog(g_host, "save_handoff=bad_msg;") == 1);
    g_now += core::kBadMsgLogMs;
    g_host.core->OnPeer(junk, 3, g_now);
    Drain(g_host);
    CHECK(CountLog(g_host, "save_handoff=bad_msg; why=short; count=6") == 1);
    // A host STATE arriving at the host, or a CHUNK at the host, is refused; the
    // core's one line names the role/direction reason (never the stale decode one).
    wire::Message st{};
    st.kind = wire::Kind::state;
    st.state.role = wire::Role::host;
    std::uint8_t buf[wire::kMaxMessage];
    std::size_t n = wire::Encode(st, buf, sizeof(buf));
    g_now += core::kBadMsgLogMs;
    g_host.core->OnPeer(buf, n, g_now);
    Drain(g_host);
    CHECK(CountLog(g_host, "save_handoff=bad_msg; why=role; count=7") == 1);
    CHECK(CountLog(g_host, "why=ok") == 0 && CountLog(g_host, "save_handoff=bad_msg;") == 3);
    CHECK(g_host.core->stats().bad_msg == 7);
    // A stale CHUNK (another xfer) at an idle guest is ignored and not logged.
    wire::Message ch{};
    ch.kind = wire::Kind::chunk;
    ch.xfer = 0x12345678u;
    ch.chunk.slot = 0;
    ch.chunk.len = 1;
    ch.chunk.payload = buf + 900;
    n = wire::Encode(ch, buf, sizeof(buf));
    const std::size_t logs = g_guest.logs.size();
    g_guest.core->OnPeer(buf, n, g_now);
    Drain(g_guest);
    CHECK(g_guest.core->stats().stale == 1 && g_guest.logs.size() == logs);
}

// Review SS-1: the WM_CLOSE did not close the game (no_window, a slow close);
// a veto, or the guest's own Continue, still drops the request before the
// tooling can apply it. Staleness alone never cancels an exit.
void ExitThenCancel(bool window, bool decline) {
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_guest.window = window;
    BindHost(30010);
    CHECK(Run(30000, GuestExited));
    Run(200);
    CHECK(TokenOf(g_guest) == Token::exiting && g_guest.view.lua_code == 4 && g_guest.request_present);
    CHECK(TokenOf(g_host) == Token::partner_restarting);
    const std::uint32_t xfer = g_guest.staged.begin()->first;
    const unsigned deletes = g_guest.deletes;
    if (decline) g_guest.core->OnDecline(core::DeclineSource::second_continue, g_now);
    else g_guest.core->OnContinue(30050, g_now);  // its own save, without an op 3 (lock off, no link)
    Drain(g_guest);
    const wire::AbortReason reason = decline ? wire::AbortReason::user : wire::AbortReason::not_title;
    CHECK(g_guest.sent.back().first == wire::Kind::abort && g_guest.sent.back().second == static_cast<std::uint8_t>(reason));
    CHECK(g_guest.deletes == deletes + 1 && OpIndex(g_guest, "delete:" + Hex8(xfer)) >= 0);
    CHECK(!g_guest.request_present && g_guest.staged.empty());
    if (decline) {
        CHECK(HasLog(g_guest, ("save_handoff=abort; why=user; xfer=" + Hex8(xfer)).c_str()));
        CHECK(TokenOf(g_guest) == Token::kept && g_guest.view.lua_code == 5 && CountToast(g_guest, Toast::g8_kept) == 1);
    } else {
        CHECK(HasLog(g_guest, ("save_handoff=abort; why=not_title; xfer=" + Hex8(xfer)).c_str()));
        CHECK(TokenOf(g_guest) == Token::failed && g_guest.view.why == static_cast<std::uint8_t>(wire::Fail::not_title));
        CHECK(FirstToast(g_guest, Toast::g7_failed)->a == static_cast<std::uint32_t>(wire::Fail::not_title));
    }
    g_guest.rep = InGame(30050);  // its own Continue loads: the title is left
    Run(1000);
    if (decline) CHECK(TokenOf(g_host) == Token::kept && CountToast(g_host, Toast::h5_partner_kept) == 1);
    else CHECK(TokenOf(g_host) == Token::failed && g_host.view.why == static_cast<std::uint8_t>(wire::Fail::not_title));
    // Nothing more: no second exit, no exit_slow, no G9 beyond the no_window one.
    Run(30000);
    CHECK(g_guest.exits == 1 && !HasLog(g_guest, "save_handoff=exit_slow"));
    CHECK(CountToast(g_guest, Toast::g9_quit_manually) == (window ? 0u : 1u));
}
void TestExitVeto() {
    for (const bool window : {true, false})
        for (const bool decline : {true, false}) ExitThenCancel(window, decline);
    // F9 Keep my own save while exiting: the same veto.
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_guest.window = false;
    BindHost(30010);
    CHECK(Run(30000, GuestExited));
    g_guest.core->OnDecline(core::DeclineSource::f9, g_now);
    Drain(g_guest);
    CHECK(!g_guest.request_present && TokenOf(g_guest) == Token::kept && HasLog(g_guest, "save_handoff=declined; source=f9"));
    // Reports stop for more than 30 s while exiting (a slow WM_CLOSE teardown):
    // the request stays, the token stays exiting.
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30010);
    CHECK(Run(30000, GuestExited));
    const unsigned deletes = g_guest.deletes;
    g_guest.report_on = false;
    Run(35000);
    CHECK(g_guest.deletes == deletes && g_guest.request_present && !g_guest.staged.empty());
    CHECK(TokenOf(g_guest) == Token::exiting && !HasLog(g_guest, "why=not_title"));
    // An op 2 while still counting down is no exit cancel (the 2 s title rule owns it).
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30010);
    CHECK(Run(20000, GuestCountdown));
    g_guest.core->OnContinue(30050, g_now);
    Drain(g_guest);
    CHECK(TokenOf(g_guest) == Token::countdown && g_guest.request_present);
}

// Review N2: the host in sending with the link dead and no CLOSE (UDP guest
// crash): failed stall after kChunkStallMs, the snapshot freed by the idle path.
void HostStall(bool refuse_all) {
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_host.files = MakeFiles(1, 12000);
    g_host.pump_rate = 2;
    BindHost(30010);
    CHECK(Run(30000, GuestReceiving));
    CHECK(TokenOf(g_host) == Token::sending);
    g_host.drop_in = ~0u;  // the guest is gone: nothing reaches the host any more
    if (refuse_all) g_host.link_up = false;  // every CHUNK (and STATE) refused: not_connected
    else g_host.pump_rate = 0;               // nothing acknowledged: the queue stays full, no CHUNK queued
    CHECK(!Run(core::kChunkStallMs - 200, [] { return TokenOf(g_host) == Token::failed; }));
    CHECK(TokenOf(g_host) == Token::sending);
    CHECK(Run(400, [] { return TokenOf(g_host) == Token::failed; }));
    CHECK(g_host.view.why == static_cast<std::uint8_t>(wire::Fail::stall) && g_host.view.pct == 0);
    CHECK(HasLog(g_host, "save_handoff=abort; why=stall; xfer=") && HasLog(g_host, "save_handoff=failed; why=stall"));
    CHECK(CountToast(g_host, Toast::h6_failed) == 1);
    static unsigned released;
    released = g_host.releases;
    CHECK(Run(core::kSnapshotKeepMs + 1000, [] { return g_host.releases > released; }));
    CHECK(g_host.releases == released + 1);  // the 8 MiB snapshot is freed by the idle path
}
void TestHostStall() {
    HostStall(true);
    HostStall(false);
}

// ---- Pure decisions.
void TestDecisions() {
    CHECK(core::Loc(30010) == 10 && core::Loc(40010) == 10 && core::Loc(30020) == 20 && core::Loc(40020) == 20);
    CHECK(core::Loc(30050) == 50 && core::Loc(40050) == 50 && core::Loc(10020) == 0 && core::Loc(0) == 0 && core::Loc(1) == 0);
    CHECK(core::Supported(40050) && !core::Supported(10036));
    CHECK(core::SettleMission(30010) && core::SettleMission(30020) && core::SettleMission(30050));
    CHECK(!core::SettleMission(40010) && !core::SettleMission(10020));
    // HostEligible: the first failing reason in the P2-7 order.
    core::HostFacts ok{};
    ok.mode = wire::Mode::scene;
    ok.report_fresh = true;
    ok.story = wire::Story::open;
    ok.set_known = true;
    ok.total = 608012;
    ok.bound_mission = 30010;
    CHECK(core::HostEligible(ok) == wire::Elig::eligible);
    for (std::uint32_t m : {30010u, 30020u, 30050u, 40010u, 40020u, 40050u}) {
        core::HostFacts f = ok;
        f.bound_mission = m;
        CHECK(core::HostEligible(f) == wire::Elig::eligible);
    }
    core::HostFacts all = ok;  // every reason at once: off wins, then the next ...
    all.mode = wire::Mode::off;
    all.report_fresh = false;
    all.story = wire::Story::opening;
    all.unexpected = true;
    all.total = wire::kMaxTotal + 1;
    all.saving = true;
    all.set_known = false;
    all.bound_mission = 0;
    CHECK(core::HostEligible(all) == wire::Elig::off);
    all.mode = wire::Mode::stage;
    CHECK(core::HostEligible(all) == wire::Elig::story_unknown);
    all.report_fresh = true;
    CHECK(core::HostEligible(all) == wire::Elig::story_opening);
    all.story = wire::Story::unknown;
    CHECK(core::HostEligible(all) == wire::Elig::story_unknown);
    all.story = wire::Story::open;
    CHECK(core::HostEligible(all) == wire::Elig::unexpected_files);
    all.unexpected = false;
    CHECK(core::HostEligible(all) == wire::Elig::too_large);
    all.total = wire::kMaxTotal;
    CHECK(core::HostEligible(all) == wire::Elig::saving);
    all.saving = false;
    CHECK(core::HostEligible(all) == wire::Elig::continue_unknown);
    all.set_known = true;
    CHECK(core::HostEligible(all) == wire::Elig::continue_unknown);
    all.bound_mission = 10020;
    CHECK(core::HostEligible(all) == wire::Elig::continue_unsupported);
    all.bound_mission = 30050;
    CHECK(core::HostEligible(all) == wire::Elig::eligible);
    // GuestNeed.
    core::SideFacts own{}, host{};
    own.set_known = host.set_known = true;
    own.set[0] = 1;
    host.set[0] = 2;
    own.story = wire::Story::open;
    host.story = wire::Story::open;
    own.mission = 40050;
    host.mission = 40010;
    host.bound_mission = 30010;
    core::NeedWhy why = core::NeedWhy::same;
    CHECK(core::GuestNeed(wire::Mode::off, own, host, why) == 0 && why == core::NeedWhy::off);
    CHECK(core::GuestNeed(wire::Mode::scene, own, host, why) == 1 && why == core::NeedWhy::location);
    CHECK(core::GuestNeed(wire::Mode::stage, own, host, why) == 1 && why == core::NeedWhy::location);
    CHECK(core::GuestNeed(wire::Mode::always, own, host, why) == 1 && why == core::NeedWhy::always);
    own.mission = 40010;
    CHECK(core::GuestNeed(wire::Mode::scene, own, host, why) == 0 && why == core::NeedWhy::same_scene);
    host.bound_mission = 0;  // unbound: the host's own mission
    host.mission = 40050;
    CHECK(core::GuestNeed(wire::Mode::scene, own, host, why) == 1 && why == core::NeedWhy::location);
    host.bound_mission = 30010;
    own.story = wire::Story::opening;
    CHECK(core::GuestNeed(wire::Mode::scene, own, host, why) == 1 && why == core::NeedWhy::story);
    own.story = wire::Story::unknown;
    CHECK(core::GuestNeed(wire::Mode::scene, own, host, why) == 1 && why == core::NeedWhy::story);
    own.story = wire::Story::open;
    own.bound_mission = 10020;
    CHECK(core::GuestNeed(wire::Mode::scene, own, host, why) == 1 && why == core::NeedWhy::unsupported);
    own.bound_mission = 30010;
    CHECK(core::GuestNeed(wire::Mode::scene, own, host, why) == 0 && why == core::NeedWhy::same_scene);
    host.set[0] = 1;  // equal sets
    CHECK(core::GuestNeed(wire::Mode::always, own, host, why) == 0 && why == core::NeedWhy::same);
    host.set_known = false;  // an unknown set is never "the same"
    CHECK(core::GuestNeed(wire::Mode::always, own, host, why) == 1);
    CHECK(std::strcmp(core::NeedWhyName(core::NeedWhy::same_scene), "same_scene") == 0);
    // ExitGate.
    core::ExitFacts e{};
    e.mode = wire::Mode::scene;
    e.staged = e.request_written = e.result_sent = e.report_fresh = e.title = true;
    e.title_held_ms = core::kCountdownMs;
    CHECK(core::ExitGate(e));
    e.mode = wire::Mode::always;
    CHECK(core::ExitGate(e));
    for (int k = 0; k < 13; ++k) {
        core::ExitFacts f = e;
        switch (k) {
        case 0: f.mode = wire::Mode::stage; break;
        case 1: f.mode = wire::Mode::off; break;
        case 2: f.staged = false; break;
        case 3: f.request_written = false; break;
        case 4: f.result_sent = false; break;
        case 5: f.report_fresh = false; break;
        case 6: f.title = false; break;
        case 7: f.saving = true; break;
        case 8: f.ingame = true; break;
        case 9: f.partner_mission = true; break;
        case 10: f.terminating = true; break;
        case 11: f.title_held_ms = core::kCountdownMs - 1; break;
        default: f.declined = true; break;
        }
        CHECK(!core::ExitGate(f));
    }
    CHECK(std::strcmp(core::SourceName(core::Source::continue_), "continue") == 0);
    CHECK(std::strcmp(core::SourceName(core::Source::settle), "settle") == 0);
    CHECK(std::strcmp(core::ActionName(core::ActionKind::write_request), "write_request") == 0);
}

// ---- (19) provenance LRU and the JSON documents.
void MakeSet(std::uint8_t (&set)[wire::kShaSize], std::uint8_t seed) {
    for (unsigned i = 0; i < wire::kShaSize; ++i) set[i] = static_cast<std::uint8_t>(seed * 17 + i);
}
void TestProvenance() {
    core::Provenance p;
    std::uint8_t sets[9][wire::kShaSize];
    for (std::uint8_t i = 0; i < 9; ++i) MakeSet(sets[i], i);
    for (std::uint8_t i = 0; i < 8; ++i) p.Bind(sets[i], 30010 + (i % 3) * 10, core::Source::settle, "2026-10-15T00:00:00Z");
    CHECK(p.Count() == 8 && p.Find(sets[0]) && p.At(0).mission == 30020);  // set 7 is the most recent
    p.Bind(sets[0], 30050, core::Source::continue_, "2026-10-15T00:00:01Z");  // re-bind: moves to the front
    CHECK(p.Count() == 8 && p.At(0).mission == 30050 && p.At(0).source == core::Source::continue_);
    p.Bind(sets[8], 40050, core::Source::continue_, "2026-10-15T00:00:02Z");  // drops the LRU (set 1)
    CHECK(p.Count() == 8 && !p.Find(sets[1]) && p.Find(sets[0]) && p.Find(sets[8]) && p.Find(sets[2]));
    CHECK(p.Find(sets[8])->mission == 40050);
    char text[core::kProvenanceJsonMax];
    const std::size_t n = p.Format(text, sizeof(text));
    CHECK(n > 0 && n < sizeof(text));
    const char prefix[] = "{\"format\":\"mgs-coop-save-provenance\",\"v\":1,\"entries\":[{\"set\":\"";
    CHECK(std::strncmp(text, prefix, sizeof(prefix) - 1) == 0);
    core::Provenance q;
    CHECK(q.Parse(text, n) && q.Count() == 8);
    for (unsigned i = 0; i < 8; ++i) {
        CHECK(std::memcmp(q.At(i).set, p.At(i).set, wire::kShaSize) == 0 && q.At(i).mission == p.At(i).mission);
        CHECK(q.At(i).source == p.At(i).source && std::strcmp(q.At(i).utc, p.At(i).utc) == 0);
    }
    char small[64];
    CHECK(p.Format(small, sizeof(small)) == 0 && small[0] == 0);
    // Refusals clear the table.
    std::string doc(text, n);
    auto refused = [](const std::string& s) {
        core::Provenance r;
        std::uint8_t set[wire::kShaSize];
        MakeSet(set, 1);
        r.Bind(set, 30010, core::Source::settle, "x");
        return !r.Parse(s.data(), s.size()) && r.Count() == 0;
    };
    auto replace = [&](const char* from, const char* to) {
        std::string s = doc;
        const std::size_t at = s.find(from);
        CHECK(at != std::string::npos);
        s.replace(at, std::strlen(from), to);
        return s;
    };
    CHECK(refused(replace("\"v\":1", "\"v\":2")));
    CHECK(refused(replace("mgs-coop-save-provenance", "mgs-coop-save-provenancE")));
    CHECK(refused(replace("\"source\":\"continue\"", "\"source\":\"guess\"")));
    CHECK(refused(replace("\"mission\":40050", "\"mission\":0")));
    CHECK(refused(replace("\"mission\":40050", "\"mission\":100000")));
    CHECK(refused(replace("\"mission\":40050", "\"mission\":-1")));
    CHECK(refused(doc.substr(0, doc.size() - 1)));
    CHECK(refused(doc + "x"));
    CHECK(refused(""));
    CHECK(refused("{\"format\":\"mgs-coop-save-provenance\",\"v\":1}"));  // no entries
    {
        std::string s = doc;
        s.replace(s.find("\"set\":\"") + 7, 1, "Z");
        CHECK(refused(s));
    }
    {  // 9 entries
        std::string s = doc;
        const std::size_t first = s.find("{\"set\"");
        const std::size_t end = s.find('}', first);
        s.insert(s.size() - 2, "," + s.substr(first, end - first + 1));
        std::string nine = s;
        nine.replace(nine.rfind("\"set\":\"") + 7, 2, "FF");
        CHECK(refused(nine));
    }
    // A PowerShell-style document: BOM, CRLF, indentation, other key order, \u escapes, an unknown key.
    {
        const char ps[] =
            "\xEF\xBB\xBF{\r\n    \"entries\":  [\r\n                    {\r\n                        \"utc\":  \"2026-10-15T10:00:00\\u002e5Z\",\r\n"
            "                        \"source\":  \"settle\",\r\n                        \"extra\":  [1, {\"a\": null}],\r\n"
            "                        \"mission\":  30020,\r\n"
            "                        \"set\":  \"00112233445566778899aabbccddeeff00112233445566778899AABBCCDDEEFF\"\r\n"
            "                    }\r\n                ],\r\n    \"v\":  1,\r\n    \"format\":  \"mgs-coop-save-provenance\"\r\n}\r\n";
        core::Provenance r;
        CHECK(r.Parse(ps, sizeof(ps) - 1) && r.Count() == 1 && r.At(0).mission == 30020);
        CHECK(std::strcmp(r.At(0).utc, "2026-10-15T10:00:00.5Z") == 0 && r.At(0).source == core::Source::settle);
        CHECK(r.At(0).set[1] == 0x11 && r.At(0).set[31] == 0xFF);
    }
    // Core::LoadProvenance: malformed text queues a rewrite.
    core::Config c{};
    c.role = wire::Role::host;
    c.mode = wire::Mode::scene;
    g_host_core.Reset(c);
    core::Action a;
    while (g_host_core.PopAction(a)) {
    }
    CHECK(!g_host_core.LoadProvenance("{oops", 5));
    bool rewrite = false;
    while (g_host_core.PopAction(a)) rewrite = rewrite || a.kind == core::ActionKind::write_provenance;
    CHECK(rewrite);
    CHECK(g_host_core.LoadProvenance(text, n) && g_host_core.provenance().Count() == 8);
}

void TestDocuments() {
    // handoff.json
    core::Manifest m{};
    m.xfer = 0x0A1B2C3Du;
    std::snprintf(m.created_utc, sizeof(m.created_utc), "%s", "2026-10-15T12:00:00Z");
    std::snprintf(m.host_build, sizeof(m.host_build), "%s", "0.13.0+c07d0a2");
    m.host_mission = 30010;
    m.n = 3;
    const std::uint8_t slots[3] = {0, 2, 4};
    for (unsigned i = 0; i < 3; ++i) {
        m.files[i].slot = slots[i];
        m.files[i].size = 199916 + i;
        MakeSet(m.files[i].sha256, static_cast<std::uint8_t>(i + 3));
    }
    CHECK(wire::SetSha256(m.files, m.n, m.set));
    char text[core::kManifestJsonMax];
    const std::size_t n = core::FormatManifest(m, text, sizeof(text));
    CHECK(n > 0);
    const std::string doc(text, n);
    CHECK(doc.find("{\"format\":\"mgs-coop-save-handoff\",\"v\":1,\"xfer\":\"0A1B2C3D\",\"created_utc\":\"2026-10-15T12:00:00Z\","
                   "\"host_build\":\"0.13.0+c07d0a2\",\"host_mission\":30010,\"set_sha256\":\"") == 0);
    CHECK(doc.find("{\"entry\":\"287700/local/TPP_GAME_DATA0\",\"bytes\":199916,\"sha256\":\"") != std::string::npos);
    CHECK(doc.find("{\"entry\":\"remote/TPP_GAME_DATA\",\"bytes\":199918,") != std::string::npos);
    core::Manifest back{};
    CHECK(core::ParseManifest(text, n, back));
    CHECK(back.xfer == m.xfer && back.n == 3 && back.host_mission == 30010 && std::strcmp(back.host_build, m.host_build) == 0);
    CHECK(back.files[2].slot == 4 && back.files[2].size == 199918 && std::memcmp(back.set, m.set, wire::kShaSize) == 0);
    auto manifest_refused = [&](const char* from, const char* to) {
        std::string s = doc;
        const std::size_t at = s.find(from);
        CHECK(at != std::string::npos);
        s.replace(at, std::strlen(from), to);
        core::Manifest r{};
        r.xfer = 7;
        return !core::ParseManifest(s.data(), s.size(), r) && r.xfer == 7;
    };
    CHECK(manifest_refused("\"v\":1", "\"v\":3"));
    CHECK(manifest_refused("\"xfer\":\"0A1B2C3D\"", "\"xfer\":\"00000000\""));
    CHECK(manifest_refused("\"xfer\":\"0A1B2C3D\"", "\"xfer\":\"0A1B2C3\""));
    CHECK(manifest_refused("remote/TPP_GAME_DATA", "remote/TPP_CONFIG_DATA"));
    CHECK(manifest_refused("287700/local/PERSONAL_DATA0", "287700/local/TPP_GAME_DATA0"));  // duplicate
    CHECK(manifest_refused("287700/local/TPP_GAME_DATA0", "287700/local/PERSONAL_DATA1"));  // no slot 0/1
    CHECK(manifest_refused("\"bytes\":199916", "\"bytes\":199915"));                      // set mismatch
    CHECK(manifest_refused("\"bytes\":199916", "\"bytes\":0"));
    CHECK(manifest_refused("\"bytes\":199916", "\"bytes\":4194305"));
    CHECK(manifest_refused("\"host_mission\":30010", "\"host_mission\":100000"));
    CHECK(manifest_refused("\"files\":[", "\"filez\":["));
    // request.json
    core::Request q{};
    q.pid = 12345;
    std::snprintf(q.process_start_utc, sizeof(q.process_start_utc), "%s", "2026-10-15T11:59:00Z");
    q.xfer = 0x0A1B2C3Du;
    q.set[0] = 0xAB;
    q.mode = wire::Mode::scene;
    q.rejoin = 1;
    std::snprintf(q.created_utc, sizeof(q.created_utc), "%s", "2026-10-15T12:00:01Z");
    char rtext[core::kRequestJsonMax];
    const std::size_t rn = core::FormatRequest(q, rtext, sizeof(rtext));
    CHECK(rn > 0);
    CHECK(std::string(rtext, rn) ==
          "{\"format\":\"mgs-coop-save-handoff-request\",\"v\":1,\"pid\":12345,\"process_start_utc\":\"2026-10-15T11:59:00Z\","
          "\"xfer\":\"0A1B2C3D\",\"set_sha256\":\"AB00000000000000000000000000000000000000000000000000000000000000\","
          "\"mode\":\"scene\",\"rejoin\":1,\"created_utc\":\"2026-10-15T12:00:01Z\"}");
    core::Request qb{};
    CHECK(core::ParseRequest(rtext, rn, qb) && qb.pid == 12345 && qb.xfer == q.xfer && qb.mode == wire::Mode::scene && qb.rejoin == 1);
    CHECK(qb.set[0] == 0xAB && std::strcmp(qb.created_utc, q.created_utc) == 0);
    for (const char* bad : {"\"v\":2", "\"mode\":\"stage\"", "\"rejoin\":2", "\"pid\":-1"}) {
        std::string s(rtext, rn);
        const char* key = std::strchr(bad, ':');
        const std::string prefix(bad, static_cast<std::size_t>(key - bad + 1));
        const std::size_t at = s.find(prefix);
        const std::size_t end = s.find_first_of(",}", at);
        s.replace(at, end - at, bad);
        core::Request r{};
        CHECK(!core::ParseRequest(s.data(), s.size(), r));
    }
    core::Request stage = q;
    stage.mode = wire::Mode::stage;
    CHECK(core::FormatRequest(stage, rtext, sizeof(rtext)) == 0);
    CHECK(core::FormatRequest(q, rtext, 40) == 0 && rtext[0] == 0);
    // applied.json (the tooling writes it with PowerShell).
    core::Applied ap{};
    ap.set[31] = 0x01;
    std::snprintf(ap.applied_utc, sizeof(ap.applied_utc), "%s", "2026-10-15T12:01:00Z");
    std::snprintf(ap.aside, sizeof(ap.aside), "%s", "20261015-120100-12-before-handoff");
    char atext[core::kAppliedJsonMax];
    const std::size_t an = core::FormatApplied(ap, atext, sizeof(atext));
    CHECK(an > 0 && std::strstr(atext, "\"aside\":\"20261015-120100-12-before-handoff\"}"));
    core::Applied ab{};
    CHECK(core::ParseApplied(atext, an, ab) && ab.set[31] == 1 && std::strcmp(ab.aside, ap.aside) == 0);
    const char ps[] = "\xEF\xBB\xBF{\r\n  \"aside\": \"20261015-120100-12-before-handoff\",\r\n  \"format\": \"mgs-coop-save-handoff-applied\",\r\n"
                      "  \"applied_utc\": \"2026-10-15T12:01:00.1234567Z\",\r\n  \"v\": 1,\r\n"
                      "  \"set_sha256\": \"0000000000000000000000000000000000000000000000000000000000000001\"\r\n}";
    CHECK(core::ParseApplied(ps, sizeof(ps) - 1, ab) && std::strcmp(ab.applied_utc, "2026-10-15T12:01:00.1234567Z") == 0);
    for (const char* leaf : {"..", "a\\\\b", "a/b", "c:x", ""}) {
        std::string s(atext, an);
        const std::size_t at = s.find("20261015-120100-12-before-handoff");
        s.replace(at, std::strlen("20261015-120100-12-before-handoff"), leaf);
        CHECK(!core::ParseApplied(s.data(), s.size(), ab));
    }
    std::string v2(atext, an);
    v2.replace(v2.find("\"v\":1"), 5, "\"v\":2");
    CHECK(!core::ParseApplied(v2.data(), v2.size(), ab));
    core::Applied badleaf = ap;
    std::snprintf(badleaf.aside, sizeof(badleaf.aside), "%s", "..");
    CHECK(core::FormatApplied(badleaf, atext, sizeof(atext)) == 0);
}

// ---- save_handoff_state.
void TestState() {
    hs::ResetForTest();
    hs::View v0 = hs::Get();
    CHECK(!v0.available && v0.token == 0 && !v0.travel_unlocked);
    hs::View v{};
    v.available = true;
    v.role = 2;
    v.token = static_cast<std::uint8_t>(Token::receiving);
    v.pct = 42;
    v.why = 3;
    v.lua_code = 2;
    v.host_mission = 30010;
    v.bytes = 608012;
    v.can_keep = true;
    v.travel_unlocked = true;  // ignored by Publish
    hs::Publish(v);
    hs::View g = hs::Get();
    CHECK(g.available && g.role == 2 && g.token == static_cast<std::uint8_t>(Token::receiving) && g.pct == 42 && g.why == 3);
    CHECK(g.lua_code == 2 && g.host_mission == 30010 && g.bytes == 608012 && g.can_keep && !g.travel_unlocked);
    hs::SetTravelUnlocked(true);
    CHECK(hs::TravelUnlocked() && hs::Get().travel_unlocked);
    hs::SetTravelUnlocked(false);
    CHECK(!hs::TravelUnlocked());
    CHECK(!hs::TakeKeepOwnSave());
    hs::RequestKeepOwnSave();
    hs::RequestKeepOwnSave();
    CHECK(hs::TakeKeepOwnSave());
    CHECK(!hs::TakeKeepOwnSave());
    // TokenName: unique ASCII names of at most 18 characters, in the P2-13 order.
    const char* expected[] = {"off",       "idle",   "same",      "wait_host", "offered", "sending", "partner_staged", "partner_restarting",
                              "receiving", "staged", "countdown", "exiting",   "kept",    "refused", "failed",         "applied"};
    std::set<std::string> names;
    for (std::uint8_t t = 0; t < hs::kTokenCount; ++t) {
        const char* name = hs::TokenName(t);
        CHECK(std::strcmp(name, expected[t]) == 0);
        CHECK(std::strlen(name) >= 1 && std::strlen(name) <= 18);
        for (const char* p = name; *p; ++p) CHECK((*p >= 'a' && *p <= 'z') || *p == '_');
        names.insert(name);
    }
    CHECK(names.size() == hs::kTokenCount);
    CHECK(std::strcmp(hs::TokenName(hs::kTokenCount), "off") == 0 && std::strcmp(hs::TokenName(255), "off") == 0);
    // Readers never see a torn view while a writer alternates two views.
    hs::View a{}, b{};
    a.available = true;
    a.token = 5;
    a.pct = 10;
    a.host_mission = 30010;
    a.bytes = 1111;
    b.available = true;
    b.token = 8;
    b.pct = 90;
    b.why = 4;
    b.lua_code = 2;
    b.host_mission = 40050;
    b.bytes = 2222;
    b.can_keep = true;
    std::atomic<bool> stop{false};
    std::atomic<unsigned> torn{0}, reads{0};
    std::thread reader([&] {
        while (!stop.load()) {
            const hs::View r = hs::Get();
            const bool is_a = r.token == 5 && r.pct == 10 && r.why == 0 && r.lua_code == 0 && r.host_mission == 30010 && r.bytes == 1111 && !r.can_keep;
            const bool is_b = r.token == 8 && r.pct == 90 && r.why == 4 && r.lua_code == 2 && r.host_mission == 40050 && r.bytes == 2222 && r.can_keep;
            if (!is_a && !is_b) ++torn;
            ++reads;
        }
    });
    for (int i = 0; i < 200000; ++i) hs::Publish(i & 1 ? b : a);
    while (reads.load() < 1000) std::this_thread::yield();
    stop = true;
    reader.join();
    CHECK(torn.load() == 0);
    hs::ResetForTest();
    CHECK(!hs::Get().available && !hs::TravelUnlocked());
}

// The view a Core publishes while off.
void TestOffCore() {
    core::Config c{};
    c.role = wire::Role::guest;
    c.mode = wire::Mode::off;
    g_guest_core.Reset(c);
    core::Action a;
    bool published = false;
    while (g_guest_core.PopAction(a))
        if (a.kind == core::ActionKind::publish) {
            published = true;
            CHECK(!a.view.available && a.view.token == static_cast<std::uint8_t>(Token::off) && a.view.lua_code == 0);
        }
    CHECK(published);
}

}  // namespace

// ---- Online 2026-10-22 (the first two-PC Steam session; docs/save-handoff.md).
std::uint8_t WhyOf(hs::WaitWhy why) { return static_cast<std::uint8_t>(why); }

// P1: a guest co-op save with only remote\TPP_GAME_DATA and PERSONAL_DATA (an
// old Steam save the game has not written on this PC yet). The 0.13.5 glue
// gave no valid set, so need was never decided (STATE need=0 set=00000000) and
// the travel lock fell back to the story-start escape. The guest glue now
// hashes slot 4 as an identity (save_handoff_io_tests RemoteOnly): need=1
// why=story and the OFFER at the title.
void TestRemoteOnlyGuest() {
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_guest.files = MakeFiles(2, 2000, 0x30);  // slots 4 and 5 only
    g_guest.rep = Title(40010, wire::Story::opening);
    BindHost(30010);
    Run(20000);
    CHECK(!HasLog(g_guest, "save_handoff=need;") && !HasLog(g_host, "save_handoff=offer;"));
    CHECK(HasLog(g_host, "save_handoff=state_rx; role=guest; phase=2; story=1; mission=40010; bound=0; need=0; elig=0; set=00000000"));
    CHECK(TokenOf(g_guest) == Token::idle && g_guest.view.lua_code == 0);
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_guest.files = MakeFiles(2, 2000, 0x30);
    g_guest.remote_primary = true;
    g_guest.rep = Title(40010, wire::Story::opening);
    BindHost(30010);
    CHECK(Run(20000, GuestCountdown));
    CHECK(HasLog(g_guest, "save_handoff=need; need=1; why=story"));
    CHECK(HasLog(g_host, "save_handoff=state_rx; role=guest; phase=2; story=1; mission=40010; bound=0; need=1; elig=0; set="));
    CHECK(HasLog(g_host, "save_handoff=offer; xfer=") && StagedEqualsHost());
}

// P3: the host is in game in Mother Base with a set not bound yet (a deploy is
// no Continue). The guest at its title waits for the settle bind: G11 "checked
// there (about 10 s)", not G4 "once the host is in Mother Base"; the host gets
// H8, not H3 "go to Mother Base". The OFFER comes kSettleBindMs after the set
// was first seen. Leaving for the helicopter before the bind gives G4/H3.
void TestSettleWait() {
    Setup(wire::Mode::scene, wire::Mode::scene);
    // G11 names the seconds for a host with the 10 s window (kSettle10Min*); below, an older one: no number.
    g_guest_core.OnBuildOk(true, "0.13.6+test");
    Drain(g_guest);
    g_guest.rep = Title(40010);  // Loc 10 vs the host's 30050: need=1 why=location
    g_host.rep = InGame(30050);
    const std::uint64_t start = g_now;
    Run(3000);
    CHECK(TokenOf(g_guest) == Token::wait_host && g_guest.view.why == WhyOf(hs::WaitWhy::settling) && g_guest.view.lua_code == 1);
    CHECK(TokenOf(g_host) == Token::refused && g_host.view.why == static_cast<std::uint8_t>(wire::Elig::continue_unknown));
    CHECK(CountToast(g_guest, Toast::g11_host_settling) == 1 && CountToast(g_guest, Toast::g4_wait_host) == 0);
    CHECK(FirstToast(g_guest, Toast::g11_host_settling)->a == 30050 && FirstToast(g_guest, Toast::g11_host_settling)->b == 10);
    CHECK(CountToast(g_host, Toast::h8_settling) == 1 && CountToast(g_host, Toast::h3_partner_elsewhere) == 0);
    CHECK(FirstToast(g_host, Toast::h8_settling)->a == 10);
    CHECK(Run(core::kSettleBindMs + 5000, [] { return HasLog(g_host, "save_handoff=offer;"); }));
    const std::uint64_t offer_at = g_now - start;
    CHECK(offer_at >= core::kSettleBindMs && offer_at <= core::kSettleBindMs + 3000);
    CHECK(HasLog(g_host, "; mission=30050; source=settle"));
    CHECK(Run(20000, GuestCountdown) && StagedEqualsHost());
    CHECK(CountToast(g_guest, Toast::g11_host_settling) == 1 && CountToast(g_host, Toast::h8_settling) == 1);
    // The host leaves for its helicopter before the bind: the original texts.
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_guest.rep = Title(40010);
    g_host.rep = InGame(30050);
    Run(3000);
    g_host.rep = InGame(40050);
    Run(5000);
    CHECK(!HasLog(g_host, "save_handoff=provenance;"));
    CHECK(TokenOf(g_guest) == Token::wait_host && g_guest.view.why == WhyOf(hs::WaitWhy::host_elsewhere));
    CHECK(CountToast(g_guest, Toast::g11_host_settling) == 1 && CountToast(g_guest, Toast::g4_wait_host) == 1);
    CHECK(FirstToast(g_guest, Toast::g11_host_settling)->b == 0);  // a 0.13.0 host: its window is not this build's 10 s
    CHECK(CountToast(g_host, Toast::h8_settling) == 1 && CountToast(g_host, Toast::h3_partner_elsewhere) == 1);
    CHECK(FirstToast(g_host, Toast::h8_settling)->a == 10);  // the host's own window
    Run(30000);
    CHECK(CountToast(g_guest, Toast::g4_wait_host) == 1 && CountToast(g_host, Toast::h3_partner_elsewhere) == 1);  // once per state
}

// P6: the host's save is ready while the guest plays its own save: the
// handoff needs the guest's title. G12 once per link session and the F9 why;
// at the title the OFFER follows.
void TestGuestInGame() {
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30010);
    g_guest.rep = InGame(30020);
    Run(20000);
    CHECK(HasLog(g_guest, "save_handoff=need; need=1; why=location") && !HasLog(g_host, "save_handoff=offer;"));
    CHECK(TokenOf(g_guest) == Token::wait_host && g_guest.view.why == WhyOf(hs::WaitWhy::title));
    CHECK(CountToast(g_guest, Toast::g12_go_to_title) == 1 && FirstToast(g_guest, Toast::g12_go_to_title)->a == 30010);
    Run(30000);
    CHECK(CountToast(g_guest, Toast::g12_go_to_title) == 1);
    g_guest.rep = Title(40020);
    CHECK(Run(20000, GuestCountdown));
    CHECK(CountToast(g_guest, Toast::g12_go_to_title) == 1 && CountToast(g_guest, Toast::g4_wait_host) == 0);
    // A new link session may tell it once more.
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30010);
    g_guest.rep = InGame(30020);
    Run(5000);
    NewSession();
    Run(5000);
    CHECK(CountToast(g_guest, Toast::g12_go_to_title) == 2);
}

// Review (online 2026-10-22): the guest's start-up mission 1 (the Lua report
// says ingame=1, title=0 there) is no location and no "go back to the title".
// With the host bound and eligible, a guest whose set differs decides no
// need=1 why=location and gets no G12 while booting; at its title at the
// host's place need=0. Before: need=1 why=location at mission 1 (Loc 0) and
// G12, once per link session, used up at the boot. A host settling in Mother
// Base got H8 (and the guest G11) for that boot as well. The story rule waits
// for the title as well (the boot reads the opening), and a host booting with
// the guest gets no H4 (the guest no G5) for its boot.
void TestGuestStartup() {
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30050);
    g_guest.rep = InGame(1);
    Run(20000);
    CHECK(!HasLog(g_guest, "save_handoff=need;") && !HasLog(g_host, "save_handoff=offer;"));
    CHECK(TokenOf(g_guest) == Token::idle && g_guest.view.lua_code == 0 && CountToast(g_guest, Toast::g12_go_to_title) == 0);
    g_guest.rep = Title(40050);
    Run(5000);
    CHECK(HasLog(g_guest, "save_handoff=need; need=0; why=same_scene") && !HasLog(g_guest, "save_handoff=need; need=1"));
    CHECK(TokenOf(g_guest) == Token::same && CountToast(g_guest, Toast::g12_go_to_title) == 0 && !HasLog(g_host, "save_handoff=offer;"));
    // The host settles in Mother Base (in game, not bound yet): no G11 / H8 / H3 for the guest's boot.
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_host.rep = InGame(30050);
    g_guest.rep = InGame(1);
    Run(5000);
    CHECK(!HasLog(g_guest, "save_handoff=need;") && CountToast(g_guest, Toast::g11_host_settling) == 0 &&
          CountToast(g_guest, Toast::g4_wait_host) == 0);
    CHECK(CountToast(g_host, Toast::h8_settling) == 0 && CountToast(g_host, Toast::h3_partner_elsewhere) == 0);
    // The boot reports the story opening before the save loads (hidden runs
    // 247ae49a / 1eff8016: story=1, then 2, in mission 1), so the story rule waits
    // for the title too: no need, no G12 at mission 1; a story-opening guest
    // decides need=1 why=story at its title and gets the OFFER there.
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30050);
    core::Report boot = InGame(1);
    boot.story = wire::Story::opening;
    g_guest.rep = boot;
    Run(10000);
    CHECK(!HasLog(g_guest, "save_handoff=need;") && !HasLog(g_host, "save_handoff=offer;"));
    CHECK(TokenOf(g_guest) == Token::idle && g_guest.view.lua_code == 0 && CountToast(g_guest, Toast::g12_go_to_title) == 0);
    CHECK(CountToast(g_host, Toast::h3_partner_elsewhere) == 0 && CountToast(g_host, Toast::h8_settling) == 0);
    g_guest.rep = Title(40010, wire::Story::opening);
    CHECK(Run(20000, GuestCountdown) && StagedEqualsHost());
    CHECK(HasLog(g_guest, "save_handoff=need; need=1; why=story"));
    CHECK(CountToast(g_guest, Toast::g12_go_to_title) == 0);
    // Both copies booting together (the same boot story): the host's own story is
    // unknown in mission 1 (transient), so neither copy is told "story opening"
    // (before: H4 on the host and G5 on the guest at the boot). At the titles,
    // with the host's save open, still none.
    Setup(wire::Mode::scene, wire::Mode::scene);
    g_host.rep = boot;
    g_guest.rep = boot;
    Run(10000);
    CHECK(HasLog(g_guest, "; elig=3; set=") && !HasLog(g_guest, "; elig=2; set="));
    CHECK(CountToast(g_host, Toast::h4_story_opening) == 0 && CountToast(g_guest, Toast::g5_host_story_opening) == 0);
    CHECK(!HasLog(g_guest, "save_handoff=need;") && g_host.toasts.empty() && g_guest.toasts.empty());
    g_host.rep = Title(40010);
    g_guest.rep = Title(40050);
    Run(5000);
    CHECK(HasLog(g_guest, "save_handoff=need; need=1; why=location"));
    CHECK(CountToast(g_host, Toast::h4_story_opening) == 0 && CountToast(g_guest, Toast::g5_host_story_opening) == 0);
}

// P4 (guest): the WM_CLOSE waits until RESULT exiting left channel 4 (here:
// pumped to the host), at most kExitFlushMs; at once when it was refused unsent.
void TestExitFlush() {
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30010);
    CHECK(Run(20000, GuestCountdown));
    CHECK(Run(11000, GuestExited));
    CHECK(HasLog(g_guest, "save_handoff=exit_flush; ms=") && HasLog(g_guest, "; acked=1"));
    CHECK(g_guest.exit_at - g_guest.request_at < core::kCountdownMs + 100);
    CHECK(OpIndex(g_guest, "exit") > OpIndex(g_guest, "result:exiting"));
    // Nothing leaves the guest (a stuck link): kExitFlushMs.
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30010);
    CHECK(Run(20000, GuestCountdown));
    g_guest.pump_rate = 0;
    CHECK(Run(11000, [] { return TokenOf(g_guest) == Token::exiting; }));
    const std::uint64_t exiting_at = g_now;
    CHECK(g_guest.exits == 0);
    CHECK(!Run(core::kExitFlushMs - 100, GuestExited));
    CHECK(Run(200, GuestExited));
    CHECK(g_guest.exit_at - exiting_at >= core::kExitFlushMs - 40);
    CHECK(HasLog(g_guest, "; acked=0") && HasLog(g_guest, "save_handoff=exit; how=wm_close"));
    // Not connected: RESULT exiting is refused, the WM_CLOSE goes at once.
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30010);
    CHECK(Run(20000, GuestCountdown));
    g_guest.link_up = false;
    CHECK(Run(11000, GuestExited));
    CHECK(g_guest.exit_at - g_guest.request_at < core::kCountdownMs + 100 && HasLog(g_guest, "; acked=0"));
    // A veto while the flush waits: no WM_CLOSE at all.
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30010);
    CHECK(Run(20000, GuestCountdown));
    g_guest.pump_rate = 0;
    CHECK(Run(11000, [] { return TokenOf(g_guest) == Token::exiting; }));
    g_guest.core->OnDecline(core::DeclineSource::f9, g_now);
    Drain(g_guest);
    Run(3000);
    CHECK(g_guest.exits == 0 && TokenOf(g_guest) == Token::kept && !g_guest.request_present);
}

// P4 (host): RESULT exiting lost (the guest's Steam link parked at its
// WM_CLOSE). A scene/always guest with a verified stage that leaves is
// restarting on the host's save: partner_restarting and H2. A stage-mode
// guest never exits, so its leaving stays partner_staged.
void TestHostInfersRestart() {
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30010);
    CHECK(Run(20000, GuestCountdown));
    Run(200);
    CHECK(TokenOf(g_host) == Token::partner_staged);
    g_host.drop_in = 1u << static_cast<unsigned>(wire::Kind::result);
    CHECK(Run(11000, GuestExited));
    Run(500);
    CHECK(TokenOf(g_host) == Token::partner_staged && !HasLog(g_host, "status=exiting"));  // the 0.13.5 state
    g_host.core->OnPeerClose(g_now);
    Drain(g_host);
    CHECK(TokenOf(g_host) == Token::partner_restarting && HasLog(g_host, "save_handoff=partner_restarting; how=link_closed"));
    CHECK(CountToast(g_host, Toast::h2_partner_restarting) == 1 && FirstToast(g_host, Toast::h2_partner_restarting)->a == 30010);
    Run(5000);
    CHECK(TokenOf(g_host) == Token::partner_restarting);  // sticky until the next session
    NewSession();
    Run(1000);
    CHECK(TokenOf(g_host) != Token::partner_restarting);
    // RESULT exiting arrived: no second H2 at the close.
    Setup(wire::Mode::scene, wire::Mode::scene);
    BindHost(30010);
    CHECK(Run(31000, GuestExited));
    Run(500);
    CHECK(TokenOf(g_host) == Token::partner_restarting && CountToast(g_host, Toast::h2_partner_restarting) == 1);
    g_host.core->OnPeerClose(g_now);
    Drain(g_host);
    CHECK(CountToast(g_host, Toast::h2_partner_restarting) == 1 && !HasLog(g_host, "how=link_closed"));
    // A stage-mode guest leaving is no restart.
    Setup(wire::Mode::scene, wire::Mode::stage);
    BindHost(30010);
    CHECK(Run(20000, GuestStaged));
    Run(500);
    CHECK(TokenOf(g_host) == Token::partner_staged);
    g_host.core->OnPeerClose(g_now);
    Drain(g_host);
    CHECK(TokenOf(g_host) == Token::partner_staged && CountToast(g_host, Toast::h2_partner_restarting) == 0);
}

int main(int argc, char** argv) {
    g_verbose = argc > 1 && std::strcmp(argv[1], "--verbose") == 0;
    TestDecisions();
    TestProvenance();
    TestDocuments();
    TestState();
    TestOffCore();
    TestTransferScene();
    TestTransferStage();
    TestIdenticalSets();
    TestSameScene();
    TestGuestStory();
    TestHostStory();
    TestUnbound();
    TestUnsupported();
    TestSettle();
    TestSessionReset();
    TestCorruptChunk();
    TestDiskError();
    TestDecline();
    TestTitleLost();
    TestStaleReport();
    TestPartnerMission();
    TestBuildGate();
    TestPacing();
    TestRefusedSets();
    TestSnapshotChanged();
    TestTimers();
    TestEdges();
    TestExitVeto();
    TestHostStall();
    TestRemoteOnlyGuest();
    TestSettleWait();
    TestGuestInGame();
    TestGuestStartup();
    TestExitFlush();
    TestHostInfersRestart();
    std::printf("save_handoff_core_tests: %u checks passed\n", checks);
    return 0;
}
