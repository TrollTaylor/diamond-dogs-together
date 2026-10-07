// Original, separately compiled copied-save stock46 experiment. No hooks,
// worker routing, alias preparation, local-index edits, rollback or retries.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <strsafe.h>
#include "mgscoop/equipment_stock_writer.hpp"
#include "mgscoop/equipment_stock_writer_contract.hpp"
#include "equipment_stock_writer_pins.hpp"

namespace mgscoop::equipment_stock_writer {
namespace {
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
    std::uint16_t equip=46,stock=0,stock_sub=0,ammo=0,ammo_sub=0;
    std::uint8_t suppression_life=0,flags=4;
};
#pragma pack(pop)
static_assert(sizeof(Request)==20 && offsetof(Request,equip)==8 && offsetof(Request,flags)==19);
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
        "stock46_writer=%s; reason=%s mission=30010 instance=1 slot=1 equip=46 receiver=157 epoch=%u expected_handle=%x result=%d mismatch_rva=%llx attempted=%u retry=0 local_index_write=0 alias_call=0 rollback_calls=0\r\n",
        phase,reason ? reason : "ok",observed_epoch,expected_handle,result,
        static_cast<unsigned long long>(mismatch),unsigned(authorization.Attempted()));
    log(line);
}
void PackageReceipt(void (*log)(const char*) noexcept,const Context& c,const contract::PackageObservation& sample) noexcept {
    if(!log) return;
    auto reader=Read;contract::View<decltype(reader)> v{reader,c.base,c.image_span};
    const auto p=sample.reads ? sample : contract::ObservePackage(v);char line[1024]{};
    StringCchPrintfA(line,sizeof(line),
        "stock46_writer_package=read_only; reads=%x checks=%x service=%llx backing=%llx allocation=%llx capacity=%llx size=%llx parameters=%llx variant=%u table=%llx method8=%llx method28=%llx method38=%llx descriptor46=%llx observations=%u native_calls=0\r\n",
        p.reads,p.checks,static_cast<unsigned long long>(p.service),static_cast<unsigned long long>(p.backing),
        static_cast<unsigned long long>(p.allocation),static_cast<unsigned long long>(p.capacity),
        static_cast<unsigned long long>(p.size),static_cast<unsigned long long>(p.parameters),p.variant,
        static_cast<unsigned long long>(p.table),static_cast<unsigned long long>(p.methods[0]),
        static_cast<unsigned long long>(p.methods[1]),static_cast<unsigned long long>(p.methods[2]),
        static_cast<unsigned long long>(p.descriptor),readiness.Count());
    log(line);
}
void OptionalReceipt(void (*log)(const char*) noexcept,const contract::OptionalLease& o) noexcept {
    if(!log) return;
    char line[640]{};
    StringCchPrintfA(line,sizeof(line),
        "stock46_writer_optional=read_only; asset=%llx source=%llx header=%llx source_bytes=%llu parameters=%u render_global=%u render_bytes=%u arena_stride=2048 native_calls=0\r\n",
        static_cast<unsigned long long>(o.asset),static_cast<unsigned long long>(o.source),
        static_cast<unsigned long long>(o.header),static_cast<unsigned long long>(o.bytes),o.parameters,o.global,o.render_bytes);
    log(line);
}
const char* Readback(const Context& c,contract::Snapshot& after,bool& bound) noexcept {
    bound=false;
    auto reader=Read;
    if(const auto reason=contract::Capture(c.base,c.image_span,reader,c.player,c.remote_block,after)) return reason;
    if(const auto reason=contract::LocalUnchanged(before,after)) return reason;
    if(after.remote_handles[1]!=expected_handle || after.cache_refs!=before.cache_refs+1) return "remote_handle_or_refs";
    for(unsigned i=0;i<7;++i) if(i!=1 && after.remote_handles[i]!=before.remote_handles[i]) return "other_remote_slot_changed";
    contract::View<decltype(reader)> view{reader,c.base,c.image_span};
    if(const auto reason=contract::DelayedCachesPreserved(view,before)) return reason;
    contract::Bytes14 row{};std::uint32_t mapped=0;
    if(!view.At(after.meta_rows,before.new_index*0xE,row) || contract::Word16(row.data())!=46 ||
       row[8]!=4 || !view.At(after.map_rows,before.new_index*4,mapped)) return "new_handle_owner";
    const auto current=contract::Word32(after.remote_binder.data()+12);
    const auto pending=contract::Word32(after.remote_binder.data()+16);
    if(current!=expected_handle && pending!=expected_handle) return "model_handle_missing";
    if(current!=expected_handle) return nullptr; // queued, not rendered evidence
    if(mapped>=60) return "bound_without_resource";
    if(const auto reason=contract::QualifiedBoundResource(view,before,mapped)) return reason;
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
#endif
}
void BeginAllocation(std::uint32_t mission) noexcept {
#ifdef MGS_LOCAL_STOCK46_WRITER_TEST
    authorization.Begin(mission);readiness.Reset();before={};scratch={};observed_epoch=expected_handle=observations=0;
    tracking=readiness_logged=false;
#else
    (void)mission;
#endif
}
void End() noexcept {
#ifdef MGS_LOCAL_STOCK46_WRITER_TEST
    authorization.End();readiness.Reset();before={};scratch={};tracking=readiness_logged=false;observations=0;
#endif
}
void Observe(const Context& c,void (*log)(const char*) noexcept) noexcept {
#ifdef MGS_LOCAL_STOCK46_WRITER_TEST
    if(!c.enabled) return;
    if(tracking) {
        if(!c.owner || !c.offline_gates || !c.body_ready || c.mission!=30010 || c.body_epoch!=observed_epoch ||
           c.player!=before.player || c.remote_block!=before.bodies[1]) {tracking=false;Receipt(log,"observation_stopped","owner_lease_changed");return;}
        bool bound=false;
        const char* reason=Readback(c,scratch,bound);
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
            readiness_logged=true;PackageReceipt(log,c,before.package_observation);OptionalReceipt(log,before.optional);
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
    expected_handle=(before.new_index<<16)|(46u<<5)|4u;
    std::uintptr_t mismatch=0;
    if(!BodyGate(c.base,c.image_span,mismatch)) {Receipt(log,"refused","full_body_gate",INT32_MIN,mismatch);return;}
    // Re-read after hashing, directly before mutation. Even a successful
    // preflight cannot lend stale pointers to the dispatcher.
    auto& refreshed=scratch;
    reason=contract::Preflight(c.base,c.image_span,reader,c.player,c.remote_block,refreshed);
    if(!reason) reason=contract::LocalUnchanged(before,refreshed);
    if(reason || refreshed.ring!=before.ring || refreshed.ring_cursor!=before.ring_cursor ||
       refreshed.new_index!=before.new_index || refreshed.meta_rows!=before.meta_rows || refreshed.map_rows!=before.map_rows) {
        Receipt(log,"refused",reason ? reason : "allocation_lease_changed");return;
    }
    if(refreshed.next_resource!=before.next_resource || refreshed.selected_group!=before.selected_group ||
       refreshed.selected_resource_index!=before.selected_resource_index ||
       refreshed.package_service!=before.package_service || refreshed.package_parameters!=before.package_parameters ||
       refreshed.selected_banks!=before.selected_banks || refreshed.selected_bank_arrays!=before.selected_bank_arrays ||
       refreshed.selected_cache_counts!=before.selected_cache_counts || refreshed.selected_cache_arrays!=before.selected_cache_arrays ||
       refreshed.selected_bank_aux!=before.selected_bank_aux ||
       refreshed.selected_bank_allocations!=before.selected_bank_allocations ||
       refreshed.selected_bank_allocation_bytes!=before.selected_bank_allocation_bytes ||
       refreshed.selected_bank_used_bytes!=before.selected_bank_used_bytes ||
       refreshed.main_cache_candidate!=before.main_cache_candidate || refreshed.main_cache_entries!=before.main_cache_entries ||
       refreshed.banks!=before.banks || refreshed.eligible_resources!=before.eligible_resources ||
       refreshed.initial_maps!=before.initial_maps || refreshed.initial_owners!=before.initial_owners ||
       refreshed.initial_load_flags!=before.initial_load_flags || refreshed.initial_engine_metadata!=before.initial_engine_metadata ||
       refreshed.free_queue!=before.free_queue || refreshed.free_cursor!=before.free_cursor) {
        Receipt(log,"refused","selected_resource_lease_changed");return;
    }
    const Request request{};std::int32_t result=INT32_MIN;std::uintptr_t returned=0;
    const bool called=Call(c.base+0x11AC2E0,before.iface,&request,&result,&returned);
    bool bound=false;
    const char* readback=Readback(c,scratch,bound);
    if(!called || returned!=reinterpret_cast<std::uintptr_t>(&result) || result!=0 || readback) {
        Receipt(log,"call_unverified",readback ? readback : !called ? "seh_fault" : "dispatch_result",result);return;
    }
    Receipt(log,bound ? "handle_bound" : "handle_queued",nullptr,result);
    if(!bound) {tracking=true;observations=0;}
#else
    (void)c;(void)log;
#endif
}
}
