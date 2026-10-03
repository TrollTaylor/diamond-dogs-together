// Nearest-player targeting adapts Dynamite c90f252f8200765f66c005d6bf4357fda0c0d98e.
// GPLv3 with Commons Clause 1.0; third_party/dynamite/LICENSE.
// Changes: game-thread Lua soldier queries, bounded native roster/pose reads,
// actual member indices, expiring selection, no background game-pointer calls.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <limits>
#include <locale.h>
#include "mgscoop/enemy_targeting.hpp"
#include "targeting_contract.hpp"

namespace mgscoop::enemy_targeting {
namespace {
std::uintptr_t image=0;
DWORD owner_thread=0;
_locale_t numeric_locale=nullptr;
ULONGLONG next_snapshot=0,pending_time=0;
// Expiry and index share one atomic word, so readers cannot mix generations.
std::atomic<ULONGLONG> selected{0};
struct Roster {
    std::uintptr_t session=0,root=0,array=0,host=0,local=0;
    std::array<std::uintptr_t,2> members{}; // indexed by actual player index
    bool operator==(const Roster& other) const noexcept {
        return session==other.session && root==other.root && array==other.array &&
            host==other.host && local==other.local && members==other.members;
    }
};
Roster pending{};
unsigned int pending_mask=0;
bool Read(std::uintptr_t address,void* out,std::size_t size) noexcept {
    if(address<0x10000 || size==0 || size>1024 || address>0x00007fffffffffff-size) return false;
    SIZE_T read=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),out,size,&read) && read==size;
}
template<class T> bool Read(std::uintptr_t address,T& out) noexcept {return Read(address,&out,sizeof(out));}
bool ReadRoster(std::uintptr_t session,Roster& out,bool host_only=true) noexcept {
    if(!image || !session || GetCurrentThreadId()!=owner_thread) return false;
    std::uintptr_t root=0,current=0,main=0,primary=0,secondary=0,array=0,host=0,local=0;
    std::uint32_t state=0,capacity=0;
    if(!Read(image+0x2bef840,root) || !root || !Read(root+0xcc8,current) || current!=session ||
       !Read(image+0x2b9dfb0,main) || main!=session ||
       !Read(session,primary) || primary!=image+0x2126f70 ||
       !Read(session+0x20,secondary) || secondary!=image+0x2127040 ||
       !Read(session+0x90,state) || (state!=3 && (host_only || state!=7)) ||
       !Read(session+0x38,local) || !local || !Read(session+0x40,host) || !host ||
       (state==3 ? host!=local : host==local) ||
       !Read(session+0x50,array) || !array || !Read(session+0x68,capacity) || capacity<2 || capacity>16) return false;
    Roster result{}; result.session=session; result.root=root; result.array=array; result.host=host; result.local=local;
    unsigned present=0;
    for(unsigned slot=0;slot<capacity;++slot) {
        std::uintptr_t member=0,table=0;
        unsigned char flags=0,index=0;
        if(!Read(array+slot*sizeof(member),member)) return false;
        if(!member) continue;
        if(!Read(member+0x44,flags)) return false;
        if(!(flags&0x10)) continue;
        if(!Read(member+0x18,table) || table!=image+0x2127620 ||
           !Read(member+0x40,index) || index>1 || result.members[index]) return false;
        result.members[index]=member; ++present;
    }
    if(present!=2 || result.members[0]!=host || (local!=result.members[0] && local!=result.members[1])) return false;
    out=result; return true;
}
struct Pose { float xyz[3]{}; bool alive=false; };
bool ReadRecords(std::array<unsigned char,0x1c0>& records) noexcept {
    int bank=0,after=0;
    std::array<unsigned char,0x1c0> again{};
    if(!Read(image+0x2bd3fd0,bank) || bank<0 || bank>1) return false;
    const auto address=image+0x2bd3ff0+static_cast<std::uintptr_t>(bank)*16*0xe0;
    if(!Read(address,records) || !Read(address,again) || records!=again ||
       !Read(image+0x2bd3fd0,after) || after!=bank) return false;
    return true;
}
bool ReadPoses(Pose (&out)[2]) noexcept {
    std::array<unsigned char,0x1c0> records{};
    if(!ReadRecords(records)) return false;
    for(unsigned i=0;i<2;++i) {
        const auto* record=records.data()+i*0xe0;
        std::memcpy(out[i].xyz,record+0x10,sizeof(out[i].xyz));
        for(float value:out[i].xyz) if(!std::isfinite(value) || std::fabs(value)>1000000.0f) return false;
        std::uint32_t status=0; std::memcpy(&status,record+0xc0+sizeof(status),sizeof(status));
        out[i].alive=(status&(1u<<30))==0; // published DEAD bit 62
    }
    return true;
}
}
const char* RespawnContext(std::uintptr_t owned_session) noexcept {
    Roster before{},after{};
    std::array<unsigned char,0x1c0> records{};
    if(!ReadRoster(owned_session,before,false) || !ReadRecords(records) ||
       !ReadRoster(owned_session,after,false) || !(before==after)) return "return nil";
    std::uint32_t status[2]{};
    for(unsigned i=0;i<2;++i) std::memcpy(&status[i],records.data()+i*0xe0+0xc4,sizeof(status[i]));
    const unsigned local=before.local==before.members[0] ? 0u:1u;
    static char source[128]{};
    const int count=sprintf_s(source,"return {localIndex=%u,dead0=%s,dead1=%s}",local,
        status[0]&(1u<<30) ? "true":"false",status[1]&(1u<<30) ? "true":"false");
    return count>0 ? source : "return nil";
}
bool HasTwoPresentMembers(std::uintptr_t session) noexcept {
    Roster before{},after{};
    return ReadRoster(session,before,false) && ReadRoster(session,after,false) && before==after;
}
bool Initialize(void* base,std::size_t span) noexcept {
    if(image) return image==reinterpret_cast<std::uintptr_t>(base) && owner_thread==GetCurrentThreadId();
    if(!base) return false;
    unsigned char buffer[1024]{};
    for(const auto& body:targeting_contract::bodies) {
        if(body.rva>span || body.size>span-body.rva || body.size>sizeof(buffer) ||
           !Read(reinterpret_cast<std::uintptr_t>(base)+body.rva,buffer,body.size) ||
           std::memcmp(buffer,body.bytes,body.size)!=0) return false;
    }
    numeric_locale=_create_locale(LC_NUMERIC,"C");
    if(!numeric_locale) return false;
    // Kept for the process lifetime, like the pinned detours.
    image=reinterpret_cast<std::uintptr_t>(base); owner_thread=GetCurrentThreadId(); return true;
}
bool AdoptMissionThread(std::uint32_t allocation_thread) noexcept {
    if(!image) return true; // Ordinary mission; targeting was never installed.
    const auto current=GetCurrentThreadId();
    if(!allocation_thread || (owner_thread!=allocation_thread && owner_thread!=current)) return false;
    selected.store(0,std::memory_order_release);
    pending_mask=0; pending={}; pending_time=0; next_snapshot=0;
    owner_thread=current;
    return true;
}
void Clear() noexcept {
    selected.store(0,std::memory_order_release);
    if(GetCurrentThreadId()==owner_thread) { pending_mask=0; pending={}; }
}
unsigned int SelectedIndex() noexcept {
    const auto choice=selected.load(std::memory_order_acquire);
    if(!choice || GetTickCount64()>=(choice>>2)) return std::numeric_limits<unsigned int>::max();
    return static_cast<unsigned int>((choice&3)-1);
}
const char* Snapshot(std::uintptr_t owned_host) noexcept {
    if(GetCurrentThreadId()!=owner_thread || !image) return "return nil";
    const auto now=GetTickCount64();
    if(now<next_snapshot) return nullptr;
    next_snapshot=now+500;
    pending_mask=0;
    Roster before{},after{}; Pose poses[2]{};
    if(!ReadRoster(owned_host,before) || !ReadPoses(poses) || !ReadRoster(owned_host,after) || !(before==after)) {
        Clear(); return "return nil";
    }
    pending=before; pending_time=now;
    for(unsigned i=0;i<2;++i) if(poses[i].alive) pending_mask|=1u<<i;
    if(!pending_mask) {Clear(); return "return nil";}
    static char source[512]{};
    const int length=_snprintf_s_l(source,sizeof(source),_TRUNCATE,
        "return {{index=0,x=%.9g,y=%.9g,z=%.9g,alive=%s},{index=1,x=%.9g,y=%.9g,z=%.9g,alive=%s}}",
        numeric_locale,static_cast<double>(poses[0].xyz[0]),static_cast<double>(poses[0].xyz[1]),static_cast<double>(poses[0].xyz[2]),poses[0].alive?"true":"false",
        static_cast<double>(poses[1].xyz[0]),static_cast<double>(poses[1].xyz[1]),static_cast<double>(poses[1].xyz[2]),poses[1].alive?"true":"false");
    if(length<0 || static_cast<std::size_t>(length)>=sizeof(source)) {Clear(); return "return nil";}
    return source;
}
bool Commit(std::uintptr_t owned_host,unsigned int index) noexcept {
    if(GetCurrentThreadId()!=owner_thread || index>1 || !(pending_mask&(1u<<index))) return false;
    const auto now=GetTickCount64();
    Roster current{},after{}; Pose poses[2]{};
    const bool valid=now-pending_time<750 && ReadRoster(owned_host,current) && current==pending &&
        ReadPoses(poses) && poses[index].alive && ReadRoster(owned_host,after) && after==current;
    pending_mask=0;
    if(!valid) { Clear(); return false; }
    selected.store(((now+1000)<<2)|(index+1),std::memory_order_release);
    return true;
}
}
