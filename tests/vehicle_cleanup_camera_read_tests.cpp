#include "vehicle_cleanup_camera_read.hpp"
#include <map>
#include <cstdio>
namespace camera=mgscoop::vehicle_cleanup_camera_read;
struct Memory {
 std::map<std::uintptr_t,unsigned char> bytes;
 template<class T> void Put(std::uintptr_t p,T value){const auto* raw=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(T);++i)bytes[p+i]=raw[i];}
 bool operator()(std::uintptr_t p,void* out,std::size_t n){auto* raw=static_cast<unsigned char*>(out);for(std::size_t i=0;i<n;++i){auto x=bytes.find(p+i);if(x==bytes.end())return false;raw[i]=x->second;}return true;}
};
struct Rig {
 static constexpr std::uintptr_t image=0x140000000,player=0x100000,data=0x200000,component=0x300000,holder=0x400000,resource=0x500000;
 Memory m;
 Rig(){m.Put(player+0xb8,component+0x20);m.Put(component+0x20,image+0x23b8250);m.Put(component,image+0x23b8218);
  m.Put(component+0x20+0x348,player);m.Put(image+0x23b8250+0x58,image+0x1115540);
  m.Put(component+0x20+0xba8,holder);m.Put(holder+0x10,resource);m.Put(resource,image+0x23d1ec8);m.Put(image+0x23d1ec8+0x18,image+0x129e990);
  m.Put(resource+0x40,image+0x23d1f80);m.Put(image+0x23d1f80+0x68,image+0x129f120);m.Put(image+0x23d1f80+0x70,image+0x129f590);
  m.Put(resource+0x28,component+0x20);m.Put(resource+0xf8,data);m.Put(player+0x214,std::uint32_t(0));m.Put(resource+0x104,std::uint32_t(0));m.Put(resource+0x40+0xa4,std::uint32_t(4));
 }
 bool Read(camera::Owner& out){return camera::Read(m,image,player,data,out);}
};
unsigned checks=0,failures=0;void Check(bool x,const char* name){++checks;if(!x){++failures;std::printf("FAIL %s\n",name);}}
int main(){Rig r;camera::Owner a{},b{};
 Check(r.Read(a)&&a.flags==4,"synthetic actual camera resource owner chain");
 Check(r.Read(b)&&camera::Same(a,b),"matching two-read native camera identity");
 r=Rig{};r.m.Put(Rig::resource+0x28,Rig::component+0x30);Check(!r.Read(b),"resource belongs to exact component");
 r=Rig{};r.m.Put(Rig::resource+0xf8,Rig::data+8);Check(!r.Read(b),"resource actual Q identity required");
 r=Rig{};r.m.Put(Rig::resource+0x104,std::uint32_t(1));Check(!r.Read(b),"resource has real local0 creator binding");
 r=Rig{};r.m.Put(Rig::resource,Rig::image+0x23d1eb0);Check(!r.Read(b),"creator descriptor table not confused with final resource");
 r=Rig{};r.m.Put(Rig::image+0x23d1f80+0x70,Rig::image+0x129f580);Check(!r.Read(b),"exact native reset slot required");
 Check(camera::CleanupReset(Rig::image,a,a.interface,Rig::image+0x124cc44,1),"exact remote cleanup reset membership");
 Check(!camera::CleanupReset(Rig::image,a,a.interface,Rig::image+0x124cc44,0),"local0 native reset retained");
 Check(!camera::CleanupReset(Rig::image,a,a.interface,Rig::image+0x124cc45,1),"foreign caller preserved");
 Check(!camera::CleanupReset(Rig::image,a,a.interface+8,Rig::image+0x124cc44,1),"foreign camera interface preserved");
 b=a;++b.flags;Check(!camera::Same(a,b),"current camera writes invalidate readonly closure");
 std::printf("Vehicle cleanup camera: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
