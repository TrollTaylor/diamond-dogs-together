// BODY0 round: the pure part of the write watch on body 0's transform (native/include/mgscoop/horse_body_watch.hpp).
#include "mgscoop/horse_body_watch.hpp"

namespace mgscoop::horse_body_watch {
namespace {
constexpr std::uint64_t kOurs = 1ull | 4ull | (3ull << 16) | (3ull << 18) | (3ull << 20) | (3ull << 22);
constexpr std::uint64_t kSet = 1ull | 4ull | (1ull << 16) | (3ull << 18) | (1ull << 20) | (3ull << 22);
}

std::uint64_t Dr7(std::uint64_t dr7, bool on) noexcept { return (dr7 & ~kOurs) | (on ? kSet : 0ull); }

bool Ours(std::uint64_t dr6) noexcept { return (dr6 & 3ull) != 0; }

bool PinLive(std::uint64_t last_ms, std::uint64_t now_ms) noexcept { return last_ms && now_ms >= last_ms && now_ms - last_ms <= kPinMs; }

void Note(Table& t, std::uint32_t rva, std::uint32_t tid, std::uint32_t ord, const std::uint32_t (&ret)[kRets]) noexcept {
    ++t.traps;
    for (std::uint32_t i = 0; i < t.used; ++i) {
        Rip& r = t.rip[i];
        if (r.rva != rva) continue;
        ++r.n;
        if (ord < r.ord_min) r.ord_min = ord;
        if (ord > r.ord_max) r.ord_max = ord;
        return;
    }
    if (t.used >= kRips) {
        ++t.dropped;
        return;
    }
    Rip& r = t.rip[t.used++];
    r.rva = rva;
    r.n = 1;
    r.tid = tid;
    r.ord_min = r.ord_max = ord;
    for (std::size_t k = 0; k < kRets; ++k) r.ret[k] = ret[k];
}

std::size_t StackRets(const std::uint64_t* stack, std::size_t n, std::uintptr_t image, std::uint32_t text_lo, std::uint32_t text_hi,
                      std::uint32_t (&ret)[kRets]) noexcept {
    std::size_t k = 0;
    for (std::size_t i = 0; i < kRets; ++i) ret[i] = 0;
    for (std::size_t i = 0; stack && i < n && k < kRets; ++i) {
        const std::uint64_t v = stack[i];
        if (v < image) continue;
        const std::uint64_t rva = v - image;
        if (rva >= text_lo && rva < text_hi) ret[k++] = static_cast<std::uint32_t>(rva);
    }
    return k;
}

void Target(float x, float y, float z, float h, float (&centre)[4], float (&foot)[4], float& foot_y) noexcept {
    centre[0] = x;
    centre[1] = y;
    centre[2] = z;
    centre[3] = 0.f;
    foot[0] = x;
    foot[1] = y - h;
    foot[2] = z;
    foot[3] = 0.f;
    foot_y = y - h;
}

} // namespace mgscoop::horse_body_watch
