#pragma once
// WS3 vehicles (docs/vehicle-ws3.md): read-only access to one TppVehicle2 unit
// record and its realized slot body (the ride census, partner_ride_census=1),
// and the guarded tilt write of the viewer's copy (partner_ride_tilt=1).
//
// Chain (mgsvtpp.exe 1.0.15.4; every step pinned in tools/generate_partner_contract.py):
//   service = *(*(*(*(0x2C3A8A0)+8)+0x98)+0x2A8)     the DM anchor's own lookup (0x11987C9..0x11987F3);
//             vtable 0x22AC8E0, +0x18 = 0xA272A0 (the id lookup)
//   object  = *(service+0xB0), vtable 0x22ED9F0: +0 = 0xB2DD30 (count = u16 object+0x1C0),
//             +0x78 = 0xB36780 (the SetPosition forwarder: V vt+0x70)
//   rec     = *(object+0x10) + (id & 0x1FF) * 0x80, live bit *(object+0x28) (0xA272A0)
//   rec+0x20 position (x, y, z), +0x2C yaw (GetPosition 0xB2BA61, SetPosition 0x16FBEFB),
//   +0x44 / +0x48 the other two Euler angles (0x55DDA0), +0x6C type (category << 4 | variant;
//   0x16FDDC0), +0x6E realized slot (0xFF none; 0x16FBEDC), +0x6F slot, +0x78 index
//   V    = *(object+0x1A8) (vt+0x70 = 0x16FBEC0), RB = *(V+0x98) (vt+0x78 = 0x16FDDC0)
//   cat  = (*(RB+0x70))[type >> 4] (vt+0x1F0 = 0xBA9710), body = (*(cat+0x1C8))[slot]
// 0xBA9710's per-frame (small) move builds the physics pose from 0x55DDA0(body)
// (body+0x2C/+0x44/+0x48); the seat anchor builds the seat matrix from
// 0x55DDA0(rec) (0xB7AF90 -> 0x55DB80). The census proves (or refutes) that
// body == rec before any write is enabled (critic #16): the tilt write refuses
// unless the two pointers are equal at write time.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace mgscoop::partner_vehicle {

// RVAs (static_asserted against the generated contract in partner_vehicle.cpp).
inline constexpr std::uintptr_t kServiceRootRva = 0x2C3A8A0;
inline constexpr std::uintptr_t kServiceVtRva = 0x22AC8E0, kLookupRva = 0xA272A0;
inline constexpr std::uintptr_t kObjectVtRva = 0x22ED9F0, kCountFnRva = 0xB2DD30, kSetPosFnRva = 0xB36780;
inline constexpr std::uintptr_t kVFnRva = 0x16FBEC0, kRbFnRva = 0x16FDDC0, kCatFnRva = 0xBA9710;
inline constexpr std::uintptr_t kVVtRvas[] = {0x2441010, 0x2441120};
inline constexpr std::uintptr_t kRbVtRvas[] = {0x24416B0, 0x2441780};
inline constexpr std::uintptr_t kCatVtRvas[] = {0x22F7F90, 0x22F82E0, 0x22F8630, 0x22F8980, 0x22F8CD0, 0x22FCB50};
// Offsets.
inline constexpr std::size_t kWorld = 8, kManager = 0x98, kService = 0x2A8, kObject = 0xB0, kRecords = 0x10, kBitmap = 0x28,
                             kCount = 0x1C0, kV = 0x1A8, kRb = 0x98, kCats = 0x70, kSlots = 0x1C8;
inline constexpr std::size_t kVSlot = 0x70, kRbSlot = 0x78, kCatSlot = 0x1F0, kLookupSlot = 0x18, kSetPosSlot = 0x78;
inline constexpr std::size_t kRecStride = 0x80, kPos = 0x20, kYaw = 0x2C, kA44 = 0x44, kA48 = 0x48, kType = 0x6C,
                             kRealized = 0x6E, kSlot = 0x6F, kIndex = 0x78;
