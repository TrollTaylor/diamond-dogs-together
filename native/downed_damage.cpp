// Exact retail damage calculation virtual adapter. See the hash-bound evidence
// in docs/evidence/downed-damage-2026-10-10.json. No arbitrary game calls.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstring>
#include <cmath>
#include <atomic>
#include <cstdio>
#include <intrin.h>
#include "MinHook.h"
#include "mgscoop/downed_damage.hpp"
#include "mgscoop/downed_player.hpp"
#include "mgscoop/near_code.hpp"
#include "mgscoop/link_crypto.hpp"
#include "downed_damage_contract.hpp"
namespace mgscoop::downed_damage {
namespace {
namespace c = downed_damage_contract;
using Calc = float (*)(void*, std::uint32_t, void*, void*, std::uint8_t*, float*, float*, float*, std::uint8_t*);
std::uintptr_t base{};
Calc original{};
bool installed[2]{};
bool defenseInstalled{},canInstalled{};
using PadUpdate = void (*)(void*); // exact natural9C2A98 RCX-only caller; return ignored
PadUpdate originalPad{};
std::uintptr_t padTarget{}, padImage{};
bool padCreated{}, padInstalled{};
thread_local unsigned padDepth{};
struct PadDepth { bool outer{padDepth++ == 0}; ~PadDepth() noexcept { --padDepth; } };
std::atomic<unsigned> padCalls{}, padObserved{}, padCaps{}, padSamples{};
SRWLOCK padCacheLock = SRWLOCK_INIT;
struct PadCache { downed_player::PadObservation latest{}; std::uint64_t at{}; };
PadCache padCache{};
using Defense=std::uint32_t (*)(void*,void*,void*,void*,void*);
using Can=bool (*)(void*,void*,void*,void*,void*);
Defense originalDefense{};Can originalCan{};
std::atomic<unsigned> defenseCalls{},canCalls{},canDeclines{},defenseSamples{},canSamples{};
std::atomic<unsigned> calls{}, exactCalls{}, scopedCalls{}, samples{};
void (*logger)(const char*) noexcept{};
// Inspection and diagnostics must not change the thread's Win32 error state.
// Restore the incoming value before retail code, then retain its resulting value.
struct LastErrorState {
    DWORD value{GetLastError()};
    void Restore() const noexcept { SetLastError(value); }
    void Capture() noexcept { value=GetLastError(); }
    ~LastErrorState() noexcept { Restore(); }
};
template<class T> bool Read(std::uintptr_t p,T& out) noexcept {
    SIZE_T n=0; return p && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(p),&out,sizeof(out),&n) && n==sizeof(out);
}
// Always forward once BEFORE any eligibility/ownership check and without the
// downed lock. Nested/foreign/remote callbacks are never allowed to cap input.
__declspec(noinline) void UpdatePad(void* self) noexcept {
    LastErrorState error;
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    PadDepth depth;
    error.Restore();
    originalPad(self);
    error.Capture();
    padCalls.fetch_add(1,std::memory_order_relaxed);
    const auto observation=downed_player::AfterPadUpdate(reinterpret_cast<std::uintptr_t>(self),
        depth.outer, caller==padImage+c::pad_update_return);
    if(observation.observed) padObserved.fetch_add(1,std::memory_order_relaxed);
    if(observation.capped) padCaps.fetch_add(1,std::memory_order_relaxed);
    AcquireSRWLockExclusive(&padCacheLock);
    padCache={observation,GetTickCount64()};
    ReleaseSRWLockExclusive(&padCacheLock);
    if(observation.capped && logger && padSamples.fetch_add(1,std::memory_order_relaxed)<8) {
        char line[192]{};
        std::snprintf(line,sizeof(line),"downed_pad=cap;before=%.3f;after=%.3f;local=%u;thread=%u;gen=%u;caller=0x%llx;fresh=1;look_write=0\r\n",
            double(observation.before),double(observation.after),observation.local,observation.thread,observation.generation,
            static_cast<unsigned long long>(caller-padImage));
        logger(line);
    }
}
struct Hit {
    std::uintptr_t player{};
    std::uint32_t local{};
    std::uint16_t attacker{}, flags{};
    std::uint8_t bodyPart{};
    float life{}, max{};  // max: record+4 (downed_player ReadLife), read outside `exact`
    bool scoped{};
};
bool Inspect(void* object,std::uint32_t id,void* packet,void* record,Hit& hit) noexcept {
    const auto obj=reinterpret_cast<std::uintptr_t>(object);
    const auto pkt=reinterpret_cast<std::uintptr_t>(packet);
    const auto rec=reinterpret_cast<std::uintptr_t>(record);
    std::uintptr_t q{},records{};
    const bool exact= id<=1 && Read(obj+8,hit.player) &&
        downed_player::PassiveOfflineContext(hit.player,hit.local) &&
        Read(hit.player+0x60,q) && Read(q+8,records) &&
        records && records+id*0x4c==rec && Read(rec,hit.life) &&
        Read(pkt+0x20,hit.attacker) && Read(pkt+0x22,hit.flags) &&
        Read(pkt+0x26,hit.bodyPart);
    if(exact && !Read(rec+4,hit.max)) hit.max=0;
    std::uint32_t currentLocal{};
    hit.scoped=exact && downed_player::Scope(hit.player,currentLocal) && currentLocal==hit.local;
    return exact;
}
bool Neutralize(void* packet,std::uint8_t* mode,float* stun,float* rawLife,float* rawStun,std::uint8_t* special) noexcept {
    __try {
        *mode=0;*stun=0;*rawLife=0;*rawStun=0;*special=0;
        // No pending hit and no foreign-player notification. This is the actual
        // queued packet owned by the natural caller; never a world/network packet.
        auto* flags=reinterpret_cast<std::uint16_t*>(static_cast<unsigned char*>(packet)+0x22);
        *flags=static_cast<std::uint16_t>(*flags&~3u);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
struct DefenseHit {
    std::uintptr_t player{},packet{},entry{};
    std::uint16_t target{0xffff},attacker{0xffff},attack{0xffff},rawFlags{};
    std::uint32_t flags{},local{0xffffffff};
    bool exact{},scoped{};
};
DefenseHit InspectDefense(void* hitIface,void* targetIface,void* manager) noexcept {
    DefenseHit h{};std::uintptr_t data{},hit{},owner{},map{};std::uint16_t slot{};std::uint32_t start{};
    if(Read(reinterpret_cast<std::uintptr_t>(targetIface)+0x28,data) && data>=8)
        Read(data,h.target); // (data-8)+8, the exact encoded game-object id.
    if(Read(reinterpret_cast<std::uintptr_t>(hitIface)+0x28,hit) && hit>=0x20) {
        h.packet=hit-0x20;
        Read(h.packet+0x26,h.attacker);Read(h.packet+0x28,h.attack);Read(h.packet+0x24,h.rawFlags);
    }
    const auto m=reinterpret_cast<std::uintptr_t>(manager);
    if(Read(m+0x408,owner) && Read(owner+0x138,h.player)) {
        Read(h.player+0x204,h.flags);
        h.exact=h.target<=1 && downed_player::PassiveOfflineContext(h.player,h.local);
        std::uint32_t current{};h.scoped=h.exact && downed_player::Scope(h.player,current) && current==h.local;
        if(Read(h.player+0x70,map) && Read(map+(h.target&0x1ff)*2,slot) && slot!=0xffff &&
           Read(m+8,start) && slot>=start && Read(m+0x280,h.entry)) h.entry+=(slot-start)*0x20;
        else h.entry=0;
    }
    return h;
}
__declspec(noinline) std::uint32_t Defend(void* self,void* hit,void* target,void* collision,void* manager) noexcept {
    LastErrorState error;
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto h=InspectDefense(hit,target,manager);
    defenseCalls.fetch_add(1,std::memory_order_relaxed);
    error.Restore();
    const auto result=originalDefense(self,hit,target,collision,manager);
    error.Capture();
    if(h.target<=1 && logger && defenseSamples.fetch_add(1,std::memory_order_relaxed)<64) {
        char line[320]{};
        std::snprintf(line,sizeof(line),"downed_defense=sample;target=%u;attacker=%u;attack=%u;flags=0x%x;raw=0x%x;exact=%u;scoped=%u;result=%u;caller=0x%llx\r\n",
            unsigned(h.target),unsigned(h.attacker),unsigned(h.attack),h.flags,unsigned(h.rawFlags),h.exact?1u:0u,h.scoped?1u:0u,result,
            static_cast<unsigned long long>(caller>=base?caller-base:caller));
        logger(line);
    }
    return result;
}
__declspec(noinline) bool CanDefend(void* self,void* hit,void* target,void* collision,void* manager) noexcept {
    LastErrorState error;
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto h=InspectDefense(hit,target,manager);std::uint32_t entryFlags{};
    const bool entryRead=h.entry && Read(h.entry+0x18,entryFlags);
    canCalls.fetch_add(1,std::memory_order_relaxed);
    error.Restore();
    const bool result=originalCan(self,hit,target,collision,manager);
    error.Capture();
    if(!result) canDeclines.fetch_add(1,std::memory_order_relaxed);
    if(h.target<=1 && logger && canSamples.fetch_add(1,std::memory_order_relaxed)<64) {
        char line[320]{};
        std::snprintf(line,sizeof(line),"downed_can=sample;target=%u;attacker=%u;attack=%u;flags=0x%x;entry_read=%u;entry_flags=0x%x;exact=%u;scoped=%u;result=%u;caller=0x%llx\r\n",
            unsigned(h.target),unsigned(h.attacker),unsigned(h.attack),h.flags,entryRead?1u:0u,entryFlags,h.exact?1u:0u,h.scoped?1u:0u,result?1u:0u,
            static_cast<unsigned long long>(caller>=base?caller-base:caller));
        logger(line);
    }
    return result;
}
float Calculate(void* object,std::uint32_t id,void* packet,void* record,std::uint8_t* mode,
                float* stun,float* rawLife,float* rawStun,std::uint8_t* special) noexcept {
    LastErrorState error;
    Hit hit{};
    const bool offline=Inspect(object,id,packet,record,hit);
    const bool scoped=offline && hit.scoped;
    calls.fetch_add(1,std::memory_order_relaxed);
    if(offline) exactCalls.fetch_add(1,std::memory_order_relaxed);
    if(scoped) scopedCalls.fetch_add(1,std::memory_order_relaxed);
    const bool protected_local=offline && downed_player::IsProtected(hit.player,id);
    // A downed (protected) player keeps its shield for every natural part (0..11) at
    // once; a non-natural part (the sea / out-of-bounds kill, part 12) is calculated
    // first so that a kill lethal even at full health stays the stock death.
    const bool protected_environment=protected_local && !NaturalBodyPart(hit.bodyPart);
    if(protected_local && !protected_environment && Neutralize(packet,mode,stun,rawLife,rawStun,special)) return 0;
    error.Restore();
    const float amount=original(object,id,packet,record,mode,stun,rawLife,rawStun,special);
    error.Capture();
    float stun_amount{};std::uint8_t output_mode{};
    const bool stun_read=Read(reinterpret_cast<std::uintptr_t>(stun),stun_amount);
    const bool mode_read=Read(reinterpret_cast<std::uintptr_t>(mode),output_mode);
    if(id<=1 && logger && samples.fetch_add(1,std::memory_order_relaxed)<32) {
        std::uint16_t attacker{},flags{};std::uint8_t type{};float life{};std::uintptr_t vt{},p{};
        const auto pkt=reinterpret_cast<std::uintptr_t>(packet),obj=reinterpret_cast<std::uintptr_t>(object);
        Read(pkt+0x20,attacker);Read(pkt+0x22,flags);Read(pkt+0x26,type);
        Read(reinterpret_cast<std::uintptr_t>(record),life);Read(obj,vt);Read(obj+8,p);
        char line[320]{};
        std::snprintf(line,sizeof(line),"downed_damage=sample;id=%u;exact=%u;scoped=%u;attacker=%u;flags=0x%x,0x%x;part=%u;life=%.1f;damage=%.1f;stun=%.1f;mode=%u;mode_read=%u;vt_rva=0x%llx;owner_read=%u\r\n",
            id,offline?1u:0u,scoped?1u:0u,unsigned(attacker),unsigned(hit.flags),unsigned(flags),unsigned(type),
            double(life),double(amount),stun_read?double(stun_amount):-1.,unsigned(output_mode),mode_read?1u:0u,
            static_cast<unsigned long long>(vt>=base && vt-base<0x5000000 ? vt-base:0),p?1u:0u);
        logger(line);
    }
    if(protected_environment) {
        // Downed and then crawled into the sea (or blown out of bounds): the stock
        // death, so PLAYER_FALL_DEAD drives the shared game over (the downed state has
        // no bleed-out; the session's sea victims lay downed in the water). Anything
        // weaker (a part-12 packet that is lethal only at the downed health) keeps the shield.
        if(ProtectedEnvironmentKill(scoped,id,hit.local,hit.bodyPart,hit.life,hit.max,amount)) {
            if(logger) {
                char line[256]{};
                std::snprintf(line,sizeof(line),"downed_damage=stock_death;reason=environment;protected=1;id=%u;life=%.1f;max=%.1f;damage=%.1f;flags=0x%x;part=%u\r\n",
                    id,double(hit.life),double(hit.max),double(amount),unsigned(hit.flags),unsigned(hit.bodyPart));
                logger(line);
            }
            return amount;
        }
        if(Neutralize(packet,mode,stun,rawLife,rawStun,special)) return 0;
    }
    if(!offline || !stun_read) return amount;
    const bool sleep=mode_read && SleepDamage(output_mode,stun_amount);
    const bool unsafe_notification=UnsafeOfflineSleep(offline,id,hit.attacker,hit.flags,hit.bodyPart,stun_amount);
    // Only the actual local shooter's natural direct hit on its replica is
    // forwarded. Part zero is a head-style hit; torso and limb parts are 1..11.
    // Remote visual bullets remain suppressed by the existing filter.
    const bool friendly_replica=FriendlyReplicaHit(scoped,id,hit.local,hit.attacker,hit.flags,
        hit.bodyPart,amount,mode_read?output_mode:0,stun_amount);
    if(friendly_replica) {
        const bool queued=downed_player::QueueFriendlyHit(hit.player,amount,static_cast<std::uint8_t>(sleep?2:0));
        if(Neutralize(packet,mode,stun,rawLife,rawStun,special)) {
            if(logger) logger(queued ? "friendly_hit=queued;target=replica;native_damage=0\r\n" :
                                      "friendly_hit=ignored;reason=queue_unavailable;native_damage=0\r\n");
            return 0;
        }
        return amount;
    }
    const auto decision=Evaluate(scoped,id,hit.local,hit.attacker,hit.flags,hit.bodyPart,hit.life,amount,stun_amount,mode_read?output_mode:0,false);
    bool downed=decision.bypass;
    if(decision.cause) downed=downed_player::CaptureHit(hit.player,id,decision.cause);
    // Record the actual first native lethal packet even after the general
    // sample budget was consumed by earlier friendly or nonlethal hits.
    if(downed && decision.cause==1 && logger) {
        char line[256]{};
        std::snprintf(line,sizeof(line),"downed_damage=lethal_capture;id=%u;attacker=%u;life=%.3f;damage=%.3f;scoped=%u;flags=0x%x;part=%u;thread=%u;tick_ms=%llu\r\n",
            id,unsigned(hit.attacker),double(hit.life),double(amount),scoped?1u:0u,unsigned(hit.flags),unsigned(hit.bodyPart),
            GetCurrentThreadId(),static_cast<unsigned long long>(GetTickCount64()));
        logger(line);
    }
    // The sea/out-of-bounds kill stays the stock death (shared game over).
    if(!downed && logger && EnvironmentKill(scoped,id,hit.local,hit.bodyPart,hit.life,amount)) {
        char line[224]{};
        std::snprintf(line,sizeof(line),"downed_damage=stock_death;reason=environment;id=%u;life=%.1f;damage=%.1f;flags=0x%x;part=%u\r\n",
            id,double(hit.life),double(amount),unsigned(hit.flags),unsigned(hit.bodyPart));
        logger(line);
    }
    // Even when revival is temporarily unavailable, an exact offline direct
    // dart must not enter the verified null-synchronizer notification path.
    if(!downed && !unsafe_notification) return amount;
    if(!Neutralize(packet,mode,stun,rawLife,rawStun,special)) {
        if(logger) logger("downed_damage=refused;reason=output_write\r\n");
        return amount;
    }
    if(logger) logger(!downed ? "downed_damage=ignored;cause=sleep;reason=downed_unavailable\r\n" :
                      decision.cause==2 ? "downed_damage=intercept;cause=sleep\r\n" :
                      decision.cause==1 ? "downed_damage=intercept;cause=lethal\r\n" :
                                         "downed_damage=intercept;cause=replica\r\n");
    return 0;
}
}
bool CodeGate(const void* image,std::size_t span) noexcept {
    if(!image) return false;
    const auto address=reinterpret_cast<std::uintptr_t>(image);
    // Full retail bodies can exceed 4 KiB (alternate locomotion is 4152 bytes).
    // Retain every complete hash; size the bounded stack snapshot from the contract.
    constexpr std::size_t snapshotSize = []() constexpr {
        std::size_t maximum = 0;
        for (const auto& pin : c::pins) if (pin.size > maximum) maximum = pin.size;
        return maximum;
    }();
    static_assert(snapshotSize > 0 && snapshotSize <= 8192, "Review larger native body contracts before increasing the snapshot bound");
    unsigned char snapshot[snapshotSize]{};
    for(const auto& pin:c::pins) {
        if(pin.size>sizeof(snapshot) || pin.rva>span || pin.size>span-pin.rva) return false;
        SIZE_T n=0;
        if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address+pin.rva),snapshot,pin.size,&n) || n!=pin.size) return false;
        std::uint8_t digest[32]{};
        link::Sha256Digest(snapshot,pin.size,digest);
        if(std::memcmp(digest,pin.digest,sizeof(digest))!=0) return false;
    }
    for(const auto slot:c::slots) {
        std::uintptr_t target{};
        if(slot>span || sizeof(target)>span-slot || !Read(address+slot,target) ||
            target!=address+c::calculate) return false;
    }
    std::uintptr_t d{},a{},p{};
    return Read(address+c::defense_slot,d) && d==address+c::defense && Read(address+c::can_slot,a) && a==address+c::can &&
        c::pad_update_slot<=span && sizeof(p)<=span-c::pad_update_slot &&
        Read(address+c::pad_update_slot,p) && p==address+c::pad_update;
}
bool Install(void* image,std::size_t span,void (*log)(const char*) noexcept) noexcept {
    if(base) return base==reinterpret_cast<std::uintptr_t>(image) && installed[0] && installed[1] && defenseInstalled && canInstalled && padInstalled;
    if(!log || !CodeGate(image,span)) return false;
    base=reinterpret_cast<std::uintptr_t>(image);
    original=reinterpret_cast<Calc>(base+c::calculate);
    logger=log;
    originalDefense=reinterpret_cast<Defense>(base+c::defense);
    originalCan=reinterpret_cast<Can>(base+c::can);
    for(unsigned i=0;i<2;++i) {
        const auto site=base+c::slots[i];
        const char* why=near_code::PatchQword(reinterpret_cast<void*>(site),base+c::calculate,
                                             reinterpret_cast<std::uintptr_t>(&Calculate));
        std::uintptr_t now{};
        installed[i]=Read(site,now) && now==reinterpret_cast<std::uintptr_t>(&Calculate);
        if(why || !installed[i]) {
            logger("downed_damage=refused;reason=vtable_patch\r\n");
            Shutdown();return false;
        }
    }
    const char* why=near_code::PatchQword(reinterpret_cast<void*>(base+c::defense_slot),base+c::defense,reinterpret_cast<std::uintptr_t>(&Defend));
    std::uintptr_t now{};defenseInstalled=Read(base+c::defense_slot,now) && now==reinterpret_cast<std::uintptr_t>(&Defend);
    if(why || !defenseInstalled) {Shutdown();return false;}
    why=near_code::PatchQword(reinterpret_cast<void*>(base+c::can_slot),base+c::can,reinterpret_cast<std::uintptr_t>(&CanDefend));
    canInstalled=Read(base+c::can_slot,now) && now==reinterpret_cast<std::uintptr_t>(&CanDefend);
    if(why || !canInstalled) {Shutdown();return false;}
    // Full body/caller hashes above precede our own entry patch. Preserve the
    // PlayerPad vtable itself for the independent existing partner gates.
    const auto target=base+c::pad_update;
    const MH_STATUS initialized=MH_Initialize();
    if(initialized!=MH_OK && initialized!=MH_ERROR_ALREADY_INITIALIZED) {
        logger("downed_pad=refused;reason=hook_initialize\r\n"); Shutdown();return false;
    }
    if(padCreated && padTarget!=target) { Shutdown();return false; }
    if(!padCreated) {
        if(MH_CreateHook(reinterpret_cast<void*>(target),reinterpret_cast<void*>(&UpdatePad),
            reinterpret_cast<void**>(&originalPad))!=MH_OK) {
            logger("downed_pad=refused;reason=hook_create\r\n"); Shutdown();return false;
        }
        padTarget=target;padImage=base;padCreated=true;
    }
    if(MH_EnableHook(reinterpret_cast<void*>(padTarget))!=MH_OK) {
        logger("downed_pad=refused;reason=hook_enable\r\n"); Shutdown();return false;
    }
    padInstalled=true;
    logger("downed_damage=installed;scope=paired_owner_only;pad_fresh_cap=1;disk_writes=0\r\n");
    return true;
}
bool StatusLine(char* out,std::size_t capacity) noexcept {
    if(!out || !capacity) return false;
    const int n=std::snprintf(out,capacity,"damage;installed=%u;calls=%u;exact=%u;scoped=%u;defense=%u;can=%u;declines=%u",
        installed[0]&&installed[1]?1u:0u,calls.load(std::memory_order_relaxed),exactCalls.load(std::memory_order_relaxed),
        scopedCalls.load(std::memory_order_relaxed),defenseCalls.load(std::memory_order_relaxed),
        canCalls.load(std::memory_order_relaxed),canDeclines.load(std::memory_order_relaxed));
    return n>=0 && static_cast<std::size_t>(n)<capacity;
}
bool PadStatusLine(char* out,std::size_t capacity) noexcept {
    if(!out || !capacity) return false;
    AcquireSRWLockShared(&padCacheLock); const auto cache=padCache; ReleaseSRWLockShared(&padCacheLock);
    const auto bounded=[](unsigned value) noexcept {return value>999999u?999999u:value;};
    const auto now=GetTickCount64();
    const unsigned age=cache.at && now>=cache.at ? static_cast<unsigned>(now-cache.at>999999?999999:now-cache.at) : 999999u;
    const auto& o=cache.latest;
    const int n=std::snprintf(out,capacity,"downpad;on=%u;n=%u;ok=%u;caps=%u;mag=%d,%d;id=%u;thr=%u;gen=%u;age=%u;reason=%s",
        padInstalled?1u:0u,bounded(padCalls.load(std::memory_order_relaxed)),bounded(padObserved.load(std::memory_order_relaxed)),
        bounded(padCaps.load(std::memory_order_relaxed)),int(o.before*1000),int(o.after*1000),o.local,o.thread,o.generation,age,
        o.reason?o.reason:"unobserved");
    return n>=0 && static_cast<std::size_t>(n)<capacity;
}
void Shutdown() noexcept {
    if(!base) return;
    bool clean=true;
    if(padInstalled) {
        const auto status=MH_DisableHook(reinterpret_cast<void*>(padTarget));
        if(status==MH_OK || status==MH_ERROR_DISABLED) padInstalled=false;
        else clean=false;
    }
    // Never remove a trampoline or uninitialize shared MinHook: an already
    // entered original update must complete even during a failed installation.
    for(unsigned i=0;i<2;++i) if(installed[i]) {
        const auto site=base+c::slots[i];
        const char* why=near_code::PatchQword(reinterpret_cast<void*>(site),reinterpret_cast<std::uintptr_t>(&Calculate),base+c::calculate);
        std::uintptr_t now{};
        installed[i]=!Read(site,now) || now!=base+c::calculate;
        if(why || installed[i]) clean=false;
    }
    for(unsigned i=0;i<2;++i) {
        bool& owned=i?canInstalled:defenseInstalled;
        if(!owned) continue;
        const auto site=base+(i?c::can_slot:c::defense_slot),originalSite=base+(i?c::can:c::defense);
        const auto hook=i?reinterpret_cast<std::uintptr_t>(&CanDefend):reinterpret_cast<std::uintptr_t>(&Defend);
        const char* why=near_code::PatchQword(reinterpret_cast<void*>(site),hook,originalSite);
        std::uintptr_t now{};owned=!Read(site,now) || now!=originalSite;
        if(why || owned) clean=false;
    }
    if(clean) base=0;
    // original is retained: an already-entered natural call still completes.
}
}
