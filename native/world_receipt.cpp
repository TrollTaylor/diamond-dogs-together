// Original MGS Co-op world sync receipt parsing and formatting
// (docs/world-sync-protocol.md "Receipts").
#include "mgscoop/world_receipt.hpp"

#include <cstdio>
#include <cstring>
#include "mgscoop/partner_receipt.hpp"

namespace mgscoop::world {
namespace {
template <std::size_t N>
bool Exact(const char* source, std::size_t size, const char (&literal)[N]) noexcept {
    return size == N - 1 && std::memcmp(source, literal, N - 1) == 0;
}
template <std::size_t N>
bool Prefixed(const char* source, std::size_t size, const char (&prefix)[N]) noexcept {
    return size > N - 1 && std::memcmp(source, prefix, N - 1) == 0;
}

// One integer token at p, ending at a space or the end: an optional '-', no
// '+', no leading zeros, no "-0", at most 10 digits. False: syntax.
bool Token(const char*& p, const char* end, std::int64_t& value) noexcept {
    bool negative = false;
    if (p < end && *p == '-') { negative = true; ++p; }
    const char* digits = p;
    std::int64_t v = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        if (p - digits >= 10) return false;
        v = v * 10 + (*p - '0');
        ++p;
    }
    const std::ptrdiff_t n = p - digits;
    if (n == 0) return false;
    if (n > 1 && digits[0] == '0') return false;
    if (negative && v == 0) return false;
    if (p < end && *p != ' ') return false;
    value = negative ? -v : v;
    return true;
}

bool Append(char* out, std::size_t capacity, std::size_t& at, const char* text, std::size_t n) noexcept {
    if (at + n + 1 > capacity) return false;
    std::memcpy(out + at, text, n);
    at += n;
    out[at] = 0;
    return true;
}

bool Blank(char c) noexcept { return c == ' ' || c == '\t'; }
bool SameText(const char* a, std::size_t n, const char* literal) noexcept {
    const std::size_t m = std::strlen(literal);
    return n == m && std::memcmp(a, literal, n) == 0;
}
} // namespace

Receipt Classify(const char* source, std::size_t size, const char* name) noexcept {
    if (!source || !name || size == 0 || size > max_receipt) return Receipt::none;
    for (std::size_t i = 0; i < size; ++i)
        if (source[i] == 0 || name[i] != source[i]) return Receipt::none;
    if (name[size] != 0) return Receipt::none;
    if (Prefixed(source, size, cfg_prefix)) return Receipt::cfg;
    if (Prefixed(source, size, tx_prefix)) return Receipt::tx;
    if (Prefixed(source, size, rx_prefix)) return Receipt::rx;
    if (Prefixed(source, size, log_prefix)) return Receipt::log;
    if (Prefixed(source, size, warp_prefix)) return Receipt::warp;
    if (Prefixed(source, size, note_prefix)) return Receipt::note;
    if (Prefixed(source, size, save_prefix)) return Receipt::save;
    return Receipt::none;
}

bool ParseCfg(const char* source, std::size_t size, std::uint32_t& mission, std::uint8_t& phase) noexcept {
    if (!source || !Prefixed(source, size, cfg_prefix) || size > max_receipt) return false;
    const char* p = source + sizeof(cfg_prefix) - 1;
    const char* end = source + size;
    std::int64_t m = 0, f = 0;
    if (!Token(p, end, m) || p >= end || *p != ' ') return false;
    ++p;
    if (!Token(p, end, f) || p != end) return false;
    if (m < 0 || m > 99999 || (f != 0 && f != 1)) return false;
    mission = static_cast<std::uint32_t>(m);
    phase = static_cast<std::uint8_t>(f);
    return true;
}

