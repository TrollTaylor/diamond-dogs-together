#pragma once
// Steam interface forwarding proxies for the co-op lab (lab-only).
//
// A proxy is a one-pointer object whose vtable forwards every slot to a real
// interface through generated x64 thunks, except a handful of overridden slots.
// The guest uses it to report a fake SteamID and persona, redirect its save
// folder and rewrite its auth ticket for the local master server; the host uses
// a save-only variant that overrides nothing but GetUserDataFolder.
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
// vtable and its code page live for the process lifetime. Returns the proxy, or
// null on failure. An override slot >= slot_count is rejected.
void* build_forwarding_proxy(void* real, unsigned slot_count, const ProxyOverride* overrides,
                             unsigned override_count, void* context) noexcept;

namespace identity {

struct Settings {
    std::uint64_t self_id = 0;         // guest fake id; 0 = keep the real identity
    char self_name[64]{};              // persona override; empty = keep real
    std::uint64_t peer_id = 0;         // loopback peer id (for friends overrides)
    char peer_name[64]{};              // name reported for the peer
    char save_dir[260]{};              // empty = no save redirect
    bool rewrite_auth_ticket = false;  // guest: rewrite ticket SteamID for fuse
};

// Record the lab identity once (from early_patches). Returns false if the
// settings are internally inconsistent (e.g. a non-individual self id).
bool configure(const Settings& settings) noexcept;
bool configured() noexcept;

// Wrap a freshly returned real interface pointer, building the proxy lazily on
// first use and caching it per real pointer. Returns `real` unchanged when no
// override applies. A null `real` is returned unchanged.
void* wrap_user(void* real) noexcept;
void* wrap_friends(void* real) noexcept;

// True when `id` is a lab loopback identity (self or configured peer).
bool is_loopback_id(std::uint64_t id) noexcept;

void reset_for_test() noexcept;

}  // namespace identity
}  // namespace mgscoop::lab
