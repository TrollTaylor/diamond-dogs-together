#include "../native/coop_passenger.hpp"
#include <map>
#include <cstdio>
using namespace mgscoop::coop_passenger::detail;
struct Memory {
 std::map<std::uintptr_t,unsigned char> bytes;
 template<class T> void Put(std::uintptr_t p,T value){const auto* raw=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(T);++i)bytes[p+i]=raw[i];}
 bool operator()(std::uintptr_t p,void* out,std::size_t n){auto* raw=static_cast<unsigned char*>(out);for(std::size_t i=0;i<n;++i){const auto x=bytes.find(p+i);if(x==bytes.end())return false;raw[i]=x->second;}return true;}
};
struct Rig {
 static constexpr std::uintptr_t image=0x140000000,root=0x10000,world=0x20000,registry=0x30000,player=0x40000,map=0x50000;
 static constexpr std::size_t span=0xa01d000;
 Memory m;
 Rig(){m.Put(image+0x2c3a8a0,root);m.Put(root+8,world);m.Put(world+0x60,registry);m.Put(registry,image+0x2300000);m.Put(image+0x2300038,image+0x1000000);m.Put(player+0x120,map);m.Put(map,std::uintptr_t(0x70000));m.Put(map+8,std::uintptr_t(0));}
 bool Read(EngineRead& out){return ReadEngine(m,image,span,player,out);}
};
unsigned count=0,failures=0;
void Check(bool x,const char* message){++count;if(!x){++failures;std::printf("FAIL %s\n",message);}}
int main(){
 EngineRead a{},b{};Rig r;Check(r.Read(a),"current registry and native driver binding can be observed");
 Check(a.bound[0]==0x70000&&a.bound[1]==0,"null remote native binding is preserved");
 Check(r.Read(b)&&Same(a,b),"two stable native reads close");
 r.m.Put(r.map+8,std::uintptr_t(0x80000));Check(r.Read(b)&&!Same(a,b),"a native remote attachment change invalidates closure");
 r=Rig{};r.m.Put(r.world+0x60,std::uintptr_t(0));Check(!r.Read(b),"missing registry refuses");
 r=Rig{};r.m.Put(r.registry,Rig::image+Rig::span-0x20);Check(!r.Read(b),"query slot crossing image refuses");
 r=Rig{};r.m.Put(Rig::image+0x2300038,Rig::image+Rig::span);Check(!r.Read(b),"out of image query refuses");
 r=Rig{};r.m.Put(r.player+0x120,std::uintptr_t(0));Check(!r.Read(b),"missing native per body map refuses");
 r=Rig{};r.m.bytes.erase(r.map+8);Check(!r.Read(b),"unreadable remote binding refuses");
 r=Rig{};r.m.Put(r.map,std::uintptr_t(0));Check(r.Read(b)&&!b.bound[0]&&!b.bound[1],"healthy unmounted null bindings remain valid observations");
 b=a;b.registry+=8;Check(!Same(a,b),"registry replacement invalidates closure");
 b=a;b.map+=8;Check(!Same(a,b),"map allocation replacement invalidates closure");
 Check(!ReadEngine(r.m,Rig::image,0x2c3a8a0,Rig::player,b),"truncated image refuses before root read");
 std::printf("Engine census: %u checks, %u failures\n",count,failures);return failures?1:0;
}
