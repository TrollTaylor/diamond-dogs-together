// Original MGS Co-op automatic save handoff: the module glue (join-fix phase 2
// B7; mgs_coop_lab). One save_handoff_core::Core driven from the partner_net
// HandoffHooks, the file actions under <save_dir>-handoff, the exit request
// and the Lua SAVE receipt (docs/save-handoff.md).
#include "mgscoop/save_handoff.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <strsafe.h>
#include <cstring>
#include "mgscoop/coop_hud.hpp"
#include "mgscoop/lab_status.hpp"
#include "mgscoop/link_crypto.hpp"
#include "mgscoop/save_handoff_state.hpp"
#include "mgscoop/save_redirect.hpp"
#include "mgscoop/world_receipt.hpp"

namespace mgscoop::save_handoff {

// ============================================================ file actions
namespace io {
namespace {

std::size_t WideLength(const wchar_t* s) noexcept {
    std::size_t n = 0;
    while (s && s[n]) ++n;
    return n;
}

bool Join(wchar_t* out, std::size_t capacity, const wchar_t* base, const wchar_t* leaf) noexcept {
    if (!out || !capacity) return false;
    out[0] = 0;
    if (!base || !base[0] || !leaf) return false;
    if (FAILED(StringCchPrintfW(out, capacity, L"%s\\%s", base, leaf))) {
        out[0] = 0;
        return false;
    }
    return true;
}

// A slot's file name ("TPP_GAME_DATA0") or its path below a save or stage
// folder ("287700\local\TPP_GAME_DATA0") as a wide string.
bool SlotLeaf(std::uint8_t slot, bool with_folder, wchar_t* out, std::size_t capacity) noexcept {
    const char* text = with_folder ? wire::SlotPath(slot) : wire::SlotName(slot);
    if (!text || !out || !capacity) return false;
    std::size_t i = 0;
    for (; text[i]; ++i) {
        if (i + 1 >= capacity) {
            out[0] = 0;
            return false;
        }
        out[i] = static_cast<wchar_t>(static_cast<unsigned char>(text[i]));
    }
    out[i] = 0;
    return true;
}

bool IsDirectory(const wchar_t* path) noexcept {
    const DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}
bool IsFile(const wchar_t* path) noexcept {
    const DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}
bool EnsureDirectory(const wchar_t* path) noexcept {
    if (!path || !path[0]) return false;
    if (CreateDirectoryW(path, nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS && IsDirectory(path);
}
// Deletes one named file; true when it is gone afterwards.
bool DeleteKnown(const wchar_t* path) noexcept {
    if (DeleteFileW(path)) return true;
    const DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

std::uint64_t FileTimeValue(const FILETIME& t) noexcept {
    return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
}

// Absent is a valid answer (present=false); a folder named like a slot or an
// unreadable entry is not.
bool StatFile(const wchar_t* path, Stamp& out) noexcept {
    out = Stamp{};
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data)) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
    out.present = true;
    out.size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    out.mtime = FileTimeValue(data.ftLastWriteTime);
    return true;
}

bool SameStamp(const Stamp& a, const Stamp& b) noexcept {
    return a.present == b.present && (!a.present || (a.size == b.size && a.mtime == b.mtime));
}
bool SameStamps(const Stamps& a, const Stamps& b) noexcept {
    for (unsigned s = 0; s < wire::kSlots; ++s)
        if (!SameStamp(a.slot[s], b.slot[s])) return false;
    return a.unexpected == b.unexpected;
}

// Whether `folder` holds a progress name outside the slot table for that
// folder (remote: slots 4/5; local: 0..3). A missing folder lists as empty.
bool ScanUnexpected(const wchar_t* folder, bool remote, bool& unexpected) noexcept {
    wchar_t pattern[kPathChars]{};
    if (!Join(pattern, kPathChars, folder, L"*")) return false;
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW(pattern, &fd);
    if (find == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!wire::IsProgressName(fd.cFileName)) continue;
        bool known = false;
        for (std::uint8_t s = 0; s < wire::kSlots && !known; ++s) {
            if (wire::SlotIsRemote(s) != remote) continue;
            wchar_t leaf[32]{};
            if (SlotLeaf(s, false, leaf, ARRAYSIZE(leaf)) && lstrcmpW(leaf, fd.cFileName) == 0) known = true;
        }
        if (!known) unexpected = true;
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    return true;
}

// Streams one local file into SHA-256 with full sharing (the game's own open
// is never refused because of this handle). Exactly `expected` bytes.
bool HashLocalFile(const wchar_t* path, std::uint64_t expected, std::uint8_t (&sha)[wire::kShaSize]) noexcept {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    link::Sha256 ctx{};
    link::Sha256Init(ctx);
    std::uint8_t buffer[16384];
    std::uint64_t total = 0;
    bool ok = true;
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(file, buffer, sizeof(buffer), &read, nullptr)) {
            ok = false;
            break;
        }
        if (!read) break;
        total += read;
        if (total > expected) {
            ok = false;
            break;
        }
        link::Sha256Update(ctx, buffer, read);
    }
    CloseHandle(file);
    if (!ok || total != expected) return false;
    link::Sha256Final(ctx, sha);
    return true;
}

// A remote slot, read whole through the save redirect's shared I/O lock.
bool HashRemoteFile(std::uint8_t slot, std::uint64_t expected, std::uint8_t (&sha)[wire::kShaSize]) noexcept {
    if (!expected || expected > wire::kMaxFileBytes) return false;
    const auto bytes = static_cast<std::size_t>(expected);
    void* scratch = VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!scratch) return false;
    std::size_t got = 0;
    const bool ok = lab::save::read_remote_snapshot(wire::SlotName(slot), scratch, bytes, got) && got == bytes;
    if (ok) link::Sha256Digest(scratch, got, sha);
    VirtualFree(scratch, 0, MEM_RELEASE);
    return ok;
}

bool HashSlotFile(const Dirs& dirs, std::uint8_t slot, std::uint64_t expected, std::uint8_t (&sha)[wire::kShaSize]) noexcept {
    if (wire::SlotIsRemote(slot)) return HashRemoteFile(slot, expected, sha);
    wchar_t path[kPathChars]{};
    return SlotFile(dirs, slot, path, kPathChars) && HashLocalFile(path, expected, sha);
}

// Reads one slot whole into `out` (exactly `size` bytes; a longer file fails).
bool ReadSlotInto(const Dirs& dirs, std::uint8_t slot, std::uint8_t* out, std::size_t size) noexcept {
    if (wire::SlotIsRemote(slot)) {
        std::size_t got = 0;
        return lab::save::read_remote_snapshot(wire::SlotName(slot), out, size, got) && got == size;
    }
    wchar_t path[kPathChars]{};
    if (!SlotFile(dirs, slot, path, kPathChars)) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    std::size_t total = 0;
    bool ok = true;
    while (total < size) {
        DWORD read = 0;
        const DWORD want = static_cast<DWORD>(size - total > 0x100000 ? 0x100000 : size - total);
        if (!ReadFile(file, out + total, want, &read, nullptr) || !read) {
            ok = false;
            break;
        }
        total += read;
    }
    if (ok) {
        std::uint8_t extra = 0;
        DWORD read = 0;
        if (!ReadFile(file, &extra, 1, &read, nullptr) || read != 0) ok = false;  // the file grew
    }
    CloseHandle(file);
    return ok && total == size;
}

void FormatXfer(std::uint32_t xfer, wchar_t (&out)[24], bool partial) noexcept {
    if (FAILED(StringCchPrintfW(out, ARRAYSIZE(out), partial ? L"%08X.partial" : L"%08X", xfer))) out[0] = 0;
}

bool IsHex8(const wchar_t* s) noexcept {
    for (int i = 0; i < 8; ++i) {
        const wchar_t c = s[i];
        if (!((c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'F') || (c >= L'a' && c <= L'f'))) return false;
    }
    return true;
}

// The known names of one staging folder: each slot file and its atomic-write
// temporary, then handoff.json and its temporary.
bool DeleteKnownStageFiles(const wchar_t* dir) noexcept {
    bool all = true;
    for (std::uint8_t s = 0; s < wire::kSlots; ++s) {
        wchar_t leaf[64]{}, path[kPathChars]{}, temporary[kPathChars]{};
        if (!SlotLeaf(s, true, leaf, ARRAYSIZE(leaf)) || !Join(path, kPathChars, dir, leaf)) {
            all = false;
            continue;
        }
        all = DeleteKnown(path) && all;
        if (lab::MakeTemporaryPath(path, temporary, kPathChars)) all = DeleteKnown(temporary) && all;
    }
    wchar_t manifest[kPathChars]{}, temporary[kPathChars]{};
    if (Join(manifest, kPathChars, dir, L"handoff.json")) {
        all = DeleteKnown(manifest) && all;
        if (lab::MakeTemporaryPath(manifest, temporary, kPathChars)) all = DeleteKnown(temporary) && all;
    }
    return all;
}

// The staging tree's folders, each removed only when empty (never recursive).
void RemoveStageFolders(const wchar_t* dir) noexcept {
    static const wchar_t* const kFolders[] = {L"287700\\local", L"287700", L"remote"};
    for (const wchar_t* leaf : kFolders) {
        wchar_t path[kPathChars]{};
        if (Join(path, kPathChars, dir, leaf)) RemoveDirectoryW(path);
    }
    RemoveDirectoryW(dir);
}

}  // namespace

