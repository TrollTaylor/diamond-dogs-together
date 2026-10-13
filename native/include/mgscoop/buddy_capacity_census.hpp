#pragma once
// Original pure local experimental D-Horse capacity census. No hook, engine
// call, command, input forwarding or memory write. Never admitted in release.
#include <array>
#include <cstddef>
#include <cstdint>
namespace mgscoop::buddy_capacity_census {
struct Pin {std::uintptr_t rva;std::size_t bytes;const char* sha256;};
inline constexpr Pin kPins[]={
 {0xbf99e0,34,"76eadda6f658a477094e637c28f73db48a3b809cac9bd09e310af883f76c0a13"},
 {0xbf9a10,8,"0eb5b43389c356e1e8636cc907f5864472bb505c38fe604a3c8681722dfadf2f"},
 {0xbf8a20,102,"01f5e3dcecc8b7351c2e1958b2f28a5aa3839f59dceb86bcf6a43a0b97a0f5e7"},
 {0x9e4b90,188,"b57816d5dd3f337dc231a12d59ab22ac0dde1ab7d09a26360a209fe7a81bb150"},
 {0x9e5470,279,"28190465380f73f484b50fdaa8a1258c5367dbcdb6aee72d7862ba1829c81cfa"},
 {0x9e6060,107,"c62ef3483d7ecea21b938bf5d2e3293da6c79a31901d7b483179fc054687f3ec"},
 {0xe4c450,37,"efbd95908ff2eaedd5254095f15c93dc5e89e04507ae5ddb2ea6eef2552957f0"},
 {0xe7c680,156,"7c7332c1f782af774725b2c8d52bf9b68b66542c53a42a34399a813a7b951ca8"},
 {0xe7c720,37,"9586fe3239ca0adcf23183f6e7ade1dae35b53ccf030589454398a86b620a4fe"},
 {0xe7c750,123,"b41dabc7c3ceb54236ad13db68cc9abd16539cb6758143d012bae4a0a2fed262"},
 {0xe4b1d0,527,"c370b2d03484d1379e8680b91e638effdaf4681b2f668d3bd1f0748e9aec25b4"},
 {0xe4b9b0,92,"e5bfd385931ac93351ad631ddc6b771369935c939a0787fd50e380172fb0c39c"},
 {0xa46030,87,"a34b19b8aed39371ac1e5565bde9275af0bdab9d21fb9beb89dfab25489eb025"},
};
using ReadFn=bool(*)(void*,std::uintptr_t,void*,std::size_t) noexcept;
struct Reader{void* context{};ReadFn read{};};
struct Authorization{bool private_worker{},offline{},copied_saves{},candidate_catalog{},exact_executable{},exact_code_pins{};};
struct Owner{std::uintptr_t P{};std::uint32_t mission{},generation{},thread{},buddy_cycle{};std::uint64_t epoch{},refreshed_ms{};bool field_ready{},horse_active{},demo_playing{},terminating{};};
struct Actor{std::uintptr_t record{},ride_link{};std::uint16_t game_id{0xffff},rider{0xffff},pending_rider{0xffff};bool realized{},occupied{};std::array<float,4> position{};};
struct Census{std::uintptr_t outer{},receiver{},component{},records{},kind{},positions{},realized_indices{},controller{};std::uint64_t pack_hash{};const char* pack_token{};std::uint32_t declared_total{},declared_realized{},count{},realized_count{},realized_mask{};std::uint16_t class_index{};std::array<Actor,2> actors{};};
bool Compiled() noexcept;
// Root supplies exact current native owner/admission; none come from worker
// request text. nullptr succeeds; failures never change output. No cache.
const char* ReadCurrentCensus(Reader,std::uintptr_t image,Authorization,const Owner&,std::uint32_t actual_thread,std::uint64_t now,Census&) noexcept;
// Same verified horse pose read for a normal session. Still requires the code
// pins and the live horse record. It does not require the private worker,
// the capacity archive, or a copied-save gate. Failures leave output unchanged.
const char* ReadPlayPose(Reader,std::uintptr_t image,Authorization,const Owner&,std::uint32_t actual_thread,std::uint64_t now,Census&) noexcept;
}
