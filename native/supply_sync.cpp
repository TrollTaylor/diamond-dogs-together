// Supply drops synced (docs/world-sync-protocol.md section 26, lua/coop/world_supply.lua): the supply-box observer's
// portable core (request decode, event ring, receipt grammar, poll reply). Pure: no Win32, no game memory.
#include "mgscoop/supply_sync.hpp"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace mgscoop::supply_sync {
namespace {
std::uint32_t U32(const unsigned char* p) noexcept {
    std::uint32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}
bool Mm(const unsigned char* p, std::int32_t& out) noexcept {
    float f = 0;
    std::memcpy(&f, p, sizeof(f));
    if (!std::isfinite(f)) return false;
    const double mm = std::floor(static_cast<double>(f) * 1000.0 + 0.5);
    if (mm > kPosLimitMm || mm < -static_cast<double>(kPosLimitMm)) return false;
    out = static_cast<std::int32_t>(mm);
    return true;
}
// Digits only, no leading zeros (a lone 0 is fine), within u32.
bool ParseU32(const char* p, const char* end, std::uint32_t& out) noexcept {
    if (p >= end || (*p == '0' && end - p > 1)) return false;
    std::uint64_t v = 0;
    for (; p < end; ++p) {
        if (*p < '0' || *p > '9') return false;
        v = v * 10 + static_cast<unsigned>(*p - '0');
        if (v > 0xFFFFFFFFull) return false;
    }
    out = static_cast<std::uint32_t>(v);
    return true;
}
struct Writer {
    char* out;
    std::size_t cap, n = 0;
    bool ok = true;
    void Put(const char* fmt, ...) noexcept {
        if (!ok) return;
        va_list args;
        va_start(args, fmt);
        const int w = std::vsnprintf(out + n, cap - n, fmt, args);
        va_end(args);
        if (w < 0 || static_cast<std::size_t>(w) >= cap - n) { ok = false; return; }
        n += static_cast<std::size_t>(w);
    }
};
} // namespace

bool DecodeRequest(const unsigned char* request, std::size_t size, Event& out) noexcept {
    if (!request || size < kRequestSize) return false;
    Event e{};
    if (!Mm(request + 0x100, e.pos_mm[0]) || !Mm(request + 0x104, e.pos_mm[1]) || !Mm(request + 0x108, e.pos_mm[2])) return false;
    e.kind = kRequest;
    e.drop_type = U32(request + 0x0);
    e.equip_id = U32(request + 0x4);
    e.parts_type = U32(request + 0x8);
    std::memcpy(&e.aim_object, request + 0x110, sizeof(e.aim_object));
    e.wait_level = request[0x112];
    e.level = request[0x120];
    e.flags = request[0x12d];
    out = e;
    return true;
}

bool RequestAccepted(const CoreView& before, const CoreView& after) noexcept {
    if (!before.ok || !after.ok) return true;
    return before.state != after.state || before.flags != after.flags || std::memcmp(before.stored, after.stored, kRequestSize) != 0;
}

std::uint32_t Ring::Push(const Event& e) noexcept {
    last_ = last_ == 0xFFFFFFFFu ? 1 : last_ + 1;
    Event copy = e;
    copy.seq = last_;
    events_[(last_ - 1) % kRing] = copy;
    if (count_ < kRing) ++count_;
    return last_;
}

std::size_t Ring::Since(std::uint32_t after, Event* out, std::size_t max, std::uint32_t& lost) const noexcept {
    lost = 0;
    if (!out || !max || after >= last_) return 0;
    const std::uint32_t oldest = last_ - static_cast<std::uint32_t>(count_) + 1;
    std::uint32_t first = after + 1;
    if (first < oldest) {
        lost = oldest - first;
        first = oldest;
    }
    std::size_t n = 0;
    for (std::uint32_t s = first; s <= last_ && n < max; ++s) out[n++] = events_[(s - 1) % kRing];
    return n;
}

void Ring::Clear() noexcept {
    last_ = 0;
    count_ = 0;
}

