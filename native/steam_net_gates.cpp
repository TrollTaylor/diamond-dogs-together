// Original MGS Co-op structural gates for steamclient64 interface objects.
#include "mgscoop/steam_net_gates.hpp"

namespace mgscoop::steam_gate {
namespace {
std::uint16_t U16(const std::uint8_t* p) noexcept { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t U32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint64_t U64(const std::uint8_t* p) noexcept { return static_cast<std::uint64_t>(U32(p)) | (static_cast<std::uint64_t>(U32(p + 4)) << 32); }

// [address, address + length) inside the module image.
bool InModule(const ModuleView& view, std::uintptr_t address, std::size_t length) noexcept {
    if (address < view.base) return false;
    const std::uintptr_t offset = address - view.base;
    return offset <= view.size && length <= view.size - offset;
}
const std::uint8_t* At(std::uintptr_t address) noexcept {
    return reinterpret_cast<const std::uint8_t*>(address);
}
}  // namespace

bool ReadModuleView(const std::uint8_t* image, std::size_t size, ModuleView& out) noexcept {
    out = ModuleView{};
    if (!image || size < 0x200) return false;
    if (image[0] != 'M' || image[1] != 'Z') return false;
    const std::uint32_t pe = U32(image + 0x3C);
    if (pe > size - 24 || U32(image + pe) != 0x00004550u) return false;
    if (U16(image + pe + 4) != 0x8664) return false;  // x64 only
    const std::uint16_t count = U16(image + pe + 6);
    const std::uint16_t optional = U16(image + pe + 20);
    const std::size_t table = static_cast<std::size_t>(pe) + 24 + optional;
    if (count == 0 || count > kMaxSections || table > size || static_cast<std::size_t>(count) * 40 > size - table) return false;
    if (optional < 0x40 || U16(image + pe + 24) != 0x20B) return false;  // PE32+
    const std::uint32_t size_of_image = U32(image + pe + 24 + 56);
    if (size_of_image == 0 || size_of_image > size) return false;
    out.base = reinterpret_cast<std::uintptr_t>(image);
    out.size = size_of_image;
    for (unsigned i = 0; i < count; ++i) {
        const std::uint8_t* s = image + table + 40u * i;
        const std::uint32_t vsize = U32(s + 8), rva = U32(s + 12), raw = U32(s + 16);
        const std::uint32_t span = vsize > raw ? vsize : raw;
        if (rva > size_of_image || span > size_of_image - rva) return false;
        out.sections[i] = Section{rva, span, U32(s + 36)};
    }
    out.count = count;
    return true;
}

Region Classify(const ModuleView& view, std::uintptr_t address, std::size_t length) noexcept {
    if (!length || !InModule(view, address, length)) return Region::outside;
    const std::uintptr_t rva = address - view.base;
    for (unsigned i = 0; i < view.count && i < kMaxSections; ++i) {
        const Section& s = view.sections[i];
        if (rva < s.rva || rva - s.rva >= s.size || length > s.size - (rva - s.rva)) continue;
        const std::uint32_t c = s.characteristics;
        if ((c & kScnExecute) && (c & kScnRead) && !(c & kScnWrite)) return Region::code;
        if ((c & kScnRead) && !(c & kScnWrite) && !(c & kScnExecute)) return Region::rdata;
        return Region::other;
    }
    return Region::outside;
}

bool RttiNameIs(const ModuleView& view, std::uintptr_t vptr, const char* expected) noexcept {
    if (!expected || vptr < 8 || Classify(view, vptr - 8, 8) != Region::rdata) return false;
    const std::uint64_t col = U64(At(vptr - 8));
    // _RTTICompleteObjectLocator (x64): signature 1, offset, cdOffset, TD rva, CHD rva, self rva.
    if (Classify(view, static_cast<std::uintptr_t>(col), 24) != Region::rdata) return false;
    const std::uint8_t* c = At(static_cast<std::uintptr_t>(col));
    if (U32(c) != 1 || U32(c + 4) != 0) return false;
    if (U32(c + 20) != static_cast<std::uint32_t>(static_cast<std::uintptr_t>(col) - view.base)) return false;
    const std::uintptr_t td = view.base + U32(c + 12);
    std::size_t length = 0;
    while (expected[length]) ++length;
    // TypeDescriptor: pVFTable, spare, then the NUL-terminated decorated name.
    if (!InModule(view, td + 16, length + 1)) return false;
    const auto* name = reinterpret_cast<const char*>(td + 16);
    for (std::size_t i = 0; i <= length; ++i)
        if (name[i] != expected[i]) return false;
    return true;
}

bool IsForwarder(const std::uint8_t* code, std::size_t available, unsigned slot) noexcept {
    if (!code || available < 10 || slot > 31) return false;
    // mov rcx,[rcx+8]
    if (code[0] != 0x48 || code[1] != 0x8B || code[2] != 0x49 || code[3] != 0x08) return false;
    const std::uint8_t disp = static_cast<std::uint8_t>(8 * slot);
    const std::uint8_t* p = code + 4;
    // mov rax,[rcx] ; jmp [rax+disp8]
    if (available >= 11 && p[0] == 0x48 && p[1] == 0x8B && p[2] == 0x01 && p[3] == 0x48 && p[4] == 0xFF && p[5] == 0x60)
        return p[6] == disp;
    // mov r10,[rcx] ; jmp [r10] (slot 0) | jmp [r10+disp8]
    if (p[0] == 0x4C && p[1] == 0x8B && p[2] == 0x11 && p[3] == 0x49 && p[4] == 0xFF) {
        if (p[5] == 0x22) return slot == 0;
        if (available >= 11 && p[5] == 0x62) return p[6] == disp;
    }
    return false;
}

const char* CheckInterface(const ModuleView& view, std::uintptr_t object, const InterfaceRule& rule, ReadQword read,
                           void* context) noexcept {
    if (!object) return "iface_missing";
    std::uint64_t vptr = 0;
    if (!read || !read(context, object, vptr)) return "iface_module";
    if (Classify(view, static_cast<std::uintptr_t>(vptr), 8u * (rule.slots ? rule.slots : 1)) != Region::rdata) return "iface_module";
    if (!RttiNameIs(view, static_cast<std::uintptr_t>(vptr), rule.rtti)) return "iface_rtti";
    for (unsigned i = 0; i < rule.slots; ++i) {
        const std::uint64_t slot = U64(At(static_cast<std::uintptr_t>(vptr) + 8u * i));
        if (Classify(view, static_cast<std::uintptr_t>(slot), 11) != Region::code) return "iface_shape";
        if (rule.forwarders && !IsForwarder(At(static_cast<std::uintptr_t>(slot)), 11, i)) return "iface_shape";
    }
    if (!rule.forwarders) return nullptr;
    // The inner object (adapter +8) and its vtable.
    std::uint64_t inner = 0, inner_vptr = 0;
    if (!read(context, object + 8, inner) || !inner || !read(context, static_cast<std::uintptr_t>(inner), inner_vptr))
        return "iface_shape";
    if (Classify(view, static_cast<std::uintptr_t>(inner_vptr), 8u * rule.slots) != Region::rdata) return "iface_shape";
    for (unsigned i = 0; i < rule.slots; ++i) {
        const std::uint64_t slot = U64(At(static_cast<std::uintptr_t>(inner_vptr) + 8u * i));
        if (Classify(view, static_cast<std::uintptr_t>(slot), 1) != Region::code) return "iface_shape";
    }
    return nullptr;
}

const char* CheckClient(const ModuleView& view, std::uintptr_t object, const char* rtti, unsigned slot, ReadQword read,
                        void* context) noexcept {
    std::uint64_t vptr = 0;
    if (!object || !read || !read(context, object, vptr)) return "client_iface";
    if (Classify(view, static_cast<std::uintptr_t>(vptr), 8u * (slot + 1)) != Region::rdata) return "client_iface";
    if (!RttiNameIs(view, static_cast<std::uintptr_t>(vptr), rtti)) return "client_iface";
    const std::uint64_t target = U64(At(static_cast<std::uintptr_t>(vptr) + 8u * slot));
    if (Classify(view, static_cast<std::uintptr_t>(target), 1) != Region::code) return "client_iface";
    return nullptr;
}

const char* InitializedGate(const void* client, std::int32_t user, std::int32_t pipe, const void* steam_user) noexcept {
    return client && user && pipe && steam_user ? nullptr : "not_initialized";
}

const char* IdentityGate(bool selftest, bool logged_on, std::uint64_t self, std::uint64_t expected_self, bool host,
                         std::uint64_t host_id) noexcept {
    if (selftest) return nullptr;
    if (!logged_on) return "offline";
    const bool individual = (self >> 32) == 0x01100001u && (self & 0xffffffffu) != 0;
    if (!individual || self != expected_self) return "self_mismatch";
    if (!host && host_id && host_id == self) return "peer_is_self";
    return nullptr;
}

bool PathUnder(const wchar_t* module_path, const wchar_t* steam_path) noexcept {
    if (!module_path || !steam_path || !steam_path[0]) return false;
    const auto fold = [](wchar_t c) noexcept -> wchar_t {
        if (c == L'/') return L'\\';
        if (c >= L'A' && c <= L'Z') return static_cast<wchar_t>(c - L'A' + L'a');
        return c;
    };
    std::size_t n = 0;
    while (steam_path[n]) ++n;
    while (n && fold(steam_path[n - 1]) == L'\\') --n;  // trailing separators
    if (!n) return false;
    for (std::size_t i = 0; i < n; ++i)
        if (!module_path[i] || fold(module_path[i]) != fold(steam_path[i])) return false;
    if (fold(module_path[n]) != L'\\') return false;
    // No ".." component after the prefix.
    for (std::size_t i = n; module_path[i]; ++i)
        if (module_path[i] == L'.' && module_path[i + 1] == L'.') return false;
    return true;
}

}  // namespace mgscoop::steam_gate
