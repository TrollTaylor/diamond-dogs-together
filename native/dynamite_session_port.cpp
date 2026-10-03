// Adapted from unknown321/Dynamite l_CreateHostSession and issue_7 allocation
// workaround, commit c90f252f8200765f66c005d6bf4357fda0c0d98e.
// GPLv3 with Commons Clause 1.0; third_party/dynamite/LICENSE.
// Changes: current-build bool ABI, borrowed game target, no Lua UI calls,
// exact-site scoped allocation route instead of skipping a global first call,
// complete mapped allocation-body checks, and process-lifetime module pin.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <strsafe.h>
#include <cstring>
#include <atomic>
#include "mgscoop/dynamite_session_port.hpp"
#include "mgscoop/allocation_bridge.hpp"
#include "session_allocation_contract.hpp"
#include "session_accept_contract.hpp"
#include "session_close_contract.hpp"
#include "preparing_disconnect_contract.hpp"
#include "client_join_contract.hpp"
#include "mgscoop/client_session_request.hpp"
#include "mgscoop/combat_role_patch.hpp"
#include "mgscoop/full_close_patch.hpp"
#include "mgscoop/enemy_targeting.hpp"
#include "mgscoop/session_release.hpp"

namespace {
std::atomic<std::uintptr_t> created_host{0};
std::atomic<DWORD> created_host_thread{0};
std::atomic<std::uintptr_t> owned_session{0};
std::atomic<DWORD> owned_session_thread{0};
std::uintptr_t owned_session_root=0;
bool owned_session_is_client=false;
bool close_authorized=false;
bool close_dispatch_authorized=false;
bool close_sequence_finished=false;
bool Read(std::uintptr_t address,void* output,std::size_t size) noexcept {
    if(address<0x10000 || size>256 || address>0x00007fffffffffff-size) return false;
    SIZE_T read=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),output,size,&read) && read==size;
}
template<class T> bool Read(std::uintptr_t address,T& out) noexcept { return Read(address,&out,sizeof(out)); }
bool Match(std::uintptr_t base,std::size_t span,const SessionBodyContract& body) noexcept {
    if(body.rva>span || body.size>span-body.rva) return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    BCRYPT_HASH_HANDLE hash=nullptr;
    unsigned char buffer[256]{},digest[32]{};
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok) ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    for(std::size_t offset=0;ok && offset<body.size;) {
        const auto count=body.size-offset<sizeof(buffer) ? body.size-offset:sizeof(buffer);
        SIZE_T read=0;
        ok=ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(base+body.rva+offset),buffer,count,&read) && read==count;
        if(ok) ok=BCryptHashData(hash,buffer,static_cast<ULONG>(count),0)>=0;
        offset+=count;
    }
    if(ok) ok=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    if(hash) BCryptDestroyHash(hash);
    if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
    return ok && std::memcmp(digest,body.sha256,32)==0;
}
}

