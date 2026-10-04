#pragma once
#include <cstddef>
#include <cstdint>
namespace mgscoop::master_server {
// Point the game's master-server gate URL at `url` (e.g. a local fuse instance,
// http://127.0.0.1:6667/tppstm/gate) in memory only. An empty or null url is a
// no-op that returns true. Must verify the exact 1.0.15.4 instruction/string
// bytes before writing; refuses (returns false, logs a reason) on any mismatch.
// No executable disk write.
//
// Method (static evidence: native/master_server_contract.hpp):
//  - url length <= 48 (the original string): overwrite the string in place,
//    NUL-padded to its original 49 bytes, then restore page protection;
//  - 49..69 chars: retarget the disp32 of the only code reference (the lea at
//    0x7D346C) to a process-lifetime read-only buffer allocated within +-2 GB.
// Logs `master_server=redirected; method=<in_place|lea_retarget>; url=<url>`,
// or `master_server=refused; reason=<...>` / `master_server=failed; reason=<...>`.
// Calling again with the same URL after success returns true without changes.
bool Install(void* base, std::size_t span, const char* url, void (*log)(const char*) noexcept) noexcept;

// ---- Pure decision logic (portable, no Win32; unit tested on synthetic images) ----

inline constexpr std::size_t kMaxUrlLength = 69;  // CoopConfig::master_url[70]

enum class UrlStatus : std::uint8_t { ok, empty, too_long, bad_scheme, bad_suffix, bad_character, missing_host };
// http(s)://<host...>/tppstm/gate, printable ASCII without space, '"' or '\\',
// at most kMaxUrlLength chars. `length` receives the length when ok.
UrlStatus ValidateUrl(const char* url, std::size_t& length) noexcept;

enum class Method : std::uint8_t { none, in_place, lea_retarget };
enum class Refusal : std::uint8_t {
    none, invalid_url, bad_site, image_span, unreadable, instruction_bytes, lea_target, string_bytes, padding_bytes
};

// Description of one patch site. RetailSite() is the 1.0.15.4 contract; tests
// also build synthetic sites.
struct Site {
    std::uintptr_t context_rva;            // first expected code byte
    const unsigned char* context;          // exact expected code bytes (includes the lea)
    std::size_t context_size;              // <= 64
    std::uintptr_t lea_rva;                // lea rdx,[rip+disp32]
    std::size_t lea_size;                  // 7
    std::size_t disp_offset;               // 3
    std::uintptr_t string_rva;             // original URL
    const char* original;                  // original URL text
    std::size_t original_length;           // without terminator, < 128
    std::size_t padding_after_terminator;  // zero bytes that must follow the terminator (gate only)
};
const Site& RetailSite() noexcept;

// Reads `size` bytes at image RVA `rva`; false when unreadable.
using ReadImage = bool (*)(void* context, std::uintptr_t rva, void* out, std::size_t size) noexcept;

struct Plan {
    Method method = Method::none;   // none with refusal==none means "empty url, nothing to do"
    Refusal refusal = Refusal::none;
    UrlStatus url_status = UrlStatus::empty;
    std::size_t url_length = 0;
    std::uintptr_t disp_rva = 0;     // RVA of the disp32 to retarget
    std::uintptr_t next_ip_rva = 0;  // RVA the disp32 is relative to
    std::int32_t original_disp = 0;
};
Plan PlanRedirect(const Site& site, std::size_t span, ReadImage read, void* context, const char* url) noexcept;

// Bytes for an in-place write: the url, then NULs up to original_length + 1.
// False if the url does not fit or out_size is smaller than original_length + 1.
bool BuildInPlaceBytes(const Site& site, const char* url, std::size_t url_length,
                       unsigned char* out, std::size_t out_size) noexcept;

// rel32 displacement from next_ip to target; false when out of int32 range.
bool Rel32(std::uint64_t next_ip, std::uint64_t target, std::int32_t& out) noexcept;

// Allocation-granularity aligned [low, high] start addresses for a `size`-byte
// block that a rel32 at next_ip can reach with a 64 KiB safety margin, clamped
// to [min_address, max_address]. False when empty or granularity is not a power of two.
bool NearWindow(std::uint64_t next_ip, std::uint64_t size, std::uint64_t granularity,
                std::uint64_t min_address, std::uint64_t max_address,
                std::uint64_t& low, std::uint64_t& high) noexcept;

const char* MethodName(Method value) noexcept;
const char* RefusalName(Refusal value) noexcept;
const char* UrlStatusName(UrlStatus value) noexcept;
}
