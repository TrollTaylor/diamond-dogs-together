// Original partner name tag game-thread source (docs/partner-nametag.md).
// Reads only: the camera chain of native/nametag_contract.hpp, the partner
// body through partner_player's validated block chain and the local gun record
// (ReadGun k=0). Nothing is written to the game and no game function is called.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include "mgscoop/nametag_game.hpp"
#include "mgscoop/nametag.hpp"
#include "mgscoop/partner_net.hpp"
#include "nametag_contract.hpp"
#include "partner_player_contract.hpp"
#include "native_ui_contract.hpp"
#include "mgscoop/native_ui_core.hpp"
#include "mgscoop/partner_input_hook.hpp"
#include "mgscoop/teammate_targeting.hpp"

namespace mgscoop::nametag_game {
namespace {
namespace c = nametag_contract;
namespace nt = nametag;
namespace pp = partner_player;
// Lua runtime thread state (Install runs before that thread uses it).
std::uintptr_t image = 0;
std::size_t image_span = 0;
LogFn log_fn = nullptr;
bool ready = false, viewer_host = true;
std::uint32_t max_m = 300;
char local_name[nt::kNameBytes + 1]{};
nt::LocalSource local_src = nt::LocalSource::none;
// Names (reliable channel 3, session scoped: both ends resend after a new handshake).
bool name_sent = false, local_dirty = true, remote_valid = false;
std::uint32_t sent_session = 0, name_tx = 0, name_rx = 0, name_invalid = 0, rx_session = 0;
char remote_name[nt::kNameBytes + 1]{};
std::uint8_t drain_buffer[link::kMaxReliableMessage]{};
// Camera (nt::ApplyCameraHistory: torn reads reuse the last good camera twice).
nt::CameraHistory camera_history{};
// Per mission (nt::CensusDue: the first tick that read the viewport's vtable).
std::uint32_t mission = 0;
nt::CensusState census = nt::CensusState::none;
nt::Ease height{};
// Logging and the frame stream.
unsigned log_lines = 0;
bool logged_any = false, logged_visible = false;
nt::Reason logged_reason = nt::Reason::ok;
nt::Refusal logged_refusal = nt::Refusal::none;
std::uint64_t seq = 0;
nt::Frame last_frame{};
constexpr unsigned kLogCap = 256;
bool ui_ready = false;
unsigned ui_logs = 0;
volatile LONG hud_job_logs = 0;
std::uint64_t ui_next = 0;
native_ui::CensusStamp ui_last{};
std::uint32_t ui_last_shared_selected=UINT32_MAX,ui_last_shared_flags=UINT32_MAX;
std::int32_t ui_last_rgb[6]{INT32_MIN,INT32_MIN,INT32_MIN,INT32_MIN,INT32_MIN,INT32_MIN};

void Log(const char* line) noexcept { if (log_fn) log_fn(line); }
void LogFormat(const char* format, ...) noexcept {
    char line[640]{};
    va_list args; va_start(args, format);
    const HRESULT hr = StringCchVPrintfA(line, ARRAYSIZE(line), format, args);
    va_end(args);
    if (SUCCEEDED(hr) || hr == STRSAFE_E_INSUFFICIENT_BUFFER) Log(line);
}
bool Read(std::uintptr_t address, void* out, std::size_t size) noexcept {
    SIZE_T copied = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), out, size, &copied) && copied == size;
}
template<class T> bool ReadValue(std::uintptr_t address, T& out) noexcept { return Read(address, &out, sizeof(out)); }
bool Finite3(const float* v) noexcept { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
std::int32_t Mm(float v) noexcept { return std::isfinite(v) && std::fabs(v) < 2.0e6f ? static_cast<std::int32_t>(std::lround(v * 1000.0f)) : INT32_MIN; }
// A float as a logged int (-1 when not finite or out of int range: no undefined cast).
int LogInt(float v) noexcept { return std::isfinite(v) && std::fabs(v) < 2.0e9f ? static_cast<int>(v) : -1; }
std::uint64_t Rva(std::uintptr_t value) noexcept { return value >= image && value - image < image_span ? value - image : value; }

const char* VerifyContract(std::uintptr_t& mismatch) noexcept {
    mismatch = 0;
    unsigned char bytes[0x300]{};
    for (const auto& site : c::sites) {
        mismatch = site.rva;
        if (site.size > sizeof(bytes) || site.rva > image_span || site.size > image_span - site.rva ||
            !Read(image + site.rva, bytes, site.size) || std::memcmp(bytes, site.bytes, site.size) != 0) return "contract";
    }
    constexpr std::uintptr_t statics[] = {c::selector_slot_rva, c::viewport_vtable_rva, c::sub_viewport_vtable_rva, c::camera_vtable_rva};
    for (const std::uintptr_t rva : statics) {
        mismatch = rva;
        if (rva > image_span || image_span - rva < 8) return "image";
    }
    mismatch = 0;
    return nullptr;
}

void ResolveLocalName(const InstallConfig& config) noexcept {
    local_name[0] = 0; local_src = nt::LocalSource::none;
    const auto take = [&](const char* text, nt::LocalSource source) noexcept {
        if (!text || !text[0] || local_name[0]) return;
        nt::SanitizeName(text, std::strlen(text), local_name);
        if (local_name[0]) local_src = source;
    };
    const LabConfig* lab = config.lab;
    const bool lab_present = lab && lab->present;
    if (lab_present) take(lab->nametag_name, nt::LocalSource::lab);
    if (lab_present && lab->mode == LabMode::lab) take(lab->self_name, nt::LocalSource::self);
    if (config.coop) take(config.coop->name, nt::LocalSource::coop);
    char persona[128]{};
    if (!local_name[0] && config.steam_persona && config.steam_persona(persona, sizeof(persona))) take(persona, nt::LocalSource::steam);
}

void Names() noexcept {
    const auto info = partner_net::GetLinkInfo();
    if (info.state != link::LinkState::connected) return;
    if (!name_sent || sent_session != info.session || local_dirty) {
        std::uint8_t bytes[nt::kWireMax]{};
        const std::size_t size = nt::EncodeName(local_name, info.session, bytes);
        const auto result = partner_net::SendReliable(nt::kChannel, bytes, size, link::ReliableMode::latest);
        if (result == link::ReliableResult::queued || result == link::ReliableResult::replaced) {
            const bool first = !name_sent;
            name_sent = true; sent_session = info.session; local_dirty = false;
            if (name_tx != UINT32_MAX) ++name_tx;
            if (first || log_lines < kLogCap) {
                ++log_lines;
                LogFormat("nametag=name_tx; session=%u; bytes=%u; src=%s\r\n", info.session, unsigned(size), nt::LocalSourceName(local_src));
            }
        }
    }
    for (unsigned n = 0; n < 4; ++n) {
        std::size_t size = 0;
        if (!partner_net::DrainReliable(nt::kChannel, drain_buffer, sizeof(drain_buffer), size)) break;
        char name[nt::kNameBytes + 1]{};
        std::uint32_t session = 0;
        const auto error = nt::DecodeName(drain_buffer, size, name, session);
        if (error != nt::WireError::none) {
            if (name_invalid != UINT32_MAX) ++name_invalid;
            if (log_lines < kLogCap) { ++log_lines; LogFormat("nametag=name_rx_refused; reason=%s; bytes=%u\r\n", nt::WireErrorName(error), unsigned(size)); }
            continue;
        }
        if (name_rx != UINT32_MAX) ++name_rx;
        rx_session = session;
        const bool changed = !remote_valid || std::strcmp(name, remote_name) != 0;
        std::memcpy(remote_name, name, sizeof(remote_name));
        remote_valid = name[0] != 0;
        if (changed && log_lines < kLogCap) {
            ++log_lines;
            LogFormat("nametag=name_rx; sender_session=%u; chars=%u; empty=%u\r\n", session, unsigned(std::strlen(name)), name[0] ? 0u : 1u);
        }
    }
}

nt::Refusal ReadCamera(bool owner_thread, nt::CameraRaw& raw) noexcept {
    raw = {};
    if (!owner_thread) return nt::Refusal::thread;
    std::uintptr_t selector = 0, handle = 0, viewport = 0;
    if (!ReadValue(image + c::selector_slot_rva, selector) || !selector) return nt::Refusal::selector;
    if (!ReadValue(selector + c::selector_handle, handle) || !handle) return nt::Refusal::handle;
    if (!ReadValue(handle + c::handle_viewport, viewport) || !viewport) return nt::Refusal::viewport;
    unsigned char matrices[c::vp_matrices_size]{}, tail[c::vp_tail_size]{};
    if (!ReadValue(viewport, raw.vp_vtable) || !Read(viewport + c::vp_matrices, matrices, sizeof(matrices)) ||
        !Read(viewport + c::vp_tail, tail, sizeof(tail))) return nt::Refusal::viewport;
    std::memcpy(raw.proj, matrices + c::vp_proj, sizeof(raw.proj));
    std::memcpy(raw.view, matrices + c::vp_view, sizeof(raw.view));
    std::memcpy(raw.viewproj, matrices + c::vp_viewproj, sizeof(raw.viewproj));
    std::memcpy(&raw.cam, tail + c::vp_camera, sizeof(raw.cam));
    std::memcpy(raw.rect, tail + c::vp_rect, sizeof(raw.rect));
    std::memcpy(&raw.px_w, tail + c::vp_pixels, sizeof(raw.px_w));
    std::memcpy(&raw.px_h, tail + c::vp_pixels + 4, sizeof(raw.px_h));
    std::memcpy(&raw.pixel_aspect, tail + c::vp_pixel_aspect, sizeof(raw.pixel_aspect));
    if (raw.cam) {
        unsigned char block[c::cam_block_size]{};
        if (!ReadValue(raw.cam, raw.cam_vtable) || !Read(raw.cam + c::cam_block, block, sizeof(block))) return nt::Refusal::camera;
        std::memcpy(&raw.cam_flags, block + c::cam_flags, sizeof(raw.cam_flags));
        std::memcpy(&raw.focal, block + c::cam_focal, sizeof(raw.focal));
        std::memcpy(raw.shift, block + c::cam_shift, sizeof(raw.shift));
        std::memcpy(&raw.near_m, block + c::cam_near, sizeof(raw.near_m));
        std::memcpy(&raw.far_m, block + c::cam_far, sizeof(raw.far_m));
    }
    return nt::Refusal::none;
}

struct Body { bool ok, foot_from_t; nt::Vec3 foot, pos; float quat[4]; };
Body ReadBody(const pp::Addresses& addresses, const pp::State& state) noexcept {
    Body body{};
    if (!pp::Realized(state, 1) || state.parts[1] != 3) return body;
    pp::Block block{};
    if (pp::ResolveBlock(addresses, state, 1, block) || !block.t) return body;
    float raw[0x60 / 4]{};  // T+0x10 foot, T+0x20 position, T+0x60 rotation
    if (!Read(block.t + 0x10, raw, sizeof(raw))) return body;
    const float* foot = raw;
    const float* pos = raw + 4;
    const float* quat = raw + 20;
    if (!Finite3(pos) || std::fabs(pos[0]) > 1.0e6f || std::fabs(pos[1]) > 1.0e6f || std::fabs(pos[2]) > 1.0e6f) return body;
    body.pos = {pos[0], pos[1], pos[2]};
    const float dy = pos[1] - foot[1];
    body.foot_from_t = Finite3(foot) && std::hypot(pos[0] - foot[0], pos[2] - foot[2]) <= 0.05f && dy >= 0.2f && dy <= 1.2f;
    body.foot = body.foot_from_t ? nt::Vec3{foot[0], foot[1], foot[2]} : nt::Vec3{pos[0], pos[1] - 0.9f, pos[2]};
    for (unsigned i = 0; i < 4; ++i) body.quat[i] = std::isfinite(quat[i]) ? quat[i] : (i == 3 ? 1.0f : 0.0f);
    body.ok = true;
    return body;
}

void Census(nt::Refusal refusal, const nt::CameraRaw& raw, const nt::Camera& camera) noexcept {
    if (!nt::CensusDue(census, refusal)) return;
    LogFormat("nametag_census; mission=%u; refusal=%s; vp_vt=0x%llx; cam_vt=0x%llx; w=%u; h=%u; rect_permille=%d,%d,%d,%d; "
              "vfov_mdeg=%d; near_mm=%d; far_m=%d; vp_err_ppm=%d; flags=0x%x; thread=%lu\r\n",
        mission, nt::RefusalName(refusal), static_cast<unsigned long long>(Rva(raw.vp_vtable)),
        static_cast<unsigned long long>(Rva(raw.cam_vtable)), raw.px_w, raw.px_h,
        Mm(raw.rect[0]), Mm(raw.rect[1]), Mm(raw.rect[2]), Mm(raw.rect[3]), Mm(camera.vfov_deg), Mm(raw.near_m),
        LogInt(raw.far_m), LogInt(camera.vp_err * 1.0e6f), raw.cam_flags, GetCurrentThreadId());
}

// Read the same UI owner/camera used by the native FEA420 reticle update.
// No virtual call, mutation, inferred projection hit or color meaning.
bool SharedHud(std::uintptr_t ui,std::uintptr_t& shared,std::uint32_t& selected,std::uint32_t& flags) noexcept {
    namespace u=native_ui_contract;
    std::uintptr_t provider=0,vt=0,get=0,again=0;
    if(!ReadValue(ui+u::ui_provider,provider) || !ReadValue(provider,vt) || vt!=image+u::provider_vtable_rva ||
        !ReadValue(vt,get) || get!=image+u::provider_get_rva) return false;
    shared=provider+u::provider_data;
    return ReadValue(shared+u::shared_selected,selected) && ReadValue(shared+u::shared_flags,flags) &&
        ReadValue(ui+u::ui_provider,again) && again==provider;
}
struct ReticleRgb {
    bool valid{};
    std::uint32_t state{};
    std::uintptr_t node[2]{},vtable[2]{};
    std::int32_t rgb[6]{INT32_MIN,INT32_MIN,INT32_MIN,INT32_MIN,INT32_MIN,INT32_MIN};
};
ReticleRgb ReadReticleRgb() noexcept {
    namespace u=native_ui_contract;
    ReticleRgb out{};
    std::uintptr_t common=0,vt=0,again=0;
    if(!ReadValue(image+u::common_global_rva,common) || !ReadValue(common,vt) || vt!=image+u::common_vtable_rva ||
        !ReadValue(common+u::reticle+u::reticle_state,out.state)) return out;
    for(unsigned n=0;n<2;++n) {
        float color[4]{};
        if(!ReadValue(common+u::reticle+u::reticle_nodes[n],out.node[n]) || !out.node[n] ||
            !ReadValue(out.node[n],out.vtable[n]) || !Read(out.node[n]+u::node_color,color,sizeof(color)) || !Finite3(color)) return out;
        for(unsigned k=0;k<3;++k) out.rgb[n*3+k]=Mm(color[k]);
        if(!ReadValue(common+u::reticle+u::reticle_nodes[n],again) || again!=out.node[n]) return out;
    }
    if(!ReadValue(image+u::common_global_rva,again) || again!=common) return out;
    out.valid=true;return out;
}
void NativeUiCensus(const pp::State& state, std::uint64_t now) noexcept {
    if (!ui_ready || ui_logs >= 160 || now < ui_next) return;
    ui_next = now + 200;
    namespace u = native_ui_contract;
    namespace pc = partner_player_contract;
    native_ui::Raw r{};
    r.p = state.P; r.count = state.count; r.local = state.local;
    r.ids[0] = state.ids[0]; r.ids[1] = state.ids[1];
    if (!ReadValue(r.p + u::p_ui, r.ui) || r.ui < 0x20 ||
        !ReadValue(r.ui, r.ui_vtable) || !ReadValue(r.ui - 0x20, r.primary_vtable) ||
        r.ui_vtable != image + u::ui_vtable_rva || r.primary_vtable != image + u::ui_primary_vtable_rva ||
        !ReadValue(r.ui + u::ui_owner, r.owner) || r.owner != r.p ||
        !ReadValue(r.ui + u::ui_hit, r.hit) || !ReadValue(r.ui + u::ui_category, r.category) ||
        !ReadValue(r.ui + u::ui_hud, r.hud) || !ReadValue(r.hud, r.hud_vtable) ||
        !ReadValue(r.hud_vtable + u::hud_set_category, r.setter) ||
        !ReadValue(r.p + pc::p_cam, r.cam) || !ReadValue(r.cam, r.cam_vtable) ||
        !ReadValue(r.cam + u::cam_flags, r.cam_flags) ||
        !ReadValue(r.cam + u::cam_hit_a, r.ray[0]) || !ReadValue(r.cam + u::cam_hit_b, r.ray[1])) return;
    if (!native_ui::Valid(r, {image + u::ui_vtable_rva, image + u::ui_primary_vtable_rva,
            image + pc::cam_vtable_rva, image + pc::code_begin_rva, image + pc::code_end_rva})) return;
    std::uintptr_t provider=0, provider_vt=0, provider_get=0;
    // FE71B0, final secondary UI vt+190, naturally obtains this provider.
    ReadValue(r.ui + 0x48, provider); ReadValue(provider, provider_vt); ReadValue(provider_vt, provider_get);
    // Complete camera update 111D4B0 in teammate_targeting_contract proves
    // these read-only fields precede the natural assist-acquisition caller.
    std::uint32_t assist_flags=0;float assist_gain=0;
    const bool assist_read=ReadValue(r.cam+0x204,assist_flags) && ReadValue(r.cam+0x2c8,assist_gain);
    const auto gain_milli=Mm(assist_gain);
    const bool assist_gate=assist_read && std::isfinite(assist_gain) && assist_gain>0 &&
        (assist_flags&0x60000)==0x60000 && !(assist_flags&0x400000);
    const auto ray_hit = native_ui::RayHit(r);
    native_ui::CensusStamp stamp{r.hit,ray_hit,r.category,r.cam_flags,assist_flags,gain_milli};
    const bool hint_read=ReadValue(r.ui+0x370,stamp.hint) && ReadValue(r.ui+0x374,stamp.hint_flags) &&
        ReadValue(r.ui+0x3a0,stamp.hint_active) && ReadValue(r.ui+0x3a3,stamp.hint_priority);
    std::uintptr_t shared=0;
    std::uint32_t shared_selected=UINT32_MAX,shared_flags=UINT32_MAX;
    const bool shared_read=SharedHud(r.ui,shared,shared_selected,shared_flags);
    const auto color=ReadReticleRgb();
    if(ui_logs && native_ui::SameCensus(stamp,ui_last) && shared_selected==ui_last_shared_selected &&
        shared_flags==ui_last_shared_flags && std::memcmp(color.rgb,ui_last_rgb,sizeof(ui_last_rgb))==0) return;
    ui_last=stamp;ui_last_shared_selected=shared_selected;ui_last_shared_flags=shared_flags;
    std::memcpy(ui_last_rgb,color.rgb,sizeof(ui_last_rgb));
    ++ui_logs;
    LogFormat("native_ui_census;mission=%u;hit=%u;ray_hit=%u;category=%u;partner_id=%u;local_id=%u;ui_vt=0x%llx;hud_vt=0x%llx;category_setter=0x%llx;provider_vt=0x%llx;provider_get=0x%llx;cam_flags=0x%x;assist_read=%u;assist_flags=0x%x;assist_gain_milli=%d;assist_gate=%u;scope=read_only_actual_ray;visual_mapping=unverified\r\n",
        mission, r.hit, ray_hit, r.category, r.ids[1], r.ids[0], static_cast<unsigned long long>(Rva(r.ui_vtable)),
        static_cast<unsigned long long>(Rva(r.hud_vtable)), static_cast<unsigned long long>(Rva(r.setter)), static_cast<unsigned long long>(Rva(provider_vt)),
        static_cast<unsigned long long>(Rva(provider_get)), r.cam_flags,assist_read?1u:0u,assist_flags,gain_milli,assist_gate?1u:0u);
    LogFormat("native_reticle_rgb_census;mission=%u;hit=%u;category=%u;color_read=%u;state=0x%x;node_vt=0x%llx,0x%llx;rgb_milli=%d,%d,%d;%d,%d,%d;shared_read=%u;selected=%u;shared_flags=0x%x;hint_read=%u;hint=%u;hint_flags=0x%x;hint_active=%u;hint_priority=%u;scope=original_nodes_read_only\r\n",
        mission,r.hit,r.category,color.valid?1u:0u,color.state,static_cast<unsigned long long>(Rva(color.vtable[0])),
        static_cast<unsigned long long>(Rva(color.vtable[1])),color.rgb[0],color.rgb[1],color.rgb[2],color.rgb[3],color.rgb[4],color.rgb[5],
        shared_read?1u:0u,shared_selected,shared_flags,hint_read?1u:0u,stamp.hint,stamp.hint_flags,unsigned(stamp.hint_active),unsigned(stamp.hint_priority));
}

void PublishFrame(const nt::Frame& frame) noexcept {
    last_frame = frame;
    nt::Publish(frame);
}
} // namespace