bool HasDynamiteSessionOwnership() noexcept {
    return owned_session.load(std::memory_order_acquire)!=0;
}
bool ReconcileDynamiteSessionRelease(void* verified_base,std::size_t span,
                                    void (*log)(const char*) noexcept) noexcept {
    if(!HasDynamiteSessionOwnership()) return true;
    if(!verified_base || !log || GetCurrentThreadId()!=owned_session_thread.load(std::memory_order_acquire)) return false;
    if(!mgscoop::SessionSlotsReleased(reinterpret_cast<std::uintptr_t>(verified_base),span,owned_session_root,
        [](std::uintptr_t address,std::uintptr_t& value) noexcept { return Read(address,value); })) return false;
    // This clears adapter metadata only. A natural game close is sufficient;
    // do not invoke a Lua getter that could allocate a replacement session.
    owned_session.store(0,std::memory_order_release);
    created_host.store(0,std::memory_order_release);
    owned_session_root=0;
    mgscoop::combat_role::BindSession(0);
    log("session_release=observed; singleton_slots_empty=1 ownership_cleared=1 native_calls=0 reconnect_enabled=0\r\n");
    return true;
}
bool AdoptDynamiteSessionThread(void* verified_base,std::uint32_t allocation_thread,
                               void (*log)(const char*) noexcept) noexcept {
    if(!verified_base || !allocation_thread || !log) return false;
    const auto expected=owned_session.load(std::memory_order_acquire);
    if(!expected) return true; // Diagnostic/client preparation has no session yet.
    // No migration of a joining/closing client, and no borrowed session adopted
    // merely because it happens to be in the game's singleton slots.
    if(owned_session_is_client || close_authorized ||
       owned_session_thread.load(std::memory_order_acquire)!=allocation_thread ||
       created_host_thread.load(std::memory_order_acquire)!=allocation_thread ||
       created_host.load(std::memory_order_acquire)!=expected) return false;
    const auto base=reinterpret_cast<std::uintptr_t>(verified_base);
    struct Context { std::uintptr_t root=0,session=0,main=0,primary=0,secondary=0; std::uint32_t state=0; };
    const auto read=[&](Context& c) noexcept {
        return Read(base+0x2bef840,c.root) && c.root && Read(c.root+0xcc8,c.session) && c.session==expected &&
            Read(base+0x2b9dfb0,c.main) && c.main==expected &&
            Read(expected,c.primary) && c.primary==base+0x2126f70 &&
            Read(expected+0x20,c.secondary) && c.secondary==base+0x2127040 &&
            Read(expected+0x90,c.state) && c.state==3;
    };
    Context before{},after{};
    if(!read(before) || !read(after) || before.root!=after.root) {
        log("session_thread_handoff=refused; reason=owned_host_context\r\n"); return false;
    }
    const auto current=GetCurrentThreadId();
    created_host_thread.store(current,std::memory_order_release);
    owned_session_thread.store(current,std::memory_order_release);
    char line[192]{};
    StringCchPrintfA(line,sizeof(line),"session_thread_handoff=accepted; allocation_thread=%lu mission_thread=%lu game_calls=0\r\n",
        static_cast<DWORD>(allocation_thread),current);
    log(line);
    return true;
}
void ForgetDynamiteHostAdmission() noexcept {
    if(GetCurrentThreadId()==created_host_thread.load(std::memory_order_acquire))
        created_host.store(0,std::memory_order_release);
}
std::uintptr_t OwnedDynamiteHostForTargeting() noexcept {
    if(GetCurrentThreadId()!=created_host_thread.load(std::memory_order_acquire)) return 0;
    return created_host.load(std::memory_order_acquire);
}
bool HasOwnedDynamiteClientSession() noexcept {
    return GetCurrentThreadId()==owned_session_thread.load(std::memory_order_acquire) &&
        owned_session.load(std::memory_order_acquire)!=0 && owned_session_is_client;
}
std::uintptr_t OwnedDynamiteSessionForRespawn() noexcept {
    if(GetCurrentThreadId()!=owned_session_thread.load(std::memory_order_acquire) || close_authorized) return 0;
    return owned_session.load(std::memory_order_acquire);
}
static bool InspectOwnedCloseContext(void* verified_base,std::size_t span,std::uint16_t outgoing_mission,
                                     void (*log)(const char*) noexcept) noexcept {
    if(!verified_base || !log || outgoing_mission!=30050 ||
       GetCurrentThreadId()!=owned_session_thread.load(std::memory_order_acquire)) return false;
    const auto expected=owned_session.load(std::memory_order_acquire);
    const auto base=reinterpret_cast<std::uintptr_t>(verified_base);
    std::uintptr_t root=0,session=0,main=0,primary=0,secondary=0,slot=0,vars=0;
    std::uint32_t state=0; std::uint16_t mission=0;
    if(!expected || !Read(base+0x2bef840,root) || !root || !Read(root+0xcc8,session) || session!=expected ||
       !Read(base+0x2b9dfb0,main) || main!=expected ||
       !Read(session,primary) || primary!=base+0x2126f70 ||
       !Read(session+0x20,secondary) || secondary!=base+0x2127040 ||
       !Read(secondary+0x60,slot) || slot!=base+0x312b80 ||
       !Read(session+0x90,state) || state<2 || state>9 ||
       !Read(base+0x2c16920,vars) || vars!=base+0x2c01360 ||
       !Read(vars+0x12,mission)) {
        log("session_close=refused; reason=owned_session_context\r\n"); return false;
    }
    for(const auto& body:session_close_bodies) if(!Match(base,span,body)) {
        log("session_close=refused; reason=body_mismatch\r\n"); return false;
    }
    return true;
}
bool CanCloseDynamiteSession(void* verified_base,std::size_t span,std::uint16_t outgoing_mission,
                            void (*log)(const char*) noexcept) noexcept {
    if(ReconcileDynamiteSessionRelease(verified_base,span,log)) return false;
    // Authorize the sequence once, before either Lua call can invoke a lazy
    // getter. The actual CloseSession call needs a second ownership check.
    if(!verified_base || !log || GetCurrentThreadId()!=owned_session_thread.load(std::memory_order_acquire) ||
       close_authorized || !InspectOwnedCloseContext(verified_base,span,outgoing_mission,log)) return false;
    const auto base=reinterpret_cast<std::uintptr_t>(verified_base);
    std::uintptr_t slot=0,owner=0,table=0,child=0,child_table=0,self=0;
    if(!Read(base+0x2126f70+0x78,slot) || slot!=base+0x313490 ||
       !Read(base+0x2befdd0,owner) || !owner || !Read(owner,table) || table!=base+0x221be58 ||
       !Read(owner+0x58,child) ||
       (child && (!Read(child+0x18,child_table) || child_table!=base+0x222f1f8 ||
                  !Read(child+0x28,self) || self!=child))) {
        log("session_close=refused; reason=preparing_disconnect_owner\r\n");return false;
    }
    for(const auto& body:preparing_disconnect_bodies) if(!Match(base,span,body)) {
        log("session_close=refused; reason=preparing_disconnect_body\r\n");return false;
    }
    // Reread the borrowed owner chain after hashing; no allocation or native
    // call is used to manufacture a missing singleton or optional child.
    std::uintptr_t current_owner=0,current_child=0;
    if(!Read(base+0x2befdd0,current_owner) || current_owner!=owner ||
       !Read(owner+0x58,current_child) || current_child!=child ||
       !InspectOwnedCloseContext(verified_base,span,outgoing_mission,log)) return false;
    close_authorized=true;
    log("session_close=sequence_authorized; preparing_disconnect_first=1 close_requires_recheck=1\r\n");
    return true;
}
bool CanFinishDynamiteSessionClose(void* base,std::size_t span,std::uint16_t outgoing_mission,
                                  void (*log)(const char*) noexcept) noexcept {
    if(!base || !log || GetCurrentThreadId()!=owned_session_thread.load(std::memory_order_acquire) ||
       !close_authorized || close_dispatch_authorized || close_sequence_finished) return false;
    if(!InspectOwnedCloseContext(base,span,outgoing_mission,log)) return false;
    if(!mgscoop::full_close_patch::Activate(base,span,outgoing_mission,log)) return false;
    close_dispatch_authorized=true;
    log("preparing_disconnect=lua_returned; completion=unverified owned_close_reauthorized=1\r\n");
    return true;
}
bool ObserveDynamiteSessionClose(void* base,std::size_t span,void (*log)(const char*) noexcept,bool returned) noexcept {
    if(!base || !log || GetCurrentThreadId()!=owned_session_thread.load(std::memory_order_acquire) ||
       !close_authorized || close_sequence_finished || (returned && !close_dispatch_authorized)) return false;
    const bool restored=mgscoop::full_close_patch::Deactivate();
    close_sequence_finished=true;
    log(returned ? "session_close=lua_returned; asynchronous_cleanup=unverified reconnect_enabled=0\r\n"
                 : "session_close=sequence_failed; close_may_not_have_run=1 retry_disabled=1\r\n");
    ObserveDynamiteSession(base,log);
    // A returned request is not completed asynchronous cleanup. Retain the
    // ownership record until the actual singleton slots have been cleared.
    if(returned) ReconcileDynamiteSessionRelease(base,span,log);
    return restored;
}

