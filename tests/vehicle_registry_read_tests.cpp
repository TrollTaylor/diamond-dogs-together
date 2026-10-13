#include "vehicle_registry_read.hpp"
#include <map>
#include <cstdio>
namespace vr=mgscoop::vehicle_registry_read;
namespace pd=mgscoop::coop_passenger::detail;
struct Memory {
 std::map<std::uintptr_t,unsigned char> bytes;
 template<class T> void Put(std::uintptr_t p,T value){const auto* raw=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(T);++i)bytes[p+i]=raw[i];}
 bool operator()(std::uintptr_t p,void* out,std::size_t n){auto* raw=static_cast<unsigned char*>(out);for(std::size_t i=0;i<n;++i){const auto x=bytes.find(p+i);if(x==bytes.end())return false;raw[i]=x->second;}return true;}
};
struct Rig {
 static constexpr std::uintptr_t image=0x140000000,provider=0x10000,holder=0x20000,descriptor=0x30000,primary=0x40000,query=0x50000,bitsHeader=0x60000;
 pd::EngineRead engine{};pd::Unit unit{};Memory m;
 Rig(){engine.registry=image+0x2c38008;engine.vtable=image+0x2305e70;engine.query=image+0xbf3a80;
  unit.vehicle=0x6408;unit.object=primary+0x20;unit.records=0x70000;unit.bitmap=0x80000;unit.allocated=23;unit.liveWord=0x100;
  m.Put(image+0x2c389a0,provider);m.Put(provider,image+0x23073c0);m.Put(image+0x23073c0+0x20,image+0xbf99e0);
  m.Put(provider+0x30+std::uintptr_t(unit.vehicle>>9)*8,holder);m.Put(holder+8,descriptor);m.Put(descriptor+0x20,primary);
  m.Put(primary,image+0x22ed930);m.Put(image+0x22ed930+0x40,image+0xb2dbe0);m.Put(image+0x22ed930+0x70,image+0xb33420);
  m.Put(primary+8,descriptor);m.Put(primary+0x1e0,unit.allocated);m.Put(primary+0x30,unit.records);m.Put(primary+0x1c8,bitsHeader);
  m.Put(descriptor+0x28,query);m.Put(query,image+0x23063f8);m.Put(image+0x23063f8+0x10,image+0xbf5450);
  m.Put(query+0x20,primary);m.Put(query+0x30,std::uint32_t(0));m.Put(bitsHeader+8,unit.bitmap);m.Put(unit.bitmap,unit.liveWord);
 }
 bool Observe(vr::Lookup& out){return vr::Observe(m,image,0xa01d000,engine,unit,out);}
 bool Callable(vr::Lookup& out){return vr::Callable(m,image,unit,out);}
};
unsigned checks=0,failures=0;void Check(bool x,const char* label){++checks;if(!x){++failures;std::printf("FAIL %s\n",label);}}
int main(){Rig r;vr::Lookup a{},b{};
 Check(r.Observe(a)&&r.Callable(a),"synthetic actual proxy forwards to exact typed primary allocation");
 Check(r.Observe(b)&&r.Callable(b)&&vr::Same(a,b),"matching current proxy owner and live allocation closure");
 r=Rig{};r.m.Put(Rig::descriptor+0x20,Rig::primary+0x10);Check(!r.Observe(b),"provider cannot resolve different primary object");
 r=Rig{};r.m.Put(Rig::primary+8,std::uintptr_t(1));Check(!r.Observe(b),"descriptor bidirectionally bound to primary");
 r=Rig{};r.m.Put(Rig::primary+0x1e0,std::uint16_t(24));Check(!r.Observe(b),"primary count matches qualified unit allocator");
 r=Rig{};r.engine.registry+=8;Check(!r.Observe(b),"different singleton refuses");
 r=Rig{};r.m.Put(Rig::provider,Rig::image+0x23073c8);Check(!r.Observe(b),"different provider final class refuses");
 r=Rig{};r.m.Put(Rig::query,Rig::image+0x22ed990);r.m.Put(Rig::image+0x22ed990+0x10,Rig::image+0xb33420);
 Check(r.Observe(b)&&!r.Callable(b),"old interior method table is observed but never called");
 r=Rig{};r.m.Put(Rig::query,Rig::image+0x22ed9f0);r.m.Put(Rig::image+0x22ed9f0+0x10,Rig::image+0xb2ac60);
 Check(r.Observe(b)&&!r.Callable(b),"secondary deletion ABI is observed but never called");
 r=Rig{};r.m.Put(Rig::query+0x20,Rig::primary+0x10);Check(r.Observe(b)&&!r.Callable(b),"proxy owner replacement refuses invocation");
 r=Rig{};r.m.Put(Rig::query+0x30,std::uint32_t(2));Check(r.Observe(b)&&!r.Callable(b),"native busy proxy mode2 refuses");
 r=Rig{};r.m.Put(Rig::query+0x30,std::uint32_t(1));Check(r.Observe(b)&&r.Callable(b),"mode1 follows original BF5450 admission");
 r=Rig{};r.m.Put(Rig::query+0xc,std::uint32_t(900));Check(r.Observe(b)&&r.Callable(b),"constructor cached extent not mistaken for native live allocator");
 r=Rig{};r.m.Put(Rig::image+0x22ed930+0x70,Rig::image+0xb33430);Check(r.Observe(b)&&!r.Callable(b),"inner exact dispatch slot required");
 r=Rig{};r.m.Put(Rig::primary+0x30,std::uintptr_t(0x71000));Check(r.Observe(b)&&!r.Callable(b),"different primary records refuse");
 r=Rig{};r.m.Put(Rig::bitsHeader+8,std::uintptr_t(0x81000));Check(r.Observe(b)&&!r.Callable(b),"different primary bitmap refuses");
 r=Rig{};r.m.Put(r.unit.bitmap,std::uint32_t(0));Check(r.Observe(b)&&!r.Callable(b),"despawned bitmap refuses");
 b=a;++b.queryOwner;Check(!vr::Same(a,b),"changing inner owner invalidates current closure");
 b=a;++b.queryMode;Check(!vr::Same(a,b),"changing native mode invalidates current closure");
 b=a;++b.innerQuerySlot;Check(!vr::Same(a,b),"changing dispatch invalidates current closure");
 b=a;++b.query;Check(!vr::Same(a,b),"replaced query receiver invalidates current closure");
 std::printf("Registry lookup v2: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
