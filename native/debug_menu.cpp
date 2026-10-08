#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "mgscoop/debug_menu.hpp"
#include "mgscoop/debug_actions.hpp"

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
std::uint32_t camp_id{}, route_id{}, spawn_id{}, metadata_id{}, nearby_id{};
bool camps_open{}, routes_open{}, nearby_open{};
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
void AddReceipt(std::uint32_t id, std::uint8_t status, const char* command, const char* result) noexcept {
    // A final result replaces its own interim receipt; otherwise the oldest drops.
    std::size_t slot = kReceiptCount - 1;
    if (id && status != 2)
        for (std::size_t i = 0; i < kReceiptCount; ++i)
            if (state.receipts[i].id == id && state.receipts[i].status == 2) { slot = i; break; }
    for (std::size_t i = slot; i > 0; --i) state.receipts[i] = state.receipts[i - 1];
    Receipt& entry = state.receipts[0];
    entry = {};
    entry.id = id; entry.status = status;
    Copy(entry.command, sizeof(entry.command), command);
    Copy(entry.result, sizeof(entry.result), result ? result : "unknown");
}
const char* DispatchedText(std::uint32_t id) noexcept {
    if (!id) return "";
    for (std::size_t n = 0; n < 64; ++n) {
        const auto& cmd = dispatched[(dispatch_at + 63 - n) % 64];
        if (cmd.id == id && cmd.generation == state.generation) return cmd.text;
    }
    return "";
}
void Reject(const char* reason, const char* text = "") noexcept {
    ++state.rejected;
    Copy(state.last_result, sizeof(state.last_result), reason);
    AddReceipt(0, 3, text, reason);
}
void Clear(const char* reason) noexcept {
    if (count) { Reject(reason); }
    head = count = 0;
    state.queue_depth = 0;
}
void ClearCamps() noexcept {
    state.enemy_camp_count = state.enemy_route_count = state.enemy_spawn_hash = 0;
    state.enemy_spawn_valid = false;
    std::memset(state.enemy_camps, 0, sizeof(state.enemy_camps));
    std::memset(state.enemy_routes, 0, sizeof(state.enemy_routes));
    std::memset(state.enemy_spawn_pos_mm, 0, sizeof(state.enemy_spawn_pos_mm));
    state.enemy_routes_camp[0] = 0;
    state.enemy_camps_loaded = false;
    camp_id = route_id = spawn_id = metadata_id = 0;
    camps_open = routes_open = false;
}
void ClearEnemies() noexcept {
    ClearCamps();
    std::memset(state.enemy_nearby, 0, sizeof(state.enemy_nearby));
    state.enemy_nearby_count = state.enemy_nearby_radius = 0;
    nearby_id = 0; nearby_open = false;
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
    const bool nearby = std::strcmp(author->text, "enemy_list") == 0 || std::strncmp(author->text, "enemy_list ", 11) == 0;
    if (nearby) {
        if (kind.Is("nearby")) {
            std::uint32_t mission = 0, radius = 0;
            if (!reader.Next().Unsigned(99999, mission) || mission != state.mission || !reader.Next().Unsigned(300, radius) ||
                radius < 10 || !reader.End() || !Newer(id, nearby_id)) return false;
            std::memset(state.enemy_nearby, 0, sizeof(state.enemy_nearby));
            state.enemy_nearby_count = 0; state.enemy_nearby_radius = radius;
            nearby_id = id; nearby_open = true;
            return true;
        }
        if (kind.Is("soldier")) {
            std::uint32_t index = 0, hash = 0, code = 0, flags = 0;
            std::int32_t distance = 0;
            if (!nearby_open || id != nearby_id || !reader.Next().Unsigned(kNearbyCount, index) || index != state.enemy_nearby_count + 1 ||
                !reader.Next().Unsigned(0xffffffffu, hash) || !reader.Next().Signed(0, 1000000000, distance) ||
                !reader.Next().Unsigned(8, code) || !(code == 0 || code == 2 || code == 3 || code == 4 || code == 5 || code == 8) ||
                !reader.Next().Unsigned(7, flags)) return false;
            const auto name = reader.Next();
            if (!name.Name() || !reader.End() || state.enemy_nearby_count >= kNearbyCount) return false;
            auto& entry = state.enemy_nearby[state.enemy_nearby_count++];
            name.CopyTo(entry.name); entry.hash = hash; entry.state = code; entry.flags = flags; entry.distance_mm = distance;
            return true;
        }
        if (kind.Is("done")) {
            if (!reader.End() || !nearby_open || id != nearby_id) return false;
            nearby_open = false;
            return true;
        }
        return false;
    }
    if (kind.Is("clear")) {
        std::uint32_t mission = 0;
        if (!list || !reader.Next().Unsigned(99999, mission) || mission != state.mission || !reader.End() || !Newer(id, metadata_id)) return false;
        ClearCamps(); camp_id = metadata_id = id; camps_open = true;
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
        if (list && camps_open && id == camp_id) { camps_open = false; state.enemy_camps_loaded = true; return true; }
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
bool ParseLuaStateText(const char* text, std::size_t size, LuaFlags& out) noexcept {
    out = {};
    if (!text || !size || size >= 200) return false;
    for (std::size_t i = 0; i < size; ++i) if (text[i] < 0x20 || text[i] > 0x7e) return false;
    // Single spaces only: the reader skips runs, so count separators exactly.
    std::size_t fields = 1;
    for (std::size_t i = 0; i < size; ++i) {
        if (text[i] != ' ') continue;
        if (i == 0 || i + 1 == size || text[i + 1] == ' ') return false;
        ++fields;
    }
    if (fields != 2 && fields != 12) return false;
    Tokens reader{ text, size };
    std::uint32_t ammo = 0, flag = 0;
    if (!reader.Next().Unsigned(1, ammo) || !reader.Next().Unsigned(255, out.bookmark_mask)) return false;
    out.infinite_ammo = ammo == 1;
    if (fields == 2) return reader.End();
    if (!reader.Next().Unsigned(0xffffffffu, out.pending_id)) return false;
    const auto kind = reader.Next();
    if (!(kind.Is("none") || kind.Is("warp") || kind.Is("equip") || kind.Is("spawn") || kind.Is("enemy"))) return false;
    kind.CopyTo(out.pending_kind);
    if (!reader.Next().Unsigned(100000, out.pending_frames)) return false;
    if (!reader.Next().Unsigned(1, flag)) return false;
    out.god_mode = flag == 1;
    if (!reader.Next().Unsigned(1, flag)) return false;
    out.fulton_infinite = flag == 1;
    if (!reader.Next().Unsigned(1, flag)) return false;
    out.slowmo_active = flag == 1;
    if (!reader.Next().Unsigned(86399, out.game_clock_s) || !reader.Next().Signed(-1, 100000000, out.player_life) ||
        !reader.Next().Signed(-1, 100000000, out.player_life_max)) return false;
    const auto reason = reader.Next();
    if (!reason.size || reason.size >= sizeof(out.guard_reason)) return false;
    for (std::size_t i = 0; i < reason.size; ++i) if (!((reason.text[i] >= 'a' && reason.text[i] <= 'z') || reason.text[i] == '_')) return false;
    reason.CopyTo(out.guard_reason);
    return reader.End();
}
void ApplyLuaFlags(const LuaFlags& value, bool full) noexcept {
    state.infinite_ammo = value.infinite_ammo;
    state.bookmark_mask = value.bookmark_mask & 255u;
    if (!full) return;
    state.god_mode = value.god_mode; state.fulton_infinite = value.fulton_infinite; state.slowmo_active = value.slowmo_active;
    state.pending_id = value.pending_id; state.pending_frames = value.pending_frames; state.game_clock_s = value.game_clock_s;
    state.player_life = value.player_life; state.player_life_max = value.player_life_max;
    Copy(state.pending_kind, sizeof(state.pending_kind), value.pending_kind);
    Copy(state.guard_reason, sizeof(state.guard_reason), value.guard_reason);
}
void ResetLuaFlags() noexcept {
    LuaFlags none{};
    Copy(none.pending_kind, sizeof(none.pending_kind), "none");
    ApplyLuaFlags(none, true);
}
// Fields owned by the bridge, Lua receipts or the overlay; telemetry never replaces them.
void KeepOwned(Snapshot& to, const Snapshot& from) noexcept {
    to.enabled = from.enabled; to.offline = from.offline; to.saves_redirected = from.saves_redirected; to.host = from.host;
    to.generation = from.generation; to.mission = from.mission; to.gameplay_ready = from.gameplay_ready; to.terminating = from.terminating;
    to.infinite_ammo = from.infinite_ammo; to.bookmark_mask = from.bookmark_mask; to.god_mode = from.god_mode;
    to.fulton_infinite = from.fulton_infinite; to.slowmo_active = from.slowmo_active; to.pending_id = from.pending_id;
    to.pending_frames = from.pending_frames; to.game_clock_s = from.game_clock_s; to.player_life = from.player_life;
    to.player_life_max = from.player_life_max;
    std::memcpy(to.pending_kind, from.pending_kind, sizeof(to.pending_kind));
    std::memcpy(to.guard_reason, from.guard_reason, sizeof(to.guard_reason));
    to.last_id = from.last_id; to.completed = from.completed; to.rejected = from.rejected; to.interim = from.interim;
    to.enemy_camp_count = from.enemy_camp_count; to.enemy_route_count = from.enemy_route_count;
    to.enemy_spawn_hash = from.enemy_spawn_hash; to.enemy_spawn_valid = from.enemy_spawn_valid; to.enemy_camps_loaded = from.enemy_camps_loaded;
    std::memcpy(to.enemy_camps, from.enemy_camps, sizeof(to.enemy_camps));
    std::memcpy(to.enemy_routes, from.enemy_routes, sizeof(to.enemy_routes));
    std::memcpy(to.enemy_routes_camp, from.enemy_routes_camp, sizeof(to.enemy_routes_camp));
    std::memcpy(to.enemy_spawn_pos_mm, from.enemy_spawn_pos_mm, sizeof(to.enemy_spawn_pos_mm));
    std::memcpy(to.enemy_nearby, from.enemy_nearby, sizeof(to.enemy_nearby));
    to.enemy_nearby_count = from.enemy_nearby_count; to.enemy_nearby_radius = from.enemy_nearby_radius;
    std::memcpy(to.last_command, from.last_command, sizeof(to.last_command));
    std::memcpy(to.last_result, from.last_result, sizeof(to.last_result));
    std::memcpy(to.receipts, from.receipts, sizeof(to.receipts));
    to.overlay_open = from.overlay_open; to.overlay_frames = from.overlay_frames;
    to.overlay_request_serial = from.overlay_request_serial; to.overlay_result_serial = from.overlay_result_serial;
    std::memcpy(to.overlay_category, from.overlay_category, sizeof(to.overlay_category));
    std::memcpy(to.overlay_search, from.overlay_search, sizeof(to.overlay_search));
    std::memcpy(to.overlay_result, from.overlay_result, sizeof(to.overlay_result));
    std::memcpy(to.last_ui_action, from.last_ui_action, sizeof(to.last_ui_action));
    std::memcpy(to.role, from.role, sizeof(to.role));
}
Snapshot previous_scratch{}; // Publish only, under the exclusive lock (keeps the game thread's stack small)
}
bool Enabled() noexcept { return enabled.load(std::memory_order_acquire); }
Snapshot GetSnapshot() noexcept {
    AcquireSRWLockShared(&lock);
    const Snapshot copy = state;
    ReleaseSRWLockShared(&lock);
    return copy;
}
bool ValidText(const char* text) noexcept { return Valid(text); }
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
    ResetLuaFlags();
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
    ResetLuaFlags();
    state.pose_valid = state.body_valid = state.remote_valid = false;
    ClearEnemies(); std::memset(dispatched, 0, sizeof(dispatched)); dispatch_at = 0;
    ReleaseSRWLockExclusive(&lock);
}
void EndMission() noexcept {
    AcquireSRWLockExclusive(&lock);
    Clear("Mission ended; pending actions discarded.");
    state.gameplay_ready = false;
    state.terminating = true;
    state.pose_valid = state.body_valid = state.remote_valid = false;
    ClearEnemies(); std::memset(dispatched, 0, sizeof(dispatched)); dispatch_at = 0;
    ReleaseSRWLockExclusive(&lock);
}
void Publish(const Snapshot& value) noexcept {
    AcquireSRWLockExclusive(&lock);
    // Lifecycle/action fields belong to the bridge, not a telemetry publisher.
    previous_scratch = state;
    state = value;
    KeepOwned(state, previous_scratch);
    state.queue_depth = static_cast<std::uint32_t>(count);
    state.published_tick64 = GetTickCount64();
    ReleaseSRWLockExclusive(&lock);
}
namespace {
std::uint32_t QueueCommandImpl(const char* text, std::uint32_t mission, std::uint32_t generation) noexcept {
    AcquireSRWLockExclusive(&lock);
    const char* refusal = generation && (generation != state.generation || mission != state.mission) ? "Debug mission generation changed." :
        !state.enabled ? "Debug is disabled." : !state.gameplay_ready || state.terminating ?
        "Wait for supported free-roam or Mother Base gameplay." : !Valid(text) ? "Invalid command text." :
        count == kQueueCapacity ? "Action queue full; wait for the game update." : nullptr;
    if (refusal) {
        Reject(refusal, Valid(text) ? text : "");
        ReleaseSRWLockExclusive(&lock); return 0;
    }
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
    return cmd.id;
}
}
bool QueueCommand(const char* text) noexcept { return QueueCommandImpl(text, 0, 0) != 0; }
std::uint32_t QueueCommandId(const char* text) noexcept { return QueueCommandImpl(text, 0, 0); }
std::uint32_t QueueCommandForGeneration(const char* text, std::uint32_t mission, std::uint32_t generation) noexcept {
    return mission && generation ? QueueCommandImpl(text, mission, generation) : 0;
}
void ClearHistory() noexcept {
    AcquireSRWLockExclusive(&lock);
    std::memset(state.receipts, 0, sizeof(state.receipts));
    ReleaseSRWLockExclusive(&lock);
}
void SetOverlayStatus(const OverlayStatus& value) noexcept {
    AcquireSRWLockExclusive(&lock);
    state.overlay_open = value.open;
    state.overlay_frames = value.frames;
    Copy(state.overlay_category, sizeof(state.overlay_category), value.category);
    Copy(state.overlay_search, sizeof(state.overlay_search), value.search);
    ReleaseSRWLockExclusive(&lock);
}
namespace {
std::uint32_t menu_serial{}, menu_outstanding{};
bool menu_ready{};
char menu_text[kCommandCapacity]{};
void MenuResultLocked(std::uint32_t serial, const char* result) noexcept {
    state.overlay_result_serial = serial;
    Copy(state.overlay_result, sizeof(state.overlay_result), result);
    if (menu_outstanding == serial) { menu_outstanding = 0; menu_ready = false; }
}
}
std::uint32_t RequestMenu(const char* text, std::uint32_t mission, std::uint32_t generation) noexcept {
    debug_actions::OverlayRequest parsed{};
    const bool valid = debug_actions::ParseOverlayRequest(text, parsed);
    AcquireSRWLockExclusive(&lock);
    std::uint32_t serial = ++menu_serial;
    if (!serial) serial = ++menu_serial;
    state.overlay_request_serial = serial;
    const char* refusal = !valid ? "menu_args" : !state.enabled ? "menu_unavailable;reason=disabled" :
        generation != state.generation || mission != state.mission ? "menu_stale" : menu_outstanding ? "menu_busy" : nullptr;
    if (refusal) MenuResultLocked(serial, refusal);
    else { menu_outstanding = serial; menu_ready = true; Copy(menu_text, sizeof(menu_text), text); }
    ReleaseSRWLockExclusive(&lock);
    return serial;
}
bool TakeMenuRequest(std::uint32_t& serial, char (&text)[kCommandCapacity]) noexcept {
    AcquireSRWLockExclusive(&lock);
    const bool ready = menu_ready;
    if (ready) { serial = menu_outstanding; std::memcpy(text, menu_text, sizeof(text)); menu_ready = false; }
    ReleaseSRWLockExclusive(&lock);
    return ready;
}
void ReportMenuResult(std::uint32_t serial, const char* result) noexcept {
    AcquireSRWLockExclusive(&lock);
    MenuResultLocked(serial, result);
    ReleaseSRWLockExclusive(&lock);
}
void SetLastUiAction(const char* action) noexcept {
    AcquireSRWLockExclusive(&lock);
    Copy(state.last_ui_action, sizeof(state.last_ui_action), action);
    ReleaseSRWLockExclusive(&lock);
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
    if (!current) Reject("Stale mission action discarded.", value.text);
    ReleaseSRWLockExclusive(&lock);
    return current;
}
void Result(std::uint32_t id, unsigned code, const char* message) noexcept {
    if (code > 2) return;
    AcquireSRWLockExclusive(&lock);
    if (code == 2) ++state.interim;
    else {
        ++state.completed;
        if (code == 0) ++state.rejected;
    }
    if (id == state.last_id) Copy(state.last_result, sizeof(state.last_result), message);
    AddReceipt(id, static_cast<std::uint8_t>(code == 1 ? 0 : code == 0 ? 1 : 2), DispatchedText(id), message);
    ReleaseSRWLockExclusive(&lock);
}
void LuaState(bool ammo, std::uint32_t bookmarks) noexcept {
    LuaFlags value{};
    value.infinite_ammo = ammo; value.bookmark_mask = bookmarks;
    AcquireSRWLockExclusive(&lock);
    ApplyLuaFlags(value, false);
    ReleaseSRWLockExclusive(&lock);
}
void LuaState(const LuaFlags& value) noexcept {
    AcquireSRWLockExclusive(&lock);
    ApplyLuaFlags(value, true);
    ReleaseSRWLockExclusive(&lock);
}
bool ParseLuaState(const char* text, std::size_t size, LuaFlags& out) noexcept { return ParseLuaStateText(text, size, out); }
bool LuaStateReceipt(const char* text, std::size_t size) noexcept {
    LuaFlags value{};
    if (!ParseLuaStateText(text, size, value)) return false;
    std::size_t fields = 1;
    for (std::size_t i = 0; i < size; ++i) if (text[i] == ' ') ++fields;
    AcquireSRWLockExclusive(&lock);
    ApplyLuaFlags(value, fields == 12);
    ReleaseSRWLockExclusive(&lock);
    return true;
}
bool EnemyReceipt(const char* text, std::size_t size) noexcept {
    AcquireSRWLockExclusive(&lock);
    const bool accepted = EnemyReceiptLocked(text, size);
    ReleaseSRWLockExclusive(&lock);
    return accepted;
}
bool DispatchedCommand(std::uint32_t id, const char* prefix) noexcept {
    if (!id || !prefix || !*prefix) return false;
    AcquireSRWLockShared(&lock);
    const auto n = std::strlen(prefix);
    const char* text = state.enabled && state.gameplay_ready && !state.terminating && state.host ? DispatchedText(id) : "";
    const bool found = std::strncmp(text, prefix, n) == 0 && (text[n] == 0 || text[n] == ' ');
    ReleaseSRWLockShared(&lock);
    return found;
}
} // namespace mgscoop::debug_menu