void Install(const InstallConfig& config) noexcept {
    const DWORD error = GetLastError();
    image = reinterpret_cast<std::uintptr_t>(config.base);
    image_span = config.span;
    log_fn = config.log;
    viewer_host = config.host;
    ready = false; ui_ready = false;
    nt::Settings settings{};
    const LabConfig* lab = config.lab;
    const CoopConfig* coop = config.coop;
    const bool lab_present = lab && lab->present;
    if (lab_present && lab->nametag != NametagSetting::unset) settings.on = lab->nametag == NametagSetting::on;
    else if (coop && coop->nametag != NametagSetting::unset) settings.on = coop->nametag == NametagSetting::on;
    const NametagCrosshair crosshair = lab_present && lab->nametag_crosshair != NametagCrosshair::unset ? lab->nametag_crosshair
        : coop ? coop->friendly_crosshair : NametagCrosshair::unset;
    settings.crosshair = crosshair == NametagCrosshair::always ? nt::Crosshair::always
        : crosshair == NametagCrosshair::off ? nt::Crosshair::off : nt::Crosshair::aim;
    settings.max_m = lab_present ? lab->nametag_max_m : 300;
    max_m = settings.max_m;
    if (!config.session_ok || !image || !image_span) {
        nt::Configure(false, config.host, settings);
        SetLastError(error);
        return;
    }
    std::uintptr_t mismatch = 0;
    if (const char* reason = VerifyContract(mismatch)) {
        nt::Configure(false, config.host, settings);
        LogFormat("nametag=refused; reason=%s; rva=0x%llx; scope=camera_contract\r\n", reason, static_cast<unsigned long long>(mismatch));
        SetLastError(error);
        return;
    }
    ui_ready = true;
    for (const auto& site : native_ui_contract::sites) {
        unsigned char bytes[0xa00]{};
        if (site.size > sizeof(bytes) || site.rva > image_span || site.size > image_span - site.rva ||
            !Read(image + site.rva, bytes, site.size) || std::memcmp(bytes, site.bytes, site.size) != 0) {
            ui_ready = false;
            LogFormat("native_ui_census=refused;reason=contract;rva=0x%llx\r\n", static_cast<unsigned long long>(site.rva));
            break;
        }
    }
    partner_input_hook::RegisterHudProbe(&BeginPartnerHud, &EndPartnerHud);
    ResolveLocalName(config);
    ready = true;
    nt::Configure(true, config.host, settings);
    LogFormat("nametag=ready; on=%u; crosshair=%s; max_m=%u; local_src=%s; local_chars=%u; contract_sites=%u; reads=camera_body_gun_only\r\n",
        settings.on ? 1u : 0u, nt::CrosshairName(settings.crosshair), settings.max_m, nt::LocalSourceName(local_src),
        unsigned(std::strlen(local_name)), unsigned(sizeof(c::sites) / sizeof(c::sites[0])));
    SetLastError(error);
}

