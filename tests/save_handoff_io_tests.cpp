// Join-fix phase 2 save handoff tests (spec track B): the glue's file actions
// (save_handoff::io) against fake save trees in a temporary folder under the
// build folder, the exit window choice with an injected enumerator, and the
// module's install-time gates (Configure reasons, the SAVE receipt replies,
// the travel lock bit). Nothing here touches a real save: every path is below
// <exe folder>\save-handoff-io-<pid>, which the test removes at the end.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include "mgscoop/coop_hud.hpp"
#include "mgscoop/link_crypto.hpp"
#include "mgscoop/save_handoff.hpp"
#include "mgscoop/save_handoff_state.hpp"
#include "mgscoop/save_handoff_wire.hpp"
#include "mgscoop/save_redirect.hpp"

namespace {
namespace fs = std::filesystem;
namespace io = mgscoop::save_handoff::io;
namespace wire = mgscoop::save_handoff_wire;
namespace core = mgscoop::save_handoff_core;
namespace module = mgscoop::save_handoff;

unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) {
        std::fprintf(stderr, "failed check %u: %s\n", checks, what);
        std::exit(1);
    }
}

fs::path g_root;

std::vector<std::uint8_t> Pattern(std::size_t size, unsigned seed) {
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t i = 0; i < size; ++i) bytes[i] = static_cast<std::uint8_t>((i * 13u + seed * 101u + (i >> 9)) & 0xFFu);
    return bytes;
}
void WriteFileBytes(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    FILE* f = nullptr;
    Check(_wfopen_s(&f, path.c_str(), L"wb") == 0 && f != nullptr, "test file opens");
    if (!bytes.empty()) Check(std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size(), "test file written");
    std::fclose(f);
}
std::vector<std::uint8_t> ReadFileBytes(const fs::path& path) {
    std::vector<std::uint8_t> bytes;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return bytes;
    std::uint8_t buffer[65536];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0) bytes.insert(bytes.end(), buffer, buffer + n);
    std::fclose(f);
    return bytes;
}
std::string ReadFileText(const fs::path& path) {
    const auto bytes = ReadFileBytes(path);
    return std::string(bytes.begin(), bytes.end());
}

// The P2-6 slot table, spelled out independently of the wire module.
const char* const kSlotFiles[wire::kSlots] = {"287700\\local\\TPP_GAME_DATA0", "287700\\local\\TPP_GAME_DATA1",
                                              "287700\\local\\PERSONAL_DATA0", "287700\\local\\PERSONAL_DATA1",
                                              "remote\\TPP_GAME_DATA",         "remote\\PERSONAL_DATA"};
// Six fixture files (sizes as on a real lab seed are a few hundred KB; small here).
const std::size_t kSizes[wire::kSlots] = {199916, 199916, 4096, 4096, 100002, 2048};

struct Fixture {
    std::vector<std::uint8_t> files[wire::kSlots];
    std::uint8_t set[32];
};

// set_sha256 per P2-6, computed here with link::Sha256 only: for each present
// slot in ascending order, u8 slot, u32 size LE and the 32-byte file SHA-256.
void ExpectedSet(const Fixture& fixture, std::uint8_t (&out)[32]) {
    mgscoop::link::Sha256 ctx{};
    mgscoop::link::Sha256Init(ctx);
    for (unsigned s = 0; s < wire::kSlots; ++s) {
        if (fixture.files[s].empty()) continue;
        const auto size = static_cast<std::uint32_t>(fixture.files[s].size());
        const std::uint8_t head[5] = {static_cast<std::uint8_t>(s), static_cast<std::uint8_t>(size), static_cast<std::uint8_t>(size >> 8),
                                      static_cast<std::uint8_t>(size >> 16), static_cast<std::uint8_t>(size >> 24)};
        std::uint8_t sha[32]{};
        mgscoop::link::Sha256Digest(fixture.files[s].data(), fixture.files[s].size(), sha);
        mgscoop::link::Sha256Update(ctx, head, sizeof(head));
        mgscoop::link::Sha256Update(ctx, sha, sizeof(sha));
    }
    mgscoop::link::Sha256Final(ctx, out);
}

Fixture MakeSaves(const fs::path& saves, unsigned seed) {
    Fixture fixture{};
    for (unsigned s = 0; s < wire::kSlots; ++s) {
        fixture.files[s] = Pattern(kSizes[s] + seed, seed * 7 + s);
        WriteFileBytes(saves / kSlotFiles[s], fixture.files[s]);
    }
    // Settings files are never part of the set.
    WriteFileBytes(saves / "287700\\local\\TPP_CONFIG_DATA0", Pattern(512, 99));
    WriteFileBytes(saves / "287700\\local\\TPP_GRAPHICS_CONFIG", Pattern(256, 98));
    ExpectedSet(fixture, fixture.set);
    return fixture;
}

bool SameSha(const std::uint8_t* a, const std::uint8_t* b) { return std::memcmp(a, b, 32) == 0; }

// ---------------------------------------------------------------- paths
void Paths(io::Dirs& dirs) {
    const fs::path saves = g_root / "saves";
    const fs::path game = g_root / "game";
    const std::string save_dir = saves.string() + "\\";
    Check(io::MakeDirs(save_dir.c_str(), game.c_str(), dirs), "MakeDirs");
    Check(fs::path(dirs.save) == saves, "save folder without the trailing separator");
    Check(fs::path(dirs.handoff) == fs::path(saves.wstring() + L"-handoff"), "handoff folder is <save_dir>-handoff");
    Check(fs::path(dirs.rejoin) == game / "mgs-coop" / "link-rejoin.txt", "link-rejoin.txt in the game's mgs-coop folder");
    wchar_t path[io::kPathChars]{};
    Check(io::SlotFile(dirs, 0, path, io::kPathChars) && fs::path(path) == saves / "287700" / "local" / "TPP_GAME_DATA0", "slot 0 path");
    Check(io::SlotFile(dirs, 5, path, io::kPathChars) && fs::path(path) == saves / "remote" / "PERSONAL_DATA", "slot 5 path");
    Check(!io::SlotFile(dirs, 6, path, io::kPathChars), "slot 6 has no path");
    Check(io::StageDir(dirs, 0x0A1B2C3Du, true, path, io::kPathChars) && fs::path(path).filename() == fs::path(L"0A1B2C3D.partial"), "partial folder");
    Check(io::StageDir(dirs, 0x0A1B2C3Du, false, path, io::kPathChars) && fs::path(path).filename() == fs::path(L"0A1B2C3D"), "committed folder");
    Check(!io::StageDir(dirs, 0, false, path, io::kPathChars), "xfer 0 has no folder");
    Check(io::StageFile(dirs, 0x0A1B2C3Du, true, 4, path, io::kPathChars) &&
              fs::path(path) == fs::path(dirs.handoff) / "0A1B2C3D.partial" / "remote" / "TPP_GAME_DATA",
          "staged slot path");
    io::Dirs none{};
    Check(io::MakeDirs(save_dir.c_str(), nullptr, none) && none.rejoin[0] == 0, "no game folder, no rejoin file");
    const std::string long_dir = "D:\\" + std::string(200, 'x');
    Check(!io::MakeDirs(long_dir.c_str(), nullptr, none) && none.save[0] == 0, "a save_dir past the 248 budget is refused");
    Check(!io::MakeDirs("", nullptr, none) && !io::MakeDirs(nullptr, nullptr, none), "an empty save_dir is refused");
}

