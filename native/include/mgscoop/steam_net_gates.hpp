#pragma once
// Structural gates for interface objects that live in steamclient64.dll
// (docs/steam-online.md section 5, gates 4 and 7). steamclient64 is updated by
// Valve, so nothing in it is pinned by RVA: an object is accepted only when
//   - its vptr lies in a read-only, non-executable section of the module,
//   - the x64 RTTI behind vptr[-1] (CompleteObjectLocator -> TypeDescriptor)
//     names the expected adapter class,
//   - every used slot points into an executable section, and
//   - (Messages002) each slot is the adapter forwarder
//       48 8B 49 08  +  48 8B 01 48 FF 60 xx | 4C 8B 11 49 FF 22 (N=0) | 4C 8B 11 49 FF 62 xx
//     with xx == 8N, and the inner object's vptr/slots pass the same
//     section checks.
// Pure: every read is bounded to the module view (the object itself and the
// inner object are read through the caller's accessor, which the loader wraps
// in __try). Unit tested with synthetic images.
#include <cstddef>
#include <cstdint>

namespace mgscoop::steam_gate {

inline constexpr std::uint32_t kScnExecute = 0x20000000u;
inline constexpr std::uint32_t kScnRead = 0x40000000u;
inline constexpr std::uint32_t kScnWrite = 0x80000000u;
inline constexpr unsigned kMaxSections = 24;

struct Section {
    std::uint32_t rva;
    std::uint32_t size;  // max(VirtualSize, SizeOfRawData) as mapped
    std::uint32_t characteristics;
};
struct ModuleView {
    std::uintptr_t base;
    std::size_t size;  // SizeOfImage
    Section sections[kMaxSections];
    unsigned count;
};

// Parses the PE headers of a mapped image (bounded by `size`). False for a
// malformed header, a non-x64 image or more than kMaxSections sections.
bool ReadModuleView(const std::uint8_t* image, std::size_t size, ModuleView& out) noexcept;

enum class Region : std::uint8_t { outside, code, rdata, other };
// The section kind holding [address, address + length).
Region Classify(const ModuleView& view, std::uintptr_t address, std::size_t length) noexcept;

// RTTI class name check of an object whose vptr is `vptr`.
bool RttiNameIs(const ModuleView& view, std::uintptr_t vptr, const char* expected) noexcept;

// The adapter forwarder for slot `slot` (see above).
bool IsForwarder(const std::uint8_t* code, std::size_t available, unsigned slot) noexcept;

// Reads 8 bytes of an object outside the module (the loader's accessor
// catches access violations); false when unreadable.
using ReadQword = bool (*)(void* context, std::uintptr_t address, std::uint64_t& out) noexcept;

struct InterfaceRule {
    const char* rtti;     // expected adapter class name
    unsigned slots;       // slots [0, slots) must be code
    bool forwarders;      // Messages002: forwarder shape + inner object checks
};
// Reasons (stable log spellings): iface_missing, iface_module, iface_rtti,
// iface_shape. nullptr = accepted.
const char* CheckInterface(const ModuleView& view, std::uintptr_t object, const InterfaceRule& rule, ReadQword read,
                           void* context) noexcept;
// The ISteamClient object (gate 4, reason client_iface): vptr in rdata, the
// RTTI name, and `slot` (12) in code.
const char* CheckClient(const ModuleView& view, std::uintptr_t object, const char* rtti, unsigned slot, ReadQword read,
                        void* context) noexcept;

// Gate 2 (reason not_initialized): SteamClient(), GetHSteamUser(),
// GetHSteamPipe() and SteamUser() are all non-zero.
const char* InitializedGate(const void* client, std::int32_t user, std::int32_t pipe, const void* steam_user) noexcept;
// Gate 5. Play: offline (BLoggedOn false), self_mismatch (GetSteamID not an
// individual id or not net_steam_self), peer_is_self (a guest's host is this
// account). The lab self-test records logged_on and checks nothing here.
const char* IdentityGate(bool selftest, bool logged_on, std::uint64_t self, std::uint64_t expected_self, bool host,
                         std::uint64_t host_id) noexcept;

// True when `module_path` lies under the Steam install directory
// `steam_path` (registry SteamPath; '/' and '\' equal, case-insensitive
// ASCII, the directory itself must be followed by a separator).
bool PathUnder(const wchar_t* module_path, const wchar_t* steam_path) noexcept;

}  // namespace mgscoop::steam_gate
