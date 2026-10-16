#include "mgscoop/partner_support_policy.hpp"
#include "mgscoop/partner_support_equip.hpp"
#include "mgscoop/partner_support_guard.hpp"
#include "mgscoop/partner_fire_filter.hpp"
#include "mgscoop/partner_slots.hpp"
#include <cstdio>
#include <initializer_list>
#include <cstring>
namespace sp=mgscoop::partner_support_policy;
namespace se=mgscoop::partner_support_equip;
namespace sg=mgscoop::partner_support_guard;
namespace sf=mgscoop::partner_fire_filter;
namespace slots=mgscoop::partner_slots;
// WS1 B/C fixtures: a fake gun prim, gun record and handle manager X in local
// memory, read through a bounds-checked reader (an out-of-range read is an
// unread, as a fault is in the hook). Synthetic only; no game code or memory.
namespace fake {
namespace k=sp::rec240;
constexpr std::uintptr_t kBase=0x140000000ull,kBlk=0x7ff012340000ull;
constexpr std::size_t kPrim=0x0,kRec=0x200,kEngine=0x1000,kRows=0x2000,kSize=0x3000;
alignas(16) unsigned char mem[kSize];
std::uintptr_t At(std::size_t off) noexcept {return reinterpret_cast<std::uintptr_t>(mem)+off;}
bool Read(std::uintptr_t address,void* out,std::size_t size) noexcept {
 const auto lo=reinterpret_cast<std::uintptr_t>(mem);
 if(address<lo || address-lo>kSize || size>kSize-(address-lo)) return false;
 std::memcpy(out,reinterpret_cast<const void*>(address),size);return true;
}
template<class T> void Put(std::size_t off,T v) noexcept {std::memcpy(mem+off,&v,sizeof(v));}
constexpr std::uint32_t Handle(std::uint32_t idx,std::uint32_t equip,std::uint32_t kind=12) noexcept {return (idx<<16)|(equip<<5)|kind;}
// Body 1 holds grenade 354 in hand slot 3, class 2, handle index 5 owned by 1;
// row 4 is the viewer's (equip 12, owner 0).
void Reset(std::uint32_t handle=Handle(5,354)) noexcept {
 std::memset(mem,0,sizeof(mem));
 Put<std::uintptr_t>(kPrim,kBase+k::kPrimVtRva);Put<std::uintptr_t>(kPrim+k::kPrimBlk,kBlk);
 Put<std::uintptr_t>(kPrim+k::kPrimRec,At(kRec));Put<std::uintptr_t>(kPrim+k::kPrimEngine,At(kEngine));
 Put<std::uint16_t>(kRec+k::kRecEquip,354);Put<std::uint8_t>(kRec+k::kRecSlotHand,3);Put<std::uint32_t>(kRec+k::kRecFlags,0x20);
 Put<std::uint32_t>(kRec+k::kRecHandle,handle);Put<std::uint32_t>(kRec+k::kRecClassWord,(2u<<6)|0x1Fu|0x800u);
 Put<std::uintptr_t>(kEngine,kBase+k::kEngineVtRva);Put<std::uint32_t>(kEngine+k::kEngineCapacity,64);
 Put<std::uintptr_t>(kEngine+k::kEngineRows,At(kRows));
 Put<std::uint16_t>(kRows+5*k::kRowStride+k::kRowEquip,354);Put<std::uint16_t>(kRows+5*k::kRowStride+k::kRowOwner,1);
 Put<std::uint8_t>(kRows+5*k::kRowStride+4,0x80); // the pin bit (vt+0x160) is not part of the check
 Put<std::uint16_t>(kRows+4*k::kRowStride+k::kRowEquip,12);Put<std::uint16_t>(kRows+4*k::kRowStride+k::kRowOwner,0);
}
sp::RecordIn Record(std::uint16_t equip=354) noexcept {return sp::ReadRecord(Read,kBase,At(kPrim),At(kRec),kBlk,equip);}
sp::Rec240Row Row(std::uint16_t equip=354) noexcept {return sp::CheckRow(Read,kBase,At(kPrim),Record(equip));}
}
template<class Check> void Rec240Fixtures(Check& check) {
 using sp::Rec240;
 static_assert(!sp::kRec240Latch,"WS1 C starts detect-only (log, no latch) until its hidden run");
 // --- B: record_ready also requires rec+0x240 == the Match-verified slot-3 handle.
 sp::RecordIn r{};r.chain=true;r.switching=false;r.rec_equip=r.equip=354;r.slot_hand=3;r.cls=2;
 r.rec_handle=r.slot_handle=fake::Handle(5,354);
 check(sp::RecordIdentity(r) && sp::RecordReady(r),"B: matching rec+0x240 is record ready");
 {auto x=r;x.rec_handle=fake::Handle(4,354);check(sp::RecordIdentity(x) && !sp::RecordReady(x),"B: rec+0x240 on another index refuses");}
 {auto x=r;x.rec_handle=fake::Handle(5,349);check(!sp::RecordReady(x),"B: rec+0x240 of another equip refuses");}
 {auto x=r;x.rec_handle=fake::Handle(5,354,13);check(!sp::RecordReady(x),"B: rec+0x240 of another kind refuses");}
 {auto x=r;x.slot_handle=0;check(!sp::RecordReady(x),"B: no Match-verified handle (Ready false) refuses");}
 {auto x=r;x.rec_handle=0;x.slot_handle=0;check(!sp::RecordReady(x),"B: two empty handles never match");}
 {auto x=r;x.chain=false;check(!sp::RecordIdentity(x) && !sp::RecordReady(x),"B: broken prim chain refuses");}
 {auto x=r;x.rec_equip=349;check(!sp::RecordIdentity(x) && !sp::RecordReady(x),"B: record still on the previous equip refuses");}
 {auto x=r;x.slot_hand=0;check(!sp::RecordIdentity(x),"B: record not in hand slot 3 refuses");}
 {auto x=r;x.switching=true;check(!sp::RecordIdentity(x),"B: switching record refuses");}
 for(unsigned equip: {0u,0x800u,0xFFFFu}){auto x=r;x.equip=x.rec_equip=static_cast<std::uint16_t>(equip);check(!sp::RecordIdentity(x),"B: equip range");}
 // The composed gate: a mismatching rec+0x240 alone closes support admission.
 {sp::GateIn g{true,false,true,true,true,true,true,true,true,true,3,12,354};
  auto x=r;x.rec_handle=fake::Handle(4,354);g.record_ready=sp::RecordReady(x);check(!sp::Admit(g),"B: Admit refuses a foreign rec+0x240");
  g.record_ready=sp::RecordReady(r);check(sp::Admit(g),"B: Admit passes the matching rec+0x240");}
 // ReadRecord reads the pinned offsets through the reader only.
 fake::Reset();
 {const auto x=fake::Record();
  check(x.chain && !x.switching && x.rec_equip==354 && x.slot_hand==3 && x.cls==2 && x.rec_handle==fake::Handle(5,354),"B: ReadRecord fields");}
 {fake::Put<std::uintptr_t>(fake::kPrim,fake::kBase+0x23B3F68);check(!fake::Record().chain,"B: wrong prim vtable");fake::Reset();}
 {const auto x=sp::ReadRecord(fake::Read,fake::kBase,fake::At(fake::kPrim),fake::At(fake::kRec),fake::kBlk+0x6A8,354);check(!x.chain,"B: prim of another block");}
 {const auto x=sp::ReadRecord(fake::Read,fake::kBase,fake::At(fake::kPrim),fake::At(fake::kRec)+0x610,fake::kBlk,354);check(!x.chain,"B: another record");}
 {const auto x=sp::ReadRecord(fake::Read,fake::kBase,0,fake::At(fake::kRec),fake::kBlk,354);check(!x.chain,"B: no prim");}
 {fake::Put<std::uint32_t>(fake::kRec+fake::k::kRecFlags,0xA0);check(fake::Record().switching,"B: switch bit 0x80 read");fake::Reset();}
 {const auto x=sp::ReadRecord([](std::uintptr_t,void*,std::size_t) noexcept {return false;},fake::kBase,fake::At(fake::kPrim),fake::At(fake::kRec),fake::kBlk,354);
  check(!x.chain && !sp::RecordReady(x),"B: unread memory is never ready");}
 // --- C: the per-job row check on the fake manager tables.
 fake::Reset();
 {const auto row=fake::Row();check(row.verdict==Rec240::ok && row.idx==5 && row.capacity==64 && row.row_equip==354 && row.row_owner==1,"C: own row ok (pin bit ignored)");
  check(!sp::Rec240Foreign(row.verdict) && !sp::Rec240Latches(row.verdict,true),"C: ok never latches");}
 for(unsigned cls=0;cls<8;++cls){fake::Reset();fake::Put<std::uint32_t>(fake::kRec+fake::k::kRecClassWord,(cls<<6)|0x3Fu);
  const auto row=fake::Row();check((row.verdict!=Rec240::skip)==(cls==2 || cls==3),"C: judged only for support classes 2/3 (iface reset gate)");}
 // A stale handle (index 4, equip 354) whose row is now the viewer's equip 12.
 fake::Reset(fake::Handle(4,354));check(fake::Row(354).verdict==Rec240::equip,"C: stale handle on the viewer's row (generation word)");
 // Mid-switch (critic #13): the remote is already on another equip, or the switch bit is set: not judged.
 check(fake::Row(349).verdict==Rec240::skip,"C: remote already on another equip (activation pending) is not judged");
 fake::Reset();fake::Put<std::uint32_t>(fake::kRec+fake::k::kRecFlags,0x80);check(fake::Row().verdict==Rec240::skip,"C: switching record is not judged");
 fake::Reset();fake::Put<std::uint8_t>(fake::kRec+fake::k::kRecSlotHand,0);check(fake::Row().verdict==Rec240::skip,"C: record not in hand slot 3 is not judged");
 fake::Reset();fake::Put<std::uintptr_t>(fake::kPrim+fake::k::kPrimBlk,0);check(fake::Row().verdict==Rec240::skip,"C: broken prim chain is not judged");
 // Foreign rows: owner, generation (equip) word, range, empty.
 fake::Reset(fake::Handle(4,354));fake::Put<std::uint16_t>(fake::kRows+4*fake::k::kRowStride,354);
 {const auto row=fake::Row();check(row.verdict==Rec240::owner && row.row_owner==0,"C: viewer-owned row with the same equip word is foreign (owner)");}
 fake::Reset();fake::Put<std::uint16_t>(fake::kRows+5*fake::k::kRowStride+fake::k::kRowOwner,2);check(fake::Row().verdict==Rec240::owner,"C: owner 2 is foreign");
 fake::Reset();fake::Put<std::uint16_t>(fake::kRows+5*fake::k::kRowStride,349);check(fake::Row().verdict==Rec240::equip,"C: row reused for another equip (generation word)");
 fake::Reset(fake::Handle(64,354));check(fake::Row().verdict==Rec240::range,"C: index == capacity is out of range");
 fake::Reset(fake::Handle(63,354));fake::Put<std::uint16_t>(fake::kRows+63*fake::k::kRowStride,354);fake::Put<std::uint16_t>(fake::kRows+63*fake::k::kRowStride+2,1);
 check(fake::Row().verdict==Rec240::ok,"C: last in-range index ok");
 fake::Reset(fake::Handle(0xFFFF,354));check(fake::Row().verdict==Rec240::empty,"C: empty index 0xFFFF");
 fake::Reset(0);check(fake::Row().verdict==Rec240::empty,"C: zero handle");
 fake::Reset(fake::Handle(5,0));check(fake::Row().verdict==Rec240::empty,"C: handle without an equip");
 // Unread manager chain or row: never a foreign verdict, never a latch.
 fake::Reset();fake::Put<std::uintptr_t>(fake::kEngine,fake::kBase+0x234B470);check(fake::Row().verdict==Rec240::unread,"C: sibling manager vtable is not the qualified class");
 fake::Reset();fake::Put<std::uintptr_t>(fake::kPrim+fake::k::kPrimEngine,0);check(fake::Row().verdict==Rec240::unread,"C: no manager");
 fake::Reset();fake::Put<std::uint32_t>(fake::kEngine+fake::k::kEngineCapacity,0);check(fake::Row().verdict==Rec240::unread,"C: zero capacity");
 fake::Reset();fake::Put<std::uint32_t>(fake::kEngine+fake::k::kEngineCapacity,0x10000);check(fake::Row().verdict==Rec240::unread,"C: capacity above 16 bits");
 fake::Reset();fake::Put<std::uintptr_t>(fake::kEngine+fake::k::kEngineRows,0);check(fake::Row().verdict==Rec240::unread,"C: no row table");
 fake::Reset(fake::Handle(0x400,354));fake::Put<std::uint32_t>(fake::kEngine+fake::k::kEngineCapacity,0xFFFF);
 {const auto row=fake::Row();check(row.verdict==Rec240::unread && !sp::Rec240Latches(row.verdict,true),"C: unreadable row is unread, never a latch");}
 // Detect-only first: the default never latches; the latch mode latches every foreign verdict only.
 for(Rec240 v: {Rec240::skip,Rec240::ok,Rec240::unread,Rec240::empty,Rec240::range,Rec240::equip,Rec240::owner}){
  const bool foreign=v==Rec240::empty || v==Rec240::range || v==Rec240::equip || v==Rec240::owner;
  check(sp::Rec240Foreign(v)==foreign,"C: foreign verdict set");
  check(!sp::Rec240Latches(v,sp::kRec240Latch),"C: detect-only default never latches");
  check(sp::Rec240Latches(v,true)==foreign,"C: latch mode latches exactly the foreign verdicts");}
 check(std::strcmp(sp::Rec240Name(Rec240::owner),"owner")==0 && std::strcmp(sp::Rec240Name(Rec240::ok),"ok")==0 &&
       std::strcmp(sp::Rec240Name(Rec240::empty),"empty")==0 && std::strcmp(sp::Rec240Name(Rec240::unread),"unread")==0,"C: verdict names");
}
int main(){
 unsigned checks=0,fail=0;
 const auto check=[&](bool v,const char* label){++checks;if(!v){++fail;std::printf("FAIL %s\n",label);}};
 sp::GateIn good{true,false,true,true,true,true,true,true,true,true,3,12,354};
 check(sp::Admit(good),"grenade admitted");
 {auto x=good;x.live=false;check(!sp::Admit(x),"non-live refused");x=good;x.stale=true;check(!sp::Admit(x),"stale refused");x=good;x.gun_valid=false;check(!sp::Admit(x),"missing gun group refused");x=good;x.p218_window=true;check(!sp::Admit(x),"implicit required pose p218 window refused");}
 check((sp::Strip(0,true,true,true)&0x8058u)==0x8040u,"support pad keeps hold/fire and strips action/cqc/reload");
 check((sp::Strip(0,true,true,false)&0x8078u)==0x8078u,"refused support pad strips action/cqc/hold/fire/reload");
 for(unsigned slot=0;slot<16;++slot)for(unsigned type=0;type<32;++type){auto x=good;x.slot=static_cast<std::uint8_t>(slot);x.type=static_cast<std::uint8_t>(type);
  check(sp::Admit(x)==(slot==3 && type==12),"slot/type matrix incl placed and arms");}
 for(unsigned i=0;i<10;++i){auto x=good;switch(i){case 0:x.enabled=false;break;case 1:x.latched=true;break;case 2:x.guard_installed=false;break;case 3:x.guard_armed=false;break;case 4:x.filter_live=false;break;case 5:x.filter_armed=false;break;case 6:x.slot_valid=false;break;case 7:x.equip_valid=false;break;case 8:x.body_ready=false;break;default:x.record_ready=false;}
  check(!sp::Admit(x),"missing safety owner refuses");}
 for(unsigned equip: {0u,0x800u,0xFFFFu}){auto x=good;x.equip=static_cast<std::uint16_t>(equip);check(!sp::Admit(x),"equip range");}
 for(unsigned id=0;id<4;++id)for(unsigned cls=0;cls<8;++cls){
  const bool expected=id==1 && (cls==2 || cls==3);
  check(sg::detail::ShouldForce(id,cls,0x1234,0x1234,true)==expected,"guard policy matrix (no window/thread input)");}
 check(!sg::detail::ShouldForce(1,2,0x1234,0x5678,true),"wrong P");
 check(!sg::detail::ShouldForce(1,2,0,0,true),"unarmed P");
 check(!sg::detail::ShouldForce(1,2,0x1234,0x1234,false),"key off");
 // Key off is Round 1: no frame is a support request, whatever the slot or strip.
 for(unsigned slot=0;slot<16;++slot)for(unsigned valid=0;valid<2;++valid)for(std::uint32_t strip: {0u,0x40u,0x50u,0x58u}){
  check(!sp::Request(false,valid!=0,static_cast<std::uint8_t>(slot),strip),"key off never requests support");
  check(sp::Request(true,valid!=0,static_cast<std::uint8_t>(slot),strip)==((valid && slot>2) || strip==0x40u),"key on request matrix");}
 // Key off and no request: Strip returns the driver's Round 1 mask unchanged (incl. unsupported=0's empty mask).
 for(std::uint32_t mask: {0u,0x10u,0x50u,0x58u})check(sp::Strip(mask,false,false,false)==mask,"key off strip is the Round 1 mask");
 for(unsigned mode=0;mode<256;++mode)for(unsigned sw=0;sw<2;++sw){
  const bool busy=sw || mode==0xFF || mode==8 || mode==11 || mode==12 || (mode>=28 && mode<=42);
  check(sp::BodyBusy(static_cast<std::uint8_t>(mode),sw!=0)==busy,"body busy matrix");}
 for(unsigned owner: {0u,1u,2u,0x201u,0xFFFFu})for(unsigned preview=0;preview<2;++preview){
  (void)preview;check(sf::detail::Drops(1,static_cast<std::uint16_t>(owner))==(owner==1),"full owner incl preview, zero and soldiers");}
 se::Preconditions p{};p.enabled=p.owner=p.gates=p.body_ready=p.safety_ready=true;p.player_flags=1;p.local=0;p.local_id=0;p.inventory_local=0;p.body_id=1;p.equip=354;p.pool=2;p.type=12;p.sub=0;p.restriction=0;
 check(se::Refusal(p)==nullptr,"equip admitted");
 for(unsigned i=0;i<20;++i){auto q=p;const char* reason="";switch(i){
 case 0:q.enabled=false;reason="off";break;case 1:q.owner=false;reason="thread";break;case 2:q.gates=false;reason="admission";break;
 case 3:q.body_ready=false;reason="body";break;case 4:q.safety_ready=false;reason="safety";break;case 5:q.player_flags=0;reason="offline";break;
 case 6:q.player_flags=0x101;reason="offline";break;case 7:q.local=1;reason="p214";break;case 8:q.local_id=1;reason="p218";break;
 case 9:q.inventory_local=1;reason="inventory_local";break;case 10:q.body_id=0;reason="body_id";break;case 11:q.pool=0xFF;reason="pool";break;
 case 12:q.restriction=1;reason="restriction";break;case 13:q.equip=0;reason="equip_range";break;case 14:q.equip=0x800;reason="equip_range";break;
 case 15:q.type=13;reason="type";break;case 16:q.type=14;reason="type";break;case 17:q.sub=8;reason="sub";break;
 case 18:q.request_flags=0;reason="request_flags";break;default:q.request_flags=20;reason="request_flags";}
 const auto why=se::Refusal(q);check(why && std::strcmp(why,reason)==0,"equip refusal");}
 sp::StockLatch latch{};check(!sp::CheckStock(latch,true,true,12,12)&&!latch.off,"stock unchanged");
 check(!sp::CheckStock(latch,false,true,0,12)&&!latch.off,"unread stock is not changed evidence");
 check(sp::CheckStock(latch,true,true,12,11)&&latch.off,"stock change latches");
 check(!sp::CheckStock(latch,true,true,12,12)&&latch.off,"latch persists");
 check(slots::StripBits(slots::Gate::support)==0x40,"support permits HOLD/FIRE only");
 check(slots::StripBits(slots::Gate::unsupported)==0x58,"placed/arms unsupported mask");
 slots::AimGateIn aim{};aim.enabled=aim.judged=aim.slot_valid=true;aim.gate=slots::Gate::support;aim.slot=3;
 check(slots::AimGate(aim)==slots::AimRefusal::none,"support toggle aim");aim.slot=6;check(slots::AimGate(aim)==slots::AimRefusal::slot,"arms no aim");
 Rec240Fixtures(check);
 std::printf("%u support policy checks, %u failures\n",checks,fail);return fail?1:0;
}
