#include "mgscoop/equipment_stock_contract.hpp"
#include <cstdio>
#include <cstdlib>
#include <map>
namespace {
using namespace mgscoop::equipment_stock_contract;
unsigned checks=0;
void Check(bool value) { ++checks; if(!value) { std::fprintf(stderr,"stock contract check %u failed\n",checks); std::exit(1); } }
constexpr std::uintptr_t base=0x140000000ull,manager=0x10000000,request=0x10001000,queue=0x10002000;
constexpr std::uintptr_t queueData=0x10003000,fixture_container=0x10004000,fixture_globals=0x10005000,fixture_services=0x10006000;
constexpr std::uintptr_t engine=0x10007000,fixture_dependency=0x10008000,info=0x10009000,cache=0x1000B000,refs=0x1000C000;
struct Memory {
    std::map<std::uintptr_t,std::uint8_t> bytes;
    std::uintptr_t unreadable=0;
    template<class T> void Set(std::uintptr_t at,const T& value) {
        const auto* p=reinterpret_cast<const std::uint8_t*>(&value);
        for(std::size_t i=0;i<sizeof(T);++i) bytes[at+i]=p[i];
    }
    bool operator()(std::uintptr_t at,void* out,std::size_t size) {
        if(unreadable && at<=unreadable && unreadable-at<size) return false;
        auto* p=static_cast<std::uint8_t*>(out);
        for(std::size_t i=0;i<size;++i) {
            const auto found=bytes.find(at+i); if(found==bytes.end()) return false; p[i]=found->second;
        }
        return true;
    }
    void Method(std::uintptr_t object,std::size_t vt,std::size_t slot,std::size_t fn) {
        Set(object,base+vt); Set(base+vt+slot,base+fn);
    }
    void Append() {
        Set(queue,std::uint32_t(6));
        for(unsigned i=0;i<hashes.size();++i) Set(queueData+(i+2)*8,hashes[i]);
    }
};
Memory Ready() {
    Memory m; m.Set(base+0x2A82C30,std::uintptr_t(0));
    m.Set(base+0x2C1E3B0,manager); m.Set(manager+0xF0,request); m.Set(request+0x18,queue);
    for(auto offset : {0x40u,0x48u,0x50u,0x58u,0x60u,0x68u,0x70u}) {
        const std::uintptr_t group=0x11000000+offset*0x100;
        m.Set(manager+offset,group); m.Set(group+0x18,std::uint32_t(4096));
    }
    m.Set(base+0x2C3A8A0,fixture_container); m.Set(fixture_container+8,fixture_globals); m.Set(fixture_globals+0x98,fixture_services);
    m.Set(fixture_services+0x1E8,engine); m.Method(engine,0x234B840,0x280,0xDC3840);
    const auto types=base+0x2A70920,resource=base+0x2A711F0;
    m.Set(engine+8,types); m.Method(types,0x22AD180,0,0xA2A3E0); m.Method(types,0x22AD180,0x10,0xA2A520);
    m.Set(engine+0x10,resource); m.Method(resource,0x22B85C0,0x10,0xA3C670);
    m.Set(engine+0x18,fixture_dependency); m.Method(fixture_dependency,0x2349EE0,0x10,0xDB69E0); m.Method(fixture_dependency,0x2349EE0,0x18,0xDB6AC0);
    m.Method(fixture_dependency,0x2349EE0,0x20,0xDB6CB0); m.Method(fixture_dependency,0x2349EE0,0x28,0xDB69D0);
    m.Method(fixture_dependency,0x2349EE0,0x30,0xDB6A20); m.Method(fixture_dependency,0x2349EE0,0x38,0xDB6CC0);
    m.Method(fixture_dependency,0x2349EE0,0x40,0xDB6CD0);
    m.Set(base+0x2349348+36*16,hashes[1]); m.Set(base+0x2349348+55*16,hashes[2]);
    m.Set(base+0x2349348+64*16,hashes[3]);
    m.Set(fixture_dependency+0x30,types); m.Set(fixture_dependency+0x38,resource);
    m.Set(types+8+46*2,descriptor); m.Set(resource+8,base+0x2C25C50);
    m.Set(base+0x2C25C50+(393-1)*12,recipe);
    m.Set(base+0x2C21420,std::array<std::uint64_t,3>{parts,package,2});
    m.Set(engine+0x1D8,info); m.Set(engine+0x1E0,cache); m.Set(engine+0x1E8,refs);
    const std::uint16_t ids[]{6,24,52,39};
    for(unsigned i=0;i<4;++i) { m.Set(cache+i*0x38+0x34,std::uint8_t(3)); m.Set(info+i*0x90+0x5E,ids[i]); }
    std::array<std::uint8_t,0x90> ib{}; std::uint16_t id=46;
    std::memcpy(ib.data()+0x5E,&id,sizeof(id)); ib[0x78]=4; ib[0x7A]=157;
    std::array<std::uint8_t,0x38> cb{}; cb[0x34]=3;
    m.Set(info+4*0x90,ib); m.Set(cache+4*0x38,cb); m.Set(refs+4*4,std::uint32_t(0));
    m.Set(queue,std::uint32_t(2)); m.Set(queue+4,std::uint32_t(16)); m.Set(queue+8,queueData);
    m.Set(queueData,std::uint64_t(100)); m.Set(queueData+8,std::uint64_t(200));
    return m;
}
template<class Edit> void Refuse(Edit edit,const char* reason) {
    auto m=Ready(); edit(m); Snapshot s;
    const auto* actual=Capture(base,0x3000000,m,s);
    Check(actual && std::strcmp(actual,reason)==0);
}
}
int main() {
    auto m=Ready(); Snapshot before,after;
    Check(Capture(base,0x3000000,m,before)==nullptr && before.queue_count==2 && before.cache_refs==0);
    Check(before.seen_hashes==0 && before.cache_bytes[0x34]==3 && before.info_bytes[0x7A]==157);
    Check(Verify(base,0x3000000,m,before,after)!=nullptr);
    m.Append(); Check(Verify(base,0x3000000,m,before,after)==nullptr && after.queue_count==6 && after.seen_hashes==15);
    Check(Verify(base,0x3000000,m,before,after)==nullptr);
    Snapshot ignored; Check(std::strcmp(Capture(base,0x2000000,m,ignored),"image_bounds")==0);
    Refuse([](Memory& x){x.unreadable=base+0x2A82C30;},"player_slot_read");
    Refuse([](Memory& x){x.Set(base+0x2A82C30,std::uintptr_t(1));},"player_not_null");
    Refuse([](Memory& x){x.Set(manager+0xF0,std::uintptr_t(0));},"mission_queue");
    Refuse([](Memory& x){x.Set(manager+0x60,std::uintptr_t(0));},"allocated_groups");
    Refuse([](Memory& x){x.Set(manager+0x48,std::uintptr_t(0));},"allocated_groups");
    Refuse([](Memory& x){x.Set(manager+0x50,std::uintptr_t(0));},"allocated_groups");
    Refuse([](Memory& x){x.Set(manager+0x58,std::uintptr_t(0));},"allocated_groups");
    Refuse([](Memory& x){x.Set(engine,base+0x234B841);},"engine_contract");
    Refuse([](Memory& x){x.Set(base+0x22AD180+0x10,base+0xA2A521);},"parameter_services");
    Refuse([](Memory& x){x.Set(base+0x2A70920+8+46*2,std::uint16_t(235*64+4));},"stock46_bank");
    Refuse([](Memory& x){x.Set(base+0x2C21420+8,std::uint64_t(0));},"stock46_bank");
    Refuse([](Memory& x){x.Set(base+0x2C25C50+(393-1)*12,std::uint8_t(156));},"stock46_bank");
    Refuse([](Memory& x){x.Set(fixture_dependency+0x30,std::uintptr_t(0));},"dependency_owner");
    Refuse([](Memory& x){x.Set(base+0x2349348+64*16,std::uint64_t(0));},"dependency_hashes");
    Refuse([](Memory& x){x.Set(engine+0x1E0,std::uintptr_t(0));},"cache_arrays");
    Refuse([](Memory& x){x.Set(info+0x5E,std::uint16_t(46));},"earlier_stock_match");
    Refuse([](Memory& x){x.Set(cache+4*0x38+0x34,std::uint8_t(1));},"cache4_not_protected_stock46");
    Refuse([](Memory& x){x.Set(refs+4*4,std::uint32_t(1));},"cache4_not_protected_stock46");
    Refuse([](Memory& x){x.Set(info+4*0x90+0x7A,std::uint8_t(156));},"cache4_not_protected_stock46");
    Refuse([](Memory& x){x.Set(queue+4,std::uint32_t(4));},"queue_bounds_or_room");
    Refuse([](Memory& x){x.Set(queue+4,std::uint32_t(5));},"queue_bounds_or_room");
    Refuse([](Memory& x){x.Set(queue+4,std::uint32_t(4097));},"queue_bounds_or_room");
    Refuse([](Memory& x){x.Set(queue,std::uint32_t(17));},"queue_bounds_or_room");
    Refuse([](Memory& x){x.unreadable=queueData;},"queue_hash_read");
    m=Ready(); Check(Capture(base,0x3000000,m,before)==nullptr); m.Append();
    m.Set(queueData,std::uint64_t(101));
    Check(std::strcmp(Verify(base,0x3000000,m,before,after),"old_queue_prefix_changed")==0);
    m=Ready(); Check(Capture(base,0x3000000,m,before)==nullptr); m.Append();
    m.Set(info+4*0x90+0x10,std::uint8_t(1));
    Check(std::strcmp(Verify(base,0x3000000,m,before,after),"cache4_changed")==0);
    m=Ready(); Check(Capture(base,0x3000000,m,before)==nullptr); m.Append();
    m.Set(queue+4,std::uint32_t(17));
    Check(std::strcmp(Verify(base,0x3000000,m,before,after),"owner_or_queue_changed")==0);
    std::printf("equipment_stock_contract_tests: %u checks passed; game_reads=0 native_calls=0\n",checks);
}
