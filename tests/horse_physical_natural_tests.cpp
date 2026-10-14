#include "horse_player_physical_read.hpp"
#include <map>
#include <cstring>
#include <cstdio>
using namespace mgscoop::carry_plugin_read;
unsigned good=0,bad=0;void Check(bool b,const char* l){if(b)++good;else{++bad;std::printf("FAIL %s\n",l);}}
struct Memory {
 std::map<std::uintptr_t,unsigned char> bytes;
 bool flip=false;unsigned scans=0;
 template<class T>void Put(std::uintptr_t p,T x){unsigned char v[sizeof(T)]{};std::memcpy(v,&x,sizeof(x));for(unsigned i=0;i<sizeof(T);++i)bytes[p+i]=v[i];}
 void Zero(std::uintptr_t p,unsigned n){for(unsigned i=0;i<n;++i)bytes[p+i]=0;}
 bool operator()(std::uintptr_t p,void* out,std::size_t n) noexcept {
  if(p==0x10000&&++scans==2&&flip)Put<std::uint16_t>(0x70000+0x78,1);
  auto* v=static_cast<unsigned char*>(out);for(std::size_t i=0;i<n;++i){auto j=bytes.find(p+i);if(j==bytes.end())return false;v[i]=j->second;}return true;
 }
};
Scope S(){Scope s{};s.exactGate=s.admitted=s.ready=s.armed=true;s.image=0x140000000;s.player=0x10000;s.data=0x20000;s.state=0x30000;s.thread=s.ownerThread=9;s.mission=s.ownedMission=30050;s.epoch=s.ownedEpoch=2;s.generation=s.ownedGeneration=3;s.now=1000;s.poll=900;s.sequenceBefore=s.sequenceAfter=8;return s;}
Memory Fixture(){Memory m;auto s=S();
 m.Put(s.player,s.image+0x2295490);m.Put(s.player+0x60,s.data);m.Put(s.data,s.image+0x23ad970);m.Put(s.data+0x1e8,s.state);m.Put<std::uint32_t>(s.state+0x40,2);m.Put<std::uintptr_t>(s.player+0xa8,0);m.Put<std::uint32_t>(s.player+0x204,0x401);m.Put<std::uint32_t>(s.player+0x208,2);m.Put<std::uint32_t>(s.player+0x20c,2);m.Put<std::uint32_t>(s.player+0x214,0);
 for(auto pair:{std::pair<unsigned,std::uintptr_t>{0x70,0x31000},{0x78,0x32000},{0x90,0x33000},{0x378,0x40000},{0x50,0x34000},{0xd0,0xb0000}})m.Put(s.player+pair.first,pair.second);
 for(unsigned k=0;k<2;++k){auto block=std::uintptr_t(0x40000+k*0x6a8),ps=std::uintptr_t(0x50000+k*0x1000),node=std::uintptr_t(0x60000+k*0x1000),prim=std::uintptr_t(0x80000+k*0x1000),rec=std::uintptr_t(0x70000+k*0x1000),psrec=std::uintptr_t(0xa0000+k*0x1000),bitmap=std::uintptr_t(0x90000+k*0x1000);
  m.Put<std::uint16_t>(0x31000+k*2,static_cast<std::uint16_t>(k));m.Put<std::uint8_t>(0x32000+k,static_cast<std::uint8_t>(k));m.Put<std::uint8_t>(0x33000+k,3);m.Put(0x34000+k*8,block);m.Put(block,s.image+0x2295210);m.Put(block+0x138,s.player);m.Put<std::uint32_t>(block+8,1);m.Put<std::uint32_t>(block+0xc,k);m.Put(block+0x78,ps);m.Put(ps,s.image+0x23b0c70);m.Put(ps+0x408,block);m.Put<std::uint32_t>(ps+8,k);
  m.Put(block+0x320,node);m.Put(node,s.image+0x2306628);m.Put<std::uint32_t>(node+0x20,1);m.Put<std::uint32_t>(node+0x24,k);m.Put<std::uint32_t>(node+0x2c,0x32);m.Put(block+0xb8,prim);m.Put(prim,s.image+0x23b9ac0);m.Put(prim+0x10,s.image+0x23b9b18);m.Put(prim+0x30,s.image+0x23b9b50);m.Put(prim+8,block);m.Put(prim+0x38,node);m.Put(block+0x548,prim+0x30);m.Put(node+0x38,prim+0x30);m.Put(prim+0x68,rec);m.Put(node+8,bitmap);m.Put<std::uint32_t>(bitmap,0);
  m.Put<std::uint16_t>(rec+0x78,0xffff);m.Put<std::uint16_t>(rec+0x7c,0xffff);m.Put<std::uint16_t>(rec+0x7a,static_cast<std::uint16_t>(k));m.Zero(rec+0x380,5);m.Zero(rec+0x6a2,8);m.Put(ps+0x280,psrec);m.Put<std::uint32_t>(psrec,0);
 }
 m.Put<std::uintptr_t>(0xb0000,s.image+0x22e8f00);m.Put<std::uintptr_t>(0xb0010,s.player);m.Put<std::uintptr_t>(0xb0a60,0x80000);m.Put<std::uint16_t>(0xb0aa0,1);m.Put<std::uint32_t>(0xb0a6c,0x2d);m.Put<std::uint32_t>(0xb1268,0x2d);m.Put<std::uint32_t>(0xb0ae4,0);m.Put<std::uint32_t>(0xb1288,0x20);return m;
}

