// Project-authored. MD5 and SHA-256 through Windows CNG (BCrypt), plus hex helpers.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ddt {

using Md5Digest = std::array<std::uint8_t, 16>;
using Sha256Digest = std::array<std::uint8_t, 32>;

bool Md5(const void* data, std::size_t size, Md5Digest& out);
bool Sha256(const void* data, std::size_t size, Sha256Digest& out);

// Incremental SHA-256 for data that arrives in pieces.
class Sha256Stream {
public:
    Sha256Stream();
    ~Sha256Stream();
    Sha256Stream(const Sha256Stream&) = delete;
    Sha256Stream& operator=(const Sha256Stream&) = delete;
    bool ok() const { return ok_; }
    bool Update(const void* data, std::size_t size);
    bool Finish(Sha256Digest& out);

private:
    void* algorithm_ = nullptr;
    void* hash_ = nullptr;
    bool ok_ = false;
};

// SHA-256 and size of a whole file, read in fixed-size pieces.
bool Sha256File(const std::wstring& path, Sha256Digest& out, std::uint64_t& size, std::string& error);

std::string ToHex(const std::uint8_t* data, std::size_t size);  // lowercase
template <std::size_t N>
std::string ToHex(const std::array<std::uint8_t, N>& digest) { return ToHex(digest.data(), N); }
// Accepts upper or lower case; false unless `hex` is exactly 2*N hex digits.
bool FromHex(const std::string& hex, std::uint8_t* out, std::size_t size);

}  // namespace ddt
