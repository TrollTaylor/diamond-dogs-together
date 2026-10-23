// Integration test: builds mgs-coop-player-runtime.dat from a real MGSV:TPP
// install into a temp folder and checks the pinned size and SHA-256, after
// checking that every source package round-trips through ParseFpk/WriteFpk.
// The game folder is read only. MGS_GAME_DIR overrides the folder given on the command
// line; when neither holds the game archives the test is SKIPPED (exit 77).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

#include "launcher/file_io.hpp"
#include "launcher/fpk.hpp"
#include "launcher/hash.hpp"
#include "launcher/qar.hpp"
#include "launcher/runtime_dat.hpp"

namespace {

constexpr int kSkipped = 77;

std::wstring GameFolder(int argc, wchar_t** argv) {
    wchar_t value[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"MGS_GAME_DIR", value, static_cast<DWORD>(std::size(value)));
    if (length > 0 && length < std::size(value)) return value;
    return argc > 1 ? argv[1] : L"";
}

std::size_t CountEntries(const std::wstring& folder) {
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((folder + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return 0;
    std::size_t count = 0;
    do {
        if (std::wstring(data.cFileName) != L"." && std::wstring(data.cFileName) != L"..") ++count;
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return count;
}

// Every source package parses and writes back byte for byte (the FPK writer
// follows the game's own layout, not only the layout of the pinned outputs).
bool SourcePackagesRoundTrip(const std::wstring& game, std::string& error) {
    struct Source {
        const wchar_t* chunk;
        std::uint64_t hash;
    };
    const Source sources[] = {
        {L"master\\chunk0.dat", 0x52292cbcda547cb2ull}, {L"master\\chunk0.dat", 0xed512cbcda547cb2ull},
        {L"master\\chunk3.dat", 0x5229c558f8a01d7aull}, {L"master\\chunk3.dat", 0x522b852ee6759072ull},
        {L"master\\chunk3.dat", 0xed53852ee6759072ull}, {L"master\\chunk2.dat", 0x522af337cf10ef1dull},
        {L"master\\chunk2.dat", 0xed52f337cf10ef1dull}, {L"master\\chunk4.dat", 0x522b11c3eac3c0e2ull},
        {L"master\\chunk4.dat", 0xed5311c3eac3c0e2ull},
    };
    for (const Source& s : sources) {
        ddt::QarReader reader;
        ddt::QarEntryInfo info;
        std::vector<std::uint8_t> plain, rewritten;
        ddt::FpkFile package;
        if (!reader.OpenFile(ddt::JoinPath(game, s.chunk), error) ||
            reader.Find(s.hash, info, error) != ddt::QarReader::Lookup::found || !reader.Read(info, plain, 64u << 20, error) ||
            !ddt::ParseFpk(plain.data(), plain.size(), package, error) || !ddt::WriteFpk(package, rewritten, error) ||
            rewritten != plain) {
            char hash[32];
            std::snprintf(hash, sizeof(hash), "0x%016llx", static_cast<unsigned long long>(s.hash));
            error = std::string("source ") + hash + " did not round-trip: " + error;
            return false;
        }
    }
    return true;
}

long long TimedBuild(const std::wstring& game, const std::wstring& out, std::string& error) {
    const auto start = std::chrono::steady_clock::now();
    float last = -1.0f;
    bool monotonic = true;
    const bool ok = ddt::BuildRuntimeDat(game, out, [&](float fraction, const std::string& text) {
        if (fraction < last) monotonic = false;
        last = fraction;
        std::printf("  [%3d%%] %s\n", static_cast<int>(fraction * 100.0f + 0.5f), text.c_str());
    }, error);
    const long long ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    if (ok && (!monotonic || last != 1.0f)) {
        error = "progress did not run monotonically to 100%";
        return -1;
    }
    return ok ? ms : -1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    const std::wstring game = GameFolder(argc, argv);
    if (game.empty() || !ddt::FileExists(ddt::JoinPath(game, L"master\\chunk0.dat"))) {
        std::printf("SKIPPED: no MGSV:TPP game archives at \"%s\" (set MGS_GAME_DIR to the folder with mgsvtpp.exe)\n",
                    ddt::Utf8FromWide(game).c_str());
        return kSkipped;
    }
    std::string error;
    if (!ddt::CheckEmbeddedRuntimeAssets(error)) {
        std::fprintf(stderr, "FAILED: %s\n", error.c_str());
        return 1;
    }
    if (!SourcePackagesRoundTrip(game, error)) {
        std::fprintf(stderr, "FAILED: %s\n", error.c_str());
        return 1;
    }
    std::printf("all 9 source packages round-trip through ParseFpk/WriteFpk\n");

    wchar_t temp[MAX_PATH + 1]{};
    GetTempPathW(MAX_PATH, temp);
    const std::wstring folder = std::wstring(temp) + L"ddt_runtime_dat_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
                                std::to_wstring(GetTickCount64());
    if (!CreateDirectoryW(folder.c_str(), nullptr)) {
        std::fprintf(stderr, "FAILED: cannot create a temp folder\n");
        return 1;
    }
    const std::wstring out = folder + L"\\mgs-coop-player-runtime.dat";
    int result = 0;

    std::printf("building from %s\n", ddt::Utf8FromWide(game).c_str());
    const long long first = TimedBuild(game, out, error);
    ddt::Sha256Digest digest{};
    std::uint64_t size = 0;
    std::string detail;
    if (first < 0) {
        std::fprintf(stderr, "FAILED: build: %s\n", error.c_str());
        result = 1;
    } else if (!ddt::Sha256File(out, digest, size, detail)) {
        std::fprintf(stderr, "FAILED: cannot read the result: %s\n", detail.c_str());
        result = 1;
    } else {
        std::printf("result: %llu bytes, sha256 %s, built in %lld ms\n", static_cast<unsigned long long>(size),
                    ddt::ToHex(digest).c_str(), first);
        if (size != ddt::kRuntimeDatSize || ddt::ToHex(digest) != ddt::kRuntimeDatSha256) {
            std::fprintf(stderr, "FAILED: expected %llu bytes, sha256 %s\n",
                         static_cast<unsigned long long>(ddt::kRuntimeDatSize), ddt::kRuntimeDatSha256);
            result = 1;
        }
    }
    if (result == 0) {
        // A second build replaces the existing file and leaves no temp file behind.
        const long long second = TimedBuild(game, out, error);
        if (second < 0 || !ddt::VerifyRuntimeDat(out, detail) || CountEntries(folder) != 1) {
            std::fprintf(stderr, "FAILED: rebuild over an existing file: %s %s\n", error.c_str(), detail.c_str());
            result = 1;
        } else {
            std::printf("rebuild over the existing file: ok in %lld ms\n", second);
        }
    }
    DeleteFileW(out.c_str());
    RemoveDirectoryW(folder.c_str());
    if (result == 0) std::printf("ddt_runtime_dat_integration: exact archive reproduced\n");
    return result;
}