bool Ready() noexcept { return ready && nt::Enabled(); }

static HudJobProbe ReadPartnerHud(std::uintptr_t p) noexcept {
    HudJobProbe out{};
    if (!ui_ready || !p) return out;
    namespace u = native_ui_contract;
    namespace pc = partner_player_contract;
    std::uintptr_t vt=0, owner=0, q=0, actions=0, ids=0;
    std::uint32_t count=0, local=0;
    std::uint16_t remote=0xffff;
    out.p = p;
    if (!ReadValue(p,vt) || vt != image + pc::p_vtable_rva ||
        !ReadValue(p + pc::p_count,count) || count != 2 ||
        !ReadValue(p + pc::p_local,local) || local != 0 ||
        !ReadValue(p + u::p_ui,out.ui) || out.ui < 0x20 ||
        !ReadValue(out.ui,vt) || vt != image + u::ui_vtable_rva ||
        !ReadValue(out.ui + u::ui_owner,owner) || owner != p ||
        !ReadValue(out.ui + 0x740,out.flags) || !ReadValue(out.ui + 0x744,out.action) ||
        !ReadValue(out.ui+0x370,out.hint) || !ReadValue(out.ui+0x374,out.hint_flags) ||
        !ReadValue(out.ui+0x3a0,out.hint_active) || !ReadValue(out.ui+0x3a3,out.hint_priority) ||
        !ReadValue(p + pc::p_ids,ids) || !ReadValue(ids + 2,remote) || remote != 1 ||
        !ReadValue(p + pc::p_q,q) || !ReadValue(q + 0x20,actions) ||
        !ReadValue(actions + remote,out.remote_action)) return {};
    out.shared_valid=SharedHud(out.ui,out.shared,out.shared_selected,out.shared_flags);
    out.valid = true;
    return out;
}
HudJobProbe BeginPartnerHud(std::uintptr_t p) noexcept {
    teammate_targeting::EndQuietJob();
    const auto out=ReadPartnerHud(p);
    if(out.valid) teammate_targeting::BeginQuietJob(p,out.ui,partner_input_hook::HookThread());
    return out;
}
void EndPartnerHud(const HudJobProbe& before) noexcept {
    // Always disarm, including a refused before-probe or exhausted log budget.
    teammate_targeting::EndQuietJob();
    if (!before.valid || hud_job_logs>=96) return;
    const auto after = ReadPartnerHud(before.p);
    if (!after.valid || after.ui != before.ui) return;
    if(before.flags==after.flags && before.action==after.action && before.remote_action==after.remote_action &&
        before.shared_valid==after.shared_valid && before.shared_selected==after.shared_selected && before.shared_flags==after.shared_flags &&
        before.hint==after.hint && before.hint_flags==after.hint_flags &&
        before.hint_active==after.hint_active && before.hint_priority==after.hint_priority && hud_job_logs>=4) return;
    if (InterlockedIncrement(&hud_job_logs) > 96) return;
    LogFormat("partner_hud_job_census;mission=%u;remote_action=0x%x;ui_flags_before=0x%x;ui_flags_after=0x%x;action_before=%u;action_after=%u;shared_before=%u;shared_after=%u;selected_before=%u;selected_after=%u;shared_flags_before=0x%x;shared_flags_after=0x%x;scope=read_only_remote_job;quiet_mapping=unverified\r\n",
        mission,after.remote_action,before.flags,after.flags,before.action,after.action,before.shared_valid?1u:0u,after.shared_valid?1u:0u,
        before.shared_selected,after.shared_selected,before.shared_flags,after.shared_flags);
    LogFormat("partner_hint_job_census;mission=%u;remote_action=0x%x;hint_before=%u;hint_after=%u;flags_before=0x%x;flags_after=0x%x;active_before=%u;active_after=%u;priority_before=%u;priority_after=%u;scope=read_only_remote_job;quiet_hint78_mapping=candidate\r\n",
        mission,after.remote_action,before.hint,after.hint,before.hint_flags,after.hint_flags,unsigned(before.hint_active),unsigned(after.hint_active),unsigned(before.hint_priority),unsigned(after.hint_priority));
}

