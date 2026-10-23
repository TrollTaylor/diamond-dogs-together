// Unit tests for the launcher archive utilities (native/launcher): CRC-32,
// Adler-32, MD5/SHA-256, inflate (stored, fixed and dynamic blocks, zlib
// wrapper, error cases), zip read/write, QAR write->read, FPK round trip,
// the FOX2 string-table rewrite, the embedded runtime assets, and the
// runtime .dat builder's refusal of mismatching sources. Synthetic data only;
// the compressed vectors were produced once with Python's zlib.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "launcher/crc32.hpp"
#include "launcher/file_io.hpp"
#include "launcher/fox2.hpp"
#include "launcher/fpk.hpp"
#include "launcher/hash.hpp"
#include "launcher/inflate.hpp"
#include "launcher/qar.hpp"
#include "launcher/runtime_dat.hpp"
#include "launcher/zip.hpp"

namespace {

using Bytes = std::vector<std::uint8_t>;
unsigned checks = 0, failures = 0;

void Check(bool value, const char* what, int line) {
    ++checks;
    if (!value) {
        ++failures;
        std::fprintf(stderr, "FAILED (line %d): %s\n", line, what);
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

bool Contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

Bytes FromHexString(const char* hex) {
    const std::string text(hex);
    Bytes out(text.size() / 2);
    if (!ddt::FromHex(text, out.data(), out.size())) {
        std::fprintf(stderr, "bad hex vector\n");
        std::exit(2);
    }
    return out;
}
Bytes FromText(const std::string& text) { return Bytes(text.begin(), text.end()); }
std::string Sha256Hex(const Bytes& b) {
    ddt::Sha256Digest d{};
    ddt::Sha256(b.data(), b.size(), d);
    return ddt::ToHex(d);
}

// Same generator as the Python script that produced the vectors.
std::string LcgText(std::size_t n, std::uint64_t seed) {
    static const char* const words[16] = {"alpha", "bravo", "charlie", "delta", "echo", "foxtrot", "golf", "hotel",
                                          "india", "juliet", "kilo", "lima", "mike", "november", "oscar", "papa"};
    std::string out;
    std::uint64_t s = seed;
    while (out.size() < n) {
        s = (s * 1103515245u + 12345u) & 0x7fffffffu;
        out += words[(s >> 16) % 16];
        s = (s * 1103515245u + 12345u) & 0x7fffffffu;
        out += ((s >> 16) % 7) ? " " : ".\n";
    }
    out.resize(n);
    return out;
}

Bytes Pseudorandom(std::size_t n, std::uint32_t seed) {
    Bytes out(n);
    std::uint32_t s = seed;
    for (auto& b : out) {
        s = s * 1664525u + 1013904223u;
        b = static_cast<std::uint8_t>(s >> 24);
    }
    return out;
}

// zlib -9 of LcgText(3000, 12345): a single dynamic-Huffman block.
constexpr char kDynZlib[] =
    "78da7d565b92db300cfbf7297402df499bf536eedaeb8c9beef4f8ad013e4065a63f4e2413244482b4f6f573691fc79f"
    "e7793cdba33f7afb3abe97fd6d39dbe7ba1d6dbfde1fbf6efd6cefcbf6ecf3f4f3f7b62ecf309ba70bc56762018bd5b6"
    "eebdad5fef6ba753d8d22977dfcefe7d846b983b2778b217c0fd38b60f03ef4a5e82d07c9e96dbfd68b77b3fff2d89c3"
    "1992d865304f88ee31faf6b8f7763f9ecb46fffc0bb472816fec02314f744d8f1e3302f1a55345124895070967ee864f"
    "9ad4342207e560b4c24b722d6fb15feb42402d58c98155bad401443d5738b7e4089606a60538e071194469b143a013f4"
    "df527064085e2d2e39b338a2c9cbc4888a48dda76b3396883c4f45326443cfa2513315a1813b2c7cfb7f722f45c0026e"
    "a90f8d06ac74843e696702bbf09148be298242109edf8cf468cc5429b3fd1492be80475f2819fa045fa9835b9a4be8a3"
    "c412dbd27d592aace017b1b59c7c22ad24015b5a780a1092197532242084f170006d01c37e10a96931d1048c2bb39146"
    "cebf55ac08c0e4eb144b95bb63b2d5013676a7b565052015d217f049a908aa0d8dc05784e5e0225877795aad7f76265d"
    "726ad122ab27c336d7a663e7af11804ca172ad494ed73978856dcd947d8d6099036b507f14a67ef16ada58b814cf30d0"
    "63c9f313e9e7cb1128a3dedc53ee3eedc741a0c28dd3cf53187016903c39eb00d60f4bca3bd3f6dae7710c1d18e5733f"
    "4f61537b4cfa5af45e251ad051d65a1583d829719efce632595ab8a11025aa08da69bee439f07586e71cb61c931ce8c8"
    "fc29422ab72706121437ca4d258e9a3da7fcca8429e30d5ec8e0e586518a9697b354b9cabad6c757d69fe5b6a764581b"
    "37a0f00ca3d213adc449d947927f65337ccf359172f9e25f29373b818eca258f5b722d158783d4292ca06464655d80a4"
    "e9cbfdb2d4daae0b32934b19553675900c0504746c18e2b2faf3d4ff02869d3c70";
constexpr char kDynPlainSha256[] = "eb97c0297f33fda3b97c5b25aaff889efc24aa951fd58e452a03dff250b32bca";
// Raw deflate, Z_FIXED, of "the quick brown fox jumps over the lazy dog. " x 20: one final fixed block.
constexpr char kFixedRaw[] =
    "2bc94855282ccd4cce56482aca2fcf5348cbaf50c82acd2d2856c82f4b2d5228014ae72456552aa4e4a7eb8179a38a47"
    "158f2aa6aa6200";
// zlib level 0 of "Hello, stored block!\n" x 3: one stored block.
constexpr char kStoredZlib[] =
    "7801013f00c0ff48656c6c6f2c2073746f72656420626c6f636b210a48656c6c6f2c2073746f72656420626c6f636b21"
    "0a48656c6c6f2c2073746f72656420626c6f636b210abad31576";
constexpr char kEmptyZlib[] = "78da030000000001";
// Raw deflate of LcgText(600, 777) ended with a sync flush: a non-final dynamic
// block, then an empty non-final stored block (byte aligned).
constexpr char kDynSyncRaw[] =
    "6c514112832010bbf30a5ec09f568b850aae43a9d3e7b764178b33bda0926c36897e0e6c177ed7c2d5f273a662ef9c16"
    "3b072a297a1bb75b243d373e7c9e7c712670f5c9fa36db2fed4e3bc90181a9d0c1ce9c70df011093227212d698d852da"
    "830a0cdfe20b8a6a050c0153cc24afcef425222d038fd73747853367725cbdcadd7caad4a328491f8da588338804b20e"
    "0e82600ebdc846553e933587aad382ff29e5c47445bf4736dc8321f2c87ead4d4a913ed45e97007e092959a48ffe9b87"
    "4a8638d8344687959f1f680bae65f5adf9030000ffff";
// Python zipfile: "hello.txt" deflated ("Hello from a deflated zip entry. " x 8),
// "data/blob.bin" stored (bytes 0..39).
constexpr char kZip[] =
    "504b03041400000008000060575dc0f02a7d26000000080100000900000068656c6c6f2e747874f348cdc9c957482bca"
    "cf55485448494dcb492c494d51a8ca2c5048cd2b29aad453f018190a00504b03041400000000000060575d3c2ea60d28"
    "000000280000000d000000646174612f626c6f622e62696e000102030405060708090a0b0c0d0e0f1011121314151617"
    "18191a1b1c1d1e1f2021222324252627504b010214001400000008000060575dc0f02a7d260000000801000009000000"
    "000000000000000080010000000068656c6c6f2e747874504b010214001400000000000060575d3c2ea60d2800000028"
    "0000000d000000000000000000000080014d000000646174612f626c6f622e62696e504b050600000000020002007200"
    "0000a00000000000";

std::string FixedPlain() {
    std::string s;
    for (int i = 0; i < 20; ++i) s += "the quick brown fox jumps over the lazy dog. ";
    return s;
}

// Hand-written fixed-Huffman streams.
class BitWriter {
public:
    void Bits(std::uint32_t value, int count) {
        for (int i = 0; i < count; ++i) {
            acc_ |= ((value >> i) & 1u) << used_;
            if (++used_ == 8) Flush();
        }
    }
    void Code(std::uint32_t code, int length) {  // Huffman codes go most significant bit first
        for (int i = length - 1; i >= 0; --i) Bits((code >> i) & 1u, 1);
    }
    void Fixed(int symbol) {
        if (symbol < 144) Code(0x30u + static_cast<std::uint32_t>(symbol), 8);
        else if (symbol < 256) Code(0x190u + static_cast<std::uint32_t>(symbol - 144), 9);
        else if (symbol < 280) Code(static_cast<std::uint32_t>(symbol - 256), 7);
        else Code(0xC0u + static_cast<std::uint32_t>(symbol - 280), 8);
    }
    Bytes Finish() {
        if (used_) Flush();
        return out_;
    }

private:
    void Flush() {
        out_.push_back(static_cast<std::uint8_t>(acc_));
        acc_ = 0;
        used_ = 0;
    }
    Bytes out_;
    std::uint32_t acc_ = 0;
    int used_ = 0;
};

void StoredBlock(Bytes& out, const Bytes& data, std::size_t from, std::size_t length, bool final) {
    out.push_back(final ? 1 : 0);
    out.push_back(static_cast<std::uint8_t>(length));
    out.push_back(static_cast<std::uint8_t>(length >> 8));
    out.push_back(static_cast<std::uint8_t>(~length));
    out.push_back(static_cast<std::uint8_t>(~length >> 8));
    out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(from),
               data.begin() + static_cast<std::ptrdiff_t>(from + length));
}

bool Raw(const Bytes& in, Bytes& out, std::size_t max = 1u << 24) {
    std::string error;
    out.clear();
    return ddt::InflateRaw(in.data(), in.size(), out, max, error);
}
std::string RawError(const Bytes& in, std::size_t max = 1u << 24) {
    std::string error;
    Bytes out;
    return ddt::InflateRaw(in.data(), in.size(), out, max, error) ? std::string() : error;
}
bool Zlib(const Bytes& in, Bytes& out, std::size_t max = 1u << 24) {
    std::string error;
    out.clear();
    return ddt::ZlibDecompress(in.data(), in.size(), out, max, error);
}

void TestChecksums() {
    const std::string nine = "123456789";
    CHECK(ddt::Crc32(nine.data(), nine.size()) == 0xCBF43926u);
    CHECK(ddt::Crc32(nullptr, 0) == 0);
    CHECK(ddt::Crc32(nine.data() + 4, 5, ddt::Crc32(nine.data(), 4)) == 0xCBF43926u);
    const std::string wiki = "Wikipedia";
    CHECK(ddt::Adler32(wiki.data(), wiki.size()) == 0x11E60398u);
    const Bytes big(100000, 0xFF);  // long run: exercises the deferred modulo
    std::uint32_t a = 1, b = 0;
    for (const std::uint8_t v : big) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    CHECK(ddt::Adler32(big.data(), big.size()) == (b << 16 | a));

    ddt::Md5Digest md5{};
    CHECK(ddt::Md5("abc", 3, md5) && ddt::ToHex(md5) == "900150983cd24fb0d6963f7d28e17f72");
    ddt::Sha256Digest sha{};
    CHECK(ddt::Sha256("abc", 3, sha) &&
          ddt::ToHex(sha) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(ddt::Sha256("", 0, sha) &&
          ddt::ToHex(sha) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    ddt::Sha256Stream stream;
    ddt::Sha256Digest streamed{};
    CHECK(stream.ok() && stream.Update("a", 1) && stream.Update("bc", 2) && stream.Finish(streamed));
    CHECK(ddt::ToHex(streamed) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::uint8_t raw[2]{};
    CHECK(ddt::FromHex("aB0f", raw, 2) && raw[0] == 0xAB && raw[1] == 0x0F);
    CHECK(!ddt::FromHex("abc", raw, 2) && !ddt::FromHex("zz00", raw, 2));
}

void TestInflate() {
    Bytes out;
    // Dynamic block (zlib wrapper).
    CHECK(Zlib(FromHexString(kDynZlib), out));
    CHECK(out.size() == 3000 && Sha256Hex(out) == kDynPlainSha256);
    CHECK(out == FromText(LcgText(3000, 12345)));
    // Fixed block (raw).
    CHECK(Raw(FromHexString(kFixedRaw), out) && out == FromText(FixedPlain()));
    // Stored block (zlib wrapper) and the empty stream.
    std::string stored_plain;
    for (int i = 0; i < 3; ++i) stored_plain += "Hello, stored block!\n";
    CHECK(Zlib(FromHexString(kStoredZlib), out) && out == FromText(stored_plain));
    CHECK(Zlib(FromHexString(kEmptyZlib), out) && out.empty());

    // Stored + dynamic + empty stored + fixed blocks in one stream.
    Bytes multi;
    const Bytes head = FromText("STORED-PART|");
    StoredBlock(multi, head, 0, head.size(), false);
    const Bytes dyn = FromHexString(kDynSyncRaw), fixed = FromHexString(kFixedRaw);
    multi.insert(multi.end(), dyn.begin(), dyn.end());
    multi.insert(multi.end(), fixed.begin(), fixed.end());
    std::size_t consumed = 0;
    std::string error;
    out.clear();
    CHECK(ddt::InflateRaw(multi.data(), multi.size(), out, 1u << 20, error, &consumed));
    CHECK(out == FromText("STORED-PART|" + LcgText(600, 777) + FixedPlain()));
    CHECK(consumed == multi.size());
    // Trailing bytes after the final block are not consumed.
    Bytes padded = multi;
    padded.push_back(0xAA);
    padded.push_back(0xBB);
    out.clear();
    CHECK(ddt::InflateRaw(padded.data(), padded.size(), out, 1u << 20, error, &consumed) && consumed == multi.size());

    // Large multi-block stored stream (block size limit is 65535).
    const Bytes random = Pseudorandom(200000, 7);
    Bytes stored;
    for (std::size_t at = 0; at < random.size(); at += 65535) {
        const std::size_t n = std::min<std::size_t>(65535, random.size() - at);
        StoredBlock(stored, random, at, n, at + n == random.size());
    }
    CHECK(Raw(stored, out) && out == random);

    // Hand-written fixed blocks: literal + overlapping matches.
    {
        BitWriter w;
        w.Bits(1, 1);  // final
        w.Bits(1, 2);  // fixed Huffman
        w.Fixed('a');
        w.Fixed(257);   // length 3
        w.Code(0, 5);   // distance 1
        w.Fixed(256);
        CHECK(Raw(w.Finish(), out) && out == FromText("aaaa"));
    }
    {
        BitWriter w;
        w.Bits(1, 1);
        w.Bits(1, 2);
        for (char c : std::string("abcde")) w.Fixed(c);
        w.Fixed(265);  // length 11 + 1 extra bit
        w.Bits(1, 1);  // -> 12
        w.Code(4, 5);  // distance 5 + 1 extra bit
        w.Bits(0, 1);  // -> 5
        w.Fixed(256);
        CHECK(Raw(w.Finish(), out) && out == FromText("abcdeabcdeabcdeab"));
    }
    {
        BitWriter w;  // match before any output
        w.Bits(1, 1);
        w.Bits(1, 2);
        w.Fixed('a');
        w.Fixed(257);
        w.Code(1, 5);  // distance 2 with only 1 byte produced
        w.Fixed(256);
        CHECK(Contains(RawError(w.Finish()), "too far"));
    }
    {
        BitWriter w;  // literal/length symbol 286 is not valid
        w.Bits(1, 1);
        w.Bits(1, 2);
        w.Fixed(286);
        CHECK(!RawError(w.Finish()).empty());
    }
    {
        BitWriter w;  // distance symbol 30 is not valid
        w.Bits(1, 1);
        w.Bits(1, 2);
        w.Fixed('a');
        w.Fixed(257);
        w.Code(30, 5);
        CHECK(!RawError(w.Finish()).empty());
    }
    {
        BitWriter w;  // block type 3
        w.Bits(1, 1);
        w.Bits(3, 2);
        CHECK(Contains(RawError(w.Finish()), "block type"));
    }
    {
        BitWriter w;  // fixed block without end-of-block code
        w.Bits(1, 1);
        w.Bits(1, 2);
        w.Fixed('a');
        CHECK(Contains(RawError(w.Finish()), "truncated"));
    }
    CHECK(Contains(RawError(Bytes{0x01, 0x05, 0x00, 0x00, 0x00, 1, 2, 3, 4, 5}), "length check"));
    CHECK(Contains(RawError(Bytes{0x01, 0x05, 0x00, 0xFA, 0xFF, 1, 2}), "truncated"));
    CHECK(Contains(RawError(Bytes{}), "truncated"));

    // Truncation, output limit, zlib header/trailer checks.
    Bytes dyn_zlib = FromHexString(kDynZlib);
    Bytes half(dyn_zlib.begin(), dyn_zlib.begin() + static_cast<std::ptrdiff_t>(dyn_zlib.size() / 2));
    CHECK(!Zlib(half, out));
    CHECK(Contains(RawError(FromHexString(kFixedRaw), 100), "larger than allowed"));
    CHECK(Raw(FromHexString(kFixedRaw), out, FixedPlain().size()));  // exactly at the limit
    Bytes bad_adler = dyn_zlib;
    bad_adler.back() ^= 1;
    CHECK(!Zlib(bad_adler, out));
    Bytes no_trailer(dyn_zlib.begin(), dyn_zlib.end() - 4);
    CHECK(!Zlib(no_trailer, out));
    Bytes bad_header = dyn_zlib;
    bad_header[1] ^= 1;
    CHECK(!Zlib(bad_header, out));
    CHECK(!Zlib(Bytes{0x78, 0xBB, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00}, out));  // preset dictionary
    CHECK(!Zlib(Bytes{0x79, 0x9C, 0x03, 0x00}, out));                           // method 9
}

Bytes ReplaceLast(Bytes data, const std::string& from, const std::string& to) {
    for (std::size_t i = data.size() - from.size() + 1; i-- > 0;) {
        if (std::memcmp(data.data() + i, from.data(), from.size()) == 0) {
            std::memcpy(data.data() + i, to.data(), to.size());
            return data;
        }
    }
    return data;
}

void TestZip() {
    CHECK(ddt::IsSafeZipPath("save/TPP_GAME_DATA"));
    CHECK(ddt::IsSafeZipPath("logs/"));
    CHECK(ddt::IsSafeZipPath("a.b/c d.txt"));
    const char* unsafe[] = {"", "/abs", "../x", "a/../b", "a/./b", "./a", "a//b", "C:/x", "a\\b", "a:b", "con",
                            "dir/NUL.txt", "lpt1", "trail.", "trail ", "x\ty", "caf\xc3\xa9", "q?", "/", "a//"};
    for (const char* name : unsafe) Check(!ddt::IsSafeZipPath(name), name, __LINE__);
    CHECK(!ddt::IsSafeZipPath(std::string(300, 'a')));

    // Writer -> reader round trip.
    ddt::ZipWriter writer;
    std::uint16_t time = 0, date = 0;
    ddt::ToDosDateTime(2026, 10, 23, 13, 45, 31, time, date);
    CHECK(date == ((46 << 9) | (10 << 5) | 23) && time == ((13 << 11) | (45 << 5) | 15));
    writer.SetTimestamp(time, date);
    std::string error;
    const Bytes blob = Pseudorandom(70000, 3);
    CHECK(writer.AddDirectory("saves/", error));
    CHECK(writer.AddFile("saves/PERSONAL_DATA", blob, error));
    CHECK(writer.AddFile("empty.txt", Bytes{}, error));
    CHECK(writer.AddFile("manifest.json", FromText("{\"v\":1}"), error));
    CHECK(!writer.AddFile("Manifest.JSON", FromText("x"), error));  // case-insensitive duplicate
    CHECK(!writer.AddFile("saves", FromText("x"), error));          // already a folder
    CHECK(!writer.AddFile("../escape", FromText("x"), error));
    CHECK(!writer.AddFile("dir/", FromText("x"), error));
    Bytes archive;
    CHECK(writer.Finish(archive, error));
    CHECK(!writer.AddFile("late.txt", FromText("x"), error));

    ddt::ZipReader reader;
    CHECK(reader.Open(archive, ddt::ZipLimits{}, error));
    CHECK(reader.entries().size() == 4);
    const ddt::ZipEntry* folder = reader.Find("saves/");
    CHECK(folder && folder->is_directory);
    const ddt::ZipEntry* data = reader.Find("saves/PERSONAL_DATA");
    Bytes extracted;
    CHECK(data && !data->is_directory && data->method == 0 && data->dos_date == date && data->dos_time == time);
    CHECK(data && reader.Extract(*data, extracted, error) && extracted == blob);
    const ddt::ZipEntry* empty = reader.Find("empty.txt");
    CHECK(empty && reader.Extract(*empty, extracted, error) && extracted.empty());
    CHECK(reader.Find("missing") == nullptr);

    // Limits.
    ddt::ZipLimits few;
    few.max_entries = 3;
    CHECK(!reader.Open(archive, few, error) && Contains(error, "too many"));
    ddt::ZipLimits small;
    small.max_entry_size = 1000;
    CHECK(!reader.Open(archive, small, error) && Contains(error, "larger"));
    ddt::ZipLimits total;
    total.max_total_size = 70005;
    CHECK(!reader.Open(archive, total, error) && Contains(error, "larger"));
    ddt::ZipLimits tiny_archive;
    tiny_archive.max_archive_size = 100;
    CHECK(!reader.Open(archive, tiny_archive, error));

    // Corruption.
    Bytes corrupt = archive;
    corrupt[corrupt.size() / 4] ^= 0x40;  // inside PERSONAL_DATA
    CHECK(reader.Open(corrupt, ddt::ZipLimits{}, error));
    data = reader.Find("saves/PERSONAL_DATA");
    CHECK(data && !reader.Extract(*data, extracted, error) && Contains(error, "CRC"));
    CHECK(!reader.Open(Bytes(archive.begin(), archive.end() - 1), ddt::ZipLimits{}, error));
    CHECK(!reader.Open(Bytes(10, 0), ddt::ZipLimits{}, error));
    CHECK(!reader.Open(ReplaceLast(archive, "manifest.json", "../anifest.js"), ddt::ZipLimits{}, error) &&
          Contains(error, "unsafe"));
    // Names that only differ in case collide on Windows.
    ddt::ZipWriter pair_writer;
    Bytes pair;
    CHECK(pair_writer.AddFile("aaaa.txt", FromText("1"), error) && pair_writer.AddFile("bbbb.txt", FromText("2"), error) &&
          pair_writer.Finish(pair, error));
    CHECK(reader.Open(pair, ddt::ZipLimits{}, error));
    CHECK(!reader.Open(ReplaceLast(pair, "bbbb.txt", "AAAA.TXT"), ddt::ZipLimits{}, error) && Contains(error, "duplicate"));
    CHECK(!reader.Open(ReplaceLast(pair, "bbbb.txt", "aaaa.txt"), ddt::ZipLimits{}, error) && Contains(error, "duplicate"));

    // Deflated entries written by another tool.
    const Bytes python_zip = FromHexString(kZip);
    CHECK(reader.Open(python_zip, ddt::ZipLimits{}, error));
    std::string hello;
    for (int i = 0; i < 8; ++i) hello += "Hello from a deflated zip entry. ";
    const ddt::ZipEntry* deflated = reader.Find("hello.txt");
    CHECK(deflated && deflated->method == 8 && reader.Extract(*deflated, extracted, error) && extracted == FromText(hello));
    const ddt::ZipEntry* plain = reader.Find("data/blob.bin");
    Bytes counting(40);
    for (std::size_t i = 0; i < counting.size(); ++i) counting[i] = static_cast<std::uint8_t>(i);
    CHECK(plain && plain->method == 0 && reader.Extract(*plain, extracted, error) && extracted == counting);
    // A central directory that understates the size is caught while inflating.
    Bytes understated = python_zip;
    const std::size_t central = 0xA0;  // first central directory record (hello.txt)
    understated[central + 24] = 100;
    understated[central + 25] = 0;
    CHECK(reader.Open(understated, ddt::ZipLimits{}, error));
    deflated = reader.Find("hello.txt");
    CHECK(deflated && !reader.Extract(*deflated, extracted, error) && Contains(error, "larger than allowed"));
    CHECK(!reader.Open(ReplaceLast(python_zip, "data/blob.bin", "../a/blob.bi"), ddt::ZipLimits{}, error));
}

void TestQar() {
    // Known scramble bytes: hash low 0x359926d7, data block 26 uses table pair 2.
    Bytes zeros(8, 0);
    ddt::QarScramble(zeros.data(), zeros.size(), 0x359926d7u, 208);
    CHECK(ddt::ToHex(zeros.data(), zeros.size()) == "0632450802131288");
    Bytes text = FromText(LcgText(1001, 5));
    Bytes copy = text;
    ddt::QarScramble(copy.data(), copy.size(), 0x12345678u, 0);
    CHECK(copy != text);
    ddt::QarScramble(copy.data(), 500, 0x12345678u, 0);  // in two pieces: position-aware
    ddt::QarScramble(copy.data() + 500, copy.size() - 500, 0x12345678u, 500);
    CHECK(copy == text);

    std::vector<ddt::QarWriteEntry> entries;
    const std::size_t sizes[] = {0, 1, 7, 8, 9, 1000, 5000, 1024 - 32, 1024 - 31};
    std::uint64_t hash = 0x1d2a000000000000ull;
    for (std::size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        ddt::QarWriteEntry e;
        hash = hash * 6364136223846793005ull + 1442695040888963407ull;
        e.hash = hash;
        e.plain = Pseudorandom(sizes[i], static_cast<std::uint32_t>(i + 1));
        entries.push_back(std::move(e));
    }
    // A compressed entry (zlib payload) and one sharing a section key prefix.
    ddt::QarWriteEntry compressed;
    compressed.hash = 0xed53c3fac11e6913ull;
    compressed.plain = FromText(LcgText(3000, 12345));
    compressed.zlib = FromHexString(kDynZlib);
    entries.push_back(compressed);
    ddt::QarWriteEntry sibling;
    sibling.hash = 0xed53c3fa00000013ull;  // same section key (low byte + high u32) as `compressed`
    sibling.plain = FromText("sibling");
    entries.push_back(sibling);

    Bytes archive;
    std::string error;
    CHECK(ddt::WriteQar(entries, ddt::kQarRuntimeFlags, archive, error));
    CHECK(archive.size() % 1024 == 0 && std::memcmp(archive.data(), "SQAR", 4) == 0);
    ddt::QarReader reader;
    CHECK(reader.OpenMemory(archive, error));
    CHECK(reader.version() == 1 && reader.flags() == ddt::kQarRuntimeFlags && reader.entry_count() == entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        ddt::QarEntryInfo info;
        Bytes plain;
        const bool found = reader.Find(entries[i].hash, info, error) == ddt::QarReader::Lookup::found;
        Check(found, "QAR entry found", __LINE__);
        if (!found) continue;
        CHECK(info.offset % 1024 == 0 && info.plain_size == entries[i].plain.size());
        CHECK(reader.Read(info, plain, 1u << 20, error) && plain == entries[i].plain);
        ddt::QarEntryInfo at;
        CHECK(reader.EntryAt(i, at, error) && at.hash == entries[i].hash && at.offset == info.offset);
    }
    ddt::QarEntryInfo info;
    CHECK(reader.Find(0x0123456789abcdefull, info, error) == ddt::QarReader::Lookup::not_found);
    CHECK(reader.Find(compressed.hash, info, error) == ddt::QarReader::Lookup::found && info.stored_size != info.plain_size);
    Bytes plain;
    CHECK(!reader.Read(info, plain, 2999, error) && Contains(error, "larger"));

    // Deterministic: same input, same bytes.
    Bytes again;
    CHECK(ddt::WriteQar(entries, ddt::kQarRuntimeFlags, again, error) && again == archive);
    CHECK(!ddt::WriteQar({entries[0], entries[0]}, ddt::kQarRuntimeFlags, again, error) && Contains(error, "duplicate"));

    // Tampered data fails the MD5 check.
    Bytes tampered = archive;
    CHECK(reader.OpenMemory(tampered, error) && reader.Find(entries[5].hash, info, error) == ddt::QarReader::Lookup::found);
    tampered[static_cast<std::size_t>(info.offset) + 32 + 10] ^= 1;
    CHECK(reader.OpenMemory(tampered, error) && reader.Find(entries[5].hash, info, error) == ddt::QarReader::Lookup::found);
    CHECK(!reader.Read(info, plain, 1u << 20, error) && Contains(error, "MD5"));

    // Entries that carry the second encryption layer are refused.
    ddt::QarWriteEntry layered;
    layered.hash = 42;
    layered.plain = Bytes{0xE6, 0xEF, 0xF8, 0xA0, 1, 2, 3, 4};
    CHECK(ddt::WriteQar({layered}, ddt::kQarRuntimeFlags, archive, error) && reader.OpenMemory(archive, error));
    CHECK(reader.Find(42, info, error) == ddt::QarReader::Lookup::found);
    CHECK(!reader.Read(info, plain, 1024, error) && Contains(error, "second encryption layer"));

    // Many entries push the first file past 1024.
    std::vector<ddt::QarWriteEntry> many(200);
    for (std::size_t i = 0; i < many.size(); ++i) {
        many[i].hash = 0x5000000000000000ull + i * 0x100000001ull;
        many[i].plain = FromText("entry " + std::to_string(i));
    }
    CHECK(ddt::WriteQar(many, ddt::kQarRuntimeFlags, archive, error) && reader.OpenMemory(archive, error));
    CHECK(reader.Find(many[199].hash, info, error) == ddt::QarReader::Lookup::found && info.offset >= 2048);
    CHECK(reader.Read(info, plain, 1024, error) && plain == FromText("entry 199"));
    CHECK(ddt::WriteQar({}, ddt::kQarRuntimeFlags, archive, error) && archive.size() == 1024 &&
          reader.OpenMemory(archive, error) && reader.entry_count() == 0);

    // Header checks.
    CHECK(!reader.OpenMemory(Bytes(31, 0), error));
    Bytes wrong_version = archive;
    wrong_version[4 + 5 * 4] ^= 2;  // version field
    CHECK(!reader.OpenMemory(wrong_version, error) && Contains(error, "version"));
    Bytes huge_count = archive;
    huge_count[4 + 4 + 3] ^= 0x10;  // entry count far beyond the file
    CHECK(!reader.OpenMemory(huge_count, error));
}

void TestFpk() {
    // The layout of a reference-only package (one reference, no entries): 48-byte
    // header, one 16-byte reference record, the string, padding to 16.
    ddt::FpkFile refs;
    refs.references.push_back("/Assets/tpp/pack/resident/resident00.fpk");
    Bytes out;
    std::string error;
    CHECK(ddt::WriteFpk(refs, out, error));
    CHECK(out.size() == 112 && std::memcmp(out.data(), "foxfpk\0win", 10) == 0 && out[10] == 112);
    CHECK(out[32] == 2 && out[36] == 0 && out[40] == 1 && out[48] == 0x40 && out[56] == 40);
    CHECK(std::memcmp(out.data() + 64, refs.references[0].data(), 40) == 0 && out[104] == 0);

    ddt::FpkFile package;
    package.is_fpkd = true;
    const char* paths[] = {"/Assets/a/first.fox2", "/Assets/b/second.lua", "/Assets/c/third.bin"};
    const std::size_t sizes[] = {0, 5, 40};
    for (int i = 0; i < 3; ++i) {
        ddt::FpkEntry e;
        CHECK(ddt::SetFpkEntryPath(e, paths[i]));
        e.data = Pseudorandom(sizes[i], static_cast<std::uint32_t>(i + 9));
        package.entries.push_back(e);
    }
    package.references = {"/Assets/x/ref1.fpk", "/Assets/y/ref2.fpk"};
    CHECK(ddt::WriteFpk(package, out, error));
    CHECK(out.size() % 16 == 0 && std::memcmp(out.data(), "foxfpkdwin", 10) == 0);
    ddt::FpkFile parsed;
    CHECK(ddt::ParseFpk(out.data(), out.size(), parsed, error));
    CHECK(parsed.is_fpkd && parsed.entries.size() == 3 && parsed.references == package.references);
    for (int i = 0; i < 3 && parsed.entries.size() == 3; ++i) {
        CHECK(parsed.entries[i].path == paths[i] && parsed.entries[i].data == package.entries[i].data);
        ddt::Md5Digest md5{};
        ddt::Md5(paths[i], std::strlen(paths[i]), md5);
        CHECK(parsed.entries[i].path_md5 == md5);
    }
    Bytes rewritten;
    CHECK(ddt::WriteFpk(parsed, rewritten, error) && rewritten == out);
    // Data offsets are 16-aligned.
    for (int i = 0; i < 3; ++i) CHECK(out[48 + 48 * i] % 16 == 0);

    Bytes bad = out;
    bad[6] = 'x';
    CHECK(!ddt::ParseFpk(bad.data(), bad.size(), parsed, error));
    bad = out;
    bad.push_back(0);
    CHECK(!ddt::ParseFpk(bad.data(), bad.size(), parsed, error));
    bad = out;
    bad[48 + 16] = 0xF0;  // first entry's string offset past the end
    bad[48 + 17] = 0xFF;
    CHECK(!ddt::ParseFpk(bad.data(), bad.size(), parsed, error));
    bad = out;
    bad[48 + 8] = 0xFF;  // first entry's data size past the end
    bad[48 + 9] = 0xFF;
    CHECK(!ddt::ParseFpk(bad.data(), bad.size(), parsed, error));
    CHECK(!ddt::ParseFpk(out.data(), 40, parsed, error));
}

// ---- Synthetic FOX2.
void Put16(Bytes& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
}
void Put32(Bytes& b, std::uint32_t v) {
    Put16(b, static_cast<std::uint16_t>(v));
    Put16(b, static_cast<std::uint16_t>(v >> 16));
}
void Put64(Bytes& b, std::uint64_t v) {
    Put32(b, static_cast<std::uint32_t>(v));
    Put32(b, static_cast<std::uint32_t>(v >> 32));
}
void Pad(Bytes& b) {
    while (b.size() % 16) b.push_back(0);
}

struct Property {
    std::uint64_t name;
    std::uint8_t type, container;
    std::uint16_t count;
    Bytes values;  // unpadded
};
Bytes MakeProperty(const Property& p) {
    Bytes out;
    Bytes values = p.values;
    Pad(values);
    Put64(out, p.name);
    out.push_back(p.type);
    out.push_back(p.container);
    Put16(out, p.count);
    Put16(out, 0x20);
    Put16(out, static_cast<std::uint16_t>(0x20 + values.size()));
    out.resize(0x20, 0);
    out.insert(out.end(), values.begin(), values.end());
    return out;
}
Bytes MakeEntity(std::uint64_t class_hash, const std::vector<Property>& props) {
    Bytes body;
    for (const Property& p : props) {
        const Bytes one = MakeProperty(p);
        body.insert(body.end(), one.begin(), one.end());
    }
    Bytes out;
    Put16(out, 0x40);
    Put16(out, 0);
    Put16(out, 0);
    out.insert(out.end(), {'e', 'n', 't', 0});
    Put32(out, 0x03170000);  // address
    out.resize(0x1C, 0);
    Put64(out, class_hash);
    Put16(out, static_cast<std::uint16_t>(props.size()));
    Put16(out, 0);
    Put32(out, 0x40);
    Put32(out, static_cast<std::uint32_t>(0x40 + body.size()));
    Put32(out, static_cast<std::uint32_t>(0x40 + body.size()));
    out.resize(0x40, 0);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}
Bytes Hashes(std::initializer_list<std::uint64_t> list) {
    Bytes out;
    for (std::uint64_t v : list) Put64(out, v);
    return out;
}
Bytes MakeFox2(const std::vector<Bytes>& entities, const std::vector<std::pair<std::uint64_t, std::string>>& table) {
    Bytes out;
    Put32(out, 0x786F62F2);
    Put32(out, 0x35);
    Put32(out, static_cast<std::uint32_t>(entities.size()));
    Put32(out, 0);  // table offset, patched below
    Put32(out, 0x20);
    out.resize(0x20, 0);
    for (const Bytes& e : entities) out.insert(out.end(), e.begin(), e.end());
    const std::uint32_t table_offset = static_cast<std::uint32_t>(out.size());
    for (int i = 0; i < 4; ++i) out[12 + i] = static_cast<std::uint8_t>(table_offset >> (8 * i));
    for (const auto& s : table) {
        Put64(out, s.first);
        Put32(out, static_cast<std::uint32_t>(s.second.size()));
        out.insert(out.end(), s.second.begin(), s.second.end());
    }
    Put64(out, 0);
    Pad(out);
    out.insert(out.end(), {0, 0, 'e', 'n', 'd'});
    Pad(out);
    return out;
}

void TestFox2() {
    enum : std::uint64_t {
        Alpha = 0xA1, Beta = 0xA2, Name = 0xB1, Items = 0xB2, Count = 0xB3, Files = 0xB4, Paths = 0xB5, Empty = 0xB6,
        K1 = 0xC1, K2 = 0xC2, Zeta = 0xD1, File = 0xD2, Unknown = 0xEE
    };
    const std::uint64_t blank = ddt::kFoxEmptyStringHash;
    const std::vector<Bytes> entities = {
        MakeEntity(Alpha, {{Name, 11, 0, 1, Hashes({blank})},
                           {Items, 13, 2, 2, Hashes({K2, 0x03170100, K1, 0x03170200})},
                           {Count, 5, 0, 1, Bytes{5, 0, 0, 0}}}),
        MakeEntity(Beta, {{Name, 11, 0, 1, Hashes({Zeta})},
                          {Files, 20, 2, 1, Hashes({K1, File})},
                          {Paths, 12, 1, 2, Hashes({Unknown, Zeta})},
                          {Empty, 1, 2, 0, Bytes{}}}),
    };
    // The game sorts its table alphabetically and leaves out the empty string.
    const std::vector<std::pair<std::uint64_t, std::string>> table = {
        {File, "/path/file"}, {Alpha, "Alpha"}, {Beta, "Beta"},   {Count, "count"}, {Empty, "empty"},
        {Files, "files"},     {Items, "items"}, {K1, "k1"},       {K2, "k2"},       {Name, "name"},
        {Paths, "paths"},     {Zeta, "zeta"}};
    const Bytes fox2 = MakeFox2(entities, table);
    ddt::Fox2Strings strings;
    std::string error;
    CHECK(ddt::ParseFox2Strings(fox2.data(), fox2.size(), strings, error));
    CHECK(strings.table == table);
    Bytes out;
    CHECK(ddt::RewriteFox2StringTable(fox2.data(), fox2.size(), out, error));
    CHECK(out.size() % 16 == 0 && std::equal(fox2.begin(), fox2.begin() + static_cast<std::ptrdiff_t>(strings.table_offset), out.begin()));
    ddt::Fox2Strings rewritten;
    CHECK(ddt::ParseFox2Strings(out.data(), out.size(), rewritten, error));
    const std::vector<std::pair<std::uint64_t, std::string>> expected = {
        {Alpha, "Alpha"}, {Name, "name"}, {blank, ""},  {Items, "items"}, {K2, "k2"},        {K1, "k1"},       {Count, "count"},
        {Beta, "Beta"},   {Zeta, "zeta"}, {Files, "files"}, {File, "/path/file"}, {Paths, "paths"}, {Empty, "empty"}};
    CHECK(rewritten.table == expected);
    Bytes twice;  // idempotent once in first-use order
    CHECK(ddt::RewriteFox2StringTable(out.data(), out.size(), twice, error) && twice == out);

    auto orphan_table = table;
    orphan_table.push_back({0xF0, "orphan"});
    const Bytes orphan = MakeFox2(entities, orphan_table);
    CHECK(!ddt::RewriteFox2StringTable(orphan.data(), orphan.size(), out, error) && Contains(error, "orphan"));
    const Bytes link = MakeFox2({MakeEntity(Alpha, {{Name, 22, 0, 1, Bytes(32, 0)}})}, {{Alpha, "Alpha"}, {Name, "name"}});
    CHECK(!ddt::RewriteFox2StringTable(link.data(), link.size(), out, error) && Contains(error, "data type"));
    const Bytes vector_map = MakeFox2({MakeEntity(Alpha, {{Name, 14, 2, 1, Bytes(32, 0)}})}, {{Alpha, "Alpha"}, {Name, "name"}});
    CHECK(!ddt::RewriteFox2StringTable(vector_map.data(), vector_map.size(), out, error));
    Bytes no_end = fox2;
    no_end[no_end.size() - 12] = 'x';  // the "end" marker
    CHECK(!ddt::RewriteFox2StringTable(no_end.data(), no_end.size(), out, error));
    Bytes bad_property = fox2;
    bad_property[0x20 + 0x40 + 14] = 0xFF;  // first property's size past the entity
    bad_property[0x20 + 0x40 + 15] = 0x7F;
    CHECK(!ddt::RewriteFox2StringTable(bad_property.data(), bad_property.size(), out, error));
    Bytes bad_entity = fox2;
    bad_entity[0x20 + 6] = 'x';  // "ent" magic
    CHECK(!ddt::RewriteFox2StringTable(bad_entity.data(), bad_entity.size(), out, error));
}

std::wstring MakeTempFolder() {
    wchar_t base[MAX_PATH + 1]{};
    GetTempPathW(MAX_PATH, base);
    const std::wstring folder = std::wstring(base) + L"ddt_archive_tests_" + std::to_wstring(GetCurrentProcessId()) +
                                L"_" + std::to_wstring(GetTickCount64());
    CreateDirectoryW(folder.c_str(), nullptr);
    return folder;
}

std::size_t CountFiles(const std::wstring& folder) {
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((folder + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return 0;
    std::size_t count = 0;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) ++count;
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return count;
}

void TestBuilderRefusals() {
    std::string error;
    CHECK(ddt::CheckEmbeddedRuntimeAssets(error));

    const std::wstring root = MakeTempFolder();
    const std::wstring game = root + L"\\game", master = game + L"\\master", out_dir = root + L"\\out";
    const std::wstring chunk0 = master + L"\\chunk0.dat", out = out_dir + L"\\mgs-coop-player-runtime.dat";
    CreateDirectoryW(game.c_str(), nullptr);
    CreateDirectoryW(out_dir.c_str(), nullptr);

    float last = -1.0f;
    std::string stage;
    auto progress = [&](float fraction, const std::string& text) {
        last = fraction;
        stage = text;
    };
    CHECK(!ddt::BuildRuntimeDat(game, out, progress, error) && Contains(error, "master folder"));
    CreateDirectoryW(master.c_str(), nullptr);
    CHECK(!ddt::BuildRuntimeDat(game, out, progress, error) && Contains(error, "master/chunk0.dat was not found"));
    CHECK(!ddt::BuildRuntimeDat(game, root + L"\\missing\\x.dat", progress, error) && Contains(error, "output folder"));

    // Right hash, wrong bytes: the builder names the source and refuses.
    std::vector<ddt::QarWriteEntry> entries(2);
    entries[0].hash = 0x52292cbcda547cb2ull;  // two_players_game_obj.fpk
    entries[0].plain = Bytes(112, 0);
    entries[1].hash = 0xed512cbcda547cb2ull;  // two_players_game_obj.fpkd
    entries[1].plain = Bytes(2400, 0);
    Bytes archive;
    CHECK(ddt::WriteQar(entries, 0x3001E0, archive, error));
    CHECK(ddt::WriteWholeFile(chunk0, archive.data(), archive.size(), error));
    CHECK(!ddt::BuildRuntimeDat(game, out, progress, error) &&
          Contains(error, "two_players_game_obj.fpk in master/chunk0.dat does not match"));
    CHECK(last >= 0.0f && last < 0.8f && !stage.empty());

    // Wrong size.
    entries[0].plain = Bytes(100, 0);
    CHECK(ddt::WriteQar(entries, 0x3001E0, archive, error));
    CHECK(ddt::WriteWholeFile(chunk0, archive.data(), archive.size(), error));
    CHECK(!ddt::BuildRuntimeDat(game, out, progress, error) && Contains(error, "unexpected size"));

    // Missing entry.
    entries.erase(entries.begin());
    CHECK(ddt::WriteQar(entries, 0x3001E0, archive, error));
    CHECK(ddt::WriteWholeFile(chunk0, archive.data(), archive.size(), error));
    CHECK(!ddt::BuildRuntimeDat(game, out, progress, error) &&
          Contains(error, "two_players_game_obj.fpk in master/chunk0.dat was not found"));

    // Not a QAR at all.
    CHECK(ddt::WriteWholeFile(chunk0, "not an archive", 14, error));
    CHECK(!ddt::BuildRuntimeDat(game, out, progress, error) && Contains(error, "master/chunk0.dat could not be read"));

    // Nothing was left behind in the output folder; an existing file is not touched.
    CHECK(CountFiles(out_dir) == 0);
    CHECK(ddt::WriteWholeFile(out, "old", 3, error));
    CHECK(!ddt::BuildRuntimeDat(game, out, progress, error));
    Bytes kept;
    CHECK(ddt::ReadWholeFile(out, kept, 100, error) && kept == FromText("old") && CountFiles(out_dir) == 1);
    CHECK(!ddt::VerifyRuntimeDat(out, error));

    DeleteFileW(out.c_str());
    DeleteFileW(chunk0.c_str());
    RemoveDirectoryW(master.c_str());
    RemoveDirectoryW(game.c_str());
    RemoveDirectoryW(out_dir.c_str());
    RemoveDirectoryW(root.c_str());
}

void TestPaths() {
    CHECK(ddt::JoinPath(L"C:\\Games\\", L"\\master\\chunk0.dat") == L"C:\\Games\\master\\chunk0.dat");
    CHECK(ddt::JoinPath(L"D:/x", L"y") == L"D:/x\\y");
    CHECK(ddt::ParentDirectory(L"C:\\a\\b.dat") == L"C:\\a");
    CHECK(ddt::ParentDirectory(L"C:\\b.dat") == L"C:\\");
    CHECK(ddt::ParentDirectory(L"b.dat") == L".");
    CHECK(ddt::Utf8FromWide(L"caf\u00e9") == "caf\xc3\xa9" && ddt::WideFromUtf8("caf\xc3\xa9") == L"caf\u00e9");
}

}  // namespace

int main() {
    TestChecksums();
    TestInflate();
    TestZip();
    TestQar();
    TestFpk();
    TestFox2();
    TestBuilderRefusals();
    TestPaths();
    if (failures) {
        std::fprintf(stderr, "ddt_archive_tests: %u of %u checks failed\n", failures, checks);
        return 1;
    }
    std::printf("ddt_archive_tests: %u checks passed\n", checks);
    return 0;
}
