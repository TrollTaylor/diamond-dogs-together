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
// Logs the inputs the game weighs before a client request, with safe reads
// only: IsConnectReady inputs (FobTarget +0x10 state, +0x14 threshold, byte at
// base+0x2BD7BC0, int at base+0x2BD7BB4) and SessionConnectInfo fields
// (+0x00 state, +0x70, +0xB8, +0xC8). The host identity value itself is never
// logged. Call it on the owned mission thread immediately before the request.
void LogClientConnectDiagnostics(void* base, std::size_t span, std::uintptr_t target,
                                 void (*log)(const char*) noexcept) noexcept;
// Client-join reset support (synthesis gap G11). The borrowed FobTarget and its
// SessionConnectInfo fields that the native 0x7EE380 request writes (+0x10,
// +0x48 and connect-info +0xB8) are recorded at submission. Once an owned
// client session has been closed through the verified path and its singleton
// slots are empty, RestoreClientRequestFields reverts exactly those fields to
// their recorded pre-call values in place; the game keeps object ownership.
bool HasRecordedClientRequest() noexcept;
bool RecordedClientRequestSlotsEmpty(void* base, std::size_t span) noexcept;
bool RestoreClientRequestFields(void* base, std::size_t span, void (*log)(const char*) noexcept) noexcept;
void ForgetClientRequestRecord() noexcept;
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
