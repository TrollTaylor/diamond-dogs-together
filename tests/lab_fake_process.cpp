// Stand-in processes for the lab script end-to-end test (tests/lab_scripts_tests.ps1).
// Never the game and never fuse:
//  - with "-standalone ... -listenAddr 127.0.0.1:<port>" it plays fuse: listens on
//    that loopback TCP port, writes one fake -writeLog request file, runs until killed;
//  - otherwise it plays a lab game instance (copied as <fixture>\mgsvtpp.exe): it
//    refuses to run unless <exeDir>\mgs-coop\lab.ini exists, shows an off-screen
//    window, writes the docs/lab-protocol.md log lines and lab-status.json for its
//    role, consumes join.trigger (guest) and exchanges one UDP datagram over the
//    configured loopback ports, and exits on WM_CLOSE.
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
int listen_port = 0, peer_port = 0;
SOCKET udp = INVALID_SOCKET;
int step = 0;
std::string session = "none";
std::string join = "idle";

std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

void Append(const char* line) {
    const std::wstring path = mod_dir + L"\\script-observer.log";
    HANDLE f = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, line, static_cast<DWORD>(std::strlen(line)), &written, nullptr);
    WriteFile(f, "\r\n", 2, &written, nullptr);
    CloseHandle(f);
}

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
    std::string line = std::string("session_status=") + value;
    Append(line.c_str());
    Status();
}

bool ReadLabIni() {
    const std::wstring path = mod_dir + L"\\lab.ini";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
        const auto eq = s.find('=');
        if (eq == std::string::npos || s[0] == '#') continue;
        const std::string key = s.substr(0, eq), value = s.substr(eq + 1);
        if (key == "role") role = value;
        else if (key == "listen_port") listen_port = std::atoi(value.c_str());
        else if (key == "peer_port") peer_port = std::atoi(value.c_str());
    }
    std::fclose(f);
    return (role == "host" || role == "guest") && listen_port > 0 && peer_port > 0;
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
    if (role == "host") {
        if (step == 0) {
            Append("mode=host_v3; peer_acceptance=after_mission_start; max_players=2");
            Append("dynamite_host=returned; result=1 thread=1 tick_ms=1 peer_acceptance_requested=0");
            Append("host_admission=authorized; owned_host=1 same_thread=1 lua_interface=SessionEnableAccept");
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
        Append("mode=client_v1; configured_host=1");
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
    if (!ReadLabIni()) return 3;  // only runs inside a prepared lab fixture
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 4;
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

int RunFuse(int argc, char** argv) {
    int port = 0;
    bool write_log = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-listenAddr") == 0 && i + 1 < argc && std::strncmp(argv[i + 1], "127.0.0.1:", 10) == 0)
            port = std::atoi(argv[i + 1] + 10);
        if (std::strcmp(argv[i], "-writeLog") == 0) write_log = true;
    }
    if (port <= 0) return 2;
    if (write_log) {
        const long long now = static_cast<long long>(std::time(nullptr));
        char dir[64], file[160];
        std::snprintf(dir, sizeof(dir), "log\\%lld", now);
        CreateDirectoryA("log", nullptr);
        CreateDirectoryA(dir, nullptr);
        std::snprintf(file, sizeof(file), "%s\\%lld000_CMD_GET_URLLIST_request", dir, now);
        FILE* f = nullptr;
        if (fopen_s(&f, file, "wb") == 0 && f) { std::fputs("{}", f); std::fclose(f); }
    }
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 4;
    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(static_cast<u_short>(port));
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (server == INVALID_SOCKET || bind(server, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0 || listen(server, 8) != 0) return 5;
    std::printf("fake fuse listening on 127.0.0.1:%d\n", port);
    std::fflush(stdout);
    for (;;) {
        SOCKET client = accept(server, nullptr, nullptr);
        if (client != INVALID_SOCKET) closesocket(client);
    }
}
} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-standalone") == 0) return RunFuse(argc, argv);
    return RunGame();
}