bool ParseCommand(const char* text, std::size_t size, Parsed& out) noexcept {
    if (!text) return false;
    const char* end = text + size;
    const char* space = static_cast<const char*>(std::memchr(text, ' ', size));
    if (!space) return false;
    const std::size_t word = static_cast<std::size_t>(space - text);
    Parsed p{};
    if (word == 4 && std::memcmp(text, "poll", 4) == 0) p.cmd = Command::poll;
    else if (word == 6 && std::memcmp(text, "mirror", 6) == 0) p.cmd = Command::mirror;
    else if (word == 6 && std::memcmp(text, "remove", 6) == 0) p.cmd = Command::remove;
    else return false;
    if (!ParseU32(space + 1, end, p.arg)) return false;
    if (p.cmd == Command::mirror && p.arg > 1) return false;
    out = p;
    return true;
}

std::size_t FormatPoll(const Snapshot& s, const Event* events, std::size_t count, std::uint32_t last, std::uint32_t lost,
                       char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    Writer w{out, capacity};
    if (!s.on) {
        w.Put("return {on=0}");
        return w.ok ? w.n : 0;
    }
    w.Put("return {on=1,inst=%u,box=%u,st=%lu,fl=%lu,last=%lu,lost=%lu,ev={", s.installed ? 1u : 0u, s.box_known ? 1u : 0u,
          static_cast<unsigned long>(s.box_known ? s.state : 0), static_cast<unsigned long>(s.box_known ? s.flags : 0),
          static_cast<unsigned long>(last), static_cast<unsigned long>(lost));
    for (std::size_t i = 0; events && i < count; ++i) {
        const Event& e = events[i];
        w.Put("%s{%lu,%u,%u,%lu,%lu,%lu,%ld,%ld,%ld,%u,%u,%u,%u,%lu,%lu,%u}", i ? "," : "", static_cast<unsigned long>(e.seq),
              unsigned(e.kind), unsigned(e.mirror), static_cast<unsigned long>(e.drop_type), static_cast<unsigned long>(e.equip_id),
              static_cast<unsigned long>(e.parts_type), static_cast<long>(e.pos_mm[0]), static_cast<long>(e.pos_mm[1]),
              static_cast<long>(e.pos_mm[2]), unsigned(e.level), unsigned(e.wait_level), unsigned(e.flags), unsigned(e.aim_object),
              static_cast<unsigned long>(e.caller_rva), static_cast<unsigned long>(e.state), unsigned(e.accepted));
    }
    w.Put("}}");
    return w.ok ? w.n : 0;
}

std::size_t FormatLogLine(const Event& e, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    Writer w{out, capacity};
    if (e.kind == kRequest)
        w.Put("supply=request;seq=%lu;mirror=%u;drop=%lu;equip=%lu;parts=%lu;x_mm=%ld;y_mm=%ld;z_mm=%ld;level=%u;wait=%u;flags=0x%x;"
              "aim=%u;caller=0x%lx;state=%lu;accepted=%u\r\n",
              static_cast<unsigned long>(e.seq), unsigned(e.mirror), static_cast<unsigned long>(e.drop_type),
              static_cast<unsigned long>(e.equip_id), static_cast<unsigned long>(e.parts_type), static_cast<long>(e.pos_mm[0]),
              static_cast<long>(e.pos_mm[1]), static_cast<long>(e.pos_mm[2]), unsigned(e.level), unsigned(e.wait_level),
              unsigned(e.flags), unsigned(e.aim_object), static_cast<unsigned long>(e.caller_rva), static_cast<unsigned long>(e.state),
              unsigned(e.accepted));
    else
        w.Put("supply=reset;seq=%lu;mirror=%u;caller=0x%lx;state=%lu\r\n", static_cast<unsigned long>(e.seq), unsigned(e.mirror),
              static_cast<unsigned long>(e.caller_rva), static_cast<unsigned long>(e.state));
    return w.ok ? w.n : 0;
}

} // namespace mgscoop::supply_sync
