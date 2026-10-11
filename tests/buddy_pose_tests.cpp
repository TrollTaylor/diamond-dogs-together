#include "mgscoop/buddy_pose.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <initializer_list>
namespace b=mgscoop::buddy_pose;
namespace {
unsigned checks{};void Check(bool v,const char* text){++checks;if(!v){std::fprintf(stderr,"FAIL %u %s\n",checks,text);std::exit(1);}}
struct Memory {
 std::map<std::uintptr_t,unsigned char> bytes;unsigned reads{},writes{};
 std::uintptr_t changeAt{};unsigned changeOn{},visits{};std::uintptr_t changedValue{};
 template<class T>void Put(std::uintptr_t p,const T& value){const auto* data=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(T);++i)bytes[p+i]=data[i];}
 static bool Read(void* context,std::uintptr_t p,void* out,std::size_t n)noexcept{
  auto& m=*static_cast<Memory*>(context);++m.reads;
  if(p==m.changeAt&&++m.visits==m.changeOn)m.Put(p,m.changedValue);
  auto* data=static_cast<unsigned char*>(out);for(std::size_t i=0;i<n;++i){auto it=m.bytes.find(p+i);if(it==m.bytes.end())return false;data[i]=it->second;}return true;
 }
};
constexpr std::uintptr_t base=0x140000000,P=0x400000,registry=0x500000,entry=0x600000,descriptor=0x700000,outer=0x800000,
 handle=0x900000,primary=0xa00000,secondary=primary+0x20,kind=0xb00000,positions=0xc00000,indices=0xd00000,selector=0xe00000,controller=0xf00000;
