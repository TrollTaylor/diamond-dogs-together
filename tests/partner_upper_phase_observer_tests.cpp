// Detached fake-memory and forwarding fixtures only. No game/native calls.
#include "mgscoop/partner_upper_phase_observer.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <thread>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
namespace e = mgscoop::partner_upper_end_observer;
namespace p = mgscoop::partner_upper_phase_observer;
unsigned checks = 0;
void Check(bool ok, const char* why) { ++checks; if (!ok) { std::fprintf(stderr, "FAIL %u: %s\n", checks, why); std::exit(1); } }
bool Is(const char* actual, const char* expected) { return actual && std::strcmp(actual, expected) == 0; }
constexpr std::uintptr_t base = 0x140000000, P = 0x200000, Q = 0x201000, S = 0x202000, block = 0x203000;
constexpr std::uintptr_t ps = 0x204000, cs = 0x205000, entry = 0x206000, controllers = 0x207000;
constexpr std::uintptr_t buffer = controllers + 0x20, records = buffer + 0x20, lower = 0x208000, upper = 0x209000;
constexpr std::uintptr_t bank = 0x20a000, descriptors = 0x20b000, lower_table = 0x20c000, upper_table = 0x20d000;
struct Memory {
    std::map<std::uintptr_t, std::uint8_t> bytes;
    unsigned reads = 0; std::uintptr_t refuse = 0; bool clear_on_read = false;
    void Fill(std::uintptr_t at, std::size_t n) { for (std::size_t i = 0; i < n; ++i) bytes[at + i] = 0; }
    template<class T> void Put(std::uintptr_t at, const T& value) {
        const auto* raw = reinterpret_cast<const std::uint8_t*>(&value);
        for (std::size_t i = 0; i < sizeof(value); ++i) bytes[at + i] = raw[i];
    }
    void Ptr(std::uintptr_t at, std::uintptr_t value) { Put(at, value); }
    void U32(std::uintptr_t at, std::uint32_t value) { Put(at, value); }
    void U8(std::uintptr_t at, std::uint8_t value) { Put(at, value); }
    void Relative(std::uintptr_t at, std::uintptr_t target) { Put(at, static_cast<std::int32_t>(target - at)); }
    Memory() {
        Put(base + 0x2c01372, std::uint16_t(30050)); Ptr(base + 0x2a82c30, P);
        Fill(P, 0x390); Fill(Q, 0x200); Fill(S, 0x50); Fill(block, 0x6a8); Fill(ps, 0x410);
        Fill(cs, 0x18); Fill(entry, 0xc0); Fill(controllers, 0xe0); Fill(lower, 0x200); Fill(upper, 0x200);
        Fill(bank, 0x30); Fill(descriptors, 4 * 0x38); Fill(lower_table, 10 * 0x48); Fill(upper_table, 122 * 0x48);
        Ptr(P, base + 0x2295490); U32(P + 0x204, 1); U32(P + 0x208, 2); U32(P + 0x20c, 2); Ptr(P + 0x60, Q);
        Ptr(Q, base + 0x23ad970); Ptr(Q + 0x1e8, S); U32(S + 0x40, 2); Ptr(S, 0x240040); U8(0x240040, 3);
        Ptr(P + 0x70, 0x240000); Ptr(P + 0x78, 0x240010); Ptr(P + 0x90, 0x240020);
        Put(0x240000, std::uint16_t(0)); Put(0x240002, std::uint16_t(1));
        U8(0x240010, 0); U8(0x240011, 1); U8(0x240020, 3); U8(0x240021, 3);
        Ptr(P + 0x378, block - 0x6a8); Ptr(P + 0x58, block - 0x6a8); Ptr(P + 0x50, 0x240030); Ptr(0x240038, block);
        Ptr(block, base + 0x2295210); Ptr(block + 0x138, P); U32(block + 8, 1); U32(block + 0xc, 1); Ptr(block + 0x78, ps);
        Ptr(ps, base + 0x23b09f0); U32(ps + 8, 1); Ptr(ps + 0x408, block); Ptr(ps + 0x238, cs);
        Ptr(cs, base + 0x22e6070); U32(cs + 8, 1); U32(cs + 0xc, 1); Ptr(cs + 0x10, entry);
        Ptr(entry + 0x78, controllers - 0x2100); Ptr(entry + 0x90, controllers); Ptr(entry + 0x98, buffer);
        U32(entry + 0xac, 1); U32(entry + 0xb0, 4); U32(buffer + 0xc, 0x28); Ptr(buffer + 0x10, records);
        Ptr(controllers, lower); Ptr(controllers + 16, upper);
        Ptr(lower, base + 0x250ba50); Ptr(upper, base + 0x250ba50); Ptr(lower + 0x1b8, bank); Ptr(upper + 0x1b8, bank);
        U32(upper + 0xd4, 2); U32(bank + 0x1c, 4); Relative(bank + 0x20, descriptors);
        U32(descriptors + 8, 10); Relative(descriptors + 12, lower_table);
        U32(descriptors + 2 * 0x38 + 8, 122); Relative(descriptors + 2 * 0x38 + 12, upper_table);
        for (unsigned i = 0; i < 122; ++i) {
            const auto node = upper_table + i * 0x48, code = std::uintptr_t(0x220000 + i * 8);
            Relative(node + 0x18, code); Put(code, std::uint64_t(0x1000 + i));
        }
        for (unsigned i = 0; i < 10; ++i) {
            const auto node = lower_table + i * 0x48, code = std::uintptr_t(0x221000 + i * 8);
            Relative(node + 0x18, code); Put(code, std::uint64_t(0x2000 + i));
        }
        Put(0x220000 + 103 * 8, e::kEnd); Put(0x220000 + 65 * 8, std::uint64_t(0x91a5b8ac42f8));
        Put(0x220000 + 116 * 8, std::uint64_t(0xb8a0bf169f98));
        U8(upper_table + 65 * 0x48 + 0x14, 6); Relative(upper_table + 65 * 0x48 + 0x24, upper_table + 116 * 0x48);
        Ptr(lower + 0x1a0, lower_table + 2 * 0x48); Ptr(upper + 0x1a0, upper_table + 20 * 0x48);
    }
    static bool Read(void* context, std::uintptr_t at, void* out, std::size_t n) noexcept {
        auto& m = *static_cast<Memory*>(context); ++m.reads; SetLastError(901);
        if (m.clear_on_read) { m.clear_on_read = false; p::Clear(); }
        if (m.refuse && at <= m.refuse && m.refuse - at < n) return false;
        auto* raw = static_cast<std::uint8_t*>(out);
        for (std::size_t i = 0; i < n; ++i) {
            const auto found = m.bytes.find(at + i); if (found == m.bytes.end()) return false; raw[i] = found->second;
        }
        return true;
    }
    e::Reader Reader() { return {this, Read}; }
};
p::Owner Owner(std::uint64_t generation = 7) { return {generation, 8, 30050, GetCurrentThreadId(), 873, 1, 1}; }
p::Update Update(std::uint32_t thread = 999) { return {entry,base+0xaf0381,thread}; }
bool Configure(Memory& memory) { return p::Configure(base, {true,true,true,true}, memory.Reader()); }
std::vector<p::Observation> Drain() {
    std::vector<p::Observation> rows; p::Observation row{};
    while (p::Drain(row)) rows.push_back(row); return rows;
}
void Resolver() {
    Memory memory; p::Snapshot snapshot{};
    Check(p::ReadGhostSnapshot(memory.Reader(),base,30050,snapshot), "exact allocated body1 resolves independently");
    Check(snapshot.identity.block==block && snapshot.identity.entry==entry && snapshot.identity.cs==cs
        && snapshot.end_node.index==103 && snapshot.fire_selector.child_index==116, "remote hierarchy and exact graph ancestry");
    Check(snapshot.identity.allocation==controllers-0x2100 && snapshot.identity.controllers==controllers,
        "constructor workspace prefix is allowed while actual array/buffer/record layout stays exact");
    for (unsigned i=0;i<23;++i) {
        Memory bad;
        if(i==0)bad.U32(P+0x214,1);
        if(i==1)bad.U32(P+0x204,0x101);
        if(i==2)bad.Ptr(P+0xa8,1);
        if(i==3)bad.U32(P+0x208,1);
        if(i==4)bad.U32(P+0x20c,1);
        if(i==5)bad.U8(0x240040,1);
        if(i==6)bad.U8(0x240021,0);
        if(i==7)bad.Ptr(0x240038,block+1);
        if(i==8)bad.Ptr(block+0x138,P+1);
        if(i==9)bad.Ptr(ps+0x408,block+1);
        if(i==10)bad.U32(cs+0xc,2);
        if(i==11)bad.U32(ps+8,0);
        if(i==12)bad.U32(entry+0xb0,2);
        if(i==13)bad.Ptr(entry+0x98,buffer+1);
        if(i==14)bad.U8(buffer+8,1);
        if(i==15)bad.Ptr(buffer+0x10,records+1);
        if(i==16)bad.Ptr(controllers+16,lower);
        if(i==17)bad.U32(upper+0xd4,0);
        if(i==18)bad.U32(descriptors+2*0x38+8,121);
        if(i==19)bad.Relative(upper_table+65*0x48+0x24,upper_table+117*0x48);
        if(i==20)bad.Ptr(upper+0x1a0,upper_table+1);
        if(i==21)bad.refuse=records+80;
        if(i==22)bad.Put(base+0x2c01372,std::uint16_t(30010));
        p::Snapshot untouched{};untouched.end_node.code=123;
        Check(!p::ReadGhostSnapshot(bad.Reader(),base,30050,untouched)&&untouched.end_node.code==123,
            "malformed body/capacity/graph/read fails atomically");
    }
    Memory ranged;
    // Body1 can inherit a CS range whose base is0, so its entry is entries+C0.
    ranged.U32(block+8,2);ranged.U32(block+0xc,0);ranged.U32(ps+8,0);
    ranged.U32(cs+8,2);ranged.U32(cs+0xc,0);ranged.Ptr(cs+0x10,entry-0xc0);
    Check(p::ReadGhostSnapshot(ranged.Reader(),base,30050,snapshot)&&snapshot.identity.entry==entry,
        "validated base0/count2 range selects body1 entry exactly");
}
void PolicyAndBudget() {
    Memory m;
    if(!p::Compiled()) {
        Check(!Configure(m),"OFF configuration is unavailable"); const auto n=m.reads;
        Check(!p::PublishGhostOwner(Owner(),GetCurrentThreadId(),1000),"OFF cannot publish");
        Check(!p::BeforeUpdate(Update(),1000).qualified&&m.reads==n,"OFF never reads or qualifies callbacks");
        Check(!p::Install(reinterpret_cast<void*>(base),0xa01d000)&&!p::Installed(),"OFF cannot install hook");return;
    }
    for(unsigned field=0;field<4;++field) {
        p::Authorization a{true,true,true,true};
        if(field==0)a.offline=false;if(field==1)a.copied_saves=false;
        if(field==2)a.exact_executable=false;if(field==3)a.exact_code_pins=false;
        Check(!p::Configure(base,a,m.Reader()),"each authorization requirement is independent");
    }
    Check(Configure(m),"ON configures read-only probe");
    const auto owner=Owner();
    Check(!p::PublishGhostOwner(owner,owner.owner_thread+1,1000),"worker cannot publish natural owner lease");
    Check(p::PublishGhostOwner(owner,owner.owner_thread,1000),"natural publisher independently pins ghost lease");
    for(unsigned field=0;field<4;++field) {
        auto q=Update();if(field==0)q.entry-=0xc0;if(field==1)++q.caller;if(field==2)q.thread=0;
        const auto n=m.reads;
        Check(!p::BeforeUpdate(q,field==3?1251:1000).qualified&&n==m.reads,"foreign/local/caller/thread/expired tuple fast rejects without RPM");
    }
    auto t=p::BeforeUpdate(Update(),1000);
    Check(t.qualified&&t.update.thread!=t.lease.owner.owner_thread,"actual natural worker phase is observed with owner-issued authority");
    m.Ptr(upper+0x1a0,upper_table+116*0x48);m.Put(records+80+32,std::uint64_t(0x91a5b8ac42f8));
    p::AfterUpdate(t,0xfedcba9876543210,1001);
    auto rows=Drain();Check(rows.size()==2&&rows[0].serial==rows[1].serial&&rows[1].after
        &&rows[0].snapshot.upper.current.index==20&&rows[1].snapshot.upper.current.index==116
        &&rows[1].snapshot.lower.current.index==2&&rows[1].original_rax==0xfedcba9876543210,
        "paired readbacks retain lower and opaque RAX without acceptance/completion assertion");
    auto n=m.reads;p::AfterUpdate(t,3,1002);Check(m.reads==n&&Drain().empty(),"duplicate after refused before reads");
    Check(p::BeforeUpdate(Update(),1002).qualified,"second phase baseline can sample immediately");
    n=m.reads;Check(!p::BeforeUpdate(Update(),1003).qualified&&m.reads==n,"cadence rejects without reads after two baselines");
    p::Clear();Check(!p::PublishGhostOwner(owner,owner.owner_thread,1004),"Clear cannot resurrect same generation");
    auto fresh=Owner(8);Check(p::PublishGhostOwner(fresh,fresh.owner_thread,2000),"fresh generation accepted after lifecycle invalidation");
    t=p::BeforeUpdate(Update(),2000);m.clear_on_read=true;p::AfterUpdate(t,4,2001);
    Check(Drain().empty(),"Clear during RPM invalidates in-flight after and queued before");
    fresh=Owner(9);Check(p::PublishGhostOwner(fresh,fresh.owner_thread,3000),"new generation after read invalidation");
    for(unsigned pair=0;pair<p::kPairCap;++pair) {
        const auto now=3000+pair*100;
        Check(p::PublishGhostOwner(fresh,fresh.owner_thread,now),"owner refresh retains immutable allocation and budget");
        t=p::BeforeUpdate(Update(),now);Check(t.qualified,"bounded natural phase pair qualifies");p::AfterUpdate(t,pair,now+1);
        rows=Drain();Check(rows.size()==2,"every bounded pair drains exactly twice");
    }
    Check(p::PublishGhostOwner(fresh,fresh.owner_thread,7000),"refresh after cap still maintains lease");
    n=m.reads;Check(!p::BeforeUpdate(Update(),7000).qualified&&m.reads==n,"all64 lifetime cap survives drain and refresh");
    auto changed=fresh;++changed.body_epoch;Check(!p::PublishGhostOwner(changed,changed.owner_thread,7001),"body epoch change requires new generation");
    ++changed.generation;
    Check(p::PublishGhostOwner(changed,changed.owner_thread,7001),"new owner-issued observation generation renews budget independently of native playback");
    t=p::BeforeUpdate(Update(),7001);Check(t.qualified,"renewed manual observation window captures immediately after prior64 cap");
    p::AfterUpdate(t,5,7002);Check(Drain().size()==2,"renewed observation window retains full paired readbacks");
    p::Clear();
}
void EpochZero() {
    Memory memory;auto owner=Owner(200);owner.body_epoch=0;owner.mission=30010;
    memory.Put(base+0x2c01372,std::uint16_t(owner.mission));
    if(!p::Compiled()) {
        Check(!Configure(memory),"OFF epoch0 configuration unavailable");const auto reads=memory.reads;
        Check(!p::PublishGhostOwner(owner,owner.owner_thread,1000)
            &&!p::BeforeUpdate(Update(),1000).qualified&&memory.reads==reads,
            "legitimate epoch0 does not enable OFF observation or reads");return;
    }
    Check(Configure(memory),"epoch0 fixture configures exact allocated mission30010 body");
    auto invalid=owner;invalid.generation=0;auto reads=memory.reads;
    Check(!p::PublishGhostOwner(invalid,invalid.owner_thread,1000)&&memory.reads==reads,
        "zero observation generation still fast rejects despite valid epoch0");
    Check(!p::PublishGhostOwner(owner,owner.owner_thread+1,1000)&&memory.reads==reads,
        "epoch0 does not authorize worker-issued owner leases");
    Check(p::PublishGhostOwner(owner,owner.owner_thread,1000),"initial allocation epoch0 is a valid immutable lease");
    reads=memory.reads;auto foreign=Update();foreign.entry-=0xc0;
    Check(!p::BeforeUpdate(foreign,1000).qualified&&memory.reads==reads,
        "epoch0 local or foreign entry remains a zero-read rejection");
    auto ticket=p::BeforeUpdate(Update(),1000);
    Check(ticket.qualified&&ticket.lease.owner.body_epoch==0&&ticket.lease.owner.generation==owner.generation,
        "natural worker ticket retains owner-issued epoch0 and generation");
    p::AfterUpdate(ticket,0x123456789abcdef0,1001);auto rows=Drain();
    Check(rows.size()==2&&rows[0].owner.body_epoch==0&&rows[1].owner.body_epoch==0
        &&rows[1].original_rax==0x123456789abcdef0&&rows[1].update.entry==entry,
        "epoch0 before/after receipt preserves identity, epoch and opaque original return");
    Check(p::PublishGhostOwner(owner,owner.owner_thread,1001),"ordinary epoch0 refresh keeps the same observation generation");
    const auto pending=p::BeforeUpdate(Update(),1001);Check(pending.qualified,"second epoch0 baseline qualifies");
    Check(p::PublishGhostOwner(owner,owner.owner_thread,1002),"second ordinary epoch0 refresh accepted");
    reads=memory.reads;Check(!p::BeforeUpdate(Update(),1002).qualified&&memory.reads==reads,
        "epoch0 refresh does not reset baseline budget or cadence");
    auto changed=owner;++changed.body_epoch;
    Check(!p::PublishGhostOwner(changed,changed.owner_thread,1003),"epoch0 to1 requires a new observation generation");
    ++changed.generation;
    Check(p::PublishGhostOwner(changed,changed.owner_thread,1003),"new owner generation admits legitimate epoch1 after allocation change");
    reads=memory.reads;p::AfterUpdate(pending,99,1004);
    Check(memory.reads==reads&&Drain().empty(),"new epoch/generation suppresses old in-flight epoch0 ticket and queued receipt before RPM");
    ticket=p::BeforeUpdate(Update(),1004);Check(ticket.qualified&&ticket.lease.owner.body_epoch==1,
        "new epoch receives its own immediate baseline budget");
    p::AfterUpdate(ticket,77,1005);rows=Drain();
    Check(rows.size()==2&&rows[0].owner.generation==changed.generation&&rows[1].owner.body_epoch==1,
        "only new epoch/generation receipts survive lifecycle change");
    p::Clear();Check(!p::PublishGhostOwner(changed,changed.owner_thread,1006),"Clear cannot resurrect epoch1 with the same generation");
    ++owner.generation;++owner.generation;
    Check(p::PublishGhostOwner(owner,owner.owner_thread,2000),"fresh mission observation generation can legitimately start at epoch0 again");
    for(unsigned pair=0;pair<p::kPairCap;++pair) {
        const auto now=2000+pair*100;
        Check(p::PublishGhostOwner(owner,owner.owner_thread,now),"epoch0 lifetime owner refresh succeeds without churn");
        ticket=p::BeforeUpdate(Update(),now);Check(ticket.qualified,"epoch0 bounded natural pair qualifies");
        p::AfterUpdate(ticket,pair,now+1);rows=Drain();
        Check(rows.size()==2&&rows[0].owner.generation==owner.generation&&rows[1].owner.body_epoch==0,
            "epoch0 bounded pair preserves one stable observation generation");
    }
    Check(p::PublishGhostOwner(owner,owner.owner_thread,6000),"epoch0 refresh after lifetime cap maintains the lease");
    reads=memory.reads;Check(!p::BeforeUpdate(Update(),6000).qualified&&memory.reads==reads,
        "epoch0 all64 lifetime cap survives ordinary refresh and drain");
    p::Clear();reads=memory.reads;p::AfterUpdate(ticket,88,6001);
    Check(memory.reads==reads&&Drain().empty(),"Clear invalidates old epoch0 callbacks before RPM");
    Check(!p::PublishGhostOwner(owner,owner.owner_thread,6001),"Clear cannot resurrect an epoch0 lease with the same generation");
}
void* original_entry=nullptr; unsigned original_calls=0; std::uint32_t original_error=0;
std::uintptr_t FakeOriginal(void* entry_ptr) { ++original_calls;original_entry=entry_ptr;original_error=GetLastError();SetLastError(0x1234);return 0xfedcba9876543210; }
void Forwarding() {
    Memory memory;const auto now=GetTickCount64();
    if(p::Compiled()){Check(Configure(memory),"forward configure");auto owner=Owner(100);Check(p::PublishGhostOwner(owner,owner.owner_thread,now),"forward owner");}
    SetLastError(0x4321);
    const auto rax=p::ForwardUpdate(FakeOriginal,reinterpret_cast<void*>(entry),base+0xaf0381,999,now);
    Check(original_calls==1&&original_entry==reinterpret_cast<void*>(entry)&&original_error==0x4321
        &&rax==0xfedcba9876543210&&GetLastError()==0x1234,"real wrapper preserves Entry*, raw RAX and both LastError boundaries exactly");
    if(p::Compiled()){const auto rows=Drain();Check(rows.size()==2,"real forwarding boundary captures before/after ghost independently");}
    p::Clear();const auto reads=memory.reads;SetLastError(0x7777);
    Check(p::ForwardUpdate(FakeOriginal,reinterpret_cast<void*>(0x8888),base+0x1234,444,now)==0xfedcba9876543210
        &&original_calls==2&&original_entry==reinterpret_cast<void*>(0x8888)&&original_error==0x7777&&memory.reads==reads,
        "unrelated calls preserve original semantics and read no body");
}
constexpr std::uintptr_t local_block=block-0x6a8, local_ps=0x250000, local_cs=0x251000, local_entry=0x252000;
constexpr std::uintptr_t local_controllers=0x253000, local_buffer=local_controllers+0x20, local_records=local_buffer+0x20;
constexpr std::uintptr_t local_lower=0x254000, local_upper=0x255000;
void CopyRange(Memory& m, std::uintptr_t source, std::uintptr_t target, std::size_t size) {
    for(std::size_t i=0;i<size;++i)m.bytes[target+i]=m.bytes.at(source+i);
}
void AddLocalBody(Memory& m) {
    // Same fake process P/Q/S has two disjoint, independently owned blocks.
    CopyRange(m,block,local_block,0x6a8);CopyRange(m,ps,local_ps,0x410);
    CopyRange(m,cs,local_cs,0x18);CopyRange(m,entry,local_entry,0xc0);
    CopyRange(m,controllers,local_controllers,0xe0);
    CopyRange(m,lower,local_lower,0x200);CopyRange(m,upper,local_upper,0x200);
    m.Ptr(P+0x58,local_block);m.Ptr(0x240030,local_block);
    m.U32(local_block+0xc,0);m.Ptr(local_block+0x78,local_ps);
    m.U32(local_ps+8,0);m.Ptr(local_ps+0x408,local_block);m.Ptr(local_ps+0x238,local_cs);
    m.U32(local_cs+0xc,0);m.Ptr(local_cs+0x10,local_entry);
    m.Ptr(local_entry+0x78,local_controllers-0x2100);m.Ptr(local_entry+0x90,local_controllers);m.Ptr(local_entry+0x98,local_buffer);
    m.Ptr(local_buffer+0x10,local_records);m.Ptr(local_controllers,local_lower);m.Ptr(local_controllers+16,local_upper);
}
std::vector<e::Observation> DrainEnd() {
    std::vector<e::Observation> rows;e::Observation row{};
    while(e::Drain(row))rows.push_back(row);return rows;
}
unsigned reentrant_calls=0;void* reentrant_entry=nullptr;std::uint32_t reentrant_error=0;
std::uintptr_t ReentrantOriginal(void* entry_ptr) {
    ++reentrant_calls;reentrant_entry=entry_ptr;reentrant_error=GetLastError();
    const auto nested=p::ForwardUpdate(FakeOriginal,reinterpret_cast<void*>(0x8888),base+0x1234,444,GetTickCount64());
    Check(nested==0xfedcba9876543210&&GetLastError()==0x1234,"reentrant foreign original returns exact raw RAX/LastError");
    SetLastError(0xABCD);return 0x8765432101234567;
}
void CombinedForwarding() {
    if(!p::Compiled()||!e::Compiled())return;
    Memory memory;AddLocalBody(memory);const auto now=GetTickCount64();auto owner=Owner(7);owner.body_epoch=0;
    Check(Configure(memory)&&e::Configure(base,{true,true,true,true},memory.Reader()),"combined independent observers configure same fake process");
    Check(p::PublishGhostOwner(owner,owner.owner_thread,now)&&e::PublishOwner(owner,owner.owner_thread,now),"same established Lua owner independently leases two allocated bodies");
    e::Snapshot local{};p::Snapshot ghost{};
    Check(e::ReadSnapshot(memory.Reader(),base,30050,local)&&p::ReadGhostSnapshot(memory.Reader(),base,30050,ghost)
        &&local.identity.P==ghost.identity.P&&local.identity.Q==ghost.identity.Q&&local.identity.S==ghost.identity.S
        &&local.identity.block!=ghost.identity.block&&local.identity.cs!=ghost.identity.cs&&local.identity.entry!=ghost.identity.entry
        &&local.identity.lower!=ghost.identity.lower&&local.identity.upper!=ghost.identity.upper,
        "one fake process proves local/ghost leases select disjoint blocks and CS entries");
    const e::Submission submission{local_cs,base+0x105d3b3,0,2,999,0x41000000,0,e::kEnd,false};
    const auto end_ticket=e::BeforeSubmission(submission,now);Check(end_ticket.qualified,"qualify exact natural local End170 before multiplexed updates");
    e::AfterSubmission(end_ticket,7,now);Check(DrainEnd().size()==2,"local natural End submission receipt paired");
    original_calls=0;SetLastError(0x4321);
    auto rax=p::ForwardUpdate(FakeOriginal,reinterpret_cast<void*>(local_entry),base+0xaf0381,999,now);
    Check(rax==0xfedcba9876543210&&original_calls==1&&original_entry==reinterpret_cast<void*>(local_entry)
        &&original_error==0x4321&&GetLastError()==0x1234,"multiplexed body0 update forwards original exactly once");
    auto local_rows=DrainEnd();auto ghost_rows=Drain();
    Check(local_rows.size()==2&&local_rows[0].phase==e::Phase::update_before&&local_rows[1].phase==e::Phase::update_after
        &&local_rows[0].correlated_end==end_ticket.serial&&local_rows[0].snapshot.identity.entry==local_entry&&ghost_rows.empty(),
        "actual body0 forwarding records only independently qualified local End phase");
    SetLastError(0x4322);rax=p::ForwardUpdate(FakeOriginal,reinterpret_cast<void*>(entry),base+0xaf0381,999,GetTickCount64());
    Check(rax==0xfedcba9876543210&&original_calls==2&&original_entry==reinterpret_cast<void*>(entry)
        &&original_error==0x4322&&GetLastError()==0x1234,"multiplexed body1 update forwards original exactly once");
    local_rows=DrainEnd();ghost_rows=Drain();
    Check(local_rows.empty()&&ghost_rows.size()==2&&!ghost_rows[0].after&&ghost_rows[1].after
        &&ghost_rows[0].snapshot.identity.entry==entry&&ghost_rows[1].original_rax==rax
        &&ghost_rows[0].owner.body_epoch==0&&ghost_rows[1].owner.body_epoch==0,
        "actual body1 forwarding records only independent remote phase lease");
    const auto reads=memory.reads;SetLastError(0x4323);
    rax=p::ForwardUpdate(FakeOriginal,reinterpret_cast<void*>(0x8888),base+0xaf0381,999,GetTickCount64());
    Check(rax==0xfedcba9876543210&&original_calls==3&&original_error==0x4323&&GetLastError()==0x1234
        &&memory.reads==reads&&DrainEnd().empty()&&Drain().empty(),"foreign entry has zero observer reads and one unchanged original call");
    SetLastError(0x4324);rax=p::ForwardUpdate(ReentrantOriginal,reinterpret_cast<void*>(entry),base+0xaf0381,999,GetTickCount64());
    Check(rax==0x8765432101234567&&reentrant_calls==1&&reentrant_entry==reinterpret_cast<void*>(entry)
        &&reentrant_error==0x4324&&original_calls==4&&original_entry==reinterpret_cast<void*>(0x8888)&&GetLastError()==0xABCD,
        "reentrant wrapper preserves each original argument/RAX/LastError independently without routing");
    Check(Drain().size()==2&&DrainEnd().empty(),"reentrant foreign entry cannot consume local End observation or remote before/after pairing");
    p::Clear();SetLastError(0x4325);
    rax=p::ForwardUpdate(FakeOriginal,reinterpret_cast<void*>(local_entry),base+0xaf0381,999,GetTickCount64());
    Check(rax==0xfedcba9876543210&&original_calls==5&&original_error==0x4325&&GetLastError()==0x1234
        &&DrainEnd().size()==2&&Drain().empty(),"remote Clear leaves local End lease and reserved update budget active");
    e::Clear();++owner.generation;
    Check(p::PublishGhostOwner(owner,owner.owner_thread,GetTickCount64()),"remote lease renews independently after local End Clear");
    SetLastError(0x4326);rax=p::ForwardUpdate(FakeOriginal,reinterpret_cast<void*>(entry),base+0xaf0381,999,GetTickCount64());
    Check(rax==0xfedcba9876543210&&original_calls==6&&original_error==0x4326&&GetLastError()==0x1234
        &&Drain().size()==2&&DrainEnd().empty(),"local End Clear leaves remote phase authority and budget independent");
    p::Clear();e::Clear();
}
int main() {
    Resolver();PolicyAndBudget();EpochZero();Forwarding();CombinedForwarding();
    std::printf("PASS: %u upper phase checks; compiled=%d; end_compiled=%d; native_calls=0; writes=0\n",checks,p::Compiled(),e::Compiled());return 0;
}
