// Project-authored developer CLI for the runtime archive builder. Not shipped
// to players (the launcher exe calls BuildRuntimeDat directly).
//
//   ddt_datbuild build <gameDir> <out.dat>          build and verify the runtime .dat
//   ddt_datbuild verify <file.dat>                  check size and SHA-256
//   ddt_datbuild list <archive.dat>                 list QAR entries (hash, offset, sizes, MD5)
//   ddt_datbuild check <archive.dat>                read every entry and check its MD5
//   ddt_datbuild extract <archive.dat> <hash> <out> write one entry's plain bytes
#include <chrono>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

#include "launcher/file_io.hpp"
#include "launcher/hash.hpp"
#include "launcher/qar.hpp"
#include "launcher/runtime_dat.hpp"

namespace {

int Usage() {
    std::fprintf(stderr,
                 "usage:\n"
                 "  ddt_datbuild build <gameDir> <out.dat>\n"
                 "  ddt_datbuild verify <file.dat>\n"
                 "  ddt_datbuild list <archive.dat>\n"
                 "  ddt_datbuild check <archive.dat>\n"
                 "  ddt_datbuild extract <archive.dat> <hash-hex> <out-file>\n");
    return 2;
}

bool ParseHash(const std::wstring& text, std::uint64_t& hash) {
    std::wstring digits = text;
    if (digits.size() > 2 && digits[0] == L'0' && (digits[1] == L'x' || digits[1] == L'X')) digits = digits.substr(2);
    if (digits.empty() || digits.size() > 16) return false;
    hash = 0;
    for (const wchar_t c : digits) {
        int v = -1;
        if (c >= L'0' && c <= L'9') v = c - L'0';
        if (c >= L'a' && c <= L'f') v = c - L'a' + 10;
        if (c >= L'A' && c <= L'F') v = c - L'A' + 10;
        if (v < 0) return false;
        hash = hash << 4 | static_cast<std::uint64_t>(v);
    }
    return true;
}

int Build(const std::wstring& game, const std::wstring& out) {
    const auto start = std::chrono::steady_clock::now();
    std::string error;
    int last_percent = -1;
    const bool ok = ddt::BuildRuntimeDat(game, out, [&](float fraction, const std::string& text) {
        const int percent = static_cast<int>(fraction * 100.0f + 0.5f);
        if (percent != last_percent) std::printf("[%3d%%] %s\n", percent, text.c_str());
        last_percent = percent;
    }, error);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    if (!ok) {
        std::fprintf(stderr, "build failed after %lld ms: %s\n", static_cast<long long>(ms), error.c_str());
        return 1;
    }
    std::printf("built %llu bytes, sha256 %s, in %lld ms\n", static_cast<unsigned long long>(ddt::kRuntimeDatSize),
                ddt::kRuntimeDatSha256, static_cast<long long>(ms));
    return 0;
}

int Verify(const std::wstring& path) {
    std::string error;
    if (!ddt::VerifyRuntimeDat(path, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("ok: %llu bytes, sha256 %s\n", static_cast<unsigned long long>(ddt::kRuntimeDatSize), ddt::kRuntimeDatSha256);
    return 0;
}

int List(const std::wstring& path) {
    ddt::QarReader reader;
    std::string error;
    if (!reader.OpenFile(path, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("version %u flags 0x%x entries %zu\n", reader.version(), reader.flags(), reader.entry_count());
    for (std::size_t i = 0; i < reader.entry_count(); ++i) {
        ddt::QarEntryInfo info;
        if (!reader.EntryAt(i, info, error)) {
            std::fprintf(stderr, "entry %zu: %s\n", i, error.c_str());
            return 1;
        }
        std::printf("%zu 0x%016llx offset %llu stored %u plain %u md5 %s\n", i, static_cast<unsigned long long>(info.hash),
                    static_cast<unsigned long long>(info.offset), info.stored_size, info.plain_size,
                    ddt::ToHex(info.md5).c_str());
    }
    return 0;
}

// Reads every entry (unscramble, inflate, MD5). Entries with the second
// encryption layer are counted separately, since the reader refuses them.
int Check(const std::wstring& path) {
    ddt::QarReader reader;
    std::string error;
    if (!reader.OpenFile(path, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    const auto start = std::chrono::steady_clock::now();
    std::size_t ok = 0, compressed = 0, layered = 0, failed = 0;
    unsigned long long bytes = 0;
    std::vector<std::uint8_t> plain;
    for (std::size_t i = 0; i < reader.entry_count(); ++i) {
        ddt::QarEntryInfo info;
        if (!reader.EntryAt(i, info, error) || !reader.Read(info, plain, 1u << 30, error)) {
            if (error.find("second encryption layer") != std::string::npos) {
                ++layered;
                continue;
            }
            ++failed;
            std::fprintf(stderr, "entry %zu (0x%016llx): %s\n", i, static_cast<unsigned long long>(info.hash), error.c_str());
            continue;
        }
        ++ok;
        if (info.stored_size != info.plain_size) ++compressed;
        bytes += plain.size();
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    std::printf("%zu entries: %zu ok (%zu inflated, %llu plain bytes), %zu second-layer (not supported), %zu failed, %lld ms\n",
                reader.entry_count(), ok, compressed, bytes, layered, failed, static_cast<long long>(ms));
    return failed ? 1 : 0;
}

int Extract(const std::wstring& path, const std::wstring& hash_text, const std::wstring& out) {
    std::uint64_t hash = 0;
    if (!ParseHash(hash_text, hash)) return Usage();
    ddt::QarReader reader;
    std::string error;
    if (!reader.OpenFile(path, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    ddt::QarEntryInfo info;
    const auto found = reader.Find(hash, info, error);
    if (found != ddt::QarReader::Lookup::found) {
        std::fprintf(stderr, "%s\n", found == ddt::QarReader::Lookup::not_found ? "entry not found" : error.c_str());
        return 1;
    }
    std::vector<std::uint8_t> plain;
    if (!reader.Read(info, plain, 512u << 20, error) || !ddt::WriteWholeFile(out, plain.data(), plain.size(), error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    ddt::Sha256Digest digest{};
    ddt::Sha256(plain.data(), plain.size(), digest);
    std::printf("wrote %zu bytes, sha256 %s\n", plain.size(), ddt::ToHex(digest).c_str());
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return Usage();
    const std::wstring command = argv[1];
    if (command == L"build" && argc == 4) return Build(argv[2], argv[3]);
    if (command == L"verify" && argc == 3) return Verify(argv[2]);
    if (command == L"list" && argc == 3) return List(argv[2]);
    if (command == L"check" && argc == 3) return Check(argv[2]);
    if (command == L"extract" && argc == 5) return Extract(argv[2], argv[3], argv[4]);
    return Usage();
}
