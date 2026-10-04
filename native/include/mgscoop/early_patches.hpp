#pragma once
// DllMain-early lab patches (co-op single-PC harness; lab-only).
//
// Installed from DllMain(DLL_PROCESS_ATTACH) only when GetLabConfig().present.
// dinput8.dll is a static import of mgsvtpp.exe, so this runs before the exe
// entry point and before the startup mutex check (0x13F9) and first GetSteamID
// (0x143E). Under the loader lock we do IAT pointer writes only: no LoadLibrary,
// no thread creation, no game-function calls. The pure decision helpers are
// exposed for unit testing; the installer needs Windows.
#include <cstddef>

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

#ifdef _WIN32
// Install the lab's DllMain-early patches against the running mgsvtpp.exe. Gated
// on the exe TimeDateStamp/SizeOfImage and the exact code bytes at 0x13D6/0x1415
// (plus the title, the SteamUser getter site and the XInput thunk); refuses
// (logging a reason to early.log) on any mismatch. A no-op when lab is absent,
// so default behaviour with no lab.ini is byte-for-byte unchanged.
void InstallEarlyLabPatches() noexcept;
#endif

}  // namespace mgscoop::lab
