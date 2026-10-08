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
#include "soldier_warp_contract.hpp"

namespace mgscoop::soldier_warp {
namespace {
namespace c = soldier_warp_contract;

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
    for (const auto& site : c::sites) {
        mismatch = site.rva;
        if (site.size > sizeof(bytes) || site.rva > span || site.size > span - site.rva ||
            !ReadBlock(base + site.rva, bytes, site.size) || std::memcmp(bytes, site.bytes, site.size) != 0) return "body_contract";
    }
    for (const auto& slot : c::slots) {
        mismatch = slot.rva;
        std::uintptr_t value = 0;
        if (slot.rva > span || span - slot.rva < sizeof(value) || !ReadValue(base + slot.rva, value) || value != base + slot.target)
            return "slot_contract";
    }
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
    if (!std::isfinite(request.yaw) || std::fabs(request.yaw) > 8.0f) { result.refusal = "input"; return result; }
    if (!(squared <= kMaxRangeM * kMaxRangeM)) { result.refusal = "range"; return result; }
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
    if (!moved && !waiting) result.refusal = "anomaly";
    return result;
}

const char* Gate::Admit(std::uint32_t command_id, std::uint32_t go) noexcept {
    if (latched) return "latched";
    if (!command_id) return "target";
    if (command_id != command) { command = command_id; go_id = go; attempts = 0; landed = false; }
    else if (go != go_id) return "target";
    if (landed) return "landed";
    if (attempts >= kMaxAttempts) return "budget";
    ++attempts;
    return nullptr;
}

const char* Gate::Record(const Result& result) noexcept {
    if (!result.refusal && result.out == 0 && result.handled == 1) landed = true;
    if (latched || !result.refusal || (std::strcmp(result.refusal, "fault") != 0 && std::strcmp(result.refusal, "anomaly") != 0))
        return nullptr;
    latched = result.refusal;
    return latched;
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
            k.hop_read ? hop : "unread", static_cast<unsigned long long>(Rva(base, span, k.leaf_vt)), k.pinned ? "pinned" : "engine",
            k.count, k.realized, k.stance, result.out, unsigned(result.handled), result.refusal ? result.refusal : "none"))) {
        out[0] = 0;
        return 0;
    }
    return std::strlen(out);
}
} // namespace mgscoop::soldier_warp
