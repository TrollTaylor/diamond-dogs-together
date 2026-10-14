#include <cstdio>
#include "native_pose_hook_groups.hpp"
#include "carry_dispatch_lifetime.hpp"
namespace g=mgscoop::native_pose_hook_groups;namespace d=mgscoop::carry_dispatch_lifetime;
int main(){unsigned count=0;auto check=[&](bool v,const char* label){++count;if(!v){std::printf("failed %s\n",label);return false;}return true;};
 struct Hook{std::uintptr_t rva=0;void* wrapper=nullptr;};struct Patch{std::uintptr_t rva=0,wrapper=0;unsigned char original[5]{};};Hook hooks[g::total]{};Patch patches[g::total]{};
 for(std::size_t n=0;n<g::total;++n){hooks[n].rva=g::expected[n];hooks[n].wrapper=reinterpret_cast<void*>(0x700000+n*0x10);}
 const auto prefix=[](std::uintptr_t rva,unsigned char(&bytes)[5])noexcept{for(unsigned n=0;n<5;++n)bytes[n]=static_cast<unsigned char>(rva>>n);return true;};
 const auto read=[&](std::uintptr_t rva,void* bytes,std::size_t n)noexcept{unsigned char expected[5]{};if(n!=5||!prefix(rva,expected))return false;for(unsigned i=0;i<5;++i)static_cast<unsigned char*>(bytes)[i]=expected[i];return true;};
 if(!check(g::total==47&&g::deferredFirst==46&&g::varsFirst==13&&g::providerFirst==20&&g::routeFirst==28&&g::originFirst==36&&g::queueFirst==38,"composed boundaries"))return 1;
 if(!check(g::ValidateOrder([&](std::size_t n)noexcept{return hooks[n].rva;}),"actual ordered install vector"))return 1;
 if(!check(d::InitializeHookReceipts(hooks,patches,prefix,read)&&patches[46].rva==0xfe00d0&&patches[45].rva==0x5c4910&&patches[38].rva==0xfdea70&&patches[37].rva==0x11268b0&&patches[36].rva==0xbf3a80&&patches[35].rva==0xbf91b0&&patches[27].rva==0x3a1db30&&patches[12].rva==0x111cba0,"all47 actual receipts incl Q/origin/arena/route/camera"))return 1;
 const auto original=hooks[13].rva;hooks[13].rva=hooks[20].rva;
 if(!check(!g::ValidateOrder([&](std::size_t n)noexcept{return hooks[n].rva;}),"misindexed vars/provider entry rejected"))return 1;hooks[13].rva=original;
 hooks[46].rva=0;
 if(!check(!g::ValidateOrder([&](std::size_t n)noexcept{return hooks[n].rva;}),"omitted final setter entry rejected"))return 1;hooks[46].rva=g::expected[46];
 const auto failed=[&](std::uintptr_t rva,void* bytes,std::size_t n)noexcept{return rva!=g::expected[46]&&read(rva,bytes,n);};
 if(!check(!d::InitializeHookReceipts(hooks,patches,prefix,failed),"last-entry prefix failure prevents hook installation"))return 1;
 std::printf("%u composite actual-hook source checks\n",count);return 0;}
