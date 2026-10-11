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
struct Hit {
    std::uintptr_t player{};
    std::uint32_t local{};
    std::uint16_t attacker{}, flags{};
    std::uint8_t bodyPart{};
    float life{};
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
    if(protected_local && Neutralize(packet,mode,stun,rawLife,rawStun,special)) return 0;
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
    // Even when revival is temporarily unavailable, an exact offline teammate
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
    unsigned char snapshot[4096]{};
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
    std::uintptr_t d{},a{};
    return Read(address+c::defense_slot,d) && d==address+c::defense && Read(address+c::can_slot,a) && a==address+c::can;
}
bool Install(void* image,std::size_t span,void (*log)(const char*) noexcept) noexcept {
    if(base) return base==reinterpret_cast<std::uintptr_t>(image) && installed[0] && installed[1] && defenseInstalled && canInstalled;
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
    logger("downed_damage=installed;scope=paired_owner_only;disk_writes=0\r\n");
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
void Shutdown() noexcept {
    if(!base) return;
    bool clean=true;
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
