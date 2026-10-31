// Original MGS Co-op G4 truck test levers (docs/native-puppet.md "Truck test levers"): the
// lab-only door spot of one vehicle (see mgscoop/vehicle_door.hpp). Pure parts first; the live
// read and the three read-only native calls (Windows only) at the end.
#include "mgscoop/vehicle_door.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "mgscoop/partner_board_policy.hpp"
#include "mgscoop/partner_vehicle.hpp"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "partner_player_contract.hpp"

namespace mgscoop::vehicle_door {
namespace {
namespace c = mgscoop::partner_player_contract;
namespace pb = mgscoop::partner_board;
static_assert(kDoorOffsetM == c::door_offset_m && kDoorRadiusM == c::door_radius_m, "the stock door constants");
static_assert(pb::kUnitRiders == c::board_unit_riders && pb::kUnitIndex == c::board_unit_index, "record offsets");
static_assert(pb::kQAct == c::q_act && pb::kPUnits == c::board_p_units, "viewer offsets");

// A decimal 0..max without a sign or a leading zero (a lone 0 is fine); advances p.
bool Number(const char*& p, const char* end, unsigned max, unsigned& value) noexcept {
    if (p == end || *p < '0' || *p > '9') return false;
    if (*p == '0' && p + 1 != end && p[1] >= '0' && p[1] <= '9') return false;
    unsigned long long n = 0;
    while (p != end && *p >= '0' && *p <= '9') {
        n = n * 10u + static_cast<unsigned>(*p - '0');
        if (n > max) return false;
        ++p;
    }
    value = static_cast<unsigned>(n);
    return true;
}
bool Space(const char*& p, const char* end) noexcept {
    if (p == end || *p != ' ') return false;
    ++p;
    return true;
}
bool Finite3(const float* v) noexcept { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
long Thousandths(float v) noexcept {
    if (!std::isfinite(v)) return 0;
    const double r = std::floor(static_cast<double>(v) * 1000.0 + 0.5);
    return r > 2000000000.0 || r < -2000000000.0 ? 0 : static_cast<long>(r);
}
} // namespace

bool ParseRequest(const char* text, std::size_t size, Request& out) noexcept {
    out = Request{};
    if (!text || !size || size > 32) return false;
    const char* p = text;
    const char* end = text + size;
    Request r{};
    constexpr char partner[] = "partner";
    if (size >= sizeof(partner) - 1 && std::memcmp(p, partner, sizeof(partner) - 1) == 0) {
        r.target = Target::partner;
        p += sizeof(partner) - 1;
    } else {
        unsigned id = 0;
        if (!Number(p, end, 0xFFFEu, id) || (id >> 9) != kVehicleType) return false;
        r.id = static_cast<std::uint16_t>(id);
    }
    unsigned seat = 0, out_cm = 0;
    if (!Space(p, end) || !Number(p, end, 1u, seat) || !Space(p, end) || !Number(p, end, kOutMaxCm, out_cm) || p != end) return false;
    r.seat = static_cast<std::uint8_t>(seat);
    r.out_cm = static_cast<std::uint16_t>(out_cm);
    out = r;
    return true;
}

const char* GateRefusal(const GateIn& in) noexcept {
    if (!in.enabled) return "off";
    if (!in.lab) return "lab_only";
    if (!in.runtime) return "runtime";
    if (!in.pins) return "pins";
    if (in.uncertain) return "uncertain";
    return nullptr;
}

const char* PartnerTarget(bool ride_live, std::uint16_t vehicle, std::uint16_t& id) noexcept {
    id = kNone;
    if (!ride_live || vehicle == kNone || (vehicle >> 9) != kVehicleType) return "partner_ride";
    id = vehicle;
    return nullptr;
}

Spot DoorSpot(const Facts& f, unsigned seat, unsigned out_cm) noexcept {
    Spot s{};
    if (seat > 1) { s.refusal = "seat"; return s; }
    if (out_cm > kOutMaxCm) { s.refusal = "out"; return s; }
    const float* a = seat ? f.seat1 : f.seat0;  // the door's seat
    const float* b = seat ? f.seat0 : f.seat1;  // the other front seat
    if (!Finite3(a) || !Finite3(b) || !Finite3(f.rec)) { s.refusal = "nonfinite"; return s; }
    const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    const float flat = std::sqrt(d[0] * d[0] + d[2] * d[2]);
    if (!(len >= kMinAxisM) || !(flat >= kMinAxisM)) { s.refusal = "seat_axis"; return s; }
    for (int i = 0; i < 3; ++i) s.door[i] = a[i] + kDoorOffsetM * d[i] / len;
    s.axis[0] = d[0] / flat;
    s.axis[1] = d[2] / flat;
    const float out = static_cast<float>(out_cm) / 100.0f;
    s.stand[0] = s.door[0] + out * s.axis[0];
    s.stand[1] = f.rec[1] + kStandLiftM;
    s.stand[2] = s.door[2] + out * s.axis[1];
    s.face = std::atan2(a[0] - s.stand[0], a[2] - s.stand[2]);
    if (!Finite3(s.door) || !Finite3(s.stand) || !std::isfinite(s.face)) { s.refusal = "nonfinite"; return s; }
    s.refusal = nullptr;
    return s;
}

std::size_t FormatAnswer(const Answer& answer, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    namespace v = mgscoop::partner_vehicle;
    int n = 0;
    if (answer.refusal || !answer.facts || !answer.spot || answer.spot->refusal) {
        const char* why = answer.refusal ? answer.refusal : !answer.facts || !answer.spot ? "answer" : answer.spot->refusal;
        n = std::snprintf(out, capacity, "return {ok=false,reason='%s'}", why);
    } else {
        const Facts& f = *answer.facts;
        const Spot& s = *answer.spot;
        n = std::snprintf(out, capacity,
                          "return {ok=true,id=%u,peer=%u,pseat=%u,type=%u,seats=%d,rider0=%u,x=%d,y=%d,z=%d,face=%d,dx=%d,dy=%d,dz=%d,"
                          "ax=%ld,az=%ld,rx=%d,ry=%d,rz=%d}",
                          unsigned(f.id), answer.peer ? 1u : 0u, unsigned(answer.peer_seat), unsigned(f.type), f.seats, unsigned(f.rider0),
                          v::Mm(s.stand[0]), v::Mm(s.stand[1]), v::Mm(s.stand[2]), v::Mrad(s.face), v::Mm(s.door[0]), v::Mm(s.door[1]),
                          v::Mm(s.door[2]), Thousandths(s.axis[0]), Thousandths(s.axis[1]), v::Mm(f.rec[0]), v::Mm(f.rec[1]), v::Mm(f.rec[2]));
    }
    if (n < 0 || static_cast<std::size_t>(n) >= capacity) {
        out[0] = 0;
        return 0;
    }
    return static_cast<std::size_t>(n);
}

std::size_t FormatLog(const Request& request, const Answer& answer, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    namespace v = mgscoop::partner_vehicle;
    const char* target = request.target == Target::partner ? "partner" : "id";
    const unsigned id = answer.facts ? answer.facts->id : request.id;
    int n = 0;
    if (answer.refusal || !answer.facts || !answer.spot || answer.spot->refusal) {
        const char* why = answer.refusal ? answer.refusal : !answer.facts || !answer.spot ? "answer" : answer.spot->refusal;
        n = std::snprintf(out, capacity, "partner_vehicle_door=refused; target=%s; id=%u; seat=%u; reason=%s\r\n", target, id,
                          unsigned(request.seat), why);
    } else {
        const Facts& f = *answer.facts;
        const Spot& s = *answer.spot;
        n = std::snprintf(out, capacity,
                          "partner_vehicle_door=answer; target=%s; id=%u; seat=%u; out_cm=%u; type=%u; seats=%d; rider0=%u; pseat=%u; "
                          "rec_mm=%d,%d,%d; yaw_mrad=%d; seat0_mm=%d,%d,%d; seat1_mm=%d,%d,%d; door_mm=%d,%d,%d; stand_mm=%d,%d,%d; "
                          "face_mrad=%d\r\n",
                          target, id, unsigned(request.seat), unsigned(request.out_cm), unsigned(f.type), f.seats, unsigned(f.rider0),
                          unsigned(answer.peer_seat), v::Mm(f.rec[0]), v::Mm(f.rec[1]), v::Mm(f.rec[2]), v::Mrad(f.rec[3]), v::Mm(f.seat0[0]),
                          v::Mm(f.seat0[1]), v::Mm(f.seat0[2]), v::Mm(f.seat1[0]), v::Mm(f.seat1[1]), v::Mm(f.seat1[2]), v::Mm(s.door[0]),
                          v::Mm(s.door[1]), v::Mm(s.door[2]), v::Mm(s.stand[0]), v::Mm(s.stand[1]), v::Mm(s.stand[2]), v::Mrad(s.face));
    }
    if (n < 0 || static_cast<std::size_t>(n) >= capacity) {
        out[0] = 0;
        return 0;
    }
    return static_cast<std::size_t>(n);
}

bool SameDigest(const Digest& a, const Digest& b) noexcept {
    return std::memcmp(a.riders, b.riders, sizeof(a.riders)) == 0 && a.type6c == b.type6c && a.realized == b.realized &&
           a.slot == b.slot && a.index == b.index && a.viewer_act == b.viewer_act && a.viewer_unit == b.viewer_unit;
}

#ifdef _WIN32
namespace {
using TypeFn = std::uint8_t (*)(void* unit);
using CountFn = int (*)(void* unit);
using SeatMatrixFn = float* (*)(void* unit, float* out, std::uint8_t seat);

bool LiveRead(std::uintptr_t address, void* out, std::size_t size) noexcept {
    SIZE_T read = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), out, size, &read) && read == size;
}
const c::Site* ContractSite(std::uintptr_t rva) noexcept {
    for (const auto& site : c::code_sites)
        if (site.rva == rva) return &site;
    return nullptr;
}
// The record's identity bytes (riders, type, realized slot, index) and the viewer's own act
// (*(Q+0x18))[local] and ridden unit (*(P+0x120))[local]; the viewer part stays 0 when unreadable.
bool ReadDigest(std::uintptr_t rec, std::uintptr_t P, std::uintptr_t Q, std::uint32_t local, Digest& d) noexcept {
    d = Digest{};
    unsigned char raw[mgscoop::partner_vehicle::kRecStride]{};
    if (!LiveRead(rec, raw, sizeof(raw))) return false;
    std::memcpy(d.riders, raw + pb::kUnitRiders, sizeof(d.riders));
    namespace v = mgscoop::partner_vehicle;
    d.type6c = raw[v::kType];
    d.realized = raw[v::kRealized];
    d.slot = raw[v::kSlot];
    std::memcpy(&d.index, raw + pb::kUnitIndex, sizeof(d.index));
    if (P && Q && local <= 3) {
        std::uintptr_t acts = 0, units = 0;
        if (LiveRead(Q + pb::kQAct, &acts, sizeof(acts)) && acts) LiveRead(acts + 2u * local, &d.viewer_act, sizeof(d.viewer_act));
        if (LiveRead(P + pb::kPUnits, &units, sizeof(units)) && units) LiveRead(units + 8u * local, &d.viewer_unit, sizeof(d.viewer_unit));
    }
    return true;
}
int CallFilter(DWORD code) noexcept {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}
// GetVehicleType 0x55E7F0, the seat count 0xA27CC0 and the seat world matrix 0xB7AF90 for seats 0
// and 1 (16-byte aligned outputs: the matrix is stored with movaps). Read-only game functions; no C++
// object with a destructor lives in this frame (SEH).
bool CallNative(std::uintptr_t image, std::uintptr_t rec, std::uint8_t& type, int& count, float* m0, float* m1) noexcept {
    const auto type_fn = reinterpret_cast<TypeFn>(image + c::board_type_rva);
    const auto count_fn = reinterpret_cast<CountFn>(image + c::board_count_rva);
    const auto seat_fn = reinterpret_cast<SeatMatrixFn>(image + c::door_seat_matrix_rva);
    void* unit = reinterpret_cast<void*>(rec);
    __try {
        type = type_fn(unit);
        count = count_fn(unit);
        seat_fn(unit, m0, 0);
        seat_fn(unit, m1, 1);
        return true;
    } __except (CallFilter(GetExceptionCode())) {
        return false;
    }
}
} // namespace

