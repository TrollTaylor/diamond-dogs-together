// MGS_COOP_HORSE_DRIVE_V1 live write (native/include/mgscoop/horse_drive.hpp; contract native/horse_drive_contract.hpp).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cmath>
#include <cstring>
#include "mgscoop/horse_drive.hpp"
#include "horse_drive_contract.hpp"

namespace mgscoop::horse_drive {
namespace {
namespace c = horse_drive_contract;
template <class T>
bool Get(std::uintptr_t address, T& value) noexcept {
    SIZE_T read = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), &value, sizeof(value), &read) &&
           read == sizeof(value);
}
bool Inside(std::uintptr_t image, std::size_t span, std::uintptr_t v) noexcept { return v >= image && v - image < span; }
using SetFn = void (*)(void*, std::uint32_t, const float*);
using RotFn = void (*)(void*, const float*);
using PoseFn = void (*)(void*, const float*, const float*);
// SEH wrapper (hold0): T.vt+0x70(t, quat) then T.vt+0x68(t, pos, quat); POD arguments only.
bool CallHold(std::uintptr_t rot, std::uintptr_t pose, std::uintptr_t t, const float* p, const float* q) noexcept {
    __try {
        reinterpret_cast<RotFn>(rot)(reinterpret_cast<void*>(t), q);
        reinterpret_cast<PoseFn>(pose)(reinterpret_cast<void*>(t), p, q);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// SEH wrapper: POD arguments only.
bool CallSet(std::uintptr_t fn, std::uintptr_t cs, std::uint32_t slot, const float* m) noexcept {
    __try {
        reinterpret_cast<SetFn>(fn)(reinterpret_cast<void*>(cs), slot, m);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}

const char* PinsReason(std::uintptr_t image, std::size_t span) noexcept {
    if (!image || span < 0x3000000) return "pins";
    unsigned char buf[0x420];
    for (const auto& s : c::sites) {
        if (s.size > sizeof(buf) || !Inside(image, span, image + s.rva + s.size)) return "pins";
        SIZE_T read = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(image + s.rva), buf, s.size, &read) || read != s.size ||
            std::memcmp(buf, s.bytes, s.size) != 0)
            return "pins";
    }
    for (const auto& s : c::slots) {
        std::uintptr_t target = 0;
        if (!Get(image + s.vtable_rva + s.offset, target) || target != image + s.target_rva) return "pins";
    }
    return nullptr;
}

const char* Write(std::uintptr_t image, std::size_t span, std::uintptr_t receiver, std::uintptr_t kind, std::uint16_t idx,
                  float x, float y, float z, float yaw) noexcept {
    std::uintptr_t vt = 0, positions = 0, indices = 0, groups = 0, group = 0, cs = 0, csvt = 0, fn = 0;
    std::uint32_t realized = 0, per = 0, groupCount = 0, csCount = 0, csBase = 0;
    if (!Get(receiver, vt) || (vt != image + c::receiver_vtables[0] && vt != image + c::receiver_vtables[1])) return "receiver";
    if (!Get(kind + 0x20, positions) || !positions || !Get(kind + 0x28, indices) || !Get(kind + 0x44, realized) || realized > 2)
        return "kind";
    // receiver vt+0x78 (0x140E4C540), read here instead of called: the realized slot of idx, its group, the CS.
    std::uint32_t slot = 0xffff;
    for (std::uint32_t j = 0; j < realized; ++j) {
        std::uint16_t v = 0;
        if (!Get(indices + j * 2u, v)) return "kind";
        if (v == idx) {
            slot = j;
            break;
        }
    }
    if (slot == 0xffff) return "unrealized";
    if (!Get(receiver + 0xf0, per) || !per || !Get(receiver + 0x110, groups) || !groups || !Get(receiver + 0x118, groupCount) ||
        slot / per >= groupCount || !Get(groups + 8u * (slot / per), group) || !group || !Get(group + 0x10, cs) || !cs ||
        (cs & 7u) || !Get(cs, csvt) || csvt != image + c::cs_vtable)
        return "cs";
    if (!Get(cs + 8, csCount) || !Get(cs + 0xc, csBase) || slot < csBase || slot - csBase >= csCount) return "slot";
    if (!Get(csvt + c::cs_set_transform, fn) || fn != image + c::cs_set_transform_rva || !Inside(image, span, fn)) return "cs";
    alignas(16) float m[16];
    Matrix(x, y, z, yaw, m);
    // 1. The census pose slot (the call-enter's stores at 0x140E6AC34..0x140E6AC4E).
    alignas(16) const float pose[4] = {x, y, z, yaw};
    std::memcpy(reinterpret_cast<void*>(positions + static_cast<std::uintptr_t>(idx) * 16u), pose, sizeof(pose));
    // 2. The engine's own transform write (0x140E6AF3F).
    if (!CallSet(fn, cs, slot, m)) return "call";
    return nullptr;
}

const char* HoldBody(std::uintptr_t image, std::uintptr_t t, std::uintptr_t t_vt, std::uintptr_t set_rot, std::uintptr_t set_pose,
                     float x, float y, float z, float yaw) noexcept {
    std::uintptr_t vt = 0, rot = 0, pose = 0;
    if (!image || !t || !Get(t, vt) || vt != t_vt || vt != image + kBodyTVtableRva || !Get(vt + 0x70, rot) || !Get(vt + 0x68, pose) ||
        rot != set_rot || pose != set_pose || rot != image + kBodySetRotRva || pose != image + kBodySetPoseRva)
        return "body";
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(yaw)) return "body";
    // Aligned as the partner driver's ApplyTransform passes them (Warp passes aligned vectors).
    alignas(16) const float p[4] = {x, y, z, 0.f};
    alignas(16) const float q[4] = {0.f, std::sin(yaw * 0.5f), 0.f, std::cos(yaw * 0.5f)};
    return CallHold(rot, pose, t, p, q) ? nullptr : "call";
}

} // namespace mgscoop::horse_drive
