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
#include "mgscoop/worker_scenario.hpp"
#include "mgscoop/early_patches.hpp"
#include "sleep_body0_lease.hpp"
#include "sleep_loaded_requests.hpp"
#include "sleep_graph_read.hpp"
#include "sleep_worker_controls.hpp"
#include "sleep_body0_transition_read.hpp"
#include "sleep_body0_rest.hpp"
#include "mgscoop/native_pose_window.hpp"
#include "mgscoop/coop_sleep.hpp"
#include "stock_sleep_release_policy.hpp"
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
// MGO's down rules give a revived player a short InvincibleTime.
constexpr std::uint64_t kReviveGraceMs = 3000;
std::uint64_t revivedAt = 0;
float originalHealth = 0, maximumHealth = 0, observedLife = 0;
bool configuredOnce = false, configurePending = false, pendingPassed = false, shutdownPending = false;
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

namespace sl=sleep_body0_lease;
namespace sg=sleep_graph_read;
namespace sc=sleep_worker_controls;
namespace st=sleep_body0_transition_read;namespace sr=sleep_body0_rest;
bool nativeConfigured=false,nativeGatePassed=false,nativeEnabled=false,nativePrepared=false,nativeAbort=false;
sl::Lease nativeLease{};
sc::Receipt nativePad{};
std::uint64_t nativePadSerial=0;
std::uintptr_t nativeGraphLifetime=0,nativeGraphMog=0;
st::Observation nativeTransition{};
std::uint64_t nativeRestPendingAt=0;
NativeRestDiagnostics nativeRestDiagnostics{};
const char* nativeReadReason="sleep_native_read_context";
sl::Sample nativeLastSample{};std::uint32_t nativeLastSequence=0;std::uint64_t nativeLastSampleAt=0;
bool NativeActive() noexcept {return nativePrepared||nativeLease.phase!=sl::Phase::empty;}
namespace ssr=stock_sleep_release_policy;
ssr::Release stockRelease{};
ssr::Episode StockEpisode() noexcept{return {mission,cycle.generation,epoch};}
bool StockReleasePending() noexcept{return stockRelease.Pending(StockEpisode(),cycle.down,cycle.cause);}
bool SleepActive() noexcept {return NativeActive()||coop_sleep::Active(0)||StockReleasePending();}
void NativeFlags(Reply& r) noexcept {r.nativeSleepActive=NativeActive();r.nativeSleepReleaseReady=false;}
bool NativeWorker() noexcept {return worker_scenario::Compiled()&&worker_scenario::Active()&&worker_scenario::ThreadAdmitted()&&lab::SavesRedirected();}
bool NativeCas(std::uintptr_t at,std::uint32_t expected,std::uint32_t desired,const sl::Scope& sampled) noexcept {
 // Check immediately before the actual write. Success always reports that
 // actual CAS receipt, even if a subsequent window check refuses progression.
 const auto seq=partner_input_hook::WindowSeq();
 if(!at||(at&3u)||!verified||!nativeGatePassed||!armed||stopped||
    !sc::CasWindow(sampled,ownerThread,GetCurrentThreadId(),seq,partner_defer::InBatch(),NativeWorker()))return false;
 __try {return static_cast<std::uint32_t>(_InterlockedCompareExchange(reinterpret_cast<volatile long*>(at),static_cast<long>(desired),static_cast<long>(expected)))==expected;}
 __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool SameNativeState(const partner_player::State& a,const partner_player::State& b) noexcept {
 if(a.P!=b.P||a.Q!=b.Q||a.S!=b.S||a.exists!=b.exists||a.local_bits!=b.local_bits||a.slots!=b.slots||a.sync!=b.sync||a.realizer!=b.realizer||a.flags!=b.flags||a.count!=b.count||a.realized_count!=b.realized_count||a.local!=b.local||a.rbits!=b.rbits||a.s0!=b.s0||a.s8!=b.s8)return false;
 for(unsigned k=0;k<2;++k)if(a.ids[k]!=b.ids[k]||a.pool[k]!=b.pool[k]||a.parts[k]!=b.parts[k])return false;
 return a.count==2&&a.local==0&&(a.flags&1u)&&!(a.flags&0x100u)&&!a.sync;
}
bool NamedMasks(const LocalPadRead& p) noexcept {
 sc::Masks a{},b{};
 if(!Read(p.pad+c::pp_mask_tables,a.table)||!Read(p.pad+c::pp_masks,a.aggregate)||!Read(p.pad+c::pp_mask_tables,b.table)||!Read(p.pad+c::pp_masks,b.aggregate))return false;
 return std::memcmp(&a,&b,sizeof(a))==0&&std::memcmp(a.aggregate,p.raw.pp_mask,sizeof(a.aggregate))==0&&sc::NamedFull(a);
}
sc::Receipt PadIdentity(const LocalPadRead& p,std::uint32_t seq) noexcept {
 return {p.state.P,p.state.Q,p.life,p.pad,p.source,cycle.generation,GetCurrentThreadId(),seq,0,0,false};
}
struct NativeRead {
 sl::Scope scope{};sl::Sample sample{};sg::Observation graph{};LocalPadRead pad{};
 bool requestsPresent=false,zeroCap=false;st::Observation transition{};sl::ObservationProof observation{};
 sr::Descriptor restDescriptor{};bool restDescriptorReady=false;
};
bool ReadNative(bool admitted,bool scanRequests,bool requireMasks,NativeRead& out,bool cleanupRefresh=false) noexcept {
 out={};const auto seq=partner_input_hook::WindowSeq();const auto now=GetTickCount64();nativeReadReason="sleep_native_read_context";
 if(!nativeGatePassed||!verified||ownerThread!=GetCurrentThreadId()||!armed||!cycle.down||cycle.cause!=2||!NativeWorker()||(seq&1u)||partner_defer::InBatch())return false;
 partner_player::State first{},second{},after{};std::uintptr_t rec=0,rec2=0;float cur=0,max=0,cur2=0,max2=0;
 nativeReadReason="sleep_native_read_pair";
 if(!Owned(first,rec,cur,max)||!Owned(second,rec2,cur2,max2)||!SameNativeState(first,second)||rec!=rec2||sl::Bits(cur)!=sl::Bits(cur2)||sl::Bits(max)!=sl::Bits(max2))return false;
 std::uintptr_t pp=0;nativeReadReason="sleep_native_read_pad";
 if(!Read(second.P+c::p_pad,pp)||!ReadLocalPad(pp,out.pad))return false;
 const bool named=NamedMasks(out.pad);nativeReadReason="sleep_native_read_named_masks";if(requireMasks&&!named)return false;
 sleep_plugin_read::Scope gs{};gs.exactGate=true;gs.admitted=true; // read-only retained actual two-body ownership; no new admission
 gs.ready=verified&&!stopped;gs.armed=armed;gs.image=addresses.image_base;gs.player=second.P;gs.data=second.Q;gs.state=second.S;
 gs.mission=gs.ownedMission=mission;gs.epoch=gs.ownedEpoch=epoch;gs.generation=gs.ownedGeneration=cycle.generation;gs.thread=gs.ownerThread=ownerThread;gs.now=now;gs.poll=cleanupRefresh?now:lastPoll;gs.sequenceBefore=gs.sequenceAfter=seq;
 auto reader=[](std::uintptr_t at,void* target,std::size_t size) noexcept {SIZE_T n=0;return sleep_plugin_read::Range(at,size)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),target,size,&n)&&n==size;};
 sg::Observation ga{},gb{},gc{};st::Observation ta{},tb{};
 sr::Descriptor da{},db{},dc{};bool ra=false,rb=false,rc=false;
 if(!sg::ReadOnce(reader,gs,ga,&nativeReadReason))return false;
 if(ga.owner.body[0].mode==5)ra=sr::ReadDescriptor(reader,gs,ga.owner.body[0],da);
 if(!sg::ReadOnce(reader,gs,gb,&nativeReadReason))return false;
 if(gb.owner.body[0].mode==5)rb=sr::ReadDescriptor(reader,gs,gb.owner.body[0],db);
 nativeReadReason="sleep_native_read_graph_pair";if(!sg::Same(ga,gb))return false;
 if(!st::ReadLocalOwnedOnce(reader,gs.image,gs.player,gb.graph[0].entry,ta,&nativeReadReason,requireMasks))return false;
 nativeReadReason="sleep_native_read_requests";
 if(scanRequests){sleep_loaded_requests::Reply requests{};if(!sleep_loaded_requests::Read(reader,gb.graph[0],requests)||!requests.ready)return false;out.requestsPresent=requests.found==0xffff;}
 float stun=0,stunAgain=0,stunMax=0,stunMaxAgain=0;nativeReadReason="sleep_native_read_stun";
 if(!Read(rec+0x2c,stun)||!Read(rec+0x30,stunMax))return false;
 if(!st::ReadLocalOwnedOnce(reader,gs.image,gs.player,gb.graph[0].entry,tb,&nativeReadReason,requireMasks))return false;
 nativeReadReason="sleep_native_read_transition_pair";if(!st::Same(ta,tb))return false;
 if(!sg::ReadOnce(reader,gs,gc,&nativeReadReason))return false;
 if(gc.owner.body[0].mode==5)rc=sr::ReadDescriptor(reader,gs,gc.owner.body[0],dc);
 nativeReadReason="sleep_native_read_graph_after";if(!sg::Same(gb,gc))return false;
 nativeReadReason="sleep_native_read_stun_after";
 if(!Read(rec+0x2c,stunAgain)||!Read(rec+0x30,stunMaxAgain)||sl::Bits(stun)!=sl::Bits(stunAgain)||sl::Bits(stunMax)!=sl::Bits(stunMaxAgain))return false;
 LocalPadRead padAfter{};nativeReadReason="sleep_native_read_identity_after";
 if(!ReadLocalPad(pp,padAfter)||!SameLocalPad(out.pad,padAfter))return false;
 const bool namedAfter=NamedMasks(padAfter);
 if((requireMasks&&!namedAfter)||!Owned(after,rec2,cur2,max2)||!SameNativeState(second,after)||rec2!=rec||sl::Bits(cur2)!=sl::Bits(cur)||sl::Bits(max2)!=sl::Bits(max)||partner_input_hook::WindowSeq()!=seq||partner_defer::InBatch())return false;
 const auto& b=gc.owner.body[0];const auto& g=gc.graph[0];nativeReadReason="sleep_native_read_body0";
 if(b.id||b.lifeRecord!=rec||tb.pad!=padAfter.pad||tb.source!=padAfter.source)return false;
 nativeReadReason="sleep_native_read_retained_lease";
 if(nativeLease.phase!=sl::Phase::empty&&(g.lifetime!=nativeGraphLifetime||g.mog!=nativeGraphMog||
    tb.ui!=nativeTransition.ui||tb.cam!=nativeTransition.cam||tb.pad!=nativeTransition.pad||tb.source!=nativeTransition.source||tb.selector!=nativeTransition.selector||
    tb.request!=nativeTransition.request||tb.records!=nativeTransition.records||tb.parameterCount!=nativeTransition.parameterCount||tb.requestStride!=nativeTransition.requestStride||tb.requestLayers!=nativeTransition.requestLayers))return false;
 const auto completed=GetTickCount64();nativeReadReason="sleep_native_read_fresh";
 if(completed<now||completed-now>sl::kFreshMs||(!cleanupRefresh&&!detail::FreshScope(completed,lastPoll)))return false;
 out.sample.owner={second.P,second.Q,rec,b.block,b.ps,b.primary,b.node,b.records,b.bitmap,g.cs,g.controller,mission,cycle.generation,ownerThread,epoch};
 out.sample.health=cur;out.sample.maximumHealth=max;out.sample.stun=stun;out.sample.maximumStun=stunMax;out.sample.lifeState=b.lifeState;
 out.sample.mode=b.mode;out.sample.requested=b.requested;out.sample.action=b.action;out.sample.stance=padAfter.stance;out.sample.cause=cycle.cause;out.sample.qAction=b.qAction;
 out.sample.nodeActive=b.nodeActive;out.sample.graphCurrent=g.currentNode;out.sample.graphSleepCandidate=g.sleepHashCandidate;
 const auto finalSeq=partner_input_hook::WindowSeq();const bool finalBatch=partner_defer::InBatch();
 if(requireMasks)out.observation=sc::RetainedObservation(nativePad,PadIdentity(padAfter,seq),sc::RetentionInputs{out.pad.raw.pp_derived[0],padAfter.raw.pp_derived[0],named,namedAfter,seq,finalSeq,false,finalBatch,completed});
 out.graph=gc;out.pad=padAfter;out.transition=tb;out.zeroCap=sc::FreshZero(nativePad,PadIdentity(padAfter,seq),completed);
 out.restDescriptorReady=ra&&rb&&rc&&sr::Same(da,db)&&sr::Same(db,dc);out.restDescriptor=out.restDescriptorReady?dc:sr::Descriptor{};
 out.scope.exactRetailGate=verified;out.scope.sleepCodeGate=out.scope.seedCodeGate=nativeGatePassed;out.scope.workerActive=worker_scenario::Active();out.scope.threadAdmitted=worker_scenario::ThreadAdmitted();out.scope.copiedSave=lab::SavesRedirected();out.scope.coherent=true;out.scope.admitted=admitted;out.scope.pairedOffline=admitted;
 out.scope.namedButtonsLocked=out.scope.namedTriggersLocked=named&&namedAfter;out.scope.analogCapQualified=out.zeroCap;out.scope.thread=ownerThread;out.scope.sequenceBefore=seq;out.scope.sequenceAfter=finalSeq;out.scope.batchBefore=false;out.scope.batchAfter=finalBatch;out.scope.now=completed;out.scope.poll=cleanupRefresh?completed:lastPoll;
 if(cleanupRefresh)lastPoll=completed; // only after the complete current-owner typed proof
 nativeLastSample=out.sample;nativeLastSequence=seq;nativeLastSampleAt=completed;
 nativeReadReason="sleep_native_read_ready";return true;
}
void NativeLabAbort(const char* reason) noexcept {
 nativeAbort=true;lastReason=reason;
 // Refuse delivery and let the existing owned hidden supervisor perform its
 // process cleanup/finally. Never release masks or forget a partial write.
 if(worker_scenario::Active())worker_scenario::Refuse(reason);
}
void NativeWatchdog() noexcept {
 if(!NativeActive()||nativeAbort)return;
 if(sc::PendingExpired(nativeLease,nativePrepared,downAt,GetTickCount64()))
  NativeLabAbort(nativeLease.phase==sl::Phase::empty?"sleep_native_prepare_abort":"sleep_native_pending_abort");
}
const char* ZeroPendingReason(const NativeRead& r) noexcept {
 if(!nativePad.zero||!nativePad.serial)return "sleep_zero_receipt_missing";
 if(nativePad.sequence!=r.scope.sequenceBefore)return "sleep_zero_window_changed";
 if(r.scope.now<nativePad.at||r.scope.now-nativePad.at>sl::kFreshMs)return "sleep_zero_receipt_stale";
 return "sleep_zero_owner_changed";
}
bool SameNativeSample(const sl::Sample& a,const sl::Sample& b) noexcept {
 return sl::Same(a.owner,b.owner)&&sl::Bits(a.health)==sl::Bits(b.health)&&sl::Bits(a.maximumHealth)==sl::Bits(b.maximumHealth)&&sl::Bits(a.stun)==sl::Bits(b.stun)&&sl::Bits(a.maximumStun)==sl::Bits(b.maximumStun)&&a.lifeState==b.lifeState&&a.mode==b.mode&&a.requested==b.requested&&a.action==b.action&&a.stance==b.stance&&a.cause==b.cause&&a.qAction==b.qAction&&a.nodeActive==b.nodeActive&&a.graphCurrent==b.graphCurrent&&a.graphSleepCandidate==b.graphSleepCandidate;
}
bool NativeReleaseProof(bool cleanupRefresh=false) noexcept {
 if(nativeAbort||nativeLease.phase!=sl::Phase::retired||!nativeLease.wakeAuthorized)return false;
 NativeRead a{},b{};
 if(!ReadNative(false,false,false,a,cleanupRefresh)||!ReadNative(false,false,false,b,cleanupRefresh))return false;
 if(!SameNativeSample(a.sample,b.sample)||!sg::Same(a.graph,b.graph)||!st::Same(a.transition,b.transition)||a.scope.sequenceBefore!=b.scope.sequenceBefore){nativeReadReason="sleep_native_release_pair";return false;}
 if(sl::Same(nativeLease.owner,b.sample.owner)&&!sl::RetiredSample(b.sample)){NativeLabAbort("sleep_native_retired_drift");return false;}
 if(!sc::ReleaseSample(nativeLease,a.scope,a.sample)||!sc::ReleaseSample(nativeLease,b.scope,b.sample)){nativeReadReason="sleep_native_release_guard";return false;}
 nativeLease.lastSeen=b.scope.now;return true;
}
bool NativeMaintainRest(bool admitted,bool wake,bool cleanupRefresh,NativeRead& current) noexcept {
 if(wake||cleanupRefresh||!nativeEnabled||nativeLease.phase!=sl::Phase::active||!nativeLease.nativeEntered||current.sample.mode!=5)return true;
 if(nativeRestDiagnostics.checks!=UINT64_MAX)++nativeRestDiagnostics.checks;
 nativeRestDiagnostics.sampledAt=current.scope.now;nativeRestDiagnostics.sampleSequence=current.scope.sequenceAfter;
 nativeRestDiagnostics.generation=cycle.generation;nativeRestDiagnostics.stun=current.sample.stun;nativeRestDiagnostics.maximumStun=current.sample.maximumStun;
 nativeRestDiagnostics.descriptorObserved=current.restDescriptorReady;nativeRestDiagnostics.descriptorFlags=current.restDescriptor.flags;
 const auto pose=partner_input_hook::NativePoseWindowStatus();
 // The existing CarryHeld getter is writer-only. Outside that writer, refuse
 // any actual peer hold instead of interpreting its false value as no Carry.
 const bool blocked=pose.held||pose.owned||pose.faulted;
 const auto canonical=armed&&!stopped&&cycle.down&&cycle.cause==2&&cycle.generation==nativeLease.owner.generation;
 if(!current.restDescriptorReady){nativeRestDiagnostics.reason="sleep_rest_body0_descriptor_unreadable";nativeRestDiagnostics.stage=1;NativeLabAbort("sleep_rest_body0_descriptor_unreadable");return false;}
 const auto hash=current.graph.graph[0].stateHash;
 const auto plan=sr::Maintain(current.scope,current.sample,nativeLease,hash,current.restDescriptor,true,canonical,wake,blocked);
 nativeRestDiagnostics.reason=plan.reason;
 if(!plan.valid){
  if(std::strcmp(plan.reason,"sleep_rest_body0_zero_pending")==0&&current.observation.retainedNaturalZero){
   nativeRestDiagnostics.stage=2;
   if(!nativeRestPendingAt)nativeRestPendingAt=current.scope.now;
   if(const auto expired=sr::PendingDeadline(nativeRestPendingAt,current.scope.now)){nativeRestDiagnostics.reason=expired;nativeRestDiagnostics.stage=4;NativeLabAbort(expired);return false;}
   nativeReadReason=plan.reason;return true; // readonly retention only; no write or clock refresh
  }
  nativeRestDiagnostics.stage=3;NativeLabAbort(plan.reason);return false;
 }
 if(const auto expired=sr::PendingDeadline(nativeRestPendingAt,current.scope.now)){nativeRestDiagnostics.reason=expired;nativeRestDiagnostics.stage=4;NativeLabAbort(expired);return false;}
 if(!plan.count){
  const auto zero=sr::ObserveZero(current.zeroCap,current.observation.retainedNaturalZero);
  if(!zero.valid){nativeRestDiagnostics.reason="sleep_rest_body0_zero_receipt";nativeRestDiagnostics.stage=5;NativeLabAbort("sleep_rest_body0_zero_receipt");return false;}
  if(zero.clearPending)nativeRestPendingAt=0;
  nativeRestDiagnostics.stage=10;return true; // weak readonly zero never creates or renews a pending deadline
 }
 NativeRead fresh{};
 if(!ReadNative(admitted,false,true,fresh)||!fresh.restDescriptorReady||!SameNativeSample(current.sample,fresh.sample)||
 !sg::Same(current.graph,fresh.graph)||!st::Same(current.transition,fresh.transition)||!sr::Same(current.restDescriptor,fresh.restDescriptor)){
  nativeRestDiagnostics.reason="sleep_rest_body0_before_changed";nativeRestDiagnostics.stage=6;NativeLabAbort("sleep_rest_body0_before_changed");return false;
 }
 const auto again=partner_input_hook::NativePoseWindowStatus();
 const auto next=sr::Maintain(fresh.scope,fresh.sample,nativeLease,fresh.graph.graph[0].stateHash,fresh.restDescriptor,true,
 canonical,wake,again.held||again.owned||again.faulted);
 if(!next.valid||next.count!=1||next.changes[0].offset!=0x2c||next.changes[0].desired!=sl::Bits(0.f)||next.changes[0].expected!=plan.changes[0].expected||
 !NativeCas(fresh.sample.owner.life+0x2c,next.changes[0].expected,next.changes[0].desired,fresh.scope)){
  nativeRestDiagnostics.reason="sleep_rest_body0_cas_refused";nativeRestDiagnostics.stage=7;NativeLabAbort("sleep_rest_body0_cas_refused");return false;
 }
 // Count the actual successful write immediately, even if its post-read refuses.
 if(nativeRestDiagnostics.successfulCas!=UINT64_MAX)++nativeRestDiagnostics.successfulCas;
 NativeRead after{};
 if(!ReadNative(admitted,false,true,after)||!after.restDescriptorReady||!sr::SameSample(fresh.sample,after.sample,true)||
 !st::Same(fresh.transition,after.transition)||!sr::Same(fresh.restDescriptor,after.restDescriptor)||fresh.graph.graph[0].stateHash!=after.graph.graph[0].stateHash){
  nativeRestDiagnostics.reason="sleep_rest_body0_after_changed";nativeRestDiagnostics.stage=8;NativeLabAbort("sleep_rest_body0_after_changed");return false;
 }
 const auto completed=partner_input_hook::NativePoseWindowStatus();
 const auto restPost=sr::Maintain(after.scope,after.sample,nativeLease,after.graph.graph[0].stateHash,after.restDescriptor,true,
 canonical,wake,completed.held||completed.owned||completed.faulted);
 if(!restPost.valid||restPost.count){nativeRestDiagnostics.reason="sleep_rest_body0_zero_unproved";nativeRestDiagnostics.stage=9;NativeLabAbort("sleep_rest_body0_zero_unproved");return false;}
 current=after;nativeRestPendingAt=0;nativeRestDiagnostics.reason="sleep_rest_body0_maintained";nativeRestDiagnostics.stage=11;return true;
}
bool NativeAdvance(bool admitted,bool wake,Reply& out,bool cleanupRefresh=false) noexcept {
 NativeFlags(out);if(!NativeActive())return false;
 out.down=true;out.cause=2;out.generation=cycle.generation;out.applied=false;
 if(nativeAbort){out.reason="sleep_native_lab_abort_required";return true;}
 // An authorized retired owner can be polled while Lua is removing masks.
 // Revalidate current native retirement directly; Observe requires real masks.
 if(nativeLease.phase==sl::Phase::retired&&nativeLease.wakeAuthorized){
  if(NativeReleaseProof(cleanupRefresh)){out.nativeSleepReleaseReady=true;out.applied=true;out.reason="sleep_native_retired";}
  else {const auto now=GetTickCount64();if(now>=nativeLease.lastSeen&&now-nativeLease.lastSeen>sl::kRetireMs)NativeLabAbort("sleep_native_release_abort");out.reason=nativeAbort?"sleep_native_lab_abort_required":nativeReadReason;}
  return true;
 }
 NativeRead read{};
 if(!ReadNative(admitted,nativeLease.phase==sl::Phase::empty,true,read,cleanupRefresh)){
  NativeWatchdog();out.reason=nativeAbort?"sleep_native_lab_abort_required":nativeReadReason;return true;
 }
 if(nativeLease.phase==sl::Phase::empty){
  if(wake&&sl::RetiredSample(read.sample)){nativeLease=sl::RetireEmpty(read.sample,read.scope.now);nativeGraphLifetime=read.graph.graph[0].lifetime;nativeGraphMog=read.graph.graph[0].mog;nativeTransition=read.transition;}
  else {
   const auto plan=sl::Seed(read.scope,read.sample,nativeLease,read.requestsPresent);
   if(!plan.valid){
    if(read.scope.now>=downAt&&read.scope.now-downAt>sl::kAcquireMs)NativeLabAbort("sleep_native_prepare_abort");
    out.reason=nativeAbort?"sleep_native_lab_abort_required":!read.zeroCap?ZeroPendingReason(read):plan.reason;return true;
   }
   if(!NativeCas(read.sample.owner.life+plan.changes[0].offset,plan.changes[0].expected,plan.changes[0].desired,read.scope)){
    if(read.scope.now>=downAt&&read.scope.now-downAt>sl::kAcquireMs)NativeLabAbort("sleep_native_prepare_abort");
    out.reason=nativeAbort?"sleep_native_lab_abort_required":"sleep_seed_first_cas_changed";return true;
   }
   // Commit repair ownership immediately after the first actual write, before
   // the second read/CAS can fault or refuse.
   nativeLease=sl::Commit(read.sample,read.scope.now,true,false);nativeGraphLifetime=read.graph.graph[0].lifetime;nativeGraphMog=read.graph.graph[0].mog;nativeTransition=read.transition;
   NativeRead between{};
   const bool current=ReadNative(admitted,false,true,between,cleanupRefresh)&&sl::Same(nativeLease.owner,between.sample.owner)&&between.zeroCap&&sl::Repair(between.scope,between.sample,nativeLease).valid;
   if(current&&NativeCas(between.sample.owner.life+plan.changes[1].offset,plan.changes[1].expected,plan.changes[1].desired,between.scope))nativeLease.phase=sl::Phase::seeded;
  }
 }
 if(nativeLease.phase==sl::Phase::quarantined){
  NativeRead repair{};bool repaired=false;
  if(ReadNative(false,false,true,repair,cleanupRefresh)){const auto plan=sl::Repair(repair.scope,repair.sample,nativeLease);if(plan.valid&&plan.count==1)repaired=NativeCas(repair.sample.owner.life+plan.changes[0].offset,plan.changes[0].expected,plan.changes[0].desired,repair.scope);}
  sl::MarkRepaired(nativeLease,repaired);
  // Even an exact rollback is a failed experiment. Keep this cycle locked and
  // require copied-process cleanup; never drop ownership on a partial failure.
  NativeLabAbort(repaired?"sleep_seed_partial_repaired_abort":"sleep_seed_partial_unrepaired_abort");out.reason="sleep_native_lab_abort_required";return true;
 }
 if(wake&&!sl::RequestWake(nativeLease,cycle.generation,read.scope.now)){out.reason="sleep_wake_generation";return true;}
 if(nativeLease.phase==sl::Phase::wakePending){
  NativeRead latest{};if(ReadNative(false,false,true,latest,cleanupRefresh)){const auto plan=sl::Wake(latest.scope,latest.sample,nativeLease);
   if(plan.valid&&plan.count==1&&!NativeCas(latest.sample.owner.life+plan.changes[0].offset,plan.changes[0].expected,plan.changes[0].desired,latest.scope)){NativeWatchdog();out.reason=nativeAbort?"sleep_native_lab_abort_required":"sleep_wake_cas_changed";return true;}
  }
 }
 NativeRead latest{};if(!ReadNative(admitted&&!wake&&!cleanupRefresh,false,true,latest,cleanupRefresh)){NativeWatchdog();out.reason=nativeAbort?"sleep_native_lab_abort_required":nativeReadReason;return true;}
 if(!NativeMaintainRest(admitted,wake,cleanupRefresh,latest)){out.reason="sleep_native_lab_abort_required";return true;}
 const auto receipt=sl::Observe(nativeLease,latest.scope,latest.sample,latest.observation);if(receipt.labAbortRequired)NativeLabAbort(receipt.reason);
 out.nativeSleepActive=true;
 // Retirement is never an unlock receipt without an independent typed pair.
 out.nativeSleepReleaseReady=receipt.retired&&!nativeAbort&&NativeReleaseProof(cleanupRefresh);
 out.applied=!nativeAbort&&(wake?out.nativeSleepReleaseReady:(latest.zeroCap||latest.observation.retainedNaturalZero)&&Controls(latest.pad.raw));
 out.reason=nativeAbort?"sleep_native_lab_abort_required":!latest.zeroCap&&!latest.observation.retainedNaturalZero?latest.observation.reason:receipt.reason;return true;
}
bool NativeRetireForClear() noexcept {
 if(!NativeActive())return true;
 if(nativeLease.phase==sl::Phase::empty)return ownerThread==GetCurrentThreadId()&&!(partner_input_hook::WindowSeq()&1u)&&!partner_defer::InBatch(); // no seed write; retain pending flag until Clear succeeds
 if(ownerThread!=GetCurrentThreadId()||nativeAbort)return false;
 Reply out{};NativeAdvance(false,true,out,true);return out.nativeSleepReleaseReady&&NativeReleaseProof(true);
}
void ForgetNativeRetired() noexcept {
 nativeLease={};nativeRestPendingAt=0;nativePrepared=nativeAbort=false;nativePad={};nativeGraphLifetime=nativeGraphMog=0;nativeTransition={};
}

