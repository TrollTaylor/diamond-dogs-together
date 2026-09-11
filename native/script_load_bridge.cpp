// Project-authored bridge, following the luaL_loadbuffer replacement boundary
// documented in tpp-mod's scripting.cpp. See THIRD_PARTY_NOTICES.md.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include "mgscoop/script_load_bridge.hpp"

namespace mgscoop::script_bridge {
namespace {
SRWLOCK install_lock = SRWLOCK_INIT;
std::atomic<LoadBuffer> original{nullptr};
std::atomic<unsigned> active{0};
LoadBuffer installed_target = nullptr;
Policy installed_policy{};
thread_local bool selecting = false;

struct ActiveCall {
    ActiveCall() { active.fetch_add(1, std::memory_order_relaxed); }
    ~ActiveCall() { active.fetch_sub(1, std::memory_order_release); }
};

int Bridge(void* state, const char* buffer, std::size_t size, const char* name) {
    const DWORD entry_error = GetLastError();
    ActiveCall call;
    const auto invoke = original.load(std::memory_order_acquire);
    Replacement replacement{};
    bool replaced = false;
    // Selector reentry forwards unchanged; each natural nested load may still
    // select after the outer selector has returned. Never touch the Lua stack.
    if (!selecting && installed_policy.select) {
        selecting = true;
        const bool selected = installed_policy.select(installed_policy.context, buffer, size, name, replacement);
        selecting = false;
        replaced = selected && replacement.bytes && replacement.size <= max_replacement_bytes;
    }
    SetLastError(entry_error);
    const int result = invoke(state, replaced ? replacement.bytes : buffer,
                              replaced ? replacement.size : size, name);
    const DWORD result_error = GetLastError();
    if (!selecting && installed_policy.observe) {
        selecting = true;
        installed_policy.observe(installed_policy.context, result, replaced);
        selecting = false;
    }
    SetLastError(result_error);
    return result;
}

bool ExecutableBytesMatch(LoadBuffer target, const std::uint8_t* expected, std::size_t count) noexcept {
    MEMORY_BASIC_INFORMATION memory{};
    auto address = reinterpret_cast<const void*>(target);
    if (!VirtualQuery(address, &memory, sizeof(memory)) || memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const DWORD protection = memory.Protect & 0xff;
    if (protection != PAGE_EXECUTE_READ && protection != PAGE_EXECUTE_READWRITE &&
        protection != PAGE_EXECUTE_WRITECOPY) return false;
    const auto offset = reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    if (offset > memory.RegionSize || count > memory.RegionSize - offset) return false;
    return std::memcmp(address, expected, count) == 0;
}
}

InstallResult Install(LoadBuffer target, const std::uint8_t* expected, std::size_t count, Policy policy) noexcept {
    if (!target || !expected || count < 14 || count > 64) return InstallResult::invalid;
    AcquireSRWLockExclusive(&install_lock);
    InstallResult result = InstallResult::hook_error;
    if (installed_target) result = InstallResult::already_installed;
    else if (!ExecutableBytesMatch(target, expected, count)) result = InstallResult::bytes_differ;
    else {
        const auto initialized = MH_Initialize();
        if (initialized == MH_OK || initialized == MH_ERROR_ALREADY_INITIALIZED) {
            void* trampoline = nullptr;
            void* address = reinterpret_cast<void*>(target);
            if (MH_CreateHook(address, reinterpret_cast<void*>(&Bridge), &trampoline) == MH_OK) {
                installed_policy = policy;
                installed_target = target;
                original.store(reinterpret_cast<LoadBuffer>(trampoline), std::memory_order_release);
                if (MH_EnableHook(address) == MH_OK) result = InstallResult::installed;
                else if (MH_RemoveHook(address) == MH_OK) {
                    original.store(nullptr, std::memory_order_release);
                    installed_target = nullptr;
                    installed_policy = {};
                }
                // Failed removal keeps ownership latched; do not reuse storage.
            }
        }
    }
    ReleaseSRWLockExclusive(&install_lock);
    return result;
}

bool RemoveAfterCallersStopped() noexcept {
    AcquireSRWLockExclusive(&install_lock);
    bool removed = false;
    if (installed_target && active.load(std::memory_order_acquire) == 0) {
        void* address = reinterpret_cast<void*>(installed_target);
        if (MH_DisableHook(address) == MH_OK && MH_RemoveHook(address) == MH_OK) {
            original.store(nullptr, std::memory_order_release);
            installed_target = nullptr;
            installed_policy = {};
            removed = true;
        }
    }
    ReleaseSRWLockExclusive(&install_lock);
    return removed;
}
}
