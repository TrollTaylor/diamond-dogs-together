// Exercises the Quiet/Ocelot reset bypass gate on plain data buffers: byte
// contents, hash gate, ownership, page protection restoration, last-error
// preservation and refusal reasons. Optionally repeats the production path on
// an in-memory copy of the real body when the fingerprinted executable is
// installed (read-only file access). Nothing is executed from the buffers and
// no MGSV process, Lua state or Steam account is involved.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "mgscoop/player_type_patch.hpp"
#include "player_type_contract.hpp"

namespace {
unsigned checks = 0;
std::string logged;
void Check(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void Log(const char* line) noexcept {
    try { logged += line; } catch (...) {}
}
bool Logged(const char* text) { return logged.find(text) != std::string::npos; }

void Sha256(const unsigned char* bytes, std::size_t size, unsigned char (&out)[32]) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    if (ok) ok = BCryptHashData(hash, const_cast<PUCHAR>(bytes), static_cast<ULONG>(size), 0) >= 0;
    if (ok) ok = BCryptFinishHash(hash, out, sizeof(out), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    Check(ok, "test SHA-256");
}

DWORD Protection(const void* address) {
    MEMORY_BASIC_INFORMATION memory{};
    return VirtualQuery(address, &memory, sizeof(memory)) ? memory.Protect : 0;
}

// Writes into a read-only fixture page and restores its protection.
void Poke(unsigned char* address, const void* bytes, std::size_t size) {
    DWORD old = 0;
    Check(VirtualProtect(address, size, PAGE_READWRITE, &old) != 0, "fixture unprotect");
    std::memcpy(address, bytes, size);
    Check(VirtualProtect(address, size, old, &old) != 0, "fixture reprotect");
}

using mgscoop::player_type::detail::Contract;
using mgscoop::player_type::detail::State;
using mgscoop::player_type::detail::SetActiveWith;

constexpr unsigned char kOriginal[6] = {0x0f, 0x85, 0x9c, 0x07, 0x00, 0x00};
constexpr unsigned char kReplacement[6] = {0x48, 0xe9, 0x9c, 0x07, 0x00, 0x00};

void SyntheticGate() {
    constexpr std::size_t span = 0x3000, body_rva = 0x1000, body_size = 96, site = 0x20;
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, span, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    auto* other = static_cast<unsigned char*>(VirtualAlloc(nullptr, span, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Check(image && other, "fixture allocation");
    unsigned char body[body_size]{};
    unsigned seed = 0x2545f491u;
    for (auto& b : body) { seed = seed * 1103515245u + 12345u; b = static_cast<unsigned char>(seed >> 16); }
    std::memcpy(body + site, kOriginal, sizeof(kOriginal));
    std::memcpy(image + body_rva, body, body_size);
    std::memcpy(other + body_rva, body, body_size);
    DWORD old = 0;
    Check(VirtualProtect(image, span, PAGE_READONLY, &old) != 0, "fixture read-only");
    Check(VirtualProtect(other, span, PAGE_READONLY, &old) != 0, "second fixture read-only");

    Contract contract{body_rva, body_size, site, {}, {}, {}};
    std::memcpy(contract.original, kOriginal, 6);
    std::memcpy(contract.replacement, kReplacement, 6);
    Sha256(body, body_size, contract.sha256);
    State state{nullptr, false};
    auto* at = image + body_rva;
    auto unchanged_except_site = [&](const unsigned char* expected_site) {
        for (std::size_t i = 0; i < body_size; ++i) {
            const unsigned char want = i >= site && i < site + 6 ? expected_site[i - site] : body[i];
            if (at[i] != want) return false;
        }
        return true;
    };

    // Argument and contract validation.
    Check(!SetActiveWith(state, contract, nullptr, span, true, Log), "null base rejected");
    Check(!SetActiveWith(state, contract, image, span, true, nullptr), "null logger rejected");
    Contract bad = contract; bad.replacement[3] ^= 1;
    Check(!SetActiveWith(state, bad, image, span, true, Log) && Logged("reason=invalid_contract"), "rel32 change rejected");
    bad = contract; std::memcpy(bad.replacement, kOriginal, 2);
    Check(!SetActiveWith(state, bad, image, span, true, Log), "no-op replacement rejected");
    bad = contract; bad.site_offset = body_size - 5;
    Check(!SetActiveWith(state, bad, image, span, true, Log), "site outside body rejected");
    bad = contract; bad.body_size = mgscoop::player_type::detail::max_body_size + 1;
    Check(!SetActiveWith(state, bad, image, span, true, Log), "oversized body rejected");
    bad = contract; bad.body_size = 4; bad.site_offset = 0;
    Check(!SetActiveWith(state, bad, image, span, true, Log), "body shorter than the branch rejected");
    Check(!SetActiveWith(state, contract, image, body_rva + body_size - 1, true, Log) && Logged("reason=span"),
          "truncated span rejected");
    bad = contract; bad.body_rva = body_rva + 1; bad.site_offset = site;  // odd site address
    Check(!SetActiveWith(state, bad, image, span, true, Log) && Logged("reason=branch_alignment"), "unaligned branch rejected");
    Check(unchanged_except_site(kOriginal) && !state.active && !state.owner, "refusals leave bytes and state untouched");

    // Disabling with nothing owned is a no-op.
    Check(SetActiveWith(state, contract, image, span, false, Log), "disable without ownership succeeds");
    Check(unchanged_except_site(kOriginal), "no-op disable leaves bytes");

    // Hash gate.
    unsigned char flipped = static_cast<unsigned char>(body[5] ^ 0x40);
    Poke(at + 5, &flipped, 1);
    Check(!SetActiveWith(state, contract, image, span, true, Log) && Logged("reason=body_hash"), "changed body rejected");
    Poke(at + 5, body + 5, 1);

    // Foreign replacement already present: never adopted.
    Poke(at + site, kReplacement, 6);
    Check(!SetActiveWith(state, contract, image, span, true, Log) && Logged("reason=branch_bytes_changed_or_unowned"),
          "unowned replacement rejected");
    Check(SetActiveWith(state, contract, image, span, false, Log) && unchanged_except_site(kReplacement),
          "disable never restores foreign bytes");
    Poke(at + site, kOriginal, 6);

    // Enable: exactly two bytes change, protection and last error preserved.
    logged.clear();
    SetLastError(0x1234);
    Check(SetActiveWith(state, contract, image, span, true, Log), "enable");
    Check(GetLastError() == 0x1234, "last error preserved on enable");
    Check(unchanged_except_site(kReplacement) && state.active && state.owner == image, "only the branch opcode changed");
    Check(Protection(at + site) == PAGE_READONLY, "page protection restored after enable");
    Check(Logged("player_type_patch=active; branch_rva=0x1020"), "activation logged with branch address");
    Check(SetActiveWith(state, contract, image, span, true, Log) && unchanged_except_site(kReplacement), "enable idempotent");

    // Ownership is per image base.
    Check(!SetActiveWith(state, contract, other, span, true, Log) && Logged("reason=different_image_base"),
          "second image rejected while owned");

    // Owned bytes changed by someone else: refuse both directions.
    const unsigned char foreign[6] = {0x0f, 0x84, 0x9c, 0x07, 0x00, 0x00};
    Poke(at + site, foreign, 6);
    Check(!SetActiveWith(state, contract, image, span, false, Log), "tampered owned branch not restored blindly");
    Check(!SetActiveWith(state, contract, image, span, true, Log), "tampered owned branch not re-adopted");
    Check(unchanged_except_site(foreign) && state.active, "refusal keeps state");
    Poke(at + site, kReplacement, 6);

    // Disable restores the original bytes; repeated disable is a no-op.
    logged.clear();
    SetLastError(0x4321);
    Check(SetActiveWith(state, contract, image, span, false, Log), "disable");
    Check(GetLastError() == 0x4321, "last error preserved on disable");
    Check(unchanged_except_site(kOriginal) && !state.active, "original bytes restored");
    Check(Protection(at + site) == PAGE_READONLY, "page protection restored after disable");
    Check(Logged("player_type_patch=inactive; branch_rva=0x1020 original_bytes_restored=1"), "restoration logged");
    Check(SetActiveWith(state, contract, image, span, false, Log), "disable idempotent");
    // Re-enable/disable cycles stay exact.
    for (int i = 0; i < 3; ++i) {
        Check(SetActiveWith(state, contract, image, span, true, Log) && unchanged_except_site(kReplacement), "cycle enable");
        Check(SetActiveWith(state, contract, image, span, false, Log) && unchanged_except_site(kOriginal), "cycle disable");
    }

    // Unreadable body (reserved, uncommitted page) is refused, not faulted.
    auto* reserved = static_cast<unsigned char*>(VirtualAlloc(nullptr, span, MEM_RESERVE, PAGE_NOACCESS));
    Check(reserved && VirtualAlloc(reserved, 0x1000, MEM_COMMIT, PAGE_READONLY) == reserved, "partial commit");
    State fresh{nullptr, false};
    Check(!SetActiveWith(fresh, contract, reserved, span, true, Log) && Logged("reason=body_unreadable"),
          "uncommitted body rejected");
    VirtualFree(reserved, 0, MEM_RELEASE);
    VirtualFree(other, 0, MEM_RELEASE);
    VirtualFree(image, 0, MEM_RELEASE);
}

void ProductionContractShape() {
    namespace c = mgscoop::player_type_contract;
    Check(c::body_rva == 0x9cd030 && c::body_size == 6388 && c::site_offset == 0x340, "pinned extent and site");
    Check(std::memcmp(c::original_bytes, kOriginal, 6) == 0, "original bytes are the Dynamite-mapped jne");
    Check(std::memcmp(c::replacement_bytes, kReplacement, 6) == 0, "replacement bytes are the Dynamite jmp");
    std::int32_t rel = 0;
    std::memcpy(&rel, c::original_bytes + 2, 4);
    Check(c::body_rva + c::site_offset + 6 + static_cast<std::uintptr_t>(rel) == c::keep_path_rva, "rel32 reaches keep path");
    Check((c::body_rva + c::site_offset) % 2 == 0, "site word aligned");
    Check((c::body_rva + c::site_offset) % 64 + 6 <= 64, "branch within one cache line");
    // Production entry on a zero image: disable is a no-op, enable refuses.
    constexpr std::size_t span = c::body_rva + c::body_size;
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, span, MEM_RESERVE | MEM_COMMIT, PAGE_READONLY));
    Check(image != nullptr, "zero image");
    Check(mgscoop::player_type::SetActive(image, span, false, Log), "production disable no-op");
    Check(!mgscoop::player_type::SetActive(image, span, true, Log) && !mgscoop::player_type::IsActive(),
          "production refuses unrelated bytes");
    VirtualFree(image, 0, MEM_RELEASE);
}

// Optional: verify the generated contract against the installed executable.
// Opens the file read-only, maps the body RVA through the section table and
// runs the production gate on an in-memory copy.
bool InstalledExecutableGate() {
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"MGS_TPP_EXE", path, MAX_PATH);
    if (!length || length >= MAX_PATH) lstrcpyW(path, L"D:\\Games\\steamapps\\common\\MGS_TPP\\mgsvtpp.exe");
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    HANDLE mapping = GetFileSizeEx(file, &size) && size.QuadPart > 0x1000
        ? CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr) : nullptr;
    const auto* view = mapping ? static_cast<const unsigned char*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0)) : nullptr;
    bool ran = false;
    if (view) {
        static constexpr unsigned char fingerprint[32] = {
            0x08,0x5c,0x2f,0x82,0xd1,0xc9,0x63,0xc4,0x0b,0x3d,0x2d,0x55,0x78,0x66,0x61,0xdf,
            0xee,0x2b,0x18,0xcb,0xbf,0x38,0x8a,0x71,0x0c,0x00,0xfa,0x76,0xc5,0xe9,0xbb,0x45};
        unsigned char digest[32]{};
        Sha256(view, static_cast<std::size_t>(size.QuadPart), digest);
        if (std::memcmp(digest, fingerprint, 32) == 0) {
            namespace c = mgscoop::player_type_contract;
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(view);
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(view + dos->e_lfanew);
            const auto* section = IMAGE_FIRST_SECTION(nt);
            std::size_t offset = 0;
            for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
                if (c::body_rva >= section->VirtualAddress &&
                    c::body_rva + c::body_size <= section->VirtualAddress + section->SizeOfRawData)
                    offset = section->PointerToRawData + (c::body_rva - section->VirtualAddress);
            Check(offset != 0 && offset + c::body_size <= static_cast<std::size_t>(size.QuadPart), "body mapped from file");
            constexpr std::size_t span = c::body_rva + c::body_size;
            auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, span, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
            Check(image != nullptr, "image copy allocation");
            std::memcpy(image + c::body_rva, view + offset, c::body_size);
            DWORD old = 0;
            Check(VirtualProtect(image, span, PAGE_READONLY, &old) != 0, "image copy read-only");
            auto* site = image + c::body_rva + c::site_offset;
            Check(std::memcmp(site, kOriginal, 6) == 0, "installed branch bytes match contract");
            Check(mgscoop::player_type::SetActive(image, span, true, Log) && mgscoop::player_type::IsActive(),
                  "production gate accepts the installed body");
            Check(std::memcmp(site, kReplacement, 6) == 0, "installed copy patched");
            Check(std::memcmp(image + c::body_rva, view + offset, c::site_offset) == 0 &&
                  std::memcmp(site + 2, view + offset + c::site_offset + 2, c::body_size - c::site_offset - 2) == 0,
                  "installed copy otherwise unchanged");
            Check(mgscoop::player_type::SetActive(image, span, true, Log), "installed copy idempotent");
            Check(mgscoop::player_type::SetActive(image, span, false, Log) && !mgscoop::player_type::IsActive(),
                  "installed copy restored");
            Check(std::memcmp(image + c::body_rva, view + offset, c::body_size) == 0, "installed copy byte-identical again");
            Check(Protection(site) == PAGE_READONLY, "installed copy protection restored");
            VirtualFree(image, 0, MEM_RELEASE);
            ran = true;
        }
        UnmapViewOfFile(view);
    }
    if (mapping) CloseHandle(mapping);
    CloseHandle(file);
    return ran;
}
} // namespace

int main() {
    SyntheticGate();
    ProductionContractShape();
    const bool installed = InstalledExecutableGate();
    std::printf("%u checks passed; synthetic gate, production contract shape; installed executable body %s\n",
                checks, installed ? "verified in memory copy" : "skipped (not present or different fingerprint)");
    return 0;
}