// ---------------------------------------------------------------- hashing
void Hashing(const io::Dirs& dirs, const Fixture& fixture) {
    io::HashCache cache{};
    core::SetInfo info{};
    io::HashJob job{};
    io::HashBegin(job);
    unsigned steps = 0;
    while (!io::HashStep(dirs, job, cache, info) && steps < 20) ++steps;
    Check(steps == 5, "a cold pass reads one file per step (six steps)");
    Check(info.ok && info.stable && !info.unexpected && info.n == 6, "six slots hashed");
    std::uint64_t total = 0;
    for (unsigned i = 0; i < 6; ++i) {
        Check(info.files[i].slot == i && info.files[i].size == fixture.files[i].size(), "ascending slots with sizes");
        std::uint8_t sha[32]{};
        mgscoop::link::Sha256Digest(fixture.files[i].data(), fixture.files[i].size(), sha);
        Check(SameSha(info.files[i].sha256, sha), "file SHA-256");
        total += fixture.files[i].size();
    }
    Check(info.total == total, "total bytes");
    Check(SameSha(info.set, fixture.set), "the set hash equals the P2-6 computation over the six fixture files");
    char hex[wire::kShaHexChars + 1]{};
    wire::FormatShaHex(info.set, hex);
    std::printf("fixture set_sha256 %s\n", hex);
    // A warm pass reads nothing; a changed file is read again alone.
    io::HashBegin(job);
    Check(io::HashStep(dirs, job, cache, info) && info.ok && SameSha(info.set, fixture.set), "a warm pass finishes in one step");
    Fixture changed = fixture;
    changed.files[2] = Pattern(kSizes[2] + 9, 77);
    WriteFileBytes(fs::path(dirs.save) / kSlotFiles[2], changed.files[2]);
    ExpectedSet(changed, changed.set);
    io::HashBegin(job);
    steps = 0;
    while (!io::HashStep(dirs, job, cache, info) && steps < 20) ++steps;
    Check(steps == 1 && info.ok && SameSha(info.set, changed.set), "only the changed file is read again");
    WriteFileBytes(fs::path(dirs.save) / kSlotFiles[2], fixture.files[2]);
    Check(io::HashSetNow(dirs, cache, info) && SameSha(info.set, fixture.set), "restored");

    // Another progress name in either folder: unexpected_files.
    const fs::path extra = fs::path(dirs.save) / "287700\\local\\TPP_GAME_DATA2";
    WriteFileBytes(extra, Pattern(100, 5));
    Check(io::HashSetNow(dirs, cache, info) && info.unexpected && info.ok, "an extra TPP_GAME_DATA2 is unexpected");
    fs::remove(extra);
    const fs::path remote_extra = fs::path(dirs.save) / "remote\\PERSONAL_DATA0";
    WriteFileBytes(remote_extra, Pattern(100, 6));
    Check(io::HashSetNow(dirs, cache, info) && info.unexpected, "a numbered name in remote is unexpected");
    fs::remove(remote_extra);
    WriteFileBytes(fs::path(dirs.save) / "remote\\notes.txt", Pattern(10, 1));
    Check(io::HashSetNow(dirs, cache, info) && !info.unexpected && SameSha(info.set, fixture.set), "other names are ignored");
    fs::remove(fs::path(dirs.save) / "remote\\notes.txt");

    // Empty, oversize and missing primary slots.
    WriteFileBytes(fs::path(dirs.save) / kSlotFiles[3], {});
    Check(io::HashSetNow(dirs, cache, info) && !info.ok, "an empty slot file is not a valid set");
    WriteFileBytes(fs::path(dirs.save) / kSlotFiles[3], Pattern(wire::kMaxFileBytes + 1, 3));
    Check(io::HashSetNow(dirs, cache, info) && info.total > wire::kMaxTotal, "a file over 4 MiB makes the set too large");
    WriteFileBytes(fs::path(dirs.save) / kSlotFiles[3], fixture.files[3]);
    const fs::path s0 = fs::path(dirs.save) / kSlotFiles[0], s1 = fs::path(dirs.save) / kSlotFiles[1];
    fs::rename(s0, s0.wstring() + L".away");
    fs::rename(s1, s1.wstring() + L".away");
    Check(io::HashSetNow(dirs, cache, info) && !info.ok && info.n == 4, "no TPP_GAME_DATA0/1: not a valid set");
    fs::rename(s0.wstring() + L".away", s0);
    Check(io::HashSetNow(dirs, cache, info) && info.ok && info.n == 5 && info.files[0].slot == 0 && info.files[1].slot == 2,
          "slot 0 alone is enough");
    fs::rename(s1.wstring() + L".away", s1);
    Check(io::HashSetNow(dirs, cache, info) && info.ok && SameSha(info.set, fixture.set), "the full set again");

    // Online 2026-10-22 P1: a co-op copy seeded from an old Steam save has only
    // remote\TPP_GAME_DATA and PERSONAL_DATA. For the guest (remote_primary)
    // that is its own set; the host still needs slot 0 or 1.
    for (unsigned s = 0; s < 4; ++s) {
        const fs::path slot = fs::path(dirs.save) / kSlotFiles[s];
        fs::rename(slot, slot.wstring() + L".away");
    }
    io::HashJob job_host{};
    io::HashBegin(job_host);
    while (!io::HashStep(dirs, job_host, cache, info)) {
    }
    Check(!info.ok && info.n == 2 && std::strcmp(io::SetProblem(job_host, info), "no_primary") == 0,
          "remote files only: no set for the host (no_primary)");
    io::HashJob job_guest{};
    io::HashBegin(job_guest);
    while (!io::HashStep(dirs, job_guest, cache, info, true)) {
    }
    Check(info.ok && info.stable && info.n == 2 && info.files[0].slot == 4 && info.files[1].slot == 5 && !io::SetProblem(job_guest, info),
          "remote files only: the guest's own set (slots 4 and 5)");
    std::uint8_t remote_set[32]{};
    wire::FileEntry remote_files[2] = {info.files[0], info.files[1]};
    Check(wire::SetSha256(remote_files, 2, remote_set) && SameSha(info.set, remote_set), "the remote-only set hash is the P2-6 set of slots 4 and 5");
    const fs::path s4 = fs::path(dirs.save) / kSlotFiles[4];
    fs::rename(s4, s4.wstring() + L".away");
    Check(io::HashSetNow(dirs, cache, info, true) && !info.ok && info.n == 1, "PERSONAL_DATA alone is no set, not even for the guest");
    fs::rename(s4.wstring() + L".away", s4);
    WriteFileBytes(fs::path(dirs.save) / kSlotFiles[4], {});
    io::HashJob job_empty{};
    io::HashBegin(job_empty);
    while (!io::HashStep(dirs, job_empty, cache, info, true)) {
    }
    Check(!info.ok && std::strcmp(io::SetProblem(job_empty, info), "read") == 0, "an empty remote slot: read");
    WriteFileBytes(fs::path(dirs.save) / kSlotFiles[4], fixture.files[4]);
    for (unsigned s = 0; s < 4; ++s) {
        const fs::path slot = fs::path(dirs.save) / kSlotFiles[s];
        fs::rename(slot.wstring() + L".away", slot);
    }
    Check(io::HashSetNow(dirs, cache, info, true) && info.ok && SameSha(info.set, fixture.set), "the full set again (guest rule)");
}

