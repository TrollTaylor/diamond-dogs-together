// Master-server redirect: pure decision logic against synthetic images, plus the
// Win32 Install path against a synthetic in-process image (never the game).
// Run without arguments for validation/planning/in-place; with "retarget" for
// the lea retarget path (Install keeps once-per-process state).
#include "mgscoop/master_server_patch.hpp"
#include "master_server_contract.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace mgscoop::master_server;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}

constexpr char kLabUrl[] = "http://127.0.0.1:6667/tppstm/gate";
std::string UrlOfLength(std::size_t n, const char* scheme = "http://") {
    const std::size_t fixed = std::strlen(scheme) + 12;
    return std::string(scheme) + std::string(n - fixed, 'a') + "/tppstm/gate";
}

struct Buffer { const unsigned char* data; std::size_t size; bool fail; };
bool ReadBuffer(void* context, std::uintptr_t rva, void* out, std::size_t size) noexcept {
    const auto* buffer = static_cast<const Buffer*>(context);
    if (buffer->fail || rva > buffer->size || size > buffer->size - rva) return false;
    std::memcpy(out, buffer->data + rva, size);
    return true;
}

// Lays out the retail contract bytes at their RVAs in an otherwise zero image.
void LayRetail(unsigned char* image) {
    const Site& site = RetailSite();
    std::memcpy(image + site.context_rva, site.context, site.context_size);
    std::memcpy(image + site.string_rva, site.original, site.original_length + 1);
    // Alignment padding is zero; the next constant (byte 0x80 at +0x3F) is outside the gate.
    image[site.string_rva + 0x3f] = 0x80;
}

Plan PlanOn(const std::vector<unsigned char>& image, const char* url, std::size_t span = 0, bool fail = false) {
    Buffer buffer{image.data(), image.size(), fail};
    return PlanRedirect(RetailSite(), span ? span : image.size(), &ReadBuffer, &buffer, url);
}

void TestValidation() {
    std::size_t length = 99;
    Check(ValidateUrl(kLabUrl, length) == UrlStatus::ok && length == 33, "lab url ok");
    Check(ValidateUrl("https://mgstpp-game.konamionline.com/tppstm/gate", length) == UrlStatus::ok && length == 48, "original ok");
    Check(ValidateUrl(UrlOfLength(69).c_str(), length) == UrlStatus::ok && length == 69, "69 ok");
    Check(ValidateUrl(UrlOfLength(70).c_str(), length) == UrlStatus::too_long && length == 0, "70 too long");
    Check(ValidateUrl(UrlOfLength(69, "https://").c_str(), length) == UrlStatus::ok, "https 69 ok");
    Check(ValidateUrl(nullptr, length) == UrlStatus::empty, "null empty");
    Check(ValidateUrl("", length) == UrlStatus::empty, "empty");
    Check(ValidateUrl("ftp://host/tppstm/gate", length) == UrlStatus::bad_scheme, "ftp");
    Check(ValidateUrl("HTTP://host/tppstm/gate", length) == UrlStatus::bad_scheme, "scheme case");
    Check(ValidateUrl("127.0.0.1:6667/tppstm/gate", length) == UrlStatus::bad_scheme, "no scheme");
    Check(ValidateUrl("http://host/tppstm/gate/", length) == UrlStatus::bad_suffix, "trailing slash");
    Check(ValidateUrl("http://host/tppstm/main", length) == UrlStatus::bad_suffix, "main");
    Check(ValidateUrl("http://host/", length) == UrlStatus::bad_suffix, "no path");
    Check(ValidateUrl("http://ho st/tppstm/gate", length) == UrlStatus::bad_character, "space");
    Check(ValidateUrl("http://ho\"st/tppstm/gate", length) == UrlStatus::bad_character, "quote");
    Check(ValidateUrl("http://ho\\st/tppstm/gate", length) == UrlStatus::bad_character, "backslash");
    Check(ValidateUrl("http://ho\tst/tppstm/gate", length) == UrlStatus::bad_character, "tab");
    Check(ValidateUrl("http://ho\x7fst/tppstm/gate", length) == UrlStatus::bad_character, "del");
    Check(ValidateUrl("http://ho\xc3\xa9st/tppstm/gate", length) == UrlStatus::bad_character, "non-ascii");
    Check(ValidateUrl("http:///tppstm/gate", length) == UrlStatus::missing_host, "empty host");
    Check(ValidateUrl("http://tppstm/gate", length) == UrlStatus::missing_host, "suffix overlaps scheme");
    Check(ValidateUrl("https://tppstm/gate", length) == UrlStatus::missing_host, "https suffix overlaps scheme");
    Check(ValidateUrl("http://h/tppstm/gate", length) == UrlStatus::ok && length == 20, "one-char host");
    Check(std::strcmp(UrlStatusName(UrlStatus::missing_host), "missing_host") == 0, "status name");
}

