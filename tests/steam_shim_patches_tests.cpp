// Unit tests for the co-op lab's pure decision helpers: mutex rename, XInput
// routing, save-path mapping, wire decode, loss schedule and endpoint filter,
// plus the save handoff's redirect helpers (local and handoff paths, the
// remote snapshot read under concurrent game writes).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "mgscoop/early_patches.hpp"
#include "mgscoop/steam_abi.hpp"
#include "mgscoop/steam_identity.hpp"
#include "mgscoop/lab_config.hpp"
#include "mgscoop/save_redirect.hpp"
#include "mgscoop/udp_transport.hpp"
#include "mgscoop/steam_net_abi.hpp"
#include "steam_shim_contract.hpp"
#include <algorithm>
#include <string>
#include <vector>

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

unsigned provider_calls=0;
bool FakeWorkerProvider(unsigned i,void* output,std::size_t bytes) noexcept {
    ++provider_calls;if(i!=0||bytes!=16)return false;
    std::memset(output,0,bytes);static_cast<unsigned char*>(output)[6]=255;return true;
}
void TestXInputRoute() {
    using namespace mgscoop::lab;
    unsigned char virtual_state[16]{};
    Check(TryWorkerXInput(-1,0,virtual_state,sizeof(virtual_state),FakeWorkerProvider)&&virtual_state[6]==255&&provider_calls==1,"nonecanserveexplicitvirtualproviderwithoutphysicalpoll");
    for(int setting:{-2,0,1,2,3}) Check(!TryWorkerXInput(setting,0,virtual_state,sizeof(virtual_state),FakeWorkerProvider),"providercannotchangephysicalroutes");
    Check(!TryWorkerXInput(-1,1,virtual_state,sizeof(virtual_state),FakeWorkerProvider)&&
          !TryWorkerXInput(-1,0,nullptr,sizeof(virtual_state),FakeWorkerProvider)&&
          !TryWorkerXInput(-1,0,virtual_state,15,FakeWorkerProvider)&&
          !TryWorkerXInput(-1,0,virtual_state,sizeof(virtual_state),nullptr)&&provider_calls==1,"foreignindexnullwrongsizeandmissingprovidernevercallcallback");
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

// Save handoff (join-fix phase 2 B3): <save_dir>\287700\local\<name> with the
// remote name rules, and <save_dir>-handoff within the 248-character budget.
void TestHandoffPaths() {
    using namespace mgscoop::lab::save;
    char buf[520];
    Check(format_local_path("D:\\lab\\saves", "TPP_GAME_DATA0", buf, sizeof(buf)) &&
              std::strcmp(buf, "D:\\lab\\saves\\287700\\local\\TPP_GAME_DATA0") == 0,
          "local slot path");
    Check(format_local_path("D:\\lab\\saves\\\\", "PERSONAL_DATA1", buf, sizeof(buf)) &&
              std::strcmp(buf, "D:\\lab\\saves\\287700\\local\\PERSONAL_DATA1") == 0,
          "local path trims trailing separators");
    for (const char* bad : {"..", ".", "a\\b", "a/b", "C:evil", "", "..\\TPP_GAME_DATA0"}) {
        buf[0] = 'x';
        buf[1] = 0;
        Check(!format_local_path("D:\\lab\\saves", bad, buf, sizeof(buf)) && buf[0] == 0, "local traversal refused");
    }
    Check(!format_local_path("D:\\lab\\saves", nullptr, buf, sizeof(buf)) && !format_local_path("", "TPP_GAME_DATA0", buf, sizeof(buf)) &&
              !format_local_path("\\\\", "TPP_GAME_DATA0", buf, sizeof(buf)),
          "local path needs a name and a save_dir");
    char tiny[24];
    Check(!format_local_path("D:\\lab\\saves", "TPP_GAME_DATA0", tiny, sizeof(tiny)) && tiny[0] == 0, "local path overflow");

    Check(format_handoff_dir("D:\\Games\\MGS_TPP\\mgs-coop\\coop-saves", buf, sizeof(buf)) &&
              std::strcmp(buf, "D:\\Games\\MGS_TPP\\mgs-coop\\coop-saves-handoff") == 0,
          "handoff dir is <save_dir>-handoff");
    Check(format_handoff_dir("D:\\run\\guest-saves\\/\\", buf, sizeof(buf)) && std::strcmp(buf, "D:\\run\\guest-saves-handoff") == 0,
          "handoff dir trims trailing separators");
    Check(!format_handoff_dir("", buf, sizeof(buf)) && !format_handoff_dir(nullptr, buf, sizeof(buf)) &&
              !format_handoff_dir("\\", buf, sizeof(buf)),
          "handoff dir needs a save_dir");
    // The longest staged path (the folder plus kHandoffLongestTail) stays below 248.
    Check(kHandoffLongestTail == std::strlen("\\00000000.partial\\287700\\local\\TPP_GAME_DATA0.mgs-coop-"
                                             "00000000000000000000000000000000.tmp"),
          "the tooling's longest staged name");
    const std::size_t longest = kHandoffPathLimit - 1 - kHandoffLongestTail - (sizeof(kHandoffSuffix) - 1);
    std::string dir = "D:\\" + std::string(longest - 3, 'a');
    Check(dir.size() == longest && format_handoff_dir(dir.c_str(), buf, sizeof(buf)) &&
              std::strlen(buf) + kHandoffLongestTail == kHandoffPathLimit - 1,
          "the longest accepted save_dir");
    Check(format_handoff_dir((dir + "\\").c_str(), buf, sizeof(buf)), "a trailing separator is not counted");
    dir += 'a';
    buf[0] = 'x';
    buf[1] = 0;
    Check(!format_handoff_dir(dir.c_str(), buf, sizeof(buf)) && buf[0] == 0, "one more character is refused");
    char small[16];
    Check(!format_handoff_dir("D:\\lab\\saves", small, sizeof(small)) && small[0] == 0, "handoff dir overflow");
}

// read_remote_snapshot under concurrent game writes: the redirect's I/O lock
// makes the snapshot see whole files only, and the game's exclusive write never
// fails because a snapshot holds the file open.
struct FakeStorage {
    const void** vtable;
};
std::uintptr_t MGSCOOP_STEAM_CALL GenericStorageSlot(void*) noexcept { return 0; }
using FileWriteFn = bool(MGSCOOP_STEAM_CALL*)(void*, const char*, const void*, std::int32_t);
using FileReadFn = std::int32_t(MGSCOOP_STEAM_CALL*)(void*, const char*, void*, std::int32_t);
struct WriterState {
    void* proxy;
    volatile LONG stop;
    volatile LONG writes, failures;
};
DWORD WINAPI WriterThread(void* parameter) {
    auto* state = static_cast<WriterState*>(parameter);
    static std::vector<unsigned char> a(65536, 0xA5), b(65536 + 4096, 0x5B);
    const auto write = reinterpret_cast<FileWriteFn>((*static_cast<void* const* const*>(state->proxy))[mgscoop::steam::storage_slot::FileWrite]);
    for (unsigned i = 0; !state->stop; ++i) {
        const auto& data = (i & 1) ? b : a;
        if (write(state->proxy, "TPP_GAME_DATA", data.data(), static_cast<std::int32_t>(data.size()))) InterlockedIncrement(&state->writes);
        else InterlockedIncrement(&state->failures);
    }
    return 0;
}
void TestRemoteSnapshot() {
    namespace save = mgscoop::lab::save;
    save::reset_for_test();
    char temp[MAX_PATH]{};
    Check(GetTempPathA(MAX_PATH, temp) > 0, "temp path");
    char dir[MAX_PATH]{};
    std::snprintf(dir, sizeof(dir), "%smgscoop-snapshot-test-%lu", temp, GetCurrentProcessId());
    std::vector<unsigned char> buffer(1 << 20);
    std::size_t size = 7;
    Check(!save::read_remote_snapshot("TPP_GAME_DATA", buffer.data(), buffer.size(), size) && size == 0, "not configured: refused");
    save::configure(dir);
    Check(save::configured(), "save redirect configured");
    char current[MAX_PATH]{};
    Check(save::current_save_dir(current, sizeof(current)) && std::strcmp(current, dir) == 0, "current_save_dir");
    char tiny[4];
    Check(!save::current_save_dir(tiny, sizeof(tiny)) && tiny[0] == 0, "current_save_dir overflow");
    static const void* vt[mgscoop::steam::storage_slot::kCount];
    for (auto& entry : vt) entry = reinterpret_cast<const void*>(&GenericStorageSlot);
    static FakeStorage storage{vt};
    void* proxy = save::wrap_remote_storage(&storage);
    Check(proxy && proxy != &storage, "storage proxy");
    const auto write = reinterpret_cast<FileWriteFn>((*static_cast<void* const* const*>(proxy))[mgscoop::steam::storage_slot::FileWrite]);
    const auto read = reinterpret_cast<FileReadFn>((*static_cast<void* const* const*>(proxy))[mgscoop::steam::storage_slot::FileRead]);
    Check(!save::read_remote_snapshot("TPP_GAME_DATA", buffer.data(), buffer.size(), size), "a missing file is refused");
    const unsigned char hello[5] = {'h', 'e', 'l', 'l', 'o'};
    Check(write(proxy, "TPP_GAME_DATA", hello, 5), "game write");
    Check(save::read_remote_snapshot("TPP_GAME_DATA", buffer.data(), buffer.size(), size) && size == 5 && std::memcmp(buffer.data(), hello, 5) == 0,
          "snapshot of a whole file");
    Check(!save::read_remote_snapshot("TPP_GAME_DATA", buffer.data(), 4, size) && size == 0, "a file larger than the buffer is refused");
    Check(!save::read_remote_snapshot("..\\x", buffer.data(), buffer.size(), size) && !save::read_remote_snapshot(nullptr, buffer.data(), 8, size),
          "unsafe names refused");
    unsigned char back[8]{};
    Check(read(proxy, "TPP_GAME_DATA", back, sizeof(back)) == 5, "the game's own read still works");
    // Two threads: the game writes alternating 64 KB / 68 KB files while the
    // snapshot reads 1000 times; every read is one whole version.
    WriterState state{proxy, 0, 0, 0};
    HANDLE thread = CreateThread(nullptr, 0, &WriterThread, &state, 0, nullptr);
    Check(thread != nullptr, "writer thread");
    unsigned whole = 0, refused = 0;
    for (unsigned i = 0; i < 1000; ++i) {
        if (!save::read_remote_snapshot("TPP_GAME_DATA", buffer.data(), buffer.size(), size)) {
            ++refused;
            continue;
        }
        const bool a = size == 65536 && std::all_of(buffer.begin(), buffer.begin() + 65536, [](unsigned char c) { return c == 0xA5; });
        const bool b = size == 65536 + 4096 &&
                       std::all_of(buffer.begin(), buffer.begin() + 65536 + 4096, [](unsigned char c) { return c == 0x5B; });
        const bool first = size == 5;  // before the writer's first write
        if (!(a || b || first)) {
            std::fprintf(stderr, "torn snapshot: size %zu\n", size);
            Check(false, "a snapshot never returns torn bytes");
        }
        ++whole;
    }
    InterlockedExchange(&state.stop, 1);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    std::printf("remote snapshot: %u whole reads, %u refused, %ld game writes, %ld failed\n", whole, refused,
                static_cast<long>(state.writes), static_cast<long>(state.failures));
    Check(state.failures == 0, "a snapshot never makes the game's write fail");
    Check(state.writes > 0 && whole + refused == 1000 && whole > 0, "both threads ran");
    // Clean up exactly what this test created.
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\remote\\TPP_GAME_DATA", dir);
    DeleteFileA(path);
    std::snprintf(path, sizeof(path), "%s\\remote", dir);
    RemoveDirectoryA(path);
    std::snprintf(path, sizeof(path), "%s\\287700\\local", dir);
    RemoveDirectoryA(path);
    std::snprintf(path, sizeof(path), "%s\\287700", dir);
    RemoveDirectoryA(path);
    RemoveDirectoryA(dir);
    save::reset_for_test();
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
        // The hidden worker's one-PC play rehearsal marks its line.
        Check(format_play_line(0, false, true, true, line, sizeof(line), true) &&
              std::strcmp(line, "play=1; loopback=0; mutex=renamed; refused=0x0; saves=redirected; rehearsal=1\r\n") == 0,
              "play rehearsal line");
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
// Steam online (docs/steam-online.md 6.1): the SteamAPI_Init IAT wrapper.
int g_init_calls = 0, g_register_calls = 0, g_register_id = 0;
void* g_register_object = nullptr;
bool g_init_result = true;
bool FakeInit() { ++g_init_calls; return g_init_result; }
void FakeRegister(void* object, int id) {
    ++g_register_calls;
    g_register_object = object;
    g_register_id = id;
}
void TestInitWrapper() {
    using namespace mgscoop::lab;
    // Only mode=play, net=1, net_transport=steam (and lab.ini present).
    Check(WantInitWrapper({true, true, true, true, true, true}) && WantInitWrapper({true, true, true, true, false, true}), "play + net + steam");
    Check(!WantInitWrapper({true, false, true, true, true, true}), "lab: never");
    Check(!WantInitWrapper({true, true, false, true, true, true}), "net=0: never");
    Check(!WantInitWrapper({true, true, true, false, true, true}), "udp: never");
    Check(!WantInitWrapper({false, true, true, true, true, true}), "no lab.ini: never");
    Check(!WantInitWrapper({true, true, true, true, true, false}), "saves not redirected (play refused): never");
    // net_steam_dispatch (docs/steam-online.md 20): 0 both, 1 fnptr, 2 callback, 3 poll.
    Check(WantInitWrapper({true, true, true, true, true, true, 0}) && WantInitWrapper({true, true, true, true, false, true, 0}), "both: host and guest");
    Check(WantInitWrapper({true, true, true, true, true, true, 2}) && WantInitWrapper({true, true, true, true, false, true, 2}), "callback: host and guest");
    Check(!WantInitWrapper({true, true, true, true, true, true, 1}) && WantInitWrapper({true, true, true, true, false, true, 1}),
          "fnptr: no host 1251, the guest keeps 337");
    Check(!WantInitWrapper({true, true, true, true, true, true, 3}) && !WantInitWrapper({true, true, true, true, false, true, 3}), "poll: never");
    Check(InitWrapperSkipReason({true, true, true, true, true, true, 0}) == nullptr &&
              std::strcmp(InitWrapperSkipReason({true, true, true, true, true, true, 1}), "dispatch_fnptr") == 0 &&
              InitWrapperSkipReason({true, true, true, true, false, true, 1}) == nullptr &&
              std::strcmp(InitWrapperSkipReason({true, true, true, true, false, true, 3}), "dispatch_poll") == 0 &&
              InitWrapperSkipReason({true, false, true, true, true, true, 3}) == nullptr,
          "skip reasons only for play + steam");
    // Registers once, only on a true result; returns Init's result unchanged.
    int object = 0;
    InitWrapperState state{};
    Check(RunInitWrapper(&FakeInit, &FakeRegister, &object, 1251, state) && g_init_calls == 1 && g_register_calls == 1 &&
              g_register_id == 1251 && g_register_object == &object && state.registered,
          "true: Init once, then RegisterCallback(object, 1251)");
    Check(RunInitWrapper(&FakeInit, &FakeRegister, &object, 1251, state) && g_init_calls == 2 && g_register_calls == 1, "never twice");
    g_init_result = false;
    InitWrapperState failed{};
    Check(!RunInitWrapper(&FakeInit, &FakeRegister, &object, 337, failed) && g_register_calls == 1 && !failed.registered && failed.called,
          "false: no registration, false returned");
    g_init_result = true;
    // The pins (a synthetic steam_api64 image with the contract bytes).
    namespace sc = mgscoop::steam_shim_contract;
    std::vector<std::uint8_t> image(sc::kSteamSizeOfImage, 0);
    image[0] = 'M';
    image[1] = 'Z';
    const std::uint32_t pe = 0x100;
    std::memcpy(&image[0x3C], &pe, 4);
    image[pe] = 'P';
    image[pe + 1] = 'E';
    std::memcpy(&image[pe + 8], &sc::kSteamTimeDateStamp, 4);
    std::memcpy(&image[pe + 24 + 56], &sc::kSteamSizeOfImage, 4);
    for (const sc::ByteGate* gate : {&sc::steam_init_export, &sc::steam_register_export, &sc::steam_register_body, &sc::steam_dispatch})
        std::memcpy(&image[gate->rva], gate->bytes, gate->size);
    Check(InitWrapperRefusal(image.data(), image.size()) == nullptr, "pins match");
    Check(sc::steam_init_export.rva == 0x5420 && sc::steam_init_export.size == 7 && sc::steam_init_export.bytes[0] == 0x33 &&
              sc::steam_init_export.bytes[1] == 0xC9 && sc::steam_init_export.bytes[2] == 0xE9,
          "SteamAPI_Init 0x5420: xor ecx,ecx; jmp");
    image[sc::steam_init_export.rva + 3] ^= 1;
    Check(std::strcmp(InitWrapperRefusal(image.data(), image.size()), "pin_mismatch") == 0, "an export byte mismatch refuses");
    image[sc::steam_init_export.rva + 3] ^= 1;
    image[sc::steam_dispatch.rva + 4] ^= 1;
    Check(std::strcmp(InitWrapperRefusal(image.data(), image.size()), "pin_mismatch") == 0, "a dispatch byte mismatch refuses");
    image[sc::steam_dispatch.rva + 4] ^= 1;
    image[pe + 8] ^= 1;
    Check(std::strcmp(InitWrapperRefusal(image.data(), image.size()), "steam_api64_mismatch") == 0, "another steam_api64 build refuses");
    Check(std::strcmp(InitWrapperRefusal(nullptr, 0), "steam_api64_not_mapped") == 0, "not mapped");
    // The flat exports the owner context resolves are all pinned by name.
    bool names = true;
    for (const auto& named : sc::kOnlineExports) names = names && named.gate->size >= 7 && named.gate->rva < sc::kSteamSizeOfImage;
    Check(names && sizeof(sc::kOnlineExports) / sizeof(sc::kOnlineExports[0]) == 16, "16 pinned flat exports");
    Check(sc::kGameRegisterCallbackSites == 9, "the game's 9 RegisterCallback sites (none of ours)");
    Check(!InitWrapperInstalled(), "never installed outside the game");
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
    TestHandoffPaths();
    TestRemoteSnapshot();
    TestInitWrapper();
    std::printf("steam_shim_patches_tests: %u checks passed\n", g_checks);
    return 0;
}