// ---------------------------------------------------------------- snapshot
void Snapshot(const io::Dirs& dirs, const Fixture& fixture) {
    io::SnapshotBuffer snap{};
    Check(io::SnapshotRead(dirs, snap), "snapshot read");
    Check(snap.base && snap.info.ok && snap.info.n == 6 && SameSha(snap.info.set, fixture.set), "snapshot set");
    for (unsigned s = 0; s < wire::kSlots; ++s)
        Check(snap.data[s] && std::memcmp(snap.data[s], fixture.files[s].data(), fixture.files[s].size()) == 0, "snapshot bytes per slot");
    Check(io::SnapshotVerify(dirs, snap), "the second read equals the first");
    // A file changed between the two reads (same size, other bytes).
    auto other = fixture.files[1];
    other[1000] ^= 0xFF;
    WriteFileBytes(fs::path(dirs.save) / kSlotFiles[1], other);
    Check(!io::SnapshotVerify(dirs, snap), "a file changed between the reads is refused");
    WriteFileBytes(fs::path(dirs.save) / kSlotFiles[1], fixture.files[1]);
    Check(io::SnapshotVerify(dirs, snap), "unchanged again");
    // A remote slot that grew between the reads.
    auto grown = fixture.files[4];
    grown.push_back(1);
    WriteFileBytes(fs::path(dirs.save) / kSlotFiles[4], grown);
    Check(!io::SnapshotVerify(dirs, snap), "a remote slot that changed size is refused");
    WriteFileBytes(fs::path(dirs.save) / kSlotFiles[4], fixture.files[4]);
    io::SnapshotFree(snap);
    Check(!snap.base && !snap.data[0] && !io::SnapshotVerify(dirs, snap), "freed");
}

// ---------------------------------------------------------------- staging
std::vector<std::uint8_t> g_host[wire::kSlots];

bool StageAll(const io::Dirs& dirs, std::uint32_t xfer, unsigned skip_last_chunk_of = 99) {
    for (std::uint8_t s = 0; s < wire::kSlots; ++s) {
        const auto& bytes = g_host[s];
        for (std::size_t offset = 0; offset < bytes.size(); offset += wire::kChunk) {
            const std::size_t n = bytes.size() - offset < wire::kChunk ? bytes.size() - offset : wire::kChunk;
            if (s == skip_last_chunk_of && offset + n == bytes.size()) break;
            if (!io::StageWrite(dirs, xfer, s, static_cast<std::uint32_t>(offset), bytes.data() + offset, n)) return false;
        }
    }
    return true;
}

void Entries(wire::FileEntry (&files)[wire::kMaxFiles], std::uint8_t (&set)[32]) {
    Fixture host{};
    for (unsigned s = 0; s < wire::kSlots; ++s) {
        host.files[s] = g_host[s];
        files[s].slot = static_cast<std::uint8_t>(s);
        files[s].size = static_cast<std::uint32_t>(g_host[s].size());
        mgscoop::link::Sha256Digest(g_host[s].data(), g_host[s].size(), files[s].sha256);
    }
    ExpectedSet(host, set);
}

