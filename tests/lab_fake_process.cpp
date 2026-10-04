// Stand-in processes for the lab script end-to-end test (tests/lab_scripts_tests.ps1).
// Never the game and never fuse:
//  - with "-standalone ... -listenAddr 127.0.0.1:<port>" it plays fuse: listens on
//    that loopback TCP port; for every connection it writes one CMD_GET_URLLIST
//    and one CMD_AUTH_STEAMTICKET -writeLog request file (with -writeLog) and
//    prints fuse-style console lines, including the SteamID the client sent;
//  - otherwise it plays a lab game instance (copied as <fixture>\mgsvtpp.exe): it
//    refuses to run unless <exeDir>\mgs-coop\lab.ini exists, writes the
//    docs/lab-protocol.md early.log / steam-observer.log / probe.log lines for its
//    lab.ini and coop.ini, "logs in" to the fake fuse at the coop.ini master_url,
//    shows an off-screen window, writes the script-observer.log lines and
//    lab-status.json for its role, consumes join.trigger (guest), exchanges one
//    UDP datagram over the configured loopback ports and exits on WM_CLOSE.
// Test knobs (environment): MGS_COOP_FAKE_HOST_STEAMID - the id the host writes to
// host-steamid.txt and presents to fuse; MGS_COOP_FAKE_UNSAFE=<role>:<saves|identity|master>
// - that role logs the corresponding "rule not in force" line instead.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

namespace {
std::wstring mod_dir;
std::string role;
std::string save_dir;
std::string self_steamid;
std::string mutex_suffix;
std::string master_url;
int listen_port = 0, peer_port = 0;
SOCKET udp = INVALID_SOCKET;
int step = 0;
std::string session = "none";
std::string join = "idle";

std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

std::string Env(const char* name) {
    char value[128]{};
    const DWORD n = GetEnvironmentVariableA(name, value, sizeof(value));
    return n > 0 && n < sizeof(value) ? std::string(value, n) : std::string();
}

bool Unsafe(const char* what) { return Env("MGS_COOP_FAKE_UNSAFE") == role + ":" + what; }

void AppendTo(const wchar_t* file, const std::string& line) {
    const std::wstring path = mod_dir + L"\\" + file;
    HANDLE f = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    WriteFile(f, "\r\n", 2, &written, nullptr);
    CloseHandle(f);
}

void Append(const std::string& line) { AppendTo(L"script-observer.log", line); }

void Status() {
    char json[512];
    std::snprintf(json, sizeof(json),
                  "{\"version\":1,\"pid\":%lu,\"role\":\"%s\",\"mode\":\"%s\",\"mission\":30050,\"session_status\":\"%s\","
                  "\"members\":0,\"local_index\":-1,\"join\":\"%s\",\"join_attempts\":0,\"last_refusal\":\"\",\"updated_tick_ms\":%llu}",
                  GetCurrentProcessId(), role.c_str(), role == "host" ? "host_v3" : "client_v1", session.c_str(), join.c_str(),
                  static_cast<unsigned long long>(GetTickCount64()));
    const std::wstring tmp = mod_dir + L"\\lab-status.json.tmp";
    const std::wstring dst = mod_dir + L"\\lab-status.json";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, json, static_cast<DWORD>(std::strlen(json)), &written, nullptr);
    CloseHandle(f);
    MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING);
}

void SetSession(const char* value) {
    session = value;
    Append(std::string("session_status=") + value);
    Status();
}

bool ReadIni(const wchar_t* file, void (*apply)(const std::string&, const std::string&)) {
    const std::wstring path = mod_dir + L"\\" + file;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
        const auto eq = s.find('=');
        if (eq == std::string::npos || s.empty() || s[0] == '#') continue;
        apply(s.substr(0, eq), s.substr(eq + 1));
    }
    std::fclose(f);
    return true;
}

void ApplyLab(const std::string& key, const std::string& value) {
    if (key == "role") role = value;
    else if (key == "listen_port") listen_port = std::atoi(value.c_str());
    else if (key == "peer_port") peer_port = std::atoi(value.c_str());
    else if (key == "save_dir") save_dir = value;
    else if (key == "self_steamid") self_steamid = value;
    else if (key == "mutex_suffix") mutex_suffix = value;
}

void ApplyCoop(const std::string& key, const std::string& value) {
    if (key == "master_url") master_url = value;
}

