// D7 dog guard: pure policy (docs/vehicles-companions-design.md 6.12, 8.9). Original code.
#include "mgscoop/dog_guard.hpp"
namespace mgscoop::dog_guard {
std::uint8_t Encode(const Record& h) noexcept {
 if(!h.max) return 0;
 const unsigned state=h.state==kStateDown||h.life==0?2u:h.state==kStateStunned?1u:0u;
 unsigned life=0;
 if(h.life) {life=(unsigned(h.life)*31u+h.max-1u)/h.max;if(life<1u)life=1u;if(life>31u)life=31u;}
 if(state==2u) life=0;
 return static_cast<std::uint8_t>(kValid|(state<<5)|life);
}
Follow Decode(std::uint8_t b) noexcept {
 Follow f{};f.valid=(b&kValid)!=0;f.state=static_cast<std::uint8_t>((b>>5)&3u);f.life31=static_cast<std::uint8_t>(b&31u);return f;
}
Plan Guard(const Record& local,std::uint8_t follow,bool killIssued) noexcept {
 Plan p{};p.next=local;
 if(local.state==kStateDown) return p;
 if(p.next.pending||p.next.pendingStun) {p.next.pending=0;p.next.pendingStun=0;p.clearedPending=true;}
 const auto f=Decode(follow);
 const bool down=f.valid&&f.state==2;
 if(down) {
  if(!killIssued) {p.next.flags=static_cast<std::uint8_t>(p.next.flags|kFlagKill);p.kill=true;}
 } else {
  if((p.next.flags&kFlagKill)&&!killIssued) {p.next.flags=static_cast<std::uint8_t>(p.next.flags&~kFlagKill);p.clearedKill=true;}
  if(f.valid&&local.max) {
   unsigned target=f.life31?(unsigned(f.life31)*local.max+30u)/31u:1u;
   if(target<1u) target=1u;
   if(target>local.max) target=local.max;
   if(local.life!=target) {p.next.life=static_cast<std::uint16_t>(target);p.life=true;}
   if(f.state==1&&local.stun!=0) {p.next.stun=0;p.stun=true;}
   else if(f.state!=1&&local.state==kStateStunned&&local.stun!=local.stunMax) {p.next.stun=local.stunMax;p.stun=true;}
  }
 }
 p.write=!Same(p.next,local);
 return p;
}
const char* InstallRefusal(bool partnerGate,bool companionHostOwned) noexcept {
 return !companionHostOwned?"key_off":!partnerGate?"partner_gate":nullptr;
}
Admission Admit(bool guest,bool requestGuest,std::uint8_t arm,std::uint8_t labOp,const Scope& s) noexcept {
 if(requestGuest!=guest||(arm&&!guest)||arm>2) return {"role"};
 if(!s.installed) return {"admission"};
 if(labOp&&!s.labMode) return {"lab_only"};
 if(!arm&&!labOp) return {};
 if(!s.exact) return {"admission"};
 if(!s.runtime||s.terminating||!s.mission||!s.configured) return {"scope"};
 const bool linked=s.paired&&s.linkUp&&s.effective>=3&&s.epoch;
 if(arm==1&&linked) return {nullptr,Grant::full,labOp!=0};
 if(!arm) return linked?Admission{nullptr,Grant::none,true}:Admission{"scope"};
 // A full arm without the link, or a hold: an armed lease is held drop-only; nothing new is armed.
 if(s.armed) return {"hold",Grant::hold,false};
 return {arm==2?"not_armed":"scope"};
}
Plan Lab(const Record& local,std::uint8_t op,std::uint8_t pct) noexcept {
 Plan p{};p.next=local;
 if(local.state==kStateDown||pct>100) return p;
 if(op==1&&local.max) {
  if(pct==0) {p.next.flags=static_cast<std::uint8_t>(p.next.flags|kFlagKill);p.kill=true;}
  else {unsigned v=unsigned(local.max)*pct/100u;if(v<1u)v=1u;p.next.life=static_cast<std::uint16_t>(v);p.life=p.next.life!=local.life;}
 } else if(op==2&&local.stunMax) {
  p.next.stun=static_cast<std::uint16_t>(unsigned(local.stunMax)*pct/100u);p.stun=p.next.stun!=local.stun;
 }
 p.write=!Same(p.next,local);
 return p;
}
} // namespace mgscoop::dog_guard
