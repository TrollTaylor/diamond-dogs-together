#include "mgscoop/partner_upper_probe.hpp"
#include "mgscoop/partner_receipt.hpp"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#if defined(MGS_UPPER_PROBE_TESTING)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <thread>
#include <atomic>
#endif
namespace u = mgscoop::partner_upper_probe;
static unsigned checks = 0, failures = 0;
static void Check(bool ok, const char* name) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", name); } }
static bool Is(const char* value, const char* expected) { return value && std::strcmp(value, expected) == 0; }
#if defined(MGS_UPPER_PROBE_TESTING)
static unsigned forwarded = 0;
static unsigned observations = 0;
static bool clear_refused = false;
static void ObserveLog(const char* line) noexcept {
    ++observations;
    Check(std::strstr(line, "upper_probe=submit;") && std::strstr(line, "observe_only=1"), "bounded observation emitted");
    if (std::strstr(line, "verdict=generation")) clear_refused = true;
    SetLastError(999); // Logging/API work must not leak into the original.
}
static std::uint32_t FloatBits(float value) {
    std::uint32_t bits = 0; std::memcpy(&bits, &value, sizeof(bits)); return bits;
}
static float FloatFromBits(std::uint32_t bits) {
    float value = 0; std::memcpy(&value, &bits, sizeof(value)); return value;
}
static std::uintptr_t Fake170(void* p, std::uint32_t id, std::uint32_t channel, std::uint64_t state, float blend, std::uint32_t option) {
    ++forwarded;
    Check(GetLastError() == 123, "170 forwards incoming LastError");
    Check(p == reinterpret_cast<void*>(0x1234) && id == 1 && channel == 0 && state == 0xaa0ea22a5e0e && blend == 0.25f && option == 77,
          "170 forwards every argument without channel rewrite");
    SetLastError(456);
    return 0xabcdef;
}
static std::uintptr_t Fake190(void* p, std::uint32_t id, std::uint32_t channel, std::uint64_t state, float blend) {
    ++forwarded;
    Check(GetLastError() == 123, "190 forwards incoming LastError");
    Check(p == reinterpret_cast<void*>(0x5678) && id == 0 && channel == 2 && state == 0x789daebcd0a && blend == 0.5f,
          "190 local/natural channel2 arguments unchanged");
    SetLastError(456);
    return 0xfedcba;
}
static std::uintptr_t FakeConsume(void* p, std::uint32_t id, std::uint64_t state, std::uint64_t file) {
    ++forwarded;
    Check(GetLastError() == 123, "consumer forwards incoming LastError");
    Check(p == reinterpret_cast<void*>(0x3456) && id == 1 && state == 0x81582ffa24b9 && file == u::RequestCode(29),
          "consumer forwards primitive/id/state/file unchanged");
    SetLastError(456);
    return 0x112233;
}
static std::uintptr_t Wide170(void* p, std::uint32_t id, std::uint32_t channel, std::uint64_t state, float blend, std::uint32_t option) {
    ++forwarded;
    Check(GetLastError() == 123, "foreign 170 incoming LastError");
    Check(p == reinterpret_cast<void*>(UINTPTR_MAX) && id == UINT32_MAX && channel == UINT32_MAX &&
          state == UINT64_MAX && FloatBits(blend) == 0x7fc01234 && option == UINT32_MAX,
          "refused/foreign 170 retains all bits including NaN blend");
    SetLastError(789); return UINTPTR_MAX;
}
static std::uintptr_t Wide190(void* p, std::uint32_t id, std::uint32_t channel, std::uint64_t state, float blend) {
    ++forwarded;
    Check(GetLastError() == 123, "foreign 190 incoming LastError");
    Check(p == nullptr && id == 0 && channel == 2 && state == 0 && FloatBits(blend) == 0x80000000,
          "natural channel2/190 retains negative zero blend");
    SetLastError(789); return UINTPTR_MAX - 1;
}
static std::uintptr_t WideConsume(void* p, std::uint32_t id, std::uint64_t state, std::uint64_t file) {
    ++forwarded;
    Check(GetLastError() == 123, "foreign consumer incoming LastError");
    Check(p == nullptr && id == UINT32_MAX && state == UINT64_MAX && file == UINT64_MAX,
          "refused/foreign consumer retains every bit");
    SetLastError(789); return UINTPTR_MAX - 2;
}
#if defined(MGS_LOCAL_UPPER_CHANNEL2) && MGS_LOCAL_UPPER_CHANNEL2
static std::uint32_t upper_expected_channel = 2;
#if defined(MGS_LOCAL_UPPER_FIRE_ZERO_BLEND) && MGS_LOCAL_UPPER_FIRE_ZERO_BLEND
static constexpr std::uint32_t upper_input_blend_bits = 0x41000000u, upper_input_option = 0;
#else
static constexpr std::uint32_t upper_input_blend_bits = 0x7fc01234u, upper_input_option = UINT32_MAX;
#endif
static unsigned upper_forwarded = 0, upper_logs = 0;
static bool upper_block = false;
static std::atomic<bool> native_entered{false}, clear_started{false}, clear_finished{false};
static void UpperLog(const char*) noexcept { ++upper_logs; SetLastError(999); }
static std::uintptr_t Upper170(void* p, std::uint32_t id, std::uint32_t channel, std::uint64_t state, float blend, std::uint32_t option) {
    ++upper_forwarded;
    Check(GetLastError() == 123, "upper170 preserves incoming LastError despite guard/logger work");
    Check(p == reinterpret_cast<void*>(0x1234) && id == 1 && channel == upper_expected_channel &&
          state == 0x91a5b8ac42f8 &&
          FloatBits(blend) == (upper_expected_channel == 2 && u::ZeroBlendCompiled() ? 0u : upper_input_blend_bits) &&
          option == upper_input_option,
          "upper170 retains arguments except the separately admitted channel/blend trial");
    if (upper_block) {
        native_entered.store(true, std::memory_order_release);
        const auto deadline = GetTickCount64() + 5000;
        while (u::TestCurrentGeneration() != 0 && GetTickCount64() < deadline) Sleep(0);
        Check(clear_started.load(std::memory_order_acquire) && u::TestCurrentGeneration() == 0,
              "concurrent Clear invalidated generation while original submission active");
        Check(!clear_finished.load(std::memory_order_acquire), "lifecycle Clear waits until original method returns");
    }
    SetLastError(789); return UINTPTR_MAX;
}
#if defined(MGS_LOCAL_UPPER_FIRE_ZERO_BLEND) && MGS_LOCAL_UPPER_FIRE_ZERO_BLEND
struct Seen170 {
    void* cs = nullptr; std::uint32_t id = 0, channel = 0, blend = 0, option = 0, error = 0, calls = 0;
    std::uint64_t state = 0;
};
static Seen170 seen170;
static std::uintptr_t Record170(void* cs, std::uint32_t id, std::uint32_t channel,
                                std::uint64_t state, float blend, std::uint32_t option) {
    seen170 = {cs, id, channel, FloatBits(blend), option, GetLastError(), seen170.calls + 1, state};
    SetLastError(789); return UINTPTR_MAX - 7;
}
#endif
static std::uintptr_t Upper190(void* p, std::uint32_t id, std::uint32_t channel, std::uint64_t state, float blend) {
    ++upper_forwarded;
    Check(GetLastError() == 123, "upper190 preserves incoming LastError");
    Check(p == reinterpret_cast<void*>(0x1234) && id == 1 && channel == upper_expected_channel &&
          state == 0x91a5b8ac42f8 && FloatBits(blend) == 0x80000000,
          "refused upper190 preserves actual method and every argument bit");
    SetLastError(789); return UINTPTR_MAX - 1;
}
#endif
#endif
int main() {
    u::Token t{};
    t.generation = 7; t.armed_ms = 1000; t.mission = 30010; t.owner_thread = 42; t.gani = 21;
    t.P = 1; t.Q = 2; t.block = 3; t.primitive = 4; t.record = 5; t.cs = 6;
    const std::uintptr_t base = 0x140000000;
    u::Consumer c{t.primitive, 1, t.owner_thread, u::RequestCode(21), 0, base + 0x1198d81};
    Check(!u::ConsumerRefusal(t, c, 1000, 7, base), "known named ghost request");
    Check(!u::ConsumerRefusal(t, c, 3000, 7, base), "bounded lifetime endpoint");
    Check(Is(u::ConsumerRefusal(t, c, 3001, 7, base), "expired"), "expired request");
    Check(Is(u::ConsumerRefusal(t, c, 999, 7, base), "expired"), "clock rollback");
    Check(Is(u::ConsumerRefusal(t, c, 1000, 8, base), "generation"), "old mission generation");
    Check(Is(u::ConsumerRefusal(t, c, 1000, 0, base), "generation"), "clear suppresses observation");
    auto other = c;
    other.thread = 43; Check(!u::ConsumerRefusal(t, other, 1000, 7, base), "known natural worker consumer does not require Lua owner thread");
    other.thread = 0; Check(Is(u::ConsumerRefusal(t, other, 1000, 7, base), "thread"), "consumer thread must be actually observed");
    other = c; other.caller = base + 0x1198d22;
    Check(!u::ConsumerRefusal(t, other, 1000, 7, base), "first exact natural dispatcher call");
    other.caller += 1;
    Check(Is(u::ConsumerRefusal(t, other, 1000, 7, base), "consumer_caller"), "first dispatcher return site exact");
    other = c; other.caller += 1;
    Check(Is(u::ConsumerRefusal(t, other, 1000, 7, base), "consumer_caller"), "second dispatcher return site exact");
    other = c; other.caller = base + 0x11991e4;
    Check(Is(u::ConsumerRefusal(t, other, 1000, 7, base), "consumer_caller"), "CS caller cannot substitute dispatcher caller");
    other = c; other.body_id = 0; Check(Is(u::ConsumerRefusal(t, other, 1000, 7, base), "body_id"), "local body excluded");
    other = c; other.primitive = 9; Check(Is(u::ConsumerRefusal(t, other, 1000, 7, base), "primitive"), "foreign DM primitive excluded");
    other = c; other.state ^= 1; Check(Is(u::ConsumerRefusal(t, other, 1000, 7, base), "named_request"), "named hash exact");
    other = c; other.file = 1; Check(Is(u::ConsumerRefusal(t, other, 1000, 7, base), "named_request"), "named request cannot carry a file");
    auto missing = t; missing.cs = 0; Check(Is(u::ConsumerRefusal(missing, c, 1000, 7, base), "token"), "no guessed CS");
    for (auto mission : {0u, 1u, 30011u, 40010u, UINT32_MAX}) {
        auto token = t; token.mission = mission;
        Check(Is(u::ConsumerRefusal(token, c, 1000, 7, base), "mission"), "unsupported mission excluded");
    }
    for (auto mission : {30010u, 30020u, 30050u}) {
        auto token = t; token.mission = mission;
        Check(!u::ConsumerRefusal(token, c, 1000, 7, base), "supported mission identity");
    }
    for (unsigned gani = 0; gani <= 31; ++gani) {
        auto token = t; token.gani = static_cast<std::uint8_t>(gani);
        const auto code = u::RequestCode(token.gani);
        if (gani < 21 || gani > 30) { Check(code == 0, "only verified gun probes whitelist"); continue; }
        auto request = c;
        request.state = gani <= 25 ? code : 0x81582ffa24b9;
        request.file = gani <= 25 ? 0 : code;
        Check(!u::ConsumerRefusal(token, request, 1000, 7, base), "every exact manual request accepted for observation");
        if (gani >= 26) {
            request.file = code | u::kFileCodeVariantBit;
            Check(!u::ConsumerRefusal(token, request, 1000, 7, base), "verified FilePtr bit50 variant remains the same whitelisted clip");
            request.file ^= 1;
            Check(Is(u::ConsumerRefusal(token, request, 1000, 7, base), "file_request"), "variant cannot hide a different resource hash");
            request.file = code;
            request.state = 0x73e906957928;
            Check(!u::ConsumerRefusal(token, request, 1000, 7, base), "both verified path-play states");
            request.file ^= 1;
            Check(Is(u::ConsumerRefusal(token, request, 1000, 7, base), "file_request"), "path hash mismatch");
            request.file = code; request.state = u::RequestCode(21);
            Check(Is(u::ConsumerRefusal(token, request, 1000, 7, base), "path_state"), "named hash is not a path-play state");
        }
    }
    u::Submission s{t.cs, base + 0x11991e4, 1, 0, 42, c.state, false};
    Check(!u::SubmissionRefusal(t, c, s, base), "exact CS170 DM caller");
    auto changed = s; changed.cs = 9; Check(Is(u::SubmissionRefusal(t, c, changed, base), "cs"), "foreign CS");
    changed = s; changed.body_id = 0; Check(Is(u::SubmissionRefusal(t, c, changed, base), "body_id"), "local CS submission");
    changed = s; changed.channel = 2; Check(Is(u::SubmissionRefusal(t, c, changed, base), "channel"), "natural channel2 is untouched");
    changed = s; changed.thread = 43; Check(Is(u::SubmissionRefusal(t, c, changed, base), "thread"), "TLS cannot authorize other thread");
    changed = s; changed.state ^= 1; Check(Is(u::SubmissionRefusal(t, c, changed, base), "state"), "submission equals current consumer");
    changed = s; changed.caller += 1; Check(Is(u::SubmissionRefusal(t, c, changed, base), "caller"), "wrong callsite");
    changed = s; changed.slot190 = true; changed.caller = base + 0x11991a6;
    Check(!u::SubmissionRefusal(t, c, changed, base), "exact observed slot190 caller is guarded without inferred graph classification");
    auto path = t; path.gani = 29;
    auto path_consumer = c; path_consumer.state = 0x81582ffa24b9; path_consumer.file = u::RequestCode(path.gani);
    changed.state = path_consumer.state;
    Check(!u::SubmissionRefusal(path, path_consumer, changed, base), "exact first CS190 path caller");
    changed.caller = base + 0x11991ce; Check(!u::SubmissionRefusal(path, path_consumer, changed, base), "exact second CS190 path caller");
    changed.caller = base + 0x11991e4; Check(Is(u::SubmissionRefusal(path, path_consumer, changed, base), "caller"), "CS170 caller cannot substitute for190");
    changed.slot190 = false;
    Check(!u::SubmissionRefusal(path, path_consumer, changed, base), "live path29 slot170 caller is valid");
    Check(Is(u::SubmissionRefusal(t, c, s, 0), "image"), "no guessed image base");
    Check(Is(u::SubmissionRefusal(t, c, s, UINTPTR_MAX), "image"), "caller RVA addition cannot wrap");
    other = c; other.thread = 43;
    Check(Is(u::SubmissionRefusal(t, other, s, base), "thread"), "Lua owner cannot substitute for current worker consumer");
    changed = s; changed.thread = other.thread;
    Check(!u::SubmissionRefusal(t, other, changed, base), "worker CS submission is bound to actual consumer thread");
    changed.thread = 44;
    Check(Is(u::SubmissionRefusal(t, other, changed, base), "thread"), "another worker cannot use this consumer TLS context");
    Check(t.owner_thread == 42 && other.thread == 43, "worker policy leaves owner-issued token immutable");
    other = c; other.file = 1;
    Check(Is(u::SubmissionRefusal(t, other, s, base), "named_request"), "submission requires valid matched consumer payload");
    for (std::uint32_t channel = 1; channel <= 255; ++channel) {
        changed = s; changed.channel = channel;
        Check(Is(u::SubmissionRefusal(t, c, changed, base), "channel"), "all nonzero channels remain excluded");
    }
    // No policy takes an output channel reference: even accepted observations
    // retain the original channel0. This fixture is not live hook/thread proof.
    Check(s.channel == 0 && c.body_id == 1, "validation does not mutate observed inputs");
    u::MotionCapacity capacity{};
    capacity.cs = t.cs; capacity.entries = 0x10000; capacity.entry = 0x100c0;
    capacity.entry_count = 2; capacity.base_id = 0; capacity.channel_count = 3;
    capacity.allocation = 0x20000; capacity.controllers = 0x20000;
    capacity.buffer = 0x20020; capacity.records = 0x20040;
    capacity.stride = 0x28; capacity.extra_count = 0;
    capacity.controller2 = 0x30000; capacity.controller2_vtable = base + 0x22f0000;
    capacity.storage_writable = true;
    Check(!u::CapacityRefusal(t, capacity, base), "third constructed channel capacity is observable");
    auto bad_capacity = capacity; bad_capacity.cs += 1;
    Check(Is(u::CapacityRefusal(t, bad_capacity, base), "cs"), "capacity cannot substitute a foreign CS");
    for (auto count : {0u, 1u, 2u, 17u, UINT32_MAX}) {
        bad_capacity = capacity; bad_capacity.channel_count = count;
        Check(Is(u::CapacityRefusal(t, bad_capacity, base), "channel_count"), "channel2 requires allocated count and 16-bit mask bounds");
    }
    for (std::uint32_t count = 3; count <= 16; ++count) {
        auto valid = capacity; valid.channel_count = count;
        valid.buffer = valid.controllers + ((count * 8 + 15) & ~std::uintptr_t(15));
        valid.records = valid.buffer + 0x20;
        Check(!u::CapacityRefusal(t, valid, base), "all supported allocated counts respect constructor alignment");
        for (unsigned extras : {0u, 1u, 7u, 255u}) {
            valid.extra_count = static_cast<std::uint8_t>(extras); valid.stride = 0x28 + extras * 24;
            Check(!u::CapacityRefusal(t, valid, base), "extra arrays use exact constructor stride");
            ++valid.stride;
            Check(Is(u::CapacityRefusal(t, valid, base), "record_stride"), "record stride cannot exceed its constructed extent");
        }
    }
    bad_capacity = capacity; bad_capacity.entry_count = 1;
    Check(Is(u::CapacityRefusal(t, bad_capacity, base), "entry_id"), "ghost entry must exist");
    bad_capacity = capacity; bad_capacity.base_id = 2;
    Check(Is(u::CapacityRefusal(t, bad_capacity, base), "entry_id"), "body subtraction cannot underflow");
    bad_capacity = capacity; bad_capacity.base_id = 1; bad_capacity.entry = capacity.entries;
    Check(!u::CapacityRefusal(t, bad_capacity, base), "one-based CS with exact entry is supported");
    bad_capacity = capacity; ++bad_capacity.entry;
    Check(Is(u::CapacityRefusal(t, bad_capacity, base), "entry"), "CS entry stride is exact");
    bad_capacity = capacity; bad_capacity.entries = UINTPTR_MAX;
    Check(Is(u::CapacityRefusal(t, bad_capacity, base), "entry"), "CS entry arithmetic cannot wrap");
    bad_capacity = capacity; bad_capacity.allocation = capacity.controllers + 1;
    Check(Is(u::CapacityRefusal(t, bad_capacity, base), "allocation"), "controller storage belongs after allocation base");
    bad_capacity = capacity; ++bad_capacity.buffer;
    Check(Is(u::CapacityRefusal(t, bad_capacity, base), "buffer_layout"), "request buffer follows aligned controller pointer array");
    bad_capacity = capacity; bad_capacity.controllers = UINTPTR_MAX;
    Check(Is(u::CapacityRefusal(t, bad_capacity, base), "buffer_layout"), "buffer arithmetic cannot wrap");
    bad_capacity = capacity; ++bad_capacity.records;
    Check(Is(u::CapacityRefusal(t, bad_capacity, base), "record_layout"), "request records follow 32-byte header reservation");
    bad_capacity = capacity; bad_capacity.controller2 = 0;
    Check(Is(u::CapacityRefusal(t, bad_capacity, base), "controller2"), "third channel must have its constructed controller");
    bad_capacity = capacity; bad_capacity.controller2_vtable = base - 1;
    Check(Is(u::CapacityRefusal(t, bad_capacity, base), "controller2_vtable"), "controller vtable must belong to exact game image");
    bad_capacity = capacity; bad_capacity.controller2_vtable = base + 0xa01d000;
    Check(Is(u::CapacityRefusal(t, bad_capacity, base), "controller2_vtable"), "controller vtable upper bound is exclusive");
    Check(Is(u::CapacityRefusal(t, capacity, UINTPTR_MAX), "controller2_vtable"), "controller image arithmetic cannot wrap");
    bad_capacity = capacity; bad_capacity.storage_writable = false;
    Check(Is(u::CapacityRefusal(t, bad_capacity, base), "storage"), "readable-only storage cannot support a future write");
    Check(capacity.channel_count == 3 && capacity.records == 0x20040, "capacity observation never mutates storage description");
    auto finite = t; finite.gani = 24; finite.manual_channel2 = true;
    auto finite_consumer = c; finite_consumer.state = 0x91a5b8ac42f8;
    auto finite_capacity = capacity; finite_capacity.controller2_vtable = base + 0x250ba50;
    auto finite_submission = s; finite_submission.state = finite_consumer.state;
    Check(u::kManualChannel2Gani == 24 && u::kManualChannel2State == 0x91a5b8ac42f8, "prototype declares one exact named upper state");
    Check(!u::RewriteRefusal(finite, finite_consumer, finite_submission, finite_capacity, 1000, 7, base, false),
          "only exact manually admitted finite gun request can select constructed channel2");
    Check(Is(u::RewriteRefusal(finite, finite_consumer, finite_submission, finite_capacity, 1000, 7, base, true), "already_submitted"),
          "one upper submission limit per request generation");
    auto no_manual = finite; no_manual.manual_channel2 = false;
    Check(Is(u::RewriteRefusal(no_manual, finite_consumer, finite_submission, finite_capacity, 1000, 7, base, false), "manual_off"),
          "observation tokens cannot authorize a rewrite");
    for (unsigned gani = 21; gani <= 30; ++gani) {
        if (gani == 24) continue;
        auto no_finite = finite; no_finite.gani = static_cast<std::uint8_t>(gani);
        Check(Is(u::RewriteRefusal(no_finite, finite_consumer, finite_submission, finite_capacity, 1000, 7, base, false), "manual_fire_only"),
              "loop and all other gun probes remain observation only");
    }
    Check(Is(u::RewriteRefusal(finite, finite_consumer, finite_submission, finite_capacity, 3001, 7, base, false), "expired"),
          "upper permission expires before actual submission");
    Check(Is(u::RewriteRefusal(finite, finite_consumer, finite_submission, finite_capacity, 1000, 0, base, false), "generation"),
          "lifecycle clear revokes upper permission");
    bad_capacity = finite_capacity; bad_capacity.controller2_vtable = base + 0x250ba58;
    Check(Is(u::RewriteRefusal(finite, finite_consumer, finite_submission, bad_capacity, 1000, 7, base, false), "controller2_kind"),
          "upper rewrite requires exact independently observed controller implementation");
    bad_capacity = finite_capacity; bad_capacity.channel_count = 2;
    Check(Is(u::RewriteRefusal(finite, finite_consumer, finite_submission, bad_capacity, 1000, 7, base, false), "channel_count"),
          "capacity refusal always suppresses upper rewrite");
    auto wrong_slot = finite_submission; wrong_slot.slot190 = true; wrong_slot.caller = base + 0x11991a6;
    Check(Is(u::RewriteRefusal(finite, finite_consumer, wrong_slot, finite_capacity, 1000, 7, base, false), "named_fire_slot"),
          "named Fire prototype requires the exact natural170 method");
    auto wrong_named = finite_consumer; wrong_named.file = u::RequestCode(29);
    Check(Is(u::RewriteRefusal(finite, wrong_named, finite_submission, finite_capacity, 1000, 7, base, false), "named_request"),
          "named upper prototype never accepts path file payload");
    auto path_manual = path; path_manual.manual_channel2 = true;
    Check(Is(u::RewriteRefusal(path_manual, path_consumer, changed, finite_capacity, 1000, 7, base, false), "manual_fire_only"),
          "H2 generic path29 remains observation only even with a synthetic manual token");
    using Trigger = mgscoop::partner::Trigger;
    Trigger manual_trigger{}; manual_trigger.dmplay = true; manual_trigger.dmplay_gani = 29;
    Check(u::IsManualPlayTrigger(manual_trigger), "pure manual gun request may retain only read-only capacity evidence");
    Check(!u::IsManualPlayTrigger(Trigger{}), "empty trigger always takes full invalidation");
    auto nongun = manual_trigger; nongun.dmplay_gani = 0;
    Check(!u::IsManualPlayTrigger(nongun), "unrelated motion request invalidates capacity proof");
    bool Trigger::* const invalidators[] = {
        &Trigger::stop, &Trigger::has_stage, &Trigger::has_source, &Trigger::has_apply,
        &Trigger::has_anim, &Trigger::has_aset, &Trigger::has_reset, &Trigger::has_look,
        &Trigger::has_probe, &Trigger::census, &Trigger::parts_explicit, &Trigger::mirror_on,
        &Trigger::has_input, &Trigger::has_stick, &Trigger::tap, &Trigger::press,
        &Trigger::has_cam, &Trigger::has_hold, &Trigger::has_yaw, &Trigger::has_words,
        &Trigger::has_bits, &Trigger::has_correct, &Trigger::has_isend, &Trigger::has_p218,
        &Trigger::has_padprobe, &Trigger::has_holdlog, &Trigger::has_dm, &Trigger::dmstop,
        &Trigger::has_dmmap, &Trigger::has_dmspeed, &Trigger::has_lookgate, &Trigger::has_lookrecover,
        &Trigger::has_recon, &Trigger::has_align, &Trigger::has_slot, &Trigger::has_runaway,
        &Trigger::has_wrap, &Trigger::place};
    for (auto flag : invalidators) {
        auto combined = manual_trigger; combined.*flag = true;
        Check(!u::IsManualPlayTrigger(combined), "each combined trigger setting/action revokes capacity proof");
    }
#if defined(MGS_UPPER_PROBE_TESTING)
    u::TestOriginals(FakeConsume, Fake170, Fake190);
    SetLastError(123);
    Check(u::TestConsume(reinterpret_cast<void*>(0x3456), 1, 0x81582ffa24b9, u::RequestCode(29)) == 0x112233, "consumer RAX forwarded");
    Check(GetLastError() == 456, "consumer original LastError preserved");
    SetLastError(123);
    Check(u::Test170(reinterpret_cast<void*>(0x1234), 1, 0, 0xaa0ea22a5e0e, 0.25f, 77) == 0xabcdef, "170 RAX forwarded");
    Check(GetLastError() == 456, "170 original LastError preserved");
    SetLastError(123);
    Check(u::Test190(reinterpret_cast<void*>(0x5678), 0, 2, 0x789daebcd0a, 0.5f) == 0xfedcba, "190 RAX forwarded");
    Check(GetLastError() == 456, "190 original LastError preserved");
    Check(forwarded == 3, "each original called once");
    u::TestOriginals(WideConsume, Wide170, Wide190);
    SetLastError(123);
    Check(u::TestConsume(nullptr, UINT32_MAX, UINT64_MAX, UINT64_MAX) == UINTPTR_MAX - 2, "foreign consumer full RAX bits");
    Check(GetLastError() == 789, "foreign consumer original LastError");
    SetLastError(123);
    Check(u::Test170(reinterpret_cast<void*>(UINTPTR_MAX), UINT32_MAX, UINT32_MAX, UINT64_MAX,
                     FloatFromBits(0x7fc01234), UINT32_MAX) == UINTPTR_MAX, "foreign 170 full RAX bits");
    Check(GetLastError() == 789, "foreign 170 original LastError");
    SetLastError(123);
    Check(u::Test190(nullptr, 0, 2, 0, FloatFromBits(0x80000000)) == UINTPTR_MAX - 1, "natural 190 full RAX bits");
    Check(GetLastError() == 789, "natural 190 original LastError");
    Check(forwarded == 6, "all accepted/foreign wrapper examples forward once");
    u::TestOriginals(FakeConsume, Fake170, Fake190);
    auto actual_consumer = c; actual_consumer.thread = GetCurrentThreadId();
    Check(u::TestObservation(t, actual_consumer, base, ObserveLog), "fixture arms only pure-valid observed-thread TLS context");
    SetLastError(123);
    Check(u::Test170(reinterpret_cast<void*>(0x1234), 1, 0, 0xaa0ea22a5e0e, 0.25f, 77) == 0xabcdef,
          "active observation preserves original RAX");
    Check(GetLastError() == 456, "active observation preserves original LastError");
    Check(observations == 1, "active logger ran before the original");
    std::thread unrelated_worker([] {
        SetLastError(123);
        Check(u::Test170(reinterpret_cast<void*>(0x1234), 1, 0, 0xaa0ea22a5e0e, 0.25f, 77) == 0xabcdef,
              "unrelated worker forwards original RAX");
        Check(GetLastError() == 456, "unrelated worker forwards original LastError");
    });
    unrelated_worker.join();
    Check(observations == 1, "actual second thread cannot inherit consumer TLS context");
    u::Clear("fixture_cleanup");
    SetLastError(123);
    Check(u::Test170(reinterpret_cast<void*>(0x1234), 1, 0, 0xaa0ea22a5e0e, 0.25f, 77) == 0xabcdef,
          "cleared observation still forwards once");
    Check(GetLastError() == 456 && clear_refused && observations == 2, "clear invalidates accepted TLS generation");
    u::TestEndObservation();
    Check(forwarded == 9, "observation, unrelated worker and cleanup did not duplicate originals");
    Check(!u::Installed(), "fixture never installed real hooks");
#if defined(MGS_LOCAL_UPPER_CHANNEL2) && MGS_LOCAL_UPPER_CHANNEL2
    Check(u::RewriteCompiled(), "separate explicit fixture option enables only finite manual prototype");
    finite.cs = 0x1234; finite.armed_ms = GetTickCount64();
    finite_capacity.cs = finite.cs;
    auto upper_consumer = finite_consumer; upper_consumer.thread = GetCurrentThreadId();
    u::TestOriginals(FakeConsume, Upper170, Upper190);
    Check(u::TestRewrite(finite, upper_consumer, finite_capacity, base, UpperLog), "fixture provides isolated fake identity/capacity with real token guards");
    SetLastError(123);
    Check(u::Test170(reinterpret_cast<void*>(0x1234), 1, 0, 0x91a5b8ac42f8,
                     FloatFromBits(upper_input_blend_bits), upper_input_option) == UINTPTR_MAX, "accepted finite170 forwards full RAX");
    Check(GetLastError() == 789, "accepted finite170 preserves original LastError after lease release");
    upper_expected_channel = 0;
    SetLastError(123);
    Check(u::Test170(reinterpret_cast<void*>(0x1234), 1, 0, 0x91a5b8ac42f8,
                     FloatFromBits(upper_input_blend_bits), upper_input_option) == UINTPTR_MAX, "second submission forwards original without another upper write");
    u::TestEndObservation();
    upper_expected_channel = 0;
    Check(u::TestRewrite(finite, upper_consumer, finite_capacity, base, UpperLog), "alternate-method fixture is isolated");
    SetLastError(123);
    Check(u::Test190(reinterpret_cast<void*>(0x1234), 1, 0, 0x91a5b8ac42f8,
                     FloatFromBits(0x80000000)) == UINTPTR_MAX - 1, "refused named190 forwards full RAX without changing channel");
    Check(GetLastError() == 789, "refused named190 preserves negative zero and original LastError");
    upper_expected_channel = 2;
    SetLastError(123);
    Check(u::Test170(reinterpret_cast<void*>(0x1234), 1, 0, 0x91a5b8ac42f8,
                     FloatFromBits(upper_input_blend_bits), upper_input_option) == UINTPTR_MAX, "refused190 did not consume the exact170 upper claim");
    u::TestEndObservation();
    upper_expected_channel = 0;
    Check(u::TestRewrite(finite, upper_consumer, finite_capacity, base, UpperLog), "wrong-caller fixture is isolated");
    u::TestRewriteCaller(base + 0x11991e5);
    SetLastError(123);
    Check(u::Test170(reinterpret_cast<void*>(0x1234), 1, 0, 0x91a5b8ac42f8,
                     FloatFromBits(upper_input_blend_bits), upper_input_option) == UINTPTR_MAX, "foreign caller retains channel0 and full original arguments");
    u::TestEndObservation();
    Check(u::TestRewrite(finite, upper_consumer, finite_capacity, base, UpperLog), "identity fixture is isolated");
    u::TestRewriteIdentity(false);
    SetLastError(123);
    Check(u::Test170(reinterpret_cast<void*>(0x1234), 1, 0, 0x91a5b8ac42f8,
                     FloatFromBits(upper_input_blend_bits), upper_input_option) == UINTPTR_MAX, "changed identity refuses upper and forwards original");
    u::TestEndObservation();
    bad_capacity = finite_capacity; bad_capacity.channel_count = 2;
    Check(u::TestRewrite(finite, upper_consumer, bad_capacity, base, UpperLog), "undersized storage fixture enters only fake context");
    SetLastError(123);
    Check(u::Test170(reinterpret_cast<void*>(0x1234), 1, 0, 0x91a5b8ac42f8,
                     FloatFromBits(upper_input_blend_bits), upper_input_option) == UINTPTR_MAX, "undersized storage never selects channel2");
    u::TestEndObservation();
    Check(u::TestRewrite(finite, upper_consumer, finite_capacity, base, UpperLog), "capacity-change fixture is isolated");
    auto resized_capacity = finite_capacity; resized_capacity.channel_count = 4;
    u::TestRewriteCapacity(resized_capacity);
    SetLastError(123);
    Check(u::Test170(reinterpret_cast<void*>(0x1234), 1, 0, 0x91a5b8ac42f8,
                     FloatFromBits(upper_input_blend_bits), upper_input_option) == UINTPTR_MAX, "valid but changed capacity cannot replace the earlier immutable proof");
    u::TestRewriteCapacity(finite_capacity); upper_expected_channel = 2;
    SetLastError(123);
    Check(u::Test170(reinterpret_cast<void*>(0x1234), 1, 0, 0x91a5b8ac42f8,
                     FloatFromBits(upper_input_blend_bits), upper_input_option) == UINTPTR_MAX, "refused submission did not spend the one upper claim");
    u::TestEndObservation(); upper_expected_channel = 0;
    Check(u::TestRewrite(finite, upper_consumer, finite_capacity, base, UpperLog), "unrelated-worker upper fixture is isolated");
    const auto logs_before_worker = upper_logs;
    std::thread foreign_upper_worker([] {
        SetLastError(123);
        Check(u::Test170(reinterpret_cast<void*>(0x1234), 1, 0, 0x91a5b8ac42f8,
                         FloatFromBits(upper_input_blend_bits), upper_input_option) == UINTPTR_MAX, "unrelated actual worker cannot inherit upper permission");
        Check(GetLastError() == 789, "unrelated upper worker retains original LastError");
    });
    foreign_upper_worker.join();
    Check(upper_logs == logs_before_worker, "unrelated worker has no private consumer context or rewrite receipt");
    u::Clear("fixture_upper_stop");
    SetLastError(123);
    Check(u::Test170(reinterpret_cast<void*>(0x1234), 1, 0, 0x91a5b8ac42f8,
                     FloatFromBits(upper_input_blend_bits), upper_input_option) == UINTPTR_MAX, "cleared upper token forwards original channel0");
    u::TestEndObservation();
    upper_expected_channel = 2;
    Check(u::TestRewrite(finite, upper_consumer, finite_capacity, base, nullptr), "lifecycle race fixture is isolated");
    upper_block = true;
    std::thread clearing_worker([] {
        const auto deadline = GetTickCount64() + 5000;
        while (!native_entered.load(std::memory_order_acquire) && GetTickCount64() < deadline) Sleep(0);
        clear_started.store(true, std::memory_order_release);
        u::Clear("fixture_concurrent_lifecycle");
        clear_finished.store(true, std::memory_order_release);
    });
    SetLastError(123);
    const auto leased_result = u::Test170(reinterpret_cast<void*>(0x1234), 1, 0, 0x91a5b8ac42f8,
                                         FloatFromBits(upper_input_blend_bits), upper_input_option);
    const auto leased_error = GetLastError();
    clearing_worker.join();
    Check(leased_result == UINTPTR_MAX && leased_error == 789, "concurrent clear retains full result and original LastError");
    Check(clear_finished.load(std::memory_order_acquire), "Clear completes after accepted original finishes");
    upper_block = false; upper_expected_channel = 0;
    SetLastError(123);
    Check(u::Test170(reinterpret_cast<void*>(0x1234), 1, 0, 0x91a5b8ac42f8,
                     FloatFromBits(upper_input_blend_bits), upper_input_option) == UINTPTR_MAX, "no upper submission remains after Clear returned");
    Check(upper_forwarded == 13, "every upper accepted/refused/lifecycle example invokes one original");
    u::TestEndObservation();
    Check(!u::Installed(), "manual upper fixture never installed or called native game code");
#if defined(MGS_LOCAL_UPPER_FIRE_ZERO_BLEND) && MGS_LOCAL_UPPER_FIRE_ZERO_BLEND
    Check(u::ZeroBlendCompiled(), "separate zero-blend option is compiled only for the manual24 trial");
    u::TestOriginals(FakeConsume, Record170, Upper190);
    auto renewed = [&] {
        u::TestEndObservation(); finite.armed_ms = GetTickCount64();
        Check(u::TestRewrite(finite, upper_consumer, finite_capacity, base, UpperLog), "fresh fake lease retains full existing guards");
    };
    auto receipt = [&](void* cs, std::uint32_t id, std::uint32_t channel, std::uint64_t state,
                       std::uint32_t blend, std::uint32_t option, std::uint32_t expected_channel,
                       std::uint32_t expected_blend, const char* label) {
        const auto prior_calls = seen170.calls; SetLastError(123);
        const auto result = u::Test170(cs, id, channel, state, FloatFromBits(blend), option);
        const auto error = GetLastError();
        Check(result == UINTPTR_MAX - 7 && error == 789 && seen170.calls == prior_calls + 1 && seen170.error == 123,
              "zero-blend/refused forwarding keeps one original, raw RAX and LastError");
        Check(seen170.cs == cs && seen170.id == id && seen170.channel == expected_channel && seen170.state == state &&
              seen170.blend == expected_blend && seen170.option == option, label);
    };
    constexpr auto fire = u::kManualChannel2State;
    void* const cs = reinterpret_cast<void*>(finite.cs);
    renewed();
    receipt(cs, 1, 0, fire, 0x00000000u, 0, 0, 0x00000000u, "input0 is refused; zero output never proves admission");
    receipt(cs, 1, 0, fire, 0x80000000u, 0, 0, 0x80000000u, "negative-zero input is preserved and refused");
    receipt(cs, 1, 0, fire, 0x7fc01234u, 0, 0, 0x7fc01234u, "NaN input payload is preserved and refused");
    receipt(cs, 1, 0, fire, 0x41000000u, 1, 0, 0x41000000u, "nonzero option is preserved and refused");
    receipt(cs, 1, 0, fire, 0x41000000u, 0, 2, 0u, "exact input8 option0 becomes only channel2 blend0 after refusals");
    receipt(cs, 1, 0, fire, 0x41000000u, 0, 0, 0x41000000u, "second submission keeps input8 and channel0");
    renewed(); u::TestRewriteCaller(base + 0x11991e5);
    receipt(cs, 1, 0, fire, 0x41000000u, 0, 0, 0x41000000u, "foreign caller cannot gain blend or channel rewrite");
    renewed();
    receipt(cs, 0, 0, fire, 0x41000000u, 0, 0, 0x41000000u, "local body0 remains bit-exact");
    receipt(cs, 1, 2, fire, 0x41000000u, 0, 2, 0x41000000u, "already natural channel2 remains bit-exact");
    receipt(cs, 1, 0, fire ^ 1, 0x41000000u, 0, 0, 0x41000000u, "foreign state remains bit-exact");
    receipt(reinterpret_cast<void*>(0x4321), 1, 0, fire, 0x41000000u, 0, 0, 0x41000000u, "foreign CS remains bit-exact");
    auto extra_capacity = finite_capacity; extra_capacity.extra_count = 1; extra_capacity.stride = 0x40;
    u::TestEndObservation(); finite.armed_ms = GetTickCount64();
    Check(u::TestRewrite(finite, upper_consumer, extra_capacity, base, UpperLog), "fake extra-record capacity remains isolated");
    receipt(cs, 1, 0, fire, 0x41000000u, 0, 0, 0x41000000u, "extra-record layout cannot receive zero-blend rewrite");
    u::TestEndObservation();
#else
    Check(!u::ZeroBlendCompiled(), "existing manual prototype leaves zero-blend experiment OFF");
#endif
    Check(u::TestRewrite(finite, upper_consumer, finite_capacity, base, nullptr), "manual-trigger cleanup fixture is isolated");
    u::ClearTrigger(manual_trigger);
    Check(u::TestCurrentGeneration() == 0 && u::TestCapacityRecorded(),
          "pure manual trigger revokes request permission while retaining previous read-only capacity proof");
    auto combined_trigger = manual_trigger; combined_trigger.has_input = true;
    u::ClearTrigger(combined_trigger);
    Check(u::TestCurrentGeneration() == 0 && !u::TestCapacityRecorded(),
          "combined input/settings trigger revokes both request and read-only capacity proof");
    u::TestEndObservation();
#else
    Check(!u::RewriteCompiled(), "ordinary observer build cannot rewrite a channel");
    Check(!u::TestRewrite(finite, finite_consumer, finite_capacity, base, nullptr), "ordinary observer fixture cannot authorize upper experiment");
#endif
#else
    Check(!u::Compiled() && !u::Installed(), "default OFF compiles inactive stubs");
    Check(!u::RewriteCompiled(), "default OFF excludes manual channel2 experiment");
    Check(!u::ZeroBlendCompiled(), "default OFF excludes manual zero-blend experiment");
    Check(!u::ArmRequest(7, 30010, 42, 21), "default OFF cannot arm an observation");
    u::Clear("fixture");
#endif
    std::printf("partner upper probe checks: %u, failures: %u\n", checks, failures);
    return failures ? 1 : 0;
}