void Staging(const io::Dirs& dirs) {
    for (unsigned s = 0; s < wire::kSlots; ++s) g_host[s] = Pattern(kSizes[s] + 31, 200 + s);
    wire::FileEntry files[wire::kMaxFiles]{};
    std::uint8_t set[32]{};
    Entries(files, set);
    const std::uint32_t xfer = 0x0A1B2C3Du;
    const fs::path handoff(dirs.handoff);
    const fs::path partial = handoff / "0A1B2C3D.partial", final_dir = handoff / "0A1B2C3D";
    const std::string manifest = "{\"format\":\"mgs-coop-save-handoff\",\"v\":1,\"xfer\":\"0A1B2C3D\"}";
    const std::string request = "{\"format\":\"mgs-coop-save-handoff-request\",\"v\":1,\"pid\":1,\"xfer\":\"0A1B2C3D\"}";

    // Half-written: the last chunk of slot 4 is missing.
    Check(StageAll(dirs, xfer, 4), "stage writes");
    Check(fs::exists(partial / "287700" / "local" / "TPP_GAME_DATA0") && fs::exists(partial / "remote" / "TPP_GAME_DATA") &&
              !fs::exists(final_dir),
          "stage writes go to <xfer>.partial");
    std::uint8_t chunk[wire::kChunk]{};
    Check(!io::StageWrite(dirs, xfer, 0, 5000, chunk, 100), "a chunk past the written end is refused (in-order offsets)");
    Check(!io::StageWrite(dirs, xfer, 6, 0, chunk, 100) && !io::StageWrite(dirs, xfer, 0, 0, chunk, wire::kChunk + 1) &&
              !io::StageWrite(dirs, 0, 0, 0, chunk, 10) && !io::StageWrite(dirs, xfer, 0, wire::kMaxFileBytes - 10, chunk, 11),
          "slot, size, xfer and file bounds");
    Check(!io::CommitStage(dirs, xfer, files, 6, set), "no manifest yet: not committed");
    Check(io::WriteStageManifest(dirs, xfer, manifest.data(), manifest.size()), "handoff.json inside the partial folder");
    Check(ReadFileText(partial / "handoff.json") == manifest, "manifest content");
    Check(!io::CommitStage(dirs, xfer, files, 6, set) && fs::exists(partial) && !fs::exists(final_dir),
          "a half-written .partial is never committed");
    Check(!io::WriteRequest(dirs, xfer, wire::Mode::scene, request.data(), request.size()) && !fs::exists(handoff / "request.json"),
          "no request for a .partial");
    // The missing chunk, then a wrong set and a corrupted byte.
    const auto& last = g_host[4];
    const std::size_t tail = last.size() % wire::kChunk ? last.size() % wire::kChunk : wire::kChunk;
    Check(io::StageWrite(dirs, xfer, 4, static_cast<std::uint32_t>(last.size() - tail), last.data() + last.size() - tail, tail), "last chunk");
    std::uint8_t wrong[32]{};
    Check(!io::CommitStage(dirs, xfer, files, 6, wrong), "a set that does not match the entries is refused");
    wire::FileEntry corrupt[wire::kMaxFiles]{};
    std::memcpy(corrupt, files, sizeof(files));
    corrupt[2].sha256[0] ^= 1;
    Check(!io::CommitStage(dirs, xfer, corrupt, 6, set), "a staged file that does not match its entry is refused");
    Check(!io::CommitStage(dirs, xfer, files, 5, set), "a staged file that is not in the manifest is refused");
    // Commit, then request.json last and only in scene or always mode.
    Check(io::CommitStage(dirs, xfer, files, 6, set) && !fs::exists(partial) && fs::exists(final_dir / "handoff.json"),
          "commit renames <xfer>.partial to <xfer>");
    for (unsigned s = 0; s < wire::kSlots; ++s) Check(ReadFileBytes(final_dir / kSlotFiles[s]) == g_host[s], "staged bytes equal the host's");
    Check(!io::CommitStage(dirs, xfer, files, 6, set), "a committed stage is never merged into");
    Check(!io::WriteRequest(dirs, xfer, wire::Mode::stage, request.data(), request.size()) &&
              !io::WriteRequest(dirs, xfer, wire::Mode::off, request.data(), request.size()) && !fs::exists(handoff / "request.json"),
          "stage and off modes never write request.json");
    Check(!io::WriteRequest(dirs, 0x11111111u, wire::Mode::scene, request.data(), request.size()), "a request for another xfer is refused");
    Check(io::WriteRequest(dirs, xfer, wire::Mode::scene, request.data(), request.size()) && ReadFileText(handoff / "request.json") == request,
          "request.json in scene mode");
    Check(io::WriteRequest(dirs, xfer, wire::Mode::always, request.data(), request.size()), "request.json in always mode");
    Check(!fs::exists(handoff / "request.json.tmp") && !fs::exists(final_dir / "handoff.json.tmp"), "no temporary left");

    // Delete: request.json first, known names only; a foreign file survives.
    WriteFileBytes(final_dir / "remote" / "notes.txt", Pattern(10, 3));
    Check(io::DeleteStage(dirs, xfer), "delete stage");
    Check(!fs::exists(handoff / "request.json"), "request.json deleted");
    for (unsigned s = 0; s < wire::kSlots; ++s) Check(!fs::exists(final_dir / kSlotFiles[s]), "staged slot files deleted");
    Check(!fs::exists(final_dir / "handoff.json") && !fs::exists(final_dir / "287700"), "manifest and empty folders deleted");
    Check(fs::exists(final_dir / "remote" / "notes.txt"), "a foreign file survives with its folder");
    fs::remove(final_dir / "remote" / "notes.txt");
    Check(io::DeleteStage(dirs, xfer) && !fs::exists(final_dir), "the emptied folders go on the next delete");
    // A request.json that names another transfer is not this stage's.
    const std::string foreign = "{\"format\":\"mgs-coop-save-handoff-request\",\"v\":1,\"pid\":1,\"xfer\":\"0F0F0F0F\"}";
    WriteFileBytes(handoff / "request.json", std::vector<std::uint8_t>(foreign.begin(), foreign.end()));
    Check(StageAll(dirs, xfer) && io::DeleteStage(dirs, xfer) && !fs::exists(partial), "a partial stage is deleted");
    Check(fs::exists(handoff / "request.json"), "another transfer's request.json is kept");
    WriteFileBytes(handoff / "request.json", std::vector<std::uint8_t>{'x'});
    Check(io::DeleteStage(dirs, xfer) && !fs::exists(handoff / "request.json"), "a request.json without an xfer is deleted");
    Check(io::DeleteStageDir(L"") == false && io::DeleteStage(dirs, 0) == false, "no folder, no xfer");
}

// ---------------------------------------------------------------- leftovers
bool FakeRequestPid(const char* text, std::size_t, std::uint32_t& pid) noexcept {
    const char* p = std::strstr(text, "\"pid\":");
    if (!p) return false;
    pid = static_cast<std::uint32_t>(std::strtoul(p + 6, nullptr, 10));
    return true;
}

