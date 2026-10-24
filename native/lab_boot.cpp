// The developer test boot's Lua receipts (docs/test-boot.md); pure.
#include "mgscoop/lab_boot.hpp"
#include <cstdio>
#include <cstring>

namespace mgscoop::lab_boot {
namespace {
bool NulFree(const char* source, std::size_t size) noexcept {
    for (std::size_t i = 0; i < size; ++i) if (!source[i]) return false;
    return true;
}
bool SameName(const char* source, std::size_t size, const char* name) noexcept {
    return name && std::strlen(name) == size && std::memcmp(name, source, size) == 0;
}
bool Kept(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '=' || c == ':' ||
        c == ',' || c == '.' || c == ';' || c == '-';
}
} // namespace

Receipt Classify(const char* source, std::size_t size, const char* name) noexcept {
    if (!source || !size || size > kMaxReceipt || !SameName(source, size, name) || !NulFree(source, size)) return Receipt::none;
    if (size == sizeof(kCfgSource) - 1 && std::memcmp(source, kCfgSource, size) == 0) return Receipt::cfg;
    constexpr std::size_t prefix = sizeof(kLogPrefix) - 1;
    if (size > prefix && std::memcmp(source, kLogPrefix, prefix) == 0) return Receipt::log;
    return Receipt::none;
}

bool Admitted(const Gate& gate) noexcept {
    return gate.worker_compiled && gate.worker_active && gate.lab_mode && gate.lab_active && gate.offline && gate.copied_saves;
}

std::size_t FormatCfgReply(bool admitted, bool fast, const LabBoot& boot, bool host, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    int n = 0;
    if (!admitted || (!fast && !boot.mission)) {
        n = std::snprintf(out, capacity, "return 0");
    } else {
        n = std::snprintf(out, capacity, "return 1,%d,%u,%d,%ld,%ld,%ld,%ld,%ld,%u,%d", fast ? 1 : 0, static_cast<unsigned>(boot.mission),
                          boot.has_pos ? 1 : 0, static_cast<long>(boot.x_mm), static_cast<long>(boot.y_mm), static_cast<long>(boot.z_mm),
                          static_cast<long>(boot.yaw_mdeg), static_cast<long>(boot.clock_s), static_cast<unsigned>(boot.weather), host ? 1 : 2);
    }
    if (n <= 0 || static_cast<std::size_t>(n) >= capacity) { out[0] = 0; return 0; }
    return static_cast<std::size_t>(n);
}

std::size_t FormatLogLine(const char* source, std::size_t size, char* out, std::size_t capacity) noexcept {
    constexpr std::size_t prefix = sizeof(kLogPrefix) - 1;
    if (!out || !capacity) return 0;
    out[0] = 0;
    if (!source || size <= prefix || std::memcmp(source, kLogPrefix, prefix) != 0) return 0;
    const char* payload = source + prefix;
    const std::size_t length = size - prefix;
    if (!length || length > kMaxLogPayload) return 0;
    constexpr char head[] = "lab_boot=";
    std::size_t need = sizeof(head) - 1 + length + 2;
    for (std::size_t i = 0; i < length; ++i) if (payload[i] == ';') ++need;
    if (need + 1 > capacity) return 0;
    std::size_t at = 0;
    for (std::size_t i = 0; head[i]; ++i) out[at++] = head[i];
    for (std::size_t i = 0; i < length; ++i) {
        const char c = Kept(payload[i]) ? payload[i] : '_';
        out[at++] = c;
        if (c == ';') out[at++] = ' ';
    }
    out[at++] = '\r';
    out[at++] = '\n';
    out[at] = 0;
    return at;
}

} // namespace mgscoop::lab_boot
