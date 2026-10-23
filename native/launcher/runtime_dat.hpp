// Project-authored. Builds mgs-coop\mgs-coop-player-runtime.dat from the
// player's own game archives (docs/runtime-archive-builder.md). The result
// must be byte-exact: the loader (native/archive_mount.cpp) refuses anything
// else.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ddt {

constexpr char kRuntimeDatSha256[] = "87bbe82bb1e907cf405200155da2794bf7e1f5a3d45bf66426a38cdd31a91f96";
constexpr std::uint64_t kRuntimeDatSize = 16333824;

// progress(fraction 0..1, short stage text). May be empty. Called on the
// caller's thread.
using RuntimeDatProgress = std::function<void(float, const std::string&)>;

// Reads only the needed entries from <gameDir>\master\chunk0/2/3/4.dat,
// checks every source against its pinned SHA-256, applies the transforms,
// adds the embedded entries, checks every output entry and the whole archive,
// writes a temporary file next to `outPath`, re-hashes it from disk, then
// moves it into place (replacing an existing file). The parent folder of
// `outPath` must exist. On failure `error` names the failing source or step
// and nothing is left at `outPath` that was not there before.
bool BuildRuntimeDat(const std::wstring& gameDir, const std::wstring& outPath, RuntimeDatProgress progress,
                     std::string& error);

// Same, but returns the archive bytes instead of writing a file.
bool BuildRuntimeDatBytes(const std::wstring& gameDir, std::vector<std::uint8_t>& out, const RuntimeDatProgress& progress,
                          std::string& error);

// True when `path` exists with the pinned size and SHA-256.
bool VerifyRuntimeDat(const std::wstring& path, std::string& error);

// Self-check of the data compiled into the launcher (the two controller
// packages and the archive probe Lua) against their pinned SHA-256.
bool CheckEmbeddedRuntimeAssets(std::string& error);

}  // namespace ddt
