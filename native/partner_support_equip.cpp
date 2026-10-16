// Original body-1 support writer: slot 3 type 12 (thrown) and, WS4 v1, slot 3
// type 13 C4 391..395 and slot 6 type 14 arms 515..521 (ChangeEquip type 14 ->
// ctl vt+0x1F0 0x11B1B60: for a non-local id only ctl vt+0xA0(id, 6, equip) and
// ctl vt+0x98(id, 6, 0, equip), contract-pinned). Static contract: equip-audit.md.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <array>
#include <cstring>
#include "mgscoop/partner_support_equip.hpp"
#include "mgscoop/equipment_stock_writer_contract.hpp"
#include "partner_player_contract.hpp"
namespace mgscoop::partner_support_equip {
namespace {
namespace c=partner_player_contract;
namespace ec=equipment_stock_writer_contract;
SRWLOCK g_lock=SRWLOCK_INIT;
Context g_context{};
// The handle each lane allocated (0: slot 3, 1: slot 6).
std::uint32_t g_owned[2]{},g_owned_mission=0,g_owned_gen=0;
std::uintptr_t g_owned_player=0,g_owned_block=0;
bool g_uncertain=false;
unsigned g_lines=0,g_key_lines=0;
std::uint64_t g_log_ms=0;
const char* g_reason=nullptr;
bool Read(std::uintptr_t address,void* out,std::size_t size) noexcept {
    if(!address || address>UINTPTR_MAX-size) return false;
    SIZE_T copied=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),out,size,&copied) && copied==size;
}
struct Snapshot {
    Preconditions p{};
    std::uintptr_t ctl=0,inventory=0,engine=0,row=0,handles=0,metadata=0,pool_rows=0;
    std::uint32_t handle=0,pool_capacity=0,pool_cursor=0;
    std::array<std::uint16_t,8> supports{};
    std::array<std::uint8_t,0x3A> local_row{};
    std::array<std::uint32_t,7> local_handles{},remote_handles{};
    std::array<std::uint8_t,7*6> local_metadata{};
    std::array<std::uint8_t,c::vars_ammo_end-c::vars_ammo_begin> local_ammo{};
    std::uint16_t current=0,arm=0;
    std::uint8_t slot=0xFF;
};
bool Empty(std::uint32_t h) noexcept {return EmptyHandle(h);}
unsigned Lane(std::uint8_t slot) noexcept {return slot==6 ? 1u : 0u;}
const char* Capture(const Context& x,Snapshot& s) noexcept {
    s=Snapshot{};s.p.enabled=x.enabled;s.p.owner=x.owner;s.p.gates=x.gates;
    s.p.body_ready=x.body_ready;s.p.safety_ready=x.safety_ready;s.p.equip=x.equip;
    s.p.placed=x.placed;s.p.arm=x.arm;s.p.slot=x.slot;
    if(!x.enabled || !x.owner || !x.gates || !x.body_ready || !x.safety_ready) return Refusal(s.p);
    auto reader=Read;ec::View<decltype(reader)> v{reader,x.base,x.image_span};
    std::uintptr_t current=0,ids=0,pools=0,blocks=0,buffer=0,q=0,rows=0,vars=0;
    std::uint32_t count=0,body_count=0,body_base=0;
    if(!v.Image(c::player_slot_rva) || !v.At(x.base,c::player_slot_rva,current) || current!=x.player ||
       !v.Method(x.player,0x2295490,0x28,0x9CCB40) || !v.At(x.player,0x208,count) || count!=2 ||
       !v.At(x.player,0x204,s.p.player_flags) || !v.At(x.player,0x214,s.p.local) || !v.At(x.player,0x218,s.p.local_id) ||
       !v.At(x.player,0x70,ids) || !v.At(ids,2,s.p.body_id) || !v.At(x.player,0x78,pools) || !v.At(pools,1,s.p.pool)) return "player_lease";
    if(!(s.p.player_flags&1) || (s.p.player_flags&0x100)) return "offline";
    if(s.p.local==1) return "p214";
    if(s.p.local_id==1) return "p218";
    if(s.p.body_id!=1) return "body_id";
    if(s.p.pool==0xFF || s.p.pool>=2) return "pool";
    if(!v.At(x.player,0x50,blocks) || !v.At(x.player,0x378,buffer) || !buffer ||
       !v.At(blocks,std::size_t(s.p.pool)*8,current) || current!=x.remote_block || current!=buffer+std::size_t(s.p.pool)*0x6A8 ||
       !v.Method(x.remote_block,0x2295210,0x180,0x9C3B40) || !v.At(x.remote_block,8,body_count) || body_count!=1 ||
       !v.At(x.remote_block,0xC,body_base) || body_base!=1 || !v.At(x.remote_block,0x138,current) || current!=x.player) return "body_chain";
    std::uintptr_t predicate=0,node=0,pred_rows=0,writer=0;
    if(!v.At(x.remote_block,0x70,predicate) || !v.Method(predicate,0x23BC1B0,0x30,0x1156640) ||
       !v.At(predicate,0x168,node) || node!=x.remote_block || !v.At(predicate,0x1A8,pred_rows) ||
       !v.At(pred_rows,0,s.p.restriction) || !v.At(x.remote_block,0xF0,writer) ||
       !v.Method(writer,0x23C95D8,0x28,0x1209EC0) || !v.At(writer,8,current) || current!=x.remote_block) return "restriction_chain";
    if(s.p.restriction) return "restriction"; // 1156640's pure id-minus-base predicate
    if(!v.At(x.player,0xC8,s.ctl) || !v.Method(s.ctl,0x23C1720,0x198,0x11AC2E0) ||
       !v.Method(s.ctl,0x23C1720,0x10,0x11AFA80) || !v.Method(s.ctl,0x23C1720,0x18,0x11B0900) ||
       !v.Method(s.ctl,0x23C1720,0xA0,0x11B0700) || !v.Method(s.ctl,0x23C1720,0x98,0x11AFF30) ||
       !v.Method(s.ctl,0x23C1720,0x1F0,0x11B1B60) ||
       !v.At(s.ctl,0x18,current) || current!=x.player || !v.At(s.ctl,8,s.inventory)) return "equipment_interface";
    std::uintptr_t inv_vt=0;std::uint32_t stride=0,total=0;
    if(!v.At(s.inventory,0,inv_vt) || (inv_vt!=x.base+c::store_vt_rva && inv_vt!=x.base+c::store_vt_derived_rva) ||
       !v.At(inv_vt,8,current) || current!=x.base+0xEE9880 || !v.At(inv_vt,0x10,current) || current!=x.base+0xEE9D70 ||
       !v.At(s.inventory,0x30,s.p.inventory_local) || !v.At(s.inventory,0xC,stride) || stride!=7 ||
       !v.At(s.inventory,0x10,total) || total!=14 || !v.At(s.inventory,0x40,s.handles) || !s.handles ||
       !v.At(s.inventory,0x50,s.metadata) || !s.metadata || !v.At(s.inventory,0x58,s.engine) ||
       !v.Method(s.engine,0x234B840,0,0xDC3250) || !v.Method(s.engine,0x234B840,8,0xDC63A0) ||
       !v.Method(s.engine,0x234B840,0x10,0xDC2A50)) return "inventory_chain";
    if(s.p.inventory_local==1) return "inventory_local";
    if(!v.At(s.ctl,0x28,current) || current!=s.engine) return "controller_engine";
    if(!v.At(x.player,0x60,q) || !v.At(q,0,current) || current!=x.base+c::q_vtable_rva ||
       !v.At(q,0xC0,rows) || !rows || !v.At(rows,0,s.local_row) ||
       !v.At(s.handles,0,s.local_handles) || !v.At(s.handles,7*4,s.remote_handles) ||
       !v.At(s.metadata,0,s.local_metadata) || !v.At(x.base,c::vars_slot_rva,vars) || !vars ||
       !v.At(s.ctl,0x20,current) || current!=vars ||
       !Read(vars+c::vars_ammo_begin,s.local_ammo.data(),s.local_ammo.size())) return "local_snapshot";
    s.row=rows+0x3A;s.handle=s.remote_handles[x.slot==6 ? 6 : 3];
    std::uint8_t slot=0,sub=0;
    if(!v.At(s.row,0,s.current) || !v.At(s.row,0x36,slot) || !v.At(s.row,0x37,sub) ||
       !v.At(s.row,0xC,s.supports) || !v.At(s.row,8,s.arm)) return "equipment_row";
    s.slot=slot&0xF;s.p.sub=sub&0xF;
    // Packed descriptors are read, never the type getter (or its alias path).
    if(!x.equip || x.equip>=0x800) return "equip_range";
    std::uint32_t index=x.equip;
    if(index>=0x600) index-=c::types_bias_hi;
    else if(index>=0x400 && index<0x450) index-=c::types_bias_mid;
    else if(index>=0x230) return "type_index";
    std::uint16_t packed=0;std::uintptr_t types=0;
    if(index>=c::stock_ids || !v.At(s.engine,8,types) || types!=x.base+c::equip_types_rva ||
       !v.Method(types,c::equip_types_vt_rva,0,0xA2A3E0) || !v.At(types,8+std::size_t(index)*2,packed)) return "type_table";
    s.p.type=static_cast<std::uint8_t>(packed&0x3F);
    if(const char* why=Refusal(s.p)) return why;
    // The lower writer resolves its engine via this exact global chain.
    std::uintptr_t root=0,global=0,services=0,owner_ctl=0,model=0;
    if(!v.At(x.base,c::service_root_rva,root) || !v.At(root,8,global) || !v.At(global,0x98,services) ||
       !v.At(services,0x1E8,current) || current!=s.engine || !v.At(services,0x1A0,owner_ctl) ||
       !v.Method(owner_ctl,0x25564F0,0x208,0x1E02AD0) || !v.Method(owner_ctl,0x25564F0,0x1F8,0x1E02B40) ||
       !v.At(x.remote_block,0x618,model) || !v.Method(model,0x22EA550,0x20,0xB0C720)) return "native_owner_chain";
    if(!v.At(s.engine,0x68,s.pool_capacity) || !v.At(s.engine,0x6C,s.pool_cursor) || !s.pool_capacity ||
       s.pool_capacity>0xFFFF || s.pool_cursor>s.pool_capacity || !v.At(s.engine,0x140,s.pool_rows) || !s.pool_rows) return "pool_bounds";
    return nullptr;
}
MatchIn MatchView(const Snapshot& s) noexcept {
    MatchIn m{};m.slot=s.p.slot;m.type=s.p.type;m.equip=s.p.equip;m.handle=s.handle;m.pool_capacity=s.pool_capacity;
    m.row_slot=s.slot;m.sub=s.p.sub;m.current=s.current;m.arm=s.arm;
    for(unsigned i=0;i<8;++i) m.supports[i]=s.supports[i];
    // The manager row only for a non-empty in-range handle (MatchRefusal refuses the rest first).
    if(!Empty(s.handle) && (s.handle>>16)<s.pool_capacity) {
        std::array<std::uint8_t,6> meta{};
        if(Read(s.pool_rows+std::size_t(s.handle>>16)*6,meta.data(),meta.size())) {
            m.meta_ok=true;m.meta_equip=ec::Word16(meta.data());m.meta_owner=ec::Word16(meta.data()+2);m.meta_flags=meta[4];
        }
    }
    return m;
}
const char* Match(const Snapshot& s,bool active) noexcept {return MatchRefusal(MatchView(s),active);}
// The viewer's row, handles, metadata and ammo vars, and body 1's handles of
// every slot except the lane's own (3, or 6 for an arm).
bool SameLocal(const Snapshot& a,const Snapshot& b) noexcept {
    if(a.local_row!=b.local_row || a.local_handles!=b.local_handles || a.local_metadata!=b.local_metadata || a.local_ammo!=b.local_ammo) return false;
    const unsigned lane=a.p.slot==6 ? 6u : 3u;
    for(unsigned i=0;i<7;++i) if(i!=lane && a.remote_handles[i]!=b.remote_handles[i]) return false;
    return true;
}
bool IdleReason(const char* reason) noexcept {
    return reason && (!std::strcmp(reason,"slot") || !std::strcmp(reason,"off") || !std::strcmp(reason,"admission") ||
                      !std::strcmp(reason,"body_busy"));
}
// Idle reasons (the normal not-holding-a-grenade states) log only when the
// reason changes, under their own small cap; equip/follow outcomes and every
// other refusal keep a separate cap so they are never crowded out.
void Receipt(LogFn log,const Context& x,const Snapshot& s,const char* phase,const char* reason,std::int32_t result=INT32_MIN) noexcept {
    if(!log) return;
    const bool idle=std::strcmp(phase,"refused")==0 && IdleReason(reason);
    const bool repeated=g_reason && reason && std::strcmp(g_reason,reason)==0;
    if(idle && repeated) return;
    if(!idle && repeated && x.now_ms>=g_log_ms && x.now_ms-g_log_ms<1000) return;
    unsigned& lines=idle ? g_lines : g_key_lines;
    if(lines>=(idle ? 64u : 512u)) return;
    g_log_ms=x.now_ms;g_reason=reason;++lines;
    char line[448]{};
    StringCchPrintfA(line,ARRAYSIZE(line),
        "partner_support=%s; slot=%u; equip=%u; type=%u; reason=%s; instance=1; handle=%08x; body_slot=%u; sub=%u; result=%d; mission=%u; gen=%u; rec_handle_risk=pinned\r\n",
        phase,unsigned(x.slot),unsigned(x.equip),unsigned(s.p.type),reason ? reason : "ok",s.handle,unsigned(s.slot),unsigned(s.p.sub),result,x.mission,x.realize_gen);
    log(line);
}
#pragma pack(push,1)
struct Request {std::uint64_t type=0x326C48DA30C0ull;std::uint16_t equip=0,stock=0xFFFF,stock_sub=0,ammo=0xFFFF,ammo_sub=0;std::uint8_t suppression=0,flags=4;};
#pragma pack(pop)
static_assert(sizeof(Request)==20 && offsetof(Request,flags)==19);
bool Dispatch(std::uintptr_t function,std::uintptr_t ctl,const Request* request,std::int32_t* result) noexcept {
    using Fn=std::int32_t* (*)(void*,std::int32_t*,std::uint32_t,const Request*);
    __try {return reinterpret_cast<Fn>(function)(reinterpret_cast<void*>(ctl),result,1,request)==result;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// SetCurrentSlot(ctl, id 1, slot, sub): case 3 row+0 = row[6+sub] then ctl
// vt+0xA0; case 6 row+0 = row+8 (no call); the rest is local-only for id 1.
bool SetSlot(std::uintptr_t function,std::uintptr_t ctl,std::uint8_t slot,std::uint8_t sub) noexcept {
    using Fn=void (*)(void*,std::uint32_t,std::int32_t,std::uint8_t,char,char,int);
    __try {reinterpret_cast<Fn>(function)(reinterpret_cast<void*>(ctl),1,slot,sub,0,0,0);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool ContextFor(std::uintptr_t base,std::uintptr_t player,std::uintptr_t block,std::uint16_t equip,std::uint8_t slot,Context& out) noexcept {
    out=g_context;
    return out.enabled && out.owner && out.gates && out.body_ready && out.safety_ready && !g_uncertain &&
        out.base==base && out.player==player && out.remote_block==block && LaneSlot(slot,out.arm) && out.slot==slot && out.equip==equip;
}
}
void Observe(const Context& x,LogFn log) noexcept {
    if(!TryAcquireSRWLockExclusive(&g_lock)) return;
    g_context=x;
    // The uncertain latch is mission-scoped (review-1ca3ce6): a re-realize of
    // the body must not re-arm a writer whose last call may have touched the
    // viewer's state. Ownership of the allocated handle follows the body.
    if(g_owned_mission!=x.mission) {g_uncertain=false;g_lines=0;g_key_lines=0;g_reason=nullptr;}
    if(g_owned_mission!=x.mission || (x.player && x.remote_block &&
       (g_owned_gen!=x.realize_gen || g_owned_player!=x.player || g_owned_block!=x.remote_block))) {
        g_owned[0]=g_owned[1]=0;
        g_owned_mission=x.mission;g_owned_gen=x.realize_gen;g_owned_player=x.player;g_owned_block=x.remote_block;
    }
    if(!x.enabled) {ReleaseSRWLockExclusive(&g_lock);return;}
    // Slot 3 always; slot 6 only with the arm key (else the old idle "slot").
    Snapshot before{};const char* why=!LaneSlot(x.slot,x.arm) ? "slot" : Capture(x,before);
    const unsigned lane=Lane(x.slot);
    if(!why && g_uncertain) why="uncertain_latched";
    if(!why && !Match(before,false)) {
        // Already allocated for this exact remote equip. Let the drain's
        // native slot follower activate it instead of re-dispatching.
        ReleaseSRWLockExclusive(&g_lock);return;
    }
    if(!why && x.body_busy) why="body_busy";
    if(!why && !Empty(before.handle) && before.handle!=g_owned[lane]) why="foreign_handle";
    if(!why && !Empty(before.handle)) {
        std::array<std::uint8_t,6> meta{};
        if(!Read(before.pool_rows+std::size_t(before.handle>>16)*6,meta.data(),meta.size()) || (meta[5]&1u)) why="release_helper_unqualified";
        else if(ec::Word16(meta.data()+2)!=1) why="prior_handle_owner";
    }
    if(!why && before.pool_capacity-before.pool_cursor<3) why="pool_margin";
    std::uintptr_t mismatch=0;
    if(!why && !(x.pins_intact && x.pins_intact(x.base,x.image_span,mismatch))) why="full_body_gate";
    Snapshot late{};
    if(!why) why=Capture(x,late);
    if(!why && (before.ctl!=late.ctl || before.inventory!=late.inventory || before.engine!=late.engine || before.row!=late.row ||
       before.handles!=late.handles || before.handle!=late.handle || before.pool_cursor!=late.pool_cursor || !SameLocal(before,late))) why="lease_changed";
    std::int32_t result=INT32_MIN;
    if(!why) {
        Request request{};request.equip=x.equip;
        const bool called=Dispatch(x.base+0x11AC2E0,late.ctl,&request,&result);
        Snapshot after{};const char* after_why=Capture(x,after);
        if(!called || result!=0) why="dispatch_result";
        else if(after_why) why=after_why;
        else if(!SameLocal(late,after)) why="local_or_other_slot_changed";
        else why=Match(after,true);
        if(why) {g_uncertain=true;g_context.safety_ready=false;}
        else {g_owned[lane]=after.handle;before=after;}
    }
    if(why) g_context.safety_ready=false;
    Receipt(log,x,before,why ? "refused" : "equip",why ? why : "ok",result);
    ReleaseSRWLockExclusive(&g_lock);
}
bool Ready(std::uintptr_t base,std::uintptr_t player,std::uintptr_t block,std::uint16_t equip,std::uint8_t* decoded_type,
           std::uint32_t* handle,std::uint8_t slot) noexcept {
    if(decoded_type) *decoded_type=0;
    if(handle) *handle=0;
    if(!TryAcquireSRWLockShared(&g_lock)) return false;
    Context x{};Snapshot s{};
    const bool ready=ContextFor(base,player,block,equip,slot,x) && !Capture(x,s) && !Match(s,true);
    if(decoded_type) *decoded_type=s.p.type;
    // The Match-verified lane handle Y+0x40[1*7+slot]: owner 1, this equip,
    // kind = its type (12/13 slot 3, 14 slot 6), index below the manager's
    // capacity (WS1 B).
    if(handle && ready) *handle=s.handle;
    ReleaseSRWLockShared(&g_lock);return ready;
}
bool Uncertain() noexcept {
    if(!TryAcquireSRWLockShared(&g_lock)) return false;
    const bool uncertain=g_uncertain;
    ReleaseSRWLockShared(&g_lock);return uncertain;
}
const char* Follow(std::uintptr_t base,std::size_t span,std::uintptr_t player,std::uintptr_t block,std::uint16_t equip,LogFn log,
                   std::uint8_t slot) noexcept {
    if(!TryAcquireSRWLockExclusive(&g_lock)) return "busy";
    Context x{};Snapshot before{};
    const char* why=ContextFor(base,player,block,equip,slot,x) ? nullptr : "admission";
    if(!why && span!=x.image_span) why="image_span";
    if(!why) why=Capture(x,before);
    if(!why) why=Match(before,false);
    if(!why && AlreadyCurrent(MatchView(before))) {
        ReleaseSRWLockExclusive(&g_lock);return nullptr;
    }
    std::uintptr_t mismatch=0;
    if(!why && !(x.pins_intact && x.pins_intact(base,span,mismatch))) why="full_body_gate";
    Snapshot late{};if(!why) why=Capture(x,late);
    if(!why && (before.handle!=late.handle || before.row!=late.row || before.ctl!=late.ctl || !SameLocal(before,late))) why="lease_changed";
    if(!why) {
        // Slot 3: the sub holding the equip; slot 6 (an arm): sub 0, as ChangeEquip's own activation.
        std::uint8_t sub=0;
        if(slot!=6) while(sub<8 && late.supports[sub]!=equip) ++sub;
        if(sub>=8) why="support_list";
        else {
            const bool called=SetSlot(base+0x11AFA80,late.ctl,slot==6 ? 6 : 3,sub);
            Snapshot after{};const char* after_why=Capture(x,after);
            why=!called ? "follow_fault" : after_why ? after_why : !SameLocal(late,after) ? "local_or_other_slot_changed" : Match(after,true);
            if(why) {g_uncertain=true;g_context.safety_ready=false;}
            else before=after;
        }
    }
    Receipt(log,x,before,why ? "refused" : "follow",why ? why : "ok");
    ReleaseSRWLockExclusive(&g_lock);return why;
}
}
