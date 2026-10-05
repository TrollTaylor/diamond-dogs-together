// Original MGS Co-op link v2 cryptography: in-tree SHA-256 / HMAC-SHA256 and
// the link key schedule (docs/link-protocol.md).
#include "mgscoop/link_crypto.hpp"

#include <cstring>

namespace mgscoop::link {
namespace {
constexpr std::uint32_t kInit[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
constexpr std::uint32_t kRound[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

std::uint32_t Rotr(std::uint32_t x, unsigned n) noexcept { return (x >> n) | (x << (32 - n)); }

void Compress(std::uint32_t (&h)[8], const std::uint8_t* block) noexcept {
    std::uint32_t w[64];
    for (unsigned i = 0; i < 16; ++i)
        w[i] = (static_cast<std::uint32_t>(block[4 * i]) << 24) | (static_cast<std::uint32_t>(block[4 * i + 1]) << 16) |
               (static_cast<std::uint32_t>(block[4 * i + 2]) << 8) | static_cast<std::uint32_t>(block[4 * i + 3]);
    for (unsigned i = 16; i < 64; ++i) {
        const std::uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (unsigned i = 0; i < 64; ++i) {
        const std::uint32_t s1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = k + s1 + ch + kRound[i] + w[i];
        const std::uint32_t s0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = s0 + maj;
        k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
}

void StateFromKeyBlock(const std::uint8_t (&block)[64], std::uint8_t pad, std::uint32_t (&out)[8]) noexcept {
    std::uint8_t padded[64];
    for (unsigned i = 0; i < 64; ++i) padded[i] = static_cast<std::uint8_t>(block[i] ^ pad);
    for (unsigned i = 0; i < 8; ++i) out[i] = kInit[i];
    Compress(out, padded);
    SecureZero(padded, sizeof(padded));
}

void ResumeState(const std::uint32_t (&state)[8], Sha256& ctx) noexcept {
    for (unsigned i = 0; i < 8; ++i) ctx.h[i] = state[i];
    ctx.length = 64;
    ctx.used = 0;
}
} // namespace

void Sha256Init(Sha256& ctx) noexcept {
    for (unsigned i = 0; i < 8; ++i) ctx.h[i] = kInit[i];
    ctx.length = 0;
    ctx.used = 0;
}

void Sha256Update(Sha256& ctx, const void* data, std::size_t size) noexcept {
    const auto* p = static_cast<const std::uint8_t*>(data);
    if (!p) return;
    ctx.length += size;
    if (ctx.used) {
        const std::size_t take = size < 64 - ctx.used ? size : 64 - ctx.used;
        std::memcpy(ctx.block + ctx.used, p, take);
        ctx.used += take;
        p += take;
        size -= take;
        if (ctx.used < 64) return;
        Compress(ctx.h, ctx.block);
        ctx.used = 0;
    }
    while (size >= 64) {
        Compress(ctx.h, p);
        p += 64;
        size -= 64;
    }
    if (size) {
        std::memcpy(ctx.block, p, size);
        ctx.used = size;
    }
}

void Sha256Final(Sha256& ctx, std::uint8_t (&out)[kSha256Size]) noexcept {
    const std::uint64_t bits = ctx.length * 8;
    ctx.block[ctx.used++] = 0x80;
    if (ctx.used > 56) {
        while (ctx.used < 64) ctx.block[ctx.used++] = 0;
        Compress(ctx.h, ctx.block);
        ctx.used = 0;
    }
    while (ctx.used < 56) ctx.block[ctx.used++] = 0;
    for (unsigned i = 0; i < 8; ++i) ctx.block[56 + i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
    Compress(ctx.h, ctx.block);
    for (unsigned i = 0; i < 8; ++i) {
        out[4 * i] = static_cast<std::uint8_t>(ctx.h[i] >> 24);
        out[4 * i + 1] = static_cast<std::uint8_t>(ctx.h[i] >> 16);
        out[4 * i + 2] = static_cast<std::uint8_t>(ctx.h[i] >> 8);
        out[4 * i + 3] = static_cast<std::uint8_t>(ctx.h[i]);
    }
    SecureZero(ctx.block, sizeof(ctx.block));
}

void Sha256Digest(const void* data, std::size_t size, std::uint8_t (&out)[kSha256Size]) noexcept {
    Sha256 ctx;
    Sha256Init(ctx);
    Sha256Update(ctx, data, size);
    Sha256Final(ctx, out);
}

void HmacKeyInit(HmacKey& out, const void* key, std::size_t size) noexcept {
    std::uint8_t block[64]{};
    if (size > 64) {
        std::uint8_t digest[kSha256Size];
        Sha256Digest(key, size, digest);
        std::memcpy(block, digest, sizeof(digest));
        SecureZero(digest, sizeof(digest));
    } else if (size) {
        std::memcpy(block, key, size);
    }
    StateFromKeyBlock(block, 0x36, out.inner);
    StateFromKeyBlock(block, 0x5c, out.outer);
    out.valid = true;
    SecureZero(block, sizeof(block));
}

void HmacBegin(const HmacKey& key, Sha256& ctx) noexcept { ResumeState(key.inner, ctx); }

void HmacEnd(const HmacKey& key, Sha256& ctx, std::uint8_t (&out)[kSha256Size]) noexcept {
    std::uint8_t inner[kSha256Size];
    Sha256Final(ctx, inner);
    ResumeState(key.outer, ctx);
    Sha256Update(ctx, inner, sizeof(inner));
    Sha256Final(ctx, out);
    SecureZero(inner, sizeof(inner));
}

void Hmac(const HmacKey& key, const void* message, std::size_t size, std::uint8_t (&out)[kSha256Size]) noexcept {
    Sha256 ctx;
    HmacBegin(key, ctx);
    Sha256Update(ctx, message, size);
    HmacEnd(key, ctx, out);
}

void HmacSha256(const void* key, std::size_t key_size, const void* message, std::size_t size,
                std::uint8_t (&out)[kSha256Size]) noexcept {
    HmacKey k;
    HmacKeyInit(k, key, key_size);
    Hmac(k, message, size, out);
    SecureZero(&k, sizeof(k));
}

bool ConstantTimeEqual(const void* a, const void* b, std::size_t size) noexcept {
    const auto* x = static_cast<const std::uint8_t*>(a);
    const auto* y = static_cast<const std::uint8_t*>(b);
    unsigned diff = 0;
    for (std::size_t i = 0; i < size; ++i) diff |= static_cast<unsigned>(x[i] ^ y[i]);
    return diff == 0;
}

void SecureZero(void* data, std::size_t size) noexcept {
    volatile auto* p = static_cast<volatile std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) p[i] = 0;
}

void Kdf(const HmacKey& key, const char* label, const void* context, std::size_t context_size,
         std::uint8_t (&out)[kSha256Size]) noexcept {
    Sha256 ctx;
    HmacBegin(key, ctx);
    Sha256Update(ctx, label, std::strlen(label));
    const std::uint8_t zero = 0;
    Sha256Update(ctx, &zero, 1);
    if (context_size) Sha256Update(ctx, context, context_size);
    HmacEnd(key, ctx, out);
}

void DerivePskBytes(const char (&secret)[kSecretChars], std::uint8_t (&out)[kSha256Size]) noexcept {
    HmacSha256(kPskLabel, sizeof(kPskLabel) - 1, secret, kSecretChars, out);
}

void DerivePsk(const char (&secret)[kSecretChars], HmacKey& out) noexcept {
    std::uint8_t raw[kSha256Size];
    DerivePskBytes(secret, raw);
    HmacKeyInit(out, raw, sizeof(raw));
    SecureZero(raw, sizeof(raw));
}

void DeriveHandshakeKey(const HmacKey& psk, HmacKey& out) noexcept {
    std::uint8_t raw[kSha256Size];
    Kdf(psk, "hs", nullptr, 0, raw);
    HmacKeyInit(out, raw, sizeof(raw));
    SecureZero(raw, sizeof(raw));
}

void DeriveSessionKeys(const HmacKey& psk, const std::uint8_t (&ng)[kNonceSize], const std::uint8_t (&nh)[kNonceSize],
                       SessionKeys& out) noexcept {
    std::uint8_t context[2 * kNonceSize];
    std::memcpy(context, ng, kNonceSize);
    std::memcpy(context + kNonceSize, nh, kNonceSize);
    std::uint8_t raw[kSha256Size];
    Kdf(psk, "g2h", context, sizeof(context), raw);
    HmacKeyInit(out.g2h, raw, sizeof(raw));
    Kdf(psk, "h2g", context, sizeof(context), raw);
    HmacKeyInit(out.h2g, raw, sizeof(raw));
    Kdf(psk, "sid", context, sizeof(context), raw);
    std::uint64_t sid = 0;
    for (unsigned i = 0; i < 8; ++i) sid |= static_cast<std::uint64_t>(raw[i]) << (8 * i);
    out.sid = sid ? sid : 1;
    SecureZero(raw, sizeof(raw));
}

bool IsCanonicalSecretChar(char c) noexcept {
    for (const char* p = kCrockford; *p; ++p)
        if (*p == c) return true;
    return false;
}

const char* SecretStatusName(SecretStatus status) noexcept {
    switch (status) {
    case SecretStatus::missing: return "missing";
    case SecretStatus::bad_length: return "bad_length";
    case SecretStatus::bad_char: return "bad_char";
    default: return "ok";
    }
}

SecretStatus ParseSecretText(const char* text, std::size_t size, char (&out)[kSecretChars]) noexcept {
    if (!text || (size != kSecretChars + 1 && size != kSecretChars + 2)) return SecretStatus::bad_length;
    for (std::size_t i = 0; i < kSecretChars; ++i)
        if (!IsCanonicalSecretChar(text[i])) return SecretStatus::bad_char;
    const bool lf = size == kSecretChars + 1 && text[kSecretChars] == '\n';
    const bool crlf = size == kSecretChars + 2 && text[kSecretChars] == '\r' && text[kSecretChars + 1] == '\n';
    if (!lf && !crlf) return SecretStatus::bad_char;
    std::memcpy(out, text, kSecretChars);
    return SecretStatus::ok;
}

} // namespace mgscoop::link
