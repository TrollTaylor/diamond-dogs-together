#pragma once
#include <cstddef>
#include <cstdint>
#include "mgscoop/debug_actions.hpp"

// Original F9 menu drawing (Dear ImGui). Render thread only, between NewFrame and
// Render. Game actions are queued as text through debug_menu; nothing here calls
// the game, Windows input or graphics APIs.
namespace mgscoop::debug_menu_ui {
struct Options {
    bool worker{};      // fixed 900x600 layout, no automatic sends, scripted presses only
};
// Draws one frame. Returns false when the menu asked to close (title X or Esc).
bool Draw(const Options& options) noexcept;
// Menu-closed bookkeeping (resets transient focus requests).
void OnClosed() noexcept;

// Request surface shared by the worker text commands and the fixture tests.
void SelectCategory(debug_actions::Category value) noexcept;
debug_actions::Category CurrentCategory() noexcept;
// Sets (or clears) the search text; returns the number of matching actions.
std::size_t SetSearch(const char* text) noexcept;
const char* Search() noexcept;
void SetFooterExpanded(bool value) noexcept;
// Starts a scripted press of a registered action id (resolved over the next frames).
bool BeginPress(const char* action_id) noexcept;
// Copies a resolved press result once; false while unresolved or idle.
bool TakePressResult(char* out, std::size_t capacity) noexcept;
// Counts a Present in which the menu was not drawn (press -> not_drawn after 3).
void NoteUndrawnFrame() noexcept;
// Confirms or cancels the inline confirmation; results use the worker grammar.
void Confirm(char* out, std::size_t capacity) noexcept;
void Cancel(char* out, std::size_t capacity) noexcept;
// Selects a soldier from the current nearby list; false when it is not listed.
bool Select(std::uint32_t hash) noexcept;
bool HasPendingConfirm() noexcept;
void ResetForTest() noexcept;
// True when the page drew a row for this action in the latest frame.
bool RowDrawnForTest(const char* action_id) noexcept;
} // namespace mgscoop::debug_menu_ui
