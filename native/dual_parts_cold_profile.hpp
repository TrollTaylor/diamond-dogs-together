#pragma once
#include <cstring>
#include "dual_parts_local_profile.hpp"
namespace mgscoop::dual_parts_cold_profile {
namespace p=dual_parts_local_profile;namespace cp=native_pose_carry_participants;
// Private coherent stamp, never exported and never mutation/lease permission.
struct Stamp {cp::Pair models{};p::Profile profile{};};
inline std::uint32_t Reason(const char* why) noexcept {
 if(!why)return 0;
 constexpr const char* labels[]={"profile_type_unsupported","profile_special_unsupported","profile_face_default_unqualified",
 "profile_native_refresh","profile_normalizer_pending","profile_desired_current_changed","profile_owner","profile_providers",
 "profile_vars","profile_current_arrays","profile_pair_changed","cold_models","cold_model_changed"};
 for(unsigned k=0;k<sizeof(labels)/sizeof(labels[0]);++k)if(!std::strcmp(why,labels[k]))return k+1;
 return 255;
}
inline bool Same(const Stamp& a,const Stamp& b) noexcept {
 return cp::Same(a.models,b.models)&&cp::CoherentCallbacks(a.models.body[0],b.models.body[0])&&
 cp::CoherentCallbacks(a.models.body[1],b.models.body[1])&&p::Same(a.profile,b.profile);
}
template<class R>bool Read(R& r,std::uintptr_t image,std::uintptr_t player,Stamp& out,std::uint32_t& reason) noexcept {
 out={};reason=Reason("cold_models");cp::Pair a{},b{};p::Profile first{},last{};const char* why=nullptr;
 if(!cp::ReadPair(r,image,player,a))return false;
 if(!p::ReadOnce(r,image,a,first,&why)||!p::ReadOnce(r,image,a,last,&why)){reason=Reason(why);return false;}
 if(!p::Same(first,last)){reason=Reason("profile_pair_changed");return false;}
 if(!cp::ReadPair(r,image,player,b)||!cp::Same(a,b)||!cp::CoherentCallbacks(a.body[0],b.body[0])||!cp::CoherentCallbacks(a.body[1],b.body[1])){
  reason=Reason("cold_model_changed");return false;
 }
 out.models=b;out.profile=last;reason=Reason(p::Refusal(last));return true;
}
// Supports observing an unsupported/mismatched ordinary profile without ever
// granting forwarding. The real outer cold wrapper retains all no-hold,
// copied-save/thread/even/ticket/fresh Arm+birth+post-pair gates.
}
