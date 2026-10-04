// Master-server gate URL redirect for mgsvtpp.exe 1.0.15.4, in memory only.
// Original implementation. Dynamite (unknown321, c90f252, GPLv3 + Commons
// Clause) overwrote the URL string in place on 1.0.15.3 and tpp-mod retargets
// the lea; this file uses its own static contract (master_server_contract.hpp)
// and copies no code from either.
#include "mgscoop/master_server_patch.hpp"
#include "mgscoop/near_code.hpp"
#include "master_server_contract.hpp"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <strsafe.h>
#endif

namespace mgscoop::master_server {
namespace {
constexpr char kSuffix[] = "/tppstm/gate";
constexpr std::size_t kSuffixLength = sizeof(kSuffix) - 1;
constexpr std::size_t kMaxContext = 64;
constexpr std::size_t kMaxOriginal = 127;
constexpr std::size_t kMaxPadding = 16;

bool UrlChar(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return u > 0x20 && u < 0x7f && c != '"' && c != '\\';
}

bool HasPrefix(const char* text, std::size_t length, const char* prefix, std::size_t prefix_length) noexcept {
    if (length < prefix_length) return false;
    for (std::size_t i = 0; i < prefix_length; ++i) if (text[i] != prefix[i]) return false;
    return true;
}

constexpr Site kRetail{
    master_server_contract::context_rva,
    master_server_contract::context_bytes,
    sizeof(master_server_contract::context_bytes),
    master_server_contract::lea_rva,
    master_server_contract::lea_size,
    master_server_contract::disp_offset,
    master_server_contract::string_rva,
    master_server_contract::original_url,
    master_server_contract::original_length,
    master_server_contract::padding_after_terminator,
};
static_assert(sizeof(master_server_contract::original_url) == master_server_contract::original_length + 1);
static_assert(master_server_contract::string_code_references == 1,
              "in-place redirect requires every code reference to be accounted for");
static_assert(master_server_contract::lea_rva >= master_server_contract::context_rva &&
              master_server_contract::lea_rva + master_server_contract::lea_size <=
                  master_server_contract::context_rva + sizeof(master_server_contract::context_bytes));

bool SiteIsCoherent(const Site& site) noexcept {
    return site.context && site.original && site.context_size && site.context_size <= kMaxContext &&
           site.lea_size >= site.disp_offset + 4 && site.lea_rva >= site.context_rva &&
           site.lea_rva - site.context_rva + site.lea_size <= site.context_size &&
           site.original_length && site.original_length <= kMaxOriginal &&
           site.padding_after_terminator <= kMaxPadding;
}

bool InRange(std::size_t span, std::uintptr_t rva, std::size_t size) noexcept {
    return rva <= span && size <= span - rva;
}
} // namespace

UrlStatus ValidateUrl(const char* url, std::size_t& length) noexcept {
    length = 0;
    if (!url || !*url) return UrlStatus::empty;
    std::size_t n = 0;
    while (url[n]) {
        if (n == kMaxUrlLength) return UrlStatus::too_long;
        ++n;
    }
    for (std::size_t i = 0; i < n; ++i) if (!UrlChar(url[i])) return UrlStatus::bad_character;
    std::size_t scheme = 0;
    if (HasPrefix(url, n, "http://", 7)) scheme = 7;
    else if (HasPrefix(url, n, "https://", 8)) scheme = 8;
    else return UrlStatus::bad_scheme;
    if (n < kSuffixLength || !HasPrefix(url + n - kSuffixLength, kSuffixLength, kSuffix, kSuffixLength))
        return UrlStatus::bad_suffix;
    // At least one host character between the scheme and the suffix.
    if (n - kSuffixLength <= scheme || url[scheme] == '/') return UrlStatus::missing_host;
    length = n;
    return UrlStatus::ok;
}

bool IsLoopbackUrl(const char* url) noexcept {
    std::size_t length = 0;
    if (ValidateUrl(url, length) != UrlStatus::ok) return false;
    const std::size_t scheme = url[4] == 's' ? 8 : 7;
    // The authority ends at the first '/' after the scheme (the suffix guarantees one).
    std::size_t end = scheme;
    while (end < length && url[end] != '/') ++end;
    const char* authority = url + scheme;
    const std::size_t authority_length = end - scheme;
    for (std::size_t i = 0; i < authority_length; ++i)
        if (authority[i] == '@') return false;  // userinfo can disguise the real host
    std::size_t host_length = 0;
    if (authority[0] == '[') {
        while (host_length < authority_length && authority[host_length] != ']') ++host_length;
        if (host_length == authority_length) return false;
        ++host_length;  // include ']'
    } else {
        while (host_length < authority_length && authority[host_length] != ':') ++host_length;
    }
    if (host_length < authority_length) {
        // Optional port: ':' then 1..5 digits with a value of 1..65535.
        const std::size_t digits = authority_length - host_length - 1;
        if (authority[host_length] != ':' || digits == 0 || digits > 5) return false;
        unsigned long port = 0;
        for (std::size_t i = host_length + 1; i < authority_length; ++i) {
            if (authority[i] < '0' || authority[i] > '9') return false;
            port = port * 10 + static_cast<unsigned long>(authority[i] - '0');
        }
        if (port == 0 || port > 65535) return false;
    }
    auto equals = [&](const char* expected, bool fold_case) noexcept {
        std::size_t i = 0;
        for (; i < host_length && expected[i]; ++i) {
            char c = authority[i];
            if (fold_case && c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (c != expected[i]) return false;
        }
        return i == host_length && !expected[i];
    };
    return equals("127.0.0.1", false) || equals("localhost", true) || equals("[::1]", false);
}

const Site& RetailSite() noexcept { return kRetail; }

Plan PlanRedirect(const Site& site, std::size_t span, ReadImage read, void* context, const char* url) noexcept {
    Plan plan{};
    if (!url || !*url) return plan;  // nothing to do
    plan.url_status = ValidateUrl(url, plan.url_length);
    if (plan.url_status != UrlStatus::ok) { plan.refusal = Refusal::invalid_url; return plan; }
    if (!read || !SiteIsCoherent(site)) { plan.refusal = Refusal::bad_site; return plan; }
    const std::size_t string_window = site.original_length + 1 + site.padding_after_terminator;
    if (!InRange(span, site.context_rva, site.context_size) || !InRange(span, site.string_rva, string_window)) {
        plan.refusal = Refusal::image_span; return plan;
    }
    unsigned char code[kMaxContext]{};
    if (!read(context, site.context_rva, code, site.context_size)) { plan.refusal = Refusal::unreadable; return plan; }
    for (std::size_t i = 0; i < site.context_size; ++i)
        if (code[i] != site.context[i]) { plan.refusal = Refusal::instruction_bytes; return plan; }
    const std::size_t disp_at = site.lea_rva - site.context_rva + site.disp_offset;
    const std::uint32_t raw = static_cast<std::uint32_t>(code[disp_at]) |
                              (static_cast<std::uint32_t>(code[disp_at + 1]) << 8) |
                              (static_cast<std::uint32_t>(code[disp_at + 2]) << 16) |
                              (static_cast<std::uint32_t>(code[disp_at + 3]) << 24);
    const auto disp = static_cast<std::int32_t>(raw);
    const std::uint64_t next_ip = static_cast<std::uint64_t>(site.lea_rva) + site.lea_size;
    if (static_cast<std::int64_t>(next_ip) + disp != static_cast<std::int64_t>(site.string_rva)) {
        plan.refusal = Refusal::lea_target; return plan;
    }
    unsigned char text[kMaxOriginal + 1 + kMaxPadding]{};
    if (!read(context, site.string_rva, text, string_window)) { plan.refusal = Refusal::unreadable; return plan; }
    for (std::size_t i = 0; i < site.original_length; ++i)
        if (text[i] != static_cast<unsigned char>(site.original[i])) { plan.refusal = Refusal::string_bytes; return plan; }
    if (text[site.original_length] != 0) { plan.refusal = Refusal::string_bytes; return plan; }
    for (std::size_t i = site.original_length + 1; i < string_window; ++i)
        if (text[i] != 0) { plan.refusal = Refusal::padding_bytes; return plan; }
    plan.disp_rva = site.lea_rva + site.disp_offset;
    plan.next_ip_rva = static_cast<std::uintptr_t>(next_ip);
    plan.original_disp = disp;
    // In place only when the new string fits in the original bytes; the single
    // code reference is fixed by the contract (string_code_references == 1).
    plan.method = plan.url_length <= site.original_length ? Method::in_place : Method::lea_retarget;
    return plan;
}

bool BuildInPlaceBytes(const Site& site, const char* url, std::size_t url_length,
                       unsigned char* out, std::size_t out_size) noexcept {
    if (!url || !out || url_length > site.original_length || out_size < site.original_length + 1) return false;
    for (std::size_t i = 0; i < url_length; ++i) {
        if (!url[i]) return false;
        out[i] = static_cast<unsigned char>(url[i]);
    }
    for (std::size_t i = url_length; i <= site.original_length; ++i) out[i] = 0;
    return true;
}

bool Rel32(std::uint64_t next_ip, std::uint64_t target, std::int32_t& out) noexcept {
    return near_code::Rel32(next_ip, target, out);
}

bool NearWindow(std::uint64_t next_ip, std::uint64_t size, std::uint64_t granularity,
                std::uint64_t min_address, std::uint64_t max_address,
                std::uint64_t& low, std::uint64_t& high) noexcept {
    return near_code::NearWindow(next_ip, size, granularity, min_address, max_address, low, high);
}

const char* MethodName(Method value) noexcept {
    switch (value) {
    case Method::in_place: return "in_place";
    case Method::lea_retarget: return "lea_retarget";
    default: return "none";
    }
}

const char* RefusalName(Refusal value) noexcept {
    switch (value) {
    case Refusal::invalid_url: return "invalid_url";
    case Refusal::bad_site: return "bad_site";
    case Refusal::image_span: return "image_span";
    case Refusal::unreadable: return "unreadable";
    case Refusal::instruction_bytes: return "instruction_bytes";
    case Refusal::lea_target: return "lea_target";
    case Refusal::string_bytes: return "string_bytes";
    case Refusal::padding_bytes: return "padding_bytes";
    default: return "none";
    }
}

const char* UrlStatusName(UrlStatus value) noexcept {
    switch (value) {
    case UrlStatus::ok: return "ok";
    case UrlStatus::empty: return "empty";
    case UrlStatus::too_long: return "too_long";
    case UrlStatus::bad_scheme: return "bad_scheme";
    case UrlStatus::bad_suffix: return "bad_suffix";
    case UrlStatus::bad_character: return "bad_character";
    case UrlStatus::missing_host: return "missing_host";
    default: return "unknown";
    }
}

#ifdef _WIN32
namespace {
SRWLOCK lock = SRWLOCK_INIT;
void* installed_base = nullptr;
char installed_url[kMaxUrlLength + 1] = {};
// 1 once a verified redirect to a loopback URL is in place (set under lock).
volatile LONG loopback_redirect = 0;

bool ReadLive(void* context, std::uintptr_t rva, void* out, std::size_t size) noexcept {
    SIZE_T read = 0;
    return ReadProcessMemory(GetCurrentProcess(), static_cast<unsigned char*>(context) + rva, out, size, &read) &&
           read == size;
}

bool ReadAbsolute(std::uintptr_t address, void* out, std::size_t size) noexcept {
    SIZE_T read = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), out, size, &read) &&
           read == size;
}

bool SameText(const char* a, const char* b) noexcept {
    std::size_t i = 0;
    for (; a[i] && b[i]; ++i) if (a[i] != b[i]) return false;
    return a[i] == b[i];
}

void Emit(void (*log)(const char*) noexcept, const char* format, const char* a, const char* b) noexcept {
    if (!log) return;
    char line[256]{};
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), format, a, b))) log(line);
}

