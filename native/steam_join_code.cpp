// Original MGS Co-op Steam join code (MGS2) and invite-string parsers.
#include "mgscoop/steam_join_code.hpp"

#include "mgscoop/steam_abi.hpp"

namespace mgscoop::steam_code {
namespace {
bool IsBlank(char c) noexcept { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }
char Upper(char c) noexcept { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; }
std::size_t Length(const char* s) noexcept {
    std::size_t n = 0;
    while (s[n]) ++n;
    return n;
}
// Decimal digits of `value` (most significant first); returns the count.
std::size_t Decimal(std::uint64_t value, char (&out)[21]) noexcept {
    char reversed[21]{};
    std::size_t n = 0;
    do {
        reversed[n++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value && n < 20);
    for (std::size_t i = 0; i < n; ++i) out[i] = reversed[n - 1 - i];
    out[n] = 0;
    return n;
}
void Zero(Code& code) noexcept { link::SecureZero(&code, sizeof(code)); }
}  // namespace

const char* StatusName(Status status) noexcept {
    switch (status) {
    case Status::ok: return "ok";
    case Status::empty: return "empty";
    case Status::bad_prefix: return "bad_prefix";
    case Status::bad_char: return "bad_char";
    case Status::bad_length: return "bad_length";
    case Status::bad_check: return "bad_check";
    default: return "bad_steamid";
    }
}

void Check(const char (&secret)[link::kSecretChars], std::uint64_t host, char (&out)[3]) noexcept {
    link::Sha256 ctx{};
    link::Sha256Init(ctx);
    link::Sha256Update(ctx, "MGS2|", 5);
    link::Sha256Update(ctx, secret, link::kSecretChars);
    link::Sha256Update(ctx, "|", 1);
    char digits[21]{};
    const std::size_t n = Decimal(host, digits);
    link::Sha256Update(ctx, digits, n);
    std::uint8_t d[link::kSha256Size]{};
    link::Sha256Final(ctx, d);
    out[0] = link::kCrockford[d[0] >> 3];
    out[1] = link::kCrockford[((d[0] & 7) << 2) | (d[1] >> 6)];
    out[2] = 0;
}

bool Format(const char (&secret)[link::kSecretChars], std::uint64_t host, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    out[0] = 0;
    if (capacity < kCodeChars + 1 || !steam::is_individual_steamid(host)) return false;
    for (char c : secret)
        if (!link::IsCanonicalSecretChar(c)) return false;
    char check[3]{};
    Check(secret, host, check);
    std::size_t at = 0;
    for (char c : kPrefix) if (c) out[at++] = c;
    for (std::size_t i = 0; i < link::kSecretChars; ++i) {
        if (i % 5 == 0) out[at++] = '-';
        out[at++] = secret[i];
    }
    out[at++] = '-';
    out[at++] = check[0];
    out[at++] = check[1];
    out[at++] = '@';
    char digits[21]{};
    const std::size_t n = Decimal(host, digits);
    for (std::size_t i = 0; i < n; ++i) out[at++] = digits[i];
    out[at] = 0;
    return at == kCodeChars;
}

bool FormatInvite(const char (&secret)[link::kSecretChars], std::uint64_t host, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    out[0] = 0;
    if (capacity < kInvitePrefixSize + kCodeChars + 1) return false;
    for (std::size_t i = 0; i < kInvitePrefixSize; ++i) out[i] = kInvitePrefix[i];
    if (!Format(secret, host, out + kInvitePrefixSize, capacity - kInvitePrefixSize)) {
        link::SecureZero(out, capacity);
        return false;
    }
    return true;
}

bool ParseId64(const char* text, std::size_t size, std::uint64_t& out) noexcept {
    out = 0;
    if (!text || size != 17 || text[0] == '0') return false;
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < size; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        value = value * 10 + static_cast<std::uint64_t>(text[i] - '0');
    }
    if (!steam::is_individual_steamid(value)) return false;
    out = value;
    return true;
}

Status Parse(const char* text, std::size_t size, Code& out) noexcept {
    Zero(out);
    char compact[512]{};
    std::size_t n = 0;
    for (std::size_t i = 0; text && i < size && text[i]; ++i) {
        if (IsBlank(text[i])) continue;
        if (n + 1 >= sizeof(compact)) {
            link::SecureZero(compact, sizeof(compact));
            return Status::bad_length;
        }
        compact[n++] = text[i];
    }
    if (!n) return Status::empty;
    std::size_t at = n;
    for (std::size_t i = n; i-- > 0;)
        if (compact[i] == '@') { at = i; break; }
    Status status = Status::ok;
    char body[512]{};
    std::size_t k = 0;
    if (at == n) {
        status = Status::bad_steamid;
    } else {
        // Left: upper case, '-' removed, the MGS2 prefix, then Crockford with O->0, I/L->1.
        char left[512]{};
        std::size_t m = 0;
        for (std::size_t i = 0; i < at; ++i)
            if (compact[i] != '-') left[m++] = Upper(compact[i]);
        if (m < 4 || left[0] != 'M' || left[1] != 'G' || left[2] != 'S' || left[3] != '2') status = Status::bad_prefix;
        for (std::size_t i = 4; status == Status::ok && i < m; ++i) {
            char c = left[i];
            if (c == 'O') c = '0';
            else if (c == 'I' || c == 'L') c = '1';
            if (!link::IsCanonicalSecretChar(c)) status = Status::bad_char;
            else body[k++] = c;
        }
        link::SecureZero(left, sizeof(left));
        if (status == Status::ok && k != link::kSecretChars + 2) status = Status::bad_length;
    }
    std::uint64_t host = 0;
    if (status == Status::ok && !ParseId64(compact + at + 1, n - at - 1, host)) status = Status::bad_steamid;
    if (status == Status::ok) {
        char secret[link::kSecretChars]{};
        for (std::size_t i = 0; i < link::kSecretChars; ++i) secret[i] = body[i];
        char check[3]{};
        Check(secret, host, check);
        if (body[link::kSecretChars] != check[0] || body[link::kSecretChars + 1] != check[1]) {
            status = Status::bad_check;
        } else {
            for (std::size_t i = 0; i < link::kSecretChars; ++i) out.secret[i] = secret[i];
            out.secret[link::kSecretChars] = 0;
            out.host = host;
        }
        link::SecureZero(secret, sizeof(secret));
    }
    link::SecureZero(compact, sizeof(compact));
    link::SecureZero(body, sizeof(body));
    return status;
}

const char* JoinStatusName(JoinStatus status) noexcept {
    switch (status) {
    case JoinStatus::ok: return "ok";
    case JoinStatus::not_terminated: return "not_terminated";
    case JoinStatus::not_printable: return "not_printable";
    case JoinStatus::not_mgscoop: return "not_mgscoop";
    case JoinStatus::extra_text: return "extra_text";
    case JoinStatus::code: return "code";
    default: return "host_mismatch";
    }
}

const char* JoinReason(const JoinResult& result) noexcept {
    return result.status == JoinStatus::code ? StatusName(result.code) : JoinStatusName(result.status);
}

JoinResult ParseJoinRequest(std::uint64_t friend_id, const char* connect, std::size_t capacity, Code& out) noexcept {
    Zero(out);
    JoinResult result{JoinStatus::not_terminated, Status::empty};
    if (!connect) return result;
    std::size_t length = 0;
    while (length < capacity && length < 256 && connect[length]) ++length;
    if (length >= capacity || length >= 256) return result;
    for (std::size_t i = 0; i < length; ++i) {
        const auto c = static_cast<unsigned char>(connect[i]);
        if (c < 0x20 || c > 0x7e) {
            result.status = JoinStatus::not_printable;
            return result;
        }
    }
    if (length < kInvitePrefixSize) {
        result.status = JoinStatus::not_mgscoop;
        return result;
    }
    for (std::size_t i = 0; i < kInvitePrefixSize; ++i)
        if (connect[i] != kInvitePrefix[i]) {
            result.status = JoinStatus::not_mgscoop;
            return result;
        }
    const char* token = connect + kInvitePrefixSize;
    const std::size_t token_size = length - kInvitePrefixSize;
    for (std::size_t i = 0; i < token_size; ++i)
        if (token[i] == ' ') {
            result.status = JoinStatus::extra_text;
            return result;
        }
    result.code = Parse(token, token_size, out);
    if (result.code != Status::ok) {
        result.status = JoinStatus::code;
        return result;
    }
    if (out.host != friend_id) {
        Zero(out);
        result.status = JoinStatus::host_mismatch;
        return result;
    }
    result.status = JoinStatus::ok;
    return result;
}

const char* CommandLineStatusName(CommandLineStatus status) noexcept {
    switch (status) {
    case CommandLineStatus::ok: return "ok";
    case CommandLineStatus::none: return "none";
    case CommandLineStatus::multiple: return "multiple";
    case CommandLineStatus::missing_code: return "missing_code";
    default: return "bad_code";
    }
}

CommandLineStatus ExtractCommandLineJoin(const wchar_t* command_line, Code& out, Status& code_status) noexcept {
    Zero(out);
    code_status = Status::empty;
    if (!command_line) return CommandLineStatus::none;
    static constexpr wchar_t kKey[] = L"+mgscoop_join";
    constexpr std::size_t kKeySize = sizeof(kKey) / sizeof(kKey[0]) - 1;
    const auto blank = [](wchar_t c) noexcept { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; };
    const wchar_t* code_start = nullptr;
    std::size_t code_size = 0;
    unsigned keys = 0;
    bool expect_code = false, missing = false;
    for (const wchar_t* p = command_line; *p;) {
        while (*p && blank(*p)) ++p;
        if (!*p) break;
        const wchar_t* start = p;
        while (*p && !blank(*p)) ++p;
        const auto size = static_cast<std::size_t>(p - start);
        if (expect_code) {
            expect_code = false;
            code_start = start;
            code_size = size;
            continue;
        }
        bool key = size == kKeySize;
        for (std::size_t i = 0; key && i < kKeySize; ++i) key = start[i] == kKey[i];
        if (key) {
            ++keys;
            expect_code = true;
        }
    }
    if (expect_code) missing = true;
    if (!keys) return CommandLineStatus::none;
    if (keys > 1) return CommandLineStatus::multiple;
    if (missing || !code_start) return CommandLineStatus::missing_code;
    char narrow[kCodeBuffer]{};
    if (code_size >= sizeof(narrow)) {
        code_status = Status::bad_length;
        return CommandLineStatus::bad_code;
    }
    for (std::size_t i = 0; i < code_size; ++i) {
        const wchar_t c = code_start[i];
        narrow[i] = (c > 0x20 && c < 0x7f) ? static_cast<char>(c) : '\x01';  // non-ASCII -> bad_char
    }
    code_status = Parse(narrow, code_size, out);
    link::SecureZero(narrow, sizeof(narrow));
    return code_status == Status::ok ? CommandLineStatus::ok : CommandLineStatus::bad_code;
}

void FormatPeer(std::uint64_t id, char (&out)[24]) noexcept {
    for (char& c : out) c = 0;
    const char* prefix = id ? "steam:..." : "steam:none";
    std::size_t at = 0;
    for (std::size_t i = 0; prefix[i]; ++i) out[at++] = prefix[i];
    if (!id) return;
    char digits[21]{};
    const std::size_t n = Decimal(id, digits);
    const std::size_t from = n > 4 ? n - 4 : 0;
    for (std::size_t i = from; i < n; ++i) out[at++] = digits[i];
}

bool FormatId64(std::uint64_t id, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    char digits[21]{};
    const std::size_t n = Decimal(id, digits);
    if (n + 1 > capacity) {
        out[0] = 0;
        return false;
    }
    for (std::size_t i = 0; i <= n; ++i) out[i] = digits[i];
    return Length(out) == n;
}

}  // namespace mgscoop::steam_code
