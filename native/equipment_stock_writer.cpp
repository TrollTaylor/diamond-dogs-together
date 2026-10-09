// Original, separately compiled copied-save stock46 experiment. No hooks,
// worker routing, alias preparation, local-index edits, rollback or retries.
// The generalized writer (ObserveGeneral) dispatches the same qualified
// ChangeEquip request for other stock guns, one slot at a time, each behind the
// stock46-shape preflight with that gun's own derived data.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <strsafe.h>
#include "mgscoop/equipment_stock_writer.hpp"
#include "mgscoop/equipment_stock_writer_contract.hpp"
#include "equipment_stock_writer_pins.hpp"
#include "partner_player_contract.hpp"

namespace mgscoop::equipment_stock_writer {
namespace {
bool Read(std::uintptr_t address,void* out,std::size_t size) noexcept {
    if(!address || address>UINTPTR_MAX-size) return false;
    SIZE_T copied=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),out,size,&copied) && copied==size;
}
bool BodyGate(std::uintptr_t base,std::size_t span,std::uintptr_t& mismatch) noexcept {
    mismatch=0;
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return false;
    bool ok=true;
    for(const auto& body:equipment_stock_writer_pins::bodies) {
        BCRYPT_HASH_HANDLE hash=nullptr;
        unsigned char digest[32]{},chunk[512]{};
        ok=base && base<=UINTPTR_MAX-span && body.rva<=span && body.size<=span-body.rva &&
            BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
        for(std::size_t offset=0;ok && offset<body.size;) {
            const auto count=(std::min)(sizeof(chunk),body.size-offset);
            ok=Read(base+body.rva+offset,chunk,count) &&
                BCryptHashData(hash,chunk,static_cast<ULONG>(count),0)>=0;
            offset+=count;
        }
        if(ok) ok=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0 && std::memcmp(digest,body.sha256,sizeof(digest))==0;
        if(hash) BCryptDestroyHash(hash);
        if(!ok) {mismatch=body.rva;break;}
    }
    BCryptCloseAlgorithmProvider(algorithm,0);
    return ok;
}
#ifdef MGS_LOCAL_STOCK46_WRITER_TEST
namespace contract=equipment_stock_writer_contract;
Authorization authorization;
Readiness readiness;
contract::Snapshot before;
contract::Snapshot scratch;
std::uint32_t observed_epoch=0,expected_handle=0;
unsigned observations=0;
bool tracking=false;
bool readiness_logged=false;
#pragma pack(push,1)
struct Request {
    std::uint64_t type=0x326C48DA30C0ull;
    std::uint16_t equip=46,stock=RequestAmountsFor(kAmmoTest).stock,stock_sub=0,ammo=RequestAmountsFor(kAmmoTest).ammo,ammo_sub=0;
    std::uint8_t suppression_life=0,flags=4;
};
#pragma pack(pop)
static_assert(sizeof(Request)==20 && offsetof(Request,equip)==8 && offsetof(Request,flags)==19);
// A trivial SEH boundary contains the single outer dispatcher call. It uses
// exactly the public wrapper's output-pointer ABI and constant instance1.
bool Call(std::uintptr_t function,std::uintptr_t iface,const Request* request,
          std::int32_t* result,std::uintptr_t* returned) noexcept {
    using Dispatch=std::int32_t* (*)(void*,std::int32_t*,std::uint32_t,const Request*);
    __try {
        *returned=reinterpret_cast<std::uintptr_t>(reinterpret_cast<Dispatch>(function)(
            reinterpret_cast<void*>(iface),result,1,request));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void Receipt(void (*log)(const char*) noexcept,const char* phase,const char* reason,
             std::int32_t result=INT32_MIN,std::uintptr_t mismatch=0) noexcept {
    if(!log) return;
    char line[640]{};
    StringCchPrintfA(line,sizeof(line),
        "stock46_writer=%s; reason=%s mission=30010 instance=1 slot=1 equip=46 receiver=157 epoch=%u expected_handle=%x result=%d mismatch_rva=%llx attempted=%u retry=0 local_index_write=0 alias_call=0 rollback_calls=0 stock=%u ammo=%u ammo_experiment=%u\r\n",
        phase,reason ? reason : "ok",observed_epoch,expected_handle,result,
        static_cast<unsigned long long>(mismatch),unsigned(authorization.Attempted()),unsigned(RequestAmountsFor(kAmmoTest).stock),
        unsigned(RequestAmountsFor(kAmmoTest).ammo),kAmmoTest ? 1u : 0u);
    log(line);
}
void PackageReceipt(void (*log)(const char*) noexcept,std::uintptr_t base,std::size_t span,const contract::PackageObservation& sample,
                    const contract::Target& t,const char* label,unsigned count) noexcept {
    if(!log) return;
    auto reader=Read;contract::View<decltype(reader)> v{reader,base,span};
    const auto p=sample.reads ? sample : contract::ObservePackage(v,t);char line[1024]{};
    StringCchPrintfA(line,sizeof(line),
        "%s_package=read_only; reads=%x checks=%x service=%llx backing=%llx allocation=%llx capacity=%llx size=%llx parameters=%llx variant=%u table=%llx method8=%llx method28=%llx method38=%llx descriptor%u=%llx observations=%u native_calls=0\r\n",
        label,p.reads,p.checks,static_cast<unsigned long long>(p.service),static_cast<unsigned long long>(p.backing),
        static_cast<unsigned long long>(p.allocation),static_cast<unsigned long long>(p.capacity),
        static_cast<unsigned long long>(p.size),static_cast<unsigned long long>(p.parameters),p.variant,
        static_cast<unsigned long long>(p.table),static_cast<unsigned long long>(p.methods[0]),
        static_cast<unsigned long long>(p.methods[1]),static_cast<unsigned long long>(p.methods[2]),
        unsigned(t.equip),static_cast<unsigned long long>(p.descriptor),count);
    log(line);
}
void OptionalReceipt(void (*log)(const char*) noexcept,const contract::OptionalLease& o,const char* label,bool present) noexcept {
    if(!log) return;
    char line[640]{};
    StringCchPrintfA(line,sizeof(line),
        "%s_optional=read_only; present=%u asset=%llx source=%llx header=%llx source_bytes=%llu parameters=%u render_global=%u render_bytes=%u arena_stride=2048 native_calls=0\r\n",
        label,present ? 1u : 0u,static_cast<unsigned long long>(o.asset),static_cast<unsigned long long>(o.source),
        static_cast<unsigned long long>(o.header),static_cast<unsigned long long>(o.bytes),o.parameters,o.global,o.render_bytes);
    log(line);
}
// Accepted readback after one dispatch of target t: exactly the target slot's
// handle (expected) changed, its cache entry gained one reference, its new
// metadata row names t and its cache entry, the binder record of the slot has
// it current or pending, local equipment and every initially occupied cache
// entry are unchanged, and a bound handle maps an initially qualified resource.
const char* Readback(std::uintptr_t base,std::size_t span,std::uintptr_t player,std::uintptr_t remote_block,
                     const contract::Target& t,std::uint32_t expected,const contract::Snapshot& lease,
                     contract::Snapshot& after,bool& bound) noexcept {
    bound=false;
    auto reader=Read;
    if(const auto reason=contract::Capture(base,span,reader,player,remote_block,after,t)) return reason;
    if(const auto reason=contract::LocalUnchanged(lease,after)) return reason;
    if(after.remote_handles[t.slot]!=expected || after.cache_refs!=lease.cache_refs+1) return "remote_handle_or_refs";
    for(unsigned i=0;i<7;++i) if(i!=t.slot && after.remote_handles[i]!=lease.remote_handles[i]) return "other_remote_slot_changed";
    contract::View<decltype(reader)> view{reader,base,span};
    if(const auto reason=contract::DelayedCachesPreserved(view,lease)) return reason;
    contract::Bytes14 row{};std::uint32_t mapped=0;
    if(!view.At(after.meta_rows,lease.new_index*0xE,row) || contract::Word16(row.data())!=t.equip ||
       row[8]!=t.cache_index || !view.At(after.map_rows,lease.new_index*4,mapped)) return "new_handle_owner";
    const auto current=contract::Word32(after.remote_binder.data()+t.slot*12);
    const auto pending=contract::Word32(after.remote_binder.data()+t.slot*12+4);
    if(current!=expected && pending!=expected) return "model_handle_missing";
    if(current!=expected) return nullptr; // queued, not rendered evidence
    if(mapped>=60) return "bound_without_resource";
    if(const auto reason=contract::QualifiedBoundResource(view,lease,mapped)) return reason;
    // Native DBF1F0 maps a bounded resource ID across group sizes. Follow the
    // same pure data chain; ADBE00's bank getter itself has no index check.
    std::uintptr_t groups=0,group=0,bank=0,resources=0,pointer=0;std::uint32_t size=0,count=0;
    if(!view.At(after.engine,0x130,groups)) return "bound_resource_groups";
    bool found=false;
    for(unsigned i=0;i<32;++i) {
        if(!view.At(groups,i*8,group) || !view.At(group,0x30,size) || size!=(i<28?2u:1u)) return "bound_resource_group";
        if(mapped<size) {found=true;break;}
        mapped-=size;
    }
    if(!found || !view.At(group,0x10,bank) || !view.Method(bank,0x22E2970,0x240,0xADBE00) ||
       !view.At(bank,0x50,count) || count!=size || mapped>=count ||
       !view.At(bank,0x58,resources) || !view.At(resources,mapped*8,pointer) || !pointer) return "bound_resource_pointer";
    bound=true;
    return nullptr;
}
// The selected resource lease of a preflight re-read directly before the call.
const char* LeaseChanged(const contract::Snapshot& lease,const contract::Snapshot& refreshed) noexcept {
    if(refreshed.ring!=lease.ring || refreshed.ring_cursor!=lease.ring_cursor ||
       refreshed.new_index!=lease.new_index || refreshed.meta_rows!=lease.meta_rows || refreshed.map_rows!=lease.map_rows)
        return "allocation_lease_changed";
    if(refreshed.next_resource!=lease.next_resource || refreshed.selected_group!=lease.selected_group ||
       refreshed.selected_resource_index!=lease.selected_resource_index ||
       refreshed.package_service!=lease.package_service || refreshed.package_parameters!=lease.package_parameters ||
       refreshed.selected_banks!=lease.selected_banks || refreshed.selected_bank_arrays!=lease.selected_bank_arrays ||
       refreshed.selected_cache_counts!=lease.selected_cache_counts || refreshed.selected_cache_arrays!=lease.selected_cache_arrays ||
       refreshed.selected_bank_aux!=lease.selected_bank_aux ||
       refreshed.selected_bank_allocations!=lease.selected_bank_allocations ||
       refreshed.selected_bank_allocation_bytes!=lease.selected_bank_allocation_bytes ||
       refreshed.selected_bank_used_bytes!=lease.selected_bank_used_bytes ||
       refreshed.main_cache_candidate!=lease.main_cache_candidate || refreshed.main_cache_entries!=lease.main_cache_entries ||
       refreshed.banks!=lease.banks || refreshed.eligible_resources!=lease.eligible_resources ||
       refreshed.initial_maps!=lease.initial_maps || refreshed.initial_owners!=lease.initial_owners ||
       refreshed.initial_load_flags!=lease.initial_load_flags || refreshed.initial_engine_metadata!=lease.initial_engine_metadata ||
       refreshed.free_queue!=lease.free_queue || refreshed.free_cursor!=lease.free_cursor)
        return "selected_resource_lease_changed";
    return nullptr;
}

// The viewer's weapon/ammo vars block and shoot count (partner_player_contract,
// generator-checked: vars = *(base+0x2C16920), +0x548..+0x670 and +0x144).
// leak.ammo_leaks digests them only around block 1's job; a dispatch runs on the
// mission owner, so the writer reads them directly before and after its call.
namespace ppc=partner_player_contract;
struct LocalVars {
    std::array<std::uint8_t,ppc::vars_ammo_end-ppc::vars_ammo_begin> ammo{};
    std::uint32_t shots=0;
};
bool SameVars(const LocalVars& a,const LocalVars& b) noexcept {return a.ammo==b.ammo && a.shots==b.shots;}
bool ReadLocalVars(std::uintptr_t base,LocalVars& out) noexcept {
    std::uintptr_t vars=0;
    return Read(base+ppc::vars_slot_rva,&vars,sizeof(vars)) && vars &&
        Read(vars+ppc::vars_ammo_begin,out.ammo.data(),out.ammo.size()) && Read(vars+ppc::vars_shoot_count,&out.shots,sizeof(out.shots));
}

// --- The generalized writer state (one allocation) ---------------------------
struct General {
    bool active=false,ordered=false,epoch_set=false,tracking=false,readiness_logged=false,census_ok=false,census_logged=false;
    std::uint32_t mission=0,observed_epoch=0,expected=0;
    // The last logged wait reason (a static string; logged on change, at most kWaitLines per allocation).
    const char* wait_logged=nullptr;
    unsigned wait_lines=0;
    std::uint8_t order[kSlots]{0,2,1};
    unsigned cursor=0,plan_waits=0,observations=0;
    Readiness readiness;
    contract::Target target{};
    std::uint32_t dispatched_handles[kSlots]{};
    std::uint16_t dispatched_equips[kSlots]{};  // never a tier C candidate of another slot
    unsigned candidate_lines=0;
    std::array<contract::CacheEntry,contract::kCacheEntries> census{};
};
General general;
contract::Snapshot general_before;
contract::Snapshot general_scratch;
Status status;
// Observations (~100 ms each) a slot waits for the manifest before refusing.
constexpr unsigned kPlanWaitLimit=300;
constexpr unsigned kWaitLines=8;
void SetReason(SlotStatus& slot,const char* reason) noexcept {StringCchCopyA(slot.reason,sizeof(slot.reason),reason ? reason : "ok");}
void GeneralReceipt(void (*log)(const char*) noexcept,const char* phase,unsigned slot,const char* reason,
                    std::int32_t result=INT32_MIN,std::uintptr_t mismatch=0) noexcept {
    if(!log || slot>=kSlots) return;
    const auto& s=status.slots[slot];
    char line[640]{};
    StringCchPrintfA(line,sizeof(line),
        "equip_writer=%s; reason=%s; mission=%u; slot=%u; want=%u; want_type=%u; equip=%u; tier=%s; family=%s; type=%u; cache=%u; candidates=%u; candidate=%u; epoch=%u; expected_handle=%x; result=%d; mismatch_rva=%llx; dispatches=%u; instance=1; retry=0; alias_call=0; rollback_calls=0; replace_filled=0; evict=0\r\n",
        phase,reason ? reason : "ok",general.mission,slot,unsigned(s.want),unsigned(s.want_type),unsigned(s.equip),TierName(s.tier),
        s.equip ? (s.optional ? "optional" : "plain") : "none",unsigned(s.type),unsigned(s.cache_index),s.candidates,unsigned(s.candidate),
        general.observed_epoch,s.handle,result,static_cast<unsigned long long>(mismatch),status.dispatches);
    log(line);
}
void CensusReceipt(void (*log)(const char*) noexcept) noexcept {
    if(!log) return;
    char line[2048]{};
    StringCchPrintfA(line,sizeof(line),"equip_resident; mission=%u; used=%u; entries=",general.mission,unsigned(status.resident_count));
    for(unsigned i=0;i<status.resident_count;++i) {
        const auto& e=status.resident[i];
        char item[48]{};
        StringCchPrintfA(item,sizeof(item),"%s%u:%u/t%u/r%u/f%u/n%u",i ? "," : "",unsigned(e.index),unsigned(e.equip),unsigned(e.type),
                         unsigned(e.receiver),unsigned(e.flags),e.refs);
        StringCchCatA(line,sizeof(line),item);
    }
    StringCchCatA(line,sizeof(line),"; native_calls=0\r\n");
    log(line);
}
// The equipment engine through the player's equip iface (Capture re-reads and
// pins the whole chain before any use; this only feeds the pure census).
template<class View> bool EngineOf(View& v,std::uintptr_t player,std::uintptr_t& engine) noexcept {
    std::uintptr_t iface=0,inventory=0;engine=0;
    return v.At(player,0xC8,iface) && v.Method(iface,0x23C1720,0x198,0x11AC2E0) && v.At(iface,8,inventory) &&
        v.Method(inventory,0x23895D0,8,0xEE9880) && v.At(inventory,0x58,engine) && v.Method(engine,0x234B840,0,0xDC3250);
}
// The other remote slots hold exactly what this allocation dispatched (or nothing).
const char* OtherSlots(const contract::Snapshot& s,unsigned slot) noexcept {
    for(unsigned i=0;i<7;++i) {
        if(i==slot) continue;
        const std::uint32_t want=i<kSlots ? general.dispatched_handles[i] : 0u;
        if(s.remote_handles[i]!=want) return "other_slot_unexpected";
    }
    return nullptr;
}
void Advance(void (*log)(const char*) noexcept,unsigned slot,SlotState state,const char* phase,const char* reason,
             std::int32_t result=INT32_MIN,std::uintptr_t mismatch=0) noexcept {
    auto& s=status.slots[slot];
    s.state=state;
    SetReason(s,reason);
    if(state==SlotState::refused || state==SlotState::unverified) ++status.refused;
    if(state==SlotState::bound) ++status.bound;
    GeneralReceipt(log,phase,slot,reason,result,mismatch);
    general.tracking=false;general.observations=0;general.readiness.Reset();general.readiness_logged=false;
    general.plan_waits=0;++general.cursor;
}
// Nothing spent: the not yet planned slot reports why the writer waits
// (gates, body, engine_chain, census); a change is logged (capped).
void Waiting(void (*log)(const char*) noexcept,unsigned slot,const char* reason) noexcept {
    auto& s=status.slots[slot];
    if(s.state!=SlotState::idle && s.state!=SlotState::plan_wait) return;
    SetReason(s,reason);
    if(general.wait_logged==reason || general.wait_lines>=kWaitLines || !log) return;
    general.wait_logged=reason;++general.wait_lines;
    char line[160]{};
    StringCchPrintfA(line,sizeof(line),"equip_writer=waiting; reason=%s; mission=%u; slot=%u; native_calls=0\r\n",
        reason ? reason : "none",general.mission,slot);
    log(line);
}
// One line per tier C candidate that passed the census filters (capped per
// allocation): its pass, type, receiver, the reason (ok or the first failing
// check of Derive/Capture) and the optional header shape words it showed.
constexpr unsigned kCandidateLines=24;
// Candidates (each a full Capture on the mission owner) per slot and allocation.
constexpr unsigned kCandidatesPerSlot=16;
void CandidateReceipt(void (*log)(const char*) noexcept,unsigned slot,std::uint16_t equip,contract::SubstitutePass pass,
                      const contract::CacheEntry& e,const char* reason,const contract::Snapshot& candidate_scratch) noexcept {
    if(!log || general.candidate_lines>=kCandidateLines) return;
    ++general.candidate_lines;
    char shape[48]="none";
    const auto* h=candidate_scratch.optional.header_bytes.data();
    if(contract::Word32(h)==0x0C012B72)
        StringCchPrintfA(shape,sizeof(shape),"%u.%u.%u.%u.%u.%u",unsigned(contract::Word16(h+8)),unsigned(contract::Word16(h+10)),
            unsigned(contract::Word16(h+12)),unsigned(contract::Word16(h+14)),unsigned(contract::Word16(h+16)),unsigned(contract::Word16(h+18)));
    char line[384]{};
    StringCchPrintfA(line,sizeof(line),
        "equip_writer=candidate; mission=%u; slot=%u; want=%u; equip=%u; tier=c; pass=%s; type=%u; receiver=%u; flags=%u; refs=%u; reason=%s; shape=%s; native_calls=0\r\n",
        general.mission,slot,unsigned(status.slots[slot].want),unsigned(equip),contract::SubstitutePassName(pass),unsigned(e.type),
        unsigned(e.receiver),unsigned(e.flags),e.refs,reason ? reason : "ok",shape);
    log(line);
}
// Picks slot `slot`'s target: A1 the manifest's base id (of the manifest's
// type), C after ANY A1 refusal (substitute only) a resident protected
// unreferenced stock gun (the same type and receiver, the same type, then the
// slot class), D refuse.
template<class View> void Plan(const GeneralContext& c,View& v,std::uintptr_t engine,unsigned slot,
                               void (*log)(const char*) noexcept) noexcept {
    auto& s=status.slots[slot];
    s.want=c.want[slot];
    s.want_type=c.type[slot];
    if(!c.manifest) {
        s.state=SlotState::plan_wait;SetReason(s,"no_manifest");
        if(++general.plan_waits>=kPlanWaitLimit) {s.tier=Tier::d;Advance(log,slot,SlotState::refused,"refused","no_manifest");}
        return;
    }
    // The remote carries nothing in this slot (no alias, no base): nothing to follow.
    if(!s.want && !c.alias[slot]) {s.tier=Tier::d;Advance(log,slot,SlotState::refused,"refused","no_base");return;}
    // The viewer's own weapons (vars.weapons u16[6] at the start of the ammo block) are never a target of
    // either tier: never an entry the viewer's own player uses, whatever its census refs say.
    LocalVars viewer{};
    const bool viewer_ok=ReadLocalVars(c.base,viewer);
    std::array<std::uint16_t,6> viewer_weapons{};
    if(viewer_ok) for(unsigned i=0;i<viewer_weapons.size();++i) viewer_weapons[i]=contract::Word16(viewer.ammo.data()+i*2);
    contract::Target t{};
    const char* why=nullptr;
    if(!s.want) why="no_base";
    else {
        why=contract::Derive(v,engine,s.want,general.census,t);
        if(!why && t.slot!=slot) why="type_slot";
        // A1 needs the manifest's own type of the slot (version 2; 0 unknown).
        if(!why && c.type[slot] && t.type!=c.type[slot]) why="type_mismatch";
        if(!why && !contract::ResidentProtected(general.census[t.cache_index]))
            why=general.census[t.cache_index].refs ? "cache_shared" : "cache_unprotected";
        if(!why && general.census[t.cache_index].type!=t.type) why="cache_type";
        if(!why && !viewer_ok) why="local_vars_unread";
        if(!why) for(auto id:viewer_weapons) if(id && id==s.want) why="viewer_weapon";
    }
    Tier tier=why ? Tier::d : Tier::a1;
    if(why && c.substitute) {
        if(log) {
            char line[192]{};
            StringCchPrintfA(line,sizeof(line),"equip_writer=a1_refused; mission=%u; slot=%u; want=%u; type=%u; reason=%s; next=c; native_calls=0\r\n",
                general.mission,slot,unsigned(s.want),unsigned(c.type[slot]),why);
            log(line);
        }
        // The want's table type and GunBasic receiver (the fallbacks of a version 1 manifest).
        std::uint8_t want_type=0,want_receiver=0;
        std::uintptr_t types=0,resource=0,basic=0;std::uint16_t packed=0;std::array<std::uint8_t,12> recipe{};
        if(s.want && s.want<0x230 && v.At(engine,8,types) && types==v.base+0x2A70920 && v.Method(types,0x22AD180,0,0xA2A3E0) &&
           v.At(types,8+std::size_t(s.want)*2,packed)) {
            want_type=static_cast<std::uint8_t>(packed&0x3F);
            const std::size_t row=packed>>6;
            if(row && row<=0x1818/12 && v.At(engine,0x10,resource) && resource==v.base+0x2A711F0 && v.At(resource,8,basic) &&
               basic==v.base+0x2C25C50 && v.At(basic,(row-1)*12,recipe)) want_receiver=recipe[0];
        }
        contract::SubstituteQuery q{};
        q.slot=slot;q.want=s.want;
        q.type=contract::SubstituteType(c.type[slot],want_type,slot);
        q.receiver=c.receiver[slot] ? c.receiver[slot] : want_receiver;
        for(unsigned i=0;i<kSlots;++i) if(i!=slot) q.dispatched[i]=general.dispatched_equips[i];
        q.viewer=viewer_weapons;
        const char* last_why=viewer_ok ? nullptr : "local_vars_unread";
        bool picked=false;
        std::array<bool,contract::kCacheEntries> tried{};
        for(unsigned p=0;viewer_ok && !picked && p<3;++p) {
            const auto pass=static_cast<contract::SubstitutePass>(p);
            for(;;) {
                const unsigned i=contract::NextCandidate(general.census,q,pass,tried);
                if(i>=contract::kCacheEntries) break;
                if(s.candidates>=kCandidatesPerSlot) {last_why="candidate_limit";p=3;break;}
                tried[i]=true;
                const auto& e=general.census[i];
                ++s.candidates;s.candidate=e.equip;
                general_scratch={};
                contract::Target candidate{};
                const char* reason=contract::CandidateExcluded(e,q);
                if(!reason) reason=contract::Derive(v,engine,e.equip,general.census,candidate);
                if(!reason && (candidate.slot!=slot || candidate.cache_index!=i || candidate.type!=e.type)) reason="candidate_shape";
                if(!reason) {
                    // Its packages must be loaded in this mission (the whole Capture holds).
                    auto reader=Read;
                    reason=contract::Capture(c.base,c.image_span,reader,c.player,c.remote_block,general_scratch,candidate);
                }
                // The census is the allocation's first snapshot: an entry referenced since (an earlier
                // slot's dispatch, the local player) is never picked (the fresh refs of the Capture;
                // Preflight refuses the same, but only after the slot's tier was spent).
                if(!reason && general_scratch.cache_refs) reason="stock_cache_in_use";
                CandidateReceipt(log,slot,e.equip,pass,e,reason,general_scratch);
                if(!reason) {picked=true;t=candidate;break;}
                last_why=reason;
            }
        }
        // A final refusal names the last candidate (s.candidate) and its reason.
        if(picked) {why=nullptr;tier=Tier::c;}
        else why=last_why ? last_why : "no_resident";
    }
    if(why) {
        s.tier=Tier::d;s.equip=0;
        Advance(log,slot,SlotState::refused,"refused",why);
        return;
    }
    general.target=t;
    s.tier=tier;s.equip=t.equip;s.optional=t.optional;s.type=t.type;s.cache_index=static_cast<std::uint8_t>(t.cache_index);
    s.state=SlotState::readiness;SetReason(s,"planned");
    general.readiness.Reset();general.readiness_logged=false;
    GeneralReceipt(log,"plan",slot,"planned");
}
#endif
}

bool PinsIntact(std::uintptr_t base,std::size_t span,std::uintptr_t& mismatch) noexcept {
    return BodyGate(base,span,mismatch);
}
void BeginAllocation(std::uint32_t mission) noexcept {
#ifdef MGS_LOCAL_STOCK46_WRITER_TEST
    authorization.Begin(mission);readiness.Reset();before={};scratch={};observed_epoch=expected_handle=observations=0;
    tracking=readiness_logged=false;
    general=General{};general.active=true;general.mission=mission;general_before={};general_scratch={};
    status=Status{};status.active=true;status.mission=mission;
#else
    (void)mission;
#endif
}
void End() noexcept {
#ifdef MGS_LOCAL_STOCK46_WRITER_TEST
    authorization.End();readiness.Reset();before={};scratch={};tracking=readiness_logged=false;observations=0;
    general=General{};general_before={};general_scratch={};status.active=false;
#endif
}
void ReadStatus(Status& out) noexcept {
#ifdef MGS_LOCAL_STOCK46_WRITER_TEST
    out=status;
#else
    out=Status{};
#endif
}
void Observe(const Context& c,void (*log)(const char*) noexcept) noexcept {
#ifdef MGS_LOCAL_STOCK46_WRITER_TEST
    if(!c.enabled) return;
    if(tracking) {
        if(!c.owner || !c.offline_gates || !c.body_ready || c.mission!=30010 || c.body_epoch!=observed_epoch ||
           c.player!=before.player || c.remote_block!=before.bodies[1]) {tracking=false;Receipt(log,"observation_stopped","owner_lease_changed");return;}
        bool bound=false;
        const char* reason=Readback(c.base,c.image_span,c.player,c.remote_block,contract::kStock46,expected_handle,before,scratch,bound);
        if(reason || bound || ++observations>=50) {
            tracking=false;Receipt(log,bound ? "handle_bound" : "observation_stopped",
                reason ? reason : bound ? "ok" : "model_wait_limit");
        }
        return;
    }
    // Wait on pure observations before spending the one mutation attempt.
    // Large all-bank snapshots use module storage rather than game-thread stack.
    auto reader=Read;const char* reason=nullptr;
    if(authorization.CanObserveReadiness(c)) {
        if(!readiness_logged) observed_epoch=c.body_epoch;
        else if(c.body_epoch!=observed_epoch) {
            authorization.Try(c);Receipt(log,"refused","readiness_body_epoch_changed");return;
        }
        reason=contract::Preflight(c.base,c.image_span,reader,c.player,c.remote_block,before);
        const char* waiting=readiness.Observe(reason==nullptr);
        if(!readiness_logged) {
            readiness_logged=true;PackageReceipt(log,c.base,c.image_span,before.package_observation,contract::kStock46,"stock46_writer",readiness.Count());
            OptionalReceipt(log,before.optional,"stock46_writer",true);
            if(reason) Receipt(log,"readiness_wait",reason);
        }
        if(waiting) {
            if(std::strcmp(waiting,"readiness_limit")==0) {
                authorization.Try(c);Receipt(log,"refused",reason ? reason : waiting);
            }
            return;
        }
    }
    reason=authorization.Try(c);
    if(reason) {
        if(authorization.Attempted() && std::strcmp(reason,"already_attempted")!=0) Receipt(log,"refused",reason);
        return;
    }
    observed_epoch=c.body_epoch;
    reason=contract::Preflight(c.base,c.image_span,reader,c.player,c.remote_block,before);
    if(reason) {Receipt(log,"refused",reason);return;}
    expected_handle=contract::ExpectedHandle(before.new_index,contract::kStock46);
    std::uintptr_t mismatch=0;
    if(!BodyGate(c.base,c.image_span,mismatch)) {Receipt(log,"refused","full_body_gate",INT32_MIN,mismatch);return;}
    // Re-read after hashing, directly before mutation. Even a successful
    // preflight cannot lend stale pointers to the dispatcher.
    auto& refreshed=scratch;
    reason=contract::Preflight(c.base,c.image_span,reader,c.player,c.remote_block,refreshed);
    if(!reason) reason=contract::LocalUnchanged(before,refreshed);
    if(!reason) reason=LeaseChanged(before,refreshed);
    if(reason) {Receipt(log,"refused",reason);return;}
    const Request request{};std::int32_t result=INT32_MIN;std::uintptr_t returned=0;
    const bool called=Call(c.base+0x11AC2E0,before.iface,&request,&result,&returned);
    bool bound=false;
    const char* readback=Readback(c.base,c.image_span,c.player,c.remote_block,contract::kStock46,expected_handle,before,scratch,bound);
    if(!called || returned!=reinterpret_cast<std::uintptr_t>(&result) || result!=0 || readback) {
        Receipt(log,"call_unverified",readback ? readback : !called ? "seh_fault" : "dispatch_result",result);return;
    }
    Receipt(log,bound ? "handle_bound" : "handle_queued",nullptr,result);
    if(!bound) {tracking=true;observations=0;}
#else
    (void)c;(void)log;
#endif
}
void ObserveGeneral(const GeneralContext& c,void (*log)(const char*) noexcept) noexcept {
#ifdef MGS_LOCAL_STOCK46_WRITER_TEST
    auto& g=general;
    if(!c.enabled || !g.active || c.mission!=g.mission) return;
    status.substitute=c.substitute;
    if(g.cursor>=kSlots) return;
    auto reader=Read;
    contract::View<decltype(reader)> v{reader,c.base,c.image_span};
    if(g.tracking) {
        const unsigned slot=g.order[g.cursor];
        if(!c.owner || !c.gates || !c.body_ready || c.body_epoch!=g.observed_epoch ||
           c.player!=general_before.player || c.remote_block!=general_before.bodies[1]) {
            Advance(log,slot,SlotState::unverified,"observation_stopped","owner_lease_changed");return;
        }
        bool bound=false;
        const char* reason=Readback(c.base,c.image_span,c.player,c.remote_block,g.target,g.expected,general_before,general_scratch,bound);
        if(reason) {Advance(log,slot,SlotState::unverified,"observation_stopped",reason);return;}
        if(bound) {Advance(log,slot,SlotState::bound,"handle_bound",nullptr);return;}
        if(++g.observations>=50) Advance(log,slot,SlotState::queued,"observation_stopped","model_wait_limit");
        return;
    }
    // Nothing is spent while an owner gate is down (not the owner thread: silent;
    // any other gate: the pending slot reports it).
    if(!c.owner) return;
    if(!c.gates || !c.body_ready || !c.player || !c.remote_block || !c.base || !c.image_span) {
        Waiting(log,g.order[g.cursor],!c.gates ? (c.gate_reason ? c.gate_reason : "gates") : "body");
        return;
    }
    if(!g.epoch_set) {g.epoch_set=true;g.observed_epoch=c.body_epoch;}
    // The remote's current slot goes last; the order is fixed once a manifest
    // is known, never after a slot was decided.
    if(!g.ordered && (c.manifest || g.cursor>0)) {
        if(g.cursor==0) {
            SlotOrder(c.remote_slot,g.order);
            for(auto& pending:status.slots) if(pending.state==SlotState::plan_wait) {pending.state=SlotState::idle;SetReason(pending,"ordered");}
        }
        g.ordered=true;
    }
    const unsigned slot=g.order[g.cursor];
    auto& s=status.slots[slot];
    if(c.body_epoch!=g.observed_epoch) {
        // One attempt per slot per allocation: a re-realized body ends the rest.
        while(g.cursor<kSlots) {
            const unsigned rest=g.order[g.cursor];
            status.slots[rest].tier=status.slots[rest].tier==Tier::none ? Tier::d : status.slots[rest].tier;
            Advance(log,rest,SlotState::refused,"refused","body_epoch_changed");
        }
        return;
    }
    std::uintptr_t engine=0;
    if(!EngineOf(v,c.player,engine)) {Waiting(log,slot,"engine_chain");return;}
    if(!g.census_ok) {
        g.census_ok=contract::Census(v,engine,g.census);
        if(!g.census_ok) {Waiting(log,slot,"census");return;}
        status.census_ok=true;status.resident_count=0;
        for(unsigned i=0;i<contract::kCacheEntries;++i) {
            const auto& e=g.census[i];
            if(!(e.flags&1)) continue;
            auto& r=status.resident[status.resident_count++];
            r.equip=e.equip;r.type=e.type;r.receiver=e.receiver;r.flags=e.flags;r.index=static_cast<std::uint8_t>(i);r.refs=e.refs;
        }
        if(!g.census_logged) {g.census_logged=true;CensusReceipt(log);}
    }
    if(s.state==SlotState::idle || s.state==SlotState::plan_wait) {
        Plan(c,v,engine,slot,log);
        return;
    }
    if(s.state!=SlotState::readiness) return;
    // Readiness: the strict preflight of this target, at most 50 observations.
    const char* reason=contract::Preflight(c.base,c.image_span,reader,c.player,c.remote_block,general_before,g.target);
    if(!reason) reason=OtherSlots(general_before,slot);
    const char* waiting=g.readiness.Observe(reason==nullptr);
    if(!g.readiness_logged) {
        g.readiness_logged=true;
        PackageReceipt(log,c.base,c.image_span,general_before.package_observation,g.target,"equip_writer",g.readiness.Count());
        OptionalReceipt(log,general_before.optional,"equip_writer",g.target.optional);
        if(reason) GeneralReceipt(log,"readiness_wait",slot,reason);
    }
    if(waiting) {
        if(std::strcmp(waiting,"readiness_limit")==0) Advance(log,slot,SlotState::refused,"refused",reason ? reason : waiting);
        return;
    }
    // The slot's one attempt from here on.
    g.expected=contract::ExpectedHandle(general_before.new_index,g.target);
    s.handle=g.expected;
    std::uintptr_t mismatch=0;
    if(!BodyGate(c.base,c.image_span,mismatch)) {Advance(log,slot,SlotState::refused,"refused","full_body_gate",INT32_MIN,mismatch);return;}
    auto& refreshed=general_scratch;
    reason=contract::Preflight(c.base,c.image_span,reader,c.player,c.remote_block,refreshed,g.target);
    if(!reason) reason=OtherSlots(refreshed,slot);
    if(!reason) reason=contract::LocalUnchanged(general_before,refreshed);
    if(!reason) reason=LeaseChanged(general_before,refreshed);
    if(reason) {Advance(log,slot,SlotState::refused,"refused",reason);return;}
    // The viewer's ammo/weapon vars and shoot count, directly around the call.
    LocalVars vars_before{};
    if(!ReadLocalVars(c.base,vars_before)) {Advance(log,slot,SlotState::refused,"refused","local_vars_unread");return;}
    Request request{};request.equip=g.target.equip;
    std::int32_t result=INT32_MIN;std::uintptr_t returned=0;
    ++status.dispatches;
    const bool called=Call(c.base+0x11AC2E0,general_before.iface,&request,&result,&returned);
    LocalVars vars_after{};
    const bool vars_read=ReadLocalVars(c.base,vars_after);
    const bool vars_changed=!vars_read || !SameVars(vars_after,vars_before);
    bool bound=false;
    const char* readback=!vars_read ? "local_vars_unread" : vars_changed ? "local_ammo_changed" :
        Readback(c.base,c.image_span,c.player,c.remote_block,g.target,g.expected,general_before,general_scratch,bound);
    if(!called || returned!=reinterpret_cast<std::uintptr_t>(&result) || result!=0 || readback) {
        // A confirmed handle still occupies the slot: later slots expect it.
        if(called && readback==nullptr) {g.dispatched_handles[slot]=g.expected;g.dispatched_equips[slot]=g.target.equip;}
        else if(called) g.dispatched_equips[slot]=g.target.equip;
        Advance(log,slot,SlotState::unverified,"call_unverified",readback ? readback : !called ? "seh_fault" : "dispatch_result",result);
        // The viewer's vars moved (or became unreadable) across our call: no further dispatch this allocation.
        while(vars_changed && g.cursor<kSlots) {
            const unsigned rest=g.order[g.cursor];
            status.slots[rest].tier=status.slots[rest].tier==Tier::none ? Tier::d : status.slots[rest].tier;
            Advance(log,rest,SlotState::refused,"refused","prior_local_change");
        }
        return;
    }
    g.dispatched_handles[slot]=g.expected;
    g.dispatched_equips[slot]=g.target.equip;
    if(bound) {Advance(log,slot,SlotState::bound,"handle_bound",nullptr,result);return;}
    s.state=SlotState::tracking;SetReason(s,"queued");
    GeneralReceipt(log,"handle_queued",slot,nullptr,result);
    g.tracking=true;g.observations=0;
#else
    (void)c;(void)log;
#endif
}
}
