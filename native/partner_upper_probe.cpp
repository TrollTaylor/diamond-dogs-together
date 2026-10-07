// Original MGS Co-op: exact-build upper-motion audit and finite manual trial.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include "mgscoop/partner_upper_probe.hpp"
#include "mgscoop/partner_upper_end_observer.hpp"
#include "mgscoop/partner_receipt.hpp"

namespace mgscoop::partner_upper_probe {
std::uint64_t RequestCode(std::uint8_t gani) noexcept {
    static constexpr std::uint64_t codes[] = {
        0xaa0ea22a5e0e, 0x50130ae2e584, 0xde97403fc93c, 0x91a5b8ac42f8, 0x0789daebcd0a,
        0xfc530d2a53bf52c1, 0xfc535178a51b64ff, 0xfc53958377e25e9f,
        0xfc5068d7166952e6, 0xfc5130ebf89389d5};
    return gani >= 21 && gani <= 30 ? codes[gani - 21] : 0;
}
bool IsManualPlayTrigger(const partner::Trigger& t) noexcept {
    return t.dmplay && RequestCode(t.dmplay_gani) && !t.stop && !t.has_stage && !t.has_source &&
        !t.has_apply && !t.has_anim && !t.has_aset && !t.has_reset && !t.has_look && !t.has_probe &&
        !t.census && !t.parts_explicit && !t.mirror_on && !t.has_input && !t.has_stick && !t.tap &&
        !t.press && !t.has_cam && !t.has_hold && !t.has_yaw && !t.has_words && !t.has_bits &&
        !t.has_correct && !t.has_isend && !t.has_p218 && !t.has_padprobe && !t.has_holdlog &&
        !t.has_dm && !t.dmstop && !t.has_dmmap && !t.has_dmspeed && !t.has_lookgate &&
        !t.has_lookrecover && !t.has_recon && !t.has_align && !t.has_slot && !t.has_runaway &&
        !t.has_wrap && !t.place;
}
void ClearTrigger(const partner::Trigger& t) noexcept { Clear(IsManualPlayTrigger(t) ? "new_request" : "trigger"); }
const char* ConsumerRefusal(const Token& t, const Consumer& c, std::uint64_t now,
                           std::uint64_t current_generation, std::uintptr_t base) noexcept {
    if (!t.generation || t.generation != current_generation) return "generation";
    if (t.mission != 30010 && t.mission != 30020 && t.mission != 30050) return "mission";
    if (!t.owner_thread || !t.P || !t.Q || !t.block || !t.primitive || !t.record || !t.cs) return "token";
    if (now < t.armed_ms || now - t.armed_ms > kTokenLifetimeMs) return "expired";
    if (!RequestCode(t.gani)) return "gani";
    if (!base || base > UINTPTR_MAX - 0x11991e4) return "image";
    // Token publication remains restricted to the established Lua owner.
    // The game may consume it on that owner or a natural action worker.
    // These are the only two direct calls in pinned dispatcher 1198CC0.
    if (c.caller != base + 0x1198d22 && c.caller != base + 0x1198d81) return "consumer_caller";
    if (!c.thread) return "thread";
    if (c.body_id != 1) return "body_id";
    if (c.primitive != t.primitive) return "primitive";
    if (t.gani <= 25) {
        if (c.file != 0 || c.state != RequestCode(t.gani)) return "named_request";
    } else {
        // FilePtr is an encoded 64-bit file code, not a pointer, for GANI's
        // extension type 0x1F8A (858D0 only indirects types 0x31C/0xEF).
        // The public path resolver can select the one bit-50 variant through
        // 1A09850. Admit exactly these two forms of each known resource.
        const auto code = RequestCode(t.gani);
        if (c.file != code && c.file != (code | kFileCodeVariantBit)) return "file_request";
        // Exact two predefined path-play states selected by 1198CC0. These
        // are opaque graph identifiers, not an inferred layer or loop flag.
        if (c.state != 0x81582ffa24b9 && c.state != 0x73e906957928) return "path_state";
    }
    return nullptr;
}
const char* SubmissionRefusal(const Token& t, const Consumer& c, const Submission& s,
                             std::uintptr_t base) noexcept {
    // Revalidate the immutable consumer fields here too. A submission is only
    // meaningful inside the matching consumer; public callers cannot authorize
    // it by providing a CS/id/channel tuple alone.
    if (const char* why = ConsumerRefusal(t, c, t.armed_ms, t.generation, base)) return why;
    if (s.thread != c.thread) return "thread";
    if (s.cs != t.cs) return "cs";
    if (s.body_id != c.body_id || s.body_id != 1) return "body_id";
    if (s.channel != 0) return "channel";
    if (s.state != c.state) return "state";
    // 1199140 chooses +190 when the state's low 48 bits equal either of
    // two runtime global identifiers, otherwise +170. GANI/path form does
    // not establish that comparison; actual slot and return site are paired.
    const bool caller = s.slot190 ? s.caller == base + 0x11991a6 || s.caller == base + 0x11991ce
                                  : s.caller == base + 0x11991e4;
    return caller ? nullptr : "caller";
}
const char* CapacityRefusal(const Token& t, const MotionCapacity& c, std::uintptr_t base) noexcept {
    if (!t.cs || c.cs != t.cs) return "cs";
    if (!c.entry_count || c.base_id > 1 || 1 - c.base_id >= c.entry_count) return "entry_id";
    const auto offset = static_cast<std::uintptr_t>(1 - c.base_id) * 0xc0;
    if (!c.entries || c.entries > UINTPTR_MAX - offset || c.entry != c.entries + offset) return "entry";
    // B6E9F0 uses a 16-bit mask and an unchecked channel record index.
    // Require a third constructed record; reject counts beyond mask width.
    if (c.channel_count < 3 || c.channel_count > 16) return "channel_count";
    if (!c.allocation || !c.controllers || c.controllers < c.allocation) return "allocation";
    const auto array_bytes = (static_cast<std::uintptr_t>(c.channel_count) * 8 + 15) & ~std::uintptr_t(15);
    if (c.controllers > UINTPTR_MAX - array_bytes || c.buffer != c.controllers + array_bytes) return "buffer_layout";
    if (c.buffer > UINTPTR_MAX - 0x20 || c.records != c.buffer + 0x20) return "record_layout";
    if (c.stride != 0x28u + 24u * c.extra_count) return "record_stride";
    const auto bytes = static_cast<std::uintptr_t>(c.channel_count) * c.stride;
    if (c.records > UINTPTR_MAX - bytes) return "record_range";
    if (!c.controller2) return "controller2";
    if (!base || base > UINTPTR_MAX - 0xa01d000 || c.controller2_vtable < base ||
        c.controller2_vtable >= base + 0xa01d000) return "controller2_vtable";
    return c.storage_writable ? nullptr : "storage";
}
const char* RewriteRefusal(const Token& t, const Consumer& c, const Submission& s, const MotionCapacity& capacity,
                          std::uint64_t now, std::uint64_t current_generation, std::uintptr_t base,
                          bool already_submitted) noexcept {
    if (!t.manual_channel2) return "manual_off";
    // Only the exact named Fire submission used by natural helper 1059E80
    // is admitted. H2 proved generic path29 is absent from the upper graph.
    // No automatic/loop request or alternate method is authorized.
    if (t.gani != kManualChannel2Gani) return "manual_fire_only";
    if (const char* why = ConsumerRefusal(t, c, now, current_generation, base)) return why;
    if (const char* why = SubmissionRefusal(t, c, s, base)) return why;
    if (c.file != 0 || c.state != kManualChannel2State) return "named_fire_request";
    if (s.slot190) return "named_fire_slot";
    if (const char* why = CapacityRefusal(t, capacity, base)) return why;
    if (capacity.controller2_vtable != base + 0x250ba50) return "controller2_kind";
    return already_submitted ? "already_submitted" : nullptr;
}
} // namespace mgscoop::partner_upper_probe

