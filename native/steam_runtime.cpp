#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#if !defined(_WIN64)
#error The loaded MGSV Steam runtime adapter supports Windows x64 only.
#endif
#endif

#include "mgscoop/steam_runtime.hpp"

#if defined(_WIN32)
#include <algorithm>
#include <array>
#include <cwchar>
#endif

namespace mgscoop {
namespace {

SteamRuntimeSnapshot gated_snapshot(SteamLifecycle lifecycle) noexcept {
    SteamRuntimeSnapshot result;
    if (lifecycle == SteamLifecycle::ShuttingDown) {
        result.state = SteamRuntimeState::ShutdownInProgress;
    }
    return result;
}

template <typename Getter>
SteamInterfaceObservation query_interface(const SteamExportResolver& resolver, const char* name) noexcept {
    const auto address = resolver.resolve(resolver.context, name);
    if (!address) return SteamInterfaceObservation::ExportMissing;
    const auto getter = reinterpret_cast<Getter>(address);
    return getter() ? SteamInterfaceObservation::Available : SteamInterfaceObservation::ReturnedNull;
}

#if defined(_WIN32)
struct PreserveCallerLastError {
    DWORD value = GetLastError();
    ~PreserveCallerLastError() { SetLastError(value); }
};

SteamExport resolve_loaded_export(void* context, const char* name) noexcept {
    return reinterpret_cast<SteamExport>(GetProcAddress(static_cast<HMODULE>(context), name));
}

bool steam_module_is_beside_executable(HMODULE module) noexcept {
    constexpr DWORD capacity = 32768;
    std::array<wchar_t, capacity> actual{};
    std::array<wchar_t, capacity> expected{};
    const DWORD actual_length = GetModuleFileNameW(module, actual.data(), capacity);
    const DWORD exe_length = GetModuleFileNameW(nullptr, expected.data(), capacity);
    if (actual_length == 0 || actual_length >= capacity || exe_length == 0 || exe_length >= capacity) {
        return false;
    }
    wchar_t* separator = std::wcsrchr(expected.data(), L'\\');
    if (!separator) return false;
    constexpr wchar_t filename[] = L"steam_api64.dll";
    constexpr std::size_t filename_length = sizeof(filename) / sizeof(filename[0]);
    const auto prefix_length = static_cast<std::size_t>(separator + 1 - expected.data());
    if (filename_length > expected.size() - prefix_length) return false;
    std::copy(filename, filename + filename_length, separator + 1);
    return lstrcmpiW(actual.data(), expected.data()) == 0;
}
#endif

} // namespace

SteamRuntimeSnapshot inspect_steam_runtime(const SteamExportResolver& resolver,
                                          SteamLifecycle lifecycle) noexcept {
    // Valve requires successful SteamAPI_Init before using its other functions:
    // https://partner.steamgames.com/doc/sdk/api#initialization_and_shutdown
    // The game owns that call. Handle reads are gated too; they do not replace
    // external lifecycle evidence or serialize against a concurrent shutdown.
    if (lifecycle != SteamLifecycle::EngineInitialized) return gated_snapshot(lifecycle);

    SteamRuntimeSnapshot result;
    result.module = resolver.module_loaded ? SteamModuleObservation::Loaded : SteamModuleObservation::NotLoaded;
    if (!resolver.module_loaded) {
        result.state = SteamRuntimeState::ModuleNotLoaded;
        return result;
    }
    if (!resolver.resolve) {
        result.state = SteamRuntimeState::InvalidResolver;
        return result;
    }

    const auto user_address = resolver.resolve(resolver.context, "SteamAPI_GetHSteamUser");
    const auto pipe_address = resolver.resolve(resolver.context, "SteamAPI_GetHSteamPipe");
    result.user_handle_export = user_address != nullptr;
    result.pipe_handle_export = pipe_address != nullptr;
    if (!user_address || !pipe_address) {
        result.state = SteamRuntimeState::MissingHandleAccessors;
        return result;
    }

    const auto user_getter = reinterpret_cast<SteamHandleGetter>(user_address);
    const auto pipe_getter = reinterpret_cast<SteamHandleGetter>(pipe_address);
    result.user_handle_nonzero = user_getter() != 0;
    result.pipe_handle_nonzero = pipe_getter() != 0;
    result.handles_queried = true;
    if (!result.user_handle_nonzero || !result.pipe_handle_nonzero) {
        result.state = SteamRuntimeState::PendingContext;
        return result;
    }

    result.user = query_interface<SteamUserGetter>(resolver, "SteamUser");
    result.networking = query_interface<SteamNetworkingGetter>(resolver, "SteamNetworking");
    result.matchmaking = query_interface<SteamMatchmakingGetter>(resolver, "SteamMatchmaking");
    const bool all_available = result.user == SteamInterfaceObservation::Available &&
        result.networking == SteamInterfaceObservation::Available &&
        result.matchmaking == SteamInterfaceObservation::Available;
    result.state = all_available ? SteamRuntimeState::OpaqueInterfacesAvailable : SteamRuntimeState::PartialInterfaces;
    return result;
}

SteamRuntimeSnapshot inspect_loaded_steam_runtime(SteamLifecycle lifecycle) noexcept {
    if (lifecycle != SteamLifecycle::EngineInitialized) return gated_snapshot(lifecycle);
#if defined(_WIN32)
    const PreserveCallerLastError preserve_error;
    const HMODULE module = GetModuleHandleW(L"steam_api64.dll");
    if (!module) return inspect_steam_runtime({}, lifecycle);
    if (!steam_module_is_beside_executable(module)) {
        SteamRuntimeSnapshot result;
        result.module = SteamModuleObservation::Loaded;
        result.state = SteamRuntimeState::UnexpectedModuleLocation;
        return result;
    }
    return inspect_steam_runtime({true, module, resolve_loaded_export}, lifecycle);
#else
    SteamRuntimeSnapshot result;
    result.state = SteamRuntimeState::UnsupportedPlatform;
    return result;
#endif
}

} // namespace mgscoop