namespace hp=mgscoop::horse_player_physical_read;
Memory PhysicalFixture(){auto m=Fixture();const auto s=S();
 for(unsigned k=0;k<2;++k){const auto block=std::uintptr_t(0x40000+k*0x6a8),ps=std::uintptr_t(0x50000+k*0x1000);
  const auto cs=std::uintptr_t(0xc0000+k*0x1000),entry=std::uintptr_t(0xc4000+k*0x1000),entity=std::uintptr_t(0xc8000+k*0x1000);
  const auto list=std::uintptr_t(0xca000+k*0x1000),component=std::uintptr_t(0xd0000+k*0x1000),physical=std::uintptr_t(0xd6000+k*0x1000),transform=std::uintptr_t(0xd8000+k*0x1000);
  m.Put(block+0x10,cs);m.Put(ps+0x238,cs);m.Put(cs,s.image+0x22e6070);m.Put<std::uint32_t>(cs+8,1);m.Put<std::uint32_t>(cs+0xc,k);m.Put(cs+0x10,entry);
  m.Put(entry+0x60,transform);m.Put(entry+0x70,entity);m.Put<std::uint32_t>(entity+0x28,1);m.Put(entity+0x30,list);m.Put(list,component);
  m.Put(component,s.image+0x24c97f8);m.Put<std::uint32_t>(component+0x10,0x7f1402f4);m.Put(component+0x68,transform);m.Put(component+0x80,s.image+0x24c9830);
  m.Put(ps+0x270,physical);m.Put<std::uintptr_t>(physical,component+0x80);m.Put<std::uint32_t>(component+0xd0,0);
  for(unsigned j=0;j<16;++j)m.Put<float>(component+0x90+j*4,j%5==0?1.0f:0.0f);
 }return m;
}

