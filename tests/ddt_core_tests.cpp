// Unit tests for the Diamond Dogs Together launcher core (native/launcher/core):
// join codes, lab.ini / coop.ini text (the PowerShell generator's expected
// strings from tests/friend_package_tests.ps1, with the new header), the
// status line and reason sentences (tests/fixtures/link-reasons.json), launch
// evidence, redaction, the set hash (tests/fixtures/handoff-set.json), session
// activation (order and hash journal), hash-checked cleanup, the save handoff
// (validation, claim, import, relaunch), Share save, setup on a fake game
// folder with a fake Steam userdata, uninstall, Collect logs, the --watch
// helper and the real model end to end. Everything runs in %TEMP% folders
// with a fake Env (registry, processes, clock, shell, clipboard); the real
// game, Steam and the real clipboard are never touched.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core_env.hpp"
#include "core_install.hpp"
#include "core_json.hpp"
#include "core_log.hpp"
#include "core_model.hpp"
#include "core_paths.hpp"
#include "core_saves.hpp"
#include "core_session.hpp"
#include "core_text.hpp"
#include "core_update.hpp"
#include "core_util.hpp"
#include "launcher/zip.hpp"
#include "launcher_join_code.hpp"

using namespace ddt;
using namespace ddt::core;

namespace {

unsigned g_checks = 0, g_failures = 0;

void Check(bool ok, const char* what, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "FAILED (line %d): %s\n", line, what);
    }
}
void CheckEq(const std::string& a, const std::string& b, const char* what, int line) {
    ++g_checks;
    if (a != b) {
        ++g_failures;
        std::fprintf(stderr, "FAILED (line %d): %s\n  got:      [%s]\n  expected: [%s]\n", line, what, a.c_str(), b.c_str());
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)
#define CHECK_EQ(a, b) CheckEq((a), (b), #a, __LINE__)

bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
std::string Replace(std::string s, const std::string& from, const std::string& to) {
    const std::size_t at = s.find(from);
    if (at == std::string::npos) {
        std::fprintf(stderr, "test vector edit failed: %s\n", from.c_str());
        ++g_failures;
        return s;
    }
    return s.replace(at, from.size(), to);
}
std::string ReadText(const std::wstring& path) {
    std::string s;
    ReadTextShared(path, s, 64u << 20);
    return s;
}
void WriteText(const std::wstring& path, const std::string& text) {
    CreateDirectories(ParentOf(path));
    std::string error;
    if (PathExists(path)) DeleteFileChecked(path);
    if (!WriteAtomic(path, text, false, error)) {
        std::fprintf(stderr, "test setup write failed: %s\n", error.c_str());
        ++g_failures;
    }
}
std::int64_t T(int y, int mo, int d, int h, int mi, int s) {
    Civil c;
    c.year = y;
    c.month = mo;
    c.day = d;
    c.hour = h;
    c.minute = mi;
    c.second = s;
    return MsFromCivil(c);
}

// ---- Fake Env ----------------------------------------------------------------------
class FakeEnv final : public Env {
public:
    struct Proc {
        std::uint32_t pid = 0;
        std::wstring path;
        std::int64_t start = 0;
        bool alive = true;
        bool game = false;
    };
    std::recursive_mutex m;
    std::int64_t now = T(2026, 10, 23, 12, 0, 0);
    std::map<std::wstring, std::uint32_t> dwords;
    std::map<std::wstring, std::wstring> strings;
    std::vector<Proc> procs;
    std::uint32_t self = 4242;
    std::int64_t self_start = T(2026, 10, 23, 11, 0, 0);
    std::vector<std::wstring> urls;
    std::vector<std::string> watchdog_sessions;
    bool watchdog_ok = true;
    bool url_ok = true;
    std::uint32_t next_pid = 9000;
    std::string clip;
    bool clip_set = false;
    std::wstring desktop, temp;
    std::function<void()> on_sleep;

    static std::wstring Key(bool hklm, const std::wstring& k, const std::wstring& n) { return std::wstring(hklm ? L"M:" : L"U:") + k + L"|" + n; }
    void SetDword(const std::wstring& k, const std::wstring& n, std::uint32_t v) {
        std::lock_guard<std::recursive_mutex> l(m);
        dwords[Key(false, k, n)] = v;
    }
    void Advance(std::int64_t ms) {
        std::lock_guard<std::recursive_mutex> l(m);
        now += ms;
    }
    void AddGame(std::uint32_t pid, const std::wstring& path, std::int64_t start) {
        std::lock_guard<std::recursive_mutex> l(m);
        procs.push_back(Proc{pid, path, start, true, true});
    }
    void AddProcess(std::uint32_t pid, std::int64_t start) {
        std::lock_guard<std::recursive_mutex> l(m);
        procs.push_back(Proc{pid, L"C:\\Other\\helper.exe", start, true, false});
    }
    void Kill(std::uint32_t pid) {
        std::lock_guard<std::recursive_mutex> l(m);
        for (Proc& p : procs)
            if (p.pid == pid) p.alive = false;
    }
    std::size_t UrlCount() {
        std::lock_guard<std::recursive_mutex> l(m);
        return urls.size();
    }

    std::int64_t NowMs() override {
        std::lock_guard<std::recursive_mutex> l(m);
        return now;
    }
    void SleepMs(std::uint32_t ms) override {
        std::function<void()> hook;
        {
            std::lock_guard<std::recursive_mutex> l(m);
            now += ms;
            hook = on_sleep;
        }
        if (hook) hook();
    }
    Civil LocalTime(std::int64_t utc_ms) override { return CivilFromMs(utc_ms); }
    bool RegDword(bool hklm, const std::wstring& key, const std::wstring& name, std::uint32_t& out) override {
        std::lock_guard<std::recursive_mutex> l(m);
        auto it = dwords.find(Key(hklm, key, name));
        if (it == dwords.end()) return false;
        out = it->second;
        return true;
    }
    bool RegString(bool hklm, const std::wstring& key, const std::wstring& name, std::wstring& out) override {
        std::lock_guard<std::recursive_mutex> l(m);
        auto it = strings.find(Key(hklm, key, name));
        if (it == strings.end()) return false;
        out = it->second;
        return true;
    }
    std::vector<ProcessRow> GameProcesses() override {
        std::lock_guard<std::recursive_mutex> l(m);
        std::vector<ProcessRow> rows;
        for (const Proc& p : procs)
            if (p.alive && p.game) rows.push_back(ProcessRow{p.pid, p.path, p.path.empty()});
        return rows;
    }
    bool ProcessStartMs(std::uint32_t pid, std::int64_t& start_ms) override {
        std::lock_guard<std::recursive_mutex> l(m);
        if (pid == self) {
            start_ms = self_start;
            return true;
        }
        for (const Proc& p : procs)
            if (p.pid == pid && p.alive) {
                start_ms = p.start;
                return true;
            }
        return false;
    }
    bool ProcessAlive(std::uint32_t pid) override {
        std::int64_t s = 0;
        return ProcessStartMs(pid, s);
    }
    std::uint32_t CurrentPid() override {
        std::lock_guard<std::recursive_mutex> l(m);
        return self;
    }
    std::int64_t CurrentStartMs() override {
        std::lock_guard<std::recursive_mutex> l(m);
        return self_start;
    }
    bool OpenUrl(const std::wstring& url) override {
        std::lock_guard<std::recursive_mutex> l(m);
        urls.push_back(url);
        return url_ok;
    }
    bool StartWatchdog(const std::wstring&, const std::string& session_id, std::uint32_t& pid_out) override {
        std::lock_guard<std::recursive_mutex> l(m);
        if (!watchdog_ok) return false;
        pid_out = next_pid++;
        procs.push_back(Proc{pid_out, L"C:\\Fake\\DiamondDogsTogether.exe", now, true, false});
        watchdog_sessions.push_back(session_id);
        return true;
    }
    bool SetClipboardText(const std::string& text) override {
        std::lock_guard<std::recursive_mutex> l(m);
        clip = text;
        clip_set = true;
        return true;
    }
    bool GetClipboardText(std::string& text) override {
        std::lock_guard<std::recursive_mutex> l(m);
        text = clip;
        return clip_set;
    }
    bool ClearClipboard() override {
        std::lock_guard<std::recursive_mutex> l(m);
        clip.clear();
        clip_set = false;
        return true;
    }
    std::wstring DesktopDir() override { return desktop; }
    std::wstring TempDir() override { return temp; }
    std::string OsVersion() override { return "Windows test"; }
};

std::wstring g_root;

// A fake game folder (G = <root>\<name>\Steam\steamapps\common\MGS_TPP) with a
// fake Steam (SteamPath, ActiveUser 12345, userdata saves) and the release files.
struct Fixture {
    std::wstring root, steam, game, userdata, desktop;
    InstancePaths paths;
    std::shared_ptr<FakeEnv> env = std::make_shared<FakeEnv>();
    std::vector<ManifestRow> real_saves_before;

    explicit Fixture(const std::wstring& name, bool install_files = true) {
        root = g_root + L"\\" + name;
        RemoveTree(root);
        steam = root + L"\\Steam";
        game = steam + L"\\steamapps\\common\\MGS_TPP";
        userdata = steam + L"\\userdata\\12345";
        desktop = root + L"\\Desktop";
        CreateDirectories(game);
        CreateDirectories(desktop);
        CreateDirectories(root + L"\\Temp");
        paths = GetInstancePaths(game);
        env->desktop = desktop;
        env->temp = root + L"\\Temp";
        std::wstring forward = steam;
        std::replace(forward.begin(), forward.end(), L'\\', L'/');
        env->strings[FakeEnv::Key(false, L"Software\\Valve\\Steam", L"SteamPath")] = forward;
        env->dwords[FakeEnv::Key(false, L"Software\\Valve\\Steam\\ActiveProcess", L"ActiveUser")] = 12345;
        WriteText(userdata + L"\\287700\\local\\TPP_GAME_DATA0", "real-game-0");
        WriteText(userdata + L"\\287700\\local\\PERSONAL_DATA0", "real-personal-0");
        WriteText(userdata + L"\\287700\\local\\TPP_GRAPHICS_CONFIG", "real-graphics");
        WriteText(userdata + L"\\287700\\remotecache.vdf", "\"287700\" {}");
        WriteText(userdata + L"\\311340\\remote\\TPP_GAME_DATA", "real-remote-game");
        if (install_files) {
            WriteText(paths.exe, "FAKE-EXE");
            WriteText(paths.steam_api, "FAKE-STEAMAPI");
            WriteText(paths.loader, "FAKE-LOADER");
            WritePackage(DDT_CORE_VERSION);
        }
        real_saves_before = HashManifest(steam + L"\\userdata", {L"12345"});
    }
    ~Fixture() { RemoveTree(root); }
    void WritePackage(const std::string& version) {
        json::Value p = json::Value::Object();
        p["product"] = "mgs-coop-local-package";
        p["version"] = version;
        json::Value files = json::Value::Array();
        json::Value row = json::Value::Object();
        row["source"] = "payload/dinput8.dll";
        row["target"] = "dinput8.dll";
        row["sha256"] = Sha256Hex(std::string("FAKE-LOADER"));
        files.Push(row);
        p["files"] = files;
        p["executable_sha256"] = kExeSha256;
        p["steam_dll_sha256"] = kSteamApiSha256;
        WriteText(paths.package_json, json::Dump(p) + "\n");
    }
    bool RealSavesUntouched() const { return CompareManifest(real_saves_before, HashManifest(steam + L"\\userdata", {L"12345"})).same; }
    std::wstring GameExe() const { return paths.exe; }
};

void InstallTestHooks() {
    InstallHooks& h = Hooks();
    h.exe_sha256 = Sha256Hex(std::string("FAKE-EXE"));
    h.steam_api_sha256 = Sha256Hex(std::string("FAKE-STEAMAPI"));
    h.verify_runtime = [](const std::wstring& path, std::string& error) {
        if (ReadText(path) == "FAKE-RUNTIME") return true;
        error = "runtime missing";
        return false;
    };
    h.build_runtime = [](const std::wstring&, const std::wstring& out, const std::function<void(float, const std::string&)>& progress,
                         std::string& error) {
        if (!IsDir(ParentOf(out))) {   // like BuildRuntimeDat: the folder must exist
            error = "The output folder does not exist.";
            return false;
        }
        if (progress) progress(0.5f, "Building");
        return WriteAtomic(out, std::string("FAKE-RUNTIME"), true, error);
    };
}

const std::string kSaveDir = "C:\\Games\\MGS_TPP\\mgs-coop\\coop-saves";
std::string DefaultsText() {
    return "partner=1\npartner_stage=3\npartner_source=net\npartner_apply=xform\npartner_anim=off\npartner_aset=qs\npartner_reset=n\n"
           "partner_look=net\npartner_rate_hz=30\npartner_interp_ms=100\npartner_offset_cm=300\npartner_teardown=clear\n"
           "partner_force_exists=0\npartner_policy_relax=0\npartner_anim_probe=0\npartner_dm=map\ncrash_probe=1\n"
           "partner_input_send=1\npartner_native_profile=gun";
}

// ---- Pure pieces ---------------------------------------------------------------------------
void TestJsonAndTime() {
    json::Value v;
    CHECK(json::Parse("{\"a\":1,\"b\":[true,null,\"x\\u00e9\\n\"],\"c\":{\"d\":-2.5},\"e\":\"\\ud83d\\ude00\"}", v));
    CHECK(v.Int("a") == 1 && v.Find("b")->size() == 3 && v.Find("b")->Elements()[2].StringRef() == "x\xC3\xA9\n");
    CHECK(v.Find("c")->Find("d")->AsDouble() == -2.5);
    CHECK_EQ(v.Str("e"), "\xF0\x9F\x98\x80");
    json::Value back;
    CHECK(json::Parse(json::Dump(v), back) && json::Dump(back) == json::Dump(v));
    CHECK(!json::Parse("{\"a\":1,}", back) && !json::Parse("[1 2]", back) && !json::Parse("", back));
    CHECK(json::Parse("\xEF\xBB\xBF{\"v\":\"1\"}", back) && back.Str("v") == "1" && back.Int("v") == 1);
    CHECK_EQ(json::Value(true).AsText(), "True");
    std::int64_t ms = 0;
    CHECK(ParseUtc("2026-10-23T18:14:03.1900197Z", ms) && FormatUtcRoundTrip(ms) == "2026-10-23T18:14:03.1900000Z");
    CHECK(ParseUtc("2026-10-23T13:14:03-05:00", ms) && FormatUtcSeconds(ms) == "2026-10-23T18:14:03Z");
    CHECK(ParseUtc("2026-10-05T12:00:00", ms) && FormatUtcSeconds(ms) == "2026-10-05T12:00:00Z");
    CHECK(!ParseUtc("yesterday", ms) && !ParseUtc("2026-13-01T00:00:00Z", ms));
    Civil c;
    c.year = 2026;
    c.month = 1;
    c.day = 2;
    c.hour = 3;
    c.minute = 4;
    c.second = 5;
    CHECK_EQ(FormatStamp(c), "20260102-030405");
}

void TestCodes() {
    const char* vectors[][3] = {{"ABCDE12345FGHJK67890", "76561197960365728", "4G"},
                                {"C00PC00PC00PC00PC00P", "76561197960265729", "D1"},
                                {"ZZZZZYYYYYXXXXXWWWWW", "76561202255233023", "5D"},
                                {"0123456789ABCDEFGHJK", "76561197960465728", "W4"}};
    for (const auto& v : vectors) {
        const std::string s = v[0];
        const std::string want = "MGS2-" + s.substr(0, 5) + "-" + s.substr(5, 5) + "-" + s.substr(10, 5) + "-" + s.substr(15, 5) + "-" + v[2] + "@" + v[1];
        CHECK_EQ(FormatJoinCode(s, std::stoull(v[1])), want);
        std::string secret;
        std::uint64_t host = 0;
        CHECK(ParseJoinCode(want, secret, host) && secret == s && Decimal(host) == v[1]);
    }
    std::string secret;
    std::uint64_t host = 0;
    CHECK(ParseJoinCode(" mgs2 abcde i2345 fghjk 67890 4g @ 76561197960365728", secret, host) && secret == "ABCDE12345FGHJK67890");
    CHECK(ParseJoinCode("\tMGS2-ABCDE-12345-\r\nFGHJK-67890-4G@76561197960365728\v\f\n", secret, host));
    CHECK(ParseJoinCode("+mgscoop_join MGS2-ABCDE-12345-FGHJK-67890-4G@76561197960365728", secret, host));
    const char* bad[] = {"", "MGS1-ABCDE-12345-FGHJK-67890-4G@203.0.113.7", "MGS2-ABCDE-12345-FGHJK-67890-4G",
                         "MGS2-ABCDU-12345-FGHJK-67890-4G@76561197960365728", "MGS2-ABCDE-12345-FGHJK-6789-6Z@76561197960365728",
                         "MGS2-ABCDE-12345-FGHJK-67890-4H@76561197960365728", "MGS2-ABCDE-12345-FGHJK-67890-4G@76561197960365729",
                         "MGS2-ABCDE-12345-FGHJK-67890-4G@076561197960365728", "MGS2-ABCDE-12345-FGHJK-67890-4G@103582791429521408",
                         "MGS2-ABCDE-12345-FGHJK-67890-4G@76561197960265728"};
    for (const char* b : bad) CHECK(!ParseJoinCode(b, secret, host) && !CheckJoinCode(b, 0).ok);
    CHECK(CheckJoinCode("", 0).message.empty());
    CHECK(Has(CheckJoinCode(bad[1], 0).message, "older version"));
    CHECK(Has(CheckJoinCode(bad[5], 0).message, "typo"));
    CHECK(Has(CheckJoinCode(bad[2], 0).message, "after @"));
    const std::string own = FormatJoinCode("ABCDE12345FGHJK67890", 76561197960365728ull);
    CHECK(!CheckJoinCode(own, 76561197960365728ull).ok && Has(CheckJoinCode(own, 76561197960365728ull).message, "your own code"));
    CHECK(CheckJoinCode(own, 76561197960465728ull).ok);
    CHECK_EQ(SteamId64FromAccount(1), "76561197960265729");
    CHECK_EQ(SteamId64FromAccount(4294967295u), "76561202255233023");
    CHECK(SteamId64FromAccount(0).empty());
    CHECK(!IsIndividualSteamId64("76561197960265728") && IsIndividualSteamId64("76561197960265729") && !IsIndividualSteamId64("7656119796026572"));
    for (int i = 0; i < 20; ++i) CHECK(IsCanonicalSecret(NewSecret()));
    CHECK(!IsCanonicalSecret("C00PC00PC00PC00PC00U") && !IsCanonicalSecret("short"));
    const std::string ok_crlf = "ABCDE12345FGHJK67890\r\n", ok_lf = "ABCDE12345FGHJK67890\n", bad_len = "ABCDE12345FGHJK67890",
                      bad_char = "ABCDE12345FGHJK6789U\n";
    CHECK_EQ(SecretFileStatus(&ok_crlf), "ok");
    CHECK_EQ(SecretFileStatus(&ok_lf), "ok");
    CHECK_EQ(SecretFileStatus(&bad_len), "bad_length");
    CHECK_EQ(SecretFileStatus(&bad_char), "bad_char");
    CHECK_EQ(SecretFileStatus(nullptr), "missing");
}

void TestIniText() {
    const std::string header = kHeader;
    CHECK_EQ(header, "# Generated by Diamond Dogs Together");
    std::string text, error;
    LabIniParams host;
    host.role = "host";
    host.save_dir = kSaveDir;
    host.steam_self = "76561197960365728";
    CHECK(MakeLabIniText(host, text, error));
    // tests/friend_package_tests.ps1 "online host lab.ini: no UDP key", with the new header.
    CHECK_EQ(text, header + "\nenabled=1\nmode=play\nrole=host\nsave_dir=" + kSaveDir +
                       "\nnet=1\nnet_transport=steam\nnet_steam_self=76561197960365728\npartner_interp=auto\n" + DefaultsText() + "\nworld=3\n");
    // Play-Coop's session text: save_handoff=scene just before the world line.
    LabIniParams scene = host;
    scene.save_handoff = "scene";
    std::string scene_text;
    CHECK(MakeLabIniText(scene, scene_text, error) && scene_text == Replace(text, "world=3\n", "save_handoff=scene\nworld=3\n"));
    LabIniParams guest = host;
    guest.role = "guest";
    guest.steam_self = "76561197960465728";
    guest.steam_peer = "76561197960365728";
    std::string guest_text;
    CHECK(MakeLabIniText(guest, guest_text, error));
    CHECK(Has(guest_text, "net_transport=steam\nnet_steam_self=76561197960465728\nnet_peer_steam=76561197960365728\n") &&
          !Has(guest_text, "net_bind") && !Has(guest_text, "net_peer_addr"));
    LabIniParams wait = guest;
    wait.steam_peer.clear();
    wait.steam_wait = true;
    std::string other;
    CHECK(MakeLabIniText(wait, other, error) && Has(other, "net_steam_self=76561197960465728\nnet_steam_wait=1\npartner_interp=auto\n"));
    LabIniParams own = guest;
    own.steam_peer = own.steam_self;
    CHECK(!MakeLabIniText(own, other, error) && Has(error, "own code"));
    LabIniParams host_peer = host;
    host_peer.steam_peer = "76561197960465728";
    CHECK(!MakeLabIniText(host_peer, other, error) && Has(error, "no Steam peer"));
    LabIniParams bad_self = host;
    bad_self.steam_self = "1";
    CHECK(!MakeLabIniText(bad_self, other, error) && Has(error, "SteamID64"));
    LabIniParams dispatch = host;
    dispatch.steam_dispatch = "FnPtr";
    CHECK(MakeLabIniText(dispatch, other, error) && Has(other, "net_steam_self=76561197960365728\nnet_steam_dispatch=fnptr\npartner_interp=auto\n"));
    dispatch.steam_dispatch = "fast";
    CHECK(!MakeLabIniText(dispatch, other, error));
    LabIniParams world0 = host;
    world0.world = 0;
    world0.save_handoff = "always";
    world0.travel_lock = "off";
    CHECK(MakeLabIniText(world0, other, error) && EndsWith(other, "partner_native_profile=gun\nsave_handoff=always\ntravel_lock=0\n"));
    LabIniParams clips = host;
    clips.partner = "clips";
    CHECK(MakeLabIniText(clips, other, error) && other == Replace(text, "partner_native_profile=gun\n", ""));
    LabIniParams lock_on = host;
    lock_on.travel_lock = "on";
    CHECK(MakeLabIniText(lock_on, other, error) && other == text);
    LabIniParams rel = host;
    rel.save_dir = "relative\\x";
    CHECK(!MakeLabIniText(rel, other, error) && Has(error, "absolute"));
    for (const std::string forbidden : {"loopback", "self_steamid", "peer_steamid", "mutex_suffix", "xinput", "net_link", "world_probe"})
        CHECK(!Has(guest_text, "\n" + forbidden + "="));
    CHECK(!Has(text, "\r"));
    // coop.ini (tests/friend_package_tests.ps1 "play coop.ini text").
    std::string coop;
    CHECK(MakeCoopIniText("quiet", "dd_male", "", coop, error));
    CHECK_EQ(coop, header + "\nmaster_url=http://127.0.0.1:47899/tppstm/gate\ncharacter=quiet\ncharacter_fallback=dd_male\n");
    std::string hud;
    CHECK(MakeCoopIniText("quiet", "dd_male", "Off", hud, error) && hud == coop + "hud=off\n");
    CHECK(!MakeCoopIniText("dog", "dd_male", "", hud, error) && Has(error, "Invalid character"));
    CHECK(!MakeCoopIniText("quiet", "dd_male", "big", hud, error));
    CHECK(MakeCoopIniText("story", "dd_male", "compact", hud, error) && Has(hud, "\ncharacter=story\n"));
    CHECK(MakeCoopIniText("snake", "dd_male", "full", hud, error) && Has(hud, "\ncharacter=snake\ncharacter_fallback=dd_male\nhud=full\n"));
    CHECK(IsGeneratedIniText(coop) && IsGeneratedIniText(kOldPlayHeader + std::string("\nenabled=1\n")) && !IsGeneratedIniText("enabled=1\n") &&
          !IsGeneratedIniText("# Generated by Diamond Dogs Together X\n"));
    const auto ini = ParseIni("# c\n a = 1 \n;x=2\nb=2\na=3\n");
    CHECK(ini.size() == 2 && ini[0].first == "a" && ini[0].second == "3");
    // The record a session keeps round-trips.
    LabIniParams back;
    CHECK(LabParamsFromRecord(LabRecord(scene), back, error));
    std::string back_text;
    CHECK(MakeLabIniText(back, back_text, error) && back_text == scene_text);
    // The per-folder mutex name (the PowerShell tooling's: SHA-256 of "C:\GAMES\MGS_TPP").
    CHECK(PlayMutexName(L"C:\\Games\\MGS_TPP") == L"Local\\mgs-coop-play-BD40F65C84F1052B");
    CHECK(PlayMutexName(L"c:\\games\\mgs_tpp\\") == PlayMutexName(L"C:\\Games\\MGS_TPP"));
}

void TestReasonFixture() {
    json::Value fixture;
    const std::string text = ReadText(Wide(DDT_SOURCE_DIR) + L"\\tests\\fixtures\\link-reasons.json");
    CHECK(json::Parse(text, fixture));
    std::size_t n = 0;
    if (const json::Value* rows = fixture.Find("link_reasons")) {
        for (const json::Value& row : rows->Elements()) {
            const std::string got = LinkReasonText(row.Str("reason"), row.Str("remote"), row.Str("own"), row.Int("verified", 1) != 0, Wording::powershell);
            CheckEq(got, row.Str("text"), ("link reason " + row.Str("reason")).c_str(), __LINE__);
            ++n;
        }
    }
    if (const json::Value* rows = fixture.Find("steam_end")) {
        for (const json::Value& row : rows->Elements()) {
            CheckEq(SteamEndText(row.Int("end"), Wording::powershell), row.Str("text"), ("steam end " + row.Str("end")).c_str(), __LINE__);
            ++n;
        }
    }
    CHECK(n >= 50);
    // The launcher wording names no PowerShell tool and no LAN/VPN code.
    for (const char* r : {"build_mismatch", "proto_mismatch", "mode_mismatch", "role_mismatch", "busy", "bad_code", "secret_missing", "crypto",
                          "no_peer_steam", "steam_offline", "steam_self_mismatch", "steam_peer_is_self", "steam_api", "steam_not_initialized",
                          "steam_client_module", "steam_iface_shape", "steam_owner_unavailable", "steam_fault", "steam_acquire_timeout",
                          "steam_transport", "transport_bind", "transport_winsock", "weird"}) {
        const std::string s = LinkReasonText(r, "0.14.0", "0.14.0", false, Wording::launcher);
        CHECK(!s.empty() && !Has(s, ".bat") && !Has(s, "Play-Coop") && !Has(s, "[L]") && !Has(s, "LAN/VPN") && !Has(s, "Collect-Logs"));
    }
    for (std::int64_t e : {5003, 4001, 5008, 5009, 5005, 3004, 4002, 4006, 1000, 2000}) {
        const std::string s = SteamEndText(e, Wording::launcher);
        CHECK(!s.empty() && !Has(s, "[L]") && !Has(s, "Play-Coop") && !Has(s, "LAN/VPN"));
    }
    for (const char* t : {"wait_host", "receiving", "staged", "countdown", "exiting", "offered", "sending", "partner_staged", "kept", "refused", "failed", "applied"}) {
        const HandoffText h = GetHandoffText(t, 10, true, Wording::launcher);
        CHECK(!Has(h.line, ".bat") && !Has(h.line, "Play-Coop") && !Has(h.line, "this window"));
    }
}

void TestStatusLine() {
    const std::string status_text =
        "{\"v\":1,\"pid\":4242,\"mode\":\"play\",\"role\":\"host\",\"state\":\"connected\",\"reason\":\"\",\"verified\":1,\"session\":1,"
        "\"peer\":\"203.0.x.x:51234\",\"local_port\":47830,\"rtt_ms\":42,\"jitter_ms\":3,\"loss_permille\":4,\"resend\":0,\"tx_Bps\":3900,"
        "\"rx_Bps\":3800,\"interp_ms\":80,\"remote_build\":\"0.12.0\",\"partner\":\"live\",\"partner_shown\":1,\"look\":1,\"mission\":30050,"
        "\"saves\":\"redirected\",\"time_utc\":\"2026-10-05T12:00:00Z\"}";
    std::int64_t now = 0;
    ParseUtc("2026-10-05T12:00:03Z", now);
    const json::Value st = ParseStatusText(status_text, 4242);
    CHECK_EQ(FormatStatusLine(st, true, now, ""), "[HOST] link=connected rtt=42ms jitter=3ms loss=0.4% interp=80ms | partner=live shown look | mission 30050");
    CHECK(ParseStatusText(status_text, 4243).is_null());
    CHECK(ParseStatusText("{\"v\":2,\"state\":\"x\"}", 0).is_null() && ParseStatusText("{\"v\":1,", 0).is_null() && ParseStatusText("", 0).is_null());
    CHECK(StartsWith(FormatStatusLine(st, false, now + 30000, ""), "[JOIN] game not responding"));
    const json::Value disabled =
        ParseStatusText(Replace(status_text, "\"state\":\"connected\",\"reason\":\"\"", "\"state\":\"disabled\",\"reason\":\"secret_missing\""), 0);
    CHECK(StartsWith(FormatStatusLine(disabled, false, now + 600000, ""), "[JOIN] link=disabled (secret_missing: "));
    CHECK(EndsWith(FormatStatusLine(ParseStatusText(Replace(status_text, "\"mission\":30050", "\"mission\":10020"), 0), true, now, ""),
                   "mission 10020 - Partner body unavailable in this mission; use Mother Base or free roam."));
    const json::Value rejected = ParseStatusText(Replace(Replace(status_text, "\"state\":\"connected\",\"reason\":\"\"", "\"state\":\"rejected\",\"reason\":\"build_mismatch\""),
                                                         "\"remote_build\":\"0.12.0\"", "\"remote_build\":\"0.12.1\""),
                                                 0);
    CHECK(Has(FormatStatusLine(rejected, false, now, "0.12.0"), "Your partner runs Diamond Dogs Together 0.12.1; you run 0.12.0. Both must use the same version."));
    CHECK_EQ(FormatStatusLine(ParseStatusText("{\"v\":1,\"state\":\"connecting\"}", 0), false, now, ""), "[JOIN] link=connecting | partner=none | mission 0");
    const std::string world_keys = ",\"world\":3,\"paired\":0,\"partner_mission\":30010,\"pair_wait\":\"different_mission\",\"progress\":\"ok\",\"time_utc\"";
    const json::Value apart = ParseStatusText(Replace(Replace(status_text, "\"mission\":30050", "\"mission\":10020"), ",\"time_utc\"", world_keys), 0);
    CHECK_EQ(FormatStatusLine(apart, true, now, ""),
             "[HOST] link=connected rtt=42ms jitter=3ms loss=0.4% interp=80ms | partner=live shown look | mission 10020 - your partner is in "
             "Afghanistan free roam. Different missions never pair: meet in the same Mother Base, free roam or helicopter.");
    for (const char* quiet : {"40050", "1", "40010", "40020"}) {
        const json::Value q = ParseStatusText(Replace(status_text, "\"mission\":30050", std::string("\"mission\":") + quiet), 0);
        CHECK(EndsWith(FormatStatusLine(q, true, now, ""), std::string("| mission ") + quiet));
    }
    const json::Value story = ParseStatusText(Replace(Replace(status_text, "\"mission\":30050", "\"mission\":40010"), ",\"time_utc\"",
                                                      ",\"world\":3,\"paired\":0,\"partner_mission\":0,\"pair_wait\":\"no_hello\",\"progress\":\"story_start\",\"time_utc\""),
                                              0);
    CHECK(EndsWith(FormatStatusLine(story, true, now, ""),
                   "| mission 40010 Your co-op save is at the story start: quit, then import your partner's save with Share-Save.bat (option I)."));
    // Steam online.
    const std::string steam_text =
        "{\"v\":1,\"pid\":4242,\"mode\":\"play\",\"role\":\"host\",\"state\":\"connected\",\"reason\":\"\",\"verified\":1,\"session\":1,"
        "\"peer\":\"steam:...5728\",\"local_port\":0,\"rtt_ms\":61,\"jitter_ms\":3,\"loss_permille\":0,\"resend\":0,\"tx_Bps\":3900,"
        "\"rx_Bps\":3800,\"interp_ms\":80,\"remote_build\":\"0.12.0\",\"partner\":\"live\",\"partner_shown\":1,\"look\":1,\"mission\":30050,"
        "\"saves\":\"redirected\",\"transport\":\"steam\",\"route\":\"relay\",\"accepting\":1,\"steam\":\"ok\",\"time_utc\":\"2026-10-05T12:00:00Z\"}";
    const json::Value sst = ParseStatusText(steam_text, 4242);
    CHECK_EQ(FormatStatusLine(sst, true, now, ""),
             "[HOST] link=connected via Steam (relay) rtt=61ms jitter=3ms loss=0.0% interp=80ms | partner=live shown look | mission 30050");
    const json::Value waiting = ParseStatusText(Replace(Replace(steam_text, "\"role\":\"host\",\"state\":\"connected\"", "\"role\":\"guest\",\"state\":\"waiting\""),
                                                        "\"route\":\"relay\"", "\"route\":\"none\""),
                                                0);
    CHECK(StartsWith(FormatStatusLine(waiting, false, now, ""), "[JOIN] link=waiting via Steam - waiting for an invite or a code"));
    const json::Value left = ParseStatusText(Replace(Replace(steam_text, "\"state\":\"connected\"", "\"state\":\"lost\""), "\"steam\":\"ok\"",
                                                     "\"steam\":\"ok\",\"steam_end\":0,\"left\":1"),
                                             0);
    CHECK(Has(FormatStatusLine(left, true, now, ""), " - your friend left; waiting for them to come back"));
    const json::Value ended = ParseStatusText(Replace(Replace(steam_text, "\"role\":\"host\",\"state\":\"connected\"", "\"role\":\"guest\",\"state\":\"connecting\""),
                                                      "\"steam\":\"ok\"", "\"steam\":\"ok\",\"steam_end\":5003,\"left\":0"),
                                              0);
    const std::string end_line = FormatStatusLine(ended, false, now, "");
    CHECK(Has(end_line, "(Steam: The host's game did not answer.") && !Has(end_line, "left"));
    const std::string stale = Replace(Replace(status_text, "\"role\":\"host\",\"state\":\"connected\",\"reason\":\"\",\"verified\":1",
                                              "\"role\":\"guest\",\"state\":\"reconnecting\",\"reason\":\"bad_code\",\"verified\":0"),
                                      "\"saves\":\"redirected\"", "\"saves\":\"redirected\",\"left\":1");
    const std::string stale_line = FormatStatusLine(ParseStatusText(stale, 0), false, now, "");
    CHECK(Has(stale_line, " - the host restarted with a new code: paste it in F9 > Partner / Co-op > Online") && !Has(stale_line, "the host left"));
    // Handoff segments (tests/friend_package_tests.ps1 table).
    const char* rows[][4] = {{"guest", "wait_host", "0", " | waiting for the host's save"},
                             {"guest", "receiving", "40", " | getting the host's save 40%"},
                             {"guest", "staged", "100", " | host save ready, MGSV restarts"},
                             {"guest", "countdown", "100", " | host save ready, MGSV restarts"},
                             {"guest", "exiting", "100", " | host save ready, MGSV restarts"},
                             {"host", "offered", "0", " | sending your save to your friend 0%"},
                             {"host", "sending", "75", " | sending your save to your friend 75%"},
                             {"host", "partner_staged", "100", " | your friend is restarting on your save"},
                             {"host", "partner_restarting", "0", " | your friend is restarting on your save"},
                             {"guest", "kept", "0", " | save not shared: you kept your own save"},
                             {"host", "kept", "0", " | save not shared: your friend kept their own save"},
                             {"guest", "failed", "0", " | save not shared: the transfer failed"},
                             {"host", "failed", "0", " | save not shared: the transfer failed"},
                             {"guest", "applied", "0", " | playing on the host's save"},
                             {"guest", "refused", "0", " | save not shared: the host's save does not continue into Mother Base or free roam yet"},
                             {"host", "refused", "0", " | save not shared: your save does not continue into Mother Base or free roam yet"}};
    for (const auto& r : rows) {
        const bool guest = std::string(r[0]) == "guest";
        const HandoffText h = GetHandoffText(r[1], std::stoi(r[2]), guest, Wording::powershell);
        CheckEq(h.segment, r[3], r[1], __LINE__);
        CHECK(!h.line.empty());
        const json::Value hs = ParseStatusText(Replace(status_text, ",\"time_utc\"", std::string(",\"handoff\":\"") + r[1] + "\",\"handoff_pct\":" + r[2] + ",\"time_utc\""), 0);
        CHECK_EQ(FormatStatusLine(hs, !guest, now, ""),
                 std::string(guest ? "[JOIN]" : "[HOST]") + " link=connected rtt=42ms jitter=3ms loss=0.4% interp=80ms | partner=live shown look" + r[3] + " | mission 30050");
    }
    CHECK_EQ(GetHandoffText("receiving", 250, true, Wording::powershell).segment, " | getting the host's save 100%");
    for (const char* quiet : {"off", "idle", "same", "something_new", ""}) {
        const HandoffText h = GetHandoffText(quiet, 0, true, Wording::powershell);
        CHECK(h.segment.empty() && h.line.empty());
    }
    // Mission names.
    CHECK_EQ(MissionName(0), "a loading screen");
    CHECK_EQ(MissionName(30050), "Mother Base");
    CHECK_EQ(MissionName(40010), "the Afghanistan helicopter");
    CHECK_EQ(MissionName(11043), "story mission 11043");
    CHECK_EQ(MissionName(12000), "mission 12000");
    CHECK_EQ(MissionName(4294967295ull), "mission 4294967295");
    // The In game page's view.
    LinkView v = LinkViewFromStatus(sst, true, now, "0.14.0");
    CHECK(v.connected && v.ping_ms == 61 && v.headline == "Connected to your partner" && v.partner_mission == "Mother Base");
    v = LinkViewFromStatus(waiting, false, now, "0.14.0");
    CHECK(!v.connected && v.headline == "Waiting for an invite" && Has(v.detail, "Shift+Tab"));
    v = LinkViewFromStatus(st, true, now + 60000, "0.14.0");
    CHECK(v.headline == "The game is not responding");
    v = LinkViewFromStatus(ParseStatusText(Replace(status_text, ",\"time_utc\"", ",\"handoff\":\"receiving\",\"handoff_pct\":40,\"time_utc\""), 0), false, now, "");
    CHECK(v.handoff_active && v.handoff_progress > 0.39f && v.handoff_progress < 0.41f && v.headline == "Getting the host's save" &&
          Has(v.detail, "Restore previous"));
    v = LinkViewFromStatus(ParseStatusText(stale, 0), false, now, "");
    CHECK(v.headline == "The host started a new game");
    v = LinkViewFromStatus(json::Value(), true, now, "");
    CHECK(!v.connected && !v.headline.empty());
}

void TestEvidence() {
    const std::string ok_early = "saves=redirected; dir=" + kSaveDir + "\r\nplay=1; loopback=0; mutex=unchanged; refused=0x0; saves=redirected\r\n";
    const std::string ok_steam = "master_server=redirected; method=test; url=http://127.0.0.1:47899/tppstm/gate\r\n";
    const std::string ok_observer = "play=active; role=host; partner=1; refused=0x0\r\nlink=config; mode=play; role=host; proto=2; bind=steam; port=0; peer=learned; auth=code; interp=auto\r\n";
    CHECK_EQ(GetLaunchEvidence(ok_early, ok_steam, ok_observer, kSaveDir, false, true).state, "ok");
    Evidence ev = GetLaunchEvidence(ok_early, ok_steam, "play=active; role=host; partner=1; refused=0x0\r\n", kSaveDir, false, true);
    CHECK(ev.state == "pending" && !ev.missing.empty() && Has(ev.missing.back(), "bind=steam"));
    ev = GetLaunchEvidence(ok_early, ok_steam, ok_observer + "link=disabled; reason=steam_offline\r\n", kSaveDir, false, true);
    CHECK(ev.state == "incomplete" && ev.link_reason == "steam_offline" && !ev.problems.empty() && Has(ev.problems.back(), "Steam is offline"));
    const std::string good_early = "early=start; version=0.12.0\r\nsaves=redirected; dir=" + kSaveDir +
                                   "\r\nplay=1; loopback=0; mutex=unchanged; refused=0x0; saves=redirected\r\n";
    const std::string good_steam = "master_server=redirected; method=x; url=http://127.0.0.1:47899/tppstm/gate\r\n";
    const std::string good_observer = "mode=offline_v1; x\r\nplay=active; role=host; partner=1; refused=0x0\r\n";
    CHECK_EQ(GetLaunchEvidence(good_early, good_steam, good_observer, kSaveDir, false, false).state, "ok");
    CHECK_EQ(GetLaunchEvidence(good_early, good_steam, "play=refused; reason=saves_not_redirected; refused=0x0\r\n", kSaveDir, false, false).state, "unsafe");
    CHECK_EQ(GetLaunchEvidence("saves=unchanged\r\n", "", "", kSaveDir, false, false).state, "unsafe");
    CHECK_EQ(GetLaunchEvidence("saves=redirected; dir=C:\\elsewhere\r\n", "", "", kSaveDir, false, false).state, "unsafe");
    CHECK_EQ(GetLaunchEvidence("", good_steam, good_observer, kSaveDir, true, false).state, "unsafe");
    ev = GetLaunchEvidence("early=start; version=0.11.1\r\nsaves=redirected; dir=" + kSaveDir + "\r\n", good_steam, "", kSaveDir, true, false);
    CHECK(ev.state == "old_loader" && std::any_of(ev.missing.begin(), ev.missing.end(), [](const std::string& m) { return Has(m, "script-observer.log play=active"); }));
    ev = GetLaunchEvidence(Replace(good_early, "refused=0x0", "refused=0x4000"), good_steam, good_observer, kSaveDir, true, false);
    CHECK(ev.state == "incomplete" && std::any_of(ev.problems.begin(), ev.problems.end(), [](const std::string& p) { return Has(p, "refused=0x4000"); }));
    ev = GetLaunchEvidence(good_early, "", good_observer, kSaveDir, true, false);
    CHECK(ev.state == "incomplete" && ev.missing.size() == 1 && StartsWith(ev.missing[0], "steam-observer.log"));
    CHECK_EQ(GetLaunchEvidence(good_early, "master_server=redirected; method=x; url=https://mgstpp-game.konamionline.com/tppstm/gate\r\n", good_observer,
                               kSaveDir, false, false).state,
             "incomplete");
    // Only new complete lines count (Read-LogFromOffset).
    const std::wstring dir = g_root + L"\\evidence";
    CreateDirectories(dir);
    const std::wstring log = dir + L"\\early.log";
    WriteText(log, "early=start; old\r\nsaves=redirected; dir=C:\\old\r\n");
    std::int64_t offset = static_cast<std::int64_t>(FileSizeOrZero(log));
    HANDLE h = CreateFileW(log.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    const std::string more = "early=start; new\r\nsaves=redirected; dir=" + kSaveDir + "\r\npartial";
    DWORD wrote = 0;
    WriteFile(h, more.data(), static_cast<DWORD>(more.size()), &wrote, nullptr);
    CloseHandle(h);
    std::string delta;
    ReadNewLines(log, offset, delta);
    CHECK(Has(delta, "dir=" + kSaveDir) && !Has(delta, "C:\\old") && !Has(delta, "partial"));
    RemoveTree(dir);
}

void TestRedaction() {
    const std::string s7 = "7K3QZ9M2XWD4HT8RVN6B";
    const std::string code1 = "MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@203.0.113.7:47830";
    const std::string leaky = "code=" + code1 + "\nlower " + LowerAscii(code1) + "\nspaced MGS1 7K3QZ 9M2XW D4HT8 RVN6B DD @ 203.0.113.7\n" +
                              "link=connected; peer=203.0.113.7:51234; x\nnet_peer_addr=198.51.100.23\n{\"peer\":\"203.0.113.9:47830\"}\nhost=8.8.4.4\n"
                              "exe 1.0.15.4 version=10.0.19045.1 at 203.0.113.50\npeer=192.168.1.20:47830\n";
    const std::string clean = RedactNetwork(leaky);
    CHECK(!Has(clean, "7K3QZ") && !Has(UpperAscii(clean), "7K3QZ") && !Has(clean, "RVN6B"));
    std::size_t count = 0;
    for (std::size_t at = clean.find("MGS1-<redacted>"); at != std::string::npos; at = clean.find("MGS1-<redacted>", at + 1)) ++count;
    CHECK(count == 3);
    CHECK(Has(clean, "peer=203.0.x.x:51234") && Has(clean, "net_peer_addr=198.51.x.x") && Has(clean, "\"peer\":\"203.0.x.x:47830\"") && Has(clean, "host=8.8.x.x"));
    CHECK(Has(clean, "1.0.15.4") && Has(clean, "10.0.19045.1") && Has(clean, "at 203.0.113.50") && Has(clean, "peer=192.168.1.20:47830"));
    const std::string code2 = FormatJoinCode(s7, 76561197960365728ull);
    const std::string steam_clean = Redact("code " + code2 + "\nlower " + LowerAscii(code2) + "\nconnect +mgscoop_join " + code2 +
                                           "\nsteam=join; via=invite; host=steam:...5728\n");
    CHECK(!Has(UpperAscii(steam_clean), "7K3QZ") && !Has(steam_clean, "76561197960365728"));
    CHECK(Has(steam_clean, "+mgscoop_join <redacted>") && Has(steam_clean, "host=steam:...5728"));
    // SteamIDs, userdata accounts and user names.
    const std::string sensitive = RedactSensitive("id 76561197960365728 n 12345678901234567 path C:\\Program Files (x86)\\Steam\\userdata\\12345678\\287700 "
                                                  "D:/Users/Jane Doe/Desktop x C:\\Users\\bob\\AppData");
    CHECK(Has(sensitive, "7656119XXXXXX5728") && Has(sensitive, "12345678901234567") && Has(sensitive, "userdata\\<account>\\287700"));
    CHECK(Has(sensitive, "D:/Users/<user>/Desktop") && Has(sensitive, "C:\\Users\\<user>\\AppData") && !Has(sensitive, "Jane") && !Has(sensitive, "bob"));
    CHECK_EQ(MaskAddress("203.0.113.7:51234"), "203.0.x.x:51234");
    for (const char* a : {"127.0.0.1", "10.1.2.3", "172.16.0.9", "172.31.255.1", "192.168.1.20:47830", "100.64.0.1", "100.127.255.255", "26.1.2.3"})
        CHECK_EQ(MaskAddress(a), a);
    for (const char* a : {"172.32.0.1", "100.128.0.1", "11.0.0.1", "192.169.0.1"}) CHECK(Has(MaskAddress(a), ".x.x"));
    const std::string log_line = RedactForLog("join " + code2 + " from D:\\Games\\MGS_TPP\\mgs-coop as 76561197960365728");
    CHECK(!Has(log_line, "7K3QZ") && !Has(log_line, "Games") && !Has(log_line, "76561197960365728") && Has(log_line, "<path>"));
}

void TestSetHash() {
    std::vector<StagedFile> rows;
    for (int i = 0; i < 6; ++i) {
        StagedFile f;
        f.entry = HandoffSlots()[static_cast<std::size_t>(i)];
        f.bytes = static_cast<std::uint64_t>(1000 * (i + 1) + i);
        f.sha256 = Sha256Hex("slot" + std::to_string(i));
        rows.push_back(f);
    }
    CHECK_EQ(rows[0].sha256, "4C4B4A1F341A258DB6343A420E19828162ACC54084240949ACA5A919C9100378");
    std::string hex, error;
    CHECK(HandoffSetSha256(rows, hex, error) && hex == "AB42AF5AF964DB2E1B9E273CF7802D6C0390078C545919B607D93CEC3CE7F2E5");
    CHECK(HandoffSetSha256({rows[5], rows[1]}, hex, error) && hex == "8FE00587663109D28A030C7E74F1472B545798D6C6A31E58BEC2F3B4C981FD1D");
    json::Value slot_rows;
    CHECK(json::Parse("[{\"slot\":5,\"size\":6005,\"sha256\":\"" + rows[5].sha256 + "\"},{\"slot\":1,\"size\":2001,\"sha256\":\"" + LowerAscii(rows[1].sha256) + "\"}]",
                      slot_rows));
    CHECK(HandoffSetSha256Json(slot_rows, hex, error) && hex == "8FE00587663109D28A030C7E74F1472B545798D6C6A31E58BEC2F3B4C981FD1D");
    StagedFile bad = rows[0];
    bad.entry = "287700/local/TPP_GAME_DATA2";
    CHECK(!HandoffSetSha256({bad}, hex, error) && Has(error, "Not a save handoff file"));
    CHECK(!HandoffSetSha256({rows[0], rows[0]}, hex, error) && Has(error, "Duplicate"));
    bad = rows[0];
    bad.bytes = 0;
    CHECK(!HandoffSetSha256({bad}, hex, error) && Has(error, "Invalid size"));
    bad.bytes = (4u << 20) + 1;
    CHECK(!HandoffSetSha256({bad}, hex, error) && Has(error, "Invalid size"));
    bad = rows[0];
    bad.sha256 = "XYZ";
    CHECK(!HandoffSetSha256({bad}, hex, error) && Has(error, "Invalid SHA-256"));
    CHECK(!HandoffSetSha256({}, hex, error) && Has(error, "No save handoff file"));
    json::Value fixture;
    CHECK(json::Parse(ReadText(Wide(DDT_SOURCE_DIR) + L"\\tests\\fixtures\\handoff-set.json"), fixture));
    const json::Value* files = fixture.Find("files");
    CHECK(files && files->size() == 6 && HandoffSetSha256Json(*files, hex, error) && hex == fixture.Str("set_sha256"));
    if (files && fixture.Find("subsets")) {
        for (const json::Value& subset : fixture.Find("subsets")->Elements()) {
            json::Value pick = json::Value::Array();
            for (const json::Value& slot : subset.Find("slots")->Elements())
                for (const json::Value& f : files->Elements())
                    if (f.Int("slot", -1) == slot.AsInt()) pick.Push(f);
            CHECK(HandoffSetSha256Json(pick, hex, error) && hex == subset.Str("set_sha256"));
        }
    }
}

void TestPathsAndDiscovery() {
    CHECK(Has(GamePathProblem(L"C:\\"), "top of a drive"));
    CHECK(!GamePathProblem(L"C:\\Games\\MGS\x00E9TPP").empty());
    CHECK(GamePathProblem(L"C:\\Games\\MGS_TPP").empty());
    CHECK(!GamePathProblem(L"C:\\" + std::wstring(200, L'a')).empty());
    const std::wstring long_ok = L"C:\\" + std::wstring(100, L'b');
    CHECK(GamePathProblem(long_ok).empty());
    CHECK(!GamePathProblem(long_ok, {"287700/local/" + std::string(120, 'x')}).empty());
    const auto libs = SteamLibraryPathsFromVdf("\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"D:\\\\SteamLibrary\"\n\t}\n\t\"1\"\t\"E:\\\\Games\\\\Steam\"\n"
                                               "\t\"path\"\t\"relative\"\n\t\"path\"\t\"d:\\\\steamlibrary\\\\\"\n}\n");
    CHECK(libs.size() == 2 && libs[0] == L"D:\\SteamLibrary" && libs[1] == L"E:\\Games\\Steam");
    CHECK_EQ(MgsvInstallDir("\"AppState\"\n{\n\t\"installdir\"\t\t\"MGS_TPP_Other\"\n}\n"), "MGS_TPP_Other");
    CHECK_EQ(MgsvInstallDir("\"installdir\" \"bad/name\""), "MGS_TPP");
    Fixture f(L"discovery");
    // The Steam root itself is a library; another library comes from libraryfolders.vdf.
    const std::wstring other = f.root + L"\\Lib2";
    WriteText(other + L"\\steamapps\\appmanifest_287700.acf", "\"AppState\"\n{\n\t\"installdir\"\t\t\"MGSV\"\n}\n");
    WriteText(other + L"\\steamapps\\common\\MGSV\\mgsvtpp.exe", "x");
    std::string vdf_path;
    for (char c : Utf8(other)) vdf_path += c == '\\' ? std::string("\\\\") : std::string(1, c);
    WriteText(f.steam + L"\\steamapps\\libraryfolders.vdf", "\"libraryfolders\"\n{\n\t\"1\"\n\t{\n\t\t\"path\"\t\t\"" + vdf_path + "\"\n\t}\n}\n");
    const auto found = FindMgsvGameFolders(*f.env);
    CHECK(found.size() == 2 && IEquals(found[0], f.game) && IEquals(found[1], other + L"\\steamapps\\common\\MGSV"));
}

// ---- Sessions ---------------------------------------------------------------------------------
ActivationRequest HostRequest(const Fixture& f, const std::string& secret) {
    ActivationRequest r;
    r.lab.role = "host";
    r.lab.save_dir = Utf8(f.paths.coop_saves);
    r.lab.steam_self = "76561197960365728";
    r.lab.save_handoff = "scene";
    r.secret = secret;
    r.character = "snake";
    r.hud = "compact";
    return r;
}

ActivationRequest GuestRequest(const Fixture& f, const std::string& secret, bool wait) {
    ActivationRequest r;
    r.lab.role = "guest";
    r.lab.save_dir = Utf8(f.paths.coop_saves);
    r.lab.steam_self = "76561197960465728";
    if (wait) r.lab.steam_wait = true;
    else r.lab.steam_peer = "76561197960365728";
    r.lab.save_handoff = "scene";
    r.secret = wait ? std::string() : secret;
    r.character = "story";
    r.hud = "full";
    return r;
}

std::vector<std::pair<std::string, std::string>> SessionRows(const InstancePaths& p) {
    std::vector<std::pair<std::string, std::string>> rows;
    json::Value s;
    if (!ReadPlaySession(p, s) || !s.Find("files")) return rows;
    for (const json::Value& f : s.Find("files")->Elements()) rows.emplace_back(f.Str("name"), f.Str("sha256"));
    return rows;
}

void TestActivationAndCleanup() {
    Fixture f(L"activate");
    CreateDirectories(f.paths.mod);
    const std::string secret = "ABCDE12345FGHJK67890";
    const std::string code = FormatJoinCode(secret, 76561197960365728ull);
    ActivationRequest req = HostRequest(f, secret);
    req.clip_sha256 = Sha256Hex(code);
    int verify_calls = 0;
    auto verify = [&](std::string&) {
        ++verify_calls;
        // The journal holds lab.ini, link-secret.txt and coop.ini, all written; the marker comes last.
        const auto rows = SessionRows(f.paths);
        CHECK(rows.size() == 3 && rows[0].first == "lab.ini" && rows[1].first == "link-secret.txt" && rows[2].first == "coop.ini");
        CHECK(IsFile(f.paths.lab_ini) && IsFile(f.paths.link_secret) && IsFile(f.paths.coop_ini) && !PathExists(f.paths.marker));
        return true;
    };
    std::string id, error;
    CHECK(ActivateSession(*f.env, f.paths, req, verify, id, error));
    CHECK(verify_calls == 1 && IsHexText(id, 32));
    const auto rows = SessionRows(f.paths);
    CHECK(rows.size() == 4 && rows[3].first == "script-observe.enabled");
    for (const auto& r : rows) CHECK_EQ(r.second, Sha256Hex(ReadText(JoinPath2(f.paths.mod, Wide(r.first)))));
    CHECK_EQ(ReadText(f.paths.marker), "offline-v1\n");
    CHECK_EQ(ReadText(f.paths.link_secret), secret + "\r\n");
    std::string lab;
    CHECK(MakeLabIniText(req.lab, lab, error) && ReadText(f.paths.lab_ini) == lab);
    CHECK(StartsWith(ReadText(f.paths.lab_ini), "# Generated by Diamond Dogs Together\nenabled=1\nmode=play\nrole=host\nsave_dir=" + Utf8(f.paths.coop_saves) + "\n"));
    CHECK_EQ(ReadText(f.paths.coop_ini), "# Generated by Diamond Dogs Together\nmaster_url=http://127.0.0.1:47899/tppstm/gate\ncharacter=snake\n"
                                         "character_fallback=dd_male\nhud=compact\n");
    json::Value s;
    CHECK(ReadPlaySession(f.paths, s) && s.Int("v") == 2 && s.Str("state") == "active" && s.Str("role") == "host" && s.Str("transport") == "steam" &&
          s.Int("launcher_pid") == 4242 && s.Find("lab")->Str("steam_self") == "76561197960365728" && s.Str("clip_sha256") == Sha256Hex(code));
    CHECK(!Has(ReadText(f.paths.play_session), secret));   // no secrets in the record
    // No cleanup while a game runs from the folder.
    f.env->AddGame(5000, f.GameExe(), f.env->NowMs());
    CleanupResult c = CleanupSession(*f.env, f.paths, true);
    CHECK(!c.ran && IsFile(f.paths.marker));
    f.env->Kill(5000);
    // A changed file stays; the clipboard holding the code is cleared; link-rejoin.txt goes.
    {
        std::string err;
        WriteAtomic(f.paths.coop_ini, std::string("character=quiet\n"), true, err);
    }
    WriteText(f.paths.link_rejoin, code + "\r\n");
    f.env->SetClipboardText(code + "\r\n");
    c = CleanupSession(*f.env, f.paths, false);
    CHECK(c.ran && !PathExists(f.paths.marker) && !PathExists(f.paths.lab_ini) && !PathExists(f.paths.link_secret) && IsFile(f.paths.coop_ini));
    CHECK(c.kept.size() == 1 && c.kept[0] == "coop.ini" && !PathExists(f.paths.link_rejoin));
    CHECK(c.removed.size() == 4 && !c.removed.empty() && c.removed[0] == "script-observe.enabled");   // the marker first
    CHECK(!f.env->clip_set);
    CHECK(ReadPlaySession(f.paths, s) && s.Str("state") == "off" && !s.Str("ended_utc").empty());
    c = CleanupSession(*f.env, f.paths, false);
    CHECK(!c.ran);   // idempotent
    DeleteFileChecked(f.paths.coop_ini);

    // A clipboard with other text is left alone; a marker with other bytes is kept.
    f.env->SetClipboardText("something else");
    CHECK(ActivateSession(*f.env, f.paths, req, {}, id, error));
    {
        std::string err;
        WriteAtomic(f.paths.marker, std::string("offline-v1\r\n"), true, err);
    }
    c = CleanupSession(*f.env, f.paths, false);
    CHECK(c.ran && f.env->clip == "something else" && IsFile(f.paths.marker) && c.kept.size() == 1);
    DeleteFileChecked(f.paths.marker);

    // A file that appears during activation stops it; cleanup removes only what was written.
    WriteText(f.paths.coop_ini, "foreign");
    CHECK(!ActivateSession(*f.env, f.paths, req, {}, id, error) && Has(error, "appeared"));
    CHECK(SessionRows(f.paths).size() == 2 && IsFile(f.paths.lab_ini) && IsFile(f.paths.link_secret));
    c = CleanupSession(*f.env, f.paths, true);
    CHECK(c.ran && !PathExists(f.paths.lab_ini) && !PathExists(f.paths.link_secret) && ReadText(f.paths.coop_ini) == "foreign");
    DeleteFileChecked(f.paths.coop_ini);

    // A failed re-check before the marker: no marker.
    CHECK(!ActivateSession(*f.env, f.paths, req, [](std::string& e) { e = "loader changed"; return false; }, id, error) && error == "loader changed");
    CHECK(!PathExists(f.paths.marker) && SessionRows(f.paths).size() == 3);
    CleanupSession(*f.env, f.paths, true);

    // A waiting guest has no link-secret.txt.
    CHECK(ActivateSession(*f.env, f.paths, GuestRequest(f, "", true), {}, id, error));
    CHECK(!PathExists(f.paths.link_secret) && Has(ReadText(f.paths.lab_ini), "net_steam_wait=1\n") && SessionRows(f.paths).size() == 3);
    CHECK(Has(ReadText(f.paths.coop_ini), "character=story\ncharacter_fallback=dd_male\nhud=full\n"));
    // A live handoff claim blocks cleanup unless forced; prev_sha256 counts as ours.
    UpdatePlaySession(*f.env, f.paths, id, [&](json::Value& t) {
        json::Value h = json::Value::Object();
        h["state"] = "relaunching";
        h["claimed_by_pid"] = 4242;
        h["claimed_start_utc"] = FormatUtcRoundTrip(f.env->self_start);
        h["claimed_utc"] = FormatUtcRoundTrip(f.env->NowMs());
        t["handoff"] = h;
        json::Value rows_json = *t.Find("files");
        for (json::Value& r : rows_json.Elements())
            if (r.Str("name") == "lab.ini") {
                r["prev_sha256"] = r.Str("sha256");
                r["sha256"] = Sha256Hex(std::string("other"));
            }
        t["files"] = rows_json;
    });
    c = CleanupSession(*f.env, f.paths, false);
    CHECK(!c.ran && !c.messages.empty() && IsFile(f.paths.marker));
    c = CleanupSession(*f.env, f.paths, true);
    CHECK(c.ran && !PathExists(f.paths.lab_ini) && !PathExists(f.paths.marker));
    CHECK(f.RealSavesUntouched());
}

// A guest session with a staged host save and request.json for game 5555.
struct HandoffScene {
    std::string id, xfer = "1A2B3C4D", set;
    std::uint32_t pid = 5555;
    std::int64_t start = 0;
};

void WriteStage(Fixture& f, const std::string& xfer, const std::vector<std::pair<std::wstring, std::string>>& files, std::string& set) {
    const std::wstring stage = f.paths.coop_saves_handoff + L"\\" + Wide(xfer);
    for (const auto& kv : files) WriteText(stage + L"\\" + kv.first, kv.second);
    const StageCheck check = CheckProgressStage(stage, nullptr);
    std::string error;
    HandoffSetSha256(check.files, set, error);
    json::Value manifest = json::Value::Object();
    manifest["format"] = "mgs-coop-save-handoff";
    manifest["v"] = 1;
    manifest["xfer"] = xfer;
    manifest["created_utc"] = FormatUtcSeconds(f.env->NowMs());
    manifest["host_mission"] = 30010;
    manifest["set_sha256"] = set;
    json::Value rows = json::Value::Array();
    for (const StagedFile& sf : check.files) {
        json::Value r = json::Value::Object();
        r["entry"] = sf.entry;
        r["bytes"] = static_cast<std::int64_t>(sf.bytes);
        r["sha256"] = sf.sha256;
        rows.Push(r);
    }
    manifest["files"] = rows;
    WriteText(stage + L"\\handoff.json", json::Dump(manifest));
}

void WriteRequest(Fixture& f, const std::string& xfer, const std::string& set, std::uint32_t pid, std::int64_t start, std::int64_t created, int rejoin) {
    json::Value req = json::Value::Object();
    req["format"] = "mgs-coop-save-handoff-request";
    req["v"] = 1;
    req["xfer"] = xfer;
    req["set_sha256"] = set;
    req["mode"] = "scene";
    req["rejoin"] = rejoin;
    req["pid"] = static_cast<std::int64_t>(pid);
    req["process_start_utc"] = FormatUtcRoundTrip(start);
    req["created_utc"] = FormatUtcSeconds(created);
    WriteText(f.paths.coop_saves_handoff + L"\\request.json", json::Dump(req));
}

HandoffScene PrepareHandoff(Fixture& f, bool wait, std::int64_t created_offset_ms = -10000, std::uint32_t request_pid = 5555) {
    HandoffScene h;
    CreateDirectories(f.paths.mod);
    WriteText(f.paths.coop_saves + L"\\287700\\local\\TPP_GAME_DATA0", "guest-0");
    WriteText(f.paths.coop_saves + L"\\287700\\local\\TPP_GRAPHICS_CONFIG", "guest-settings");
    WriteText(f.paths.coop_saves + L"\\remote\\TPP_GAME_DATA", "guest-remote");
    std::string error;
    ActivateSession(*f.env, f.paths, GuestRequest(f, "ABCDE12345FGHJK67890", wait), {}, h.id, error);
    f.env->Advance(60000);   // the game ran for a minute
    h.start = f.env->NowMs() - 50000;
    WriteStage(f, h.xfer,
               {{L"287700\\local\\TPP_GAME_DATA0", "host-0"}, {L"287700\\local\\PERSONAL_DATA0", "host-personal"}, {L"remote\\TPP_GAME_DATA", "host-remote"}},
               h.set);
    WriteRequest(f, h.xfer, h.set, request_pid, h.start, f.env->NowMs() + created_offset_ms, wait ? 1 : 0);
    return h;
}

void TestHandoff() {
    {
        Fixture f(L"handoff");
        HandoffScene h = PrepareHandoff(f, false);
        // Steam marks MGSV as running for a moment after the exit.
        f.env->SetDword(L"Software\\Valve\\Steam\\Apps\\287700", L"Running", 1);
        int sleeps = 0;
        f.env->on_sleep = [&]() {
            if (++sleeps == 4) f.env->SetDword(L"Software\\Valve\\Steam\\Apps\\287700", L"Running", 0);
        };
        int applied_calls = 0;
        const HandoffOutcome o = HandoffAfterExit(*f.env, f.paths, h.pid, h.start, false, false, 0, [&](const HandoffOutcome& a) {
            ++applied_calls;
            CHECK(a.files == 3);
        }, nullptr);
        f.env->on_sleep = nullptr;
        CHECK_EQ(o.action, "relaunched");
        CHECK(applied_calls == 1 && f.env->UrlCount() == 1 && f.env->urls[0] == L"steam://rungameid/287700");
        CHECK(sleeps > 4);   // it waited for Steam's flag
        CHECK_EQ(ReadText(f.paths.coop_saves + L"\\287700\\local\\TPP_GAME_DATA0"), "host-0");
        CHECK_EQ(ReadText(f.paths.coop_saves + L"\\287700\\local\\PERSONAL_DATA0"), "host-personal");
        CHECK_EQ(ReadText(f.paths.coop_saves + L"\\287700\\local\\TPP_GRAPHICS_CONFIG"), "guest-settings");   // settings kept
        json::Value marker;
        CHECK(json::Parse(ReadText(f.paths.coop_saves + L"\\imported-progress.json"), marker) && marker.Str("source") == "handoff");
        const auto backups = ProgressBackups(f.paths.coop_saves_replaced);
        CHECK(backups.size() == 1 && backups[0].kind == "handoff" && ReadText(backups[0].path + L"\\287700\\local\\TPP_GAME_DATA0") == "guest-0");
        CHECK(IsFile(f.paths.coop_saves_handoff + L"\\applied.json") && !PathExists(f.paths.coop_saves_handoff + L"\\request.json") &&
              !PathExists(f.paths.coop_saves_handoff + L"\\" + Wide(h.xfer)));
        const std::string lab = ReadText(f.paths.lab_ini);
        CHECK(Has(lab, "save_handoff=off\n") && !Has(lab, "save_handoff=scene"));
        json::Value s;
        CHECK(ReadPlaySession(f.paths, s));
        const json::Value hh = HandoffOf(s);
        CHECK(hh.Str("state") == "relaunching" && hh.Int("count") == 1 && hh.Str("xfer") == h.xfer && !hh.Str("aside").empty());
        bool prev = false;
        for (const json::Value& r : s.Find("files")->Elements())
            if (r.Str("name") == "lab.ini") prev = !r.Str("prev_sha256").empty() && r.Str("sha256") == Sha256Hex(lab);
        CHECK(prev && s.Find("lab")->Str("save_handoff") == "off");
        // While the restart is claimed, cleanup waits; the restarted game marks it relaunched.
        CHECK(!CleanupSession(*f.env, f.paths, false).ran);
        bool relaunched = false;
        CHECK(SetSessionGame(*f.env, f.paths, h.id, 6000, f.env->NowMs(), &relaunched) && relaunched);
        // A second request in the same session is not applied (limit).
        std::string set2;
        WriteStage(f, h.xfer, {{L"287700\\local\\TPP_GAME_DATA0", "host-0b"}}, set2);
        WriteRequest(f, h.xfer, set2, 6000, f.env->NowMs(), f.env->NowMs(), 0);
        const HandoffOutcome limit = HandoffAfterExit(*f.env, f.paths, 6000, f.env->NowMs(), false, false, 0, {}, nullptr);
        CHECK_EQ(limit.action, "limit");
        CHECK(!PathExists(f.paths.coop_saves_handoff + L"\\request.json") && ReadText(f.paths.coop_saves + L"\\287700\\local\\TPP_GAME_DATA0") == "host-0");
        CHECK(CleanupSession(*f.env, f.paths, false).ran && !PathExists(f.paths.lab_ini));
        CHECK(f.RealSavesUntouched());
    }
    {
        // A waiting guest rejoins the host from link-rejoin.txt: net_peer_steam and a new link-secret.txt.
        Fixture f(L"handoff-rejoin");
        HandoffScene h = PrepareHandoff(f, true);
        const std::string rejoin_code = FormatJoinCode("C00PC00PC00PC00PC00P", 76561197960365728ull);
        WriteText(f.paths.link_rejoin, rejoin_code + "\r\n");
        const HandoffOutcome o = HandoffAfterExit(*f.env, f.paths, h.pid, h.start, false, false, 0, {}, nullptr);
        CHECK_EQ(o.action, "relaunched");
        const std::string lab = ReadText(f.paths.lab_ini);
        CHECK(Has(lab, "net_peer_steam=76561197960365728\n") && !Has(lab, "net_steam_wait") && Has(lab, "save_handoff=off\n"));
        CHECK_EQ(ReadText(f.paths.link_secret), std::string("C00PC00PC00PC00PC00P\r\n"));
        CHECK(!PathExists(f.paths.link_rejoin));
        json::Value s;
        CHECK(ReadPlaySession(f.paths, s) && s.Find("lab")->Str("steam_peer") == "76561197960365728" && !s.Find("lab")->Bool("steam_wait"));
        CHECK(!SessionRows(f.paths).empty() && SessionRows(f.paths).back().first == "link-secret.txt");
        UpdatePlaySession(*f.env, f.paths, h.id, [&](json::Value& t) { t["handoff"] = json::Value::Object(); });
        const CleanupResult c = CleanupSession(*f.env, f.paths, false);
        CHECK(c.ran && !PathExists(f.paths.link_secret) && !PathExists(f.paths.lab_ini));
    }
    {
        // Requests for another game start: stale, removed, nothing applied.
        Fixture f(L"handoff-stale");
        HandoffScene h = PrepareHandoff(f, false, -10000, 7777);
        WriteText(f.paths.link_rejoin, "x");
        HandoffOutcome o = HandoffAfterExit(*f.env, f.paths, h.pid, h.start, false, false, 0, {}, nullptr);
        CHECK(o.action == "stale" && !o.messages.empty() && Has(o.messages[0], "another game start"));
        CHECK(!PathExists(f.paths.coop_saves_handoff + L"\\request.json") && !PathExists(f.paths.coop_saves_handoff + L"\\" + Wide(h.xfer)) &&
              !PathExists(f.paths.link_rejoin));
        CHECK_EQ(ReadText(f.paths.coop_saves + L"\\287700\\local\\TPP_GAME_DATA0"), "guest-0");
        CHECK(f.env->UrlCount() == 0);
        CleanupSession(*f.env, f.paths, true);
    }
    {
        Fixture f(L"handoff-old");
        HandoffScene h = PrepareHandoff(f, false, -31 * 60000);
        const HandoffRequest r = GetHandoffRequest(f.paths.coop_saves_handoff, h.pid, h.start, 0, f.env->NowMs());
        CHECK(r.status == "invalid" && r.problem == "the request is too old");
        CHECK(GetHandoffRequest(f.paths.coop_saves_handoff, h.pid, h.start + 5000, 0, f.env->NowMs()).problem == "the request belongs to another game start");
        CHECK(GetHandoffRequest(f.paths.coop_saves_handoff, h.pid, h.start, 0, f.env->NowMs() - 31 * 60000).status == "ok");
        // A tampered staged file fails the manifest check; so does an extra file.
        const std::wstring stage = f.paths.coop_saves_handoff + L"\\" + Wide(h.xfer);
        WriteText(stage + L"\\287700\\local\\TPP_GAME_DATA0", "host-X");   // same size, other bytes
        CHECK(Has(GetHandoffRequest(f.paths.coop_saves_handoff, h.pid, h.start, 0, f.env->NowMs() - 31 * 60000).problem, "damaged"));
        WriteText(stage + L"\\287700\\local\\TPP_GAME_DATA0", "host-0");
        WriteText(stage + L"\\notes.txt", "x");
        CHECK(Has(GetHandoffRequest(f.paths.coop_saves_handoff, h.pid, h.start, 0, f.env->NowMs() - 31 * 60000).problem, "Unexpected file"));
        CleanupSession(*f.env, f.paths, true);
        // Stale leftovers of an earlier session go at the next start (known names only).
        const StaleResult stale = RemoveStaleHandoff(*f.env, f.paths);
        CHECK(!stale.removed.empty() && !PathExists(f.paths.coop_saves_handoff + L"\\request.json") && !stale.messages.empty());
        CHECK(IsFile(stage + L"\\notes.txt"));   // an unknown file stays
    }
    {
        // Exactly one owner: another live claimant wins; the helper defers to a live window for 20 s.
        Fixture f(L"handoff-owner");
        HandoffScene h = PrepareHandoff(f, false);
        f.env->AddProcess(777, f.env->NowMs() - 1000);
        UpdatePlaySession(*f.env, f.paths, h.id, [&](json::Value& t) {
            json::Value c = json::Value::Object();
            c["state"] = "applying";
            c["claimed_by_pid"] = 777;
            c["claimed_start_utc"] = FormatUtcRoundTrip(f.env->NowMs() - 1000);
            c["claimed_utc"] = FormatUtcRoundTrip(f.env->NowMs());
            t["handoff"] = c;
        });
        CHECK_EQ(HandoffAfterExit(*f.env, f.paths, h.pid, h.start, false, false, 0, {}, nullptr).action, "claimed_by_other");
        UpdatePlaySession(*f.env, f.paths, h.id, [&](json::Value& t) { t["handoff"] = json::Value::Object(); });
        CHECK_EQ(HandoffAfterExit(*f.env, f.paths, h.pid, h.start, true, true, f.env->NowMs(), {}, nullptr).action, "deferred");
        CHECK(IsFile(f.paths.coop_saves_handoff + L"\\request.json"));
        // The game still running from the folder: the apply waits (at most 30 s), then defers.
        f.env->AddGame(8000, f.GameExe(), f.env->NowMs());
        CHECK_EQ(HandoffAfterExit(*f.env, f.paths, h.pid, h.start, false, false, 0, {}, nullptr).action, "deferred");
        f.env->Kill(8000);
        CHECK_EQ(HandoffAfterExit(*f.env, f.paths, h.pid, h.start, false, false, 0, {}, nullptr).action, "relaunched");
        // A relaunch is refused when a recorded file changed.
        {
            std::string err;
            WriteAtomic(f.paths.coop_ini, std::string("changed"), true, err);
        }
        const RelaunchResult r = StartRelaunch(*f.env, f.paths, false);
        CHECK(!r.ok && Has(r.problem, "coop.ini changed"));
        CleanupSession(*f.env, f.paths, true);
    }
}

// ---- Share save --------------------------------------------------------------------------------
void TestShareSave() {
    Fixture a(L"share-a"), b(L"share-b");
    WriteText(a.paths.coop_saves + L"\\287700\\local\\TPP_GAME_DATA0", "a-game-0");
    WriteText(a.paths.coop_saves + L"\\287700\\local\\PERSONAL_DATA0", "a-personal-0");
    WriteText(a.paths.coop_saves + L"\\287700\\local\\TPP_GRAPHICS_CONFIG", "a-settings");
    WriteText(a.paths.coop_saves + L"\\remote\\TPP_GAME_DATA", "a-remote");
    std::wstring zip;
    std::size_t files = 0;
    std::string error;
    CHECK(ExportShareSave(*a.env, a.paths.coop_saves, a.desktop, zip, files, error) && files == 3);
    CHECK(StartsWith(Utf8(LeafOf(zip)), "mgs-coop-save-") && EndsWith(Utf8(zip), ".zip"));
    ZipReader reader;
    ZipLimits limits;
    CHECK(reader.OpenFile(zip, limits, error) && reader.entries().size() == 4 && reader.Find("share-save.json") &&
          reader.Find("287700/local/TPP_GAME_DATA0") && !reader.Find("287700/local/TPP_GRAPHICS_CONFIG"));
    WriteText(b.paths.coop_saves + L"\\287700\\local\\TPP_GAME_DATA0", "b-game-0");
    WriteText(b.paths.coop_saves + L"\\287700\\local\\TPP_GAME_DATA1", "b-game-1");
    WriteText(b.paths.coop_saves + L"\\287700\\local\\TPP_GRAPHICS_CONFIG", "b-settings");
    std::wstring aside;
    CHECK(ImportShareSave(*b.env, zip, b.paths.mod, b.paths.coop_saves, b.paths.coop_saves_replaced, aside, files, error));
    CHECK_EQ(ReadText(b.paths.coop_saves + L"\\287700\\local\\TPP_GAME_DATA0"), "a-game-0");
    CHECK(!PathExists(b.paths.coop_saves + L"\\287700\\local\\TPP_GAME_DATA1"));   // never two players' progress
    CHECK_EQ(ReadText(b.paths.coop_saves + L"\\287700\\local\\TPP_GRAPHICS_CONFIG"), "b-settings");
    CHECK(EndsWith(Utf8(aside), "-before-import") && ReadText(aside + L"\\287700\\local\\TPP_GAME_DATA1") == "b-game-1");
    const ImportedInfo info = GetImportedSaveInfo(b.paths.coop_saves, b.paths.coop_saves_replaced);
    CHECK(info.present && info.kind == "share-save" && info.source == "marker");
    for (const DirEntry& e : ListDirectory(b.paths.mod)) CHECK(!StartsWith(Utf8(e.name), "save-import-") && !StartsWith(Utf8(e.name), "coop-saves-new-"));
    // Restore brings the previous copy back; the current one goes to -before-restore.
    b.env->Advance(2000);
    std::wstring from, restore_aside;
    CHECK(RestoreShareSave(*b.env, b.paths.coop_saves, b.paths.coop_saves_replaced, from, restore_aside, error));
    CHECK_EQ(ReadText(b.paths.coop_saves + L"\\287700\\local\\TPP_GAME_DATA1"), "b-game-1");
    CHECK(EndsWith(Utf8(restore_aside), "-before-restore") && ReadText(restore_aside + L"\\287700\\local\\TPP_GAME_DATA0") == "a-game-0");
    CHECK(!RestoreShareSave(*b.env, b.paths.coop_saves, b.paths.coop_saves_replaced, from, restore_aside, error) && Has(error, "no earlier"));
    // Refusals (nothing changes).
    const std::string before = ReadText(b.paths.coop_saves + L"\\287700\\local\\TPP_GAME_DATA0");
    auto refuse = [&](const std::wstring& path, const char* why) {
        std::string e;
        std::size_t n = 0;
        std::wstring as;
        const bool ok = ImportShareSave(*b.env, path, b.paths.mod, b.paths.coop_saves, b.paths.coop_saves_replaced, as, n, e);
        CheckEq(ok ? "imported" : (Has(e, why) ? why : e), why, "share save refusal", __LINE__);
        CHECK(ReadText(b.paths.coop_saves + L"\\287700\\local\\TPP_GAME_DATA0") == before);
    };
    WriteText(b.desktop + L"\\not-a-zip.zip", "hello");
    refuse(b.desktop + L"\\not-a-zip.zip", "not a co-op save zip");
    auto make_zip = [&](const std::wstring& path, const std::vector<std::pair<std::string, std::string>>& entries) {
        ZipWriter w;
        std::string e;
        for (const auto& kv : entries) w.AddFile(kv.first, kv.second.data(), kv.second.size(), e);
        Bytes out;
        w.Finish(out, e);
        std::string werr;
        if (PathExists(path)) DeleteFileChecked(path);
        WriteAtomic(path, out.data(), out.size(), false, werr);
    };
    const std::string game = "x-game";
    auto manifest_for = [&](const std::string& entry, const std::string& data) {
        return "{\"format\":\"mgs-coop-save-share\",\"v\":1,\"exported_utc\":\"2026-10-23T10:00:00Z\",\"files\":[{\"entry\":\"" + entry +
               "\",\"bytes\":" + std::to_string(data.size()) + ",\"sha256\":\"" + Sha256Hex(data) + "\"}]}";
    };
    make_zip(b.desktop + L"\\no-manifest.zip", {{"287700/local/TPP_GAME_DATA0", game}});
    refuse(b.desktop + L"\\no-manifest.zip", "share-save.json is missing");
    make_zip(b.desktop + L"\\tampered.zip", {{"287700/local/TPP_GAME_DATA0", game + "!"}, {"share-save.json", manifest_for("287700/local/TPP_GAME_DATA0", game)}});
    refuse(b.desktop + L"\\tampered.zip", "damaged");
    make_zip(b.desktop + L"\\settings.zip", {{"287700/local/TPP_GRAPHICS_CONFIG", game}, {"share-save.json", manifest_for("287700/local/TPP_GRAPHICS_CONFIG", game)}});
    refuse(b.desktop + L"\\settings.zip", "Unexpected file");
    make_zip(b.desktop + L"\\nogame.zip", {{"287700/local/PERSONAL_DATA0", game}, {"share-save.json", manifest_for("287700/local/PERSONAL_DATA0", game)}});
    refuse(b.desktop + L"\\nogame.zip", "no TPP_GAME_DATA");
    make_zip(b.desktop + L"\\otherformat.zip", {{"287700/local/TPP_GAME_DATA0", game}, {"share-save.json", "{\"format\":\"zip\"}"}});
    refuse(b.desktop + L"\\otherformat.zip", "not a Share save export");
    refuse(b.desktop + L"\\missing.zip", "not found");
    // A good third-party style export (one file) imports.
    make_zip(b.desktop + L"\\good.zip", {{"287700/local/TPP_GAME_DATA0", game}, {"share-save.json", manifest_for("287700/local/TPP_GAME_DATA0", game)}});
    CHECK(ImportShareSave(*b.env, b.desktop + L"\\good.zip", b.paths.mod, b.paths.coop_saves, b.paths.coop_saves_replaced, aside, files, error) && files == 1);
    // Export refuses a copy without progress.
    Fixture c(L"share-c");
    WriteText(c.paths.coop_saves + L"\\287700\\local\\TPP_GRAPHICS_CONFIG", "settings");
    CHECK(!ExportShareSave(*c.env, c.paths.coop_saves, c.desktop, zip, files, error) && Has(error, "no game progress"));
    CHECK(a.RealSavesUntouched() && b.RealSavesUntouched());
}

// ---- Setup, uninstall, logs ----------------------------------------------------------------------
std::vector<StepReport> RunSetupCollect(Fixture& f, const std::string& character, bool& ok, std::string& error) {
    std::vector<StepReport> reports;
    ok = RunSetup(*f.env, f.paths, character, "full", [&](const StepReport& r) { reports.push_back(r); }, nullptr, error);
    return reports;
}

void TestSetup() {
    {
        Fixture f(L"setup");
        // Leftover files that must be set aside (pass-through), and an old co-op copy from an earlier test.
        WriteText(f.paths.lab_ini, "enabled=1\nmode=lab\n");
        WriteText(f.paths.marker, "host-v3\n");
        WriteText(f.paths.coop_saves + L"\\287700\\local\\TPP_GAME_DATA0", "old-test-copy");
        bool ok = false;
        std::string error;
        const auto reports = RunSetupCollect(f, "quiet", ok, error);
        CHECK(ok);
        if (!ok) std::fprintf(stderr, "setup error: %s\n", error.c_str());
        CHECK(!reports.empty() && reports.back().index == 3 && reports.back().state == StepState::done);
        for (int i = 0; i < 4; ++i)
            CHECK(std::any_of(reports.begin(), reports.end(), [&](const StepReport& r) { return r.index == i && r.state == StepState::done; }));
        json::Value state;
        CHECK(ReadInstallState(f.paths, state) && state.Str("status") == "installed" && state.Str("version") == CoreVersion() &&
              state.Str("character") == "story" && state.Str("game_directory") == Utf8(f.game) && state.Str("save_dir") == Utf8(f.paths.coop_saves));
        CHECK(state.Find("backups") && state.Find("backups")->size() == 1 && state.Str("first_backup") == state.Find("backups")->Elements()[0].StringRef() &&
              state.Find("installs")->size() == 1 && state.Find("previous_files")->size() == 5 && state.Find("rollback_ids")->is_array());
        // The backup with its manifest (a read-only copy of userdata).
        const std::wstring backup = Wide(state.Str("first_backup"));
        json::Value manifest;
        CHECK(json::Parse(ReadText(backup + L"\\manifest.json"), manifest) && manifest.Str("product") == "mgs-coop-lab-save-backup" &&
              manifest.Find("verified")->size() == 4 && manifest.Find("all_files")->size() == 5);
        CHECK_EQ(ReadText(backup + L"\\files\\287700\\local\\TPP_GAME_DATA0"), "real-game-0");
        CHECK(CheckRealSavesAgainstBackup(backup).ok && CheckRealSavesAgainstBackup(backup).comparison.same);
        // The co-op copy: a fresh seed, the old test copy moved aside.
        CHECK_EQ(ReadText(f.paths.coop_saves + L"\\287700\\local\\TPP_GAME_DATA0"), "real-game-0");
        CHECK_EQ(ReadText(f.paths.coop_saves + L"\\remote\\TPP_GAME_DATA"), "real-remote-game");
        bool archived = false;
        for (const DirEntry& d : ListDirectory(f.paths.coop_saves_replaced)) {
            const std::wstring dir = JoinPath2(f.paths.coop_saves_replaced, d.name);
            for (const DirEntry& e : ListDirectory(dir))
                if (StartsWith(Utf8(e.name), "local-") && ReadText(JoinPath2(JoinPath2(dir, e.name), L"TPP_GAME_DATA0")) == "old-test-copy") archived = true;
        }
        CHECK(archived);
        // Pass-through: the old files are set aside (the old online marker for good).
        CHECK(!PathExists(f.paths.lab_ini) && !PathExists(f.paths.marker));
        CHECK(state.Find("set_aside")->size() == 1 && state.Find("online_archived")->size() == 1);
        // The runtime file was built into the mgs-coop folder; the settings remember the HUD.
        CHECK_EQ(ReadText(f.paths.archive), "FAKE-RUNTIME");
        Settings settings;
        CHECK(LoadSettings(f.paths, settings) && settings.character == "story" && settings.hud == "full");
        // A second run is idempotent: the copy is kept, a new backup is added.
        WriteText(f.paths.coop_saves + L"\\287700\\local\\TPP_GAME_DATA0", "coop-progress");
        CHECK(RunSetup(*f.env, f.paths, "ocelot", "", {}, nullptr, error));
        CHECK_EQ(ReadText(f.paths.coop_saves + L"\\287700\\local\\TPP_GAME_DATA0"), "coop-progress");
        CHECK(ReadInstallState(f.paths, state) && state.Find("backups")->size() == 2 && state.Find("installs")->size() == 2 &&
              state.Str("character") == "ocelot");
        // An interrupted setup (pending record, no runtime file, a stray temp file) finishes on the next run.
        state["status"] = "pending";
        std::string werr;
        WriteAtomic(f.paths.friend_install, json::Dump(state), true, werr);
        DeleteFileChecked(f.paths.archive);
        WriteText(f.paths.mod + L"\\friend-install.json.0123456789abcdef0123456789abcdef.tmp", "stale");
        CHECK(RunSetup(*f.env, f.paths, "ocelot", "", {}, nullptr, error));
        CHECK(ReadInstallState(f.paths, state) && state.Str("status") == "installed" && ReadText(f.paths.archive) == "FAKE-RUNTIME");
        CHECK(!PathExists(f.paths.mod + L"\\friend-install.json.0123456789abcdef0123456789abcdef.tmp"));
        CHECK_EQ(ReadText(f.paths.coop_saves + L"\\287700\\local\\TPP_GAME_DATA0"), "coop-progress");
        CHECK(f.RealSavesUntouched());
    }
    {
        // Adoption of a 0.13.x install: its backups and co-op copy are kept, quiet becomes story, unknown fields stay.
        Fixture f(L"adopt");
        CreateDirectories(f.paths.mod);
        WriteText(f.paths.coop_saves + L"\\287700\\local\\TPP_GAME_DATA0", "coop-0.13");
        std::string root_json;
        for (char c : Utf8(f.userdata)) root_json += c == '\\' ? std::string("\\\\") : std::string(1, c);
        WriteText(f.paths.backups_root + L"\\20261001-100000\\manifest.json",
                  "{\"product\":\"mgs-coop-lab-save-backup\",\"source_root\":\"" + root_json + "\",\"verified\":[]}");
        json::Value old = json::Value::Object();
        old["product"] = "mgs-coop-friend-install";
        old["version"] = "0.13.6";
        old["game_directory"] = Utf8(f.game);
        old["role_hint"] = "Host";
        old["character"] = "quiet";
        old["status"] = "installed";
        old["record_before_friend"] = "5ec0519faff640379b7160bf8824f6dd";
        json::Value b = json::Value::Array();
        b.Push(Utf8(f.paths.backups_root + L"\\20261001-100000"));
        old["backups"] = b;
        old["first_backup"] = Utf8(f.paths.backups_root + L"\\20261001-100000");
        json::Value ids = json::Value::Array();
        ids.Push("0902b11abe9549b784b5edbf83f06e85");
        old["rollback_ids"] = ids;
        json::Value inst = json::Value::Array();
        json::Value row = json::Value::Object();
        row["utc"] = "2026-10-01T10:00:00Z";
        row["character"] = "quiet";
        inst.Push(row);
        old["installs"] = inst;
        json::Value rule = json::Value::Object();
        rule["display_name"] = "mgs-coop play";
        old["firewall_rule"] = rule;
        old["custom_field"] = "kept";
        WriteText(f.paths.friend_install, json::Dump(old));
        WriteText(f.paths.archive, "FAKE-RUNTIME");
        CHECK(!InstallIsCurrent(old));
        auto model = MakeRealModelWithEnv(f.game, f.env);
        CHECK(model->phase() == Phase::needs_setup && model->character() == Character::story);
        bool ok = false;
        std::string error;
        RunSetupCollect(f, CharacterName(model->character()), ok, error);
        CHECK(ok);
        json::Value state;
        CHECK(ReadInstallState(f.paths, state) && InstallIsCurrent(state) && state.Str("custom_field") == "kept" && state.Str("role_hint") == "Host" &&
              state.Str("record_before_friend") == "5ec0519faff640379b7160bf8824f6dd" && state.Find("rollback_ids")->size() == 1 &&
              state.Find("backups")->size() == 2 && state.Str("first_backup") == Utf8(f.paths.backups_root + L"\\20261001-100000") &&
              state.Find("installs")->size() == 2 && state.Find("firewall_rule") && state.Str("character") == "story");
        CHECK_EQ(ReadText(f.paths.coop_saves + L"\\287700\\local\\TPP_GAME_DATA0"), "coop-0.13");
        CHECK(f.RealSavesUntouched());
    }
    {
        // The reseed guard: a first setup never replaces a copy that holds a partner's progress.
        Fixture f(L"reseed-guard");
        WriteText(f.paths.coop_saves + L"\\287700\\local\\TPP_GAME_DATA0", "partner-progress");
        WriteText(f.paths.coop_saves + L"\\imported-progress.json", "{\"format\":\"mgs-coop-imported-progress\",\"v\":1,\"source\":\"handoff\"}");
        bool ok = false;
        std::string error;
        const auto reports = RunSetupCollect(f, "avatar", ok, error);
        CHECK(ok && ReadText(f.paths.coop_saves + L"\\287700\\local\\TPP_GAME_DATA0") == "partner-progress");
        CHECK(std::any_of(reports.begin(), reports.end(), [](const StepReport& r) { return r.index == 2 && Has(r.detail, "partner"); }));
    }
    {
        // Refusals: a wrong game, a foreign loader, another version's package, a running game, no Steam.
        Fixture f(L"setup-refuse");
        bool ok = true;
        std::string error;
        WriteText(f.paths.exe, "OTHER-EXE");
        RunSetupCollect(f, "story", ok, error);
        CHECK(!ok && Has(error, "mgsvtpp.exe"));
        WriteText(f.paths.exe, "FAKE-EXE");
        WriteText(f.paths.loader, "OTHER-LOADER");
        RunSetupCollect(f, "story", ok, error);
        CHECK(!ok && Has(error, "dinput8.dll"));
        WriteText(f.paths.loader, "FAKE-LOADER");
        f.WritePackage("0.0.1");
        RunSetupCollect(f, "story", ok, error);
        CHECK(!ok && Has(error, "different Diamond Dogs Together versions"));
        f.WritePackage(DDT_CORE_VERSION);
        f.env->AddGame(5100, f.GameExe(), f.env->NowMs());
        RunSetupCollect(f, "story", ok, error);
        CHECK(!ok && Has(error, "running"));
        f.env->Kill(5100);
        f.env->dwords.erase(FakeEnv::Key(false, L"Software\\Valve\\Steam\\ActiveProcess", L"ActiveUser"));
        f.env->strings.clear();
        RunSetupCollect(f, "story", ok, error);
        CHECK(!ok && Has(error, "Steam was not found"));
        CHECK(!PathExists(f.paths.friend_install));
    }
}

void TestUninstallAndLogs() {
    Fixture f(L"uninstall");
    bool ok = false;
    std::string error;
    RunSetupCollect(f, "dd_female", ok, error);
    CHECK(ok);
    // Collect logs: an allow-listed log with a code and a SteamID is redacted; the secret never goes in.
    const std::string code = FormatJoinCode("ABCDE12345FGHJK67890", 76561197960365728ull);
    WriteText(f.paths.mod + L"\\script-observer.log", "old\nMGS Co-op script adapter start\nlink=config; code " + code + " id 76561197960365728\n");
    WriteText(f.paths.link_secret, "ABCDE12345FGHJK67890\r\n");
    LauncherLog(*f.env, f.paths, "test line " + code + " at C:\\Users\\Jane\\x");
    std::wstring zip;
    CHECK(CollectLogs(*f.env, f.paths, f.desktop, zip, error));
    ZipReader reader;
    ZipLimits limits;
    CHECK(reader.OpenFile(zip, limits, error));
    bool summary = false, observer = false, secret = false, launcher_log = false;
    for (const ZipEntry& e : reader.entries()) {
        Bytes data;
        reader.Extract(e, data, error);
        const std::string text(data.begin(), data.end());
        CHECK(!Has(text, "ABCDE") && !Has(text, "76561197960365728") && !Has(text, "Jane"));
        if (EndsWith(e.name, "/summary.txt")) summary = Has(text, "real_saves=UNCHANGED") && Has(text, "link_secret_present=True");
        if (EndsWith(e.name, "/script-observer.log")) observer = Has(text, "MGS2-<redacted>");
        if (EndsWith(e.name, "/launcher.log")) launcher_log = Has(text, "test line");
        if (Has(e.name, "link-secret")) secret = true;
    }
    CHECK(summary && observer && launcher_log && !secret);
    CHECK(StartsWith(Utf8(LeafOf(zip)), "mgs-coop-logs-join-"));
    DeleteFileChecked(f.paths.link_secret);
    // launcher.log: redacted, rotated at 1 MB.
    const std::string log = ReadText(f.paths.launcher_log);
    CHECK(Has(log, "test line") && !Has(log, "ABCDE") && !Has(log, "Jane") && Has(log, "pid=4242"));
    {
        std::string big(1100000, 'x');
        std::string werr;
        WriteAtomic(f.paths.launcher_log, big, true, werr);
    }
    LauncherLog(*f.env, f.paths, "after rotation");
    CHECK(FileSizeOrZero(f.paths.launcher_old_log) >= 1100000 && FileSizeOrZero(f.paths.launcher_log) < 1000);
    // Uninstall: refused while the game runs; then removes our loader, sets the configuration aside, keeps saves.
    f.env->AddGame(5200, f.GameExe(), f.env->NowMs());
    std::string report;
    CHECK(!Uninstall(*f.env, f.paths, report, error) && Has(error, "running"));
    f.env->Kill(5200);
    WriteText(f.paths.coop_ini, "foreign coop.ini");
    CHECK(Uninstall(*f.env, f.paths, report, error));
    CHECK(!PathExists(f.paths.loader) && !PathExists(f.paths.coop_ini) && !PathExists(f.paths.friend_install));
    CHECK(Has(report, "removed") && Has(report, "match the backup") && Has(report, "delete DiamondDogsTogether.exe and the mgs-coop folder"));
    CHECK(IsDir(f.paths.coop_saves) && IsDir(f.paths.backups_root));
    bool renamed = false, uninstalled = false;
    for (const DirEntry& e : ListDirectory(f.paths.mod)) {
        if (StartsWith(Utf8(e.name), "coop.ini.disabled-")) renamed = true;
        if (StartsWith(Utf8(e.name), "friend-install.json.uninstalled-")) uninstalled = true;
    }
    CHECK(renamed && uninstalled);
    // A loader that is not this package's stays.
    Fixture g(L"uninstall-foreign");
    WriteText(g.paths.loader, "SOMEONE-ELSES-LOADER");
    CHECK(Uninstall(*g.env, g.paths, report, error) && IsFile(g.paths.loader) && Has(report, "left alone"));
    CHECK(f.RealSavesUntouched());
}

// ---- The helper and the model ------------------------------------------------------------------------
void TestWatchdog() {
    {
        Fixture f(L"watch");
        CreateDirectories(f.paths.mod);
        std::string id, error;
        CHECK(ActivateSession(*f.env, f.paths, HostRequest(f, "ABCDE12345FGHJK67890"), {}, id, error));
        // The window (launcher pid 4242) is gone; this process is the helper, pid 9100.
        f.env->self = 9100;
        f.env->self_start = f.env->NowMs();
        f.env->AddGame(5300, f.GameExe(), f.env->NowMs());
        int sleeps = 0;
        f.env->on_sleep = [&]() {
            if (++sleeps == 5) f.env->Kill(5300);
        };
        CHECK(RunWatchdog(*f.env, f.game, id) == 0);
        f.env->on_sleep = nullptr;
        json::Value s;
        CHECK(ReadPlaySession(f.paths, s) && s.Str("state") == "off" && s.Int("game_pid") == 5300 && s.Int("watchdog_pid") == 9100);
        CHECK(!PathExists(f.paths.lab_ini) && !PathExists(f.paths.marker));
        CHECK(Has(ReadText(f.paths.launcher_log), "watch=game_exited"));
    }
    {
        // Never two helpers: a live recorded helper makes the second one exit at once.
        Fixture f(L"watch-two");
        CreateDirectories(f.paths.mod);
        std::string id, error;
        CHECK(ActivateSession(*f.env, f.paths, HostRequest(f, "ABCDE12345FGHJK67890"), {}, id, error));
        f.env->AddProcess(9200, f.env->NowMs());
        UpdatePlaySession(*f.env, f.paths, id, [&](json::Value& t) {
            t["watchdog_pid"] = 9200;
            t["watchdog_start_utc"] = FormatUtcRoundTrip(f.env->NowMs());
        });
        f.env->self = 9300;
        CHECK(RunWatchdog(*f.env, f.game, id) == 0);
        CHECK(IsFile(f.paths.marker));   // untouched: the other helper owns the session
        // The window closed before the game started: after 3 s with no game, co-op is switched off.
        f.env->Kill(9200);
        CHECK(RunWatchdog(*f.env, f.game, id) == 0);
        CHECK(!PathExists(f.paths.marker));
        // A wrong session id does nothing.
        CHECK(RunWatchdog(*f.env, f.game, "00000000000000000000000000000000") == 0);
    }
}

bool PumpUntil(LauncherModel& m, FakeEnv& env, const std::function<bool()>& done, int max_iterations = 4000) {
    for (int i = 0; i < max_iterations; ++i) {
        env.Advance(300);
        m.tick();
        if (done()) return true;
        Sleep(2);
    }
    return false;
}

void TestModel() {
    Fixture f(L"model");
    {
        auto m = MakeRealModelWithEnv(f.game, f.env);
        CHECK(m->phase() == Phase::needs_setup && m->game_dir() == Utf8(f.game) && m->hud() == "compact");
        CHECK(m->setup_steps().size() == 4 && m->setup_steps()[0].label == "Check the game");
        std::string code, error;
        CHECK(!m->host(code, error) && Has(error, "setup"));
        m->set_character(Character::avatar);
        m->start_setup();
        CHECK(m->phase() == Phase::setting_up);
        CHECK(PumpUntil(*m, *f.env, [&]() { return m->phase() != Phase::setting_up; }));
        CHECK(m->phase() == Phase::ready);
        if (m->phase() != Phase::ready) std::fprintf(stderr, "model setup error: %s\n", m->last_error().c_str());
        for (const SetupStep& s : m->setup_steps()) CHECK(s.state == SetupStep::State::done);
        m->set_hud("off");
        m->set_character(Character::dd_female);
        json::Value state;
        CHECK(ReadInstallState(f.paths, state) && state.Str("character") == "dd_female");

        // Host: the code is valid and copied to the clipboard; the helper starts; Steam is asked to start MGSV.
        CHECK(m->host(code, error));
        if (code.empty()) std::fprintf(stderr, "host error: %s\n", error.c_str());
        CHECK(m->phase() == Phase::launching && m->session_role() == Role::host && m->join_code() == code);
        CHECK(CheckJoinCode(code, 0).ok && f.env->clip == code && f.env->watchdog_sessions.size() == 1 && f.env->UrlCount() == 1);
        CHECK(Has(ReadText(f.paths.coop_ini), "character=snake\n") && Has(ReadText(f.paths.coop_ini), "hud=off\n"));
        CHECK(!m->check_code(code).ok);   // the player's own code
        CHECK(!m->link().headline.empty());
        // The game starts; the link status shows a connection; the start-up evidence is ok.
        f.env->AddGame(6100, f.GameExe(), f.env->NowMs());
        WriteText(f.paths.mod + L"\\early.log",
                  "saves=redirected; dir=" + Utf8(f.paths.coop_saves) + "\r\nplay=1; loopback=0; mutex=unchanged; refused=0x0; saves=redirected\r\n");
        WriteText(f.paths.mod + L"\\steam-observer.log", "master_server=redirected; method=x; url=http://127.0.0.1:47899/tppstm/gate\r\n");
        WriteText(f.paths.mod + L"\\script-observer.log", "play=active; role=host\r\nlink=config; mode=play; role=host; proto=2; bind=steam; port=0; x\r\n");
        CHECK(PumpUntil(*m, *f.env, [&]() { return m->phase() == Phase::in_game; }));
        WriteText(f.paths.link_status, "{\"v\":1,\"pid\":6100,\"state\":\"connected\",\"rtt_ms\":44,\"transport\":\"steam\",\"route\":\"direct\",\"mission\":30050,"
                                       "\"time_utc\":\"" + FormatUtcSeconds(f.env->NowMs() + 2000) + "\"}");
        CHECK(PumpUntil(*m, *f.env, [&]() { return m->link().connected; }, 10));
        CHECK(m->link().ping_ms == 44 && m->link().partner_mission == "Mother Base");
        // The game exits: co-op is switched off, the clipboard cleared, back to ready.
        f.env->Kill(6100);
        CHECK(PumpUntil(*m, *f.env, [&]() { return m->phase() == Phase::ready; }));
        CHECK(!PathExists(f.paths.marker) && !PathExists(f.paths.lab_ini) && !PathExists(f.paths.link_secret) && !f.env->clip_set);
        CHECK(m->join_code().empty() && m->link().headline.empty());
        const std::string log = ReadText(f.paths.launcher_log);
        CHECK(Has(log, "play=launch") && Has(log, "evidence=ok") && Has(log, "game=exited") && !Has(log, code.substr(5, 5)));

        // Join: bad codes, the own code and Steam not running are refused; a wait-for-invite session has no secret.
        CHECK(!m->join("MGS2-NOT-A-CODE", error) && !error.empty());
        CHECK(!m->join(FormatJoinCode("ABCDE12345FGHJK67890", 76561197960265728ull + 12345), error) && Has(error, "own code"));
        f.env->SetDword(L"Software\\Valve\\Steam\\ActiveProcess", L"ActiveUser", 0);
        CHECK(!m->join("", error) && Has(error, "Steam is not running"));
        f.env->SetDword(L"Software\\Valve\\Steam\\ActiveProcess", L"ActiveUser", 12345);
        CHECK(m->join("", error) && m->phase() == Phase::launching && m->session_role() == Role::join && m->join_code().empty());
        CHECK(!PathExists(f.paths.link_secret) && Has(ReadText(f.paths.lab_ini), "net_steam_wait=1\n") && Has(ReadText(f.paths.coop_ini), "character=dd_female\n"));
        // Cancel while launching switches co-op off.
        CHECK(m->cancel_launch(error) && m->phase() == Phase::ready && !PathExists(f.paths.marker));
        // Join with a code, then the window closes while the game runs: the helper stays in charge.
        const std::string partner = FormatJoinCode("C00PC00PC00PC00PC00P", 76561197960365728ull);
        CHECK(m->join(partner, error) && Has(ReadText(f.paths.lab_ini), "net_peer_steam=76561197960365728\n"));
        CHECK_EQ(ReadText(f.paths.link_secret), std::string("C00PC00PC00PC00PC00P\r\n"));
        f.env->AddGame(6200, f.GameExe(), f.env->NowMs());
        CHECK(PumpUntil(*m, *f.env, [&]() { return m->phase() == Phase::in_game; }));
        // Tools are refused during a session.
        std::string path;
        CHECK(!m->export_save(path, error) && Has(error, "Close the game first"));
        CHECK(!m->uninstall(path, error));
    }
    // A reopened window attaches to the running session.
    {
        auto m = MakeRealModelWithEnv(f.game, f.env);
        CHECK(m->phase() == Phase::in_game && m->session_role() == Role::join);
        f.env->Kill(6200);
        CHECK(PumpUntil(*m, *f.env, [&]() { return m->phase() == Phase::ready; }));
        CHECK(!PathExists(f.paths.marker));
        // Tools now work: export, collect logs.
        std::string path, error;
        CHECK(m->export_save(path, error) && IsFile(Wide(path)));
        CHECK(m->collect_logs(path, error) && IsFile(Wide(path)));
        std::string code;
        CHECK(m->host(code, error));
        f.env->AddGame(6300, f.GameExe(), f.env->NowMs());
        CHECK(PumpUntil(*m, *f.env, [&]() { return m->phase() == Phase::in_game; }));
    }
    {
        auto m = MakeRealModelWithEnv(f.game, f.env);
        CHECK(m->phase() == Phase::in_game && m->session_role() == Role::host && CheckJoinCode(m->join_code(), 0).ok);
        f.env->Kill(6300);
        CHECK(PumpUntil(*m, *f.env, [&]() { return m->phase() == Phase::ready; }));
        // Uninstall from the model.
        std::string report, error;
        CHECK(m->uninstall(report, error) && m->phase() == Phase::error && !IsFile(f.paths.loader));
    }
    CHECK(f.RealSavesUntouched());
    // Not in a game folder: Steam's folder is offered.
    {
        Fixture g(L"model-wrong", false);
        auto m = MakeRealModelWithEnv(g.desktop, g.env);
        CHECK(m->phase() == Phase::not_in_game_folder && m->discovered_game_dir().empty());
        WriteText(g.paths.exe, "x");
        auto m2 = MakeRealModelWithEnv(g.desktop, g.env);
        CHECK(m2->phase() == Phase::not_in_game_folder && IEquals(Wide(m2->discovered_game_dir()), g.game));
        // A game folder without the release files is an error with a sentence.
        auto m3 = MakeRealModelWithEnv(g.game, g.env);
        CHECK(m3->phase() == Phase::error && !m3->last_error().empty());
    }
}

void StageForModel(Fixture& f, const std::string& xfer, std::uint32_t pid) {
    std::string set;
    WriteStage(f, xfer, {{L"287700\\local\\TPP_GAME_DATA0", "host-save"}}, set);
    std::int64_t start = 0;
    f.env->ProcessStartMs(pid, start);
    WriteRequest(f, xfer, set, pid, start, f.env->NowMs(), 0);
}

void TestHandoffThroughModel() {
    // The guest's game exits for a save handoff: the window applies it and waits for the restart.
    Fixture f(L"model-handoff");
    bool ok = false;
    std::string error;
    RunSetupCollect(f, "story", ok, error);
    CHECK(ok);
    auto m = MakeRealModelWithEnv(f.game, f.env);
    CHECK(m->phase() == Phase::ready);
    const std::string partner = FormatJoinCode("C00PC00PC00PC00PC00P", 76561197960365728ull);
    CHECK(m->join(partner, error));
    f.env->AddGame(6400, f.GameExe(), f.env->NowMs());
    CHECK(PumpUntil(*m, *f.env, [&]() { return m->phase() == Phase::in_game; }));
    StageForModel(f, "0A0B0C0D", 6400);
    f.env->Kill(6400);
    const std::size_t urls_before = f.env->UrlCount();
    CHECK(PumpUntil(*m, *f.env, [&]() { return m->phase() == Phase::launching; }));
    CHECK(m->link().handoff_active && f.env->UrlCount() == urls_before + 1);
    CHECK_EQ(ReadText(f.paths.coop_saves + L"\\287700\\local\\TPP_GAME_DATA0"), "host-save");
    CHECK(IsFile(f.paths.marker) && Has(ReadText(f.paths.lab_ini), "save_handoff=off"));
    // The restarted game arrives and later exits: co-op switches off.
    f.env->AddGame(6500, f.GameExe(), f.env->NowMs());
    CHECK(PumpUntil(*m, *f.env, [&]() { return m->phase() == Phase::in_game; }));
    f.env->Kill(6500);
    CHECK(PumpUntil(*m, *f.env, [&]() { return m->phase() == Phase::ready; }));
    CHECK(!PathExists(f.paths.marker) && !PathExists(f.paths.lab_ini));
    // Restore the previous co-op save from Tools.
    CHECK(m->restore_save(error));
    CHECK_EQ(ReadText(f.paths.coop_saves + L"\\287700\\local\\TPP_GAME_DATA0"), "real-game-0");
    CHECK(f.RealSavesUntouched());
    // The restart never arrives: after 300 s co-op is switched off with a sentence.
    CHECK(m->join(partner, error));
    f.env->AddGame(6600, f.GameExe(), f.env->NowMs());
    CHECK(PumpUntil(*m, *f.env, [&]() { return m->phase() == Phase::in_game; }));
    StageForModel(f, "0A0B0C0E", 6600);
    f.env->Kill(6600);
    CHECK(PumpUntil(*m, *f.env, [&]() { return m->phase() == Phase::launching; }));
    CHECK(PumpUntil(*m, *f.env, [&]() { return m->phase() == Phase::ready; }));
    CHECK(Has(m->last_error(), "did not start again in time") && !PathExists(f.paths.marker));
    CHECK(Has(ReadText(f.paths.launcher_log), "relaunch_hint"));
}

// --write-session / --end-session (developer flags for the release gate's hidden
// runs): the setup when needed, then exactly Host's / Join's files, but no helper,
// no Steam and no clipboard; --end-session switches co-op off again.
std::string ValueOf(const std::string& out, const std::string& key) {
    const std::size_t at = out.find(key + "=");
    if (at == std::string::npos) return {};
    const std::size_t end = out.find('\n', at);
    return out.substr(at + key.size() + 1, end == std::string::npos ? std::string::npos : end - at - key.size() - 1);
}

void TestWriteSession() {
    Fixture f(L"write-session");
    std::string out;
    {
        WriteSessionRequest bad;
        bad.code = "MGS2-ANY";
        CHECK(RunWriteSessionWithEnv(f.game, f.env, bad, out) == 2 && Has(out, "error="));
        out.clear();
        WriteSessionRequest dog;
        dog.host = false;
        dog.character = "dog";
        CHECK(RunWriteSessionWithEnv(f.game, f.env, dog, out) == 2 && Has(out, "unknown character"));
        CHECK(!PathExists(f.paths.friend_install) && !PathExists(f.paths.marker));
    }
    // Host: the setup runs first (the folder needs it), then the session files.
    out.clear();
    WriteSessionRequest host;
    CHECK(RunWriteSessionWithEnv(f.game, f.env, host, out) == 0);
    if (!Has(out, "session=")) std::fprintf(stderr, "write-session host: %s\n", out.c_str());
    CHECK(Has(out, "setup=start\n") && Has(out, "setup=done\n") && Has(out, "role=host\n") && Has(out, "launch=0\n"));
    CHECK(f.env->UrlCount() == 0 && f.env->watchdog_sessions.empty() && !f.env->clip_set);
    const std::string code = ValueOf(out, "code");
    CHECK(CheckJoinCode(code, 0).ok && !CheckJoinCode(code, 76561197960265728ull + 12345).ok);
    json::Value s;
    CHECK(ReadPlaySession(f.paths, s) && SessionIsActive(s) && s.Str("role") == "host" && s.Str("clip_sha256").empty());
    CHECK_EQ(ValueOf(out, "session"), s.Str("session_id"));
    CHECK(StartsWith(ReadText(f.paths.lab_ini), kHeader) && Has(ReadText(f.paths.lab_ini), "net_steam_self=76561197960278073\n"));
    CHECK(Has(ReadText(f.paths.coop_ini), "character=snake\n") && ReadText(f.paths.marker) == "offline-v1\n" && IsFile(f.paths.link_secret));
    const std::string log = ReadText(f.paths.launcher_log);
    CHECK(Has(log, "play=written; role=host") && !Has(log, "play=launch") && !Has(log, code.substr(5, 5)));
    // --end-session: never under a running game; afterwards the files are gone.
    f.env->AddGame(7100, f.GameExe(), f.env->NowMs());
    out.clear();
    CHECK(RunEndSessionWithEnv(f.game, f.env, out) == 1 && Has(out, "MGSV is running") && PathExists(f.paths.marker));
    f.env->Kill(7100);
    out.clear();
    CHECK(RunEndSessionWithEnv(f.game, f.env, out) == 0 && Has(out, "cleanup=ran\n") && Has(out, "removed=script-observe.enabled\n"));
    CHECK(!PathExists(f.paths.marker) && !PathExists(f.paths.lab_ini) && !PathExists(f.paths.coop_ini) && !PathExists(f.paths.link_secret));
    CHECK(ReadPlaySession(f.paths, s) && !SessionIsActive(s));
    // Join with a code and a character (persisted as the joiner's choice).
    out.clear();
    WriteSessionRequest join;
    join.host = false;
    join.code = FormatJoinCode("C00PC00PC00PC00PC00P", 76561197960365728ull);
    join.character = "story";
    CHECK(RunWriteSessionWithEnv(f.game, f.env, join, out) == 0 && Has(out, "role=guest\n") && Has(out, "wait_invite=0\n") && !Has(out, "code="));
    CHECK(!Has(out, "setup=") && Has(ReadText(f.paths.lab_ini), "net_peer_steam=76561197960365728\n") && Has(ReadText(f.paths.coop_ini), "character=story\n"));
    CHECK_EQ(ReadText(f.paths.link_secret), std::string("C00PC00PC00PC00PC00P\r\n"));
    json::Value state;
    CHECK(ReadInstallState(f.paths, state) && state.Str("character") == "story");
    // A second write first tidies the earlier session (its writer is gone), like a new Join in the window.
    out.clear();
    WriteSessionRequest wait;
    wait.host = false;
    CHECK(RunWriteSessionWithEnv(f.game, f.env, wait, out) == 0 && Has(out, "wait_invite=1\n"));
    CHECK(!PathExists(f.paths.link_secret) && Has(ReadText(f.paths.lab_ini), "net_steam_wait=1\n") && Has(ReadText(f.paths.coop_ini), "character=story\n"));
    // The player's own code is refused, and nothing is left switched on.
    out.clear();
    CHECK(RunEndSessionWithEnv(f.game, f.env, out) == 0);
    out.clear();
    WriteSessionRequest own;
    own.host = false;
    own.code = code;
    CHECK(RunWriteSessionWithEnv(f.game, f.env, own, out) == 1 && Has(out, "own code") && !PathExists(f.paths.marker));
    CHECK(f.env->UrlCount() == 0 && f.env->watchdog_sessions.empty() && !f.env->clip_set);
    // Nothing to switch off in a folder without the mod folder.
    {
        Fixture g(L"write-session-empty", false);
        WriteText(g.paths.exe, "FAKE-EXE");
        out.clear();
        CHECK(RunEndSessionWithEnv(g.game, g.env, out) == 0 && Has(out, "session=none\n"));
    }
    CHECK(f.RealSavesUntouched());
}

void TestSelfTest() {
    std::string report;
    const std::wstring dir = g_root + L"\\selftest";
    CreateDirectories(dir);
    InstallHooks saved = Hooks();
    Hooks() = InstallHooks{};   // the real gates
    const int code = RunSelfTest(dir, report);
    Hooks() = saved;
    if (code != 0) std::fprintf(stderr, "%s", report.c_str());
    CHECK(code == 0 && Has(report, "core selftest:"));
    CHECK(ListDirectory(dir).empty());   // nothing written next to the exe
    RemoveTree(dir);
}


void TestUpdater() {
    // 1. SemVer parsing
    int maj = 0, min = 0, pat = 0;
    CHECK(ParseSemVer("0.14.1", maj, min, pat) && maj == 0 && min == 14 && pat == 1);
    CHECK(ParseSemVer("v0.14.3", maj, min, pat) && maj == 0 && min == 14 && pat == 3);
    CHECK(ParseSemVer("v1.2.3", maj, min, pat) && maj == 1 && min == 2 && pat == 3);
    CHECK(ParseSemVer("0.14.3+c327c26", maj, min, pat) && maj == 0 && min == 14 && pat == 3);
    CHECK(ParseSemVer(" 2.10.99 ", maj, min, pat) && maj == 2 && min == 10 && pat == 99);
    CHECK(!ParseSemVer("", maj, min, pat));
    CHECK(!ParseSemVer("invalid", maj, min, pat));
    CHECK(!ParseSemVer("1.2", maj, min, pat));

    // 2. Version comparison
    CHECK(IsVersionNewer("0.14.2", "0.14.1"));
    CHECK(IsVersionNewer("0.14.3", "0.14.2"));
    CHECK(IsVersionNewer("v0.14.3", "0.14.1"));
    CHECK(IsVersionNewer("0.15.0", "0.14.1"));
    CHECK(IsVersionNewer("1.0.0", "0.14.1"));
    CHECK(!IsVersionNewer("0.14.1", "0.14.1"));
    CHECK(!IsVersionNewer("v0.14.1", "0.14.1"));
    CHECK(!IsVersionNewer("0.14.0", "0.14.1"));
    CHECK(!IsVersionNewer("0.13.9", "0.14.1"));

    // 3. Release feed parsing
    const std::string feed_newer = R"([
      {
        "tag_name": "v0.14.3",
        "name": "Diamond Dogs Together 0.14.3",
        "draft": false,
        "body": "Fixed syncing and added auto update",
        "html_url": "https://github.com/TrollTaylor/diamond-dogs-together/releases/tag/v0.14.3",
        "assets": [
          {
            "name": "DiamondDogsTogether-0.14.3.zip",
            "size": 10485760,
            "browser_download_url": "https://github.com/TrollTaylor/diamond-dogs-together/releases/download/v0.14.3/DiamondDogsTogether-0.14.3.zip"
          }
        ]
      }
    ])";

    UpdateInfo info;
    std::string err;
    CHECK(ParseReleasesFeed(feed_newer, "0.14.1", info, err));
    CHECK(info.status == UpdateStatus::available);
    CHECK_EQ(info.latest_version, "v0.14.3");
    CHECK_EQ(info.release_title, "Diamond Dogs Together 0.14.3");
    CHECK_EQ(info.asset_name, "DiamondDogsTogether-0.14.3.zip");
    CHECK(info.download_size == 10485760);
    CHECK(Has(info.download_url, "DiamondDogsTogether-0.14.3.zip"));

    // Feed with same version: up to date
    CHECK(ParseReleasesFeed(feed_newer, "0.14.3", info, err));
    CHECK(info.status == UpdateStatus::up_to_date);

    // Feed with draft release: skipped
    const std::string feed_draft = R"([
      {
        "tag_name": "v0.15.0",
        "name": "Draft release",
        "draft": true,
        "assets": [
          {
            "name": "DiamondDogsTogether-0.15.0.zip",
            "size": 10485760,
            "browser_download_url": "https://github.com/test/download.zip"
          }
        ]
      }
    ])";
    CHECK(ParseReleasesFeed(feed_draft, "0.14.1", info, err));
    CHECK(info.status == UpdateStatus::up_to_date);

    // 4. UpdateHooks: mock HTTP GET and download
    UpdateHooks saved_hooks = GetUpdateHooks();
    GetUpdateHooks().http_get = [&](const std::string&, std::string& body, int& status, std::string&) {
        body = feed_newer;
        status = 200;
        return true;
    };

    UpdateInfo check_info;
    CHECK(CheckForUpdates("0.14.1", check_info, err));
    CHECK(check_info.status == UpdateStatus::available);
    CHECK_EQ(check_info.latest_version, "v0.14.3");

    // 5. ApplyUpdateZip on a mock game directory
    const std::wstring test_game = g_root + L"\\test-update-game";
    RemoveTree(test_game);
    CreateDirectories(test_game);
    CreateDirectories(test_game + L"\\mgs-coop");

    const std::wstring exe_path = test_game + L"\\DiamondDogsTogether.exe";
    const std::wstring old_exe_path = test_game + L"\\DiamondDogsTogether.exe.old";
    WriteText(exe_path, "original exe content");

    // Build a mock update zip containing updated DiamondDogsTogether.exe and package.json
    ZipWriter zw;
    const std::string new_exe_content = "new updated exe content 0.14.3";
    const std::string new_pkg_content = "{\"version\": \"0.14.3\"}";
    CHECK(zw.AddFile("DiamondDogsTogether.exe", new_exe_content.data(), new_exe_content.size(), err));
    CHECK(zw.AddFile("mgs-coop/package.json", new_pkg_content.data(), new_pkg_content.size(), err));
    std::vector<std::uint8_t> zip_bytes;
    CHECK(zw.Finish(zip_bytes, err));

    const std::wstring zip_path = test_game + L"\\mgs-coop\\update.zip";
    {
        HANDLE hZip = CreateFileW(zip_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD written = 0;
        WriteFile(hZip, zip_bytes.data(), static_cast<DWORD>(zip_bytes.size()), &written, nullptr);
        CloseHandle(hZip);
    }
    CHECK(PathExists(zip_path));

    InstancePaths paths = GetInstancePaths(test_game);
    CHECK(ApplyUpdateZip(paths, zip_path, err));
    // The zip should be deleted after applying
    CHECK(!PathExists(zip_path));
    // The old exe should have been created with original content
    CHECK(PathExists(old_exe_path));
    CHECK_EQ(ReadText(old_exe_path), "original exe content");
    // The current exe should have new content
    CHECK_EQ(ReadText(exe_path), new_exe_content);
    // mgs-coop/package.json should have been written
    CHECK_EQ(ReadText(test_game + L"\\mgs-coop\\package.json"), new_pkg_content);

    // 6. CleanOldExecutable
    CleanOldExecutable(test_game);
    CHECK(!PathExists(old_exe_path));

    // Restore hooks
    GetUpdateHooks() = saved_hooks;
    RemoveTree(test_game);
}

}  // namespace

int main() {
    wchar_t temp[MAX_PATH + 1]{};
    GetTempPathW(MAX_PATH, temp);
    g_root = FullPath(std::wstring(temp) + L"ddt-core-tests-" + std::to_wstring(GetCurrentProcessId()));
    RemoveTree(g_root);
    CreateDirectories(g_root);
    InstallTestHooks();

    TestJsonAndTime();
    TestCodes();
    TestIniText();
    TestReasonFixture();
    TestStatusLine();
    TestEvidence();
    TestRedaction();
    TestSetHash();
    TestPathsAndDiscovery();
    TestActivationAndCleanup();
    TestHandoff();
    TestShareSave();
    TestSetup();
    TestUninstallAndLogs();
    TestWatchdog();
    TestModel();
    TestHandoffThroughModel();
    TestWriteSession();
    TestSelfTest();
    TestUpdater();

    RemoveTree(g_root);
    std::printf("ddt_core_tests: %u checks, %u failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
