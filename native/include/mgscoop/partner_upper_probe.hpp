#pragma once
// Local exact-build direct-motion audit. The default build observes only.
// The separate default-OFF manual experiment admits one named GANI24 request
// on constructed channel2; it never synthesizes a request or native call.
#include <cstddef>
#include <cstdint>
namespace mgscoop::partner { struct Trigger; }

namespace mgscoop::partner_upper_probe {
using LogFn = void (*)(const char*) noexcept;
inline constexpr std::uint64_t kTokenLifetimeMs = 2000;
inline constexpr unsigned kReceiptCap = 64;
inline constexpr std::uint8_t kManualChannel2Gani = 24;
inline constexpr std::uint64_t kManualChannel2State = 0x91a5b8ac42f8;
// 1A09850 preserves the canonical file-code bits and selects only bit 50.
// Its meaning is left opaque; it is never removed from a forwarded argument.
inline constexpr std::uint64_t kFileCodeVariantBit = 0x4000000000000;
struct Authorization {
    bool offline = false, lab_active = false, redirected_saves = false;
};
struct Token {
    std::uint64_t generation = 0, armed_ms = 0;
    std::uint32_t mission = 0, owner_thread = 0;
    std::uint8_t gani = 0;
    std::uintptr_t P = 0, Q = 0, block = 0, primitive = 0, record = 0, cs = 0;
    bool manual_channel2 = false;
};
struct Consumer {
    std::uintptr_t primitive = 0;
    std::uint32_t body_id = 0, thread = 0;
    std::uint64_t state = 0, file = 0;
    // Actual return address into the fingerprinted natural DM dispatcher.
    std::uintptr_t caller = 0;
};
struct Submission {
    std::uintptr_t cs = 0, caller = 0;
    std::uint32_t body_id = 0, channel = 0, thread = 0;
    std::uint64_t state = 0;
    bool slot190 = false;
};
// Read-only shape of the CS entry's allocated channel storage. These fields
// follow the exact B6D980/B6C020 allocation and B6C410 consumer contract.
// A positive capacity receipt does not authorize a channel rewrite.
struct MotionCapacity {
    std::uintptr_t cs = 0, entries = 0, entry = 0, allocation = 0;
    std::uintptr_t controllers = 0, buffer = 0, records = 0;
    std::uintptr_t controller2 = 0, controller2_vtable = 0;
    std::uint32_t entry_count = 0, base_id = 0, channel_count = 0, stride = 0;
    std::uint8_t extra_count = 0;
    bool storage_writable = false;
};
// Pure policies are compiled even when the experimental hook option is OFF.
// A non-null result names the first failed guard. Nothing is rewritten.
const char* ConsumerRefusal(const Token&, const Consumer&, std::uint64_t now,
                            std::uint64_t current_generation, std::uintptr_t image_base) noexcept;
const char* SubmissionRefusal(const Token&, const Consumer&, const Submission&,
                              std::uintptr_t image_base) noexcept;
const char* CapacityRefusal(const Token&, const MotionCapacity&, std::uintptr_t image_base) noexcept;
const char* RewriteRefusal(const Token&, const Consumer&, const Submission&, const MotionCapacity&,
                          std::uint64_t now, std::uint64_t current_generation, std::uintptr_t image_base,
                          bool already_submitted) noexcept;
std::uint64_t RequestCode(std::uint8_t gani) noexcept;
bool Compiled() noexcept;
bool RewriteCompiled() noexcept;
// Separate default-OFF manual24 parameter trial. Exact input blend8/option0
// still qualifies the request; only its admitted original170 receives blend0.
bool ZeroBlendCompiled() noexcept;
// A pure gun-play trigger replaces the old request, retaining only read-only
// capacity evidence. Combined settings/actions take the full lifecycle clear.
bool IsManualPlayTrigger(const partner::Trigger&) noexcept;
void ClearTrigger(const partner::Trigger&) noexcept;
bool Installed() noexcept;
// Install is process-lifetime. All forwarding trampolines are published before
// enabling any hook. Partial failure is inactive and forwards original calls.
bool Install(const void* image_base, std::size_t image_span, Authorization, LogFn) noexcept;
// Called only for an issued manual gun DM reply, on the established natural
// mission thread. Re-reads ReadState/ResolveDm and the exact CS chain itself.
bool ArmRequest(std::uint64_t generation, std::uint32_t mission,
                std::uint32_t owner_thread, std::uint8_t gani, bool request_channel2 = false) noexcept;
// Clear before termination, disable, rearm, un-realize, range hide or relink.
// Hooks remain installed and transparently forward after the token is cleared.
void Clear(const char* reason) noexcept;
#if defined(MGS_UPPER_PROBE_TESTING)
// Fixture-only entry into the real forwarding wrappers, with fake originals.
using TestConsumeFn = std::uintptr_t(*)(void*, std::uint32_t, std::uint64_t, std::uint64_t);
using Test170Fn = std::uintptr_t(*)(void*, std::uint32_t, std::uint32_t, std::uint64_t, float, std::uint32_t);
using Test190Fn = std::uintptr_t(*)(void*, std::uint32_t, std::uint32_t, std::uint64_t, float);
void TestOriginals(TestConsumeFn, Test170Fn, Test190Fn) noexcept;
// Fixture-only TLS context, validated by the pure policy. Never marks hooks
// installed, reads game memory, or enables a native callback.
bool TestObservation(const Token&, const Consumer&, std::uintptr_t image_base, LogFn) noexcept;
// Fake capacity and identity only, without native reads or installed hooks.
// Uses the real wrapper, token lock and once-per-generation claim.
bool TestRewrite(const Token&, const Consumer&, const MotionCapacity&, std::uintptr_t image_base, LogFn) noexcept;
void TestRewriteCaller(std::uintptr_t) noexcept;
void TestRewriteIdentity(bool) noexcept;
void TestRewriteCapacity(const MotionCapacity&) noexcept;
std::uint64_t TestCurrentGeneration() noexcept;
bool TestCapacityRecorded() noexcept;
void TestEndObservation() noexcept;
std::uintptr_t TestConsume(void*, std::uint32_t, std::uint64_t, std::uint64_t);
std::uintptr_t Test170(void*, std::uint32_t, std::uint32_t, std::uint64_t, float, std::uint32_t);
std::uintptr_t Test190(void*, std::uint32_t, std::uint32_t, std::uint64_t, float);
#endif
} // namespace mgscoop::partner_upper_probe