bool MakeDirs(const char* save_dir, const wchar_t* directory, Dirs& out) noexcept {
    out = Dirs{};
    char handoff[kPathChars]{};
    if (!save_dir || !lab::save::format_handoff_dir(save_dir, handoff, sizeof(handoff))) return false;
    std::size_t base = 0;
    while (save_dir[base]) ++base;
    while (base && (save_dir[base - 1] == '\\' || save_dir[base - 1] == '/')) --base;
    if (!base || base >= kPathChars) return false;
    char trimmed[kPathChars]{};
    for (std::size_t i = 0; i < base; ++i) trimmed[i] = save_dir[i];
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, trimmed, -1, out.save, static_cast<int>(kPathChars)) <= 0 ||
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, handoff, -1, out.handoff, static_cast<int>(kPathChars)) <= 0) {
        out = Dirs{};
        return false;
    }
    if (directory && directory[0] && FAILED(StringCchPrintfW(out.rejoin, kPathChars, L"%s\\mgs-coop\\link-rejoin.txt", directory)))
        out.rejoin[0] = 0;
    return true;
}

bool SlotFile(const Dirs& dirs, std::uint8_t slot, wchar_t* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    out[0] = 0;
    wchar_t leaf[64]{};
    if (!dirs.save[0] || !SlotLeaf(slot, false, leaf, ARRAYSIZE(leaf))) return false;
    if (FAILED(StringCchPrintfW(out, capacity, wire::SlotIsRemote(slot) ? L"%s\\remote\\%s" : L"%s\\287700\\local\\%s", dirs.save,
                                leaf))) {
        out[0] = 0;
        return false;
    }
    return true;
}

bool StageDir(const Dirs& dirs, std::uint32_t xfer, bool partial, wchar_t* out, std::size_t capacity) noexcept {
    if (out && capacity) out[0] = 0;
    wchar_t leaf[24]{};
    FormatXfer(xfer, leaf, partial);
    return xfer != 0 && leaf[0] && Join(out, capacity, dirs.handoff, leaf);
}

bool StageFile(const Dirs& dirs, std::uint32_t xfer, bool partial, std::uint8_t slot, wchar_t* out, std::size_t capacity) noexcept {
    wchar_t dir[kPathChars]{}, leaf[64]{};
    return StageDir(dirs, xfer, partial, dir, kPathChars) && SlotLeaf(slot, true, leaf, ARRAYSIZE(leaf)) &&
           Join(out, capacity, dir, leaf);
}

bool HandoffFile(const Dirs& dirs, const wchar_t* leaf, wchar_t* out, std::size_t capacity) noexcept {
    return Join(out, capacity, dirs.handoff, leaf);
}

bool WriteText(const Dirs& dirs, const wchar_t* path, const char* text, std::size_t size) noexcept {
    if (!path || !path[0] || (!text && size)) return false;
    if (!EnsureDirectory(dirs.handoff)) return false;
    return lab::WriteFileAtomic(path, text, size);
}

bool ReadText(const wchar_t* path, char* out, std::size_t capacity, std::size_t& size) noexcept {
    size = 0;
    if (!path || !out || capacity < 2) return false;
    out[0] = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER length{};
    bool ok = GetFileSizeEx(file, &length) && length.QuadPart >= 0 && static_cast<unsigned long long>(length.QuadPart) < capacity;
    DWORD read = 0;
    if (ok)
        ok = ReadFile(file, out, static_cast<DWORD>(length.QuadPart), &read, nullptr) && read == static_cast<DWORD>(length.QuadPart);
    CloseHandle(file);
    if (!ok) {
        out[0] = 0;
        return false;
    }
    out[read] = 0;
    size = read;
    return true;
}

bool StatSet(const Dirs& dirs, Stamps& out) noexcept {
    out = Stamps{};
    bool ok = true;
    for (std::uint8_t s = 0; s < wire::kSlots; ++s) {
        wchar_t path[kPathChars]{};
        if (!SlotFile(dirs, s, path, kPathChars) || !StatFile(path, out.slot[s])) ok = false;
    }
    wchar_t local[kPathChars]{}, remote[kPathChars]{};
    if (FAILED(StringCchPrintfW(local, kPathChars, L"%s\\287700\\local", dirs.save)) ||
        FAILED(StringCchPrintfW(remote, kPathChars, L"%s\\remote", dirs.save)))
        return false;
    if (!ScanUnexpected(local, false, out.unexpected)) ok = false;
    if (!ScanUnexpected(remote, true, out.unexpected)) ok = false;
    return ok;
}

void HashBegin(HashJob& job) noexcept {
    job = HashJob{};
    job.active = true;
}

bool HashStep(const Dirs& dirs, HashJob& job, HashCache& cache, core::SetInfo& out, bool remote_primary) noexcept {
    if (!job.active) return false;
    if (job.next == 0 && !StatSet(dirs, job.before)) job.failed = true;
    // At most one actual read per step; cache hits do not count.
    bool read_one = false;
    while (job.next < wire::kSlots && !read_one) {
        const std::uint8_t s = job.next;
        const Stamp& st = job.before.slot[s];
        if (st.present && !job.failed) {
            if (st.size == 0) {
                job.failed = true;  // an empty progress file is never a valid set
            } else if (st.size > wire::kMaxFileBytes) {
                job.oversize = true;
                cache.valid[s] = false;
            } else if (!(cache.valid[s] && SameStamp(cache.stamp[s], st))) {
                read_one = true;
                if (HashSlotFile(dirs, s, st.size, cache.sha[s])) {
                    cache.valid[s] = true;
                    cache.stamp[s] = st;
                } else {
                    cache.valid[s] = false;
                    job.failed = true;
                }
            }
        }
        ++job.next;
    }
    if (job.next < wire::kSlots) return false;
    // Finished: the stamps again for stability, then the set.
    Stamps after{};
    const bool after_ok = StatSet(dirs, after);
    out = core::SetInfo{};
    out.unexpected = job.before.unexpected || after.unexpected;
    out.stable = after_ok && SameStamps(job.before, after);
    std::uint64_t total = 0;
    bool primary = false;
    for (std::uint8_t s = 0; s < wire::kSlots; ++s) {
        const Stamp& st = job.before.slot[s];
        if (!st.present) continue;
        total += st.size;
        // Online 2026-10-22 P1: an old Steam save copy has only remote\TPP_GAME_DATA
        // (+ PERSONAL_DATA) until the game first writes a local slot; for the guest
        // that is its own set (its need is decided), the host still needs 0 or 1.
        if (s == 0 || s == 1 || (remote_primary && s == 4)) primary = true;
        if (out.n >= wire::kMaxFiles) continue;
        wire::FileEntry& entry = out.files[out.n++];
        entry.slot = s;
        entry.size = st.size > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(st.size);
        if (cache.valid[s] && SameStamp(cache.stamp[s], st))
            for (std::size_t i = 0; i < wire::kShaSize; ++i) entry.sha256[i] = cache.sha[s][i];
    }
    // A file over the per-file limit is not read: report a total the core
    // refuses as too_large.
    if (job.oversize && total <= wire::kMaxTotal) total = static_cast<std::uint64_t>(wire::kMaxTotal) + 1;
    out.total = total;
    // An oversize file keeps a zero hash in its entry: the set stays defined
    // and the total carries the refusal.
    job.no_primary = !primary || out.n == 0;
    out.ok = !job.failed && primary && out.n > 0;
    if (out.ok && !wire::SetSha256(out.files, out.n, out.set)) out.ok = false;
    job.active = false;
    return true;
}