TxStatus ParseTx(const char* source, std::size_t size, std::uint8_t& code, std::int64_t (&values)[kMaxValues],
                 unsigned& count) noexcept {
    if (!source || !Prefixed(source, size, tx_prefix) || size > max_receipt) return TxStatus::syntax;
    const char* p = source + sizeof(tx_prefix) - 1;
    const char* end = source + size;
    std::int64_t tokens[kMaxValues + 2]{};
    unsigned n = 0;
    while (true) {
        std::int64_t v = 0;
        if (n >= kMaxValues + 2 || !Token(p, end, v)) return TxStatus::syntax;
        tokens[n++] = v;
        if (p == end) break;
        ++p;  // the single space
        if (p == end) return TxStatus::syntax;
    }
    if (tokens[0] < 1 || tokens[0] > 255) return TxStatus::unknown_op;
    const OpSpec* spec = FindOp(static_cast<std::uint8_t>(tokens[0]));
    if (!spec || !spec->lua || (spec->wire == Wire::local && spec->code != op::goto_ack)) return TxStatus::unknown_op;
    if (n - 1 != spec->lua) return TxStatus::bad_args;
    for (unsigned i = 0; i < spec->lua; ++i) {
        const std::int64_t v = tokens[i + 1];
        if (v < 0 && spec->fields[i].type != Type::i32) return TxStatus::bad_args;
        if (!FieldValid(spec->fields[i], v)) return TxStatus::bad_args;
    }
    code = spec->code;
    count = spec->lua;
    for (unsigned i = 0; i < kMaxValues; ++i) values[i] = i < count ? tokens[i + 1] : 0;
    return TxStatus::ok;
}

bool ParseRx(const char* source, std::size_t size, std::uint8_t& mask) noexcept {
    if (!source || size > max_receipt) return false;
    if (Exact(source, size, "return 'MGS_COOP_WORLD_RX_V1' -- 1")) { mask = 1; return true; }
    if (Exact(source, size, "return 'MGS_COOP_WORLD_RX_V1' -- 3")) { mask = 3; return true; }
    return false;
}

bool ParseWarp(const char* source, std::size_t size, std::uint16_t& seq, std::uint32_t& go_id) noexcept {
    if (!source || !Prefixed(source, size, warp_prefix) || size > max_receipt) return false;
    const char* p = source + sizeof(warp_prefix) - 1;
    const char* end = source + size;
    std::int64_t s = 0, g = 0;
    if (!Token(p, end, s) || p >= end || *p != ' ') return false;
    ++p;
    if (!Token(p, end, g) || p != end) return false;
    if (s < 1 || s > 65535 || g < 0 || g > 65535) return false;
    seq = static_cast<std::uint16_t>(s);
    go_id = static_cast<std::uint32_t>(g);
    return true;
}

bool ParseNote(const char* source, std::size_t size, std::uint8_t& code, std::uint32_t& a, std::uint32_t& b) noexcept {
    if (!source || !Prefixed(source, size, note_prefix) || size > max_receipt) return false;
    const char* p = source + sizeof(note_prefix) - 1;
    const char* end = source + size;
    std::int64_t c = 0, x = 0, y = 0;
    if (!Token(p, end, c) || p >= end || *p != ' ') return false;
    ++p;
    if (!Token(p, end, x) || p >= end || *p != ' ') return false;
    ++p;
    if (!Token(p, end, y) || p != end) return false;
    if (c < 1 || c > 15 || x < 0 || x > 4294967295LL || y < 0 || y > 4294967295LL) return false;
    code = static_cast<std::uint8_t>(c);
    a = static_cast<std::uint32_t>(x);
    b = static_cast<std::uint32_t>(y);
    return true;
}

