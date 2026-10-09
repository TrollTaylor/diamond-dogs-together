#pragma once
// Partner name tag and friendly crosshair (docs/partner-nametag.md): the pure
// parts. Camera validation, projection, the centre-ray hit test, easing and
// fade, name sanitizing, the reliable channel-3 name wire, the text commands,
// the status line and the worker state.json member. No Windows, game or ImGui
// calls; shared by the game-thread source (nametag_game.cpp), the overlay and
// the tests.
#include <cstddef>
#include <cstdint>

namespace mgscoop::nametag {

// Row-major 4x4 with row vectors: clip = [x y z 1] * M (the game's convention:
// the view builder 0x438c20 stores the translation in row 3 and the viewport
// update 0x1b9724 computes VP = View x Projection).
struct Mat4 { float m[4][4]; };
struct Vec3 { float x, y, z; };

enum class Crosshair : std::uint8_t { aim, always, off };
const char* CrosshairName(Crosshair value) noexcept;  // aim|always|off
bool ParseCrosshair(const char* text, std::size_t size, Crosshair& out) noexcept;

// --- Camera -------------------------------------------------------------------
// Refusal reasons, in check order (state.json camera.refusal, the census line).
enum class Refusal : std::uint8_t {
    none, contract, thread, selector, handle, viewport, sub_viewport, vp_vtable, camera, cam_vtable, ortho, size, rect,
    nonfinite, proj_form, torn
};
const char* RefusalName(Refusal value) noexcept;
// What one tick read (absolute vtable addresses; the chain readers fill the
// earlier refusals themselves and never call ValidateCamera without a viewport).
struct CameraRaw {
    std::uintptr_t vp_vtable, cam, cam_vtable;
    float proj[4][4], view[4][4], viewproj[4][4];
    float rect[4];                // x0, y0, width, height fractions of the target
    std::uint32_t px_w, px_h;
    float pixel_aspect;
    std::uint32_t cam_flags;
    float focal, shift[2], near_m, far_m;
};
struct CameraExpect { std::uintptr_t vp_vtable, sub_vp_vtable, cam_vtable; };
struct Camera {
    Mat4 proj, view, viewproj;
    float rect[4];
    std::uint32_t px_w, px_h;
    float vfov_deg, near_m, far_m;
    float vp_err;  // largest |(View x Proj)[i][j] - VP[i][j]| relative to its row scale
};
inline constexpr std::uint32_t kMinPixels = 64, kMaxPixels = 16384;
inline constexpr float kTornTolerance = 1e-3f;
// |P00| and P11 (p*b, p*c) lie in (0.05, 1000): a vertical FOV from about 0.1 deg
// (the binocular and scope zooms narrow the main camera; the friendly
// crosshair matters most there) to about 175 deg.
inline constexpr float kMinProjScale = 0.05f, kMaxProjScale = 1000.0f;
// Checks vtables, the non-perspective flag, the pixel size, the rect, finite
// values, the projection shape (0x241b00: P23 = 1, P33 = 0, P00 < 0, P11 > 0,
// |lens shift| <= 0.05, every other off-diagonal ~0) and View x Proj == VP.
Refusal ValidateCamera(const CameraRaw& raw, const CameraExpect& expect, Camera& out) noexcept;
// Per-tick camera bookkeeping (game thread). A torn read reuses the last good
// camera for up to two consecutive ticks directly after it; any other refusal,
// or a tick that read nothing (reads false), forgets it. Returns the refusal
// after the reuse (camera then holds the reused one).
struct CameraHistory { Camera last; bool last_ok; std::uint32_t torn_streak, torn_total; };
Refusal ApplyCameraHistory(CameraHistory& history, bool reads, Refusal refusal, Camera& camera) noexcept;
// The census line: once per mission on the first tick that read the
// viewport's vtable (thread/selector/handle/viewport refusals wait), and once
// more when that census refused and a later tick passes.
enum class CensusState : std::uint8_t { none, refused, ok };
bool CensusDue(CensusState& state, Refusal refusal) noexcept;

// --- Projection ---------------------------------------------------------------
inline constexpr float kMinW = 0.1f;          // in front of the camera iff clip.w > 0.1 m
inline constexpr float kMaxNdc = 1.15f;       // a little beyond the edges still draws (clipped)
struct Projected { bool in_front; float ndc[2]; float w; };
Projected Project(const Mat4& viewproj, Vec3 world) noexcept;
// [x y z 1] * M, xyz part (for the rigid view matrix: view space).
Vec3 Transform(const Mat4& m, Vec3 p) noexcept;
// NDC -> display units (D3D top-left origin) for a display of width x height
// showing the viewport rect: px = width*(x0 + w*(ndc.x+1)/2), py = height*(y0 + h*(1-ndc.y)/2).
void NdcToDisplay(const float (&rect)[4], const float (&ndc)[2], float width, float height, float& x, float& y) noexcept;
// Matrix product a x b (row vectors).
Mat4 Multiply(const Mat4& a, const Mat4& b) noexcept;

// --- Body -----------------------------------------------------------------------
// Stance 0 stand, 1 squat, 2 prone (partner Tick / remote frame spelling).
inline constexpr float kStandHeight = 1.80f, kSquatHeight = 1.25f, kProneHeight = 0.55f, kHeadLift = 0.25f;
inline constexpr float kCapsuleRadius = 0.30f, kProneHalfLength = 0.85f;
float StanceHeight(std::uint32_t stance) noexcept;
struct Capsule { Vec3 a, b; float radius; };
// Stand/squat: vertical from foot+0.15 to foot+height-0.15. Prone: horizontal,
// +-0.85 m along the body's forward (sin yaw, 0, cos yaw) at foot+0.25.
Capsule BodyCapsule(Vec3 foot, float yaw, std::uint32_t stance) noexcept;
// Yaw about +Y of a rotation quaternion {x, y, z, w} (PublishDebugSnapshot's formula).
float QuatYaw(const float (&q)[4]) noexcept;

// --- Friendly crosshair: the screen-centre ray against the partner's capsule ---
inline constexpr float kRayMin = 0.1f, kRayMax = 150.0f;
struct Aim {
    bool hit;
    float distance;          // closest ray-segment distance (m)
    float t;                 // along the ray (m)
    float miss_yaw_deg;      // + = the capsule centre is right of the crosshair on screen
    float miss_pitch_deg;    // + = above
};
// The ray leaves the view origin through NDC (0,0) (lens shift included);
// the capsule is transformed by the (rigid) view matrix.
Aim CentreRay(const Mat4& view, const Mat4& proj, const Capsule& world_capsule) noexcept;

// --- Easing and fade ------------------------------------------------------------
struct Ease { bool valid; float value; std::uint64_t ms; };
// Exponential approach with time constant tau_ms; the first call snaps.
float EaseToward(Ease& ease, float target, std::uint64_t now_ms, std::uint32_t tau_ms) noexcept;
// 1.0 up to 40 m, linear to 0.4 at 150 m, 0.4 beyond.
float FadeAlpha(float distance_m) noexcept;

// --- Names ----------------------------------------------------------------------
inline constexpr std::size_t kNameChars = 24, kNameBytes = 48;
// Strict UTF-8 decode (an invalid sequence becomes '?'); drops C0/C1 controls,
// DEL, bidi controls (U+061C, U+200E/F, U+202A..202E, U+2066..2069), zero-width
// characters (U+200B..200D, U+2060, U+FEFF) and U+00AD; keeps U+0020..007E and
// U+00A0..00FF (the overlay font's glyphs), anything else becomes '?'; collapses
// whitespace runs (space, U+00A0) to one space, trims, keeps at most 24 code
// points. Output is UTF-8 (at most 48 bytes). Returns the code point count.
std::size_t SanitizeName(const char* text, std::size_t size, char (&out)[kNameBytes + 1]) noexcept;
// The label when the partner sent no usable name: the host sees "Player 2",
// the guest sees "Host".
const char* FallbackName(bool viewer_is_host) noexcept;
// lab.ini nametag_name: 1..24 of [A-Za-z0-9 _.-], no leading/trailing space.
bool LabNameValid(const char* text, std::size_t size) noexcept;

// --- Wire: reliable channel 3 ---------------------------------------------------
// [0] 'N' [1] version 1 [2] flags 0 [3] length 0..48 [4..7] the sender's link
// session number (u32 LE, informational: session numbers are per endpoint) [8..] UTF-8.
inline constexpr std::uint8_t kChannel = 3, kMagic = 0x4E, kVersion = 1;
inline constexpr std::size_t kWireHeader = 8, kWireMax = kWireHeader + kNameBytes;
std::size_t EncodeName(const char* name, std::uint32_t session, std::uint8_t (&out)[kWireMax]) noexcept;
enum class WireError : std::uint8_t { none, size, magic, version, flags, length };
const char* WireErrorName(WireError value) noexcept;
// Decodes and re-sanitizes (out may be empty: the fallback applies).
WireError DecodeName(const std::uint8_t* bytes, std::size_t size, char (&out)[kNameBytes + 1], std::uint32_t& session) noexcept;

// --- Text commands (F9 menu, console, worker run) --------------------------------
// "nametag on|off|status" and "nametag crosshair aim|always|off".
enum class CommandOp : std::uint8_t { none, on, off, status, crosshair };
struct Command { CommandOp op; Crosshair crosshair; };
bool IsCommand(const char* text) noexcept;  // "nametag" or "nametag ..."
bool ParseCommand(const char* text, Command& out) noexcept;

// --- Visibility -------------------------------------------------------------------
enum class Reason : std::uint8_t {
    ok, off, camera, terminating, hud, no_partner, range, lost, no_body, behind, offscreen, too_far, stale, shutdown
};
const char* ReasonName(Reason value) noexcept;
enum class NameSource : std::uint8_t { fallback, remote };
enum class LocalSource : std::uint8_t { none, lab, self, coop, steam };
const char* LocalSourceName(LocalSource value) noexcept;
enum class AimSource : std::uint8_t { none, gun, hold };
const char* AimSourceName(AimSource value) noexcept;
// HUD bits from debug_menu.lua (STATE_V1 field 13).
inline constexpr std::uint32_t kHudDemo = 1, kHudIdroid = 2;

// What the game thread hands to Present (copied under a lock; Present never
// reads game memory).
struct Frame {
    std::uint64_t seq, tick_ms;   // GetTickCount64 at publish
    bool visible;                 // draw the tag
    bool crosshair;               // draw the friendly ring
    bool hover;
    float ndc[2];
    float rect[4];
    float alpha;
    float dist_m;
    char name[kNameBytes + 1];
};
inline constexpr std::uint64_t kStaleMs = 250;  // pause, loading and title stop the Lua tick

// Render-side counters (Present thread).
struct RenderStats {
    std::uint32_t frames, drawn, stale_frames, hidden_frames, crosshair_frames;
    bool crosshair;
    float last_px[2];
    float label_box[4];           // x0, y0, x1, y1 in display units
    float ring[3];                // centre x, y and radius of the last ring
    std::uint32_t bb[2], display[2];
    float alpha;
    std::uint32_t present_thread, age_ms;
};

// Game-thread telemetry (state.json "nametag", the status command).
struct CameraTelemetry {
    bool ok;
    Refusal refusal;
    std::uint64_t vp_vt, cam_vt;  // RVAs (0 unread)
    std::uint32_t px[2];
    float rect[4];
    float vfov_deg, near_m, far_m, vp_err;
    std::uint32_t torn, thread;
};
struct Telemetry {
    bool on;
    Crosshair crosshair;
    bool visible;
    Reason reason;
    char name[kNameBytes + 1];
    NameSource name_src;
    char local_name[kNameBytes + 1];
    LocalSource local_src;
    std::uint32_t name_tx, name_rx, name_invalid, link_session;
    bool body_ok;
    float body[3], foot[3], head[3];
    std::uint32_t stance;
    float dist_m;
    bool projected;
    float ndc[2], w;
    float miss_yaw_deg, miss_pitch_deg;
    bool hover, aim, crosshair_on;
    AimSource aim_src;
    std::uint32_t hud;
    std::uint64_t seq, tick_ms;
    CameraTelemetry camera;
};
// `,"nametag":{...}` (a leading comma) for the worker state.json; its length,
// or 0 (out[0] = 0) when it does not fit in `capacity` bytes with the NUL.
std::size_t FormatJson(const Telemetry& t, const RenderStats& r, char* out, std::size_t capacity) noexcept;
// "nametag;on=1;xh=aim;st=visible;why=ok;name=GuestTag;src=remote;d=6.1;stance=0;px=972,322;hov=0;aim=0;stale_frames=0"
// (<= 160 characters; the name is reduced to [A-Za-z0-9_.-], others '_').
std::size_t FormatStatus(const Telemetry& t, const RenderStats& r, char* out, std::size_t capacity) noexcept;

} // namespace mgscoop::nametag