bool HashSetNow(const Dirs& dirs, HashCache& cache, core::SetInfo& out, bool remote_primary) noexcept {
    HashJob job{};
    HashBegin(job);
    for (unsigned guard = 0; guard <= wire::kSlots + 1; ++guard)
        if (HashStep(dirs, job, cache, out, remote_primary)) return true;
    return false;
}

const char* SetProblem(const HashJob& job, const core::SetInfo& out) noexcept {
    if (out.ok) return nullptr;
    if (job.failed) return "read";
    if (job.no_primary) return "no_primary";
    return "hash";
}

bool SnapshotRead(const Dirs& dirs, SnapshotBuffer& out) noexcept {
    SnapshotFree(out);
    Stamps before{};
    if (!StatSet(dirs, before)) return false;
    std::uint64_t total = 0;
    bool primary = false;
    for (std::uint8_t s = 0; s < wire::kSlots; ++s) {
        const Stamp& st = before.slot[s];
        if (!st.present) continue;
        if (st.size == 0 || st.size > wire::kMaxFileBytes) return false;
        total += st.size;
        if (s == 0 || s == 1) primary = true;
    }
    if (!primary || !total || total > wire::kMaxTotal) return false;
    auto* base = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, static_cast<SIZE_T>(total), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!base) return false;
    out.base = base;
    out.bytes = static_cast<std::size_t>(total);
    core::SetInfo& info = out.info;
    info = core::SetInfo{};
    std::size_t offset = 0;
    bool ok = true;
    for (std::uint8_t s = 0; s < wire::kSlots && ok; ++s) {
        const Stamp& st = before.slot[s];
        if (!st.present) continue;
        const auto size = static_cast<std::size_t>(st.size);
        if (!ReadSlotInto(dirs, s, base + offset, size)) {
            ok = false;
            break;
        }
        out.data[s] = base + offset;
        wire::FileEntry& entry = info.files[info.n++];
        entry.slot = s;
        entry.size = static_cast<std::uint32_t>(size);
        link::Sha256Digest(base + offset, size, entry.sha256);
        offset += size;
    }
    Stamps after{};
    if (ok) ok = StatSet(dirs, after);
    if (!ok) {
        SnapshotFree(out);
        return false;
    }
    info.unexpected = before.unexpected || after.unexpected;
    info.stable = SameStamps(before, after);
    info.total = total;
    info.ok = wire::SetSha256(info.files, info.n, info.set);
    if (!info.ok || !info.stable) {
        SnapshotFree(out);
        return false;
    }
    return true;
}

bool SnapshotVerify(const Dirs& dirs, const SnapshotBuffer& first) noexcept {
    if (!first.base || !first.info.ok) return false;
    Stamps now{};
    if (!StatSet(dirs, now) || now.unexpected != first.info.unexpected) return false;
    unsigned index = 0;
    for (std::uint8_t s = 0; s < wire::kSlots; ++s) {
        const bool expected = index < first.info.n && first.info.files[index].slot == s;
        if (now.slot[s].present != expected) return false;
        if (!expected) continue;
        const wire::FileEntry& entry = first.info.files[index++];
        if (now.slot[s].size != entry.size) return false;
        std::uint8_t sha[wire::kShaSize]{};
        if (!HashSlotFile(dirs, s, entry.size, sha)) return false;
        for (std::size_t i = 0; i < wire::kShaSize; ++i)
            if (sha[i] != entry.sha256[i]) return false;
    }
    return index == first.info.n;
}

void SnapshotFree(SnapshotBuffer& buffer) noexcept {
    if (buffer.base) VirtualFree(buffer.base, 0, MEM_RELEASE);
    buffer = SnapshotBuffer{};
}

bool StageWrite(const Dirs& dirs, std::uint32_t xfer, std::uint8_t slot, std::uint32_t offset, const std::uint8_t* data,
                std::size_t size) noexcept {
    if (!xfer || slot >= wire::kSlots || !data || !size || size > wire::kChunk ||
        static_cast<std::uint64_t>(offset) + size > wire::kMaxFileBytes)
        return false;
    wchar_t dir[kPathChars]{}, path[kPathChars]{}, folder[kPathChars]{};
    if (!StageDir(dirs, xfer, true, dir, kPathChars) || !StageFile(dirs, xfer, true, slot, path, kPathChars)) return false;
    if (!EnsureDirectory(dirs.handoff) || !EnsureDirectory(dir)) return false;
    if (wire::SlotIsRemote(slot)) {
        if (!Join(folder, kPathChars, dir, L"remote") || !EnsureDirectory(folder)) return false;
    } else {
        if (!Join(folder, kPathChars, dir, L"287700") || !EnsureDirectory(folder)) return false;
        if (!Join(folder, kPathChars, dir, L"287700\\local") || !EnsureDirectory(folder)) return false;
    }
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, offset == 0 ? CREATE_ALWAYS : OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    // Chunks arrive in order: the file is exactly `offset` bytes long here.
    LARGE_INTEGER length{}, position{};
    position.QuadPart = offset;
    bool ok = GetFileSizeEx(file, &length) && length.QuadPart == static_cast<LONGLONG>(offset) &&
              SetFilePointerEx(file, position, nullptr, FILE_BEGIN);
    DWORD written = 0;
    if (ok) ok = WriteFile(file, data, static_cast<DWORD>(size), &written, nullptr) && written == size;
    if (!CloseHandle(file)) ok = false;
    return ok;
}

bool WriteStageManifest(const Dirs& dirs, std::uint32_t xfer, const char* text, std::size_t size) noexcept {
    wchar_t dir[kPathChars]{}, path[kPathChars]{};
    if (!text || !size || !StageDir(dirs, xfer, true, dir, kPathChars) || !IsDirectory(dir) ||
        !Join(path, kPathChars, dir, L"handoff.json"))
        return false;
    return lab::WriteFileAtomic(path, text, size);
}

bool CommitStage(const Dirs& dirs, std::uint32_t xfer, const wire::FileEntry* files, unsigned n,
                 const std::uint8_t (&set)[wire::kShaSize]) noexcept {
    if (!files || !n || n > wire::kMaxFiles) return false;
    wchar_t partial[kPathChars]{}, final_dir[kPathChars]{}, manifest[kPathChars]{};
    if (!StageDir(dirs, xfer, true, partial, kPathChars) || !StageDir(dirs, xfer, false, final_dir, kPathChars) ||
        !Join(manifest, kPathChars, partial, L"handoff.json") || !IsFile(manifest))
        return false;
    // Every staged file equals its manifest entry, and nothing else is staged.
    bool primary = false;
    for (unsigned i = 0; i < n; ++i) {
        const wire::FileEntry& entry = files[i];
        if (entry.slot >= wire::kSlots || (i && files[i - 1].slot >= entry.slot)) return false;
        if (entry.slot <= 1) primary = true;
        wchar_t path[kPathChars]{};
        Stamp st{};
        if (!StageFile(dirs, xfer, true, entry.slot, path, kPathChars) || !StatFile(path, st) || !st.present || st.size != entry.size)
            return false;
        std::uint8_t sha[wire::kShaSize]{};
        if (!HashLocalFile(path, entry.size, sha)) return false;
        for (std::size_t k = 0; k < wire::kShaSize; ++k)
            if (sha[k] != entry.sha256[k]) return false;
    }
    for (std::uint8_t s = 0; s < wire::kSlots; ++s) {
        bool listed = false;
        for (unsigned i = 0; i < n; ++i) listed = listed || files[i].slot == s;
        wchar_t path[kPathChars]{};
        Stamp st{};
        if (!listed && StageFile(dirs, xfer, true, s, path, kPathChars) && StatFile(path, st) && st.present) return false;
    }
    std::uint8_t computed[wire::kShaSize]{};
    if (!primary || !wire::SetSha256(files, n, computed)) return false;
    for (std::size_t k = 0; k < wire::kShaSize; ++k)
        if (computed[k] != set[k]) return false;
    if (IsDirectory(final_dir)) return false;  // never merge into an existing stage
    return MoveFileExW(partial, final_dir, MOVEFILE_WRITE_THROUGH) != FALSE;
}

