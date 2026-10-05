// Link v2 cryptography (docs/link-protocol.md "Keys"): the in-tree SHA-256 /
// HMAC-SHA256 against FIPS 180 and RFC 4231 and against BCrypt, the key
// schedule and join-code vectors (normative), and link-secret.txt validation.
#include "mgscoop/link_crypto.hpp"
#include "link_join_code.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#endif

namespace {
namespace link = mgscoop::link;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}
std::string Hex(const std::uint8_t* data, std::size_t size) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (std::size_t i = 0; i < size; ++i) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 15];
    }
    return out;
}
std::string Sha(const std::string& text) {
    std::uint8_t d[link::kSha256Size];
    link::Sha256Digest(text.data(), text.size(), d);
    return Hex(d, sizeof(d));
}
std::string HmacHex(const std::string& key, const std::string& message) {
    std::uint8_t d[link::kSha256Size];
    link::HmacSha256(key.data(), key.size(), message.data(), message.size(), d);
    return Hex(d, sizeof(d));
}

void Sha256Vectors() {
    // FIPS 180-2 / NIST examples.
    Check(Sha("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256 empty");
    Check(Sha("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256 abc");
    Check(Sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "sha256 two blocks");
    std::string million(1000000, 'a');
    Check(Sha(million) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "sha256 one million a");
    // Streaming in odd pieces equals one shot.
    link::Sha256 ctx;
    link::Sha256Init(ctx);
    for (std::size_t at = 0; at < million.size();) {
        const std::size_t take = (at % 97) + 1 < million.size() - at ? (at % 97) + 1 : million.size() - at;
        link::Sha256Update(ctx, million.data() + at, take);
        at += take;
    }
    std::uint8_t d[link::kSha256Size];
    link::Sha256Final(ctx, d);
    Check(Hex(d, sizeof(d)) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "sha256 streaming");
}

void HmacVectors() {
    // RFC 4231 test cases 1 and 2.
    Check(HmacHex(std::string(20, '\x0b'), "Hi There") == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
          "rfc4231 case 1");
    Check(HmacHex("Jefe", "what do ya want for nothing?") == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
          "rfc4231 case 2");
    // Case 6: a key longer than the block is hashed first.
    Check(HmacHex(std::string(131, '\xaa'), "Test Using Larger Than Block-Size Key - Hash Key First") ==
          "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54", "rfc4231 case 6");
    // Precomputed key reused: same digest every time; streaming equals one shot.
    link::HmacKey key{};
    Check(!key.valid, "a default key is not valid");
    link::HmacKeyInit(key, "Jefe", 4);
    std::uint8_t a[link::kSha256Size], b[link::kSha256Size];
    link::Hmac(key, "what do ya want for nothing?", 28, a);
    link::Sha256 ctx;
    link::HmacBegin(key, ctx);
    link::Sha256Update(ctx, "what do ya ", 11);
    link::Sha256Update(ctx, "want for nothing?", 17);
    link::HmacEnd(key, ctx, b);
    Check(Hex(a, 32) == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843" && std::memcmp(a, b, 32) == 0,
          "precomputed key, streaming");
    Check(link::ConstantTimeEqual(a, b, 32) && !link::ConstantTimeEqual(a, "x", 1), "constant-time compare");
    std::uint8_t z[4] = {1, 2, 3, 4};
    link::SecureZero(z, sizeof(z));
    Check(z[0] == 0 && z[3] == 0, "secure zero");
}

#ifdef _WIN32
// 1000 random key/message pairs against BCrypt's HMAC-SHA256.
void BcryptCrossCheck() {
    BCRYPT_ALG_HANDLE alg = nullptr;
    Check(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0, "bcrypt hmac");
    std::uint8_t key[200], message[3000];
    for (int i = 0; i < 1000; ++i) {
        std::uint8_t lengths[4];
        Check(BCryptGenRandom(nullptr, lengths, sizeof(lengths), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0, "rng");
        const ULONG key_size = 1 + (lengths[0] | (lengths[1] << 8)) % sizeof(key);
        const ULONG message_size = (lengths[2] | (lengths[3] << 8)) % sizeof(message);
        Check(BCryptGenRandom(nullptr, key, key_size, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0, "rng key");
        if (message_size) Check(BCryptGenRandom(nullptr, message, message_size, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0, "rng message");
        BCRYPT_HASH_HANDLE hash = nullptr;
        std::uint8_t expected[32], got[32];
        Check(BCryptCreateHash(alg, &hash, nullptr, 0, key, key_size, 0) == 0, "bcrypt create");
        Check(BCryptHashData(hash, message, message_size, 0) == 0, "bcrypt data");
        Check(BCryptFinishHash(hash, expected, sizeof(expected), 0) == 0, "bcrypt finish");
        BCryptDestroyHash(hash);
        link::HmacSha256(key, key_size, message, message_size, got);
        if (std::memcmp(expected, got, 32) != 0) {
            std::fprintf(stderr, "mismatch at pair %d (key %lu, message %lu)\n", i, key_size, message_size);
            Check(false, "in-tree HMAC equals BCrypt");
        }
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    Check(true, "1000 BCrypt pairs");
}
#endif

void KdfVectors() {
    const char secret[link::kSecretChars] = {'7', 'K', '3', 'Q', 'Z', '9', 'M', '2', 'X', 'W',
                                             'D', '4', 'H', 'T', '8', 'R', 'V', 'N', '6', 'B'};
    std::uint8_t raw[link::kSha256Size];
    link::DerivePskBytes(secret, raw);
    Check(Hex(raw, 32) == "aac90f271d64ba87d0d006ae8c65add7987dfa4a57948a99049a71a09133ba4e", "K_psk");
    link::HmacKey psk{};
    link::DerivePsk(secret, psk);
    std::uint8_t out[link::kSha256Size];
    link::Kdf(psk, "hs", nullptr, 0, out);
    Check(Hex(out, 32) == "32bccb0337d27add158fcfd94c38a4e84dc9bf5d39fe2979ec4e28e6d1204d90", "K_hs");
    std::uint8_t ng[link::kNonceSize], nh[link::kNonceSize], context[32];
    for (int i = 0; i < 16; ++i) { ng[i] = static_cast<std::uint8_t>(i); nh[i] = static_cast<std::uint8_t>(16 + i); }
    std::memcpy(context, ng, 16);
    std::memcpy(context + 16, nh, 16);
    link::Kdf(psk, "g2h", context, 32, out);
    Check(Hex(out, 32) == "a5eb24f7936817a1d2c0af85c3a177d6a3bc741914b0c7b3743d8ec492836b9e", "K_g2h");
    link::Kdf(psk, "h2g", context, 32, out);
    Check(Hex(out, 32) == "f98d2763078365ef13aab4fca0ad152b333b4ceb83536290dee09167ac9d8efd", "K_h2g");
    link::SessionKeys keys{};
    link::DeriveSessionKeys(psk, ng, nh, keys);
    Check(keys.sid == 0x3881b4a5739ed7d3ull, "sid");
    // The derived HmacKeys tag like keys built from the raw bytes.
    link::HmacKey from_raw{};
    link::HmacKeyInit(from_raw, out, 32);  // K_h2g bytes
    std::uint8_t t1[32], t2[32];
    link::Hmac(from_raw, "tag me", 6, t1);
    link::Hmac(keys.h2g, "tag me", 6, t2);
    Check(std::memcmp(t1, t2, 32) == 0 && keys.g2h.valid && keys.h2g.valid, "session HmacKeys");
    link::HmacKey hs{};
    link::DeriveHandshakeKey(psk, hs);
    link::Kdf(psk, "hs", nullptr, 0, out);
    link::HmacKeyInit(from_raw, out, 32);
    link::Hmac(from_raw, "x", 1, t1);
    link::Hmac(hs, "x", 1, t2);
    Check(std::memcmp(t1, t2, 32) == 0, "handshake HmacKey");
}

void JoinCodeVectors() {
    using mgscoop::test::CodeStatus;
    struct Case { const char* code; CodeStatus status; const char* host; unsigned port; };
    const Case cases[] = {
        {"MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@203.0.113.7:47830", CodeStatus::ok, "203.0.113.7", 47830},
        {"MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-2M@my-pc.tail1234.ts.net:47830", CodeStatus::ok, "my-pc.tail1234.ts.net", 47830},
        {"MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-MN@192.168.1.20:50001", CodeStatus::ok, "192.168.1.20", 50001},
        {"MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-M0@100.101.102.103:47830", CodeStatus::ok, "100.101.102.103", 47830},
        {"MGS1-C00PC-00PC0-0PC00-PC00P-V7@127.0.0.1:47820", CodeStatus::ok, "127.0.0.1", 47820},
        {"mgs1 7k3qz-9m2xw-d4ht8-rvn6b-dd@203.0.113.7:47830", CodeStatus::ok, "203.0.113.7", 47830},
        {"MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@203.0.113.7", CodeStatus::ok, "203.0.113.7", 47830},
        {"MGS1-7K3QZ-9M2XW-D4HT8-RVN6C-DD@203.0.113.7:47830", CodeStatus::bad_check, nullptr, 0},
        {"MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@203.0.113.8:47830", CodeStatus::bad_check, nullptr, 0},
        {"MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-QR@192.168.1.20:47831", CodeStatus::bad_check, nullptr, 0},
        {"MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@203.000.113.7:47830", CodeStatus::bad_host, nullptr, 0},
        {"MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@203.0.113.7:047830", CodeStatus::bad_port, nullptr, 0},
        {"MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@[::1]:47830", CodeStatus::ipv6_unsupported, nullptr, 0},
        {"MGS1-7K3QZ-9M2XW-D4HT8-RVN6U-DD@203.0.113.7:47830", CodeStatus::bad_char, nullptr, 0},
        // Tolerant and canonicalization extras.
        {"  MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@203.0.113.7:47830\r\n", CodeStatus::ok, "203.0.113.7", 47830},
        {"MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-2M@MY-PC.Tail1234.TS.net", CodeStatus::ok, "my-pc.tail1234.ts.net", 47830},
        {"MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-2M@my-pc.tail1234.ts.net.:47830", CodeStatus::bad_host, nullptr, 0},
        {"MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-2M@-my-pc.ts.net", CodeStatus::bad_host, nullptr, 0},
        {"MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@203.0.113.7:1023", CodeStatus::bad_port, nullptr, 0},
        {"MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@203.0.113.7:", CodeStatus::bad_port, nullptr, 0},
        {"MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@::1", CodeStatus::ipv6_unsupported, nullptr, 0},
        {"MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD", CodeStatus::bad_host, nullptr, 0},
        {"MGS2-7K3QZ-9M2XW-D4HT8-RVN6B-DD@203.0.113.7", CodeStatus::bad_prefix, nullptr, 0},
        {"MGS1-7K3QZ-9M2XW-D4HT8-RVN6-DD@203.0.113.7", CodeStatus::bad_length, nullptr, 0},
        {"   ", CodeStatus::empty, nullptr, 0},
    };
    for (const auto& c : cases) {
        mgscoop::test::JoinCode code{};
        const CodeStatus status = mgscoop::test::ParseJoinCode(c.code, code);
        if (status != c.status) std::fprintf(stderr, "%s -> %s\n", c.code, mgscoop::test::CodeStatusName(status));
        Check(status == c.status, "join code status");
        if (c.host) Check(std::strcmp(code.host, c.host) == 0 && code.port == c.port, "join code host and port");
        if (status == CodeStatus::ok && c.port == 47820) Check(std::strcmp(code.secret, link::kLabSecret) == 0, "lab secret code");
        if (status == CodeStatus::ok && c.port != 47820) Check(std::strcmp(code.secret, "7K3QZ9M2XWD4HT8RVN6B") == 0, "code secret");
    }
    char check[3];
    mgscoop::test::JoinCodeCheck("7K3QZ9M2XWD4HT8RVN6C", "203.0.113.7", 47830, check);
    Check(std::strcmp(check, "PY") == 0, "correct check for RVN6C is PY");
    mgscoop::test::JoinCodeCheck("7K3QZ9M2XWD4HT8RVN6B", "203.0.113.8", 47830, check);
    Check(std::strcmp(check, "2Q") == 0, "correct check for .8 is 2Q");
    mgscoop::test::JoinCodeCheck("7K3QZ9M2XWD4HT8RVN6B", "192.168.1.20", 47831, check);
    Check(std::strcmp(check, "BB") == 0, "correct check for 47831 is BB");
    mgscoop::test::JoinCodeCheck("7K3QZ9M2XWD4HT8RVN6B", "192.168.1.20", 47830, check);
    Check(std::strcmp(check, "QR") == 0, "QR is 47830's check");
    // The O/I/L mapping: a secret written with O and L parses to 0 and 1.
    char secret[] = "70000000000000000001";
    mgscoop::test::JoinCodeCheck(secret, "203.0.113.7", 47830, check);
    std::string code = std::string("MGS1-7OOOO-OOOOO-OOOOO-OOOOL-") + check + "@203.0.113.7";
    mgscoop::test::JoinCode parsed{};
    Check(mgscoop::test::ParseJoinCode(code.c_str(), parsed) == CodeStatus::ok && std::strcmp(parsed.secret, secret) == 0,
          "O -> 0 and L -> 1");
}

void SecretFile() {
    char out[link::kSecretChars]{};
    Check(link::ParseSecretText("7K3QZ9M2XWD4HT8RVN6B\n", 21, out) == link::SecretStatus::ok &&
          std::memcmp(out, "7K3QZ9M2XWD4HT8RVN6B", 20) == 0, "secret LF");
    Check(link::ParseSecretText("7K3QZ9M2XWD4HT8RVN6B\r\n", 22, out) == link::SecretStatus::ok, "secret CRLF");
    Check(link::ParseSecretText("7K3QZ9M2XWD4HT8RVN6B", 20, out) == link::SecretStatus::bad_length, "secret without newline");
    Check(link::ParseSecretText("7K3QZ9M2XWD4HT8RVN6B\n\n", 22, out) == link::SecretStatus::bad_char, "secret LF LF");
    Check(link::ParseSecretText("7K3QZ9M2XWD4HT8RVN6\n", 20, out) == link::SecretStatus::bad_length, "secret short");
    Check(link::ParseSecretText("", 0, out) == link::SecretStatus::bad_length, "secret empty");
    Check(link::ParseSecretText("7k3qz9m2xwd4ht8rvn6b\n", 21, out) == link::SecretStatus::bad_char, "secret lower case");
    Check(link::ParseSecretText("7K3QZ9M2XWD4HT8RVN6U\n", 21, out) == link::SecretStatus::bad_char, "secret U");
    Check(link::ParseSecretText("7K3QZ9M2XWD4HT8RVNOB\n", 21, out) == link::SecretStatus::bad_char, "secret O is not canonical");
    Check(std::strcmp(link::SecretStatusName(link::SecretStatus::missing), "missing") == 0 &&
          std::strcmp(link::SecretStatusName(link::SecretStatus::ok), "ok") == 0, "secret status names");
    for (const char* p = link::kLabSecret; *p; ++p) Check(link::IsCanonicalSecretChar(*p), "lab secret is canonical");
}
} // namespace

int main() {
    Sha256Vectors();
    HmacVectors();
#ifdef _WIN32
    BcryptCrossCheck();
#endif
    KdfVectors();
    JoinCodeVectors();
    SecretFile();
    std::printf("link_crypto_tests: %u checks passed\n", checks);
    return 0;
}
