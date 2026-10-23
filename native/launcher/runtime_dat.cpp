// Project-authored. See runtime_dat.hpp and docs/runtime-archive-builder.md.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "launcher/runtime_dat.hpp"

#include <array>
#include <cstring>

#include "ddt_asset_archive_probe.hpp"
#include "ddt_asset_four_player_controller.hpp"
#include "ddt_asset_scarf_controller.hpp"
#include "launcher/file_io.hpp"
#include "launcher/fox2.hpp"
#include "launcher/fpk.hpp"
#include "launcher/hash.hpp"
#include "launcher/qar.hpp"

namespace ddt {
namespace {

// ---- Pinned sources (Steam MGSV:TPP 1.0.15.4). Hashes are of the plain
// (inflated) entry bytes.
struct Source {
    const char* name;      // file name inside the game's archive, for messages
    const wchar_t* chunk;  // archive under <game>\master
    const char* chunk_name;
    std::uint64_t hash;    // QAR path hash
    std::uint32_t size;
    const char* sha256;
};

enum SourceId { kGameObjFpk, kGameObjFpkd, kFobFpk, kF30050Fpk, kF30050Fpkd, kF30010Fpk, kF30010Fpkd, kF30020Fpk, kF30020Fpkd, kSourceCount };

constexpr Source kSources[kSourceCount] = {
    {"two_players_game_obj.fpk", L"master\\chunk0.dat", "master/chunk0.dat", 0x52292cbcda547cb2ull, 112,
     "60b461fdf13b32b6e35fe67191d3b55e22d6c589468178b7446340a197816293"},
    {"two_players_game_obj.fpkd", L"master\\chunk0.dat", "master/chunk0.dat", 0xed512cbcda547cb2ull, 2400,
     "8c8e557bf2d8ccaec0450b27e6bfd81d53fd02c8e7f62d5d507b8f4b040b1efa"},
    {"mtbs_mission_fob.fpk", L"master\\chunk3.dat", "master/chunk3.dat", 0x5229c558f8a01d7aull, 2407088,
     "f2092c931361235826346509dd82b1a188c7d18787fb432e7a064a60dc96d279"},
    {"f30050.fpk", L"master\\chunk3.dat", "master/chunk3.dat", 0x522b852ee6759072ull, 989168,
     "05f6e682cfc4670269cbc5b29a923dc98de3b0ca7266d8ee010daabce974ed36"},
    {"f30050.fpkd", L"master\\chunk3.dat", "master/chunk3.dat", 0xed53852ee6759072ull, 311088,
     "3b38bdb7cc8d1de21d76fea8064ed097d9164038af548a5d9dc090225fccdb49"},
    {"f30010.fpk", L"master\\chunk2.dat", "master/chunk2.dat", 0x522af337cf10ef1dull, 2848192,
     "97c50bee8885f581b54d86f6b42585509a1393436d6225f1f26e5eb5cd402414"},
    {"f30010.fpkd", L"master\\chunk2.dat", "master/chunk2.dat", 0xed52f337cf10ef1dull, 2566688,
     "1238d2538db5ba44823cf1703cd0efed5b7b44d6fb6c09c2eaf0ac0df2d6efff"},
    {"f30020.fpk", L"master\\chunk4.dat", "master/chunk4.dat", 0x522b11c3eac3c0e2ull, 2674336,
     "c250778c09f56fcf960476fdeed9520e399b8ecb55b8437712dee5774e0a8f5c"},
    {"f30020.fpkd", L"master\\chunk4.dat", "master/chunk4.dat", 0xed5311c3eac3c0e2ull, 2112144,
     "d1dcf1e0fcb174ae4df994f6317ee7e6b6a1420a59b5d3f2916c00c783248cba"},
};

// ---- Output entries, in archive order.
struct Output {
    const char* path;
    std::uint64_t hash;
    std::uint32_t size;
    const char* sha256;
};

constexpr std::size_t kOutputCount = 13;
constexpr Output kOutputs[kOutputCount] = {
    {"/Assets/tpp/pack/player/game_object/mgs_coop_four_players_game_obj.fpk", 0x522b497af5579ed6ull, 112,
     "60b461fdf13b32b6e35fe67191d3b55e22d6c589468178b7446340a197816293"},
    {"/Assets/tpp/pack/player/game_object/mgs_coop_four_players_game_obj.fpkd", 0xed53497af5579ed6ull, 2416,
     "e15273de59af59c2317ac828a20788e5fd7a85007718ce1083baba3503c4ff7c"},
    {"/Assets/tpp/pack/player/game_object/mgs_coop_four_player_controller.fpk", 0x522bc3fac11e6913ull, 2407088,
     "f2092c931361235826346509dd82b1a188c7d18787fb432e7a064a60dc96d279"},
    {"/Assets/tpp/pack/player/game_object/mgs_coop_four_player_controller.fpkd", 0xed53c3fac11e6913ull, 6624,
     "1dd1874ae6c45434b67f377ae1605d98e4c1de7b6be61c8b211287e88b39a01d"},
    {"/Assets/tpp/script/lib/MgsCoopArchiveProbe.lua", 0x18e19c9e359926d7ull, 215,
     "4b3e9234b07238da0b3c45e65234e259701d174a4dc343ef15567ceb9e0b70bd"},
    {"/Assets/tpp/pack/mission2/free/f30050/mgs_coop_f30050.fpk", 0x52292c144a7760acull, 989168,
     "05f6e682cfc4670269cbc5b29a923dc98de3b0ca7266d8ee010daabce974ed36"},
    {"/Assets/tpp/pack/mission2/free/f30050/mgs_coop_f30050.fpkd", 0xed512c144a7760acull, 308880,
     "ed8a84bba3253ecfff6637ba029d7f5ee91907196c0e4ad82d0d99d84e54600f"},
    {"/Assets/tpp/pack/player/game_object/mgs_coop_two_player_scarf_controller.fpk", 0x522806e788ef5ea2ull, 2407088,
     "f2092c931361235826346509dd82b1a188c7d18787fb432e7a064a60dc96d279"},
    {"/Assets/tpp/pack/player/game_object/mgs_coop_two_player_scarf_controller.fpkd", 0xed5006e788ef5ea2ull, 6624,
     "92385107e7b1c32dcc0f9d07a6a149018538252a40c23e4a46cfcf45e0562342"},
    {"/Assets/tpp/pack/mission2/free/f30010/mgs_coop_f30010.fpk", 0x522a29a15d0bd10dull, 2848192,
     "97c50bee8885f581b54d86f6b42585509a1393436d6225f1f26e5eb5cd402414"},
    {"/Assets/tpp/pack/mission2/free/f30010/mgs_coop_f30010.fpkd", 0xed5229a15d0bd10dull, 2564464,
     "718b0e33875ff668144f70eac93771fac1b4df047f859d6fc81ecc9a4fda8483"},
    {"/Assets/tpp/pack/mission2/free/f30020/mgs_coop_f30020.fpk", 0x522a503e8fa24dedull, 2674336,
     "c250778c09f56fcf960476fdeed9520e399b8ecb55b8437712dee5774e0a8f5c"},
    {"/Assets/tpp/pack/mission2/free/f30020/mgs_coop_f30020.fpkd", 0xed52503e8fa24dedull, 2109952,
     "db61ba492ffbffd7c89ef3817aa1e97f3719b66b60fbd604fa8b5643f176d55c"},
};

constexpr char kGameObjectDir[] = "/Assets/tpp/level_asset/chara/player/game_object/";
constexpr char kTwoPlayersFox2[] = "two_players_game_obj.fox2";
constexpr char kFourPlayersFox2[] = "mgs_coop_four_players_game_obj.fox2";
constexpr char kSinglePlayer2Fox2[] = "single_player2_instance.fox2";
constexpr char kSinglePl2ScarfFox2[] = "single_pl2_instance_scarf.fox2";

bool DigestEquals(const Sha256Digest& digest, const char* expected_hex) {
    return ToHex(digest) == expected_hex;
}

bool HashMatches(const std::vector<std::uint8_t>& bytes, std::uint32_t size, const char* expected_hex) {
    Sha256Digest digest{};
    return bytes.size() == size && Sha256(bytes.data(), bytes.size(), digest) && DigestEquals(digest, expected_hex);
}

void Report(const RuntimeDatProgress& progress, float fraction, const std::string& text) {
    if (progress) progress(fraction, text);
}

std::string SourceLabel(const Source& s) { return std::string(s.name) + " in " + s.chunk_name; }

constexpr char kVerifyHint[] =
    " This game install does not match the supported Steam version 1.0.15.4, or its files are damaged; "
    "use Steam's \"Verify integrity of game files\" and try again.";

bool ReadSources(const std::wstring& game_dir, std::array<std::vector<std::uint8_t>, kSourceCount>& data,
                 const RuntimeDatProgress& progress, std::string& error) {
    std::uint64_t total = 0, done = 0;
    for (const Source& s : kSources) total += s.size;
    // Group by archive so each chunk file is opened once.
    const wchar_t* chunks[] = {L"master\\chunk0.dat", L"master\\chunk2.dat", L"master\\chunk3.dat", L"master\\chunk4.dat"};
    for (const wchar_t* chunk : chunks) {
        QarReader reader;
        bool opened = false;
        for (std::size_t i = 0; i < kSourceCount; ++i) {
            const Source& s = kSources[i];
            if (std::wcscmp(s.chunk, chunk) != 0) continue;
            Report(progress, 0.8f * static_cast<float>(done) / static_cast<float>(total),
                   std::string("Reading ") + s.name + " from " + s.chunk_name);
            std::string detail;
            if (!opened) {
                const std::wstring path = JoinPath(game_dir, chunk);
                if (!FileExists(path)) {
                    error = std::string("The game archive ") + s.chunk_name + " was not found." + kVerifyHint;
                    return false;
                }
                if (!reader.OpenFile(path, detail)) {
                    error = std::string("The game archive ") + s.chunk_name + " could not be read: " + detail + "." +
                            kVerifyHint;
                    return false;
                }
                opened = true;
            }
            QarEntryInfo info;
            switch (reader.Find(s.hash, info, detail)) {
                case QarReader::Lookup::found:
                    break;
                case QarReader::Lookup::not_found:
                    error = "Source " + SourceLabel(s) + " was not found." + kVerifyHint;
                    return false;
                case QarReader::Lookup::failed:
                    error = "Source " + SourceLabel(s) + " could not be read: " + detail + "." + kVerifyHint;
                    return false;
            }
            if (info.plain_size != s.size) {
                error = "Source " + SourceLabel(s) + " has an unexpected size (" + std::to_string(info.plain_size) +
                        " bytes, expected " + std::to_string(s.size) + ")." + kVerifyHint;
                return false;
            }
            if (!reader.Read(info, data[i], s.size, detail)) {
                error = "Source " + SourceLabel(s) + " could not be read: " + detail + "." + kVerifyHint;
                return false;
            }
            if (!HashMatches(data[i], s.size, s.sha256)) {
                error = "Source " + SourceLabel(s) + " does not match the expected game data (SHA-256 " +
                        std::string(s.sha256).substr(0, 16) + "...)." + kVerifyHint;
                return false;
            }
            done += s.size;
        }
    }
    return true;
}

// Entry 1: the game's two-player object package, renamed, with the FOX2
// string table in first-use order.
bool BuildGameObjectFpkd(const std::vector<std::uint8_t>& source, std::vector<std::uint8_t>& out, std::string& error) {
    FpkFile fpkd;
    if (!ParseFpk(source.data(), source.size(), fpkd, error)) return false;
    if (!fpkd.is_fpkd || fpkd.entries.size() != 1 || !fpkd.references.empty() ||
        fpkd.entries[0].path != std::string(kGameObjectDir) + kTwoPlayersFox2) {
        error = "unexpected package contents";
        return false;
    }
    FpkEntry& entry = fpkd.entries[0];
    std::vector<std::uint8_t> fox2;
    if (!RewriteFox2StringTable(entry.data.data(), entry.data.size(), fox2, error)) return false;
    entry.data = std::move(fox2);
    if (!SetFpkEntryPath(entry, std::string(kGameObjectDir) + kFourPlayersFox2)) {
        error = "MD5 is not available";
        return false;
    }
    return WriteFpk(fpkd, out, error);
}

// Entries 6, 10, 12: a mission FPKD with its single second-player instance removed.
bool BuildMissionFpkd(const std::vector<std::uint8_t>& source, const char* drop_name, std::vector<std::uint8_t>& out,
                      std::string& error) {
    FpkFile fpkd;
    if (!ParseFpk(source.data(), source.size(), fpkd, error)) return false;
    const std::string drop = std::string(kGameObjectDir) + drop_name;
    std::size_t removed = 0;
    std::vector<FpkEntry> kept;
    kept.reserve(fpkd.entries.size());
    for (FpkEntry& entry : fpkd.entries) {
        if (entry.path == drop) {
            ++removed;
            continue;
        }
        kept.push_back(std::move(entry));
    }
    if (!fpkd.is_fpkd || removed != 1) {
        error = "unexpected package contents";
        return false;
    }
    fpkd.entries = std::move(kept);
    return WriteFpk(fpkd, out, error);
}

std::vector<std::uint8_t> Bytes(const unsigned char* data, std::size_t size) {
    return std::vector<std::uint8_t>(data, data + size);
}

}  // namespace

bool BuildRuntimeDatBytes(const std::wstring& gameDir, std::vector<std::uint8_t>& out, const RuntimeDatProgress& progress,
                          std::string& error) {
    out.clear();
    error.clear();
    Report(progress, 0.0f, "Checking the game folder");
    if (gameDir.empty() || !DirectoryExists(JoinPath(gameDir, L"master"))) {
        error = "The game folder has no master folder; choose the folder that holds mgsvtpp.exe.";
        return false;
    }
    std::array<std::vector<std::uint8_t>, kSourceCount> src;
    if (!ReadSources(gameDir, src, progress, error)) return false;

    Report(progress, 0.8f, "Assembling the co-op runtime archive");
    std::vector<QarWriteEntry> entries(kOutputCount);
    for (std::size_t i = 0; i < kOutputCount; ++i) entries[i].hash = kOutputs[i].hash;
    std::string detail;
    entries[0].plain = src[kGameObjFpk];
    if (!BuildGameObjectFpkd(src[kGameObjFpkd], entries[1].plain, detail)) {
        error = "Could not convert " + SourceLabel(kSources[kGameObjFpkd]) + ": " + detail + ".";
        return false;
    }
    entries[2].plain = src[kFobFpk];
    entries[3].plain = Bytes(assets::kFourPlayerControllerFpkd, sizeof(assets::kFourPlayerControllerFpkd));
    entries[4].plain = Bytes(assets::kArchiveProbeLua, sizeof(assets::kArchiveProbeLua));
    entries[5].plain = src[kF30050Fpk];
    if (!BuildMissionFpkd(src[kF30050Fpkd], kSinglePlayer2Fox2, entries[6].plain, detail)) {
        error = "Could not convert " + SourceLabel(kSources[kF30050Fpkd]) + ": " + detail + ".";
        return false;
    }
    entries[7].plain = src[kFobFpk];
    entries[8].plain = Bytes(assets::kScarfControllerFpkd, sizeof(assets::kScarfControllerFpkd));
    entries[9].plain = src[kF30010Fpk];
    if (!BuildMissionFpkd(src[kF30010Fpkd], kSinglePl2ScarfFox2, entries[10].plain, detail)) {
        error = "Could not convert " + SourceLabel(kSources[kF30010Fpkd]) + ": " + detail + ".";
        return false;
    }
    entries[11].plain = src[kF30020Fpk];
    if (!BuildMissionFpkd(src[kF30020Fpkd], kSinglePlayer2Fox2, entries[12].plain, detail)) {
        error = "Could not convert " + SourceLabel(kSources[kF30020Fpkd]) + ": " + detail + ".";
        return false;
    }
    for (std::size_t i = 0; i < kOutputCount; ++i) {
        if (!HashMatches(entries[i].plain, kOutputs[i].size, kOutputs[i].sha256)) {
            const bool embedded = i == 3 || i == 4 || i == 8;
            error = "Archive entry " + std::to_string(i) + " (" + kOutputs[i].path + ") does not match its expected bytes" +
                    (embedded ? "; the launcher's embedded data is damaged, reinstall the mod." : "." + std::string(kVerifyHint));
            return false;
        }
    }
    Report(progress, 0.88f, "Writing the co-op runtime archive");
    if (!WriteQar(entries, kQarRuntimeFlags, out, detail)) {
        error = "Could not assemble the runtime archive: " + detail + ".";
        return false;
    }
    Sha256Digest digest{};
    if (out.size() != kRuntimeDatSize || !Sha256(out.data(), out.size(), digest) || !DigestEquals(digest, kRuntimeDatSha256)) {
        error = "The assembled runtime archive does not match the expected SHA-256.";
        out.clear();
        return false;
    }
    return true;
}

bool BuildRuntimeDat(const std::wstring& gameDir, const std::wstring& outPath, RuntimeDatProgress progress,
                     std::string& error) {
    error.clear();
    if (outPath.empty() || !DirectoryExists(ParentDirectory(outPath))) {
        error = "The output folder does not exist.";
        return false;
    }
    std::vector<std::uint8_t> bytes;
    if (!BuildRuntimeDatBytes(gameDir, bytes, progress, error)) return false;

    Report(progress, 0.92f, "Saving the co-op runtime archive");
    const std::wstring temp = outPath + L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                              std::to_wstring(GetTickCount64());
    std::string detail;
    if (!WriteWholeFile(temp, bytes.data(), bytes.size(), detail)) {
        DeleteFileW(temp.c_str());
        error = "Could not save the runtime archive: " + detail + ".";
        return false;
    }
    Report(progress, 0.96f, "Verifying the saved archive");
    Sha256Digest digest{};
    std::uint64_t size = 0;
    if (!Sha256File(temp, digest, size, detail) || size != kRuntimeDatSize || !DigestEquals(digest, kRuntimeDatSha256)) {
        DeleteFileW(temp.c_str());
        error = "The saved runtime archive did not verify" + (detail.empty() ? std::string(".") : ": " + detail + ".");
        return false;
    }
    if (!MoveFileExW(temp.c_str(), outPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD code = GetLastError();
        DeleteFileW(temp.c_str());
        error = "Could not put the runtime archive in place (" + WindowsErrorText(code) +
                "). Close the game if it is running and try again.";
        return false;
    }
    Report(progress, 1.0f, "Co-op runtime archive ready");
    return true;
}

bool CheckEmbeddedRuntimeAssets(std::string& error) {
    struct Embedded {
        std::size_t output;
        const unsigned char* data;
        std::size_t size;
    };
    const Embedded embedded[] = {
        {3, assets::kFourPlayerControllerFpkd, sizeof(assets::kFourPlayerControllerFpkd)},
        {4, assets::kArchiveProbeLua, sizeof(assets::kArchiveProbeLua)},
        {8, assets::kScarfControllerFpkd, sizeof(assets::kScarfControllerFpkd)},
    };
    for (const Embedded& e : embedded) {
        const Output& o = kOutputs[e.output];
        if (!HashMatches(Bytes(e.data, e.size), o.size, o.sha256)) {
            error = std::string("Embedded data for ") + o.path + " does not match its pinned SHA-256.";
            return false;
        }
    }
    return true;
}

bool VerifyRuntimeDat(const std::wstring& path, std::string& error) {
    Sha256Digest digest{};
    std::uint64_t size = 0;
    std::string detail;
    if (!Sha256File(path, digest, size, detail)) {
        error = "The runtime archive could not be read: " + detail + ".";
        return false;
    }
    if (size != kRuntimeDatSize || !DigestEquals(digest, kRuntimeDatSha256)) {
        error = "The runtime archive does not match the expected SHA-256.";
        return false;
    }
    return true;
}

}  // namespace ddt