void Leftovers(const io::Dirs& dirs) {
    const fs::path handoff(dirs.handoff);
    WriteFileBytes(handoff / "0000ABCD.partial" / "287700" / "local" / "TPP_GAME_DATA0", Pattern(100, 1));
    WriteFileBytes(handoff / "0000ABCD.partial" / "287700" / "local" / "TPP_GAME_DATA0.tmp", Pattern(10, 1));
    WriteFileBytes(handoff / "0000ABCD.partial" / "handoff.json", Pattern(10, 2));
    WriteFileBytes(handoff / "0000BCDE.partial" / "remote" / "PERSONAL_DATA", Pattern(100, 3));
    WriteFileBytes(handoff / "0000BCDE.partial" / "keep.txt", Pattern(10, 4));
    WriteFileBytes(handoff / "notes.partial" / "287700" / "local" / "TPP_GAME_DATA0", Pattern(10, 5));
    WriteFileBytes(handoff / "0000CDEF" / "287700" / "local" / "TPP_GAME_DATA0", Pattern(10, 6));
    WriteFileBytes(handoff / "provenance.json", Pattern(10, 7));
    WriteFileBytes(handoff / "applied.json", Pattern(10, 8));
    const std::string other = "{\"pid\":4242}";
    WriteFileBytes(handoff / "request.json", std::vector<std::uint8_t>(other.begin(), other.end()));
    const unsigned removed = io::CleanLeftovers(dirs, 777, &FakeRequestPid);
    Check(removed == 2, "one .partial folder (the other keeps a foreign file) and a foreign request.json");
    Check(!fs::exists(handoff / "0000ABCD.partial") && !fs::exists(handoff / "request.json"), "leftovers removed");
    Check(fs::exists(handoff / "0000BCDE.partial" / "keep.txt") && !fs::exists(handoff / "0000BCDE.partial" / "remote"),
          "a foreign file survives (known names only)");
    Check(fs::exists(handoff / "notes.partial" / "287700" / "local" / "TPP_GAME_DATA0"), "only <8 hex>.partial folders are leftovers");
    Check(fs::exists(handoff / "0000CDEF" / "287700" / "local" / "TPP_GAME_DATA0"), "committed stages are the tooling's");
    Check(fs::exists(handoff / "provenance.json") && fs::exists(handoff / "applied.json"), "provenance.json and applied.json kept");
    const std::string own = "{\"pid\":777}";
    WriteFileBytes(handoff / "request.json", std::vector<std::uint8_t>(own.begin(), own.end()));
    Check(io::CleanLeftovers(dirs, 777, &FakeRequestPid) == 0 && fs::exists(handoff / "request.json"), "this process's request.json stays");
    WriteFileBytes(handoff / "request.json", std::vector<std::uint8_t>{'{', '}'});
    Check(io::CleanLeftovers(dirs, 777, &FakeRequestPid) == 1 && !fs::exists(handoff / "request.json"), "a malformed request.json is removed");
    fs::remove_all(handoff / "0000BCDE.partial");
    fs::remove_all(handoff / "notes.partial");
    fs::remove_all(handoff / "0000CDEF");
    fs::remove(handoff / "applied.json");
}

// ---------------------------------------------------------------- documents
void Documents(const io::Dirs& dirs) {
    wchar_t path[io::kPathChars]{};
    Check(io::HandoffFile(dirs, L"provenance.json", path, io::kPathChars), "provenance path");
    const std::string first = "{\"format\":\"mgs-coop-save-provenance\",\"v\":1,\"entries\":[]}";
    const std::string second = "{\"format\":\"mgs-coop-save-provenance\",\"v\":1,\"entries\":[{\"set\":\"AB\",\"mission\":30010}]}";
    Check(io::WriteText(dirs, path, first.data(), first.size()), "provenance written");
    char text[2048]{};
    std::size_t size = 0;
    Check(io::ReadText(path, text, sizeof(text), size) && size == first.size() && first == text, "provenance reloaded");
    Check(io::WriteText(dirs, path, second.data(), second.size()) && io::ReadText(path, text, sizeof(text), size) && second == text,
          "provenance replaced atomically");
    Check(!fs::exists(fs::path(path).wstring() + L".tmp"), "no temporary left");
    char tiny[16];
    Check(!io::ReadText(path, tiny, sizeof(tiny), size) && size == 0 && tiny[0] == 0, "a file over the buffer is refused");
    Check(!io::ReadText(L"Z:\\definitely\\missing.json", text, sizeof(text), size), "a missing file");
}

// ---------------------------------------------------------------- rejoin
bool BuildCode(char* out, std::size_t capacity) noexcept {
    const char code[] = "MGS2-ABCD-EFGH-JKMN-PQRS-TVWX-YZ01";
    if (capacity < sizeof(code)) return false;
    std::memcpy(out, code, sizeof(code));
    return true;
}
bool NoCode(char* out, std::size_t capacity) noexcept {
    if (capacity) out[0] = 0;
    return false;
}

void Rejoin(const io::Dirs& dirs) {
    fs::create_directories(fs::path(dirs.rejoin).parent_path());
    Check(!io::WriteRejoin(dirs, &NoCode) && !fs::exists(dirs.rejoin), "no code, no file");
    Check(io::WriteRejoin(dirs, &BuildCode) && ReadFileText(dirs.rejoin) == "MGS2-ABCD-EFGH-JKMN-PQRS-TVWX-YZ01\r\n", "one code and CRLF");
    io::Dirs none = dirs;
    none.rejoin[0] = 0;
    Check(!io::WriteRejoin(none, &BuildCode) && !io::WriteRejoin(dirs, nullptr), "no game folder or builder");
    fs::remove(dirs.rejoin);
}

// ---------------------------------------------------------------- exit window
io::Window g_windows[8];
unsigned g_window_count = 0;
void* g_posted = nullptr;
unsigned g_posts = 0;
unsigned FakeEnum(io::Window* out, unsigned capacity) noexcept {
    unsigned n = 0;
    for (; n < g_window_count && n < capacity; ++n) out[n] = g_windows[n];
    return n;
}
bool FakePost(void* handle) noexcept {
    g_posted = handle;
    ++g_posts;
    return true;
}

void ExitWindow() {
    int handles[8]{};
    const std::uint32_t pid = 4242;
    g_windows[0] = io::Window{&handles[0], 999, true, false, 5000000};  // another process, largest
    g_windows[1] = io::Window{&handles[1], pid, true, true, 4000000};   // owned (a dialog)
    g_windows[2] = io::Window{&handles[2], pid, false, false, 3000000}; // invisible
    g_windows[3] = io::Window{&handles[3], pid, true, false, 100};      // small helper
    g_windows[4] = io::Window{&handles[4], pid, true, false, 2073600};  // the 1920x1080 game window
    g_window_count = 5;
    const io::Window* pick = io::PickExitWindow(g_windows, g_window_count, pid);
    Check(pick && pick->handle == &handles[4], "the largest visible unowned window of this process");
    Check(io::PostExit(&FakeEnum, &FakePost, pid) && g_posted == &handles[4] && g_posts == 1, "WM_CLOSE goes to it");
    g_window_count = 3;
    Check(!io::PostExit(&FakeEnum, &FakePost, pid) && g_posts == 1, "no candidate: no_window, nothing posted");
    g_window_count = 0;
    Check(!io::PostExit(&FakeEnum, &FakePost, pid) && !io::PickExitWindow(nullptr, 0, pid), "no windows");
    Check(!io::PostExit(nullptr, &FakePost, pid) && !io::PostExit(&FakeEnum, nullptr, pid), "no enumerator or poster");
    // The real enumerator lists only this process's top-level windows (a console
    // test has no visible one, so nothing would be closed).
    io::Window real[256]{};
    const unsigned n = io::EnumTopLevelWindows(real, 256);
    for (unsigned i = 0; i < n; ++i) Check(real[i].pid == GetCurrentProcessId(), "only this process's windows are listed");
    Check(!io::PickExitWindow(real, n, GetCurrentProcessId()), "a console test has no window of its own to close");
}

