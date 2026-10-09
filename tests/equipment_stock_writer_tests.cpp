#include "mgscoop/equipment_stock_writer.hpp"
#include "mgscoop/equipment_stock_writer_contract.hpp"
#include <cstdio>
#include <cstdlib>
#include <map>
namespace {
namespace c=mgscoop::equipment_stock_writer_contract;
namespace p=mgscoop::equipment_stock_writer;
unsigned checks=0;
void Check(bool v) {++checks;if(!v){std::fprintf(stderr,"stock writer check %u failed\n",checks);std::exit(1);}}
constexpr std::uintptr_t base=0x140000000ull,P=0x10000000,iface=0x10001000,inv=0x10002000;
constexpr std::uintptr_t handles=0x10003000,metadata=0x10004000,vars=0x10005000,engine=0x10006000;
constexpr std::uintptr_t fixture_blocks=0x10007000,fixture_buffer=0x10008000,fixture_ids=0x10009000,fixture_pools=0x1000A000,fixture_parts=0x1000B000;
constexpr std::uintptr_t manager=0x1000C000,mission_group=0x1000D000,entry=0x1000E000,files=0x1000F000,file_ptrs=0x10010000;
constexpr std::uintptr_t info=0x10011000,cache=0x10014000,refs=0x10015000,ring=0x10016000,meta=0x10017000,rows=0x10018000;
constexpr std::uintptr_t map=0x10019000,map_rows=0x1001A000,fixture_map_flags=0x1001B000,fixture_resource_rows=0x1001C000,fixture_free_queue=0x1001D000;
constexpr std::uintptr_t fixture_groups=0x1001E000;
struct Memory {
    std::map<std::uintptr_t,std::uint8_t> bytes;
    std::uintptr_t unreadable=0;
    template<class T> void Set(std::uintptr_t at,const T& v) {
        const auto* source=reinterpret_cast<const std::uint8_t*>(&v);
        for(std::size_t i=0;i<sizeof(T);++i) bytes[at+i]=source[i];
    }
    bool operator()(std::uintptr_t at,void* out,std::size_t size) {
        if(unreadable && unreadable>=at && unreadable-at<size) return false;
        auto* dst=static_cast<std::uint8_t*>(out);
        for(std::size_t i=0;i<size;++i) {auto it=bytes.find(at+i);if(it==bytes.end())return false;dst[i]=it->second;}
        return true;
    }
    void Method(std::uintptr_t obj,std::size_t vt,std::size_t slot,std::size_t fn) {Set(obj,base+vt);Set(base+vt+slot,base+fn);}
};
Memory Ready() {
    Memory m;
    m.Set(base+0x2A82C30,P);m.Method(P,0x2295490,0x28,0x9CCB40);
    m.Set(P+0x208,std::uint32_t(2));m.Set(P+0x214,std::uint32_t(0));m.Set(P+0x218,std::uint32_t(0));
    m.Set(P+0x204,std::uint32_t(0x1A487));m.Set(P+0xA8,std::uintptr_t(0));m.Set(base+0x2BD3FD4,std::uint32_t(0));
    m.Set(P+0x70,fixture_ids);m.Set(fixture_ids,std::array<std::uint16_t,2>{0,1});
    m.Set(P+0x78,fixture_pools);m.Set(fixture_pools,std::array<std::uint8_t,2>{0,1});
    m.Set(P+0x90,fixture_parts);m.Set(fixture_parts,std::array<std::uint8_t,2>{3,3});m.Set(P+0x50,fixture_blocks);m.Set(P+0x378,fixture_buffer);
    m.Set(P+0xC8,iface);m.Method(iface,0x23C1720,0x198,0x11AC2E0);m.Set(iface+0x18,P);m.Set(iface+8,inv);
    m.Method(iface,0x23C1720,0x18,0x11B0900);m.Method(iface,0x23C1720,0x20,0x11B2490);m.Method(iface,0x23C1720,0xA0,0x11B0700);
    m.Method(inv,0x23895D0,8,0xEE9880);m.Method(inv,0x23895D0,0x40,0xEE9810);
    m.Set(inv+8,std::uint32_t(2));m.Set(inv+0xC,std::uint32_t(7));m.Set(inv+0x10,std::uint32_t(14));m.Set(inv+0x30,std::uint32_t(0));
    m.Set(inv+0x40,handles);m.Set(inv+0x50,metadata);m.Set(inv+0x58,engine);
    m.Method(engine,0x234B840,0,0xDC3250);m.Method(engine,0x234B840,0x10,0xDC2A50);
    m.Set(base+0x2C3A8A0,std::uintptr_t(0x10020000));m.Set(0x10020008,std::uintptr_t(0x10021000));
    m.Set(0x10021098,std::uintptr_t(0x10022000));m.Set(0x100221A0,std::uintptr_t(0x10023000));
    m.Set(0x100221E8,engine);m.Method(0x10023000,0x25564F0,0x208,0x1E02AD0);m.Method(0x10023000,0x25564F0,0x1F8,0x1E02B40);
    m.Set(handles,std::array<std::uint32_t,14>{0x6CE1,0x16D24,0x26D62});
    m.Set(metadata,std::array<std::uint8_t,84>{30,0,0,0,30,18,8,0,0,0,15,18,10,0,0,0,255,18});
    m.Set(base+0x2C16920,vars);m.Set(vars+0xB06,std::array<std::uint16_t,3>{664,794,574});
    m.Set(vars+0xB30,std::array<std::uint8_t,36>{69,38,57,14,4,25,4,3,4,0,3,0,157,86,110,37,18,34,17,0,0,9,2,0});
    for(unsigned i=0;i<2;++i) {
        const std::uintptr_t body=fixture_buffer+i*0x6A8,pr=0x11000000+i*0x1000,wr=pr+0x200,model=pr+0x300,br=pr+0x400;
        m.Set(fixture_blocks+i*8,body);m.Set(body,base+0x2295210);m.Set(body+8,std::uint32_t(1));m.Set(body+0xC,std::uint32_t(i));m.Set(body+0x138,P);
        m.Method(body,0x2295210,0x128,0x9C5980);m.Method(body,0x2295210,0x130,0x9CC6F0);
        m.Set(body+0x70,pr);m.Method(pr,0x23BC1B0,0x30,0x1156640);m.Set(pr+0x168,body);m.Set(pr+0x1A8,pr+0x900);m.Set(pr+0x900,std::uint8_t(0));
        m.Set(body+0xF0,wr);m.Method(wr,0x23C95D8,0x28,0x1209EC0);m.Set(wr+8,body);
        m.Set(body+0x618,model);m.Method(model,0x22EA550,0x20,0xB0C720);m.Method(model,0x22EA550,0x18,0xB0CF80);
        m.Method(model,0x22EA550,0x28,0xB0C800);
        m.Set(model+0x58,std::uint32_t(1));m.Set(model+0x5C,std::uint32_t(i));m.Set(model+0x40,P);m.Set(model+0x50,engine);
        m.Set(model+0x80,std::uint16_t(40));m.Set(model+0x30,br);
        const std::uintptr_t am=0x13000000+i*0x10000,aa=am+0x1000,ai=am+0x2000;
        m.Set(model+0x78,am);m.Method(am,0x2215C78,0,0x7C74F0);
        m.Set(am+0x10,std::uint32_t(40));m.Set(am+8,aa);
        std::array<std::uintptr_t,40> items{};
        for(unsigned j=0;j<40;++j) {items[j]=ai+j*0x20;m.Set(items[j],std::array<std::uint8_t,0x20>{});}
        m.Set(aa,items);
        m.Set(br,std::array<std::uint32_t,9>{i?0xFFFFFFE0u:0x6CE1u,i?0u:0xFFFFFFE0u,0,
            i?0xFFFFFFE0u:0x16D24u,i?0u:0xFFFFFFE0u,0,i?0xFFFFFFE0u:0x26D62u,i?0u:0xFFFFFFE0u,0});
        if(i) {
            m.Set(body+0x628,pr+0xA00);m.Set(pr+0xA50,std::array<std::uint8_t,0x50>{});
            m.Set(body+0x10,pr+0xB00);m.Method(pr+0xB00,0x22E6070,0x400,0xAF0A00);
            m.Set(pr+0xB08,std::uint32_t(1));m.Set(pr+0xB0C,std::uint32_t(1));m.Set(pr+0xB10,pr+0xC00);m.Set(pr+0xC70,std::uintptr_t(0xABC));
        }
    }
    m.Set(base+0x2C1E3B0,manager);m.Set(manager+0x40,mission_group);m.Set(mission_group+0x1C,std::uint32_t(1));m.Set(mission_group+0x48,entry);
    m.Set(entry+0x70,mission_group);m.Set(entry+0x80,std::uint32_t(3));m.Set(entry+0x84,std::uint32_t(10));m.Set(entry+0x150,std::uint32_t(4));
    m.Set(entry+0x90,file_ptrs);m.Set(entry+0x98,files);
    for(unsigned i=0;i<4;++i) {
        auto f=files+i*0x120;m.Set(file_ptrs+i*8,f);m.Set(f+0x10,entry);m.Set(f+8,mgscoop::equipment_stock_contract::hashes[i]);
        m.Method(f,0x20C1CE0,0x90,0xDDF60);m.Method(f,0x20C1CE0,0x80,0xDD830);m.Method(f,0x20C1CE0,0x88,0xDD810);
        m.Method(f,0x20C1CE0,0x38,0xDDA00);
        m.Set(f+0x18,std::uint32_t(0));m.Set(f+0x1C,std::uint8_t(2));m.Set(f+0x20,std::uint32_t(7));m.Set(f+0x110,std::uintptr_t(0x50000000+i*0x10));m.Set(f+0x118,std::uint32_t(i?1:2));
        const std::uintptr_t header=0x14000000+i*0x1000,n1=header+0x100;
        const unsigned named_count=i?1u:32u;
        m.Set(f+0x28,header);m.Set(f+0x30,std::uint32_t(named_count));m.Set(header,i?n1:n1+0x40);m.Set(header+8,n1);
        m.Set(header+0x10,n1+(named_count-1)*0x40);m.Set(header+0x19,std::uint8_t(1));
        for(unsigned j=0;j<named_count;++j) {
            const auto node=n1+j*0x40;
            m.Set(node,(!i && !j)?n1+0x40:header);
            m.Set(node+8,j?((!i && j==2)?n1:node-0x40):header);
            m.Set(node+0x10,(!i && j==1)?header:(j+1<named_count?node+((!i && !j)?0x80:0x40):header));
            const auto hash=i?std::uint64_t(10):(j==1?c::optional46:mgscoop::equipment_stock_contract::parts+(j?j-1:0));
            m.Set(node+0x19,std::uint8_t(0));m.Set(node+0x20,hash);
            m.Set(node+0x28,std::uintptr_t(!i && j==1?0x16100000:0x16000000+i*0x100+j*8));
        }
    }
    m.Set(0x16000030,std::uint64_t(mgscoop::equipment_stock_contract::parts));
    m.Set(0x16000070,std::uintptr_t(0x17000000));
    m.Method(0x16100000,0x24C2890,0x48,0x1A75340);m.Set(0x16100030,c::optional46);
    m.Set(0x16100040,std::uint8_t(2));m.Set(0x16100044,std::uint32_t(0));m.Set(0x16100080,std::uint32_t(0));
    m.Set(0x16100060,std::uintptr_t(0x16200000));m.Set(0x16100068,std::uint64_t(1824));
    m.Set(0x16100098,std::uintptr_t(0x16200000));m.Set(0x16100084,std::uint32_t(3));
    std::array<std::uint8_t,0x20> oh{};const std::uint32_t magic=0x0C012B72,parameters=3;
    const std::array<std::uint16_t,6> shape{4,6,0,0,0,0};
    std::memcpy(oh.data(),&magic,4);std::memcpy(oh.data()+4,&parameters,4);std::memcpy(oh.data()+8,shape.data(),12);
    m.Set(0x16200000,oh);m.Set(base+0x2C71680,std::uint32_t(5));
    m.Set(engine+0x1D8,info);m.Set(engine+0x1E0,cache);m.Set(engine+0x1E8,refs);
    for(unsigned i=0;i<4;++i) {m.Set(info+i*0x90+0x5E,std::uint16_t(6+i));m.Set(cache+i*0x38+0x34,std::uint8_t(3));}
    std::array<std::uint8_t,0x90> ib{};ib[0x5E]=46;ib[0x78]=4;ib[0x7A]=157;m.Set(info+4*0x90,ib);
    std::array<std::uint8_t,0x38> cb{};cb[0x34]=3;m.Set(cache+4*0x38,cb);m.Set(refs+4*4,std::uint32_t(0));
    m.Set(engine+8,base+0x2A70920);m.Method(base+0x2A70920,0x22AD180,0,0xA2A3E0);m.Set(base+0x2A70920+8+46*2,std::uint16_t(0x6244));
    m.Set(engine+0x10,base+0x2A711F0);m.Set(base+0x2A711F0+8,base+0x2C25C50);
    m.Set(base+0x2C25C50+(393-1)*12,mgscoop::equipment_stock_contract::recipe);
    m.Set(base+0x2C21420,std::array<std::uint64_t,3>{mgscoop::equipment_stock_contract::parts,mgscoop::equipment_stock_contract::package,2});
    m.Set(engine+0x100,std::uint32_t(60));m.Set(engine+0x48,ring);m.Set(engine+0x50,std::uint32_t(60));m.Set(engine+0x54,std::uint32_t(6));m.Set(ring+6*4,std::uint32_t(6));
    m.Set(engine+0x120,meta);m.Method(meta,0x234A178,0,0xDB72A0);m.Set(meta+0x58,std::uint32_t(60));m.Set(meta+0x28,rows);
    for(unsigned i=0;i<60;++i) {c::Bytes14 free_row{};free_row[0]=255;free_row[1]=7;m.Set(rows+i*0xE,free_row);}
    for(unsigned i=0;i<3;++i) {c::Bytes14 row{};row[0]=std::uint8_t(0x67+i*2);row[1]=3;m.Set(rows+i*0xE,row);}
    for(unsigned i=3;i<6;++i) {c::Bytes14 extra{};extra[0]=std::uint8_t(100+i);m.Set(rows+i*0xE,extra);}
    m.Set(engine+0x128,map);m.Method(map,0x234A3E8,0,0xDB7590);m.Set(map+8,std::uint32_t(60));m.Set(map+0xC,std::uint32_t(60));
    m.Set(map+0x68,meta);m.Set(map+0x18,map_rows);m.Set(map+0x20,fixture_map_flags);m.Set(map+0x10,fixture_resource_rows);m.Set(map+0x28,fixture_free_queue);
    m.Set(map+0x30,std::uint32_t(60));m.Set(map+0x34,std::uint32_t(6));
    std::array<std::uint32_t,60> maps{},owners{},queue{};maps.fill(UINT32_MAX);owners.fill(UINT32_MAX);
    for(unsigned i=0;i<6;++i) {maps[i]=i*2;owners[i*2]=i;queue[i]=i*2;}
    unsigned q=6;queue[q++]=12;
    for(unsigned resource=0;resource<60;++resource) if(owners[resource]==UINT32_MAX && resource!=12) queue[q++]=resource;
    m.Set(map_rows,maps);m.Set(fixture_resource_rows,owners);m.Set(fixture_free_queue,queue);
    m.Set(fixture_map_flags,std::array<std::uint8_t,60>{});
    m.Set(engine+0x110,std::uint32_t(32));m.Set(engine+0x130,fixture_groups);m.Set(engine+0x138,std::uintptr_t(0x1001F000));
    m.Set(map+0x70,engine+0x40);m.Method(engine+0x40,0x234BB48,0,0xDBF370);
    for(unsigned i=0;i<32;++i) {
        auto g=0x12000000+i*0x10000;m.Set(fixture_groups+i*8,std::uintptr_t(g));m.Set(g,base+0x23DB5C0);m.Set(g+0x30,std::uint32_t(i<28?2:1));
        m.Set(0x1001F000+i*4,std::uint32_t(i<28?2:1));
        m.Set(g+0x34,std::uint32_t(i<28?i*2:56+i-28));m.Set(g+0x38,meta);
        m.Set(g+0x68,base+0x2A6D040);m.Set(g+0x70,base+0x2A6B400);
        m.Set(g+0x58,base+0x2A71328);
        for(unsigned j=0;j<2;++j) {m.Set(g+0x10+j*8,std::uintptr_t(g+0x100+j*0x100));m.Method(g+0x100+j*0x100,0x22E2970,0x240,0xADBE00);m.Set(g+0x150+j*0x100,std::uint32_t((i<28?2:1)*(j+1)));
            m.Method(g+0x100+j*0x100,0x22E2970,0x10,0xADDE80);m.Method(g+0x100+j*0x100,0x22E2970,0x1A0,0xADDDB0);
            const auto values=g+(j?0x6000:0x500);
            m.Set(g+0x158+j*0x100,std::uintptr_t(values));m.Set(values,std::array<std::uintptr_t,4>{});
            const auto b=g+0x100+j*0x100, ca=g+(j?0x6100:0x700);
            m.Set(b+0x28,std::uintptr_t(values));m.Set(b+0x38,std::uint64_t(j?0x400:0x4BC8));
            m.Set(b+0x40,std::uint64_t(j?0x400:0x4BC8));m.Set(b+0x4A,std::uint16_t(1));
            m.Set(b+8,std::uint32_t(12));m.Set(b+0x88,std::uintptr_t(ca));m.Set(ca,std::array<std::uint8_t,0x120>{});
            m.Set(b+0x90,std::uintptr_t(ca+0x120));m.Set(ca+0x120,std::array<std::uint64_t,12>{});
            m.Set(b+0x98,std::uintptr_t(ca+0x180));m.Set(ca+0x180,std::array<std::uint16_t,12>{});
            m.Set(b+0xA0,std::uintptr_t(ca+0x198));m.Set(ca+0x198,std::array<std::uintptr_t,12>{});
            m.Set(b+0x10,std::uintptr_t(ca+0x200));m.Set(ca+0x200,std::array<std::uint8_t,4>{});
            m.Set(b+0xD0,std::uintptr_t(ca+0x210));m.Set(ca+0x210,std::array<std::uint8_t,4>{});
            m.Set(b+0x60,std::uintptr_t(ca+0x220));m.Set(ca+0x220,std::array<std::uintptr_t,4>{});
            const auto N=i<28?2u:1u;
            m.Set(b+0x68,std::uintptr_t(j?0:g+0xA00));m.Set(b+0x70,std::uint32_t(2048));
            m.Set(b+0x78,std::uintptr_t(j?0:g+0x1A00));m.Set(b+0xD8,std::uintptr_t(j?0:base+0x1306FF0));
            m.Set(b+0xE0,std::uintptr_t(j?0:g-0x20));m.Set(b+0xE8,std::uintptr_t(j?0:g+0x1A40));
            if(!j) {m.Set(g+0x1A00,std::array<std::uintptr_t,2>{});
                for(unsigned target=0;target<N;++target) {
                    std::array<std::uint8_t,0x50> extra{};const auto vt=base+0x22E2950;const std::uint32_t one=1;
                    std::memcpy(extra.data(),&vt,8);std::memcpy(extra.data()+0x14,&one,4);
                    const std::uintptr_t owner=b;
                    std::memcpy(extra.data()+0x40,&owner,8);std::memcpy(extra.data()+0x4C,&target,4);
                    m.Set(g+0x1A40+target*0x50,extra);
                }
            }
        }
    }
    m.Method(base+0x2A6D040,0x22A26E0,0,0xA047D0);
    m.Set(base+0x2A6D048,std::uintptr_t(0x15000000));m.Set(base+0x2A6D050,std::uintptr_t(0x15000000));
    m.Set(base+0x2A6D058,std::uint64_t(0x90));m.Set(base+0x2A6D060,std::uint64_t(0x90));
    m.Set(base+0x2A6D070,std::uintptr_t(0x15000030));m.Set(0x15000038,std::uint32_t(0));
    m.Method(0x15000030,0x22A2660,8,0xA06440);m.Method(0x15000030,0x22A2660,0x28,0xA05B40);
    m.Method(0x15000030,0x22A2660,0x38,0xA057C0);
    m.Method(base+0x2A6B400,0x22A0F18,0,0x9FFE10);m.Set(base+0x2A6B400+8+46*8,std::uint64_t(c::optional46));
    m.Method(base+0x2A71328,0x22B89D8,0x28,0xACCC10);
    return m;
}
// The generalized writer (docs/native-puppet.md "Generalized writer"): the
// whole census readable, the dependency tables and hashes stock46's recipe
// [157,85,110,36,..,lane10 0] reaches (LUT [35,38,9,0,0] -> ids [36,55,64]).
void General(Memory& m) {
    for(unsigned i=0;i<48;++i) {
        if(i>=5) {m.Set(info+i*0x90,std::array<std::uint8_t,0x90>{});m.Set(cache+i*0x38,std::array<std::uint8_t,0x38>{});}
        if(i<4) {m.Set(info+i*0x90+0x78,std::uint8_t(2));m.Set(info+i*0x90+0x7A,std::uint8_t(69));}
        if(i!=4) m.Set(refs+i*4,std::uint32_t(i<4 ? 1 : 0));
    }
    m.Set(base+0x2349A90+157,std::uint8_t(35));m.Set(base+0x2349B80+85,std::uint8_t(38));m.Set(base+0x2349C00+110,std::uint8_t(9));
    m.Set(base+0x2349CC0+36,std::uint8_t(0));m.Set(base+0x2349A78+0,std::uint8_t(0));
    m.Set(base+0x2349980+35*2,std::array<std::uint8_t,2>{36,0});m.Set(base+0x23499E0+38,std::uint8_t(55));
    m.Set(base+0x2349A10+9*2,std::array<std::uint8_t,2>{64,0});m.Set(base+0x2349A08+0,std::uint8_t(0));m.Set(base+0x2349A68+0,std::uint8_t(0));
    m.Set(base+0x2349348+36*16,mgscoop::equipment_stock_contract::hashes[1]);
    m.Set(base+0x2349348+55*16,mgscoop::equipment_stock_contract::hashes[2]);
    m.Set(base+0x2349348+64*16,mgscoop::equipment_stock_contract::hashes[3]);
    // The slot-0 and slot-2 effect records of body 1 (zero, like slot 1's).
    m.Set(0x11001000+0xA00,std::array<std::uint8_t,0x50>{});m.Set(0x11001000+0xAA0,std::array<std::uint8_t,0x50>{});
}
// A plain (no optional asset) hip gun sharing stock46's packages: id 300,
// type 1 (slot 0), GunBasic row 393, its own protected cache entry 5.
void Plain300(Memory& m,std::uint8_t flags=3,std::uint32_t refs_value=0) {
    General(m);
    m.Set(base+0x2A70920+8+300*2,std::uint16_t((393<<6)|1));
    m.Set(base+c::PackageRowRva(300),std::array<std::uint64_t,3>{mgscoop::equipment_stock_contract::parts,
        mgscoop::equipment_stock_contract::package,2});
    std::array<std::uint8_t,0x90> ib{};const std::uint16_t id=300;std::memcpy(ib.data()+0x5E,&id,2);ib[0x78]=1;ib[0x7A]=157;
    m.Set(info+5*0x90,ib);std::array<std::uint8_t,0x38> cb{};cb[0x34]=flags;m.Set(cache+5*0x38,cb);m.Set(refs+5*4,refs_value);
}
template<class Edit> void Refuse(Edit edit,const char* reason) {
    auto m=Ready();edit(m);c::Snapshot s;auto why=c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,s);Check(why && std::strcmp(why,reason)==0);
}
}
int main() {
    // The request amounts: the qualified writer asks 0/0; only the research
    // ammo experiment (MGS_LOCAL_STOCK46_AMMO_TEST, default OFF) asks 30 rounds / 210 stock.
    Check(p::kQualifiedAmounts.stock==0 && p::kQualifiedAmounts.ammo==0 && p::kAmmoTestAmounts.stock==210 && p::kAmmoTestAmounts.ammo==30);
    Check(p::RequestAmountsFor(false).ammo==0 && p::RequestAmountsFor(true).ammo==30 && !p::kAmmoTest);
    p::Authorization a;p::Context x{};x.enabled=x.owner=x.offline_gates=x.preload_queued=x.body_ready=true;
    x.mission=30010;x.base=base;x.player=P;x.remote_block=fixture_buffer+0x6A8;x.image_span=0xA01D000;
    a.Begin(30010);auto disabled=x;disabled.enabled=false;Check(std::strcmp(a.Try(disabled),"disabled")==0 && !a.Attempted());
    auto wrong=x;wrong.owner=false;Check(std::strcmp(a.Try(wrong),"not_runtime_owner")==0 && !a.Attempted());
    wrong=x;wrong.body_ready=false;Check(std::strcmp(a.Try(wrong),"not_ready")==0 && !a.Attempted());
    wrong=x;wrong.preload_queued=false;Check(std::strcmp(a.Try(wrong),"not_ready")==0 && !a.Attempted());
    wrong=x;wrong.mission=30050;Check(std::strcmp(a.Try(wrong),"allocation_mission")==0);
    wrong=x;wrong.offline_gates=false;Check(std::strcmp(a.Try(wrong),"offline_owner_gates")==0 && a.Attempted());
    Check(std::strcmp(a.Try(x),"already_attempted")==0);a.Begin(30010);Check(a.Try(x)==nullptr && a.Attempted());
    ++x.body_epoch;Check(std::strcmp(a.Try(x),"already_attempted")==0);a.End();Check(std::strcmp(a.Try(x),"not_runtime_owner")==0);
    auto m=Ready();c::Snapshot before,after;Check(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before)==nullptr);
    Check(before.new_index==6 && before.local_handles[1]==0x16D24 && before.cache_refs==0);
    Check(c::Capture(base,0xA01D000,m,P,fixture_buffer+0x6A8,after)==nullptr && c::LocalUnchanged(before,after)==nullptr);
    Refuse([](Memory& q){q.Set(P+0x204,std::uint32_t(0x101));},"player_lease");
    Refuse([](Memory& q){q.Set(P+0xA8,std::uintptr_t(9));},"player_lease");
    Refuse([](Memory& q){q.Set(P+0x214,std::uint32_t(1));},"player_lease");
    Refuse([](Memory& q){q.Set(base+0x2BD3FD4,std::uint32_t(1));},"player_lease");
    Refuse([](Memory& q){q.Set(fixture_pools+1,std::uint8_t(0xFF));},"player_lease");
    Refuse([](Memory& q){q.Set(fixture_parts+1,std::uint8_t(2));},"player_lease");
    Refuse([](Memory& q){q.Set(iface+0x18,std::uintptr_t(0));},"equipment_interface");
    Refuse([](Memory& q){q.Set(inv+0x10,std::uint32_t(13));},"inventory_bounds");
    Refuse([](Memory& q){q.Set(base+0x2C3A8A0,std::uintptr_t(0));},"native_owner_chain");
    Refuse([](Memory& q){q.Set(0x10021098,std::uintptr_t(0));},"native_owner_chain");
    Refuse([](Memory& q){q.Set(0x100221A0,std::uintptr_t(0));},"native_owner_chain");
    Refuse([](Memory& q){q.Set(base+0x25564F0+0x208,base+0x1E02AD1);},"native_owner_chain");
    Refuse([](Memory& q){q.Set(base+0x25564F0+0x1F8,base+0x1E02B41);},"native_owner_chain");
    Refuse([](Memory& q){q.Set(0x100221E8,std::uintptr_t(0));},"native_owner_chain");
    Refuse([](Memory& q){q.Set(fixture_buffer+0x6A8+0x138,std::uintptr_t(1));},"body_equipment_owner");
    Refuse([](Memory& q){q.Set(0x11001000+0xB0C,std::uint32_t(0));},"effect_clear_context");
    Refuse([](Memory& q){q.Set(entry+0x80,std::uint32_t(2));},"mission_not_active");
    Refuse([](Memory& q){q.Set(file_ptrs,std::uintptr_t(files+0x120));},"noncanonical_file");
    Refuse([](Memory& q){q.Set(files+0x18,std::uint32_t(0x82B1280A));},"package_not_ready");
    Refuse([](Memory& q){q.Set(files+0x1C,std::uint8_t(1));},"package_not_ready");
    Refuse([](Memory& q){q.Set(files+0x118,std::uint32_t(0));},"package_not_ready");
    Refuse([](Memory& q){q.Set(files+0x30,std::uint32_t(257));},"asset_tree_bounds");
    Refuse([](Memory& q){q.Set(0x14000148,std::uintptr_t(0x14000000));},"asset_tree_owner");
    Refuse([](Memory& q){q.Set(0x14000150,std::uintptr_t(0x14000100));},"asset_tree_cycle");
    Refuse([](Memory& q){q.Set(0x14000160,std::uint64_t(mgscoop::equipment_stock_contract::parts));},"asset_tree_owner");
    Refuse([](Memory& q){q.Set(0x14000000,std::uintptr_t(0x14000100));},"asset_tree_extrema");
    Refuse([](Memory& q){q.Set(0x14000128,std::uintptr_t(0));},"asset_tree_owner");
    Refuse([](Memory& q){q.Set(0x14000120,std::uint64_t(mgscoop::equipment_stock_contract::parts-1));},"stock_parts_asset_missing");
    Refuse([](Memory& q){q.Set(0x16000030,std::uint64_t(0x1094ull<<0x33));},"stock_parts_identity");
    Refuse([](Memory& q){q.unreadable=0x16000030;},"stock_parts_identity");
    Refuse([](Memory& q){q.Set(0x16000070,std::uintptr_t(0));},"stock_parts_entity");
    Refuse([](Memory& q){q.unreadable=0x16000070;},"stock_parts_entity");
    Refuse([](Memory& q){q.Set(files+8,std::uint64_t(3));},"missing_package");
    Refuse([](Memory& q){q.Set(info+4*0x90+0x7A,std::uint8_t(156));},"stock_cache_identity");
    Refuse([](Memory& q){q.Set(base+0x2A70920+8+46*2,std::uint16_t(0));},"stock_parameter_bank");
    Refuse([](Memory& q){q.Set(meta+0x58,std::uint32_t(59));},"local_engine_bounds");
    Refuse([](Memory& q){q.Set(handles+8*4,std::uint32_t(2));},"remote_not_empty");
    Refuse([](Memory& q){q.Set(0x11001000+0xA50,std::uint64_t(1));},"remote_not_empty");
    Refuse([](Memory& q){q.Set(0x11001000+0x404,std::uint32_t(1));},"remote_binder_not_empty");
    Refuse([](Memory& q){q.Set(refs+16,std::uint32_t(1));},"stock_cache_in_use");
    Refuse([](Memory& q){q.Set(engine+0x54,std::uint32_t(60));},"new_handle_capacity");
    Refuse([](Memory& q){q.Set(ring+6*4,std::uint32_t(60));},"new_handle_capacity");
    Refuse([](Memory& q){q.Set(map_rows+6*4,std::uint32_t(2));},"new_handle_capacity");
    Refuse([](Memory& q){q.Set(rows+6*0xE,std::uint16_t(46));},"new_handle_not_free");
    Refuse([](Memory& q){q.Set(map+0x34,std::uint32_t(60));},"new_resource_not_free");
    Refuse([](Memory& q){q.Set(fixture_resource_rows+12*4,std::uint32_t(2));},"new_resource_not_free");
    Refuse([](Memory& q){q.Set(0x12000000+0x250,std::uint32_t(2));},"resource_bank_capacity");
    Refuse([](Memory& q){q.unreadable=vars+0xB30;},"local_vars");
    Refuse([](Memory& q){q.Set(0x13010010,std::uint32_t(39));},"attachment_manager_bounds");
    Refuse([](Memory& q){q.Set(0x13011000,std::uintptr_t(0));},"attachment_item_missing");
    Refuse([](Memory& q){q.Set(0x13011008,std::uintptr_t(0x13012000));},"attachment_item_alias");
    Refuse([](Memory& q){q.Set(0x13011000,std::uintptr_t(0x13002000));},"attachment_item_local_alias");
    Refuse([](Memory& q){q.Set(0x13012000+13*0x20,std::uintptr_t(0x123));},"remote_attachment_not_empty");
    Refuse([](Memory& q){q.Set(0x12060000+0x34,std::uint32_t(11));},"selected_resource_ownership");
    Refuse([](Memory& q){q.Set(0x12060000+0x38,std::uintptr_t(1));},"selected_resource_ownership");
    Refuse([](Memory& q){q.Set(base+0x22B89D8+0x28,base+0xACCC11);},"selected_resource_ownership");
    Refuse([](Memory& q){q.Set(0x12060500,std::uintptr_t(1));},"selected_bank_not_empty");
    Refuse([](Memory& q){q.Set(0x12066010,std::uintptr_t(1));},"selected_bank_not_empty");
    Refuse([](Memory& q){q.Set(fixture_groups+8,std::uintptr_t(0x12000000));},"resource_group_alias");
    Refuse([](Memory& q){q.Set(0x12010010,std::uintptr_t(0x12000100));},"resource_bank_alias");
    Refuse([](Memory& q){q.Set(0x12060108,std::uint32_t(65));},"selected_cache_bounds");
    Refuse([](Memory& q){q.Set(0x12060108,std::uint32_t(0));},"selected_cache_bounds");
    Refuse([](Memory& q){q.Set(0x12060188,std::uintptr_t(0));},"selected_cache_bounds");
    Refuse([](Memory& q){q.Set(0x12060708,std::uint64_t(1));},"selected_cache_null_entry_dirty");
    Refuse([](Memory& q){q.Set(0x12060820,std::uint64_t(1));},"selected_cache_null_entry_dirty");
    Refuse([](Memory& q){q.Set(0x12060880,std::uint16_t(1));},"selected_cache_null_entry_dirty");
    Refuse([](Memory& q){q.Set(0x12060898,std::uintptr_t(1));},"selected_cache_null_entry_dirty");
    Refuse([](Memory& q){q.Set(0x12060910,std::uint8_t(2));},"selected_bank_aux_not_empty");
    Refuse([](Memory& q){q.Set(0x12060900,std::uint8_t(13));},"selected_bank_aux_not_empty");
    Refuse([](Memory& q){q.Set(0x12060920,std::uintptr_t(1));},"selected_bank_aux_not_empty");
    Refuse([](Memory& q){q.Set(0x12060138,std::uint64_t(1024*1024+1));},"resource_bank_allocation");
    Refuse([](Memory& q){q.Set(0x12060140,std::uint64_t(0x4BC9));},"selected_cache_allocation");
    Refuse([](Memory& q){q.Set(0x1206014A,std::uint16_t(0));},"selected_cache_allocation");
    Refuse([](Memory& q){q.Set(0x12060198,std::uintptr_t(0x120650D0));},"selected_cache_allocation");
    Refuse([](Memory& q){q.Set(0x12060198,std::uintptr_t(0x12060820));},"selected_cache_array_alias");
    Refuse([](Memory& q){q.Set(0x12060138,std::uint64_t(0x6000));},"selected_cache_bank_alias");
    Refuse([](Memory& q){q.Set(0x12000128,std::uintptr_t(0x12060500));},"selected_cache_other_bank_alias");
    Refuse([](Memory& q){q.Set(0x12060700,std::uintptr_t(0x16000000));q.Set(0x12060820,entry);},"selected_cache_stock_match");
    Refuse([](Memory& q){for(unsigned i=0;i<12;++i)q.Set(0x12060700+i*0x18,std::uintptr_t(1));},"selected_cache_no_null_entry");
    Refuse([](Memory& q){q.Set(0x12066100,std::uintptr_t(1));},"selected_cache_not_empty");
    // Paired H4 V9: released different gun cache0 remains intact; native
    // first-NULL choice is1, despite entry0 having refs0. Both target flag1
    // and optional index=count sentinel are valid released bookkeeping.
    m=Ready();m.Set(0x12060700,std::array<std::uint64_t,3>{0x18FC58830ull,0xE0A90D432B25A65Eull,0});
    m.Set(0x12060820,entry);m.Set(0x12060898,std::uintptr_t(0x18FC58320ull));
    m.Set(0x12060910,std::uint8_t(1));m.Set(0x12066300,std::uint8_t(12));m.Set(0x12066310,std::uint8_t(1));
    Check(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before)==nullptr && before.main_cache_candidate==1);
    c::View<Memory> cache_view{m,base,0xA01D000};Check(c::MainCachePreserved(cache_view,before)==nullptr);
    m.Set(0x12060718,std::uintptr_t(0x16000000));Check(c::MainCachePreserved(cache_view,before)==nullptr);
    m.Set(0x12060880,std::uint16_t(1));Check(std::strcmp(c::MainCachePreserved(cache_view,before),"other_main_cache_changed")==0);
    m=Ready();Check(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before)==nullptr);
    Refuse([](Memory& q){q.Set(base+0x2A6D058,std::uint64_t(0x80));},"selected_package_bounds");
    Refuse([](Memory& q){q.Set(base+0x2A6D070,std::uintptr_t(0x15000040));},"selected_package_bounds");
    Refuse([](Memory& q){q.Set(0x15000038,std::uint32_t(1));},"selected_package_bounds");
    Refuse([](Memory& q){q.Set(base+0x22A2660+0x28,base+0xA05B41);},"selected_package_bounds");
    Refuse([](Memory& q){q.Set(base+0x2A6B400+8+46*8,std::uint64_t(0));},"selected_package_bounds");
    m.Set(vars+0xB30,std::uint8_t(70));Check(c::Capture(base,0xA01D000,m,P,fixture_buffer+0x6A8,after)==nullptr);
    Check(std::strcmp(c::LocalUnchanged(before,after),"local_equipment_changed")==0);
    m=Ready();m.Set(handles,std::uint32_t(0x6CE2));Check(c::Capture(base,0xA01D000,m,P,fixture_buffer+0x6A8,after)==nullptr);
    Check(std::strcmp(c::LocalUnchanged(before,after),"local_equipment_changed")==0);
    m=Ready();m.Set(rows+0xB,std::uint8_t(1));Check(c::Capture(base,0xA01D000,m,P,fixture_buffer+0x6A8,after)==nullptr);
    Check(std::strcmp(c::LocalUnchanged(before,after),"local_equipment_changed")==0);
    // Readiness is actual strict preflight success, never an elapsed delay.
    p::Readiness wait;p::Authorization early;early.Begin(30010);
    Check(early.CanObserveReadiness(x) && !early.Attempted());
    for(unsigned i=0;i<49;++i) Check(std::strcmp(wait.Observe(false),"readiness_wait")==0 && !early.Attempted());
    Check(wait.Observe(true)==nullptr && wait.Count()==49 && early.Try(x)==nullptr && early.Attempted());
    Check(!early.CanObserveReadiness(x));wait.Reset();early.Begin(30010);
    for(unsigned i=0;i<50;++i) wait.Observe(false);
    Check(std::strcmp(wait.Observe(true),"readiness_limit")==0 && !early.Attempted());
    Check(early.Try(x)==nullptr && early.Attempted());
    m=Ready();c::View<Memory> delayed_view{m,base,0xA01D000};
    auto package=c::ObservePackage(delayed_view);Check(package.reads==0x7FF && package.checks==0x3FF);
    m.Set(base+0x2A6D058,std::uint64_t(0x80));
    Check(std::strcmp(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before),"selected_package_bounds")==0);
    Check(before.package_observation.capacity==0x80 && !(before.package_observation.checks&2));
    m.Set(base+0x2A6D058,std::uint64_t(0x90));
    Check(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before)==nullptr);
    Refuse([](Memory& q){q.Set(fixture_map_flags+3,std::uint8_t(1));},"delayed_map_pending");
    Refuse([](Memory& q){q.Set(fixture_map_flags+3,std::uint8_t(2));},"delayed_map_pending");
    Refuse([](Memory& q){q.Set(rows+3*0xE,std::uint16_t(0x7FF));},"delayed_map_owner");
    Refuse([](Memory& q){q.Set(fixture_resource_rows+13*4,std::uint32_t(0));},"delayed_map_owner");
    Refuse([](Memory& q){q.Set(fixture_free_queue+7*4,std::uint32_t(12));},"delayed_free_queue_owner");
    Refuse([](Memory& q){q.unreadable=fixture_map_flags+32;},"delayed_map_unreadable");
    Refuse([](Memory& q){q.Set(0x12070188,std::uintptr_t(1));},"delayed_bank_array_bounds");
    Refuse([](Memory& q){q.Set(0x12070820,std::uint64_t(1));},"delayed_cache_null_dirty");
    Refuse([](Memory& q){for(unsigned i=0;i<11;++i)q.Set(0x12070700+i*0x18,std::uintptr_t(1));},"delayed_cache_capacity");
    Refuse([](Memory& q){q.Set(0x12070500,std::uintptr_t(1));},"delayed_free_target_not_empty");
    m=Ready();m.Set(rows+20*0xE,std::uint16_t(46)); // dormant valid row, no request/mapping
    Check(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before)==nullptr);
    // Retained different asset/zero-ref entry in a DIFFERENT eventual bank.
    m=Ready();m.Set(0x12070700,std::array<std::uint64_t,3>{0x18111111,0x1234,0});
    m.Set(0x12070820,std::uint64_t(0x18222222));m.Set(0x12070898,std::uintptr_t(0x18333333));
    Check(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before)==nullptr);
    Check(before.next_resource==12 && (before.eligible_resources&(1ull<<14)) && !(before.eligible_resources&1));
    Check(c::DelayedCachesPreserved(delayed_view,before)==nullptr);
    // Another native request takes unused1. Stock binds unused2 in bank7,
    // although predispatch predicted bank6/resource12. No eviction is accepted.
    m.Set(0x12070718,std::uintptr_t(0x18444444));m.Set(0x12070828,std::uint64_t(0x18555555));
    m.Set(0x12070882,std::uint16_t(1));m.Set(0x120708A0,std::uintptr_t(0x18666666));
    m.Set(0x12070730,std::uintptr_t(before.stock_parts_asset));m.Set(0x12070830,std::uint64_t(before.mission_entry));
    m.Set(0x12070884,std::uint16_t(1));m.Set(0x120708A8,std::uintptr_t(0x18777777));
    m.Set(0x12070500,std::uintptr_t(0x18888888));m.Set(0x12070900,std::uint8_t(2));
    m.Set(0x12070920,std::uintptr_t(0x12070A00));m.Set(0x12070A00,std::uint32_t(1184));
    m.Set(0x12070A0C,std::uint32_t(0x20));
    m.Set(fixture_resource_rows+14*4,std::uint32_t(before.new_index));
    Check(c::DelayedCachesPreserved(delayed_view,before)==nullptr);
    Check(c::QualifiedBoundResource(delayed_view,before,14)==nullptr);
    m.Set(0x12070920,std::uintptr_t(0x12070A08));
    Check(std::strcmp(c::QualifiedBoundResource(delayed_view,before,14),"bound_render_unqualified")==0);
    m.Set(0x12070920,std::uintptr_t(0x12070A00));m.Set(0x12070A00,std::uint32_t(2049));
    Check(std::strcmp(c::QualifiedBoundResource(delayed_view,before,14),"bound_render_unqualified")==0);
    m.Set(0x12070A00,std::uint32_t(1184));m.Set(0x12070A0C,std::uint32_t(0));
    Check(std::strcmp(c::QualifiedBoundResource(delayed_view,before,14),"bound_render_unqualified")==0);
    m.Set(0x12070A0C,std::uint32_t(0x20));
    Check(std::strcmp(c::QualifiedBoundResource(delayed_view,before,0),"bound_resource_unqualified")==0);
    m.Set(0x12070900,std::uint8_t(0));
    Check(std::strcmp(c::QualifiedBoundResource(delayed_view,before,14),"bound_cache_unqualified")==0);
    m.Set(0x12070900,std::uint8_t(2));m.Set(0x12070830,std::uint64_t(0));
    Check(std::strcmp(c::QualifiedBoundResource(delayed_view,before,14),"bound_stock_cache_identity")==0);
    m.Set(0x12070820,std::uint64_t(0));
    Check(std::strcmp(c::DelayedCachesPreserved(delayed_view,before),"delayed_occupied_cache_changed")==0);
    m=Ready();Check(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before)==nullptr);
    m.Set(0x12070188,std::uintptr_t(0x18999999));
    Check(std::strcmp(c::DelayedCachesPreserved(delayed_view,before),"delayed_bank_owner_changed")==0);
    // Same visible local rows cannot lend a replacement mapping service the
    // old reverse-owner lease. Capture can be valid; LocalUnchanged refuses.
    m=Ready();Check(c::Capture(base,0xA01D000,m,P,fixture_buffer+0x6A8,before)==nullptr);
    constexpr std::uintptr_t new_map=0x19000000,new_maps=0x19001000,new_meta=0x19002000,new_rows=0x19003000,new_owners=0x19004000;
    m.Method(new_map,0x234A3E8,0,0xDB7590);m.Set(new_map+8,std::uint32_t(60));
    m.Set(new_map+0xC,std::uint32_t(60));m.Set(new_map+0x68,meta);
    m.Set(new_map+0x18,map_rows);m.Set(new_map+0x10,fixture_resource_rows);
    m.Set(new_map+0x20,fixture_map_flags);m.Set(new_map+0x28,fixture_free_queue);
    m.Set(new_map+0x30,std::uint32_t(60));m.Set(new_map+0x34,std::uint32_t(6));
    m.Set(new_map+0x70,engine+0x40);m.Set(engine+0x128,new_map);
    Check(c::Capture(base,0xA01D000,m,P,fixture_buffer+0x6A8,after)==nullptr);
    Check(std::strcmp(c::LocalUnchanged(before,after),"owner_changed")==0);
    m.Set(engine+0x128,map);std::array<std::uint32_t,60> fresh_maps{};Check(m(map_rows,fresh_maps.data(),sizeof(fresh_maps)));
    m.Set(new_maps,fresh_maps);m.Set(map+0x18,new_maps);
    Check(c::Capture(base,0xA01D000,m,P,fixture_buffer+0x6A8,after)==nullptr);
    Check(std::strcmp(c::LocalUnchanged(before,after),"owner_changed")==0);
    m.Set(map+0x18,map_rows);m.Method(new_meta,0x234A178,0,0xDB72A0);
    m.Set(new_meta+0x58,std::uint32_t(60));m.Set(new_meta+0x28,rows);
    m.Set(engine+0x120,new_meta);m.Set(map+0x68,new_meta);
    Check(c::Capture(base,0xA01D000,m,P,fixture_buffer+0x6A8,after)==nullptr);
    Check(std::strcmp(c::LocalUnchanged(before,after),"owner_changed")==0);
    m.Set(engine+0x120,meta);m.Set(map+0x68,meta);std::array<c::Bytes14,60> fresh_metadata{};
    Check(m(rows,fresh_metadata.data(),sizeof(fresh_metadata)));m.Set(new_rows,fresh_metadata);m.Set(meta+0x28,new_rows);
    Check(c::Capture(base,0xA01D000,m,P,fixture_buffer+0x6A8,after)==nullptr);
    Check(std::strcmp(c::LocalUnchanged(before,after),"owner_changed")==0);
    m.Set(meta+0x28,rows);std::array<std::uint32_t,60> fresh_owners{};
    Check(m(fixture_resource_rows,fresh_owners.data(),sizeof(fresh_owners)));m.Set(new_owners,fresh_owners);m.Set(map+0x10,new_owners);
    Check(c::Capture(base,0xA01D000,m,P,fixture_buffer+0x6A8,after)==nullptr);
    Check(std::strcmp(c::LocalUnchanged(before,after),"owner_changed")==0);
    // Native bank lookup has an object-vtable prefix: poison both neighbors.
    // The previous logical45 hash cannot authorize native46, even if present.
    m=Ready();m.Set(base+0x2A6B400+46*8,std::uint64_t(0x67011DD7778F7009ull));
    m.Set(base+0x2A6B400+8+47*8,std::uint64_t(0x67011DD7778F7009ull));
    Check(c::ObservePackage(delayed_view).descriptor==c::optional46);
    Check(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before)==nullptr);
    Check(before.optional.asset==0x16100000 && before.optional.bytes==1824 &&
          before.optional.parameters==3 && before.optional.global==5 && before.optional.render_bytes==1184);
    Refuse([](Memory& q){q.Set(base+0x2A6B400+8+46*8,std::uint64_t(0x67011DD7778F7009ull));},"selected_package_bounds");
    Refuse([](Memory& q){q.Set(0x14000160,c::optional46+1);},"stock_optional_asset_missing");
    Refuse([](Memory& q){q.Set(0x14000168,std::uintptr_t(0x16000000));},"stock_optional_state");
    Refuse([](Memory& q){q.Set(base+0x24C2890+0x48,base+0x1A75341);},"stock_optional_state");
    Refuse([](Memory& q){q.Set(0x16100030,std::uint64_t(0));},"stock_optional_state");
    Refuse([](Memory& q){q.Set(0x16100040,std::uint8_t(1));},"stock_optional_state");
    Refuse([](Memory& q){q.Set(0x16100044,std::uint32_t(1));},"stock_optional_state");
    Refuse([](Memory& q){q.Set(0x16100080,std::uint32_t(0x1000000));},"stock_optional_state");
    Refuse([](Memory& q){q.Set(0x16100098,std::uintptr_t(0x16200008));},"stock_optional_source");
    Refuse([](Memory& q){q.Set(0x16100068,std::uint64_t(31));},"stock_optional_source");
    Refuse([](Memory& q){q.Set(0x16100068,std::uint64_t(1024*1024+1));},"stock_optional_source");
    Refuse([](Memory& q){q.Set(0x16100060,std::uintptr_t(UINTPTR_MAX-31));q.Set(0x16100098,std::uintptr_t(UINTPTR_MAX-31));},"stock_optional_source");
    Refuse([](Memory& q){q.Set(0x16200000,std::uint32_t(0));},"stock_optional_header");
    Refuse([](Memory& q){q.Set(0x16100084,std::uint32_t(2));},"stock_optional_header");
    Refuse([](Memory& q){q.Set(0x16100068,std::uint64_t(79));},"stock_optional_header");
    Refuse([](Memory& q){q.Set(0x16200012,std::uint16_t(0x1000));},"stock_optional_shape");
    Refuse([](Memory& q){q.Set(0x1620000C,std::uint16_t(1));},"stock_optional_shape");
    Refuse([](Memory& q){q.Set(0x16200010,std::uint16_t(1));},"stock_optional_shape");
    // The generalized shape: word0 = n in 1..8, word1 = m(n) of the exe's per-parameter table (1A95470 mode 0).
    Refuse([](Memory& q){q.Set(0x1620000A,std::uint16_t(5));},"stock_optional_shape");
    Refuse([](Memory& q){q.Set(0x16200008,std::uint16_t(0));q.Set(0x1620000A,std::uint16_t(0));},"stock_optional_shape");
    Refuse([](Memory& q){q.Set(0x16200008,std::uint16_t(9));q.Set(0x1620000A,std::uint16_t(11));},"stock_optional_shape");
    Refuse([](Memory& q){q.Set(0x16200008,std::uint16_t(3));q.Set(0x1620000A,std::uint16_t(6));},"stock_optional_shape");
    Check(c::kOptionalShapes[3].n==4 && c::kOptionalShapes[3].m==6 && c::kOptionalShapes[3].size==544 &&
          c::OptionalShapeOf(4,6)==&c::kOptionalShapes[3] && !c::OptionalShapeOf(4,7) && !c::OptionalShapeOf(0,2) && !c::OptionalShapeOf(9,11));
    for(std::uint16_t n=1;n<=8;++n) {
        const auto& shape=c::kOptionalShapes[n-1];
        auto q=Ready();q.Set(0x16200008,n);q.Set(0x1620000A,shape.m);
        Check(shape.n==n && c::Preflight(base,0xA01D000,q,P,fixture_buffer+0x6A8,before)==nullptr &&
              before.optional.render_bytes==512+shape.size+128);
    }
    {
        // n8 (832 bytes) at g 113: 512 + 832 + 992 > 2048.
        auto q=Ready();q.Set(0x16200008,std::uint16_t(8));q.Set(0x1620000A,std::uint16_t(10));q.Set(base+0x2C71680,std::uint32_t(113));
        Check(std::strcmp(c::Preflight(base,0xA01D000,q,P,fixture_buffer+0x6A8,before),"stock_optional_render_size")==0);
    }
    Refuse([](Memory& q){q.Set(base+0x2C71680,std::uint32_t(154));},"stock_optional_global");
    Refuse([](Memory& q){q.Set(base+0x2C71680,std::uint32_t(114));},"stock_optional_render_size");
    m=Ready();m.Set(base+0x2C71680,std::uint32_t(113));
    Check(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before)==nullptr && before.optional.render_bytes==2048);
    // Qualify a DIFFERENT eventual resource's main render backing, not just
    // the predicted resource12/group6. All ownership failures refuse first.
    Refuse([](Memory& q){q.Set(0x12070168,std::uintptr_t(0));},"delayed_main_render_shape");
    Refuse([](Memory& q){q.Set(0x12070170,std::uint32_t(2047));},"delayed_render_bank_bounds");
    Refuse([](Memory& q){q.Set(0x12070178,std::uintptr_t(0x12070A00));},"delayed_bank_array_alias");
    Refuse([](Memory& q){q.Set(0x120701E8,std::uintptr_t(0x120750C0));},"delayed_bank_array_bounds");
    Refuse([](Memory& q){q.Set(0x120701D8,base+0x1306FF1);},"delayed_main_render_shape");
    Refuse([](Memory& q){q.Set(0x120701E0,std::uintptr_t(0x12070000));},"delayed_render_owner");
    Refuse([](Memory& q){q.Set(0x12071A40,base+0x22E2951);},"delayed_render_owner");
    Refuse([](Memory& q){q.Set(0x12071A80,std::uintptr_t(0x12060100));},"delayed_render_owner");
    Refuse([](Memory& q){q.Set(0x12071A8C,std::uint32_t(1));},"delayed_render_owner");
    Refuse([](Memory& q){q.Set(0x12071A00,std::uintptr_t(1));},"delayed_free_render_not_empty");
    Refuse([](Memory& q){q.Set(0x12071A54,std::uint32_t(0));},"delayed_free_render_not_empty");
    Refuse([](Memory& q){q.Set(0x12071A88,std::uint32_t(1));},"delayed_free_render_not_empty");
    Refuse([](Memory& q){q.Set(0x12070140,std::uint64_t(0x1500));},"delayed_bank_array_bounds");
    Refuse([](Memory& q){q.Set(0x12070268,std::uintptr_t(0x12070A00));},"delayed_subbank_render_shape");
    m=Ready();Check(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before)==nullptr);
    m.Set(base+0x2C71680,std::uint32_t(6));
    Check(c::Capture(base,0xA01D000,m,P,fixture_buffer+0x6A8,after)==nullptr);
    Check(std::strcmp(c::LocalUnchanged(before,after),"stock_optional_changed")==0);
    m.Set(base+0x2C71680,std::uint32_t(5));m.Set(0x16200014,std::uint32_t(1));
    Check(c::Capture(base,0xA01D000,m,P,fixture_buffer+0x6A8,after)==nullptr);
    Check(std::strcmp(c::LocalUnchanged(before,after),"stock_optional_changed")==0);
    m=Ready();Check(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before)==nullptr);
    m.Set(0x12071A54,std::uint32_t(0));m.Set(0x12071A88,std::uint32_t(1));
    Check(c::DelayedCachesPreserved(delayed_view,before)==nullptr); // legitimate typed extra content can change
    m.Set(0x12071A80,std::uintptr_t(1));
    Check(std::strcmp(c::DelayedCachesPreserved(delayed_view,before),"delayed_render_owner_changed")==0);
    // Early async optional readiness is a pure wait; no mutation attempt is
    // spent until the exact loaded/header/all-bank preflight passes.
    m=Ready();early.Begin(30010);wait.Reset();m.Set(0x16100040,std::uint8_t(1));
    Check(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before)!=nullptr);
    Check(std::strcmp(wait.Observe(false),"readiness_wait")==0 && !early.Attempted());
    m.Set(0x16100040,std::uint8_t(2));
    Check(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before)==nullptr);
    Check(wait.Observe(true)==nullptr && early.Try(x)==nullptr && early.Attempted());
    // --- The generalized writer contract (W3) ----------------------------------
    Check(c::TypeSlot(1)==0 && c::TypeSlot(5)==0 && c::TypeSlot(7)==0 && c::TypeSlot(4)==1 && c::TypeSlot(6)==1 &&
          c::TypeSlot(8)==1 && c::TypeSlot(2)==2 && c::TypeSlot(3)==2 && c::TypeSlot(9)==0xFF && c::TypeSlot(0)==0xFF);
    Check(c::PackageRowRva(46)==0x2C21420 && c::ExpectedHandle(6,c::kStock46)==((6u<<16)|(46u<<5)|4u));
    {
        m=Ready();General(m);c::View<Memory> gv{m,base,0xA01D000};
        std::array<c::CacheEntry,48> census{};
        Check(c::Census(gv,engine,census) && census[4].equip==46 && census[4].flags==3 && census[4].type==4 &&
              census[4].receiver==157 && census[4].refs==0 && census[0].refs==1 && !census[5].flags);
        Check(c::FirstEntry(census,46)==4 && c::FirstEntry(census,47)==48 && c::ResidentProtected(census[4]) &&
              !c::ResidentProtected(census[0]));
        // stock46 through Derive: the same contract, measured parsed counts kept.
        c::Target t{};
        Check(c::Derive(gv,engine,46,census,t)==nullptr && t.slot==1 && t.type==4 && t.row==393 && t.cache_index==4 &&
              t.optional && t.optional_hash==c::optional46 && t.exact_parsed && !t.require_remote_empty &&
              t.hash_count==4 && t.hashes==c::kStock46.hashes && t.recipe==c::kStock46.recipe);
        Check(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before,t)==nullptr && before.cache_refs==0);
        // Another slot's weapon does not block slot 1; the target slot never gets replaced.
        m.Set(handles+7*4,std::uint32_t(0x7002E1));
        Check(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before,t)==nullptr);
        Check(std::strcmp(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before),"remote_not_empty")==0);
        m.Set(handles+8*4,std::uint32_t(0x8005C4));
        Check(std::strcmp(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before,t),"remote_slot_not_empty")==0);
        m=Ready();General(m);m.Set(metadata+8*6+2,std::uint8_t(1));
        Check(std::strcmp(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before,t),"remote_slot_not_empty")==0);
        m=Ready();General(m);m.Set(0x11001000+0x404+12,std::uint32_t(7));
        Check(std::strcmp(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before,t),"remote_binder_not_empty")==0);
        m=Ready();General(m);m.Set(0x11001000+0x400,std::uint32_t(7)); // slot 0's binder record: not slot 1's business
        Check(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before,t)==nullptr);
        // A malformed target refuses before any read.
        auto bad=t;bad.slot=0;Check(std::strcmp(c::Capture(base,0xA01D000,m,P,fixture_buffer+0x6A8,after,bad),"target_shape")==0);
        bad=t;bad.cache_index=48;Check(std::strcmp(c::Capture(base,0xA01D000,m,P,fixture_buffer+0x6A8,after,bad),"target_shape")==0);
    }
    {
        // A plain hip gun (family N): no optional descriptor (id above 0x7C), NULL-optional render size.
        m=Ready();Plain300(m);c::View<Memory> gv{m,base,0xA01D000};
        std::array<c::CacheEntry,48> census{};Check(c::Census(gv,engine,census));
        c::Target t{};
        Check(c::Derive(gv,engine,300,census,t)==nullptr && t.slot==0 && t.type==1 && t.cache_index==5 && !t.optional &&
              !t.exact_parsed && t.hash_count==4 && t.hashes==c::kStock46.hashes);
        Check(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before,t)==nullptr);
        Check(before.optional.render_bytes==640 && before.optional.global==5 && !before.optional.asset &&
              before.package_observation.reads==0x3FF && before.package_observation.checks==0x1FF);
        Check(c::ObservePackage(gv,t).reads==0x3FF && c::PackageReads(t)==0x3FF && c::PackageChecks(t)==0x1FF);
        // Slot 0's attachments 0..12 and effect record must be empty.
        m.Set(0x13012000+2*0x20,std::uintptr_t(0x55));
        Check(std::strcmp(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before,t),"remote_attachment_not_empty")==0);
        m=Ready();Plain300(m);m.Set(0x11001000+0xA08,std::uint8_t(1));
        Check(std::strcmp(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before,t),"remote_slot_not_empty")==0);
        // A bound plain target's render object is 512 + align16(8g+88) = 640 at g 5.
        m=Ready();Plain300(m);Check(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before,t)==nullptr);
        c::View<Memory> bv{m,base,0xA01D000};
        m.Set(0x12070500,std::uintptr_t(0x18888888));m.Set(0x12070900,std::uint8_t(2));
        m.Set(0x12070730,std::uintptr_t(before.stock_parts_asset));m.Set(0x12070830,std::uint64_t(before.mission_entry));
        m.Set(0x12070884,std::uint16_t(1));m.Set(0x120708A8,std::uintptr_t(0x18777777));
        m.Set(0x12070920,std::uintptr_t(0x12070A00));m.Set(0x12070A00,std::uint32_t(640));m.Set(0x12070A0C,std::uint32_t(0x20));
        m.Set(fixture_resource_rows+14*4,std::uint32_t(before.new_index));
        Check(c::QualifiedBoundResource(bv,before,14)==nullptr);
        m.Set(0x12070A00,std::uint32_t(1184));
        Check(std::strcmp(c::QualifiedBoundResource(bv,before,14),"bound_render_unqualified")==0);
        // A wrong cache identity, a shared or unprotected entry.
        m=Ready();Plain300(m);m.Set(info+5*0x90+0x78,std::uint8_t(4));
        Check(std::strcmp(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before,t),"stock_cache_identity")==0);
        m=Ready();Plain300(m,3,1);
        Check(std::strcmp(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before,t),"stock_cache_in_use")==0);
        m=Ready();Plain300(m,1,0);
        Check(std::strcmp(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before,t),"stock_cache_identity")==0);
        Check(c::Census(gv,engine,census) && !c::ResidentProtected(census[5]));
        // The package row and the type table are re-read against the target.
        m=Ready();Plain300(m);m.Set(base+c::PackageRowRva(300)+16,std::uint64_t(3));
        Check(std::strcmp(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before,t),"stock_parameter_bank")==0);
        m=Ready();Plain300(m);m.Set(base+0x2A70920+8+300*2,std::uint16_t((393<<6)|5));
        Check(std::strcmp(c::Preflight(base,0xA01D000,m,P,fixture_buffer+0x6A8,before,t),"stock_parameter_bank")==0);
    }
    {
        // Derive refusals.
        m=Ready();Plain300(m);c::View<Memory> gv{m,base,0xA01D000};
        std::array<c::CacheEntry,48> census{};Check(c::Census(gv,engine,census));
        c::Target t{};
        Check(std::strcmp(c::Derive(gv,engine,0,census,t),"equip_range")==0);
        Check(std::strcmp(c::Derive(gv,engine,0x230,census,t),"equip_range")==0);
        m.Set(base+0x2A70920+8+301*2,std::uint16_t((393<<6)|9));
        Check(std::strcmp(c::Derive(gv,engine,301,census,t),"type")==0);
        m.Set(base+0x2A70920+8+301*2,std::uint16_t(1));
        Check(std::strcmp(c::Derive(gv,engine,301,census,t),"row")==0);
        m.Set(base+0x2A70920+8+301*2,std::uint16_t((515<<6)|1));
        Check(std::strcmp(c::Derive(gv,engine,301,census,t),"row")==0);
        m.Set(base+0x2A70920+8+301*2,std::uint16_t((393<<6)|1));
        Check(std::strcmp(c::Derive(gv,engine,301,census,t),"package_row")==0);
        m.Set(base+c::PackageRowRva(301),std::array<std::uint64_t,3>{mgscoop::equipment_stock_contract::parts,
            mgscoop::equipment_stock_contract::package,2});
        Check(std::strcmp(c::Derive(gv,engine,301,census,t),"no_cache_entry")==0);
        m.Set(base+c::PackageRowRva(301),std::array<std::uint64_t,3>{0x1094ull<<0x33,mgscoop::equipment_stock_contract::package,2});
        Check(std::strcmp(c::Derive(gv,engine,301,census,t),"parts_class")==0);
        m.Set(base+0x2349348+55*16,std::uint64_t(0));
        Check(std::strcmp(c::Derive(gv,engine,300,census,t),"dependency_hash")==0);
        m=Ready();Plain300(m);m.Set(engine+8,std::uintptr_t(0x1234));
        Check(std::strcmp(c::Derive(gv,engine,300,census,t),"type_table")==0);
    }
    {
        // Tier C (the substitute search): run 53e36944's census shape. 6 t2 r18; 36 t1 r70; 39 t1 r69 with three
        // references (the soldiers' rifle: never shared); the viewer's alias 871 (f1/n1); 46 t4; 27 t1 r69; a later
        // second entry of 36 (not the id's first); 16 t2 r40; 40 t5 (the hip class).
        std::array<c::CacheEntry,48> census{};
        census[0]={6,2,18,3,0};census[1]={36,1,70,3,0};census[2]={39,1,69,3,3};census[3]={871,1,69,1,1};census[4]={46,4,157,3,0};
        census[5]={27,1,69,3,0};census[6]={36,1,70,3,0};census[7]={16,2,40,3,0};census[8]={40,5,77,3,0};census[9]={50,1,69,1,0};
        c::SubstituteQuery q{};q.slot=0;q.type=1;q.receiver=69;q.want=0;
        std::array<bool,48> tried{};
        Check(c::NextCandidate(census,q,c::SubstitutePass::type_receiver,tried)==5);  // 27: 39 (n3), 871 (alias), 50 (f1) never
        tried[5]=true;
        Check(c::NextCandidate(census,q,c::SubstitutePass::type_receiver,tried)==48);
        Check(c::NextCandidate(census,q,c::SubstitutePass::type,tried)==1);  // 36, its first entry
        tried[1]=true;
        Check(c::NextCandidate(census,q,c::SubstitutePass::type,tried)==48);  // index 6 is not 36's first entry
        Check(c::NextCandidate(census,q,c::SubstitutePass::slot_class,tried)==8);  // 40 (type 5, the hip class)
        tried[8]=true;
        Check(c::NextCandidate(census,q,c::SubstitutePass::slot_class,tried)==48);
        // Without a type only the slot class; without a receiver no first pass.
        c::SubstituteQuery any=q;any.type=0;std::array<bool,48> none{};
        Check(c::NextCandidate(census,any,c::SubstitutePass::type_receiver,none)==48 && c::NextCandidate(census,any,c::SubstitutePass::type,none)==48 &&
              c::NextCandidate(census,any,c::SubstitutePass::slot_class,none)==5);
        c::SubstituteQuery norecv=q;norecv.receiver=0;
        Check(c::NextCandidate(census,norecv,c::SubstitutePass::type_receiver,none)==48 && c::NextCandidate(census,norecv,c::SubstitutePass::type,none)==5);
        // The secondary slot: 6 (t2) then 16 (t2); slot 1: 46 (t4).
        c::SubstituteQuery sec{};sec.slot=2;sec.type=2;sec.receiver=11;
        Check(c::NextCandidate(census,sec,c::SubstitutePass::type_receiver,none)==48 && c::NextCandidate(census,sec,c::SubstitutePass::type,none)==0);
        c::SubstituteQuery back{};back.slot=1;back.type=0;
        Check(c::NextCandidate(census,back,c::SubstitutePass::slot_class,none)==4);
        // Explicit exclusions: the want, the viewer's own weapon, an equip already dispatched this allocation.
        c::SubstituteQuery ex=q;ex.want=27;
        Check(std::strcmp(c::CandidateExcluded(census[5],ex),"want")==0);
        ex=q;ex.viewer[1]=27;
        Check(std::strcmp(c::CandidateExcluded(census[5],ex),"viewer_weapon")==0);
        ex=q;ex.dispatched[2]=27;
        Check(std::strcmp(c::CandidateExcluded(census[5],ex),"dispatched")==0 && c::CandidateExcluded(census[1],ex)==nullptr);
        // The substitute's type: the manifest's (of this slot), else the want's table type (of this slot), else the class.
        Check(c::SubstituteType(1,0,0)==1 && c::SubstituteType(0,1,0)==1 && c::SubstituteType(4,1,0)==1 && c::SubstituteType(4,0,1)==4 &&
              c::SubstituteType(0,11,0)==0 && c::SubstituteType(2,0,2)==2 && c::SubstituteType(9,0,1)==0);
        Check(std::strcmp(c::SubstitutePassName(c::SubstitutePass::slot_class),"slot_class")==0);
    }
    std::printf("equipment_stock_writer_tests: %u checks passed; native_calls=0 game_reads=0\n",checks);
}
