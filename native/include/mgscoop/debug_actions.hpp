#pragma once
#include <cstddef>
#include <cstdint>
#include "mgscoop/debug_menu.hpp"

// Pure F9 menu data: action registry, search, availability and command text.
// No ImGui, Windows or game calls; shared by the overlay, the worker and tests.
namespace mgscoop::debug_actions {
enum class Category : std::uint8_t { player, enemies, partner, world, tools };
inline constexpr std::size_t kCategoryCount = 5;
inline constexpr std::uint32_t kHostOnly = 1, kConfirm = 2, kNeedsPose = 4, kNeedsSpawn = 8, kNeedsCamp = 16,
    kNeedsRoutes = 32, kParam = 64, kToggle = 128, kNeedsPartner = 256, kNeedsBookmark = 512, kNeedsClip = 1024,
    kNeedsSelection = 2048, kUnverified = 4096, kSyncs = 8192;
struct ActionDef {
    const char* id;
    Category category;
    const char* label;
    const char* command;  // fixed text; a template for kParam rows; nullptr for UI-only rows
    const char* keywords;
    const char* tooltip;
    std::uint32_t flags;
};
const ActionDef* Actions(std::size_t& count) noexcept;
const ActionDef* FindAction(const char* id) noexcept;
// Every word must match (case-insensitive) label, command, keywords or category name.
std::size_t SearchActions(const char* query, const ActionDef** out, std::size_t capacity) noexcept;
// nullptr when available, else one of: mission gameplay host_only position no_spawn
// no_camp no_routes partner_elsewhere partner_pose bookmark_empty clip_unavailable selection.
const char* DisabledReason(const ActionDef& action, const debug_menu::Snapshot& s, bool has_selection) noexcept;
bool PartnerPoseReady(const debug_menu::Snapshot& s) noexcept;
const char* CategoryName(Category value) noexcept;
const char* CategoryLabel(Category value) noexcept;
// Aliases: coop->partner, diag->tools, mission->tools.
bool ParseCategory(const char* text, Category& out) noexcept;
const char* MissionName(std::uint32_t mission) noexcept;
const char* LifeCodeName(std::uint32_t code) noexcept;

enum class OverlayOp : std::uint8_t { none, open, close, toggle, category, search, press, confirm, cancel, footer, select };
struct OverlayRequest { std::uint32_t serial{}; OverlayOp op{}; char arg[97]{}; };
// Grammar (worker only): menu open [cat] | close | toggle | category <cat> | search [text<=48]
//   | press <action_id> | confirm | cancel | footer 0|1 | select <hash>
bool IsMenuText(const char* text) noexcept;
bool ParseOverlayRequest(const char* text, OverlayRequest& out) noexcept;

// Formatters: caller buffer, false when a value is out of range or the buffer is short.
bool FormatTeleport(char* out, std::size_t capacity, float x, float y, float z, float yaw) noexcept;
bool FormatStep(char* out, std::size_t capacity, float dx, float dy, float dz) noexcept;
bool FormatClock(char* out, std::size_t capacity, std::uint32_t seconds) noexcept; // HH:MM:SS
bool FormatTimeSet(char* out, std::size_t capacity, int hours, int minutes) noexcept; // time <s>
bool FormatSlowmo(char* out, std::size_t capacity, float rate, float seconds) noexcept;
bool FormatPartnerModes(char* out, std::size_t capacity, const char* apply, const char* input, const char* anim,
                        const char* aset, const char* dm, const char* look, const char* reset) noexcept;
bool FormatEnemyOp(char* out, std::size_t capacity, const char* op, std::uint32_t hash) noexcept;
bool FormatClear(char* out, std::size_t capacity, int radius) noexcept;
} // namespace mgscoop::debug_actions
