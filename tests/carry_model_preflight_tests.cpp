#include "carry_model_preflight_read.hpp"
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
using namespace mgscoop;
namespace mp=carry_model_preflight_read;
struct Reader {
 std::map<std::uintptr_t,unsigned char> data;
 std::uintptr_t refuse=0,mutate=0;unsigned hits=0;
 bool operator()(std::uintptr_t p,void* dest,std::size_t n) noexcept {
  if(refuse&&p<=refuse&&refuse-p<n)return false;
  auto* out=static_cast<unsigned char*>(dest);
  for(std::size_t k=0;k<n;++k){auto i=data.find(p+k);if(i==data.end())return false;out[k]=i->second;}
  if(p==mutate&&++hits>1){if(n==sizeof(std::int32_t)){std::int32_t v=2;std::memcpy(dest,&v,sizeof(v));}}
  return true;
 }
 template<class T> void Put(std::uintptr_t p,T v){const auto* b=reinterpret_cast<const unsigned char*>(&v);for(std::size_t k=0;k<sizeof(v);++k)data[p+k]=b[k];}
};
constexpr std::uintptr_t image=0x140000000ULL,block=0x10000000,ps=0x11000000,primary=0x12000000,cs=0x13000000,entry=0x14000000,allocation=0x15000000;
constexpr std::uintptr_t slab=allocation+0x1a0;
static Reader Make(carry_plugin_read::Body& b,unsigned body=0){
 Reader r;b={};b.id=body;b.block=block;b.ps=ps;b.primary=primary;
 r.Put(block,image+0x2295210);r.Put<std::uint32_t>(block+8,1);r.Put<std::uint32_t>(block+0xc,body);r.Put(block+0x78,ps);r.Put(ps,image+0x23b09f0);r.Put(ps+0x408,block);r.Put<std::uint32_t>(ps+8,body);r.Put(primary,image+0x23b9ac0);r.Put(primary+8,block);r.Put(block+0x10,cs);r.Put(ps+0x238,cs);r.Put(cs,image+0x22e6070);r.Put<std::uint32_t>(cs+8,1);r.Put<std::uint32_t>(cs+0xc,body);r.Put(cs+0x10,entry);r.Put(entry+0x78,allocation);r.Put(entry+0x88,slab);r.Put<std::uint32_t>(entry+0xac,1);r.Put<std::uint32_t>(entry+0xb0,1);r.Put<std::uint8_t>(entry+0xb9,2);r.Put<std::uint8_t>(entry+0xba,2);
 for(unsigned k=0;k<2;++k){const auto p=slab+k*0x60;r.Put(p,image+0x22f6380);r.Put<std::int32_t>(p+8,1);r.Put<std::uintptr_t>(p+0x40,0);r.Put<std::uintptr_t>(p+0x48,0);r.Put<std::uint64_t>(p+0x50,0);r.Put<std::uint32_t>(p+0x58,0);}return r;
}
static void Bind(Reader& r,unsigned id=0){
 r.Put(slab+0x40,image+0x112fc50);r.Put(slab+0x48,primary);r.Put<std::uint64_t>(slab+0x50,0x6504d35367f0ULL);r.Put<std::uint32_t>(slab+0x58,id);
 r.Put(slab+0x60+0x40,image+0x112fcd0);r.Put(slab+0x60+0x48,primary);r.Put<std::uint64_t>(slab+0x60+0x50,0x20ca5ed42a91ULL);r.Put<std::uint32_t>(slab+0x60+0x58,id);
}
int main(){unsigned checks=0;auto check=[&](bool pass,const char* label){++checks;if(!pass){std::printf("FAIL %s\n",label);return false;}return true;};bool ok=true;carry_plugin_read::Body b{};mp::Model v{};const char* why=nullptr;
 auto r=Make(b);ok&=check(mp::ReadModelPair(r,image,b,v,&why)&&v.freeCallbacks==2&&!v.registrationPresent,"cold typed current slab");
 Bind(r);ok&=check(mp::ReadModelPair(r,image,b,v,&why)&&v.registrationPresent&&v.ownedCallbacks==2,"actual callback pair");
 r=Make(b,1);Bind(r,1);ok&=check(mp::ReadModelPair(r,image,b,v,&why)&&v.registrationPresent,"body1 actual ID");
 r=Make(b);Bind(r);r.Put<std::int32_t>(slab+8,0);ok&=check(!mp::ReadModelOnce(r,image,b,v,&why),"released baseline");
 r=Make(b);Bind(r);r.Put<std::int32_t>(slab+8,-1);ok&=check(!mp::ReadModelOnce(r,image,b,v,&why),"corrupt negative baseline");
 r=Make(b);Bind(r);r.Put(slab+0x60+0x40,image+0x112fc50);r.Put<std::uint64_t>(slab+0x60+0x50,0x6504d35367f0ULL);ok&=check(!mp::ReadModelOnce(r,image,b,v,&why),"duplicate own event");
 r=Make(b);Bind(r);r.Put(slab+0x40,image+0x112fc51);ok&=check(!mp::ReadModelOnce(r,image,b,v,&why),"unknown raw-owner callback");
 r=Make(b);Bind(r);r.Put<std::uint32_t>(slab+0x58,1);ok&=check(!mp::ReadModelOnce(r,image,b,v,&why),"foreign body callback");
 r=Make(b);r.Put(slab+0x48,primary);ok&=check(!mp::ReadModelOnce(r,image,b,v,&why),"half cleared callback");
 r=Make(b);r.Put(ps+0x238,cs+0x1000);ok&=check(!mp::ReadModelOnce(r,image,b,v,&why),"PS CS owner mismatch");
 r=Make(b);r.Put<std::uint32_t>(cs+8,2);ok&=check(!mp::ReadModelOnce(r,image,b,v,&why),"model count outside one-body block");
 r=Make(b);r.Put<std::uint32_t>(cs+0xc,1);ok&=check(!mp::ReadModelOnce(r,image,b,v,&why),"model base mismatch");
 r=Make(b);r.Put(cs,image+0x22e5b20);ok&=check(!mp::ReadModelOnce(r,image,b,v,&why),"destructing CS base type");
 r=Make(b);r.Put(slab,image+0x22f6300);ok&=check(!mp::ReadModelOnce(r,image,b,v,&why),"wrong callback type");
 r=Make(b);r.Put(entry+0x88,slab+0x10);ok&=check(!mp::ReadModelOnce(r,image,b,v,&why),"callback slab constructor layout mismatch");
 r=Make(b);r.Put(entry+0x78,std::numeric_limits<std::uintptr_t>::max()-16);ok&=check(!mp::ReadModelOnce(r,image,b,v,&why),"backing overflow");
 r=Make(b);r.Put<std::uint8_t>(entry+0xba,1);ok&=check(!mp::ReadModelOnce(r,image,b,v,&why),"no spare slots for pair");
 r=Make(b);r.refuse=slab+0x60+0x58;ok&=check(!mp::ReadModelOnce(r,image,b,v,&why),"unreadable final callback field");
 r=Make(b);r.mutate=slab+8;ok&=check(!mp::ReadModelPair(r,image,b,v,&why)&&std::strcmp(why,"model_changed")==0,"foreign ref mutation between reads");
 r=Make(b);r.Put<std::uint64_t>(slab+0x40,0x20000000);r.Put<std::uint64_t>(slab+0x48,0x30000000);r.Put<std::uint64_t>(slab+0x50,0x40000000);ok&=check(mp::ReadModelOnce(r,image,b,v,&why)&&!v.registrationPresent,"unrelated model callback does not prove carry");
 std::printf("carry_model_preflight_tests %s %u checks (pure fixtures only)\n",ok?"PASS":"FAIL",checks);return ok?0:1;
}
