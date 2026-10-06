#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "mgscoop/debug_menu.hpp"

namespace mgscoop::debug_menu {
namespace {
SRWLOCK lock = SRWLOCK_INIT;
std::atomic<bool> enabled{false};
Snapshot state{};
Command queue[kQueueCapacity]{};
std::size_t head{}, count{};
std::uint32_t next_id{};
void Copy(char* dst, std::size_t capacity, const char* src) noexcept {
    if (!capacity) return;
    std::size_t n = 0;
    if (src) while (n + 1 < capacity && src[n]) { dst[n] = src[n]; ++n; }
    dst[n] = 0;
}
bool Valid(const char* text) noexcept {
    if (!text || !*text) return false;
    std::size_t n = 0;
    for (; n < kCommandCapacity && text[n]; ++n) {
        const unsigned char c = static_cast<unsigned char>(text[n]);
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == ' ' || c == '_' || c == '-' || c == '+' || c == '.' || c == '=' || c == ',' || c == ':')) return false;
    }
    return n < kCommandCapacity;
}
void AddHistory(std::uint32_t id, bool ok, const char* message) noexcept {
    for (std::size_t i = 7; i > 0; --i) std::memcpy(state.history[i], state.history[i - 1], sizeof(state.history[i]));
    std::snprintf(state.history[0], sizeof(state.history[0]), "#%u %s: %.185s", id, ok ? "OK" : "REFUSED", message ? message : "unknown");
}
void Reject(const char* reason) noexcept {
    ++state.rejected;
    Copy(state.last_result, sizeof(state.last_result), reason);
    AddHistory(0, false, reason);
}
void Clear(const char* reason) noexcept {
    if (count) { Reject(reason); }
    head = count = 0;
    state.queue_depth = 0;
}
}
bool Enabled() noexcept { return enabled.load(std::memory_order_acquire); }
Snapshot GetSnapshot() noexcept {
    AcquireSRWLockShared(&lock);
    const Snapshot copy = state;
    ReleaseSRWLockShared(&lock);
    return copy;
}
void Configure(bool allowed, bool host, bool saves_redirected) noexcept {
    allowed = allowed && saves_redirected;
    AcquireSRWLockExclusive(&lock);
    state = {};
    state.enabled = allowed;
    state.offline = allowed;
    state.saves_redirected = saves_redirected;
    state.host = host;
    Copy(state.role, sizeof(state.role), host ? "host" : "guest");
    Copy(state.last_result, sizeof(state.last_result), allowed ? "Ready. F9 opens the debug menu." : "Debug disabled outside offline copied-save sessions.");
    head = count = 0;
    next_id = 0;
    enabled.store(allowed, std::memory_order_release);
    ReleaseSRWLockExclusive(&lock);
}
void BeginMission(std::uint32_t mission) noexcept {
    AcquireSRWLockExclusive(&lock);
    Clear("Mission changed; pending actions discarded.");
    ++state.generation;
    state.mission = mission;
    state.terminating = false;
    state.gameplay_ready = state.enabled && (mission == 30010 || mission == 30020 || mission == 30050);
    state.bookmark_mask = 0;
    state.infinite_ammo = false;
    state.pose_valid = state.body_valid = false;
    ReleaseSRWLockExclusive(&lock);
}
void EndMission() noexcept {
    AcquireSRWLockExclusive(&lock);
    Clear("Mission ended; pending actions discarded.");
    state.gameplay_ready = false;
    state.terminating = true;
    state.pose_valid = state.body_valid = false;
    ReleaseSRWLockExclusive(&lock);
}
void Publish(const Snapshot& value) noexcept {
    AcquireSRWLockExclusive(&lock);
    // Lifecycle/action fields belong to the bridge, not a telemetry publisher.
    const Snapshot previous = state;
    state = value;
    state.enabled = previous.enabled;
    state.offline = previous.offline;
    state.saves_redirected = previous.saves_redirected;
    state.host = previous.host;
    state.generation = previous.generation;
    state.gameplay_ready = previous.gameplay_ready;
    state.terminating = previous.terminating;
    state.infinite_ammo = previous.infinite_ammo;
    state.bookmark_mask = previous.bookmark_mask;
    state.queue_depth = static_cast<std::uint32_t>(count);
    state.last_id = previous.last_id;
    state.completed = previous.completed;
    state.rejected = previous.rejected;
    std::memcpy(state.last_command, previous.last_command, sizeof(state.last_command));
    std::memcpy(state.last_result, previous.last_result, sizeof(state.last_result));
    std::memcpy(state.history, previous.history, sizeof(state.history));
    Copy(state.role, sizeof(state.role), state.host ? "host" : "guest");
    ReleaseSRWLockExclusive(&lock);
}
bool QueueCommand(const char* text) noexcept {
    AcquireSRWLockExclusive(&lock);
    const char* refusal = !state.enabled ? "Debug is disabled." : !state.gameplay_ready || state.terminating ?
        "Wait for supported free-roam or Mother Base gameplay." : !Valid(text) ? "Invalid command text." :
        count == kQueueCapacity ? "Action queue full; wait for the game update." : nullptr;
    if (refusal) { Reject(refusal); ReleaseSRWLockExclusive(&lock); return false; }
    Command cmd{};
    cmd.id = ++next_id;
    if (!cmd.id) cmd.id = ++next_id;
    cmd.generation = state.generation;
    Copy(cmd.text, sizeof(cmd.text), text);
    queue[(head + count) % kQueueCapacity] = cmd;
    ++count;
    state.queue_depth = static_cast<std::uint32_t>(count);
    state.last_id = cmd.id;
    Copy(state.last_command, sizeof(state.last_command), text);
    Copy(state.last_result, sizeof(state.last_result), "Queued for the natural game update.");
    ReleaseSRWLockExclusive(&lock);
    return true;
}
bool Pop(Command& value) noexcept {
    AcquireSRWLockExclusive(&lock);
    if (!state.enabled || !state.gameplay_ready || state.terminating || !count) {
        ReleaseSRWLockExclusive(&lock); return false;
    }
    value = queue[head];
    head = (head + 1) % kQueueCapacity;
    --count;
    state.queue_depth = static_cast<std::uint32_t>(count);
    const bool current = value.generation == state.generation;
    if (!current) Reject("Stale mission action discarded.");
    ReleaseSRWLockExclusive(&lock);
    return current;
}
void Result(std::uint32_t id, bool ok, const char* message) noexcept {
    AcquireSRWLockExclusive(&lock);
    ++state.completed;
    if (!ok) ++state.rejected;
    if (id == state.last_id) Copy(state.last_result, sizeof(state.last_result), message);
    AddHistory(id, ok, message);
    ReleaseSRWLockExclusive(&lock);
}
void LuaState(bool ammo, std::uint32_t bookmarks) noexcept {
    AcquireSRWLockExclusive(&lock);
    state.infinite_ammo = ammo;
    state.bookmark_mask = bookmarks & 255u;
    ReleaseSRWLockExclusive(&lock);
}
} // namespace mgscoop::debug_menu