DWORD WritableFor(DWORD protection) noexcept { return near_code::WritableFor(protection); }

// The string the lea currently addresses equals url (with terminator).
bool VerifyEffective(unsigned char* image, const Plan& plan, const char* url, std::size_t length) noexcept {
    std::int32_t disp = 0;
    if (!ReadAbsolute(reinterpret_cast<std::uintptr_t>(image) + plan.disp_rva, &disp, sizeof(disp))) return false;
    const std::uintptr_t target = reinterpret_cast<std::uintptr_t>(image) + plan.next_ip_rva +
                                  static_cast<std::uintptr_t>(static_cast<std::intptr_t>(disp));
    char text[kMaxUrlLength + 1]{};
    if (!ReadAbsolute(target, text, length + 1)) return false;
    for (std::size_t i = 0; i < length; ++i) if (text[i] != url[i]) return false;
    return text[length] == 0;
}

bool WriteInPlace(unsigned char* image, const Site& site, const char* url, std::size_t length) noexcept {
    unsigned char bytes[kMaxOriginal + 1]{};
    if (!BuildInPlaceBytes(site, url, length, bytes, sizeof(bytes))) return false;
    auto* target = image + site.string_rva;
    const std::size_t count = site.original_length + 1;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(target, &info, sizeof(info)) || info.State != MEM_COMMIT) return false;
    DWORD previous = 0, ignored = 0;
    if (!VirtualProtect(target, count, WritableFor(info.Protect), &previous)) return false;
    // Tail first, so a racing reader never sees the new host glued to the old path.
    volatile unsigned char* out = target;
    for (std::size_t i = count; i-- > 0;) out[i] = bytes[i];
    const bool restored = VirtualProtect(target, count, previous, &ignored) != 0;
    return restored;
}