bool ParseSave(const char* source, std::size_t size, std::uint8_t& op, std::uint32_t& a, std::uint32_t& b, std::uint32_t& c,
               std::uint32_t& d, std::uint32_t& e) noexcept {
    if (!source || !Prefixed(source, size, save_prefix) || size > max_receipt) return false;
    const char* p = source + sizeof(save_prefix) - 1;
    const char* end = source + size;
    std::int64_t v[6]{};
    for (unsigned i = 0; i < 6; ++i) {
        if (i) {
            if (p >= end || *p != ' ') return false;
            ++p;
        }
        if (!Token(p, end, v[i]) || v[i] < 0) return false;
    }
    if (p != end) return false;
    // P2-2 ranges per op.
    switch (v[0]) {
    case 1:  // REPORT mission title story saving ingame
        if (v[1] > 99999 || v[2] > 1 || v[3] > 2 || v[4] > 1 || v[5] > 1) return false;
        break;
    case 2:  // CONTINUE target title story 0 0
        if (v[1] < 1 || v[1] > 99999 || v[2] > 1 || v[3] > 2 || v[4] != 0 || v[5] != 0) return false;
        break;
    case 3:  // DECLINE reason 0 0 0 0
        if (v[1] < 1 || v[1] > 2 || v[2] != 0 || v[3] != 0 || v[4] != 0 || v[5] != 0) return false;
        break;
    default:
        return false;
    }
    op = static_cast<std::uint8_t>(v[0]);
    a = static_cast<std::uint32_t>(v[1]);
    b = static_cast<std::uint32_t>(v[2]);
    c = static_cast<std::uint32_t>(v[3]);
    d = static_cast<std::uint32_t>(v[4]);
    e = static_cast<std::uint32_t>(v[5]);
    return true;
}

std::size_t FormatCfgReply(const CfgReply& r, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    const int n = std::snprintf(out, capacity, "return %u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u", unsigned(r.level), unsigned(r.role),
                                unsigned(r.proto), unsigned(r.link), unsigned(r.epoch), unsigned(r.probe), unsigned(r.mode),
                                unsigned(r.session), unsigned(r.remote_mission), unsigned(r.remote_level), unsigned(r.wait),
                                unsigned(r.handoff), unsigned(r.lock));
    if (n < 0 || static_cast<std::size_t>(n) >= capacity) { out[0] = 0; return 0; }
    return static_cast<std::size_t>(n);
}

std::size_t FormatTxReply(TxStatus status, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    const int n = status == TxStatus::ok ? std::snprintf(out, capacity, "return 1")
                                         : std::snprintf(out, capacity, "return 0,%u", unsigned(status));
    if (n < 0 || static_cast<std::size_t>(n) >= capacity) { out[0] = 0; return 0; }
    return static_cast<std::size_t>(n);
}

std::size_t FormatRxReply(const RxReply& r, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    out[0] = 0;
    std::size_t at = 0;
    char number[24];
    const auto put = [&](std::int64_t v, bool first) {
        const int n = std::snprintf(number, sizeof(number), first ? "%lld" : ",%lld", static_cast<long long>(v));
        return n > 0 && Append(out, capacity, at, number, static_cast<std::size_t>(n));
    };
    bool ok = Append(out, capacity, at, "return ", 7);
    if (r.empty) {
        ok = ok && put(0, true) && put(0, false) && put(r.flags, false) && put(r.qctrl, false) && put(r.qworld, false) &&
             put(r.epoch, false);
    } else {
        ok = ok && put(r.queue, true) && put(r.op, false);
        for (unsigned i = 0; ok && i < r.count && i < kMaxValues; ++i) ok = put(r.v[i], false);
    }
    if (!ok) { out[0] = 0; return 0; }
    return at;
}

std::size_t FormatLogLine(const char* source, std::size_t size, std::uint64_t now_ms, char* out, std::size_t capacity) noexcept {
    if (!out || capacity == 0) return 0;
    out[0] = 0;
    constexpr std::size_t skip = sizeof(log_prefix) - 1;
    if (!source || size <= skip || size > max_receipt || std::memcmp(source, log_prefix, skip) != 0) return 0;
    char clean[max_log_payload + 1]{};
    const std::size_t length = partner::SanitizeLogPayload(source + skip, size - skip, clean, sizeof(clean));
    if (!length) return 0;
    std::size_t at = 0;
    bool ok = Append(out, capacity, at, "world_lua=", 10);
    for (std::size_t i = 0; ok && i < length; ++i)
        ok = clean[i] == ';' ? Append(out, capacity, at, "; ", 2) : Append(out, capacity, at, clean + i, 1);
    char tail[40];
    const int n = std::snprintf(tail, sizeof(tail), "; t_ms=%llu\r\n", static_cast<unsigned long long>(now_ms));
    if (ok) ok = n > 0 && Append(out, capacity, at, tail, static_cast<std::size_t>(n));
    if (!ok) { out[0] = 0; return 0; }
    return at;
}