bool CanChangeDynamiteHostAdmission(void* verified_base,std::size_t span,
                                   void (*log)(const char*) noexcept) noexcept {
    if(!verified_base || !log) return false;
    const auto expected=created_host.load(std::memory_order_acquire);
    if(!expected || GetCurrentThreadId()!=created_host_thread.load(std::memory_order_acquire)) return false;
    const auto base=reinterpret_cast<std::uintptr_t>(verified_base);
    std::uintptr_t owner=0,session=0,main=0,table=0,slot=0;
    std::uint32_t state=0;
    if(!Read(base+0x2bef840,owner) || !owner || !Read(owner+0xcc8,session) || session!=expected ||
       !Read(base+0x2b9dfb0,main) || main!=expected || !Read(session,table) || table!=base+0x2126f70 ||
       !Read(table+0x70,slot) || slot!=base+0x3134a0 || !Read(session+0x90,state) || state!=3) {
        log("host_admission=refused; reason=owned_host_state\r\n"); return false;
    }
    for(const auto& body:session_accept_bodies) if(!Match(base,span,body)) {
        log("host_admission=refused; reason=code_mismatch\r\n"); return false;
    }
    log("host_admission=authorized; owned_host=1 same_thread=1 lua_interface=SessionEnableAccept\r\n");
    return true;
}

