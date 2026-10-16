#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <cstddef>
#include <initializer_list>
#include "mgscoop/partner_support_equip.hpp"
#include "partner_player_contract.hpp"
#include "equipment_stock_writer_pins.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
namespace se=mgscoop::partner_support_equip;
namespace c=mgscoop::partner_player_contract;
unsigned checks=0;
void Check(bool ok,const char* label) {++checks;if(!ok){std::fprintf(stderr,"failed: %s\n",label);std::exit(1);}}
void Is(const se::Preconditions& p,const char* reason) {
    const char* actual=se::Refusal(p);
    Check(reason ? actual && std::strcmp(actual,reason)==0 : !actual,reason ? reason : "qualified type12");
}

// --- WS4 review (WS4-3): the real writer (Observe / Ready / Follow) of the slot-6 arm lane on a fake world. -------
// A synthetic image (vtables and slots at their contract RVAs, read through the writer's own ReadProcessMemory
// reader) and fake game objects in local memory. ChangeEquip 0x11AC2E0 and SetCurrentSlot 0x11AFA80 are the only
// executable bytes: an absolute jump to the fakes below, which play the native effects for id 1 (equip-audit:
// Y+0x40[1*7+6] allocated, row+8 = the arm, flags 4 activates slot 6; case 6 row+0 = row+8). No game code runs.
namespace world {
template<class T> void Put(std::uintptr_t a,T v) {std::memcpy(reinterpret_cast<void*>(a),&v,sizeof(v));}
template<class T> T Get(std::uintptr_t a) {T v{};std::memcpy(&v,reinterpret_cast<const void*>(a),sizeof(v));return v;}
std::uintptr_t Addr(const void* p) {return reinterpret_cast<std::uintptr_t>(p);}
constexpr std::size_t kSpan=0x2C40000;  // past service_root_rva 0x2C3A8A0
static_assert(c::service_root_rva+8<kSpan && c::vars_slot_rva+8<kSpan && c::player_slot_rva+8<kSpan &&
              c::equip_types_rva+8+2*c::stock_ids<kSpan,"the fake image covers every RVA the writer reads");
#pragma pack(push,1)
struct Request {std::uint64_t type;std::uint16_t equip,stock,stock_sub,ammo,ammo_sub;std::uint8_t suppression,flags;};
#pragma pack(pop)
static_assert(sizeof(Request)==20 && offsetof(Request,flags)==19,"ChangeEquip's request (partner_support_equip.cpp)");
struct Objects {
    unsigned char player[0x400];
    unsigned char ids[8],pools[8];
    std::uintptr_t blocks[2];
    unsigned char buffer[0x6A8*2];   // the block buffer; body 1's block is buffer + 1*0x6A8
    unsigned char predicate[0x200],pred_rows[8],writer[0x20];
    unsigned char ctl[0x40],inventory[0x80],engine[0x180],q[0xD0];
    std::uint32_t handles[14];       // Y+0x40: 7 slots of instance 0, then 7 of instance 1
    unsigned char metadata[14*6];
    unsigned char rows[0x3A*2];      // instance 0's row, then body 1's (row = rows + 0x3A)
    unsigned char vars[0x700];
    unsigned char root[0x10],global[0xA0],services[0x200],owner_ctl[8],model[8];
    unsigned char pool_rows[64*6];   // the manager rows X+0x140 (equip u16, owner u16, flags, model)
};
unsigned char* g_image=nullptr;
std::uintptr_t g_base=0;
Objects* g_o=nullptr;
std::string g_log;
void Log(const char* line) noexcept {g_log+=line;}
bool Logged(const char* part) {return g_log.find(part)!=std::string::npos;}
bool g_pins_ok=true;
bool Pins(std::uintptr_t base,std::size_t span,std::uintptr_t& mismatch) noexcept {mismatch=0;return base==g_base && span==kSpan && g_pins_ok;}
// The fakes' effects and what they were called with.
unsigned g_dispatches=0,g_follows=0,g_next_index=20;
bool g_activate=true,g_touch_slot3=false;
std::uintptr_t g_call_ctl=0;
std::uint32_t g_call_id=0;std::uint16_t g_call_equip=0;std::uint8_t g_call_flags=0;
std::int32_t g_set_slot=-1;std::uint8_t g_set_sub=0xFF;
std::uintptr_t Row() {return Addr(g_o->rows)+0x3A;}
std::uint32_t Handle(std::uint32_t index,std::uint32_t equip,std::uint32_t kind) {return (index<<16)|(equip<<5)|kind;}
void Meta(std::uint32_t index,std::uint16_t equip,std::uint16_t owner,std::uint8_t flags=0) {
    const std::uintptr_t m=Addr(g_o->pool_rows)+index*6;
    Put<std::uint16_t>(m,equip);Put<std::uint16_t>(m+2,owner);Put<std::uint8_t>(m+4,flags);Put<std::uint8_t>(m+5,0);
}
std::int32_t* FakeChangeEquip(void* ctl,std::int32_t* result,std::uint32_t id,const Request* request) noexcept {
    ++g_dispatches;g_call_ctl=Addr(ctl);g_call_id=id;g_call_equip=request->equip;g_call_flags=request->flags;
    const std::uint32_t index=g_next_index++;
    g_o->handles[7+6]=Handle(index,request->equip,14);
    Meta(index,request->equip,1);
    Put<std::uint16_t>(Row()+8,request->equip);                                   // ctl vt+0x98(id, 6, 0, equip)
    if(g_activate){Put<std::uint8_t>(Row()+0x36,6);Put<std::uint16_t>(Row(),request->equip);}  // flags 4: SetCurrentSlot(id, 6, 0)
    if(g_touch_slot3)g_o->handles[7+3]^=0x10000u;                                  // another slot of body 1 changes
    *result=0;return result;
}
void FakeSetCurrentSlot(void* ctl,std::uint32_t id,std::int32_t slot,std::uint8_t sub,char,char,int) noexcept {
    ++g_follows;g_call_ctl=Addr(ctl);g_call_id=id;g_set_slot=slot;g_set_sub=sub;
    if(slot==6){Put<std::uint8_t>(Row()+0x36,6);Put<std::uint16_t>(Row(),Get<std::uint16_t>(Row()+8));}
}
void Jump(std::uintptr_t rva,std::uintptr_t to) {
    unsigned char* at=g_image+rva;
    DWORD old=0;
    Check(VirtualProtect(at,16,PAGE_EXECUTE_READWRITE,&old)!=0,"fake image: executable stub page");
    const unsigned char stub[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};  // mov rax, imm64; jmp rax
    std::memcpy(at,stub,sizeof(stub));
    std::memcpy(at+2,&to,8);
    FlushInstructionCache(GetCurrentProcess(),at,sizeof(stub));
}
void Method(std::uintptr_t object,std::uintptr_t vt,std::size_t slot,std::uintptr_t fn) {
    Put<std::uintptr_t>(object,g_base+vt);Put<std::uintptr_t>(g_base+vt+slot,g_base+fn);
}
void Build() {
    if(!g_image) {
        g_image=static_cast<unsigned char*>(VirtualAlloc(nullptr,kSpan,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        Check(g_image!=nullptr,"fake image allocation");
        g_base=Addr(g_image);g_o=new Objects();
        Jump(0x11AC2E0,reinterpret_cast<std::uintptr_t>(&FakeChangeEquip));
        Jump(c::set_current_slot_rva,reinterpret_cast<std::uintptr_t>(&FakeSetCurrentSlot));
    }
    std::memset(g_o,0,sizeof(*g_o));
    Objects& o=*g_o;
    const std::uintptr_t player=Addr(o.player),block=Addr(o.buffer)+0x6A8;
    Put<std::uintptr_t>(g_base+c::player_slot_rva,player);
    Method(player,0x2295490,0x28,0x9CCB40);
    Put<std::uint32_t>(player+0x208,2);Put<std::uint32_t>(player+0x204,1);Put<std::uint32_t>(player+0x214,0);Put<std::uint32_t>(player+0x218,0);
    Put<std::uintptr_t>(player+0x70,Addr(o.ids));Put<std::uint16_t>(Addr(o.ids)+2,1);
    Put<std::uintptr_t>(player+0x78,Addr(o.pools));o.pools[1]=1;
    Put<std::uintptr_t>(player+0x50,Addr(o.blocks));Put<std::uintptr_t>(player+0x378,Addr(o.buffer));o.blocks[1]=block;
    Method(block,0x2295210,0x180,0x9C3B40);
    Put<std::uint32_t>(block+8,1);Put<std::uint32_t>(block+0xC,1);Put<std::uintptr_t>(block+0x138,player);
    Put<std::uintptr_t>(block+0x70,Addr(o.predicate));Method(Addr(o.predicate),0x23BC1B0,0x30,0x1156640);
    Put<std::uintptr_t>(Addr(o.predicate)+0x168,block);Put<std::uintptr_t>(Addr(o.predicate)+0x1A8,Addr(o.pred_rows));
    Put<std::uintptr_t>(block+0xF0,Addr(o.writer));Method(Addr(o.writer),0x23C95D8,0x28,0x1209EC0);Put<std::uintptr_t>(Addr(o.writer)+8,block);
    const std::uintptr_t ctl=Addr(o.ctl);
    Put<std::uintptr_t>(player+0xC8,ctl);
    Method(ctl,c::equip_ctl_vt_rva,0x198,0x11AC2E0);Method(ctl,c::equip_ctl_vt_rva,0x10,c::set_current_slot_rva);
    Method(ctl,c::equip_ctl_vt_rva,0x18,0x11B0900);Method(ctl,c::equip_ctl_vt_rva,0xA0,0x11B0700);
    Method(ctl,c::equip_ctl_vt_rva,0x98,0x11AFF30);Method(ctl,c::equip_ctl_vt_rva,0x1F0,0x11B1B60);
    Put<std::uintptr_t>(ctl+0x18,player);Put<std::uintptr_t>(ctl+8,Addr(o.inventory));
    Put<std::uintptr_t>(ctl+0x28,Addr(o.engine));Put<std::uintptr_t>(ctl+0x20,Addr(o.vars));
    const std::uintptr_t inv=Addr(o.inventory);
    Method(inv,c::store_vt_rva,8,0xEE9880);Put<std::uintptr_t>(g_base+c::store_vt_rva+0x10,g_base+0xEE9D70);
    Put<std::uint32_t>(inv+0x30,0);Put<std::uint32_t>(inv+0xC,7);Put<std::uint32_t>(inv+0x10,14);
    Put<std::uintptr_t>(inv+0x40,Addr(o.handles));Put<std::uintptr_t>(inv+0x50,Addr(o.metadata));Put<std::uintptr_t>(inv+0x58,Addr(o.engine));
    const std::uintptr_t engine=Addr(o.engine);
    Method(engine,0x234B840,0,0xDC3250);Method(engine,0x234B840,8,0xDC63A0);Method(engine,0x234B840,0x10,0xDC2A50);
    Put<std::uintptr_t>(engine+8,g_base+c::equip_types_rva);
    Put<std::uint32_t>(engine+0x68,64);Put<std::uint32_t>(engine+0x6C,10);Put<std::uintptr_t>(engine+0x140,Addr(o.pool_rows));
    Method(g_base+c::equip_types_rva,c::equip_types_vt_rva,0,0xA2A3E0);
    Put<std::uint16_t>(g_base+c::equip_types_rva+8+2*354,12);Put<std::uint16_t>(g_base+c::equip_types_rva+8+2*516,14);
    Put<std::uintptr_t>(player+0x60,Addr(o.q));Put<std::uintptr_t>(Addr(o.q),g_base+c::q_vtable_rva);Put<std::uintptr_t>(Addr(o.q)+0xC0,Addr(o.rows));
    Put<std::uintptr_t>(g_base+c::vars_slot_rva,Addr(o.vars));
    Put<std::uintptr_t>(g_base+c::service_root_rva,Addr(o.root));Put<std::uintptr_t>(Addr(o.root)+8,Addr(o.global));
    Put<std::uintptr_t>(Addr(o.global)+0x98,Addr(o.services));Put<std::uintptr_t>(Addr(o.services)+0x1E8,engine);
    Put<std::uintptr_t>(Addr(o.services)+0x1A0,Addr(o.owner_ctl));
    Method(Addr(o.owner_ctl),0x25564F0,0x208,0x1E02AD0);Put<std::uintptr_t>(g_base+0x25564F0+0x1F8,g_base+0x1E02B40);
    Put<std::uintptr_t>(block+0x618,Addr(o.model));Method(Addr(o.model),0x22EA550,0x20,0xB0C720);
    // The viewer's own state the writer must never touch, and body 1 holding its gun (slot 1, equip 46) with
    // grenade 354 in its support list (sub 0) and no arm. Instance 0 holds a grenade handle in slot 3.
    o.handles[3]=Handle(4,354,12);Meta(4,354,0);
    Put<std::uint16_t>(Addr(o.rows),46);Put<std::uint8_t>(Addr(o.rows)+0x36,1);
    Put<std::uint16_t>(Row(),46);Put<std::uint8_t>(Row()+0x36,1);Put<std::uint8_t>(Row()+0x37,0);Put<std::uint16_t>(Row()+0xC,354);
    for(std::size_t k=0;k<sizeof(o.vars);++k)o.vars[k]=static_cast<unsigned char>(k*7);
    g_dispatches=g_follows=0;g_activate=true;g_touch_slot3=false;g_pins_ok=true;g_set_slot=-1;g_set_sub=0xFF;g_call_id=0;
}
std::uint64_t g_now=10000;
se::Context Arm(std::uint32_t mission,std::uint16_t equip=516,std::uint8_t slot=6) {
    se::Context x{};
    x.enabled=x.owner=x.gates=x.body_ready=x.safety_ready=true;x.arm=true;
    x.base=g_base;x.player=Addr(g_o->player);x.remote_block=Addr(g_o->buffer)+0x6A8;x.image_span=kSpan;
    x.mission=mission;x.realize_gen=1;x.now_ms=(g_now+=2000);x.equip=equip;x.slot=slot;x.pins_intact=&Pins;
    return x;
}
std::string Hex(std::uint32_t h) {char b[16]{};std::snprintf(b,sizeof(b),"%08x",h);return b;}

void TestArmLane() {
    // 1. The slot-6 dispatch: ChangeEquip(ctl, id 1, equip 516, flags 4); the handle the writer judges and owns is
    // body 1's Y+0x40[1*7+6]; SameLocal ignores only that handle (the dispatch changes it).
    Build();g_log.clear();
    auto x=Arm(41);
    const std::uint32_t viewer3=g_o->handles[3],body3=g_o->handles[7+3];
    se::Observe(x,&Log);
    const std::uint32_t h6=g_o->handles[7+6];
    Check(g_dispatches==1 && g_call_ctl==Addr(g_o->ctl) && g_call_id==1 && g_call_equip==516 && g_call_flags==4,
          "slot 6: one ChangeEquip(ctl, id 1, 516, flags 4)");
    Check(Logged(("partner_support=equip; slot=6; equip=516; type=14; reason=ok; instance=1; handle="+Hex(h6)+"; body_slot=6").c_str()),
          "slot 6: equipped, judged on Y+0x40[1*7+6] (kind 14), body 1's slot 6 active");
    Check(g_o->handles[3]==viewer3 && g_o->handles[7+3]==body3 && !se::Uncertain(),"slot 6: the viewer's and body 1's slot-3 handles untouched");
    // Ready answers the arm lane only, with the Match-verified slot-6 handle (WS1 B for slot 6).
    std::uint8_t type=0;std::uint32_t handle=0;
    Check(se::Ready(g_base,x.player,x.remote_block,516,&type,&handle,6) && type==14 && handle==h6,"Ready(slot 6): type 14, the slot-6 handle");
    Check(!se::Ready(g_base,x.player,x.remote_block,516,&type,&handle,3) && handle==0,"Ready(slot 3) for the arm: refused");
    // 2. Already allocated for this exact equip: no second dispatch, no line.
    const std::size_t lines=g_log.size();
    se::Observe(Arm(41),&Log);
    Check(g_dispatches==1 && g_log.size()==lines,"slot 6 already allocated: no re-dispatch");
    // 3. Follow: body 1 back on its gun (slot 1) with the arm still allocated: SetCurrentSlot(ctl, 1, 6, 0), no
    // support-list search (516 is not in it; a slot-3 follow would refuse support_list).
    Put<std::uint8_t>(Row()+0x36,1);Put<std::uint16_t>(Row(),46);
    Check(!se::Ready(g_base,x.player,x.remote_block,516,nullptr,nullptr,6),"Ready(slot 6) while slot 6 is not current: refused");
    const char* why=se::Follow(g_base,kSpan,x.player,x.remote_block,516,&Log,6);
    Check(!why && g_follows==1 && g_call_id==1 && g_set_slot==6 && g_set_sub==0 && g_call_ctl==Addr(g_o->ctl),"Follow(slot 6): SetCurrentSlot(ctl, 1, 6, 0)");
    Check(Get<std::uint8_t>(Row()+0x36)==6 && Get<std::uint16_t>(Row())==516 && Logged("partner_support=follow; slot=6; equip=516; type=14; reason=ok"),
          "Follow(slot 6): row+0 = row+8, logged");
    Check(!se::Follow(g_base,kSpan,x.player,x.remote_block,516,&Log,6) && g_follows==1,"Follow(slot 6) already current: no call");
    Check(!std::strcmp(se::Follow(g_base,kSpan,x.player,x.remote_block,516,&Log,3),"admission") && g_follows==1,"Follow(slot 3) for the arm context: refused");
    // 4. An owned slot-6 handle is ours (g_owned[1]): a p6 bit on its row re-dispatches instead of foreign_handle.
    Meta(h6>>16,516,1,1);
    se::Observe(Arm(41),&Log);
    Check(g_dispatches==2 && g_o->handles[7+6]!=h6 && !Logged("reason=foreign_handle"),"slot 6: our own handle is re-dispatched, never foreign");
    // ... and lanes own their handles separately: the slot-6 handle's value in slot 3 is foreign to the slot-3 lane.
    const std::uint32_t owned6=g_o->handles[7+6];
    g_o->handles[7+3]=owned6;
    se::Observe(Arm(41,354,3),&Log);
    Check(g_dispatches==2 && Logged(("partner_support=refused; slot=3; equip=354; type=12; reason=foreign_handle; instance=1; handle="+Hex(owned6)).c_str()),
          "slot 3: the slot-6 lane's handle is not slot 3's (g_owned per lane)");
}
void TestArmLaneRefusals() {
    // A slot-6 handle we did not allocate (another arm, owner 1): foreign_handle, no dispatch.
    Build();g_log.clear();
    g_o->handles[7+6]=Handle(30,515,14);Meta(30,515,1);Put<std::uint16_t>(Row()+8,515);
    se::Observe(Arm(42),&Log);
    Check(g_dispatches==0 && Logged(("partner_support=refused; slot=6; equip=516; type=14; reason=foreign_handle; instance=1; handle="+Hex(Handle(30,515,14))).c_str()),
          "slot 6: a handle we did not allocate refuses foreign_handle");
    // Body 1's slot 6 already holds a matching 516 handle while its slot 3 is empty: the writer judges slot 6
    // (Match passes, nothing to do); judging slot 3 would have dispatched.
    Build();g_log.clear();
    g_o->handles[7+6]=Handle(31,516,14);Meta(31,516,1);Put<std::uint16_t>(Row()+8,516);g_o->handles[7+3]=0;
    se::Observe(Arm(43),&Log);
    Check(g_dispatches==0 && g_log.empty(),"slot 6: the existing Y+0x40[1*7+6] is the lane's handle");
    // The pins gate runs before the slot-6 dispatch too (full_body_gate, WS4-2).
    Build();g_log.clear();g_pins_ok=false;
    se::Observe(Arm(44),&Log);
    Check(g_dispatches==0 && Logged("partner_support=refused; slot=6; equip=516; type=14; reason=full_body_gate"),"slot 6: pins refuse before the dispatch");
    // Another slot of body 1 changing inside the dispatch: SameLocal checks every slot but 6 (uncertain latch).
    Build();g_log.clear();g_touch_slot3=true;
    se::Observe(Arm(45),&Log);
    Check(g_dispatches==1 && Logged("partner_support=refused; slot=6; equip=516; type=14; reason=local_or_other_slot_changed") && se::Uncertain(),
          "slot 6: a slot-3 handle change in the dispatch latches uncertain");
    // Without the arm key slot 6 is the old idle refusal; the stun arm 515 is no lane (WS4-1: type).
    Build();g_log.clear();
    auto x=Arm(46);x.arm=false;se::Observe(x,&Log);
    Check(g_dispatches==0 && Logged("partner_support=refused; slot=6; equip=516; type=0; reason=slot"),"slot 6 without the arm key: slot");
    Put<std::uint16_t>(g_base+c::equip_types_rva+8+2*515,14);
    se::Observe(Arm(46,515),&Log);
    Check(g_dispatches==0 && Logged("partner_support=refused; slot=6; equip=515; type=14; reason=type"),"the stun arm 515 with the key: type (no lane)");
}
// WS4-2: the writer's pre-call SHA list covers ctl vt+0x1F0 0x11B1B60 whole (the slot-6 dispatch's own callee),
// and its digest is that of the seven contract parts (two generators, the same retail bytes).
void TestArmEquipPinned() {
    const mgscoop::equipment_stock_writer_pins::Body* body=nullptr;
    for(const auto& b:mgscoop::equipment_stock_writer_pins::bodies) if(b.rva==c::arm_equip_rva) body=&b;
    Check(body && body->size==0x207,"0x11B1B60 (0x207 bytes, ret at 0x11B1D66) is in the writer's SHA list");
    std::vector<unsigned char> bytes;
    for(std::uintptr_t at=c::arm_equip_rva;at<c::arm_equip_rva+0x207;) {
        const c::Site* part=nullptr;
        for(const auto& s:c::code_sites) if(s.rva==at) part=&s;
        Check(part!=nullptr,"the contract pins 0x11B1B60 contiguously");
        if(!part)return;
        bytes.insert(bytes.end(),part->bytes,part->bytes+part->size);at+=part->size;
    }
    Check(bytes.size()==0x207,"the seven parts are 0x207 bytes");
    unsigned char digest[32]{};
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    const bool hashed=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0 &&
        BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0 &&
        BCryptHashData(hash,bytes.data(),static_cast<ULONG>(bytes.size()),0)>=0 && BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    if(hash)BCryptDestroyHash(hash);
    if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    Check(hashed,"sha256");
    Check(body && std::memcmp(digest,body->sha256,sizeof(digest))==0,"the writer's 0x11B1B60 gate is the contract's bytes");
}
}

int main() {
    se::Preconditions good{};good.enabled=good.owner=good.gates=good.body_ready=good.safety_ready=true;
    good.player_flags=1;good.local=good.local_id=good.inventory_local=0;
    good.body_id=1;good.pool=1;good.equip=354;good.type=12;good.sub=0;good.restriction=0;
    Is(good,nullptr);
    auto p=good;p.enabled=false;Is(p,"off");p=good;p.owner=false;Is(p,"thread");
    p=good;p.gates=false;Is(p,"admission");p=good;p.body_ready=false;Is(p,"body");
    p=good;p.safety_ready=false;Is(p,"safety");p=good;p.player_flags=0;Is(p,"offline");
    p=good;p.player_flags=0x101;Is(p,"offline");p=good;p.local=1;Is(p,"p214");
    p=good;p.local_id=1;Is(p,"p218");p=good;p.inventory_local=1;Is(p,"inventory_local");
    for(auto id:{0u,2u,0xFFFFu}) {p=good;p.body_id=static_cast<std::uint16_t>(id);Is(p,"body_id");}
    p=good;p.pool=0xFF;Is(p,"pool");p=good;p.restriction=1;Is(p,"restriction");
    for(auto equip:{0u,0x800u,0xFFFFu}) {p=good;p.equip=static_cast<std::uint16_t>(equip);Is(p,"equip_range");}
    for(auto type:{0u,1u,3u,9u,13u,14u,17u}) {p=good;p.type=static_cast<std::uint8_t>(type);Is(p,"type");}
    for(auto sub:{8u,15u,255u}) {p=good;p.sub=static_cast<std::uint8_t>(sub);Is(p,"sub");}
    p=good;p.request_flags=0;Is(p,"request_flags");p=good;p.request_flags=20;Is(p,"request_flags");
    for(auto equip:{354u,349u,395u,411u,366u,370u,378u,388u}) {p=good;p.equip=static_cast<std::uint16_t>(equip);Is(p,nullptr);}
    world::TestArmLane();
    world::TestArmLaneRefusals();
    world::TestArmEquipPinned();
    std::printf("partner_support_equip_tests: %u checks (pure preconditions; the slot-6 writer on a fake world; no gameplay evidence)\n",checks);
}