bool ParseTrigger(const char* text, std::size_t size, Trigger& out, char* canonical, std::size_t canonical_capacity,
                  const char*& reason) noexcept {
    reason = "empty";
    if (!text || size == 0) return false;
    if (size > max_trigger) { reason = "too_long"; return false; }
    std::size_t i = 0;
    if (size >= 3 && static_cast<unsigned char>(text[0]) == 0xef && static_cast<unsigned char>(text[1]) == 0xbb &&
        static_cast<unsigned char>(text[2]) == 0xbf)
        i = 3;
    while (size > i && (text[size - 1] == '\n' || text[size - 1] == '\r' || Blank(text[size - 1]))) --size;
    Trigger result{};
    unsigned tokens = 0;
    reason = "token";
    while (i < size) {
        while (i < size && Blank(text[i])) ++i;
        if (i >= size) break;
        const std::size_t start = i;
        while (i < size && !Blank(text[i])) ++i;
        const char* token = text + start;
        const std::size_t length = i - start;
        std::size_t eq = 0;
        while (eq < length && token[eq] != '=') ++eq;
        if (eq == length) return false;
        const char* key = token;
        const char* value = token + eq + 1;
        const std::size_t vlen = length - eq - 1;
        for (std::size_t k = 0; k < length; ++k)
            if (static_cast<unsigned char>(token[k]) < 0x21 || static_cast<unsigned char>(token[k]) > 0x7e) return false;
        if (SameText(key, eq, "world")) {
            if (result.has_world) { reason = "repeat"; return false; }
            if (vlen != 1 || value[0] < '0' || value[0] > '3') return false;
            result.has_world = true;
            result.world = static_cast<std::uint8_t>(value[0] - '0');
        } else if (SameText(key, eq, "world_probe")) {
            if (result.has_probe) { reason = "repeat"; return false; }
            WorldProbe probe = WorldProbe::none;
            if (SameText(value, vlen, "off")) probe = WorldProbe::none;
            else if (SameText(value, vlen, "none") || !ParseWorldProbe(value, vlen, probe)) return false;
            result.has_probe = true;
            result.probe = probe;
        } else if (SameText(key, eq, "goto")) {
            if (result.has_goto) { reason = "repeat"; return false; }
            if (SameText(value, vlen, "30010")) result.goto_target = 30010;
            else if (SameText(value, vlen, "30020")) result.goto_target = 30020;
            else return false;
            result.has_goto = true;
        } else if (SameText(key, eq, "goto_lz")) {
            if (result.has_lz) { reason = "repeat"; return false; }
            if (vlen != 1 || (value[0] != '0' && value[0] != '1')) return false;
            result.has_lz = true;
            result.lz = static_cast<std::uint8_t>(value[0] - '0');
        } else {
            return false;
        }
        ++tokens;
    }
    if (!tokens) { reason = "empty"; return false; }
    if (result.has_lz && !result.has_goto) return false;
    if (canonical && canonical_capacity) {
        canonical[0] = 0;
        std::size_t at = 0;
        char part[48];
        bool ok = true;
        const auto add = [&](const char* piece) {
            const std::size_t n = std::strlen(piece);
            if (at && ok) ok = Append(canonical, canonical_capacity, at, " ", 1);
            if (ok) ok = Append(canonical, canonical_capacity, at, piece, n);
        };
        if (result.has_world) { std::snprintf(part, sizeof(part), "world=%u", unsigned(result.world)); add(part); }
        if (result.has_probe) {
            std::snprintf(part, sizeof(part), "world_probe=%s", result.probe == WorldProbe::none ? "off" : WorldProbeName(result.probe));
            add(part);
        }
        if (result.has_goto) { std::snprintf(part, sizeof(part), "goto=%u", result.goto_target); add(part); }
        if (result.has_lz) { std::snprintf(part, sizeof(part), "goto_lz=%u", unsigned(result.lz)); add(part); }
        if (!ok) canonical[0] = 0;
    }
    out = result;
    reason = nullptr;
    return true;
}

} // namespace mgscoop::world