const b::Scope scope{P,30010,7,71,42,1000,true,true,false,false,true,true};
Memory Good(){Memory m;
 m.Put(base+0x2a82c30,P);m.Put(base+0x2c01372,std::uint16_t(30010));m.Put(P,base+0x2295490);m.Put(P+0x204,std::uint32_t(1));m.Put(P+0x208,std::uint32_t(2));m.Put(P+0x20c,std::uint32_t(2));m.Put(P+0x214,std::uint32_t(0));
 m.Put(base+0x2c28930,selector);m.Put(selector+0x10,controller);m.Put(controller+0x175,std::uint8_t(2));m.Put(controller+0x17f,std::uint8_t(0));
 m.Put(base+0x2c389a0,registry);m.Put(registry,base+0x23073c0);m.Put(registry+0x30+19u*8u,entry);m.Put(entry+8,descriptor);m.Put(descriptor+0x20,outer);
 m.Put(outer,base+0x22bdf30);m.Put(outer+0x1c,std::uint16_t(19));m.Put(outer+0x70,std::uint32_t(1));m.Put(outer+0x80,handle);m.Put(handle+0x10,primary);
 m.Put(primary,base+0x2315d80);m.Put(secondary,base+0x2315db0);m.Put(secondary+8,std::uint32_t(1));m.Put(secondary+0x10,kind);
 m.Put(kind+0x40,std::uint32_t(1));m.Put(kind+0x44,std::uint32_t(1));m.Put(kind+0x28,indices);m.Put(indices,std::uint16_t(0));m.Put(kind+0x20,positions);
 const float pose[4]{801.75f,348.25f,1639.5f,-0.75f};m.Put(positions,pose);return m;
}
const char* Get(Memory& m,b::Pose& out,const b::Scope& owner=scope,std::uint32_t thread=71,std::uint64_t now=1000,std::uint16_t id=0x2600){return b::ReadPose({&m,Memory::Read},base,owner,thread,now,id,out);}
void Refuse(Memory m,const char* why,const b::Scope& owner=scope,std::uint32_t thread=71,std::uint64_t now=1000,std::uint16_t id=0x2600){b::Pose out{};out.id=123;out.position={11.f,22.f,33.f,44.f};const auto r=Get(m,out,owner,thread,now,id);Check(r&&std::strcmp(r,why)==0,"exact refusal reason");Check(out.id==123&&out.position[1]==22.f,"failure leaves actual output untouched");Check(m.writes==0,"no engine memory mutation");}
}
int main(){auto m=Good();b::Pose out{};Check(!Get(m,out),"actual Dog lookup succeeds");Check(out.id==0x2600&&out.outer==outer&&out.primary==primary&&out.positions==positions,"exact typed identity returned");Check(out.position[0]==801.75f&&out.position[1]==348.25f&&out.position[2]==1639.5f,"actual nonzero terrain pose read");Check(m.writes==0,"successful reader performs no engine writes");
 for(unsigned i=0;i<12;++i){auto owner=scope;switch(i){case 0:owner.exact_executable=false;break;case 1:owner.exact_code_pins=false;break;case 2:owner.P=0;break;case 3:owner.generation=0;break;case 4:owner.epoch=0;break;case 5:owner.thread=0;break;case 6:owner.paired=false;break;case 7:owner.field_ready=false;break;case 8:owner.demo_playing=true;break;case 9:owner.terminating=true;break;case 10:owner.mission=10020;break;default:owner.refreshed_ms=1001;break;}Refuse(Good(),i<2?"admission":"scope",owner);}
 Refuse(Good(),"scope",scope,72);Refuse(Good(),"scope",scope,71,1251);m=Good();Check(!Get(m,out,scope,71,1250),"inclusive scope lease permits250ms");
 for(const auto id:{std::uint16_t(0xffff),std::uint16_t(0x2601),std::uint16_t(0x1a00),std::uint16_t(0)})Refuse(Good(),"dog_id",scope,71,1000,id);
 auto bad=Good();bad.Put(base+0x2a82c30,std::uintptr_t(P+16));Refuse(bad,"player_scope");bad=Good();bad.Put(base+0x2c01372,std::uint16_t(30020));Refuse(bad,"player_scope");bad=Good();bad.Put(P,base+0x2295498);Refuse(bad,"player_scope");
 for(auto v:{0u,0x101u}){bad=Good();bad.Put(P+0x204,v);Refuse(bad,"player_scope");}for(auto off:{0x208u,0x20cu}){bad=Good();bad.Put(P+off,std::uint32_t(1));Refuse(bad,"player_scope");}bad=Good();bad.Put(P+0x214,std::uint32_t(2));Refuse(bad,"player_scope");
 bad=Good();bad.Put(controller+0x175,std::uint8_t(1));Refuse(bad,"dog_selection");bad=Good();bad.Put(controller+0x17f,std::uint8_t(1));Refuse(bad,"dog_selection");bad=Good();bad.Put(selector+0x10,std::uintptr_t(0));Refuse(bad,"dog_selection");
 bad=Good();bad.Put(registry,base+0x23073c8);Refuse(bad,"dog_registry");bad=Good();bad.Put(entry+8,std::uintptr_t(0));Refuse(bad,"dog_registry");bad=Good();bad.Put(outer,base+0x22bdf38);Refuse(bad,"dog_registry");bad=Good();bad.Put(outer+0x1c,std::uint16_t(13));Refuse(bad,"dog_registry");bad=Good();bad.Put(outer+0x70,std::uint32_t(0));Refuse(bad,"dog_registry");bad=Good();bad.Put(handle+0x10,std::uintptr_t(0));Refuse(bad,"dog_primary_vt");
 bad=Good();bad.Put(primary,base+0x2315c38);Refuse(bad,"dog_primary_vt");bad=Good();bad.Put(secondary,base+0x2315c70);Refuse(bad,"dog_secondary");for(auto n:{0u,2u}){bad=Good();bad.Put(secondary+8,n);Refuse(bad,"dog_secondary");bad=Good();bad.Put(kind+0x40,n);Refuse(bad,"dog_secondary");bad=Good();bad.Put(kind+0x44,n);Refuse(bad,"dog_unrealized");}
 bad=Good();bad.Put(indices,std::uint16_t(1));Refuse(bad,"dog_unrealized");bad=Good();bad.Put(kind+0x20,std::uintptr_t(0));Refuse(bad,"dog_unrealized");bad=Good();bad.Put(kind+0x20,std::uintptr_t(0x7fffffffffff-4));Refuse(bad,"dog_unrealized");
 for(float value:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),1000001.f}){bad=Good();bad.Put(positions+4,value);Refuse(bad,"dog_position");}
 for(auto address:{base+0x2a82c30,base+0x2c389a0,registry+0x30+19u*8u,entry+8,descriptor+0x20,outer+0x80,handle+0x10,secondary+0x10,kind+0x20,kind+0x28,base+0x2c28930,selector+0x10,registry,outer,primary,secondary,P}){bad=Good();bad.changeAt=address;bad.changeOn=2;bad.changedValue=0;Refuse(bad,"dog_changed");}
 for(auto address:{P+0x204,P+0x208,P+0x20c,outer+0x70,secondary+8,kind+0x40,kind+0x44,controller+0x175}){bad=Good();bad.changeAt=address;bad.changeOn=2;bad.changedValue=0;Refuse(bad,"dog_changed");}
 m=Good();auto cleanup=scope;cleanup.paired=false;cleanup.cleanup_only=true;Check(!Get(m,out,cleanup),"explicit read-only cleanup may read after pairing is withdrawn");Check(!cleanup.paired,"cleanup never fabricates paired state");cleanup.demo_playing=true;Refuse(Good(),"scope",cleanup);cleanup.demo_playing=false;cleanup.field_ready=false;Refuse(Good(),"scope",cleanup);cleanup.field_ready=true;Refuse(Good(),"scope",cleanup,72);Refuse(Good(),"scope",cleanup,71,1251);
 m=Good();auto owner=scope;owner.mission=30020;m.Put(base+0x2c01372,std::uint16_t(30020));Check(!Get(m,out,owner),"actual selected Dog path valid in Africa");owner.mission=30050;m.Put(base+0x2c01372,std::uint16_t(30050));Check(!Get(m,out,owner),"actual selected Dog path valid in Mother Base");
 std::printf("buddy pose bounded reader %u checks PASS\n",checks);return 0;}