void ResetNativeUiCensus() noexcept {
    ui_logs=0;InterlockedExchange(&hud_job_logs,0);ui_next=0;ui_last={};
    ui_last_shared_selected=UINT32_MAX;ui_last_shared_flags=UINT32_MAX;
    for(auto& v:ui_last_rgb) v=INT32_MIN;
}

void MissionStart(std::uint32_t value) noexcept {
    mission = value; census = nt::CensusState::none; height = {};
    ResetNativeUiCensus();
    camera_history.last_ok = false; camera_history.torn_streak = 0;
    logged_any = false;
}

void MissionEnd() noexcept {
    mission = 0;
    camera_history.last_ok = false; camera_history.torn_streak = 0;
    if (!ready) return;
    nt::Frame hidden = last_frame;
    hidden.visible = hidden.crosshair = hidden.hover = false;
    hidden.seq = ++seq; hidden.tick_ms = GetTickCount64();
    PublishFrame(hidden);
}

void Tick(const TickInput& in) noexcept {
    if (!Ready() || !in.addresses) return;
    const DWORD error = GetLastError();
    const std::uint64_t now = GetTickCount64();
    const nt::Settings settings = nt::GetSettings();
    Names();

    nt::Telemetry t{};
    t.on = settings.on; t.crosshair = settings.crosshair; t.hud = in.hud;
    t.local_src = local_src; std::memcpy(t.local_name, local_name, sizeof(t.local_name));
    t.name_src = remote_valid ? nt::NameSource::remote : nt::NameSource::fallback;
    StringCchCopyA(t.name, ARRAYSIZE(t.name), remote_valid ? remote_name : nt::FallbackName(viewer_host));
    t.name_tx = name_tx; t.name_rx = name_rx; t.name_invalid = name_invalid;
    t.link_session = partner_net::GetLinkInfo().session;

    // Nothing can be shown (off, mission end, a demo or the iDroid, the partner
    // body hidden): no camera, body or gun read this tick (the names still flow).
    nt::Reason gate = nt::Reason::ok;
    if (!settings.on) gate = nt::Reason::off;
    else if (in.terminating) gate = nt::Reason::terminating;
    else if (in.hud & (nt::kHudDemo | nt::kHudIdroid)) gate = nt::Reason::hud;
    else if (in.partner_hidden != nt::Reason::ok) gate = in.partner_hidden;
    const bool reads = gate == nt::Reason::ok;

    // Camera (refusals hide the tag; torn reads reuse the last good camera up to twice).
    nt::CameraRaw raw{};
    nt::Camera camera{};
    nt::Refusal refusal = nt::Refusal::none;
    if (reads) {
        refusal = ReadCamera(in.owner_thread, raw);
        if (refusal == nt::Refusal::none)
            refusal = nt::ValidateCamera(raw, {image + c::viewport_vtable_rva, image + c::sub_viewport_vtable_rva, image + c::camera_vtable_rva}, camera);
        Census(refusal, raw, camera);
    }
    const float vp_err = camera.vp_err;  // of this tick's read, also when a torn read reuses the last camera
    refusal = nt::ApplyCameraHistory(camera_history, reads, refusal, camera);
    auto& ct = t.camera;
    ct.ok = reads && refusal == nt::Refusal::none; ct.refusal = refusal;
    ct.vp_vt = raw.vp_vtable ? Rva(raw.vp_vtable) : 0; ct.cam_vt = raw.cam_vtable ? Rva(raw.cam_vtable) : 0;
    ct.px[0] = raw.px_w; ct.px[1] = raw.px_h;
    std::memcpy(ct.rect, raw.rect, sizeof(ct.rect));
    ct.vfov_deg = camera.vfov_deg; ct.near_m = raw.near_m; ct.far_m = raw.far_m; ct.vp_err = vp_err;
    ct.torn = camera_history.torn_total; ct.thread = GetCurrentThreadId();

    // Partner body, stance and the local aim.
    pp::State state{};
    const bool state_ok = reads && pp::ReadState(*in.addresses, state) == pp::Refusal::none;
    if (state_ok && in.owner_thread) NativeUiCensus(state, now);
    Body body{};
    if (state_ok) body = ReadBody(*in.addresses, state);
    std::uint32_t stance = 0;
    const auto sample = partner_net::SampleRemote(in.interp_ms);
    pp::Anim anim{};
    if (sample.valid && sample.status != pose::Status::none && sample.age_ms <= 1000 && sample.stance <= 2) stance = sample.stance;
    else if (state_ok && pp::ReadAnim(state, anim) && anim.stance[1] >= 1 && anim.stance[1] <= 3) stance = anim.stance[1] - 1u;
    pp::Gun1 gun{};
    const bool gun_ok = state_ok && pp::ReadGun(*in.addresses, state, 0, gun);
    t.aim = gun_ok ? gun.node_active && (gun.gun3c0 & partner_player_contract::gun_flag_hold) != 0 : in.tick_aim;
    t.aim_src = gun_ok ? nt::AimSource::gun : nt::AimSource::hold;
    t.stance = stance;

    nt::Frame frame{};
    frame.seq = ++seq; frame.tick_ms = now;
    std::memcpy(frame.name, t.name, sizeof(frame.name));
    nt::Projected projected{};
    float dist = 0.0f;
    if (body.ok) {
        t.body_ok = true;
        t.body[0] = body.pos.x; t.body[1] = body.pos.y; t.body[2] = body.pos.z;
        t.foot[0] = body.foot.x; t.foot[1] = body.foot.y; t.foot[2] = body.foot.z;
        const float h = nt::EaseToward(height, nt::HeadHeight(stance, in.seated), now, 150);
        const nt::Vec3 head{body.foot.x, body.foot.y + h + nt::kHeadLift, body.foot.z};
        t.head[0] = head.x; t.head[1] = head.y; t.head[2] = head.z;
        if (ct.ok) {
            projected = nt::Project(camera.viewproj, head);
            const nt::Vec3 v = nt::Transform(camera.view, head);
            dist = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            t.projected = projected.in_front; t.ndc[0] = projected.ndc[0]; t.ndc[1] = projected.ndc[1]; t.w = projected.w;
            t.dist_m = dist;
            const nt::Aim aim = nt::CentreRay(camera.view, camera.proj,
                                              nt::BodyCapsule(body.foot, nt::QuatYaw(body.quat), nt::CapsuleStance(stance, in.seated)));
            t.hover = aim.hit; t.miss_yaw_deg = aim.miss_yaw_deg; t.miss_pitch_deg = aim.miss_pitch_deg;
        }
    }
    nt::Reason reason = gate;
    if (reads) {
        if (!ct.ok) reason = nt::Reason::camera;
        else if (!body.ok) reason = nt::Reason::no_body;
        else if (!projected.in_front) reason = nt::Reason::behind;
        else if (std::fabs(projected.ndc[0]) > nt::kMaxNdc || std::fabs(projected.ndc[1]) > nt::kMaxNdc) reason = nt::Reason::offscreen;
        else if (dist > static_cast<float>(max_m)) reason = nt::Reason::too_far;
    }
    const bool shown_context = reads && ct.ok && body.ok;
    if (!shown_context) t.hover = false;
    t.crosshair_on = false;  // native reticle owns crosshair drawing
    t.visible = reason == nt::Reason::ok;
    t.reason = reason;
    t.seq = frame.seq; t.tick_ms = now;
    frame.visible = t.visible; frame.crosshair = t.crosshair_on; frame.hover = t.hover;
    frame.ndc[0] = projected.ndc[0]; frame.ndc[1] = projected.ndc[1];
    std::memcpy(frame.rect, ct.ok ? camera.rect : raw.rect, sizeof(frame.rect));
    frame.alpha = nt::FadeAlpha(dist); frame.dist_m = dist;
    PublishFrame(frame);
    nt::PublishTelemetry(t);

    if (!logged_any || logged_visible != t.visible || logged_reason != reason || logged_refusal != refusal) {
        logged_any = true; logged_visible = t.visible; logged_reason = reason; logged_refusal = refusal;
        if (log_lines < kLogCap) {
            ++log_lines;
            if (t.visible) LogFormat("nametag=visible; mission=%u; src=%s; d_mm=%d; ndc_permille=%d,%d; stance=%u; foot=%s; seated=%u\r\n", mission,
                t.name_src == nt::NameSource::remote ? "remote" : "fallback", Mm(dist), Mm(projected.ndc[0]), Mm(projected.ndc[1]),
                stance, body.foot_from_t ? "t10" : "centre", in.seated ? 1u : 0u);
            else LogFormat("nametag=hidden; mission=%u; reason=%s; refusal=%s\r\n", mission, nt::ReasonName(reason), nt::RefusalName(refusal));
        }
    }
    SetLastError(error);
}