bool WriteRequest(const Dirs& dirs, std::uint32_t xfer, wire::Mode mode, const char* text, std::size_t size) noexcept {
    // Only scene / always request a restart; stage never does (P2-11).
    if (mode != wire::Mode::scene && mode != wire::Mode::always) return false;
    wchar_t partial[kPathChars]{}, final_dir[kPathChars]{}, manifest[kPathChars]{}, path[kPathChars]{};
    if (!text || !size || !StageDir(dirs, xfer, true, partial, kPathChars) || !StageDir(dirs, xfer, false, final_dir, kPathChars) ||
        !Join(manifest, kPathChars, final_dir, L"handoff.json") || !HandoffFile(dirs, L"request.json", path, kPathChars))
        return false;
    // Written last: the stage is committed (never a .partial) with its manifest.
    if (IsDirectory(partial) || !IsDirectory(final_dir) || !IsFile(manifest)) return false;
    return lab::WriteFileAtomic(path, text, size);
}

bool DeleteStageDir(const wchar_t* dir) noexcept {
    if (!dir || !dir[0]) return false;
    if (!IsDirectory(dir)) return true;
    const bool all = DeleteKnownStageFiles(dir);
    RemoveStageFolders(dir);
    return all;
}

bool DeleteStage(const Dirs& dirs, std::uint32_t xfer) noexcept {
    if (!xfer) return false;
    bool all = true;
    // request.json first, when it names this transfer (or names none).
    wchar_t request[kPathChars]{};
    if (HandoffFile(dirs, L"request.json", request, kPathChars)) {
        if (IsFile(request)) {
            char text[1024]{};
            std::size_t size = 0;
            char needle[24]{};
            StringCchPrintfA(needle, ARRAYSIZE(needle), "\"xfer\":\"%08X\"", xfer);
            const bool readable = ReadText(request, text, sizeof(text), size);
            const bool names_xfer = readable && std::strstr(text, "\"xfer\":\"") != nullptr;
            const bool ours = readable && std::strstr(text, needle) != nullptr;
            if (!readable || !names_xfer || ours) all = DeleteKnown(request) && all;
        }
        wchar_t temporary[kPathChars]{};
        if (lab::MakeTemporaryPath(request, temporary, kPathChars)) DeleteKnown(temporary);
    }
    wchar_t partial[kPathChars]{}, final_dir[kPathChars]{};
    if (!StageDir(dirs, xfer, true, partial, kPathChars) || !StageDir(dirs, xfer, false, final_dir, kPathChars)) return false;
    all = DeleteStageDir(partial) && all;
    all = DeleteStageDir(final_dir) && all;
    return all;
}

unsigned CleanLeftovers(const Dirs& dirs, std::uint32_t own_pid,
                        bool (*request_pid)(const char* text, std::size_t size, std::uint32_t& pid) noexcept) noexcept {
    unsigned removed = 0;
    wchar_t pattern[kPathChars]{};
    if (Join(pattern, kPathChars, dirs.handoff, L"*.partial")) {
        WIN32_FIND_DATAW fd{};
        HANDLE find = FindFirstFileW(pattern, &fd);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                // Exactly <8 hex>.partial folders.
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || WideLength(fd.cFileName) != 16 || !IsHex8(fd.cFileName) ||
                    lstrcmpiW(fd.cFileName + 8, L".partial") != 0)
                    continue;
                wchar_t dir[kPathChars]{};
                // Counted when the folder is gone (a foreign file keeps it).
                if (Join(dir, kPathChars, dirs.handoff, fd.cFileName) && DeleteStageDir(dir) && !IsDirectory(dir)) ++removed;
            } while (FindNextFileW(find, &fd));
            FindClose(find);
        }
    }
    wchar_t request[kPathChars]{};
    if (HandoffFile(dirs, L"request.json", request, kPathChars) && IsFile(request)) {
        char text[1024]{};
        std::size_t size = 0;
        std::uint32_t pid = 0;
        const bool parsed = ReadText(request, text, sizeof(text), size) && request_pid && request_pid(text, size, pid);
        if ((!parsed || pid != own_pid) && DeleteFileW(request)) ++removed;
    }
    return removed;
}

bool WriteRejoin(const Dirs& dirs, bool (*build)(char* out, std::size_t capacity) noexcept) noexcept {
    if (!build || !dirs.rejoin[0]) return false;
    char line[160]{};
    bool ok = build(line, sizeof(line) - 2);
    std::size_t length = 0;
    if (ok) {
        while (length < sizeof(line) && line[length]) ++length;
        ok = length > 0 && length + 2 < sizeof(line);
    }
    if (ok) {
        line[length++] = '\r';
        line[length++] = '\n';
        ok = lab::WriteFileAtomic(dirs.rejoin, line, length);
    }
    link::SecureZero(line, sizeof(line));
    return ok;
}

const Window* PickExitWindow(const Window* windows, unsigned count, std::uint32_t pid) noexcept {
    const Window* best = nullptr;
    for (unsigned i = 0; windows && i < count; ++i) {
        const Window& w = windows[i];
        if (w.pid != pid || !w.visible || w.owned || !w.handle) continue;
        if (!best || w.area > best->area) best = &w;
    }
    return best;
}

bool PostExit(EnumWindowsFn enumerate, PostCloseFn post, std::uint32_t pid) noexcept {
    if (!enumerate || !post) return false;
    static Window windows[256];  // the hook thread only
    unsigned count = enumerate(windows, 256);
    if (count > 256) count = 256;
    const Window* target = PickExitWindow(windows, count, pid);
    return target && post(target->handle);
}

namespace {
struct EnumState {
    Window* out;
    unsigned capacity, count;
};
BOOL CALLBACK CollectWindow(HWND window, LPARAM parameter) noexcept {
    auto* state = reinterpret_cast<EnumState*>(parameter);
    if (state->count >= state->capacity) return FALSE;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    // Only this process's windows: a desktop has hundreds of hidden top-level
    // windows, and the game's must not fall past the capacity.
    if (pid != GetCurrentProcessId()) return TRUE;
    RECT rect{};
    const bool sized = GetWindowRect(window, &rect) != FALSE;
    Window& w = state->out[state->count++];
    w.handle = window;
    w.pid = pid;
    w.visible = IsWindowVisible(window) != FALSE;
    w.owned = GetWindow(window, GW_OWNER) != nullptr;
    w.area = sized ? static_cast<std::int64_t>(rect.right - rect.left) * static_cast<std::int64_t>(rect.bottom - rect.top) : 0;
    return TRUE;
}
}  // namespace

unsigned EnumTopLevelWindows(Window* out, unsigned capacity) noexcept {
    if (!out || !capacity) return 0;
    EnumState state{out, capacity, 0};
    EnumWindows(&CollectWindow, reinterpret_cast<LPARAM>(&state));
    return state.count;
}

bool PostWindowClose(void* handle) noexcept {
    return handle && PostMessageW(static_cast<HWND>(handle), WM_CLOSE, 0, 0) != FALSE;
}

}  // namespace io

