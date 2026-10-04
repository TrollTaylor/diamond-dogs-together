#pragma once
// Steam interface forwarding proxies for the co-op lab (lab-only).
//
// A proxy is a one-pointer object whose vtable forwards every slot to a real
// interface through generated x64 thunks, except a handful of overridden slots.
// The guest uses it to report a fake SteamID and persona and to redirect its
// save folder; both roles use a SteamFriends proxy in loopback mode so that no
// lab loopback id (the guest's fake id, the configured or learned peer) is ever
// passed to real Steam persona/overlay calls; the host uses a save-only
// ISteamUser variant that overrides nothing but GetUserDataFolder.
#include <cstddef>
#include <cstdint>

#include "mgscoop/steam_abi.hpp"

namespace mgscoop::lab {

// The only field the game reads is the vtable at offset 0. Overrides recover
// `real` and `context` from the object pointer handed to them as `this`.
struct ProxyHeader {
    const void** vtable;
    void* real;
    void* context;
};

struct ProxyOverride {
    unsigned slot;
    const void* fn;
};

constexpr unsigned kThunkStride = 24;  // 19 bytes used; padded for alignment

// Encode  48 B9 <real> | 48 8B 01 | FF A0 <slot*8>  (mov rcx,real; mov rax,[rcx];
// jmp [rax+disp]) into out[0..kThunkStride). Pure; unit tested.
std::size_t encode_forward_thunk(std::uint8_t* out, const void* real, unsigned slot) noexcept;

// Build a forwarding proxy over `real` with `slot_count` slots. Non-overridden
// slots get a thunk in a VirtualAlloc'd PAGE_EXECUTE_READ page. The proxy, its
// vtable and its code page live for the process lifetime (callers cache the
// proxy per real pointer). Returns the proxy, or null on failure. An override
// slot >= slot_count is rejected.
void* build_forwarding_proxy(void* real, unsigned slot_count, const ProxyOverride* overrides,
                             unsigned override_count, void* context) noexcept;

namespace identity {

// Fallback persona reported for a loopback peer whose HELLO carried no name.
inline constexpr char kPeerFallbackName[] = "lab-peer";

struct Settings {
    std::uint64_t self_id = 0;  // guest fake id; 0 = keep the real identity
    char self_name[64]{};       // persona override; empty = keep real
    std::uint64_t peer_id = 0;  // configured loopback peer id (0 = learn from the emulator)
    char save_dir[260]{};       // empty = no save redirect
    bool loopback = false;      // loopback emulation active (wrap SteamFriends in both roles)
    void (*log)(const char* line) noexcept = nullptr;  // early.log (bounded: one line per event kind)
};

// Record the lab identity once (from early_patches). Returns false if the
// settings are internally inconsistent (a non-individual self id, or a fake
// self id without loopback, which would let it reach real Steam networking).
bool configure(const Settings& settings) noexcept;
bool configured() noexcept;

// Which proxies the configured settings need (pure decisions over Settings).
bool needs_user_proxy(const Settings& settings) noexcept;
bool needs_friends_proxy(const Settings& settings) noexcept;

// Wrap a freshly returned real interface pointer, building the proxy lazily on
// first use and caching it per real pointer. Returns `real` unchanged when no
// override applies. A null `real` is returned unchanged.
void* wrap_user(void* real) noexcept;
void* wrap_friends(void* real) noexcept;

// The loopback emulator reports the peer's id when learned and its persona on
// every HELLO (empty name = unknown). Thread-safe.
void note_peer(std::uint64_t id, const char* name) noexcept;
// True when `id` is a lab loopback identity: the fake self id, the configured
// peer id or the peer id learned from the emulator.
bool is_loopback_id(std::uint64_t id) noexcept;
// Persona reported for the loopback peer (HELLO name or kPeerFallbackName).
const char* peer_name() noexcept;

// The guest's GetAuthSessionTicket rewrites ticket bytes 12..19 to the fake id
// (fuse reads the SteamID there) only once steam_observer has confirmed that
// the master-server redirect to a local URL is installed. Until then the
// ticket is passed through untouched (never a tampered ticket to Konami).
void arm_ticket_rewrite(bool armed) noexcept;
bool ticket_rewrite_armed() noexcept;

void reset_for_test() noexcept;

}  // namespace identity
}  // namespace mgscoop::lab