void* AllocateNear(std::uintptr_t next_ip, std::size_t size) noexcept { return near_code::AllocateNear(next_ip, size); }

// Returns nullptr on success, else a failure reason.
const char* Retarget(unsigned char* image, const Plan& plan, const char* url, std::size_t length) noexcept {
    const std::uintptr_t next_ip = reinterpret_cast<std::uintptr_t>(image) + plan.next_ip_rva;
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const std::size_t page = system.dwPageSize ? system.dwPageSize : 4096u;
    void* buffer = AllocateNear(next_ip, page);
    if (!buffer) return "allocate_near";
    auto* text = static_cast<char*>(buffer);
    for (std::size_t i = 0; i < length; ++i) text[i] = url[i];
    text[length] = 0;
    DWORD previous = 0, ignored = 0;
    std::int32_t disp = 0;
    if (!VirtualProtect(buffer, page, PAGE_READONLY, &previous) ||
        !Rel32(next_ip, reinterpret_cast<std::uintptr_t>(buffer), disp)) {
        VirtualFree(buffer, 0, MEM_RELEASE);
        return "buffer_protect_or_range";
    }
    auto* field = image + plan.disp_rva;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(field, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        !VirtualProtect(field, sizeof(disp), WritableFor(info.Protect), &previous)) {
        VirtualFree(buffer, 0, MEM_RELEASE);
        return "code_protect";
    }
    // One locked 32-bit exchange (the field does not cross a cache line in the
    // contract layout); refuse if anything changed the displacement meanwhile.
    const long observed = _InterlockedCompareExchange(reinterpret_cast<volatile long*>(field),
                                                      static_cast<long>(disp), static_cast<long>(plan.original_disp));
    const bool exchanged = observed == static_cast<long>(plan.original_disp);
    FlushInstructionCache(GetCurrentProcess(), field, sizeof(disp));
    const bool restored = VirtualProtect(field, sizeof(disp), previous, &ignored) != 0;
    if (!exchanged) {
        VirtualFree(buffer, 0, MEM_RELEASE);
        return "displacement_changed";
    }
    // The buffer is intentionally never freed: the game may run the gate state
    // machine at any time until process exit.
    return restored ? nullptr : "code_protection_restore";
}
} // namespace