// ============================================================ module
namespace {
namespace core = ::mgscoop::save_handoff_core;
namespace wire = ::mgscoop::save_handoff_wire;
namespace hstate = ::mgscoop::save_handoff_state;

// Any-thread state (atomics).
volatile LONG g_configured = 0;
volatile LONG g_role = 0;         // 1 host, 2 guest (recorded even when the transfer is off)
volatile LONG g_travel_lock = 0;  // the effective lab.ini travel_lock
volatile LONG g_mode = 0;         // wire::Mode when configured
volatile LONG g_lua_code = 0;     // the published LuaCode (CFG r[12])
volatile LONG g_host_state_seen = 0;
// Host: a guest restarted on this copy's save in this process (the token
// partner_restarting was published once); PeerOnHandedSave, item 2.
volatile LONG g_handed_off = 0;
volatile LONG g_shutdown = 0;
// NoteMission (runtime / termination thread).
volatile LONG g_mission = 0, g_mission_terminating = 0, g_mission_seq = 0;

// The Lua report seqlock (single writer: HandleLua on the runtime thread).
volatile LONG g_report_seq = 0;
volatile LONG g_report_mission = 0, g_report_flags = 0, g_report_count = 0;
volatile LONG64 g_report_at = 0;
// CONTINUE / DECLINE mailbox (leaf lock; the hook thread pops).
SRWLOCK g_mail_lock = SRWLOCK_INIT;
struct Mail {
    std::uint8_t op;
    std::uint32_t a;
};
constexpr unsigned kMailSlots = 8;
Mail g_mail[kMailSlots]{};
unsigned g_mail_head = 0, g_mail_count = 0;
// The SAVE receipt rate gate (runtime thread only, under the receipt lock).
std::uint64_t g_rate_window_ms = 0;
unsigned g_rate_count = 0;

// Hook-thread state (the link's receive / transport thread, after Configure).
LogFn g_log = nullptr;
wire::Mode g_policy = wire::Mode::off;
io::Dirs g_dirs{};
core::Core g_core;
core::Action g_action{};
io::HashCache g_cache{};
io::HashJob g_hash{};
core::SetInfo g_hash_out{};
// The snapshot job: phase 0 idle, 1 first read pending, 2 second read pending.
struct SnapJob {
    std::uint8_t phase;
    std::uint64_t started_ms, first_ms;
};
SnapJob g_snap_job{};
io::SnapshotBuffer g_snap{};
core::Snapshot g_snap_view{};
bool g_build_known = false, g_build_ok = false;
std::uint32_t g_build_session = 0;
char g_build[link::kBuildSize + 1]{};
LONG g_report_seen = 0, g_mission_seen = 0;
core::Report g_report{};
bool g_have_report = false;
bool g_shutdown_done = false;
bool g_applied_pending = false;
core::Applied g_applied{};
std::uint64_t g_io_log_ms = 0;
char g_set_logged[24]{};  // the last "save_handoff=set" key ("ok:<8 hex>" or "no:<why>")
// P2-10 exit_slow after a WM_CLOSE: a one-shot timer-queue timer, independent of
// the core and the link thread (the overlay's PartnerShutdown stops the core and
// a Steam link parks). g_exit_armed holds the armed generation (0 disarmed); the
// callback fires only for its own generation, once. Disarmed when the core
// leaves the exiting token (a veto or the guest's own Continue).
LONG g_exit_armed = 0, g_exit_gen = 0;
HANDLE g_exit_timer = nullptr;  // hook thread only
DWORD g_exit_slow_ms = static_cast<DWORD>(core::kExitSlowMs);
io::EnumWindowsFn g_exit_enum = &io::EnumTopLevelWindows;
io::PostCloseFn g_exit_post = &io::PostWindowClose;
std::uint8_t g_peer_buffer[link::kMaxReliableMessage];
char g_text[core::kManifestJsonMax + core::kProvenanceJsonMax];  // the hook thread's document buffer

constexpr std::uint64_t kSnapshotWaitMs = 10000;   // a snapshot read that cannot start in time is refused
constexpr std::uint64_t kTicksPerSecond = 10000000ull;  // FILETIME

std::uint64_t Now() noexcept { return partner_net::NowMs(); }

void Write(const char* text, std::uint64_t now_ms) noexcept {
    if (!g_log) return;
    char line[core::kLogChars + 48]{};
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "%s; t_ms=%llu\r\n", text, static_cast<unsigned long long>(now_ms))))
        g_log(line);
}

// A failed file action, at most once per 10 s (the core logs the outcome).
void LogIo(const char* op, std::uint64_t now_ms) noexcept {
    const DWORD error = GetLastError();
    if (g_io_log_ms && now_ms - g_io_log_ms < 10000) return;
    g_io_log_ms = now_ms;
    char text[96]{};
    if (SUCCEEDED(StringCchPrintfA(text, ARRAYSIZE(text), "save_handoff=io_error; op=%s; error=%lu", op, error))) Write(text, now_ms);
}

void FormatUtc(const SYSTEMTIME& st, char (&out)[core::kUtcCapacity]) noexcept {
    if (FAILED(StringCchPrintfA(out, ARRAYSIZE(out), "%04u-%02u-%02uT%02u:%02u:%02uZ", st.wYear, st.wMonth, st.wDay, st.wHour,
                                st.wMinute, st.wSecond)))
        out[0] = 0;
}
void UtcNow(char (&out)[core::kUtcCapacity]) noexcept {
    SYSTEMTIME st{};
    GetSystemTime(&st);
    FormatUtc(st, out);
}
std::uint32_t RandomU32() noexcept {
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        std::uint32_t value = 0;
        if (BCRYPT_SUCCESS(BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&value), sizeof(value), BCRYPT_USE_SYSTEM_PREFERRED_RNG)) &&
            value)
            return value;
    }
    const auto fallback = static_cast<std::uint32_t>(GetTickCount64() ^ (static_cast<std::uint64_t>(GetCurrentProcessId()) << 16));
    return fallback ? fallback : 1u;
}
// "yyyy-MM-ddTHH:mm:ssZ" -> FILETIME ticks (0 when malformed).
std::uint64_t ParseUtc(const char* text) noexcept {
    if (!text) return 0;
    unsigned v[6]{};
    const char* p = text;
    static constexpr char kSeparators[6] = {'-', '-', 'T', ':', ':', 'Z'};
    static constexpr unsigned kWidths[6] = {4, 2, 2, 2, 2, 2};
    for (unsigned i = 0; i < 6; ++i) {
        for (unsigned d = 0; d < kWidths[i]; ++d, ++p) {
            if (*p < '0' || *p > '9') return 0;
            v[i] = v[i] * 10 + static_cast<unsigned>(*p - '0');
        }
        if (*p++ != kSeparators[i]) return 0;
    }
    if (*p) return 0;
    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(v[0]);
    st.wMonth = static_cast<WORD>(v[1]);
    st.wDay = static_cast<WORD>(v[2]);
    st.wHour = static_cast<WORD>(v[3]);
    st.wMinute = static_cast<WORD>(v[4]);
    st.wSecond = static_cast<WORD>(v[5]);
    FILETIME ft{};
    if (!SystemTimeToFileTime(&st, &ft)) return 0;
    return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}
