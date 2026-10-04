// Original MGS Co-op character receipt helpers (pure, portable).
#include "mgscoop/character_receipt.hpp"
#include <cstring>

namespace mgscoop::character {
namespace {
template <std::size_t N>
bool Exact(const char* source, std::size_t size, const char (&literal)[N]) noexcept {
    return size == N - 1 && std::memcmp(source, literal, N - 1) == 0;
}
template <std::size_t N>
bool Prefixed(const char* source, std::size_t size, const char (&prefix)[N]) noexcept {
    return size > N - 1 && size - (N - 1) <= max_payload && std::memcmp(source, prefix, N - 1) == 0;
}
bool PayloadChar(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '=' || c == ':' || c == ',' || c == '.' || c == ';' || c == '-';
}
// Appends text; false when the output (plus terminator) would not fit.
bool Append(char* out, std::size_t capacity, std::size_t& at, const char* text) noexcept {
    const std::size_t length = std::strlen(text);
    if (length >= capacity - at) return false;
    std::memcpy(out + at, text, length);
    at += length;
    out[at] = 0;
    return true;
}
} // namespace

Receipt Classify(const char* source, std::size_t size, const char* name) noexcept {
    if (!source || !name || size == 0 || size > sizeof(readback_prefix) - 1 + max_payload) return Receipt::none;
    // Byte-wise so a shorter name is never read past its terminator; receipts
    // never contain NUL, so a terminator in either string is a mismatch.
    for (std::size_t i = 0; i < size; ++i)
        if (source[i] == 0 || name[i] != source[i]) return Receipt::none;
    if (name[size] != 0) return Receipt::none;
    if (Exact(source, size, query_source)) return Receipt::query;
    if (Exact(source, size, bypass_on_source)) return Receipt::bypass_on;
    if (Exact(source, size, bypass_off_source)) return Receipt::bypass_off;
    if (Exact(source, size, players_source)) return Receipt::players;
    if (Prefixed(source, size, report_prefix)) return Receipt::report;
    if (Prefixed(source, size, readback_prefix)) return Receipt::readback;
    return Receipt::none;
}

std::size_t FormatQuery(const CoopConfig& coop, bool readback, char* out, std::size_t capacity) noexcept {
    if (!out || capacity == 0) return 0;
    out[0] = 0;
    std::size_t at = 0;
    const bool ok = Append(out, capacity, at, "return '") &&
                    Append(out, capacity, at, PlayerCharacterName(coop.character)) &&
                    Append(out, capacity, at, "','") &&
                    Append(out, capacity, at, PlayerCharacterName(coop.character_fallback)) &&
                    Append(out, capacity, at, readback ? "',true" : "',false");
    if (!ok) { out[0] = 0; return 0; }
    return at;
}

bool NeedsResetBypass(const CoopConfig& coop) noexcept {
    return coop.character == PlayerCharacter::ocelot || coop.character == PlayerCharacter::quiet;
}

bool FormatLogLine(Receipt kind, const char* source, std::size_t size, char* out, std::size_t capacity) noexcept {
    if (!out || capacity == 0) return false;
    out[0] = 0;
    if (!source || (kind != Receipt::report && kind != Receipt::readback)) return false;
    const std::size_t skip = kind == Receipt::report ? sizeof(report_prefix) - 1 : sizeof(readback_prefix) - 1;
    const char* prefix = kind == Receipt::report ? report_prefix : readback_prefix;
    if (size <= skip || size - skip > max_payload || std::memcmp(source, prefix, skip) != 0) return false;
    for (std::size_t i = skip; i < size; ++i) if (!PayloadChar(source[i])) return false;
    std::size_t at = 0;
    if (!Append(out, capacity, at, kind == Receipt::report ? "character=" : "character_readback=")) return false;
    for (std::size_t i = skip; i < size; ++i) {
        const char piece[3] = {source[i], source[i] == ';' ? ' ' : '\0', '\0'};
        if (!Append(out, capacity, at, piece)) { out[0] = 0; return false; }
    }
    if (!Append(out, capacity, at, "\r\n")) { out[0] = 0; return false; }
    return true;
}

std::size_t FormatPlayersReply(bool ready, const char* stage, std::uint32_t limit, const unsigned char* types,
                               std::size_t count, char* out, std::size_t capacity) noexcept {
    if (!out || capacity == 0) return 0;
    out[0] = 0;
    std::size_t at = 0;
    const auto number = [&](unsigned value) noexcept {
        char digits[12]{};
        std::size_t n = sizeof(digits) - 1;
        do { digits[--n] = static_cast<char>('0' + value % 10); value /= 10; } while (value && n);
        return Append(out, capacity, at, digits + n);
    };
    bool ok = true;
    if (ready && types && count >= 1 && count <= 2 && limit >= 1 && limit <= 4 && count <= limit) {
        ok = Append(out, capacity, at, "return true,") && number(limit);
        for (std::size_t i = 0; ok && i < count; ++i) ok = Append(out, capacity, at, ",") && number(types[i]);
    } else {
        const char* reason = ready ? "invalid_sample" : stage;
        std::size_t length = 0;
        while (reason && length <= 32 && reason[length] &&
               ((reason[length] >= 'a' && reason[length] <= 'z') || reason[length] == '_')) ++length;
        if (!reason || length == 0 || length > 32 || reason[length] != 0) reason = "unknown";
        ok = Append(out, capacity, at, "return false,'") && Append(out, capacity, at, reason) &&
             Append(out, capacity, at, "'");
    }
    if (!ok) { out[0] = 0; return 0; }
    return at;
}

} // namespace mgscoop::character
