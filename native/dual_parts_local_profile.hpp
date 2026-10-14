#pragma once
#include <cstdint>
#include "native_pose_carry_participants.hpp"
namespace mgscoop::dual_parts_local_profile {
namespace cp=native_pose_carry_participants;
struct Profile {
 std::uintptr_t player=0,go=0,data=0,vars=0,script=0,services=0,map=0,manager=0,locator=0;
 std::uintptr_t current[6]{},selection=0;
 std::uint32_t refresh=0,faceCache=0,equipFlags=0;
 std::uint8_t type=0,parts=0,camo=0,auxiliary=0,currentType=0,currentParts=0,currentCamo=0,currentEncoded=0,currentAux=0;
 std::uint16_t face=0,currentFace=0,selector=0;
};
inline constexpr std::uint8_t partMap[256]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,8,9,10,0,11,4,5,6,3,12,13,0,14,15,16,17,18,19,20,21,22,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,23,23,23,23,23,23,23,23,23,23,23,23,24,24,24,24,24,24,24,24,24,24,24,24,25,25,25,25,25,25,25,25,25,25,25,25,26,27,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
inline constexpr std::uint8_t auxiliaryAllowed[26]={1,1,1,0,0,0,0,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
inline constexpr std::uint8_t encodedOneClass[26]={1,1,1,0,0,0,0,1,1,1,1,1,1,1,0,1,1,1,1,0,0,0,0,1,1,1};
// Complete native9CD030 desired/current branch, deliberately narrowed to
// ordinary type0/3 and populated face cache. Special/custom profiles and the
// sentinel native default-face getter remain unsupported, not guessed.
inline const char* Refusal(const Profile& p) noexcept {
 if(p.type!=0&&p.type!=3)return "profile_type_unsupported";
 if(p.parts>25||p.camo==0x73||p.camo==0x74||partMap[p.camo]>25)return "profile_special_unsupported";
 if(p.faceCache==0xff||p.faceCache>0xffff)return "profile_face_default_unqualified";
 if(p.refresh&1u)return "profile_native_refresh";
 const auto desired=partMap[p.camo]?partMap[p.camo]:p.parts;
 // The first controlled slice refuses a queued normalizer as well. Native
 // may write unchanged vars, but must not convert a different requested suit.
 if(desired!=p.parts||p.face!=p.faceCache)return "profile_normalizer_pending";
 std::uint8_t encoded=0;
 if(encodedOneClass[desired]){encoded=1;if(p.selector>=0x203&&p.selector<=0x209){constexpr std::uint8_t values[]={2,3,4,5,1,6,7};encoded=values[p.selector-0x203];}}
 const auto auxiliary=(p.equipFlags&0x100u)||!auxiliaryAllowed[desired]?std::uint8_t{0}:p.auxiliary;
 if(p.currentType!=p.type||p.currentParts!=desired||p.currentCamo!=p.camo||p.currentEncoded!=encoded||p.currentFace!=p.faceCache||p.currentAux!=auxiliary)return "profile_desired_current_changed";
 return nullptr;
}
inline bool Same(const Profile& a,const Profile& b) noexcept {
 if(a.player!=b.player||a.go!=b.go||a.data!=b.data||a.vars!=b.vars||a.script!=b.script||a.services!=b.services||a.map!=b.map||a.manager!=b.manager||a.locator!=b.locator||a.selection!=b.selection||
 a.refresh!=b.refresh||a.faceCache!=b.faceCache||a.equipFlags!=b.equipFlags||a.type!=b.type||a.parts!=b.parts||a.camo!=b.camo||a.auxiliary!=b.auxiliary||
 a.currentType!=b.currentType||a.currentParts!=b.currentParts||a.currentCamo!=b.currentCamo||a.currentEncoded!=b.currentEncoded||a.currentAux!=b.currentAux||
 a.face!=b.face||a.currentFace!=b.currentFace||a.selector!=b.selector)return false;
 for(unsigned k=0;k<6;++k)if(a.current[k]!=b.current[k])return false;return true;
}
// Caller must already obtain cp::Pair from actual shared/even core and check
// both constructor births, retained models and complete native code gate.
// This bounded reader has no native function invocation or writable Scope.
template<class R>bool ReadOnce(R& r,std::uintptr_t image,const cp::Pair& pair,Profile& out,const char** why=nullptr) noexcept {
 out={};const auto stage=[&](const char* value) noexcept {if(why)*why=value;};stage("profile_owner");
 if(!cp::Range(image,0xa01d000)||!cp::Range(pair.player,0x4d8)||pair.player<0x20||pair.go!=pair.player-0x20||!cp::Range(pair.data,0xc8)||pair.vars!=image+0x2c01360)return false;
 std::uintptr_t ptr=0,vt=0;std::uint32_t value=0;
 if(!cp::Read(r,image+0x2a82c30,ptr)||ptr!=pair.player||!cp::Read(r,pair.player,vt)||vt!=image+0x2295490||!cp::Read(r,pair.go,vt)||vt!=image+0x22953d0||
 !cp::Read(r,pair.player+0x60,ptr)||ptr!=pair.data||!cp::Read(r,pair.data,vt)||vt!=image+0x23ad970||
 !cp::Read(r,pair.player+0x214,value)||value||!cp::Read(r,pair.player+0x218,value)||value||
 !cp::Read(r,pair.player+0x140,ptr)||ptr!=pair.vars||!cp::Read(r,image+0x2c16920,ptr)||ptr!=pair.vars)return false;
 out.player=pair.player;out.go=pair.go;out.data=pair.data;out.vars=pair.vars;
 stage("profile_providers");
 if(!cp::Read(r,image+0x2c3a8a0,out.manager)||!cp::Range(out.manager,0x18)||(out.manager&7u)||!cp::Read(r,out.manager,vt)||vt!=image+0x2308648||
 !cp::Read(r,out.manager+8,out.locator)||out.locator!=image+0x2c6c0c0||
 !cp::Read(r,out.locator+0x98,out.services)||out.services!=image+0x2c00d40||!cp::Read(r,out.services,vt)||vt!=image+0x2289588||
 !cp::Read(r,out.services+0x10,ptr)||ptr!=pair.vars||!cp::Read(r,out.services+0x1a0,out.map)||out.map!=image+0x2b56d90||
 !cp::Read(r,out.map,vt)||vt!=image+0x25564f0||!cp::Read(r,pair.player+0x48,out.script)||out.script!=image+0x2c1bf20||
 !cp::Read(r,out.script,vt)||vt!=image+0x22973c0||
 !cp::Read(r,image+0x25564f0+0x240,ptr)||ptr!=image+0x1e02ba0||!cp::Read(r,image+0x22973c0+0x290,ptr)||ptr!=image+0x9d19f0)return false;
 stage("profile_vars");
 if(!cp::Read(r,pair.vars+0xfb,out.type)||!cp::Read(r,pair.vars+0xf8,out.parts)||!cp::Read(r,pair.vars+0xf9,out.camo)||!cp::Read(r,pair.vars+0xfc,out.face)||
 !cp::Read(r,pair.vars+0xfe,out.auxiliary)||!cp::Read(r,pair.go+0x4f4,out.faceCache)||!cp::Read(r,pair.go+0x4f0,out.equipFlags)||!cp::Read(r,out.script+0x194,out.refresh))return false;
 stage("profile_current_arrays");constexpr std::uint32_t offsets[]={0x40,0x48,0x50,0x58,0x60,0x68};
 for(unsigned k=0;k<6;++k)if(!cp::Read(r,pair.data+offsets[k],out.current[k])||!cp::Range(out.current[k],k==4?4:2)||(k==4&&(out.current[k]&1u)))return false;
 if(!cp::Read(r,out.current[0],out.currentType)||!cp::Read(r,out.current[1],out.currentParts)||!cp::Read(r,out.current[2],out.currentCamo)||
 !cp::Read(r,out.current[3],out.currentEncoded)||!cp::Read(r,out.current[4],out.currentFace)||!cp::Read(r,out.current[5],out.currentAux)||
 !cp::Read(r,pair.data+0xc0,out.selection)||!cp::Range(out.selection,2*0x3a)||(out.selection&1u)||!cp::Read(r,out.selection+8,out.selector))return false;
 for(unsigned k=0;k<6;++k)for(unsigned j=k+1;j<6;++j){const auto kn=k==4?4u:2u,jn=j==4?4u:2u;
  if(out.current[k]<out.current[j]+jn&&out.current[j]<out.current[k]+kn)return false;}
 stage("profile_ready");return true;
}
template<class R>bool ReadPair(R& r,std::uintptr_t image,const cp::Pair& pair,Profile& out,const char** why=nullptr) noexcept {
 out={};Profile a{},b{};if(!ReadOnce(r,image,pair,a,why)||!ReadOnce(r,image,pair,b,why))return false;
 if(!Same(a,b)){if(why)*why="profile_pair_changed";return false;}out=a;
 if(const auto refusal=Refusal(out)){if(why)*why=refusal;out={};return false;}return true;
}
} // namespace mgscoop::dual_parts_local_profile
