// Adapted from Dynamite GetPatches RespawnActionPluginImpl::UpdatePlaySubState,
// c90f252f8200765f66c005d6bf4357fda0c0d98e, GPLv3 + Commons Clause 1.0.
// See third_party/dynamite/LICENSE. Current-build dataflow, complete body hashes,
// scoped activation and atomic restoration replace the permanent upstream patch.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <intrin.h>
#include <cstring>
#include "mgscoop/respawn_patch.hpp"
#include "mgscoop/combat_role_patch.hpp"
#include "respawn_native_contract.hpp"

namespace mgscoop::respawn_patch {
namespace {
SRWLOCK lock=SRWLOCK_INIT;
void* owner=nullptr;
DWORD owner_thread=0,original_protection=0;
bool active=false;
bool flush_pending=false;
constexpr short original_word=0x1974; // JE +0x19
constexpr short replacement_word=static_cast<short>(0x9066); // two-byte NOP
static_assert((body_rva+branch_offset)%2==0);
bool Digest(unsigned char* bytes,std::size_t size,const unsigned char* expected) noexcept {
    BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
    unsigned char digest[32]{};
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok) ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if(ok) ok=BCryptHashData(hash,bytes,static_cast<ULONG>(size),0)>=0;
    if(ok) ok=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    if(hash) BCryptDestroyHash(hash);
    if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
    return ok && std::memcmp(digest,expected,sizeof(digest))==0;
}
bool Read(void* base,std::size_t span,std::uintptr_t rva,unsigned char* out,std::size_t size) noexcept {
    if(rva>span || size>span-rva) return false;
    SIZE_T read=0;
    return ReadProcessMemory(GetCurrentProcess(),static_cast<unsigned char*>(base)+rva,out,size,&read) && read==size;
}
}
bool SetActive(void* base,std::size_t span,bool enabled,void (*log)(const char*) noexcept) noexcept {
    if(!base || !log) return false;
    const DWORD incoming=GetLastError();
    AcquireSRWLockExclusive(&lock);
    struct Finally { DWORD error; ~Finally(){ReleaseSRWLockExclusive(&lock);SetLastError(error);} } finally{incoming};
    if(!enabled && !owner) return true;
    if(owner && (owner!=base || owner_thread!=GetCurrentThreadId())) return false;
    unsigned char snapshot[body_size]{};
    short observed=0;
    if(!Read(base,span,body_rva,snapshot,sizeof(snapshot))) return false;
    std::memcpy(&observed,snapshot+branch_offset,sizeof(observed));
    if(observed!=(active ? replacement_word:original_word)) {
        log("respawn_native=refused; reason=opcode_changed\r\n"); return false;
    }
    std::memcpy(snapshot+branch_offset,&original_word,sizeof(original_word));
    if(!Digest(snapshot,body_size,body_sha256)) {
        log("respawn_native=refused; reason=body_hash\r\n"); return false;
    }
    for(const auto& side:side_bodies) {
        // The combat adaptation owns a detour of this getter. Its exact owned
        // post-install body replaces the original-byte check, while all other
        // supporting bodies and the respawn action retain their normal hashes.
        if(side.rva==0x313fe0 && combat_role::InstalledFor(base)) {
            if(!combat_role::MatchesOwnedBody(base,span)) {
                log("respawn_native=refused; reason=owned_role_detour_changed\r\n"); return false;
            }
            continue;
        }
        if(side.size>sizeof(snapshot) || !Read(base,span,side.rva,snapshot,side.size) ||
           !Digest(snapshot,side.size,side.sha256)) {
            log("respawn_native=refused; reason=supporting_body_hash\r\n"); return false;
        }
    }
    auto* opcode=reinterpret_cast<volatile short*>(static_cast<unsigned char*>(base)+body_rva+branch_offset);
    if(reinterpret_cast<std::uintptr_t>(opcode)%alignof(short)) return false;
    DWORD protection=0,ignored=0;
    if(enabled==active) {
        if(!owner) return true;
        const bool flushed=!flush_pending || FlushInstructionCache(GetCurrentProcess(),const_cast<short*>(opcode),sizeof(short))!=0;
        if(flushed) flush_pending=false;
        const bool restored=VirtualProtect(const_cast<short*>(opcode),sizeof(short),original_protection,&ignored)!=0;
        return flushed && restored;
    }
    if(!VirtualProtect(const_cast<short*>(opcode),sizeof(short),PAGE_EXECUTE_READWRITE,&protection)) return false;
    if(!owner) { owner=base; owner_thread=GetCurrentThreadId(); original_protection=protection; }
    const short before=active ? replacement_word:original_word;
    const short after=enabled ? replacement_word:original_word;
    const bool changed=_InterlockedCompareExchange16(opcode,after,before)==before;
    if(changed) {active=enabled;flush_pending=true;}
    const bool flushed=changed && FlushInstructionCache(GetCurrentProcess(),const_cast<short*>(opcode),sizeof(short));
    if(flushed) flush_pending=false;
    if(changed && !flushed) {
        if(_InterlockedCompareExchange16(opcode,before,after)==after) active=!enabled;
        flush_pending=FlushInstructionCache(GetCurrentProcess(),const_cast<short*>(opcode),sizeof(short))==0;
    }
    const bool restored=VirtualProtect(const_cast<short*>(opcode),sizeof(short),original_protection,&ignored)!=0;
    if(!changed || !flushed || !restored) {
        log("respawn_native=failed; reason=exchange_flush_or_protection\r\n"); return false;
    }
    log(enabled ? "respawn_native=active; guest_health_reset_host_gate_bypassed=1 disk_writes=0\r\n"
                : "respawn_native=inactive; original_health_reset_gate_restored=1\r\n");
    return true;
}
}
