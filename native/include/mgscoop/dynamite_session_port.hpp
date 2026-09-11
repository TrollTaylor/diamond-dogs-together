#pragma once
#include <cstddef>
#include <cstdint>

// Caller supplies the verified game-owned FobTarget and exact mission-thread
// receipt. Does not create a target, enable peer acceptance or bypass predicates.
bool CreateDynamiteHostSession(void* base, std::size_t span, std::uintptr_t target,
                               void (*log)(const char*) noexcept) noexcept;
bool CreateDynamiteClientSession(void* base, std::size_t span, std::uintptr_t target,
                                 std::uint64_t host, void (*log)(const char*) noexcept) noexcept;
// Read-only client prerequisites. Never installs an allocation hook or calls the game.
bool InspectDynamiteClientSession(void* base, std::size_t span, std::uintptr_t target,
                                  void (*log)(const char*) noexcept) noexcept;
void ObserveDynamiteSession(void* base,void (*log)(const char*) noexcept) noexcept;
// Lua literal describing this adapter's repeatedly observed owned session only.
// Native session/roster state is not proof of guest actor control or shared combat.
const char* ReadDynamiteSessionStatus(void* verified_base) noexcept;
// Read-only gate for the mission Lua wrapper. Requires this process's created
// host, the original creation thread, and unchanged Lua admission code bodies.
bool CanChangeDynamiteHostAdmission(void* base,std::size_t span,
                                  void (*log)(const char*) noexcept) noexcept;
void ForgetDynamiteHostAdmission() noexcept;
// Returns this adapter's owned host only on its creation thread; no game call.
std::uintptr_t OwnedDynamiteHostForTargeting() noexcept;
bool HasOwnedDynamiteClientSession() noexcept;
std::uintptr_t OwnedDynamiteSessionForRespawn() noexcept;
bool CanCloseDynamiteSession(void* base,std::size_t span,void (*log)(const char*) noexcept) noexcept;
// Called only after the one-shot preparing-member disconnect Lua call returned.
// Revalidates the existing owned main session before allowing CloseSession.
bool CanFinishDynamiteSessionClose(void* base,std::size_t span,void (*log)(const char*) noexcept) noexcept;
// Return value reports restoration of the owned close-branch adaptation, not
// asynchronous session cleanup. False must retain native restoration work.
bool ObserveDynamiteSessionClose(void* base,void (*log)(const char*) noexcept,bool returned) noexcept;