// ---------------------------------------------------------------- module gates
char g_lines[64][192];
unsigned g_line_count = 0;
void Capture(const char* line) noexcept {
    if (g_line_count < 64) lstrcpynA(g_lines[g_line_count++], line, 192);
}
bool Logged(const char* prefix) {
    for (unsigned i = 0; i < g_line_count; ++i)
        if (std::strncmp(g_lines[i], prefix, std::strlen(prefix)) == 0) return true;
    return false;
}

const char* Lua(const char* tail) {
    const std::string s = std::string("return 'MGS_COOP_WORLD_SAVE_V1' -- ") + tail;
    return module::HandleLua(s.data(), s.size());
}

void ModuleGates(const io::Dirs& dirs) {
    namespace hs = mgscoop::save_handoff_state;
    const std::string save_dir = fs::path(dirs.save).string();
    const std::wstring game = (g_root / "game").wstring();
    module::Config config{};
    config.role = 2;
    config.mode = mgscoop::SaveHandoff::off;
    config.play = true;
    config.net = true;
    config.link_v2 = true;
    config.saves_redirected = true;
    config.world_on = true;
    config.travel_lock = true;
    config.save_dir = save_dir.c_str();
    config.directory = game.c_str();
    config.log = &Capture;
    struct Case { mgscoop::SaveHandoff mode; bool link_v2, saves, world; const char* dir; const char* line; };
    const std::string long_dir = "D:\\" + std::string(200, 'x');
    const Case cases[] = {
        {mgscoop::SaveHandoff::off, true, true, true, nullptr, "save_handoff=disabled; reason=off; t_ms="},
        {mgscoop::SaveHandoff::scene, false, true, true, nullptr, "save_handoff=disabled; reason=legacy_link; t_ms="},
        {mgscoop::SaveHandoff::scene, true, false, true, nullptr, "save_handoff=disabled; reason=saves; t_ms="},
        {mgscoop::SaveHandoff::scene, true, true, false, nullptr, "save_handoff=disabled; reason=world_off; t_ms="},
        {mgscoop::SaveHandoff::scene, true, true, true, long_dir.c_str(), "save_handoff=disabled; reason=path; t_ms="},
    };
    for (const Case& c : cases) {
        module::ResetForTest();
        g_line_count = 0;
        module::Config k = config;
        k.mode = c.mode;
        k.link_v2 = c.link_v2;
        k.saves_redirected = c.saves;
        k.world_on = c.world;
        if (c.dir) k.save_dir = c.dir;
        Check(!module::Configure(k) && !module::Configured() && Logged(c.line), "a disabled reason is logged");
        Check(module::LuaCode(0) == 0 && !module::GuestHandoffActive(), "off: r[12] 0");
        Check(module::LockAllowed() == 1, "the travel lock still applies to a guest with save_handoff off");
        const hs::View off = hs::Get();
        Check(off.available == c.world && off.role == 2 && off.token == static_cast<std::uint8_t>(hs::Token::off),
              "disabled: F9 Shared save (off line, Travel lock) only while the guest's world lock may apply");
        const char* reply = Lua("1 40050 1 2 0 0");
        Check(reply && std::strcmp(reply, "return false") == 0, "SAVE while off: return false");
    }
    Check(Lua("1 40050 1 2 0") == nullptr && Lua("9 0 0 0 0 0") == nullptr, "a malformed SAVE stays literal");
    hs::SetTravelUnlocked(true);
    Check(module::LockAllowed() == 0, "the F9 unlock turns the lock off");
    hs::SetTravelUnlocked(false);
    module::ResetForTest();
    module::Config host = config;
    host.role = 1;
    module::Configure(host);
    Check(module::LockAllowed() == 0, "never for the host");
    Check(!hs::Get().available, "disabled host: no F9 Shared save section");
    module::ResetForTest();
    module::Config unlocked = config;
    unlocked.travel_lock = false;
    module::Configure(unlocked);
    Check(module::LockAllowed() == 0, "lab.ini travel_lock=0");
    Check(!hs::Get().available, "disabled guest without the lock: no F9 Shared save section");

    // The Play-Coop relaunch after an applied handoff runs with save_handoff=off:
    // applied.json (written for this process start) still gives G10 and the token.
    {
        const fs::path applied = fs::path(dirs.handoff) / "applied.json";
        SYSTEMTIME st{};
        GetSystemTime(&st);
        char text[320]{};
        std::snprintf(text, sizeof(text),
                      "{\r\n    \"format\":  \"mgs-coop-save-handoff-applied\",\r\n    \"v\":  1,\r\n    \"set_sha256\":  \"%s\",\r\n"
                      "    \"applied_utc\":  \"%04u-%02u-%02uT%02u:%02u:%02uZ\",\r\n    \"aside\":  \"20261015-120100-before-handoff\"\r\n}\n",
                      "AB42AF5A00000000000000000000000000000000000000000000000000000001", st.wYear, st.wMonth, st.wDay, st.wHour,
                      st.wMinute, st.wSecond);
        WriteFileBytes(applied, std::vector<std::uint8_t>(text, text + std::strlen(text)));
        std::uint32_t cursor = 0;
        mgscoop::coop_hud::Event events[64]{};
        while (mgscoop::coop_hud::Take(events, 64, cursor) == 64) {}
        module::ResetForTest();
        g_line_count = 0;
        Check(!module::Configure(config) && Logged("save_handoff=disabled; reason=off") && Logged("save_handoff=applied_seen; set=AB42AF5A"),
              "save_handoff=off guest: applied.json is still seen");
        const hs::View view = hs::Get();
        Check(view.available && view.token == static_cast<std::uint8_t>(hs::Token::applied), "the applied token reaches F9");
        const unsigned taken = mgscoop::coop_hud::Take(events, 64, cursor);
        bool g10 = false;
        for (unsigned i = 0; i < taken; ++i)
            g10 = g10 || (events[i].kind == mgscoop::coop_hud::EventKind::save_handoff &&
                          events[i].arg == static_cast<std::int32_t>(hs::Toast::g10_applied) && std::strcmp(events[i].text, "0 0") == 0);
        Check(g10, "toast G10 is posted");
        module::ResetForTest();
        module::Config host_off = config;
        host_off.role = 1;
        g_line_count = 0;
        module::Configure(host_off);
        Check(!Logged("save_handoff=applied_seen"), "never for the host");
        fs::remove(applied);
    }

    // Configured (guest, scene): leftovers cleaned, the receipt rate gate.
    module::ResetForTest();
    g_line_count = 0;
    const fs::path handoff(dirs.handoff);
    WriteFileBytes(handoff / "0000ABCD.partial" / "287700" / "local" / "TPP_GAME_DATA0", Pattern(100, 1));
    module::Config scene = config;
    scene.mode = mgscoop::SaveHandoff::scene;
    Check(module::Configure(scene) && module::Configured() && module::Hooks() && module::Hooks()->on_idle && module::Hooks()->on_session &&
              module::Hooks()->on_peer_close,
          "configured with hooks");
    Check(Logged("save_handoff=configured; role=guest; mode=scene; channel=4; dir=ok; t_ms="), "configured line");
    Check(Logged("save_handoff=cleanup; removed=1") && !fs::exists(handoff / "0000ABCD.partial"), "Configure removes leftovers");
    Check(hs::Get().available, "the F9 Shared save section is available");
    Check(!module::GuestHandoffActive(), "no host STATE seen yet");
    unsigned accepted = 0, refused = 0;
    for (int i = 0; i < 6; ++i) {
        const char* reply = Lua("1 40050 1 2 0 0");
        Check(reply != nullptr, "REPORT parsed");
        if (std::strcmp(reply, "return true") == 0) ++accepted;
        else ++refused;
    }
    Check(accepted == 4 && refused == 2, "at most 4 SAVE receipts per second");
    const char* decline = Lua("3 1 0 0 0 0");
    Check(decline && std::strcmp(decline, "return true") == 0, "a DECLINE is never rate-gated");
    module::NoteMission(40050, false);
    module::Shutdown();
    const char* after = Lua("1 40050 1 2 0 0");
    Check(after && std::strcmp(after, "return false") == 0, "after Shutdown: return false");
    module::ResetForTest();
    hs::ResetForTest();
}