bool Clear(bool restore) noexcept {
    const bool nativeWasActive = NativeActive();
    // Never erase a live/partial native seed, or restore HP while it is asleep.
    if (nativeWasActive && (!restore || !NativeRetireForClear())) return false;
    // The normal stock sleep owns its wake until the native graph is idle.
    // Configure/stop must not discard that duty or restore HP into a sleeper.
    if (coop_sleep::Active(0) && (!restore || !coop_sleep::Hold(0, false))) return false;
    if (restore && !SlowCrawl(true)) return false;
    if (!restore && crawl.applied) return false;
    crawl = {}; crawlSp = crawlRec = 0;
    if (restore && cycle.down && ownerThread == GetCurrentThreadId() && !(partner_input_hook::WindowSeq() & 1u)) {
        partner_player::State s{}; std::uintptr_t record = 0; float cur = 0, max = 0;
        if (nativeWasActive) {
            if (!Owned(s, record, cur, max) || originalHealth <= 0 || originalHealth > max || !StoreLife(record, originalHealth)) return false;
        } else if (Owned(s, record, cur, max) && originalHealth > 0 && originalHealth <= max) StoreLife(record, originalHealth);
    }
    armed = false; controlsApplied = false; cycle.down = false; cycle.cause = 0; friendlyRecovery = {};
    stockRelease.LogicalExit(true);
    hitHead = hitCount = 0; incomingSerial = 0; observedLife = maximumHealth = 0;
    ownedP = ownedQ = ownedLife = ownedVars = 0; mission = ownerThread = 0; epoch = 0; lastPoll = downAt = lastControls = revivedAt = 0;
    ForgetNativeRetired();
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
    stopped = false; failed = false; configurePending = false; shutdownPending = false; nativeEnabled = false;
    if (configuredOnce) detail::NewScene(cycle); else { cycle = {}; configuredOnce = true; }
    lastReason = verified ? "ready" : "bytes";
}
void Configure(const partner_player::Addresses& a, bool passed) noexcept {
    AcquireSRWLockExclusive(&lock);
    if (!Clear(true)) {
        // Keep the old lease until a natural owner can restore it, or establish
        // a typed replacement. Configure never silently loses cleanup authority.
        pendingAddresses = a; pendingPassed = passed; configurePending = true;
        // A held native sleep must keep fresh natural cap callbacks alive.
        // Only the old exact owner may complete retirement and configuration.
        stopped = !SleepActive(); lastReason = SleepActive() ? "sleep_native_retirement_pending" : "crawl_restore_pending";
        ReleaseSRWLockExclusive(&lock); return;
    }
    FinishConfigure(a, passed);
    ReleaseSRWLockExclusive(&lock);
}
void ConfigureNativeSleepTest(bool passed) noexcept {
    AcquireSRWLockExclusive(&lock);
    // This secondary gate is process-owned and cannot be re-opened by Lua.
    if (!nativeConfigured) { nativeConfigured = true; nativeGatePassed = passed; }
    ReleaseSRWLockExclusive(&lock);
}
bool SetNativeSleepTest(bool enabled) noexcept {
    AcquireSRWLockExclusive(&lock);
    partner_player::State s{}; std::uintptr_t rec=0; float life=0, max=0;
    const bool healthy = !NativeActive() && !cycle.down && !configurePending && !shutdownPending;
    bool ok = healthy;
    if (enabled) ok = ok && worker_scenario::Compiled() && nativeConfigured && nativeGatePassed &&
        verified && !stopped && !failed && armed && ownerThread==GetCurrentThreadId() && NativeWorker() &&
        !(partner_input_hook::WindowSeq()&1u) && !partner_defer::InBatch() && Owned(s,rec,life,max);
    if (ok) { nativeEnabled=enabled; nativePad={}; nativeRestDiagnostics={}; lastReason=enabled?"sleep_native_test_enabled":"sleep_native_test_disabled"; }
    ReleaseSRWLockExclusive(&lock); return ok;
}
bool Control(std::uint32_t m, std::uint16_t ep, bool admitted, std::uint8_t command, std::uint32_t generation, Reply& out) noexcept {
    out = {}; out.reason = "context";
    AcquireSRWLockExclusive(&lock);
    const auto finish = [&](bool result, const char* reason) noexcept {
        // Even current-owner context/HP/input failures must not bypass the
        // lease watchdog by returning before NativeAdvance is reached.
        if(NativeActive()&&armed&&m==mission&&ep==epoch&&ownerThread==GetCurrentThreadId())NativeWatchdog();
        if(nativeAbort){reason="sleep_native_lab_abort_required";out.applied=false;out.nativeSleepReleaseReady=false;}
        out.nativeSleepActive = SleepActive();
        if (out.nativeSleepActive) {
            out.down=cycle.down; out.cause=cycle.cause; out.generation=cycle.generation;
            // Cleanup duty survives failed current-owner reads, but does not
            // authorize Lua to mask a replacement body. Only the successful
            // ordinary identity/health/input path may publish readiness.
        }
        lastReason = reason; out.reason = reason; ReleaseSRWLockExclusive(&lock); return result;
    };
    if (configurePending) {
        if (!Clear(true)) return finish(false, NativeActive()?"sleep_native_retirement_pending":"crawl_restore_pending");
        FinishConfigure(pendingAddresses, pendingPassed);
    }
    if (shutdownPending) {
        if (!Clear(true)) return finish(false, NativeActive()?"sleep_native_retirement_pending":"crawl_restore_pending");
        shutdownPending=false; stopped=true;
    }
    if (command == 3) {
        // Shutdown/configure may have retired this lease already. Absence is
        // acknowledged without reading or changing another native owner.
        if (!armed) { out.applied = true; return finish(true, "cleanup_absent"); }
        if (armed && mission == m && epoch == ep && ownerThread == GetCurrentThreadId()) {
            if (!Clear(true)) return finish(false, NativeActive()?"sleep_native_retirement_pending":"crawl_restore_pending");
            out.applied = true; return finish(true, "restored");
        }
        return finish(false, "cleanup_owner");
    }
    if (!verified || stopped || !admitted || !ep || command > 5 || (m != 30010 && m != 30020 && m != 30050))
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
    out.carryHeld = (partner_input::Effective(raw).held & partner_pad::kReload) != 0;
    while (hitCount && lastPoll >= hits[hitHead].at && lastPoll - hits[hitHead].at > 1500) {
        hitHead = (hitHead + 1) % 8; --hitCount;
    }
    if (hitCount) { out.hitSequence = hits[hitHead].sequence; out.hitDamageMilli = hits[hitHead].damage; out.hitCause = hits[hitHead].cause; }
    if (command == 5) {
        if (!cycle.down || generation!=cycle.generation || cycle.cause!=2) return finish(false,"generation");
        if (NativeActive()) { NativeAdvance(admitted,true,out); return finish(true,out.reason); }
        // Once an authorized wake begins, native idle must survive the later
        // actual input-mask update and logical revive. Polling cannot reseed.
        if(coop_sleep::Configured()&&!stockRelease.Request(StockEpisode(),cycle.down,cycle.cause))return finish(false,"sleep_stock_release_owner");
        if (coop_sleep::Active(0)) {
            out.nativeSleepReleaseReady=coop_sleep::Hold(0,false);
            stockRelease.ObservePhysicalRetirement(StockEpisode(),out.nativeSleepReleaseReady);
            out.applied=out.nativeSleepReleaseReady;
            return finish(true,out.applied?"sleep_stock_retired":"sleep_stock_wake_pending");
        }
        stockRelease.ObservePhysicalRetirement(StockEpisode(),true);
        out.applied=true;out.nativeSleepReleaseReady=true;return finish(true,"sleep_native_absent");
    }
    if (command == 2) {
        if (!cycle.down || generation != cycle.generation) return finish(false, "generation");
        // This proof intentionally reads the actual released masks; it never
        // calls Observe with invented named locks/zero-cap after preflight.
        if (NativeActive()) {
            if (!NativeReleaseProof()) return finish(true,nativeAbort?"sleep_native_lab_abort_required":nativeReadReason);
            out.nativeSleepReleaseReady=true;
        }
        if (coop_sleep::Active(0) && !coop_sleep::Hold(0,false)) return finish(true,"sleep_stock_wake_pending");
        if (!Released(raw)) return finish(true, "restore_pending");
        if (!SlowCrawl(true)) return finish(true, "crawl_restore_pending");
        partner_player::State again{}; std::uintptr_t againRec = 0; float againCur = 0, againMax = 0;
        if (!Owned(again, againRec, againCur, againMax) || !StoreLife(againRec, detail::RecoveredHealth(cycle.cause, originalHealth, againMax))) return finish(false, "health");
        friendlyRecovery = {}; detail::Revive(cycle, generation); controlsApplied = false; downAt = lastControls = 0;
        revivedAt = GetTickCount64();
        stockRelease.LogicalExit(!cycle.down);
        ForgetNativeRetired();
        out.down = false; out.cause = 0; out.applied = true;
        // Baseline now belongs to the live game's normal restrictions.
        baselineButtons = raw.pp_mask[0]; baselineTriggers = raw.pp_mask[2];
        return finish(true, "revived");
    }
    if (cycle.down) {
        if (cycle.cause==2 && (nativeEnabled || NativeActive())) {
            nativePrepared=true; NativeFlags(out);
            LocalPadRead p1{},p2{}; std::uintptr_t pp=0;
            const bool masked=Read(s.P+c::p_pad,pp)&&ReadLocalPad(pp,p1)&&ReadLocalPad(pp,p2)&&
                SameLocalPad(p1,p2)&&SameNativeState(p1.state,p2.state)&&NamedMasks(p1)&&NamedMasks(p2)&&
                partner_input_hook::WindowSeq()==seq&&!partner_defer::InBatch();
            // Standing preparation must never issue the normal Prone request.
            // It preserves pre-dart HP and restores any old crawl scalar.
            const bool protection=StoreLife(record,detail::IncapacitatedHealth(2,originalHealth,max))&&SlowCrawl(true);
            if (masked && protection) { controlsApplied=true; lastControls=lastPoll; }
            if (!protection) {
                const auto deadline=lastControls?lastControls:downAt;
                if (lastPoll>=deadline && lastPoll-deadline>sl::kRetireMs) NativeLabAbort("sleep_native_health_abort");
                out.applied=false; return finish(true,nativeAbort?"sleep_native_lab_abort_required":"sleep_native_health_pending");
            }
            // NativeAdvance also handles already-authorized retirement after
            // the masks are removed; no mask absence can seed or wake a body.
            NativeAdvance(admitted,false,out); return finish(true,out.reason);
        }
        // Check this frame first: a paused but still locked body stays down.
        // Failed requests and a later lost lock still have a bounded deadline.
        // A friendly dart puts the body in the stock player sleep (coop_sleep:
        // life state 2, the MGO/FOB sleep graph) instead of the prone fallback.
        bool prone = false;
        const bool stockSleep = cycle.cause == 2 && coop_sleep::Configured();
        if(stockSleep&&StockReleasePending()) {
            if(!stockRelease.physicalRetired)stockRelease.ObservePhysicalRetirement(StockEpisode(),coop_sleep::Hold(0,false));
            // Native retirement is complete but actual raw mask removal may
            // take another pad update. Do not re-mask or request prone here.
            if(stockRelease.physicalRetired){out.applied=true;out.nativeSleepReleaseReady=true;return finish(true,"sleep_stock_controls_retiring");}
            const bool protectedHealth=StoreLife(record,detail::IncapacitatedHealth(2,originalHealth,max));
            const bool masked=Controls(raw);if(protectedHealth&&masked){controlsApplied=true;lastControls=lastPoll;}
            out.applied=protectedHealth&&masked;return finish(true,"sleep_stock_wake_pending");
        }
        const bool nativeSleep = stockSleep && stockRelease.MaySeed(StockEpisode(),cycle.down,cycle.cause) && coop_sleep::Hold(0, true);
        // A partial or waking native cycle still owns the posture. Never issue
        // a prone request over its graph or silently replace it with fallback.
        if (stockSleep && !nativeSleep && coop_sleep::Active(0)) {
            const bool protectedHealth=StoreLife(record,detail::IncapacitatedHealth(2,originalHealth,max));
            const bool masked=Controls(raw);
            if(protectedHealth&&masked){controlsApplied=true;lastControls=lastPoll;}
            out.applied=protectedHealth&&masked;
            return finish(true,"sleep_stock_owner_pending");
        }
        if (nativeSleep) prone = true;
        const bool protection = (nativeSleep || Prone(s, prone)) && StoreLife(record, detail::IncapacitatedHealth(cycle.cause, originalHealth, max));
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
        return finish(true, applied ? cycle.cause == 2 ? (nativeSleep ? "sleeping_native" : "sleeping_prone_fallback") : "downed" : pendingReason);
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
    const bool nativeCycle=cycle.down && cycle.cause==2 && (nativeEnabled || SleepActive());
    if (nativeCycle) {
        if (!SameNativeState(first.state,second.state) || !NamedMasks(first) || !NamedMasks(second)) return finish("sleep_named_controls");
    } else if (cycle.down && controlsApplied && (first.stance != 3 || !Controls(first.raw))) return finish("controls");
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
    if (nativeCycle && controlsApplied && plan.magnitude==0.f && NativeWorker()) {
        // Receipt only after this exact natural completion and actual output,
        // with current PP/source/pair/type/masks/sequence revalidated after CAS.
        LocalPadRead after{}; auto expected=second; expected.raw.pp_derived[0]=plan.magnitude;
        scope.now=GetTickCount64(); scope.sequenceAfter=partner_input_hook::WindowSeq(); scope.batchAfter=partner_defer::InBatch();
        if (!detail::PadRefusal(scope) && ReadLocalPad(pad,after) && SameLocalPad(expected,after) &&
            SameNativeState(second.state,after.state) && NamedMasks(after) &&
            partner_input_hook::WindowSeq()==seq && !partner_defer::InBatch()) {
            nativePad=PadIdentity(after,seq); nativePad.at=scope.now;
            ++nativePadSerial; if (!nativePadSerial) ++nativePadSerial;
            nativePad.serial=nativePadSerial; nativePad.zero=true;
        } else { nativePad={}; out.reason="sleep_zero_receipt_refused"; return finish(out.reason); }
    }
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
    if (!cycle.down) { originalHealth = cur; downAt = now; lastControls = 0; controlsApplied = false; nativePad={}; }
    friendlyRecovery = {}; detail::Enter(cycle, cause);
    ReleaseSRWLockExclusive(&lock); return true;
}
bool IsProtected(std::uintptr_t P, std::uint32_t id) noexcept {
    AcquireSRWLockShared(&lock);
    partner_player::State s{}; std::uintptr_t record = 0; float cur = 0, max = 0;
    const auto now = GetTickCount64();
    const bool grace = !cycle.down && revivedAt && now >= revivedAt && now - revivedAt < kReviveGraceMs;
    const bool ok = (cycle.down || grace) && !failed && detail::FreshScope(now, lastPoll) &&
        id == 0 && P == ownedP && Owned(s, record, cur, max);
    ReleaseSRWLockShared(&lock); return ok;
}
NativeRestDiagnostics GetNativeRestDiagnostics() noexcept {
 const DWORD incoming=GetLastError();AcquireSRWLockShared(&lock);auto out=nativeRestDiagnostics;
 out.enabled=nativeEnabled;out.active=NativeActive();out.aborted=nativeAbort;
 out.capSequence=nativePad.sequence;out.capAt=nativePad.at;out.pendingAt=nativeRestPendingAt;
 out.currentSequence=partner_input_hook::WindowSeq();
 ReleaseSRWLockShared(&lock);SetLastError(incoming);return out;
}
Snapshot GetSnapshot() noexcept {
    AcquireSRWLockShared(&lock);
    const Snapshot out{verified && !stopped, armed, cycle.down, controlsApplied,
        mission, cycle.generation, epoch, cycle.cause, lastPoll, lastControls,
        observedLife, maximumHealth, lastReason, nativeEnabled, NativeActive(), nativeLastSample.stun, nativeLastSample.maximumStun,
        static_cast<std::uint8_t>(nativeLease.phase), nativeLastSequence, nativeLastSampleAt};
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
        originalHealth = cur; downAt = GetTickCount64(); lastControls = 0; controlsApplied = false; nativePad={};
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
    if (!Clear(true)) {
        shutdownPending=true; stopped=!SleepActive();
        lastReason=SleepActive()?"sleep_native_retirement_pending":"crawl_restore_pending";
    } else { shutdownPending=false; stopped=true; nativeEnabled=false; }
    ReleaseSRWLockExclusive(&lock);
}
} // namespace mgscoop::downed_player
