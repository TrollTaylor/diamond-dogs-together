// Original MGS Co-op WS3 vehicles (docs/vehicle-ws3.md): the vehicle unit and
// realized body census, and the guarded tilt write of the viewer's copy.
#include "mgscoop/partner_vehicle.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "partner_player_contract.hpp"

namespace mgscoop::partner_vehicle {
namespace {
namespace c = mgscoop::partner_player_contract;
static_assert(kServiceRootRva == c::service_root_rva, "service root");
static_assert(kServiceVtRva == c::vehicle_service_vt_rva && kLookupRva == c::vehicle_lookup_rva, "vehicle service");
static_assert(kObjectVtRva == c::vehicle_object_vt_rva && kCountFnRva == c::vehicle_count_fn_rva && kSetPosFnRva == c::vehicle_setpos_fn_rva,
              "vehicle object");
static_assert(kVFnRva == c::vehicle_v_fn_rva && kRbFnRva == c::vehicle_rb_fn_rva && kCatFnRva == c::vehicle_cat_fn_rva, "body chain");
static_assert(sizeof(kVVtRvas) == sizeof(c::vehicle_v_vts) && sizeof(kRbVtRvas) == sizeof(c::vehicle_rb_vts) &&
                  sizeof(kCatVtRvas) == sizeof(c::vehicle_cat_vts), "vtable sets");

bool Finite(float v) noexcept { return std::isfinite(v); }
std::uint32_t Rva(std::uintptr_t value, std::uintptr_t image) noexcept {
    return value >= image && value - image < 0x10000000u ? static_cast<std::uint32_t>(value - image) : 0u;
}
} // namespace

bool SameRead(const Read& a, const Read& b) noexcept {
    if (a.refusal != b.refusal || a.body_refusal != b.body_refusal) return false;
    const Unit& x = a.unit;
    const Unit& y = b.unit;
    if (x.id != y.id || x.service != y.service || x.object != y.object || x.records != y.records || x.rec != y.rec || x.type != y.type ||
        x.realized != y.realized || x.slot != y.slot || x.index78 != y.index78)
        return false;
    const Body& p = a.body;
    const Body& q = b.body;
    return p.v == q.v && p.v_vt == q.v_vt && p.rb == q.rb && p.rb_vt == q.rb_vt && p.cats == q.cats && p.cat == q.cat &&
           p.cat_vt == q.cat_vt && p.cat_fn == q.cat_fn && p.slots == q.slots && p.body == q.body;
}

std::int32_t Mrad(float radians) noexcept {
    if (!Finite(radians)) return INT32_MIN;
    const double v = std::floor(static_cast<double>(radians) * 1000.0 + 0.5);
    if (v > 2147483647.0 || v < -2147483647.0) return INT32_MIN;
    return static_cast<std::int32_t>(v);
}
std::int32_t Mm(float metres) noexcept { return Mrad(metres); }

const char* TiltRefusal(const TiltIn& in) noexcept {
    if (!in.enabled) return "off";
    if (!in.pins) return "pins";
    if (!in.runtime) return "runtime";
    if (!in.peer) return "peer";
    if (!in.first || !in.second) return "read";
    if (in.id == 0xFFFF || (in.id >> 9) != kVehicleType) return "type";
    if (in.id == in.local) return "local";
    if (in.first->refusal) return in.first->refusal;
    if (in.first->unit.id != in.id) return "read";
    if (in.first->unit.realized == kNotRealized) return "unrealized";
    if (in.first->body_refusal) return in.first->body_refusal;
    if (!SameRead(*in.first, *in.second)) return "changed";
    // Critic #16: the physics pose (0xBA9710 -> 0x55DDA0(body)) and the seat
    // anchor (0x55DDA0(rec)) must read the same object we write.
    if (!in.first->Same()) return "identity";
    if (!Finite(in.first->unit.a44) || !Finite(in.first->unit.a48)) return "nonfinite";
    if (in.a44_mrad < -kTiltMaxMrad || in.a44_mrad > kTiltMaxMrad || in.a48_mrad < -kTiltMaxMrad || in.a48_mrad > kTiltMaxMrad)
        return "angle";
    if ((in.first->unit.rec & 3u) != 0) return "alignment";
    return nullptr;
}

const char* TiltReadRefusal(const TiltReadIn& in, std::int32_t& a44_mrad, std::int32_t& a48_mrad) noexcept {
    a44_mrad = a48_mrad = 0;
    if (!in.enabled) return "off";
    if (!in.runtime) return "runtime";
    if (!in.peer) return "peer";
    if (!in.read) return "read";
    if (in.id == 0xFFFF || (in.id >> 9) != kVehicleType) return "type";
    if (in.id != in.local) return "not_local";
    if (in.read->refusal) return in.read->refusal;
    if (in.read->unit.id != in.id) return "read";
    const std::int32_t a = Mrad(in.read->unit.a44), b = Mrad(in.read->unit.a48);
    if (a == INT32_MIN || b == INT32_MIN) return "nonfinite";
    if (a < -kTiltMaxMrad || a > kTiltMaxMrad || b < -kTiltMaxMrad || b > kTiltMaxMrad) return "angle";
    a44_mrad = a;
    a48_mrad = b;
    return nullptr;
}

std::size_t FormatCensus(const char* who, const Read& read, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    const std::uintptr_t image = read.image;
    const Unit& u = read.unit;
    const Body& b = read.body;
    const int n = std::snprintf(
        out, capacity,
        "partner_ride_census; who=%s; id=%u; ref=%s; type=0x%02x; cat=%u; var=%u; realized=%u; slot=%u; idx78=%u; rec=0x%llx; "
        "pos_mm=%d,%d,%d; yaw_mrad=%d; a44_mrad=%d; a48_mrad=%d; body_ref=%s; v_vt=0x%x; rb_vt=0x%x; cat_vt=0x%x; cat_fn=0x%x; "
        "body=0x%llx; body_pos_mm=%d,%d,%d; body_yaw_mrad=%d; body_a44_mrad=%d; body_a48_mrad=%d; same=%u\r\n",
        who ? who : "-", unsigned(u.id), read.refusal ? read.refusal : "ok", unsigned(u.type), unsigned(Category(u.type)),
        unsigned(Variant(u.type)), unsigned(u.realized), unsigned(u.slot), unsigned(u.index78), static_cast<unsigned long long>(u.rec),
        Mm(u.pos[0]), Mm(u.pos[1]), Mm(u.pos[2]), Mrad(u.pos[3]), Mrad(u.a44), Mrad(u.a48),
        read.body_refusal ? read.body_refusal : "ok", Rva(b.v_vt, image), Rva(b.rb_vt, image), Rva(b.cat_vt, image), Rva(b.cat_fn, image),
        static_cast<unsigned long long>(b.body), Mm(b.pos[0]), Mm(b.pos[1]), Mm(b.pos[2]), Mrad(b.pos[3]), Mrad(b.a44), Mrad(b.a48),
        read.Same() ? 1u : 0u);
    if (n < 0 || static_cast<std::size_t>(n) >= capacity) {
        out[0] = 0;
        return 0;
    }
    return static_cast<std::size_t>(n);
}

#ifdef _WIN32
namespace {
bool LiveRead(std::uintptr_t address, void* out, std::size_t size) noexcept {
    SIZE_T read = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), out, size, &read) && read == size;
}
const c::Site* ContractSite(std::uintptr_t rva) noexcept {
    for (const auto& site : c::code_sites)
        if (site.rva == rva) return &site;
    return nullptr;
}
bool StoreFloat(std::uintptr_t address, float value) noexcept {
    __try {
        *reinterpret_cast<volatile float*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
} // namespace

void ReadLive(const void* image, std::uint16_t id, Read& out) noexcept {
    auto reader = [](std::uintptr_t address, void* buffer, std::size_t size) noexcept { return LiveRead(address, buffer, size); };
    ReadVehicle(reader, reinterpret_cast<std::uintptr_t>(image), id, out);
}

const char* VerifyImage(const void* image, std::size_t span) noexcept {
    const auto* base = static_cast<const unsigned char*>(image);
    if (!base) return "base";
    for (const std::uintptr_t rva : c::vehicle_blocks) {
        const c::Site* site = ContractSite(rva);
        if (!site) return "contract";
        if (site->rva > span || site->size > span - site->rva) return "span";
        if (std::memcmp(base + site->rva, site->bytes, site->size) != 0) return "block";
    }
    // The small-move thresholds 0xBA9710 compares against (1.0f squared metres, 0.1f radians).
    for (const auto& data : c::vehicle_data) {
        if (data.rva > span || 4 > span - data.rva) return "span";
        std::uint32_t value = 0;
        std::memcpy(&value, base + data.rva, sizeof(value));
        if (value != data.value) return "threshold";
    }
    return nullptr;
}

const char* WriteTilt(const Read& gated, std::int32_t a44_mrad, std::int32_t a48_mrad) noexcept {
    if (!gated.Same() || !gated.unit.rec || (gated.unit.rec & 3u)) return "identity";
    const float a = static_cast<float>(a44_mrad) * 0.001f, b = static_cast<float>(a48_mrad) * 0.001f;
    if (!StoreFloat(gated.unit.rec + kA44, a) || !StoreFloat(gated.unit.rec + kA48, b)) return "fault";
    return nullptr;
}
#endif

} // namespace mgscoop::partner_vehicle
