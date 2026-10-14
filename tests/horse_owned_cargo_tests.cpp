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
int main(){const auto scope=S();std::uintptr_t mismatch=0;Memory gated;
 for(const auto& site:mgscoop::horse_physical_profile_contract::sites)for(std::size_t k=0;k<site.size;++k)gated.bytes[scope.image+site.rva+k]=site.bytes[k];
 for(const auto& slot:mgscoop::horse_physical_profile_contract::slots)gated.Put(scope.image+slot.rva,scope.image+slot.target);
 Check(pr::CodeGate(gated,scope.image,0xa01d000,mismatch),"complete physical update and closed selector fingerprints");
 for(const auto& site:mgscoop::horse_physical_profile_contract::sites){auto m=gated;m.bytes[scope.image+site.rva+site.size-1]^=1;Check(!pr::CodeGate(m,scope.image,0xa01d000,mismatch)&&mismatch==site.rva,"each profile full span includes final byte");}
 auto slotMutation=gated;slotMutation.Put<std::uintptr_t>(scope.image+0x24c9810,scope.image+0x96ead0);Check(!pr::CodeGate(slotMutation,scope.image,0xa01d000,mismatch),"primary +18 callback target must match");
 auto memory=ProfileFixture();hp::Observation physical{};Check(hp::Observe(memory,scope,physical),"correct 24C97F8 actual class profile fixture");pr::Profile profile{};
 Check(pr::Observe(memory,scope.image,physical.body[0],profile)&&profile.profile==0xec000&&pr::MatrixCompatible(profile),"original highest non-null profile selector and mode1");
 auto fallback=memory;fallback.Put<std::uintptr_t>(0xea008,0);fallback.Put<std::uintptr_t>(0xea000,0xed000);fallback.Put<std::uint32_t>(0xed00c,2);Check(pr::Observe(fallback,scope.image,physical.body[0],profile)&&profile.profile==0xed000&&pr::MatrixCompatible(profile),"selector falls back to first profile mode2");
 for(auto flags:{1u,2u,0x20u,0x23u}){auto m=memory;m.Put<std::uint32_t>(0xd00d0,flags);Check(pr::Observe(m,scope.image,physical.body[0],profile)&&!pr::MatrixCompatible(profile),"reset/external/paused flags never grant matrix apply");}
 for(auto mode:{0u,3u,UINT32_MAX}){auto m=memory;m.Put<std::uint32_t>(0xec00c,mode);Check(pr::Observe(m,scope.image,physical.body[0],profile)&&!pr::MatrixCompatible(profile),"unproved profile modes refused");}
 auto absent=memory;absent.Put<std::uint16_t>(0xd0040,0);Check(pr::Observe(absent,scope.image,physical.body[0],profile)&&!profile.profile&&!pr::MatrixCompatible(profile),"zero profile follows original early exit");
 auto unbounded=memory;unbounded.Put<std::uint16_t>(0xd0040,65);Check(!pr::Observe(unbounded,scope.image,physical.body[0],profile),"unbounded profile count refused");
 auto inactive=memory;inactive.Put<std::uint8_t>(0xd0070,0);Check(pr::Observe(inactive,scope.image,physical.body[0],profile)&&!pr::MatrixCompatible(profile),"inactive original owner refused");
 pc::Sample nativeSample{};const auto raw=pc::RawWindow{0xd0000,0xe0000,scope.image+0xf00100,9};Check(pc::SampleCurrent(memory,scope,raw,nativeSample)&&nativeSample.body==0,"actual callback self matches owned current body0");
 auto foreign=raw;foreign.self=0xf0000;Check(!pc::SampleCurrent(memory,scope,foreign,nativeSample),"foreign component callback never reported as player");
 auto noWindow=raw;noWindow.poseContext=0;Check(!pc::SampleCurrent(memory,scope,noWindow,nativeSample),"null natural context refused");
 {cargo::Controller c;Fake f;Check(!c.QueueLoad(Id()),"cargo prototype is OFF by default");Step(c,f);Check(!f.captures&&!f.admit&&!f.apply,"disabled prototype does not read/call/write");}
 {cargo::Controller c;Fake f;c.SetEnabledForQualifiedWorker(true);auto invalidId=Id();invalidId.body=2;Check(!c.QueueLoad(invalidId),"only actual owned bodies0/1 accepted");invalidId=Id();invalidId.horse=0x1200;Check(!c.QueueLoad(invalidId),"non-horse game object refused");}
 {cargo::Controller c;Fake f;Start(c,f);Check(c.phase()==Phase::binding_physical&&f.admit==1&&!f.apply,"native reply is provisional and never immediately binds");Step(c,f);Check(c.phase()==Phase::loaded&&f.apply==1&&c.HoldIncapacity(),"fresh actual cargo words precede fake binding and incapacity held");c.QueueDetach(Id());Step(c,f);Check(!f.detach&&c.phase()==Phase::detaching_horse,"detach queues without changing current original slot");Step(c,f);Check(f.detach==1&&c.phase()==Phase::awaiting_physical_release&&c.HoldIncapacity(),"original kind2 followed by fresh release wait");Step(c,f);Check(f.release==1&&c.HoldIncapacity(),"horse clear alone does not release player controls");f.state.physicalReleased=true;Step(c,f);Check(c.phase()==Phase::retired&&!c.HoldIncapacity(),"same slot physical and carry retirement precede wake eligibility");}
 {cargo::Controller c;Fake f;c.SetEnabledForQualifiedWorker(true);Check(c.QueueLoad(Id()),"retirement pending intent");Step(c,f);Step(c,f);Check(f.retire==1&&!f.admit&&c.HoldIncapacity(),"old carry retirement issued once then awaits actual readback");c.QueueDetach(Id());Step(c,f);Check(c.phase()==Phase::awaiting_player_retirement&&c.HoldIncapacity(),"cancel cannot release while old native carried node lives");f.state.carryRetired=true;Step(c,f);Check(c.phase()==Phase::retired&&!f.admit,"cancel safely retires before native horse call");}
 {cargo::Controller c;Fake f;c.SetEnabledForQualifiedWorker(true);Check(c.QueueLoad(Id()),"no window intent");auto w=Window();w.poseContext=0;c.BeforeOriginal(w,f.Backend());Check(!f.captures&&!f.admit&&!f.apply&&c.HoldIncapacity(),"no window means no backend/native write");}
 {cargo::Controller c;Fake f;Start(c,f);f.state.identity.generation++;Step(c,f);Check(!f.apply&&!f.detach&&c.HoldIncapacity(),"changed generation revokes all native actions");}
 {cargo::Controller c;Fake f;Start(c,f);f.state.horse.unit++;Step(c,f);Check(!f.apply&&!f.detach&&c.HoldIncapacity(),"changed horse unit never bound or cleaned as owned");}
 {cargo::Controller c;Fake f;f.replyOk=false;Start(c,f);Step(c,f);Check(!f.apply&&c.phase()==Phase::detaching_horse,"partial native commit with rejected reply cleans owned slot without binding");Step(c,f);Check(f.detach==1,"partial admission cleanup uses original kind2");}
 {cargo::Controller c;Fake f;f.replyOk=f.commit=false;Start(c,f);Step(c,f);Check(c.phase()==Phase::awaiting_physical_release&&!f.apply&&!f.detach,"native refusal with unchanged free slot never fabricates acceptance");}
 {cargo::Controller c;Fake f;f.commit=false;Start(c,f);for(unsigned n=0;n<241;++n)Step(c,f);Check(c.phase()==Phase::awaiting_physical_release&&!f.apply&&f.admit==1,"bounded provisional wait never repeats admission or writes occupancy");}
 for(auto flag:{1u,2u,0x20u}){cargo::Controller c;Fake f;Start(c,f);f.state.profile.flags=flag;Step(c,f);Check(!f.apply&&c.HoldIncapacity(),"native unsafe physical profile never copied");}
 {cargo::Controller c;Fake f;Start(c,f);f.state.horse.cargoMatrix[0]=NAN;Step(c,f);Check(!f.apply,"invalid cargo matrix never copied");}
 pc::Probe probe{};probe.image=scope.image;probe.Candidates(physical,scope.ownerThread,true);currentProbe=&probe;SetLastError(123);pc::ForwardOriginal(&Original,raw,&probe,&Sample,nullptr);Check(originalCalls==1&&sampleCalls==1&&GetLastError()==777&&pc::nesting==0,"read-only callback forwards exactly once, preserves original LastError and TLS");
 originalCalls=sampleCalls=0;recursive=true;pc::ForwardOriginal(&Original,raw,&probe,&Sample,nullptr);Check(originalCalls==2&&sampleCalls==1&&pc::nesting==0,"nested callback forwards original once per entry without recursive sampler");recursive=false;
 originalCalls=sampleCalls=0;pc::ForwardOriginal(&NullWindowOriginal,noWindow,&probe,&Sample,nullptr);Check(originalCalls==1&&!sampleCalls&&GetLastError()==888,"unqualified no-window callback still forwards original and no sampler");
 originalCalls=sampleCalls=0;auto wrongThread=raw;wrongThread.thread=27;pc::ForwardOriginal(&NullWindowOriginal,wrongThread,&probe,&Sample,nullptr);Check(originalCalls==1&&!sampleCalls,"other native thread counted but never enters current-owner sampler"); originalCalls=sampleCalls=0;pc::ForwardOriginal(&NullWindowOriginal,foreign,&probe,&Sample,nullptr);Check(originalCalls==1&&!sampleCalls,"foreign scene component takes atomic fast rejection before heavy sampler"); Check(probe.rawCalls.load()==6,"raw counters include every native entry and nested callback"); char rawLine[100]{};Check(probe.RawLine(rawLine,sizeof(rawLine))&&std::strstr(rawLine,"calls=6"),"separate bounded raw caller/thread diagnostics"); probe.Candidates(physical,scope.ownerThread,false); originalCalls=sampleCalls=0;pc::ForwardOriginal(&Original,raw,&probe,&Sample,nullptr);Check(originalCalls==1&&!sampleCalls,"cleared stale cache never samples current-looking callback"); char line[161]{};Check(probe.Line(line,sizeof(line))&&std::strstr(line,"f00100,9,1,1,0,1"),"bounded body callback caller/thread/profile telemetry");Check(!probe.Line(line,10),"callback diagnostic truncation refused");
 std::printf("%u passed, %u failed\n",good,bad);return bad?1:0;
}
