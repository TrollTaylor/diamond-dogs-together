// A fixture that binds fixed 127.0.0.1 UDP ports runs one copy at a time per
// machine. CTest RESOURCE_LOCK only orders the tests of one ctest process, and
// the suites of different checkouts may now run at the same time (docs/STATUS.md
// "Developer test loop"), so the fixture holds a named mutex while it runs.
// The previous holder releases the mutex when main returns, before its process
// teardown closes the sockets of its global transports, so the new holder also
// waits until every port of its range binds exclusively again.
// gate (optional): the name of an older, wider lock this one replaced. After its
// own mutex the lock also waits for the gate's mutex, probes its ports while it
// holds it and then lets it go, so a run of the older code (which holds the gate
// for its whole run) is never overlapped, and one that starts later waits for the
// ports bound here through its own probe.
#pragma once

#include <winsock2.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>

namespace mgscoop::test {

class MachinePortLock {
public:
    MachinePortLock(const wchar_t* name, std::uint16_t first_port, std::uint16_t last_port,
                    const wchar_t* gate = nullptr) noexcept
        : handle_(CreateMutexW(nullptr, FALSE, name)) {
        owned_ = Acquire(handle_, name, L"lock");
        const HANDLE gate_handle = gate ? CreateMutexW(nullptr, FALSE, gate) : nullptr;
        const bool gate_owned = gate && Acquire(gate_handle, gate, L"gate");
        WaitPortsFree(first_port, last_port);
        if (gate_owned) ReleaseMutex(gate_handle);
        if (gate_handle) CloseHandle(gate_handle);
    }
    ~MachinePortLock() {
        if (owned_) ReleaseMutex(handle_);
        if (handle_) CloseHandle(handle_);
    }
    MachinePortLock(const MachinePortLock&) = delete;
    MachinePortLock& operator=(const MachinePortLock&) = delete;

private:
    static bool Acquire(HANDLE handle, const wchar_t* name, const wchar_t* what) noexcept {
        bool owned = false;
        if (handle) {
            // An exited holder leaves the mutex abandoned; that still hands it over.
            const DWORD result = WaitForSingleObject(handle, 300000);
            owned = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
        }
        if (!owned) std::fprintf(stderr, "port %ls %ls not acquired within 300 s; running anyway\n", what, name);
        return owned;
    }
    static void WaitPortsFree(std::uint16_t first_port, std::uint16_t last_port) noexcept {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return;
        const ULONGLONG deadline = GetTickCount64() + 30000;
        for (unsigned port = first_port; port <= last_port; ++port) {
            while (!Free(static_cast<std::uint16_t>(port)) && GetTickCount64() < deadline) Sleep(50);
        }
        WSACleanup();
    }
    static bool Free(std::uint16_t port) noexcept {
        const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET) return true;
        BOOL exclusive = TRUE;
        setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        const bool bound = bind(s, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
        closesocket(s);
        return bound;
    }

    HANDLE handle_ = nullptr;
    bool owned_ = false;
};

}  // namespace mgscoop::test