const char* VerifyImage(const void* image, std::size_t span) noexcept {
    const auto* base = static_cast<const unsigned char*>(image);
    if (!base) return "base";
    for (const std::uintptr_t rva : c::door_blocks) {
        const c::Site* site = ContractSite(rva);
        if (!site) return "contract";
        if (site->rva > span || site->size > span - site->rva) return "span";
        // M6 seat 1 (partner_board): the seat matrix's seat-1 remap jne (0xB79A43) may be the board's jmp.
        for (std::size_t i = 0; i < site->size; ++i) {
            const std::uintptr_t at = site->rva + i;
            const unsigned char b = base[at];
            if (at == c::board_remap_rva + 3 && (b == 0x75 || b == 0xEB)) continue;
            if (b != site->bytes[i]) return "block";
        }
    }
    for (const auto& data : c::door_data) {
        if (data.rva > span || 4 > span - data.rva) return "span";
        std::uint32_t value = 0;
        std::memcpy(&value, base + data.rva, sizeof(value));
        if (value != data.value) return "data";
    }
    return nullptr;
}

const char* ReadFacts(const void* image, std::uint16_t id, std::uintptr_t P, std::uintptr_t Q, std::uint32_t local, Facts& out) noexcept {
    out = Facts{};
    out.id = id;
    mgscoop::partner_vehicle::Read r{};
    mgscoop::partner_vehicle::ReadLive(image, id, r);
    if (r.refusal) return r.refusal;
    const std::uintptr_t rec = r.unit.rec;
    if (r.unit.id != id || !rec) return "read";
    if (rec & 15u) return "alignment";  // 0x55DB80 loads record+0x20 with movaps
    Digest before{}, after{};
    if (!ReadDigest(rec, P, Q, local, before)) return "read";
    out.rider0 = before.riders[0];
    std::memcpy(out.rec, r.unit.pos, sizeof(out.rec));
    alignas(16) float m0[16]{};
    alignas(16) float m1[16]{};
    std::uint8_t type = 0xFF;
    int count = -1;
    if (!CallNative(reinterpret_cast<std::uintptr_t>(image), rec, type, count, m0, m1)) return "fault";
    if (!ReadDigest(rec, P, Q, local, after) || !SameDigest(before, after)) return "changed";
    out.type = type;
    out.seats = count;
    std::memcpy(out.seat0, m0 + 12, sizeof(out.seat0));
    std::memcpy(out.seat1, m1 + 12, sizeof(out.seat1));
    return nullptr;
}
#endif

} // namespace mgscoop::vehicle_door
