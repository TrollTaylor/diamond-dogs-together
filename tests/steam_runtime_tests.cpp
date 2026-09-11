#include "mgscoop/steam_runtime.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace mgscoop;

struct FakeSteam {
    std::int32_t user_handle = 1;
    std::int32_t pipe_handle = 2;
    bool null_user = false;
    bool null_networking = false;
    bool null_matchmaking = false;
    std::array<bool, 5> missing{};
    std::array<unsigned, 5> calls{};
    std::array<unsigned, 5> resolutions{};
    unsigned unexpected_resolution = 0;
};

FakeSteam* current = nullptr;
int opaque_user_storage = 0;
int opaque_networking_storage = 0;
int opaque_matchmaking_storage = 0;

extern "C" std::int32_t MGSCOOP_STEAM_CALL fake_user_handle() {
    ++current->calls[0];
    return current->user_handle;
}
extern "C" std::int32_t MGSCOOP_STEAM_CALL fake_pipe_handle() {
    ++current->calls[1];
    return current->pipe_handle;
}
extern "C" ISteamUser* MGSCOOP_STEAM_CALL fake_user() {
    ++current->calls[2];
    return current->null_user ? nullptr : reinterpret_cast<ISteamUser*>(&opaque_user_storage);
}
extern "C" ISteamNetworking* MGSCOOP_STEAM_CALL fake_networking() {
    ++current->calls[3];
    return current->null_networking ? nullptr : reinterpret_cast<ISteamNetworking*>(&opaque_networking_storage);
}
extern "C" ISteamMatchmaking* MGSCOOP_STEAM_CALL fake_matchmaking() {
    ++current->calls[4];
    return current->null_matchmaking ? nullptr : reinterpret_cast<ISteamMatchmaking*>(&opaque_matchmaking_storage);
}

SteamExport resolve_fake(void* context, const char* name) noexcept {
    auto& fake = *static_cast<FakeSteam*>(context);
    const char* const names[] = {
        "SteamAPI_GetHSteamUser", "SteamAPI_GetHSteamPipe", "SteamUser", "SteamNetworking", "SteamMatchmaking"
    };
    const SteamExport functions[] = {
        reinterpret_cast<SteamExport>(&fake_user_handle), reinterpret_cast<SteamExport>(&fake_pipe_handle),
        reinterpret_cast<SteamExport>(&fake_user), reinterpret_cast<SteamExport>(&fake_networking),
        reinterpret_cast<SteamExport>(&fake_matchmaking)
    };
    for (std::size_t index = 0; index < fake.resolutions.size(); ++index) {
        if (std::strcmp(name, names[index]) == 0) {
            ++fake.resolutions[index];
            return fake.missing[index] ? nullptr : functions[index];
        }
    }
    ++fake.unexpected_resolution;
    return nullptr;
}

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

unsigned total(const std::array<unsigned, 5>& counters) {
    unsigned count = 0;
    for (unsigned value : counters) count += value;
    return count;
}

SteamRuntimeSnapshot inspect(FakeSteam& fake, SteamLifecycle lifecycle = SteamLifecycle::EngineInitialized,
                             bool module_loaded = true) {
    current = &fake;
    const auto result = inspect_steam_runtime({module_loaded, &fake, resolve_fake}, lifecycle);
    current = nullptr;
    require(fake.unexpected_resolution == 0, "only the five approved read-only accessors can be resolved");
    return result;
}

void explicit_lifecycle_gate() {
    FakeSteam fake;
    auto result = inspect(fake, SteamLifecycle::Unconfirmed);
    require(result.state == SteamRuntimeState::PendingLifecycle, "unconfirmed initialization remains pending");
    require(result.module == SteamModuleObservation::NotInspected, "pending lifecycle does not claim module absence");
    require(!result.handles_queried && total(fake.calls) == 0 && total(fake.resolutions) == 0,
            "no resolver or Steam calls before verified initialization");
    result = inspect(fake, SteamLifecycle::ShuttingDown);
    require(result.state == SteamRuntimeState::ShutdownInProgress, "shutdown blocks context queries");
    require(total(fake.calls) == 0 && total(fake.resolutions) == 0, "shutdown performs no Steam queries");
    result = inspect(fake, static_cast<SteamLifecycle>(99));
    require(result.state == SteamRuntimeState::PendingLifecycle && total(fake.calls) == 0,
            "invalid lifecycle value cannot open the gate");
    result = inspect_loaded_steam_runtime(SteamLifecycle::Unconfirmed);
    require(result.state == SteamRuntimeState::PendingLifecycle && result.module == SteamModuleObservation::NotInspected,
            "native wrapper also gates before module access");
}

