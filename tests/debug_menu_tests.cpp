#include "mgscoop/debug_menu.hpp"
#include "mgscoop/debug_actions.hpp"
#include "mgscoop/nametag.hpp"
#include "mgscoop/native_ui_core.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <initializer_list>
#include <thread>

namespace debug = mgscoop::debug_menu;
namespace {
unsigned checks{}, failures{};
void Check(bool value, const char* label) {
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); }
}
// Partner name tag core (docs/partner-nametag.md): projection with the
// emulated 0x241b00 matrix, camera refusals, the centre ray, easing/fade, the
// name sanitizer, the channel-3 wire, the commands, the STATE_V1 HUD field and
// the shared handoff.
namespace tag = mgscoop::nametag;
bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
// One snapshot copy at a time: main reads many snapshots, and each by-value
// temporary (about 20 KB with the native telemetry) would get its own stack
// slot in one frame (stack overflow once the telemetry grows).
const debug::Snapshot& Snap() {
    static debug::Snapshot snapshot{};
    snapshot = debug::GetSnapshot();
    return snapshot;
}
// The game's projection (0x241b00, emulated in tools/generate_nametag_contract.py):
// P00 = p*b, P11 = p*c, P20/P21 lens shift, P23 = 1, P33 = 0, depth near +1 / far -1.
tag::Mat4 GameProjection(float p, float b, float c, float sx = 0.0f, float sy = 0.0f) {
    tag::Mat4 m{};
    m.m[0][0] = p * b; m.m[1][1] = p * c; m.m[2][0] = sx; m.m[2][1] = sy;
    m.m[2][2] = -1.000025f; m.m[2][3] = 1.0f; m.m[3][2] = 0.10000125f; m.m[3][3] = 0.0f;
    return m;
}
// A rigid view (0x438c20 style: rows R^T, translation row -pos*R^T) from camera
// axes right/up/forward (world) and position.
tag::Mat4 View(tag::Vec3 right, tag::Vec3 up, tag::Vec3 forward, tag::Vec3 pos) {
    tag::Mat4 v{};
    const tag::Vec3 axes[3] = {right, up, forward};
    for (int j = 0; j < 3; ++j) { v.m[0][j] = axes[j].x; v.m[1][j] = axes[j].y; v.m[2][j] = axes[j].z; }
    for (int j = 0; j < 3; ++j) v.m[3][j] = -(pos.x * v.m[0][j] + pos.y * v.m[1][j] + pos.z * v.m[2][j]);
    v.m[3][3] = 1.0f;
    return v;
}
void NametagTests() {
    // --- Projection (16:9, 1920x1080, full viewport) ---------------------------------
    const tag::Mat4 proj = GameProjection(1.2f, -1.0f, 16.0f / 9.0f);
    const tag::Mat4 view = View({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {10, 2, -5});
    const tag::Mat4 vp = tag::Multiply(view, proj);
    const float full[4] = {0, 0, 1, 1};
    auto ahead = tag::Project(vp, {10, 2, 5});
    float x = 0, y = 0;
    tag::NdcToDisplay(full, ahead.ndc, 1920, 1080, x, y);
    Check(ahead.in_front && Near(ahead.w, 10.0f) && Near(ahead.ndc[0], 0) && Near(ahead.ndc[1], 0) && Near(x, 960) && Near(y, 540),
          "a point straight ahead projects to the viewport centre (clip.w = view z)");
    auto right = tag::Project(vp, {8, 2, 5});   // view x -2: P00 < 0 mirrors it to the screen's right
    tag::NdcToDisplay(full, right.ndc, 1920, 1080, x, y);
    Check(right.in_front && Near(right.ndc[0], 0.24f) && x > 960 && Near(y, 540), "x is mirrored (b < 0): view -x is screen right");
    auto up = tag::Project(vp, {10, 3, 5});
    tag::NdcToDisplay(full, up.ndc, 1920, 1080, x, y);
    Check(up.in_front && Near(up.ndc[1], 0.21333f) && y < 540, "view +y is screen up (D3D top-left origin)");
    auto edge = tag::Project(vp, {10.0f - 10.0f / 1.2f, 2, 5});
    tag::NdcToDisplay(full, edge.ndc, 1920, 1080, x, y);
    Check(Near(edge.ndc[0], 1.0f) && Near(x, 1920.0f, 0.05f), "the horizontal FOV edge maps to the right border");
    Check(!tag::Project(vp, {10, 2, -10}).in_front && !tag::Project(vp, {10, 2, -4.95f}).in_front, "behind or within 0.1 m: not in front");
    const float inset[4] = {0.25f, 0.5f, 0.5f, 0.5f};
    tag::NdcToDisplay(inset, ahead.ndc, 1000, 800, x, y);
    Check(Near(x, 500) && Near(y, 600), "a sub-rect viewport maps NDC into its own rect");
    const tag::Mat4 shifted = tag::Multiply(view, GameProjection(1.2f, -1.0f, 16.0f / 9.0f, 0.03f, -0.02f));
    auto lens = tag::Project(shifted, {10, 2, 5});
    Check(Near(lens.ndc[0], 0.03f) && Near(lens.ndc[1], -0.02f), "lens shift offsets the projected centre");
    // 21:9 (2560x1080): b = -(16h)/(9w), c = 16/9 (Hor+): a wider horizontal view, the same vertical one.
    const tag::Mat4 wide = tag::Multiply(view, GameProjection(1.2f, -(16.0f * 1080) / (9.0f * 2560), 16.0f / 9.0f));
    auto wide_right = tag::Project(wide, {8, 2, 5}), wide_up = tag::Project(wide, {10, 3, 5});
    Check(Near(wide_right.ndc[0], 0.18f) && Near(wide_up.ndc[1], up.ndc[1]), "21:9 widens the horizontal view only");
    // A yawed camera: looking along world +X.
    const tag::Mat4 yawed = tag::Multiply(View({0, 0, -1}, {0, 1, 0}, {1, 0, 0}, {0, 1, 0}), proj);
    auto yaw_ahead = tag::Project(yawed, {25, 1, 0});
    Check(yaw_ahead.in_front && Near(yaw_ahead.w, 25.0f) && Near(yaw_ahead.ndc[0], 0) && Near(yaw_ahead.ndc[1], 0), "rotated view: ahead is the centre");
    Check(!tag::Project(yawed, {-25, 1, 0}).in_front, "rotated view: behind");
    const tag::Vec3 local = tag::Transform(view, {10, 2, 5});
    Check(Near(local.x, 0) && Near(local.y, 0) && Near(local.z, 10), "Transform applies the row-vector view");

    // --- Camera validation (refusals in check order) -----------------------------------
    tag::CameraRaw raw{};
    raw.vp_vtable = 0x140000000ull + 0x20f1c08; raw.cam = 0x5000; raw.cam_vtable = 0x140000000ull + 0x20f08c8;
    std::memcpy(raw.proj, proj.m, sizeof(raw.proj)); std::memcpy(raw.view, view.m, sizeof(raw.view)); std::memcpy(raw.viewproj, vp.m, sizeof(raw.viewproj));
    raw.rect[2] = raw.rect[3] = 1.0f; raw.px_w = 1920; raw.px_h = 1080; raw.pixel_aspect = 1.0f; raw.cam_flags = 0x18;
    raw.near_m = 0.05f; raw.far_m = 4000.0f;
    const tag::CameraExpect expect{0x140000000ull + 0x20f1c08, 0x140000000ull + 0x21e9e68, 0x140000000ull + 0x20f08c8};
    tag::Camera camera{};
    Check(tag::ValidateCamera(raw, expect, camera) == tag::Refusal::none && Near(camera.vfov_deg, 2.0f * std::atan(1.0f / 2.1333334f) * 57.2957795f, 0.01f) &&
          camera.vp_err < 1e-5f, "a consistent perspective viewport passes");
    const auto refused = [&](auto mutate) { tag::CameraRaw r = raw; mutate(r); tag::Camera c{}; return tag::ValidateCamera(r, expect, c); };
    Check(refused([](tag::CameraRaw& r) { r.vp_vtable = 0x140000000ull + 0x21e9e68; }) == tag::Refusal::sub_viewport, "TppSubViewport refused");
    Check(refused([](tag::CameraRaw& r) { r.vp_vtable = 0x140000000ull + 0x20f1c10; }) == tag::Refusal::vp_vtable, "unknown viewport vtable refused");
    Check(refused([](tag::CameraRaw& r) { r.cam = 0; }) == tag::Refusal::camera, "no camera refused");
    Check(refused([](tag::CameraRaw& r) { r.cam_vtable = 1; }) == tag::Refusal::cam_vtable, "unknown camera vtable refused");
    Check(refused([](tag::CameraRaw& r) { r.cam_flags = 0x1c; }) == tag::Refusal::ortho, "camera+0x100 bit 2 (non-perspective) refused");
    Check(refused([](tag::CameraRaw& r) { r.px_w = 32; }) == tag::Refusal::size && refused([](tag::CameraRaw& r) { r.px_h = 20000; }) == tag::Refusal::size,
          "pixel size outside 64..16384 refused");
    Check(refused([](tag::CameraRaw& r) { r.rect[2] = 0; }) == tag::Refusal::rect && refused([](tag::CameraRaw& r) { r.rect[0] = 0.5f; }) == tag::Refusal::rect &&
          refused([](tag::CameraRaw& r) { r.rect[1] = NAN; }) == tag::Refusal::rect, "empty, overflowing or non-finite rect refused");
    Check(refused([](tag::CameraRaw& r) { r.view[3][0] = INFINITY; }) == tag::Refusal::nonfinite, "non-finite matrix refused");
    Check(refused([](tag::CameraRaw& r) { r.proj[2][3] = 0.5f; }) == tag::Refusal::proj_form && refused([](tag::CameraRaw& r) { r.proj[3][3] = 1.0f; }) == tag::Refusal::proj_form &&
          refused([](tag::CameraRaw& r) { r.proj[0][0] = 1.2f; }) == tag::Refusal::proj_form && refused([](tag::CameraRaw& r) { r.proj[2][0] = 0.1f; }) == tag::Refusal::proj_form &&
          refused([](tag::CameraRaw& r) { r.proj[0][1] = 0.2f; }) == tag::Refusal::proj_form, "projection shape checks (P23, P33, sign, shift, off-diagonals)");
    Check(refused([](tag::CameraRaw& r) { r.viewproj[0][0] *= 1.01f; }) == tag::Refusal::torn, "View x Proj != VP (a torn read) refused");
    tag::CameraRaw far_raw = raw;
    const tag::Mat4 far_view = View({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {3000, 400, -2500});
    std::memcpy(far_raw.view, far_view.m, sizeof(far_raw.view));
    tag::Mat4 far_vp = tag::Multiply(far_view, proj);
    far_vp.m[3][2] += 0.001f;  // float rounding of a large translation row stays within tolerance
    std::memcpy(far_raw.viewproj, far_vp.m, sizeof(far_raw.viewproj));
    Check(tag::ValidateCamera(far_raw, expect, camera) == tag::Refusal::none, "large world coordinates: per-row tolerance");
    // Binocular / scope zoom narrows the main camera: a ~0.3 deg vertical FOV still passes, beyond 1000 is refused.
    const auto zoomed = [&](float p) {
        tag::CameraRaw r = raw;
        const tag::Mat4 zp = GameProjection(p, -1.0f, 16.0f / 9.0f), zvp = tag::Multiply(view, zp);
        std::memcpy(r.proj, zp.m, sizeof(r.proj)); std::memcpy(r.viewproj, zvp.m, sizeof(r.viewproj));
        tag::Camera c{};
        return tag::ValidateCamera(r, expect, c);
    };
    Check(zoomed(30.0f) == tag::Refusal::none && zoomed(225.0f) == tag::Refusal::none, "narrow zoomed FOV (P11 53 and 400) passes");
    Check(zoomed(600.0f) == tag::Refusal::proj_form && zoomed(0.02f) == tag::Refusal::proj_form, "P11 beyond 1000 or below 0.05 refused");

    // --- Torn-read reuse and the census ------------------------------------------------
    {
        tag::CameraHistory history{};
        tag::Camera good{}, read{};
        good.vfov_deg = 50.0f; good.px_w = 1920;
        read = good;
        Check(tag::ApplyCameraHistory(history, true, tag::Refusal::torn, read) == tag::Refusal::torn && history.torn_total == 1,
              "a torn read without a good camera before it hides");
        read = good;
        Check(tag::ApplyCameraHistory(history, true, tag::Refusal::none, read) == tag::Refusal::none && history.last_ok, "a good read is kept");
        read = {};
        Check(tag::ApplyCameraHistory(history, true, tag::Refusal::torn, read) == tag::Refusal::none && Near(read.vfov_deg, 50.0f), "first torn read reuses it");
        read = {};
        Check(tag::ApplyCameraHistory(history, true, tag::Refusal::torn, read) == tag::Refusal::none && read.px_w == 1920, "second torn read reuses it");
        read = {};
        Check(tag::ApplyCameraHistory(history, true, tag::Refusal::torn, read) == tag::Refusal::torn && !history.last_ok && history.torn_total == 4,
              "the third consecutive torn read hides");
        read = good;
        tag::ApplyCameraHistory(history, true, tag::Refusal::none, read);
        read = {};
        Check(tag::ApplyCameraHistory(history, true, tag::Refusal::sub_viewport, read) == tag::Refusal::sub_viewport && !history.last_ok,
              "another refusal forgets the last camera");
        read = {};
        Check(tag::ApplyCameraHistory(history, true, tag::Refusal::torn, read) == tag::Refusal::torn, "so a torn read after it is not papered over");
        read = good;
        tag::ApplyCameraHistory(history, true, tag::Refusal::none, read);
        read = {};
        Check(tag::ApplyCameraHistory(history, false, tag::Refusal::none, read) == tag::Refusal::none && !history.last_ok && history.torn_streak == 0,
              "a tick without reads (off, demo, iDroid, hidden partner) forgets it too");
        read = {};
        Check(tag::ApplyCameraHistory(history, true, tag::Refusal::torn, read) == tag::Refusal::torn, "no stale camera after the gap");

        tag::CensusState census = tag::CensusState::none;
        Check(!tag::CensusDue(census, tag::Refusal::selector) && !tag::CensusDue(census, tag::Refusal::handle) &&
              !tag::CensusDue(census, tag::Refusal::viewport) && !tag::CensusDue(census, tag::Refusal::thread) && census == tag::CensusState::none,
              "census waits for a read viewport vtable");
        Check(tag::CensusDue(census, tag::Refusal::vp_vtable) && census == tag::CensusState::refused, "census logs the first vtable read");
        Check(!tag::CensusDue(census, tag::Refusal::torn) && !tag::CensusDue(census, tag::Refusal::vp_vtable), "one refused census per mission");
        Check(tag::CensusDue(census, tag::Refusal::none) && census == tag::CensusState::ok, "a later passing tick logs once more");
        Check(!tag::CensusDue(census, tag::Refusal::none) && !tag::CensusDue(census, tag::Refusal::rect), "then nothing until the next mission");
        census = tag::CensusState::none;
        Check(tag::CensusDue(census, tag::Refusal::none) && !tag::CensusDue(census, tag::Refusal::cam_vtable), "a first passing census is the only one");
    }
    Check(!std::strcmp(tag::RefusalName(tag::Refusal::proj_form), "proj_form") && !std::strcmp(tag::RefusalName(tag::Refusal::sub_viewport), "sub_viewport"),
          "refusal spellings");

    // --- Centre ray and the partner capsule -------------------------------------------
    const tag::Mat4 origin = View({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0});
    auto aim = tag::CentreRay(origin, proj, tag::BodyCapsule({0, -1, 10}, 0.0f, 0));
    Check(aim.hit && aim.distance < 1e-4f && Near(aim.t, 10.0f, 0.01f), "standing partner straight ahead: hit at 10 m");
    Check(tag::CentreRay(origin, proj, tag::BodyCapsule({0.25f, -1, 10}, 0.0f, 0)).hit, "inside the 0.30 m radius: hit");
    aim = tag::CentreRay(origin, proj, tag::BodyCapsule({0.35f, -1, 10}, 0.0f, 0));
    Check(!aim.hit && Near(aim.distance, 0.35f), "outside the radius: miss");
    Check(tag::CentreRay(origin, proj, tag::BodyCapsule({0, 0.0f, 10}, 0.0f, 0)).hit, "the axis starts 0.15 m above the foot, inside the radius");
    Check(!tag::CentreRay(origin, proj, tag::BodyCapsule({0, 0.2f, 10}, 0.0f, 0)).hit, "a capsule entirely above the ray: miss");
    Check(tag::CentreRay(origin, proj, tag::BodyCapsule({0, -1.3f, 10}, 0.0f, 1)).hit && !tag::CentreRay(origin, proj, tag::BodyCapsule({0, -1.6f, 10}, 0.0f, 1)).hit,
          "squat capsule is 1.25 m tall");
    Check(tag::CentreRay(origin, proj, tag::BodyCapsule({0, -0.25f, 10}, 0.0f, 2)).hit, "prone along the ray: hit");
    Check(tag::CentreRay(origin, proj, tag::BodyCapsule({1.0f, -0.25f, 10}, 1.5707963f, 2)).hit &&
          !tag::CentreRay(origin, proj, tag::BodyCapsule({1.0f, -0.25f, 10}, 0.0f, 2)).hit, "prone across the ray follows the body yaw");
    Check(!tag::CentreRay(origin, proj, tag::BodyCapsule({0, -1, -10}, 0.0f, 0)).hit, "behind the camera: miss");
    Check(!tag::CentreRay(origin, proj, tag::BodyCapsule({0, -1, 200}, 0.0f, 0)).hit, "beyond 150 m: miss");
    Check(tag::CentreRay(origin, proj, tag::BodyCapsule({0, -1, 0.05f}, 0.0f, 0)).hit, "the ray starts 0.1 m out; a body at the lens still counts");
    aim = tag::CentreRay(origin, proj, tag::BodyCapsule({2, -1, 10}, 0.0f, 0));
    Check(!aim.hit && Near(aim.miss_yaw_deg, -11.3099f, 0.01f), "view +x is screen left: negative miss_yaw");
    aim = tag::CentreRay(origin, proj, tag::BodyCapsule({0, 1, 10}, 0.0f, 0));
    Check(!aim.hit && aim.miss_pitch_deg > 5.0f, "a capsule above the crosshair: positive miss_pitch");
    const tag::Mat4 shifted_proj = GameProjection(1.2f, -1.0f, 16.0f / 9.0f, 0.03f, 0.0f);
    aim = tag::CentreRay(origin, shifted_proj, tag::BodyCapsule({0.25f, -1, 10}, 0.0f, 0));
    Check(aim.hit && aim.distance < 0.01f, "the centre ray includes the lens shift");
    Check(Near(tag::QuatYaw({0, 0.7071068f, 0, 0.7071068f}), 1.5707963f) && Near(tag::QuatYaw({0, 0, 0, 1}), 0.0f), "quaternion yaw");
    Check(Near(tag::StanceHeight(0), 1.8f) && Near(tag::StanceHeight(1), 1.25f) && Near(tag::StanceHeight(2), 0.55f) && Near(tag::StanceHeight(9), 1.8f),
          "stance heights");

    // --- Easing and fade ----------------------------------------------------------------
    tag::Ease ease{};
    Check(Near(tag::EaseToward(ease, 1.8f, 1000, 150), 1.8f), "the first sample snaps");
    Check(Near(tag::EaseToward(ease, 1.25f, 1150, 150), 1.8f - 0.55f * 0.6321206f), "150 ms: 63% of the way");
    Check(Near(tag::EaseToward(ease, 1.25f, 3000, 150), 1.25f), "settles");
    Check(Near(tag::EaseToward(ease, 0.55f, 2000, 150), 0.55f), "a clock going backwards snaps");
    Check(Near(tag::FadeAlpha(0), 1) && Near(tag::FadeAlpha(40), 1) && Near(tag::FadeAlpha(95), 0.7f) && Near(tag::FadeAlpha(150), 0.4f) &&
          Near(tag::FadeAlpha(900), 0.4f) && Near(tag::FadeAlpha(NAN), 1), "fade 1.0 to 40 m, 0.4 at 150 m");

    // --- Name sanitizer ------------------------------------------------------------------
    char name[tag::kNameBytes + 1]{};
    const auto clean = [&](const char* text, std::size_t size = 0) {
        return tag::SanitizeName(text, size ? size : std::strlen(text), name);
    };
    Check(clean("GuestTag") == 8 && !std::strcmp(name, "GuestTag"), "plain ASCII unchanged");
    Check(clean("  Big   Boss \t ") == 8 && !std::strcmp(name, "Big Boss"), "whitespace collapsed and trimmed, tab dropped");
    Check(clean("\x01" "Ab\x7f" "c\x1b[31m") == 7 && !std::strcmp(name, "Abc[31m"), "C0 controls and DEL dropped");
    Check(clean("\xE2\x80\xAE" "evil" "\xE2\x80\xAC\xE2\x81\xA6" "x" "\xD8\x9C") == 5 && !std::strcmp(name, "evilx"), "bidi controls dropped");
    Check(clean("a\xE2\x80\x8B" "b\xEF\xBB\xBF" "c\xC2\xAD" "d") == 4 && !std::strcmp(name, "abcd"), "zero-width, BOM and soft hyphen dropped");
    Check(clean("Jos\xC3\xA9 \xC3\x91" "a\xC2\xA0\xC2\xA0x") == 9 && !std::strcmp(name, "Jos\xC3\xA9 \xC3\x91" "a x"), "Latin-1 kept; NBSP collapses");
    Check(clean("\xE6\x97\xA5\xE6\x9C\xAC" "Z") == 3 && !std::strcmp(name, "??Z"), "outside the font: one '?' per code point");
    Check(clean("\xC2\x85" "a\xC2\x9F") == 1 && !std::strcmp(name, "a"), "C1 controls dropped");
    Check(clean("\xC3(") == 2 && !std::strcmp(name, "?("), "a broken sequence costs one '?'");
    Check(clean("\xC0\xAF" "x") == 2 && !std::strcmp(name, "?x"), "overlong encoding refused");
    Check(clean("\xED\xA0\x80" "y") == 2 && !std::strcmp(name, "?y"), "encoded surrogate refused");
    Check(clean("z\xF0\x9F\x98") == 2 && !std::strcmp(name, "z?"), "truncated sequence at the end");
    Check(clean("\xF0\x9F\x98\x80" "!") == 2 && !std::strcmp(name, "?!"), "astral code point is one '?'");
    Check(clean("ab\0cd", 5) == 4 && !std::strcmp(name, "abcd"), "embedded NUL dropped");
    Check(clean("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123") == 24 && !std::strcmp(name, "ABCDEFGHIJKLMNOPQRSTUVWX"), "at most 24 code points");
    Check(clean("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9"
                "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9") == 24 &&
          std::strlen(name) == 48, "24 Latin-1 code points fill exactly 48 bytes");
    Check(clean("ABCDEFGHIJKLMNOPQRSTUVW XYZ") == 23 && !std::strcmp(name, "ABCDEFGHIJKLMNOPQRSTUVW"), "never ends on the collapsed space");
    Check(clean("") == 0 && !name[0] && clean(" \t\xE2\x80\x8B ") == 0 && !name[0], "empty after sanitizing");
    Check(tag::SanitizeName(nullptr, 4, name) == 0 && !name[0], "null input");
    Check(!std::strcmp(tag::FallbackName(true), "Player 2") && !std::strcmp(tag::FallbackName(false), "Host"), "fallback names by viewer role");
    Check(tag::LabNameValid("HostTag", 7) && tag::LabNameValid("A b.c-d_e", 9) && !tag::LabNameValid("", 0) && !tag::LabNameValid(" a", 2) &&
          !tag::LabNameValid("a ", 2) && !tag::LabNameValid("a;b", 3) && !tag::LabNameValid("ABCDEFGHIJKLMNOPQRSTUVWXY", 25), "lab.ini nametag_name charset");

    // --- Wire (reliable channel 3) --------------------------------------------------------
    std::uint8_t wire[tag::kWireMax]{};
    std::uint32_t session = 0;
    auto size = tag::EncodeName("HostTag", 0x01020304u, wire);
    Check(size == 15 && wire[0] == 'N' && wire[1] == 1 && wire[2] == 0 && wire[3] == 7 && wire[4] == 4 && wire[7] == 1, "wire layout");
    Check(tag::DecodeName(wire, size, name, session) == tag::WireError::none && !std::strcmp(name, "HostTag") && session == 0x01020304u, "round trip");
    size = tag::EncodeName("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9"
                           "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 1, wire);
    Check(size == tag::kWireMax && tag::DecodeName(wire, size, name, session) == tag::WireError::none && std::strlen(name) == 48, "longest name");
    size = tag::EncodeName("\xE2\x80\xAE" "x", 2, wire);
    Check(size == 9 && tag::DecodeName(wire, size, name, session) == tag::WireError::none && !std::strcmp(name, "x"), "the sender sanitizes too");
    std::uint8_t hostile[] = {'N', 1, 0, 5, 0, 0, 0, 0, 0xE2, 0x80, 0xAE, 'o', 'k'};
    Check(tag::DecodeName(hostile, sizeof(hostile), name, session) == tag::WireError::none && !std::strcmp(name, "ok"), "the receiver re-sanitizes");
    std::uint8_t empty[] = {'N', 1, 0, 0, 9, 0, 0, 0};
    Check(tag::DecodeName(empty, sizeof(empty), name, session) == tag::WireError::none && !name[0] && session == 9, "an empty name is valid (fallback)");
    size = tag::EncodeName("Ok", 3, wire);
    std::uint8_t bad[tag::kWireMax]{};
    std::memcpy(bad, wire, size); bad[0] = 'M';
    Check(tag::DecodeName(bad, size, name, session) == tag::WireError::magic, "wrong magic");
    std::memcpy(bad, wire, size); bad[1] = 2;
    Check(tag::DecodeName(bad, size, name, session) == tag::WireError::version, "wrong version");
    std::memcpy(bad, wire, size); bad[2] = 1;
    Check(tag::DecodeName(bad, size, name, session) == tag::WireError::flags, "unknown flags");
    std::memcpy(bad, wire, size); bad[3] = 3;
    Check(tag::DecodeName(bad, size, name, session) == tag::WireError::length, "length mismatch");
    std::memcpy(bad, wire, size); bad[3] = 49;
    Check(tag::DecodeName(bad, size, name, session) == tag::WireError::length, "length over 48");
    Check(tag::DecodeName(wire, 7, name, session) == tag::WireError::size && tag::DecodeName(nullptr, 10, name, session) == tag::WireError::size &&
          tag::DecodeName(wire, tag::kWireMax + 1, name, session) == tag::WireError::size, "short, null or oversized message");

    // --- Commands ---------------------------------------------------------------------
    tag::Command command{};
    Check(tag::ParseCommand("nametag on", command) && command.op == tag::CommandOp::on, "nametag on");
    Check(tag::ParseCommand("nametag off", command) && command.op == tag::CommandOp::off, "nametag off");
    Check(tag::ParseCommand("nametag status", command) && command.op == tag::CommandOp::status, "nametag status");
    Check(tag::ParseCommand("nametag crosshair always", command) && command.op == tag::CommandOp::crosshair && command.crosshair == tag::Crosshair::always &&
          tag::ParseCommand("nametag crosshair aim", command) && command.crosshair == tag::Crosshair::aim &&
          tag::ParseCommand("nametag crosshair off", command) && command.crosshair == tag::Crosshair::off, "crosshair modes");
    for (const char* bad_text : {"nametag", "nametag ", "nametag  on", "nametag on ", "nametag on extra", "nametag crosshair", "nametag crosshair 0",
         "nametag crosshair sometimes", "nametagon", "Nametag on", "nametag ON"})
        Check(!tag::ParseCommand(bad_text, command), "malformed name tag command refused");
    Check(tag::IsCommand("nametag") && tag::IsCommand("nametag bogus") && !tag::IsCommand("nametags") && !tag::IsCommand("name tag") && !tag::IsCommand(nullptr),
          "native command routing");
    Check(debug::ValidText("nametag crosshair always") && debug::ValidText("nametag status"), "commands pass the queue charset");
    for (const char* id : {"partner_nametag_on", "partner_nametag_off", "partner_nametag_status"}) {
        const auto* a = mgscoop::debug_actions::FindAction(id);
        Check(a && a->category == mgscoop::debug_actions::Category::partner && a->command && tag::ParseCommand(a->command, command), "F9 Partner rows send valid commands");
    }

    // --- STATE_V1 field 13: HUD bits --------------------------------------------------------
    debug::LuaFlags flags{};
    const auto parse = [&](const char* text) { return debug::ParseLuaState(text, std::strlen(text), flags); };
    Check(parse("0 0 0 none 0 0 0 0 0 -1 -1 ok 3") && flags.hud == 3, "thirteen-field state carries the HUD bits");
    Check(parse("0 0 0 none 0 0 0 0 0 -1 -1 ok") && flags.hud == 0, "twelve-field state: no HUD bits");
    for (const char* bad_state : {"0 0 0 none 0 0 0 0 0 -1 -1 ok 8", "0 0 0 none 0 0 0 0 0 -1 -1 ok -1", "0 0 0 none 0 0 0 0 0 -1 -1 ok 1 2",
         "0 0 0 none 0 0 0 0 0 -1 -1 ok x", "0 0 0 none 0 0 0 0 0 -1 -1 ok  1"})
        Check(!parse(bad_state), "malformed HUD field refused");
    debug::Configure(true, true, true); debug::BeginMission(30010);
    const char hud_state[] = "0 0 0 none 0 0 0 0 0 -1 -1 ok 2";
    Check(debug::LuaStateReceipt(hud_state, sizeof(hud_state) - 1) && debug::LuaHud() == 2, "the HUD bits reach the name tag");
    debug::BeginMission(30020);
    Check(debug::LuaHud() == 0, "a mission change clears the HUD bits");
    Check(debug::LuaStateReceipt(hud_state, sizeof(hud_state) - 1) && debug::LuaHud() == 2, "HUD bits again");
    const char twelve_state[] = "0 0 0 none 0 0 0 0 0 -1 -1 ok";
    Check(debug::LuaStateReceipt(twelve_state, sizeof(twelve_state) - 1) && debug::LuaHud() == 0, "an older 12-field producer clears them");

    // --- Shared handoff (game thread -> Present) ------------------------------------------
    tag::Configure(false, true, tag::Settings{});
    tag::Frame frame{};
    frame.visible = true; frame.tick_ms = 5000; frame.seq = 1; strcpy_s(frame.name, "GuestTag");
    tag::Publish(frame);
    tag::Frame taken{};
    Check(!tag::Enabled() && !tag::Take(taken), "not configured: nothing published or drawn");
    tag::Settings settings{}; settings.crosshair = tag::Crosshair::always; settings.max_m = 120;
    tag::Configure(true, false, settings);
    Check(tag::Enabled() && !tag::Host() && tag::GetSettings().on && tag::GetSettings().crosshair == tag::Crosshair::always && tag::GetSettings().max_m == 120,
          "configured settings");
    Check(!tag::Take(taken), "no frame before the first publish");
    tag::Publish(frame);
    Check(tag::Take(taken) && taken.seq == 1 && !std::strcmp(taken.name, "GuestTag"), "frame handed over");
    Check(tag::RenderPending(5100) && !tag::RenderPending(5000 + tag::kStaleMs + 1) && !tag::RenderPending(4000), "fresh only (stale after 250 ms)");
    frame.visible = false; tag::Publish(frame);
    Check(!tag::RenderPending(5100), "a hidden frame draws nothing");
    frame.crosshair = true; tag::Publish(frame);
    Check(!tag::RenderPending(5100), "legacy crosshair intent alone never draws over the native reticle");
    tag::SetOn(false); tag::SetCrosshair(tag::Crosshair::off);
    Check(!tag::GetSettings().on && tag::GetSettings().crosshair == tag::Crosshair::off, "runtime toggles");
    tag::RenderNote note{};
    note.kind = tag::Drawn::stale; tag::NoteRender(note);
    note.kind = tag::Drawn::drawn; note.px[0] = 900; note.px[1] = 300; note.label_box[2] = 950; note.crosshair = true; note.ring[2] = 10; tag::NoteRender(note);
    note = {}; note.kind = tag::Drawn::hidden; tag::NoteRender(note);
    auto stats = tag::GetRenderStats();
    Check(stats.frames == 3 && stats.stale_frames == 1 && stats.drawn == 1 && stats.hidden_frames == 1 && stats.crosshair_frames == 1 &&
          Near(stats.last_px[0], 900) && Near(stats.label_box[2], 950) && Near(stats.ring[2], 10) && !stats.crosshair, "render counters");
    tag::Telemetry telemetry{};
    telemetry.on = true; telemetry.visible = true; telemetry.dist_m = 6.14f; telemetry.name_src = tag::NameSource::remote;
    strcpy_s(telemetry.name, "Guest Tag;x=\xC3\xA9");
    tag::PublishTelemetry(telemetry);
    char status[200]{};
    tag::FormatStatus(tag::GetTelemetry(), stats, status, sizeof(status));
    Check(!std::strcmp(status, "nametag;on=1;xh=aim;st=visible;why=ok;name=Guest_Tag_x__;src=remote;d=6.1;stance=0;px=900,300;hov=0;aim=0;stale_frames=1"),
          "status line (name reduced to the result charset)");
    telemetry = {}; std::memset(telemetry.name, 'W', 24); telemetry.reason = tag::Reason::terminating; telemetry.crosshair = tag::Crosshair::always;
    telemetry.dist_m = 123456.0f; stats.last_px[0] = -123456.0f; stats.stale_frames = 0xFFFFFFFFu;
    const auto length = tag::FormatStatus(telemetry, stats, status, sizeof(status));
    Check(length <= 160 && std::strstr(status, ";why=terminating;") && std::strstr(status, ";d=9999.9;"), "worst-case status fits in 160 characters");
    tag::Refuse();
    Check(!tag::Enabled() && !tag::Take(taken), "a refusal hides everything");
    tag::Configure(true, true, tag::Settings{});
    tag::Publish(frame);
    tag::Shutdown();
    Check(tag::ShuttingDown() && !tag::Take(taken), "window shutdown hides at once");
    tag::Configure(false, true, tag::Settings{});
}
}
int main() {
    debug::Configure(false, true, true);
    Check(!debug::Enabled(), "disabled by default");
    Check(!debug::QueueCommand("position"), "off rejects commands");
    debug::Configure(true, true, false);
    Check(!debug::Enabled(), "save gate enforced");
    debug::Configure(true, true, true);
    Check(debug::Enabled(), "copied save debug enabled");
    Check(!debug::QueueCommand("position"), "title has no gameplay actions");
    debug::BeginMission(40010);
    Check(!debug::QueueCommand("position"), "ACC action gate");
    debug::BeginMission(30011);
    Check(!debug::QueueCommand("position"), "alternate mission action gate");
    debug::BeginMission(30010);
    for (const auto* bad : {"", "teleport 0 0 0 0;evil()", "weapon hip \"x\"", "position\n", "position\\", "position\t"})
        Check(!debug::QueueCommand(bad), "reject injection and control bytes");
    char huge[debug::kCommandCapacity + 1]{};
    std::memset(huge, 'a', sizeof(huge) - 1);
    Check(!debug::QueueCommand(huge), "reject oversize command");
    Check(debug::QueueCommand("teleport -123.5 456 789 180"), "numeric command queued");
    debug::Command command{};
    Check(debug::Pop(command), "owner drains command");
    Check(std::strcmp(command.text, "teleport -123.5 456 789 180") == 0, "text preserved");
    Check(command.generation == Snap().generation, "mission generation bound");
    debug::Result(command.id, true, "verified_position");
    Check(std::strcmp(Snap().last_result, "verified_position") == 0, "accepted result visible");
    const auto before = Snap();
    debug::Snapshot telemetry{};
    telemetry.mission = 30010;
    telemetry.stage = 3;
    telemetry.player_pos[0] = 123.5f;
    debug::Publish(telemetry);
    const auto after = Snap();
    Check(after.enabled && after.gameplay_ready && after.host && after.saves_redirected, "telemetry cannot disable lifecycle gates");
    Check(after.stage == 3 && after.player_pos[0] == 123.5f, "copied telemetry published");
    Check(after.last_id == before.last_id && after.completed == before.completed, "telemetry keeps action history");
    for (unsigned i = 0; i < debug::kQueueCapacity; ++i) Check(debug::QueueCommand("position"), "bounded queue accepts capacity");
    Check(!debug::QueueCommand("position"), "bounded queue refuses overflow");
    std::uint32_t previous = 0;
    for (unsigned i = 0; i < debug::kQueueCapacity; ++i) {
        Check(debug::Pop(command), "FIFO drains");
        Check(command.id > previous, "IDs ordered");
        previous = command.id;
    }
    Check(!debug::Pop(command), "empty queue returns no command");
    Check(debug::QueueCommand("step 0 0 10"), "pre-travel command accepted");
    const auto old_generation = Snap().generation;
    debug::BeginMission(30020);
    Check(Snap().generation != old_generation && !debug::Pop(command), "travel discards old teleport");
    debug::LuaState(true, 255);
    Check(Snap().infinite_ammo && Snap().bookmark_mask == 255, "Lua-owned cheat state copied");
    debug::EndMission();
    Check(!debug::QueueCommand("position") && !debug::Pop(command), "termination refuses actions");
    debug::BeginMission(30050);
    Check(!Snap().infinite_ammo && Snap().bookmark_mask == 0, "cheat state resets across scenes");

    debug::Configure(true, true, true);
    debug::BeginMission(30010);
    const auto receipt = [](std::uint32_t id, const char* payload) {
        char text[300]{};
        const auto n = std::snprintf(text, sizeof(text), "return 'MGS_COOP_DEBUG_ENEMY_V1' -- %u %s", id, payload);
        return n > 0 && debug::EnemyReceipt(text, static_cast<std::size_t>(n));
    };
    Check(!receipt(1, "clear 30010"), "undispatched metadata cannot authorize a UI list");
    Check(debug::QueueCommand("enemy_camps"), "camp request queued");
    const auto queued_id = Snap().last_id;
    Check(!receipt(queued_id, "clear 30010"), "queued metadata must wait until owner dispatch");
    Check(debug::Pop(command), "camp owner dispatch");
    const auto camps_id = command.id;
    Check(!receipt(camps_id, "clear 30020"), "wrong mission metadata refused");
    Check(receipt(camps_id, "clear 30010"), "current dispatched camp list accepted");
    Check(!receipt(camps_id, "clear 30010"), "duplicate clear cannot erase accepted entries");
    Check(receipt(camps_id, "camp afgh_test_cp -1 64"), "safe camp with unknown loaded distance");
    for (const auto* bad : {"camp afgh_test_cp 5 2", "camp bad/name 5 2", "camp bad 5 0", "camp bad 5 65",
         "camp bad -2 2", "camp bad 1000000001 2", "camp bad nan 2", "camp bad 5 2 extra", "camp bad\t 5 2"})
        Check(!receipt(camps_id, bad), "malformed/duplicate camp is rejected");
    char payload[200]{};
    for (unsigned i = 1; i < 32; ++i) {
        std::snprintf(payload, sizeof(payload), "camp afgh_test_%u_cp 1000000000 1", i);
        Check(receipt(camps_id, payload), "camp list accepts bounded capacity");
    }
    Check(!receipt(camps_id, "camp overflow_cp 1 1"), "camp capacity never overflows");
    Check(receipt(camps_id, "done"), "camp transaction closes");
    Check(!receipt(camps_id, "camp late_cp 1 1"), "closed list rejects late entry");
    Check(debug::QueueCommand("enemy_routes afgh_test_cp") && debug::Pop(command), "selected route owner dispatch");
    const auto routes_id = command.id;
    Check(!receipt(routes_id, "routes wrong_cp"), "route list bound to requested camp");
    Check(receipt(routes_id, "routes afgh_test_cp"), "selected current camp route list accepted");
    Check(!receipt(camps_id, "clear 30010"), "older clear cannot erase newer selected routes");
    Check(!receipt(routes_id, "route 0 rt_zero"), "route index0 rejected");
    Check(!receipt(routes_id, "route 2 rt_gap"), "route gaps rejected");
    for (unsigned i = 1; i <= 64; ++i) {
        std::snprintf(payload, sizeof(payload), "route %u rt_%u", i, i);
        Check(receipt(routes_id, payload), "route list accepts bounded capacity");
    }
    Check(!receipt(routes_id, "route 65 rt_overflow"), "route capacity never overflows");
    Check(receipt(routes_id, "done"), "route transaction closes");
    Check(!receipt(routes_id, "done"), "duplicate completion rejected");
    Check(debug::QueueCommand("spawn_enemy closest") && debug::Pop(command), "spawn owner dispatch");
    const auto spawn_request = command.id;
    Check(!receipt(spawn_request, "spawned 1000000001 0 0 1"), "spawn coordinate overflow rejected");
    Check(!receipt(spawn_request, "spawned 1 2 3 4294967296"), "unsigned entity overflow rejected");
    Check(receipt(spawn_request, "spawned -1000000000 0 1000000000 4294967295"), "signed coordinate and unsigned hash boundaries accepted");
    Check(!receipt(spawn_request, "spawned 1 2 3 1"), "same spawn cannot replay metadata");
    auto enemy_snapshot = Snap();
    Check(enemy_snapshot.enemy_camp_count == 32 && enemy_snapshot.enemy_route_count == 64 &&
          std::strcmp(enemy_snapshot.enemy_routes_camp, "afgh_test_cp") == 0, "copied camps/routes readable by render thread");
    Check(enemy_snapshot.enemy_spawn_valid && enemy_snapshot.enemy_spawn_hash == 0xffffffffu &&
          enemy_snapshot.enemy_spawn_pos_mm[0] == -1000000000 && enemy_snapshot.enemy_spawn_pos_mm[2] == 1000000000,
          "spawn readback remains exact signed mm");
    telemetry = {}; telemetry.mission = 99999; telemetry.world_configured = true; telemetry.world_effective = 3;
    debug::Publish(telemetry);
    enemy_snapshot = Snap();
    Check(enemy_snapshot.mission == 30010 && enemy_snapshot.enemy_camp_count == 32 && enemy_snapshot.enemy_route_count == 64 &&
          enemy_snapshot.enemy_spawn_valid, "telemetry cannot clobber owner mission or Lua enemy metadata");
    Check(enemy_snapshot.world_configured && enemy_snapshot.world_effective == 3, "new world telemetry published independently");
    debug::BeginMission(30020);
    Check(!receipt(spawn_request, "spawned 1 2 3 1") && !Snap().enemy_spawn_valid &&
          Snap().enemy_camp_count == 0, "travel clears metadata and previous-generation authorization");
    Check(debug::QueueCommand("position") && debug::Pop(command), "unrelated owner request");
    Check(!receipt(command.id, "clear 30020"), "unrelated command cannot forge camp metadata");
    Check(debug::QueueCommand("enemy_routes absent_cp") && debug::Pop(command), "unknown camp request delivered for guarded refusal");
    Check(!receipt(command.id, "routes absent_cp"), "metadata requires listed camp");
    Check(debug::QueueCommand("enemy_camps") && debug::Pop(command), "metadata request dispatched before cache-expiry test");
    const auto expired_id = command.id;
    for (unsigned i = 0; i < 64; ++i) Check(debug::QueueCommand("position") && debug::Pop(command), "bounded dispatch authorization cache advances");
    Check(!receipt(expired_id, "clear 30020"), "expired dispatch cannot replay a metadata transaction");
    char oversized[300]{}; std::memset(oversized, 'a', sizeof(oversized));
    Check(!debug::EnemyReceipt(oversized, sizeof(oversized)), "oversize receipt rejected before parsing");
    Check(!debug::EnemyReceipt(nullptr, 0), "null receipt rejected");
    // soldier_warp authorization: only a popped host "spawn_enemy here" of this generation.
    debug::Configure(true, true, true); debug::BeginMission(30010);
    const auto here_queued = debug::QueueCommandId("spawn_enemy here come");
    Check(here_queued && !debug::DispatchedCommand(here_queued, "spawn_enemy here"), "queued (unpopped) spawn cannot authorize a warp");
    Check(debug::Pop(command) && command.id == here_queued, "spawn here dispatched");
    Check(debug::DispatchedCommand(command.id, "spawn_enemy here"), "dispatched spawn here authorizes the warp receipt");
    Check(!debug::DispatchedCommand(command.id, "spawn_enemy here co") && !debug::DispatchedCommand(command.id + 1, "spawn_enemy here") &&
          !debug::DispatchedCommand(0, "spawn_enemy here") && !debug::DispatchedCommand(command.id, "") &&
          !debug::DispatchedCommand(command.id, nullptr), "prefix is a whole word and the id is exact");
    const auto here_id = command.id;
    Check(debug::QueueCommand("spawn_enemy closest") && debug::Pop(command), "closest dispatched");
    Check(!debug::DispatchedCommand(command.id, "spawn_enemy here"), "other spawn modes cannot warp");
    Check(!debug::DispatchedCommand(here_id, "spawn_enemy here"), "a later pop ends the earlier spawn's warp authorization");
    debug::BeginMission(30020);
    Check(!debug::DispatchedCommand(here_id, "spawn_enemy here"), "travel invalidates the warp authorization");
    debug::Configure(true, false, true); debug::BeginMission(30010);
    Check(debug::QueueCommand("spawn_enemy here") && debug::Pop(command), "guest spawn here dispatched for its Lua refusal");
    Check(!debug::DispatchedCommand(command.id, "spawn_enemy here"), "guest cannot authorize a warp");
    Check(debug::QueueCommand("spawn_enemy closest") && debug::Pop(command), "guest queues action for explicit Lua refusal");
    Check(!receipt(command.id, "spawned 1 2 3 1"), "guest cannot publish local guard mutation");
    debug::EndMission(); Check(!receipt(command.id, "clear 30010"), "termination rejects all metadata");
    debug::Configure(true, true, true); debug::BeginMission(30050);

    // Receipts: LOG codes 0 refused, 1 ok, 2 interim; queue refusals are status 3 with id 0.
    debug::Configure(true, true, true); debug::BeginMission(30010);
    const auto first_id = debug::QueueCommandId("teleport 1 2 3 0");
    Check(first_id != 0 && debug::QueueCommandId("position") == first_id + 1, "QueueCommandId returns increasing ids");
    Check(debug::Pop(command) && command.id == first_id, "id popped in order");
    debug::Result(first_id, 2, "submitted_warp");
    auto rs = Snap();
    Check(rs.interim == 1 && rs.completed == 0 && rs.receipts[0].id == first_id && rs.receipts[0].status == 2,
          "interim result is a status-2 receipt and not completed");
    Check(std::strcmp(rs.receipts[0].command, "teleport 1 2 3 0") == 0, "receipt carries the dispatched command text");
    debug::Result(first_id, 1, "warp_position_readback;error_m=0.100");
    rs = Snap();
    Check(rs.completed == 1 && rs.receipts[0].id == first_id && rs.receipts[0].status == 0 && rs.receipts[1].id == 0,
          "final result replaces its interim receipt");
    Check(debug::Pop(command), "second command dispatched");
    debug::Result(command.id, 0, "position_unreadable");
    rs = Snap();
    Check(rs.completed == 2 && rs.rejected == 1 && rs.receipts[0].status == 1 && std::strcmp(rs.receipts[0].command, "position") == 0,
          "refused final result counted and paired by id");
    Check(debug::QueueCommandId("bad;text") == 0, "invalid text returns id 0");
    rs = Snap();
    Check(rs.receipts[0].id == 0 && rs.receipts[0].status == 3 && rs.rejected == 2 && rs.receipts[0].command[0] == 0,
          "queue refusal is a status-3 receipt with id 0 and no unsafe text");
    debug::Result(99999, 3, "bad code");
    Check(Snap().receipts[0].status == 3, "unknown result code ignored");
    for (unsigned i = 0; i < 20; ++i) { Check(debug::QueueCommand("position") && debug::Pop(command), "receipt fill"); debug::Result(command.id, 1, "position=1,2,3;yaw=0"); }
    rs = Snap();
    Check(rs.receipts[15].id != 0 && rs.receipts[0].id == command.id, "receipt ring is bounded and newest first");
    debug::ClearHistory();
    Check(Snap().receipts[0].id == 0 && Snap().completed == rs.completed, "ClearHistory empties only the log");
    // Spawn sync history rows (REMOTE_V1): id 0, "remote spawn_enemy", never a command or a counter.
    const auto remote = [](const char* text) { return debug::RemoteReceipt(text, std::strlen(text)); };
    const auto completed_before = Snap().completed;
    Check(remote("1 remote_spawn;seq=1;ent=4121274369;state=landed;mode=warp;come=0;err_m=0.03"), "applied row accepted");
    rs = Snap();
    Check(rs.receipts[0].id == 0 && rs.receipts[0].status == 0 && std::strcmp(rs.receipts[0].command, "remote spawn_enemy") == 0 &&
          std::strcmp(rs.receipts[0].result, "remote_spawn;seq=1;ent=4121274369;state=landed;mode=warp;come=0;err_m=0.03") == 0 &&
          rs.completed == completed_before, "remote row is an OK receipt with id 0");
    Check(remote("0 guest_spawn;seq=2;why=native_off") && Snap().receipts[0].status == 1, "refused row");
    for (const auto* bad : {"", "1", "1 ", "2 x", "1x y", "1 a b", "1 a\"b", "1 a\nb", "0 a'b"})
        Check(!remote(bad), "malformed remote row rejected");
    char long_row[200]{};
    std::memset(long_row, 'a', 163);
    long_row[0] = '1'; long_row[1] = ' ';
    Check(!remote(long_row), "remote row above 160 characters rejected");
    long_row[162] = 0;
    Check(remote(long_row), "160 characters accepted");
    Check(!debug::RemoteReceipt(nullptr, 3), "null remote row");
    debug::ClearHistory();

    // STATE_V1: exactly 2 (legacy), 12 or 13 (HUD bits) bounded fields.
    debug::LuaFlags flags{};
    const auto parse = [&](const char* text) { return debug::ParseLuaState(text, std::strlen(text), flags); };
    Check(parse("1 129") && flags.infinite_ammo && flags.bookmark_mask == 129, "legacy two-field state accepted");
    Check(parse("1 3 4294967295 warp 34 1 0 1 43200 3000 6000 ok") && flags.pending_id == 4294967295u && std::strcmp(flags.pending_kind, "warp") == 0 &&
          flags.pending_frames == 34 && flags.god_mode && !flags.fulton_infinite && flags.slowmo_active && flags.game_clock_s == 43200 &&
          flags.player_life == 3000 && flags.player_life_max == 6000 && std::strcmp(flags.guard_reason, "ok") == 0, "twelve-field state parsed");
    Check(parse("0 0 0 none 0 0 0 0 0 -1 -1 on_foot") && flags.player_life == -1 && std::strcmp(flags.guard_reason, "on_foot") == 0, "unreadable life is -1");
    for (const auto* bad : {"1", "1 2 3", "0 0 0 none 0 0 0 0 0 -1 -1", "0 0 0 none 0 0 0 0 0 -1 -1 ok extra", "2 0", "1 256",
         "0 0 0 teleport 0 0 0 0 0 -1 -1 ok", "0 0 0 none 0 2 0 0 0 -1 -1 ok", "0 0 0 none 0 0 0 0 86400 -1 -1 ok",
         "0 0 0 none 0 0 0 0 0 -2 -1 ok", "0 0 0 none 0 0 0 0 0 -1 -1 OK", "0  0", " 0 0", "0 0 ", "0 0 0 none 0 0 0 0 0 -1 -1 ok;x",
         "0 0 0 none 100001 0 0 0 0 -1 -1 ok", "0 0 0 none 0 0 0 0 0 -1 -1 aaaaaaaaaaaaaaaaaaaaaaaa"})
        Check(!parse(bad), "malformed or out-of-range state rejected");
    const char twelve[] = "1 5 7 spawn 12 1 1 0 3600 10 20 ok";
    Check(debug::LuaStateReceipt(twelve, sizeof(twelve) - 1), "state receipt applied");
    rs = Snap();
    Check(rs.infinite_ammo && rs.bookmark_mask == 5 && rs.pending_id == 7 && std::strcmp(rs.pending_kind, "spawn") == 0 && rs.god_mode &&
          rs.fulton_infinite && rs.game_clock_s == 3600 && rs.player_life == 10 && rs.player_life_max == 20, "Lua-owned flags visible");
    debug::Snapshot over{}; over.god_mode = false; over.player_life = 1; over.pending_id = 0; over.overlay_open = true; over.receipts[0].id = 77;
    over.enemy_nearby_count = 5; over.enemy_camps_loaded = true; over.interim = 9;
    debug::Publish(over);
    rs = Snap();
    Check(rs.god_mode && rs.player_life == 10 && rs.pending_id == 7 && !rs.overlay_open && rs.receipts[0].id != 77 &&
          rs.enemy_nearby_count == 0 && !rs.enemy_camps_loaded && rs.interim == 1 && rs.published_tick64 != 0,
          "Publish preserves Lua-, receipt-, enemy- and overlay-owned fields");
    debug::OverlayStatus overlay{}; overlay.open = true; overlay.frames = 4; strcpy_s(overlay.category, "enemies");
    const auto before_overlay = Snap();
    const auto serial = debug::RequestMenu("menu open enemies", before_overlay.mission, before_overlay.generation);
    std::uint32_t taken = 0; char menu_text[debug::kCommandCapacity]{};
    Check(serial && debug::TakeMenuRequest(taken, menu_text) && taken == serial && !std::strcmp(menu_text, "menu open enemies") &&
          !debug::TakeMenuRequest(taken, menu_text), "menu request taken once");
    const auto busy = debug::RequestMenu("menu close", before_overlay.mission, before_overlay.generation);
    Check(busy == serial + 1 && !std::strcmp(Snap().overlay_result, "menu_busy") && Snap().overlay_result_serial == busy,
          "single menu slot refuses a second request until the first resolves");
    debug::ReportMenuResult(serial, "menu_open;category=enemies;frames=4");
    const auto stale = debug::RequestMenu("menu close", before_overlay.mission, before_overlay.generation + 1);
    Check(!std::strcmp(Snap().overlay_result, "menu_stale") && Snap().overlay_result_serial == stale, "stale menu request refused");
    const auto malformed = debug::RequestMenu("menu press nothing", before_overlay.mission, before_overlay.generation);
    Check(!std::strcmp(Snap().overlay_result, "menu_args") && Snap().overlay_result_serial == malformed &&
          !debug::TakeMenuRequest(taken, menu_text), "malformed menu request refused without a slot");
    const auto ok = debug::RequestMenu("menu close", before_overlay.mission, before_overlay.generation);
    Check(debug::TakeMenuRequest(taken, menu_text) && taken == ok, "slot free after the result");
    debug::ReportMenuResult(ok, "menu_open;category=enemies;frames=4");
    debug::SetOverlayStatus(overlay); debug::SetLastUiAction("spawn_enemy_here");
    debug::Publish(over);
    rs = Snap();
    Check(rs.overlay_open && rs.overlay_frames == 4 && rs.overlay_result_serial == ok && !std::strcmp(rs.overlay_result, "menu_open;category=enemies;frames=4") && std::strcmp(rs.overlay_category, "enemies") == 0 &&
          std::strcmp(rs.last_ui_action, "spawn_enemy_here") == 0, "overlay status survives telemetry");
    Check(std::strcmp(rs.last_command, before_overlay.last_command) == 0 && std::strcmp(rs.last_result, before_overlay.last_result) == 0 &&
          rs.receipts[0].id == before_overlay.receipts[0].id, "overlay status never touches last_command/last_result/receipts");
    debug::BeginMission(30020);
    rs = Snap();
    Check(!rs.god_mode && rs.player_life == -1 && std::strcmp(rs.pending_kind, "none") == 0, "mission change resets Lua-owned state");

    // Nearby soldier transaction.
    Check(debug::QueueCommand("enemy_list 120") && debug::Pop(command), "nearby list dispatched");
    const auto list_id = command.id;
    Check(!receipt(list_id, "nearby 30010 120"), "nearby bound to the current mission");
    Check(!receipt(list_id, "nearby 30020 9") && !receipt(list_id, "nearby 30020 301"), "nearby radius bounded");
    Check(!receipt(list_id, "soldier 1 5 1000 0 0 sol_a"), "soldier requires an open list");
    Check(receipt(list_id, "nearby 30020 120"), "nearby list opened");
    Check(!receipt(list_id, "soldier 2 5 1000 0 0 sol_a"), "soldier indices ordered from 1");
    Check(receipt(list_id, "soldier 1 4294967295 1500 5 7 sol_a"), "dead marked placed soldier accepted");
    for (const auto* bad : {"soldier 2 6 1000 1 0 sol_b", "soldier 2 6 1000 6 0 sol_b", "soldier 2 6 1000 9 0 sol_b",
         "soldier 2 6 -1 0 0 sol_b", "soldier 2 6 1000 0 8 sol_b", "soldier 2 6 1000 0 0 bad/name", "soldier 2 6 1000 0 0 sol_b extra"})
        Check(!receipt(list_id, bad), "soldier code/flags/name bounded to Lua wire values");
    char soldier[96]{};
    const unsigned codes[] = {0, 2, 3, 4, 8};
    for (unsigned i = 2; i <= debug::kNearbyCount; ++i) {
        std::snprintf(soldier, sizeof(soldier), "soldier %u %u %u %u 0 sol_%u", i, 100 + i, i * 1000, codes[i % 5], i);
        Check(receipt(list_id, soldier), "nearby list accepts 12 ordered soldiers");
    }
    Check(!receipt(list_id, "soldier 13 1 1 0 0 sol_x"), "nearby capacity never overflows");
    Check(receipt(list_id, "done") && !receipt(list_id, "done"), "nearby list closes once");
    rs = Snap();
    Check(rs.enemy_nearby_count == 12 && rs.enemy_nearby_radius == 120 && rs.enemy_nearby[0].hash == 4294967295u &&
          rs.enemy_nearby[0].state == 5 && rs.enemy_nearby[0].flags == 7 && std::strcmp(rs.enemy_nearby[0].name, "sol_a") == 0, "nearby copied");
    Check(debug::QueueCommand("position") && debug::Pop(command) && !receipt(command.id, "nearby 30020 80"), "nearby author must be enemy_list");
    Check(debug::QueueCommand("enemy_camps") && debug::Pop(command), "camps dispatched");
    Check(receipt(command.id, "clear 30020") && receipt(command.id, "camp mafr_a_cp 1000 2"), "camps listed");
    Check(!Snap().enemy_camps_loaded, "camps not loaded before done");
    Check(receipt(command.id, "done") && Snap().enemy_camps_loaded, "camps done sets enemy_camps_loaded");
    Check(Snap().enemy_nearby_count == 12, "camp refresh keeps the nearby table");
    debug::BeginMission(30020);
    Check(!Snap().enemy_camps_loaded && Snap().enemy_nearby_count == 0, "mission change clears enemy metadata");

    // Registry, search, availability and formatters.
    namespace act = mgscoop::debug_actions;
    std::size_t action_count = 0;
    const auto* actions = act::Actions(action_count);
    bool ids_unique = action_count > 60, fixed_valid = true;
    unsigned per_category[act::kCategoryCount]{};
    for (std::size_t i = 0; i < action_count; ++i) {
        for (std::size_t j = i + 1; j < action_count; ++j) if (std::strcmp(actions[i].id, actions[j].id) == 0) ids_unique = false;
        ++per_category[static_cast<unsigned>(actions[i].category)];
        if (actions[i].command && !(actions[i].flags & act::kParam))
            fixed_valid = fixed_valid && debug::ValidText(actions[i].command) && std::strlen(actions[i].command) <= 200;
        Check(act::FindAction(actions[i].id) == &actions[i], "every action is findable");
    }
    Check(ids_unique, "action ids unique"); Check(fixed_valid, "fixed commands pass the queue charset and size");
    for (unsigned c = 0; c < act::kCategoryCount; ++c) Check(per_category[c] > 0, "every category has actions");
    const char* expected_quick[] = {"player_position", "spawn_enemy_here", "enemy_kill_nearest", "enemy_sleep_nearest", "player_heal",
        "bookmark_save", "bookmark_return", "partner_goto", "world_time_noon", "enemy_clear", "cp_phase_alert", "mission_status"};
    for (const auto* id : expected_quick) Check(act::FindAction(id) != nullptr, "spec action id present");
    Check(std::strcmp(act::FindAction("enemy_kill_nearest")->command, "enemy kill nearest") == 0 &&
          std::strcmp(act::FindAction("enemy_unmark_all")->command, "enemy_unmark") == 0 &&
          std::strcmp(act::FindAction("mission_status")->command, "status") == 0 &&
          std::strcmp(act::FindAction("world_time_noon")->command, "time 43200") == 0, "registry command texts");
    const act::ActionDef* found[32]{};
    auto has = [&](std::size_t n, const char* id) { for (std::size_t i = 0; i < n && i < 32; ++i) if (!std::strcmp(found[i]->id, id)) return true; return false; };
    Check(std::strcmp(act::FindAction("buddy_horse_spawn")->command, "horse_spawn") == 0 &&
          std::strcmp(act::FindAction("buddy_horse_status")->command, "horse_status") == 0, "normal-release horse commands in F9 registry");
    auto n = act::SearchActions("horse", found, 32);
    Check(n == 2 && has(n, "buddy_horse_spawn") && has(n, "buddy_horse_status"), "horse search discovers summon and state");
    n = act::SearchActions("recall", found, 32);
    Check(n == 1 && has(n, "buddy_horse_spawn"), "missing-horse recall is discoverable through the existing stock action");
    n = act::SearchActions("spawn", found, 32);
    Check(n >= 3 && has(n, "spawn_enemy_here") && has(n, "spawn_enemy_closest") && has(n, "spawn_enemy_route"), "search spawn lists the three spawn rows");
    n = act::SearchActions("NOON", found, 32); Check(n == 1 && has(n, "world_time_noon"), "search is case-insensitive");
    n = act::SearchActions("kill near", found, 32); Check(n == 1 && has(n, "enemy_kill_nearest"), "every word must match");
    Check(act::SearchActions("", found, 32) == 0 && act::SearchActions("   ", found, 32) == 0 && act::SearchActions(nullptr, found, 32) == 0, "empty search matches nothing");
    Check(act::SearchActions("zzzz", found, 32) == 0, "unknown word matches nothing");
    debug::Snapshot avail{}; avail.enabled = avail.gameplay_ready = avail.host = true;
    const auto& teleport = *act::FindAction("player_teleport");
    const auto reason = [&](const char* id, bool selection = false) { const char* r = act::DisabledReason(*act::FindAction(id), avail, selection); return r ? r : "ok"; };
    Check(!std::strcmp(reason("player_position"), "ok") && !act::DisabledReason(teleport, avail, false), "gameplay host can act");
    avail.gameplay_ready = false;
    Check(!std::strcmp(reason("player_position"), "gameplay") && !std::strcmp(reason("tools_clear_log"), "ok"), "UI-only rows work outside gameplay");
    avail.gameplay_ready = true; avail.host = false;
    Check(!std::strcmp(reason("spawn_enemy_here"), "host_only") && !std::strcmp(reason("enemy_list"), "ok") &&
          !std::strcmp(reason("enemy_selected_info", true), "ok") && !std::strcmp(reason("enemy_refresh_camps"), "ok") &&
          !std::strcmp(reason("world_time_noon"), "host_only") && !std::strcmp(reason("partner_show"), "ok"), "guest host-only rule");
    avail.host = true;
    Check(!std::strcmp(reason("player_use_current"), "position") && !std::strcmp(reason("enemy_goto_spawn"), "no_spawn") &&
          !std::strcmp(reason("enemy_goto_camp"), "no_camp") && !std::strcmp(reason("spawn_enemy_route"), "no_routes") &&
          !std::strcmp(reason("partner_goto"), "partner_pose") && !std::strcmp(reason("bookmark_return"), "bookmark_empty") &&
          !std::strcmp(reason("partner_clip_play"), "clip_unavailable") && !std::strcmp(reason("enemy_selected_kill"), "selection"), "missing prerequisites named");
    avail.remote_valid = avail.world_paired = true; strcpy_s(avail.link_state, "connected"); avail.remote_age_ms = 1000;
    Check(!std::strcmp(reason("partner_goto"), "ok"), "partner pose ready");
    avail.remote_age_ms = 1001; Check(!std::strcmp(reason("partner_goto"), "partner_pose"), "stale partner pose refused");
    avail.remote_age_ms = 10; strcpy_s(avail.link_state, "lost"); Check(!std::strcmp(reason("partner_goto"), "partner_pose"), "link down refused");
    act::Category cat{};
    Check(act::ParseCategory("enemies", cat) && cat == act::Category::enemies && act::ParseCategory("coop", cat) && cat == act::Category::partner &&
          act::ParseCategory("diag", cat) && cat == act::Category::tools && act::ParseCategory("mission", cat) && cat == act::Category::tools &&
          !act::ParseCategory("Enemies", cat) && !act::ParseCategory("", cat), "category names and aliases");
    char text[220]{};
    Check(act::FormatTeleport(text, sizeof(text), -123.5f, 4, 100000, -180) && !std::strcmp(text, "teleport -123.500 4.000 100000.000 -180.000"), "teleport format");
    Check(!act::FormatTeleport(text, sizeof(text), 100001, 0, 0, 0) && !act::FormatTeleport(text, sizeof(text), 0, 0, 0, 181) &&
          !act::FormatTeleport(text, 8, 0, 0, 0, 0), "teleport clamps and buffer");
    Check(act::FormatStep(text, sizeof(text), 0, 3, -100) && !std::strcmp(text, "step 0.000 3.000 -100.000") && !act::FormatStep(text, sizeof(text), 100.5f, 0, 0), "step format");
    Check(act::FormatClock(text, sizeof(text), 45296) && !std::strcmp(text, "12:34:56") && !act::FormatClock(text, sizeof(text), 86400), "clock format");
    Check(act::FormatTimeSet(text, sizeof(text), 23, 59) && !std::strcmp(text, "time 86340") && !act::FormatTimeSet(text, sizeof(text), 24, 0), "time set format");
    Check(act::FormatSlowmo(text, sizeof(text), 0.25f, 3) && !std::strcmp(text, "slowmo 0.25 3.0") && !act::FormatSlowmo(text, sizeof(text), 0.01f, 3) &&
          !act::FormatSlowmo(text, sizeof(text), 0.5f, 31), "slowmo format");
    Check(act::FormatPartnerModes(text, sizeof(text), "xform", "off", "net", "qs", "map", "net", "ac") &&
          !std::strcmp(text, "partner stage=3 source=net apply=xform input=off anim=net aset=qs dm=map look=net reset=ac"), "partner modes with reset");
    Check(act::FormatPartnerModes(text, sizeof(text), "input", "net", "off", "q", "on", "copy", nullptr) && !std::strstr(text, "reset") &&
          !act::FormatPartnerModes(text, sizeof(text), "xform", "off", "net", "qs", "map", "net", "none"), "partner reset values n|a|c|ac only");
    Check(act::FormatEnemyOp(text, sizeof(text), "sleep", 4294967295u) && !std::strcmp(text, "enemy sleep 4294967295") &&
          !act::FormatEnemyOp(text, sizeof(text), "wake", 1), "enemy op format");
    Check(act::FormatClear(text, sizeof(text), 25) && !std::strcmp(text, "clear_enemies 25") && !act::FormatClear(text, sizeof(text), 0) &&
          !act::FormatClear(text, sizeof(text), 101), "clear format");
    act::OverlayRequest request{};
    const struct { const char* text; act::OverlayOp op; const char* arg; } overlay_ok[] = {
        {"menu open", act::OverlayOp::open, ""}, {"menu open enemies", act::OverlayOp::open, "enemies"}, {"menu close", act::OverlayOp::close, ""},
        {"menu toggle", act::OverlayOp::toggle, ""}, {"menu category coop", act::OverlayOp::category, "coop"},
        {"menu search", act::OverlayOp::search, ""}, {"menu search kill near", act::OverlayOp::search, "kill near"},
        {"menu press spawn_enemy_here", act::OverlayOp::press, "spawn_enemy_here"}, {"menu confirm", act::OverlayOp::confirm, ""},
        {"menu cancel", act::OverlayOp::cancel, ""}, {"menu footer 1", act::OverlayOp::footer, "1"}, {"menu select 4294967295", act::OverlayOp::select, "4294967295"}};
    for (const auto& item : overlay_ok)
        Check(act::ParseOverlayRequest(item.text, request) && request.op == item.op && !std::strcmp(request.arg, item.arg), "overlay request grammar");
    for (const auto* bad : {"menu", "menu ", "menu open nowhere", "menu open player extra", "menu close now", "menu category", "menu press",
         "menu press no_such_action", "menu footer 2", "menu select 4294967296", "menu select -1", "menu select 01", "menu bogus",
         "menu search ", "menu  open", "position", "menu search aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "menu search a;b"})
        Check(!act::ParseOverlayRequest(bad, request), "overlay request rejects");
    char long_search[80] = "menu search ";
    std::memset(long_search + 12, 'a', 48);
    Check(act::ParseOverlayRequest(long_search, request) && std::strlen(request.arg) == 48, "48-character search accepted");
    long_search[60] = 'a';
    Check(!act::ParseOverlayRequest(long_search, request), "49-character search refused");
    Check(act::IsMenuText("menu open") && !act::IsMenuText("menuopen") && !act::IsMenuText("position"), "menu text routing");

    debug::Configure(true, true, true); debug::BeginMission(30050);
    // Real producer/consumer interleaving checks the UI/runtime exchange.
    std::atomic<bool> done{false};
    std::atomic<unsigned> accepted{0};
    std::thread producer([&] {
        for (unsigned i = 0; i < 1000; ++i) {
            if (debug::QueueCommand("position")) accepted.fetch_add(1);
            std::this_thread::yield();
        }
        done.store(true);
    });
    unsigned received = 0;
    previous = 0;
    bool unique = true;
    while (!done.load() || Snap().queue_depth) {
        if (debug::Pop(command)) { unique = unique && command.id > previous; previous = command.id; ++received; }
        else std::this_thread::yield();
    }
    producer.join();
    Check(unique && received == accepted.load(), "concurrent accepted requests delivered once in order");
    debug::Configure(false, false, true);
    Check(!debug::Enabled() && Snap().queue_depth == 0, "disable clears all actions");
    NametagTests();
    {
        namespace ui = mgscoop::native_ui;
        const ui::Expected expected{0x700,0x6d0,0x8250,0x1000,0x20000};
        ui::Raw raw{};
        raw.p=0x50000; raw.ui=0x60020; raw.owner=raw.p; raw.cam=0x70000; raw.hud=0x80000;
        raw.ui_vtable=expected.ui_vtable; raw.primary_vtable=expected.primary_vtable;
        raw.cam_vtable=expected.cam_vtable; raw.hud_vtable=0x90000; raw.setter=0x12340;
        raw.count=2; raw.local=0; raw.ids[0]=0; raw.ids[1]=1; raw.category=1;
        Check(ui::Valid(raw, expected), "native UI census accepts exact live owned chain");
        auto bad=raw; bad.owner+=8; Check(!ui::Valid(bad,expected), "UI owner mismatch refused");
        bad=raw; bad.ui_vtable=0x390; Check(!ui::Valid(bad,expected), "UI base construction vtable refused");
        bad=raw; bad.local=1; Check(!ui::Valid(bad,expected), "partner UI does not own local HUD");
        bad=raw; bad.count=1; Check(!ui::Valid(bad,expected), "single-player chain refused");
        bad=raw; bad.setter=expected.code_end; Check(!ui::Valid(bad,expected), "non-code HUD setter refused");
        bad=raw; bad.category=3; Check(!ui::Valid(bad,expected), "unknown reticle category refused");
        raw.cam_flags=(1u<<29)|(1u<<28); raw.ray[0]=0x8800000000000001ull;
        Check(ui::RayHit(raw)==1,"native camera hit uses selected ray A with valid game-object type");
        raw.cam_flags=1u<<29; raw.ray[1]=0x8800000000000007ull;
        Check(ui::RayHit(raw)==7,"native camera alternate ray B selected");
        raw.ray[1]=0x8000000000000007ull;
        Check(ui::RayHit(raw)==0xffff,"non-game-object ray handle refused");
        raw.ray[1]=0x0800000000000007ull;
        Check(ui::RayHit(raw)==0xffff,"unsigned ray handle refused");
        raw.cam_flags=0;
        Check(ui::RayHit(raw)==0xffff,"camera ray valid flag required");
    }
    std::printf("debug menu: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