#include "horse_owned_cargo_prototype.hpp"
#include "horse_physical_callback_forward.hpp"
namespace pr=mgscoop::horse_physical_profile_read;
namespace pc=mgscoop::horse_physical_callback_probe;
namespace cargo=mgscoop::horse_owned_cargo_prototype;
using Phase=mgscoop::horse_player_cargo_api::Phase;
Memory ProfileFixture(){auto m=PhysicalFixture();const auto s=S();for(unsigned k=0;k<2;++k){auto component=std::uintptr_t(0xd0000+k*0x1000),list=std::uintptr_t(0xea000+k*0x100),profile=std::uintptr_t(0xec000+k*0x100);m.Put<std::uint16_t>(component+0x40,2);m.Put(component+0x48,list);m.Put<std::uintptr_t>(list,0);m.Put(list+8,profile);m.Put<std::uint32_t>(profile+0xc,1);m.Put<std::uint8_t>(component+0x70,1);m.Put<std::uint8_t>(component+0x71,0);}return m;}
cargo::Identity Id(){cargo::Identity id{};id.mission=30050;id.epoch=2;id.generation=3;id.bodyBirth=4;id.horseBirth=5;id.grant=6;id.body=1;id.horse=0x1a00;return id;}
cargo::RawWindow Window(){return {0xd1000,0xe0000,0x140f00100,9};}
struct Fake {
 cargo::Snapshot state{};unsigned captures=0,retire=0,admit=0,detach=0,apply=0,release=0;
 bool qualified=true,called=true,retireAccept=true,replyOk=true,commit=true,releaseAccept=true;
 Fake(){state.identity=Id();state.physical.component=0xd1000;state.profile.component=0xd1000;state.profile.profile=0xec100;state.profile.profileCount=1;state.profile.mode=1;state.profile.ownerActive[0]=1;state.maskHeld=state.residentGraph=true;
  state.horse.controller=0xf0000;state.horse.receiver=0xf0020;state.horse.record=0xf7000;state.horse.sdRecord=0xf8000;state.horse.unit=0xf7070;state.horse.gameId=0x1a00;state.horse.nativeIndex=0;
  for(unsigned k=0;k<16;++k)state.horse.cargoMatrix[k]=k%5==0?1.f:0.f;
 }
 static bool Capture(void* ctx,const cargo::RawWindow&,const cargo::Identity&,cargo::Snapshot& out,const char*& why){auto& f=*static_cast<Fake*>(ctx);++f.captures;why="fake_window_refused";out=f.state;return f.qualified;}
 static bool Retire(void* ctx,const cargo::Snapshot&,const char*& why){auto& f=*static_cast<Fake*>(ctx);++f.retire;why="fake_retirement_pending";return f.retireAccept;}
 static bool Horse(void* ctx,const cargo::Snapshot&,cargo::HorseMessage& message,std::int32_t& result,const char*& why){auto& f=*static_cast<Fake*>(ctx);why="fake_receiver_refused";if(!f.called)return false;result=f.replyOk?0:-1;message.unit=f.state.horse.unit;
  if(message.kind==1){++f.admit;if(f.commit){f.state.horse.body=f.state.horse.sdBody=message.body;f.state.horse.sdCargo=1;}}
  if(message.kind==2){++f.detach;if(f.commit){f.state.horse.body=f.state.horse.sdBody=0xffff;f.state.horse.sdCargo=0;}}return true;
 }
 static bool Apply(void* ctx,const cargo::RawWindow&,const cargo::Snapshot&,const float(&)[16],const char*& why){auto& f=*static_cast<Fake*>(ctx);++f.apply;why="fake_matrix_only";return true;}
 static bool Release(void* ctx,const cargo::RawWindow&,const cargo::Snapshot&,const char*& why){auto& f=*static_cast<Fake*>(ctx);++f.release;why="fake_physical_release_pending";return f.releaseAccept;}
 cargo::Backend Backend(){return {this,&Capture,&Retire,&Horse,&Apply,&Release};}
};
void Step(cargo::Controller& c,Fake& f){c.BeforeOriginal(Window(),f.Backend());}
void Start(cargo::Controller& c,Fake& f){c.SetEnabledForQualifiedWorker(true);Check(c.QueueLoad(Id()),"qualified fixture can stage intent");f.state.carryRetired=true;Step(c,f);Step(c,f);}
unsigned originalCalls=0,sampleCalls=0;bool recursive=false;pc::Probe* currentProbe=nullptr;
bool Sample(void*,const pc::RawWindow& raw,pc::Sample& out) noexcept {++sampleCalls;out.body=0;out.profile.component=raw.self;out.profile.profileCount=1;out.profile.mode=1;return true;}
void Original(void* self,void* pose){++originalCalls;Check(reinterpret_cast<std::uintptr_t>(self)==0xd0000&&reinterpret_cast<std::uintptr_t>(pose)==0xe0000,"original receives unchanged two arguments");if(recursive&&originalCalls==1){auto nested=pc::RawWindow{0xd0000,0xe0000,0x140f00100,9};pc::ForwardOriginal(&Original,nested,currentProbe,&Sample,nullptr);}SetLastError(777);}
void NullWindowOriginal(void*,void*){++originalCalls;SetLastError(888);}

