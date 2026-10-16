// Original scoped teammate targeting. Qualified aim, reticle and Quiet filters default on.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <intrin.h>
#include <atomic>
#include <cstring>
#include <MinHook.h>
#include "mgscoop/teammate_targeting.hpp"
#include "mgscoop/teammate_targeting_core.hpp"
#include "teammate_targeting_contract.hpp"
#include "partner_player_contract.hpp"
#include "native_ui_contract.hpp"
#include "mgscoop/native_ui_core.hpp"
namespace mgscoop::teammate_targeting {
namespace {
namespace c=teammate_targeting_contract;
namespace p=partner_player_contract;
using Acquire=void (*)(void*,std::uint16_t*,float*,std::uint8_t*);
Acquire original=nullptr;
using Classify=int (*)(void*,std::uint16_t);
Classify original_classify=nullptr;
using QueueHint=void (*)(void*,std::uint16_t,std::uint32_t,std::uint8_t,std::uint8_t,std::uint8_t,std::uint8_t);
QueueHint original_hint=nullptr;
QueueHint original_prompt[4]{};
std::atomic<Mode> prompt_mode{Mode::filter};
std::atomic<bool> prompt_ready{false};
std::atomic<unsigned> prompt_samples{0},prompt_filtered{0},prompt_receipts{0},prompt_refusals{0};
std::atomic<unsigned long long> prompt_last{0};
using detail::quiet_job;
std::atomic<Mode> quiet_mode{Mode::filter};
std::atomic<bool> quiet_ready{false};
std::atomic<unsigned> quiet_samples{0},quiet_filtered{0},quiet_receipts{0},quiet_refusals{0};
std::atomic<Mode> reticle_mode{Mode::filter};
std::atomic<bool> reticle_ready{false};
std::atomic<unsigned> ui_samples{0},ui_matches{0},ui_filtered{0},ui_receipts{0},ui_refusals{0};
std::atomic<unsigned> ui_last_id{0xfffe},ui_last_category{0xffffffff};
std::atomic<bool> enabled{false};
std::atomic<Mode> mode{Mode::filter};
std::atomic<bool> hook_ready{false};
std::atomic<unsigned> sampled{0},matched{0},filtered{0},last_candidate{0xfffe};
std::atomic<unsigned> receipts{0},last_id{0xfffe},refusal_receipts{0};
std::uintptr_t image=0;
void (*logger)(const char*) noexcept=nullptr;
void* installed=nullptr;
SRWLOCK lock=SRWLOCK_INIT;
bool Read(std::uintptr_t at,void* out,std::size_t size) noexcept {
    SIZE_T copied=0;
    return at && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,size,&copied) && copied==size;
}
template<class T> bool Value(std::uintptr_t at,T& out) noexcept {return Read(at,&out,sizeof(out));}
std::uintptr_t Rva(std::uintptr_t at) noexcept {return at>=image ? at-image : at;}
void Refusal(const char* reason,const Scope& s) noexcept {
    if(refusal_receipts.fetch_add(1,std::memory_order_relaxed)>=16 || !logger) return;
    char line[640]{};
    if(SUCCEEDED(StringCchPrintfA(line,ARRAYSIZE(line),
        "teammate_aim_refused;gate=%s;candidate=%u;camera=0x%llx;cam_vt=0x%llx;secondary_vt=0x%llx;owner=0x%llx;player=0x%llx;p_vt=0x%llx;p_cam=0x%llx;id_out=0x%llx;position_out=0x%llx;flags=0x%x;count=%u;local=%u;ids=%u,%u;caller=0x111e8ed;scope=read_only_natural_call\r\n",
        reason,unsigned(s.candidate),static_cast<unsigned long long>(s.camera),
        static_cast<unsigned long long>(Rva(s.camera_vtable)),static_cast<unsigned long long>(Rva(s.secondary_vtable)),
        static_cast<unsigned long long>(s.owner),static_cast<unsigned long long>(s.player),
        static_cast<unsigned long long>(Rva(s.player_vtable)),static_cast<unsigned long long>(s.player_camera),
        static_cast<unsigned long long>(s.id_out),static_cast<unsigned long long>(s.position_out),
        s.flags,s.count,s.local,unsigned(s.ids[0]),unsigned(s.ids[1])))) logger(line);
}
__declspec(noinline) void AcquireTarget(void* camera,std::uint16_t* id,float* position,std::uint8_t* kind) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    original(camera,id,position,kind);
    if(!enabled.load(std::memory_order_acquire) || caller!=image+c::caller_return || !kind) return;
    const DWORD error=GetLastError();
    Scope s{};s.caller=caller;s.camera=reinterpret_cast<std::uintptr_t>(camera);
    s.id_out=reinterpret_cast<std::uintptr_t>(id);s.position_out=reinterpret_cast<std::uintptr_t>(position);
    std::uintptr_t ids=0;
    const Expected e{image+c::caller_return,image+c::camera_primary_vtable,image+c::camera_secondary_vtable,image+p::p_vtable_rva};
    // The original naturally wrote this output first. Recording refusals never
    // treats an unqualified camera, owner or target as mutation authority.
    if(!Value(s.id_out,s.candidate)) {
        Refusal("output_read",s);SetLastError(error);return;
    }
    last_candidate.store(s.candidate,std::memory_order_relaxed);
    const char* refusal=nullptr;
    if(!Value(s.camera,s.camera_vtable) || s.camera_vtable!=e.camera_vtable) refusal="camera_primary";
    else if(!Value(s.camera+0x20,s.secondary_vtable) || s.secondary_vtable!=e.secondary_vtable) refusal="camera_secondary";
    else if(!Value(s.camera+c::camera_owner,s.owner) || !Value(image+p::player_slot_rva,s.player) || s.owner!=s.player) refusal="camera_owner";
    else if(!Value(s.player,s.player_vtable) || !Value(s.player+p::p_cam,s.player_camera)) refusal="player_chain";
    else if(!Value(s.player+p::p_flags,s.flags) || !Value(s.player+p::p_count,s.count) || !Value(s.player+p::p_local,s.local)) refusal="player_lifecycle_read";
    else if(!Value(s.player+p::p_ids,ids) || !Read(ids,s.ids,sizeof(s.ids))) refusal="roster_read";
    else if(!Owned(s,e)) refusal="owned_scope";
    if(refusal) {Refusal(refusal,s);SetLastError(error);return;}
    const Mode current=mode.load(std::memory_order_acquire);
    const bool teammate=Teammate(s,e);
    sampled.fetch_add(1,std::memory_order_relaxed);
    if(teammate) matched.fetch_add(1,std::memory_order_relaxed);
    if(current==Mode::filter && teammate) {
        // This is the native caller's output buffer, not a game object/team.
        // FFFF is the caller's own no-target sentinel; 111E890 rejects it.
        *id=0xffff;*kind=0;filtered.fetch_add(1,std::memory_order_relaxed);
    }
    const unsigned previous=last_id.exchange(s.candidate,std::memory_order_relaxed);
    if((previous!=s.candidate || teammate) && receipts.fetch_add(1,std::memory_order_relaxed)<64 && logger) {
        char line[256]{};
        if(SUCCEEDED(StringCchPrintfA(line,ARRAYSIZE(line),"teammate_aim_acquisition;mode=%s;candidate=%u;owned_teammate=%u;output=%u;caller=0x111e8ed;scope=local_camera;live_steering_effect=unverified\r\n",
            current==Mode::filter?"filter":"observe",unsigned(s.candidate),teammate?1u:0u,current==Mode::filter&&teammate?0xffffu:unsigned(s.candidate)))) logger(line);
    }
    SetLastError(error);
}
__declspec(noinline) int ClassifyTarget(void* primary,std::uint16_t id) {
    namespace u=native_ui_contract;
    namespace n=native_ui;
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const int category=original_classify(primary,id);
    if(!enabled.load(std::memory_order_acquire) || caller!=image+u::classifier_return) return category;
    const DWORD error=GetLastError();
    n::Classification q{};
    q.caller=caller;q.primary=reinterpret_cast<std::uintptr_t>(primary);q.ui=q.primary+0x20;
    q.candidate=id;q.category=category;
    const n::ClassificationExpected e{image+u::classifier_return,image+u::ui_vtable_rva,image+u::ui_primary_vtable_rva,image+p::p_vtable_rva};
    std::uintptr_t ids=0;
    const char* reason=nullptr;
    if(!Value(q.primary,q.primary_vtable) || q.primary_vtable!=e.primary_vtable) reason="ui_primary";
    else if(!Value(q.ui,q.ui_vtable) || q.ui_vtable!=e.ui_vtable) reason="ui_secondary";
    else if(!Value(q.ui+u::ui_owner,q.owner) || !Value(image+p::player_slot_rva,q.p) || q.owner!=q.p) reason="ui_owner";
    else if(!Value(q.p,q.p_vtable) || !Value(q.p+u::p_ui,q.p_ui)) reason="player_chain";
    else if(!Value(q.p+p::p_flags,q.flags) || !Value(q.p+p::p_count,q.count) || !Value(q.p+p::p_local,q.local)) reason="player_lifecycle";
    else if(!Value(q.p+p::p_ids,ids) || !Read(ids,q.ids,sizeof(q.ids))) reason="roster";
    else if(!n::OwnedClassification(q,e)) reason="owned_scope";
    if(reason) {
        if(ui_refusals.fetch_add(1,std::memory_order_relaxed)<16 && logger) {
            char line[256]{};
            if(SUCCEEDED(StringCchPrintfA(line,ARRAYSIZE(line),"teammate_reticle_refused;gate=%s;candidate=%u;category=%d;caller=0xfea736;scope=original_result_preserved\r\n",reason,unsigned(id),category))) logger(line);
        }
        SetLastError(error);return category;
    }
    const auto current=reticle_mode.load(std::memory_order_acquire);
    const bool teammate=n::OwnedTeammate(q,e);
    const int output=n::FriendlyCategory(q,e,current==Mode::filter);
    ui_samples.fetch_add(1,std::memory_order_relaxed);
    if(teammate) ui_matches.fetch_add(1,std::memory_order_relaxed);
    if(output!=category) ui_filtered.fetch_add(1,std::memory_order_relaxed);
    const auto last_id_value=ui_last_id.exchange(id,std::memory_order_relaxed);
    const auto last_category_value=ui_last_category.exchange(static_cast<unsigned>(category),std::memory_order_relaxed);
    if((teammate || last_id_value!=id || last_category_value!=static_cast<unsigned>(category)) &&
        ui_receipts.fetch_add(1,std::memory_order_relaxed)<64 && logger) {
        char line[320]{};
        if(SUCCEEDED(StringCchPrintfA(line,ARRAYSIZE(line),"teammate_reticle_classification;mode=%s;candidate=%u;owned_teammate=%u;category=%d;output=%d;caller=0xfea736;scope=local_owned_ui;original_geometry=preserved;friendly_style=%s\r\n",
            current==Mode::filter?"filter":"observe",unsigned(id),teammate?1u:0u,category,output,output!=category?"native_category2":"unchanged"))) logger(line);
    }
    SetLastError(error);return output;
}

