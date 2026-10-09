#pragma once
// DllMain-early lab patches (co-op single-PC harness; lab-only).
//
// Installed from DllMain(DLL_PROCESS_ATTACH) only when GetLabConfig().present.
// dinput8.dll is a static import of mgsvtpp.exe, so this runs before the exe
// entry point and before the startup mutex check (0x13F9) and first GetSteamID
// (0x143E). Under the loader lock we do IAT pointer writes only: no LoadLibrary,
// no thread creation, no game-function calls. The pure decision helpers are
// exposed for unit testing; the installer needs Windows.
//
// Install order: the isolation patches (SteamNetworking gate, SteamUser /
// SteamFriends / SteamRemoteStorage getter wrappers) go in first; the mutex is
// renamed only if every isolation patch the configuration requires installed,
// otherwise the second instance exits at 0x1595 exactly as without the lab.
#include <cstddef>
#include <cstdint>

namespace mgscoop::lab {

// The game's single-instance mutex name (verified at 0x20A0980).
inline constexpr wchar_t kMutexTitle[] = L"METAL GEAR SOLID V: THE PHANTOM PAIN";

// Decide the effective mutex name. The suffix (ASCII, from lab.ini) is appended
// only when `requested` exactly equals kMutexTitle; on any other name, an empty
// suffix, or a buffer overflow, returns `requested` unchanged and sets
// *renamed=false. Pure: no Windows calls.
const wchar_t* mutex_effective_name(const wchar_t* requested, const char* suffix,
                                    wchar_t* buf, std::size_t buf_chars, bool& renamed) noexcept;

enum class XInputAction { Passthrough, Remap, Disconnected };
struct XInputRoute {
    XInputAction action;
    unsigned index;  // physical pad to poll for Passthrough/Remap
};

// Map an XInput poll per the lab setting (kXInputPassthrough, kXInputNone, or a
// pad index 0..3). passthrough forwards unchanged; none disconnects every pad;
// pad N exposes only physical pad N as controller 0 and disconnects the rest.
XInputRoute route_xinput(int setting, unsigned requested_index) noexcept;

// What the lab requires before the guest's mutex may be renamed.
struct IsolationState {
    bool second_instance = false;  // mutex_suffix configured
    bool want_identity = false;    // fake self_steamid configured
    bool want_gate = false;        // loopback or fake identity: real networking must be gated
    bool saves_configured = false;
    bool saves_ok = false;
    bool identity_ok = false;
    bool gate_ok = false;
};
// Returns null when the mutex may be renamed, otherwise a short reason
// ("save_dir_required", "save_isolation_failed", "identity_isolation_failed",
// "networking_gate_failed"). A first instance (no suffix) never renames. Pure.
const char* mutex_rename_blocker(const IsolationState& state) noexcept;

// True for an http(s) URL whose host is exactly 127.0.0.1 or localhost
// (optionally with :port), i.e. a local fuse instance. Pure.
bool is_local_master_url(const char* url) noexcept;

// The early.log play line, printed only with lab.ini mode=play right after the
// saves= line: "play=1; loopback=%u; mutex=renamed|unchanged; refused=0x%X;
// saves=redirected|unchanged\r\n". Returns false when it does not fit. Pure.
bool format_play_line(std::uint32_t refused, bool loopback, bool mutex_renamed, bool saves_redirected, char* out,
                      std::size_t capacity) noexcept;

// Save-redirect result of InstallEarlyLabPatches (DllMain): true only when the
// SteamRemoteStorage/SteamUser wrappers for lab.ini save_dir verifiably
// installed. False before installation, without lab.ini and on any refusal.
// The observer's play gate: mode=play runs co-op only on redirected saves.
bool SavesRedirected() noexcept;
// Test hook for the pure state behind SavesRedirected() (never called by the loader).
void set_saves_redirected_for_test(bool value) noexcept;
// This loader's version string (MGS_COOP_PROBE_VERSION; the link HELLO build).
const char* ProbeVersion() noexcept;

// ---- Steam online callbacks (docs/steam-online.md 6.1). The SteamAPI_Init IAT
// slot (0x208E130) gets a wrapper only for mode=play, net=1 and
// net_transport=steam: it calls the original Init on the main thread at 0x1428
// (before the game creates any thread that runs callbacks), returns its result
// unchanged and, only when that is true, registers our static 1251 (host) or
// 337 (guest) callback object once. Never unregistered.
// saves: the save redirect is in place (play refuses everything without it,
// so no callback is registered then either).
struct LabConfigView {
    bool present, play, net, steam, host, saves;
};
bool WantInitWrapper(const LabConfigView& lab) noexcept;
// Pins of the shipped steam_api64 image (TimeDateStamp/SizeOfImage,
// SteamAPI_Init 0x5420, RegisterCallback 0x5530 and its 0x2B10 body, the
// dispatch bytes 0x2DE0). nullptr when they match, else the early.log reason.
const char* InitWrapperRefusal(const std::uint8_t* steam_base, std::size_t span) noexcept;
using SteamInitFn = bool (*)();
using SteamRegisterFn = void (*)(void* callback, int id);
struct InitWrapperState {
    bool called, result, registered;
    unsigned calls;
};
// The wrapper body: calls `init`, registers (object, id) only on a true result
// and only the first time; returns init's result unchanged.
bool RunInitWrapper(SteamInitFn init, SteamRegisterFn reg, void* object, int id, InitWrapperState& state);
// True when DllMain installed the wrapper (steam_observer then leaves the slot).
bool InitWrapperInstalled() noexcept;
InitWrapperState InitWrapperResult() noexcept;

#ifdef _WIN32
// Install the lab's DllMain-early patches against the running mgsvtpp.exe. Gated
// on the exe TimeDateStamp/SizeOfImage and the exact code bytes at 0x13D6/0x1415
// (plus the title, the SteamUser getter site and the XInput thunk), and, for the
// Steam getter wrappers, on the steam_api64 TimeDateStamp/SizeOfImage and the
// exact getter export bytes; refuses (logging a reason to early.log) on any
// mismatch. A no-op when lab is absent, so default behaviour with no lab.ini is
// byte-for-byte unchanged.
void InstallEarlyLabPatches() noexcept;

// True when the SteamNetworking IAT slot (0x208E138) holds the lab gate.
bool networking_gate_installed() noexcept;

// Re-read every IAT slot InstallEarlyLabPatches wrote and compare it with the
// wrapper it installed. Logs "early=verified; slots=N" or one
// "early=lost; slot=<name>" line per lost slot to early.log and to `log` (if
// any). Returns the number of lost slots. Called from steam_observer after its
// full SHA-256 checks of the exe and steam_api64.
unsigned VerifyEarlyLabPatches(void (*log)(const char* line) noexcept) noexcept;
#endif

}  // namespace mgscoop::lab