void ObserveDynamiteSession(void* verified_base,void (*log)(const char*) noexcept) noexcept {
    if(!verified_base || !log) return;
    const auto base=reinterpret_cast<std::uintptr_t>(verified_base);
    std::uintptr_t owner=0,session=0,manager=0,job=0,owner_after=0,session_after=0,manager_after=0;
    if(!Read(base+0x2bef840,owner) || !owner || !Read(owner+0xcc8,session) || !Read(owner+0xd00,manager)) {
        log("dynamite_session=unreadable; calls=0 writes=0\r\n"); return;
    }
    unsigned char accept=0xff;
    std::uint32_t state=0xffffffff;
    const bool session_read=session && Read(session+0x90,state) && Read(session+0xe2,accept);
    std::uint32_t manager_header[4]{},job_header[4]{};
    std::uintptr_t table=0;
    const bool manager_read=manager && Read(manager,table) && table==base+0x221c5a8 &&
        Read(manager-16,manager_header) && Read(manager+8,job);
    const bool job_read=manager_read && job && Read(job-16,job_header);
    const bool consistent=Read(base+0x2bef840,owner_after) && owner==owner_after &&
        Read(owner+0xcc8,session_after) && Read(owner+0xd00,manager_after) &&
        session==session_after && manager==manager_after;
    char line[640]{};
    StringCchPrintfA(line,ARRAYSIZE(line),
        "dynamite_session=observed; owner=%p session=%p manager=%p job=%p session_read=%u state=%u accept=%u manager_heap=%u job_heap=%u consistent=%u calls=0 writes=0 atomic=0\r\n",
        reinterpret_cast<void*>(owner),reinterpret_cast<void*>(session),reinterpret_cast<void*>(manager),reinterpret_cast<void*>(job),
        session_read?1u:0u,state,static_cast<unsigned>(accept),
        manager_read && manager_header[0]==16 && manager_header[1]==115 && manager_header[2]==104 && manager_header[3]==0xdead ? 1u:0u,
        job_read && job_header[0]==16 && job_header[1]==115 && job_header[2]==160 && job_header[3]==0xdead ? 1u:0u,
        consistent?1u:0u);
    log(line);
}
const char* ReadDynamiteSessionStatus(void* verified_base) noexcept {
    constexpr auto unavailable="return 'unavailable'";
    if(!verified_base || GetCurrentThreadId()!=owned_session_thread.load(std::memory_order_acquire) || close_authorized)
        return unavailable;
    const auto expected=owned_session.load(std::memory_order_acquire);
    if(!expected)return unavailable;
    const auto base=reinterpret_cast<std::uintptr_t>(verified_base);
    struct Snapshot {
        std::uintptr_t root=0,session=0,main=0,table=0;
        std::uint32_t state=0;
        unsigned char flags[4]{}; // E1 started, E2 accepting, E3 preparing close, E4 error
    };
    const auto sample=[&](Snapshot& s) noexcept {
        return Read(base+0x2bef840,s.root) && s.root && Read(s.root+0xcc8,s.session) && s.session==expected &&
            Read(base+0x2b9dfb0,s.main) && s.main==expected && Read(expected,s.table) && s.table==base+0x2126f70 &&
            Read(expected+0x90,s.state) && Read(expected+0xe1,s.flags);
    };
    Snapshot before{},after{};
    if(!sample(before))return unavailable;
    const bool two=mgscoop::enemy_targeting::HasTwoPresentMembers(expected);
    if(!sample(after) || before.root!=after.root || before.session!=after.session || before.main!=after.main ||
       before.table!=after.table || before.state!=after.state || std::memcmp(before.flags,after.flags,4)!=0)
        return unavailable;
    if(after.flags[3])return "return 'session_error'";
    if(after.flags[2])return unavailable;
    if(owned_session_is_client) {
        if(after.state==7 && after.flags[0] && two)return "return 'client_established'";
        if(after.state>=4 && after.state<=7)return "return 'client_connecting'";
    } else if(after.state==3 && after.flags[0]) {
        if(two)return "return 'host_peer_present'";
        if(after.flags[1])return "return 'host_ready'";
        return "return 'host_not_accepting'";
    }
    return unavailable;
}

