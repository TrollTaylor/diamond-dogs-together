#pragma once
// Two-PC link v2 cryptography (docs/link-protocol.md "Keys"). Pure, portable
// and allocation-free: an in-tree SHA-256 and HMAC-SHA256 with precomputed
// inner/outer states, the key schedule, constant-time comparison and the
// link-secret.txt validator. No handles and no lifetime: every key is a POD
// that can live in constant-initialized static storage and be used from any
// thread. Nonces come from the caller (BCryptGenRandom on the transport
// thread in partner_net); nothing here is random.
#include <cstddef>
#include <cstdint>

namespace mgscoop::link {

inline constexpr std::size_t kSha256Size = 32;
inline constexpr std::size_t kSecretChars = 20;   // 100-bit pre-shared secret
inline constexpr std::size_t kNonceSize = 16;
// Crockford base32 (no I, L, O, U).
inline constexpr char kCrockford[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
// Lab-only secret: used only with net_bind=loopback and no link-secret.txt.
inline constexpr char kLabSecret[] = "C00PC00PC00PC00PC00P";
// HMAC key of K_psk = HMAC-SHA256(key = kPskLabel, msg = S).
inline constexpr char kPskLabel[] = "mgs-coop/link/v2";

struct Sha256 {
    std::uint32_t h[8];
    std::uint64_t length;  // bytes processed
    std::uint8_t block[64];
    std::size_t used;
};
void Sha256Init(Sha256& ctx) noexcept;
void Sha256Update(Sha256& ctx, const void* data, std::size_t size) noexcept;
void Sha256Final(Sha256& ctx, std::uint8_t (&out)[kSha256Size]) noexcept;
void Sha256Digest(const void* data, std::size_t size, std::uint8_t (&out)[kSha256Size]) noexcept;

// HMAC-SHA256 key with the SHA-256 states after the (key ^ ipad) and
// (key ^ opad) blocks. A default (all-zero) key is not valid.
struct HmacKey {
    std::uint32_t inner[8];
    std::uint32_t outer[8];
    bool valid;
};
void HmacKeyInit(HmacKey& out, const void* key, std::size_t size) noexcept;
// Streaming use: HmacBegin, Sha256Update..., HmacEnd.
void HmacBegin(const HmacKey& key, Sha256& ctx) noexcept;
void HmacEnd(const HmacKey& key, Sha256& ctx, std::uint8_t (&out)[kSha256Size]) noexcept;
void Hmac(const HmacKey& key, const void* message, std::size_t size, std::uint8_t (&out)[kSha256Size]) noexcept;
// One-shot helper (tests and the key schedule).
void HmacSha256(const void* key, std::size_t key_size, const void* message, std::size_t size,
                std::uint8_t (&out)[kSha256Size]) noexcept;

// Constant-time comparison (no early exit).
bool ConstantTimeEqual(const void* a, const void* b, std::size_t size) noexcept;
// Zeroes memory the optimizer cannot elide (volatile stores).
void SecureZero(void* data, std::size_t size) noexcept;

// Kdf(K, label, ctx) = HMAC-SHA256(K, ASCII(label) || 0x00 || ctx).
void Kdf(const HmacKey& key, const char* label, const void* context, std::size_t context_size,
         std::uint8_t (&out)[kSha256Size]) noexcept;

// K_psk = HMAC-SHA256(kPskLabel, S) for the 20 canonical secret characters;
// the raw K_psk bytes are cleared before returning.
void DerivePsk(const char (&secret)[kSecretChars], HmacKey& out) noexcept;
// K_hs = Kdf(K_psk, "hs", "").
void DeriveHandshakeKey(const HmacKey& psk, HmacKey& out) noexcept;
// K_g2h / K_h2g = Kdf(K_psk, "g2h"/"h2g", Ng || Nh); sid = u64 LE of
// Kdf(K_psk, "sid", Ng || Nh)[0..8), 1 when that is 0.
struct SessionKeys {
    HmacKey g2h, h2g;
    std::uint64_t sid;
};
void DeriveSessionKeys(const HmacKey& psk, const std::uint8_t (&ng)[kNonceSize], const std::uint8_t (&nh)[kNonceSize],
                       SessionKeys& out) noexcept;
// Raw-byte variants for the test vectors (K_psk, K_hs and the session keys as bytes).
void DerivePskBytes(const char (&secret)[kSecretChars], std::uint8_t (&out)[kSha256Size]) noexcept;

// True for an upper-case character of kCrockford.
bool IsCanonicalSecretChar(char c) noexcept;

// link-secret.txt: exactly 20 canonical characters followed by "\n" or
// "\r\n" (21 or 22 bytes, ASCII). `missing` is the caller's (no file).
enum class SecretStatus : std::uint8_t { ok, missing, bad_length, bad_char };
const char* SecretStatusName(SecretStatus status) noexcept;
SecretStatus ParseSecretText(const char* text, std::size_t size, char (&out)[kSecretChars]) noexcept;

} // namespace mgscoop::link
