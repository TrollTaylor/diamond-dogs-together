#pragma once
// Separate default-OFF manual diagnostic. No automatic play/stop capability.
// Submit exactly one existing upper idle request at the allocated body's own
// natural Entry update. Never uses DirectMotion or whole Gun helpers.
#include "mgscoop/partner_upper_phase_observer.hpp"
namespace mgscoop::partner_upper_manual_idle {
namespace phase = partner_upper_phase_observer;
namespace end = partner_upper_end_observer;
using Reader = end::Reader;
using Authorization = end::Authorization;
using Owner = end::Owner;
using Snapshot = end::Snapshot;
using Update = end::Update;
using Lease = end::Lease;
constexpr std::uint64_t kIdle = 0xc1a2050d3802;
constexpr std::uint64_t kCommandMs = 2000, kOwnerMs = 250;
constexpr unsigned kCommandCap = 8, kReceiptCap = kCommandCap * 4;
// Hash BEFORE the existing CS170/B6 hooks change their prologues. The exact
// executable gate separately includes the graph dispatch/commit routines.
inline constexpr end::Pin kPins[] = {
    {0xaf2ba0,0x8e,"bf4f8d6085f686bad8a6eecd5863a6a8b0b39c0f0df19e3803e6e7f648a709ff"},
    {0xb6e9f0,0xa5,"0cbabd97f75b22722107aa37ff97343684ece2e4c76938b4a2448a1dd8b504d1"},
    {0xaf0340,0x5d,"2ceda96c5086a8a142d8460b117c92e78bd2b55ef74db4c35ee934b4ae9b90c4"},
    {0xb6c410,0x1f2,"6ffea8164e703db1f24e1e9f0e3f53e0a0a2083d2fdc52cacb1361969e3adf6e"}
};
// Additional idle name resolution/direct commit bodies are still unhooked.
// They must independently match in-process before Configure can authorize.
inline constexpr end::Pin kAdditionalPins[] = {
    {0x1c704e0,0x377,"5b6de58b0bcd4ad760a461cc5b9fcdda2d99e4f6f8f49d3757138adfb12c0109"},
    {0x1c72a10,0x1fd,"06d53e07d2e2e44f787474d021992b92533995904896167e1e4fc8ef36bcc435"},
    {0x1c719e0,0xb4,"a07b1c212d30f480f9a9b2f7f4657a034d579fba700c2da0a99c5c262faa9150"}
};
using WritableFn = bool (*)(void*,std::uintptr_t,std::size_t) noexcept;
struct StorageQuery { void* context = nullptr; WritableFn writable = nullptr; };
enum class Stage : std::uint8_t { armed, before_submit, after_submit, after_update, refused };
struct Ticket {
    bool submitted = false;
    std::uint64_t command = 0, lifecycle = 0, issued_ms = 0;
    Lease lease{}; Update update{};
};
struct Receipt {
    Stage stage = Stage::refused;
    std::uint64_t command = 0, lifecycle = 0, now_ms = 0;
    Owner owner{}; Update update{}; Snapshot snapshot{};
    std::uintptr_t raw_submit_rax = 0, raw_update_rax = 0;
    const char* refusal = nullptr;
    bool submit_attempted = false, immediate_lower_unchanged = false, upper_idle_current = false, upper_noqueued = false;
    // These are readback facts, never accepted/stopped/completed capabilities.
};
bool Compiled() noexcept;
// Exact standalone test trigger, with surrounding ASCII whitespace only.
// Parsing does not arm or change the normal partner driver/DirectMotion.
bool IsTrigger(const char*,std::size_t) noexcept;
bool CodeGate(std::uintptr_t image,std::size_t span,Reader,std::uintptr_t* mismatch=nullptr) noexcept;
// Configure once before installing the existing phase hook. Empty query uses
// bounded VirtualQuery in the current process; tests inject a fake query.
bool Configure(std::uintptr_t image, Authorization, Reader, StorageQuery = {}) noexcept;
// This is a separate body1 permission lease. A phase observation lease cannot
// enqueue a command. Established Lua owner only; epoch0 is valid.
bool PublishOwner(const Owner&,std::uint32_t actual_thread,std::uint64_t now_ms) noexcept;
// Explicit manual nonce only, increasing even across Clear. Refuse unless
// Fire116 currently owns upper2, exact idle89 exists, and storage is writable.
bool Arm(std::uint64_t command,std::uint32_t actual_thread,std::uint64_t now_ms) noexcept;
Ticket BeforeUpdate(const Update&,std::uint64_t now_ms) noexcept;
void AfterUpdate(const Ticket&,std::uintptr_t original_rax,std::uint64_t now_ms) noexcept;
bool Drain(Receipt&) noexcept;
// Invalidate before body/mission/reset/un-realize/relink/termination/disable
// and failed owner ticks. No reset of the lifetime command/receipt budget.
void Clear() noexcept;
// Reuses phase's existing End/body1 observation multiplexer and original ABI.
std::uintptr_t ForwardUpdate(phase::OriginalFn,void* entry,std::uintptr_t actual_caller,
                             std::uint32_t actual_thread,std::uint64_t now_ms) noexcept;
#if defined(MGS_UPPER_MANUAL_IDLE_TESTING)
using TestSubmitFn = std::uintptr_t (*)(void*,std::uint32_t,std::uint32_t,std::uint64_t,float,std::uint32_t);
// Only test builds can replace the validated virtual method with a fake.
void TestSubmit(TestSubmitFn) noexcept;
bool TestWritable(std::uintptr_t,std::size_t) noexcept;
using TestPinFn = bool (*)(Reader,std::uintptr_t,const end::Pin&) noexcept;
void TestPins(TestPinFn) noexcept;
bool TestPinHash(Reader,std::uintptr_t,const end::Pin&) noexcept;
#endif
} // namespace mgscoop::partner_upper_manual_idle
