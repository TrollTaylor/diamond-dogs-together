// Original local downed body. Exact native damage/pad binding gate supplied by
// coordinator; component traversal reuses the fingerprinted partner contract.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <cmath>
#include <cstring>
#include "mgscoop/downed_player.hpp"
#include "mgscoop/partner_input_hook.hpp"
#include "mgscoop/partner_defer.hpp"
#include "mgscoop/partner_pad_bits.hpp"
#include "partner_player_contract.hpp"
namespace mgscoop::downed_player {
namespace c = partner_player_contract;
namespace {
SRWLOCK lock = SRWLOCK_INIT;
partner_player::Addresses addresses{};
bool verified = false, stopped = false, armed = false, controlsApplied = false, failed = false;
std::uint32_t mission = 0, ownerThread = 0, baselineButtons = 0, baselineTriggers = 0;
std::uint16_t epoch = 0;
std::uintptr_t ownedP = 0, ownedQ = 0, ownedLife = 0, ownedVars = 0;
std::uint64_t lastPoll = 0, downAt = 0, lastControls = 0;
float originalHealth = 0, maximumHealth = 0, observedLife = 0;
bool configuredOnce = false, configurePending = false, pendingPassed = false;
partner_player::Addresses pendingAddresses{};
struct Hit { std::uint32_t sequence, damage; std::uint8_t cause; std::uint64_t at; };
Hit hits[8]{};
unsigned hitHead = 0, hitCount = 0;
std::uint32_t hitSerial = 0, incomingSerial = 0;
detail::Cycle cycle{};
detail::RegenCeiling friendlyRecovery{};
detail::CrawlLease crawl{};
std::uintptr_t crawlSp = 0, crawlRec = 0;
const char* lastReason = "disabled";
template<class T> bool Read(std::uintptr_t ptr, T& out) noexcept {
    SIZE_T count = 0;
    return ptr && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(ptr), &out, sizeof(out), &count) && count == sizeof(out);
}
bool In(std::uintptr_t p, std::uintptr_t begin, std::uintptr_t end) noexcept { return begin < end && p >= begin && p < end; }
// BFEFA0/BFF050 registry service -> GM+0x10 is the same exact vars bank
// registered at 998A34 (playerLife U16+0x11C). Never invoke the allocator.
bool VarsBank(std::uintptr_t& bank) noexcept {
    bank = 0;
    const auto base = addresses.image_base;
    std::uintptr_t root = 0, vt = 0, service = 0, gm = 0, global = 0;
    if (!base || addresses.vars_slot != base + c::vars_slot_rva ||
        !Read(base + c::service_root_rva, root) || !root || !Read(root, vt) || vt != base + 0x2308648 ||
        !Read(root + c::root_service, service) || !service || !Read(service + c::svc_gm, gm) || !gm ||
        !Read(gm + 0x10, bank) || bank != base + 0x2C01360 || !Read(addresses.vars_slot, global) || global != bank)
        return false;
    std::uintptr_t rootAfter = 0, serviceAfter = 0, gmAfter = 0, bankAfter = 0;
    return Read(base + c::service_root_rva, rootAfter) && rootAfter == root &&
        Read(root + c::root_service, serviceAfter) && serviceAfter == service &&
        Read(service + c::svc_gm, gmAfter) && gmAfter == gm && Read(gm + 0x10, bankAfter) && bankAfter == bank;
}
bool ReadLife(const partner_player::State& s, std::uintptr_t& record, float& current, float& max) noexcept {
    record = 0;
    if (!Read(s.Q + 8, record) || !record || (record & 3u) || !Read(record, current) || !Read(record + 4, max)) return false;
    return std::isfinite(current) && std::isfinite(max) && current > 0 && max >= 1 && max <= 65535.f && current <= max;
}
bool Live(partner_player::State& s, std::uintptr_t& record, float& current, float& max) noexcept {
    return verified && !stopped && partner_player::ReadState(addresses, s) == partner_player::Refusal::none &&
        partner_player::Realized(s, 0) && partner_player::Realized(s, 1) && s.ids[0] == 0 && s.ids[1] == 1 &&
        s.parts[0] == c::parts_loaded_state && s.parts[1] == c::parts_loaded_state && ReadLife(s, record, current, max);
}
bool Owned(partner_player::State& s, std::uintptr_t& record, float& current, float& max) noexcept {
    std::uintptr_t bank = 0;
    return armed && Live(s, record, current, max) && s.P == ownedP && s.Q == ownedQ && record == ownedLife && max == maximumHealth &&
        VarsBank(bank) && bank == ownedVars;
}
// The game compares ceil(Q.current) with vars.playerLife at 11F78F4..7902,
// then overwrites Q from the word on a mismatch. Keep both owned fields in
// agreement; max/stun/dead fields remain game-owned. Two CAS writes are not a
// cross-field atomic transaction, so a refused Q exchange conditionally rolls
// back our word. All writers here require the natural owner outside a window.
bool StoreLife(std::uintptr_t record, float value, float* capObserved = nullptr) noexcept {
    const auto windowSeq = partner_input_hook::WindowSeq();
    if (!armed || ownerThread != GetCurrentThreadId() || (windowSeq & 1u)) return false;
    partner_player::State s{}; std::uintptr_t rec = 0, bank = 0; float current = 0, max = 0;
    std::uint16_t beforeWord = 0, expectedWord = 0, afterWord = 0;
    if (!Owned(s, rec, current, max) || rec != record || !VarsBank(bank) || bank != ownedVars ||
        !detail::HealthWord(current, max, expectedWord) || !detail::HealthWord(value, max, afterWord) ||
        !Read(bank + 0x11C, beforeWord) || beforeWord != expectedWord || partner_input_hook::WindowSeq() != windowSeq) return false;
    // A recovery cap may never raise a fresher native damage result. Later
    // changes are rejected by the existing float/word compare exchanges.
    if (capObserved && value >= current) { *capObserved = current; observedLife = current; return true; }
    std::uint32_t beforeBits = 0, afterBits = 0;
    std::memcpy(&beforeBits, &current, sizeof(beforeBits)); std::memcpy(&afterBits, &value, sizeof(afterBits));
    bool changedWord = false;
    __try {
        auto* word = reinterpret_cast<volatile short*>(bank + 0x11C);
        if (_InterlockedCompareExchange16(word, static_cast<short>(afterWord), static_cast<short>(beforeWord)) != static_cast<short>(beforeWord)) return false;
        changedWord = true;
        if (_InterlockedCompareExchange(reinterpret_cast<volatile long*>(record), static_cast<long>(afterBits), static_cast<long>(beforeBits)) != static_cast<long>(beforeBits)) {
            _InterlockedCompareExchange16(word, static_cast<short>(beforeWord), static_cast<short>(afterWord)); return false;
        }
        observedLife = value; if (capObserved) *capObserved = value; return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if (changedWord) {
            __try { _InterlockedCompareExchange16(reinterpret_cast<volatile short*>(bank + 0x11C), static_cast<short>(beforeWord), static_cast<short>(afterWord)); }
            __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        return false;
    }
}
// Reuse the existing exact-owner dual health store. No native timer writes.
bool RecoverLife(std::uintptr_t record, float& current, std::uint64_t now) noexcept {
    auto next = friendlyRecovery;
    const float target = detail::RegenTarget(next, now, current, !cycle.down);
    float retained = target;
    if (target < current && !StoreLife(record, target, &retained)) return false;
    if (next.active && retained < next.life) next.life = retained;
    friendlyRecovery = next; current = retained; return true;
}
using ComponentFn = void*(*)(void*);
using StanceFn = void(*)(void*, std::uint32_t, std::uint32_t);
bool GetComponent(std::uintptr_t fn, std::uintptr_t ps, std::uintptr_t& component) noexcept {
    __try { component = reinterpret_cast<std::uintptr_t>(reinterpret_cast<ComponentFn>(fn)(reinterpret_cast<void*>(ps))); return component != 0; }
    __except(EXCEPTION_EXECUTE_HANDLER) { component = 0; return false; }
}
bool CallStance(std::uintptr_t fn, std::uintptr_t component) noexcept {
    __try { reinterpret_cast<StanceFn>(fn)(reinterpret_cast<void*>(component), 0, 3); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Prone(const partner_player::State& s, bool& observed) noexcept {
    observed = false;
    std::uintptr_t stanceArray = 0; std::uint8_t stance = 0;
    if (!Read(s.Q + c::q_anim_30, stanceArray) || !Read(stanceArray, stance)) return false;
    if (stance == 3) { observed = true; return true; }
    partner_player::Block b{};
    if (partner_player::ResolveBlock(addresses, s, 0, b) || b.stance_refusal || b.id != 0) return false;
    std::uintptr_t slot = 0, registry = 0, inner = 0, object = 0, vt = 0, lookup = 0;
    if (!Read(b.ps_vt + 0xC8, slot) || slot != addresses.ps_component || !In(slot, addresses.code_begin, addresses.code_end) ||
        !Read(b.ps + c::ps_comp, registry) || !registry || !Read(registry + c::comp_inner, inner) || !inner ||
        !Read(inner + c::comp_object, object) || !object || !Read(object, vt) || !In(vt, addresses.image_begin, addresses.image_end) ||
        !Read(vt + 0x18, lookup) || !In(lookup, addresses.code_begin, addresses.code_end)) return false;
    std::uintptr_t component = 0, componentVt = 0, request = 0;
    if (!GetComponent(slot, b.ps, component) || !Read(component, componentVt) || !In(componentVt, addresses.image_begin, addresses.image_end) ||
        !Read(componentVt + c::x_vt_request, request) || !In(request, addresses.code_begin, addresses.code_end)) return false;
    // Re-read the player and health ownership after lookup, before any call.
    partner_player::State again{}; std::uintptr_t rec = 0; float life = 0, max = 0;
    if (!Owned(again, rec, life, max) || again.P != s.P || again.Q != s.Q) return false;
    if (!CallStance(request, component)) return false;
    if (!Read(stanceArray, stance)) return false;
    observed = stance == 3;
    return true;
}
std::uint32_t CrawlBits(float value) noexcept {
    std::uint32_t bits = 0; std::memcpy(&bits, &value, sizeof(bits)); return bits;
}
// Same exact per-id service/record chain as the existing native rate mirror,
// but local0 only. No native call, pointer from Lua, timer or global speed write.
bool SlowCrawl(bool restore) noexcept {
    if (restore && !crawl.applied) return true;
    const auto seq = partner_input_hook::WindowSeq();
    if (!armed || (!restore && ownerThread != GetCurrentThreadId()) || (seq & 1u) || partner_defer::InBatch()) return false;
    partner_player::State s{}; std::uintptr_t lifeRecord = 0; float life = 0, max = 0;
    // Speed cleanup is independently owned by exact player/rate identity. A
    // changed health bank/max must not discard a still-live crawl record.
    if (restore) {
        if (partner_player::ReadState(addresses, s) != partner_player::Refusal::none) return false;
    } else if (!Owned(s, lifeRecord, life, max)) return false;
    partner_player::RateRead before{}, after{};
    if (!partner_player::ReadRates(addresses, s, 0, before) || before.id != 0) return false;
    partner_player::State again{}; std::uintptr_t againRecord = 0; float againLife = 0, againMax = 0;
    if (restore) {
        if (partner_player::ReadState(addresses, again) != partner_player::Refusal::none) return false;
    } else if (!Owned(again, againRecord, againLife, againMax)) return false;
    if (!partner_player::ReadRates(addresses, again, 0, after) || after.id != 0 ||
        partner_input_hook::WindowSeq() != seq || partner_defer::InBatch()) return false;
    const detail::CrawlIdentity first{s.P, s.Q, before.sp, before.rec};
    const detail::CrawlIdentity second{again.P, again.Q, after.sp, after.rec};
    const detail::CrawlIdentity bound = crawl.applied ? detail::CrawlIdentity{ownedP, ownedQ, crawlSp, crawlRec} : first;
    const auto owner = detail::CheckCrawlOwner(bound, true, first, true, second);
    if (owner == detail::CrawlOwner::retry) return false;
    if (owner == detail::CrawlOwner::retired) {
        // Only two coherent typed replacement identities retire the old lease.
        if (!restore) return false;
        crawl = {}; crawlSp = crawlRec = 0; return true;
    }
    // A foreign natural lifecycle thread may retire a proven replacement
    // read-only, but it never writes an unchanged old owner's rate record.
    if (ownerThread != GetCurrentThreadId()) return false;
    if (std::memcmp(before.f, after.f, sizeof(before.f)) != 0) return false;
    const auto plan = detail::PlanCrawl(crawl, before.f[0], before.f[1], before.f[2], restore);
    if (!plan.valid) return false;
    if (plan.write) {
        if (before.rec & 3u) return false; // both conditional float-word exchanges require4-byte alignment
        const auto oldBase = CrawlBits(before.f[0]), newBase = CrawlBits(plan.base);
        const auto oldProduct = CrawlBits(before.f[2]), newProduct = CrawlBits(plan.product);
        bool baseStored = false;
        __try {
            auto* base = reinterpret_cast<volatile long*>(before.rec + c::rate_first);
            auto* product = reinterpret_cast<volatile long*>(before.rec + c::rate_first + 8);
            if (_InterlockedCompareExchange(base, static_cast<long>(newBase), static_cast<long>(oldBase)) != static_cast<long>(oldBase)) return false;
            baseStored = true;
            if (_InterlockedCompareExchange(product, static_cast<long>(newProduct), static_cast<long>(oldProduct)) != static_cast<long>(oldProduct)) {
                _InterlockedCompareExchange(base, static_cast<long>(oldBase), static_cast<long>(newBase)); return false;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            if (baseStored) {
                __try { _InterlockedCompareExchange(reinterpret_cast<volatile long*>(before.rec + c::rate_first),
                    static_cast<long>(oldBase), static_cast<long>(newBase)); } __except(EXCEPTION_EXECUTE_HANDLER) {}
            }
            return false;
        }
    }
    // Commit ownership only after the complete conditional pair (or coherent no-op).
    crawl = plan.next; crawlSp = crawl.applied ? before.sp : 0; crawlRec = crawl.applied ? before.rec : 0;
    return true;
}
bool Controls(const partner_input::PadRaw& raw) noexcept {
    const auto buttons = detail::ButtonsForCause(cycle.cause);
    return buttons && (raw.pp_mask[0] & buttons) == buttons && (raw.pp_mask[2] & kBlockedTriggers) == kBlockedTriggers;
}
bool Released(const partner_input::PadRaw& raw) noexcept {
    return (raw.pp_mask[0] & detail::ButtonsForCause(cycle.cause) & ~baselineButtons) == 0 &&
        (raw.pp_mask[2] & kBlockedTriggers & ~baselineTriggers) == 0;
}
struct LocalPadRead {
    partner_player::State state{};
    std::uintptr_t life{}, pad{}, source{}, stanceArray{};
    float current{}, maximum{};
    std::uint8_t stance{};
    partner_input::PadRaw raw{};
};
bool ReadLocalPad(std::uintptr_t incoming, LocalPadRead& out) noexcept {
    std::uintptr_t vt = 0, owner = 0, sourceVt = 0;
    return Owned(out.state, out.life, out.current, out.maximum) && out.state.local == 0 &&
        Read(out.state.P + c::p_pad, out.pad) && out.pad == incoming && out.pad && !(out.pad & 3u) &&
        Read(out.pad, vt) && vt == addresses.pp_vtable &&
        Read(out.pad + c::pp_owner, owner) && owner == out.state.P &&
        Read(out.pad + c::pp_src, out.source) && out.source &&
        Read(out.source, sourceVt) && sourceVt == addresses.image_base + c::src_vtable_rva &&
        Read(out.state.Q + c::q_anim_30, out.stanceArray) && out.stanceArray &&
        Read(out.stanceArray, out.stance) && partner_player::ReadPad(addresses, out.state, out.raw);
}
bool SameLocalPad(const LocalPadRead& a, const LocalPadRead& b) noexcept {
    return a.state.P == b.state.P && a.state.Q == b.state.Q && a.life == b.life &&
        a.pad == b.pad && a.source == b.source && a.stanceArray == b.stanceArray && a.stance == b.stance &&
        CrawlBits(a.current) == CrawlBits(b.current) && CrawlBits(a.maximum) == CrawlBits(b.maximum) &&
        std::memcmp(&a.raw, &b.raw, sizeof(a.raw)) == 0;
}
bool Clear(bool restore) noexcept {
    if (restore && !SlowCrawl(true)) return false;
    if (!restore && crawl.applied) return false;
    crawl = {}; crawlSp = crawlRec = 0;
    if (restore && cycle.down && ownerThread == GetCurrentThreadId() && !(partner_input_hook::WindowSeq() & 1u)) {
        partner_player::State s{}; std::uintptr_t record = 0; float cur = 0, max = 0;
        if (Owned(s, record, cur, max) && originalHealth > 0 && originalHealth <= max) StoreLife(record, originalHealth);
    }
    armed = false; controlsApplied = false; cycle.down = false; cycle.cause = 0; friendlyRecovery = {};
    hitHead = hitCount = 0; incomingSerial = 0; observedLife = maximumHealth = 0;
    ownedP = ownedQ = ownedLife = ownedVars = 0; mission = ownerThread = 0; epoch = 0; lastPoll = downAt = lastControls = 0;
    return true;
}
bool Position(const partner_player::State& s, Reply& out) noexcept {
    if (!s.vars_ok) return false;
    for (unsigned i = 0; i < 3; ++i) {
        const double v = static_cast<double>(s.vars_pos[i]) * 1000.0;
        if (!std::isfinite(v) || v < -1000000000.0 || v > 1000000000.0) return false;
        out.pos[i] = static_cast<std::int32_t>(std::llround(v));
    }
    return true;
}
}
void FinishConfigure(const partner_player::Addresses& a, bool passed) noexcept {
    addresses = a; verified = passed && a.player_slot && a.ps_component;
    stopped = false; failed = false; configurePending = false;
    if (configuredOnce) detail::NewScene(cycle); else { cycle = {}; configuredOnce = true; }
    lastReason = verified ? "ready" : "bytes";
}
void Configure(const partner_player::Addresses& a, bool passed) noexcept {
    AcquireSRWLockExclusive(&lock);
    if (!Clear(true)) {
        // Keep the old lease until a natural owner can restore it, or establish
        // a typed replacement. Configure never silently loses cleanup authority.
        pendingAddresses = a; pendingPassed = passed; configurePending = true;
        stopped = true; lastReason = "crawl_restore_pending";
        ReleaseSRWLockExclusive(&lock); return;
    }
    FinishConfigure(a, passed);
    ReleaseSRWLockExclusive(&lock);
}
bool Control(std::uint32_t m, std::uint16_t ep, bool admitted, std::uint8_t command, std::uint32_t generation, Reply& out) noexcept {
    out = {}; out.reason = "context";
    AcquireSRWLockExclusive(&lock);
    const auto finish = [&](bool result, const char* reason) noexcept {
        lastReason = reason; out.reason = reason; ReleaseSRWLockExclusive(&lock); return result;
    };
    if (configurePending) {
        if (!Clear(true)) return finish(false, "crawl_restore_pending");
        FinishConfigure(pendingAddresses, pendingPassed);
    }
    if (command == 3) {
        // Shutdown/configure may have retired this lease already. Absence is
        // acknowledged without reading or changing another native owner.
        if (!armed) { out.applied = true; return finish(true, "cleanup_absent"); }
        if (armed && mission == m && epoch == ep && ownerThread == GetCurrentThreadId()) {
            if (!SlowCrawl(true)) return finish(false, "crawl_restore_pending");
            Clear(true); out.applied = true; return finish(true, "restored");
        }
        return finish(false, "cleanup_owner");
    }
    if (!verified || stopped || !admitted || !ep || command > 4 || (m != 30010 && m != 30020 && m != 30050))
        return finish(false, "context");
    if (armed && (mission != m || epoch != ep || ownerThread != GetCurrentThreadId())) return finish(false, "owner");
    if (failed) return finish(false, "controls_timeout");
    const auto seq = partner_input_hook::WindowSeq();
    if (seq & 1u) return finish(false, "input_window");
    partner_player::State s{}; std::uintptr_t record = 0; float cur = 0, max = 0;
    if (!Live(s, record, cur, max)) { friendlyRecovery = {}; return finish(false, "player"); }
    std::uintptr_t bank = 0; std::uint16_t bankWord = 0, healthWord = 0;
    if (!VarsBank(bank) || !detail::HealthWord(cur, max, healthWord) || !Read(bank + 0x11C, bankWord) || bankWord != healthWord)
        return finish(false, "health_mirror");
    // The public named mask stays installed during this read. Exempt only its
    // independently observed entry from our receipt, never from native input.
    if (command == 4) {
        if (!armed || generation != kReviveActionHash) return finish(false, "revive_context");
        partner_player::State owned{}; std::uintptr_t ownedRec = 0; float life = 0, limit = 0;
        if (!Owned(owned, ownedRec, life, limit) || owned.P != s.P || owned.Q != s.Q || ownedRec != record)
            return finish(false, "revive_identity");
        std::uintptr_t pp = 0, ppAfter = 0;
        detail::ButtonMaskEntry before[8]{}, after[8]{};
        partner_input::PadRaw raw{};
        if (!Read(s.P + c::p_pad, pp) || !pp || !Read(pp + c::pp_mask_tables, before) ||
            !partner_player::ReadPad(addresses, s, raw) || !Read(pp + c::pp_mask_tables, after) ||
            !Read(s.P + c::p_pad, ppAfter) || ppAfter != pp || std::memcmp(before, after, sizeof(before)) ||
            partner_input_hook::WindowSeq() != seq || !Position(s, out) || partner_input_hook::WindowSeq() != seq)
            return finish(false, "revive_input");
        std::uint32_t others = 0; bool present = false;
        if (!detail::ReviveActionMask(before, raw.pp_mask[0], others, present)) return finish(false, "revive_mask");
        raw.pp_mask[0] = others;
        out.ready = true; out.down = cycle.down; out.cause = cycle.cause; out.generation = cycle.generation;
        out.reviveMaskObserved = true; out.reviveMaskPresent = present;
        out.applied = present && !cycle.down;
        out.held = out.applied && (partner_input::Effective(raw).held & partner_pad::kAction) != 0;
        return finish(true, "revive_action");
    }
    partner_input::PadRaw raw{};
    if (!partner_player::ReadPad(addresses, s, raw) || partner_input_hook::WindowSeq() != seq || !Position(s, out))
        return finish(false, "input_pose");
    detail::IdentityChange rebound = detail::IdentityChange::none;
    if (armed) {
        const auto change = detail::CompareIdentity({ownedP, ownedQ, ownedLife, ownedVars, maximumHealth},
                                                   {s.P, s.Q, record, bank, max});
        if (change != detail::IdentityChange::none) {
            // A healthy lease can outlive a retail health/character reinitialization.
            // Retire it read-only and arm from two coherent current reads. Never
            // restore old health into another owner or migrate an active down cycle.
            partner_player::State again{}; std::uintptr_t againRec = 0, againBank = 0;
            float againCur = 0, againMax = 0; std::uint16_t againWord = 0, expectedWord = 0;
            const bool coherent = command == 0 && !cycle.down &&
                Live(again, againRec, againCur, againMax) && again.P == s.P && again.Q == s.Q &&
                againRec == record && againMax == max && VarsBank(againBank) && againBank == bank &&
                detail::HealthWord(againCur, againMax, expectedWord) && Read(bank + 0x11C, againWord) &&
                againWord == expectedWord && partner_input_hook::WindowSeq() == seq;
            if (!detail::HealthyRebind(change, cycle.down, command, coherent))
                return finish(false, detail::IdentityReason(change));
            if (!Clear(false)) return finish(false, "crawl_restore_pending");
            detail::NewScene(cycle); rebound = change;
            s = again; record = againRec; cur = againCur; max = againMax;
        }
    }
    if (!armed) {
        armed = true; mission = m; epoch = ep; ownerThread = GetCurrentThreadId();
        ownedP = s.P; ownedQ = s.Q; ownedLife = record; ownedVars = bank; maximumHealth = max;
        originalHealth = cur; observedLife = cur; baselineButtons = raw.pp_mask[0]; baselineTriggers = raw.pp_mask[2];
    }
    lastPoll = GetTickCount64();
    if (!RecoverLife(record, cur, lastPoll)) return finish(false, "hit_recovery_health");
    observedLife = cur;
    out.ready = true; out.generation = cycle.generation; out.cause = cycle.cause; out.down = cycle.down;
    out.held = (partner_input::Effective(raw).held & partner_pad::kAction) != 0;
    while (hitCount && lastPoll >= hits[hitHead].at && lastPoll - hits[hitHead].at > 1500) {
        hitHead = (hitHead + 1) % 8; --hitCount;
    }
    if (hitCount) { out.hitSequence = hits[hitHead].sequence; out.hitDamageMilli = hits[hitHead].damage; out.hitCause = hits[hitHead].cause; }
    if (command == 2) {
        if (!cycle.down || generation != cycle.generation) return finish(false, "generation");
        if (!Released(raw)) return finish(true, "restore_pending");
        if (!SlowCrawl(true)) return finish(true, "crawl_restore_pending");
        partner_player::State again{}; std::uintptr_t againRec = 0; float againCur = 0, againMax = 0;
        if (!Owned(again, againRec, againCur, againMax) || !StoreLife(againRec, detail::RecoveredHealth(cycle.cause, originalHealth, againMax))) return finish(false, "health");
        friendlyRecovery = {}; detail::Revive(cycle, generation); controlsApplied = false; downAt = lastControls = 0;
        out.down = false; out.cause = 0; out.applied = true;
        // Baseline now belongs to the live game's normal restrictions.
        baselineButtons = raw.pp_mask[0]; baselineTriggers = raw.pp_mask[2];
        return finish(true, "revived");
    }
    if (cycle.down) {
        // Check this frame first: a paused but still locked body stays down.
        // Failed requests and a later lost lock still have a bounded deadline.
        bool prone = false;
        const bool protection = Prone(s, prone) && StoreLife(record, detail::IncapacitatedHealth(cycle.cause, originalHealth, max));
        const bool masked = Controls(raw);
        // The fallback sleep body is prone but fully immobile. Do not create a
        // crawl-rate lease for sleep; the fresh native input cap below is zero.
        const bool crawlReady = protection && prone && masked && SlowCrawl(cycle.cause == 2);
        const bool applied = protection && prone && masked && crawlReady;
        const auto pendingReason = detail::ControlsPendingReason(prone, masked, crawlReady);
        if (applied) { controlsApplied = true; lastControls = lastPoll; }
        else if (detail::ControlsExpired(lastPoll, downAt, lastControls, controlsApplied, applied)) {
            if (!SlowCrawl(true)) return finish(false, "crawl_restore_pending");
            Clear(true); failed = true; out.ready = false; out.down = false; out.cause = 0;
            return finish(false, !protection ? "controls_timeout_health" : !prone ? "controls_timeout_prone" :
                !masked ? "controls_timeout_mask" : "controls_timeout_crawl");
        }
        if (!protection) return finish(false, "prone_health");
        out.applied = applied;
        return finish(true, applied ? cycle.cause == 2 ? "sleeping_prone_fallback" : "downed" : pendingReason);
    }
    out.applied = command == 0;
    return finish(true, detail::RebindReason(rebound));
}
PadObservation AfterPadUpdate(std::uintptr_t pad, bool outerUpdate, bool exactCaller) noexcept {
    PadObservation out{false, false, -1.f, -1.f, 0xffffffffu, GetCurrentThreadId(), 0, "context"};
    // Control may be inside an exact native stance/component call. Never wait
    // or recursively acquire its lock from a natural input callback; retry only
    // after the next freshly completed original update, never with old input.
    if (!TryAcquireSRWLockExclusive(&lock)) { out.reason = "busy"; return out; }
    const auto finish = [&](const char* reason) noexcept {
        out.reason = reason; ReleaseSRWLockExclusive(&lock); return out;
    };
    // Fresh means this is only reached after one complete original update.
    const auto seq = partner_input_hook::WindowSeq();
    detail::PadScope scope{true, outerUpdate, exactCaller, verified, stopped, failed, armed,
        ownerThread, out.thread, GetTickCount64(), lastPoll, seq, seq,
        partner_defer::InBatch(), partner_defer::InBatch()};
    out.generation = cycle.generation;
    if (const auto reason = detail::PadRefusal(scope)) return finish(reason);
    LocalPadRead first{}, second{};
    if (!ReadLocalPad(pad, first) || !ReadLocalPad(pad, second) || !SameLocalPad(first, second)) return finish("identity");
    scope.now = GetTickCount64(); scope.sequenceAfter = partner_input_hook::WindowSeq(); scope.batchAfter = partner_defer::InBatch();
    if (const auto reason = detail::PadRefusal(scope)) return finish(reason);
    // Observe healthy input for a demonstrable next-update reset; only the exact
    // protected downed/prone body with both named restrictions may be changed.
    if (cycle.down && controlsApplied && (first.stance != 3 || !Controls(first.raw))) return finish("controls");
    const auto plan = detail::PlanPadMagnitude(first.raw.pp_derived[0], cycle.down, controlsApplied, cycle.cause);
    if (!plan.valid) return finish("magnitude");
    if (plan.write) {
        const auto before = CrawlBits(first.raw.pp_derived[0]), after = CrawlBits(plan.magnitude);
        __try {
            if (_InterlockedCompareExchange(reinterpret_cast<volatile long*>(first.pad + c::pp_derived),
                static_cast<long>(after), static_cast<long>(before)) != static_cast<long>(before)) return finish("changed");
        } __except(EXCEPTION_EXECUTE_HANDLER) { return finish("write"); }
    }
    out.observed = true; out.capped = plan.write; out.local = 0;
    out.before = first.raw.pp_derived[0]; out.after = plan.magnitude;
    // This is an ephemeral per-frame output, not an owned input lease. Never
    // restore a cached held magnitude after release, revive, stop or timeout.
    return finish(cycle.down ? controlsApplied ? cycle.cause == 2 ? "sleeping" : "downed" : "controls_pending" : "healthy");
}
bool Scope(std::uintptr_t P, std::uint32_t& local) noexcept {
    local = 0xFFFFFFFFu;
    AcquireSRWLockShared(&lock);
    const auto now = GetTickCount64();
    partner_player::State s{}; std::uintptr_t record = 0; float cur = 0, max = 0;
    const bool ok = armed && !failed && detail::FreshScope(now, lastPoll) && P == ownedP && Owned(s, record, cur, max);
    if (ok) local = s.local;
    ReleaseSRWLockShared(&lock); return ok;
}
bool CaptureHit(std::uintptr_t P, std::uint32_t id, std::uint8_t cause) noexcept {
    AcquireSRWLockExclusive(&lock);
    const auto now = GetTickCount64();
    partner_player::State s{}; std::uintptr_t record = 0; float cur = 0, max = 0;
    if (!armed || failed || P != ownedP || id != 0 || (cause != 1 && cause != 2) || !detail::FreshScope(now, lastPoll) ||
        !Owned(s, record, cur, max)) {
        ReleaseSRWLockExclusive(&lock); return false;
    }
    // The natural damage calculation is neutralized by the caller before its
    // health/stun apply. A damage worker may differ from the Lua owner: latch
    // this positive owned life only; the next natural owner poll installs the
    // prone/mask and synchronized health without a cross-thread native store.
    if (!cycle.down) { originalHealth = cur; downAt = now; lastControls = 0; controlsApplied = false; }
    friendlyRecovery = {}; detail::Enter(cycle, cause);
    ReleaseSRWLockExclusive(&lock); return true;
}
bool IsProtected(std::uintptr_t P, std::uint32_t id) noexcept {
    AcquireSRWLockShared(&lock);
    partner_player::State s{}; std::uintptr_t record = 0; float cur = 0, max = 0;
    const bool ok = cycle.down && !failed && detail::FreshScope(GetTickCount64(), lastPoll) &&
        id == 0 && P == ownedP && Owned(s, record, cur, max);
    ReleaseSRWLockShared(&lock); return ok;
}
Snapshot GetSnapshot() noexcept {
    AcquireSRWLockShared(&lock);
    const Snapshot out{verified && !stopped, armed, cycle.down, controlsApplied,
        mission, cycle.generation, epoch, cycle.cause, lastPoll, lastControls,
        observedLife, maximumHealth, lastReason};
    ReleaseSRWLockShared(&lock); return out;
}
bool PassiveOfflineContext(std::uintptr_t P, std::uint32_t& local) noexcept {
    local = 0xFFFFFFFFu;
    AcquireSRWLockShared(&lock);
    partner_player::State s{}; std::uintptr_t record = 0; float cur = 0, max = 0;
    const bool ok = P && Live(s, record, cur, max) && s.P == P;
    if (ok) local = s.local;
    ReleaseSRWLockShared(&lock); return ok;
}
bool QueueFriendlyHit(std::uintptr_t P, float damage, std::uint8_t cause) noexcept {
    if (!std::isfinite(damage) || damage < 0 || damage > 1e6f || (cause != 0 && cause != 2)) return false;
    AcquireSRWLockExclusive(&lock);
    const auto now = GetTickCount64();
    partner_player::State s{}; std::uintptr_t record = 0; float cur = 0, max = 0;
    if (!armed || failed || P != ownedP || !detail::FreshScope(now, lastPoll) ||
        hitCount == 8 || !Owned(s, record, cur, max)) { ReleaseSRWLockExclusive(&lock); return false; }
    ++hitSerial; if (!hitSerial) ++hitSerial;
    hits[(hitHead + hitCount) % 8] = {hitSerial, static_cast<std::uint32_t>(std::llround(static_cast<double>(damage)*1000)), cause, now};
    ++hitCount; ReleaseSRWLockExclusive(&lock); return true;
}
bool FriendlyControl(std::uint32_t m, std::uint16_t ep, bool admitted,
    std::uint8_t command, std::uint32_t sequence, std::uint32_t targetGeneration,
    std::uint32_t damageMilli, std::uint8_t cause, Reply& out) noexcept {
    out = {}; out.reason = "hit_context";
    AcquireSRWLockExclusive(&lock);
    const auto finish = [&](bool result, const char* reason) noexcept {
        lastReason = reason; out.reason = reason; ReleaseSRWLockExclusive(&lock); return result;
    };
    if (!verified || stopped || failed || !admitted || !armed || !ep || m != mission || ep != epoch ||
        ownerThread != GetCurrentThreadId() || command > 1 || !sequence || damageMilli > 1000000000u ||
        (cause != 0 && cause != 2)) return finish(false, "hit_context");
    if (partner_input_hook::WindowSeq() & 1u) return finish(false, "input_window");
    partner_player::State s{}; std::uintptr_t record = 0; float cur = 0, max = 0;
    if (!Owned(s, record, cur, max) || !Position(s, out)) return finish(false, "hit_identity");
    observedLife = cur;
    out.ready = true; out.down = cycle.down; out.generation = cycle.generation; out.cause = cycle.cause;
    if (command == 0) {
        if (targetGeneration || damageMilli || cause) return finish(false, "hit_ack_args");
        if (hitCount && hits[hitHead].sequence == sequence) { hitHead = (hitHead + 1) % 8; --hitCount; }
        out.applied = true; return finish(true, "hit_ack");
    }
    const std::uint32_t delta = sequence - incomingSerial;
    if (incomingSerial && (!delta || delta > 0x7FFFFFFFu)) { out.applied = true; return finish(true, "hit_duplicate"); }
    if (targetGeneration != cycle.generation || cycle.down) {
        incomingSerial = sequence; out.applied = true; return finish(true, "hit_stale_cycle");
    }
    const auto now = GetTickCount64();
    if (!RecoverLife(record, cur, now)) return finish(false, "hit_recovery_health");
    const float amount = static_cast<float>(damageMilli) / 1000.f;
    if (cause == 2 || amount >= cur) {
        if (!StoreLife(record, detail::IncapacitatedHealth(cause == 2 ? 2 : 1, cur, max))) return finish(false, "hit_health");
        originalHealth = cur; downAt = GetTickCount64(); lastControls = 0; controlsApplied = false;
        friendlyRecovery = {}; detail::Enter(cycle, cause == 2 ? 2 : 1);
        out.down = true; out.generation = cycle.generation; out.cause = cycle.cause;
    } else if (amount > 0) {
        float remaining = cur - amount;
        if (!StoreLife(record, remaining, &remaining)) return finish(false, "hit_health");
        friendlyRecovery = {now, remaining, true};
    }
    incomingSerial = sequence;
    out.applied = true; return finish(true, "hit_applied");
}
void Shutdown() noexcept {
    AcquireSRWLockExclusive(&lock);
    if (!Clear(true)) lastReason = "crawl_restore_pending";
    stopped = true; ReleaseSRWLockExclusive(&lock);
}
} // namespace mgscoop::downed_player