inline constexpr std::uint16_t kVehicleType = 50;  // GameObject type (id >> 9)
inline constexpr std::uint8_t kNotRealized = 0xFF, kCategoryUnknown = 0xFF;
// |angle| bound of the tilt sanity gate (just under pi/2, in milliradians).
inline constexpr std::int32_t kTiltMaxMrad = 1570;
// M4 VEHICLE_STATE (docs/vehicles-companions-design.md 6.4): the record state words and the
// body life block. rec+0x70 (u32; IsAlive 0xB2C0D3 tests & 0x800004, GetState 0xB2BED6 & 0x18000)
// and rec+0x7C (u16) feed the engine's own ene_veh_state encoding (StoreToSVars 0xB35BC0,
// 0xB35C90..0xB35D62). The life block: SetBodyLife (instance command 0x3BA5A795, handler
// 0xB2BC69) and StoreToSVars (ene_veh_life, 0xB35F60..0xB35FBD) reach it through the object's
// vt+0x98 = 0xB2DD00: *(object+0x150) + index*0x18, 12 u16; word 0 the body life (SetBodyLife
// writes it), words 1..11 the other parts (StoreToSVars copies all 12; Spawn's wheelLife sets them).
inline constexpr std::size_t kFlags70 = 0x70, kFlags7C = 0x7C;
inline constexpr std::uintptr_t kLifeFnRva = 0xB2DD00;
inline constexpr std::size_t kLifeSlot = 0x98, kLifeBase = 0x150, kLifeStride = 0x18;
inline constexpr unsigned kLifeWords = 12;
// 0xB2DD00: movzx eax,dx; lea edx,[rax+rax*2]; mov rax,[rcx+0x150]; shl edx,2; movsxd r8,edx; lea rax,[rax+r8*2]; ret
inline constexpr unsigned char kLifeFnBytes[24] = {0x0F, 0xB7, 0xC2, 0x8D, 0x14, 0x40, 0x48, 0x8B, 0x81, 0x50, 0x01, 0x00,
                                                   0x00, 0xC1, 0xE2, 0x02, 0x4C, 0x63, 0xC2, 0x4A, 0x8D, 0x04, 0x40, 0xC3};

struct Unit {
    std::uint16_t id = 0xFFFF;
    std::uintptr_t service = 0, object = 0, records = 0, bitmap = 0, rec = 0;
    std::uint16_t count = 0, index78 = 0;
    std::uint8_t type = 0, realized = kNotRealized, slot = 0;
    float pos[4]{};  // x, y, z, yaw (rec+0x20..+0x2C)
    float a44 = 0, a48 = 0;
    std::uint32_t flags70 = 0;  // rec+0x70
    std::uint16_t flags7c = 0;  // rec+0x7C
};
struct Body {
    std::uintptr_t v = 0, v_vt = 0, rb = 0, rb_vt = 0, cats = 0, cat = 0, cat_vt = 0, cat_fn = 0, slots = 0, body = 0;
    float pos[4]{};
    float a44 = 0, a48 = 0;
};
struct Read {
    std::uintptr_t image = 0;            // the image the read used (census RVAs)
    const char* refusal = "unread";      // the unit chain (nullptr: unit ok)
    const char* body_refusal = "unread"; // the realized body chain (nullptr: body ok)
    Unit unit;
    Body body;
    // The realized slot body is the unit record itself (critic #16).
    bool Same() const noexcept { return !refusal && !body_refusal && body.body && body.body == unit.rec; }
};
inline std::uint8_t Category(std::uint8_t type) noexcept { return static_cast<std::uint8_t>(type >> 4); }
inline std::uint8_t Variant(std::uint8_t type) noexcept { return static_cast<std::uint8_t>(type & 15u); }