// True when a prompt request was queued from inside the validated instance-1
// job; it must not reach the local HUD. `field` names the queue's field block.
bool PartnerPrompt(void* ui,std::size_t field,std::uint16_t id,std::uint32_t priority,std::uint8_t enable,std::uintptr_t caller) noexcept {
    namespace u=native_ui_contract;namespace n=native_ui;
    if(!prompt_ready.load(std::memory_order_acquire) || !enabled.load(std::memory_order_acquire) || !quiet_job.armed) return false;
    const DWORD error=GetLastError();
    n::QuietHint q{};auto& s=q.owner;
    s.caller=caller;s.ui=reinterpret_cast<std::uintptr_t>(ui);s.primary=s.ui>=0x20?s.ui-0x20:0;
    q.thread=GetCurrentThreadId();q.owner_thread=quiet_job.owner_thread;q.job_id=1;q.armed=quiet_job.armed;
    q.hint=id;q.priority=priority;q.enable=enable;
    const n::ClassificationExpected e{caller,image+u::ui_vtable_rva,image+u::ui_primary_vtable_rva,image+p::p_vtable_rva};
    std::uintptr_t ids=0;
    const bool read=s.ui==quiet_job.ui && Value(s.primary,s.primary_vtable) && Value(s.ui,s.ui_vtable) &&
        Value(s.ui+u::ui_owner,s.owner) && Value(image+p::player_slot_rva,s.p) && s.p==quiet_job.p &&
        Value(s.p,s.p_vtable) && Value(s.p+u::p_ui,s.p_ui) && Value(s.p+p::p_flags,s.flags) &&
        Value(s.p+p::p_count,s.count) && Value(s.p+p::p_local,s.local) &&
        Value(s.p+p::p_ids,ids) && Read(ids,s.ids,sizeof(s.ids));
    if(!read || !n::OwnedPartnerPrompt(q,e)) {
        if(prompt_refusals.fetch_add(1,std::memory_order_relaxed)<16 && logger) {
            char line[256]{};
            if(SUCCEEDED(StringCchPrintfA(line,ARRAYSIZE(line),"teammate_partner_prompt_refused;queue=0x%x;id=%u;caller=0x%llx;scope=original_queue_preserved\r\n",
                unsigned(field),unsigned(id),static_cast<unsigned long long>(Rva(caller))))) logger(line);
        }
        SetLastError(error);return false;
    }
    const Mode current=prompt_mode.load(std::memory_order_acquire);
    const bool suppressed=n::SuppressPartnerPrompt(q,e,current==Mode::filter);
    prompt_samples.fetch_add(1,std::memory_order_relaxed);
    if(suppressed) prompt_filtered.fetch_add(1,std::memory_order_relaxed);
    // Partner jobs repeat their prompts every frame; record each distinct request.
    const unsigned long long key=(static_cast<unsigned long long>(field)<<48)^(static_cast<unsigned long long>(id)<<32)^
        (static_cast<unsigned long long>(enable)<<31)^Rva(caller);
    if(prompt_last.exchange(key,std::memory_order_relaxed)!=key && prompt_receipts.fetch_add(1,std::memory_order_relaxed)<64 && logger) {
        char line[320]{};
        if(SUCCEEDED(StringCchPrintfA(line,ARRAYSIZE(line),"teammate_partner_prompt;mode=%s;queue=0x%x;id=%u;priority=%u;enable=%u;suppressed=%u;caller=0x%llx;scope=owned_partner_job\r\n",
            current==Mode::filter?"filter":"observe",unsigned(field),unsigned(id),priority,unsigned(enable),suppressed?1u:0u,
            static_cast<unsigned long long>(Rva(caller))))) logger(line);
    }
    SetLastError(error);return suppressed;
}
template<unsigned N> __declspec(noinline) void QueuePrompt(void* ui,std::uint16_t id,std::uint32_t priority,
    std::uint8_t flag,std::uint8_t enable,std::uint8_t extra6,std::uint8_t extra7) {
    if(!PartnerPrompt(ui,native_ui_contract::prompt_queue_fields[N],id,priority,enable,reinterpret_cast<std::uintptr_t>(_ReturnAddress())))
        original_prompt[N](ui,id,priority,flag,enable,extra6,extra7);
}
constexpr QueueHint prompt_hooks[4]={&QueuePrompt<0>,&QueuePrompt<1>,&QueuePrompt<2>,&QueuePrompt<3>};

