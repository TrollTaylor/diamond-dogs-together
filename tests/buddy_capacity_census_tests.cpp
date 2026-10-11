#include "mgscoop/buddy_capacity_census.hpp"
#include "mgscoop/buddy_capacity_catalog.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <initializer_list>
namespace b=mgscoop::buddy_capacity_census;
namespace {
unsigned checks{};void Check(bool v,const char* why){++checks;if(!v){std::fprintf(stderr,"FAIL %u %s\n",checks,why);std::exit(1);}}
struct Memory{
 std::map<std::uintptr_t,unsigned char>bytes;unsigned reads{},writes{};std::uintptr_t changeAt{},changedValue{};unsigned changeOn{},visits{};
 template<class T>void Put(std::uintptr_t p,const T& value){const auto* data=reinterpret_cast<const unsigned char*>(&value);for(std::size_t n=0;n<sizeof(T);++n)bytes[p+n]=data[n];}
 static bool Read(void* context,std::uintptr_t p,void* out,std::size_t n)noexcept{auto& m=*static_cast<Memory*>(context);++m.reads;if(p==m.changeAt&&++m.visits==m.changeOn)m.Put(p,m.changedValue);auto* data=static_cast<unsigned char*>(out);for(std::size_t j=0;j<n;++j){auto it=m.bytes.find(p+j);if(it==m.bytes.end())return false;data[j]=it->second;}return true;}
};
constexpr std::uintptr_t base=0x140000000,P=0x400000,registry=0x500000,entry=0x600000,descriptor=0x700000,outer=0x800000,handle=0x900000,
 primary=0xa00000,secondary=primary+0x20,component=0xb00020,records=0xc00000,kind=0xd00000,positions=0xe00000,indices=0xf00000,selector=0x1000000,controller=0x1100000;
const b::Authorization auth{true,true,true,true,true,true};const b::Owner owner{P,30010,9,71,2,42,1000,true,true,false,false};
Memory Good(unsigned count=2){Memory m;
 m.Put(base+0x2a82c30,P);m.Put(base+0x2c01372,std::uint16_t(30010));m.Put(P,base+0x2295490);m.Put(P+0x204,std::uint32_t(1));m.Put(P+0x208,std::uint32_t(2));m.Put(P+0x20c,std::uint32_t(2));m.Put(P+0x214,std::uint32_t(0));
 m.Put(base+0x2c28930,selector);m.Put(selector+0x10,controller);m.Put(controller+0x175,std::uint8_t(1));m.Put(controller+0x17f,std::uint8_t(0));
 m.Put(controller+0x38,mgscoop::buddy_capacity::kHorseMainPacks[0].hash);
 m.Put(base+0x2c389a0,registry);m.Put(registry,base+0x23073c0);m.Put(registry+0x30+13u*8u,entry);m.Put(entry+8,descriptor);m.Put(descriptor+0x20,outer);
 m.Put(outer,base+0x229b880);m.Put(outer+0x1c,std::uint16_t(13));m.Put(outer+0x40,std::uint32_t(count));m.Put(outer+0x44,std::uint32_t(count));m.Put(outer+0x50,handle);m.Put(handle+0x10,primary);
 m.Put(primary,base+0x2362620);m.Put(secondary,base+0x2362650);m.Put(secondary+8,std::uint16_t(13));m.Put(secondary+0xc,std::uint32_t(count));m.Put(secondary+0x68,component);
 m.Put(component-0x20,base+0x2367b80);m.Put(component,base+0x2336cf0);m.Put(component+0x10,std::uint32_t(count));m.Put(component+8,records);
 m.Put(secondary+0x18,kind);m.Put(kind+0x40,std::uint32_t(count));m.Put(kind+0x44,std::uint32_t(count));m.Put(kind+0x28,indices);m.Put(kind+0x20,positions);
 for(unsigned i=0;i<count&&i<2;++i){m.Put(indices+i*2u,static_cast<std::uint16_t>(i));auto rec=records+i*0x2f0u;m.Put(rec,std::uintptr_t(0));m.Put(rec+0xe,std::uint16_t(0xffff));m.Put(rec+0x10,std::uint16_t(0xffff));m.Put(rec+0x2dd,std::uint8_t(0));const float xyz[4]{1.f+float(i)*4.f,348.25f,-7.75f,-0.5f};m.Put(positions+i*16u,xyz);}return m;
}
const char* Get(Memory& m,b::Census& out,b::Authorization a=auth,b::Owner o=owner,unsigned thread=71,std::uint64_t now=1000){return b::ReadCurrentCensus({&m,Memory::Read},base,a,o,thread,now,out);}
void Refuse(Memory m,const char* why,b::Authorization a=auth,b::Owner o=owner,unsigned thread=71,std::uint64_t now=1000){b::Census out{};out.count=777;auto reason=Get(m,out,a,o,thread,now);Check(reason&&std::strcmp(reason,why)==0,"exact refusal reason");Check(out.count==777,"failure preserves actual output");Check(m.writes==0,"no engine memory writes");}
}
int main(){auto m=Good();b::Census out{};
 if(!b::Compiled()){Check(Get(m,out)&&m.reads==0,"capacity OFF refuses without memory reads");Check(m.writes==0,"capacity OFF has no writes");std::printf("buddy capacity pure OFF %u checks PASS\n",checks);return 0;}
 Check(!Get(m,out),"qualified typed current two-horse census");Check(out.count==2&&out.realized_count==2&&out.realized_mask==3,"allocation and realization proved separately");
 Check(out.pack_hash==mgscoop::buddy_capacity::kHorseMainPacks[0].hash&&out.pack_token&&std::strcmp(out.pack_token,"00")==0,"retained main pack is variant 00");
 Check(out.actors[0].game_id==6656&&out.actors[1].game_id==6657&&out.actors[1].record==records+0x2f0,"separate index1 has distinct actual controller record");Check(out.actors[1].position[1]==348.25f&&out.actors[1].position[0]==5.f,"raw typed second pose height preserved");Check(m.writes==0,"success performs no engine writes");
 m=Good(1);Check(!Get(m,out)&&out.count==1&&out.realized_mask==1&&out.actors[1].game_id==0xffff,"baseline singleton remains one; never reads or fabricates second index");
 for(unsigned i=0;i<6;++i){auto a=auth;switch(i){case 0:a.private_worker=false;break;case 1:a.offline=false;break;case 2:a.copied_saves=false;break;case 3:a.candidate_catalog=false;break;case 4:a.exact_executable=false;break;default:a.exact_code_pins=false;break;}Refuse(Good(),"admission",a);}
 for(unsigned i=0;i<11;++i){auto o=owner;switch(i){case 0:o.P=0;break;case 1:o.generation=0;break;case 2:o.thread=0;break;case 3:o.buddy_cycle=0;break;case 4:o.epoch=0;break;case 5:o.field_ready=false;break;case 6:o.horse_active=false;break;case 7:o.demo_playing=true;break;case 8:o.terminating=true;break;case 9:o.mission=10020;break;default:o.refreshed_ms=1001;break;}Refuse(Good(),"scope",auth,o);}
 Refuse(Good(),"scope",auth,owner,72);Refuse(Good(),"scope",auth,owner,71,1251);m=Good();Check(!Get(m,out,auth,owner,71,1250),"inclusive250ms owner lease");
 auto bad=Good();bad.Put(base+0x2a82c30,std::uintptr_t(P+16));Refuse(bad,"player_scope");bad=Good();bad.Put(P+0x204,std::uint32_t(0x101));Refuse(bad,"player_scope");bad=Good();bad.Put(P+0x208,std::uint32_t(1));Refuse(bad,"player_scope");
 bad=Good();bad.Put(controller+0x175,std::uint8_t(2));Refuse(bad,"horse_selection");bad=Good();bad.Put(controller+0x17f,std::uint8_t(1));Refuse(bad,"horse_selection");
 bad=Good();bad.Put(registry,base+0x23073c8);Refuse(bad,"horse_registry");bad=Good();bad.Put(outer,base+0x22bdf30);Refuse(bad,"horse_registry");bad=Good();bad.Put(outer+0x1c,std::uint16_t(19));Refuse(bad,"horse_registry");for(unsigned n:{0u,3u}){bad=Good();bad.Put(outer+0x40,n);Refuse(bad,"horse_registry");}
 bad=Good();bad.Put(component,base+0x2326cf0);Refuse(bad,"component_vt");
 bad=Good();bad.Put(secondary,base+0x24426a0);Refuse(bad,"receiver_secondary_vt");bad=Good();bad.Put(primary,base+0x23624a8);Refuse(bad,"receiver_primary_vt");bad=Good();bad.Put(secondary+8,std::uint16_t(19));Refuse(bad,"receiver_count");bad=Good();bad.Put(secondary+0xc,std::uint32_t(1));Refuse(bad,"receiver_count");
 bad=Good();bad.Put(component,base+0x246c8c8);Refuse(bad,"component_vt");bad=Good();bad.Put(component+0x10,std::uint32_t(1));Refuse(bad,"component_count");bad=Good();bad.Put(component+8,std::uintptr_t(0));Refuse(bad,"component_count");
 bad=Good();bad.Put(kind+0x40,std::uint32_t(1));Refuse(bad,"realization");bad=Good();bad.Put(kind+0x44,std::uint32_t(3));Refuse(bad,"realization");bad=Good();bad.Put(kind+0x28,std::uintptr_t(0));Refuse(bad,"realized_array");bad=Good();bad.Put(indices+2,std::uint16_t(0));Refuse(bad,"realized_index");bad=Good();bad.Put(indices+2,std::uint16_t(2));Refuse(bad,"realized_index");
 bad=Good();bad.Put(kind+0x44,std::uint32_t(1));Check(!Get(bad,out)&&out.count==2&&out.realized_mask==1&&!out.actors[1].realized,"allocated second controller does not claim rendered second actor");
 bad=Good();bad.Put(positions+16,std::numeric_limits<float>::quiet_NaN());Refuse(bad,"position");bad=Good();bad.Put(records+0x2f0+0x2dd,std::uint8_t(1));Refuse(bad,"rider_record");bad=Good();bad.bytes.erase(records+0x2f0+0xe);Refuse(bad,"actor_record");
 bad=Good();bad.Put(records+0x2f0,std::uintptr_t(0x1200000));bad.Put(records+0x2f0+0xe,std::uint16_t(1));bad.Put(records+0x2f0+0x2dd,std::uint8_t(1));Check(!Get(bad,out)&&out.actors[1].rider==1&&out.actors[1].occupied,"rider identity is actual word and not boolean occupied");
 for(auto address:{base+0x2a82c30,base+0x2c28930,selector+0x10,base+0x2c389a0,registry,registry+0x30+13u*8u,entry+8,descriptor+0x20,outer,outer+0x50,handle+0x10,primary,secondary,secondary+0x68,component,component+8,secondary+0x18,kind+0x20,kind+0x28}){bad=Good();bad.changeAt=address;bad.changeOn=2;Refuse(bad,"identity_changed");}
 for(auto address:{P+0x204,P+0x208,P+0x20c,controller+0x175,outer+0x40,secondary+0xc,component+0x10,kind+0x40,kind+0x44}){bad=Good();bad.changeAt=address;bad.changeOn=2;Refuse(bad,"identity_changed");}
 for(auto address:{records+0x2f0,records+0x2f0+0xe,records+0x2f0+0x10,records+0x2f0+0x2dd}){bad=Good();bad.changeAt=address;bad.changeOn=2;bad.changedValue=2;Refuse(bad,"rider_changed");}
 m=Good();auto o=owner;o.mission=30020;m.Put(base+0x2c01372,std::uint16_t(30020));Check(!Get(m,out,auth,o),"current typed Horse census available in Africa");o.mission=30050;m.Put(base+0x2c01372,std::uint16_t(30050));Check(!Get(m,out,auth,o),"current typed Horse census available in Mother Base");
 bad=Good();bad.bytes.erase(controller+0x38);Refuse(bad,"pack_unknown");bad=Good();bad.Put(controller+0x38,std::uint64_t(0));Refuse(bad,"pack_unknown");
 bad=Good();bad.Put(controller+0x38,mgscoop::buddy_capacity::kWalkerMainPack.hash);Refuse(bad,"pack_unknown");
 bad=Good();bad.changeAt=controller+0x38;bad.changeOn=2;bad.changedValue=1;Refuse(bad,"pack_changed");
 for(const auto& row:mgscoop::buddy_capacity::kHorseMainPacks){bad=Good();bad.Put(controller+0x38,row.hash);Check(!Get(bad,out)&&out.pack_hash==row.hash&&out.pack_token&&std::strcmp(out.pack_token,row.token)==0,"each verified horse main-pack hash is named and does not invent a second body");}
 bad=Good(1);bad.Put(controller+0x38,mgscoop::buddy_capacity::kHorseMainPacks[1].hash);Check(!Get(bad,out)&&out.count==1&&out.pack_token&&std::strcmp(out.pack_token,"03")==0,"singleton still reports the retained pack");
 std::printf("buddy capacity pure ON %u checks PASS\n",checks);return 0;}
