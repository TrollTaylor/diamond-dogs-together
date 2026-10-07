#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include "mgscoop/partner_upper_manual_idle.hpp"
#include <atomic>
#include <cstring>
namespace mgscoop::partner_upper_manual_idle {
namespace {
constexpr std::uintptr_t max_address=0x7fffffffffff;
struct Core {
    std::atomic_flag lock=ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> lifecycle{0},active{0};
    bool configured=false,pending=false,claimed=false,after_done=false;
    std::uintptr_t gated_image=0;Reader gated_reader{};
    Reader reader{};StorageQuery query{};std::uintptr_t image=0;
    Lease lease{};std::uint64_t last_generation=0,last_command=0,command=0,issued_ms=0;
    unsigned commands=0,queued=0,drained=0;
    std::array<Receipt,kReceiptCap> receipts{};
} core;
struct Lock {bool held=!core.lock.test_and_set(std::memory_order_acquire);
             ~Lock(){if(held)core.lock.clear(std::memory_order_release);}};
struct Context {Reader reader{};StorageQuery query{};std::uintptr_t image=0;
                Lease lease{};std::uint64_t lifecycle=0,command=0,issued_ms=0;};
#if defined(MGS_UPPER_MANUAL_IDLE_TESTING)
TestSubmitFn test_submit=nullptr;
TestPinFn test_pin=nullptr;
#endif
bool Range(std::uintptr_t a,std::size_t n) noexcept {
    return a&&a<=max_address&&n&&n<=0x50000&&n<=max_address-a;
}
bool Read(Reader r,std::uintptr_t a,void* out,std::size_t n) noexcept {
    return r.read&&Range(a,n)&&r.read(r.context,a,out,n);
}
template<class T>bool At(Reader r,std::uintptr_t a,std::size_t o,T& out) noexcept {
    return a&&a<=max_address&&o<=max_address-a&&Read(r,a+o,&out,sizeof(out));
}
bool PinHash(Reader reader,std::uintptr_t image,const end::Pin&pin) noexcept {
    if(!pin.size||pin.size>0x1000||pin.rva>0xa01d000||pin.size>0xa01d000-pin.rva
        ||!image||image>max_address-0xa01d000||!pin.sha256||std::strlen(pin.sha256)!=64)return false;
    std::uint8_t bytes[0x1000]{},digest[32]{};
    if(!Read(reader,image+pin.rva,bytes,pin.size))return false;
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok)ok=BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0;
    if(ok)ok=BCryptHashData(hash,bytes,static_cast<ULONG>(pin.size),0)>=0;
    if(ok)ok=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);
    constexpr char alphabet[]="0123456789abcdef";char text[65]{};
    for(unsigned n=0;n<32;++n){text[n*2]=alphabet[digest[n]>>4];text[n*2+1]=alphabet[digest[n]&15];}
    return ok&&!std::memcmp(text,pin.sha256,64);
}
bool NativeWritable(void*,std::uintptr_t at,std::size_t size) noexcept {
    if(!Range(at,size))return false;const auto end_address=at+size;
    for(unsigned segment=0;segment<32;++segment) {
        MEMORY_BASIC_INFORMATION m{};
        if(VirtualQuery(reinterpret_cast<void*>(at),&m,sizeof(m))!=sizeof(m)||m.State!=MEM_COMMIT
            ||(m.Protect&PAGE_GUARD))return false;
        const DWORD p=m.Protect&0xffu;
        if(p!=PAGE_READWRITE&&p!=PAGE_WRITECOPY&&p!=PAGE_EXECUTE_READWRITE&&p!=PAGE_EXECUTE_WRITECOPY)return false;
        const auto begin=reinterpret_cast<std::uintptr_t>(m.BaseAddress);
        // RegionSize describes the full mapping, not our bounded request.
        // A small owned record may legitimately live inside a large arena.
        if(!begin||begin>max_address||!m.RegionSize||m.RegionSize>max_address-begin
            ||begin>at||at>=begin+m.RegionSize)return false;
        if(begin+m.RegionSize>=end_address)return true;at=begin+m.RegionSize;
    }
    return false;
}
bool Mission(std::uint32_t m) noexcept {return m==30010||m==30020||m==30050;}
bool Fresh(const Lease& l,std::uint64_t now) noexcept {
    return now>=l.refreshed_ms&&now-l.refreshed_ms<=kOwnerMs;
}
bool SameOwner(const Owner&a,const Owner&b) noexcept {
    return a.generation==b.generation&&a.body_epoch==b.body_epoch&&a.mission==b.mission
        &&a.owner_thread==b.owner_thread&&a.equip==b.equip&&a.stance==b.stance&&a.flags==b.flags;
}
bool Live(const Context& c) noexcept {
    return c.lifecycle==core.lifecycle.load(std::memory_order_acquire)
        &&c.lease.owner.generation==core.active.load(std::memory_order_acquire);
}
bool IdleNode(Reader r,const Snapshot&s) noexcept {
    const auto table=s.identity.upper_table;
    if(!Range(table,122*0x48)||s.identity.upper_count!=122)return false;
    const auto node=table+89*0x48;std::uint8_t type=0;std::int32_t rel=0;std::uint64_t code=0;
    if(!At(r,node,0x14,type)||type!=3||!At(r,node,0x18,rel)||!rel||rel< -0x400000||rel>0x400000)return false;
    const auto address=static_cast<std::int64_t>(node+0x18)+rel;
    return address>0&&At(r,static_cast<std::uintptr_t>(address),0,code)&&code==kIdle;
}
const char* Resolve(const Context& c,Snapshot&s,bool require_fire,bool writable) noexcept {
    if(!phase::ReadGhostSnapshot(c.reader,c.image,c.lease.owner.mission,s))return "snapshot";
    if(!end::SameIdentity(c.lease.identity,s.identity))return "identity";
    const auto&i=s.identity;std::uint32_t count=0,bid=0;
    if(!At(c.reader,i.block,8,count)||count!=1||!At(c.reader,i.block,0xc,bid)||bid!=1
        ||!At(c.reader,i.cs,8,count)||count!=1||!At(c.reader,i.cs,0xc,bid)||bid!=1||i.entry!=i.entries)return "exact_body_bounds";
    if(!(s.entry_flags&1)||(s.entry_flags&2))return "phase_flags";
    std::uintptr_t method=0;
    if(!At(c.reader,c.image+0x22e6070,0x170,method)||method!=c.image+0xaf2ba0)return "method";
    if(!IdleNode(c.reader,s))return "idle_node";
    if(require_fire&&(s.upper.current.index!=116||s.upper.current.type!=0
        ||s.upper.current.pointer!=i.upper_table+116*0x48||s.upper.queued.pointer))return "fire_child";
    if(writable&&(!c.query.writable||!c.query.writable(c.query.context,i.buffer,0x18)
        ||!c.query.writable(c.query.context,i.records,4*0x28)))return "writable_storage";
    return Live(c)?nullptr:"lifecycle";
}
void Record(Stage stage,const Context&c,const Update&q,const Snapshot&s,std::uint64_t now,
            std::uintptr_t submit,std::uintptr_t update,const char*why,bool lower_same=false,bool attempted=false) noexcept {
    if(core.queued>=kReceiptCap)return;
    auto&out=core.receipts[core.queued++];out={};out.stage=stage;out.command=c.command;out.lifecycle=c.lifecycle;
    out.now_ms=now;out.owner=c.lease.owner;out.update=q;out.snapshot=s;
    out.raw_submit_rax=submit;out.raw_update_rax=update;out.refusal=why;out.immediate_lower_unchanged=lower_same;out.submit_attempted=attempted;
    out.upper_idle_current=s.upper.current.index==89&&s.upper.current.type==3&&s.upper.current.code==kIdle;
    out.upper_noqueued=s.upper.queued.pointer==0;
}
Context Copy() noexcept {return {core.reader,core.query,core.image,core.lease,
                                core.lifecycle.load(std::memory_order_acquire),core.command,core.issued_ms};}
