#include "mgscoop/partner_support_policy.hpp"
#include "mgscoop/partner_support_equip.hpp"
#include "mgscoop/partner_support_guard.hpp"
#include "mgscoop/partner_fire_filter.hpp"
#include "mgscoop/partner_slots.hpp"
#include "mgscoop/partner_input.hpp"
#include <cstdio>
#include <initializer_list>
#include <cstring>
#include <utility>
namespace sp=mgscoop::partner_support_policy;
namespace se=mgscoop::partner_support_equip;
namespace sg=mgscoop::partner_support_guard;
namespace sf=mgscoop::partner_fire_filter;
namespace slots=mgscoop::partner_slots;
namespace pi=mgscoop::partner_input;
using mgscoop::PartnerInput;
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
 static_assert(sp::kRec240Latch,"WS1 C latches a foreign row (flipped after the hidden runs showed no false positive)");
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
 // rec240_stale (review WS1-1): any rec+0x240 other than the slot handle under
 // the identity (in practice a same-equip handle on another index); never the
 // gap of a normal equip/follow.
 check(!sp::RecordStale(r),"B: matching rec+0x240 is not stale");
 {auto x=r;x.rec_handle=fake::Handle(4,354);check(sp::RecordStale(x) && !sp::RecordReady(x),"B: same equip on another index is stale (refused)");}
 for(std::uint32_t h: {fake::Handle(5,349),fake::Handle(5,354,13),0u}){auto x=r;x.rec_handle=h;
  check(sp::RecordStale(x) && !sp::RecordReady(x),"B: any other rec+0x240 under the identity is stale (refused)");}
 {auto x=r;x.slot_handle=0;x.rec_handle=fake::Handle(4,354);check(!sp::RecordStale(x),"B: no Match-verified handle is not judged stale");}
 {auto x=r;x.switching=true;x.rec_handle=fake::Handle(4,354);check(!sp::RecordStale(x),"B: switching record is not judged stale");}
 // The activation 0x105FDB0 after an equip 349 -> 354: rec+0x240 (0x10602E8) is
 // stored before rec+0x24C (0x10602F0). No state of that pass, or of a follow
 // from a gun slot (rec+0x3D9 != 3 until 0x105FE5E), is stale.
 {auto x=r;x.rec_equip=349;x.rec_handle=fake::Handle(7,349);
  check(!sp::RecordStale(x) && !sp::RecordReady(x),"B: before the activation (previous equip) is not stale");
  x.rec_handle=r.slot_handle;check(!sp::RecordStale(x) && !sp::RecordReady(x),"B: after the rec+0x240 store only is not stale");
  x.rec_equip=354;check(!sp::RecordStale(x) && sp::RecordReady(x),"B: after the rec+0x24C store is ready");}
 {auto x=r;x.slot_hand=0;x.rec_equip=0x2D;x.rec_handle=fake::Handle(2,0x2D,1);check(!sp::RecordStale(x),"B: follow from a gun slot is not stale");}
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
 // The default (latch) latches exactly the foreign verdicts; detect-only mode never latches.
 for(Rec240 v: {Rec240::skip,Rec240::ok,Rec240::unread,Rec240::empty,Rec240::range,Rec240::equip,Rec240::owner}){
  const bool foreign=v==Rec240::empty || v==Rec240::range || v==Rec240::equip || v==Rec240::owner;
  check(sp::Rec240Foreign(v)==foreign,"C: foreign verdict set");
  check(!sp::Rec240Latches(v,false),"C: detect-only mode never latches");
  check(sp::Rec240Latches(v,sp::kRec240Latch)==foreign,"C: the default latches exactly the foreign verdicts");}
 check(std::strcmp(sp::Rec240Name(Rec240::owner),"owner")==0 && std::strcmp(sp::Rec240Name(Rec240::ok),"ok")==0 &&
       std::strcmp(sp::Rec240Name(Rec240::empty),"empty")==0 && std::strcmp(sp::Rec240Name(Rec240::unread),"unread")==0,"C: verdict names");
}
// --- WS2: play admission, the viewer-own rebase, the stall freeze and the FIRE buffer.
namespace ws2 {
// The hook's old slot-3 digest formula, kept here to pin SlotDigest to it.
std::uint32_t OldDigest(const std::uint16_t (&w)[8]) noexcept {
 std::uint32_t h=2166136261u;
 for(const auto v:w){h=(h^(v&0xFFu))*16777619u;h=(h^(v>>8))*16777619u;}
 return h?h:1;
}
// One episode as SupportCheck runs it, on fake slot-3 words: a bracket reads
// before/after the body-1 job, the gap check runs at each bracket after the
// first, the per-job compare and the episode compare latch.
// Instance 0's gun record at a bracket: mode, its state (rec+0x2C2), the
// slot-3 sub it holds (0xFF: not slot 3), that item's count and bit 29.
sp::ViewerRec V(std::uint8_t mode,std::uint8_t state=0,std::uint8_t sub=0xFF,std::uint16_t count=0,bool no_spend=false) noexcept {
 sp::ViewerRec v{};v.mode=mode;v.state=state;v.slot=sub<8 ? 3 : 0;v.sub=sub<8 ? sub : 0;v.count=count;v.no_spend=no_spend;
 return v;
}
struct Episode {
 std::uint16_t vars[8]{12,16383,12,8,12,12,8,8};   // the lab seed's slot-3 stock words
 std::uint16_t episode[8]{};std::uint32_t episode_stock=0;bool active=false;
 std::uint16_t prev[8]{};bool prev_ok=false;sp::ViewerRec prev_viewer{};std::uint32_t prev_jobs=0;
 std::uint32_t jobs=0;sp::StockLatch latch{};unsigned rebased=0;sp::Gap last=sp::Gap::none;
 // One body-1 job bracket. job_k/job_d: what body 1's own job changes; extra_jobs:
 // body-1 jobs that ran in the gap without a bracket.
 void Bracket(const sp::ViewerRec& viewer,int job_k=-1,int job_d=0,std::uint32_t extra_jobs=0){
  jobs+=1+extra_jobs;
  std::uint16_t before[8];std::memcpy(before,vars,sizeof(before));
  if(job_k>=0)vars[job_k]=static_cast<std::uint16_t>(vars[job_k]+job_d);
  std::uint16_t after[8];std::memcpy(after,vars,sizeof(after));
  last=sp::Gap::none;
  if(active && episode_stock){
   sp::GapIn g{};g.prev_ok=prev_ok;g.now_ok=true;std::memcpy(g.prev,prev,sizeof(g.prev));std::memcpy(g.now,before,sizeof(g.now));
   g.viewer_prev=prev_viewer;g.viewer_now=viewer;g.other_jobs=jobs-prev_jobs-1u;
   const auto gap=sp::ClassifyGap(g);last=gap.kind;
   if(sp::GapRebases(gap.kind) && !latch.off){sp::Rebase(episode,g.prev,g.now);episode_stock=sp::SlotDigest(episode);++rebased;}
  }
  if(!active){active=true;std::memcpy(episode,before,sizeof(episode));episode_stock=sp::SlotDigest(before);}
  sp::CheckStock(latch,true,true,sp::SlotDigest(before),sp::SlotDigest(after));              // per job
  sp::CheckStock(latch,episode_stock!=0,true,episode_stock,sp::SlotDigest(after));          // across the episode
  std::memcpy(prev,after,sizeof(prev));prev_ok=true;prev_viewer=viewer;prev_jobs=jobs;
 }
};
// A v5 slot-3 sample of grenade 354 with `held`, `age_ms` old.
pi::Sample Sample(std::uint32_t held,std::uint32_t age_ms,std::uint32_t seq,std::uint32_t press=0,std::uint32_t release=0) noexcept {
 pi::Sample s{};s.valid=true;s.seq=seq;s.age_ms=age_ms;s.edge_press=press;s.edge_release=release;
 auto& p=s.packet;p.version=5;p.held=held;p.cam_cdeg=0;p.inv_slot=3;
 p.xflags=pi::XFlag::Equip;p.gun_equip=354;p.gun_flags=pi::GunFlag::Valid;
 return s;
}
// The hook's SupportGate on a composed frame, every safety owner on and body
// 1's equip ready or not (Ready and the record read).
sp::GateIn Gate(const pi::Injection& j,bool body_ready=true) noexcept {
 sp::GateIn x{true,false,true,true,true,true,j.slot_valid,j.equip_valid,body_ready,body_ready,j.inv_slot,
              static_cast<std::uint8_t>(body_ready ? 12 : 0),j.equip};
 x.live=j.live;x.stale=j.stale;x.gun_valid=j.gun_valid;
 return x;
}
// One frame through the main window (Compose, then SupportFrame's StepFrame/
// Freeze/AddEdges) and one consumer (NativeView's Strip): what body 1's job
// sees. strip: the driver's Arm.strip (0x40 = the support gate).
struct Pipe {
 pi::Config c{};pi::EdgeState edge{};sp::FrameTrack track{};std::uint32_t strip=0x40u;bool body_ready=true;
 sp::FrameOut out{};pi::Injection frame{};
 explicit Pipe(std::uint32_t stall_ms){
  c.mode=PartnerInput::net;c.mask=0xFFFFFFFFu;c.words[0]=pi::Word::held;c.words[1]=pi::Word::press;c.words[2]=pi::Word::release;
  c.stale_ms=250;c.stall_ms=stall_ms;
 }
 pi::Injection Step(const pi::Sample& s,std::uint64_t now_ms){
  pi::Injection j=pi::Compose(c,s,0,pi::Overlay{},edge);
  const bool request=sp::Request(true,j.slot_valid,j.inv_slot,strip);
  out=sp::StepFrame(track,request,Gate(j,body_ready),strip,j.held,j.edge_press,now_ms);
  if(out.verdict==sp::Frame::frozen){j=pi::Freeze(j,sp::kLevelBits,out.level,c);j.support_frozen=true;}
  if(out.strip)j=pi::Strip(j,out.strip);   // a kept FIRE press leaves the published frame
  if(out.add_held || out.add_press || out.add_release)j=pi::AddEdges(j,out.add_held,out.add_press,out.add_release,c);
  frame=j;
  // NativeView: admission with freshness waived only for a frozen frame.
  return View(body_ready);
 }
 // A later consumer of the same published frame (a drain's ForConsumer full
 // view) with body 1's readiness at that consumer's time.
 pi::Injection View(bool ready) const {
  sp::GateIn y=Gate(frame,ready);if(frame.support_frozen)y=sp::Fresh(y);
  const bool req=sp::Request(true,frame.slot_valid,frame.inv_slot,strip);
  return pi::Strip(frame,sp::Strip(strip,req,true,req && sp::Admit(y)));
 }
};
template<class Check> void Fixtures(Check& check) {
 constexpr std::uint32_t H=sp::kPadHold,F=sp::kPadFire,HF=sp::kLevelBits;
 // --- Stage A: ModeRefusal (lab always; play only with link v2 connected).
 check(sp::ModeRefusal(true,false,false)==nullptr && sp::ModeRefusal(true,false,true)==nullptr,"mode: lab admits with or without a link");
 check(sp::ModeRefusal(false,true,true)==nullptr,"mode: play admits with link v2 connected");
 check(std::strcmp(sp::ModeRefusal(false,true,false),"link_not_connected")==0,"mode: play refuses without a connected link");
 check(std::strcmp(sp::ModeRefusal(false,false,true),"mode")==0 && std::strcmp(sp::ModeRefusal(false,false,false),"mode")==0,
       "mode: any other mode refuses");
 check(sp::ModeRefusal(true,true,false)==nullptr,"mode: lab wins (never both)");
 // --- C(b) pure gap verdicts.
 {const std::uint16_t seed[8]{12,16383,12,8,12,12,8,8};
  check(sp::SlotDigest(seed)==OldDigest(seed),"C(b): SlotDigest is the hook's slot digest");
  const auto gap=[&](std::initializer_list<std::pair<int,int>> d,const sp::ViewerRec& vp,const sp::ViewerRec& vn,std::uint32_t jobs=0,bool pok=true,bool nok=true){
   sp::GapIn g{};g.prev_ok=pok;g.now_ok=nok;std::memcpy(g.prev,seed,sizeof(seed));std::memcpy(g.now,seed,sizeof(seed));
   for(const auto& e: d)g.now[e.first]=static_cast<std::uint16_t>(g.now[e.first]+e.second);
   g.viewer_prev=vp;g.viewer_now=vn;g.other_jobs=jobs;return sp::ClassifyGap(g);};
  // The viewer's grenade 354 is sub 0 (k6, 12): T0 before the release, T1 the
  // throw block's 1 -> 2 with its own count decremented.
  const sp::ViewerRec T0=V(37,1,0,12),T1=V(37,2,0,11),N=V(0);
  check(gap({},N,N).kind==sp::Gap::none,"C(b): no change");
  check(gap({{0,-1}},T0,T1,0,false).kind==sp::Gap::unread && gap({{0,-1}},T0,T1,0,true,false).kind==sp::Gap::unread,"C(b): unread end never rebases");
  {const auto o=gap({{0,-1}},T0,T1);check(o.kind==sp::Gap::viewer_throw && o.word==0 && o.before==12 && o.after==11 && o.changed==1,
   "C(b): viewer's own throw (the release step entered in the gap, its own word, its count)");}
  check(gap({{0,-1}},V(33,0,0,12),T1).kind==sp::Gap::viewer_throw,"C(b): viewer's own throw from aim (mode 33) to the release");
  check(gap({{3,-1}},V(46,3,3,8),V(46,4,3,7)).kind==sp::Gap::viewer_throw && gap({{3,-1}},V(46,3,3,8),V(46,5,3,7)).kind==sp::Gap::viewer_throw,
   "C(b): viewer's own placement (mode 46 state 3 -> 4/5)");
  // WS2 review: holding a slot-3 item is not spending it.
  for(unsigned m: {28u,29u,32u,30u,33u,42u,43u,45u,47u}){
   const auto h=V(static_cast<std::uint8_t>(m),0,0,12);
   check(gap({{2,-1}},h,h).kind==sp::Gap::foreign,"C(b): viewer holding a grenade (28/29/32..47) with a -1 on another word latches");
   check(gap({{0,-1}},h,V(static_cast<std::uint8_t>(m),0,0,11)).kind==sp::Gap::foreign,"C(b): its own word without the release step latches");
  }
  check(gap({{0,-1}},V(37,1,0,12),V(37,1,0,11)).kind==sp::Gap::foreign,"C(b): mode 37 before the release (state 1) never spends");
  check(gap({{0,-1}},V(46,3,0,12),V(46,6,0,11)).kind==sp::Gap::foreign,"C(b): mode 46 state 6 (no placement) never spends");
  check(gap({{0,-1}},T1,V(37,2,0,10)).kind==sp::Gap::foreign,"C(b): one credit per release (already released at the gap's start)");
  check(gap({{0,-1}},V(0xFF),T1).kind==sp::Gap::foreign,"C(b): an unread start never attributes");
  check(gap({{2,-1}},T0,T1).kind==sp::Gap::foreign,"C(b): released, but another word (k8) went down");
  check(gap({{2,-1}},V(37,1,2,12),V(37,2,2,12)).kind==sp::Gap::foreign,"C(b): released, but the record kept its count (no decrement)");
  check(gap({{0,-1}},T0,V(37,2,0,11,true)).kind==sp::Gap::foreign,"C(b): bit 29 (no stock write) never attributes");
  {auto s=T1;s.slot=0;check(gap({{0,-1}},T0,s).kind==sp::Gap::foreign,"C(b): not slot 3");}
  {auto s=T1;s.sub=8;check(gap({{0,-1}},T0,s).kind==sp::Gap::foreign,"C(b): sub out of range");}
  for(unsigned m: {0u,7u,11u,27u,48u,0xFFu})check(gap({{0,-1}},V(static_cast<std::uint8_t>(m)),V(static_cast<std::uint8_t>(m))).kind==sp::Gap::foreign,
   "C(b): a decrement while the viewer is not throwing (or unread) is a deferred-leak candidate");
  check(gap({{0,-2}},T0,V(37,2,0,10)).kind==sp::Gap::foreign,"C(b): two at once is not one throw");
  check(gap({{0,-1},{2,-1}},T0,T1).kind==sp::Gap::foreign,"C(b): two words down is not one throw");
  check(gap({{0,-1},{3,1}},T0,T1).kind==sp::Gap::foreign,"C(b): a throw and a pickup in one gap stays unattributed");
  check(gap({{0,-1}},T0,T1,1).kind==sp::Gap::foreign,"C(b): an unbracketed body-1 job in the gap: never attributed");
  check(gap({{0,-1}},T0,T1,0xFFFFFFFFu).kind==sp::Gap::foreign,"C(b): a job count that went backwards fails closed");
  {const auto o=gap({{0,1}},N,N);check(o.kind==sp::Gap::viewer_pickup && o.after==13,"C(b): viewer's pickup (+1, any viewer mode)");}
  check(gap({{0,4},{2,2}},V(0xFF),V(0xFF)).kind==sp::Gap::viewer_pickup,"C(b): several pickups");
  check(gap({{0,sp::kPickupMax}},N,N).kind==sp::Gap::viewer_pickup && gap({{0,sp::kPickupMax+1}},N,N).kind==sp::Gap::foreign,"C(b): pickup bound");
  check(gap({{0,1}},N,N,1).kind==sp::Gap::foreign,"C(b): a pickup with an unbracketed body-1 job stays unattributed");
  check(gap({{3,0xFFFF-8}},N,N).kind==sp::Gap::foreign,"C(b): a word set to 0xFFFF (the guard's count) is never a pickup");
  check(!sp::GapRebases(sp::Gap::none) && !sp::GapRebases(sp::Gap::unread) && !sp::GapRebases(sp::Gap::foreign) &&
        sp::GapRebases(sp::Gap::viewer_throw) && sp::GapRebases(sp::Gap::viewer_pickup),"C(b): only the viewer's own gaps rebase");
  check(std::strcmp(sp::GapName(sp::Gap::viewer_throw),"throw")==0 && std::strcmp(sp::GapName(sp::Gap::viewer_pickup),"pickup")==0 &&
        std::strcmp(sp::GapName(sp::Gap::foreign),"foreign")==0,"C(b): gap names");
  {std::uint16_t e[8]{0,1,2,3,4,5,6,7};const std::uint16_t p[8]{5,5,5,5,5,5,5,5};const std::uint16_t n[8]{4,6,5,5,5,5,5,0xFFFF};
   sp::Rebase(e,p,n);check(e[0]==0xFFFF && e[1]==2 && e[2]==2 && e[7]==1,"C(b): rebase moves by the delta (u16 wrap)");}
 }
 // --- C(b) episodes: the per-job compares are unchanged.
 const auto dec=[](Episode& e,int k){e.vars[k]=static_cast<std::uint16_t>(e.vars[k]-1);};
 {Episode e;e.Bracket(V(28,0,0,12));e.Bracket(V(33,0,0,12));dec(e,0);   // the viewer throws between two brackets
  e.Bracket(V(37,2,0,11));check(e.last==sp::Gap::viewer_throw,"C(b): the gap is the viewer's throw");e.Bracket(V(37,2,0,11));
  check(!e.latch.off && e.rebased==1,"C(b): the viewer's own throw during a partner episode does not latch");}
 {Episode e;e.Bracket(V(46,3,3,8));dec(e,3);e.Bracket(V(46,4,3,7));e.Bracket(V(46,4,3,7));
  check(!e.latch.off && e.rebased==1,"C(b): the viewer's own placement during a partner episode does not latch");}
 {Episode e;e.Bracket(V(0));e.vars[2]=static_cast<std::uint16_t>(e.vars[2]+1);e.Bracket(V(0));e.Bracket(V(0));
  check(!e.latch.off && e.rebased==1,"C(b): the viewer's pickup during a partner episode does not latch");}
 {Episode e;e.Bracket(V(33,0,0,12));dec(e,0);e.Bracket(V(37,2,0,11));e.vars[0]=static_cast<std::uint16_t>(e.vars[0]+1);e.Bracket(V(0));
  check(!e.latch.off && e.rebased==2,"C(b): throw then pickup, each in its own gap");}
 {Episode e;e.Bracket(V(37,2,0,12));e.Bracket(V(37,2,0,12),0,-1);
  check(e.latch.off,"C(b): a body-1 leak inside its own job still latches (per-job compare), even while the viewer throws");}
 {Episode e;e.Bracket(V(0));e.Bracket(V(0),6,-1);check(e.latch.off,"C(b): a body-1 leak in its own job latches");}
 {Episode e;e.Bracket(V(0));dec(e,0);e.Bracket(V(0));
  check(e.latch.off && e.rebased==0 && e.last==sp::Gap::foreign,"C(b): an out-of-job decrement the viewer did not throw still latches (deferred leak)");}
 {Episode e;e.Bracket(V(37,1,0,12));dec(e,0);e.Bracket(V(37,2,0,11),-1,0,1);
  check(e.latch.off && e.last==sp::Gap::foreign,"C(b): an unbracketed body-1 job in the gap: the decrement still latches");}
 {Episode e;e.Bracket(V(37,1,0,12));e.vars[0]=static_cast<std::uint16_t>(e.vars[0]-2);e.Bracket(V(37,2,0,10));
  check(e.latch.off,"C(b): a double decrement still latches");}
 {Episode e;e.Bracket(V(37,1,0,12));dec(e,0);e.Bracket(V(37,2,0,11),4,-1);
  check(e.latch.off && e.rebased==1,"C(b): a rebased viewer throw does not hide a body-1 leak in the same bracket");}
 // WS2 review: a deferred body-1 decrement while the viewer only holds a slot-3
 // item (the lab seed: the viewer holds 354 = k6 in the hold hub, body 1 holds
 // 395 = k8) or right after the viewer's own rebased throw still latches.
 for(unsigned m: {28u,32u,45u}){
  Episode e;const auto h=V(static_cast<std::uint8_t>(m),0,0,12);e.Bracket(h);dec(e,2);e.Bracket(h);
  check(e.latch.off && e.rebased==0 && e.last==sp::Gap::foreign,"C(b): viewer holding (28/32/45), a -1 on k8 outside body 1's bracket latches");
 }
 {Episode e;e.Bracket(V(28,0,0,12));dec(e,0);e.Bracket(V(28,0,0,11));
  check(e.latch.off && e.rebased==0,"C(b): viewer holding its grenade, a -1 on its own word without a release step latches");}
 {Episode e;e.Bracket(V(33,0,0,12));dec(e,0);e.Bracket(V(37,2,0,11));dec(e,0);e.Bracket(V(37,2,0,10));
  check(e.latch.off && e.rebased==1 && e.last==sp::Gap::foreign,"C(b): a decrement in the gap after a rebased viewer throw latches (same word)");}
 {Episode e;e.Bracket(V(33,0,0,12));dec(e,0);e.Bracket(V(37,2,0,11));dec(e,2);e.Bracket(V(37,2,0,11));
  check(e.latch.off && e.rebased==1 && e.last==sp::Gap::foreign,"C(b): a decrement in the gap after a rebased viewer throw latches (another word)");}
 {Episode e;e.Bracket(V(33,0,0,12));dec(e,0);e.Bracket(V(37,2,0,11));e.Bracket(V(28,0,0,11));e.Bracket(V(33,0,0,11));dec(e,0);e.Bracket(V(37,2,0,10));
  check(!e.latch.off && e.rebased==2,"C(b): two viewer throws, each with its own release step, do not latch");}
 // --- Stall freeze (critic #4): StallHold and the frame pipeline.
 {sp::StallHoldIn s{};s.stall_hold=true;s.stale_ms=250;s.stall_ms=400;s.age_ms=300;s.v5_valid=true;s.slot=3;s.equip=354;s.was_support=true;s.support_equip=354;
  check(sp::StallHold(s),"stall hold: stale v5 slot 3 of the same support equip inside the window");
  {auto x=s;x.age_ms=250;check(!sp::StallHold(x),"stall hold: not stale yet (the live path judges)");}
  {auto x=s;x.age_ms=400;check(sp::StallHold(x),"stall hold: up to stall_ms");x.age_ms=401;check(!sp::StallHold(x),"stall hold: beyond the window");}
  {auto x=s;x.stall_hold=false;check(!sp::StallHold(x),"stall hold: partner_native_stall=neutral (lab default) never holds");}
  {auto x=s;x.stall_ms=250;check(!sp::StallHold(x),"stall hold: stall_ms <= stale_ms is off");}
  {auto x=s;x.was_support=false;check(!sp::StallHold(x),"stall hold: only a held support verdict");}
  {auto x=s;x.support_equip=349;check(!sp::StallHold(x),"stall hold: another equip");}
  {auto x=s;x.slot=2;check(!sp::StallHold(x),"stall hold: another slot");}
  {auto x=s;x.v5_valid=false;check(!sp::StallHold(x),"stall hold: no v5 gun group");}
  {auto x=s;x.equip=0;x.support_equip=0;check(!sp::StallHold(x),"stall hold: no equip");}}
 // WS2 review: the observer's writer context through a stall is keyed on the
 // driver's last support verdict, not on support_stall (a frame that crosses
 // stale_ms between the driver's read and the observer's).
 {sp::StallHoldIn d{};d.stall_hold=true;d.stale_ms=250;d.stall_ms=400;d.age_ms=247;d.v5_valid=true;d.slot=3;d.equip=354;
  d.was_support=true;d.support_equip=354;
  check(!sp::StallHold(d),"context: the driver's tick read 247 ms (live): no support_stall, support_equip judged live");
  sp::StallHoldIn o=d;o.age_ms=252;
  check(sp::ContextHold(o),"context: the observer then reads 252 ms (just stale): slot 3 / equip kept (Ready still answers)");
  o.age_ms=5;check(sp::ContextHold(o),"context: fresh again before the driver's next tick: kept");
  o.age_ms=400;check(sp::ContextHold(o),"context: up to stall_ms");o.age_ms=401;check(!sp::ContextHold(o),"context: beyond the window");
  o=d;o.age_ms=300;o.support_equip=0;check(!sp::ContextHold(o),"context: no driver support verdict");
  o=d;o.age_ms=300;o.equip=349;check(!sp::ContextHold(o),"context: another equip");
  o=d;o.age_ms=300;o.slot=2;check(!sp::ContextHold(o),"context: another slot");
  o=d;o.age_ms=300;o.v5_valid=false;check(!sp::ContextHold(o),"context: no v5 gun group");
  o=d;o.age_ms=300;o.stall_hold=false;check(!sp::ContextHold(o),"context: partner_native_stall=neutral (lab) never holds");
  o=d;o.age_ms=300;o.stall_ms=250;check(!sp::ContextHold(o),"context: stall_ms <= stale_ms is off");
  o=d;o.age_ms=300;o.support_equip=0x800;o.equip=0x800;check(!sp::ContextHold(o),"context: equip range");}
 for(std::uint32_t gap_ms: {251u,260u,400u,600u,1000u}){
  Pipe p(1000);std::uint64_t t=1000;std::uint32_t seq=1;
  auto j=p.Step(Sample(H,0,seq++),t);check(p.out.verdict==sp::Frame::admitted && (j.held&HF)==H,"freeze: aim admitted");
  t+=16;j=p.Step(Sample(HF,0,seq++,F),t);check(p.out.verdict==sp::Frame::admitted && (j.held&HF)==HF && (j.edge_press&F),"freeze: FIRE press admitted");
  const auto held_seq=seq-1;
  bool kept=true,edges=false;
  for(std::uint32_t age=251;;){
   t+=16;j=p.Step(Sample(HF,age,held_seq),t);
   kept=kept && p.out.verdict==sp::Frame::frozen && p.frame.support_frozen && (j.held&HF)==HF;
   edges=edges || (j.edge_press&HF) || (j.edge_release&HF) || ((j.w[1]|j.w[2])&HF);
   if(age>=gap_ms)break;
   age=age+80>gap_ms ? gap_ms : age+80;
  }
  check(kept,"freeze: a 250..1000 ms stall keeps the admitted HOLD|FIRE level (no early release)");
  check(!edges,"freeze: no new HOLD/FIRE edge during the stall");
  t+=16;j=p.Step(Sample(HF,5,seq++),t);
  check(p.out.verdict==sp::Frame::admitted && (j.held&HF)==HF && !(j.edge_press&HF),"freeze: fresh input resumes with no phantom second press");
  t+=16;j=p.Step(Sample(H,5,seq++,0,F),t);
  check((j.held&HF)==H && (j.edge_release&F) && !(j.edge_press&F),"freeze: the remote's release after the stall is one release edge");
 }
 {Pipe p(1000);std::uint64_t t=1000;std::uint32_t seq=1;
  p.Step(Sample(H,0,seq++),t);t+=16;p.Step(Sample(HF,0,seq++,F),t);
  t+=16;auto j=p.Step(Sample(HF,1100,seq-1),t);
  check(p.out.verdict==sp::Frame::refused && !(j.held&HF),"freeze: beyond the stall window the old strip applies");
  t+=16;j=p.Step(Sample(HF,1200,seq-1),t);check(!(j.held&HF),"freeze: and stays stripped");
  t+=16;j=p.Step(Sample(HF,5,seq++),t);check(p.out.verdict==sp::Frame::admitted && (j.held&HF)==HF,"freeze: fresh input admits again");}
 {Pipe p(400);std::uint64_t t=1000;std::uint32_t seq=1;
  p.Step(Sample(H,0,seq++),t);t+=16;p.Step(Sample(HF,0,seq++,F),t);
  t+=16;auto j=p.Step(Sample(HF,300,seq-1),t);check(p.out.verdict==sp::Frame::frozen && (j.held&HF)==HF,"freeze: play default stall_ms 400 freezes at 300 ms");
  t+=16;j=p.Step(Sample(HF,500,seq-1),t);check(p.out.verdict==sp::Frame::refused && !(j.held&HF),"freeze: play default beyond 400 ms strips");}
 {Pipe p(0);std::uint64_t t=1000;std::uint32_t seq=1;   // lab default: partner_native_stall=neutral
  p.Step(Sample(HF,0,seq++,F),t);t+=16;auto j=p.Step(Sample(HF,300,seq-1),t);
  check(p.out.verdict==sp::Frame::refused && !(j.held&HF),"freeze: no stall hold (lab default) keeps the old strip");}
 {Pipe p(1000);std::uint64_t t=1000;std::uint32_t seq=1;
  p.Step(Sample(H,0,seq++),t);t+=16;p.Step(Sample(HF,0,seq++,F),t);
  p.body_ready=false;t+=16;auto j=p.Step(Sample(HF,300,seq-1),t);
  check(p.out.verdict==sp::Frame::refused && !(j.held&HF),"freeze: every other gate still applies (body 1 not ready)");}
 {Pipe p(1000);std::uint64_t t=1000;std::uint32_t seq=1;
  p.Step(Sample(H,0,seq++),t);t+=16;p.Step(Sample(HF,0,seq++,F),t);
  p.strip=0x58u;t+=16;auto j=p.Step(Sample(HF,300,seq-1),t);
  check(p.out.verdict==sp::Frame::refused && !(j.held&HF),"freeze: never against the driver's own strip");}
 {Pipe p(1000);std::uint64_t t=1000;std::uint32_t seq=1;
  p.Step(Sample(H,0,seq++),t);t+=16;p.Step(Sample(HF,0,seq++,F),t);
  t+=16;auto s=Sample(HF,300,seq-1);s.packet.gun_equip=349;auto j=p.Step(s,t);
  check(p.out.verdict==sp::Frame::refused && !(j.held&HF),"freeze: only the same slot-3 equip");}
 {sp::FrameTrack t{};sp::GateIn x{true,false,true,true,true,true,true,true,true,true,3,12,354};
  sp::StepFrame(t,true,x,0x40u,H,0,0);
  sp::GateIn s=x;s.live=false;s.stale=true;
  const auto o=sp::StepFrame(t,true,s,0x40u,HF,F,16);
  check(o.verdict==sp::Frame::frozen && o.level==H,"freeze: the level is the last ADMITTED one, not the stale frame's");
  {pi::Config c{};c.mask=0xFFFFFFFFu;c.words[0]=pi::Word::held;c.words[1]=pi::Word::press;c.words[2]=pi::Word::release;
   pi::Injection j{};j.held=HF|0x4u;j.w[0]=HF|0x4u;j.w[1]=F;j.edge_press=F|0x4u;j.edge_release=H;j.tap12=F;j.long12=H;j.synth=F;
   const auto z=pi::Freeze(j,HF,H,c);
   check(z.held==(H|0x4u) && z.w[0]==(H|0x4u) && !(z.w[1]&HF) && z.edge_press==0x4u && !z.edge_release && !z.tap12 && z.long12==H && !z.synth,
         "freeze: Freeze sets the level and removes every HOLD/FIRE edge only");
   check(pi::LevelOnly(pi::Freeze(j,HF,H,c),c).held==(H|0x4u),"freeze: the level reaches level-only consumers");
   pi::Injection f=j;f.support_frozen=true;
   check(pi::LevelOnly(f,c).support_frozen && pi::Strip(f,0x10u).support_frozen && !pi::Neutral(f).support_frozen,"freeze: the flag follows the frame, never Neutral");}}
 // --- C(c) the FIRE-edge buffer.
 {Pipe p(1000);std::uint64_t t=1000;std::uint32_t seq=1;p.body_ready=false;
  auto j=p.Step(Sample(0,0,seq++,F,F),t);   // select-then-throw: a FIRE tap while body 1 is still being equipped
  check(p.out.verdict==sp::Frame::refused && p.out.buffered && !(j.held&F),"buffer: a FIRE tap waiting on the equip is buffered (not injected)");
  p.body_ready=true;t+=200;j=p.Step(Sample(0,0,seq++),t);
  check(p.out.delivered && (j.held&F) && (j.edge_press&F) && (j.w[1]&F) && (j.w[0]&F),"buffer: delivered on the first admitted frame <= 300 ms");
  check(!(j.edge_release&F) && !(j.w[2]&F),"buffer: the delivered tap is not also released in the same frame (WS2 review)");
  t+=16;j=p.Step(Sample(0,0,seq++),t);
  check(!(j.held&F) && (j.edge_release&F) && (j.w[2]&F) && !p.out.delivered,"buffer: the tap's release edge on the next frame");
  t+=16;j=p.Step(Sample(0,0,seq++),t);check(!(j.edge_release&F) && !(j.edge_press&F),"buffer: delivered once");}
 {Pipe p(1000);std::uint64_t t=1000;std::uint32_t seq=1;p.body_ready=false;
  p.Step(Sample(F,0,seq++,F),t);p.body_ready=true;t+=300;auto j=p.Step(Sample(F,0,seq++),t);
  check(p.out.delivered && (j.edge_press&F) && (j.held&F),"buffer: a press still held is delivered as its press edge at 300 ms");
  t+=16;j=p.Step(Sample(F,0,seq++),t);check(!(j.edge_release&F) && (j.held&F),"buffer: a held FIRE gets no synthetic release");}
 {Pipe p(1000);std::uint64_t t=1000;std::uint32_t seq=1;p.body_ready=false;
  p.Step(Sample(0,0,seq++,F,F),t);t+=150;p.Step(Sample(0,0,seq++),t);check(!p.out.dropped,"buffer: still waiting at 150 ms");
  p.body_ready=true;t+=151;auto j=p.Step(Sample(0,0,seq++),t);
  check(p.out.dropped && !p.out.delivered && !(j.edge_press&F) && !(j.held&F),"buffer: dropped after 300 ms");}
 {Pipe p(1000);std::uint64_t t=1000;std::uint32_t seq=1;p.body_ready=false;
  p.Step(Sample(0,0,seq++,F,F),t);t+=100;auto s=Sample(0,0,seq++);s.packet.gun_equip=349;p.Step(s,t);
  check(p.out.dropped,"buffer: another equip drops it");}
 {Pipe p(1000);std::uint64_t t=1000;std::uint32_t seq=1;p.body_ready=false;
  p.Step(Sample(0,0,seq++,F,F),t);t+=100;auto s=Sample(0,0,seq++);s.packet.inv_slot=0;p.Step(s,t);
  check(p.out.dropped,"buffer: leaving slot 3 drops it");}
 {Pipe p(1000);std::uint64_t t=1000;std::uint32_t seq=1;p.body_ready=false;
  p.Step(Sample(0,0,seq++,F,F),t);t+=16;p.Step(Sample(0,0,seq++),t);   // the tap's synthesized window, then its release
  check(p.out.verdict==sp::Frame::refused && !p.out.dropped,"buffer: kept while waiting");
  p.body_ready=true;t+=16;auto j=p.Step(Sample(F,0,seq++,F),t);
  check(!p.out.delivered && !p.out.dropped && (j.edge_press&F),"buffer: a fresh press at admission supersedes it (one press)");}
 {sp::FrameTrack t{};sp::GateIn x{true,false,true,true,true,true,true,true,false,false,3,0,354};
  auto y=x;y.latched=true;check(!sp::StepFrame(t,true,y,0x58u,0,F,0).buffered,"buffer: never while support is latched off");
  y=x;y.guard_armed=false;check(!sp::StepFrame(t,true,y,0x58u,0,F,0).buffered,"buffer: never without the guard");
  y=x;y.stale=true;check(!sp::StepFrame(t,true,y,0x58u,0,F,0).buffered,"buffer: never from a stale frame");
  y=x;y.slot=2;check(!sp::StepFrame(t,true,y,0x58u,0,F,0).buffered,"buffer: slot 3 only");
  y=x;y.body_ready=y.record_ready=true;y.type=13;check(!sp::StepFrame(t,true,y,0x58u,0,F,0).buffered,"buffer: never for a placed item (type 13)");
  check(sp::StepFrame(t,true,x,0x58u,0,F,0).buffered,"buffer: waiting on body 1's equip");
  y=x;y.body_ready=y.record_ready=true;y.type=12;
  auto o=sp::StepFrame(t,true,y,0x58u,0,0,50);check(o.verdict==sp::Frame::refused && !o.dropped,"buffer: the driver's gate one tick behind keeps waiting");
  o=sp::StepFrame(t,true,y,0x40u,0,0,66);check(o.delivered && o.add_press==F && o.add_held==F,"buffer: delivered once the driver passes FIRE");}
 {pi::Config c{};c.mask=~0x10u;c.words[0]=pi::Word::held;c.words[1]=pi::Word::press;c.words[2]=pi::Word::release;
  pi::Injection j{};const auto z=pi::AddEdges(j,F,F,0,c);check(!z.held && !z.edge_press && !z.w[0],"buffer: AddEdges stays inside the mask");
  c.mask=0xFFFFFFFFu;const auto r=pi::AddEdges(j,0,0,F,c);check(r.edge_release==F && r.w[2]==F && !r.held,"buffer: AddEdges release word");
  pi::Injection h{};h.held=F;check(!pi::AddEdges(h,0,0,F,c).edge_release,"buffer: never a release of a held bit");
  pi::Injection r2{};r2.edge_release=F|0x4u;r2.w[2]=F|0x4u;
  const auto d=pi::AddEdges(r2,F,F,0,c);
  check((d.held&F) && (d.edge_press&F) && (d.w[1]&F) && d.edge_release==0x4u && d.w[2]==0x4u,
        "buffer: AddEdges clears a same-frame release of an added held/press bit, nothing else");}
 // WS2 review: a press the main window buffers never reaches a later consumer
 // of the same frame that sees body 1 become ready (one remote press, one press).
 for(unsigned tap=0;tap<2;++tap){
  Pipe p(1000);std::uint64_t t=1000;std::uint32_t seq=1;p.body_ready=false;
  unsigned presses=0,releases=0;bool contradictory=false;
  const auto count=[&](const pi::Injection& v){presses+=(v.edge_press&F) ? 1u : 0u;releases+=(v.edge_release&F) ? 1u : 0u;
                                               contradictory=contradictory || ((v.edge_press&F) && (v.edge_release&F));};
  auto j=tap ? p.Step(Sample(0,0,seq++,F,F),t) : p.Step(Sample(F,0,seq++,F),t);
  check(p.out.buffered && !(j.edge_press&F) && !(j.held&F),"buffer: the main window refuses and buffers");
  auto d1=p.View(true);   // drain1 takes frame k after body 1 became ready in an earlier job of the frame
  check(!(d1.edge_press&F) && !(d1.held&F) && !(d1.w[0]&F) && !(d1.w[1]&F) && !(d1.synth&F),
        "buffer: a later consumer that sees body 1 ready in the same frame gets no FIRE");
  count(d1);
  p.body_ready=true;t+=16;j=p.Step(tap ? Sample(0,0,seq++) : Sample(F,0,seq++),t);count(j);d1=p.View(true);count(d1);
  check(p.out.delivered && (j.edge_press&F) && (d1.edge_press&F),"buffer: delivered on the next frame (each consumer once)");
  for(unsigned k=0;k<3;++k){t+=16;j=p.Step(tap ? Sample(0,0,seq++) : Sample(F,0,seq++),t);count(j);d1=p.View(true);count(d1);}
  check(presses==2 && !contradictory,"buffer: one press frame for one remote press (main + drain1), never with its release");
  check(releases==(tap ? 2u : 0u),"buffer: a tap's single release frame; a held press none");
 }
 {Pipe p(1000);std::uint64_t t=1000;std::uint32_t seq=1;p.body_ready=false;
  p.Step(Sample(F,0,seq++,F),t);t+=16;p.Step(Sample(F,0,seq++),t);
  check(p.out.verdict==sp::Frame::refused && !(p.View(true).held&F),"buffer: still waiting, a later consumer gets no FIRE level either");}
}
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
 ws2::Fixtures(check);
 std::printf("%u support policy checks, %u failures\n",checks,fail);return fail?1:0;
}