// P2-10 exit_slow after a WM_CLOSE (review N1): the overlay's PartnerShutdown
// stops the core right after the close is posted, so the glue's own timer logs
// exit_slow and posts G9 once; a veto (the core leaving the exiting token)
// cancels it, and no_window leaves it to the core.
unsigned CountLines(const char* prefix) {
    unsigned n = 0;
    for (unsigned i = 0; i < g_line_count; ++i)
        if (std::strncmp(g_lines[i], prefix, std::strlen(prefix)) == 0) ++n;
    return n;
}
unsigned TakeG9(std::uint32_t& cursor) {
    namespace hs = mgscoop::save_handoff_state;
    mgscoop::coop_hud::Event events[64]{};
    unsigned g9 = 0, taken = 0;
    while ((taken = mgscoop::coop_hud::Take(events, 64, cursor)) > 0)
        for (unsigned i = 0; i < taken; ++i)
            if (events[i].kind == mgscoop::coop_hud::EventKind::save_handoff &&
                events[i].arg == static_cast<std::int32_t>(hs::Toast::g9_quit_manually))
                ++g9;
    return g9;
}
bool WaitLine(const char* prefix, unsigned ms) {
    for (unsigned waited = 0; waited < ms; waited += 10) {
        if (CountLines(prefix)) return true;
        Sleep(10);
    }
    return CountLines(prefix) != 0;
}
void ExitSlow(const io::Dirs& dirs) {
    namespace hs = mgscoop::save_handoff_state;
    const std::string save_dir = fs::path(dirs.save).string();
    const std::wstring game = (g_root / "game").wstring();
    module::Config scene{};
    scene.role = 2;
    scene.mode = mgscoop::SaveHandoff::scene;
    scene.play = scene.net = scene.link_v2 = scene.saves_redirected = scene.world_on = scene.travel_lock = true;
    scene.save_dir = save_dir.c_str();
    scene.directory = game.c_str();
    scene.log = &Capture;
    int handle = 0;
    const auto configure = [&](std::uint32_t delay_ms) {
        module::ResetForTest();
        hs::ResetForTest();
        g_line_count = 0;
        Check(module::Configure(scene), "guest scene configured");
        g_windows[0] = io::Window{&handle, GetCurrentProcessId(), true, false, 2073600};
        g_window_count = 1;
        g_posts = 0;
        g_posted = nullptr;
        module::SetExitForTest(&FakeEnum, &FakePost, delay_ms);
    };
    core::Action exit{};
    exit.kind = core::ActionKind::post_exit;
    std::uint32_t cursor = 0;
    TakeG9(cursor);

    // WM_CLOSE posted, then PartnerShutdown and one hook tick: the core is stopped,
    // yet exit_slow and G9 still come, exactly once.
    configure(150);
    module::ExecuteForTest(exit);
    Check(g_posts == 1 && g_posted == &handle, "post_exit closes the game window");
    module::Shutdown();
    module::Hooks()->on_idle(GetTickCount64());
    Check(WaitLine("save_handoff=exit_slow", 3000), "exit_slow after the delay although the core is shut down");
    Sleep(400);
    Check(CountLines("save_handoff=exit_slow") == 1, "exactly one exit_slow line");
    Check(TakeG9(cursor) == 1, "exactly one G9 (quit MGSV manually)");

    // A veto first (the core publishes kept): no exit_slow, no G9.
    configure(1000);
    module::ExecuteForTest(exit);
    core::Action kept{};
    kept.kind = core::ActionKind::publish;
    kept.view.token = static_cast<std::uint8_t>(hs::Token::kept);
    module::ExecuteForTest(kept);
    Sleep(1500);
    Check(CountLines("save_handoff=exit_slow") == 0 && TakeG9(cursor) == 0, "a declined exit: the timer stays silent");
    // The exiting token itself keeps it armed.
    configure(150);
    module::ExecuteForTest(exit);
    core::Action exiting{};
    exiting.kind = core::ActionKind::publish;
    exiting.view.token = static_cast<std::uint8_t>(hs::Token::exiting);
    module::ExecuteForTest(exiting);
    Check(WaitLine("save_handoff=exit_slow", 3000) && TakeG9(cursor) == 1, "the exiting token keeps the timer armed");

    // no_window: nothing armed here (the core logs exit_slow and G9 was shown at once).
    configure(150);
    g_window_count = 0;
    module::ExecuteForTest(exit);
    Sleep(500);
    Check(g_posts == 0 && CountLines("save_handoff=exit_slow") == 0 && TakeG9(cursor) == 0, "no_window: the glue arms nothing");
    module::ResetForTest();
    hs::ResetForTest();
}