// This process's creation time (FILETIME ticks) and its UTC text.
std::uint64_t ProcessStart(char (&utc)[core::kUtcCapacity]) noexcept {
    utc[0] = 0;
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0;
    SYSTEMTIME st{};
    if (FileTimeToSystemTime(&created, &st)) FormatUtc(st, utc);
    return (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}

bool RequestPid(const char* text, std::size_t size, std::uint32_t& pid) noexcept {
    core::Request request{};
    if (!core::ParseRequest(text, size, request)) return false;
    pid = request.pid;
    return true;
}

// The Lua report is fresh (<= kPhaseFreshMs) with saving=0: files may be read.
bool FilesQuiet(std::uint64_t now_ms) noexcept {
    return g_have_report && !g_report.saving && now_ms >= g_report.at_ms && now_ms - g_report.at_ms <= core::kPhaseFreshMs;
}

// A title (4xxxx) or start-up mission is no partner mission for the exit gate.
std::uint32_t GameplayMission(std::uint32_t mission) noexcept {
    return mission < 10000 || mission / 10000 == 4 ? 0u : mission;
}

void ReleaseSnapshot() noexcept {
    io::SnapshotFree(g_snap);
    g_snap_view = core::Snapshot{};
    g_snap_job = SnapJob{};
}

// The exit_slow timer callback (a timer-queue thread): never touches g_core.
VOID CALLBACK ExitSlowFired(PVOID parameter, BOOLEAN) noexcept {
    const auto gen = static_cast<LONG>(reinterpret_cast<LONG_PTR>(parameter));
    if (!gen || InterlockedCompareExchange(&g_exit_armed, 0, gen) != gen) return;  // disarmed or superseded
    Write("save_handoff=exit_slow", Now());
    coop_hud::PostSaveHandoff(static_cast<std::uint8_t>(hstate::Toast::g9_quit_manually), 0, 0);
}

// A WM_CLOSE was posted: the process should be gone within kExitSlowMs.
void ArmExitSlow() noexcept {
    LONG gen = InterlockedIncrement(&g_exit_gen);
    if (!gen) gen = InterlockedIncrement(&g_exit_gen);
    InterlockedExchange(&g_exit_armed, gen);
    if (g_exit_timer) {
        DeleteTimerQueueTimer(nullptr, g_exit_timer, nullptr);  // non-blocking; an old generation no-ops anyway
        g_exit_timer = nullptr;
    }
    HANDLE timer = nullptr;
    if (CreateTimerQueueTimer(&timer, nullptr, &ExitSlowFired, reinterpret_cast<PVOID>(static_cast<LONG_PTR>(gen)), g_exit_slow_ms, 0,
                              WT_EXECUTEONLYONCE))
        g_exit_timer = timer;
    else
        InterlockedCompareExchange(&g_exit_armed, 0, gen);
}

void PublishView(hstate::View view) noexcept {
    // The core left exiting (ABORT user / not_title): no exit_slow or G9 any more.
    if (view.token != static_cast<std::uint8_t>(hstate::Token::exiting)) InterlockedExchange(&g_exit_armed, 0);
    view.available = true;
    view.role = static_cast<std::uint8_t>(g_role);
    hstate::Publish(view);
    InterlockedExchange(&g_lua_code, static_cast<LONG>(view.lua_code));
    if (g_role == 1 && view.token == static_cast<std::uint8_t>(hstate::Token::partner_restarting)) InterlockedExchange(&g_handed_off, 1);
    partner_net::PublishHandoffState(view.token, view.pct);
}

void Execute(const core::Action& a, std::uint64_t now_ms, bool& chunk_refused) noexcept {
    switch (a.kind) {
    case core::ActionKind::send: {
        bool queued = false;
        // No channel-4 byte unless the peer build passed the gate (P2-4), and
        // no CHUNK after the first refused one of this drain.
        const bool is_chunk = a.message == wire::Kind::chunk;
        if (g_build_ok && !(is_chunk && chunk_refused) && a.size && a.size <= sizeof(a.data)) {
            const link::ReliableResult result = partner_net::SendReliable(
                wire::kChannel, a.data, a.size, a.latest ? link::ReliableMode::latest : link::ReliableMode::append);
            queued = result == link::ReliableResult::queued || result == link::ReliableResult::replaced;
        }
        if (!queued && is_chunk) chunk_refused = true;
        g_core.OnSendResult(a, queued, now_ms);
        break;
    }
    case core::ActionKind::hash_set:
        io::HashBegin(g_hash);
        break;
    case core::ActionKind::snapshot:
        ReleaseSnapshot();
        g_snap_job.phase = 1;
        g_snap_job.started_ms = now_ms;
        break;
    case core::ActionKind::release_snapshot:
        ReleaseSnapshot();
        break;
    case core::ActionKind::stage_write: {
        const bool ok = io::StageWrite(g_dirs, a.xfer, a.slot, a.offset, a.data, a.size);
        if (!ok) LogIo("stage_write", now_ms);
        g_core.OnStageDone(ok, now_ms);
        break;
    }
    case core::ActionKind::write_manifest: {
        const std::size_t size = core::FormatManifest(g_core.manifest(), g_text, sizeof(g_text));
        const bool ok = size && io::WriteStageManifest(g_dirs, a.xfer, g_text, size);
        if (!ok) LogIo("write_manifest", now_ms);
        g_core.OnStageDone(ok, now_ms);
        break;
    }
    case core::ActionKind::commit_stage: {
        const core::Manifest& m = g_core.manifest();
        const bool ok = m.xfer == a.xfer && io::CommitStage(g_dirs, a.xfer, m.files, m.n, m.set);
        if (!ok) LogIo("commit_stage", now_ms);
        g_core.OnStageDone(ok, now_ms);
        break;
    }
    case core::ActionKind::write_request: {
        const core::Request& r = g_core.request();
        const std::size_t size = core::FormatRequest(r, g_text, sizeof(g_text));
        // The lab.ini policy and the request's own mode must both be scene/always.
        const bool ok = size && r.mode == g_policy && io::WriteRequest(g_dirs, r.xfer, g_policy, g_text, size);
        if (!ok) LogIo("write_request", now_ms);
        g_core.OnStageDone(ok, now_ms);
        break;
    }
    case core::ActionKind::write_rejoin:
        if (!io::WriteRejoin(g_dirs, &partner_net::BuildGuestJoinCode)) Write("save_handoff=rejoin; written=0", now_ms);
        break;
    case core::ActionKind::write_provenance: {
        const std::size_t size = g_core.provenance().Format(g_text, sizeof(g_text));
        wchar_t path[io::kPathChars]{};
        if (!size || !io::HandoffFile(g_dirs, L"provenance.json", path, io::kPathChars) || !io::WriteText(g_dirs, path, g_text, size))
            LogIo("write_provenance", now_ms);
        break;
    }
    case core::ActionKind::delete_stage: {
        const bool ok = io::DeleteStage(g_dirs, a.xfer);
        if (!ok) LogIo("delete_stage", now_ms);
        g_core.OnStageDone(ok, now_ms);
        break;
    }
    case core::ActionKind::post_exit: {
        const bool found = io::PostExit(g_exit_enum, g_exit_post, GetCurrentProcessId());
        // no_window: the core keeps ticking and owns exit_slow (G9 shown at once).
        if (found) ArmExitSlow();
        g_core.OnExitPosted(found, now_ms);
        break;
    }
    case core::ActionKind::toast: {
        char text[32]{};
        StringCchPrintfA(text, ARRAYSIZE(text), "%lu %lu", static_cast<unsigned long>(a.a), static_cast<unsigned long>(a.b));
        coop_hud::Post(coop_hud::EventKind::save_handoff, static_cast<std::int32_t>(a.code), text);
        break;
    }
    case core::ActionKind::log:
        Write(a.text, now_ms);
        break;
    case core::ActionKind::publish:
        PublishView(a.view);
        break;
    default:
        break;
    }
}

// Runs every queued action in order; a report may queue more (same loop).
void Drain(std::uint64_t now_ms) noexcept {
    bool chunk_refused = false;
    for (unsigned guard = 0; guard < 4096 && g_core.PopAction(g_action); ++guard) Execute(g_action, now_ms, chunk_refused);
}

// The own on-disk set, one line per change (online 2026-10-22 P1/P8: a set
// without slot 0/1 was silent): "save_handoff=set; ok=1; set=..; files=..;
// local=0|1" or "save_handoff=set; ok=0; why=read|no_primary|hash; files=..".
// An unstable pass (a file changed while it was read) logs nothing.
void LogSet(std::uint64_t now_ms) noexcept {
    const core::SetInfo& s = g_hash_out;
    if (s.ok && !s.stable) return;
    char key[24]{}, text[128]{};
    if (const char* problem = io::SetProblem(g_hash, s)) {
        StringCchPrintfA(key, ARRAYSIZE(key), "no:%s", problem);
        StringCchPrintfA(text, ARRAYSIZE(text), "save_handoff=set; ok=0; why=%s; files=%u", problem, unsigned{s.n});
    } else {
        char hex[wire::kShaHexChars + 1]{};
        wire::FormatShaHex(s.set, hex);
        bool local = false;
        for (unsigned i = 0; i < s.n; ++i) local = local || !wire::SlotIsRemote(s.files[i].slot);
        StringCchPrintfA(key, ARRAYSIZE(key), "ok:%.8s", hex);
        StringCchPrintfA(text, ARRAYSIZE(text), "save_handoff=set; ok=1; set=%.8s; files=%u; local=%u", hex, unsigned{s.n},
                         local ? 1u : 0u);
    }
    if (lstrcmpA(key, g_set_logged) == 0) return;
    StringCchCopyA(g_set_logged, ARRAYSIZE(g_set_logged), key);
    Write(text, now_ms);
}

void StepHash(std::uint64_t now_ms) noexcept {
    if (!g_hash.active || !FilesQuiet(now_ms)) return;
    // The guest's own set may be remote-only (P1); the host's never is (its snapshot needs slot 0/1).
    if (!io::HashStep(g_dirs, g_hash, g_cache, g_hash_out, g_role == 2)) return;
    LogSet(now_ms);
    g_core.OnSetHashed(g_hash_out, now_ms);
    Drain(now_ms);
}

void StepSnapshot(std::uint64_t now_ms) noexcept {
    if (!g_snap_job.phase) return;
    const bool quiet = FilesQuiet(now_ms);
    bool done = false, ok = false;
    if (g_snap_job.phase == 1) {
        if (quiet) {
            if (io::SnapshotRead(g_dirs, g_snap)) {
                g_snap_job.phase = 2;
                g_snap_job.first_ms = now_ms;
            } else {
                done = true;
            }
        } else if (now_ms - g_snap_job.started_ms > kSnapshotWaitMs) {
            done = true;
        }
    } else if (now_ms - g_snap_job.first_ms >= core::kSnapshotGapMs) {
        if (quiet) {
            done = true;
            ok = io::SnapshotVerify(g_dirs, g_snap);
        } else if (now_ms - g_snap_job.first_ms > kSnapshotWaitMs) {
            done = true;
        }
    }
    if (!done) return;
    g_snap_job = SnapJob{};
    if (ok) {
        g_snap_view = core::Snapshot{};
        g_snap_view.info = g_snap.info;
        for (unsigned s = 0; s < wire::kSlots; ++s) g_snap_view.data[s] = g_snap.data[s];
        g_core.OnSnapshotRead(true, &g_snap_view, now_ms);
    } else {
        ReleaseSnapshot();
        g_core.OnSnapshotRead(false, nullptr, now_ms);
    }
    Drain(now_ms);
}

// The peer build gate (P2-4), checked again on every new session or build string.
void CheckBuild(std::uint64_t now_ms) noexcept {
    const partner_net::LinkInfo info = partner_net::GetLinkInfo();
    if (g_build_known && info.session == g_build_session && lstrcmpA(info.remote_build, g_build) == 0) return;
    g_build_known = true;
    g_build_session = info.session;
    StringCchCopyA(g_build, ARRAYSIZE(g_build), info.remote_build);
    g_build_ok = g_build[0] && wire::RemoteBuildAtLeast(g_build, wire::kHandoffMinMajor, wire::kHandoffMinMinor, wire::kHandoffMinPatch);
    g_core.OnBuildOk(g_build_ok, g_build);
    Drain(now_ms);
}

// The newest Lua report (seqlock read; a torn read waits for the next tick).
bool ReadReport(core::Report& out) noexcept {
    const LONG before = g_report_seq;
    if (before & 1) return false;
    const auto mission = static_cast<std::uint32_t>(g_report_mission);
    const auto flags = static_cast<std::uint32_t>(g_report_flags);
    const auto at = static_cast<std::uint64_t>(g_report_at);
    MemoryBarrier();
    if (g_report_seq != before) return false;
    out = core::Report{};
    out.mission = mission;
    out.title = (flags & 1u) != 0;
    out.story = static_cast<wire::Story>((flags >> 1) & 3u);
    out.saving = (flags & 8u) != 0;
    out.ingame = (flags & 16u) != 0;
    out.at_ms = at;
    return true;
}

bool PopMail(Mail& out) noexcept {
    bool taken = false;
    AcquireSRWLockExclusive(&g_mail_lock);
    if (g_mail_count) {
        out = g_mail[g_mail_head];
        g_mail_head = (g_mail_head + 1) % kMailSlots;
        --g_mail_count;
        taken = true;
    }
    ReleaseSRWLockExclusive(&g_mail_lock);
    return taken;
}

void OnIdleHook(std::uint64_t now_ms) noexcept {
    if (!g_configured || g_shutdown_done) return;
    if (g_shutdown) {
        // PartnerShutdown: ABORT shutdown when mid-transfer, then nothing more.
        g_shutdown_done = true;
        g_core.OnShutdown(now_ms);
        Drain(now_ms);
        ReleaseSnapshot();
        g_hash = io::HashJob{};
        return;
    }
    if (g_applied_pending) {
        g_applied_pending = false;
        g_core.OnAppliedSeen(g_applied, now_ms);
        Drain(now_ms);
    }
    CheckBuild(now_ms);
    const LONG reports = g_report_count;
    if (reports != g_report_seen) {
        core::Report report{};
        if (ReadReport(report)) {
            g_report_seen = reports;
            g_report = report;
            g_have_report = true;
            g_core.OnLua(report, now_ms);
            Drain(now_ms);
        }
    }
    Mail mail{};
    while (PopMail(mail)) {
        if (mail.op == 2) g_core.OnContinue(mail.a, now_ms);
        else g_core.OnDecline(mail.a == 2 ? core::DeclineSource::f9 : core::DeclineSource::second_continue, now_ms);
        Drain(now_ms);
    }
    if (hstate::TakeKeepOwnSave()) {
        g_core.OnDecline(core::DeclineSource::f9, now_ms);
        Drain(now_ms);
    }
    const LONG mission_seq = g_mission_seq;
    if (mission_seq != g_mission_seen) {
        g_mission_seen = mission_seq;
        g_core.OnPartnerMission(GameplayMission(static_cast<std::uint32_t>(g_mission)), g_mission_terminating != 0, now_ms);
        Drain(now_ms);
    }
    for (unsigned i = 0; i < 32; ++i) {
        std::size_t size = 0;
        if (!partner_net::DrainReliable(wire::kChannel, g_peer_buffer, sizeof(g_peer_buffer), size)) break;
        // GuestHandoffActive: a STATE from the host (header, kind 1, role 1).
        if (size == wire::kStateSize && g_peer_buffer[0] == wire::kMagic &&
            g_peer_buffer[2] == static_cast<std::uint8_t>(wire::Kind::state) &&
            g_peer_buffer[8] == static_cast<std::uint8_t>(wire::Role::host))
            InterlockedExchange(&g_host_state_seen, 1);
        g_core.OnPeer(g_peer_buffer, size, now_ms);
        Drain(now_ms);
    }
    // "save_handoff=bad_msg" is the core's (it alone knows the role/direction reasons).
    StepHash(now_ms);
    StepSnapshot(now_ms);
    g_core.Tick(now_ms, partner_net::ReliableQueued(wire::kChannel));
    Drain(now_ms);
}

void OnSessionHook(std::uint32_t session) noexcept {
    if (!g_configured || g_shutdown_done) return;
    const std::uint64_t now = Now();
    g_build_known = false;  // the next tick checks the new peer's build
    g_core.OnSession(session, now);
    Drain(now);
}

void OnPeerCloseHook() noexcept {
    if (!g_configured || g_shutdown_done) return;
    const std::uint64_t now = Now();
    g_core.OnPeerClose(now);
    Drain(now);
}

const partner_net::HandoffHooks kHooks{&OnIdleHook, &OnSessionHook, &OnPeerCloseHook};

void LogDisabled(LogFn log, const char* reason) noexcept {
    if (!log) return;
    char line[128]{};
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "save_handoff=disabled; reason=%s; t_ms=%llu\r\n", reason,
                                   static_cast<unsigned long long>(Now()))))
        log(line);
}