namespace detail {
inline bool Range(std::uintptr_t p, std::size_t n) noexcept { return p && n && p <= std::numeric_limits<std::uintptr_t>::max() - n; }
template <class Reader, class T> bool Get(Reader& r, std::uintptr_t p, T& out) noexcept { return Range(p, sizeof(T)) && r(p, &out, sizeof(T)); }
template <std::size_t N> bool OneOf(std::uintptr_t value, std::uintptr_t image, const std::uintptr_t (&rvas)[N]) noexcept {
    for (const std::uintptr_t rva : rvas)
        if (value == image + rva) return true;
    return false;
}
} // namespace detail

// Pure read through `reader(address, out, size) -> bool` (SEH/ReadProcessMemory
// in the game, a fixture map in tests). Never writes; a refusal names the first
// failing link (static text).
template <class Reader>
void ReadVehicle(Reader& r, std::uintptr_t image, std::uint16_t id, Read& out) noexcept {
    using detail::Get;
    out = Read{};
    out.image = image;
    out.unit.id = id;
    out.body_refusal = "unit";
    Unit& u = out.unit;
    const auto fail = [&](const char* why) noexcept { out.refusal = why; };
    if (!image) return fail("image");
    if (id == 0xFFFF || (id >> 9) != kVehicleType) return fail("type");
    const unsigned index = id & 0x1FFu;
    std::uintptr_t root = 0, world = 0, manager = 0, vt = 0, fn = 0;
    if (!Get(r, image + kServiceRootRva, root) || !root || !Get(r, root + kWorld, world) || !world || !Get(r, world + kManager, manager) ||
        !manager || !Get(r, manager + kService, u.service) || !u.service) return fail("service");
    if (!Get(r, u.service, vt) || vt != image + kServiceVtRva) return fail("service_vt");
    if (!Get(r, vt + kLookupSlot, fn) || fn != image + kLookupRva) return fail("lookup");
    if (!Get(r, u.service + kObject, u.object) || !u.object) return fail("object");
    if (!Get(r, u.object, vt) || vt != image + kObjectVtRva) return fail("object_vt");
    if (!Get(r, vt, fn) || fn != image + kCountFnRva) return fail("count_fn");
    if (!Get(r, vt + kSetPosSlot, fn) || fn != image + kSetPosFnRva) return fail("setpos_fn");
    if (!Get(r, u.object + kCount, u.count) || !u.count || u.count > 512 || index >= u.count) return fail("index");
    std::uint32_t word = 0;
    if (!Get(r, u.object + kBitmap, u.bitmap) || !u.bitmap || !Get(r, u.bitmap + (index >> 5) * 4u, word)) return fail("bitmap");
    if (!(word & (1u << (index & 31u)))) return fail("live");
    if (!Get(r, u.object + kRecords, u.records) || !detail::Range(u.records, std::size_t(u.count) * kRecStride)) return fail("records");
    u.rec = u.records + std::uintptr_t(index) * kRecStride;
    unsigned char rec[kRecStride]{};
    if (!r(u.rec, rec, sizeof(rec))) return fail("record");
    std::memcpy(u.pos, rec + kPos, sizeof(u.pos));
    std::memcpy(&u.a44, rec + kA44, sizeof(u.a44));
    std::memcpy(&u.a48, rec + kA48, sizeof(u.a48));
    u.type = rec[kType];
    u.realized = rec[kRealized];
    u.slot = rec[kSlot];
    std::memcpy(&u.index78, rec + kIndex, sizeof(u.index78));
    std::memcpy(&u.flags70, rec + kFlags70, sizeof(u.flags70));
    std::memcpy(&u.flags7c, rec + kFlags7C, sizeof(u.flags7c));
    out.refusal = nullptr;
    // The realized body (0x16FBEC0 -> 0x16FDDC0 -> cat vt+0x1F0 -> [cat+0x1C8][slot]).
    Body& b = out.body;
    const auto body_fail = [&](const char* why) noexcept { out.body_refusal = why; };
    if (u.realized == kNotRealized) return body_fail("unrealized");
    if (!Get(r, u.object + kV, b.v) || !b.v) return body_fail("v");
    if (!Get(r, b.v, b.v_vt) || !detail::OneOf(b.v_vt, image, kVVtRvas)) return body_fail("v_vt");
    if (!Get(r, b.v_vt + kVSlot, fn) || fn != image + kVFnRva) return body_fail("v_fn");
    if (!Get(r, b.v + kRb, b.rb) || !b.rb) return body_fail("rb");
    if (!Get(r, b.rb, b.rb_vt) || !detail::OneOf(b.rb_vt, image, kRbVtRvas)) return body_fail("rb_vt");
    if (!Get(r, b.rb_vt + kRbSlot, fn) || fn != image + kRbFnRva) return body_fail("rb_fn");
    if (!Get(r, b.rb + kCats, b.cats) || !b.cats || !Get(r, b.cats + std::uintptr_t(Category(u.type)) * 8u, b.cat) || !b.cat)
        return body_fail("cat");
    if (!Get(r, b.cat, b.cat_vt) || !b.cat_vt) return body_fail("cat");
    Get(r, b.cat_vt + kCatSlot, b.cat_fn);
    if (!detail::OneOf(b.cat_vt, image, kCatVtRvas)) return body_fail("cat_vt");
    if (b.cat_fn != image + kCatFnRva) return body_fail("cat_fn");
    if (!Get(r, b.cat + kSlots, b.slots) || !b.slots || !Get(r, b.slots + std::uintptr_t(u.slot) * 8u, b.body) || !b.body)
        return body_fail("body");
    unsigned char body[kA48 + 4]{};
    if (!r(b.body, body, sizeof(body))) return body_fail("body_read");
    std::memcpy(b.pos, body + kPos, sizeof(b.pos));
    std::memcpy(&b.a44, body + kA44, sizeof(b.a44));
    std::memcpy(&b.a48, body + kA48, sizeof(b.a48));
    out.body_refusal = nullptr;
}

