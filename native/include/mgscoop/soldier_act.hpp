#pragma once
// Enemy sync M7 FM1 (docs/world-sync-design.md 7.15, the far mirror; plan "owner-brains" act layer): probe P1.
// The engine's own FOB act sync (native/soldier_act_contract.hpp, tools/generate_soldier_act_contract.py) serialises each
// guard's 0x1e0-byte request record R (written by his brain, read by his body) on the owner and applies it on the other
// peer. This module hooks the request manager update 0x13735c0 (this = M, every frame, the soldier thread) read-only for
// the research census (lab key act_census): every 0.5 s one soldier_act_census line per realized guard within 250 m of
// the local player (the network-position table of U1, S's mode/peer/owner bytes, R's flags, the request and order packed
// with the engine's own 0x13704a0 / 0x1370900 into scratch buffers, gait / posture / kinds, the aim point, the weapon
// bytes), a soldier_engage_bins line per second (Defend hits on the local player per shooter, with his distance), and a
// soldier_act_bands line every 10 s (realized copies per 25 m band: the realize radius).
// The research command act_probe (U2: does a puppet's body run a posted request?) puppets one guard (SetPuppet's bit 27
// of his flag record) and posts another guard's request to him every update for a while on this one copy.
#include <cstddef>
#include <cstdint>

