// Original, separately compiled copied-save stock46 experiment. No hooks,
// worker routing, alias preparation, local-index edits or rollback.
// The generalized writer (ObserveGeneral) dispatches the same qualified
// ChangeEquip request for other stock guns, one slot at a time, each behind the
// stock46-shape preflight with that gun's own derived data. Weapons4: pure-read
// planning and readiness may be retried after a transient refusal (never a
// dispatch), and the body lease re-syncs the slots from body 1's live handles.
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
// A shared target (a1s/cs): the refs bracketing the call (refs_before, the
// re-read directly before it) moved by exactly one (`bracket`: the
// dispatch's own readback; while tracking, others may move them), the bank
// may have reused the target's own (asset, mission entry) entry (`bank`).
const char* Readback(std::uintptr_t base,std::size_t span,std::uintptr_t player,std::uintptr_t remote_block,
                     const contract::Target& t,std::uint32_t expected,const contract::Snapshot& lease,
                     contract::Snapshot& after,bool& bound,bool bracket=false,std::uint32_t refs_before=0,
                     Bank* bank=nullptr) noexcept {
    bound=false;
    if(bank) *bank=Bank::none;
    auto reader=Read;
    if(const auto reason=contract::Capture(base,span,reader,player,remote_block,after,t)) return reason;
    if(const auto reason=contract::LocalUnchanged(lease,after)) return reason;
    if(after.remote_handles[t.slot]!=expected) return "remote_handle_or_refs";
    if(!t.shared && after.cache_refs!=lease.cache_refs+1) return "remote_handle_or_refs";
    if(t.shared && bracket && after.cache_refs!=refs_before+1) return "shared_refs_moved";
    if(t.shared && !after.cache_refs) return "remote_handle_or_refs";
    for(unsigned i=0;i<7;++i) if(i!=t.slot && after.remote_handles[i]!=lease.remote_handles[i]) return "other_remote_slot_changed";
    contract::View<decltype(reader)> view{reader,base,span};
    if(const auto reason=contract::DelayedCachesPreserved(view,lease,t.shared)) return reason;
    contract::Bytes14 row{};std::uint32_t mapped=0;
    if(!view.At(after.meta_rows,lease.new_index*0xE,row) || contract::Word16(row.data())!=t.equip ||
       row[8]!=t.cache_index || !view.At(after.map_rows,lease.new_index*4,mapped)) return "new_handle_owner";
    const auto current=contract::Word32(after.remote_binder.data()+t.slot*12);
    const auto pending=contract::Word32(after.remote_binder.data()+t.slot*12+4);
    if(current!=expected && pending!=expected) return "model_handle_missing";
    if(current!=expected) return nullptr; // queued, not rendered evidence
    if(mapped>=60) return "bound_without_resource";
    bool bank_shared=false;
    if(const auto reason=contract::QualifiedBoundResource(view,lease,mapped,t.shared,&bank_shared)) return reason;
    if(bank) *bank=bank_shared ? Bank::shared : Bank::fresh;
    // Native DBF1F0 maps a bounded resource ID across group sizes. Follow the
    // same pure data chain; ADBE00's bank getter itself has no index check.
    std::uintptr_t groups=0,group=0,resource_bank=0,resources=0,pointer=0;std::uint32_t size=0,count=0;
    if(!view.At(after.engine,0x130,groups)) return "bound_resource_groups";
    bool found=false;
    for(unsigned i=0;i<32;++i) {
        if(!view.At(groups,i*8,group) || !view.At(group,0x30,size) || size!=(i<28?2u:1u)) return "bound_resource_group";
        if(mapped<size) {found=true;break;}
        mapped-=size;
    }
    if(!found || !view.At(group,0x10,resource_bank) || !view.Method(resource_bank,0x22E2970,0x240,0xADBE00) ||
       !view.At(resource_bank,0x50,count) || count!=size || mapped>=count ||
       !view.At(resource_bank,0x58,resources) || !view.At(resources,mapped*8,pointer) || !pointer) return "bound_resource_pointer";
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
// Weapons4: the slots are scheduled round-robin (PickPosition: a waiting slot
// yields, the remote's current slot last on the first pass), a refusal before
// a dispatch backs off and plans again (RetryAfter), the body lease
// (realize_gen + Capture's identity) re-syncs the slots from body 1's live
// handles instead of the motion epoch ending the allocation.
struct General {
    bool active=false,ordered=false,lease_set=false,tracking=false;
    std::uint32_t mission=0,observed_epoch=0,expected=0;
    // The last logged wait reason (a static string; logged on change, at most kWaitLines per allocation).
    const char* wait_logged=nullptr;
    unsigned wait_lines=0;
    std::uint8_t order[kSlots]{0,2,1};
    unsigned cursor=0,plan_waits=0,observations=0,tracking_slot=kSlots;
    Readiness readiness[kSlots];
    bool readiness_logged[kSlots]{},binder_logged[kSlots]{};
    contract::Target targets[kSlots]{};
    SlotSchedule sched[kSlots]{};
    BodyLease lease{};
    bool manifest_set=false;
    ManifestKey manifest_key{};
    std::uint32_t dispatched_handles[kSlots]{};
    std::uint16_t dispatched_equips[kSlots]{};  // never a tier C candidate of another slot
    unsigned candidate_lines=0,body_lines=0,retry_lines=0;
    std::uint32_t census_logged_gen=UINT32_MAX;
    std::array<contract::CacheEntry,contract::kCacheEntries> census{};
};
General general;
contract::Snapshot general_before;
contract::Snapshot general_scratch;
Status status;
// Observations (~100 ms each) a slot waits for the manifest before refusing.
constexpr unsigned kPlanWaitLimit=300;
constexpr unsigned kWaitLines=8;
// equip_writer=body (re-sync) and equip_writer=backoff lines per allocation.
constexpr unsigned kBodyLines=8;
constexpr unsigned kRetryLines=24;
void SetReason(SlotStatus& slot,const char* reason) noexcept {StringCchCopyA(slot.reason,sizeof(slot.reason),reason ? reason : "ok");}
void GeneralReceipt(void (*log)(const char*) noexcept,const char* phase,unsigned slot,const char* reason,
                    std::int32_t result=INT32_MIN,std::uintptr_t mismatch=0) noexcept {
    if(!log || slot>=kSlots) return;
    const auto& s=status.slots[slot];
    char line[768]{};
    StringCchPrintfA(line,sizeof(line),
        "equip_writer=%s; reason=%s; mission=%u; slot=%u; want=%u; want_type=%u; equip=%u; tier=%s; family=%s; type=%u; cache=%u; candidates=%u; candidate=%u; epoch=%u; gen=%u; expected_handle=%x; result=%d; mismatch_rva=%llx; dispatches=%u; refs_before=%u; refs_after=%u; bank=%s; shared=%s; instance=1; retry=%u; alias_call=0; rollback_calls=0; replace_filled=0; evict=0\r\n",
        phase,reason ? reason : "ok",general.mission,slot,unsigned(s.want),unsigned(s.want_type),unsigned(s.equip),TierName(s.tier),
        s.equip ? (s.optional ? "optional" : "plain") : "none",unsigned(s.type),unsigned(s.cache_index),s.candidates,unsigned(s.candidate),
        general.observed_epoch,general.lease.gen,s.handle,result,static_cast<unsigned long long>(mismatch),status.dispatches,
        s.refs_before,s.refs_after,BankName(s.bank),SharedModeName(status.shared),unsigned(s.retries));
    log(line);
}
void CensusReceipt(void (*log)(const char*) noexcept) noexcept {
    if(!log) return;
    char line[2560]{};
    StringCchPrintfA(line,sizeof(line),"equip_resident; mission=%u; gen=%u; used=%u; entries=",general.mission,general.lease.gen,
        unsigned(status.resident_count));
    for(unsigned i=0;i<status.resident_count;++i) {
        const auto& e=status.resident[i];
        char item[56]{};
        StringCchPrintfA(item,sizeof(item),"%s%u:%u/t%u/r%u/f%u/n%u/v%u",i ? "," : "",unsigned(e.index),unsigned(e.equip),unsigned(e.type),
                         unsigned(e.receiver),unsigned(e.flags),e.refs,unsigned(e.viewer));
        StringCchCatA(line,sizeof(line),item);
    }
    StringCchCatA(line,sizeof(line),"; native_calls=0\r\n");
    log(line);
}
// The body lease (BodyLease): the player, body 1, its model and binder
// records, the inventory and the engine, read through the same pinned
// methods Capture checks (Capture re-reads and pins the whole chain before
// any use; this only keys the re-sync and feeds the pure census).
template<class View> bool LeaseOf(View& v,const GeneralContext& c,BodyLease& lease) noexcept {
    std::uintptr_t iface=0;
    lease=BodyLease{};lease.gen=c.realize_gen;lease.player=c.player;lease.body=c.remote_block;
    return v.At(c.player,0xC8,iface) && v.Method(iface,0x23C1720,0x198,0x11AC2E0) && v.At(iface,8,lease.inventory) &&
        v.Method(lease.inventory,0x23895D0,8,0xEE9880) && v.At(lease.inventory,0x58,lease.engine) &&
        v.Method(lease.engine,0x234B840,0,0xDC3250) && v.At(c.remote_block,0x618,lease.model) &&
        v.Method(lease.model,0x22EA550,0x20,0xB0C720) && v.At(lease.model,0x30,lease.binder) && lease.binder;
}
// Body 1's live handles of the three gun slots (inventory handles 7..9).
template<class View> bool LiveHandles(View& v,std::uintptr_t inventory,std::uint32_t (&live)[kSlots]) noexcept {
    std::uintptr_t handles=0;std::uint32_t stride=0;
    if(!v.At(inventory,0xC,stride) || stride!=7 || !v.At(inventory,0x40,handles) || !handles) return false;
    for(unsigned s=0;s<kSlots;++s) if(!v.At(handles,(7+s)*4,live[s])) return false;
    return true;
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
// A slot's decision: its state, reason and receipt.
void Finish(void (*log)(const char*) noexcept,unsigned slot,SlotState state,const char* phase,const char* reason,
            std::int32_t result=INT32_MIN,std::uintptr_t mismatch=0) noexcept {
    auto& s=status.slots[slot];
    s.state=state;
    if(state==SlotState::unverified) general.sched[slot].uncertain=true;
    SetReason(s,reason);
    if(state==SlotState::refused || state==SlotState::unverified) ++status.refused;
    if(state==SlotState::bound) ++status.bound;
    GeneralReceipt(log,phase,slot,reason,result,mismatch);
    general.readiness[slot].Reset();general.readiness_logged[slot]=false;
    if(general.tracking && general.tracking_slot==slot) {general.tracking=false;general.tracking_slot=kSlots;general.observations=0;}
}
// A refusal before the slot's dispatch: transient ones back off and plan
// again (RetryAfter), permanent ones (or a spent budget) end the slot this gen.
void Refuse(void (*log)(const char*) noexcept,unsigned slot,const char* reason,bool permanent,std::uint64_t now,
            bool plan_time,std::uintptr_t mismatch=0) noexcept {
    auto& s=status.slots[slot];
    auto& sc=general.sched[slot];
    if(RetryAfter(sc,permanent,now)) {
        s.retries=sc.retries;s.state=SlotState::backoff;SetReason(s,reason);
        general.readiness[slot].Reset();general.readiness_logged[slot]=false;
        if(general.retry_lines<kRetryLines) {++general.retry_lines;GeneralReceipt(log,"backoff",slot,reason,INT32_MIN,mismatch);}
        return;
    }
    if(plan_time) {s.tier=Tier::d;s.equip=0;}
    Finish(log,slot,SlotState::refused,"refused",reason,INT32_MIN,mismatch);
}
// Nothing spent: the pending slot reports why the writer waits (gates, body,
// engine_chain, census, body_busy); a change is logged (capped).
void Waiting(void (*log)(const char*) noexcept,unsigned slot,const char* reason) noexcept {
    if(slot>=kSlots) return;
    auto& s=status.slots[slot];
    if(FinalState(s.state) || s.state==SlotState::tracking) return;
    SetReason(s,reason);
    if(general.wait_logged==reason || general.wait_lines>=kWaitLines || !log) return;
    general.wait_logged=reason;++general.wait_lines;
    char line[192]{};
    StringCchPrintfA(line,sizeof(line),"equip_writer=waiting; reason=%s; mission=%u; slot=%u; gen=%u; native_calls=0\r\n",
        reason ? reason : "none",general.mission,slot,general.lease.gen);
    log(line);
}
// One line per tier C candidate that passed the census filters (capped per
// allocation): its pass, type, receiver, the reason (ok or the first failing
// check of Derive/Capture) and the optional header shape words it showed.
constexpr unsigned kCandidateLines=24;
// Candidates (each a full Capture on the mission owner) per slot and plan.
constexpr unsigned kCandidatesPerSlot=16;
void CandidateReceipt(void (*log)(const char*) noexcept,unsigned slot,std::uint16_t equip,contract::SubstitutePass pass,
                      const contract::CacheEntry& e,const char* reason,const contract::Snapshot& candidate_scratch,bool shared) noexcept {
    if(!log || general.candidate_lines>=kCandidateLines) return;
    ++general.candidate_lines;
    char shape[48]="none";
    const auto* h=candidate_scratch.optional.header_bytes.data();
    if(contract::Word32(h)==0x0C012B72)
        StringCchPrintfA(shape,sizeof(shape),"%u.%u.%u.%u.%u.%u",unsigned(contract::Word16(h+8)),unsigned(contract::Word16(h+10)),
            unsigned(contract::Word16(h+12)),unsigned(contract::Word16(h+14)),unsigned(contract::Word16(h+16)),unsigned(contract::Word16(h+18)));
    char line[448]{};
    StringCchPrintfA(line,sizeof(line),
        "equip_writer=candidate; mission=%u; slot=%u; want=%u; equip=%u; tier=%s; pass=%s; type=%u; receiver=%u; flags=%u; refs=%u; viewer=%u; reason=%s; shape=%s; native_calls=0\r\n",
        general.mission,slot,unsigned(status.slots[slot].want),unsigned(equip),shared ? "cs" : "c",contract::SubstitutePassName(pass),
        unsigned(e.type),unsigned(e.receiver),unsigned(e.flags),e.refs,unsigned(e.viewer),reason ? reason : "ok",shape);
    log(line);
}
// The census of the allocation's current gen (refreshed before every plan:
// a retry sees the entries referenced or released since), the viewer's
// references included; one equip_resident line per gen.
template<class View> bool TakeCensus(View& v,void (*log)(const char*) noexcept) noexcept {
    std::array<std::uint8_t,contract::kCacheEntries> viewer{};
    if(!contract::Census(v,general.lease.engine,general.census) ||
       !contract::ViewerReferences(v,general.lease.inventory,general.lease.engine,viewer)) {status.census_ok=false;return false;}
    for(unsigned i=0;i<contract::kCacheEntries;++i) general.census[i].viewer=viewer[i];
    status.census_ok=true;status.resident_count=0;
    for(unsigned i=0;i<contract::kCacheEntries;++i) {
        const auto& e=general.census[i];
        if(!(e.flags&1)) continue;
        auto& r=status.resident[status.resident_count++];
        r.equip=e.equip;r.type=e.type;r.receiver=e.receiver;r.flags=e.flags;r.index=static_cast<std::uint8_t>(i);r.refs=e.refs;
        r.viewer=e.viewer;
    }
    if(general.census_logged_gen!=general.lease.gen) {general.census_logged_gen=general.lease.gen;CensusReceipt(log);}
    return true;
}
// Picks slot `slot`'s target: A1 the manifest's base id (of the manifest's
// type; A1s when other actors reference its entry and shared tiers are on),
// C after ANY A1 refusal (substitute only) a resident protected stock gun (the
// same type and receiver, the same type, then the slot class; per pass the
// unreferenced entries then the shared ones, `prefer` the reverse), D refuse.
template<class View> void Plan(const GeneralContext& c,View& v,unsigned slot,void (*log)(const char*) noexcept) noexcept {
    auto& s=status.slots[slot];
    s.want=c.want[slot];
    s.want_type=c.type[slot];
    if(!c.manifest) {
        s.state=SlotState::plan_wait;SetReason(s,"no_manifest");
        if(++general.plan_waits>=kPlanWaitLimit) Refuse(log,slot,"no_manifest",true,c.now_ms,true);
        return;
    }
    // The remote carries nothing in this slot (no alias, no base): nothing to follow.
    if(!s.want && !c.alias[slot]) {Refuse(log,slot,"no_base",true,c.now_ms,true);return;}
    const std::uintptr_t engine=general.lease.engine;
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
        if(!why) {
            const auto& e=general.census[t.cache_index];
            if(!contract::ResidentProtected(e)) {
                // A1s: referenced by other actors only (never the viewer), shared tiers on.
                if(e.refs && e.flags==3 && c.shared!=SharedMode::off) {
                    why=contract::SharedRefusal(e);
                    if(!why) t.shared=true;
                } else why=e.refs ? "cache_shared" : "cache_unprotected";
            }
        }
        if(!why && general.census[t.cache_index].type!=t.type) why="cache_type";
        if(!why && !viewer_ok) why="local_vars_unread";
        if(!why) for(auto id:viewer_weapons) if(id && id==s.want) why="viewer_weapon";
        t.measured=c.measured;
    }
    Tier tier=why ? Tier::d : t.shared ? Tier::a1s : Tier::a1;
    bool permanent=PermanentReason(why);
    if(why && c.substitute) {
        if(log) {
            char line[224]{};
            StringCchPrintfA(line,sizeof(line),"equip_writer=a1_refused; mission=%u; slot=%u; want=%u; type=%u; reason=%s; next=c; shared=%s; native_calls=0\r\n",
                general.mission,slot,unsigned(s.want),unsigned(c.type[slot]),why,SharedModeName(c.shared));
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
        bool picked=false,any_transient=!viewer_ok;
        std::array<bool,contract::kCacheEntries> tried{};
        // Per pass the populations in order: off unreferenced only, on unreferenced then shared, prefer the reverse.
        const unsigned populations=c.shared==SharedMode::off ? 1u : 2u;
        unsigned tried_count=0;
        for(unsigned p=0;viewer_ok && !picked && p<3;++p) {
            const auto pass=static_cast<contract::SubstitutePass>(p);
            for(unsigned k=0;!picked && k<populations;++k) {
                q.shared=c.shared==SharedMode::prefer ? k==0 : k==1;
                for(;;) {
                    const unsigned i=contract::NextCandidate(general.census,q,pass,tried);
                    if(i>=contract::kCacheEntries) break;
                    if(tried_count>=kCandidatesPerSlot) {last_why="candidate_limit";p=3;k=populations;break;}
                    tried[i]=true;++tried_count;
                    const auto& e=general.census[i];
                    ++s.candidates;s.candidate=e.equip;
                    general_scratch={};
                    contract::Target candidate{};
                    const char* reason=contract::CandidateExcluded(e,q);
                    if(!reason) reason=contract::Derive(v,engine,e.equip,general.census,candidate);
                    if(!reason && (candidate.slot!=slot || candidate.cache_index!=i || candidate.type!=e.type)) reason="candidate_shape";
                    candidate.shared=q.shared;candidate.measured=c.measured;
                    if(!reason) {
                        // Its packages must be loaded in this mission (the whole Capture holds).
                        auto reader=Read;
                        reason=contract::Capture(c.base,c.image_span,reader,c.player,c.remote_block,general_scratch,candidate);
                    }
                    // The census is a snapshot: an unshared candidate referenced since (an earlier slot's
                    // dispatch, the local player) is never picked; a shared one never names an entry the viewer
                    // references now (the fresh Capture's body 0 rows) and stays bounded.
                    if(!reason && !q.shared && general_scratch.cache_refs) reason="stock_cache_in_use";
                    if(!reason && q.shared && general_scratch.cache_refs>=contract::kSharedRefsBound) reason="shared_refs_bound";
                    if(!reason && q.shared) for(const auto entry:general_scratch.local_cache_index) if(entry==i) reason="viewer_shared";
                    CandidateReceipt(log,slot,e.equip,pass,e,reason,general_scratch,q.shared);
                    if(!reason) {picked=true;t=candidate;break;}
                    last_why=reason;
                    if(!PermanentReason(reason)) any_transient=true;
                }
            }
        }
        // A final refusal names the last candidate (s.candidate) and its reason; it is permanent only
        // when every candidate's was (a candidate whose packages were not ready yet is retried).
        if(picked) {why=nullptr;tier=t.shared ? Tier::cs : Tier::c;}
        else {
            why=last_why ? last_why : "no_resident";
            permanent=!any_transient && PermanentReason(why);
        }
    }
    if(why) {Refuse(log,slot,why,permanent,c.now_ms,true);return;}
    general.targets[slot]=t;
    s.tier=tier;s.equip=t.equip;s.optional=t.optional;s.type=t.type;s.cache_index=static_cast<std::uint8_t>(t.cache_index);
    s.refs_before=general.census[t.cache_index].refs;s.refs_after=0;s.bank=Bank::none;
    s.state=SlotState::readiness;SetReason(s,"planned");
    general.readiness[slot].Reset();general.readiness_logged[slot]=false;
    GeneralReceipt(log,"plan",slot,"planned");
}
// The body lease changed (realize_gen or Capture's identity): every slot
// re-syncs from body 1's live handle (ResyncSlot): the handle this allocation
// dispatched is kept, an empty slot plans again, anything else is a foreign
// handle (never replaced). The tracking dispatch ends with it; the gen's
// retry and dispatch budgets start again (at most kDispatchesPerAllocation).
void ResyncBody(void (*log)(const char*) noexcept,const BodyLease& lease,const std::uint32_t (&live)[kSlots],
                std::uint32_t epoch) noexcept {
    auto& g=general;
    const char* why=lease.gen!=g.lease.gen ? "realize_gen" : "identity";
    unsigned kept=0,empty=0,foreign=0;
    g.tracking=false;g.tracking_slot=kSlots;g.observations=0;
    for(unsigned slot=0;slot<kSlots;++slot) {
        auto& s=status.slots[slot];
        auto& sc=g.sched[slot];
        sc.retries=0;sc.gen_dispatches=0;sc.visited=false;s.retries=0;
        g.readiness[slot].Reset();g.readiness_logged[slot]=false;
        // A failed dispatch remains latched even when a new body is empty or
        // its handle happens to match. A handle alone cannot verify model binding.
        if(sc.uncertain) {s.state=SlotState::unverified;continue;}
        const Resync r=ResyncSlot(g.dispatched_handles[slot],live[slot]);
        if(r==Resync::kept) {
            ++kept;
            s.state=RetainedSlotState(s.state);
            if(s.state==SlotState::bound) SetReason(s,"kept");
            else if(s.state==SlotState::queued) SetReason(s,"kept_unbound");
            // Do not resume Readback with the prior body's snapshot, or
            // count a retained queued handle as a verified model binding.
        } else if(r==Resync::empty) {
            ++empty;
            g.dispatched_handles[slot]=0;g.dispatched_equips[slot]=0;
            // A permanent refusal stays (nothing in the slot can change it; a foreign handle
            // that left can); anything else plans again.
            if(!(s.state==SlotState::refused && PermanentReason(s.reason) && std::strcmp(s.reason,"foreign_handle")!=0)) {
                s.state=SlotState::idle;s.tier=Tier::none;s.equip=0;s.handle=0;s.bank=Bank::none;SetReason(s,"resync");
            }
        } else {
            ++foreign;
            // Keep only our earlier dispatched identity. This foreign handle
            // must not become ours at the next realize generation.
            s.state=SlotState::refused;SetReason(s,"foreign_handle");++status.refused;
        }
    }
    ++status.resyncs;status.kept+=kept;
    if(log && g.body_lines<kBodyLines) {
        ++g.body_lines;
        char line[320]{};
        StringCchPrintfA(line,sizeof(line),
            "equip_writer=body; reason=%s; mission=%u; gen=%u; previous_gen=%u; epoch=%u; kept=%u; empty=%u; foreign=%u; handles=%x,%x,%x; native_calls=0\r\n",
            why,g.mission,lease.gen,g.lease.gen,epoch,kept,empty,foreign,live[0],live[1],live[2]);
        log(line);
    }
    g.lease=lease;status.gen=lease.gen;
}
// The binder record words of the slot (once per slot and allocation):
// equip_writer=binder; slot=..; words=current,pending,flags.
void BinderReceipt(void (*log)(const char*) noexcept,unsigned slot,const contract::Snapshot& s) noexcept {
    if(!s.binder_records[1]) return;
    auto& out=status.slots[slot];
    for(unsigned i=0;i<3;++i) out.binder[i]=contract::Word32(s.remote_binder.data()+slot*12+i*4);
    if(general.binder_logged[slot] || !log) return;
    general.binder_logged[slot]=true;
    char line[224]{};
    StringCchPrintfA(line,sizeof(line),"equip_writer=binder; mission=%u; slot=%u; gen=%u; words=%08x,%08x,%08x; empty=%u; native_calls=0\r\n",
        general.mission,slot,general.lease.gen,out.binder[0],out.binder[1],out.binder[2],
        contract::BinderEmpty(out.binder[0],out.binder[1],out.binder[2]) ? 1u : 0u);
    log(line);
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
    status.substitute=c.substitute;status.shared=c.shared;
    g.observed_epoch=c.body_epoch;
    auto reader=Read;
    contract::View<decltype(reader)> v{reader,c.base,c.image_span};
    // The pending slot (the round-robin cursor's) reports the waits.
    const unsigned pending=g.tracking ? g.tracking_slot : g.order[g.cursor%kSlots];
    // Nothing is spent while an owner gate is down (not the owner thread: silent;
    // any other gate: the pending slot reports it). A tracking dispatch waits too:
    // a body that unrealizes comes back with a new lease (the re-sync below).
    if(!c.owner) return;
    if(!c.gates || !c.body_ready || !c.player || !c.remote_block || !c.base || !c.image_span) {
        Waiting(log,pending,!c.gates ? (c.gate_reason ? c.gate_reason : "gates") : "body");
        return;
    }
    BodyLease lease{};
    if(!LeaseOf(v,c,lease)) {Waiting(log,pending,"engine_chain");return;}
    if(!g.lease_set) {g.lease_set=true;g.lease=lease;status.gen=lease.gen;}
    else if(lease!=g.lease) {
        std::uint32_t live[kSlots]{};
        if(!LiveHandles(v,lease.inventory,live)) {Waiting(log,pending,"body_handles");return;}
        ResyncBody(log,lease,live,c.body_epoch);
    }
    if(g.tracking) {
        const unsigned slot=g.tracking_slot;
        if(slot>=kSlots) {g.tracking=false;return;}
        bool bound=false;Bank bank=Bank::none;
        const char* reason=Readback(c.base,c.image_span,c.player,c.remote_block,g.targets[slot],g.expected,general_before,
                                    general_scratch,bound,false,0,&bank);
        if(reason) {Finish(log,slot,SlotState::unverified,"observation_stopped",reason);return;}
        if(bound) {
            status.slots[slot].refs_after=general_scratch.cache_refs;status.slots[slot].bank=bank;
            Finish(log,slot,SlotState::bound,"handle_bound",nullptr);return;
        }
        if(++g.observations>=50) Finish(log,slot,SlotState::queued,"observation_stopped","model_wait_limit");
        return;
    }
    // The remote's current slot goes last; the order is fixed once a manifest
    // is known, never after a slot was decided.
    bool decided=false;
    for(const auto& s:status.slots) decided=decided || (s.state!=SlotState::idle && s.state!=SlotState::plan_wait);
    if(!g.ordered && (c.manifest || decided)) {
        if(!decided) {
            SlotOrder(c.remote_slot,g.order);
            for(auto& s:status.slots) if(s.state==SlotState::plan_wait) {s.state=SlotState::idle;SetReason(s,"ordered");}
            for(auto& sc:g.sched) sc.visited=false;
        }
        g.ordered=true;
    }
    // Cache residency can arrive after the bounded retries were exhausted.
    // A new received manifest renews only this pre-dispatch readiness; all
    // native-call and ownership state is preserved. Repeated identical data
    // never restarts the timer or retry budget.
    if(c.manifest) {
        const auto key=ManifestOf(c);
        if(g.manifest_set && key!=g.manifest_key) {
            std::uint32_t live[kSlots]{};
            if(!LiveHandles(v,g.lease.inventory,live)) {Waiting(log,pending,"body_handles");return;}
            for(unsigned slot=0;slot<kSlots;++slot) {
                if(!RenewCacheReadiness(status.slots[slot],g.sched[slot],g.dispatched_handles[slot],live[slot])) continue;
                g.readiness[slot].Reset();g.readiness_logged[slot]=false;
                SetReason(status.slots[slot],"cache_renewed");
                if(g.retry_lines<kRetryLines) {++g.retry_lines;GeneralReceipt(log,"cache_renewed",slot,"manifest");}
            }
        }
        g.manifest_key=key;g.manifest_set=true;
    }
    SlotState states[kSlots]{};
    for(unsigned s=0;s<kSlots;++s) states[s]=status.slots[s].state;
    const unsigned position=PickPosition(states,g.sched,g.order,g.cursor%kSlots,c.now_ms);
    if(position>=kSlots) return;
    const unsigned slot=g.order[position];
    auto& s=status.slots[slot];
    auto& sc=g.sched[slot];
    if(s.state==SlotState::idle || s.state==SlotState::plan_wait || s.state==SlotState::backoff) {
        sc.visited=true;g.cursor=(position+1)%kSlots;
        if(!TakeCensus(v,log)) {Waiting(log,slot,"census");return;}
        Plan(c,v,slot,log);
        return;
    }
    if(s.state!=SlotState::readiness) return;
    // Body 1 switching or reloading (gun mode 8/11/12): nothing is dispatched
    // and no readiness observation is spent (not a turn).
    if(c.body_busy) {Waiting(log,slot,"body_busy");return;}
    sc.visited=true;g.cursor=(position+1)%kSlots;
    const auto& target=g.targets[slot];
    // Readiness: the strict preflight of this target, at most 50 observations (a waiting slot yields its turn).
    const char* reason=contract::Preflight(c.base,c.image_span,reader,c.player,c.remote_block,general_before,target);
    BinderReceipt(log,slot,general_before);
    if(!reason) reason=OtherSlots(general_before,slot);
    const char* waiting=g.readiness[slot].Observe(reason==nullptr);
    if(!g.readiness_logged[slot]) {
        g.readiness_logged[slot]=true;
        PackageReceipt(log,c.base,c.image_span,general_before.package_observation,target,"equip_writer",g.readiness[slot].Count());
        OptionalReceipt(log,general_before.optional,"equip_writer",target.optional);
        if(reason) GeneralReceipt(log,"readiness_wait",slot,reason);
    }
    if(waiting) {
        if(std::strcmp(waiting,"readiness_limit")==0) {
            const char* final_reason=reason ? reason : waiting;
            Refuse(log,slot,final_reason,PermanentReason(final_reason),c.now_ms,false);
        }
        return;
    }
    if(!CanDispatch(sc)) {Refuse(log,slot,"dispatch_budget",true,c.now_ms,false);return;}
    // The slot's one attempt this gen from here on.
    g.expected=contract::ExpectedHandle(general_before.new_index,target);
    s.handle=g.expected;
    std::uintptr_t mismatch=0;
    if(!BodyGate(c.base,c.image_span,mismatch)) {Refuse(log,slot,"full_body_gate",true,c.now_ms,false,mismatch);return;}
    auto& refreshed=general_scratch;
    reason=contract::Preflight(c.base,c.image_span,reader,c.player,c.remote_block,refreshed,target);
    if(!reason) reason=OtherSlots(refreshed,slot);
    if(!reason) reason=contract::LocalUnchanged(general_before,refreshed);
    if(!reason) reason=LeaseChanged(general_before,refreshed);
    if(reason) {Refuse(log,slot,reason,PermanentReason(reason),c.now_ms,false);return;}
    // The viewer's ammo/weapon vars and shoot count, directly around the call.
    LocalVars vars_before{};
    if(!ReadLocalVars(c.base,vars_before)) {Refuse(log,slot,"local_vars_unread",false,c.now_ms,false);return;}
    // A shared entry's references directly before the call (others may hold it).
    const std::uint32_t refs_before=refreshed.cache_refs;
    s.refs_before=refs_before;
    Request request{};request.equip=target.equip;
    std::int32_t result=INT32_MIN;std::uintptr_t returned=0;
    ++status.dispatches;++sc.gen_dispatches;++sc.dispatches;
    const bool called=Call(c.base+0x11AC2E0,general_before.iface,&request,&result,&returned);
    LocalVars vars_after{};
    const bool vars_read=ReadLocalVars(c.base,vars_after);
    const bool vars_changed=!vars_read || !SameVars(vars_after,vars_before);
    bool bound=false;Bank bank=Bank::none;
    const char* readback=!vars_read ? "local_vars_unread" : vars_changed ? "local_ammo_changed" :
        Readback(c.base,c.image_span,c.player,c.remote_block,target,g.expected,general_before,general_scratch,bound,true,refs_before,&bank);
    if(!called || returned!=reinterpret_cast<std::uintptr_t>(&result) || result!=0 || readback) {
        // Never retried; the live handle of the slot stays what this allocation
        // dispatched (later slots expect it: no OtherSlots cascade).
        if(called) {
            std::uint32_t live[kSlots]{};
            g.dispatched_handles[slot]=LiveHandles(v,g.lease.inventory,live) ? live[slot] : g.expected;
            g.dispatched_equips[slot]=target.equip;
        }
        s.refs_after=general_scratch.cache_refs;
        Finish(log,slot,SlotState::unverified,"call_unverified",readback ? readback : !called ? "seh_fault" : "dispatch_result",result);
        // The viewer's vars moved (or became unreadable) across our call: no further dispatch this allocation.
        if(vars_changed) for(unsigned rest=0;rest<kSlots;++rest) {
            auto& r=status.slots[rest];
            if(FinalState(r.state)) continue;
            if(r.tier==Tier::none) r.tier=Tier::d;
            Finish(log,rest,SlotState::refused,"refused","prior_local_change");
        }
        return;
    }
    g.dispatched_handles[slot]=g.expected;
    g.dispatched_equips[slot]=target.equip;
    s.refs_after=general_scratch.cache_refs;
    if(bound) {s.bank=bank;Finish(log,slot,SlotState::bound,"handle_bound",nullptr,result);return;}
    s.state=SlotState::tracking;SetReason(s,"queued");
    GeneralReceipt(log,"handle_queued",slot,nullptr,result);
    g.tracking=true;g.tracking_slot=slot;g.observations=0;
#else
    (void)c;(void)log;
#endif
}
}
