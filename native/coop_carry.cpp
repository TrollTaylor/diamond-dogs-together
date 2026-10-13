// Co-op teammate carrying through the stock player-carries-player code.
// See include/mgscoop/coop_carry.hpp and docs/coop-carry.md.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include "mgscoop/coop_carry.hpp"
#include "mgscoop/near_code.hpp"
#include "mgscoop/partner_input_hook.hpp"
#include "mgscoop/worker_scenario.hpp"
#include "MinHook.h"

namespace mgscoop::coop_carry {
namespace {

// ---- Pinned retail sites (mgsvtpp.exe 1.0.15.4, SHA-256 085c2f82...bb45) ----
constexpr std::uintptr_t kImageSpan = 0xa01d000;
// TppGameStatus provider (static object 0x2BD6160): vtable slot 0 is the flag
// query 0x53EB20 = byte[provider + 0x80 + flag * 0x78] != 0.
constexpr std::uintptr_t kProviderSlot0 = 0x2191a90;
constexpr std::uintptr_t kProviderIsSet = 0x53eb20;
constexpr unsigned char kProviderIsSetBytes[] = {0x33,0xc0,0x44,0x0f,0xb6,0xc2,0x4d,0x6b,0xc0,0x78,0x41,0x38,0x84,0x08,0x80,0x00,0x00,0x00,0x0f,0x95,0xc0,0xc3};
constexpr std::uint8_t kFlagOnline = 20; // S_IS_ONLINE
// Carry candidate producer 0x112C680: call 0xBFF050; mov dl,0x14; services+0x98;
// provider +0xF8; call [vt+0]. The return address is the end of that call.
constexpr std::uintptr_t kProducerSite = 0x112c8a0;
constexpr unsigned char kProducerBytes[] = {0xe8,0xab,0x27,0xad,0xff,0xb2,0x14,0x48,0x8b,0x88,0x98,0x00,0x00,0x00,0x48,0x8b,0x89,0xf8,0x00,0x00,0x00,0x48,0x8b,0x01,0xff,0x10};
constexpr std::uintptr_t kProducerReturn = 0x112c8ba;
// Carried-recipient interface vtables (the live class and its base); slot 5
// (+0x28) is the op0 handshake receiver 0x11268B0.
constexpr std::uintptr_t kRecipientSlots[] = {0x23b8df8, 0x23b8ce8};
constexpr std::uintptr_t kOp0 = 0x11268b0;
constexpr std::size_t kOp0Size = 454;
// SHA-256 of the receiver's bytes [5, 454) (the prologue may carry another detour).
constexpr unsigned char kOp0TailSha[32] = {0xf0,0x00,0xab,0xa0,0x8a,0x61,0xbf,0xc0,0xa1,0x58,0xbc,0xd9,0xfd,0x6a,0x83,0xfc,
                                            0x30,0x90,0xa9,0x71,0xd7,0x4f,0xdc,0x12,0x86,0x38,0x72,0xa7,0x26,0x77,0x45,0x0e};
constexpr unsigned char kOp0Head[5] = {0x48,0x8b,0xc4,0x41,0x54};
constexpr std::uint64_t kOp0Hash = 0x72bc35bcdbdull; // handshake message hash
// Player system P = *(image + 0x2A82C30); Q = *(P + 0x60).
constexpr std::uintptr_t kPlayerSlot = 0x2a82c30;
constexpr std::uintptr_t kPVtable = 0x2295490, kQVtable = 0x23ad970;
constexpr std::size_t kPFlags = 0x204, kPCount = 0x208, kPLocal = 0x214, kPQ = 0x60, kPIds = 0x70, kPPool = 0x78, kPBlocks = 0x378;
constexpr std::uint32_t kPFlagOnline = 0x100;
constexpr std::size_t kQAction = 0x18, kQRequests = 0x1d8, kQRequestStride = 0x40;
constexpr std::uintptr_t kQSubmitSlot = 0x23ad9b0, kQSubmit = 0xfe00d0; // Q vt+0x40: copy request into Q+1D8[id]
constexpr std::uintptr_t kKind15Vtable = 0x23aa950;                      // vt+0 0xFD48F0 returns 0x20
constexpr std::size_t kBlockStride = 0x6a8, kBlockPs = 0x78, kPsBase = 0x8, kPsRecords = 0x280, kPsRecordStride = 0x20;

struct Kind15 {
    std::uintptr_t vtable;
    std::uint8_t kind;
    std::uint8_t pad0[7];
    std::uint8_t substate;
    std::uint8_t sequence;
    std::uint8_t force;
    std::uint8_t pad1;
    std::uint16_t carrier;
    std::uint16_t pad2;
    float extra;
    std::uint32_t pad3;
};
static_assert(sizeof(Kind15) == 0x20, "kind-15 request is 32 bytes");
static_assert(offsetof(Kind15, kind) == 8 && offsetof(Kind15, substate) == 0x10 && offsetof(Kind15, sequence) == 0x11 &&
              offsetof(Kind15, force) == 0x12 && offsetof(Kind15, carrier) == 0x14 && offsetof(Kind15, extra) == 0x18, "layout");

using IsSetFn = bool (*)(void*, std::uint8_t);
using Op0Fn = void (*)(void*, std::uint32_t, std::int32_t*, const void*);
using SubmitFn = void (*)(void*, std::uint32_t, const void*);

std::uintptr_t g_base = 0;
Log g_log = nullptr;
IsSetFn g_isSet = nullptr;
Op0Fn g_op0 = nullptr;
bool g_installed = false;
std::atomic<unsigned> g_mode{0}, g_carryable{0};
std::atomic<std::uint32_t> g_overrides{0}, g_op0Calls{0}, g_op0Ours{0}, g_op0Accepted{0}, g_guardSkips{0}, g_kind15{0}, g_kind15Refused{0};
std::atomic<std::uint32_t> g_lastCarrier{0xffff}, g_lastTarget{0xffff}, g_lastAction{0};
std::atomic<std::int32_t> g_lastResult{0};
std::atomic<std::uintptr_t> g_lastCarrierRecord{0};
std::atomic<std::uintptr_t> g_lastRecipientIface{0};
std::atomic<std::uint32_t> g_lastRecipientId{0xffff};
std::atomic<bool> g_forcePending{false};
std::atomic<bool> g_dropPending{false};
std::atomic<std::uint32_t> g_forceCleared{0};
std::atomic<std::uint32_t> g_logBudget{400};

struct LastErrorState {
    DWORD value{GetLastError()};
    void Restore() const noexcept { SetLastError(value); }
    void Capture() noexcept { value = GetLastError(); }
    ~LastErrorState() noexcept { Restore(); }
};
template <class T> bool Read(std::uintptr_t p, T& out) noexcept {
    SIZE_T n = 0;
    return p && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(p), &out, sizeof(out), &n) && n == sizeof(out);
}
bool ReadBytes(std::uintptr_t p, void* out, std::size_t size) noexcept {
    SIZE_T n = 0;
    return p && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(p), out, size, &n) && n == size;
}
template <class T> bool Write(std::uintptr_t p, T value) noexcept {
    __try { *reinterpret_cast<volatile T*>(p) = value; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void Emit(const char* text) noexcept {
    if (!g_log || !text) return;
    if (g_logBudget.load(std::memory_order_relaxed) == 0) return;
    g_logBudget.fetch_sub(1, std::memory_order_relaxed);
    g_log(text);
}
void EmitF(const char* format, ...) noexcept {
    char line[320]{};
    va_list args;
    va_start(args, format);
    const int n = _vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
    va_end(args);
    if (n > 0 || line[0]) Emit(line);
}

// --- Minimal SHA-256 for the exact-byte gate (portable, no dependency) ---
struct Sha256 {
    std::uint32_t h[8]{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    unsigned char buf[64]{}; std::size_t len = 0; std::uint64_t bits = 0;
    static std::uint32_t R(std::uint32_t x, int n) noexcept { return (x >> n) | (x << (32 - n)); }
    void Block(const unsigned char* p) noexcept {
        static const std::uint32_t k[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
            0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
            0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
            0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = (std::uint32_t(p[4*i]) << 24) | (std::uint32_t(p[4*i+1]) << 16) | (std::uint32_t(p[4*i+2]) << 8) | p[4*i+3];
        for (int i = 16; i < 64; ++i) {
            const auto s0 = R(w[i-15], 7) ^ R(w[i-15], 18) ^ (w[i-15] >> 3);
            const auto s1 = R(w[i-2], 17) ^ R(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        std::uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; ++i) {
            const auto S1 = R(e,6) ^ R(e,11) ^ R(e,25), ch = (e & f) ^ (~e & g), t1 = hh + S1 + ch + k[i] + w[i];
            const auto S0 = R(a,2) ^ R(a,13) ^ R(a,22), mj = (a & b) ^ (a & c) ^ (b & c), t2 = S0 + mj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
    }
    void Update(const unsigned char* p, std::size_t n) noexcept {
        bits += std::uint64_t(n) * 8;
        while (n--) { buf[len++] = *p++; if (len == 64) { Block(buf); len = 0; } }
    }
    void Final(unsigned char out[32]) noexcept {
        const std::uint64_t total = bits;
        const unsigned char one = 0x80, zero = 0;
        Update(&one, 1); bits -= 8;
        while (len != 56) { Update(&zero, 1); bits -= 8; }
        unsigned char lenb[8];
        for (int i = 0; i < 8; ++i) lenb[i] = static_cast<unsigned char>(total >> (56 - 8 * i));
        Update(lenb, 8);
        for (int i = 0; i < 8; ++i) { out[4*i] = static_cast<unsigned char>(h[i] >> 24); out[4*i+1] = static_cast<unsigned char>(h[i] >> 16);
            out[4*i+2] = static_cast<unsigned char>(h[i] >> 8); out[4*i+3] = static_cast<unsigned char>(h[i]); }
    }
};

bool PlayerSystem(std::uintptr_t& P, std::uintptr_t& Q, bool& online) noexcept {
    P = Q = 0; online = true;
    std::uintptr_t vt = 0, qvt = 0; std::uint32_t flags = 0, count = 0, local = 0;
    if (!g_base || !Read(g_base + kPlayerSlot, P) || !P || !Read(P, vt) || vt != g_base + kPVtable) return false;
    if (!Read(P + kPFlags, flags) || !Read(P + kPCount, count) || !Read(P + kPLocal, local) || count != 2 || local != 0) return false;
    if (!Read(P + kPQ, Q) || !Q || !Read(Q, qvt) || qvt != g_base + kQVtable) return false;
    online = (flags & kPFlagOnline) != 0;
    return true;
}
bool ReadAction(std::uintptr_t Q, unsigned id, std::uint16_t& action) noexcept {
    std::uintptr_t actions = 0;
    return id < 2 && Read(Q + kQAction, actions) && actions && Read(actions + 2 * id, action);
}
bool PsRecord(std::uintptr_t P, unsigned id, std::uintptr_t& record) noexcept {
    record = 0;
    std::uintptr_t blocks = 0, ps = 0, records = 0, pools = 0; std::uint8_t pool = 0xff; std::uint32_t base = 0;
    // P+0x78 points to the per-instance pool byte array (partner_player ReadDiagnostics).
    if (id >= 2 || !Read(P + kPPool, pools) || !pools || !Read(pools + id, pool) || pool == 0xff || !Read(P + kPBlocks, blocks) || !blocks) return false;
    const auto block = blocks + std::uintptr_t(pool) * kBlockStride;
    if (!Read(block + kBlockPs, ps) || !ps || !Read(ps + kPsBase, base) || id < base || !Read(ps + kPsRecords, records) || !records) return false;
    record = records + std::uintptr_t(id - base) * kPsRecordStride;
    return true;
}

bool IsCarriedAction(std::uint16_t a) noexcept { return a == 0x2a || a == 0x2b || a == 0x2c; }

__declspec(noinline) bool HookIsSet(void* self, std::uint8_t flag) noexcept {
    if (flag == kFlagOnline && g_mode.load(std::memory_order_relaxed) != kOff &&
        reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == g_base + kProducerReturn) {
        g_overrides.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    return g_isSet(self, flag);
}

bool SubmitKind15(void* iface, std::uint32_t id, std::uint16_t carrier) noexcept {
    // Mirror of the online branch 0x11269A4..0x1126A31 for an offline target.
    const auto i = reinterpret_cast<std::uintptr_t>(iface);
    std::uintptr_t node = 0, records = 0, P = 0, Q = 0; std::uint32_t baseId = 0; bool online = true;
    if (!PlayerSystem(P, Q, online) || online) return false;
    if (!Read(i + 8, node) || !node || !Read(node + 0x24, baseId) || id < baseId || !Read(i + 0x58, records) || !records) return false;
    const auto record = records + std::uintptr_t(id - baseId) * 0x60;
    std::uint8_t sequence = 0;
    if (!Read(record + 0x12, sequence)) return false;
    std::uint8_t next = static_cast<std::uint8_t>(sequence + 1);
    if (next > 0xfa) next = 0;
    std::uintptr_t qvt = 0, submit = 0;
    if (!Read(Q, qvt) || qvt != g_base + kQVtable || !Read(qvt + 0x40, submit) || submit != g_base + kQSubmit) return false;
    if (!Write(record + 0x12, next)) return false;
    Kind15 request{};
    request.vtable = g_base + kKind15Vtable;
    request.kind = 0x0f;
    request.sequence = next;
    request.force = 1;
    request.carrier = carrier;
    __try {
        reinterpret_cast<SubmitFn>(submit)(reinterpret_cast<void*>(Q), id, &request);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

__declspec(noinline) void HookOp0(void* iface, std::uint32_t id, std::int32_t* result, const void* message) noexcept {
    LastErrorState error;
    const auto calls = g_op0Calls.fetch_add(1, std::memory_order_relaxed);
    (void)calls;
    const unsigned mode = g_mode.load(std::memory_order_relaxed);
    bool ours = false, skipped = false;
    std::uintptr_t carrierRecord = 0; std::uint16_t carrier = 0xffff, action = 0; std::uint8_t owned = 0;
    std::uintptr_t P = 0, Q = 0; bool online = true;
    if (mode != kOff && message && result) {
        const auto m = reinterpret_cast<std::uintptr_t>(message);
        std::uint64_t hash = 0; std::uint32_t op = 0xffffffffu; std::uint16_t word = 0xffff;
        if (Read(m, hash) && hash == kOp0Hash && Read(m + 0xc, op) && op == 0 && Read(m + 0x10, carrierRecord) && carrierRecord &&
            Read(m + 8, word) && PlayerSystem(P, Q, online) && !online && id < 2 && (word & 0x1ffu) < 2u && static_cast<std::uint32_t>(word & 0x1ffu) != id &&
            ReadAction(Q, id, action)) {
            ours = true;
            carrier = static_cast<std::uint16_t>(word & 0x1ff);
            g_op0Ours.fetch_add(1, std::memory_order_relaxed);
            // Mode 2: the stock guard admits only 0x83/0x84 targets of an
            // offline-owned carrier. Skip it once, for a granted co-op body only.
            if (mode >= kAnyDowned && (g_carryable.load(std::memory_order_relaxed) & (1u << id)) && action != 0x83 && action != 0x84 &&
                !IsCarriedAction(action) && Read(carrierRecord + 0x85, owned) && owned == 1 && Write<std::uint8_t>(carrierRecord + 0x85, 0)) {
                skipped = true;
                g_guardSkips.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    error.Restore();
    g_op0(iface, id, result, message);
    error.Capture();
    if (skipped) Write<std::uint8_t>(carrierRecord + 0x85, owned);
    if (!ours) return;
    std::int32_t r = -1;
    Read(reinterpret_cast<std::uintptr_t>(result), r);
    g_lastResult.store(r, std::memory_order_relaxed);
    g_lastCarrier.store(carrier, std::memory_order_relaxed);
    g_lastTarget.store(id, std::memory_order_relaxed);
    g_lastAction.store(action, std::memory_order_relaxed);
    g_lastCarrierRecord.store(carrierRecord, std::memory_order_relaxed);
    g_lastRecipientIface.store(reinterpret_cast<std::uintptr_t>(iface), std::memory_order_relaxed);
    g_lastRecipientId.store(id, std::memory_order_relaxed);
    bool submitted = false;
    if (r == 0) {
        g_op0Accepted.fetch_add(1, std::memory_order_relaxed);
        submitted = SubmitKind15(iface, id, carrier);
        if (submitted) g_forcePending.store(true, std::memory_order_relaxed);
        (submitted ? g_kind15 : g_kind15Refused).fetch_add(1, std::memory_order_relaxed);
    }
    EmitF("coop_carry=op0; carrier=%u; target=%u; action=0x%x; result=%d; guard_skip=%u; kind15=%u\r\n",
          unsigned(carrier), unsigned(id), unsigned(action), r, skipped ? 1u : 0u, submitted ? 1u : 0u);
}

// ---- Diagnostic trace of the natural selection path (rate-limited log) ----
constexpr std::uintptr_t kPredicate = 0x112aaa0;      // candidate geometry/state predicate
constexpr std::uintptr_t kDispatchSlot = 0x23b9488;   // carry plugin interface slot -> 0x112CF50
constexpr std::uintptr_t kDispatch = 0x112cf50;
using PredicateFn = bool (*)(void*, std::uint32_t, const float*, std::uint16_t, void*);
using DispatchFn = bool (*)(void*, std::uint32_t, std::uint8_t, void*);
PredicateFn g_predicate = nullptr;
DispatchFn g_dispatch = nullptr;
std::atomic<std::uint32_t> g_predCalls{0}, g_predPlayer{0}, g_predPlayerOk{0}, g_dispatchCalls{0}, g_dispatchFound{0}, g_dispatchPlayer{0};
std::atomic<std::uint32_t> g_lastPredId{0xffff}, g_lastPredOk{0}, g_lastDispatchTarget{0xffff}, g_lastDispatchKind{0}, g_lastDispatchFlags{0};
std::atomic<std::uint32_t> g_traceBudget{120};
constexpr std::uintptr_t kCarryPoint = 0x112e7d0, kRaycast = 0x1129f90;
using CarryPointFn = bool (*)(std::uint16_t, float*);
using RaycastFn = bool (*)(void*, std::uint32_t, const float*, float, float, float, std::uint64_t);
CarryPointFn g_carryPoint = nullptr;
RaycastFn g_raycast = nullptr;
thread_local bool t_inPlayerPredicate = false;
std::atomic<std::uint32_t> g_cpCalls{0}, g_cpOk{0}, g_rayCalls{0}, g_rayBlocked{0};
std::atomic<std::int32_t> g_cpY{0}, g_cpX{0}, g_cpZ{0};
__declspec(noinline) bool HookCarryPoint(std::uint16_t id, float* out) noexcept {
    const bool ok = g_carryPoint(id, out);
    if (t_inPlayerPredicate) {
        g_cpCalls.fetch_add(1, std::memory_order_relaxed);
        if (ok) {
            g_cpOk.fetch_add(1, std::memory_order_relaxed);
            float p[3]{}; ReadBytes(reinterpret_cast<std::uintptr_t>(out), p, sizeof(p));
            g_cpX.store(static_cast<std::int32_t>(p[0] * 1000.f)); g_cpY.store(static_cast<std::int32_t>(p[1] * 1000.f)); g_cpZ.store(static_cast<std::int32_t>(p[2] * 1000.f));
        }
    }
    return ok;
}
__declspec(noinline) bool HookRaycast(void* plugin, std::uint32_t carrier, const float* from, float a4, float a5, float a6, std::uint64_t a7) noexcept {
    const bool blocked = g_raycast(plugin, carrier, from, a4, a5, a6, a7);
    if (t_inPlayerPredicate) {
        g_rayCalls.fetch_add(1, std::memory_order_relaxed);
        if (blocked) g_rayBlocked.fetch_add(1, std::memory_order_relaxed);
    }
    return blocked;
}
__declspec(noinline) bool HookPredicate(void* plugin, std::uint32_t carrier, const float* pos, std::uint16_t id, void* out) noexcept {
    LastErrorState error;
    g_predCalls.fetch_add(1, std::memory_order_relaxed);
    const bool player = (id & 0x1ffu) < 2u && (id & 0xfe00u) == 0;
    t_inPlayerPredicate = player;
    error.Restore();
    const bool ok = g_predicate(plugin, carrier, pos, id, out);
    t_inPlayerPredicate = false;
    error.Capture();
    if ((id & 0x1ffu) < 2u && (id & 0xfe00u) == 0) {
        g_predPlayer.fetch_add(1, std::memory_order_relaxed);
        if (ok) g_predPlayerOk.fetch_add(1, std::memory_order_relaxed);
        g_lastPredId.store(id, std::memory_order_relaxed);
        g_lastPredOk.store(ok ? 1u : 0u, std::memory_order_relaxed);
        if (g_traceBudget.load(std::memory_order_relaxed)) {
            g_traceBudget.fetch_sub(1, std::memory_order_relaxed);
            float p[3]{};
            ReadBytes(reinterpret_cast<std::uintptr_t>(pos), p, sizeof(p));
            EmitF("coop_carry=predicate; carrier=%u; candidate=%u; ok=%u; pos=%.2f,%.2f,%.2f\r\n", carrier, unsigned(id), ok ? 1u : 0u,
                  double(p[0]), double(p[1]), double(p[2]));
        }
    }
    return ok;
}
__declspec(noinline) bool HookDispatch(void* self, std::uint32_t id, std::uint8_t mode, void* out) noexcept {
    LastErrorState error;
    g_dispatchCalls.fetch_add(1, std::memory_order_relaxed);
    error.Restore();
    const bool found = g_dispatch(self, id, mode, out);
    error.Capture();
    if (found && out) {
        g_dispatchFound.fetch_add(1, std::memory_order_relaxed);
        std::uint16_t target = 0xffff, flags = 0; std::uint32_t kind = 0;
        const auto o = reinterpret_cast<std::uintptr_t>(out);
        Read(o + 0x20, target); Read(o + 0x2c, kind); Read(o + 0x34, flags);
        g_lastDispatchTarget.store(target, std::memory_order_relaxed);
        g_lastDispatchKind.store(kind, std::memory_order_relaxed);
        g_lastDispatchFlags.store(flags, std::memory_order_relaxed);
        if ((target & 0x1ffu) < 2u && (target & 0xfe00u) == 0) {
            g_dispatchPlayer.fetch_add(1, std::memory_order_relaxed);
            if (g_traceBudget.load(std::memory_order_relaxed)) {
                g_traceBudget.fetch_sub(1, std::memory_order_relaxed);
                EmitF("coop_carry=candidate; carrier=%u; mode=%u; target=%u; kind=0x%x; flags=0x%x\r\n", id, unsigned(mode), unsigned(target), kind, unsigned(flags));
            }
        }
    }
    return found;
}
constexpr std::uintptr_t kInitSlots[] = {0x23b9b50, 0x23b9510}; // carrier phase interface slot 0 -> thunk 0x112FEE0
constexpr std::uintptr_t kInitThunk = 0x112fee0;
using InitFn = bool (*)(void*, std::uint32_t);
InitFn g_init = nullptr;
std::atomic<std::uint32_t> g_initCalls{0}, g_initOk{0}, g_lastInitId{0xffff}, g_lastInitOk{0}, g_lastInitTarget{0xffff}, g_lastInitVariant{0};
std::atomic<std::uint32_t> g_selPflags{0}, g_selOwnerOk{0}, g_selKind{0}, g_selCur{0}, g_selValid{0}, g_selDis{0}, g_selTarget{0xffff}, g_selValidOr{0};
// ---- Mirror of the teammate's carry on this machine ----
// The carrier's machine is authoritative. Its START makes this machine's
// replica of the carrier (instance 1) carry the local body through the stock
// forced-target receiver 0x11301B0 (FAA2AAF9CD28/DB0D91C47FA, sets carrier
// flag 0x1000 and target): the next polled carrier init admits variant A, the
// carrier sends op0 to the sleeping local body and the kind-15 adapter above
// attaches it. DROP sets the replica's stock put-down request (PS record +4
// bit 0x4000, the RequestCarryOff write at 0x1156A86).
constexpr std::uintptr_t kForcedReceiver = 0x11301b0;
constexpr std::uint64_t kForcedHashA = 0xfaa2aaf9cd28ull, kForcedHashB = 0xdb0d91c47faull;
using ReceiverFn = void (*)(void*, std::uint32_t, std::int32_t*, const void*);
struct ForcedMessage { std::uint64_t a; std::uint64_t b; std::uint16_t target; std::uint16_t pad[3]; };
static_assert(sizeof(ForcedMessage) == 24, "forced target message");
std::atomic<std::uintptr_t> g_initIface[2]{};
std::atomic<bool> g_forcePendingStart{false};
std::atomic<std::uint64_t> g_forcePendingAt{0};
std::atomic<std::uint32_t> g_forcedSent{0}, g_forcedResult{0xffffffffu}, g_mirrorStarts{0}, g_mirrorDrops{0}, g_wireTx{0}, g_wireRx{0};
bool (*g_send)(const std::uint8_t*, std::size_t) noexcept = nullptr;
std::uintptr_t CarrierRecord(unsigned id) noexcept {
    const auto iface = id < 2 ? g_initIface[id].load(std::memory_order_relaxed) : 0;
    std::uintptr_t node = 0, records = 0; std::uint32_t baseId = 0;
    if (!iface || !Read(iface + 8, node) || !node || !Read(node + 0x24, baseId) || id < baseId || !Read(iface + 0x38, records) || !records) return 0;
    return records + std::uintptr_t(id - baseId) * 0x6c0;
}
bool SendForced(void* iface, std::uint32_t id, std::uint16_t target) noexcept {
    ForcedMessage message{kForcedHashA, kForcedHashB, target, {0, 0, 0}};
    std::int32_t result = -1;
    __try {
        reinterpret_cast<ReceiverFn>(g_base + kForcedReceiver)(iface, id, &result, &message);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    g_forcedResult.store(static_cast<std::uint32_t>(result), std::memory_order_relaxed);
    return result == 0;
}

__declspec(noinline) bool HookInit(void* iface, std::uint32_t id) noexcept {
    LastErrorState error;
    if (id < 2) g_initIface[id].store(reinterpret_cast<std::uintptr_t>(iface), std::memory_order_relaxed);
    // A pending mirrored pickup: the replica carrier (instance 1) takes the
    // local sleeping body as its forced target right before its own init.
    if (id == 1 && g_forcePendingStart.load(std::memory_order_relaxed)) {
        g_forcePendingStart.store(false, std::memory_order_relaxed);
        if (SendForced(iface, id, 0)) g_forcedSent.fetch_add(1, std::memory_order_relaxed);
    }
    error.Restore();
    const bool ok = g_init(iface, id);
    error.Capture();
    g_initCalls.fetch_add(1, std::memory_order_relaxed);
    if (ok) g_initOk.fetch_add(1, std::memory_order_relaxed);
    const auto primary = reinterpret_cast<std::uintptr_t>(iface) - 0x30;
    std::uintptr_t node = 0, records = 0; std::uint32_t baseId = 0; std::uint16_t target = 0xffff; std::uint8_t variant = 0;
    if (Read(primary + 0x38, node) && node && Read(node + 0x24, baseId) && id >= baseId && Read(primary + 0x68, records) && records) {
        const auto record = records + std::uintptr_t(id - baseId) * 0x6c0;
        Read(record + 0x78, target); Read(record + 0x384, variant);
    }
    // Selector row-1 snapshot (0xAFD870 gates) for the local carrier body.
    if (id == 0) {
        std::uintptr_t block = 0, P = 0, sel = 0, owner = 0; std::uint32_t pflags = 0, kind = 0, cur = 0, vflags = 0, dis = 0;
        if (Read(primary + 8, block) && block && Read(block + 0x138, P) && P && Read(P + 0xd0, sel) && sel) {
            Read(P + 0x204, pflags); Read(sel + 0x980 + 0xe0, owner); Read(sel + 0x98c + 0xe0, kind);
            Read(sel + 0x1240 + 0x28, cur); Read(sel + 0x1260 + 0x28, vflags); Read(sel + 0xa04 + 0xe0, dis);
            std::uint16_t cachedTarget = 0xffff; Read(sel + 0x9a0 + 0xe0 + 0x20, cachedTarget);
            g_selPflags.store(pflags, std::memory_order_relaxed); g_selOwnerOk.store(owner == primary ? 1u : (owner ? 2u : 0u), std::memory_order_relaxed);
            g_selKind.store(kind, std::memory_order_relaxed); g_selCur.store(cur, std::memory_order_relaxed);
            g_selValid.store(vflags, std::memory_order_relaxed); g_selValidOr.fetch_or(vflags, std::memory_order_relaxed); g_selDis.store(dis, std::memory_order_relaxed);
            g_selTarget.store(cachedTarget, std::memory_order_relaxed);
        }
    }
    g_lastInitId.store(id, std::memory_order_relaxed); g_lastInitOk.store(ok ? 1u : 0u, std::memory_order_relaxed);
    g_lastInitTarget.store(target, std::memory_order_relaxed); g_lastInitVariant.store(variant, std::memory_order_relaxed);
    if (g_traceBudget.load(std::memory_order_relaxed)) {
        g_traceBudget.fetch_sub(1, std::memory_order_relaxed);
        EmitF("coop_carry=carrier_init; carrier=%u; ok=%u; target=%u; variant=%u\r\n", id, ok ? 1u : 0u, unsigned(target), unsigned(variant));
    }
    return ok;
}
// The candidate producer itself (direct tail jump from the plugin dispatcher).
constexpr std::uintptr_t kProducer = 0x112c680;
using ProducerFn = bool (*)(void*, std::uint32_t, void*, void*);
ProducerFn g_producer = nullptr;
thread_local std::uint32_t t_producerCarrier = 0xffffffffu;
std::atomic<std::uint32_t> g_producerCalls{0}, g_producerFound{0}, g_injected{0};
__declspec(noinline) bool HookProducer(void* plugin, std::uint32_t id, void* r8, void* out) noexcept {
    const auto previous = t_producerCarrier;
    t_producerCarrier = id;
    const bool found = g_producer(plugin, id, r8, out);
    t_producerCarrier = previous;
    g_producerCalls.fetch_add(1, std::memory_order_relaxed);
    if (found) g_producerFound.fetch_add(1, std::memory_order_relaxed);
    return found;
}
// Body position from the player slots (*(S+0x20))[id] = {x, y, z, yaw}.
bool BodyPosition(unsigned id, float (&pos)[4]) noexcept {
    std::uintptr_t P = 0, Q = 0, S = 0, slots = 0; bool online = true;
    return id < 2 && PlayerSystem(P, Q, online) && Read(Q + 0x1e8, S) && S && Read(S + 0x20, slots) && slots &&
        ReadBytes(slots + 16u * id, pos, sizeof(pos));
}
// The spatial search service of the producer's three scans (S = *(*(image+0x2C3A8A0)+8); *(*(S+0xA8)+0x10)).
constexpr std::uintptr_t kServicesRoot = 0x2c3a8a0;
constexpr std::uintptr_t kScanReturns[] = {0x112c93b, 0x112ca55, 0x112cb78};
using SearchFn = std::uintptr_t (*)(void*, void*, void*);
SearchFn g_search = nullptr;
std::uintptr_t g_searchSlot = 0, g_searchFn = 0;
std::atomic<std::uint32_t> g_searchCalls[3]{}, g_searchHits[3]{}, g_searchPlayers[3]{};
std::atomic<std::uint32_t> g_searchBudget{60};
__declspec(noinline) std::uintptr_t HookSearch(void* self, void* results, void* query) noexcept {
    LastErrorState error;
    const auto ret = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    error.Restore();
    const auto r = g_search(self, results, query);
    error.Capture();
    // Offline, player bodies are absent from the scan-0 player category. Add the
    // granted co-op teammate (stock predicate 0x112AAA0 still decides).
    // Only the local player picks up: this machine's replica of the teammate
    // (carrier 1) never takes the local body naturally; the remote carrier's
    // own machine announces its pickup and the mirror below starts it here.
    if (ret == g_base + kScanReturns[0] && t_producerCarrier == 0u && g_mode.load(std::memory_order_relaxed) != kOff) {
        const unsigned target = 1u - t_producerCarrier;
        const auto res = reinterpret_cast<std::uintptr_t>(results);
        std::uintptr_t ids = 0, positions = 0; std::uint32_t capacity = 0, count = 0;
        float me[4]{}, them[4]{};
        if ((g_carryable.load(std::memory_order_relaxed) & (1u << target)) && Read(res, ids) && ids && Read(res + 8, positions) && positions &&
            Read(res + 0x10, capacity) && Read(res + 0x14, count) && count < capacity && capacity <= 64 &&
            BodyPosition(t_producerCarrier, me) && BodyPosition(target, them)) {
            const float dx = them[0] - me[0], dy = them[1] - me[1], dz = them[2] - me[2];
            if (dx * dx + dy * dy + dz * dz < 9.0f &&
                Write<std::uint16_t>(ids + 2u * count, static_cast<std::uint16_t>(target))) {
                Write<float>(positions + 0x1cu * count, them[0]); Write<float>(positions + 0x1cu * count + 4, them[1]);
                Write<float>(positions + 0x1cu * count + 8, them[2]);
                Write<std::uint32_t>(res + 0x14, count + 1);
                g_injected.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    for (unsigned k = 0; k < 3; ++k) {
        if (ret != g_base + kScanReturns[k]) continue;
        g_searchCalls[k].fetch_add(1, std::memory_order_relaxed);
        const auto res = reinterpret_cast<std::uintptr_t>(results);
        std::uintptr_t ids = 0; std::uint32_t count = 0;
        if (Read(res, ids) && Read(res + 0x14, count) && count) {
            g_searchHits[k].fetch_add(1, std::memory_order_relaxed);
            std::uint16_t list[4]{0xffff, 0xffff, 0xffff, 0xffff};
            const unsigned n = count > 4 ? 4u : count;
            ReadBytes(ids, list, n * sizeof(std::uint16_t));
            bool player = false;
            for (unsigned i = 0; i < n; ++i) if ((list[i] & 0xfe00u) == 0) player = true;
            if (player) g_searchPlayers[k].fetch_add(1, std::memory_order_relaxed);
            if (g_searchBudget.load(std::memory_order_relaxed)) {
                g_searchBudget.fetch_sub(1, std::memory_order_relaxed);
                EmitF("coop_carry=search; scan=%u; count=%u; ids=%x,%x,%x,%x\r\n", k, count, unsigned(list[0]), unsigned(list[1]), unsigned(list[2]), unsigned(list[3]));
            }
        }
        break;
    }
    return r;
}
bool InstallSearchTrace() noexcept {
    std::uintptr_t root = 0, services = 0, x = 0, object = 0, vt = 0, fn = 0;
    if (!Read(g_base + kServicesRoot, root) || !root || !Read(root + 8, services) || !services || !Read(services + 0xa8, x) || !x ||
        !Read(x + 0x10, object) || !object || !Read(object, vt) || !vt || !Read(vt, fn) || !fn) { Emit("coop_carry=search_trace_refused; reason=chain\r\n"); return false; }
    if (vt < g_base || vt - g_base >= kImageSpan || fn < g_base || fn - g_base >= 0x208d000) { Emit("coop_carry=search_trace_refused; reason=range\r\n"); return false; }
    g_search = reinterpret_cast<SearchFn>(fn); g_searchSlot = vt; g_searchFn = fn;
    if (const char* why = near_code::PatchQword(reinterpret_cast<void*>(vt), fn, reinterpret_cast<std::uintptr_t>(&HookSearch))) {
        EmitF("coop_carry=search_trace_refused; reason=patch_%s\r\n", why); return false;
    }
    EmitF("coop_carry=search_trace_installed; vt=0x%llx; fn=0x%llx\r\n", static_cast<unsigned long long>(vt - g_base), static_cast<unsigned long long>(fn - g_base));
    return true;
}
// The candidate producer and carrier-init hooks are required (search
// injection, replica pickup); predicate/carry point/raycast/dispatch
// detours are hidden-worker diagnostics only.
bool InstallTrace() noexcept {
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) { Emit("coop_carry=trace_refused; reason=minhook\r\n"); return false; }
    if (MH_CreateHook(reinterpret_cast<void*>(g_base + kProducer), reinterpret_cast<void*>(&HookProducer), reinterpret_cast<void**>(&g_producer)) != MH_OK ||
        MH_EnableHook(reinterpret_cast<void*>(g_base + kProducer)) != MH_OK) { Emit("coop_carry=trace_refused; reason=producer_hook\r\n"); return false; }
    if (worker_scenario::Compiled()) {
        if (MH_CreateHook(reinterpret_cast<void*>(g_base + kCarryPoint), reinterpret_cast<void*>(&HookCarryPoint), reinterpret_cast<void**>(&g_carryPoint)) == MH_OK)
            MH_EnableHook(reinterpret_cast<void*>(g_base + kCarryPoint));
        if (MH_CreateHook(reinterpret_cast<void*>(g_base + kRaycast), reinterpret_cast<void*>(&HookRaycast), reinterpret_cast<void**>(&g_raycast)) == MH_OK)
            MH_EnableHook(reinterpret_cast<void*>(g_base + kRaycast));
        if (MH_CreateHook(reinterpret_cast<void*>(g_base + kPredicate), reinterpret_cast<void*>(&HookPredicate), reinterpret_cast<void**>(&g_predicate)) == MH_OK)
            MH_EnableHook(reinterpret_cast<void*>(g_base + kPredicate));
        std::uintptr_t dispatchSlot = 0;
        g_dispatch = reinterpret_cast<DispatchFn>(g_base + kDispatch);
        if (Read(g_base + kDispatchSlot, dispatchSlot) && dispatchSlot == g_base + kDispatch)
            near_code::PatchQword(reinterpret_cast<void*>(g_base + kDispatchSlot), g_base + kDispatch, reinterpret_cast<std::uintptr_t>(&HookDispatch));
    }
    g_init = reinterpret_cast<InitFn>(g_base + kInitThunk);
    for (const auto slot : kInitSlots) {
        std::uintptr_t now = 0;
        if (!Read(g_base + slot, now) || now != g_base + kInitThunk) { EmitF("coop_carry=trace_refused; reason=init_slot_%llx\r\n", static_cast<unsigned long long>(slot)); continue; }
        if (const char* why = near_code::PatchQword(reinterpret_cast<void*>(g_base + slot), g_base + kInitThunk, reinterpret_cast<std::uintptr_t>(&HookInit)))
            EmitF("coop_carry=trace_refused; reason=init_%s\r\n", why);
    }
    InstallSearchTrace();
    Emit("coop_carry=trace_installed\r\n");
    return true;
}
bool g_traceInstalled = false;

// ---- Presentation isolation ----
// The carried-recipient states request a global player presentation mode
// through the P+C0 interface (vtable 0x23C7500 slot 0xF8 -> 0x11F6520,
// "mov [rcx+0x6d], dl"); it has no body argument, so instance 1's carried
// state would black out the local screen. Inside instance 1's drain job the
// request is dropped; the local player's own requests are untouched.
constexpr std::uintptr_t kModeSlots[] = {0x23c75f8, 0x23c7498};
constexpr std::uintptr_t kSetMode = 0x11f6520;
constexpr unsigned char kSetModeBytes[] = {0x88, 0x51, 0x6d, 0xc3};
using SetModeFn = void (*)(void*, std::uint8_t);
SetModeFn g_setMode = nullptr;
std::atomic<std::uint32_t> g_modeBlocked{0}, g_modePassed{0}, g_lastBlockedMode{0};
std::atomic<bool> g_localAwakeDown{false};
bool LocalBodyCarried() noexcept;
__declspec(noinline) void HookSetMode(void* iface, std::uint8_t mode) noexcept {
    if (g_installed && partner_input_hook::InDrainJob()) {
        g_modeBlocked.fetch_add(1, std::memory_order_relaxed);
        g_lastBlockedMode.store(mode, std::memory_order_relaxed);
        return;
    }
    // A wounded (awake) local player keeps the normal view while carried; the
    // stock carried presentation (mode 4) is the unconscious player's blackout.
    if (g_installed && mode && g_localAwakeDown.load(std::memory_order_relaxed) && LocalBodyCarried()) {
        g_modeBlocked.fetch_add(1, std::memory_order_relaxed);
        g_lastBlockedMode.store(mode, std::memory_order_relaxed);
        return;
    }
    g_modePassed.fetch_add(1, std::memory_order_relaxed);
    g_setMode(iface, mode);
}
bool InstallModeGuard() noexcept {
    unsigned char now[sizeof(kSetModeBytes)]{};
    if (!ReadBytes(g_base + kSetMode, now, sizeof(now)) || std::memcmp(now, kSetModeBytes, sizeof(now)) != 0) {
        Emit("coop_carry=mode_guard_refused; reason=code\r\n"); return false;
    }
    g_setMode = reinterpret_cast<SetModeFn>(g_base + kSetMode);
    bool any = false;
    for (const auto slot : kModeSlots) {
        std::uintptr_t value = 0;
        if (!Read(g_base + slot, value) || value != g_base + kSetMode) continue;
        if (!near_code::PatchQword(reinterpret_cast<void*>(g_base + slot), g_base + kSetMode, reinterpret_cast<std::uintptr_t>(&HookSetMode))) any = true;
    }
    Emit(any ? "coop_carry=mode_guard_installed\r\n" : "coop_carry=mode_guard_refused; reason=slots\r\n");
    return any;
}

// ---- Per-frame carry monitor (logs state changes; bounded) ----
struct Watch {
    std::uint16_t action[2]{};
    std::uint32_t ps0[2]{}, ps1[2]{};
    std::uint8_t reqKind[2]{}, reqSeq[2]{};
    std::uint16_t cTarget = 0xffff;
    std::uint8_t cProgress = 0, cAck = 0, cClosure = 0, cNext = 0, cVariant = 0;
    std::uint8_t rCur = 0, rNext = 0;
    std::uint16_t rCarrier = 0xffff;
    std::uintptr_t rPtr = 0;
};
Watch g_watch{};
std::atomic<std::uint32_t> g_watchBudget{400};

// ---- Carried-player auto wake ----
// The carried-player state (0x1127336..0x1127372) ends the carry the moment the
// carried body's life state (*(Q+8)+id*0x4C+0x34) is 0, i.e. an awake player.
// A wounded co-op teammate is awake but must stay on the carrier's shoulder, so
// the conditional "jne 0x1127725" (keep carrying) becomes unconditional. This
// recipient class is the player-carries-player state only (never NPC carries);
// put-down and carrier exit still end the carry through their stock paths.
constexpr std::uintptr_t kAutoWakeSite = 0x1127372;
constexpr unsigned char kAutoWakeBefore[] = {0x83, 0x7c, 0x08, 0x34, 0x00, 0x0f, 0x85, 0xad, 0x03, 0x00, 0x00};
constexpr unsigned char kAutoWakeAfter[] = {0xe9, 0xae, 0x03, 0x00, 0x00, 0x90};
bool g_autoWakePatched = false;
bool PatchAutoWake() noexcept {
    unsigned char now[sizeof(kAutoWakeBefore)]{};
    if (!ReadBytes(g_base + kAutoWakeSite - 5, now, sizeof(now))) return false;
    if (std::memcmp(now + 5, kAutoWakeAfter, sizeof(kAutoWakeAfter)) == 0) return true;
    if (std::memcmp(now, kAutoWakeBefore, sizeof(kAutoWakeBefore)) != 0) return false;
    void* site = reinterpret_cast<void*>(g_base + kAutoWakeSite);
    DWORD previous = 0, ignored = 0;
    if (!VirtualProtect(site, sizeof(kAutoWakeAfter), PAGE_EXECUTE_READWRITE, &previous)) return false;
    std::memcpy(site, kAutoWakeAfter, sizeof(kAutoWakeAfter));
    VirtualProtect(site, sizeof(kAutoWakeAfter), previous, &ignored);
    FlushInstructionCache(GetCurrentProcess(), site, sizeof(kAutoWakeAfter));
    return true;
}

bool LocalBodyCarried() noexcept {
    std::uintptr_t P = 0, Q = 0; bool online = true; std::uint16_t action = 0;
    return PlayerSystem(P, Q, online) && ReadAction(Q, 0, action) && IsCarriedAction(action);
}

bool GateBytes(std::uintptr_t rva, const unsigned char* bytes, std::size_t size) noexcept {
    unsigned char now[64]{};
    return size <= sizeof(now) && ReadBytes(g_base + rva, now, size) && std::memcmp(now, bytes, size) == 0;
}
bool GateOp0() noexcept {
    static unsigned char body[kOp0Size];
    if (!ReadBytes(g_base + kOp0, body, sizeof(body))) return false;
    // The prologue may already carry an exact private observer detour (E9 rel32).
    if (std::memcmp(body, kOp0Head, sizeof(kOp0Head)) != 0 && body[0] != 0xe9) return false;
    Sha256 sha; sha.Update(body + 5, sizeof(body) - 5);
    unsigned char digest[32]{}; sha.Final(digest);
    return std::memcmp(digest, kOp0TailSha, sizeof(digest)) == 0;
}

} // namespace

bool Install(const void* image, std::size_t span, Log log) noexcept {
    if (g_installed) return g_base == reinterpret_cast<std::uintptr_t>(image);
    if (!image || span < kImageSpan) return false;
    g_base = reinterpret_cast<std::uintptr_t>(image);
    g_log = log;
    std::uintptr_t slot = 0, submitSlot = 0, kind15Slot0 = 0;
    const bool gate = GateBytes(kProviderIsSet, kProviderIsSetBytes, sizeof(kProviderIsSetBytes)) &&
        GateBytes(kProducerSite, kProducerBytes, sizeof(kProducerBytes)) && GateOp0() &&
        Read(g_base + kProviderSlot0, slot) && slot == g_base + kProviderIsSet &&
        Read(g_base + kQSubmitSlot, submitSlot) && submitSlot == g_base + kQSubmit &&
        Read(g_base + kKind15Vtable, kind15Slot0) && kind15Slot0 == g_base + 0xfd48f0;
    for (const auto s : kRecipientSlots) {
        std::uintptr_t now = 0;
        if (!gate || !Read(g_base + s, now) || now != g_base + kOp0) {
            EmitF("coop_carry=refused; reason=code_gate; slot=0x%llx\r\n", static_cast<unsigned long long>(s));
            g_base = 0; return false;
        }
    }
    if (!gate) { Emit("coop_carry=refused; reason=code_gate\r\n"); g_base = 0; return false; }
    g_isSet = reinterpret_cast<IsSetFn>(g_base + kProviderIsSet);
    g_op0 = reinterpret_cast<Op0Fn>(g_base + kOp0);
    if (const char* why = near_code::PatchQword(reinterpret_cast<void*>(g_base + kProviderSlot0), g_base + kProviderIsSet,
                                                reinterpret_cast<std::uintptr_t>(&HookIsSet))) {
        EmitF("coop_carry=refused; reason=provider_patch_%s\r\n", why); g_base = 0; return false;
    }
    for (const auto s : kRecipientSlots) {
        if (const char* why = near_code::PatchQword(reinterpret_cast<void*>(g_base + s), g_base + kOp0, reinterpret_cast<std::uintptr_t>(&HookOp0))) {
            EmitF("coop_carry=refused; reason=recipient_patch_%s\r\n", why);
        }
    }
    g_installed = true;
    g_traceInstalled = InstallTrace();
    InstallModeGuard();
    g_autoWakePatched = PatchAutoWake();
    Emit(g_autoWakePatched ? "coop_carry=auto_wake_patched" "\r\n" : "coop_carry=auto_wake_refused" "\r\n");
    Emit("coop_carry=installed; producer=s_is_online_scan; recipient=offline_kind15; mode=0\r\n");
    return true;
}
namespace {
// Wire: u8 'K' | u8 version 1 | u8 event | u8 flags | u16 sequence | u16 reserved.
constexpr std::uint8_t kWireMagic = 0x4b, kWireVersion = 1;
constexpr std::uint8_t kEventStart = 1, kEventDrop = 2;
std::uint16_t g_txSeq = 0, g_rxSeq = 0xffff;
bool g_localCarrying = false;
std::uint64_t g_localCarryAt = 0;
bool g_mirrorActive = false;
std::uint64_t g_mirrorAt = 0;
bool SendEvent(std::uint8_t event) noexcept {
    if (!g_send) return false;
    ++g_txSeq;
    const std::uint8_t bytes[8] = {kWireMagic, kWireVersion, event, 0, static_cast<std::uint8_t>(g_txSeq & 0xff),
                                   static_cast<std::uint8_t>(g_txSeq >> 8), 0, 0};
    const bool ok = g_send(bytes, sizeof(bytes));
    if (ok) g_wireTx.fetch_add(1, std::memory_order_relaxed);
    EmitF("coop_carry=tx; event=%u; seq=%u; queued=%u\r\n", unsigned(event), unsigned(g_txSeq), ok ? 1u : 0u);
    return ok;
}
// The replica (instance 1) carrying the local body on this machine.
bool ReplicaCarryingLocal() noexcept {
    const auto r = CarrierRecord(1);
    std::uint16_t target = 0xffff; std::uint8_t progress = 0;
    return r && Read(r + 0x78, target) && Read(r + 0x81, progress) && target == 0 && progress != 0;
}
bool RequestReplicaDrop() noexcept {
    std::uintptr_t P = 0, Q = 0, rec = 0; bool online = true; std::uint32_t flags0 = 0, flags1 = 0;
    if (!PlayerSystem(P, Q, online) || !PsRecord(P, 1, rec) || !Read(rec, flags0) || !(flags0 & 0x400000u) || !Read(rec + 4, flags1)) return false;
    return Write<std::uint32_t>(rec + 4, flags1 | 0x4000u);
}
} // namespace

void SetSender(bool (*send)(const std::uint8_t*, std::size_t) noexcept) noexcept { g_send = send; }
bool IsCarryMessage(const std::uint8_t* bytes, std::size_t size) noexcept {
    return bytes && size == 8 && bytes[0] == kWireMagic;
}
bool Receive(const std::uint8_t* bytes, std::size_t size) noexcept {
    if (!IsCarryMessage(bytes, size) || bytes[1] != kWireVersion) return false;
    const std::uint16_t seq = static_cast<std::uint16_t>(bytes[4] | (bytes[5] << 8));
    if (seq == g_rxSeq) return true;
    g_rxSeq = seq;
    g_wireRx.fetch_add(1, std::memory_order_relaxed);
    const std::uint8_t event = bytes[2];
    EmitF("coop_carry=rx; event=%u; seq=%u\r\n", unsigned(event), unsigned(seq));
    if (!g_installed || g_mode.load(std::memory_order_relaxed) == kOff) return true;
    if (event == kEventStart) {
        g_forcePendingStart.store(true, std::memory_order_relaxed);
        g_forcePendingAt.store(GetTickCount64(), std::memory_order_relaxed);
        g_mirrorStarts.fetch_add(1, std::memory_order_relaxed);
    } else if (event == kEventDrop) {
        g_forcePendingStart.store(false, std::memory_order_relaxed);
        g_dropPending.store(true, std::memory_order_relaxed);
        g_mirrorDrops.fetch_add(1, std::memory_order_relaxed);
    }
    return true;
}

void Tick() noexcept {
    if (!g_installed || g_mode.load(std::memory_order_relaxed) == kOff) return;
    {
        // The local player carrying the teammate: announce start and put-down.
        const auto r = CarrierRecord(0);
        std::uint16_t target = 0xffff; std::uint8_t progress = 0, closure = 0;
        const bool carrying = r && Read(r + 0x78, target) && Read(r + 0x81, progress) && Read(r + 0x380, closure) &&
            target == 1 && progress != 0 && closure != 6;
        if (carrying && !g_localCarrying) { g_localCarrying = true; g_localCarryAt = GetTickCount64(); SendEvent(kEventStart); }
        else if (!carrying && g_localCarrying) { g_localCarrying = false; SendEvent(kEventDrop); }
        // A pending mirrored pickup that never started (stale request) expires.
        if (g_forcePendingStart.load(std::memory_order_relaxed) && GetTickCount64() - g_forcePendingAt.load() > 3000)
            g_forcePendingStart.store(false, std::memory_order_relaxed);
        // The replica carrying the local body: the remote put us down.
        const bool mirror = ReplicaCarryingLocal();
        if (mirror != g_mirrorActive) { g_mirrorActive = mirror; g_mirrorAt = GetTickCount64(); EmitF("coop_carry=mirror; active=%u\r\n", mirror ? 1u : 0u); }
        if (g_dropPending.load(std::memory_order_relaxed)) {
            if (!mirror || RequestReplicaDrop()) g_dropPending.store(false, std::memory_order_relaxed);
        }
    }
    Watch w{};
    std::uintptr_t P = 0, Q = 0, requests = 0; bool online = true;
    if (!PlayerSystem(P, Q, online)) return;
    Read(Q + kQRequests, requests);
    for (unsigned id = 0; id < 2; ++id) {
        ReadAction(Q, id, w.action[id]);
        std::uintptr_t rec = 0;
        if (PsRecord(P, id, rec)) { Read(rec, w.ps0[id]); Read(rec + 4, w.ps1[id]); }
        if (requests) { Read(requests + 0x40u * id + 8, w.reqKind[id]); Read(requests + 0x40u * id + 0x11, w.reqSeq[id]); }
    }
    if (const auto c = g_lastCarrierRecord.load()) {
        Read(c + 0x78, w.cTarget); Read(c + 0x81, w.cProgress); Read(c + 0x82, w.cAck);
        Read(c + 0x380, w.cClosure); Read(c + 0x381, w.cNext); Read(c + 0x384, w.cVariant);
    }
    if (const auto i = g_lastRecipientIface.load()) {
        std::uintptr_t node = 0, records = 0; std::uint32_t baseId = 0; const auto id = g_lastRecipientId.load();
        if (Read(i + 8, node) && node && Read(node + 0x24, baseId) && id >= baseId && Read(i + 0x58, records) && records) {
            const auto r = records + std::uintptr_t(id - baseId) * 0x60;
            Read(r + 0x10, w.rCur); Read(r + 0x11, w.rNext); Read(r + 0x28, w.rPtr); Read(r + 0x30, w.rCarrier);
        }
    }
    // The offline recipient init 0x11265E0 re-admits a kind-15 request on
    // every evaluation while its force byte is set (its committed sequence
    // equals the request's). Once the recipient has taken the request, clear
    // force so the running carry is never restarted at substate 1.
    if (requests && g_forcePending.load(std::memory_order_relaxed)) {
        const auto id = g_lastRecipientId.load();
        if (id < 2 && w.reqKind[id] == 0x0f && w.rCur >= 1 && (w.rPtr || w.rCur >= 2)) {
            std::uint8_t force = 0;
            if (Read(requests + 0x40u * id + 0x12, force) && force && Write<std::uint8_t>(requests + 0x40u * id + 0x12, 0)) {
                g_forcePending.store(false, std::memory_order_relaxed);
                g_forceCleared.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    if (std::memcmp(&w, &g_watch, sizeof(w)) == 0) return;
    g_watch = w;
    if (!g_watchBudget.load(std::memory_order_relaxed)) return;
    g_watchBudget.fetch_sub(1, std::memory_order_relaxed);
    EmitF("coop_carry=watch; t=%llu; a=%x,%x; ps=%x,%x/%x,%x; rq=%u.%u,%u.%u; c=t%u,p%u,k%u,cl%u>%u,v%u; r=%u>%u,c%u,%s; blk=%u; fc=%u\r\n",
          static_cast<unsigned long long>(GetTickCount64() % 100000000ull), unsigned(w.action[0]), unsigned(w.action[1]), w.ps0[0], w.ps1[0], w.ps0[1], w.ps1[1],
          unsigned(w.reqKind[0]), unsigned(w.reqSeq[0]), unsigned(w.reqKind[1]), unsigned(w.reqSeq[1]), unsigned(w.cTarget), unsigned(w.cProgress),
          unsigned(w.cAck), unsigned(w.cClosure), unsigned(w.cNext), unsigned(w.cVariant), unsigned(w.rCur), unsigned(w.rNext), unsigned(w.rCarrier),
          w.rPtr ? "bound" : "none", g_modeBlocked.load(), g_forceCleared.load());
}
bool Installed() noexcept { return g_installed; }
void SetMode(unsigned mode) noexcept {
    const unsigned m = mode > kAnyDowned ? kAnyDowned : mode;
    if (g_mode.exchange(m) != m) EmitF("coop_carry=mode; mode=%u\r\n", m);
}
unsigned Mode() noexcept { return g_mode.load(); }
void SetCarryable(unsigned mask) noexcept { g_carryable.store(mask & 3u); }
unsigned Carryable() noexcept { return g_carryable.load(); }

Status GetStatus() noexcept {
    Status s{};
    s.installed = g_installed; s.mode = g_mode.load(); s.carryable = g_carryable.load();
    s.overrides = g_overrides.load(); s.op0Calls = g_op0Calls.load(); s.op0Ours = g_op0Ours.load(); s.op0Accepted = g_op0Accepted.load();
    s.guardSkips = g_guardSkips.load(); s.kind15 = g_kind15.load(); s.kind15Refused = g_kind15Refused.load();
    s.lastCarrier = static_cast<std::uint16_t>(g_lastCarrier.load()); s.lastTarget = static_cast<std::uint16_t>(g_lastTarget.load());
    s.lastTargetAction = static_cast<std::uint16_t>(g_lastAction.load()); s.lastResult = g_lastResult.load();
    if (const auto rec = g_lastCarrierRecord.load()) {
        s.carrierRead = Read(rec + 0x78, s.carrierTarget) && Read(rec + 0x7c, s.carrierTarget2) && Read(rec + 0x81, s.progress) &&
            Read(rec + 0x82, s.ack) && Read(rec + 0x85, s.owned) && Read(rec + 0x384, s.variant) && Read(rec + 0x6a2, s.carrierFlags);
    }
    std::uintptr_t P = 0, Q = 0; bool online = true;
    if (g_base && PlayerSystem(P, Q, online)) {
        for (unsigned id = 0; id < 2; ++id) {
            auto& b = s.body[id];
            std::uintptr_t rec = 0, requests = 0;
            b.read = ReadAction(Q, id, b.action);
            if (PsRecord(P, id, rec)) { Read(rec, b.psFlags0); Read(rec + 4, b.psFlags1); }
            if (Read(Q + kQRequests, requests) && requests) {
                const auto r = requests + std::uintptr_t(id) * kQRequestStride;
                Read(r + 8, b.requestKind); Read(r + 0x11, b.requestSeq); Read(r + 0x12, b.requestForce); Read(r + 0x14, b.requestCarrier);
            }
        }
    }
    return s;
}
bool StatusLine(char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    const auto s = GetStatus();
    const int n = std::snprintf(out, capacity,
        "coopcarry;inst=%u;mode=%u;grant=%u;ovr=%u;op0=%u,%u,%u;skip=%u;k15=%u,%u;last=%u>%u,a%x,r%d;crec=%u,%u,%u,p%u,k%u,o%u,v%u,f%x",
        s.installed ? 1u : 0u, s.mode, s.carryable, s.overrides, s.op0Calls, s.op0Ours, s.op0Accepted, s.guardSkips, s.kind15, s.kind15Refused,
        unsigned(s.lastCarrier), unsigned(s.lastTarget), unsigned(s.lastTargetAction), s.lastResult, s.carrierRead ? 1u : 0u,
        unsigned(s.carrierTarget), unsigned(s.carrierTarget2), unsigned(s.progress), unsigned(s.ack), unsigned(s.owned), unsigned(s.variant),
        unsigned(s.carrierFlags));
    return n >= 0 && static_cast<std::size_t>(n) < capacity;
}
bool BodyLine(char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    const auto s = GetStatus();
    const auto& a = s.body[0]; const auto& b = s.body[1];
    const int n = std::snprintf(out, capacity,
        "carrybodies;b0=%u,a%x,ps%x,%x,rq%u,%u,%u,%u;b1=%u,a%x,ps%x,%x,rq%u,%u,%u,%u",
        a.read ? 1u : 0u, unsigned(a.action), a.psFlags0, a.psFlags1, unsigned(a.requestKind), unsigned(a.requestSeq), unsigned(a.requestForce),
        unsigned(a.requestCarrier), b.read ? 1u : 0u, unsigned(b.action), b.psFlags0, b.psFlags1, unsigned(b.requestKind), unsigned(b.requestSeq),
        unsigned(b.requestForce), unsigned(b.requestCarrier));
    return n >= 0 && static_cast<std::size_t>(n) < capacity;
}
bool TraceLine(char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    const int n = std::snprintf(out, capacity, "carrytrace;i=%u;pred=%u,%u,%u;lp=%u,%u;init=%u,%u;li=%u,%u,t%u,v%u;s=%u,%u,%u/%u,%u,%u/%u,%u,%u;d=%u,%u;pr=%u,%u;inj=%u",
        g_traceInstalled ? 1u : 0u, g_predCalls.load(), g_predPlayer.load(), g_predPlayerOk.load(), g_lastPredId.load(), g_lastPredOk.load(),
        g_initCalls.load(), g_initOk.load(), g_lastInitId.load(), g_lastInitOk.load(), g_lastInitTarget.load(), g_lastInitVariant.load(),
        g_searchCalls[0].load(), g_searchHits[0].load(), g_searchPlayers[0].load(), g_searchCalls[1].load(), g_searchHits[1].load(),
        g_searchPlayers[1].load(), g_searchCalls[2].load(), g_searchHits[2].load(), g_searchPlayers[2].load(), g_dispatchCalls.load(), g_dispatchPlayer.load(), g_producerCalls.load(), g_producerFound.load(), g_injected.load());
    return n >= 0 && static_cast<std::size_t>(n) < capacity;
}
bool MirrorLine(char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    const auto r0 = CarrierRecord(0), r1 = CarrierRecord(1);
    std::uint16_t t0 = 0xffff, t1 = 0xffff; std::uint8_t p0 = 0, p1 = 0, c0 = 0, c1 = 0;
    if (r0) { Read(r0 + 0x78, t0); Read(r0 + 0x81, p0); Read(r0 + 0x380, c0); }
    if (r1) { Read(r1 + 0x78, t1); Read(r1 + 0x81, p1); Read(r1 + 0x380, c1); }
    const int n = std::snprintf(out, capacity, "carrymirror;tx=%u;rx=%u;starts=%u;drops=%u;forced=%u,r%d;pend=%u;local=%u;mirror=%u;c0=t%u,p%u,cl%u;c1=t%u,p%u,cl%u",
        g_wireTx.load(), g_wireRx.load(), g_mirrorStarts.load(), g_mirrorDrops.load(), g_forcedSent.load(),
        static_cast<std::int32_t>(g_forcedResult.load()), g_forcePendingStart.load() ? 1u : 0u, g_localCarrying ? 1u : 0u,
        g_mirrorActive ? 1u : 0u, unsigned(t0), unsigned(p0), unsigned(c0), unsigned(t1), unsigned(p1), unsigned(c1));
    return n >= 0 && static_cast<std::size_t>(n) < capacity;
}
bool SelectorLine(char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    const int n = std::snprintf(out, capacity, "carrysel;pflags=%x;owner=%u;kind=%x;cur=%x;valid=%x;vor=%x;dis=%x;target=%u;cp=%u,%u,%d,%d,%d;ray=%u,%u",
        g_selPflags.load(), g_selOwnerOk.load(), g_selKind.load(), g_selCur.load(), g_selValid.load(), g_selValidOr.exchange(0), g_selDis.load(), g_selTarget.load(), g_cpCalls.load(), g_cpOk.load(), g_cpX.load(), g_cpY.load(), g_cpZ.load(), g_rayCalls.load(), g_rayBlocked.load());
    return n >= 0 && static_cast<std::size_t>(n) < capacity;
}
bool BodyCarried(unsigned id) noexcept {
    std::uintptr_t P = 0, Q = 0; bool online = true; std::uint16_t action = 0;
    return id < 2 && g_base && PlayerSystem(P, Q, online) && ReadAction(Q, id, action) && IsCarriedAction(action);
}
bool CarryPoint(unsigned id, float (&out)[3]) noexcept {
    out[0] = out[1] = out[2] = 0;
    if (!g_installed || id >= 2) return false;
    using Fn = bool (*)(std::uint16_t, float*);
    const auto fn = g_carryPoint ? g_carryPoint : reinterpret_cast<Fn>(g_base + kCarryPoint);
    float point[4]{};
    bool ok = false;
    __try { ok = fn(static_cast<std::uint16_t>(id), point); } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    if (!ok) return false;
    out[0] = point[0]; out[1] = point[1]; out[2] = point[2];
    return true;
}
void SetLocalAwakeDown(bool awake) noexcept { g_localAwakeDown.store(awake, std::memory_order_relaxed); }
bool RequestLocalPutDown() noexcept {
    if (!g_installed || !g_localCarrying) return false;
    std::uintptr_t P = 0, Q = 0, rec = 0; bool online = true; std::uint32_t flags0 = 0, flags1 = 0;
    if (!PlayerSystem(P, Q, online) || !PsRecord(P, 0, rec) || !Read(rec, flags0) || !(flags0 & 0x400000u) || !Read(rec + 4, flags1)) return false;
    if (flags1 & 0x4000u) return true;
    return Write<std::uint32_t>(rec + 4, flags1 | 0x4000u);
}
unsigned CarryRole() noexcept {
    if (!g_installed || g_mode.load(std::memory_order_relaxed) == kOff) return 0;
    if (g_localCarrying) return 1;
    if (g_mirrorActive) return 2;
    return 0;
}
bool PartnerCarryHold() noexcept {
    return g_installed && g_mode.load(std::memory_order_relaxed) != kOff && BodyCarried(1);
}
bool BodyCarrying(unsigned id) noexcept {
    std::uintptr_t P = 0, Q = 0, rec = 0; bool online = true; std::uint32_t flags = 0;
    return id < 2 && g_base && PlayerSystem(P, Q, online) && PsRecord(P, id, rec) && Read(rec, flags) && (flags & 0x400000u) != 0;
}

} // namespace mgscoop::coop_carry