namespace {
// Host and guest are mutually exclusive and each attempts creation once per
// process. Both use the same verified native allocation sites.
bool PrepareSessionAllocation(void* verified_base,std::size_t span,
                               void (*log)(const char*) noexcept) noexcept {
    if(!verified_base || !log) return false;
    const DWORD incoming=GetLastError();
    const auto base=reinterpret_cast<std::uintptr_t>(verified_base);
    for(const auto& body:session_bodies) {
        if(!Match(base,span,body)) {
            log("dynamite_host=refused; reason=allocation_body_mismatch calls=0\r\n");
            SetLastError(incoming); return false;
        }
    }
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&CreateDynamiteHostSession),&pinned)) {
        log("dynamite_host=refused; reason=module_pin calls=0\r\n");
        SetLastError(incoming); return false;
    }
    namespace bridge=mgscoop::allocation_bridge;
    const bridge::Plan plan{reinterpret_cast<bridge::Allocate>(base+0xdbab0),
        {{base+0x7e131e,88,8,115},{base+0x7e1362,144,8,115}}};
    const auto installed=bridge::Install(reinterpret_cast<bridge::Allocate>(base+0xdcb50),
        block_allocator_bytes,sizeof(block_allocator_bytes),plan);
    if(installed!=bridge::InstallResult::installed) {
        log("dynamite_host=refused; reason=allocation_bridge_install calls=0\r\n");
        SetLastError(incoming); return false;
    }
    SetLastError(incoming);
    return true;
}
}

bool CreateDynamiteHostSession(void* verified_base,std::size_t span,std::uintptr_t target,
                               void (*log)(const char*) noexcept) noexcept {
    if(!verified_base || !target || !log) return false;
    const DWORD incoming=GetLastError();
    const auto base=reinterpret_cast<std::uintptr_t>(verified_base);
    if(!PrepareSessionAllocation(verified_base,span,log)) return false;
    bool created=false;
    {
        mgscoop::allocation_bridge::Scope scope;
        if(!scope.acquired()) {
            log("dynamite_host=refused; reason=allocation_scope calls=0\r\n");
            SetLastError(incoming); return false;
        }
        // Upstream calls CreateHostSession on the captured game FobTarget and
        // records success. Current-build tpp-mod supplies the verified bool ABI.
        using CreateHost=bool (*)(void*);
        log("dynamite_host=begin; native_calls=1 exact_heap_sites=2 peer_acceptance_requested=0\r\n");
        SetLastError(incoming);
        created=reinterpret_cast<CreateHost>(base+0x7ee590)(reinterpret_cast<void*>(target));
    }
    char line[256]{};
    StringCchPrintfA(line,ARRAYSIZE(line),
        "dynamite_host=returned; result=%u thread=%lu tick_ms=%llu peer_acceptance_requested=0\r\n",
        created ? 1u:0u,GetCurrentThreadId(),GetTickCount64());
    log(line);
    if(created) {
        std::uintptr_t owner=0,session=0;
        if(Read(base+0x2bef840,owner) && owner && Read(owner+0xcc8,session) && session) {
            created_host_thread.store(GetCurrentThreadId(),std::memory_order_release);
            created_host.store(session,std::memory_order_release);
            owned_session_thread.store(GetCurrentThreadId(),std::memory_order_release);
            owned_session_is_client=false;
            owned_session_root=owner;
            owned_session.store(session,std::memory_order_release);
            mgscoop::combat_role::BindSession(session);
        }
    }
    ObserveDynamiteSession(verified_base,log);
    // Unselected callers keep using the original allocator. The module and
    // hook remain loaded until process exit; no live detour removal is attempted.
    SetLastError(incoming);
    return created;
}

namespace {
bool ClientPrerequisites(std::uintptr_t base,std::size_t span,std::uintptr_t target,
                         void (*log)(const char*) noexcept,std::uintptr_t& info,std::uintptr_t& owner) noexcept {
    for(const auto& body:client_join_bodies) if(!Match(base,span,body)) {
        log("dynamite_client=refused; reason=client_body_mismatch\r\n"); return false;
    }
    std::uintptr_t session=0,main=0;
    mgscoop::ClientConnectInfoPrefix snapshot{};
    MEMORY_BASIC_INFORMATION region{};
    if(!Read(target+0x40,info) || !Read(info,snapshot) || snapshot.state!=0 ||
       !Read(base+0x2bef840,owner) || !owner || !Read(owner+0xcc8,session) || session ||
       !Read(base+0x2b9dfb0,main) || main ||
       !VirtualQuery(reinterpret_cast<void*>(info),&region,sizeof(region)) ||
       region.State!=MEM_COMMIT || (region.Protect!=PAGE_READWRITE && region.Protect!=PAGE_WRITECOPY) ||
       info-reinterpret_cast<std::uintptr_t>(region.BaseAddress)>region.RegionSize ||
       sizeof(snapshot)>region.RegionSize-(info-reinterpret_cast<std::uintptr_t>(region.BaseAddress))) {
        log("dynamite_client=refused; reason=borrowed_info_or_existing_session\r\n"); return false;
    }
    log("dynamite_client=prerequisites_passed; borrowed_info=1 no_existing_session=1 calls=0 writes=0\r\n");
    return true;
}
}