bool Install(void* base, std::size_t span, const char* url, void (*log)(const char*) noexcept) noexcept {
    if (!url || !*url) return true;
    const DWORD incoming = GetLastError();
    AcquireSRWLockExclusive(&lock);
    struct Finally { DWORD error; ~Finally() { ReleaseSRWLockExclusive(&lock); SetLastError(error); } } finally{incoming};
    if (!base) { Emit(log, "master_server=refused; reason=%s%s\r\n", "no_image", ""); return false; }
    if (installed_base) {
        if (installed_base == base && SameText(installed_url, url)) return true;
        Emit(log, "master_server=refused; reason=%s%s\r\n", "already_redirected", "");
        return false;
    }
    auto* image = static_cast<unsigned char*>(base);
    const Site& site = RetailSite();
    const Plan plan = PlanRedirect(site, span, &ReadLive, image, url);
    if (plan.refusal == Refusal::invalid_url) {
        Emit(log, "master_server=refused; reason=invalid_url; detail=%s%s\r\n", UrlStatusName(plan.url_status), "");
        return false;
    }
    if (plan.refusal != Refusal::none || plan.method == Method::none) {
        Emit(log, "master_server=refused; reason=%s%s\r\n", RefusalName(plan.refusal), "");
        return false;
    }
    if (plan.method == Method::in_place) {
        if (!WriteInPlace(image, site, url, plan.url_length)) {
            Emit(log, "master_server=failed; reason=%s%s\r\n", "in_place_write_or_protection", "");
            // A partial write cannot be distinguished here; do not claim success.
            return false;
        }
    } else if (const char* failure = Retarget(image, plan, url, plan.url_length)) {
        Emit(log, "master_server=failed; reason=%s%s\r\n", failure, "");
        return false;
    }
    if (!VerifyEffective(image, plan, url, plan.url_length)) {
        Emit(log, "master_server=failed; reason=%s; method=%s\r\n", "verify", MethodName(plan.method));
        return false;
    }
    installed_base = base;
    for (std::size_t i = 0; i <= plan.url_length; ++i) installed_url[i] = url[i];
    if (IsLoopbackUrl(installed_url)) InterlockedExchange(&loopback_redirect, 1);
    Emit(log, "master_server=redirected; method=%s; url=%s\r\n", MethodName(plan.method), installed_url);
    return true;
}

bool RedirectedToLoopback() noexcept {
    return InterlockedCompareExchange(&loopback_redirect, 0, 0) != 0;
}
#endif
}