const char* Isolated(const Snapshot&a,const Snapshot&b) noexcept {
    if(!end::SameIdentity(a.identity,b.identity))return "post_identity";
    if(a.lower.record!=b.lower.record)return "lower_record_changed";
    for(unsigned i=0;i<3;++i)if(((a.masks[i]^b.masks[i])&~std::uint16_t{4})!=0)return "foreign_mask_changed";
    return nullptr;
}
} // namespace
bool Compiled() noexcept {
#if defined(MGS_LOCAL_UPPER_MANUAL_IDLE) && MGS_LOCAL_UPPER_MANUAL_IDLE
    return true;
#else
    return false;
#endif
}
bool IsTrigger(const char*text,std::size_t size) noexcept {
    if(!text||size>4096)return false;
    const auto white=[](char c) noexcept {return c==' '||c=='\t'||c=='\r'||c=='\n';};
    while(size&&white(*text)){++text;--size;}
    while(size&&white(text[size-1]))--size;
    constexpr char trigger[]="upperidle=1";
    return size==sizeof(trigger)-1&&!std::memcmp(text,trigger,size);
}
bool CodeGate(std::uintptr_t image,std::size_t span,Reader reader,std::uintptr_t*mismatch) noexcept {
    if(mismatch)*mismatch=0;
    if(phase::Installed())return false;Clear();std::uint64_t lifecycle=0;
    {const Lock guard;if(!guard.held)return false;core.configured=false;core.gated_image=0;core.gated_reader={};
        lifecycle=core.lifecycle.load(std::memory_order_acquire);}
    if(!Compiled()||!phase::Compiled()||span!=0xa01d000||!image||image>max_address-span||!reader.read)return false;
    for(const auto&pin:kAdditionalPins) {
#if defined(MGS_UPPER_MANUAL_IDLE_TESTING)
        const bool matched=test_pin&&test_pin(reader,image,pin);
#else
        const bool matched=PinHash(reader,image,pin);
#endif
        if(!matched){if(mismatch)*mismatch=pin.rva;return false;}
    }
    const Lock guard;if(!guard.held||phase::Installed()||lifecycle!=core.lifecycle.load(std::memory_order_acquire))return false;
    core.gated_image=image;core.gated_reader=reader;return true;
}
bool Configure(std::uintptr_t image,Authorization auth,Reader reader,StorageQuery query) noexcept {
    if(phase::Installed())return false;Clear();const Lock guard;if(!guard.held)return false;
    core.configured=false;
    if(!Compiled()||!phase::Compiled()||!image||image>max_address-0xa01d000||!auth.offline||!auth.copied_saves
        ||!auth.exact_executable||!auth.exact_code_pins||!reader.read||core.gated_image!=image
        ||core.gated_reader.context!=reader.context||core.gated_reader.read!=reader.read)return false;
    if(!query.writable)query={nullptr,NativeWritable};
    core.reader=reader;core.query=query;core.image=image;core.configured=true;
    core.last_generation=0;core.last_command=0;core.commands=0;core.queued=core.drained=0;return true;
}
bool PublishOwner(const Owner&o,std::uint32_t actual_thread,std::uint64_t now) noexcept {
    if(!Compiled()||!o.generation||!Mission(o.mission)||!o.owner_thread||actual_thread!=o.owner_thread
        ||o.stance>2||(o.flags&~0x3fu))return false;
    Context c{};{const Lock guard;if(!guard.held||!core.configured)return false;c=Copy();c.lease.owner=o;}
    Snapshot s{};
    if(!phase::ReadGhostSnapshot(c.reader,c.image,o.mission,s))return false;
    const Lock guard;if(!guard.held||c.lifecycle!=core.lifecycle.load(std::memory_order_acquire))return false;
    const auto active=core.active.load(std::memory_order_acquire);
    if(o.generation<core.last_generation||(!active&&o.generation==core.last_generation))return false;
    if(active&&o.generation==active&&(o.body_epoch!=core.lease.owner.body_epoch||o.mission!=core.lease.owner.mission
        ||o.owner_thread!=core.lease.owner.owner_thread||!end::SameIdentity(core.lease.identity,s.identity)
        ||now<core.lease.refreshed_ms))return false;
    if(active&&(o.generation!=active||!SameOwner(o,core.lease.owner))) {
        core.pending=false;core.claimed=false;core.lifecycle.fetch_add(1,std::memory_order_acq_rel);
    }
    core.lease={o,s.identity,now};core.last_generation=o.generation;
    core.active.store(o.generation,std::memory_order_release);return true;
}
bool Arm(std::uint64_t command,std::uint32_t actual_thread,std::uint64_t now) noexcept {
    Context c{};
    {const Lock guard;if(!guard.held||!Compiled()||!core.configured||!command||command<=core.last_command
        ||!core.active.load(std::memory_order_acquire)||actual_thread!=core.lease.owner.owner_thread
        ||!Fresh(core.lease,now)||core.commands>=kCommandCap||core.queued>kReceiptCap-4)return false;
        if((core.claimed&&!core.after_done)||(core.pending&&now>=core.issued_ms&&now-core.issued_ms<=kCommandMs))return false;
        c=Copy();c.command=command;c.issued_ms=now;}
    Snapshot s{};if(Resolve(c,s,true,true))return false;
    const Lock guard;if(!guard.held||!Live(c)||!SameOwner(c.lease.owner,core.lease.owner)
        ||!Fresh(core.lease,now)||command<=core.last_command||core.commands>=kCommandCap
        ||core.queued>kReceiptCap-4)return false;
    if((core.claimed&&!core.after_done)||(core.pending&&now>=core.issued_ms&&now-core.issued_ms<=kCommandMs))return false;
    core.command=core.last_command=command;core.issued_ms=now;core.pending=true;core.claimed=false;core.after_done=false;++core.commands;
    Record(Stage::armed,c,{},s,now,0,0,nullptr);return true;
}
Ticket BeforeUpdate(const Update&q,std::uint64_t now) noexcept {
    Ticket ticket{};Context c{};
    {const Lock guard;if(!guard.held||!Compiled()||!core.configured||!core.pending||core.claimed
        ||!core.active.load(std::memory_order_acquire)||q.entry!=core.lease.identity.entry
        ||q.caller!=core.image+0xaf0381||!q.thread)return ticket;
        if(!Fresh(core.lease,now)||now<core.issued_ms||now-core.issued_ms>kCommandMs){core.pending=false;return ticket;}
        c=Copy();}
    Snapshot before{};const char*why=Resolve(c,before,true,true);
    // Serialize the final lifecycle/profile check and one submission claim.
    // Reader/query callbacks may reenter Clear before this point.
    const Lock guard;
    if(!guard.held||!Live(c)||!core.pending||core.claimed||!SameOwner(c.lease.owner,core.lease.owner)
        ||core.command!=c.command||!Fresh(core.lease,now))return ticket;
    core.pending=false;core.claimed=true;
    if(why){Record(Stage::refused,c,q,before,now,0,0,why);return ticket;}
    Record(Stage::before_submit,c,q,before,now,0,0,nullptr);
    // Exactly the fingerprinted method/ABI. Test builds cannot reach this
    // game pointer; their validated method is replaced by a required fake.
    using SubmitFn=std::uintptr_t(*)(void*,std::uint32_t,std::uint32_t,std::uint64_t,float,std::uint32_t);
    std::uintptr_t result=0;
#if defined(MGS_UPPER_MANUAL_IDLE_TESTING)
    if(!test_submit){Record(Stage::refused,c,q,before,now,0,0,"missing_test_original");return ticket;}
    result=test_submit(reinterpret_cast<void*>(before.identity.cs),1,2,kIdle,0.0f,0);
#else
    const auto submit=reinterpret_cast<SubmitFn>(c.image+0xaf2ba0);
    result=submit(reinterpret_cast<void*>(before.identity.cs),1,2,kIdle,0.0f,0);
#endif
    Snapshot after{};why=Resolve(c,after,false,false);if(!why)why=Isolated(before,after);
    if(why) {
        // Retain the terminal diagnostic in its new invalidated lifecycle.
        // Nothing retries or restores bytes after an unexpected submission.
        core.active.store(0,std::memory_order_release);c.lifecycle=core.lifecycle.fetch_add(1,std::memory_order_acq_rel)+1;
        Record(Stage::refused,c,q,after,now,result,0,why,false,true);return ticket;
    }
    Record(Stage::after_submit,c,q,after,now,result,0,nullptr,true,true);
    ticket={true,c.command,c.lifecycle,c.issued_ms,c.lease,q};return ticket;
}
void AfterUpdate(const Ticket&t,std::uintptr_t rax,std::uint64_t now) noexcept {
    if(!Compiled()||!t.submitted)return;Context c{};
    {const Lock guard;if(!guard.held||!core.configured||t.lifecycle!=core.lifecycle.load(std::memory_order_acquire)
        ||t.command!=core.command||!core.claimed||core.after_done||!Fresh(core.lease,now)
        ||!SameOwner(t.lease.owner,core.lease.owner))return;c=Copy();}
    Snapshot after{};const char*why=Resolve(c,after,false,false);
    const Lock guard;if(!guard.held||!Live(c)||t.command!=core.command||!core.claimed||core.after_done)return;
    core.after_done=true;
    if(why){core.active.store(0,std::memory_order_release);c.lifecycle=core.lifecycle.fetch_add(1,std::memory_order_acq_rel)+1;
        Record(Stage::refused,c,t.update,after,now,0,rax,why,false,true);return;}
    Record(Stage::after_update,c,t.update,after,now,0,rax,nullptr,false,true);
}
bool Drain(Receipt&out) noexcept {
    const Lock guard;if(!guard.held)return false;
    while(core.drained<core.queued){const auto&r=core.receipts[core.drained++];
        const auto active=core.active.load(std::memory_order_acquire);
        if(r.lifecycle==core.lifecycle.load(std::memory_order_acquire)
            &&(r.owner.generation==active||(!active&&r.stage==Stage::refused&&r.submit_attempted))){out=r;return true;}}
    return false;
}
void Clear() noexcept {
    core.active.store(0,std::memory_order_release);core.lifecycle.fetch_add(1,std::memory_order_acq_rel);
    const Lock guard;if(guard.held){core.lease={};core.pending=false;core.claimed=false;core.after_done=false;}
}
std::uintptr_t ForwardUpdate(phase::OriginalFn original,void* entry,std::uintptr_t caller,
                            std::uint32_t thread,std::uint64_t now) noexcept {
    const DWORD incoming=GetLastError();const auto ticket=BeforeUpdate({reinterpret_cast<std::uintptr_t>(entry),caller,thread},now);
    SetLastError(incoming);const auto rax=phase::ForwardUpdate(original,entry,caller,thread,now);
    const DWORD outgoing=GetLastError();AfterUpdate(ticket,rax,GetTickCount64());SetLastError(outgoing);return rax;
}
#if defined(MGS_UPPER_MANUAL_IDLE_TESTING)
void TestSubmit(TestSubmitFn fn) noexcept {const Lock guard;if(guard.held)test_submit=fn;}
bool TestWritable(std::uintptr_t at,std::size_t size) noexcept {return NativeWritable(nullptr,at,size);}
void TestPins(TestPinFn fn) noexcept {const Lock guard;if(guard.held)test_pin=fn;}
bool TestPinHash(Reader reader,std::uintptr_t image,const end::Pin&pin) noexcept {return PinHash(reader,image,pin);}
#endif
} // namespace mgscoop::partner_upper_manual_idle
