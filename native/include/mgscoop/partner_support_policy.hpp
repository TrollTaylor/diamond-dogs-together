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
// are unaudited; behind them and the iface reset only C's detect-only row check
// sees a stale handle.
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
// Detect-only first (spec WS1 C): a foreign rec+0x240 row logs
// reason=rec240_foreign and does NOT latch until a hidden run has shown no
// false positive; then this flips to true (support latched off for the mission).
inline constexpr bool kRec240Latch=false;
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
