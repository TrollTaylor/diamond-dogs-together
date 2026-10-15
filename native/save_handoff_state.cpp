// Original MGS Co-op automatic save handoff: shared UI state (join-fix phase 2
// P2-14; docs/save-handoff.md). A seqlock over packed atomic words: the
// publisher (the handoff glue on the receive thread) never waits on a reader,
// readers (F9 UI, worker state.json, the CFG reply, link-status) retry while a
// publish is in flight. Constant-initialized, trivially destructible, no
// allocation, no Windows headers (mgs_coop_core).
#include "mgscoop/save_handoff_state.hpp"

#include <atomic>
#include <thread>

namespace mgscoop::save_handoff_state {
namespace {

// The view packed into four words (travel_unlocked lives in its own flag):
//   w0 = available | role << 8 | token << 16 | pct << 24
//   w1 = why | lua_code << 8 | can_keep << 16
//   w2 = host_mission, w3 = bytes
constexpr unsigned kWords = 4;
std::atomic<std::uint32_t> g_sequence{0};  // odd while a publish is in flight
std::atomic<std::uint32_t> g_words[kWords] = {};
std::atomic<bool> g_writer{false};         // serializes publishers (normally one)
std::atomic<bool> g_keep{false};
std::atomic<bool> g_unlocked{false};

constexpr const char* kTokenNames[kTokenCount] = {
    "off",       "idle",   "same",      "wait_host", "offered", "sending", "partner_staged", "partner_restarting",
    "receiving", "staged", "countdown", "exiting",   "kept",    "refused", "failed",         "applied",
};

void Pack(const View& v, std::uint32_t (&w)[kWords]) noexcept {
    w[0] = (v.available ? 1u : 0u) | (static_cast<std::uint32_t>(v.role) << 8) |
           (static_cast<std::uint32_t>(v.token) << 16) | (static_cast<std::uint32_t>(v.pct) << 24);
    w[1] = static_cast<std::uint32_t>(v.why) | (static_cast<std::uint32_t>(v.lua_code) << 8) |
           ((v.can_keep ? 1u : 0u) << 16);
    w[2] = v.host_mission;
    w[3] = v.bytes;
}
View Unpack(const std::uint32_t (&w)[kWords]) noexcept {
    View v{};
    v.available = (w[0] & 0xFFu) != 0;
    v.role = static_cast<std::uint8_t>(w[0] >> 8);
    v.token = static_cast<std::uint8_t>(w[0] >> 16);
    v.pct = static_cast<std::uint8_t>(w[0] >> 24);
    v.why = static_cast<std::uint8_t>(w[1]);
    v.lua_code = static_cast<std::uint8_t>(w[1] >> 8);
    v.can_keep = ((w[1] >> 16) & 0xFFu) != 0;
    v.host_mission = w[2];
    v.bytes = w[3];
    return v;
}

}  // namespace

const char* TokenName(std::uint8_t token) noexcept { return token < kTokenCount ? kTokenNames[token] : kTokenNames[0]; }

void Publish(const View& view) noexcept {
    std::uint32_t w[kWords];
    Pack(view, w);
    while (g_writer.exchange(true, std::memory_order_acquire))
        while (g_writer.load(std::memory_order_relaxed)) std::this_thread::yield();
    const std::uint32_t start = g_sequence.load(std::memory_order_relaxed);
    g_sequence.store(start + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    for (unsigned i = 0; i < kWords; ++i) g_words[i].store(w[i], std::memory_order_relaxed);
    g_sequence.store(start + 2, std::memory_order_release);
    g_writer.store(false, std::memory_order_release);
}

View Get() noexcept {
    std::uint32_t w[kWords];
    for (unsigned spins = 0;; ++spins) {
        const std::uint32_t before = g_sequence.load(std::memory_order_acquire);
        if (before & 1u) {
            if (spins > 64) std::this_thread::yield();
            continue;
        }
        for (unsigned i = 0; i < kWords; ++i) w[i] = g_words[i].load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (g_sequence.load(std::memory_order_relaxed) == before) break;
    }
    View view = Unpack(w);
    view.travel_unlocked = g_unlocked.load(std::memory_order_acquire);
    return view;
}

void RequestKeepOwnSave() noexcept { g_keep.store(true, std::memory_order_release); }
bool TakeKeepOwnSave() noexcept { return g_keep.exchange(false, std::memory_order_acq_rel); }

void SetTravelUnlocked(bool unlocked) noexcept { g_unlocked.store(unlocked, std::memory_order_release); }
bool TravelUnlocked() noexcept { return g_unlocked.load(std::memory_order_acquire); }

void ResetForTest() noexcept {
    Publish(View{});
    g_keep.store(false, std::memory_order_release);
    g_unlocked.store(false, std::memory_order_release);
}

}  // namespace mgscoop::save_handoff_state
