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
Command dispatched[64]{};
std::size_t dispatch_at{};
std::uint32_t camp_id{}, route_id{}, spawn_id{}, metadata_id{};
bool camps_open{}, routes_open{};
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
void ClearEnemies() noexcept {
    state.enemy_camp_count = state.enemy_route_count = state.enemy_spawn_hash = 0;
    state.enemy_spawn_valid = false;
    std::memset(state.enemy_camps, 0, sizeof(state.enemy_camps));
    std::memset(state.enemy_routes, 0, sizeof(state.enemy_routes));
    std::memset(state.enemy_spawn_pos_mm, 0, sizeof(state.enemy_spawn_pos_mm));
    state.enemy_routes_camp[0] = 0;
    camp_id = route_id = spawn_id = metadata_id = 0;
    camps_open = routes_open = false;
}
bool Newer(std::uint32_t value, std::uint32_t previous) noexcept {
    const auto delta = value - previous;
    return !previous || (delta && delta < 0x80000000u);
}
struct Token {
    const char* text{};
    std::size_t size{};
    bool Is(const char* expected) const noexcept {
        return size == std::strlen(expected) && std::memcmp(text, expected, size) == 0;
    }
    bool Name() const noexcept {
        if (!size || size > 64) return false;
        for (std::size_t i = 0; i < size; ++i) {
            const auto c = text[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '-' || c == '.')) return false;
        }
        return true;
    }
    void CopyTo(char* target) const noexcept { std::memcpy(target, text, size); target[size] = 0; }
    bool Unsigned(std::uint32_t max, std::uint32_t& out) const noexcept {
        if (!size) return false;
        std::uint64_t n = 0;
        for (std::size_t i = 0; i < size; ++i) {
            if (text[i] < '0' || text[i] > '9') return false;
            const auto digit = static_cast<unsigned>(text[i] - '0');
            if (n > (static_cast<std::uint64_t>(max) - digit) / 10 || digit > max) return false;
            n = n * 10 + digit;
        }
        out = static_cast<std::uint32_t>(n);
        return true;
    }
    bool Signed(std::int32_t min, std::int32_t max, std::int32_t& out) const noexcept {
        const bool negative = size && text[0] == '-';
        Token digits{ text + (negative ? 1 : 0), size - (negative ? 1 : 0) };
        std::uint32_t n = 0;
        const auto bound = static_cast<std::uint32_t>(negative ? -static_cast<std::int64_t>(min) : max);
        if (!digits.Unsigned(bound, n)) return false;
        out = static_cast<std::int32_t>(negative ? -static_cast<std::int64_t>(n) : n);
        return out >= min && out <= max;
    }
};
struct Tokens {
    const char* text{};
    std::size_t size{}, at{};
    Token Next() noexcept {
        while (at < size && text[at] == ' ') ++at;
        const auto start = at;
        while (at < size && text[at] != ' ') ++at;
        return { text + start, at - start };
    }
    bool End() noexcept { return Next().size == 0; }
};
bool EnemyReceiptLocked(const char* text, std::size_t size) noexcept {
    constexpr char prefix[] = "return 'MGS_COOP_DEBUG_ENEMY_V1' -- ";
    constexpr auto prefix_size = sizeof(prefix) - 1;
    if (!state.enabled || !state.gameplay_ready || state.terminating || !text || size <= prefix_size || size >= 256 ||
        std::memcmp(text, prefix, prefix_size) != 0) return false;
    for (std::size_t i = prefix_size; i < size; ++i) if (text[i] < 0x20 || text[i] > 0x7e) return false;
    Tokens reader{ text + prefix_size, size - prefix_size };
    std::uint32_t id = 0;
    if (!reader.Next().Unsigned(0xffffffffu, id) || !id) return false;
    const Command* author = nullptr;
    for (const auto& cmd : dispatched) if (cmd.id == id && cmd.generation == state.generation) { author = &cmd; break; }
    if (!author) return false;
    const auto kind = reader.Next();
    const bool list = std::strcmp(author->text, "enemy_camps") == 0;
    const bool route = std::strncmp(author->text, "enemy_routes ", 13) == 0;
    if (kind.Is("clear")) {
        std::uint32_t mission = 0;
        if (!list || !reader.Next().Unsigned(99999, mission) || mission != state.mission || !reader.End() || !Newer(id, metadata_id)) return false;
        ClearEnemies(); camp_id = metadata_id = id; camps_open = true;
        return true;
    }
    if (kind.Is("camp")) {
        const auto name = reader.Next();
        std::int32_t distance = 0;
        std::uint32_t count_routes = 0;
        if (!list || !camps_open || id != camp_id || id != metadata_id || !name.Name() || !reader.Next().Signed(-1, 1000000000, distance) ||
            !reader.Next().Unsigned(64, count_routes) || !count_routes || !reader.End() || state.enemy_camp_count >= 32) return false;
        for (std::uint32_t i = 0; i < state.enemy_camp_count; ++i) if (name.Is(state.enemy_camps[i].name)) return false;
        auto& camp = state.enemy_camps[state.enemy_camp_count++];
        name.CopyTo(camp.name); camp.distance_mm = distance; camp.route_count = count_routes;
        return true;
    }
    if (kind.Is("routes")) {
        const auto name = reader.Next();
        if (!route || !name.Name() || !reader.End() || !Newer(id, metadata_id)) return false;
        char expected[kCommandCapacity]{};
        std::snprintf(expected, sizeof(expected), "enemy_routes %.*s", static_cast<int>(name.size), name.text);
        if (std::strcmp(author->text, expected) != 0) return false;
        bool present = false;
        for (std::uint32_t i = 0; i < state.enemy_camp_count; ++i) if (name.Is(state.enemy_camps[i].name)) present = true;
        if (!present) return false;
        state.enemy_route_count = 0;
        std::memset(state.enemy_routes, 0, sizeof(state.enemy_routes));
        name.CopyTo(state.enemy_routes_camp);
        route_id = metadata_id = id; routes_open = true;
        return true;
    }
    if (kind.Is("route")) {
        std::uint32_t index = 0;
        const auto index_token = reader.Next(), name = reader.Next();
        if (!route || !routes_open || id != route_id || id != metadata_id || !index_token.Unsigned(64, index) || index != state.enemy_route_count + 1 ||
            !name.Name() || !reader.End() || state.enemy_route_count >= 64) return false;
        for (std::uint32_t i = 0; i < state.enemy_route_count; ++i) if (name.Is(state.enemy_routes[i].name)) return false;
        auto& entry = state.enemy_routes[state.enemy_route_count++];
        entry.index = index; name.CopyTo(entry.name);
        return true;
    }
    if (kind.Is("done")) {
        if (!reader.End() || id != metadata_id) return false;
        if (list && camps_open && id == camp_id) { camps_open = false; return true; }
        if (route && routes_open && id == route_id) { routes_open = false; return true; }
        return false;
    }
    if (kind.Is("spawned")) {
        std::int32_t position[3]{};
        if (!state.host || std::strncmp(author->text, "spawn_enemy ", 12) != 0 || !Newer(id, metadata_id)) return false;
        for (auto& axis : position) if (!reader.Next().Signed(-1000000000, 1000000000, axis)) return false;
        std::uint32_t hash = 0;
        if (!reader.Next().Unsigned(0xffffffffu, hash) || !reader.End()) return false;
        std::memcpy(state.enemy_spawn_pos_mm, position, sizeof(position));
        state.enemy_spawn_hash = hash; state.enemy_spawn_valid = true; spawn_id = metadata_id = id;
        return true;
    }
    return false;
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
    ClearEnemies(); std::memset(dispatched, 0, sizeof(dispatched)); dispatch_at = 0;
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
    ClearEnemies(); std::memset(dispatched, 0, sizeof(dispatched)); dispatch_at = 0;
    ReleaseSRWLockExclusive(&lock);
}
void EndMission() noexcept {
    AcquireSRWLockExclusive(&lock);
    Clear("Mission ended; pending actions discarded.");
    state.gameplay_ready = false;
    state.terminating = true;
    state.pose_valid = state.body_valid = false;
    ClearEnemies(); std::memset(dispatched, 0, sizeof(dispatched)); dispatch_at = 0;
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
    state.mission = previous.mission;
    state.gameplay_ready = previous.gameplay_ready;
    state.terminating = previous.terminating;
    state.infinite_ammo = previous.infinite_ammo;
    state.bookmark_mask = previous.bookmark_mask;
    state.queue_depth = static_cast<std::uint32_t>(count);
    state.last_id = previous.last_id;
    state.completed = previous.completed;
    state.rejected = previous.rejected;
    state.enemy_camp_count = previous.enemy_camp_count;
    state.enemy_route_count = previous.enemy_route_count;
    state.enemy_spawn_hash = previous.enemy_spawn_hash;
    state.enemy_spawn_valid = previous.enemy_spawn_valid;
    std::memcpy(state.enemy_camps, previous.enemy_camps, sizeof(state.enemy_camps));
    std::memcpy(state.enemy_routes, previous.enemy_routes, sizeof(state.enemy_routes));
    std::memcpy(state.enemy_routes_camp, previous.enemy_routes_camp, sizeof(state.enemy_routes_camp));
    std::memcpy(state.enemy_spawn_pos_mm, previous.enemy_spawn_pos_mm, sizeof(state.enemy_spawn_pos_mm));
    std::memcpy(state.last_command, previous.last_command, sizeof(state.last_command));
    std::memcpy(state.last_result, previous.last_result, sizeof(state.last_result));
    std::memcpy(state.history, previous.history, sizeof(state.history));
    Copy(state.role, sizeof(state.role), state.host ? "host" : "guest");
    ReleaseSRWLockExclusive(&lock);
}
namespace {
bool QueueCommandImpl(const char* text, std::uint32_t mission, std::uint32_t generation) noexcept {
    AcquireSRWLockExclusive(&lock);
    const char* refusal = generation && (generation != state.generation || mission != state.mission) ? "Debug mission generation changed." :
        !state.enabled ? "Debug is disabled." : !state.gameplay_ready || state.terminating ?
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
}
bool QueueCommand(const char* text) noexcept { return QueueCommandImpl(text, 0, 0); }
bool QueueCommandForGeneration(const char* text, std::uint32_t mission, std::uint32_t generation) noexcept {
    return mission && generation && QueueCommandImpl(text, mission, generation);
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
    if (current) dispatched[dispatch_at++ % 64] = value;
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
bool EnemyReceipt(const char* text, std::size_t size) noexcept {
    AcquireSRWLockExclusive(&lock);
    const bool accepted = EnemyReceiptLocked(text, size);
    ReleaseSRWLockExclusive(&lock);
    return accepted;
}
} // namespace mgscoop::debug_menu