#include "horse_physical_natural_cache.hpp"
namespace nr=mgscoop::horse_physical_natural_read;
Memory NaturalFixture(){auto m=ProfileFixture();const auto s=S();m.Put(s.image+0x2a82c30,s.player);m.Put<std::uint16_t>(s.image+0x2c01372,30050);m.Put<std::uintptr_t>(s.data+0x18,0x35000);m.Put<std::uint16_t>(0x35000,1);m.Put<std::uint16_t>(0x35002,0x2b);m.Put<std::uint32_t>(0xa0000,0x1000);m.Put<std::uint32_t>(0xa1000,0x140000c);m.Put<std::uint16_t>(0xd003c,2);m.Put<std::uint16_t>(0xd103c,2);m.Put<std::uint32_t>(0xec00c,UINT32_MAX);m.Put<std::uint32_t>(0xec10c,3);return m;}
nr::Published PublishedFrom(Memory& m){const auto s=S();hp::Observation current{};Check(hp::Observe(m,s,current),"published fixture comes from unchanged legitimate Lua owner scope");nr::Published p{};p.image=s.image;p.player=s.player;p.data=s.data;p.state=s.state;p.mission=s.mission;p.epoch=s.epoch;p.generation=s.generation;p.luaOwnerThread=s.ownerThread;p.birth[0]=11;p.birth[1]=12;p.at=1000;p.serial=1;p.admitted=p.exactPins=true;for(unsigned id=0;id<2;++id){p.binding[id]=current.body[id];p.ps[id]=current.carry.body[id].ps;p.blocks[id]=current.carry.body[id].block;}return p;}
unsigned sequenceReads=0;bool mutateSeq=false;
std::uint32_t WindowSequence() noexcept{return mutateSeq?(sequenceReads++?9u:8u):8u;}
bool InBatch() noexcept{return false;}
int main(){auto m=NaturalFixture();auto p=PublishedFrom(m);nr::Row row{};const auto raw=nr::RawWindow{0xd1000,0xc9000,p.image+0x1a6c17a,77};const auto oldLua=S();
 Check(nr::Observe(m,p,raw,1100,{8,8,false},row)&&row.ready&&row.actualThread==77&&row.luaOwnerThread==9,"separate actual engine thread observes current paired Entity without forging Lua owner");Check(oldLua.thread==oldLua.ownerThread&&oldLua.thread==9,"old Lua scope is unchanged");Check(row.profile.mode==3&&row.action[1]==0x2b&&row.life[1]==0x140000c,"actual carried mode3/action/PS life decoded in read-only callback");
 auto host=raw;host.self=0xd0000;host.poseContext=0xc8000;Check(nr::Observe(m,p,host,1100,{8,8,false},row)&&row.profile.mode==UINT32_MAX,"inactive host profile is valid observed -1, not matrix permission");
 Check(nr::Observe(m,p,raw,1100,{9,11,true},row)&&!row.inputEvenStable,"odd/changing input batch remains explicit READONLY evidence without writer permission");
 for(auto variant:{0u,1u,2u,3u,4u,5u,6u,7u}){auto invalid=p;switch(variant){case 0:invalid.admitted=false;break;case 1:invalid.exactPins=false;break;case 2:invalid.birth[1]=0;break;case 3:invalid.epoch=0;break;case 4:invalid.generation=0;break;case 5:invalid.at=0;break;case 6:invalid.serial=0;break;case 7:invalid.luaOwnerThread=0;break;}Check(!nr::Observe(m,invalid,raw,1100,{8,8,false},row),"unpublished/unowned/birthless native callback observation refused");}
 Check(!nr::Observe(m,p,raw,1751,{8,8,false},row)&&!std::strcmp(row.reason,"published_stale"),"stale scene publication rejected");auto wrong=raw;wrong.caller++;Check(!nr::Observe(m,p,wrong,1100,{8,8,false},row),"wrong caller outside pinned Entity dispatcher rejected");wrong=raw;wrong.poseContext=0xc8000;Check(!nr::Observe(m,p,wrong,1100,{8,8,false},row),"other body Entity cannot provide target's native context");wrong=raw;wrong.self=0xe0000;Check(!nr::Observe(m,p,wrong,1100,{8,8,false},row),"foreign scene component cannot become paired native body");wrong=raw;wrong.thread=0;Check(!nr::Observe(m,p,wrong,1100,{8,8,false},row),"unknown engine thread refused");
 struct Mutation {std::uintptr_t at,value;unsigned width;const char* label;};for(const auto x:{Mutation{p.image+0x2a82c30,0x11000,8,"current player singleton replaced"},{p.image+0x2c01372,30020,2,"current mission replaced"},{0x10060,0x21000,8,"current Q replaced"},{0x10214,1,4,"local player scope changed"},{0x10204,0x501,4,"online/offline ownership changed"},{0x33001,1,1,"partial current body realization"},{0x32001,0,1,"pooled body alias"},{0x51000,0x1423b0c78,8,"unverified current PS type"},{0xc1000,0x1422e6078,8,"unverified current character CS type"},{0xc5070,0xca000,8,"current body Entity replaced"},{0xd1000,0x1424c9800,8,"adjacent physical address point rejected"},{0xd1068,0xd8000,8,"borrowed other body transform rejected"},{0xd7000,0xd0080,8,"borrowed other native physical slot rejected"}}){auto changed=m;if(x.width==8)changed.Put<std::uintptr_t>(x.at,x.value);else if(x.width==4)changed.Put<std::uint32_t>(x.at,static_cast<std::uint32_t>(x.value));else if(x.width==2)changed.Put<std::uint16_t>(x.at,static_cast<std::uint16_t>(x.value));else changed.Put<std::uint8_t>(x.at,static_cast<std::uint8_t>(x.value));Check(!nr::Observe(changed,p,raw,1100,{8,8,false},row),x.label);}
 Memory gated;for(const auto& site:mgscoop::horse_player_physical_contract::sites)for(std::size_t k=0;k<site.size;++k)gated.bytes[p.image+site.rva+k]=site.bytes[k];for(const auto& site:mgscoop::horse_physical_profile_contract::sites)for(std::size_t k=0;k<site.size;++k)gated.bytes[p.image+site.rva+k]=site.bytes[k];for(const auto& site:mgscoop::horse_physical_natural_contract::sites)for(std::size_t k=0;k<site.size;++k)gated.bytes[p.image+site.rva+k]=site.bytes[k];for(const auto& slot:mgscoop::horse_player_physical_contract::slots)gated.Put(p.image+slot.rva,p.image+slot.target);std::uintptr_t mismatch=0;Check(nr::CodeGate(gated,p.image,0xa01d000,mismatch),"full physical/profile gates plus actual125B dispatcher admitted");for(const auto& site:mgscoop::horse_physical_natural_contract::sites){auto changed=gated;changed.bytes[p.image+site.rva+site.size-1]^=1;Check(!nr::CodeGate(changed,p.image,0xa01d000,mismatch)&&mismatch==site.rva,"all native caller chained spans include last byte");}
 nr::Cache cache{};Check(cache.Publish(p),"typed owner publication stored in atomic payload words");nr::Published copy{};std::uint64_t version=0;Check(cache.Read(copy,version)&&copy.luaOwnerThread==9&&copy.binding[1].entity==0xc9000&&cache.Current(version),"atomic published tuple round trip remains current");Check(nr::ObserveCached(m,cache,raw,1100,&WindowSequence,&InBatch,row)&&row.inputEvenStable,"cached observer resamples actual window sequence around current read");mutateSeq=true;sequenceReads=0;Check(nr::ObserveCached(m,cache,raw,1100,&WindowSequence,&InBatch,row)&&row.input.before==8&&row.input.after==9&&!row.inputEvenStable,"sequence changed during readonly layout is reported without fabricated duplicate sample");mutateSeq=false;
 Check(!nr::ObserveCached(m,cache,raw,1100,nullptr,&InBatch,row),"missing input observer cannot claim even stable input");Check(cache.Clear()&&!cache.Current(version),"replacement invalidates earlier atomic publication version");Check(!nr::ObserveCached(m,cache,raw,1100,&WindowSequence,&InBatch,row),"cleared stale publication cannot sample native entity");
 Check(nr::Observe(m,p,raw,1100,{8,8,false},row),"native line fixture restored");char line[161]{},life[161]{};Check(nr::Line(row,line,sizeof(line))&&std::strstr(line,"t=77,9")&&std::strstr(line,"ctx=1"),"bounded separate actual/Lua owner and input/context telemetry");Check(nr::LifeLine(row,life,sizeof(life))&&std::strstr(life,"q=1,2b")&&std::strstr(life,"prof=2,3,0"),"bounded per-candidate parts/action/life/profile telemetry");Check(!nr::Line(row,line,12),"native callback status truncation refused");std::printf("%u passed, %u failed\n",good,bad);return bad?1:0;
}