#if (defined(MGS_LOCAL_UPPER_PROBE) && MGS_LOCAL_UPPER_PROBE) || (defined(MGS_LOCAL_UPPER_END_OBSERVE) && MGS_LOCAL_UPPER_END_OBSERVE)
#include <bcrypt.h>
#include <intrin.h>
#include <strsafe.h>
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include "mgscoop/partner_player.hpp"
#include "mgscoop/partner_dm_patch.hpp"
#include "partner_player_contract.hpp"

namespace mgscoop::partner_upper_probe {
namespace {
namespace pp = partner_player;
namespace pc = partner_player_contract;
namespace end_observe = partner_upper_end_observer;
constexpr char kExeHash[] = "085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45";
struct Body { std::uintptr_t rva; std::size_t size; const char* hash; };
constexpr Body bodies[] = {
    {0x1059e80, 0x3b, "e9f4a40eb66a75621a76c258c20c3158238e964556aa94ce6d6ae4e1fd24cec5"},
    {0x1199140, 0xdc, "45cfdec9e28ec13b48f623fe6814fffca4b56aa63bdc215bb09ecafa4e8d4756"},
    {0x1198cc0, 0x10c, "324f221c7f8fd390e3296ffcaf746f8cca03e11f91fcaf1702e0e12fa27c189b"},
    {0xaf2ba0, 0x8e, "bf4f8d6085f686bad8a6eecd5863a6a8b0b39c0f0df19e3803e6e7f648a709ff"},
    {0xaf3270, 0x86, "c43f632d623f66ee4164a26d69fa52be99df8b869535532e4f0f4cbe86f125de"},
    {0xb6e9f0, 0xa5, "0cbabd97f75b22722107aa37ff97343684ece2e4c76938b4a2448a1dd8b504d1"},
    {0xb6c020, 0xcd, "4bfdeeac99cc3c3521539a4886b4bdeb5feba73592e5bbc6904f07307109d881"},
    {0xb6c410, 0x1f2, "6ffea8164e703db1f24e1e9f0e3f53e0a0a2083d2fdc52cacb1361969e3adf6e"},
    {0xb6d980, 0x928, "f23e0346adf5b58db22d18457b6bbf79b8f1579c976c6959659fc12cd7754909"},
    {0x1c73070, 0xe3, "71979b7c76aa20fc12d98d15a0f170335fb68cb2a0dea20329ef91eeefe302a5"},
    {0x1c71d70, 0x28b, "bbecce69633431adcf94ab1659383d3d2d4ed4c71f2c21006ece977f8e80aecc"}};
// Return bits in RAX are forwarded as-is; the retail callers do not consume a
// semantic return. No floating return exists in these exact instruction paths.
using ConsumeFn = std::uintptr_t(__fastcall*)(void*, std::uint32_t, std::uint64_t, std::uint64_t);
using Submit170Fn = std::uintptr_t(__fastcall*)(void*, std::uint32_t, std::uint32_t, std::uint64_t, float, std::uint32_t);
using Submit190Fn = std::uintptr_t(__fastcall*)(void*, std::uint32_t, std::uint32_t, std::uint64_t, float);
ConsumeFn original_consume = nullptr;
Submit170Fn original170 = nullptr;
Submit190Fn original190 = nullptr;
std::uintptr_t image = 0;
pp::Addresses addresses{};
LogFn logger = nullptr;
SRWLOCK token_lock = SRWLOCK_INIT;
Token published{};
Token capacity_identity{};
MotionCapacity capacity_proof{};
std::atomic<std::uint64_t> generation{0};
std::atomic<std::uint64_t> submitted_generation{0};
std::atomic<std::uint64_t> lifecycle_serial{0};
std::atomic<bool> installed{false};
std::atomic<bool> attempted{false};
std::atomic<unsigned> consume_lines{0}, submit_lines{0}, refusal_lines{0}, rewrite_lines{0};
struct Context { Token token{}; Consumer consumer{}; bool accepted = false; };
thread_local Context context{};
struct RestoreContext { Context saved = context; ~RestoreContext() { context = saved; } };
struct Preserve { DWORD last = GetLastError(); ~Preserve() { SetLastError(last); } };
#if defined(MGS_UPPER_PROBE_TESTING)
struct Fixture { bool rewrite = false, same_identity = true; MotionCapacity capacity{}; std::uintptr_t caller = 0; };
thread_local Fixture fixture{};
#endif
bool EndRead(void*, std::uintptr_t address, void* out, std::size_t size) noexcept {
    SIZE_T copied = 0;
    return address && out && size && address <= UINTPTR_MAX - size &&
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), out, size, &copied) && copied == size;
}
template <class T> bool Read(std::uintptr_t p, T& out) noexcept {
    SIZE_T n = 0;
    return p && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(p), &out, sizeof(out), &n) && n == sizeof(out);
}
template <class T> bool ReadAt(std::uintptr_t p, std::uintptr_t offset, T& out) noexcept {
    return p && p <= UINTPTR_MAX - offset && Read(p + offset, out);
}
bool Writable(std::uintptr_t start, std::size_t bytes) noexcept {
    if (!start || !bytes || start > UINTPTR_MAX - bytes) return false;
    const auto end = start + bytes;
    for (auto p = start; p < end;) {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(reinterpret_cast<const void*>(p), &region, sizeof(region)) != sizeof(region) ||
            region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const DWORD access = region.Protect & 0xff;
        if (access != PAGE_READWRITE && access != PAGE_WRITECOPY && access != PAGE_EXECUTE_READWRITE &&
            access != PAGE_EXECUTE_WRITECOPY) return false;
        const auto b = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if (b > UINTPTR_MAX - region.RegionSize || b + region.RegionSize <= p) return false;
        p = b + region.RegionSize;
    }
    return true;
}
void Emit(const char* line) noexcept { if (logger) logger(line); }
void Refused(const char* phase, const char* reason) noexcept {
    if (refusal_lines.fetch_add(1, std::memory_order_relaxed) >= 16) return;
    char line[160]{};
    if (SUCCEEDED(StringCchPrintfA(line, sizeof(line), "upper_probe=%s_refused; reason=%s\r\n", phase, reason))) Emit(line);
}
bool CopyToken(Token& out) noexcept {
    if (!TryAcquireSRWLockShared(&token_lock)) return false;
    out = published;
    ReleaseSRWLockShared(&token_lock);
    return out.generation != 0;
}
bool Digest(const unsigned char* digest, const char* expected) noexcept {
    static constexpr char alphabet[] = "0123456789abcdef";
    char text[65]{};
    for (unsigned i = 0; i < 32; ++i) { text[2 * i] = alphabet[digest[i] >> 4]; text[2 * i + 1] = alphabet[digest[i] & 15]; }
    return std::memcmp(text, expected, 64) == 0;
}
bool Hash(const void* bytes, std::size_t length, const char* expected, HANDLE file = INVALID_HANDLE_VALUE) noexcept {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char buffer[4096]{}, digest[32]{};
    bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    if (file != INVALID_HANDLE_VALUE) {
        DWORD n = 0;
        while (ok) {
            if (!ReadFile(file, buffer, sizeof(buffer), &n, nullptr)) { ok = false; break; }
            if (!n) break;
            ok = BCryptHashData(hash, buffer, n, 0) >= 0;
        }
    } else {
        for (std::size_t offset = 0; ok && offset < length;) {
            const std::size_t n = length - offset < sizeof(buffer) ? length - offset : sizeof(buffer);
            SIZE_T copied = 0;
            ok = ReadProcessMemory(GetCurrentProcess(), static_cast<const unsigned char*>(bytes) + offset, buffer, n, &copied) && copied == n;
            if (ok) ok = BCryptHashData(hash, buffer, static_cast<ULONG>(n), 0) >= 0;
            offset += n;
        }
    }
    if (ok) ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0 && Digest(digest, expected);
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}
bool Fingerprint(std::uintptr_t base, std::size_t span) noexcept {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (span != 0xa01d000 || !Read(base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 ||
        static_cast<std::size_t>(dos.e_lfanew) > span - sizeof(nt) || !Read(base + dos.e_lfanew, nt) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp != 0x6a4cb898 || nt.OptionalHeader.SizeOfImage != span) return false;
    wchar_t path[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(reinterpret_cast<HMODULE>(base), path, MAX_PATH);
    if (!n || n >= MAX_PATH) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const bool ok = Hash(nullptr, 0, kExeHash, file);
    CloseHandle(file);
    return ok;
}
const char* Snapshot(Token& t) noexcept {
    pp::State state{};
    const pp::Refusal refusal = pp::ReadState(addresses, state);
    if (const char* why = pp::CheckWrite(refusal, state, 1, false, pp::WriteKind::set)) return why;
    if (!pp::Realized(state, 1) || state.ids[1] != 1 || state.parts[1] != pc::parts_loaded_state) return "ghost_parts";
    pp::DmTargets dm{};
    if (const char* why = pp::ResolveDm(addresses, state, dm)) return why;
    pp::DmState live{};
    if (!pp::ReadDm(addresses, state, dm, live) || !live.cam_ok) return "dm_read";
    if (live.q18[0] == pc::q18_dm_act || live.cam1f0 || live.cam200) return "local_dm_camera";
    std::uintptr_t ps = 0, cs = 0, vt = 0;
    if (!Read(dm.blk1 + pc::blk_ps, ps) || !ps || !Read(ps + pc::ps_cs, cs) || !cs ||
        !Read(cs, vt) || vt != addresses.cs_vtable) return "cs_chain";
    t.P = state.P; t.Q = state.Q; t.block = dm.blk1; t.primitive = dm.prim; t.record = dm.rec; t.cs = cs;
    return nullptr;
}
const char* CurrentIdentity(const Token& t) noexcept {
#if defined(MGS_UPPER_PROBE_TESTING)
    if (fixture.rewrite) return fixture.same_identity ? nullptr : "identity_changed";
#endif
    Token live = t;
    if (const char* why = Snapshot(live)) return why;
    return live.P == t.P && live.Q == t.Q && live.block == t.block && live.primitive == t.primitive &&
           live.record == t.record && live.cs == t.cs ? nullptr : "identity_changed";
}
bool SameIdentity(const Token& a, const Token& b) noexcept {
    return a.mission == b.mission && a.owner_thread == b.owner_thread && a.P == b.P && a.Q == b.Q &&
        a.block == b.block && a.primitive == b.primitive && a.record == b.record && a.cs == b.cs;
}
bool SameCapacity(const MotionCapacity& a, const MotionCapacity& b) noexcept {
    return a.cs == b.cs && a.entries == b.entries && a.entry == b.entry && a.allocation == b.allocation &&
        a.controllers == b.controllers && a.buffer == b.buffer && a.records == b.records &&
        a.controller2 == b.controller2 && a.controller2_vtable == b.controller2_vtable &&
        a.entry_count == b.entry_count && a.base_id == b.base_id && a.channel_count == b.channel_count &&
        a.stride == b.stride && a.extra_count == b.extra_count && a.storage_writable && b.storage_writable;
}
const char* ReadCapacity(const Token& t, MotionCapacity& c) noexcept {
#if defined(MGS_UPPER_PROBE_TESTING)
    if (fixture.rewrite) { c = fixture.capacity; return CapacityRefusal(t, c, image); }
#endif
    c.cs = t.cs;
    if (!ReadAt(t.cs, 8, c.entry_count) || !ReadAt(t.cs, 0xc, c.base_id) ||
        !ReadAt(t.cs, 0x10, c.entries)) return "cs_entry_read";
    if (!c.entry_count || c.base_id > 1 || 1 - c.base_id >= c.entry_count) return "entry_id";
    const auto offset = static_cast<std::uintptr_t>(1 - c.base_id) * 0xc0;
    if (!c.entries || c.entries > UINTPTR_MAX - offset) return "entry";
    c.entry = c.entries + offset;
    if (!ReadAt(c.entry, 0x78, c.allocation) || !ReadAt(c.entry, 0x90, c.controllers) ||
        !ReadAt(c.entry, 0x98, c.buffer) || !ReadAt(c.entry, 0xb0, c.channel_count)) return "entry_read";
    if (!ReadAt(c.buffer, 8, c.extra_count) || !ReadAt(c.buffer, 0xc, c.stride) ||
        !ReadAt(c.buffer, 0x10, c.records)) return "buffer_read";
    if (c.channel_count < 3 || c.channel_count > 16) return "channel_count";
    // Read the constructed pointer array in one bounded operation. No virtual
    // method is invoked, and no game object or record is changed.
    std::uintptr_t controllers[16]{};
    SIZE_T copied = 0;
    const auto array_bytes = static_cast<SIZE_T>(c.channel_count) * sizeof(controllers[0]);
    if (!c.controllers || !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(c.controllers),
        controllers, array_bytes, &copied) || copied != array_bytes) return "controllers_read";
    c.controller2 = controllers[2];
    if (!Read(c.controller2, c.controller2_vtable)) return "controller2_read";
    // The count and stride are independently checked by the pure constructor
    // policy. Query only bounded storage and require its present writable state.
    if (c.stride == 0x28u + 24u * c.extra_count) {
        c.storage_writable = Writable(c.buffer, 0x18) &&
            Writable(c.records, static_cast<std::size_t>(c.channel_count) * c.stride);
    }
    return CapacityRefusal(t, c, image);
}
void ObserveCapacity() noexcept {
    // Fixture contexts never read game memory. Runtime context admission still
    // requires the exact consumer, identity and slot/caller provenance.
    if (!Installed()) return;
    MotionCapacity c{};
    const char* why = CurrentIdentity(context.token);
    if (!why) why = ReadCapacity(context.token, c);
    bool recorded = false;
    if (!why && !context.token.manual_channel2 && TryAcquireSRWLockExclusive(&token_lock)) {
        // A lifecycle clear/new arm during reads must not publish stale proof.
        if (generation.load(std::memory_order_acquire) == context.token.generation &&
            published.generation == context.token.generation && SameIdentity(published, context.token) &&
            !ConsumerRefusal(context.token, context.consumer, GetTickCount64(), context.token.generation, image)) {
            capacity_identity = context.token;
            capacity_proof = c;
            recorded = true;
        }
        ReleaseSRWLockExclusive(&token_lock);
    }
    char line[512]{};
    if (SUCCEEDED(StringCchPrintfA(line, sizeof(line),
        "upper_probe=capacity; gen=%llu; cs=%p; entries=%p; entry=%p; entries_count=%u; baseid=%u; channels=%u; allocation=%p; controllers=%p; buffer=%p; records=%p; stride=%u; extras=%u; controller2=%p; controller2vt=%llx; writable=%u; recorded=%u; verdict=%s; observe_only=1\r\n",
        context.token.generation, reinterpret_cast<void*>(c.cs), reinterpret_cast<void*>(c.entries),
        reinterpret_cast<void*>(c.entry), c.entry_count, c.base_id, c.channel_count,
        reinterpret_cast<void*>(c.allocation), reinterpret_cast<void*>(c.controllers),
        reinterpret_cast<void*>(c.buffer), reinterpret_cast<void*>(c.records), c.stride,
        unsigned(c.extra_count), reinterpret_cast<void*>(c.controller2),
        static_cast<unsigned long long>(c.controller2_vtable ? c.controller2_vtable - image : 0),
        c.storage_writable ? 1u : 0u, recorded ? 1u : 0u, why ? why : "ok"))) Emit(line);
}
void ObserveSubmission(void* cs, std::uint32_t id, std::uint32_t channel, std::uint64_t state,
                       std::uintptr_t caller, bool slot190, float blend, std::uint32_t option) noexcept {
    if (!context.accepted) return;
    const Submission s{reinterpret_cast<std::uintptr_t>(cs), caller, id, channel, GetCurrentThreadId(), state, slot190};
    const char* why = context.token.generation != generation.load(std::memory_order_acquire) ? "generation"
                      : SubmissionRefusal(context.token, context.consumer, s, image);
    if (submit_lines.fetch_add(1, std::memory_order_relaxed) >= kReceiptCap) return;
    std::uint32_t blend_bits = 0;
    std::memcpy(&blend_bits, &blend, sizeof(blend_bits));
    char line[320]{};
    if (SUCCEEDED(StringCchPrintfA(line, sizeof(line),
        "upper_probe=submit; gen=%llu; gani=%u; thread=%lu; cs=%p; id=%u; channel=%u; caller=%llx; slot=%u; state=%llx; blend=%08x; option=%u; verdict=%s; observe_only=1\r\n",
        context.token.generation, unsigned(context.token.gani), GetCurrentThreadId(), cs, id, channel,
        static_cast<unsigned long long>(caller - image), slot190 ? 190u : 170u,
        static_cast<unsigned long long>(state), blend_bits, option, why ? why : "ok"))) Emit(line);
    if (!why) ObserveCapacity();
}
std::uintptr_t SubmissionCaller(std::uintptr_t actual, bool slot190) noexcept {
#if defined(MGS_UPPER_PROBE_TESTING)
    if (fixture.rewrite) return fixture.caller ? fixture.caller : image + (slot190 ? 0x11991a6 : 0x11991e4);
#else
    (void)slot190;
#endif
    return actual;
}
// A shared lifecycle lease remains held through the original native method.
// Clear invalidates generation first, then waits for an accepted submission to
// finish before returning. New publication also requires the exclusive lock.
struct RewriteLease {
    bool held = false;
    std::uint32_t channel = 0;
    float blend = 0;
    RewriteLease(const Submission& s, float input_blend, std::uint32_t option) noexcept
        : channel(s.channel), blend(input_blend) {
        if (!RewriteCompiled() || !context.accepted || !context.token.manual_channel2) return;
        const char* why = nullptr;
        std::uint32_t input_blend_bits = 0;
        std::memcpy(&input_blend_bits, &input_blend, sizeof(input_blend_bits));
        if (!TryAcquireSRWLockShared(&token_lock)) why = "token_busy";
        else {
            held = true;
            const auto current = generation.load(std::memory_order_acquire);
            MotionCapacity capacity{};
            const auto now = GetTickCount64();
            if (current != context.token.generation || published.generation != current ||
                !SameIdentity(published, context.token)) why = "generation";
            if (!why) why = ConsumerRefusal(context.token, context.consumer, now, current, image);
            if (!why) why = CurrentIdentity(context.token);
            if (!why) why = ReadCapacity(context.token, capacity);
            if (!why && (!capacity_identity.generation || !SameIdentity(capacity_identity, context.token) ||
                !SameCapacity(capacity_proof, capacity))) why = "capacity_changed";
            if (!why) why = RewriteRefusal(context.token, context.consumer, s, capacity, GetTickCount64(), current,
                                           image, submitted_generation.load(std::memory_order_acquire) == current);
            // This flag never broadens the existing token/TLS/identity/caller
            // permission. Admission still requires the observed public input8.
            if (!why && ZeroBlendCompiled()) {
                if (input_blend_bits != 0x41000000u) why = "manual_fire_blend";
                else if (option != 0) why = "manual_fire_option";
                else if (capacity.extra_count != 0) why = "manual_fire_extras";
            }
            if (!why) {
                std::uint64_t empty = 0;
                if (!submitted_generation.compare_exchange_strong(empty, current, std::memory_order_acq_rel))
                    why = "already_submitted";
            }
            if (!why) {
                channel = 2;
                if (ZeroBlendCompiled()) blend = 0.0f;
            }
            else { ReleaseSRWLockShared(&token_lock); held = false; }
        }
        if (rewrite_lines.fetch_add(1, std::memory_order_relaxed) >= kReceiptCap) return;
        std::uint32_t forwarded_blend_bits = 0;
        std::memcpy(&forwarded_blend_bits, &blend, sizeof(forwarded_blend_bits));
        char line[384]{};
        if (SUCCEEDED(StringCchPrintfA(line, sizeof(line),
            "upper_probe=manual_channel2; gen=%llu; gani=%u; thread=%lu; slot=%u; caller=%llx; from=%u; to=%u; verdict=%s; one_submission=1; blend_from=%08x; blend_to=%08x; zero_blend_compiled=%u\r\n",
            context.token.generation, unsigned(context.token.gani), GetCurrentThreadId(), s.slot190 ? 190u : 170u,
            static_cast<unsigned long long>(s.caller - image), s.channel, channel, why ? why : "ok",
            input_blend_bits, forwarded_blend_bits, ZeroBlendCompiled() ? 1u : 0u))) Emit(line);
    }
    ~RewriteLease() {
        const DWORD last = GetLastError();
        if (held) ReleaseSRWLockShared(&token_lock);
        SetLastError(last);
    }
    RewriteLease(const RewriteLease&) = delete;
    RewriteLease& operator=(const RewriteLease&) = delete;
};
std::uintptr_t __fastcall Consume(void* primitive, std::uint32_t id, std::uint64_t state, std::uint64_t file) {
    const DWORD before = GetLastError();
    const RestoreContext restore;
    context = Context{};
    Token t{};
    if (installed.load(std::memory_order_acquire) && CopyToken(t)) {
        const Consumer c{reinterpret_cast<std::uintptr_t>(primitive), id, GetCurrentThreadId(), state, file,
                         reinterpret_cast<std::uintptr_t>(_ReturnAddress())};
        const char* why = ConsumerRefusal(t, c, GetTickCount64(), generation.load(std::memory_order_acquire), image);
        if (!why) why = CurrentIdentity(t);
        context = Context{t, c, why == nullptr};
        if ((id == 1 || c.primitive == t.primitive) && consume_lines.fetch_add(1, std::memory_order_relaxed) < kReceiptCap) {
            char line[384]{};
            if (SUCCEEDED(StringCchPrintfA(line, sizeof(line),
                "upper_probe=consume; gen=%llu; mission=%u; gani=%u; owner=%u; thread=%lu; caller=%llx; prim=%p; id=%u; state=%llx; file=%llx; verdict=%s; observe_only=1\r\n",
                t.generation, t.mission, unsigned(t.gani), t.owner_thread, GetCurrentThreadId(),
                static_cast<unsigned long long>(c.caller - image), primitive, id,
                static_cast<unsigned long long>(state), static_cast<unsigned long long>(file), why ? why : "ok"))) Emit(line);
        }
    }
    SetLastError(before);
    const std::uintptr_t result = original_consume(primitive, id, state, file);
    const DWORD after = GetLastError();
    SetLastError(after);
    return result;
}
std::uintptr_t __fastcall Submit170(void* cs, std::uint32_t id, std::uint32_t channel,
                                   std::uint64_t state, float blend, std::uint32_t option) {
    const DWORD before = GetLastError();
    const auto actual_caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    std::uint32_t blend_bits = 0;
    std::memcpy(&blend_bits, &blend, sizeof(blend_bits));
    const auto end_ticket = end_observe::BeforeSubmission(
        {reinterpret_cast<std::uintptr_t>(cs), actual_caller, id, channel, GetCurrentThreadId(), blend_bits, option, state, false}, GetTickCount64());
    const auto caller = SubmissionCaller(actual_caller, false);
    ObserveSubmission(cs, id, channel, state, caller, false, blend, option);
    const Submission submission{reinterpret_cast<std::uintptr_t>(cs), caller, id, channel, GetCurrentThreadId(), state, false};
    const RewriteLease lease(submission, blend, option);
    SetLastError(before);
    const auto result = original170(cs, id, lease.channel, state, lease.blend, option);
    const DWORD after = GetLastError();
    end_observe::AfterSubmission(end_ticket, result, GetTickCount64());
    SetLastError(after);
    return result;
}
std::uintptr_t __fastcall Submit190(void* cs, std::uint32_t id, std::uint32_t channel, std::uint64_t state, float blend) {
    const DWORD before = GetLastError();
    const auto actual_caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    std::uint32_t blend_bits = 0;
    std::memcpy(&blend_bits, &blend, sizeof(blend_bits));
    const auto end_ticket = end_observe::BeforeSubmission(
        {reinterpret_cast<std::uintptr_t>(cs), actual_caller, id, channel, GetCurrentThreadId(), blend_bits, 0, state, true}, GetTickCount64());
    const auto caller = SubmissionCaller(actual_caller, true);
    ObserveSubmission(cs, id, channel, state, caller, true, blend, 0);
    const Submission submission{reinterpret_cast<std::uintptr_t>(cs), caller, id, channel, GetCurrentThreadId(), state, true};
    const RewriteLease lease(submission, blend, 0);
    SetLastError(before);
    const auto result = original190(cs, id, lease.channel, state, blend);
    const DWORD after = GetLastError();
    end_observe::AfterSubmission(end_ticket, result, GetTickCount64());
    SetLastError(after);
    return result;
}
} // namespace
bool Compiled() noexcept {
#if defined(MGS_LOCAL_UPPER_PROBE) && MGS_LOCAL_UPPER_PROBE
    return true;
#else
    return false; // Observe-only installation cannot authorize manual dispatch.
#endif
}
bool RewriteCompiled() noexcept {
#if defined(MGS_LOCAL_UPPER_CHANNEL2) && MGS_LOCAL_UPPER_CHANNEL2
    return Compiled();
#else
    return false;
#endif
}
bool ZeroBlendCompiled() noexcept {
#if defined(MGS_LOCAL_UPPER_FIRE_ZERO_BLEND) && MGS_LOCAL_UPPER_FIRE_ZERO_BLEND
    return RewriteCompiled();
#else
    return false;
#endif
}
bool Installed() noexcept { return installed.load(std::memory_order_acquire); }
bool Install(const void* base, std::size_t span, Authorization auth, LogFn log) noexcept {
    const Preserve preserve;
    if (!auth.offline || !auth.lab_active || !auth.redirected_saves) return false;
    if (Installed()) return image == reinterpret_cast<std::uintptr_t>(base);
    if (attempted.exchange(true, std::memory_order_acq_rel)) return false;
    logger = log;
    image = reinterpret_cast<std::uintptr_t>(base);
    if (!image || !Fingerprint(image, span)) { Refused("install", "executable"); return false; }
    std::uintptr_t mismatch = 0;
    if (!pp::CodeGate(base, span, mismatch)) { Refused("install", "partner_code_gate"); return false; }
    for (const Body& b : bodies) if (b.rva > span || b.size > span - b.rva ||
        !Hash(reinterpret_cast<void*>(image + b.rva), b.size, b.hash)) { Refused("install", "body_hash"); return false; }
    std::uintptr_t target170 = 0, target190 = 0;
    if (!Read(image + pc::cs_vtable_rva + 0x170, target170) || target170 != image + 0xaf2ba0 ||
        !Read(image + pc::cs_vtable_rva + 0x190, target190) || target190 != image + 0xaf3270) {
        Refused("install", "cs_slots"); return false;
    }
    std::uintptr_t channel_graph = 0, channel_path = 0;
    if (!Read(image + 0x250ba50 + 0x48, channel_graph) || channel_graph != image + 0x1c73070 ||
        !Read(image + 0x250ba50 + 0x80, channel_path) || channel_path != image + 0x1c71d70) {
        Refused("install", "controller_slots"); return false;
    }
    void* targets[] = {reinterpret_cast<void*>(image + 0x1199140), reinterpret_cast<void*>(target170), reinterpret_cast<void*>(target190)};
    void* replacements[] = {reinterpret_cast<void*>(&Consume), reinterpret_cast<void*>(&Submit170), reinterpret_cast<void*>(&Submit190)};
    void** originals[] = {reinterpret_cast<void**>(&original_consume), reinterpret_cast<void**>(&original170), reinterpret_cast<void**>(&original190)};
    if (end_observe::Compiled()) {
        for (const auto& pin : end_observe::kPins) if (pin.rva > span || pin.size > span - pin.rva ||
            !Hash(reinterpret_cast<void*>(image + pin.rva), pin.size, pin.sha256)) {
            end_observe::Clear(); Refused("install", "end_body_hash"); return false;
        }
        if (!end_observe::Configure(image, {auth.offline && auth.lab_active, auth.redirected_saves, true, true}, {nullptr, EndRead})) {
            Refused("install", "end_configure"); return false;
        }
    }
    // The independent observe-only mode installs the two CS wrappers only.
    // It cannot install or arm a DM consumer, rewrite a channel, or submit End.
    const unsigned first_hook = Compiled() ? 0u : 1u;
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) { end_observe::Clear(); Refused("install", "hook_initialize"); return false; }
    unsigned created = first_hook;
    for (; created < 3; ++created) if (MH_CreateHook(targets[created], replacements[created], originals[created]) != MH_OK) break;
    if (created == 3) {
        bool queued = true;
        for (unsigned i = first_hook; i < 3; ++i) if (MH_QueueEnableHook(targets[i]) != MH_OK) queued = false;
        if (queued && MH_ApplyQueued() == MH_OK) {
            addresses = pp::AddressesFor(base);
            installed.store(true, std::memory_order_release);
            Emit(RewriteCompiled() ? "upper_probe=installed; hooks=3; manual_channel2_compiled=1; manual_gani=24; named_state=91a5b8ac42f8; rewrite_slot=170; channel_rewrite=guarded\r\n"
                                   : Compiled() ? "upper_probe=installed; hooks=3; observe_only=1; channel_rewrite=0\r\n"
                                                : "upper_end_observe=installed; hooks=2; native_submissions=0; mutations=0; automatic_backend=0\r\n");
            return true;
        }
    }
    // Keep forwarding trampoline storage alive during any failed installation;
    // no token can be armed until all hooks are installed. Do not uninitialize
    // shared MinHook or remove a trampoline another thread might be returning.
    end_observe::Clear();
    for (unsigned i = first_hook; i < created; ++i) MH_DisableHook(targets[i]);
    Refused("install", "hook_install");
    return false;
}
bool ArmRequest(std::uint64_t gen, std::uint32_t mission, std::uint32_t thread, std::uint8_t gani, bool request_channel2) noexcept {
    const Preserve preserve;
    if (!Compiled() || !Installed()) return false;
    const auto serial = lifecycle_serial.load(std::memory_order_acquire);
    // A refused new request must not leave an older request authorized during
    // its remaining lifetime. Publishing a new identity is all-or-nothing.
    Clear("new_request");
    if (!partner_dm_patch::Patched()) { Clear("comparator_off"); Refused("arm", "comparator_off"); return false; }
    if (!gen || !RequestCode(gani) || thread != GetCurrentThreadId() || !thread ||
        (mission != 30010 && mission != 30020 && mission != 30050)) { Refused("arm", "request_context"); return false; }
    Token t{};
    t.generation = gen; t.armed_ms = GetTickCount64(); t.mission = mission; t.owner_thread = thread; t.gani = gani;
    if (const char* why = Snapshot(t)) { Clear("arm_refused"); Refused("arm", why); return false; }
    MotionCapacity fresh{};
    const bool requested = request_channel2 && RewriteCompiled() && gani == kManualChannel2Gani;
    const char* capacity_why = requested ? ReadCapacity(t, fresh) : nullptr;
    AcquireSRWLockExclusive(&token_lock);
    if (serial != lifecycle_serial.load(std::memory_order_acquire)) {
        ReleaseSRWLockExclusive(&token_lock);
        Refused("arm", "lifecycle_changed");
        return false;
    }
    if (requested && !capacity_why) {
        if (!capacity_identity.generation || !SameIdentity(capacity_identity, t)) capacity_why = "capacity_not_observed";
        else if (!SameCapacity(capacity_proof, fresh)) capacity_why = "capacity_changed";
        else if (fresh.controller2_vtable != image + 0x250ba50) capacity_why = "controller2_kind";
        else t.manual_channel2 = true;
    }
    published = t;
    generation.store(gen, std::memory_order_release);
    ReleaseSRWLockExclusive(&token_lock);
    if (capacity_why) Refused("channel2_arm", capacity_why);
    char line[288]{};
    if (SUCCEEDED(StringCchPrintfA(line, sizeof(line), "upper_probe=armed; gen=%llu; mission=%u; gani=%u; owner=%u; prim=%p; cs=%p; expires_ms=2000; rewrite_requested=%u; rewrite_armed=%u\r\n",
        gen, mission, unsigned(gani), thread, reinterpret_cast<void*>(t.primitive), reinterpret_cast<void*>(t.cs),
        requested ? 1u : 0u, t.manual_channel2 ? 1u : 0u))) Emit(line);
    return true;
}
void Clear(const char* reason) noexcept {
    const Preserve preserve;
    const bool replacement = reason && std::strcmp(reason, "new_request") == 0;
    if (!replacement) lifecycle_serial.fetch_add(1, std::memory_order_acq_rel);
    generation.store(0, std::memory_order_release);
    AcquireSRWLockExclusive(&token_lock);
    const bool had_token = published.generation != 0;
    published = Token{};
    submitted_generation.store(0, std::memory_order_release);
    // Internal replacement retains only the earlier read-only capacity proof.
    // Every externally integrated lifecycle clear invalidates both permissions.
    if (!replacement) {
        capacity_identity = Token{};
        capacity_proof = MotionCapacity{};
    }
    ReleaseSRWLockExclusive(&token_lock);
    if (had_token) {
        char line[128]{};
        if (SUCCEEDED(StringCchPrintfA(line, sizeof(line), "upper_probe=cleared; reason=%s\r\n", reason ? reason : "unspecified"))) Emit(line);
    }
}
#if defined(MGS_UPPER_PROBE_TESTING)
void TestOriginals(TestConsumeFn consume, Test170Fn submit170, Test190Fn submit190) noexcept {
    original_consume = consume; original170 = submit170; original190 = submit190;
}
bool TestObservation(const Token& token, const Consumer& consumer, std::uintptr_t base, LogFn log) noexcept {
    if (Installed() || ConsumerRefusal(token, consumer, token.armed_ms, token.generation, base)) return false;
    image = base;
    logger = log;
    fixture = Fixture{};
    context = Context{token, consumer, true};
    generation.store(token.generation, std::memory_order_release);
    return true;
}
bool TestRewrite(const Token& token, const Consumer& consumer, const MotionCapacity& capacity, std::uintptr_t base, LogFn log) noexcept {
    if (Installed() || !RewriteCompiled() || consumer.thread != GetCurrentThreadId() ||
        ConsumerRefusal(token, consumer, GetTickCount64(), token.generation, base)) return false;
    image = base; logger = log;
    fixture = Fixture{true, true, capacity, 0};
    context = Context{token, consumer, true};
    AcquireSRWLockExclusive(&token_lock);
    published = token; capacity_identity = token; capacity_proof = capacity;
    submitted_generation.store(0, std::memory_order_release);
    generation.store(token.generation, std::memory_order_release);
    ReleaseSRWLockExclusive(&token_lock);
    return true;
}
void TestRewriteCaller(std::uintptr_t caller) noexcept { fixture.caller = caller; }
void TestRewriteIdentity(bool same) noexcept { fixture.same_identity = same; }
void TestRewriteCapacity(const MotionCapacity& capacity) noexcept { fixture.capacity = capacity; }
std::uint64_t TestCurrentGeneration() noexcept { return generation.load(std::memory_order_acquire); }
bool TestCapacityRecorded() noexcept {
    if (!TryAcquireSRWLockShared(&token_lock)) return false;
    const bool seen = capacity_identity.generation != 0;
    ReleaseSRWLockShared(&token_lock);
    return seen;
}
void TestEndObservation() noexcept {
    context = Context{};
    fixture = Fixture{};
    generation.store(0, std::memory_order_release);
    AcquireSRWLockExclusive(&token_lock);
    published = Token{}; capacity_identity = Token{}; capacity_proof = MotionCapacity{};
    submitted_generation.store(0, std::memory_order_release);
    ReleaseSRWLockExclusive(&token_lock);
    logger = nullptr;
    image = 0;
}
std::uintptr_t TestConsume(void* p, std::uint32_t id, std::uint64_t state, std::uint64_t file) { return Consume(p, id, state, file); }
std::uintptr_t Test170(void* p, std::uint32_t id, std::uint32_t channel, std::uint64_t state, float blend, std::uint32_t option) {
    return Submit170(p, id, channel, state, blend, option);
}
std::uintptr_t Test190(void* p, std::uint32_t id, std::uint32_t channel, std::uint64_t state, float blend) {
    return Submit190(p, id, channel, state, blend);
}
#endif
} // namespace mgscoop::partner_upper_probe
#else
namespace mgscoop::partner_upper_probe {
bool Compiled() noexcept { return false; }
bool RewriteCompiled() noexcept { return false; }
bool ZeroBlendCompiled() noexcept { return false; }
bool Installed() noexcept { return false; }
bool Install(const void*, std::size_t, Authorization, LogFn) noexcept { return false; }
bool ArmRequest(std::uint64_t, std::uint32_t, std::uint32_t, std::uint8_t, bool) noexcept { return false; }
void Clear(const char*) noexcept {}
} // namespace mgscoop::partner_upper_probe
#endif