void TestRetailContract() {
    namespace c = mgscoop::master_server_contract;
    const Site& site = RetailSite();
    Check(site.lea_rva == 0x7D346C && site.disp_offset == 3 && site.lea_size == 7, "lea site");
    Check(site.string_rva == 0x2218708 && site.original_length == 48, "string site");
    Check(std::strcmp(site.original, "https://mgstpp-game.konamionline.com/tppstm/gate") == 0, "original text");
    Check(site.padding_after_terminator == 7, "padding to 16-byte boundary");
    const unsigned char lea[] = {0x48, 0x8d, 0x15, 0x95, 0x52, 0xa4, 0x01};
    Check(std::memcmp(site.context + (site.lea_rva - site.context_rva), lea, sizeof(lea)) == 0, "lea bytes in context");
    std::int32_t disp = 0;
    std::memcpy(&disp, site.context + (site.lea_rva - site.context_rva) + 3, 4);
    Check(site.lea_rva + site.lea_size + static_cast<std::uintptr_t>(disp) == site.string_rva, "disp resolves to string");
    Check(c::string_code_references == 1, "single code reference recorded");
    Check(site.context_rva == 0x7D3461 && site.context_size == 27, "context window");
}

void TestPlanning() {
    const Site& site = RetailSite();
    const std::size_t size = site.string_rva + 0x60;
    std::vector<unsigned char> image(size, 0xcc);
    std::memset(image.data() + site.string_rva - 0x20, 0, 0x80);
    LayRetail(image.data());

    Plan plan = PlanOn(image, kLabUrl);
    Check(plan.refusal == Refusal::none && plan.method == Method::in_place, "lab url in place");
    Check(plan.url_length == 33 && plan.disp_rva == 0x7D346F && plan.next_ip_rva == 0x7D3473, "plan geometry");
    Check(plan.original_disp == 0x01A45295, "original disp");
    Check(PlanOn(image, UrlOfLength(48).c_str()).method == Method::in_place, "48 in place");
    Check(PlanOn(image, UrlOfLength(49).c_str()).method == Method::lea_retarget, "49 retarget");
    Check(PlanOn(image, UrlOfLength(69).c_str()).method == Method::lea_retarget, "69 retarget");
    plan = PlanOn(image, UrlOfLength(70).c_str());
    Check(plan.refusal == Refusal::invalid_url && plan.url_status == UrlStatus::too_long, "70 refused");
    plan = PlanOn(image, nullptr);
    Check(plan.refusal == Refusal::none && plan.method == Method::none, "null url no-op");
    plan = PlanOn(image, "");
    Check(plan.refusal == Refusal::none && plan.method == Method::none, "empty url no-op");
    // URL validation precedes any image access.
    Check(PlanOn(image, "http://x y/tppstm/gate", 0, true).refusal == Refusal::invalid_url, "invalid before read");
    Check(PlanOn(image, kLabUrl, 0, true).refusal == Refusal::unreadable, "unreadable");
    Check(PlanOn(image, kLabUrl, site.string_rva + 0x30).refusal == Refusal::image_span, "span ends in string");
    Check(PlanOn(image, kLabUrl, 0x7D3470).refusal == Refusal::image_span, "span ends in code");

    // Every byte of the context window is gated, including the displacement.
    // Mutate one byte at a time in place and restore it (the image is ~35 MB).
    auto gated = [&](std::uintptr_t rva, unsigned char value, Refusal expected) {
        const unsigned char saved = image[rva];
        image[rva] = value;
        const bool ok = PlanOn(image, kLabUrl).refusal == expected;
        image[rva] = saved;
        return ok;
    };
    bool all = true;
    for (std::size_t i = 0; i < site.context_size; ++i)
        all = all && gated(site.context_rva + i, static_cast<unsigned char>(site.context[i] ^ 0x01), Refusal::instruction_bytes);
    Check(all, "every context byte gated (including the disp32)");
    all = true;
    for (std::size_t i = 0; i < site.original_length; ++i)
        all = all && gated(site.string_rva + i, 'X', Refusal::string_bytes);
    Check(all, "every string byte gated");
    Check(gated(site.string_rva + site.original_length, '/', Refusal::string_bytes), "terminator gated");
    all = true;
    for (std::size_t i = 1; i <= site.padding_after_terminator; ++i)
        all = all && gated(site.string_rva + site.original_length + i, 1, Refusal::padding_bytes);
    Check(all, "alignment padding gated");
    // Bytes past the gate (the following constant) do not matter.
    Check(gated(site.string_rva + 0x40, 0x7f, Refusal::none), "following constant not gated");
    Check(PlanOn(image, kLabUrl).method == Method::in_place, "image restored after mutations");
    // An image whose string already holds a redirected URL is refused.
    std::vector<unsigned char> saved(image.begin() + static_cast<std::ptrdiff_t>(site.string_rva),
                                     image.begin() + static_cast<std::ptrdiff_t>(site.string_rva + site.original_length + 1));
    std::memset(image.data() + site.string_rva, 0, site.original_length + 1);
    std::memcpy(image.data() + site.string_rva, kLabUrl, sizeof(kLabUrl));
    Check(PlanOn(image, kLabUrl).refusal == Refusal::string_bytes, "already rewritten string refused");
    std::memcpy(image.data() + site.string_rva, saved.data(), saved.size());
}

