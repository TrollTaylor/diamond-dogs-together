// Adapted from Dynamite GetPatches DefenseCallback::TargetCallbackExec,
// c90f252f8200765f66c005d6bf4357fda0c0d98e, GPLv3 + Commons Clause 1.0.
// See third_party/dynamite/LICENSE. Current-build mapping, complete callback
// hash and mission-scoped atomic opcode changes replace the global disk patch.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <intrin.h>
#include <cstring>
#include "mgscoop/damage_patch.hpp"
#include "damage_contract.hpp"
namespace mgscoop::damage {
namespace {
SRWLOCK lock=SRWLOCK_INIT;
void* owner=nullptr;
DWORD owner_thread=0;
DWORD original_protection=0;
bool active=false;
bool flush_pending=false;
bool Digest(unsigned char* bytes) noexcept {
    BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
    unsigned char digest[32]{};
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok) ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if(ok) ok=BCryptHashData(hash,bytes,static_cast<ULONG>(body_size),0)>=0;
    if(ok) ok=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    if(hash) BCryptDestroyHash(hash);
    if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
    return ok && std::memcmp(digest,body_sha256,sizeof(digest))==0;
}
}
bool SetActive(void* base,std::size_t span,bool enabled,void (*log)(const char*) noexcept) noexcept {
    if(!base || !log || body_rva>span || body_size>span-body_rva) return false;
    const DWORD incoming=GetLastError();
    AcquireSRWLockExclusive(&lock);
    struct Finally { DWORD error; ~Finally(){ ReleaseSRWLockExclusive(&lock); SetLastError(error); } } finally{incoming};
    if(!enabled && !owner) return true;
    if(owner && (owner!=base || owner_thread!=GetCurrentThreadId())) return false;
    unsigned char snapshot[body_size]{}; SIZE_T read=0;
    auto* body=static_cast<unsigned char*>(base)+body_rva;
    if(!ReadProcessMemory(GetCurrentProcess(),body,snapshot,sizeof(snapshot),&read) || read!=sizeof(snapshot) ||
       snapshot[branch_offset]!=(active ? 0xeb : 0x74)) {
        log("coop_damage=refused; reason=callback_unreadable_or_opcode_changed\r\n"); return false;
    }
    snapshot[branch_offset]=0x74;
    if(!Digest(snapshot)) { log("coop_damage=refused; reason=callback_hash\r\n"); return false; }
    auto* opcode=reinterpret_cast<volatile char*>(body+branch_offset);
    DWORD protection=0,ignored=0;
    if(enabled==active) {
        if(!owner || !original_protection) return true;
        const bool flushed=!flush_pending || FlushInstructionCache(GetCurrentProcess(),const_cast<char*>(opcode),1)!=0;
        if(flushed) flush_pending=false;
        const bool restored=VirtualProtect(const_cast<char*>(opcode),1,original_protection,&ignored)!=0;
        return flushed && restored;
    }
    if(!VirtualProtect(const_cast<char*>(opcode),1,PAGE_EXECUTE_READWRITE,&protection)) return false;
    if(!owner) { owner=base; owner_thread=GetCurrentThreadId(); original_protection=protection; }
    const char before=static_cast<char>(active ? 0xeb : 0x74);
    const char after=static_cast<char>(enabled ? 0xeb : 0x74);
    const bool changed=_InterlockedCompareExchange8(opcode,after,before)==before;
    if(changed) {active=enabled;flush_pending=true;}
    const bool flushed=changed && FlushInstructionCache(GetCurrentProcess(),const_cast<char*>(opcode),1);
    if(flushed) flush_pending=false;
    if(changed && !flushed) {
        if(_InterlockedCompareExchange8(opcode,before,after)==after) active=!enabled;
        flush_pending=FlushInstructionCache(GetCurrentProcess(),const_cast<char*>(opcode),1)==0;
    }
    const bool restored=VirtualProtect(const_cast<char*>(opcode),1,original_protection,&ignored)!=0;
    if(!changed || !flushed || !restored) {
        log("coop_damage=failed; reason=opcode_exchange_flush_or_protection\r\n"); return false;
    }
    log(enabled ? "coop_damage=active; guest_damage_return_bypassed=1 disk_writes=0\r\n"
                : "coop_damage=inactive; original_guest_damage_rule_restored=1\r\n");
    return true;
}
}
