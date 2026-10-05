#pragma once
// Test-only join-code parser (docs/link-protocol.md "Join code"): the native
// mirror of tools/CoopPlay.psm1 ConvertFrom-CoopJoinCode, used by
// link_crypto_tests and lab_ini_check to cross-check the normative vectors.
// The DLL never parses codes (it reads link-secret.txt).
//
//   MGS1-SSSSS-SSSSS-SSSSS-SSSSS-CC@<host>:<port>
//   check = SHA256("MGS1|" + S + "|" + host + "|" + port) over the canonical forms:
//   c1 = alphabet[d[0] >> 3], c2 = alphabet[((d[0] & 7) << 2) | (d[1] >> 6)]
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "mgscoop/link_crypto.hpp"

namespace mgscoop::test {

enum class CodeStatus { ok, bad_prefix, bad_char, bad_length, bad_check, bad_host, bad_port, ipv6_unsupported, empty };

inline const char* CodeStatusName(CodeStatus status) {
    switch (status) {
    case CodeStatus::bad_prefix: return "bad_prefix";
    case CodeStatus::bad_char: return "bad_char";
    case CodeStatus::bad_length: return "bad_length";
    case CodeStatus::bad_check: return "bad_check";
    case CodeStatus::bad_host: return "bad_host";
    case CodeStatus::bad_port: return "bad_port";
    case CodeStatus::ipv6_unsupported: return "ipv6_unsupported";
    case CodeStatus::empty: return "empty";
    default: return "ok";
    }
}

struct JoinCode {
    char secret[link::kSecretChars + 1];
    char host[254];
    unsigned port;
};

inline void JoinCodeCheck(const char* secret, const char* host, unsigned port, char (&out)[3]) {
    char text[320]{};
    std::snprintf(text, sizeof(text), "MGS1|%s|%s|%u", secret, host, port);
    std::uint8_t d[link::kSha256Size];
    link::Sha256Digest(text, std::strlen(text), d);
    out[0] = link::kCrockford[d[0] >> 3];
    out[1] = link::kCrockford[((d[0] & 7) << 2) | (d[1] >> 6)];
    out[2] = 0;
}

namespace detail {
inline bool CanonicalIpv4(const char* host) {
    int octets = 0;
    const char* p = host;
    for (;;) {
        const char* start = p;
        unsigned value = 0;
        while (*p >= '0' && *p <= '9') {
            value = value * 10 + static_cast<unsigned>(*p - '0');
            if (p - start >= 3) return false;
            ++p;
        }
        const std::ptrdiff_t digits = p - start;
        if (!digits || (digits > 1 && *start == '0') || value > 255) return false;
        ++octets;
        if (!*p) break;
        if (*p != '.') return false;
        ++p;
    }
    return octets == 4;
}
inline bool CanonicalName(const char* host) {
    const std::size_t length = std::strlen(host);
    if (!length || length > 253 || host[length - 1] == '.') return false;
    bool letter = false;
    std::size_t label = 0;
    char previous = '.';
    for (std::size_t i = 0; i <= length; ++i) {
        const char c = host[i];
        if (c == '.' || c == 0) {
            if (!label || label > 63 || previous == '-') return false;
            label = 0;
        } else {
            const bool lower = c >= 'a' && c <= 'z', digit = c >= '0' && c <= '9';
            if (!lower && !digit && c != '-') return false;
            if (c == '-' && label == 0) return false;
            letter = letter || lower;
            ++label;
        }
        previous = c;
    }
    return letter;
}
} // namespace detail

inline CodeStatus ParseJoinCode(const char* text, JoinCode& out) {
    out = JoinCode{};
    char compact[512]{};
    std::size_t n = 0;
    for (std::size_t i = 0; text && text[i]; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (n + 1 >= sizeof(compact)) return CodeStatus::bad_length;
        compact[n++] = c;
    }
    if (!n) return CodeStatus::empty;
    char* at = std::strrchr(compact, '@');
    if (!at) return CodeStatus::bad_host;
    *at = 0;
    const char* right = at + 1;
    // Left: upper-case, '-' removed, MGS1 prefix, Crockford with O->0, I/L->1.
    char left[512]{};
    std::size_t m = 0;
    for (const char* p = compact; *p; ++p) {
        if (*p == '-') continue;
        left[m++] = (*p >= 'a' && *p <= 'z') ? static_cast<char>(*p - 'a' + 'A') : *p;
    }
    if (std::strncmp(left, "MGS1", 4) != 0) return CodeStatus::bad_prefix;
    char body[512]{};
    std::size_t k = 0;
    for (const char* p = left + 4; *p; ++p) {
        char c = *p;
        if (c == 'O') c = '0';
        else if (c == 'I' || c == 'L') c = '1';
        if (!link::IsCanonicalSecretChar(c)) return CodeStatus::bad_char;
        body[k++] = c;
    }
    if (k != link::kSecretChars + 2) return CodeStatus::bad_length;
    // Right: host[:port], IPv4 or DNS name only.
    if (right[0] == '[') return CodeStatus::ipv6_unsupported;
    const char* colon = std::strchr(right, ':');
    if (colon && std::strchr(colon + 1, ':')) return CodeStatus::ipv6_unsupported;
    const std::size_t host_length = colon ? static_cast<std::size_t>(colon - right) : std::strlen(right);
    if (!host_length || host_length >= sizeof(out.host)) return CodeStatus::bad_host;
    for (std::size_t i = 0; i < host_length; ++i) {
        const char c = right[i];
        out.host[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    out.host[host_length] = 0;
    bool numeric = true;
    for (const char* p = out.host; *p; ++p) numeric = numeric && ((*p >= '0' && *p <= '9') || *p == '.');
    if (numeric ? !detail::CanonicalIpv4(out.host) : !detail::CanonicalName(out.host)) return CodeStatus::bad_host;
    out.port = 47830;
    if (colon) {
        const char* p = colon + 1;
        const std::size_t digits = std::strlen(p);
        if (!digits || digits > 5 || p[0] == '0') return CodeStatus::bad_port;
        unsigned value = 0;
        for (std::size_t i = 0; i < digits; ++i) {
            if (p[i] < '0' || p[i] > '9') return CodeStatus::bad_port;
            value = value * 10 + static_cast<unsigned>(p[i] - '0');
        }
        if (value < 1024 || value > 65535) return CodeStatus::bad_port;
        out.port = value;
    }
    std::memcpy(out.secret, body, link::kSecretChars);
    out.secret[link::kSecretChars] = 0;
    char check[3];
    JoinCodeCheck(out.secret, out.host, out.port, check);
    if (body[link::kSecretChars] != check[0] || body[link::kSecretChars + 1] != check[1]) return CodeStatus::bad_check;
    return CodeStatus::ok;
}

} // namespace mgscoop::test
