#pragma once
// API proposal for a separately owned co-op cargo controller. No definitions,
// engine calls, registration or activation are supplied by this declaration.
#include <cstdint>
#include "horse_player_physical_read.hpp"
namespace mgscoop::horse_player_cargo_api {
enum class Phase : std::uint8_t { idle, awaiting_player_retirement, admitting_horse,
    binding_physical, loaded, detaching_horse, awaiting_physical_release, retired, refused };
struct Identity {
    std::uint32_t mission=0,generation=0,bodyBirth=0,horseBirth=0;
    std::uint16_t epoch=0,body=0xffff,horse=0xffff;
    std::uint64_t grant=0;
};
struct Request {Identity identity{};bool load=false;};
struct Receipt {
    Identity identity{};Phase phase=Phase::idle;
    bool horseAccepted=false,physicalBound=false,poseAccepted=false,cameraHeld=false,
         colliderHeld=false,horseReleased=false,physicalReleased=false;
    const char* reason="unqualified";
};
// Request only stages mod-owned intent. It never constitutes engine permission.
bool Submit(const Request&) noexcept;
// Actual engine admission uses original hash5167 kind1, requires currently free
// native cargo slot and readback actual unit164==body, and records its owner.
// Bind runs only in the current retained Player natural pose window, from the
// exact double-read physical census and same accepted horse generation. Its
// matrix comes from original unit90..C0; no offset or movement Warp is used.
// Resident Player pose/camera/collider qualification is required separately.
// Wake/revive remains deferred until original Horse kind2 clears actual cargo
// and the owned physical binding and native carried node have truly retired.
// Streaming/disconnect/body replacement revoke further calls immediately;
// cleanup can touch only a freshly matching owned slot/current body.
bool TickInCurrentPlayerWindow(const Identity&) noexcept;
Receipt Current() noexcept;
}
