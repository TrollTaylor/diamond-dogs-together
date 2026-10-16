#pragma once
// Save-folder and Steam Cloud isolation for the co-op lab (lab-only).
//
// GetUserDataFolder is redirected to <save_dir>\287700\local, and the
// ISteamRemoteStorage012 file slots the game uses (FileWrite/FileRead/
// FileForget/FileDelete/FileExists/GetFileSize/GetFileCount/GetFileNameAndSize)
// are redirected to <save_dir>\remote\. This keeps two same-PC instances from
// sharing the account's real userdata (287700/local and 311340/remote). The
// game writes TPP_GAME_DATA{0,1}, PERSONAL_DATA{0,1}, TPP_CONFIG_DATA{0,1} and
// TPP_GRAPHICS_CONFIG there. Path mapping is pure (unit tested); I/O is Windows.
#include <cstddef>

namespace mgscoop::lab::save {

// <save_dir>\287700\local (rejects empty save_dir or a result that won't fit).
bool format_user_data_folder(const char* save_dir, char* buf, int cub) noexcept;
// <save_dir>\remote\<name>. Rejects a name with a path separator, a drive colon
// or a ".." component, so no path escapes <save_dir>\remote.
bool format_remote_path(const char* save_dir, const char* name, char* out, std::size_t out_size) noexcept;
// True for a file name the redirect accepts (no traversal, no directory parts).
bool is_safe_remote_name(const char* name) noexcept;
// <save_dir>\287700\local\<name> with format_remote_path's name rules (no
// separator, drive colon, "." or ".."), so no path escapes the local folder.
bool format_local_path(const char* save_dir, const char* name, char* out, std::size_t out_size) noexcept;

// Automatic save handoff staging (docs/save-handoff.md; join-fix phase 2
// P2-9): "<save_dir>-handoff" with the save_dir's trailing separators trimmed
// (the game folder's mgs-coop\coop-saves-handoff in play, run\<role>-saves-
// handoff in the worker). Refused for an empty save_dir, a result that does
// not fit, or when the longest path the handoff writes below it (the folder
// plus kHandoffLongestTail) would reach kHandoffPathLimit characters.
inline constexpr char kHandoffSuffix[] = "-handoff";
// The tooling's longest staged name below the folder (FriendCommon's
// FixedPathSuffixes): \<xfer8>.partial\287700\local\TPP_GAME_DATA0.mgs-coop-<32 hex>.tmp
inline constexpr std::size_t kHandoffLongestTail = 91;
inline constexpr std::size_t kHandoffPathLimit = 248;
bool format_handoff_dir(const char* save_dir, char* out, std::size_t out_size) noexcept;

// Longest save_dir (after trimming trailing separators) the lab accepts. The
// game's GetUserDataFolder call site at 0x54CBDE passes a 0x105-byte buffer and
// later appends file names such as TPP_GRAPHICS_CONFIG, so the redirected
// folder ("<save_dir>" + 14 characters) plus a 32-character file name must stay
// below MAX_PATH (260).
constexpr std::size_t kMaxSaveDirChars = 213;
// True when save_dir is non-empty and short enough for every mapped path.
bool save_dir_fits(const char* save_dir) noexcept;

#ifdef _WIN32
// Record the save directory and create <save_dir>\287700\local and
// <save_dir>\remote. Kernel32 file APIs only, so it is DllMain-safe (called
// once from early_patches).
void configure(const char* save_dir) noexcept;
bool configured() noexcept;

// Build a RemoteStorage proxy over `real` that redirects the file slots to
// <save_dir>\remote and forwards everything else, cached per real pointer (the
// exe calls the getter at every save step). Returns `real` unchanged when no
// save_dir is configured or on allocation failure.
void* wrap_remote_storage(void* real) noexcept;

// The configured save_dir (copied under the redirect lock); false (out
// emptied) when no redirect is configured or it does not fit.
bool current_save_dir(char* out, std::size_t out_size) noexcept;

// Host snapshot of a redirected remote slot (save handoff P2-6 slots 4/5):
// <save_dir>\remote\<name> read whole into buf. The redirect's I/O lock is
// taken shared, like the game's own RS_FileRead, while RS_FileWrite and
// RS_FileDelete take it exclusive: a write that arrives during the read waits
// for it (it never fails on a sharing violation) and the read never sees a
// half-written file. False (size 0) when not configured, the name is unsafe,
// the file is missing or larger than cap, or the read fails.
bool read_remote_snapshot(const char* name, void* buf, std::size_t cap, std::size_t& size) noexcept;

void reset_for_test() noexcept;
#endif

}  // namespace mgscoop::lab::save
