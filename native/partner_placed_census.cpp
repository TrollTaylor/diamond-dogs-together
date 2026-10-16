// Original MGS Co-op WS4 step 0: the read-only C4 / bionic-arm census
// (partner_placed_census.hpp). This unit holds the pad sample seqlock the input
// hook publishes and ties the header's offsets to the generated contract.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "mgscoop/partner_placed_census.hpp"
#include "partner_player_contract.hpp"

namespace mgscoop::partner_placed_census {
namespace {
namespace c = mgscoop::partner_player_contract;
static_assert(k::kServiceRootRva == c::service_root_rva && k::kRootService == c::root_service && k::kSvcGm == c::svc_gm &&
                  k::kGmPlaced == c::gm_placed,
              "the placed service chain is the contract's");
static_assert(k::kPlacedLiveVtRva == c::placed_slot_live_rva && k::kPlacedBaseVtRva == c::placed_slot_base_rva,
              "the placed iface vtables are the contract's");
static_assert(k::kFirst == c::placed_first && k::kOwnerCount == c::placed_owner_count && k::kPerOwner == c::placed_per_owner &&
                  k::kEquips == c::placed_equips && k::kEquipStride == c::placed_equip_stride && k::kFlags == c::placed_flags &&
                  k::kFlagStride == c::placed_flag_stride && k::kOwners == c::placed_owners && k::kAge == c::placed_age,
              "the owner table layout is the contract's (0xFC54F0)");
static_assert(k::kPrimPlaceFlags == c::prim_place_flags && k::kPrimPlaceTarget == c::prim_place_target &&
                  k::kPrimPlaceLatched == c::prim_place_latched && k::kPrimPlaceBit == c::prim_place_bit,
              "the plugin placement latch is the contract's");
static_assert(k::kRecState == c::gun_state && k::kRecSub == c::gun_sub && k::kRecSurface == c::gun_surface,
              "the gun record fields are the contract's");
static_assert(k::kPadMag == c::pad_stun_mag && k::kPadMask == c::pad_stun_mask, "the stun op-0 pad fields are the contract's");
static_assert(c::pp_derived <= k::kPadMag && k::kPadMag + sizeof(float) <= c::pp_derived + c::pp_derived_size,
              "PP+0x10 lies in the window's saved derived block");
static_assert(c::pp_masks <= k::kPadMask && k::kPadMask + sizeof(std::uint32_t) <= c::pp_masks + c::pp_masks_size,
              "PP+0x2C lies in the window's saved masks");
static_assert(kTypePlaced == c::equip_type_placed, "type 13 is placed equipment");

volatile LONG g_on = 0;
volatile LONG g_seq = 0;  // odd while a write is in flight; 0: never written
PadSample g_sample{};
} // namespace

void SetPadCensus(bool on) noexcept { InterlockedExchange(&g_on, on ? 1 : 0); }
bool PadCensusOn() noexcept { return InterlockedCompareExchange(&g_on, 0, 0) != 0; }

void PublishPad(const PadSample& sample) noexcept {
    InterlockedIncrement(&g_seq);  // odd
    MemoryBarrier();
    g_sample = sample;
    MemoryBarrier();
    InterlockedIncrement(&g_seq);  // even
}

bool ReadPad(PadSample& out) noexcept {
    for (int attempt = 0; attempt < 4; ++attempt) {
        const LONG before = InterlockedCompareExchange(&g_seq, 0, 0);
        if (before == 0) return false;
        if (before & 1) continue;
        MemoryBarrier();
        PadSample copy = g_sample;
        MemoryBarrier();
        if (InterlockedCompareExchange(&g_seq, 0, 0) != before) continue;
        out = copy;
        return true;
    }
    return false;
}

namespace detail {
void ResetPadForTest() noexcept {
    InterlockedExchange(&g_on, 0);
    InterlockedExchange(&g_seq, 0);
    g_sample = PadSample{};
}
} // namespace detail

} // namespace mgscoop::partner_placed_census