// This receives the naturally called secondary UI exactly; no receiver adjust.
// Suppressed calls never enter the original queue, preserving all four fields.
__declspec(noinline) void QueueQuietHint(void* ui,std::uint16_t hint,std::uint32_t priority,
    std::uint8_t flag,std::uint8_t enable_hint,std::uint8_t extra6,std::uint8_t extra7) {
    namespace u=native_ui_contract;namespace n=native_ui;
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const bool quiet_site=hint==0x78 && caller==image+u::quiet_queue_return;
    if(!quiet_site && PartnerPrompt(ui,u::ui_hint,hint,priority,enable_hint,caller)) return;
    if(!enabled.load(std::memory_order_acquire) || !quiet_job.armed || !quiet_site) {
        original_hint(ui,hint,priority,flag,enable_hint,extra6,extra7);return;
    }
    const DWORD error=GetLastError();
    n::QuietHint q{};auto& s=q.owner;
    s.caller=caller;s.ui=reinterpret_cast<std::uintptr_t>(ui);s.primary=s.ui>=0x20?s.ui-0x20:0;
    q.thread=GetCurrentThreadId();q.owner_thread=quiet_job.owner_thread;q.job_id=1;q.armed=quiet_job.armed;
    q.hint=hint;q.priority=priority;q.flag=flag;q.enable=enable_hint;q.extra6=extra6;q.extra7=extra7;
    const n::ClassificationExpected e{image+u::quiet_queue_return,image+u::ui_vtable_rva,image+u::ui_primary_vtable_rva,image+p::p_vtable_rva};
    std::uintptr_t ids=0;
    const bool read=s.ui==quiet_job.ui && Value(s.primary,s.primary_vtable) && Value(s.ui,s.ui_vtable) &&
        Value(s.ui+u::ui_owner,s.owner) && Value(image+p::player_slot_rva,s.p) && s.p==quiet_job.p &&
        Value(s.p,s.p_vtable) && Value(s.p+u::p_ui,s.p_ui) && Value(s.p+p::p_flags,s.flags) &&
        Value(s.p+p::p_count,s.count) && Value(s.p+p::p_local,s.local) &&
        Value(s.p+p::p_ids,ids) && Read(ids,s.ids,sizeof(s.ids));
    if(!read || !n::OwnedQuietHint(q,e)) {
        if(quiet_refusals.fetch_add(1,std::memory_order_relaxed)<16 && logger)
            logger("teammate_quiet_hint_refused;scope=original_queue_preserved;hint=120;caller=0x10a498e\r\n");
        SetLastError(error);original_hint(ui,hint,priority,flag,enable_hint,extra6,extra7);return;
    }
    const Mode current=quiet_mode.load(std::memory_order_acquire);
    const bool suppressed=n::SuppressQuietHint(q,e,current==Mode::filter);
    std::uint32_t before_hint=0,before_flags=0,after_hint=0,after_flags=0;
    std::uint8_t before_active=0,before_priority=0,after_active=0,after_priority=0;
    const bool before_ok=Value(s.ui+u::ui_hint,before_hint) && Value(s.ui+u::ui_hint_flags,before_flags) &&
        Value(s.ui+u::ui_hint_active,before_active) && Value(s.ui+u::ui_hint_priority,before_priority);
    quiet_samples.fetch_add(1,std::memory_order_relaxed);
    SetLastError(error);
    if(!suppressed) original_hint(ui,hint,priority,flag,enable_hint,extra6,extra7);
    else quiet_filtered.fetch_add(1,std::memory_order_relaxed);
    const DWORD result_error=GetLastError();
    const bool after_ok=Value(s.ui+u::ui_hint,after_hint) && Value(s.ui+u::ui_hint_flags,after_flags) &&
        Value(s.ui+u::ui_hint_active,after_active) && Value(s.ui+u::ui_hint_priority,after_priority);
    if(quiet_receipts.fetch_add(1,std::memory_order_relaxed)<64 && logger) {
        char line[640]{};
        if(SUCCEEDED(StringCchPrintfA(line,ARRAYSIZE(line),
            "teammate_quiet_hint;mode=%s;hint=120;job_id=1;enable=%u;suppressed=%u;thread=%u;before_read=%u;after_read=%u;hint_before=%u;hint_after=%u;flags_before=0x%x;flags_after=0x%x;active_before=%u;active_after=%u;priority_before=%u;priority_after=%u;caller=0x10a498e;scope=owned_partner_job\r\n",
            current==Mode::filter?"filter":"observe",unsigned(enable_hint),suppressed?1u:0u,q.thread,before_ok?1u:0u,after_ok?1u:0u,
            before_hint,after_hint,before_flags,after_flags,unsigned(before_active),unsigned(after_active),unsigned(before_priority),unsigned(after_priority)))) logger(line);
    }
    SetLastError(result_error);
}
}
void BeginQuietJob(std::uintptr_t player,std::uintptr_t ui,std::uint32_t owner_thread) noexcept {
    quiet_job={};
    if(quiet_ready.load(std::memory_order_acquire) && enabled.load(std::memory_order_acquire) &&
        player && ui>=0x20 && owner_thread && owner_thread==GetCurrentThreadId())
        quiet_job={player,ui,owner_thread,true};
}
void SetQuietMode(Mode value) noexcept {
    quiet_samples.store(0,std::memory_order_relaxed);quiet_filtered.store(0,std::memory_order_relaxed);
    quiet_receipts.store(0,std::memory_order_relaxed);quiet_refusals.store(0,std::memory_order_relaxed);
    quiet_mode.store(value,std::memory_order_release);
}
bool QuietStatusLine(char* out,std::size_t capacity) noexcept {
    if(!out || !capacity) return false;
    const DWORD error=GetLastError();
    const HRESULT result=StringCchPrintfA(out,capacity,
        "quiet;mode=%s;installed=%u;enabled=%u;samples=%u;filtered=%u;refusals=%u",
        quiet_mode.load(std::memory_order_acquire)==Mode::filter?"filter":"observe",quiet_ready.load(std::memory_order_acquire)?1u:0u,
        enabled.load(std::memory_order_acquire)?1u:0u,quiet_samples.load(std::memory_order_relaxed),quiet_filtered.load(std::memory_order_relaxed),quiet_refusals.load(std::memory_order_relaxed));
    SetLastError(error);return SUCCEEDED(result);
}
void SetEnabled(bool on) noexcept {enabled.store(on,std::memory_order_release);}
void SetMode(Mode value) noexcept {
    // Debug selection can restart observation in the same hidden session.
    receipts.store(0,std::memory_order_relaxed);refusal_receipts.store(0,std::memory_order_relaxed);
    last_id.store(0xfffe,std::memory_order_relaxed);last_candidate.store(0xfffe,std::memory_order_relaxed);
    sampled.store(0,std::memory_order_relaxed);matched.store(0,std::memory_order_relaxed);filtered.store(0,std::memory_order_relaxed);
    mode.store(value,std::memory_order_release);
}
bool StatusLine(char* out,std::size_t capacity) noexcept {
    if(!out || !capacity) return false;
    const DWORD error=GetLastError();
    const auto value=mode.load(std::memory_order_acquire);
    const HRESULT result=StringCchPrintfA(out,capacity,
        "aim;mode=%s;installed=%u;enabled=%u;candidate=%u;samples=%u;matches=%u;filtered=%u;refusals=%u",
        value==Mode::filter?"filter":"observe",hook_ready.load(std::memory_order_acquire)?1u:0u,
        enabled.load(std::memory_order_acquire)?1u:0u,last_candidate.load(std::memory_order_relaxed),
        sampled.load(std::memory_order_relaxed),matched.load(std::memory_order_relaxed),filtered.load(std::memory_order_relaxed),
        refusal_receipts.load(std::memory_order_relaxed));
    SetLastError(error);return SUCCEEDED(result);
}
void SetReticleMode(Mode value) noexcept {
    ui_receipts.store(0,std::memory_order_relaxed);ui_refusals.store(0,std::memory_order_relaxed);
    ui_last_id.store(0xfffe,std::memory_order_relaxed);ui_last_category.store(0xffffffff,std::memory_order_relaxed);
    ui_samples.store(0,std::memory_order_relaxed);ui_matches.store(0,std::memory_order_relaxed);ui_filtered.store(0,std::memory_order_relaxed);
    reticle_mode.store(value,std::memory_order_release);
}
bool ReticleStatusLine(char* out,std::size_t capacity) noexcept {
    if(!out || !capacity) return false;
    const DWORD error=GetLastError();
    const HRESULT result=StringCchPrintfA(out,capacity,
        "reticle;mode=%s;installed=%u;enabled=%u;candidate=%u;category=%u;samples=%u;matches=%u;filtered=%u;refusals=%u",
        reticle_mode.load(std::memory_order_acquire)==Mode::filter?"filter":"observe",reticle_ready.load(std::memory_order_acquire)?1u:0u,
        enabled.load(std::memory_order_acquire)?1u:0u,ui_last_id.load(std::memory_order_relaxed),ui_last_category.load(std::memory_order_relaxed),
        ui_samples.load(std::memory_order_relaxed),ui_matches.load(std::memory_order_relaxed),ui_filtered.load(std::memory_order_relaxed),ui_refusals.load(std::memory_order_relaxed));
    SetLastError(error);return SUCCEEDED(result);
}
bool Install(void* base,std::size_t span,void (*log)(const char*) noexcept,bool leave_vt268) noexcept {
    if(!base || !log) return false;
    const DWORD error=GetLastError();
    AcquireSRWLockExclusive(&lock);
    struct Finally {DWORD error;~Finally(){ReleaseSRWLockExclusive(&lock);SetLastError(error);}} finally{error};
    if(installed) return installed==base;
    const auto candidate=reinterpret_cast<std::uintptr_t>(base);
    unsigned char bytes[0xa00]{};
    for(const auto& body:c::bodies) {
        if(body.size>sizeof(bytes) || body.rva>span || body.size>span-body.rva ||
            !Read(candidate+body.rva,bytes,body.size) || std::memcmp(bytes,body.bytes,body.size)!=0) {
            log("teammate_targeting=refused;reason=full_body_contract\r\n");return false;
        }
    }
    for(const auto& site:native_ui_contract::sites) {
        if(site.size>sizeof(bytes) || site.rva>span || site.size>span-site.rva ||
            !Read(candidate+site.rva,bytes,site.size) || std::memcmp(bytes,site.bytes,site.size)!=0) {
            log("teammate_targeting=refused;reason=native_ui_contract\r\n");return false;
        }
    }
    std::uintptr_t slot=0;
    if(!Value(candidate+c::camera_secondary_vtable+0x98,slot) || slot!=candidate+0x111d4b0 ||
        !Value(candidate+native_ui_contract::ui_vtable_rva+0x280,slot) || slot!=candidate+native_ui_contract::quiet_queue_rva) return false;
    for(unsigned i=0;i<4;++i)
        if(!Value(candidate+native_ui_contract::ui_vtable_rva+native_ui_contract::prompt_queue_slots[i],slot) ||
            slot!=candidate+native_ui_contract::prompt_queue_rvas[i]) return false;
    const auto init=MH_Initialize();
    if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED) return false;
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Install),&pinned)) return false;
    image=candidate;logger=log;
    const auto aim_target=reinterpret_cast<void*>(candidate+c::target_rva);
    const auto ui_target=reinterpret_cast<void*>(candidate+native_ui_contract::classifier_rva);
    if(MH_CreateHook(aim_target,reinterpret_cast<void*>(&AcquireTarget),reinterpret_cast<void**>(&original))!=MH_OK) return false;
    if(MH_CreateHook(ui_target,reinterpret_cast<void*>(&ClassifyTarget),reinterpret_cast<void**>(&original_classify))!=MH_OK) {
        MH_RemoveHook(aim_target);return false;
    }
    const auto hint_target=reinterpret_cast<void*>(candidate+native_ui_contract::quiet_queue_rva);
    if(MH_CreateHook(hint_target,reinterpret_cast<void*>(&QueueQuietHint),reinterpret_cast<void**>(&original_hint))!=MH_OK) {
        MH_RemoveHook(ui_target);MH_RemoveHook(aim_target);return false;
    }
    if(MH_EnableHook(hint_target)!=MH_OK) {
        MH_RemoveHook(hint_target);MH_RemoveHook(ui_target);MH_RemoveHook(aim_target);return false;
    }
    if(MH_EnableHook(ui_target)!=MH_OK) {
        MH_DisableHook(hint_target);MH_RemoveHook(hint_target);MH_RemoveHook(ui_target);MH_RemoveHook(aim_target);return false;
    }
    if(MH_EnableHook(aim_target)!=MH_OK) {
        MH_DisableHook(ui_target);MH_DisableHook(hint_target);
        MH_RemoveHook(hint_target);MH_RemoveHook(ui_target);MH_RemoveHook(aim_target);
        log("teammate_targeting=failed;reason=enable;inactive_forwarding=1\r\n");return false;
    }
    // Prompt hooks are optional: without them the partner's prompts stay
    // visible, but the qualified aim/reticle/Quiet filters remain installed.
    const unsigned first=leave_vt268 ? 1u : 0u;
    unsigned hooked=first;
    for(;hooked<4;++hooked) {
        const auto target=reinterpret_cast<void*>(candidate+native_ui_contract::prompt_queue_rvas[hooked]);
        if(MH_CreateHook(target,reinterpret_cast<void*>(prompt_hooks[hooked]),reinterpret_cast<void**>(&original_prompt[hooked]))!=MH_OK) break;
        if(MH_EnableHook(target)!=MH_OK) {MH_RemoveHook(target);break;}
    }
    if(hooked<4) {
        while(hooked>first) {
            const auto target=reinterpret_cast<void*>(candidate+native_ui_contract::prompt_queue_rvas[--hooked]);
            MH_DisableHook(target);MH_RemoveHook(target);
        }
        log("teammate_partner_prompt=failed;reason=hook;scope=partner_prompts_unfiltered\r\n");
    } else {
        prompt_mode.store(Mode::filter,std::memory_order_release);prompt_ready.store(true,std::memory_order_release);
        detail::prompt_filter.store(&PartnerPrompt,std::memory_order_release);
        log(leave_vt268 ?
            "teammate_partner_prompt=installed;mode=filter;queues=0x370,0x410,0x460,0x4b0;queue_0x3c0=native_pose_window;scope=owned_partner_job_only;qualification=static_contract\r\n" :
            "teammate_partner_prompt=installed;mode=filter;queues=0x370,0x3c0,0x410,0x460,0x4b0;scope=owned_partner_job_only;qualification=static_contract\r\n");
    }
    installed=base;mode.store(Mode::filter,std::memory_order_release);reticle_mode.store(Mode::filter,std::memory_order_release);
    quiet_mode.store(Mode::filter,std::memory_order_release);quiet_ready.store(true,std::memory_order_release);
    hook_ready.store(true,std::memory_order_release);reticle_ready.store(true,std::memory_order_release);
    log("teammate_targeting=installed;mode=filter;filter=enabled;scope=owned_primary_camera_only;qualification=private_controller_57_owned_filters\r\n");
    log("teammate_reticle=installed;mode=filter;filter=enabled;native_geometry_preserved=1;caller=0xfea736;qualification=private_green_reticle\r\n");
    log("teammate_quiet=installed;mode=filter;filter=enabled;scope=owned_partner_job_only;hint=120;caller=0x10a498e;qualification=private_48_owned_suppressions\r\n");
    return true;
}
} // namespace mgscoop::teammate_targeting