void TestSyntheticSite() {
    // A small synthetic layout: code at 0x100, string at 0x400.
    unsigned char context[] = {0x90, 0x48, 0x8d, 0x15, 0, 0, 0, 0, 0x90};
    const std::uintptr_t lea = 0x101, next = lea + 7, string = 0x400;
    const std::int32_t disp = static_cast<std::int32_t>(string - next);
    std::memcpy(context + 4, &disp, 4);
    const char original[] = "https://example.invalid/tppstm/gate";
    Site site{0x100, context, sizeof(context), lea, 7, 3, string, original, sizeof(original) - 1, 4};
    std::vector<unsigned char> image(0x500, 0);
    std::memcpy(image.data() + 0x100, context, sizeof(context));
    std::memcpy(image.data() + string, original, sizeof(original));
    Buffer buffer{image.data(), image.size(), false};
    Plan plan = PlanRedirect(site, image.size(), &ReadBuffer, &buffer, kLabUrl);
    Check(plan.method == Method::in_place && plan.disp_rva == 0x104 && plan.next_ip_rva == 0x108, "synthetic in place");
    Check(PlanRedirect(site, image.size(), &ReadBuffer, &buffer, UrlOfLength(36).c_str()).method == Method::lea_retarget,
          "synthetic retarget beyond original length");
    // Context whose displacement does not reach the string.
    unsigned char wrong[sizeof(context)];
    std::memcpy(wrong, context, sizeof(context));
    wrong[4] ^= 0x10;
    Site wrong_site = site;
    wrong_site.context = wrong;
    std::memcpy(image.data() + 0x100, wrong, sizeof(wrong));
    Check(PlanRedirect(wrong_site, image.size(), &ReadBuffer, &buffer, kLabUrl).refusal == Refusal::lea_target, "lea target");
    std::memcpy(image.data() + 0x100, context, sizeof(context));
    // Incoherent site descriptions.
    Site bad = site;
    bad.lea_rva = 0x105;  // lea runs past the context window
    Check(PlanRedirect(bad, image.size(), &ReadBuffer, &buffer, kLabUrl).refusal == Refusal::bad_site, "lea outside context");
    bad = site;
    bad.context_size = 0;
    Check(PlanRedirect(bad, image.size(), &ReadBuffer, &buffer, kLabUrl).refusal == Refusal::bad_site, "empty context");
    bad = site;
    bad.disp_offset = 4;
    Check(PlanRedirect(bad, image.size(), &ReadBuffer, &buffer, kLabUrl).refusal == Refusal::bad_site, "disp past lea");
    Check(PlanRedirect(site, image.size(), nullptr, &buffer, kLabUrl).refusal == Refusal::bad_site, "no reader");
}

