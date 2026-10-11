#pragma once
// Original bounded D-Dog position read. No engine calls, hooks, writes or
// capacity experiment dependency. Only the coordinator's exact paired scope
// and retail image/code pins admit a read. See scratch evidence until merged.
#include <array>
#include <cstddef>
#include <cstdint>
namespace mgscoop::buddy_pose {
using ReadFn=bool(*)(void*,std::uintptr_t,void*,std::size_t) noexcept;
struct Reader {void* context{};ReadFn read{};};
struct Pin {std::uintptr_t rva;std::size_t bytes;const char* sha256;};
inline constexpr Pin kPins[]={
 {0xbfa477,137,"180d7f4974a058dfc49636db1a9eff760b8aa3ad00a8a1e68b4f292b3a983ea1"},
 {0xbf99e0,34,"76eadda6f658a477094e637c28f73db48a3b809cac9bd09e310af883f76c0a13"},
 {0xbf9a10,8,"0eb5b43389c356e1e8636cc907f5864472bb505c38fe604a3c8681722dfadf2f"},
 {0xbf8a20,102,"01f5e3dcecc8b7351c2e1958b2f28a5aa3839f59dceb86bcf6a43a0b97a0f5e7"},
 {0xa58e70,306,"b65412d93ea4461132299e11cd1e4a1f781696988109452976e181c47164a992"},
 {0xc55a30,37,"1ad54bb828d4422d49beb73d3148a398631aabba033004339479675a8aecfe66"},
 {0xc56770,682,"54762dc9a9b496d5c619bbe67f7bb5b74874c67f18808c6a19751c00970ba9a4"},
 {0xc554c0,88,"68abb51861ead21e6f36f8172d3b81309c64c6c73cdbffcbcd613cf4b5b9fdc3"},
 {0xc55100,184,"6fa67b6d84271d64bba5150f9255f0f38f12f96f7ab746ab764454f6f933c50b"},
 {0xc551c0,342,"73b71f37b84990fb82e7b62136c630901ea0aa0ac6415e2f8112d0010154ecdd"},
 {0xc58856,46,"33cd77007341f3f63a7f5e6bdaa6f01eca4bcc9b8d192f7fdbf588f2058c3370"},
 {0xa46030,87,"a34b19b8aed39371ac1e5565bde9275af0bdab9d21fb9beb89dfab25489eb025"},
 {0xc79660,88,"7d7938bd711f2c511320f37364aac7a5c89338cd2771dc9580008f46287483aa"},
 {0xc79810,117,"a7f18afd62aefac3d712ef4808bb45b8f0b81c373450908d809f9297460a3a26"},
 {0xc796c0,314,"9fea338e78664ad857d58aa3c1f86f1fefe283e95627c59511b9b3dffb8fc890"},
 {0xc79890,594,"9222896961e08151529cf55ea4cb2d78703c67cbeb0d7a33e46bb24b0ed1456e"},
 {0xc7ba00,96,"ea008b3cff52d40940acaf7ee73abd466b8864b13eeaab849f64ac6df2f16ecc"},
};
struct Scope {
 std::uintptr_t P{};std::uint32_t mission{},generation{},thread{};std::uint16_t epoch{};
 std::uint64_t refreshed_ms{};bool paired{},field_ready{},demo_playing{},terminating{},exact_executable{},exact_code_pins{},cleanup_only{};
};
struct Pose {std::uint16_t id{0xffff};std::uintptr_t outer{},primary{},kind{},positions{},controller{};std::array<float,4> position{};};
// nullptr succeeds. Every error leaves output unchanged. Actual thread is
// supplied by native receipt owner, never by Lua arguments. No retained pointer.
// cleanup_only bypasses paired only; root must match its cached last successful
// normal Sample Scope (P/mission/generation/epoch), retired at start/terminate/P change.
const char* ReadPose(Reader,std::uintptr_t image,const Scope&,std::uint32_t actual_thread,std::uint64_t now,std::uint16_t id,Pose&) noexcept;
} // namespace