// The shared P2-6 vectors (tests/fixtures/handoff-set.json, track A): the glue
// hashing the six fixture files on disk gives the fixture's set_sha256, the
// value the wire tests and Get-CoopHandoffSetSha256 also check. Reconfigures
// the save redirect to the fixture tree (run last).
void FixtureSet() {
    const std::vector<std::uint8_t> raw = ReadFileBytes(fs::path(MGS_HANDOFF_SET_JSON));
    const std::string doc(raw.begin(), raw.end());
    Check(!doc.empty(), "tests/fixtures/handoff-set.json reads");
    const fs::path saves = g_root / "fixture-saves";
    std::size_t pos = 0;
    for (std::uint8_t s = 0; s < wire::kSlots; ++s) {
        static constexpr char key[] = "\"content_ascii\": \"";
        pos = doc.find(key, pos);
        Check(pos != std::string::npos, "fixture file content");
        pos += sizeof(key) - 1;
        const std::size_t end = doc.find('"', pos);
        Check(end != std::string::npos, "fixture file content ends");
        WriteFileBytes(saves / wire::SlotPath(s), std::vector<std::uint8_t>(doc.begin() + static_cast<std::ptrdiff_t>(pos),
                                                                            doc.begin() + static_cast<std::ptrdiff_t>(end)));
        pos = end;
    }
    static constexpr char set_key[] = "\"set_sha256\": \"";
    std::size_t at = doc.find(set_key);  // the first one is the whole set's (the subsets follow it)
    Check(at != std::string::npos && at + sizeof(set_key) - 1 + wire::kShaHexChars <= doc.size(), "fixture set_sha256");
    at += sizeof(set_key) - 1;
    const std::string expected = doc.substr(at, wire::kShaHexChars);
    mgscoop::lab::save::reset_for_test();
    mgscoop::lab::save::configure(saves.string().c_str());
    io::Dirs dirs{};
    Check(io::MakeDirs(saves.string().c_str(), nullptr, dirs), "fixture save folder");
    io::HashCache cache{};
    core::SetInfo info{};
    Check(io::HashSetNow(dirs, cache, info) && info.ok && info.stable && !info.unexpected && info.n == wire::kSlots,
          "the six fixture files hash as a set");
    char hex[wire::kShaHexChars + 1]{};
    wire::FormatShaHex(info.set, hex);
    Check(expected == hex, "the glue's set hash equals tests/fixtures/handoff-set.json set_sha256");
}

// Online 2026-10-22 P1/P8 through the module: a guest whose co-op copy has
// remote files only hashes them as its set and logs "save_handoff=set; ok=1;
// ...; local=0" once; the host logs "ok=0; why=no_primary" once.
void RemoteOnlyGlue() {
    const fs::path saves = g_root / "remote-only-saves";
    WriteFileBytes(saves / kSlotFiles[4], Pattern(5000, 11));
    WriteFileBytes(saves / kSlotFiles[5], Pattern(3000, 12));
    fs::create_directories(saves / "287700" / "local");
    WriteFileBytes(saves / "287700\\local\\TPP_GRAPHICS_CONFIG", Pattern(256, 13));
    mgscoop::lab::save::reset_for_test();
    mgscoop::lab::save::configure(saves.string().c_str());
    const std::string save_dir = saves.string();
    for (const std::uint8_t role : {std::uint8_t{2}, std::uint8_t{1}}) {
        module::ResetForTest();
        g_line_count = 0;
        module::Config c{};
        c.role = role;
        c.mode = mgscoop::SaveHandoff::scene;
        c.play = c.net = c.link_v2 = c.saves_redirected = c.world_on = c.travel_lock = true;
        c.save_dir = save_dir.c_str();
        c.log = &Capture;
        Check(module::Configure(c), "remote-only: configured");
        // A fresh title report (story opening), then hook ticks: one file per tick.
        for (int tick = 0; tick < 12; ++tick) {
            if (tick % 4 == 0) Lua("1 40010 1 1 0 0");
            module::Hooks()->on_idle(mgscoop::partner_net::NowMs());
        }
        bool remote_line = false;
        for (unsigned i = 0; i < g_line_count; ++i)
            remote_line = remote_line || (std::strncmp(g_lines[i], "save_handoff=set; ok=1; set=", 28) == 0 &&
                                          std::strstr(g_lines[i], "; files=2; local=0; t_ms=") != nullptr);
        if (role == 2)
            Check(remote_line && CountLines("save_handoff=set;") == 1, "remote-only guest: its own set, logged once");
        else
            Check(CountLines("save_handoff=set; ok=0; why=no_primary; files=2") == 1 && CountLines("save_handoff=set;") == 1,
                  "remote-only host: no set (no_primary), logged once");
    }
    module::ResetForTest();
    mgscoop::lab::save::reset_for_test();
}
}  // namespace

int main() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    g_root = fs::path(exe).parent_path() / (L"save-handoff-io-" + std::to_wstring(GetCurrentProcessId()));
    fs::remove_all(g_root);
    fs::create_directories(g_root);
    const fs::path saves = g_root / "saves";
    const Fixture fixture = MakeSaves(saves, 0);
    // Remote slots are read through the save redirect (its shared I/O lock).
    mgscoop::lab::save::reset_for_test();
    mgscoop::lab::save::configure(saves.string().c_str());
    io::Dirs dirs{};
    Paths(dirs);
    Hashing(dirs, fixture);
    Snapshot(dirs, fixture);
    Staging(dirs);
    Leftovers(dirs);
    Documents(dirs);
    Rejoin(dirs);
    ExitWindow();
    ModuleGates(dirs);
    ExitSlow(dirs);
    FixtureSet();
    RemoteOnlyGlue();
    mgscoop::lab::save::reset_for_test();
    // Only this test's own temporary tree.
    std::error_code ignored;
    fs::remove_all(g_root, ignored);
    std::printf("save_handoff_io_tests: %u checks passed\n", checks);
    return 0;
}