bool HandleCommand(const char* text, char* result, std::size_t capacity, bool& ok) noexcept {
    ok = false;
    if (!nt::IsCommand(text)) return false;
    if (!result || !capacity) return true;
    nt::Command command{};
    if (!nt::ParseCommand(text, command)) { StringCchCopyA(result, capacity, "nametag_args"); return true; }
    if (!Ready()) { StringCchCopyA(result, capacity, "nametag_unavailable;reason=not_ready"); return true; }
    switch (command.op) {
    case nt::CommandOp::on: nt::SetOn(true); break;
    case nt::CommandOp::off: {
        nt::SetOn(false);
        nt::Frame hidden = last_frame;  // hide at once; the next tick publishes reason=off
        hidden.visible = hidden.crosshair = hidden.hover = false;
        hidden.seq = ++seq; hidden.tick_ms = GetTickCount64();
        PublishFrame(hidden);
        break;
    }
    case nt::CommandOp::crosshair: nt::SetCrosshair(command.crosshair); break;
    default: break;
    }
    const nt::Settings settings = nt::GetSettings();
    nt::Telemetry t = nt::GetTelemetry();
    t.on = settings.on; t.crosshair = settings.crosshair;
    if (!settings.on) { t.visible = false; t.reason = nt::Reason::off; }
    nt::FormatStatus(t, nt::GetRenderStats(), result, capacity);
    if (command.op != nt::CommandOp::status) {
        LogFormat("nametag=setting; on=%u; crosshair=%s\r\n", settings.on ? 1u : 0u, nt::CrosshairName(settings.crosshair));
    }
    ok = true;
    return true;
}
} // namespace mgscoop::nametag_game
