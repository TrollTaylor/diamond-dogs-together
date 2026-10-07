#pragma once
// Observe the game's existing Entry* update. No request, drain, playback,
// cancel, getter, global routing, or automatic-backend capability.
#include "mgscoop/partner_upper_end_observer.hpp"
namespace mgscoop::partner_upper_phase_observer {
namespace end = partner_upper_end_observer;
using Reader = end::Reader;
using Authorization = end::Authorization;
using Owner = end::Owner;
using Snapshot = end::Snapshot;
using Identity = end::Identity;
using Lease = end::Lease;
using Update = end::Update;
constexpr std::uint64_t kLeaseMs = 250, kSampleIntervalMs = 75;
constexpr unsigned kPairCap = 32;
inline constexpr end::Pin kPins[] = {
    {0xb6c410, 0x1f2, "6ffea8164e703db1f24e1e9f0e3f53e0a0a2083d2fdc52cacb1361969e3adf6e"},
    {0xaf0340, 0x5d, "2ceda96c5086a8a142d8460b117c92e78bd2b55ef74db4c35ee934b4ae9b90c4"}
};
struct Ticket {
    bool qualified = false;
    unsigned pair = 0xffffffffu;
    std::uint64_t serial = 0, lifecycle = 0;
    Lease lease{}; Update update{};
};
struct Observation {
    bool after = false;
    std::uint64_t serial = 0, lifecycle = 0, now_ms = 0;
    Owner owner{}; Update update{}; Snapshot snapshot{};
    std::uintptr_t original_rax = 0;
    // Graph selection, masks and flags do not assert acceptance/completion/stop.
};
// Body1 resolver is independent of the local End observer's body0 lease.
bool ReadGhostSnapshot(Reader, std::uintptr_t image_base, std::uint32_t mission, Snapshot&) noexcept;
bool Compiled() noexcept;
bool Configure(std::uintptr_t image_base, Authorization, Reader) noexcept;
// Established natural Lua owner only; workers cannot create/refresh authority.
bool PublishGhostOwner(const Owner&, std::uint32_t actual_thread, std::uint64_t now_ms) noexcept;
Ticket BeforeUpdate(const Update&, std::uint64_t now_ms) noexcept;
void AfterUpdate(const Ticket&, std::uintptr_t original_rax, std::uint64_t now_ms) noexcept;
bool Drain(Observation&) noexcept;
void Clear() noexcept;
using OriginalFn = std::uintptr_t (*)(void*);
// Used by the one forwarding detour and detached fake-original fixtures.
// Forwards precisely the verified one-argument ABI and opaque raw RAX.
std::uintptr_t ForwardUpdate(OriginalFn, void* entry, std::uintptr_t actual_caller,
                             std::uint32_t actual_thread, std::uint64_t now_ms) noexcept;
// Exact code pins are independently hashed immediately before creating the
// one hook. Parent separately verifies full executable/offline/copied saves.
bool Install(void* image_base, std::size_t image_span) noexcept;
bool Installed() noexcept;
} // namespace mgscoop::partner_upper_phase_observer
