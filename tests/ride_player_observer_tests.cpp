// SCRATCH ONLY. Coordinator may compile this pure fixture in its audited build.
// It proves parser refusal and read-only behavior; it does not prove a live ride.
#include "mgscoop/ride_player_observer.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>
using namespace mgscoop::ride_player_observer;
struct Fake {
    static constexpr Address image=0x140000000ULL, P=0x1100000, Q=0x1200000,
        source=0x1300000, bitset=0x1400000, bits=0x1410000, ids=0x1500000,
        pool=0x1510000, parts=0x1520000, blocks=0x1600000,
        horse=0x1800000, node=0x1900000, occupied=0x1910000,
        records=0x2000000, motion=0x2100000, unit=0x2200000, local=0x2300000;
    std::map<Address,std::uint8_t> bytes;
    Address race_at{}, fail_at{}; unsigned race_reads{};
    template<class T> void put(Address at, T value) {
        std::uint8_t data[sizeof(T)]{}; std::memcpy(data,&value,sizeof(T));
        for(std::size_t i=0;i<sizeof(T);++i)bytes[at+i]=data[i];
    }
    void ptr(Address at, Address value){put(at,value);}
    void u32(Address at,std::uint32_t value){put(at,value);}
    void u16(Address at,std::uint16_t value){put(at,value);}
    void u8(Address at,std::uint8_t value){put(at,value);}
    Fake(){
        ptr(image+0x2a82c30,P); ptr(P,image+0x2295490);u32(P+0x204,1);
        ptr(P+0xa8,0);u32(P+0x208,2);u32(P+0x214,0);u32(P+0x218,1);
        ptr(P+0x60,Q);ptr(Q,image+0x23ad970);ptr(Q+0x1e8,source);u32(source+0x40,2);
        u32(Q+0x170,3);ptr(P+0x380,bitset);ptr(bitset,image+0x23067c0);
        ptr(bitset+0x10,bits);u32(bits,3);ptr(P+0x70,ids);u16(ids,0);u16(ids+2,1);
        ptr(P+0x78,pool);u8(pool,0);u8(pool+1,1);ptr(P+0x90,parts);u8(parts,3);u8(parts+1,3);
        u32(P+0x210,2);ptr(P+0x378,blocks);
        for(unsigned i=0;i<2;++i){Address b=blocks+i*0x6a8;ptr(b,image+0x2295210);ptr(b+0x138,P);u32(b+0xc,i);u32(b+8,1);}
        ptr(horse,image+0x23cb600);ptr(horse+0x10,image+0x23cb680);ptr(horse+0x30,image+0x23cb6b0);
        ptr(horse+8,blocks+0x6a8);ptr(horse+0x38,node);u32(node+0x24,1);u32(node+0x20,1);
        ptr(blocks+0x6a8+0x2b8,node);ptr(blocks+0x6a8+0x4e0,horse+0x30);
        ptr(blocks+0x6a8+0x110,horse);
        ptr(node+8,occupied);u32(occupied,1);ptr(horse+0x68,records);
        u8(records+0x10,0);u8(records+0x11,1);u16(records+0x8c,0xffff);u16(records+0x8e,0x1a01);
        u32(records+0x2a0,0);ptr(records+0x18,0);ptr(Q+0x1d8,motion);
        u8(motion+0x40+8,0x0b);u16(motion+0x40+0x16,0x1a00);ptr(P+0x238,local);
    }
    static bool copy(void* ctx,Address at,void* out,std::size_t n)noexcept{
        auto& f=*static_cast<Fake*>(ctx);
        if(f.fail_at>=at && f.fail_at-at<n)return false;
        if(at==f.race_at && ++f.race_reads==2){std::memset(out,0,n);return true;}
        auto* dest=static_cast<std::uint8_t*>(out);
        for(std::size_t i=0;i<n;++i){auto p=f.bytes.find(at+i);if(p==f.bytes.end())return false;dest[i]=p->second;}
        return true;
    }
    Memory memory(){return {this,copy};}
    Scope scope(){return {image,P,30010,7,true,true,true};}
};
unsigned checks=0;
void require(bool v){++checks;if(!v){std::cerr << "ride observer check failed at " << checks << "\n";std::abort();}}
template<class Edit> void refused(const char* why,Edit edit){
    Fake f;auto s=f.scope();Address p=Fake::horse;unsigned id=1;edit(f,s,p,id);
    const auto before=f.bytes;Sample out;const char* r=ReadHorse(f.memory(),s,p,id,out);
    require(r && std::string(r)==why);require(!out.current && !out.mounted && !out.permit_action);require(before==f.bytes);
}
template<class Edit> void resolver_refused(const char* why,Edit edit){
    Fake f;auto s=f.scope();unsigned id=1;edit(f,s,id);
    const auto before=f.bytes;Sample out;const char* r=ReadHorseAtInstance(f.memory(),s,id,out);
    require(r && std::string(r)==why);require(!out.current && !out.mounted && !out.permit_action);require(before==f.bytes);
}
int main(){
    Fake f;Sample out;const auto before=f.bytes;
    require(!ReadHorse(f.memory(),f.scope(),Fake::horse,1,out));
    require(out.current && out.id==1 && out.block==Fake::blocks+0x6a8 && out.record==Fake::records);
    require(out.selected_target==0x1a01 && out.replication_target==0x1a00);
    require(!out.mounted && !out.permit_action && !out.unit_matches_player);require(before==f.bytes);
    require(!ReadHorseAtInstance(f.memory(),f.scope(),1,out));
    require(out.current && out.primary==Fake::horse && out.node==Fake::node && out.selected_target==0x1a01);
    require(!out.mounted && !out.permit_action && before==f.bytes);
    f.ptr(Fake::records+0x18,Fake::unit);f.u16(Fake::unit+0x78,1);f.u16(Fake::unit+0x7c,1);
    f.u8(Fake::unit+0x80,1);f.ptr(Fake::unit+0x60,Fake::local);
    require(!ReadHorse(f.memory(),f.scope(),Fake::horse,1,out));
    require(out.unit_matches_player && !out.mounted && !out.permit_action);
    // Even a genuine-looking rider unit and target do not authorize ACTION.
    for(unsigned target=0;target<65536;target+=257){
        f.u16(Fake::records+0x8e,static_cast<std::uint16_t>(target));
        require(!ReadHorse(f.memory(),f.scope(),Fake::horse,1,out));require(!out.mounted && !out.permit_action);
    }
#define R(why, code) refused(why,[](Fake& f,Scope& s,Address& p,unsigned& id){(void)f;(void)s;(void)p;(void)id;code;})
    R("scope",s.exact_contract=false);R("scope",s.paired_scene=false);R("scope",s.owner_thread=false);
    R("scope",s.epoch=0);R("scope",s.mission=0);R("scope",id=2);
    R("range",p=0);R("range",p=0xfffffffffffffff0ULL);R("range",s.player=0);
    R("player",f.ptr(Fake::image+0x2a82c30,Fake::P+8));R("player",f.ptr(Fake::P,Fake::image+0x22953d0));
    R("player_state",f.u32(Fake::P+0x204,0));R("player_state",f.u32(Fake::P+0x204,0x101));
    R("player_state",f.u32(Fake::P+0x204,0x40001));R("player_state",f.ptr(Fake::P+0xa8,Fake::unit));
    R("player_state",f.u32(Fake::P+0x208,3));R("player_state",f.u32(Fake::P+0x214,1));
    R("player_state",f.u32(Fake::P+0x218,2));R("player_state",f.ptr(Fake::Q,Fake::image+0x23ad900));
    R("player_state",f.u32(Fake::source+0x40,1));R("unrealized",f.u32(Fake::Q+0x170,1));
    R("unrealized",f.ptr(Fake::bitset,Fake::image+0x2306730));R("unrealized",f.u32(Fake::bits,1));
    R("slot",f.u16(Fake::ids+2,0));R("slot",f.u8(Fake::pool+1,2));R("slot",f.u8(Fake::parts+1,2));
    R("slot",f.u32(Fake::P+0x210,3));R("block",f.ptr(Fake::blocks+0x6a8,Fake::image+0x2295000));
    R("block",f.ptr(Fake::blocks+0x6a8+0x138,Fake::P+8));R("block",f.u32(Fake::blocks+0x6a8+0xc,2));
    // All base/destructor vtables are refused, even when the methods overlap.
    R("controller",f.ptr(Fake::horse,Fake::image+0x23cb4f0));
    R("controller",f.ptr(Fake::horse+0x10,Fake::image+0x23cb570));
    R("controller",f.ptr(Fake::horse+0x30,Fake::image+0x23cb5a0));
    R("controller",f.ptr(Fake::horse+8,Fake::blocks));R("controller",f.u32(Fake::node+0x24,0));
    R("controller",f.u32(Fake::node+0x20,0));R("node",f.u32(Fake::occupied,0));
    R("registration",f.ptr(Fake::blocks+0x6a8+0x2b8,Fake::node+8));
    R("registration",f.ptr(Fake::blocks+0x6a8+0x4e0,Fake::horse+0x10));
    R("registration",f.ptr(Fake::blocks+0x6a8+0x110,Fake::horse+0x30));
    R("node",f.ptr(Fake::horse+0x68,0));R("record",f.fail_at=Fake::records+0x8e);
    R("replication_record",f.ptr(Fake::Q+0x1d8,0));R("changed",f.race_at=Fake::image+0x2a82c30);
    R("changed",f.race_at=Fake::P+0x378);R("changed",f.race_at=Fake::horse+0x38);
    R("changed",f.race_at=Fake::horse+0x68);R("changed",f.race_at=Fake::node+0x20);
    R("changed",f.race_at=Fake::blocks+0x6a8+0x2b8);R("changed",f.race_at=Fake::blocks+0x6a8+0x4e0);
    R("changed",f.race_at=Fake::blocks+0x6a8+0x110);
    R("changed",f.race_at=Fake::pool+1);R("changed",f.race_at=Fake::parts+1);
#undef R
    // The adjacent row 0x27 is not Horse. A plausible decoy there cannot
    // replace a missing/wrong actual row 0x25 registration.
#define RR(why, code) resolver_refused(why,[](Fake& f,Scope& s,unsigned& id){(void)f;(void)s;(void)id;code;})
    RR("scope",s.owner_thread=false);RR("scope",id=2);
    RR("player",f.ptr(Fake::image+0x2a82c30,Fake::P+8));
    RR("resolver",f.ptr(Fake::blocks+0x6a8+0x4e0,0));
    RR("resolver",f.ptr(Fake::blocks+0x6a8+0x4e0,Fake::horse+0x10));
    RR("resolver",f.ptr(Fake::blocks+0x6a8+0x4e0,0);f.ptr(Fake::blocks+0x6a8+0x4f0,Fake::horse+0x30));
    RR("registration",f.ptr(Fake::blocks+0x6a8+0x2b8,0);f.ptr(Fake::blocks+0x6a8+0x2c8,Fake::node));
    RR("controller",f.ptr(Fake::horse+8,Fake::blocks));
    RR("player_state",f.u32(Fake::P+0x204,0x101));
    RR("unrealized",f.u32(Fake::bits,1));
    RR("registration",f.race_at=Fake::blocks+0x6a8+0x4e0);
#undef RR
    // Every missing byte of any required read refuses, without writing memory.
    Fake base;std::vector<Address> fields={Fake::P+0x60,Fake::source+0x40,Fake::bits,Fake::parts+1,
        Fake::horse+0x30,Fake::node+8,Fake::records+0x18,Fake::motion+0x40+0x16};
    for(auto address:fields){Fake bad;bad.fail_at=address;const auto saved=bad.bytes;Sample sample;
        require(ReadHorse(bad.memory(),bad.scope(),Fake::horse,1,sample)!=nullptr);
        require(!sample.current && !sample.permit_action);require(saved==bad.bytes);}
    // A separate local controller/record is parsed under the same exact P.
    Fake local;local.ptr(Fake::horse+8,Fake::blocks);local.u32(Fake::node+0x24,0);
    local.ptr(Fake::blocks+0x2b8,Fake::node);local.ptr(Fake::blocks+0x4e0,Fake::horse+0x30);
    local.ptr(Fake::blocks+0x110,Fake::horse);
    local.u8(Fake::motion+8,0);local.u16(Fake::motion+0x16,0xffff);
    require(!ReadHorse(local.memory(),local.scope(),Fake::horse,0,out));require(out.id==0 && !out.permit_action);
    require(!ReadHorseAtInstance(local.memory(),local.scope(),0,out));require(out.id==0 && !out.permit_action);
    std::cout<<checks<<" scratch read-only fixture checks passed\n";
}