// M4 VEHICLE_STATE: the engine's ene_veh_state word for a record in the pool (StoreToSVars
// 0xB35C90..0xB35D62): bit 0 not (rec+0x70 & 0xC0000), bit 1 rec+0x7C bit 14, bit 2 rec+0x70
// bit 3, bits 4..9 rec+0x70 bits 9..14 (bit 3 unused).
inline std::uint16_t SvState(std::uint32_t f70, std::uint16_t f7c) noexcept {
    std::uint16_t s = (f70 & 0xC0000u) ? 0 : 1;
    if (f7c & 0x4000u) s |= 2;
    if (f70 & 0x8u) s |= 4;
    for (unsigned b = 9; b <= 14; ++b)
        if (f70 & (1u << b)) s = static_cast<std::uint16_t>(s | (1u << (b - 5)));
    return s;
}
struct Life {
    const char* refusal = "unread";  // nullptr: words valid
    std::uint16_t words[kLifeWords]{};
    std::uint16_t zero = 0;   // bits 1..11: words 1..11 that read 0
    std::uint8_t parts = 0;   // words 1..11 above 0
    std::uint16_t state = 0;  // SvState of the unit
};
// Reads the life block of an already read unit (read.refusal == nullptr): the object's
// vt+0x98 must be 0xB2DD00 with its pinned bytes (kLifeFnBytes); never writes.
template <class Reader>
void ReadLife(Reader& r, const Read& read, Life& out) noexcept {
    using detail::Get;
    out = Life{};
    if (read.refusal) { out.refusal = read.refusal; return; }
    const Unit& u = read.unit;
    const std::uintptr_t image = read.image;
    std::uintptr_t vt = 0, fn = 0, base = 0;
    if (!image || !Get(r, u.object, vt) || !vt || !Get(r, vt + kLifeSlot, fn) || fn != image + kLifeFnRva) { out.refusal = "life_fn"; return; }
    unsigned char code[sizeof(kLifeFnBytes)]{};
    if (!r(fn, code, sizeof(code)) || std::memcmp(code, kLifeFnBytes, sizeof(code)) != 0) { out.refusal = "life_code"; return; }
    if (!Get(r, u.object + kLifeBase, base) || !base) { out.refusal = "life_base"; return; }
    const std::uintptr_t at = base + std::uintptr_t(u.id & 0x1FFu) * kLifeStride;
    std::uint16_t words[kLifeWords]{};
    if (!detail::Range(at, sizeof(words)) || !r(at, words, sizeof(words))) { out.refusal = "life_read"; return; }
    std::memcpy(out.words, words, sizeof(words));
    for (unsigned i = 1; i < kLifeWords; ++i) {
        if (out.words[i] == 0) out.zero = static_cast<std::uint16_t>(out.zero | (1u << i));
        else ++out.parts;
    }
    out.state = SvState(u.flags70, u.flags7c);
    out.refusal = nullptr;
}

