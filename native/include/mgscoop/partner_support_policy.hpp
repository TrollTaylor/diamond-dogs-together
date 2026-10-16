#pragma once
// Native thrown-support admission and stock latch; pure fixture policy.
#include <cstddef>
#include <cstdint>
namespace mgscoop::partner_support_policy {
struct GateIn {
 bool enabled, latched, guard_installed, guard_armed, filter_live, filter_armed;
 bool slot_valid, equip_valid, body_ready, record_ready;
 std::uint8_t slot, type;
 std::uint16_t equip;
 bool live=true,stale=false,gun_valid=true,p218_window=false;
};
constexpr bool Admit(const GateIn& x) noexcept {
 return x.live && !x.stale && x.gun_valid && !x.p218_window && x.enabled && !x.latched && x.guard_installed && x.guard_armed && x.filter_live && x.filter_armed &&
  x.slot_valid && x.slot==3 && x.equip_valid && x.equip>0 && x.equip<0x800 && x.type==12 && x.body_ready && x.record_ready;
}
constexpr std::uint32_t Strip(std::uint32_t mask,bool request,bool key,bool admitted) noexcept {
 if(request)mask|=admitted ? 0x40u : 0x58u;
 if(key || request)mask|=0x8020u; // no Action or CQC in support windows
 return mask;
}
constexpr bool ThrowMode(std::uint8_t mode) noexcept { return mode>=28 && mode<=42; }
// Whether a native window treats this frame as a support request. With the
// key off it is always false, so Round 1's weapon gate alone decides what the
// window strips, exactly as before support existed (review-1ca3ce6).
// 0x40 is RELOAD: the driver strips it alone for an admitted support slot.
constexpr bool Request(bool key,bool slot_valid,std::uint8_t slot,std::uint32_t strip) noexcept {
 return key && ((slot_valid && slot>2) || strip==0x40u);
}
// Body 1 may not be (re)equipped or have its slot followed while its gun mode
// is unread (0xFF), switching, reloading (8/11/12) or in the throw family:
// the old slot-3 handle may still be referenced by the running throw.
constexpr bool BodyBusy(std::uint8_t mode,bool switching) noexcept {
 return switching || mode==0xFF || mode==8 || mode==11 || mode==12 || ThrowMode(mode);
}
struct StockLatch { bool off=false; };
// An unread before/after refuses admission; only actual changed evidence latches.
constexpr bool CheckStock(StockLatch& s,bool before_ok,bool after_ok,std::uint32_t before,std::uint32_t after) noexcept {
 const bool changed=before_ok && after_ok && before!=after;
 if(changed)s.off=true;
 return changed;
}

// --- WS2 Stage A: play admission (docs/anim-round2-throws.md "Throws in play").
// The one-PC lab always; two-PC play only while link v2 is connected; any other
// mode never. The observer's SupportModeReason feeds both of its gates.
constexpr const char* ModeRefusal(bool lab,bool play,bool link_connected) noexcept {
 return lab ? nullptr : play ? (link_connected ? nullptr : "link_not_connected") : "mode";
}

// --- WS2 C(b): the viewer's own slot-3 items during a partner episode.
// The episode baseline (the eight slot-3 stock words vars+0x620..0x62F) spans
// viewer frames. Between two bracketed body-1 jobs only the viewer (instance 0)
// and the world run, so a change there is the viewer's own unless a body-1 job
// ran outside the bracket. The baseline is rebased by exactly that delta only
// when every body-1 job of the gap was bracketed, no word moved to or from
// 0xFFFF (the guard's never-runs-out count), and the change is either a pickup
// (increases only, each at most kPickupMax) or the viewer's own throw or
// placement: exactly one word down by exactly 1, nothing else, the viewer
// ENTERED its spend step in this gap (ViewerSpendEntered) and that word is the
// viewer's own held item (ViewerOwnWord). Merely holding a slot-3 item (the
// hold hub 28/29, draw/holster 32, C4 in hand 43..47) never attributes.
// Anything else stays a deferred-leak candidate and the episode compare
// latches as before; the per-job compares are untouched, so a body-1 leak
// inside its own job always latches.
inline constexpr std::uint16_t kPickupMax=64;
inline constexpr std::uint16_t kStockSentinel=0xFFFF;
// Instance 0's gun record at one end of a gap (read-only; mode 0xFF unread):
// the gun mode rec+0x2C0, the mode's state rec+0x2C2, the hand slot rec+0x3DA,
// the slot sub rec+0x3DC, the count the spend decrements (rec+0x260, or
// rec+0x262 with rec+0x27C bit 9) and rec+0x3C0 bit 29 (no stock write).
struct ViewerRec {
 std::uint8_t mode=0xFF,state=0xFF,slot=0xFF,sub=0xFF;
 std::uint16_t count=0;
 bool no_spend=true;
};
// The spend step itself, not the throw family. Mode 37's throw block (op 10,
// 0x10879BF) sets rec+0x2C2 1 -> 2 and, unless bit 29 or a count >= 0x3FFF,
// decrements the count and calls the stock setter ctl vt+0xC0 with
// k = ctl vt+0xB0(rec+0x3DA, rec+0x3DC) in the same block. Mode 46's place
// block (op 9, 0x107C6FA) does the same and then sets 3 -> 4/5.
constexpr bool ViewerSpent(const ViewerRec& v) noexcept {
 return (v.mode==37 && v.state==2) || (v.mode==46 && (v.state==4 || v.state==5));
}
// Read at both ends, in the spend step at the end and not at the start: one
// credit per throw or placement (a later gap of the same step is foreign).
constexpr bool ViewerSpendEntered(const ViewerRec& prev,const ViewerRec& now) noexcept {
 return prev.mode!=0xFF && now.mode!=0xFF && !ViewerSpent(prev) && ViewerSpent(now);
}
// The decremented word is the viewer's own held item: slot 3 with k = 6 + sub
// (store vt+0x68, the ammo copy's mapping), the record's count is the word's
// new value, and the record spends (bit 29 clear).
constexpr bool ViewerOwnWord(const ViewerRec& v,std::uint8_t word,std::uint16_t after) noexcept {
 return v.slot==3 && v.sub<8 && v.sub==word && v.count==after && !v.no_spend;
}
enum class Gap : std::uint8_t { none, unread, viewer_throw, viewer_pickup, foreign };
constexpr const char* GapName(Gap g) noexcept {
 return g==Gap::none ? "none" : g==Gap::unread ? "unread" : g==Gap::viewer_throw ? "throw" : g==Gap::viewer_pickup ? "pickup" : "foreign";
}
constexpr bool GapRebases(Gap g) noexcept { return g==Gap::viewer_throw || g==Gap::viewer_pickup; }
struct GapIn {
 bool prev_ok=false,now_ok=false;      // the previous bracket's after-words, this bracket's before-words
 std::uint16_t prev[8]{},now[8]{};
 ViewerRec viewer_prev{},viewer_now{}; // instance 0's record at both ends
 std::uint32_t other_jobs=0;           // body-1 jobs that ran in the gap outside a bracket
};
struct GapOut { Gap kind=Gap::none; std::uint8_t word=0xFF; std::uint16_t before=0,after=0; std::uint8_t changed=0; };
constexpr GapOut ClassifyGap(const GapIn& g) noexcept {
 GapOut o{};
 if(!g.prev_ok || !g.now_ok){o.kind=Gap::unread;return o;}
 unsigned down=0,up=0;bool sentinel=false,big=false,by_one=true;
 for(unsigned k=0;k<8;++k){
  const std::uint16_t a=g.prev[k],b=g.now[k];
  if(a==b)continue;
  if(!o.changed){o.word=static_cast<std::uint8_t>(k);o.before=a;o.after=b;}
  ++o.changed;
  if(a==kStockSentinel || b==kStockSentinel)sentinel=true;
  if(b>a){++up;if(b-a>kPickupMax)big=true;}
  else{++down;if(a-b!=1)by_one=false;}
 }
 if(!o.changed){o.kind=Gap::none;return o;}
 if(g.other_jobs || sentinel){o.kind=Gap::foreign;return o;}
 if(!down){o.kind=big ? Gap::foreign : Gap::viewer_pickup;return o;}
 const bool own=down==1 && !up && by_one && ViewerSpendEntered(g.viewer_prev,g.viewer_now) &&
  ViewerOwnWord(g.viewer_now,o.word,o.after);
 o.kind=own ? Gap::viewer_throw : Gap::foreign;
 return o;
}
// The episode baseline moved by the gap's delta (u16 wrap, word by word).
constexpr void Rebase(std::uint16_t (&episode)[8],const std::uint16_t (&prev)[8],const std::uint16_t (&now)[8]) noexcept {
 for(unsigned k=0;k<8;++k)episode[k]=static_cast<std::uint16_t>(episode[k]+static_cast<std::uint16_t>(now[k]-prev[k]));
}
// The slot-3 digest (FNV-1a over the eight words, low byte first; 0 means unread).
constexpr std::uint32_t SlotDigest(const std::uint16_t (&words)[8]) noexcept {
 std::uint32_t h=2166136261u;
 for(const auto w:words){h=(h^(w&0xFFu))*16777619u;h=(h^(static_cast<unsigned>(w)>>8))*16777619u;}
 return h ? h : 1u;
}

// --- WS2 stall freeze (critic #4) and the FIRE-edge buffer (C(c)), per frame.
// A stale frame used to fail Admit and get HOLD|FIRE|RELOAD stripped: body 1
// released early, and when FIRE came back still held it threw again (the guard
// never runs it out). Inside the stall window (Compose carries a stale sample's
// slot/equip only there) of the same slot-3 equip as the last admitted frame,
// with every other gate passing, the frame instead holds that frame's HOLD|FIRE
// level with no edge of them. Beyond the window the old strip applies.
// A FIRE press while support only waits on body 1's equip (select-then-throw)
// is kept for kFireBufferMs and delivered on the first admitted frame of the
// same equip (a tap gets its release edge on the next frame). While it is kept,
// FIRE (level, edges, words) leaves the published frame (FrameOut.strip): the
// main window judges once per frame, but every drain consumer re-judges the
// full frame against live body-1 state, so a consumer that saw body 1 become
// ready later in the same frame would otherwise inject the press the buffer
// delivers again on the next frame (one remote press, two pins).
inline constexpr std::uint32_t kPadHold=0x08u,kPadFire=0x10u,kLevelBits=kPadHold|kPadFire;
inline constexpr std::uint64_t kFireBufferMs=300;
constexpr GateIn Fresh(GateIn x) noexcept {x.live=true;x.stale=false;return x;}
// Every gate passes on a fresh frame except body 1's equip (Ready / record);
// a decoded type other than 12 (a placed item, an arm) is never waited for.
constexpr bool WaitingOnEquip(const GateIn& x) noexcept {
 return x.live && !x.stale && x.gun_valid && !x.p218_window && x.enabled && !x.latched && x.guard_installed && x.guard_armed &&
  x.filter_live && x.filter_armed && x.slot_valid && x.slot==3 && x.equip_valid && x.equip>0 && x.equip<0x800 &&
  (x.type==0 || x.type==12) && !(x.body_ready && x.record_ready && x.type==12);
}
struct FrameTrack {
 bool have=false;std::uint16_t equip=0;std::uint32_t level=0;           // the last admitted support frame
 bool fire=false;std::uint64_t fire_ms=0;std::uint16_t fire_equip=0;     // a buffered FIRE press
 bool release=false;                                                     // a delivered tap's release is due
};
enum class Frame : std::uint8_t { none, admitted, frozen, refused };
struct FrameOut {
 Frame verdict=Frame::none;
 std::uint32_t level=0;                          // frozen: the HOLD|FIRE level to hold
 std::uint32_t add_held=0,add_press=0,add_release=0;
 std::uint32_t strip=0;                          // bits to remove from the published frame (a kept press)
 bool buffered=false,delivered=false,dropped=false;
};
// request: partner_support_policy::Request for the frame; x: its gate (fresh
// or stale as composed); strip: the driver's Arm.strip (its weapon gate, the
// play leak strip): a frame whose HOLD|FIRE the driver strips is never
// admitted or frozen here, and a buffered press waits for the driver too;
// held/press: the frame's held level and press edges.
constexpr FrameOut StepFrame(FrameTrack& t,bool request,const GateIn& x,std::uint32_t strip,std::uint32_t held,std::uint32_t press,
                             std::uint64_t now_ms) noexcept {
 FrameOut o{};
 if(t.release){t.release=false;if(!(held&kPadFire))o.add_release=kPadFire;}
 if(!request){
  if(t.fire)o.dropped=true;
  t.have=false;t.fire=false;
  return o;
 }
 const bool driver_ok=(strip&kLevelBits)==0;
 if(driver_ok && Admit(x)){
  o.verdict=Frame::admitted;
  if(t.fire){
   if(press&kPadFire){}      // a fresh press supersedes the buffered one
   else if(x.equip==t.fire_equip && now_ms>=t.fire_ms && now_ms-t.fire_ms<=kFireBufferMs){
    o.add_press=kPadFire;o.delivered=true;
    if(!(held&kPadFire)){o.add_held=kPadFire;t.release=true;}
   } else o.dropped=true;
   t.fire=false;
  }
  t.have=true;t.equip=x.equip;t.level=held&kLevelBits;
  return o;
 }
 if(driver_ok && x.stale && t.have && x.slot_valid && x.slot==3 && x.equip_valid && x.equip==t.equip && Admit(Fresh(x))){
  o.verdict=Frame::frozen;o.level=t.level;
  return o;
 }
 o.verdict=Frame::refused;t.have=false;
 // Waiting: body 1's equip, or the driver's gate one tick behind the hook's.
 const bool waiting=WaitingOnEquip(x) || (!driver_ok && Admit(x));
 if(t.fire && (!waiting || x.equip!=t.fire_equip || now_ms<t.fire_ms || now_ms-t.fire_ms>kFireBufferMs)){t.fire=false;o.dropped=true;}
 if(waiting && (press&kPadFire)){t.fire=true;t.fire_ms=now_ms;t.fire_equip=x.equip;o.buffered=true;}
 if(t.fire)o.strip=kPadFire;   // only the buffer delivers this press
 return o;
}
// The driver's support gate through an input stall (critic #4): the newest v5
// frame is older than stale_ms but within the stall hold
// (partner_native_stall=hold), still names slot 3 and the equip of the tick
// that last judged support. Every other support condition (type 12, body 1's
// equip, the observer's Ready and safety owners) is rechecked as before.
struct StallHoldIn {
 bool stall_hold=false;std::uint32_t stale_ms=0,stall_ms=0,age_ms=0;
 bool v5_valid=false;std::uint8_t slot=0xFF;std::uint16_t equip=0;
 bool was_support=false;std::uint16_t support_equip=0;
};
constexpr bool StallHold(const StallHoldIn& s) noexcept {
 return s.stall_hold && s.stall_ms>s.stale_ms && s.age_ms>s.stale_ms && s.age_ms<=s.stall_ms && s.v5_valid && s.slot==3 &&
  s.was_support && s.equip>0 && s.equip<0x800 && s.equip==s.support_equip;
}
// The observer's writer context through the same stall: it keeps slot 3 and
// support_equip (so Ready answers) while the observer's own newest v5 frame,
// fresh or stale, is at most stall_ms old and still names slot 3 and the equip
// of the driver's last support verdict (support_equip, 0 = none). Keyed on that
// verdict, not on the driver's support_stall: the driver sets support_stall
// only on a tick whose own read was already stale, so a frame that crossed
// stale_ms between the driver's read and the observer's would otherwise clear
// the context and refuse the whole stall (WS2 review, critic #4 early release).
// stall_hold, stale_ms, stall_ms, age_ms, v5_valid, slot, equip as StallHoldIn.
constexpr bool ContextHold(const StallHoldIn& s) noexcept {
 return s.stall_hold && s.stall_ms>s.stale_ms && s.age_ms<=s.stall_ms && s.v5_valid && s.slot==3 &&
  s.support_equip>0 && s.support_equip<0x800 && s.equip==s.support_equip;
}

// --- Unknown remote slot/equip debounce (run 0c9269ce; docs/anim-round2-throws.md
// "Unknown-slot glitch"). One live host frame mid-hold carried neither the v5 gun
// group nor the equip (the sender's own gun record read failed for one tick), so
// every consumer saw remote_slot=255 / remote_equip unknown: the driver's gate went
// unsupported (strip 0x58) for about 110 ms and the held throw was lost. The
// receiver (partner_net, per accepted input frame) now keeps the last complete
// support identity through such frames: while the driver's last support verdict
// names equip E (support_equip, published every driver tick) and the last complete
// frame (v5 gun group, slot 3, equip E) arrived at most kIdentityHoldMs ago, a frame
// whose slot and/or equip is UNKNOWN gets slot 3, equip E and that frame's gun group
// (partner_input::HoldIdentity), the way the stall freeze keeps the input level.
// A known part naming something else (another slot, another equip) is a real change
// and applies at once; an unknown run longer than kIdentityHoldMs applies too.
inline constexpr std::uint64_t kIdentityHoldMs=250;
// One frame's identity: v5 (version >= 5 with GunFlag::Valid), its slot (0xFF
// unknown) and its equip (XFlag::Equip, 1..65535).
struct Identity { bool v5=false; std::uint8_t slot=0xFF; bool equip_valid=false; std::uint16_t equip=0; };
constexpr bool SlotKnown(const Identity& id) noexcept { return id.v5 && id.slot<=15; }
constexpr bool EquipKnown(const Identity& id) noexcept { return id.equip_valid && id.equip>0; }
struct IdentityTrack {
 bool have=false;std::uint16_t equip=0;std::uint64_t known_ms=0;   // the last complete support identity (slot 3, equip)
 bool holding=false;std::uint64_t since_ms=0;std::uint32_t held=0;  // the current run of held frames
};
// pass: nothing held (no episode, or a complete frame); held: substitute the
// identity; resumed: the first complete frame of the same identity after a held
// run; changed: a real change (a complete or partly known frame names another
// slot/equip); expired: the unknown run outlived kIdentityHoldMs (it applies);
// off: the support verdict ended during a held run.
enum class IdHold : std::uint8_t { pass, held, resumed, changed, expired, off };
constexpr const char* IdHoldName(IdHold v) noexcept {
 return v==IdHold::held ? "held" : v==IdHold::resumed ? "resumed" : v==IdHold::changed ? "changed" : v==IdHold::expired ? "expired"
  : v==IdHold::off ? "off" : "pass";
}
constexpr bool IdHoldEnds(IdHold v) noexcept { return v==IdHold::resumed || v==IdHold::changed || v==IdHold::expired || v==IdHold::off; }
constexpr IdHold StepIdentity(IdentityTrack& t,const Identity& id,std::uint16_t support_equip,std::uint64_t now_ms) noexcept {
 const bool support=support_equip>0 && support_equip<0x800;
 const bool slot_known=SlotKnown(id),equip_known=EquipKnown(id);
 const bool was=t.holding;
 if(slot_known && equip_known){
  const bool match=support && id.slot==3 && id.equip==support_equip;
  const bool same=t.have && match && id.equip==t.equip;
  if(match){t.have=true;t.equip=id.equip;t.known_ms=now_ms;} else t.have=false;
  t.holding=false;
  return !was ? IdHold::pass : same ? IdHold::resumed : IdHold::changed;
 }
 if(!t.have || !support || support_equip!=t.equip){t.have=false;t.holding=false;return was ? IdHold::off : IdHold::pass;}
 if((slot_known && id.slot!=3) || (equip_known && id.equip!=t.equip)){t.have=false;t.holding=false;return IdHold::changed;}
 if(now_ms<t.known_ms || now_ms-t.known_ms>kIdentityHoldMs){t.have=false;t.holding=false;return IdHold::expired;}
 if(!t.holding){t.holding=true;t.since_ms=now_ms;t.held=0;}
 ++t.held;
 return IdHold::held;
}

// --- Handle call hardening (WS1; docs/anim-round2-throws.md "Handle call resolved").
// X = [prim+0x60] (stored by the gun prim base constructor at 0x101E1E7) is the
// process-wide equipment-handle manager [[gm]+0x98]+0x1E8 (= inventory+0x58 =
// ctl+0x28). Its vt+0x160 = 0xDC1740 flips bit 7 of byte 4 of the 6-byte row
// X+0x140[(h>>16)*6] and toggles the handle's model part; vt+0x38 = 0xDC64E0 can
// release the handle's model instance. Neither checks owner, bounds or the equip
// word. The gun plugin passes rec+0x240 (written only by the activation
// 0x105FDB0 at 0x10602E8, with rec+0x24C at 0x10602F0) to vt+0x160 at 0x10892CB
// (mode 28), 0x10880D3 (30), 0x108AA91 (33), 0x1087C05 (37) and, after vt+0x38
// at 0x1062683, at 0x1062696 in the iface reset 0x1061F50 (support classes 2/3
// only). B covers only the four input-driven modes. The gun plugin also passes
// rec+0x240 to 17 other X methods at 22 more sites (REC240_X_CALLS and
// REC240_X_CALLS_BY_HAND in the contract generator; docs "Other X calls"). Those
// are unaudited; behind them and the iface reset only C's per-job row check
// sees a stale handle (it latches support off; it cannot stop the in-job call).
// Offsets duplicated from partner_player_contract.hpp (this header stays pure
// and includable by fixtures); partner_input_hook.cpp static_asserts them equal.
namespace rec240 {
inline constexpr std::uintptr_t kPrimVtRva=0x23B3E80, kEngineVtRva=0x234B840;
inline constexpr std::size_t kPrimBlk=0x8, kPrimRec=0x58, kPrimEngine=0x60;
inline constexpr std::size_t kRecHandle=0x240, kRecEquip=0x24C, kRecClassWord=0x27C, kRecFlags=0x3C0, kRecSlotHand=0x3D9;
inline constexpr std::uint32_t kRecSwitch=0x80;
inline constexpr std::size_t kEngineCapacity=0x68, kEngineRows=0x140, kRowStride=6, kRowEquip=0, kRowOwner=2;
inline constexpr std::uint16_t kOwnerBody1=1;
}
// Spec WS1 C started detect-only (a foreign row only logged). The hidden runs
// at 5f11c7b showed no false positive: lab 1b85d7f2 and play 1be14967 /
// dc8cefc8 / 3a3a0913 logged only reason=ok owner=1 and no rec240_foreign,
// rec240_stale or unread. So a foreign rec+0x240 row (empty, range, equip,
// owner) now latches native support off for the mission
// (reason=rec240_foreign; mode=latch; latched=1). unread never latches.
inline constexpr bool kRec240Latch=true;
// The gun record body 1's native support would act on. chain: prim vtable,
// prim+0x8 == blk1 and prim+0x58 == rec (the existing record_ready identity).
// slot_handle: the Match-verified slot-3 handle Y+0x40[1*7+3] from
// partner_support_equip::Ready (0 = not ready).
struct RecordIn {
 bool chain=false,switching=true;
 std::uint16_t rec_equip=0,equip=0;
 std::uint8_t slot_hand=0xFF,cls=0;
 std::uint32_t rec_handle=0,slot_handle=0;
};
// The record holds the remote's equip in hand slot 3 and is not switching: the
// activation that wrote rec+0x24C wrote rec+0x240 in the same pass, so a
// gun<->support or support<->support switch is never judged on a stale handle.
constexpr bool RecordIdentity(const RecordIn& r) noexcept {
 return r.chain && r.equip>0 && r.equip<0x800 && r.rec_equip==r.equip && r.slot_hand==3 && !r.switching;
}
// B: HOLD/FIRE reach modes 28/30/33/37 only when every vt+0x160 call there
// hits the Match-verified row (owner 1, this equip, kind 12, index in range).
// Fails closed. It adds no refused frame to a normal equip/follow: until the
// activation runs, rec+0x24C still names the previous equip or rec+0x3D9 the
// gun slot (no identity), and for a kind-12 handle the activation stores
// rec+0x240 (0x10602E8) before rec+0x24C (0x10602F0); ReadRecord reads
// rec+0x24C first.
constexpr bool RecordReady(const RecordIn& r) noexcept {
 return RecordIdentity(r) && r.slot_handle!=0 && r.rec_handle==r.slot_handle;
}
// B diagnostic: the record has the identity but rec+0x240 is not the
// Match-verified slot-3 handle. This is never the gap of a normal equip/follow
// (see RecordReady): rec+0x24C, X vt+0x20 of rec+0x240 when the activation ran,
// names this equip, so rec+0x240 is in practice a same-equip handle on another
// row index (the stale-handle hazard itself). The phase-0 switch detector
// 0x103AED0 compares only the slot and the equip bits (h>>5)&0x7FF, so the
// per-frame path does not re-run the activation and the state persists. B
// refuses support until something re-runs it (a slot or equip change). The
// hook logs it as refused; reason=rec240_stale.
constexpr bool RecordStale(const RecordIn& r) noexcept {
 return RecordIdentity(r) && r.slot_handle!=0 && r.rec_handle!=r.slot_handle;
}
constexpr std::uint8_t SupportClass(std::uint32_t word27c) noexcept { return static_cast<std::uint8_t>((word27c>>6)&7u); }
// The iface reset's own gate (0x1062629..0x106263B: (rec+0x27C>>6)&7 in {2,3}).
constexpr bool SupportClassLive(std::uint8_t cls) noexcept { return cls==2 || cls==3; }
// C verdicts. skip: not judged (no identity, or not class 2/3); unread: the
// manager chain or a row read failed (never latches); empty/range/equip/owner:
// rec+0x240 does not name a live body-1 row (foreign).
enum class Rec240 : std::uint8_t { skip, ok, unread, empty, range, equip, owner };
constexpr const char* Rec240Name(Rec240 v) noexcept {
 return v==Rec240::ok ? "ok" : v==Rec240::unread ? "unread" : v==Rec240::empty ? "empty" : v==Rec240::range ? "range"
  : v==Rec240::equip ? "equip" : v==Rec240::owner ? "owner" : "skip";
}
constexpr bool Rec240Foreign(Rec240 v) noexcept {
 return v==Rec240::empty || v==Rec240::range || v==Rec240::equip || v==Rec240::owner;
}
constexpr bool Rec240Latches(Rec240 v,bool latch) noexcept { return latch && Rec240Foreign(v); }
struct Rec240Row {
 Rec240 verdict=Rec240::skip;
 RecordIn record{};
 std::uint32_t idx=0,capacity=0;
 std::uint16_t row_equip=0,row_owner=0;
};
// Read: bool(std::uintptr_t address, void* out, std::size_t size) noexcept,
// read-only and fault-safe. base: the image base (vtable identities only).
template<class Read> RecordIn ReadRecord(Read&& read,std::uintptr_t base,std::uintptr_t prim,std::uintptr_t rec,
                                         std::uintptr_t blk,std::uint16_t equip) noexcept {
 namespace k=rec240;
 RecordIn r{};r.equip=equip;
 std::uintptr_t vt=0,pblk=0,prec=0;std::uint32_t flags=0,word27c=0;
 if(!prim || !rec || !read(prim,&vt,sizeof(vt)) || vt!=base+k::kPrimVtRva || !read(prim+k::kPrimBlk,&pblk,sizeof(pblk)) ||
    pblk!=blk || !read(prim+k::kPrimRec,&prec,sizeof(prec)) || prec!=rec) return r;
 if(!read(rec+k::kRecEquip,&r.rec_equip,sizeof(r.rec_equip)) || !read(rec+k::kRecSlotHand,&r.slot_hand,sizeof(r.slot_hand)) ||
    !read(rec+k::kRecFlags,&flags,sizeof(flags)) || !read(rec+k::kRecHandle,&r.rec_handle,sizeof(r.rec_handle)) ||
    !read(rec+k::kRecClassWord,&word27c,sizeof(word27c))) return r;
 r.switching=(flags&k::kRecSwitch)!=0;r.cls=SupportClass(word27c);r.chain=true;
 return r;
}
// C: the row the game's vt+0x160 / vt+0x38 would act on, read through the
// calls' own receiver X = [prim+0x60] (its vtable must be the qualified
// 0x234B840 that partner_support_equip's Capture also requires).
template<class Read> Rec240Row CheckRow(Read&& read,std::uintptr_t base,std::uintptr_t prim,const RecordIn& record) noexcept {
 namespace k=rec240;
 Rec240Row out{};out.record=record;
 if(!RecordIdentity(record) || !SupportClassLive(record.cls)) return out;
 const std::uint32_t h=record.rec_handle;out.idx=h>>16;
 std::uintptr_t engine=0,vt=0,rows=0;
 if(!read(prim+k::kPrimEngine,&engine,sizeof(engine)) || !engine || !read(engine,&vt,sizeof(vt)) || vt!=base+k::kEngineVtRva ||
    !read(engine+k::kEngineCapacity,&out.capacity,sizeof(out.capacity)) || !out.capacity || out.capacity>0xFFFFu ||
    !read(engine+k::kEngineRows,&rows,sizeof(rows)) || !rows) {out.verdict=Rec240::unread;return out;}
 if(!h || out.idx==0xFFFFu || ((h>>5)&0x7FFu)==0) {out.verdict=Rec240::empty;return out;}
 if(out.idx>=out.capacity) {out.verdict=Rec240::range;return out;}
 const std::uintptr_t row=rows+std::uintptr_t(out.idx)*k::kRowStride;
 if(!read(row+k::kRowEquip,&out.row_equip,sizeof(out.row_equip)) || !read(row+k::kRowOwner,&out.row_owner,sizeof(out.row_owner))) {
  out.verdict=Rec240::unread;return out;
 }
 out.verdict=out.row_equip!=((h>>5)&0x7FFu) ? Rec240::equip : out.row_owner!=k::kOwnerBody1 ? Rec240::owner : Rec240::ok;
 return out;
}
}