bool InspectDynamiteClientSession(void* verified_base,std::size_t span,std::uintptr_t target,
                                  void (*log)(const char*) noexcept) noexcept {
    if(!verified_base || !target || !log) return false;
    std::uintptr_t info=0,owner=0;
    return ClientPrerequisites(reinterpret_cast<std::uintptr_t>(verified_base),span,target,log,info,owner);
}

// Direct adaptation of the pinned current-build tpp-mod session_connect command
// and Dynamite's native client startup. The game retains ownership of the target
// and connect information. Never allocate or immediately free temporary objects.
bool CreateDynamiteClientSession(void* verified_base,std::size_t span,std::uintptr_t target,
                                 std::uint64_t host,void (*log)(const char*) noexcept) noexcept {
    if(!verified_base || !target || !log || !mgscoop::IsClientHostIdentity(host)) return false;
    const DWORD incoming=GetLastError();
    struct RestoreError { DWORD value; ~RestoreError() { SetLastError(value); } } restore{incoming};
    const auto base=reinterpret_cast<std::uintptr_t>(verified_base);
    std::uintptr_t info=0,owner=0;
    if(!ClientPrerequisites(base,span,target,log,info,owner)) return false;
    if(!PrepareSessionAllocation(verified_base,span,log)) return false;
    bool requested=false;
    {
        mgscoop::allocation_bridge::Scope scope;
        if(!scope.acquired()) { log("dynamite_client=refused; reason=allocation_scope\r\n"); return false; }
        // RCX=borrowed FobTarget, RDX=that target's existing connect-info;
        // AL is a request-start result. The native body reads host identity +B8.
        using CreateClient=char (*)(void*,mgscoop::ClientConnectInfoPrefix*);
        log("dynamite_client=begin; native_calls=1 exact_heap_sites=2 borrowed_objects=1\r\n");
        SetLastError(incoming);
        requested=mgscoop::SubmitClientRequest(reinterpret_cast<void*>(target),
            *reinterpret_cast<mgscoop::ClientConnectInfoPrefix*>(info),host,
            reinterpret_cast<CreateClient>(base+0x7ee380));
    }
    if(requested) {
        std::uintptr_t owner_after=0,created=0,main_after=0,table=0,start_method=0;
        std::uint32_t state=0;
        // Native connection preparation clears E1 even though the mission is
        // already running. State 6 cannot publish its ready-member data until
        // Start sets E1. Use the existing Start method on this new client only.
        if(Read(base+0x2bef840,owner_after) && owner_after==owner &&
           Read(owner+0xcc8,created) && created && Read(base+0x2b9dfb0,main_after) && main_after==created &&
           Read(created,table) && table==base+0x2126f70 &&
           Read(table+0x68,start_method) && start_method==base+0x315140 &&
           Read(created+0x90,state) && state>=4 && state<=7) {
            owned_session_thread.store(GetCurrentThreadId(),std::memory_order_release);
            owned_session_is_client=true;
            owned_session_root=owner;
            owned_session.store(created,std::memory_order_release);
            mgscoop::combat_role::BindSession(created);
            using StartSession=void (*)(void*);
            reinterpret_cast<StartSession>(start_method)(reinterpret_cast<void*>(created));
            unsigned char started=0;
            log(Read(created+0xe1,started) && started==1
                ? "dynamite_client=start_requested; native_flag=1 connected=unverified\r\n"
                : "dynamite_client=start_unconfirmed; connected=unverified\r\n");
        } else {
            log("dynamite_client=start_refused; reason=created_client_identity_or_state connected=unverified\r\n");
        }
    }
    char line[256]{};
    StringCchPrintfA(line,ARRAYSIZE(line),
        "dynamite_client=returned; request_started=%u connected=unverified thread=%lu tick_ms=%llu\r\n",
        requested?1u:0u,GetCurrentThreadId(),GetTickCount64());
    log(line);
    ObserveDynamiteSession(verified_base,log);
    return requested;
}
