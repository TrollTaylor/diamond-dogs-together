// Original MGS Co-op debug placement: one engine set-transform message to an
// authored soldier. Contract: native/soldier_warp_contract.hpp
// (tools/generate_soldier_warp_contract.py, docs/debug-menu.md).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <charconv>
#include <cmath>
#include <cstring>
#include "mgscoop/soldier_warp.hpp"
#include "soldier_relay_contract.hpp"
#include "soldier_warp_contract.hpp"

namespace mgscoop::soldier_warp {
namespace {
namespace c = soldier_warp_contract;
namespace rc = soldier_relay_contract;

template <class T>
bool ReadValue(std::uintptr_t address, T& value) noexcept {
    SIZE_T read = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), &value, sizeof(value), &read) &&
           read == sizeof(value);
}
bool ReadBlock(std::uintptr_t address, void* out, std::size_t size) noexcept {
    SIZE_T read = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), out, size, &read) && read == size;
}
bool Inside(std::uintptr_t base, std::size_t span, std::uintptr_t value) noexcept { return value >= base && value - base < span; }
std::uintptr_t Rva(std::uintptr_t base, std::size_t span, std::uintptr_t value) noexcept { return Inside(base, span, value) ? value - base : 0; }

struct Preserve {
    DWORD error = GetLastError();
    ~Preserve() { SetLastError(error); }
};

// The two argument-preserving forwarder shapes to inner->vt[0xa8]
// (0xc15500 is the disp8 form): mov rcx,[rcx+d]; mov rax,[rcx]; jmp [rax+0xa8].
bool Forwarder(const unsigned char (&b)[24], std::uint32_t& displacement) noexcept {
    static constexpr unsigned char tail[] = {0x48, 0x8b, 0x01, 0x48, 0xff, 0xa0, 0xa8, 0x00, 0x00, 0x00};
    if (b[0] == 0x48 && b[1] == 0x8b && b[2] == 0x49 && std::memcmp(b + 4, tail, sizeof(tail)) == 0) {
        displacement = b[3];
        return true;
    }
    if (b[0] == 0x48 && b[1] == 0x8b && b[2] == 0x89 && std::memcmp(b + 7, tail, sizeof(tail)) == 0) {
        std::uint32_t d = 0;
        std::memcpy(&d, b + 3, sizeof(d));
        if (d >= 0x1000) return false;
        displacement = d;
        return true;
    }
    return false;
}

