#include "mgscoop/buddy_ride_read.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <initializer_list>
namespace b=mgscoop::buddy_ride_read;
namespace {
unsigned checks{};void Check(bool v,const char* why){++checks;if(!v){std::fprintf(stderr,"FAIL %u %s\n",checks,why);std::exit(1);}}
struct Memory{
 std::map<std::uintptr_t,unsigned char>bytes;unsigned reads{},writes{},visits{};
 std::uintptr_t change_at{},change_value{};unsigned change_on{};
 template<class T>void Put(std::uintptr_t p,const T& value){const auto* data=reinterpret_cast<const unsigned char*>(&value);for(std::size_t n=0;n<sizeof(T);++n)bytes[p+n]=data[n];}
 static bool Read(void* c,std::uintptr_t p,void* out,std::size_t n)noexcept{
  auto& m=*static_cast<Memory*>(c);++m.reads;if(p==m.change_at&&++m.visits==m.change_on)m.Put(p,m.change_value);
  auto* data=static_cast<unsigned char*>(out);for(std::size_t j=0;j<n;++j){auto it=m.bytes.find(p+j);if(it==m.bytes.end())return false;data[j]=it->second;}return true;
 }
};
constexpr std::uintptr_t base=0x140000000,P=0x400000,selector=0x500000,controller=0x600000;
const b::Authorization auth{true,true,true,true,true,true};
const b::Owner owner{P,30010,9,71,2,42,1000,1,true,false,false};
Memory Good(unsigned bank=0,unsigned kind=1,std::uint16_t id=6657){Memory m;
 m.Put(base+0x2a82c30,P);m.Put(base+0x2c01372,std::uint16_t(30010));m.Put(P,base+0x2295490);
 m.Put(P+0x204,std::uint32_t(1));m.Put(P+0x208,std::uint32_t(2));m.Put(P+0x20c,std::uint32_t(2));m.Put(P+0x214,std::uint32_t(0));
 m.Put(base+0x2c28930,selector);m.Put(selector+0x10,controller);m.Put(controller+0x175,std::uint8_t(kind));m.Put(controller+0x17f,std::uint8_t(0));
 m.Put(base+0x2bd3fd4,std::uint32_t(0));m.Put(base+0x2bd3fd0,std::uint32_t(bank));
 for(unsigned i=0;i<2;++i){auto rec=base+0x2bd3ff0+i*0xe00;m.Put(rec,base+0x2190b28);m.Put(rec+0xb6,id);}return m;
}
const char* Get(Memory& m,b::Snapshot& out,b::Authorization a=auth,b::Owner o=owner,unsigned thread=71,std::uint64_t now=1000){return b::ReadCurrent({&m,Memory::Read},base,a,o,thread,now,out);}
void Refuse(Memory m,const char* expected,b::Authorization a=auth,b::Owner o=owner,unsigned thread=71,std::uint64_t now=1000){b::Snapshot out{};out.info=777;auto reason=Get(m,out,a,o,thread,now);Check(reason&&std::strcmp(reason,expected)==0,"exact refusal");Check(out.info==777&&!out.current,"failure keeps output");Check(!m.writes,"no engine writes");}
}
int main(){auto m=Good();b::Snapshot out{};
 if(!b::Compiled()){Check(Get(m,out)&&m.reads==0,"OFF before any read");Check(!m.writes&&!out.current,"OFF no output or writes");std::printf("buddy ride read OFF %u checks PASS\n",checks);return 0;}
 Check(!Get(m,out)&&out.ride_id==6657&&out.info==base+0x2bd3ff0&&out.current,"actual local bank0 ride id");
 Check(!out.mounted&&!out.permit_action&&!m.writes,"readback never infers mount/authorizes action");
 m=Good(1);Check(!Get(m,out)&&out.bank==1&&out.info==base+0x2bd3ff0+0xe00&&out.ride_id==6657,"bank1 bounded correctly");
 m=Good(0,1,0xffff);Check(!Get(m,out)&&out.ride_id==0xffff,"natural none storage retainsFFFF");
 m=Good(0,4,22529);auto o=owner;o.active_kind=4;Check(!Get(m,out,auth,o)&&out.ride_id==22529,"actual WALKER4 storage");
 m=Good(0,1,1234);Check(!Get(m,out)&&out.ride_id==1234&&!out.mounted,"unrelated stored ID diagnostic only");
 for(unsigned i=0;i<6;++i){auto a=auth;switch(i){case 0:a.private_worker=false;break;case 1:a.offline=false;break;case 2:a.copied_saves=false;break;case 3:a.candidate_catalog=false;break;case 4:a.exact_executable=false;break;default:a.exact_code_pins=false;break;}Refuse(Good(),"admission",a);}
 for(unsigned i=0;i<12;++i){o=owner;switch(i){case 0:o.P=0;break;case 1:o.generation=0;break;case 2:o.thread=0;break;case 3:o.buddy_cycle=0;break;case 4:o.epoch=0;break;case 5:o.field_ready=false;break;case 6:o.active_kind=3;break;case 7:o.demo_playing=true;break;case 8:o.terminating=true;break;case 9:o.mission=10020;break;case 10:o.refreshed_ms=1001;break;default:o.active_kind=0;break;}Refuse(Good(),"scope",auth,o);}
 Refuse(Good(),"scope",auth,owner,72);Refuse(Good(),"scope",auth,owner,71,1251);m=Good();Check(!Get(m,out,auth,owner,71,1250),"inclusive lease250");
 for(auto address:{base+0x2bd3fd4,base+0x2bd3fd0,P+0x214}){m=Good();m.Put(address,std::uint32_t(2));Refuse(m,"owner");}
 m=Good();m.Put(controller+0x175,std::uint8_t(3));Refuse(m,"owner");m=Good();m.Put(controller+0x17f,std::uint8_t(1));Refuse(m,"owner");
 m=Good();m.Put(P+0x204,std::uint32_t(0x101));Refuse(m,"owner");m=Good();m.Put(base+0x2bd3ff0,base+0x2190b30);Refuse(m,"record");
 m=Good();m.bytes.erase(base+0x2bd3ff0+0xb6);Refuse(m,"record");
 for(auto address:{base+0x2a82c30,base+0x2c28930,selector+0x10,P,P+0x208,P+0x20c,controller+0x175,base+0x2bd3fd0,base+0x2bd3fd4,base+0x2bd3ff0}){
  m=Good();m.change_at=address;m.change_on=2;m.change_value=(address==P+0x208||address==P+0x20c)?1:2;Refuse(m,"identity_changed");
 }
 m=Good();m.change_at=base+0x2bd3ff0+0xb6;m.change_on=2;m.change_value=6656;Refuse(m,"ride_changed");
 for(unsigned mission:{30020u,30050u}){m=Good();o=owner;o.mission=mission;m.Put(base+0x2c01372,std::uint16_t(mission));Check(!Get(m,out,auth,o),"supported actual mission");}
 std::printf("buddy ride read ON %u checks PASS\n",checks);return 0;
}
