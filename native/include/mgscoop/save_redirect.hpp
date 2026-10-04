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

#ifdef _WIN32
// Record the save directory and create <save_dir>\287700\local and
// <save_dir>\remote. Safe to call once outside DllMain.
void configure(const char* save_dir) noexcept;
bool configured() noexcept;

// Build a RemoteStorage proxy over `real` that redirects the file slots to
// <save_dir>\remote and forwards everything else. Returns `real` unchanged when
// no save_dir is configured or on allocation failure.
void* wrap_remote_storage(void* real) noexcept;

void reset_for_test() noexcept;
#endif

}  // namespace mgscoop::lab::save