using SendFn = std::int32_t* (*)(void*, std::int32_t*, std::uint32_t, void*);
// SEH wrapper: POD arguments only.
bool CallSend(std::uintptr_t send, std::uintptr_t manager, std::int32_t* out, std::uint32_t id, void* message) noexcept {
    __try {
        reinterpret_cast<SendFn>(send)(reinterpret_cast<void*>(manager), out, id, message);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Resolves the dispatch chain read-only, exactly as 0xbf4200/0xbf99e0/0xbf4270
// would (the count call of 0xbf4270 is left to the engine).
const char* ResolveChain(std::uintptr_t base, std::size_t span, std::uint32_t id, Chain& chain) noexcept {
    std::uintptr_t registry = 0, registry_vt = 0, entry = 0, inner = 0, type = 0, x = 0;
    if (!ReadValue(base + c::registry_slot_rva, registry) || !registry || !ReadValue(registry, registry_vt) ||
        registry_vt != base + c::registry_vtable_rva) return "registry";
    const std::uint32_t kind = id >> 9;
    if (kind >= c::registry_type_count) return "type";
    if (!ReadValue(registry + c::registry_types + 8 * static_cast<std::uintptr_t>(kind), entry) || !entry ||
        !ReadValue(entry + c::type_entry_inner, inner) || !inner || !ReadValue(inner + c::type_inner_object, type) || !type)
        return "chain";
    chain.type_object = type;
    if (!ReadValue(type, chain.type_vt) || !ReadValue(chain.type_vt + 0x40, chain.count_fn) || !Inside(base, span, chain.count_fn) ||
        !ReadValue(type + c::type_object_inner, x) || !x || !ReadValue(x + c::inner_handle_object, chain.handle_object) ||
        !chain.handle_object || !ReadValue(chain.handle_object, chain.handle_vt) ||
        !ReadValue(chain.handle_vt + c::handle_send_slot, chain.hop) || !Inside(base, span, chain.hop))
        return "chain";
    chain.hop_read = ReadBlock(chain.hop, chain.hop_bytes, sizeof(chain.hop_bytes));
    if (!chain.hop_read) return "chain";
    // Enemy latch: the live typed dispatch (soldier_relay_contract): the soldier handle vtable's hop 0xbf5450, and the
    // object it calls (handle+0x20) is a soldier type whose +0x70 is the dispatcher 0x9e9ab0.
    std::uintptr_t typed_object = 0, typed_vt = 0, dispatch = 0;
    chain.typed = chain.handle_vt == base + rc::handle_vtable_rva && chain.hop == base + rc::hop_rva &&
                  ReadValue(chain.handle_object + rc::handle_type_object, typed_object) && typed_object &&
                  ReadValue(typed_object, typed_vt) && typed_vt == base + rc::type_vtable_rva &&
                  ReadValue(typed_vt + rc::type_dispatch_slot, dispatch) && dispatch == base + rc::dispatcher_rva;
    std::uint32_t displacement = 0;
    if (!Forwarder(chain.hop_bytes, displacement)) return nullptr;  // unknown hop shape: engine dispatch, unpinned
    // A recognized forwarder is evidence of where the message goes: it must reach the
    // verified handler on a soldier subobject, or nothing is sent.
    std::uintptr_t leaf = 0, leaf_vt = 0, handler = 0;
    if (!ReadValue(chain.handle_object + displacement, leaf) || !leaf || !ReadValue(leaf, leaf_vt)) return "leaf";
    chain.leaf = leaf; chain.leaf_vt = leaf_vt;
    if ((leaf_vt != base + c::soldier_vtable_a_rva && leaf_vt != base + c::soldier_vtable_b_rva) ||
        !ReadValue(leaf_vt + c::soldier_handler_slot, handler) || handler != base + c::handler_rva) return "leaf";
    chain.pinned = true;
    return nullptr;
}

// The handler's own first reads (0x136102a, 0x13615f5, 0x13616bf), read-only.
bool ReadLeafState(const Chain& chain, std::uint32_t index, Chain& out) noexcept {
    std::uintptr_t realize = 0, bits = 0, records = 0, data = 0;
    std::uint32_t record_count = 0, stance = 0;
    unsigned char byte = 0;
    if (!ReadValue(chain.leaf + c::leaf_count, out.count) || !ReadValue(chain.leaf + c::leaf_realize, realize) || !realize ||
        !ReadValue(realize + c::realize_bits, bits) || !bits || !ReadValue(chain.leaf + c::leaf_records, records) || !records ||
        !ReadValue(records + c::records_count, record_count) || !ReadValue(records + c::records_data, data) || !data)
        return false;
    if (index < out.count && !ReadValue(bits + (index >> 3), byte)) return false;
    const std::uintptr_t record = data + (index < record_count ? static_cast<std::uintptr_t>(index) * c::record_stride : 0);
    if (!ReadValue(record + c::record_stance, stance)) return false;
    out.realized = (byte >> (index & 7)) & 1u;
    out.stance = stance & 0xfu;
    out.leaf_state = true;
    return true;
}
} // namespace

bool ParseReceipt(const char* text, std::size_t size, std::uint32_t& command, Request& request) noexcept {
    if (!text || !size || size > 128) return false;
    std::int64_t field[6]{};
    const char* cursor = text;
    const char* const end = text + size;
    for (unsigned i = 0; i < 6; ++i) {
        if (i && (cursor >= end || *cursor++ != ' ')) return false;
        if (cursor >= end || !((*cursor >= '0' && *cursor <= '9') || *cursor == '-')) return false;
        const auto parsed = std::from_chars(cursor, end, field[i]);
        if (parsed.ec != std::errc{} || parsed.ptr == cursor) return false;
        cursor = parsed.ptr;
    }
    if (cursor != end || field[0] <= 0 || field[0] > 0xffffffffll || field[1] < 0 || field[1] > 0xffff ||
        field[5] < -8000 || field[5] > 8000) return false;
    for (unsigned i = 2; i < 5; ++i) if (field[i] < -100000000 || field[i] > 100000000) return false;
    command = static_cast<std::uint32_t>(field[0]);
    request.go_id = static_cast<std::uint32_t>(field[1]);
    for (unsigned i = 0; i < 3; ++i) request.pos[i] = static_cast<float>(field[2 + i]) * 0.001f;
    request.yaw = static_cast<float>(field[5]) * 0.001f;
    return true;
}

const char* Verify(std::uintptr_t base, std::size_t span, std::uintptr_t& mismatch) noexcept {
    const Preserve preserve;
    mismatch = 0;
    if (!base || !span) return "image";
    unsigned char bytes[512]{};
    const auto site_ok = [&](std::uintptr_t rva, std::size_t size, const unsigned char* expected) noexcept {
        mismatch = rva;
        return size <= sizeof(bytes) && rva <= span && size <= span - rva && ReadBlock(base + rva, bytes, size) &&
               std::memcmp(bytes, expected, size) == 0;
    };
    const auto slot_ok = [&](std::uintptr_t rva, std::uintptr_t target) noexcept {
        mismatch = rva;
        std::uintptr_t value = 0;
        return rva <= span && span - rva >= sizeof(value) && ReadValue(base + rva, value) && value == base + target;
    };
    for (const auto& site : c::sites) if (!site_ok(site.rva, site.size, site.bytes)) return "body_contract";
    for (const auto& site : rc::sites) if (!site_ok(site.rva, site.size, site.bytes)) return "body_contract";
    for (const auto& slot : c::slots) if (!slot_ok(slot.rva, slot.target)) return "slot_contract";
    for (const auto& slot : rc::slots) if (!slot_ok(slot.rva, slot.target)) return "slot_contract";
    constexpr std::uintptr_t statics[] = {c::manager_object_rva, c::manager_guard_rva, c::registry_slot_rva};
    for (const std::uintptr_t rva : statics) {
        mismatch = rva;
        if (rva > span || span - rva < 8) return "image";
    }
    mismatch = 0;
    return nullptr;
}

Result Warp(std::uintptr_t base, std::size_t span, const Request& request) noexcept {
    const Preserve preserve;
    Result result{};
    if (!base || span <= c::registry_slot_rva + 8) { result.refusal = "contract"; return result; }
    if ((request.go_id >> 9) != c::soldier_type || request.go_id > 0xffffu) { result.refusal = "type"; return result; }
    float squared = 0;
    for (unsigned i = 0; i < 3; ++i) {
        if (!std::isfinite(request.pos[i]) || !std::isfinite(request.player[i]) || std::fabs(request.pos[i]) > 100000.0f ||
            std::fabs(request.player[i]) > 100000.0f) { result.refusal = "input"; return result; }
        const float delta = request.pos[i] - request.player[i];
        squared += delta * delta;
    }
    if (!std::isfinite(request.yaw) || std::fabs(request.yaw) > 8.0f || !(request.max_range > 0.0f) ||
        !(request.max_range <= kMaxRangeLimitM)) { result.refusal = "input"; return result; }
    if (!(squared <= request.max_range * request.max_range)) { result.refusal = "range"; return result; }
    // The static manager (0xbf3920 returns base+0x2c38008 once bit 0 of the guard is set).
    std::uint32_t guard = 0;
    std::uintptr_t manager_vt = 0, send = 0;
    const std::uintptr_t manager = base + c::manager_object_rva;
    if (!ReadValue(base + c::manager_guard_rva, guard) || !(guard & 1u) || !ReadValue(manager, manager_vt) ||
        manager_vt != base + c::manager_vtable_rva || !ReadValue(manager_vt + c::manager_send_slot, send) || send != base + c::send_rva) {
        result.refusal = "manager"; return result;
    }
    if (const char* reason = ResolveChain(base, span, request.go_id, result.chain)) { result.refusal = reason; return result; }
    const std::uint32_t index = request.go_id & 0x1ffu;
    if (result.chain.pinned) {
        // Pinned to 0x1360f90: mirror its own guards without sending anything.
        if (!ReadLeafState(result.chain, index, result.chain)) { result.refusal = "chain"; return result; }
        if (index >= result.chain.count) { result.refusal = "index"; return result; }
        if (!result.chain.realized) { result.refusal = "unrealized"; result.out = -1; return result; }
        // Stance 1-2 sends a second action message, 3-4 rotates the pose (0x13616e0..0x13616f4).
        if (result.chain.stance >= 1 && result.chain.stance <= 4) { result.refusal = "stance"; return result; }
    }
    alignas(16) unsigned char message[c::message_size]{};
    const std::uint64_t hash = c::message_hash;
    std::memcpy(message, &hash, sizeof(hash));
    const float pos[4] = {request.pos[0], request.pos[1], request.pos[2], 0.0f};
    const double half = static_cast<double>(request.yaw) * 0.5;
    const float quat[4] = {0.0f, static_cast<float>(std::sin(half)), 0.0f, static_cast<float>(std::cos(half))};
    std::memcpy(message + c::message_pos, pos, sizeof(pos));
    std::memcpy(message + c::message_quat, quat, sizeof(quat));
    // +0x30 u16 local player index (used only by stance 1-2), +0x32 flag 0, +0x33 handled (out).
    alignas(16) std::int32_t out[4] = {-2, 0, 0, 0};
    result.sent = true;
    if (!CallSend(send, manager, out, request.go_id, message)) { result.refusal = "fault"; return result; }
    result.out = out[0];
    result.handled = message[c::message_handled];
    // The handler (0x13619e5/0x13619ea, 0x1364e5b) and SendCommand (0xbf3aa6) give only these two answers.
    const bool moved = result.out == 0 && result.handled == 1, waiting = result.out == -1 && result.handled == 0;
    // Enemy latch: on the typed dispatch, out=0 handled=0 is the dispatcher's relay after the handler refused (-1:
    // unrealized here, or no character) a guard whose slot exists on this copy: 0xbf6570 -> 0xbf6490 writes *out = 0
    // and offers the message to the guard's unrealized form (realize bit clear, 0x9e9bc9) or, realize bit set, to
    // leaf->vt[0xf0](idx)->vt[0x60] (0x9e9b9e); no component handled it. out=0x8ce64801 is the handle's mode 2. Either
    // way nothing moved and the path is explained; whether the guard was realized is not known here [U].
    const bool relayed = result.chain.typed && result.handled == 0 &&
                         (result.out == 0 || static_cast<std::uint32_t>(result.out) == rc::mode2_answer);
    if (relayed) result.refusal = "relayed";
    else if (!moved && !waiting) result.refusal = "anomaly";
    return result;
}

unsigned Gate::SoldierAnomalies(std::uint32_t go) const noexcept {
    for (unsigned i = 0; i < soldier_count && i < kTrackedSoldiers; ++i)
        if (soldiers[i].go == go) return soldiers[i].anomalies;
    return 0;
}

std::uint64_t Gate::BackoffLeft(std::uint32_t go, std::uint64_t now_ms) const noexcept {
    const auto g = static_cast<std::uint16_t>(go);
    for (const auto& r : relays)
        if (r.streak && r.go == g) return r.until > now_ms ? r.until - now_ms : 0;
    return 0;
}

unsigned Gate::RelayStreak(std::uint32_t go) const noexcept {
    const auto g = static_cast<std::uint16_t>(go);
    for (const auto& r : relays)
        if (r.streak && r.go == g) return r.streak;
    return 0;
}

const char* Gate::Admit(std::uint32_t command_id, std::uint32_t go, std::uint64_t now_ms) noexcept {
    now = now_ms;
    if (latched) return "latched";
    if (!command_id) return "target";
    if (SoldierAnomalies(go) >= kSoldierAnomalies) return "soldier";
    if (BackoffLeft(go, now_ms)) return "backoff";
    if (command_id != command) { command = command_id; go_id = go; attempts = 0; landed = false; }
    else if (go != go_id) return "target";
    if (landed) return "landed";
    if (attempts >= kMaxAttempts) return "budget";
    ++attempts;
    return nullptr;
}

const char* GuestAdmit(const GuestRule& rule, GuestGrant& grant) noexcept {
    grant = GuestGrant::none;
    if (!rule.lab && !rule.play) return "role";
    if (rule.lab && rule.key) { grant = GuestGrant::lab; return nullptr; }  // lab key: spawn sync, any kind
    if (rule.follow && rule.correct && FollowMission(rule.mission)) { grant = GuestGrant::follow; return nullptr; }
    if (rule.mission == kStaffMission && rule.correct) { grant = GuestGrant::staff; return nullptr; }
    return "disabled";
}

const char* Gate::AdmitDrive(std::uint32_t go, std::uint64_t now_ms) noexcept {
    now = now_ms;
    if (latched) return "latched";
    if (SoldierAnomalies(go) >= kSoldierAnomalies) return "soldier";
    if (BackoffLeft(go, now_ms)) return "backoff";
    command = 0;
    go_id = go;
    attempts = 0;
    landed = false;
    return nullptr;
}

bool DriveTarget(const DriveSample& prev, const DriveSample& cur, std::uint64_t t, float (&out)[3], float& yaw) noexcept {
    if (!cur.valid || t > cur.rx_ms + kDriveStaleMs + kDriveDelayMs) return false;
    yaw = cur.yaw;
    if (prev.valid && prev.rx_ms < cur.rx_ms) {
        const float span = static_cast<float>(cur.rx_ms - prev.rx_ms);
        float u = 0.0f;
        if (t <= prev.rx_ms) u = 0.0f;
        else if (t <= cur.rx_ms) u = static_cast<float>(t - prev.rx_ms) / span;
        else u = 1.0f + static_cast<float>((t - cur.rx_ms) > kDriveExtrapolateMs ? kDriveExtrapolateMs : (t - cur.rx_ms)) / span;
        for (unsigned i = 0; i < 3; ++i) out[i] = prev.pos[i] + (cur.pos[i] - prev.pos[i]) * u;
        return true;
    }
    for (unsigned i = 0; i < 3; ++i) out[i] = cur.pos[i];
    return true;
}

DriveStepResult DriveStep(const float (&from)[3], const float (&to)[3], bool active, float start_m, float stop_m, float max_step,
                          float snap_m, float lift_m, float feet_m, float vert_m) noexcept {
    DriveStepResult r{};
    const float dx = to[0] - from[0], dz = to[2] - from[2];
    r.error = std::sqrt(dx * dx + dz * dz);
    for (unsigned i = 0; i < 3; ++i) r.pos[i] = from[i];
    if (!std::isfinite(r.error)) return r;
    r.active = active ? r.error >= stop_m : r.error > start_m;
    if (!r.active) {
        // drive_vert: at rest in x/z but a storey off the target (a ledge, a wall top, a roof): the target's spot.
        if (vert_m > 0.0f && std::isfinite(to[1] - from[1]) && std::fabs(to[1] - from[1]) > vert_m) {
            r.move = r.vert = true;
            for (unsigned i = 0; i < 3; ++i) r.pos[i] = to[i];
        }
        return r;
    }
    r.move = true;
    if (r.error > snap_m) {
        r.hard = true;
        for (unsigned i = 0; i < 3; ++i) r.pos[i] = to[i];
        return r;
    }
    const float k = r.error > max_step ? max_step / r.error : 1.0f;
    r.pos[0] = from[0] + dx * k;
    r.pos[2] = from[2] + dz * k;
    const float feet = from[1] - feet_m;
    r.pos[1] = std::fabs(to[1] - feet) > 1.0f ? to[1] : feet + lift_m;
    if (k >= 1.0f) r.active = false;  // arrived: at rest until it drifts past start_m again
    return r;
}

bool DriveVertDue(float gap_m, std::uint64_t now_ms, std::uint64_t& since_ms) noexcept {
    if (!(std::fabs(gap_m) > kDriveVertM)) {
        since_ms = 0;
        return false;
    }
    if (!since_ms) {
        since_ms = now_ms ? now_ms : 1;
        return false;
    }
    return now_ms >= since_ms && now_ms - since_ms >= kDriveVertMs;
}

bool DriveVertAllow(std::uint64_t now_ms, std::uint64_t& window_ms, unsigned& count) noexcept {
    if (!window_ms || now_ms < window_ms || now_ms - window_ms >= kDriveVertWindowMs) {
        window_ms = 0;
        count = 0;
        return true;
    }
    return count < kDriveVertMax;
}

void DriveVertCount(std::uint64_t now_ms, std::uint64_t& window_ms, unsigned& count) noexcept {
    if (!window_ms) window_ms = now_ms ? now_ms : 1;
    ++count;
}

bool OwnerYAtRest(float y, std::uint64_t now_ms, OwnerRestY& state) noexcept {
    if (!std::isfinite(y)) {
        state = {};
        return false;
    }
    if (!state.valid || now_ms < state.last_ms || now_ms - state.last_ms > kDriveVertRestGapMs) {
        state = {now_ms, now_ms, y, y, true};
        return false;
    }
    const float lo = y < state.lo ? y : state.lo, hi = y > state.hi ? y : state.hi;
    state.last_ms = now_ms;
    if (!(hi - lo < kDriveVertRestM)) {
        state.since_ms = now_ms;
        state.lo = state.hi = y;
        return false;
    }
    state.lo = lo;
    state.hi = hi;
    return now_ms >= state.since_ms && now_ms - state.since_ms >= kDriveVertRestMs;
}

bool FormatRefusal(char* out, std::size_t capacity, const char* why, std::uint64_t back_ms) noexcept {
    if (!out || !capacity || !why) return false;
    const HRESULT hr = back_ms ? StringCchPrintfA(out, capacity, "return {refused=\"%s\",b=%llu}", why, static_cast<unsigned long long>(back_ms))
                               : StringCchPrintfA(out, capacity, "return {refused=\"%s\"}", why);
    return SUCCEEDED(hr);
}

const char* DriveAdmit(const DriveRule& rule) noexcept {
    if (!(rule.lab || rule.play)) return "role";
    if (!rule.snap || (rule.host && !rule.owner)) return "disabled";
    if (rule.puppet && (!rule.puppet_key || rule.host)) return "puppet";
    if (rule.mirror && (!rule.act_key || rule.host)) return "mirror";
    if (!FollowMission(rule.mission)) return "mission";
    return nullptr;
}

const char* HostAdmit(const HostRule& rule) noexcept {
    if (!rule.host || !(rule.lab || rule.play)) return "role";
    if (!rule.owner) return "disabled";
    if (!FollowMission(rule.mission)) return "mission";
    if (!rule.correct) return "kind";
    return nullptr;
}

float NearerSquared(const float (&pos)[3], const float (&host)[3], const float (&self)[3], bool host_valid, bool self_valid,
                    bool& self_nearer) noexcept {
    float to_host = 0.0f, to_self = 0.0f;
    for (unsigned i = 0; i < 3; ++i) {
        to_host += (pos[i] - host[i]) * (pos[i] - host[i]);
        to_self += (pos[i] - self[i]) * (pos[i] - self[i]);
    }
    host_valid = host_valid && std::isfinite(to_host);
    self_valid = self_valid && std::isfinite(to_self);
    self_nearer = self_valid && (!host_valid || to_self < to_host);
    if (self_nearer) return to_self;
    return host_valid ? to_host : INFINITY;
}

float NearerSquaredFlat(const float (&pos)[3], const float (&host)[3], const float (&self)[3], bool host_valid, bool self_valid,
                        bool& self_nearer) noexcept {
    const float p[3] = {pos[0], 0.0f, pos[2]}, h[3] = {host[0], 0.0f, host[2]}, s[3] = {self[0], 0.0f, self[2]};
    return NearerSquared(p, h, s, host_valid, self_valid, self_nearer);
}

void FlattenRange(Request& request) noexcept { request.player[1] = request.pos[1]; }

void RangePlayer(bool partner_nearer, const float (&own)[3], const float (&partner)[3], float (&out)[3]) noexcept {
    for (unsigned i = 0; i < 3; ++i) out[i] = partner_nearer ? partner[i] : own[i];
}

RemoteSource RemoteRangeSource(bool have_cmd, bool range_hidden, bool net_valid, unsigned net_age_ms, bool net_allowed) noexcept {
    if (have_cmd) return RemoteSource::cmd;
    if (net_allowed && range_hidden && net_valid && net_age_ms <= kRangePoseMaxAgeMs) return RemoteSource::net;
    return RemoteSource::none;
}

const char* RemoteSourceName(RemoteSource source) noexcept {
    switch (source) {
    case RemoteSource::cmd: return "cmd";
    case RemoteSource::net: return "net";
    case RemoteSource::none: return "none";
    }
    return "?";
}

bool GuestMayWarp(bool lab, bool play, bool) noexcept { return lab || play; }

const char* Gate::Record(const Result& result) noexcept {
    const auto go = static_cast<std::uint16_t>(go_id);
    if (!result.refusal && result.out == 0 && result.handled == 1) {
        landed = true;
        for (auto& r : relays)
            if (r.streak && r.go == go) r = Relay{};  // moved: that guard's relay streak ends
    }
    if (latched || !result.refusal) return nullptr;
    if (std::strcmp(result.refusal, "fault") == 0) {
        latched = result.refusal;
        return latched;
    }
    if (std::strcmp(result.refusal, "relayed") == 0) {
        // Never a latch: only this guard waits (its row, else a free row, else the row whose backoff ends first).
        ++relayed;
        Relay* row = nullptr;
        for (auto& r : relays)
            if (r.streak && r.go == go) { row = &r; break; }
        if (!row) {
            for (auto& r : relays) {
                if (!r.streak) { row = &r; break; }
                if (!row || r.until < row->until) row = &r;
            }
            *row = Relay{go, 0, 0};
        }
        if (row->streak < 255) ++row->streak;
        std::uint64_t back = kRelayBackoffMs;
        for (unsigned i = 1; i < row->streak && back < kRelayBackoffMaxMs; ++i) back *= 2;
        row->until = now + (back < kRelayBackoffMaxMs ? back : kRelayBackoffMaxMs);
        return result.refusal;
    }
    if (std::strcmp(result.refusal, "anomaly") != 0) return nullptr;
    // An anomaly counts against the gate's current guard and against the process.
    ++anomalies;
    unsigned count = 0;
    for (unsigned i = 0; i < soldier_count && i < kTrackedSoldiers; ++i)
        if (soldiers[i].go == go) { count = ++soldiers[i].anomalies; break; }
    if (!count) {
        if (soldier_count < kTrackedSoldiers) soldiers[soldier_count++] = Soldier{go, 1};
        count = 1;
    }
    if (anomalies >= kProcessAnomalies || soldier_count >= kTrackedSoldiers) {
        latched = result.refusal;
        return latched;
    }
    return count == kSoldierAnomalies ? "soldier" : nullptr;
}

const char* LeafName(const Chain& chain) noexcept {
    return chain.pinned ? "pinned" : chain.typed ? "typed" : "engine";
}

std::size_t FormatCensus(std::uintptr_t base, std::size_t span, std::uint32_t go_id, const Result& result, char* out,
                         std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    const Preserve preserve;
    const Chain& k = result.chain;
    // RVAs inside the image; anything outside it prints as 0.
    char hop[2 * sizeof(k.hop_bytes) + 1]{};
    if (k.hop_read)
        for (std::size_t i = 0; i < sizeof(k.hop_bytes); ++i) StringCchPrintfA(hop + 2 * i, sizeof(hop) - 2 * i, "%02x", k.hop_bytes[i]);
    if (FAILED(StringCchPrintfA(out, capacity,
            "soldier_warp_census; go=%u; type_vt=0x%llx; count_fn=0x%llx; handle_vt=0x%llx; hop=0x%llx; hop_bytes=%s; "
            "leaf_vt=0x%llx; leaf=%s; count=%u; realized=%u; stance=%u; out=%d; handled=%u; refusal=%s\r\n",
            go_id, static_cast<unsigned long long>(Rva(base, span, k.type_vt)), static_cast<unsigned long long>(Rva(base, span, k.count_fn)),
            static_cast<unsigned long long>(Rva(base, span, k.handle_vt)), static_cast<unsigned long long>(Rva(base, span, k.hop)),
            k.hop_read ? hop : "unread", static_cast<unsigned long long>(Rva(base, span, k.leaf_vt)),
            k.pinned ? "pinned" : k.typed ? "typed" : "engine",
            k.count, k.realized, k.stance, result.out, unsigned(result.handled), result.refusal ? result.refusal : "none"))) {
        out[0] = 0;
        return 0;
    }
    return std::strlen(out);
}
} // namespace mgscoop::soldier_warp
