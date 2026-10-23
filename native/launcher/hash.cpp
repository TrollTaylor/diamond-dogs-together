// Project-authored. See hash.hpp.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include "launcher/hash.hpp"

#include <vector>

#include "launcher/file_io.hpp"

namespace ddt {
namespace {

bool OneShot(LPCWSTR algorithm_id, const void* data, std::size_t size, unsigned char* out, ULONG out_size) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, algorithm_id, nullptr, 0));
    if (ok) ok = BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0));
    const auto* bytes = static_cast<const unsigned char*>(data);
    while (ok && size > 0) {
        const ULONG piece = size > (1u << 30) ? (1u << 30) : static_cast<ULONG>(size);
        ok = BCRYPT_SUCCESS(BCryptHashData(hash, const_cast<PUCHAR>(bytes), piece, 0));
        bytes += piece;
        size -= piece;
    }
    if (ok) ok = BCRYPT_SUCCESS(BCryptFinishHash(hash, out, out_size, 0));
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}

}  // namespace

bool Md5(const void* data, std::size_t size, Md5Digest& out) {
    return OneShot(BCRYPT_MD5_ALGORITHM, data, size, out.data(), static_cast<ULONG>(out.size()));
}

bool Sha256(const void* data, std::size_t size, Sha256Digest& out) {
    return OneShot(BCRYPT_SHA256_ALGORITHM, data, size, out.data(), static_cast<ULONG>(out.size()));
}

Sha256Stream::Sha256Stream() {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    ok_ = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
    if (ok_) ok_ = BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0));
    algorithm_ = algorithm;
    hash_ = hash;
}

Sha256Stream::~Sha256Stream() {
    if (hash_) BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(hash_));
    if (algorithm_) BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(algorithm_), 0);
}

bool Sha256Stream::Update(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    while (ok_ && size > 0) {
        const ULONG piece = size > (1u << 30) ? (1u << 30) : static_cast<ULONG>(size);
        ok_ = BCRYPT_SUCCESS(BCryptHashData(static_cast<BCRYPT_HASH_HANDLE>(hash_), const_cast<PUCHAR>(bytes), piece, 0));
        bytes += piece;
        size -= piece;
    }
    return ok_;
}

bool Sha256Stream::Finish(Sha256Digest& out) {
    if (ok_) ok_ = BCRYPT_SUCCESS(BCryptFinishHash(static_cast<BCRYPT_HASH_HANDLE>(hash_), out.data(),
                                                   static_cast<ULONG>(out.size()), 0));
    const bool result = ok_;
    ok_ = false;  // a finished CNG hash object cannot be reused
    return result;
}

bool Sha256File(const std::wstring& path, Sha256Digest& out, std::uint64_t& size, std::string& error) {
    ReadOnlyFile file;
    if (!file.Open(path, error)) return false;
    Sha256Stream stream;
    if (!stream.ok()) {
        error = "SHA-256 is not available";
        return false;
    }
    std::vector<std::uint8_t> buffer(1u << 20);
    std::uint64_t offset = 0;
    while (offset < file.size()) {
        const std::uint64_t left = file.size() - offset;
        const std::size_t piece = left < buffer.size() ? static_cast<std::size_t>(left) : buffer.size();
        if (!file.ReadAt(offset, buffer.data(), piece, error)) return false;
        if (!stream.Update(buffer.data(), piece)) {
            error = "SHA-256 failed";
            return false;
        }
        offset += piece;
    }
    if (!stream.Finish(out)) {
        error = "SHA-256 failed";
        return false;
    }
    size = file.size();
    return true;
}

std::string ToHex(const std::uint8_t* data, std::size_t size) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 15]);
    }
    return out;
}

bool FromHex(const std::string& hex, std::uint8_t* out, std::size_t size) {
    if (hex.size() != size * 2) return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < size; ++i) {
        const int hi = nibble(hex[2 * i]), lo = nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<std::uint8_t>(hi << 4 | lo);
    }
    return true;
}

}  // namespace ddt
