#include "native_pose_message_lifetime.hpp"
#include <map>
#include <cstdio>
#include <cstring>
using namespace mgscoop;namespace m=native_pose_message_lifetime;
struct Reader{std::map<std::uintptr_t,unsigned char> mem;std::uintptr_t flip=0;unsigned hits=0;
 template<class T>void Put(std::uintptr_t at,T v){auto* b=reinterpret_cast<const unsigned char*>(&v);for(unsigned i=0;i<sizeof(v);++i)mem[at+i]=b[i];}
 bool operator()(std::uintptr_t at,void* out,std::size_t n)noexcept{auto* b=static_cast<unsigned char*>(out);for(std::size_t i=0;i<n;++i){auto x=mem.find(at+i);if(x==mem.end())return false;b[i]=x->second;}if(at==flip&&++hits==2)b[6]^=1;return true;}
};
constexpr std::uintptr_t image=0x140000000,player=0x5000000,provider=0x6000000,slab=0x6100000,iface=provider+0x1c40+3*0x28;
Reader Make(){Reader r;r.Put(image+0x2a82c30,player);r.Put(player,image+0x2295490);r.Put<std::uint32_t>(player+0x204,1);r.Put<std::uintptr_t>(player+0xa8,0);r.Put<std::uint32_t>(player+0x208,2);r.Put<std::uint32_t>(player+0x20c,2);r.Put<std::uint32_t>(player+0x214,0);r.Put(image+0x2b72c00,provider);r.Put(provider,image+0x20e9b90);r.Put(player+0x148,iface);r.Put(iface,image+0x20e9b60);r.Put(iface+8,provider);r.Put<std::uintptr_t>(iface+0x10,0);r.Put<std::uint32_t>(iface+0x18,0xb8071b05);r.Put<std::uint32_t>(iface+0x20,1);r.Put<std::uint8_t>(image+0x2b72c08,1);r.Put(provider+8,slab);r.Put<std::uintptr_t>(provider+0x10,0);r.Put<std::uint64_t>(provider+0x18,0x500);r.Put<std::uint64_t>(provider+0x20,0x500);r.Put<std::uint16_t>(provider+0x2a,1);r.Put(provider+0x2340,slab);r.Put(provider+0x2350,slab+0x280);r.Put(provider+0x2360,slab);r.Put(provider+0x2368,slab+0x280);r.Put<std::uint16_t>(provider+0x2348,64);r.Put<std::uint16_t>(provider+0x234a,1);r.Put<std::uint16_t>(provider+0x234c,1);
 for(unsigned i=0;i<64;++i){r.Put<std::uint32_t>(slab+i*8,0);r.Put<std::uint16_t>(slab+i*8+4,0xffff);r.Put<std::uint16_t>(slab+i*8+6,i==63?0xffff:static_cast<std::uint16_t>(i+1));r.Put<std::uint16_t>(slab+0x200+i*2,0xffff);}
 r.Put<std::uint32_t>(slab,0xb8071b05);r.Put<std::uint16_t>(slab+4,3);r.Put<std::uint16_t>(slab+6,0xffff);r.Put<std::uint16_t>(slab+0x200+(0xb8071b05%64)*2,0);return r;}
Reader Code(){Reader r;const auto bytes=[&](const auto& sites){for(const auto& site:sites)for(std::size_t i=0;i<site.size;++i)r.mem[image+site.rva+i]=site.bytes[i];};
 bytes(native_pose_message_lifetime_contract::sites);bytes(carry_message_provider_contract::sites);
 const auto slots=[&](const auto& sites){for(const auto& site:sites)r.Put(image+site.rva,image+site.target);};
 slots(native_pose_message_lifetime_contract::slots);slots(carry_message_provider_contract::slots);
 for(const auto& c:carry_message_provider_contract::constants)r.Put(image+c.rva,c.value);return r;}
