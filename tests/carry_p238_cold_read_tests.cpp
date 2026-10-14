#include "carry_p238_cold_read.hpp"
#include <cstdio>
#include <cstring>
#include <map>
namespace p=mgscoop::carry_p238_cold_read;
struct Mem{std::map<std::uintptr_t,unsigned char> v;template<class T>void Put(std::uintptr_t a,T x){const auto* b=reinterpret_cast<const unsigned char*>(&x);for(std::size_t n=0;n<sizeof(x);++n)v[a+n]=b[n];}bool operator()(std::uintptr_t a,void* o,std::size_t n){for(std::size_t i=0;i<n;++i){auto it=v.find(a+i);if(it==v.end())return false;static_cast<unsigned char*>(o)[i]=it->second;}return true;}};
int main(){unsigned n=0,f=0;auto check=[&](bool ok){++n;if(!ok)++f;};constexpr std::uintptr_t image=0x140000000,P=0x10000,C0=0x20000,C1=0x30000,E=0x40000;Mem m;m.Put(P+0x238,C0);m.Put(C0,image+0x22e6070);m.Put(C0+8,std::uint32_t{1});m.Put(C0+0xc,std::uint32_t{0});m.Put(C0+0x10,E);p::Observation o{};
 check(p::Observe(m,image,P,C0,C1,o)&&o.read&&o.typed&&o.body0&&!o.body1&&o.count==1);m.Put(C0+8,std::uint32_t{2});check(p::Observe(m,image,P,C0,C1,o)&&o.typed&&o.count==2);
 m.Put(P+0x238,C1);m.Put(C1,image+0x22e6070);m.Put(C1+8,std::uint32_t{1});m.Put(C1+0xc,std::uint32_t{1});m.Put(C1+0x10,E);check(p::Observe(m,image,P,C0,C1,o)&&o.typed&&!o.body0&&o.body1);
 m.Put(P+0x238,std::uintptr_t{0});check(p::Observe(m,image,P,C0,C1,o)&&o.read&&!o.typed&&o.reason==2);
 m.Put(P+0x238,C1);m.Put(C1,image+0x1234);check(p::Observe(m,image,P,C0,C1,o)&&o.vtRva==0x1234&&!o.typed&&o.reason==5);
 m.Put(C1,std::uintptr_t{0x90000});check(p::Observe(m,image,P,C0,C1,o)&&!o.vtRva&&!o.typed&&o.reason==4);
 m.Put(C1,image+0x22e6070);m.Put(C1+8,std::uint32_t{2});check(p::Observe(m,image,P,C0,C1,o)&&!o.typed&&o.reason==7);
 m.Put(C1+8,std::uint32_t{1});m.Put(C1+0x10,E+1);check(p::Observe(m,image,P,C0,C1,o)&&!o.typed&&o.reason==7);
 m.Put(C1+0x10,E);unsigned calls=0;auto drift=[&](std::uintptr_t a,void* out,std::size_t z){if(a==P+0x238&&++calls==2){const std::uintptr_t changed=C0;std::memcpy(out,&changed,z);return true;}return m(a,out,z);};check(!p::Observe(drift,image,P,C0,C1,o)&&o.reason==8);
 std::printf("carry_p238_cold_read: %u assertions, %u failures; read-only authored fixtures\n",n,f);return f?1:0;}
