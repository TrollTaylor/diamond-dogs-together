#include "horse_finder_entry_read.hpp"
#include <map>
#include <cstdio>
#include <cstring>
namespace fe=mgscoop::horse_finder_entry_read;constexpr std::uintptr_t image=0x140000000ull,self=0x100000,owner=0x200000,node=0x300000,record=0x400000;
int checks=0,failed=0;void Check(bool value,const char* why){++checks;if(!value){++failed;std::printf("FAIL %s\n",why);}}
struct Memory {
 std::map<std::uintptr_t,unsigned char> bytes;
 bool operator()(std::uintptr_t at,void* out,std::size_t n)const noexcept{auto p=static_cast<unsigned char*>(out);for(std::size_t i=0;i<n;++i){auto it=bytes.find(at+i);if(it==bytes.end())return false;p[i]=it->second;}return true;}
 void Zero(std::uintptr_t at,std::size_t n){for(std::size_t i=0;i<n;++i)bytes[at+i]=0;}
 template<class T>void Put(std::uintptr_t at,const T& value){const auto p=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(value);++i)bytes[at+i]=p[i];}
};
int main(){Memory m{};m.Zero(self,0x78);m.Zero(owner,0x550);m.Zero(node,0x40);m.Zero(record,0x6c0);
 m.Put(self,image+0x23b9ac0);m.Put(self+0x30,image+0x23b9b50);m.Put(self+8,owner);m.Put(self+0x38,node);m.Put(self+0x68,record);
 m.Put(owner,image+0x2295210);m.Put(owner+8,1u);m.Put(owner+0xc,0u);m.Put(owner+0xb8,self);m.Put(owner+0x320,node);
 m.Put(node,image+0x2306628);m.Put(node+0x20,1u);m.Put(node+0x24,0u);m.Put(node+0x38,self+0x30);
 m.Put(record+0x78,std::uint16_t(1024));m.Put(record+0x6a0,std::uint16_t(6656));m.Put(record+0x6a4,std::uint16_t(0x30));m.Put(record+0x6a6,std::uint16_t(0x8004));m.Put(record+0x6a8,std::uint16_t(1));m.Put(record+0x6ac,6u);
 fe::Entry e{};Check(fe::Read(m,image,self,0,e)&&e.victim==1024&&e.f4==0x30&&e.f6==0x8004&&e.f8==1,"exact callback gate reads all flags");
 for(auto address:{self,self+0x30,self+8,self+0x38,self+0x68,owner,owner+8,owner+0xc,owner+0xb8,owner+0x320,node,node+0x20,node+0x24,node+0x38}){auto bad=m;bad.bytes[address]^=8;Check(!fe::Read(bad,image,self,0,e),"typed callback closure mutation refuses");}
 auto missing=m;missing.bytes.erase(record+0x6a8);Check(!fe::Read(missing,image,self,0,e),"unreadable full gate refuses");Check(!fe::Read(m,image,self,2,e),"foreign actor refuses");Check(!fe::Read(m,image,self+1,0,e),"unaligned self refuses");Check(fe::Read(m,image,self,0,e),"baseline restored");
 fe::Snapshot s{};s.before=e;s.after=e;s.after.f6=4;s.after.nearHorse=0xffff;s.after.prompt=7;s.at=100;s.read=true;s.input=0;s.callerRva=0x1132fd6;char line[160]{};
 Check(fe::Line(s,101,line,sizeof(line))&&std::strstr(line,"raw=1;pair=0")&&std::strstr(line,"e=30,8004,1;x=30,4,1"),"transient8000 preserved as raw only");
 Check(fe::Line(s,1600,line,sizeof(line))&&std::strstr(line,"raw=1"),"freshness inclusive1500");Check(fe::Line(s,1601,line,sizeof(line))&&std::strstr(line,"raw=0;pair=0"),"stale raw cannot claim current");
 Check(fe::Line(s,99,line,sizeof(line))&&std::strstr(line,"raw=0"),"clock reversal refuses");Check(!fe::Line(s,101,line,12),"short output refuses");Check(!fe::Line(s,101,nullptr,160),"missing output refuses");
 s.after.record++;Check(!fe::SameSource(s.before,s.after),"replaced record refuses before-after closure");s.after=s.before;s.after.victim++;Check(!fe::SameSource(s.before,s.after),"replaced recipient refuses closure");s.after=s.before;s.after.nearHorse++;Check(fe::SameSource(s.before,s.after),"legitimate finder candidate delta retained");
 std::printf("horse_finder_entry_check: %d passed, %d failed; no engine/native calls\n",checks-failed,failed);return failed?1:0;
}