// The same object and the same fields twice (a write is gated on two equal reads).
bool SameRead(const Read& a, const Read& b) noexcept;

// --- The tilt write gate (partner_ride_tilt, default on; docs/vehicle-ws3.md "A1").
struct TiltIn {
    bool enabled = false;       // partner_ride_tilt on (lab and play) and the code gate
    bool pins = false;          // the contract blocks verify (VerifyImage) and the code gate passed
    bool runtime = false;       // the mission runtime thread, not terminating
    bool peer = false;          // the peer advertised RIDE_TILT (ride ext feature)
    std::uint16_t id = 0xFFFF;  // the remote driver's vehicle (this copy's id)
    std::uint16_t local = 0xFFFF;  // the vehicle the local player rides (0xFFFF none)
    std::int32_t a44_mrad = 0, a48_mrad = 0;
    const Read* first = nullptr;
    const Read* second = nullptr;
};
// nullptr: write rec+0x44/+0x48; else a static reason.
const char* TiltRefusal(const TiltIn& in) noexcept;
// The sender's read gate: its own vehicle's record (no body needed).
struct TiltReadIn {
    bool enabled = false, runtime = false, peer = false;
    std::uint16_t id = 0xFFFF, local = 0xFFFF;
    const Read* read = nullptr;
};
const char* TiltReadRefusal(const TiltReadIn& in, std::int32_t& a44_mrad, std::int32_t& a48_mrad) noexcept;
// Rounded milliradians, INT32_MIN when not finite.
std::int32_t Mrad(float radians) noexcept;
std::int32_t Mm(float metres) noexcept;

// --- The read-only census line (partner_ride_census=1): one line per vehicle.
// "partner_ride_census; who=<who>; id=..; ref=..; type=0x..; cat=..; var=..; realized=..; slot=..; idx78=..;
//  rec=0x..; pos_mm=x,y,z; yaw_mrad=..; a44_mrad=..; a48_mrad=..; body_ref=..; v_vt=0x..; rb_vt=0x..; cat_vt=0x..;
//  cat_fn=0x..; body=0x..; body_pos_mm=x,y,z; body_yaw_mrad=..; body_a44_mrad=..; body_a48_mrad=..; same=0|1\r\n"
// (vtable rvas relative to read.image). Returns the length (0 on overflow).
std::size_t FormatCensus(const char* who, const Read& read, char* out, std::size_t capacity) noexcept;

#ifdef _WIN32
// Live reads (ReadProcessMemory; never faults).
void ReadLive(const void* image, std::uint16_t id, Read& out) noexcept;
void ReadLifeLive(const Read& read, Life& out) noexcept;
// The contract blocks this module relies on (code_sites rvas of the chain and
// the move thresholds); nullptr when they match, else "block_0x...".
const char* VerifyImage(const void* image, std::size_t span) noexcept;
// Writes rec+0x44/+0x48 (two aligned 4-byte stores) after TiltRefusal passed
// on two equal reads. nullptr on success, else a static reason.
const char* WriteTilt(const Read& gated, std::int32_t a44_mrad, std::int32_t a48_mrad) noexcept;
#endif

} // namespace mgscoop::partner_vehicle
