#pragma once
#include <cstddef>
#include <cstdint>

namespace mgscoop::equipment_stock_writer {
// The change-equip request's stock/ammo fields (request +10 / +14). The
// qualified writer requests 0 / 0; MGS_LOCAL_STOCK46_AMMO_TEST (research only,
// default OFF, its own qualification run) requests a loaded magazine so a
// native reload has stock: ammo 30, stock 210 (the debug refill's values for
// this weapon class). Changing a qualified native call's arguments is a new
// native path: never enable it in a normal build.
struct RequestAmounts {
    std::uint16_t stock, ammo;
};
inline constexpr RequestAmounts kQualifiedAmounts{0, 0};
inline constexpr RequestAmounts kAmmoTestAmounts{210, 30};
inline constexpr RequestAmounts RequestAmountsFor(bool ammo_test) noexcept { return ammo_test ? kAmmoTestAmounts : kQualifiedAmounts; }
#if defined(MGS_LOCAL_STOCK46_AMMO_TEST) && MGS_LOCAL_STOCK46_AMMO_TEST
inline constexpr bool kAmmoTest = true;
#else
inline constexpr bool kAmmoTest = false;
#endif
// Separate copied-save experiment. The owner supplies existing runtime/code,
// source, lab/save and session guards; the implementation re-reads every native
// equipment/resource owner before its sole stock46 instance1 dispatch.
struct Context {
    bool enabled=false, owner=false, offline_gates=false, preload_queued=false;
    bool body_ready=false;
    std::uint32_t mission=0, body_epoch=0;
    std::uintptr_t base=0, player=0, remote_block=0;
    std::size_t image_span=0;
};
class Authorization {
    bool active_=false,attempted_=false;
    std::uint32_t mission_=0;
public:
    void Begin(std::uint32_t mission) noexcept {active_=true;attempted_=false;mission_=mission;}
    void End() noexcept {active_=false;attempted_=false;mission_=0;}
    bool Attempted() const noexcept {return attempted_;}
    bool CanObserveReadiness(const Context& c) const noexcept {
        return c.enabled && active_ && !attempted_ && c.owner && c.offline_gates && c.body_ready &&
            c.preload_queued && c.mission==30010 && c.mission==mission_ && c.player && c.remote_block && c.base && c.image_span;
    }
    const char* Try(const Context& c) noexcept {
        if(!c.enabled) return "disabled";
        if(!active_ || !c.owner) return "not_runtime_owner";
        if(c.mission!=30010 || c.mission!=mission_) return "allocation_mission";
        if(attempted_) return "already_attempted";
        if(!c.body_ready || !c.preload_queued) return "not_ready";
        // A bounded read-only readiness pass precedes this mutation lease.
        // Warp/body epochs never reset the allocation authorization.
        attempted_=true;
        if(!c.offline_gates) return "offline_owner_gates";
        if(!c.player || !c.remote_block || !c.base || !c.image_span) return "body_lease";
        return nullptr;
    }
};
class Readiness {
    unsigned observations_=0;
public:
    static constexpr unsigned limit=50;
    void Reset() noexcept {observations_=0;}
    unsigned Count() const noexcept {return observations_;}
    // Observation count limits waiting; only the strict preflight result is
    // readiness. Failed reads never authorize a native dispatch.
    const char* Observe(bool ready) noexcept {
        if(observations_==limit) return "readiness_limit";
        if(ready) return nullptr;
        if(observations_<limit) ++observations_;
        return observations_==limit ? "readiness_limit" : "readiness_wait";
    }
};
// Every generated SHA-256 gate (native/equipment_stock_writer_pins.hpp,
// tools/generate_equip_writer_pins.py) holds in the running image; mismatch
// names the first differing span. Pure reads; available in every build.
bool PinsIntact(std::uintptr_t base,std::size_t span,std::uintptr_t& mismatch) noexcept;
// All public methods are inert in builds without MGS_LOCAL_STOCK46_WRITER_TEST.
void BeginAllocation(std::uint32_t mission) noexcept;
void End() noexcept;
void Observe(const Context& context,void (*log)(const char*) noexcept) noexcept;

// --- The generalized writer (W3; docs/native-puppet.md "Generalized writer").
// Per remote slot (0 hip, 1 back, 2 secondary) one dispatch per allocation of
// the same qualified ChangeEquip call stock46 uses, for the target a tier
// picked: A1 the manifest's base id itself (its existing protected cache
// entry, of the manifest's type), C after ANY A1 refusal a resident
// protected unreferenced stock gun (the same type and receiver first, then
// the same type, then the slot class; equipment_stock_writer_contract
// NextCandidate), D refuse and keep the slot empty. A filled slot
// is never replaced; a cache entry is never built or evicted here. One
// dispatch in flight at a time, each behind the full stock46-shape preflight
// with the target's own derived data and its own readback.
// Weapons4: tiers A1s and Cs are A1 and C of a stock cache entry other actors
// (the soldiers) already reference, never the viewer
// (partner_native_equip_shared=on|prefer; equipment_stock_writer_contract
// Target::shared).
enum class Tier : std::uint8_t { none, a1, c, d, a1s, cs };
inline const char* TierName(Tier tier) noexcept {
    switch(tier) {
    case Tier::a1: return "a1";
    case Tier::a1s: return "a1s";
    case Tier::c: return "c";
    case Tier::cs: return "cs";
    case Tier::d: return "d";
    default: return "none";
    }
}
// backoff: a transient refusal waits kRetryBackoffMs, then plans again.
enum class SlotState : std::uint8_t { idle, plan_wait, readiness, tracking, bound, queued, refused, unverified, backoff };
inline const char* SlotStateName(SlotState state) noexcept {
    switch(state) {
    case SlotState::plan_wait: return "plan_wait";
    case SlotState::readiness: return "readiness";
    case SlotState::tracking: return "tracking";
    case SlotState::bound: return "bound";
    case SlotState::queued: return "queued";
    case SlotState::refused: return "refused";
    case SlotState::unverified: return "unverified";
    case SlotState::backoff: return "backoff";
    default: return "idle";
    }
}
// partner_native_equip_shared: off (A1/C only), on (A1s/Cs after the
// unreferenced candidates of each pass; the gun profile), prefer (lab only:
// the shared candidates of each pass first).
enum class SharedMode : std::uint8_t { off, on, prefer };
inline const char* SharedModeName(SharedMode mode) noexcept {
    return mode==SharedMode::on ? "on" : mode==SharedMode::prefer ? "prefer" : "off";
}
inline constexpr unsigned kSlots=3;
// The bank entry a bound handle's resource uses (QualifiedBoundResource).
enum class Bank : std::uint8_t { none, fresh, shared };
inline const char* BankName(Bank bank) noexcept {return bank==Bank::fresh ? "fresh" : bank==Bank::shared ? "shared" : "none";}
struct SlotStatus {
    std::uint16_t want=0;       // the manifest's base id of this slot (0 unknown)
    std::uint8_t want_type=0;   // the manifest's gun type of this slot (version 2; 0 unknown)
    std::uint16_t equip=0;      // the planned equip (A1 want, C the substitute)
    std::uint32_t candidates=0; // tier C candidates tried (each logged as equip_writer=candidate)
    std::uint16_t candidate=0;  // the last one (a final refusal names it)
    Tier tier=Tier::none;
    SlotState state=SlotState::idle;
    bool optional=false;        // the target's main FPK carries the optional asset (family O) or not (N)
    std::uint8_t type=0,cache_index=0xFF;
    std::uint32_t handle=0;     // the expected handle once dispatched
    char reason[32]{};
    // Weapons4: transient refusals this gen, the entry's refs bracketing the
    // dispatch (the census refs at plan time until then), the bank entry of
    // the bound resource and the binder record words last read.
    std::uint8_t retries=0;
    std::uint32_t refs_before=0,refs_after=0;
    Bank bank=Bank::none;
    std::uint32_t binder[3]{};
};
struct ResidentEntry {
    std::uint16_t equip=0;std::uint8_t type=0,receiver=0,flags=0,index=0;std::uint32_t refs=0;
    std::uint8_t viewer=0;      // body 0's handles naming the entry (part of refs)
};
struct Status {
    bool active=false;          // an allocation is authorized
    std::uint32_t mission=0;
    bool substitute=false;      // tier C allowed (partner_native_equip=substitute)
    SharedMode shared=SharedMode::off;
    SlotStatus slots[kSlots]{};
    std::uint32_t dispatches=0,bound=0,refused=0;
    // The body lease (weapons4): its realize generation and the re-syncs it caused.
    std::uint32_t gen=0,resyncs=0,kept=0;
    bool census_ok=false;
    std::uint8_t resident_count=0;  // used entries of the 48 (the first 48 listed)
    ResidentEntry resident[48]{};
};
struct GeneralContext {
    bool enabled=false,owner=false,gates=false,body_ready=false,substitute=false,manifest=false;
    std::uint32_t mission=0,body_epoch=0;
    // The body lease (weapons4): partner_driver's Mission::realize_gen (bumped
    // at each realized transition). The motion epoch body_epoch (every warp
    // or placement) is only logged; the lease alone re-syncs the slots.
    std::uint32_t realize_gen=0;
    // Actual received manifest identity; a changed revision or slot tuple can
    // renew only cache-unavailable slots that have never dispatched.
    std::uint16_t manifest_revision=0;
    std::uint32_t manifest_epoch=0;
    // Body 1 switches or reloads (gun mode 8/11/12, rec+0x3C0 bit 7): nothing is dispatched.
    bool body_busy=false;
    SharedMode shared=SharedMode::off;
    bool measured=false;            // partner_native_equip_measured=1 (lab only)
    std::uint64_t now_ms=0;         // the retry backoff clock
    std::uintptr_t base=0,player=0,remote_block=0;
    std::size_t image_span=0;
    std::uint16_t want[kSlots]{};   // the received manifest's base ids
    std::uint16_t alias[kSlots]{};  // its aliases (0: the remote carries nothing in that slot)
    std::uint8_t type[kSlots]{};    // its gun types (version 2; 0 unknown)
    std::uint8_t receiver[kSlots]{};  // its receivers (recipe byte 0; 0 unknown)
    std::uint8_t remote_slot=0xFF;  // the remote's current slot (dispatched last), 0xFF unknown
    // Why `gates` is false (a static string: the first failing admission or owner gate).
    const char* gate_reason=nullptr;
};
// Order of the slots this allocation dispatches: the remote's current slot
// last (ChangeEquip's flags 4 leaves body 1 on the last dispatched slot).
inline void SlotOrder(std::uint8_t remote_slot,std::uint8_t (&order)[kSlots]) noexcept {
    if(remote_slot>=kSlots) {order[0]=0;order[1]=2;order[2]=1;return;}
    unsigned n=0;
    for(std::uint8_t s=0;s<kSlots;++s) if(s!=remote_slot) order[n++]=s;
    order[n]=remote_slot;
}

// --- Scheduling (weapons4; docs/native-puppet.md "Generalized writer") -----
// Pure. Before a dispatch a refusal is transient (the slot backs off
// kRetryBackoffMs, then plans again, at most kRetryBudget times per slot and
// body gen) unless PermanentReason names it (Derive/type, the viewer, the
// optional header shape, the pins, a foreign handle: tier d). At most
// kDispatchesPerGen dispatches per slot and gen, kDispatchesPerAllocation per
// slot and allocation; never a retry after a dispatch (call_unverified stays).
inline constexpr unsigned kRetryBudget=6;
inline constexpr std::uint64_t kRetryBackoffMs=2000;
inline constexpr unsigned kDispatchesPerGen=1,kDispatchesPerAllocation=2;
// The refusals a retry cannot change within a gen: what Derive reads from the
// executable tables, the plan's type and slot rules, the viewer's own
// weapons or references, the optional header shape, the pins, exhausted
// incompatible candidates, a foreign or local change. Missing cache entries
// or resident candidates can change as assets load: no_cache_entry and
// no_resident use the same bounded timer retries as other readiness waits.
inline bool PermanentReason(const char* reason) noexcept {
    if(!reason) return false;
    static constexpr const char* kPermanent[]={
        "equip_range","type_table","type","row","recipe","package_row","parts_class","dependency_tables",
        "dependency_hash","dependency_count","optional_descriptor","stock_parts_class",
        "no_base","no_manifest","type_slot","type_mismatch","cache_type","cache_unprotected","cache_shared",
        "candidate_shape","candidate_limit","want","dispatched","viewer_weapon","viewer_shared",
        "shared_refs_bound","stock_optional_shape","stock_optional_dict","stock_optional_m","stock_optional_word",
        "stock_optional_header","stock_optional_render_size","stock_optional_measure_size","full_body_gate",
        "foreign_handle","dispatch_budget","prior_local_change","local_ammo_changed","target_shape"};
    for(const char* p:kPermanent) {
        const char* a=p;const char* b=reason;
        while(*a && *a==*b) {++a;++b;}
        if(!*a && !*b) return true;
    }
    return false;
}
struct SlotSchedule {
    std::uint8_t retries=0;         // transient refusals this gen
    std::uint8_t gen_dispatches=0;  // dispatches this gen
    std::uint8_t dispatches=0;      // dispatches this allocation
    std::uint64_t retry_at=0;       // the backoff deadline (ms)
    bool visited=false;             // had a turn (the first-pass rule below)
    bool uncertain=false;           // an unverified dispatch: no further call this allocation
};
// A slot state with nothing left to do this gen.
constexpr bool FinalState(SlotState s) noexcept {
    return s==SlotState::bound || s==SlotState::queued || s==SlotState::refused || s==SlotState::unverified;
}
// Round-robin from `cursor` (a position in `order`): the next position whose
// slot has work now (idle, plan_wait, readiness, or backoff past its
// deadline); a waiting slot yields its turn to the next. The remote's current
// slot (order[2]) waits for the first pass: only once both others had a turn
// or are final (flags 4 leaves body 1 on the last dispatched slot).
// kSlots: nothing to do now.
inline unsigned PickPosition(const SlotState (&state)[kSlots],const SlotSchedule (&sched)[kSlots],
                             const std::uint8_t (&order)[kSlots],unsigned cursor,std::uint64_t now) noexcept {
    for(unsigned k=0;k<kSlots;++k) {
        const unsigned p=(cursor+k)%kSlots,s=order[p];
        if(s>=kSlots || FinalState(state[s]) || state[s]==SlotState::tracking) continue;
        if(state[s]==SlotState::backoff && now<sched[s].retry_at) continue;
        if(p==kSlots-1) {
            bool first_pass_done=true;
            for(unsigned q=0;q+1<kSlots;++q) {
                const unsigned o=order[q];
                if(o<kSlots && !sched[o].visited && !FinalState(state[o])) first_pass_done=false;
            }
            if(!first_pass_done) continue;
        }
        return p;
    }
    return kSlots;
}
// The verdict of a refusal before any dispatch of the slot this gen: back
// off (true: transient, budget left) or refuse for the gen (false).
// `permanent`: PermanentReason of the refusal (a tier C search: of all its candidates).
inline bool RetryAfter(SlotSchedule& s,bool permanent,std::uint64_t now) noexcept {
    if(s.uncertain || s.gen_dispatches || permanent || s.retries>=kRetryBudget) return false;
    ++s.retries;
    s.retry_at=now>UINT64_MAX-kRetryBackoffMs ? UINT64_MAX : now+kRetryBackoffMs;
    return true;
}
inline bool CanDispatch(const SlotSchedule& s) noexcept {
    return !s.uncertain && s.gen_dispatches<kDispatchesPerGen && s.dispatches<kDispatchesPerAllocation;
}
// A body handle is empty: 0 (a slot body 1 never had), >> 16 == 0xFFFF or equip 0.
constexpr bool EmptyHandle(std::uint32_t handle) noexcept {
    return !handle || (handle>>16)==0xFFFFu || ((handle>>5)&0x7FFu)==0;
}
// The re-sync of a slot at a body gen or identity change, from body 1's live
// handle: kept (the handle this allocation dispatched), empty (planned again),
// foreign (anything else: never replaced).
enum class Resync : std::uint8_t { kept, empty, foreign };
inline const char* ResyncName(Resync r) noexcept {return r==Resync::kept ? "kept" : r==Resync::empty ? "empty" : "foreign";}
constexpr Resync ResyncSlot(std::uint32_t dispatched,std::uint32_t live) noexcept {
    return dispatched && live==dispatched ? Resync::kept : EmptyHandle(live) ? Resync::empty : Resync::foreign;
}
// A retained owned handle proves ownership, not that the new body's model
// bound it. An interrupted observation ends queued; only qualified Readback
// may create a bound state. Existing refusal/unverified latches stay intact.
constexpr SlotState RetainedSlotState(SlotState prior) noexcept {
    return prior==SlotState::tracking ? SlotState::queued : prior;
}
// A manifest's effective writer target. Revision alone is insufficient: an
// older peer can resend new content with the same revision at a mission start.
struct ManifestKey {
    std::uint16_t revision=0;
    std::uint32_t epoch=0;
    std::uint16_t want[kSlots]{},alias[kSlots]{};
    std::uint8_t type[kSlots]{},receiver[kSlots]{};
    bool operator==(const ManifestKey& o) const noexcept {
        if(revision!=o.revision || epoch!=o.epoch) return false;
        for(unsigned s=0;s<kSlots;++s)
            if(want[s]!=o.want[s] || alias[s]!=o.alias[s] || type[s]!=o.type[s] || receiver[s]!=o.receiver[s]) return false;
        return true;
    }
    bool operator!=(const ManifestKey& o) const noexcept {return !(*this==o);}
};
inline ManifestKey ManifestOf(const GeneralContext& c) noexcept {
    ManifestKey key{};key.revision=c.manifest_revision;key.epoch=c.manifest_epoch;
    for(unsigned s=0;s<kSlots;++s) {
        key.want[s]=c.want[s];key.alias[s]=c.alias[s];key.type[s]=c.type[s];key.receiver[s]=c.receiver[s];
    }
    return key;
}
inline bool CacheReadinessReason(const char* reason) noexcept {
    if(!reason) return false;
    static constexpr const char* missing[]={"no_resident","no_cache_entry"};
    for(const char* expected:missing) {
        const char* a=expected;const char* b=reason;
        while(*a && *a==*b) {++a;++b;}
        if(!*a && !*b) return true;
    }
    return false;
}
// Only a genuine new manifest renews an exhausted missing-cache check on the
// same body. Never renew a dispatched, uncertain, owned or foreign slot, and
// never reclassify an ownership/cache-change refusal as missing readiness.
inline bool RenewCacheReadiness(SlotStatus& slot,SlotSchedule& schedule,
                               std::uint32_t dispatched,std::uint32_t live) noexcept {
    if((slot.state!=SlotState::backoff && slot.state!=SlotState::refused) || !CacheReadinessReason(slot.reason) ||
       schedule.uncertain || schedule.dispatches || schedule.gen_dispatches || slot.handle || dispatched || !EmptyHandle(live)) return false;
    schedule.retries=0;schedule.retry_at=0;schedule.visited=false;
    slot.retries=0;slot.state=SlotState::idle;slot.tier=Tier::none;slot.equip=0;
    return true;
}
// The body lease: the realize generation and the identity Capture pins
// (player, body 1, its model and binder records, the inventory, the engine).
struct BodyLease {
    std::uint32_t gen=0;
    std::uintptr_t player=0,body=0,model=0,binder=0,inventory=0,engine=0;
    bool operator==(const BodyLease& o) const noexcept {
        return gen==o.gen && player==o.player && body==o.body && model==o.model && binder==o.binder &&
            inventory==o.inventory && engine==o.engine;
    }
    bool operator!=(const BodyLease& o) const noexcept {return !(*this==o);}
};
void ObserveGeneral(const GeneralContext& context,void (*log)(const char*) noexcept) noexcept;
// A copy of the generalized writer's state (zeroed when the option is off).
void ReadStatus(Status& out) noexcept;
}