// An applied.json from the tooling's apply right before this process (the
// Play-Coop relaunch): at most 30 min before and 5 min after the start.
bool AppliedForThisStart(const core::Applied& applied, std::uint64_t process_start) noexcept {
    const std::uint64_t at = ParseUtc(applied.applied_utc);
    if (!at || !process_start) return false;
    if (at > process_start + 300 * kTicksPerSecond) return false;
    return at >= process_start || process_start - at <= 1800 * kTicksPerSecond;
}

// The guest's <save_dir>-handoff\applied.json when the tooling wrote it for
// this process start (toast G10, token applied).
bool ReadApplied(const io::Dirs& dirs, std::uint64_t process_start, core::Applied& out) noexcept {
    wchar_t path[io::kPathChars]{};
    std::size_t size = 0;
    out = core::Applied{};
    return io::HandoffFile(dirs, L"applied.json", path, io::kPathChars) && io::ReadText(path, g_text, sizeof(g_text), size) &&
           core::ParseApplied(g_text, size, out) && AppliedForThisStart(out, process_start);
}
}  // namespace

bool Configure(const Config& config) noexcept {
    // Always recorded: the travel lock works with save_handoff=off.
    InterlockedExchange(&g_role, static_cast<LONG>(config.role == 2 ? 2 : config.role == 1 ? 1 : 0));
    InterlockedExchange(&g_travel_lock, config.travel_lock ? 1 : 0);
    g_log = config.log;
    const char* reason = config.mode == SaveHandoff::off           ? "off"
                         : !config.link_v2                         ? "legacy_link"
                         : !config.saves_redirected                ? "saves"
                         : !config.net || !config.world_on         ? "world_off"
                         : config.role != 1 && config.role != 2    ? "role"
                                                                   : nullptr;
    io::Dirs dirs{};
    if (!reason && !io::MakeDirs(config.save_dir, config.directory, dirs)) reason = "path";
    if (reason) {
        LogDisabled(config.log, reason);
        partner_net::PublishHandoffState(static_cast<std::uint8_t>(hstate::Token::off), 0);
        // F9 Partner / Co-op > Shared save stays reachable for a guest whose
        // travel lock may still apply (world on): the "off" status line and the
        // Travel lock checkbox, the lock's UI escape (P2-14, U4).
        hstate::View off{};
        off.available = config.role == 2 && config.travel_lock && config.world_on;
        off.role = static_cast<std::uint8_t>(g_role);
        off.token = static_cast<std::uint8_t>(hstate::Token::off);
        // Play-Coop relaunches the guest after an applied handoff with
        // save_handoff=off (one handoff per session, P2-17): applied.json still
        // gives toast G10 and the applied token in that session.
        core::Applied applied{};
        char start_utc[core::kUtcCapacity]{};
        if (config.mode == SaveHandoff::off && config.role == 2 && io::MakeDirs(config.save_dir, config.directory, dirs) &&
            ReadApplied(dirs, ProcessStart(start_utc), applied)) {
            char hex[wire::kShaHexChars + 1]{};
            wire::FormatShaHex(applied.set, hex);
            char text[64]{};
            if (SUCCEEDED(StringCchPrintfA(text, ARRAYSIZE(text), "save_handoff=applied_seen; set=%.8s", hex))) Write(text, Now());
            coop_hud::PostSaveHandoff(static_cast<std::uint8_t>(hstate::Toast::g10_applied), 0, 0);
            off.available = true;
            off.token = static_cast<std::uint8_t>(hstate::Token::applied);
            partner_net::PublishHandoffState(off.token, 0);
        }
        hstate::Publish(off);
        return false;
    }
    g_dirs = dirs;
    g_set_logged[0] = 0;
    g_policy = static_cast<wire::Mode>(static_cast<std::uint8_t>(config.mode));
    InterlockedExchange(&g_mode, static_cast<LONG>(g_policy));
    core::Config cc{};
    cc.role = config.role == 2 ? wire::Role::guest : wire::Role::host;
    cc.mode = g_policy;
    cc.rejoin = config.role == 2 && config.steam_wait;
    cc.pid = GetCurrentProcessId();
    const std::uint64_t process_start = ProcessStart(cc.process_start_utc);
    cc.utc_now = &UtcNow;
    cc.random_u32 = &RandomU32;
    g_core.Reset(cc);
    const std::uint64_t now = Now();
    // Leftovers of an earlier process (known names only), the stored
    // provenance and, for the guest, the tooling's applied.json.
    const unsigned removed = io::CleanLeftovers(g_dirs, cc.pid, &RequestPid);
    if (removed) {
        char text[64]{};
        StringCchPrintfA(text, ARRAYSIZE(text), "save_handoff=cleanup; removed=%u", removed);
        Write(text, now);
    }
    std::size_t size = 0;
    wchar_t path[io::kPathChars]{};
    if (io::HandoffFile(g_dirs, L"provenance.json", path, io::kPathChars)) {
        if (io::ReadText(path, g_text, sizeof(g_text), size)) g_core.LoadProvenance(g_text, size);
        else if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) g_core.LoadProvenance("", 0);  // unreadable: rewritten
    }
    if (config.role == 2 && ReadApplied(g_dirs, process_start, g_applied))
        g_applied_pending = true;  // toast G10 from the first hook tick (the HUD runs by then)
    char line[160]{};
    if (g_log && SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "save_handoff=configured; role=%s; mode=%s; channel=4; dir=ok; t_ms=%llu\r\n",
                                           config.role == 2 ? "guest" : "host", wire::ModeName(g_policy),
                                           static_cast<unsigned long long>(now))))
        g_log(line);
    InterlockedExchange(&g_configured, 1);
    // The F9 Shared save section exists from here; the core's own publish
    // actions replace this view.
    PublishView(g_core.view());
    Drain(now);
    return true;
}