namespace mgscoop::soldier_act {

inline constexpr unsigned kMaxSoldiers = 512;
inline constexpr float kCensusRangeM = 250.0f;
inline constexpr std::uint64_t kCensusPeriodMs = 500, kBinsPeriodMs = 1000, kBandsPeriodMs = 10000, kPayloadPeriodMs = 2000;
inline constexpr unsigned kBands = 11;  // 25 m bands 0..250 m, then beyond

// The fixed fields of a packed 0x70 request (0x13704a0's writes, docs above).
struct Fixed {
    std::uint8_t path{}, dest_mode{}, kind_a{}, kind_b{}, gait{}, b66{}, posture{}, face_mode{}, f69{}, f6a{}, f6b{}, f6c{},
        f6d{}, weapon{};
    float dest[3]{};  // the packed +0x40 position (22/20/22 bits, 1 cm), dest modes 0..2
};
Fixed DecodeFixed(const unsigned char (&wire)[0x70]) noexcept;
// The engine's packed position (0x136f810's decode): x = (v >> 42) - 0x200000, y = ((v >> 22) & 0xfffff) - 0x80000,
// z = (v & 0x3fffff) - 0x200000, each times 0.01 m; v is the little-endian u64 at the field.
void DecodePacked(std::uint64_t v, float (&out)[3]) noexcept;
std::uint64_t EncodePacked(const float (&in)[3]) noexcept;
// Action kinds that 0x136f810 gives the catch-up (the moving kinds).
bool MovingKind(std::uint8_t kind) noexcept;
// The 25 m band of a distance (kBands - 1 beyond 250 m).
unsigned Band(float metres) noexcept;
// Lower-case hex of `size` bytes into out (2 * size + 1); false when it does not fit.
bool Hex(const unsigned char* bytes, std::size_t size, char* out, std::size_t capacity) noexcept;

// FM1, the far mirror (lab key soldier_act; docs/world-sync-design.md 7.15). The owner's copy packs each mirrored guard's
// request and order (0x13704a0 / 0x1370900, the hook thread) and sends them as SOLDIER_ACT parts; the other copy's puppet
// takes them (0x136f810 with its network-position entry pre-filled with its own position, then R+0x19e |= 0x80; the
// order 0x13701f0 every update, R+0x1d2 |= 1) and runs them with its own legs, aim and fire motions.
inline constexpr std::uint64_t kActMinMs = 100, kActResendMs = 1000, kActFreshMs = 1500;
// The puppet side re-applies an unchanged request only every kActReapplyMs (the 1 s resend keeps the stream fresh; the
// FOB peer applies only new requests, and a re-posted request restarts its motion).
inline constexpr std::uint64_t kActReapplyMs = 5000;
inline constexpr unsigned kActPartsPer100Ms = 24;
// Players 0 and 1 swap between the copies (GameObjectId type 0); any other id is unchanged.
std::uint16_t RemapPlayer(std::uint16_t id) noexcept;
// Whether two packed requests differ beyond jitter: the destination (+0x40) moved >= 5 cm, a facing position (face mode
// 2, +0x50) >= 5 cm, a facing yaw (face mode 1, u16 at +0x50) by >= 1 degree (182 of 65536), or any other byte.
bool RequestChanged(const unsigned char (&a)[0x70], const unsigned char (&b)[0x70]) noexcept;
// Orders: a target position (mode 1, packed at +0) moved >= 5 cm, or any other byte.
bool OrderChanged(const unsigned char (&a)[0x18], const unsigned char (&b)[0x18]) noexcept;
// The rate cap: a change goes out kActMinMs after the last send at the earliest, an unchanged record every kActResendMs.
bool DueSend(bool changed, std::uint64_t last_ms, std::uint64_t now_ms) noexcept;
// The network-position entry 0x136f810 reads (0x18 bytes): xyz, yaw, flags +0x14 bit 0 (valid).
void PrefillEntry(unsigned char (&entry)[0x18], const float (&pos)[3], float yaw) noexcept;

#ifdef _WIN32
using LogFn = void (*)(const char* line) noexcept;
// `base`/`span`: the image. Verifies every site and slot of soldier_act_contract.hpp and hooks the manager update (MinHook).
// Logs soldier_act=installed | refused; reason=.... Idempotent. The hook does nothing but remember M until the census or
// a probe is on.
bool Install(const void* base, std::size_t span, LogFn log) noexcept;
bool Installed() noexcept;
void SetCensus(bool on) noexcept;
bool CensusOn() noexcept;
// The local player position for the census distances (the runtime thread publishes it; the hook reads it).
void SetLocalPlayer(const float (&pos)[3], std::uint64_t now_ms) noexcept;
// U2 probe: puppet guard `dst` (SetPuppet bit) and post guard `src`'s request to him every update for `ms` (<= 30000);
// dst/src 0xffff = auto (dst: the realized guard nearest the local player within 40 m; src: a realized guard 8..60 m
// from him, a moving one first). mode 0 native (pack 0x13704a0 + unpack 0x136f810 with the entry pre-filled; refused
// unless the network-position table covers dst), 1 raw (the post 0x1368980 of src's own request bytes), 2 auto (native
// when the table covers dst, else raw). False when one is already running or the hook is not installed.
bool StartProbe(std::uint16_t dst, std::uint16_t src, std::uint32_t ms, unsigned mode) noexcept;
// "act;installed=;census=;m=;s=;calls=;lines=;faults=;probe=..." for the act_status reply.
bool StatusLine(char* out, std::size_t capacity) noexcept;
// FM1 (the runtime thread). Configure: soldier_act on this copy (both roles; the hook is installed with it).
void ConfigureMirror(bool on, bool host) noexcept;
bool MirrorOn() noexcept;
// Owner: send guard `idx` (roster `slot`) for kActFreshMs; the hook packs it every update.
void SetTx(unsigned idx, std::uint16_t slot, std::uint64_t now_ms) noexcept;
// Owner: sends the staged parts (world_link::SendAct), at most kActPartsPer100Ms per 100 ms. Parts sent.
unsigned SendStaged(std::uint64_t now_ms) noexcept;
// Puppet side: mirror guard `idx` (roster `slot`) or release him. On, pulls the newest SOLDIER_ACT of the slot into the
// hook's mailbox; true when the request is fresh (kActFreshMs). Off clears his mailbox and the fire-drop bit.
bool SetMirror(unsigned idx, std::uint16_t slot, bool on, std::uint64_t now_ms) noexcept;
void ReleaseAll() noexcept;
// Research command "mirror off|on": while paused, ACT_V1 m is refused "paused" (Lua releases every puppet).
void SetPaused(bool paused) noexcept;
bool Paused() noexcept;
struct Counters {
    std::uint32_t tx_parts, staged, rx, applied, orders, refuse_table, refuse_fault, refuse_stale, remap_player, payload_ids,
        mirrored, fire_dropped, noise_dropped, local_shots, frame_p95_us, frames;
};
Counters ReadCounters() noexcept;
#endif

} // namespace mgscoop::soldier_act
