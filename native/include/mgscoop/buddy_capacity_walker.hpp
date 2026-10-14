#pragma once
// Original pure private D-Walker census. No native call/hook/write/mount.
#include "mgscoop/buddy_capacity_census.hpp"
namespace mgscoop::buddy_capacity_walker {
using Reader=buddy_capacity_census::Reader;
using Authorization=buddy_capacity_census::Authorization;
using Pin=buddy_capacity_census::Pin;
inline constexpr Pin kPins[]={
 {0xbf99e0,34,"76eadda6f658a477094e637c28f73db48a3b809cac9bd09e310af883f76c0a13"},
 {0xbf9a10,8,"0eb5b43389c356e1e8636cc907f5864472bb505c38fe604a3c8681722dfadf2f"},
 {0xbf8a20,102,"01f5e3dcecc8b7351c2e1958b2f28a5aa3839f59dceb86bcf6a43a0b97a0f5e7"},
 {0xa81980,280,"225b1b0cef0e8d7d11be8dcaab2e7b40a21648f0e52a98ec3a5670a128b0d1eb"},
 {0xa82350,1203,"f2ac07e4a914f05917154f488f594ee9f5c40fb9d87b25c64c90772a6234b0aa"},
 {0xa83670,110,"987cc412fdf25787606825540449723293003ae825c05522b36412f2ce0fc1a4"},
 {0x1701a60,531,"423bd6b083dc0c6826d1b3d6173bd0096e14d683e5590378095313e26c7c8d7e"},
 {0x1704670,37,"26ba6faf759023e8cce04573d4a345ff26a7ceafb00349146a1fe5fb2ccb5dd4"},
 {0x1704900,150,"2c688934864303c3e2e9519d02cfca1b31b75e411eec93ec6182a1161feb75b0"},
 {0x1704d53,25,"5a63214577a282e825775659579eec63d8724e46e5d1121491cbacece8cbcf0f"},
 {0x1705014,82,"4bcd8414ba1e76c43c874571ee033c891e7d2b81ec914a1178aa0a8e4cb7ed02"},
 {0x1704250,18,"1a52e6723f65c73ab7e6fc5c3c43b775c04984bc5dd79547d4e61cd7d557d72a"},
 {0x17016b0,905,"4e890cb4cf9db14ec1eb35de77072ad6f2c62aae694b7b87210529185bf5b366"},
 {0x1706190,600,"8bcb246e91034dcc507593187f345ab7688908587746dee202b3692fc66dfbf5"},
 {0xa46030,87,"a34b19b8aed39371ac1e5565bde9275af0bdab9d21fb9beb89dfab25489eb025"},
};
struct Owner{std::uintptr_t P{};std::uint32_t mission{},generation{},thread{},buddy_cycle{};std::uint64_t epoch{},refreshed_ms{};bool field_ready{},walker_active{},demo_playing{},terminating{};};
struct Actor{std::uintptr_t record{};std::uint16_t game_id{0xffff},rider{0xffff};std::uint8_t ride_state{};bool realized{},occupied{};std::array<float,4> position{};};
struct Census{std::uintptr_t outer{},receiver{},records{},kind{},positions{},realized_indices{},controller{};std::uint64_t pack_hash{};const char* pack_token{};std::uint32_t declared_total{},declared_realized{},count{},realized_count{},realized_mask{};std::uint16_t class_index{};std::array<Actor,2> actors{};};
bool Compiled() noexcept;
// Root supplies actual native scope and per-run candidate admission.
// nullptr succeeds; failure leaves out unchanged. No cache or synthetic pending ID.
const char* ReadCurrentCensus(Reader,std::uintptr_t,Authorization,const Owner&,std::uint32_t,std::uint64_t,Census&) noexcept;
// Same verified walker pose read without the private-worker admission gate.
const char* ReadPlayPose(Reader,std::uintptr_t,Authorization,const Owner&,std::uint32_t,std::uint64_t,Census&) noexcept;
}
