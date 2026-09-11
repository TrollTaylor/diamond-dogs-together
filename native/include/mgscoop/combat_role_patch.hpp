#pragma once
#include <cstddef>
#include <cstdint>
namespace mgscoop::combat_role {
bool Install(void* base,std::size_t span,void (*log)(const char*) noexcept) noexcept;
void SetActive(bool enabled) noexcept;
void BindSession(std::uintptr_t session) noexcept;
// Lifecycle scope for callbacks that do not receive a session argument.
bool HasActiveOwnedSession() noexcept;
// For existing byte gates that inspect the now-detoured role getter.
bool InstalledFor(void* base) noexcept;
bool MatchesOwnedBody(void* base,std::size_t span) noexcept;
}
