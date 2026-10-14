#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include "camera_mode_window_contract.hpp"
#include "mgscoop/camera_mode_setter.hpp"
namespace mgscoop::camera_mode_window {
// Internal owned-window lease only. Every boolean comes from the existing
// validated natural body1 RunJobWindow, never Lua/public callers. The caller
// serializes this lease against foreign/nested jobs, mission teardown and
// global C0 updates, and retains existing paired-scene/copied-save admission.
// This helper qualifies no carry/sleep job or fallback path.
struct Scope {
 bool exactGate=false,ownedBody1=false,nativeSandbox=false,serialized=false;
 bool batch=false,stopping=false,faulted=false;
 std::uintptr_t image=0,player=0;
 std::uint32_t thread=0,ownerThread=0,generation=0,sceneEpoch=0,windowSequence=0;
 // Bound only by the actual core's compiled carry module, never Lua/config.
 camera_mode_setter::Provider presentationSetter=nullptr;
};
inline bool Range(std::uintptr_t p,std::size_t n) noexcept {return p&&n&&p<=std::numeric_limits<std::uintptr_t>::max()-n;}
inline bool Aligned(std::uintptr_t p) noexcept {return p&&!(p&7u);}
template<class Reader,class T> bool Get(Reader& r,std::uintptr_t p,T& out) noexcept {return Range(p,sizeof(T))&&r(p,&out,sizeof(T));}
inline const char* ScopeRefusal(const Scope& s,bool capturing) noexcept {
 if(!s.exactGate)return "c0_gate";
 if(!s.ownedBody1||!s.nativeSandbox||!s.serialized)return "owned_window";
 if(!s.thread||s.thread!=s.ownerThread)return "owner_thread";
 if(!s.generation||!s.sceneEpoch)return "lifetime";
 if(!s.windowSequence||!(s.windowSequence&1u))return "input_window";
 if(s.batch)return "batch";
 if(capturing&&(s.stopping||s.faulted))return "capture_disabled";
 if(!Aligned(s.image)||!Range(s.image,0x2a82c38)||!Aligned(s.player)||!Range(s.player,0x220))return "identity";
 return nullptr;
}
inline bool SameScope(const Scope& a,const Scope& b,bool capturing) noexcept {
 return !ScopeRefusal(b,capturing)&&a.image==b.image&&a.player==b.player&&a.thread==b.thread&&a.ownerThread==b.ownerThread&&a.generation==b.generation&&a.sceneEpoch==b.sceneEpoch&&a.windowSequence==b.windowSequence&&a.presentationSetter==b.presentationSetter;
}
// Full PE hash and existing partner gate remain prerequisites. This additional
// gate compares complete C0 ranges/explicit leaves and exact relocated slots.
template<class Reader> bool CodeGate(Reader& r,std::uintptr_t base,std::size_t span,std::uintptr_t& mismatch,
                                    camera_mode_setter::Provider setter=nullptr) noexcept {
 mismatch=0;if(!Range(base,span))return false;unsigned char bytes[8192]{};
 for(const auto& s:camera_mode_window_contract::sites){
  if(s.rva>span||s.size>span-s.rva||s.size>sizeof(bytes)||!r(base+s.rva,bytes,s.size)||std::memcmp(bytes,s.bytes,s.size)){mismatch=s.rva;return false;}
 }
 for(const auto& s:camera_mode_window_contract::slots){const auto off=s.vtable_rva+s.offset;std::uintptr_t v=0;
  if(off>span||sizeof(v)>span-off||s.target_rva>=span||!Get(r,base+off,v)){mismatch=off;return false;}
  if(v!=base+s.target_rva){camera_mode_setter::Receipt owned{};std::uintptr_t late=0;
   // Only the exact current carry presentation slot can compose. Every
   // other relocated virtual target retains its original equality check.
   if(off!=0x23c75f8||s.target_rva!=0x11f6520||!setter||!setter(base,base+off,v,owned)||
    !camera_mode_setter::Valid(owned,base,base+off,v)||!Get(r,base+off,late)||late!=v){mismatch=off;return false;}
  }
 }return true;
}
struct Identity {std::uintptr_t primary=0,interface=0;std::uint8_t requested=0;camera_mode_setter::Receipt setter{};};
inline bool SameOwner(const Identity& a,const Identity& b) noexcept {return a.primary==b.primary&&a.interface==b.interface&&camera_mode_setter::Same(a.setter,b.setter);}
// Never reads or writes interface+6C (the active mode). This reads the typed
// owner and requested byte only; it does not call any engine method.
template<class Reader> bool ReadIdentity(Reader& r,const Scope& s,Identity& out,const char*& reason) noexcept {
 out={};reason=nullptr;if(const char* why=ScopeRefusal(s,false)){reason=why;return false;}std::uintptr_t v=0,sync=0;std::uint32_t flags=0,count=0,realized=0,local=0;
 if(!Get(r,s.image+0x2a82c30,v)||v!=s.player||!Get(r,s.player,v)||v!=s.image+0x2295490){reason="player_identity";return false;}
 if(!Get(r,s.player+0xa8,sync)||sync||!Get(r,s.player+0x204,flags)||!(flags&1u)||(flags&0x40100u)||!Get(r,s.player+0x208,count)||count!=2||!Get(r,s.player+0x20c,realized)||realized!=2||!Get(r,s.player+0x214,local)||local){reason="provider_flags";return false;}
 // P+218 can be shadowed to1 by the existing owned window, so it is not
 // used as an independent local-body identity here.
 if(!Get(r,s.player+0xc0,out.interface)||!Aligned(out.interface)||out.interface<0x20){reason="c0_pointer";return false;}
 out.primary=out.interface-0x20;
 if(!Range(out.primary,0xa8)||!Get(r,out.primary,v)||v!=s.image+0x23c74c8||!Get(r,out.interface,v)||v!=s.image+0x23c7500){reason="c0_type";return false;}
 if(!Get(r,out.interface+8,v)||v!=s.player){reason="c0_owner";return false;}
 const auto setterSlot=s.image+0x23c7500+0xf8;
 if(!Get(r,setterSlot,v)){reason="c0_setter_slot";return false;}
 if(v==s.image+0x11f6520){out.setter={camera_mode_setter::Kind::native,s.image,setterSlot,v,v,0};}
 else if(!s.presentationSetter||!s.presentationSetter(s.image,setterSlot,v,out.setter)||
         !camera_mode_setter::Valid(out.setter,s.image,setterSlot,v)){
  reason="c0_setter_slot";return false;
 }
 // Preserve the actual installed pointer. Never replace a raw slot with an
 // alleged original value; captured serial/pointer must survive restoration.
 std::uintptr_t currentSetter=0;
 if(!Get(r,setterSlot,currentSetter)||currentSetter!=v){reason="c0_setter_changed";return false;}
 if(!Get(r,out.interface+0x6d,out.requested)){reason="c0_request_read";return false;}
 return true;
}
struct Reply {bool captured=false,changed=false,writeAttempted=false,restored=false;const char* reason="no_snapshot";};
enum class WriteStatus {written,conflict,fault};
class Token;
template<class Reader,class Sampler> bool Capture(Reader&,Sampler&,Token&,Reply&) noexcept;
template<class Reader,class Writer,class Sampler> bool Restore(Reader&,Writer&,Sampler&,Token&,Reply&) noexcept;
// Private ephemeral pointers never leave the internal Save object. A token
// cannot be copied or moved; every Restore attempt consumes it once.
class Token {
 Scope scope_{};Identity identity_{};bool armed_=false;
 template<class Reader,class Sampler> friend bool Capture(Reader&,Sampler&,Token&,Reply&) noexcept;
 template<class Reader,class Writer,class Sampler> friend bool Restore(Reader&,Writer&,Sampler&,Token&,Reply&) noexcept;
public:
 Token() noexcept=default;Token(const Token&)=delete;Token& operator=(const Token&)=delete;
 Token(Token&&)=delete;Token& operator=(Token&&)=delete;
 bool Armed() const noexcept {return armed_;}
};
// Reader and Sampler must refuse faults without throwing. Sampler reads fresh
// lease/thread/window/lifetime values before, between and after identity reads.
template<class Reader,class Sampler> bool Capture(Reader& r,Sampler& sample,Token& token,Reply& out) noexcept {
 out={};if(token.armed_){out.reason="snapshot_already_owned";return false;}
 const auto s=sample();if(const char* why=ScopeRefusal(s,true)){out.reason=why;return false;}
 Identity a{},b{};const char* why=nullptr;
 if(!ReadIdentity(r,s,a,why)){out.reason=why;return false;}
 const auto middle=sample();if(!SameScope(s,middle,true)){out.reason="scope_changed";return false;}
 if(!ReadIdentity(r,middle,b,why)){out.reason=why;return false;}
 const auto final=sample();if(!SameScope(s,final,true)){out.reason="scope_changed";return false;}
 if(!SameOwner(a,b)||a.requested!=b.requested){out.reason="c0_changed";return false;}
 token.scope_=s;token.identity_=b;token.armed_=true;out.captured=true;out.reason="c0_captured";return true;
}
// Writer performs exactly one bounded byte CAS(address,expected,desired) under
// fault handling. A conflicting requester is never overwritten or retried.
// Fault/stopping permit cleanup only while the original owned lease remains
// valid. Changed admission, generation, thread, epoch or object still refuses.
template<class Reader,class Writer,class Sampler> bool Restore(Reader& r,Writer& write,Sampler& sample,Token& token,Reply& out) noexcept {
 out={};if(!token.armed_)return false;
 const auto savedScope=token.scope_;const auto savedIdentity=token.identity_;token.armed_=false;
 const auto s=sample();if(!SameScope(savedScope,s,false)){out.reason="restore_scope_changed";return false;}
 Identity a{},b{};const char* why=nullptr;
 if(!ReadIdentity(r,s,a,why)){out.reason=why;return false;}
 if(!SameOwner(savedIdentity,a)){out.reason="restore_owner_changed";return false;}
 const auto middle=sample();if(!SameScope(savedScope,middle,false)){out.reason="restore_scope_changed";return false;}
 if(!ReadIdentity(r,middle,b,why)){out.reason=why;return false;}
 const auto before=sample();if(!SameScope(savedScope,before,false)){out.reason="restore_scope_changed";return false;}
 if(!SameOwner(savedIdentity,b)||!SameOwner(a,b)){out.reason="restore_owner_changed";return false;}
 if(a.requested!=b.requested){out.reason="restore_request_changed";return false;}
 out.changed=b.requested!=savedIdentity.requested;
 if(!out.changed){out.restored=true;out.reason="c0_unchanged";return true;}
 out.writeAttempted=true;const auto status=write(b.interface+0x6d,b.requested,savedIdentity.requested);
 if(status!=WriteStatus::written){out.reason=status==WriteStatus::conflict?"restore_conflict":"restore_write_fault";return false;}
 Identity after{};
 if(!ReadIdentity(r,before,after,why)){out.reason=why;return false;}
 const auto final=sample();if(!SameScope(savedScope,final,false)){out.reason="restore_scope_changed";return false;}
 if(!SameOwner(savedIdentity,after)){out.reason="restore_owner_changed";return false;}
 if(after.requested!=savedIdentity.requested){out.reason="restore_readback_changed";return false;}
 out.restored=true;out.reason="c0_restored";return true;
}
} // namespace mgscoop::camera_mode_window
