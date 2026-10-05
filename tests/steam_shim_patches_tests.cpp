// Unit tests for the co-op lab's pure decision helpers: mutex rename, XInput
// routing, save-path mapping, wire decode, loss schedule and endpoint filter.
#include "mgscoop/early_patches.hpp"
#include "mgscoop/lab_config.hpp"
#include "mgscoop/save_redirect.hpp"
#include "mgscoop/udp_transport.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <initializer_list>

namespace {
unsigned g_checks = 0;
void Check(bool value, const char* what) {
    ++g_checks;
    if (!value) {
        std::fprintf(stderr, "failed check %u: %s\n", g_checks, what);
        std::exit(1);
    }
}

void TestMutexName() {
    using namespace mgscoop::lab;
    wchar_t buf[128];
    bool renamed = false;
    // Exact title + valid suffix -> appended.
    const wchar_t* out = mutex_effective_name(kMutexTitle, "-guest", buf, 128, renamed);
    Check(renamed, "title renamed");
    Check(out == buf, "returns buffer on rename");
    Check(std::wcscmp(out, L"METAL GEAR SOLID V: THE PHANTOM PAIN-guest") == 0, "renamed value");
    // A different name is never touched.
    renamed = true;
    const wchar_t* other = L"Local\\SomethingElse";
    out = mutex_effective_name(other, "-guest", buf, 128, renamed);
    Check(!renamed && out == other, "non-title left unchanged");
    // Empty suffix is a no-op even for the title.
    out = mutex_effective_name(kMutexTitle, "", buf, 128, renamed);
    Check(!renamed && out == kMutexTitle, "empty suffix no-op");
    // Invalid suffix characters are rejected.
    out = mutex_effective_name(kMutexTitle, "bad suffix!", buf, 128, renamed);
    Check(!renamed && out == kMutexTitle, "invalid suffix rejected");
    // A buffer too small to hold the result leaves the name unchanged.
    wchar_t tiny[8];
    out = mutex_effective_name(kMutexTitle, "-guest", tiny, 8, renamed);
    Check(!renamed && out == kMutexTitle, "overflow leaves name unchanged");
    // Null name is returned unchanged.
    out = mutex_effective_name(nullptr, "-guest", buf, 128, renamed);
    Check(!renamed && out == nullptr, "null name unchanged");
}

void TestXInputRoute() {
    using namespace mgscoop::lab;
    // Passthrough forwards every index unchanged.
    for (unsigned i = 0; i < 4; ++i) {
        auto r = route_xinput(mgscoop::kXInputPassthrough, i);
        Check(r.action == XInputAction::Passthrough && r.index == i, "passthrough forwards");
    }
    // none disconnects every index.
    for (unsigned i = 0; i < 4; ++i) {
        auto r = route_xinput(mgscoop::kXInputNone, i);
        Check(r.action == XInputAction::Disconnected, "none disconnects");
    }
    // pad 2 -> only controller 0 is served, from physical pad 2; others gone.
    auto zero = route_xinput(2, 0);
    Check(zero.action == XInputAction::Remap && zero.index == 2, "pad N maps index 0 to N");
    for (unsigned i = 1; i < 4; ++i)
        Check(route_xinput(2, i).action == XInputAction::Disconnected, "pad N disconnects others");
    // pad 0 -> identity remap of index 0.
    auto p0 = route_xinput(0, 0);
    Check(p0.action == XInputAction::Remap && p0.index == 0, "pad 0 maps to 0");
}

void TestSavePaths() {
    using namespace mgscoop::lab::save;
    char buf[520];
    Check(format_user_data_folder("D:\\lab\\saves", buf, sizeof(buf)), "user folder ok");
    Check(std::strcmp(buf, "D:\\lab\\saves\\287700\\local") == 0, "user folder value");
    // Trailing separators on the save dir are trimmed.
    Check(format_user_data_folder("D:\\lab\\saves\\", buf, sizeof(buf)), "trailing sep ok");
    Check(std::strcmp(buf, "D:\\lab\\saves\\287700\\local") == 0, "trailing sep trimmed");
    Check(!format_user_data_folder("", buf, sizeof(buf)), "empty dir rejected");
    char tiny[8];
    Check(!format_user_data_folder("D:\\lab\\saves", tiny, sizeof(tiny)), "overflow rejected");

    Check(format_remote_path("D:\\lab\\saves", "TPP_GAME_DATA0", buf, sizeof(buf)), "remote ok");
    Check(std::strcmp(buf, "D:\\lab\\saves\\remote\\TPP_GAME_DATA0") == 0, "remote value");
    // Traversal and directory parts are rejected.
    Check(!format_remote_path("D:\\lab\\saves", "..", buf, sizeof(buf)), "dotdot rejected");
    Check(!format_remote_path("D:\\lab\\saves", ".", buf, sizeof(buf)), "dot rejected");
    Check(!format_remote_path("D:\\lab\\saves", "a\\b", buf, sizeof(buf)), "backslash rejected");
    Check(!format_remote_path("D:\\lab\\saves", "a/b", buf, sizeof(buf)), "slash rejected");
    Check(!format_remote_path("D:\\lab\\saves", "C:evil", buf, sizeof(buf)), "colon rejected");
    Check(!format_remote_path("D:\\lab\\saves", "", buf, sizeof(buf)), "empty name rejected");
    Check(is_safe_remote_name("PERSONAL_DATA1"), "normal name safe");
    Check(!is_safe_remote_name(".."), "dotdot unsafe");
}

void TestWireDecode() {
    using namespace mgscoop::lab;
    const std::uint64_t self = 0x0110000100000001ull;
    std::uint8_t buf[kHeaderSize + 16];
    DatagramHeader h{};
    h.type = DatagramType::Data;
    h.send_mode = 0;
    h.src = self;
    h.dst = 0x0110000100000002ull;
    h.length = 4;
    encode_header(h, buf);
    std::memcpy(buf + kHeaderSize, "abcd", 4);

    DatagramHeader out{};
    const std::uint8_t* payload = nullptr;
    std::size_t payload_size = 0;
    Check(decode_datagram(buf, kHeaderSize + 4, out, payload, payload_size) == DecodeStatus::ok, "decode ok");
    Check(out.src == self && out.length == 4 && payload_size == 4, "decode fields");
    Check(std::memcmp(payload, "abcd", 4) == 0, "decode payload");

    // Length must equal the trailing bytes exactly (UDP keeps boundaries).
    Check(decode_datagram(buf, kHeaderSize + 3, out, payload, payload_size) == DecodeStatus::length_mismatch,
          "short trailing rejected");
    Check(decode_datagram(buf, kHeaderSize + 5, out, payload, payload_size) == DecodeStatus::length_mismatch,
          "long trailing rejected");
    // Bad magic / version.
    std::uint8_t bad[kHeaderSize + 4];
    std::memcpy(bad, buf, sizeof(bad));
    bad[0] = 'X';
    Check(decode_datagram(bad, sizeof(bad), out, payload, payload_size) == DecodeStatus::bad_magic, "bad magic");
    std::memcpy(bad, buf, sizeof(bad));
    bad[4] = 9;
    Check(decode_datagram(bad, sizeof(bad), out, payload, payload_size) == DecodeStatus::bad_version, "bad version");
    // Non-individual source id.
    DatagramHeader b = h;
    b.src = 123;
    encode_header(b, bad);
    std::memcpy(bad + kHeaderSize, "abcd", 4);
    Check(decode_datagram(bad, kHeaderSize + 4, out, payload, payload_size) == DecodeStatus::bad_source, "bad source");
    // DATA mode out of range.
    b = h;
    b.send_mode = 7;
    encode_header(b, bad);
    std::memcpy(bad + kHeaderSize, "abcd", 4);
    Check(decode_datagram(bad, kHeaderSize + 4, out, payload, payload_size) == DecodeStatus::bad_mode, "bad mode");
    // PING must be empty.
    DatagramHeader ping{};
    ping.type = DatagramType::Ping;
    ping.src = self;
    ping.length = 2;
    std::uint8_t pbuf[kHeaderSize + 2];
    encode_header(ping, pbuf);
    Check(decode_datagram(pbuf, kHeaderSize + 2, out, payload, payload_size) == DecodeStatus::bad_control_length,
          "non-empty ping rejected");
    // too short.
    Check(decode_datagram(buf, kHeaderSize - 1, out, payload, payload_size) == DecodeStatus::too_short, "too short");
}

void TestLossSchedule() {
    using namespace mgscoop::lab;
    // Never drop at 0, always at >=1000.
    for (std::uint64_t i = 0; i < 100; ++i) {
        Check(!should_drop(0, i), "no loss at 0");
        Check(should_drop(1000, i), "all loss at 1000");
    }
    // Deterministic and proportional: over N the count is within 1 of N*p/1000.
    for (std::uint32_t permille : {5u, 100u, 333u, 900u}) {
        unsigned dropped = 0;
        const unsigned n = 10000;
        for (std::uint64_t i = 0; i < n; ++i)
            if (should_drop(permille, i)) ++dropped;
        const unsigned expected = static_cast<unsigned>((static_cast<std::uint64_t>(n) * permille) / 1000);
        Check(dropped == expected || dropped == expected + 1, "loss proportional");
    }
    // Determinism: same counter, same answer.
    Check(should_drop(250, 7) == should_drop(250, 7), "loss deterministic");
}

void TestMasterUrl() {
    using mgscoop::lab::is_local_master_url;
    Check(is_local_master_url("http://127.0.0.1:6667/tppstm/gate"), "fuse default is local");
    Check(is_local_master_url("http://localhost/tppstm/gate"), "localhost without port");
    Check(is_local_master_url("HTTPS://LOCALHOST:443/x"), "case-insensitive scheme/host");
    Check(is_local_master_url("http://127.0.0.1"), "bare host");
    Check(!is_local_master_url("http://127.0.0.1.evil.com/gate"), "host suffix rejected");
    Check(!is_local_master_url("http://localhost.example/gate"), "localhost prefix rejected");
    Check(!is_local_master_url("http://mgstpp-game.konamionline.com/tppstm/gate"), "remote host rejected");
    Check(!is_local_master_url("http://127.0.0.1:0/gate"), "port 0 rejected");
    Check(!is_local_master_url("http://127.0.0.1:65536/gate"), "port out of range rejected");
    Check(!is_local_master_url("http://127.0.0.1:/gate"), "empty port rejected");
    Check(!is_local_master_url("ftp://127.0.0.1/gate"), "other scheme rejected");
    Check(!is_local_master_url(""), "empty rejected");
    Check(!is_local_master_url(nullptr), "null rejected");
}

void TestMutexBlocker() {
    using namespace mgscoop::lab;
    IsolationState s{};
    Check(mutex_rename_blocker(s) != nullptr, "first instance never renames");
    s.second_instance = true;
    Check(std::strcmp(mutex_rename_blocker(s), "save_dir_required") == 0, "guest without save_dir blocked");
    s.saves_configured = true;
    Check(std::strcmp(mutex_rename_blocker(s), "save_isolation_failed") == 0, "failed save patch blocks");
    s.saves_ok = true;
    Check(mutex_rename_blocker(s) == nullptr, "isolated saves, real id, no gate needed -> rename");
    s.want_identity = true;
    Check(std::strcmp(mutex_rename_blocker(s), "identity_isolation_failed") == 0, "failed identity blocks");
    s.identity_ok = true;
    s.want_gate = true;
    Check(std::strcmp(mutex_rename_blocker(s), "networking_gate_failed") == 0, "missing gate blocks");
    s.gate_ok = true;
    Check(mutex_rename_blocker(s) == nullptr, "fully isolated guest -> rename");
}

void TestSaveDirFits() {
    using namespace mgscoop::lab::save;
    Check(save_dir_fits("D:\\lab\\saves"), "short dir fits");
    Check(!save_dir_fits(""), "empty does not fit");
    Check(!save_dir_fits(nullptr), "null does not fit");
    char dir[260];
    std::memset(dir, 'a', sizeof(dir));
    dir[kMaxSaveDirChars] = 0;
    Check(save_dir_fits(dir), "exactly the limit fits");
    // The limit leaves room for the game's longest file name in a 260-char path.
    char folder[300];
    Check(format_user_data_folder(dir, folder, sizeof(folder)), "limit formats");
    Check(std::strlen(folder) + 1 + 32 < 260, "limit + separator + 32-char name < MAX_PATH");
    dir[kMaxSaveDirChars] = 'a';
    dir[kMaxSaveDirChars + 1] = 0;
    Check(!save_dir_fits(dir), "one over the limit rejected");
    dir[kMaxSaveDirChars] = '\\';  // trailing separators are trimmed first
    Check(save_dir_fits(dir), "trailing separator not counted");
}

void TestEndpointFilter() {
    using namespace mgscoop::lab;
    // Two-PC play: the early.log play line and the save-redirect state.
    {
        char line[160]{};
        Check(format_play_line(0, false, false, true, line, sizeof(line)) &&
              std::strcmp(line, "play=1; loopback=0; mutex=unchanged; refused=0x0; saves=redirected\r\n") == 0, "play line");
        Check(format_play_line(0x4A01, false, false, false, line, sizeof(line)) &&
              std::strcmp(line, "play=1; loopback=0; mutex=unchanged; refused=0x4A01; saves=unchanged\r\n") == 0, "play line refused");
        Check(!format_play_line(0, false, false, true, line, 20) && line[0] == 0, "play line does not fit");
        Check(!SavesRedirected(), "saves not redirected before the DllMain patches");
        set_saves_redirected_for_test(true);
        Check(SavesRedirected(), "saves redirected");
        set_saves_redirected_for_test(false);
        Check(!SavesRedirected(), "saves state reset");
        Check(ProbeVersion() && ProbeVersion()[0] >= '0' && ProbeVersion()[0] <= '9', "probe version");
    }
    Check(is_expected_peer(0x7F000001u, 47810, 47810), "loopback peer accepted");
    Check(!is_expected_peer(0x7F000001u, 47811, 47810), "wrong port rejected");
    Check(!is_expected_peer(0x08080808u, 47810, 47810), "non-loopback rejected");
    Check(!is_expected_peer(0x7F000001u, 47810, 0), "zero peer port rejects");
}
}  // namespace

int main() {
    TestMutexName();
    TestXInputRoute();
    TestSavePaths();
    TestWireDecode();
    TestLossSchedule();
    TestEndpointFilter();
    TestMasterUrl();
    TestMutexBlocker();
    TestSaveDirFits();
    std::printf("steam_shim_patches_tests: %u checks passed\n", g_checks);
    return 0;
}
