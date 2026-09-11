#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace mgscoop {
// Steam public-universe individual desktop identity, with a nonzero account ID.
constexpr bool IsClientHostIdentity(std::uint64_t id) noexcept {
    return (id >> 32) == 0x01100001 && static_cast<std::uint32_t>(id) != 0;
}
inline std::uint64_t ParseClientHostIdentity(std::string_view text) noexcept {
    if (!text.empty() && text.back() == '\n') text.remove_suffix(1);
    if (!text.empty() && text.back() == '\r') text.remove_suffix(1);
    if (text.size() != 17) return 0;
    std::uint64_t id = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return 0;
        id = id * 10 + static_cast<unsigned>(c - '0');
    }
    return IsClientHostIdentity(id) ? id : 0;
}

// Current-build tpp-mod SessionConnectInfo prefix; no claim about later fields.
// Adapted from alicealys/tpp-mod 301bf797b7f8032941b21ed56a3570338de6ead4,
// GPLv3. See third_party/tpp-mod/LICENSE and THIRD_PARTY_NOTICES.md.
struct ClientConnectInfoPrefix {
    std::uint32_t state;
    unsigned char reserved[180];
    std::uint64_t host;
};
static_assert(offsetof(ClientConnectInfoPrefix, host) == 0xb8);
static_assert(sizeof(ClientConnectInfoPrefix) == 0xc0);

// The engine owns both objects. Only the two fields used by its connect command
// are supplied. A synchronous refusal restores those fields. A successful call
// merely starts the request; it does not prove a remote peer has connected.
template<class Invoke>
bool SubmitClientRequest(void* target, ClientConnectInfoPrefix& info,
                         std::uint64_t host, Invoke invoke) {
    if (!target || !IsClientHostIdentity(host) || info.state != 0) return false;
    const auto previous = info.host;
    info.host = host;
    const bool started = invoke(target, &info) != 0;
    if (!started) info.host = previous;
    return started;
}
}
