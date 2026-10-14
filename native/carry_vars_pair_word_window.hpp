#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include "mgscoop/native_pose_window.hpp"
#include "carry_vars_pair_word_contract.hpp"
namespace mgscoop::partner_input_hook {
// Must be implemented by the actual shared/exclusive writer domain. True
// requires installed full-byte verified gates for ALL seven native/Lua/initial/bulk-reset
// writers listed in the corrected exact vars-word manifest. No callback/bool override.
bool NativePoseVarsPairWordOwnedWindow() noexcept;
// Cleanup-only identity receipt; admits a faulted owned writer solely to restore
// this unchanged field, never a native pose/start callback.
bool NativePoseVarsPairWordCleanupWindow(const NativePoseJobContext&) noexcept;
}
namespace mgscoop::carry_vars_pair_word_window {
namespace ph=partner_input_hook;
struct Owner {
 std::uintptr_t image=0,player=0,go=0,block=0,state=0,cs=0,entry=0,vars=0;
 std::uint32_t generation=0,epoch=0,thread=0,sequence=0;
};
inline bool Same(const Owner& a,const Owner& b) noexcept {
 return a.image==b.image&&a.player==b.player&&a.go==b.go&&a.block==b.block&&a.state==b.state&&a.cs==b.cs&&a.entry==b.entry&&a.vars==b.vars&&a.generation==b.generation&&a.epoch==b.epoch&&a.thread==b.thread&&a.sequence==b.sequence;
}
inline bool SameLease(const Owner& a,const Owner& b) noexcept {
 auto x=a,y=b;x.sequence=y.sequence=0;return Same(x,y);
}
struct Shadow {
 Owner owner{};std::uint16_t word=0xffff;bool bound=false;
 // Only discard after caller's full native carry/sleep retirement and successful
 // Closed receipt. Shadow values never grant model lifetime/control permission.
};
class Token {
 friend struct Access;
 Owner owner_{};std::uint16_t local_=0xffff,installed_=0xffff;
 std::uint16_t remote_=0xffff;bool active_=false,restored_=false,restoreWritten_=false;
public:
 Token()=default;Token(const Token&)=delete;Token& operator=(const Token&)=delete;
 bool active()const noexcept{return active_;}
 bool restored()const noexcept{return restored_;}
};
struct Access {
 static Owner& owner(Token& t)noexcept{return t.owner_;}
 static std::uint16_t& local(Token& t)noexcept{return t.local_;}
 static std::uint16_t& installed(Token& t)noexcept{return t.installed_;}
 static bool& active(Token& t)noexcept{return t.active_;}
 static bool& restored(Token& t)noexcept{return t.restored_;}
 static bool& restoreWritten(Token& t)noexcept{return t.restoreWritten_;}
 static std::uint16_t& remote(Token& t)noexcept{return t.remote_;}
};
// Consume a completed token before the next actual window. An outstanding
// restoration is never erased by renewal.
inline bool Renew(Token& t) noexcept {
 if(t.active())return false;Access::owner(t)={};Access::local(t)=Access::installed(t)=Access::remote(t)=0xffff;
 Access::restored(t)=false;Access::restoreWritten(t)=false;return true;
}
inline bool Range(std::uintptr_t p,std::size_t n) noexcept {
 return p&&n&&p<=std::numeric_limits<std::uintptr_t>::max()-n;
}
template<class Reader,class T>bool At(Reader& r,std::uintptr_t p,T& v)noexcept{return Range(p,sizeof(v))&&r(p,&v,sizeof(v));}
template<class Reader>bool CodeGate(Reader& r,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch)noexcept{
 mismatch=0;if(!Range(image,span))return false;unsigned char bytes[4096]{};
 for(const auto& p:carry_vars_pair_word_contract::sites){
  if(p.rva>span||p.size>span-p.rva){mismatch=p.rva;return false;}
  for(std::size_t off=0;off<p.size;){const auto n=(p.size-off<sizeof(bytes)?p.size-off:sizeof(bytes));
   if(!r(image+p.rva+off,bytes,n)){mismatch=p.rva+off;return false;}
   for(std::size_t j=0;j<n;++j)if(bytes[j]!=p.bytes[off+j]){mismatch=p.rva+off+j;return false;}off+=n;
  }
 }
 constexpr auto at=std::uintptr_t{0x228b558};constexpr auto n=sizeof(carry_vars_pair_word_contract::field_name);
 if(at>span||n>span-at||!r(image+at,bytes,n)){mismatch=at;return false;}
 for(std::size_t j=0;j<n;++j)if(bytes[j]!=carry_vars_pair_word_contract::field_name[j]){mismatch=at+j;return false;}return true;
}
// Identity is derived only inside the ACTUAL mandatory window; it does not
// coerce the ordinary read-only Scope's odd sequence to an even value.
template<class Reader>bool ReadOwner(Reader& r,const ph::NativePoseJobContext& c,std::uintptr_t image,Owner& out,bool cleanup=false)noexcept{
 out={};if(!ph::NativePoseOwnedWindow()||!ph::NativePoseVarsPairWordOwnedWindow()||
  (cleanup&&!ph::NativePoseVarsPairWordCleanupWindow(c))||!c.modelReady||!c.selectedBodyOwned||c.selectedBody!=1||
  !c.generation||!c.sceneEpoch||!c.thread||!(c.sequence&1u)||!Range(image,0xa01d000)||image%0x1000||
  !c.player||c.go>std::numeric_limits<std::uintptr_t>::max()-0x20||c.player!=c.go+0x20||!c.block||!c.state||!c.modelCS||!c.modelEntry)return false;
 std::uintptr_t p=0,vt=0,vars=0,cs=0,entry=0,pool=0,blocks=0;std::uint32_t n=0,flags=0;std::uint8_t blockIndex=0;
 if(!At(r,image+0x2a82c30,p)||p!=c.player||!At(r,p,vt)||vt!=image+0x2295490||
  !At(r,p+0x204,flags)||!(flags&1u)||(flags&0x100u)||!At(r,p+0xa8,vt)||vt||
  !At(r,p+0x208,n)||n!=2||!At(r,p+0x20c,n)||n!=2||!At(r,p+0x214,n)||n||!At(r,p+0x218,n)||(!cleanup&&n!=1)||!At(r,p+0x78,pool)||!At(r,pool+1,blockIndex)||blockIndex>=16||
  !At(r,p+0x378,blocks)||!Range(blocks,16*0x6a8)||blocks+std::uintptr_t(blockIndex)*0x6a8!=c.block||
  (!cleanup&&!ph::RequireNativePoseOwnedJob(reinterpret_cast<void*>(c.go),blockIndex))||
  !At(r,image+0x2c16920,vars)||vars!=image+0x2c01360||!At(r,p+0x140,vt)||vt!=vars||
  !At(r,c.block,vt)||vt!=image+0x2295210||!At(r,c.block+8,n)||n!=1||!At(r,c.block+0xc,n)||n!=1||
  !At(r,c.block+0x138,vt)||vt!=p||!At(r,c.block+0x140,vt)||vt!=vars||!At(r,c.block+0x78,vt)||vt!=c.state||
  !At(r,c.state,vt)||(vt!=image+0x23b09f0&&vt!=image+0x23b0c70)||
  !At(r,c.state+0x408,vt)||vt!=c.block||!At(r,c.state+8,n)||n!=1||!At(r,c.state+0x238,cs)||cs!=c.modelCS||
  !At(r,c.block+0x10,vt)||vt!=cs||!At(r,cs,vt)||vt!=image+0x22e6070||!At(r,cs+8,n)||n!=1||
  !At(r,cs+0xc,n)||n!=1||!At(r,cs+0x10,entry)||entry!=c.modelEntry||
  !At(r,p+0xb0,vt)||vt!=c.pp||(!cleanup&&(!At(r,c.pp,vt)||vt!=c.windowPPVT||!At(r,c.pp+0x8,vt)||vt!=c.source)))return false;
 out={image,p,c.go,c.block,c.state,cs,entry,vars,c.generation,c.sceneEpoch,c.thread,c.sequence};return true;
}
template<class Reader>bool ReadPair(Reader& r,const ph::NativePoseJobContext& c,std::uintptr_t image,Owner& o,std::uint16_t& word,bool cleanup=false)noexcept{
 o={};word=0xffff;Owner a{},b{};std::uint16_t x=0,y=0;
 if(!ReadOwner(r,c,image,a,cleanup)||!At(r,a.vars+0x10e,x)||!ReadOwner(r,c,image,b,cleanup)||!At(r,b.vars+0x10e,y)||!Same(a,b)||x!=y)return false;
 o=a;word=x;return true;
}
// CompareExchange must be an aligned16-bit CAS bound only to the exact field;
// it reports the observed old word separately. No raw memcpy writer is valid.
template<class Reader,class CompareExchange>bool Before(Reader& r,CompareExchange& cas,const ph::NativePoseJobContext& c,std::uintptr_t image,Shadow& s,Token& t)noexcept{
 if(t.active()||t.restored())return false;Owner o{};std::uint16_t local=0,observed=0;
 if(!ReadPair(r,c,image,o,local)||(s.bound&&!SameLease(s.owner,o)))return false;
 const std::uint16_t remote=s.bound?s.word:0xffff;
 if(!cas(o.vars+0x10e,local,remote,observed)||observed!=local)return false;
 Access::owner(t)=o;Access::local(t)=local;Access::installed(t)=remote;Access::active(t)=true;
 Owner after{};std::uint16_t word=0;
 if(!ReadPair(r,c,image,after,word)||!Same(o,after)||word!=remote)return false;return true;
}
template<class Reader,class CompareExchange>bool After(Reader& r,CompareExchange& cas,const ph::NativePoseJobContext& c,std::uintptr_t image,Shadow& s,Token& t)noexcept{
 if(!t.active()||t.restored())return false;Owner o{};std::uint16_t remote=0,observed=0;
 if(!ReadPair(r,c,image,o,remote,true)||!Same(o,Access::owner(t)))return false;
 // Retain the native result once restoration is committed. If the verifying
 // read fails after CAS, a cleanup retry must not mistake the restored local
 // value for the remote result or perform another write.
 if(!Access::restoreWritten(t)){
  if(!cas(o.vars+0x10e,remote,Access::local(t),observed)||observed!=remote)return false;
  Access::remote(t)=remote;Access::restoreWritten(t)=true;
 }else if(remote!=Access::local(t))return false;
 Owner after{};std::uint16_t local=0;
 if(!ReadPair(r,c,image,after,local,true)||!Same(o,after)||local!=Access::local(t))return false;
 s.owner=o;s.word=Access::remote(t);s.bound=true;Access::active(t)=false;Access::restored(t)=true;return true;
}
// Called ONLY from the coordinator's genuine Closed receipt. No writes here.
inline bool CanRetireShadow(const ph::NativePoseJobContext& c,bool cleanupComplete,bool nativeParticipantsRetired,const Shadow& s,const Token& t)noexcept{
 if(!cleanupComplete||!nativeParticipantsRetired||!t.restored()||t.active()||!s.bound||c.sequence&1u)return false;
 return c.player==s.owner.player&&c.go==s.owner.go&&c.block==s.owner.block&&c.state==s.owner.state&&
  c.modelCS==s.owner.cs&&c.modelEntry==s.owner.entry&&c.generation==s.owner.generation&&c.sceneEpoch==s.owner.epoch&&c.thread==s.owner.thread&&c.sequence==s.owner.sequence+1u;
}
} // namespace mgscoop::carry_vars_pair_word_window