void missing_module_or_accessors() {
    FakeSteam fake;
    auto result = inspect(fake, SteamLifecycle::EngineInitialized, false);
    require(result.state == SteamRuntimeState::ModuleNotLoaded && total(fake.resolutions) == 0,
            "missing module stays unavailable without attempted loads or resolution");
    result = inspect_steam_runtime({true, nullptr, nullptr}, SteamLifecycle::EngineInitialized);
    require(result.state == SteamRuntimeState::InvalidResolver, "missing resolver cannot be invoked");
    for (std::size_t index : {0u, 1u}) {
        FakeSteam incomplete;
        incomplete.missing[index] = true;
        result = inspect(incomplete);
        require(result.state == SteamRuntimeState::MissingHandleAccessors, "both handle exports are prerequisites");
        require(!result.handles_queried && total(incomplete.calls) == 0, "partial handle exports cause zero exported calls");
        require(total(incomplete.resolutions) == 2, "interface accessors are not resolved when handles cannot be checked");
    }
}

void zero_handles_and_later_retry() {
    FakeSteam fake;
    fake.user_handle = 0;
    auto result = inspect(fake);
    require(result.state == SteamRuntimeState::PendingContext && result.handles_queried,
            "zero user context remains pending after lifecycle gate");
    require(!result.user_handle_nonzero && result.pipe_handle_nonzero, "handle observations are independent");
    require(fake.calls[2] == 0 && fake.calls[3] == 0 && fake.calls[4] == 0, "zero context blocks opaque interface getters");
    fake.user_handle = 12;
    fake.pipe_handle = 0;
    result = inspect(fake);
    require(result.state == SteamRuntimeState::PendingContext && result.user_handle_nonzero && !result.pipe_handle_nonzero,
            "zero pipe also blocks interface queries");
    fake.pipe_handle = 34;
    result = inspect(fake);
    require(result.state == SteamRuntimeState::OpaqueInterfacesAvailable, "later verified retry can observe ready context");
    require(fake.calls[2] == 1 && fake.calls[3] == 1 && fake.calls[4] == 1, "interfaces called only once context is nonzero");
    fake.user_handle = 0;
    result = inspect(fake);
    require(result.state == SteamRuntimeState::PendingContext, "previous availability is never cached across context loss");
    require(result.user == SteamInterfaceObservation::NotQueried && fake.calls[2] == 1,
            "stale interface availability cannot survive a missing context");
}

void individual_interface_capabilities() {
    FakeSteam fake;
    fake.null_user = true;
    fake.missing[3] = true;
    auto result = inspect(fake);
    require(result.state == SteamRuntimeState::PartialInterfaces, "partial availability does not claim all capabilities");
    require(result.user == SteamInterfaceObservation::ReturnedNull, "null interface differs from missing symbol");
    require(result.networking == SteamInterfaceObservation::ExportMissing && fake.calls[3] == 0,
            "missing networking getter is never invoked");
    require(result.matchmaking == SteamInterfaceObservation::Available, "present opaque capabilities remain reportable");
    fake.null_user = false;
    fake.missing[3] = false;
    fake.null_matchmaking = true;
    result = inspect(fake);
    require(result.matchmaking == SteamInterfaceObservation::ReturnedNull && result.state == SteamRuntimeState::PartialInterfaces,
            "matchmaking null does not claim working lobbies");
    fake.null_matchmaking = false;
    result = inspect(fake);
    require(result.state == SteamRuntimeState::OpaqueInterfacesAvailable, "fresh inspection sees recovered interfaces");
}

void successful_snapshot_is_bounded() {
    FakeSteam fake;
    const auto result = inspect(fake);
    require(result.state == SteamRuntimeState::OpaqueInterfacesAvailable, "five valid getters yield opaque availability only");
    require(result.module == SteamModuleObservation::Loaded && result.handles_queried,
            "successful result records module and handle observation");
    require(result.user_handle_export && result.pipe_handle_export && result.user_handle_nonzero && result.pipe_handle_nonzero,
            "both handle prerequisites reported");
    require(total(fake.calls) == 5 && total(fake.resolutions) == 5, "bounded query makes exactly five read-only calls");
    require(result.user == SteamInterfaceObservation::Available && result.networking == SteamInterfaceObservation::Available &&
                result.matchmaking == SteamInterfaceObservation::Available,
            "all requested opaque getters reported separately");
}
} // namespace

int main() {
    try {
        explicit_lifecycle_gate();
        missing_module_or_accessors();
        zero_handles_and_later_retry();
        individual_interface_capabilities();
        successful_snapshot_is_bounded();
        std::cout << "steam_runtime_tests: all read-only lifecycle and capability checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "steam_runtime_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