void TestHelpers() {
    const Site& site = RetailSite();
    unsigned char bytes[64];
    std::memset(bytes, 0xee, sizeof(bytes));
    Check(BuildInPlaceBytes(site, kLabUrl, 33, bytes, sizeof(bytes)), "build in place");
    Check(std::memcmp(bytes, kLabUrl, 33) == 0, "url copied");
    bool zero = true;
    for (std::size_t i = 33; i <= 48; ++i) zero = zero && bytes[i] == 0;
    Check(zero && bytes[49] == 0xee, "nul padded to exactly 49 bytes");
    Check(!BuildInPlaceBytes(site, UrlOfLength(49).c_str(), 49, bytes, sizeof(bytes)), "49 does not fit");
    Check(!BuildInPlaceBytes(site, kLabUrl, 33, bytes, 48), "small output refused");
    Check(!BuildInPlaceBytes(site, kLabUrl, 40, bytes, sizeof(bytes)), "length past terminator refused");

    std::int32_t disp = 0;
    Check(Rel32(0x1407D3473ull, 0x142218708ull, disp) && disp == 0x01A45295, "rel32 retail");
    Check(Rel32(0x100000000ull, 0x100000000ull + 0x7fffffffull, disp) && disp == INT32_MAX, "rel32 max");
    Check(!Rel32(0x100000000ull, 0x100000000ull + 0x80000000ull, disp), "rel32 max+1");
    Check(Rel32(0x100000000ull, 0x100000000ull - 0x80000000ull, disp) && disp == INT32_MIN, "rel32 min");
    Check(!Rel32(0x100000000ull, 0x100000000ull - 0x80000001ull, disp), "rel32 min-1");

    std::uint64_t low = 0, high = 0;
    const std::uint64_t next_ip = 0x1407D3473ull;
    Check(NearWindow(next_ip, 0x1000, 0x10000, 0x10000, 0x7ffffffeffffull, low, high), "near window");
    Check((low & 0xffff) == 0 && (high & 0xffff) == 0 && low <= high, "aligned window");
    Check(Rel32(next_ip, low, disp) && Rel32(next_ip, high + 0x1000 - 1, disp), "window reachable");
    Check(low >= next_ip - 0x7fff0000ull && low < next_ip - 0x7fff0000ull + 0x10000, "low tight");
    Check(NearWindow(0x30000, 0x1000, 0x10000, 0x10000, 0x7ffffffeffffull, low, high) && low == 0x10000, "low clamps to minimum");
    Check(NearWindow(0x7ffffffe0000ull, 0x1000, 0x10000, 0x10000, 0x7ffffffeffffull, low, high) &&
          high + 0x1000 - 1 <= 0x7ffffffeffffull, "high clamps to maximum");
    Check(!NearWindow(next_ip, 0x1000, 0x3000, 0x10000, 0x7ffffffeffffull, low, high), "granularity power of two");
    Check(!NearWindow(next_ip, 0, 0x10000, 0x10000, 0x7ffffffeffffull, low, high), "zero size");
    Check(!NearWindow(next_ip, 0x1000, 0x10000, 0x200000000ull, 0x7ffffffeffffull, low, high), "window outside range");
}

char log_text[2048];
unsigned log_lines = 0;
void CaptureLog(const char* line) noexcept {
    ++log_lines;
    const std::size_t used = std::strlen(log_text);
    const std::size_t add = std::strlen(line);
    if (used + add < sizeof(log_text)) std::memcpy(log_text + used, line, add + 1);
}
void ClearLog() { log_text[0] = 0; log_lines = 0; }

