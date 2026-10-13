// Stock player sleep for either co-op body. See include/mgscoop/coop_sleep.hpp.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include "mgscoop/coop_sleep.hpp"

namespace mgscoop::coop_sleep {
namespace {
constexpr std::uintptr_t kImageSpan = 0xa01d000;
constexpr std::uintptr_t kPlayerSlot = 0x2a82c30, kPVtable = 0x2295490, kQVtable = 0x23ad970;
constexpr std::size_t kPCount = 0x208, kPLocal = 0x214, kPQ = 0x60, kQLife = 0x8, kQAction = 0x18;
constexpr std::size_t kLifeStride = 0x4c, kStun = 0x2c, kStunMax = 0x30, kLifeState = 0x34;
constexpr std::uint32_t kAsleep = 2;

std::uintptr_t g_base = 0;
struct State {
    bool held = false, waking = false, seeded = false;
    std::uint64_t wakeAt = 0, seededAt = 0;
    float restoreStun = 0;
    std::uint32_t reseeds = 0, keeps = 0;
};
State g_state[2]{};
SRWLOCK g_lock = SRWLOCK_INIT;

template <class T> bool Get(std::uintptr_t p, T& out) noexcept {
    SIZE_T n = 0;
    return p && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(p), &out, sizeof(out), &n) && n == sizeof(out);
}
template <class T> bool Put(std::uintptr_t p, T value) noexcept {
    __try { *reinterpret_cast<volatile T*>(p) = value; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Record(unsigned id, std::uintptr_t& life, std::uintptr_t& Q) noexcept {
    life = Q = 0;
    std::uintptr_t P = 0, vt = 0, qvt = 0, records = 0; std::uint32_t count = 0, local = 0;
    if (id >= 2 || !g_base || !Get(g_base + kPlayerSlot, P) || !P || !Get(P, vt) || vt != g_base + kPVtable ||
        !Get(P + kPCount, count) || count != 2 || !Get(P + kPLocal, local) || local != 0 ||
        !Get(P + kPQ, Q) || !Q || !Get(Q, qvt) || qvt != g_base + kQVtable || !Get(Q + kQLife, records) || !records) return false;
    life = records + std::uintptr_t(id) * kLifeStride;
    return true;
}
bool Seed(std::uintptr_t life) noexcept {
    float stun = 0; std::uint32_t state = 0;
    if (!Get(life + kStun, stun) || !Get(life + kLifeState, state)) return false;
    if (state == kAsleep) return true;
    if (state != 0) return false; // another stock state (faint, death) is never overwritten
    return Put<float>(life + kStun, 0.f) && Put<std::uint32_t>(life + kLifeState, kAsleep);
}
} // namespace

bool Configure(const void* image, std::size_t span) noexcept {
    if (!image || span < kImageSpan) return false;
    g_base = reinterpret_cast<std::uintptr_t>(image);
    return true;
}
bool Configured() noexcept { return g_base != 0; }

bool Hold(unsigned id, bool on) noexcept {
    if (id >= 2) return false;
    AcquireSRWLockExclusive(&g_lock);
    std::uintptr_t life = 0, Q = 0;
    bool ok = Record(id, life, Q);
    auto& s = g_state[id];
    if (ok && on) {
        if (!s.held) {
            float max = 0;
            Get(life + kStunMax, max);
            s.restoreStun = std::isfinite(max) && max > 0 ? max : 0;
        }
        ok = Seed(life);
        if (ok) { s.held = true; s.waking = false; s.seeded = true; s.seededAt = GetTickCount64(); }
    } else if (ok && !on) {
        if (s.held) {
            std::uint32_t state = 0;
            if (Get(life + kLifeState, state) && state == kAsleep) Put<std::uint32_t>(life + kLifeState, 0);
            s.held = false; s.waking = true; s.wakeAt = GetTickCount64();
        }
    } else if (!ok && !on) {
        s = State{}; ok = true;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return ok;
}
bool Held(unsigned id) noexcept {
    if (id >= 2) return false;
    AcquireSRWLockShared(&g_lock);
    const bool held = g_state[id].held;
    ReleaseSRWLockShared(&g_lock);
    return held;
}

void Tick() noexcept {
    if (!g_base) return;
    AcquireSRWLockExclusive(&g_lock);
    const auto now = GetTickCount64();
    for (unsigned id = 0; id < 2; ++id) {
        auto& s = g_state[id];
        if (!s.held && !s.waking) continue;
        std::uintptr_t life = 0, Q = 0;
        if (!Record(id, life, Q)) { s = State{}; continue; }
        float stun = 0; std::uint32_t state = 0;
        if (!Get(life + kStun, stun) || !Get(life + kLifeState, state)) continue;
        if (s.held) {
            if (state == kAsleep && stun > 0) { Put<float>(life + kStun, 0.f); ++s.keeps; }
            else if (state == 0) { if (Seed(life)) ++s.reseeds; }
        } else if (s.waking) {
            // After the stock wake-up, give the body its stamina back so it
            // does not faint again on the empty gauge.
            if (state == 0 && now - s.wakeAt > 400) {
                if (s.restoreStun > 0 && stun < s.restoreStun) Put<float>(life + kStun, s.restoreStun);
                s = State{};
            } else if (now - s.wakeAt > 8000) {
                s = State{};
            }
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
}

Body Read(unsigned id) noexcept {
    Body b{};
    std::uintptr_t life = 0, Q = 0, actions = 0;
    if (!Record(id, life, Q)) return b;
    b.read = Get(life + kStun, b.stun) && Get(life + kStunMax, b.stunMax) && Get(life + kLifeState, b.lifeState);
    if (Get(Q + kQAction, actions) && actions) Get(actions + 2u * id, b.action);
    b.held = Held(id);
    return b;
}
bool StatusLine(char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    const auto a = Read(0), b = Read(1);
    const int n = std::snprintf(out, capacity, "coopsleep;b0=%u,h%u,l%u,s%.0f/%.0f,a%x;b1=%u,h%u,l%u,s%.0f/%.0f,a%x;keep=%u,%u;reseed=%u,%u",
        a.read ? 1u : 0u, a.held ? 1u : 0u, a.lifeState, double(a.stun), double(a.stunMax), unsigned(a.action),
        b.read ? 1u : 0u, b.held ? 1u : 0u, b.lifeState, double(b.stun), double(b.stunMax), unsigned(b.action),
        g_state[0].keeps, g_state[1].keeps, g_state[0].reseeds, g_state[1].reseeds);
    return n >= 0 && static_cast<std::size_t>(n) < capacity;
}

} // namespace mgscoop::coop_sleep