bool Configured() noexcept { return g_configured != 0; }

const partner_net::HandoffHooks* Hooks() noexcept { return &kHooks; }

const char* HandleLua(const char* source, std::size_t size) noexcept {
    static constexpr char yes[] = "return true";
    static constexpr char no[] = "return false";
    std::uint8_t op = 0;
    std::uint32_t a = 0, b = 0, c = 0, d = 0, e = 0;
    if (!world::ParseSave(source, size, op, a, b, c, d, e)) return nullptr;
    if (!g_configured || g_shutdown) return no;
    const std::uint64_t now = Now();
    // At most 4 receipts per second; a DECLINE (the player's veto) is never dropped.
    if (!g_rate_window_ms || now - g_rate_window_ms >= 1000) {
        g_rate_window_ms = now;
        g_rate_count = 0;
    }
    if (op != 3) {
        if (g_rate_count >= 4) return no;
        ++g_rate_count;
    }
    if (op == 1) {
        const std::uint32_t flags = (b ? 1u : 0u) | ((c & 3u) << 1) | (d ? 8u : 0u) | (e ? 16u : 0u);
        InterlockedIncrement(&g_report_seq);  // odd: writing
        InterlockedExchange(&g_report_mission, static_cast<LONG>(a));
        InterlockedExchange(&g_report_flags, static_cast<LONG>(flags));
        InterlockedExchange64(&g_report_at, static_cast<LONG64>(now));
        InterlockedIncrement(&g_report_seq);  // even: stable
        InterlockedIncrement(&g_report_count);
        return yes;
    }
    bool queued = false;
    AcquireSRWLockExclusive(&g_mail_lock);
    if (g_mail_count < kMailSlots) {
        g_mail[(g_mail_head + g_mail_count) % kMailSlots] = Mail{op, a};
        ++g_mail_count;
        queued = true;
    }
    ReleaseSRWLockExclusive(&g_mail_lock);
    return queued ? yes : no;
}

void NoteMission(std::uint32_t mission, bool terminating) noexcept {
    InterlockedExchange(&g_mission, static_cast<LONG>(mission));
    InterlockedExchange(&g_mission_terminating, terminating ? 1 : 0);
    InterlockedIncrement(&g_mission_seq);
}

std::uint8_t LuaCode(std::uint64_t) noexcept {
    if (!g_configured) return 0;
    const auto code = static_cast<std::uint32_t>(g_lua_code);
    return code <= static_cast<std::uint8_t>(hstate::LuaCode::failed) ? static_cast<std::uint8_t>(code) : 0;
}

std::uint8_t LockAllowed() noexcept {
    return g_role == 2 && g_travel_lock && !hstate::TravelUnlocked() ? 1 : 0;
}

bool PeerOnHandedSave() noexcept { return g_configured && g_role == 1 && g_handed_off != 0; }

bool GuestHandoffActive() noexcept {
    const auto mode = static_cast<wire::Mode>(static_cast<std::uint8_t>(g_mode));
    return g_configured && g_role == 2 && (mode == wire::Mode::scene || mode == wire::Mode::always) && g_host_state_seen;
}

void Shutdown() noexcept { InterlockedExchange(&g_shutdown, 1); }

void SetExitForTest(io::EnumWindowsFn enumerate, io::PostCloseFn post, std::uint32_t slow_ms) noexcept {
    g_exit_enum = enumerate ? enumerate : &io::EnumTopLevelWindows;
    g_exit_post = post ? post : &io::PostWindowClose;
    g_exit_slow_ms = slow_ms ? static_cast<DWORD>(slow_ms) : static_cast<DWORD>(core::kExitSlowMs);
}

void ExecuteForTest(const core::Action& action) noexcept {
    bool chunk_refused = false;
    Execute(action, Now(), chunk_refused);
    Drain(Now());
}

void ResetForTest() noexcept {
    InterlockedExchange(&g_exit_armed, 0);
    if (g_exit_timer) {
        DeleteTimerQueueTimer(nullptr, g_exit_timer, nullptr);
        g_exit_timer = nullptr;
    }
    SetExitForTest(nullptr, nullptr, 0);
    InterlockedExchange(&g_configured, 0);
    InterlockedExchange(&g_role, 0);
    InterlockedExchange(&g_travel_lock, 0);
    InterlockedExchange(&g_mode, 0);
    InterlockedExchange(&g_lua_code, 0);
    InterlockedExchange(&g_host_state_seen, 0);
    InterlockedExchange(&g_handed_off, 0);
    InterlockedExchange(&g_shutdown, 0);
    InterlockedExchange(&g_mission, 0);
    InterlockedExchange(&g_mission_terminating, 0);
    AcquireSRWLockExclusive(&g_mail_lock);
    g_mail_head = g_mail_count = 0;
    ReleaseSRWLockExclusive(&g_mail_lock);
    g_rate_window_ms = 0;
    g_rate_count = 0;
    g_log = nullptr;
    g_policy = wire::Mode::off;
    g_dirs = io::Dirs{};
    ReleaseSnapshot();
    g_hash = io::HashJob{};
    g_cache = io::HashCache{};
    g_set_logged[0] = 0;
    g_build_known = g_build_ok = false;
    g_have_report = false;
    g_shutdown_done = false;
    g_applied_pending = false;
    g_report_seen = g_report_count;
    g_mission_seen = g_mission_seq;
}

}  // namespace mgscoop::save_handoff