// What the lab loader would log at DllMain / DirectInput8Create time.
void WriteEarlyEvidence() {
    AppendTo(L"early.log", "early=start; version=fake; lab=1; role=" + role);
    AppendTo(L"early.log", mutex_suffix.empty() ? std::string("mutex=unchanged") : "mutex=renamed; suffix=" + mutex_suffix);
    if (role == "guest" && !Unsafe("identity")) AppendTo(L"early.log", "identity=fake; steamid=" + self_steamid);
    else AppendTo(L"early.log", "identity=real");
    AppendTo(L"early.log", Unsafe("saves") ? std::string("saves=unchanged") : "saves=redirected; dir=" + save_dir);
    AppendTo(L"early.log", "xinput=passthrough");
    AppendTo(L"probe.log", "DirectInput8Create_hresult=0x00000000");
    if (Unsafe("master")) AppendTo(L"steam-observer.log", "master_server=refused; reason=instruction_bytes");
    else AppendTo(L"steam-observer.log", "master_server=redirected; method=in_place; url=" + master_url);
    if (role == "host") {
        const std::string id = Env("MGS_COOP_FAKE_HOST_STEAMID");
        if (!id.empty()) {
            const std::wstring path = mod_dir + L"\\host-steamid.txt";
            HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f != INVALID_HANDLE_VALUE) {
                const std::string text = id + "\n";
                DWORD written = 0;
                WriteFile(f, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
                CloseHandle(f);
            }
        }
    }
}

// One "login" to the fake fuse named by coop.ini (http://127.0.0.1:<port>/tppstm/gate).
void LoginToFuse() {
    const char prefix[] = "http://127.0.0.1:";
    if (master_url.compare(0, sizeof(prefix) - 1, prefix) != 0) return;
    const int port = std::atoi(master_url.c_str() + sizeof(prefix) - 1);
    if (port <= 0) return;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return;
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(static_cast<u_short>(port));
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(s, reinterpret_cast<sockaddr*>(&to), sizeof(to)) == 0) {
        std::string id = role == "guest" ? self_steamid : Env("MGS_COOP_FAKE_HOST_STEAMID");
        if (id.empty()) id = "76561197960265729";
        id += "\n";
        send(s, id.data(), static_cast<int>(id.size()), 0);
        shutdown(s, SD_SEND);
        char sink[16];
        while (recv(s, sink, sizeof(sink), 0) > 0) {}
    }
    closesocket(s);
}

void SendTo(int port, const char* text) {
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(static_cast<u_short>(port));
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sendto(udp, text, static_cast<int>(std::strlen(text)), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
}

bool Receive(char* buffer, int size) {
    sockaddr_in from{};
    int from_size = sizeof(from);
    const int n = recvfrom(udp, buffer, size - 1, 0, reinterpret_cast<sockaddr*>(&from), &from_size);
    if (n <= 0) return false;
    buffer[n] = 0;
    return true;
}

void Tick() {
    char buffer[64];
    char line[160];
    if (role == "host") {
        if (step == 0) {
            Append("mode=host_v3; peer_acceptance=after_mission_start; max_players=2");
            std::snprintf(line, sizeof(line), "install_result=0 installed=1 pid=%lu thread=1 tick_ms=1 target_rva=0x1a178e0 lifetime=process",
                          GetCurrentProcessId());
            Append(line);
            Append("dynamite_host=returned; result=1 thread=1 tick_ms=1 peer_acceptance_requested=0");
            Append("host_admission=authorized; owned_host=1 same_thread=1 lua_interface=SessionEnableAccept");
            Append("dynamite_session=observed; owner=0000000000000001 session=0000000000000002 manager=0000000000000003 job=0000000000000004 "
                   "session_read=1 state=3 accept=1 manager_heap=1 job_heap=1 consistent=1 calls=0 writes=0 atomic=0");
            SetSession("host_ready");
            step = 1;
        } else if (step == 1 && Receive(buffer, sizeof(buffer)) && std::strcmp(buffer, "HELLO") == 0) {
            SendTo(peer_port, "WELCOME");
            SetSession("host_peer_present");
            step = 2;
        }
        return;
    }
    const std::wstring trigger = mod_dir + L"\\join.trigger";
    if (step == 0) {
        Append("mode=client_v1; configured_host=1; join=ctrl_f8_after_mission_start; max_players=2; connected=unverified");
        std::snprintf(line, sizeof(line), "install_result=0 installed=1 pid=%lu thread=1 tick_ms=1 target_rva=0x1a178e0 lifetime=process",
                      GetCurrentProcessId());
        Append(line);
        join = "armed";
        Append("client_join=armed; source=trigger_file");
        Status();
        step = 1;
    } else if (step == 1 && GetFileAttributesW(trigger.c_str()) != INVALID_FILE_ATTRIBUTES) {
        DeleteFileW(trigger.c_str());
        join = "requested";
        Append("client_join=input_received; key=trigger_file");
        Append("dynamite_client=returned; request_started=1");
        SetSession("client_requested");
        step = 2;
    } else if (step >= 2 && step < 4) {
        SendTo(peer_port, "HELLO");
        step = 3;
        if (Receive(buffer, sizeof(buffer)) && std::strcmp(buffer, "WELCOME") == 0) {
            join = "established";
            SetSession("client_established");
            step = 4;
        }
    }
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_TIMER: Tick(); return 0;
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProcW(window, message, w, l);
    }
}