int main(){unsigned n=0,bad=0;auto ck=[&](bool v,const char* s){++n;if(!v){++bad;std::printf("FAIL %s\n",s);}};m::Observation o{};auto r=Make();ck(m::ReadPair(r,image,player,o)&&o.keyEntry==0&&o.index==3,"complete current key membership/null listener");
 auto reject=[&](auto f,const char* s){auto x=Make();f(x);m::Observation z{};ck(!m::ReadPair(x,image,player,z),s);};
 reject([](auto& x){x.Put<std::uintptr_t>(iface+0x10,0x7770000);},"listener not null cannot qualify End closure");
 reject([](auto& x){x.Put<std::uint16_t>(slab+4,4);},"key maps to another typed interface");
 reject([](auto& x){x.Put<std::uint16_t>(slab+6,0);},"active bucket cycle");
 reject([](auto& x){x.Put<std::uint16_t>(slab+0x200+5*2,64);},"bucket index outside real map allocation");
 reject([](auto& x){x.Put<std::uint16_t>(slab+1*8+6,1);},"free-list cycle");
 reject([](auto& x){x.Put<std::uint16_t>(provider+0x234a,2);},"stored active count does not match topology");
 reject([](auto& x){x.Put<std::uint16_t>(provider+0x234c,0);},"active/free node alias");
 reject([](auto& x){x.Put<std::uintptr_t>(provider+0x10,0x7770000);},"arena destructor callbacks unresolved");
 reject([](auto& x){x.Put(provider+0x2368,slab+0x288);},"changed backing interior identity");
 reject([](auto& x){x.Put<std::uint64_t>(provider+0x20,0x504);},"unqualified extra arena allocation");
 reject([](auto& x){x.Put<std::uint32_t>(player+0x204,0x101);},"online/sync context refused");
 reject([](auto& x){x.Put(image+0x2b72c00,provider+8);},"replaced global provider refused");
 r=Make();r.flip=slab;ck(!m::ReadPair(r,image,player,o),"map mutation between coherent samples");
 r=Make();ck(m::ReadPair(r,image,player,o),"fresh current owner again");
 ck(m::ProtectsSetter(o,iface)&&m::ProtectsSetter(o,iface-8)&&m::ProtectsSetter(o,slab-8),"all overlapping provider/map writes protected");
 ck(!m::ProtectsSetter(o,0x8000000),"foreign object setter remains stock");
 ck(m::ProtectsInterface(o,iface)&&m::ProtectsInterface(o,provider-8)&&m::ProtectsInterface(o,slab-8),"unregister cannot write retained interface/map with mismatched provider");
 ck(!m::ProtectsInterface(o,0x8000000),"foreign unregister remains stock");
 ck(m::ProtectsArena(o,provider+8)&&m::ProtectsArena(o,provider-8)&&m::ProtectsArena(o,slab-8),"arena reset cannot clear or free retained allocation/map");
 ck(!m::ProtectsArena(o,0x8000000),"foreign arena remains stock");
 m::Observation other=o;other.used=2;ck(m::SameLease(o,other)&&!m::Same(o,other),"lifetime identity separate from current mutable map cardinality");
 ck(native_pose_message_lifetime_contract::hookCount==8&&native_pose_message_lifetime_contract::hookRvas[7]==0x3a1db30,"complete provider hook vector includes final arena reset");
 auto code=Code();std::uintptr_t mismatch=0;ck(m::CodeGate(code,image,0xa01d000,mismatch)&&!mismatch,"all full bodies/slots/constants current");
 const auto& finalSite=native_pose_message_lifetime_contract::sites[sizeof(native_pose_message_lifetime_contract::sites)/sizeof(native_pose_message_lifetime_contract::sites[0])-1];
 code.mem[image+finalSite.rva+finalSite.size-1]^=1;ck(!m::CodeGate(code,image,0xa01d000,mismatch)&&mismatch==finalSite.rva+finalSite.size-1,"last byte of exact arena reset tail thunk checked");
 code=Code();const auto& slot=native_pose_message_lifetime_contract::slots[0];code.Put(image+slot.rva,image+slot.target+1);
 ck(!m::CodeGate(code,image,0xa01d000,mismatch)&&mismatch==slot.rva,"typed final slot replacement");
 code=Code();ck(!m::CodeGate(code,image,slot.rva+sizeof(std::uintptr_t)-1,mismatch),"truncated final slot span refused");
 std::printf("native_pose_message_lifetime_tests %u checks, %u failures (pure only)\n",n,bad);return bad?1:0;}
