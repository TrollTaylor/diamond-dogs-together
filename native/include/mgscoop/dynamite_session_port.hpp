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
// The serialized mission adapter calls this only after OnAllocate returned,
// before OnInitialize enters. Revalidates the existing host before transferring
// adapter ownership from the loading worker to the observed gameplay thread.
bool AdoptDynamiteSessionThread(void* base, std::uint32_t allocation_thread,
                               void (*log)(const char*) noexcept) noexcept;
bool HasDynamiteSessionOwnership() noexcept;
// At the serialized runtime teardown boundary, forget an owned session only
// after its original root's session/manager slots and main singleton are empty.
bool ReconcileDynamiteSessionRelease(void* base,std::size_t span,
                                    void (*log)(const char*) noexcept) noexcept;
// Read-only gate for the mission Lua wrapper. Requires this process's created
// host, its adopted mission thread, and unchanged Lua admission code bodies.
bool CanChangeDynamiteHostAdmission(void* base,std::size_t span,
                                  void (*log)(const char*) noexcept) noexcept;
void ForgetDynamiteHostAdmission() noexcept;
// Returns this adapter's owned host only on its mission thread; no game call.
std::uintptr_t OwnedDynamiteHostForTargeting() noexcept;
bool HasOwnedDynamiteClientSession() noexcept;
std::uintptr_t OwnedDynamiteSessionForRespawn() noexcept;
// Close scope is the native adapter's captured outgoing mission. Live vars may
// already describe the destination during the old scene's OnTerminate.
bool CanCloseDynamiteSession(void* base,std::size_t span,std::uint16_t outgoing_mission,
                            void (*log)(const char*) noexcept) noexcept;
// Called only after the one-shot preparing-member disconnect Lua call returned.
// Revalidates the existing owned main session before allowing CloseSession.
bool CanFinishDynamiteSessionClose(void* base,std::size_t span,std::uint16_t outgoing_mission,
                                  void (*log)(const char*) noexcept) noexcept;
// Return value reports restoration of the owned close-branch adaptation, not
// asynchronous session cleanup. False must retain native restoration work.
bool ObserveDynamiteSessionClose(void* base,std::size_t span,void (*log)(const char*) noexcept,bool returned) noexcept;
