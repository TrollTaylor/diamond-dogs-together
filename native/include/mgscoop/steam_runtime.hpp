#pragma once

#include <cstdint>

// Opaque official interface names only. Their layout and methods are intentionally
// undefined: querying an accessor never authorizes a guessed vtable call.
class ISteamUser;
class ISteamNetworking;
class ISteamMatchmaking;

#if defined(_WIN32)
#define MGSCOOP_STEAM_CALL __cdecl
#else
#define MGSCOOP_STEAM_CALL
#endif

namespace mgscoop {

// These are the original, no-argument exported accessor signatures. HSteamUser
// and HSteamPipe are int32 handles, not Steam account IDs.
// Sources checked 2026-09-09:
// https://partner.steamgames.com/doc/api/ISteamClient#typedefs
// https://github.com/ValveSoftware/Proton/blob/proton_11.0/lsteamclient/steamworks_sdk_133/steam_api.h
using SteamHandleGetter = std::int32_t(MGSCOOP_STEAM_CALL*)();
using SteamUserGetter = ISteamUser*(MGSCOOP_STEAM_CALL*)();
using SteamNetworkingGetter = ISteamNetworking*(MGSCOOP_STEAM_CALL*)();
using SteamMatchmakingGetter = ISteamMatchmaking*(MGSCOOP_STEAM_CALL*)();
using SteamExport = void(MGSCOOP_STEAM_CALL*)();

enum class SteamLifecycle {
    Unconfirmed,
    // The caller has independently observed successful initialization and is
    // executing on the engine's serialized Steam lifecycle thread. No concurrent
    // initialization, shutdown, or module unload may occur during inspection.
    EngineInitialized,
    ShuttingDown
};

enum class SteamModuleObservation { NotInspected, NotLoaded, Loaded };
enum class SteamInterfaceObservation { NotQueried, ExportMissing, ReturnedNull, Available };
enum class SteamRuntimeState {
    PendingLifecycle,
    ShutdownInProgress,
    ModuleNotLoaded,
    InvalidResolver,
    MissingHandleAccessors,
    PendingContext,
    PartialInterfaces,
    OpaqueInterfacesAvailable,
    UnsupportedPlatform,
    UnexpectedModuleLocation
};

struct SteamRuntimeSnapshot {
    SteamRuntimeState state = SteamRuntimeState::PendingLifecycle;
    SteamModuleObservation module = SteamModuleObservation::NotInspected;
    bool user_handle_export = false;
    bool pipe_handle_export = false;
    bool handles_queried = false;
    bool user_handle_nonzero = false;
    bool pipe_handle_nonzero = false;
    SteamInterfaceObservation user = SteamInterfaceObservation::NotQueried;
    SteamInterfaceObservation networking = SteamInterfaceObservation::NotQueried;
    SteamInterfaceObservation matchmaking = SteamInterfaceObservation::NotQueried;
    // No interface pointers, raw handle values, SteamIDs, or AppIDs are retained.
};

struct SteamExportResolver {
    bool module_loaded = false;
    void* context = nullptr;
    // Must only inspect the already loaded module. It must not load a DLL or
    // initialize Steam. The callback and context are borrowed for this call only.
    SteamExport (*resolve)(void* context, const char* name) noexcept = nullptr;
};

// Performs at most five read-only exported calls. Without EngineInitialized,
// performs zero exported calls and zero resolver calls. Nonzero handles do not
// prove authentication, ownership, online status, or permission to create lobbies.
// There is no cached success: call again at a later verified lifecycle point to
// retry a pending result, or to observe a changed context.
SteamRuntimeSnapshot inspect_steam_runtime(const SteamExportResolver& resolver,
                                          SteamLifecycle lifecycle) noexcept;

// Windows x64 adapter: GetModuleHandle/GetProcAddress only; never LoadLibrary.
// Requires steam_api64.dll already loaded beside the running executable. The
// same explicit lifecycle/thread contract applies. Do not call from DllMain or
// infer EngineInitialized from the first DirectInput call, DLL presence, or time.
SteamRuntimeSnapshot inspect_loaded_steam_runtime(SteamLifecycle lifecycle) noexcept;

} // namespace mgscoop