int RunGame() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    dir = dir.substr(0, dir.find_last_of(L'\\'));
    mod_dir = dir + L"\\mgs-coop";
    // Only runs inside a prepared lab fixture.
    if (!ReadIni(L"lab.ini", &ApplyLab) || (role != "host" && role != "guest") || listen_port <= 0 || peer_port <= 0) return 3;
    ReadIni(L"coop.ini", &ApplyCoop);
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 4;
    WriteEarlyEvidence();
    LoginToFuse();
    udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(static_cast<u_short>(listen_port));
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (udp == INVALID_SOCKET || bind(udp, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) return 5;
    u_long nonblocking = 1;
    ioctlsocket(udp, FIONBIO, &nonblocking);
    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MgsCoopLabFixture";
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    const std::wstring title = L"MGS Co-op lab fixture " + Widen(role);
    HWND window = CreateWindowExW(WS_EX_NOACTIVATE, wc.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
                                  -6000, -6000, 320, 200, nullptr, nullptr, wc.hInstance, nullptr);
    if (!window) return 6;
    ShowWindow(window, SW_SHOWNOACTIVATE);
    SetTimer(window, 1, 100, nullptr);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    closesocket(udp);
    WSACleanup();
    return 0;
}

void WriteRequestFile(const char* dir, unsigned long long id, const char* message) {
    char file[200];
    std::snprintf(file, sizeof(file), "%s\\%llu_%s_request", dir, id, message);
    FILE* f = nullptr;
    if (fopen_s(&f, file, "wb") == 0 && f) { std::fputs("{}", f); std::fclose(f); }
}

int RunFuse(int argc, char** argv) {
    int port = 0;
    bool write_log = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-listenAddr") == 0 && i + 1 < argc && std::strncmp(argv[i + 1], "127.0.0.1:", 10) == 0)
            port = std::atoi(argv[i + 1] + 10);
        if (std::strcmp(argv[i], "-writeLog") == 0) write_log = true;
    }
    if (port <= 0) return 2;
    const long long started = static_cast<long long>(std::time(nullptr));
    char dir[64];
    std::snprintf(dir, sizeof(dir), "log\\%lld", started);
    if (write_log) {
        CreateDirectoryA("log", nullptr);
        CreateDirectoryA(dir, nullptr);
    }
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 4;
    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(static_cast<u_short>(port));
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (server == INVALID_SOCKET || bind(server, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0 || listen(server, 8) != 0) return 5;
    std::printf("time=0 level=INFO msg=starting listen=127.0.0.1:%d\n", port);
    std::fflush(stdout);
    unsigned long long sequence = static_cast<unsigned long long>(started) * 1000ull;
    for (;;) {
        SOCKET client = accept(server, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;
        char text[64]{};
        int used = 0;
        for (;;) {
            const int n = recv(client, text + used, static_cast<int>(sizeof(text)) - 1 - used, 0);
            if (n <= 0) break;
            used += n;
            if (used >= static_cast<int>(sizeof(text)) - 1) break;
        }
        closesocket(client);
        if (used == 0) continue;  // a readiness probe (Start-Lab's port check), not a login
        std::string id(text, static_cast<std::size_t>(used));
        while (!id.empty() && (id.back() == '\n' || id.back() == '\r')) id.pop_back();
        if (write_log) {
            WriteRequestFile(dir, ++sequence, "CMD_GET_URLLIST");
            WriteRequestFile(dir, ++sequence, "CMD_AUTH_STEAMTICKET");
        }
        std::printf("time=0 level=INFO msg=\"client request handled\" type=CMD_GET_URLLIST ip=127.0.0.1:1\n");
        std::printf("time=0 level=INFO msg=CMD_AUTH_STEAMTICKET steamID=%s\n", id.c_str());
        std::printf("time=0 level=INFO msg=\"client request handled\" type=CMD_AUTH_STEAMTICKET ip=127.0.0.1:1\n");
        std::printf("time=0 level=INFO msg=CMD_CREATE_PLAYER player_name=\"Fixture Player\"\n");
        std::fflush(stdout);
    }
}
} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-standalone") == 0) return RunFuse(argc, argv);
    return RunGame();
}
