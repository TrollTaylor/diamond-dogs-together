#pragma once
// Enemy sync M7 (docs/world-sync-design.md 7.13; lab key soldier_brain): each guard's owner is the only brain for the
// damage it deals. The host owns every guard but the ones the guest owns (M3, OWNER 0x49); the guest owns only those.
// In the player damage calculation (downed_damage.cpp Calculate), a soldier's hit on a player is:
//   - stock: the authority is off, the attacker is not a soldier (GameObjectId type 2), or a guard this copy owns hits
//     this copy's own player;
//   - neutralize: a guard the other copy owns hits anyone here (its brain is there: its hits on this player arrive as
//     SOLDIER_HIT, its hits on the replica are only seen);
//   - forward: a guard this copy owns hits the replica (body 1, the partner): the hit goes to the partner as SOLDIER_HIT
//     (downed_player soldier hit queue) and the replica takes nothing.
// Ownership: one bit per soldier index (GameObjectId & 0x1ff), set by Lua (MGS_COOP_SOLDIER_AUTH_V1); a soldier without a
// bit takes the role default (host: its own, guest: the host's).
#include <cstddef>
#include <cstdint>

namespace mgscoop::soldier_authority {

inline constexpr unsigned kSoldiers = 512;
inline constexpr std::uint16_t kSoldierType = 2;

enum class Action : std::uint8_t { stock, neutralize, forward };

// Pure.
inline bool IsSoldier(std::uint16_t attacker) noexcept { return attacker != 0xFFFFu && (attacker >> 9) == kSoldierType; }
Action Decide(bool enabled, std::uint32_t target, std::uint32_t local, std::uint16_t attacker, bool remote) noexcept;
const char* ActionName(Action action) noexcept;

// Process state (lock-free; written by the Lua owner thread, read by the damage callback).
void Configure(bool enabled, bool guest) noexcept;  // soldier_brain and the role (the guest's default: remote)
bool Enabled() noexcept;
void Clear() noexcept;                              // every soldier back to the role default (mission start/end)
bool Set(std::uint16_t index, bool remote) noexcept;  // false: index out of range
bool Remote(std::uint16_t attacker) noexcept;       // the attacker's soldier owned by the other copy
unsigned RemoteCount() noexcept;

// M7 step 1 evidence (soldier_perceive): soldiers' hits on each player as the defense callback sees them
// (downed_damage.cpp Defend, before any damage rule), whoever is the brain: a count per target (0 the local player,
// 1 body 1) and the attacking soldier indices. Lock-free; any thread. Non-soldier attackers and other targets are ignored.
void NoteDefense(std::uint32_t target, std::uint16_t attacker) noexcept;
struct DefenseSnapshot {
    std::uint32_t hits[2]{};
    std::uint64_t by[2][kSoldiers / 64]{};
};
DefenseSnapshot DefenseRead() noexcept;
void DefenseReset() noexcept;
// "<n>:<i>,<i>,..." for one target (soldier indices ascending, as many as fit); false when the head does not fit.
bool DefenseList(const DefenseSnapshot& s, unsigned target, char* out, std::size_t capacity) noexcept;

} // namespace mgscoop::soldier_authority