struct SyntheticImage {
    unsigned char* base = nullptr;
    std::size_t span = 0;
};
SyntheticImage MakeImage() {
    const Site& site = RetailSite();
    SyntheticImage image;
    image.span = (site.string_rva + 0x1000 + 0xffff) & ~static_cast<std::size_t>(0xffff);
    image.base = static_cast<unsigned char*>(VirtualAlloc(nullptr, image.span, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Check(image.base != nullptr, "synthetic image allocation");
    LayRetail(image.base);
    DWORD old = 0;
    // Mirror the retail protections: code execute-read, string page read-only.
    Check(VirtualProtect(image.base + 0x7D3000, 0x1000, PAGE_EXECUTE_READ, &old) != 0, "code page protection");
    Check(VirtualProtect(image.base + 0x2218000, 0x1000, PAGE_READONLY, &old) != 0, "string page protection");
    return image;
}
DWORD Protection(const void* address) {
    MEMORY_BASIC_INFORMATION info{};
    Check(VirtualQuery(address, &info, sizeof(info)) != 0, "query protection");
    return info.Protect;
}

void TestInstallInPlace() {
    const Site& site = RetailSite();
    SyntheticImage image = MakeImage();
    std::vector<unsigned char> before(image.base + site.context_rva, image.base + site.string_rva + 0x40);

    ClearLog();
    Check(Install(image.base, image.span, nullptr, &CaptureLog) && log_lines == 0, "null url no-op");
    Check(Install(image.base, image.span, "", &CaptureLog) && log_lines == 0, "empty url no-op");
    Check(!Install(image.base, image.span, "http://x/tppstm/main", &CaptureLog), "bad url refused");
    Check(std::strcmp(log_text, "master_server=refused; reason=invalid_url; detail=bad_suffix\r\n") == 0, "bad url log");
    ClearLog();
    Check(!Install(image.base, site.string_rva + 8, kLabUrl, &CaptureLog), "short span refused");
    Check(std::strcmp(log_text, "master_server=refused; reason=image_span\r\n") == 0, "span log");
    ClearLog();
    Check(!Install(nullptr, image.span, kLabUrl, &CaptureLog), "null base refused");
    Check(std::strcmp(log_text, "master_server=refused; reason=no_image\r\n") == 0, "no image log");
    // A changed instruction byte refuses without writing anything.
    DWORD old = 0;
    Check(VirtualProtect(image.base + 0x7D3000, 0x1000, PAGE_EXECUTE_READWRITE, &old) != 0, "unprotect code");
    image.base[site.lea_rva + 1] = 0x8b;  // lea -> mov
    ClearLog();
    Check(!Install(image.base, image.span, kLabUrl, &CaptureLog), "mov refused");
    Check(std::strcmp(log_text, "master_server=refused; reason=instruction_bytes\r\n") == 0, "instruction log");
    image.base[site.lea_rva + 1] = 0x8d;
    Check(VirtualProtect(image.base + 0x7D3000, 0x1000, PAGE_EXECUTE_READ, &old) != 0, "reprotect code");
    Check(std::memcmp(before.data(), image.base + site.context_rva, before.size()) == 0, "refusals wrote nothing");

    ClearLog();
    Check(Install(image.base, image.span, kLabUrl, &CaptureLog), "in-place install");
    Check(std::strcmp(log_text, "master_server=redirected; method=in_place; url=http://127.0.0.1:6667/tppstm/gate\r\n") == 0,
          "in-place log");
    Check(std::memcmp(image.base + site.string_rva, kLabUrl, sizeof(kLabUrl)) == 0, "string rewritten");
    bool zero = true;
    for (std::size_t i = 33; i <= site.original_length + site.padding_after_terminator; ++i)
        zero = zero && image.base[site.string_rva + i] == 0;
    Check(zero && image.base[site.string_rva + 0x3f] == 0x80, "nul padded; neighbour untouched");
    Check(std::memcmp(before.data(), image.base + site.context_rva, site.context_size) == 0, "code untouched in place");
    Check(Protection(image.base + site.string_rva) == PAGE_READONLY, "string protection restored");
    Check(Protection(image.base + site.lea_rva) == PAGE_EXECUTE_READ, "code protection untouched");
    ClearLog();
    Check(Install(image.base, image.span, kLabUrl, &CaptureLog) && log_lines == 0, "repeat same url is a quiet no-op");
    Check(!Install(image.base, image.span, "http://127.0.0.2:6667/tppstm/gate", &CaptureLog), "different url refused");
    Check(std::strcmp(log_text, "master_server=refused; reason=already_redirected\r\n") == 0, "already log");
}

void TestInstallRetarget() {
    const Site& site = RetailSite();
    SyntheticImage image = MakeImage();
    const std::string url = UrlOfLength(64, "https://");
    ClearLog();
    Check(Install(image.base, image.span, url.c_str(), &CaptureLog), "retarget install");
    const std::string expected = "master_server=redirected; method=lea_retarget; url=" + url + "\r\n";
    Check(expected == log_text, "retarget log");
    Check(std::memcmp(image.base + site.string_rva, site.original, site.original_length + 1) == 0, "original string kept");
    std::int32_t disp = 0;
    std::memcpy(&disp, image.base + site.lea_rva + 3, 4);
    Check(disp != 0x01A45295, "displacement changed");
    const auto* target = reinterpret_cast<const char*>(image.base + site.lea_rva + 7 + disp);
    Check(std::strcmp(target, url.c_str()) == 0, "lea addresses the new url");
    Check(Protection(target) == PAGE_READONLY, "buffer read-only");
    Check(Protection(image.base + site.lea_rva) == PAGE_EXECUTE_READ, "code protection restored");
    Check(std::memcmp(image.base + site.context_rva, site.context, site.lea_rva - site.context_rva + 3) == 0 &&
          std::memcmp(image.base + site.lea_rva + 7, site.context + (site.lea_rva - site.context_rva) + 7,
                      site.context_size - (site.lea_rva - site.context_rva) - 7) == 0,
          "only the displacement changed");
    ClearLog();
    Check(Install(image.base, image.span, url.c_str(), &CaptureLog) && log_lines == 0, "retarget repeat no-op");
}
} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "retarget") == 0) {
        TestInstallRetarget();
        std::printf("%u master-server retarget checks passed; synthetic in-process image only\n", checks);
        return 0;
    }
    TestValidation();
    TestRetailContract();
    TestPlanning();
    TestSyntheticSite();
    TestHelpers();
    TestInstallInPlace();
    std::printf("%u master-server redirect checks passed; synthetic images only, no game\n", checks);
    return 0;
}
