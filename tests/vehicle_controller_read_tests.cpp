#include "vehicle_controller_read.hpp"
#include <map>
#include <utility>
#include <cstring>
#include <cstdio>
using namespace mgscoop::vehicle_controller_read;
unsigned good=0,bad=0;
void Check(bool b,const char* name){if(b)++good;else{++bad;std::printf("FAIL %s\n",name);}}
constexpr std::uintptr_t image=0x140000000ull,P=0x10000,Q=0x20000,S=0x30000,blocks=0x40000;
constexpr std::uintptr_t Primary(unsigned k){return 0x80000+k*0x1000;}
constexpr std::uintptr_t Node(unsigned k){return 0x60000+k*0x1000;}
constexpr std::uintptr_t Storage(unsigned k){return 0x70000+k*0x1000;}
constexpr std::uintptr_t Record(unsigned k){return Storage(k)+0x30;}
struct Memory {
 std::map<std::uintptr_t,unsigned char> bytes;bool flip=false,flipPadding=false;unsigned scans=0,recordReads=0;std::uintptr_t missing=0;
 template<class T>void Put(std::uintptr_t p,T value){unsigned char v[sizeof(T)]{};std::memcpy(v,&value,sizeof(T));for(unsigned i=0;i<sizeof(T);++i)bytes[p+i]=v[i];}
 void Zero(std::uintptr_t p,unsigned n){for(unsigned i=0;i<n;++i)bytes[p+i]=0;}
 bool operator()(std::uintptr_t p,void* out,std::size_t n) noexcept {
  if(p==P&&++scans==2){if(flip)Put<std::uint8_t>(Record(0)+0x57,2);if(flipPadding)Put<std::uint32_t>(Record(0)+0x4c,0x5aabcdefu);}
  if(p==Record(0)+0x40||p==Record(1)+0x40)++recordReads;
  if(missing&&p<=missing&&missing-p<n)return false;
  auto* dst=static_cast<unsigned char*>(out);for(std::size_t i=0;i<n;++i){auto it=bytes.find(p+i);if(it==bytes.end())return false;dst[i]=it->second;}return true;
 }
};
Scope Scene(){Scope s{};s.exactGate=s.admitted=s.ready=s.armed=true;s.image=image;s.player=P;s.data=Q;s.state=S;s.thread=s.ownerThread=9;s.mission=s.ownedMission=30050;s.epoch=s.ownedEpoch=2;s.generation=s.ownedGeneration=3;s.now=1000;s.poll=900;s.sequenceBefore=s.sequenceAfter=8;return s;}
struct Sample {
 Scope base=Scene(),changed=Scene();unsigned count=0,changeAt=0;
 Scope operator()() noexcept {++count;return changeAt&&count>=changeAt?changed:base;}
};
Memory Fixture(){Memory m;
 m.Put(P,image+0x2295490);m.Put(P+0x60,Q);m.Put(Q,image+0x23ad970);m.Put(Q+0x1e8,S);m.Put<std::uint32_t>(S+0x40,2);m.Put<std::uintptr_t>(P+0xa8,0);m.Put<std::uint32_t>(P+0x204,1);m.Put<std::uint32_t>(P+0x208,2);m.Put<std::uint32_t>(P+0x20c,2);m.Put<std::uint32_t>(P+0x214,0);m.Put<std::uint32_t>(P+0x218,0);
 for(auto p:{std::pair<unsigned,std::uintptr_t>{0x70,0x31000},{0x78,0x32000},{0x90,0x33000},{0x378,blocks},{0x50,0x34000}})m.Put(P+p.first,p.second);
 for(unsigned k=0;k<2;++k){const auto block=blocks+k*0x6a8,prim=Primary(k),node=Node(k),storage=Storage(k),rec=Record(k),bitmap=std::uintptr_t(0x90000+k*0x1000);
  m.Put<std::uint16_t>(0x31000+k*2,static_cast<std::uint16_t>(k));m.Put<std::uint8_t>(0x32000+k,static_cast<std::uint8_t>(k));m.Put<std::uint8_t>(0x33000+k,3);m.Put(0x34000+k*8,block);m.Put(block,image+0x2295210);m.Put(block+0x138,P);m.Put<std::uint32_t>(block+8,1);m.Put<std::uint32_t>(block+0xc,k);m.Put(block+0x128,prim);
  m.Put(prim,image+0x23cbea8);m.Put(prim+0x10,image+0x23cbee0);m.Put(prim+0x30,image+0x23cbf10);m.Put(prim+0x48,image+0x23cbf50);m.Put(prim+8,block);m.Put(prim+0x38,node);
  m.Put(node,image+0x2306628);m.Put<std::uint32_t>(node+0x20,1);m.Put<std::uint32_t>(node+0x24,k);m.Put<std::uint32_t>(node+0x2c,0x23);m.Put(node+0x38,prim+0x30);m.Put(node+8,bitmap);m.Put<std::uint32_t>(bitmap,0);
  m.Put(prim+0x50,storage);m.Put(prim+0x58,storage);m.Put<std::uintptr_t>(prim+0x60,0x270);m.Put<std::uintptr_t>(prim+0x68,0x270);m.Put<std::uint16_t>(prim+0x70,16);m.Put<std::uint16_t>(prim+0x72,1);m.Put<std::uint32_t>(prim+0x74,0x1c0048);m.Put(prim+0x78,rec);
  m.Put(storage,image+0x645940);m.Put(storage+0x10,rec);m.Put<std::uintptr_t>(storage+0x18,0x240);m.Put<std::uintptr_t>(storage+0x20,1);m.Zero(rec+0x40,0x48);m.Put<std::uint16_t>(rec+0x84,0xffff);m.Put<std::uint16_t>(rec+0x86,0xffff);
 }return m;
}
void Phase(Memory& m,unsigned k,unsigned phase,unsigned next,unsigned packed,std::uint16_t vehicle,std::uint16_t candidate){m.Put(Record(k)+0x40,image+mgscoop::vehicle_controller_contract::phase_callbacks[phase]);m.Put<std::uint8_t>(Record(k)+0x54,static_cast<std::uint8_t>(phase));m.Put<std::uint8_t>(Record(k)+0x55,static_cast<std::uint8_t>(next));m.Put<std::uint8_t>(Record(k)+0x57,static_cast<std::uint8_t>(packed));m.Put(Record(k)+0x84,vehicle);m.Put(Record(k)+0x86,candidate);}
bool Run(Memory& m,Scope s,Reply& out){Sample sample;sample.base=s;return Observe(m,sample,out);}
int main(){
 Memory gate;std::uintptr_t mismatch=0;
 for(const auto& x:mgscoop::vehicle_controller_contract::sites)for(std::size_t j=0;j<x.size;++j)gate.bytes[image+x.rva+j]=x.bytes[j];
 for(const auto& x:mgscoop::vehicle_controller_contract::slots)gate.Put(image+x.vtable_rva+x.offset,image+x.target_rva);
 Check(CodeGate(gate,image,0xa01d000,mismatch),"complete code and slots pass");
 for(const auto& x:mgscoop::vehicle_controller_contract::sites){gate.bytes[image+x.rva]^=1;Check(!CodeGate(gate,image,0xa01d000,mismatch)&&mismatch==x.rva,"each pinned source range mutation refuses");gate.bytes[image+x.rva]^=1;}
 for(const auto& x:mgscoop::vehicle_controller_contract::slots){gate.Put<std::uintptr_t>(image+x.vtable_rva+x.offset,0);Check(!CodeGate(gate,image,0xa01d000,mismatch),"each pinned vtable slot mutation refuses");gate.Put(image+x.vtable_rva+x.offset,image+x.target_rva);}
 Check(!CodeGate(gate,image,0x1000,mismatch),"short image refuses");Check(!CodeGate(gate,0xfffffffffffffff0ull,0x1000,mismatch),"image overflow refuses");
 auto s=Scene();auto m=Fixture();Reply r{};Check(Run(m,s,r)&&r.ready&&r.generation==3,"qualified two-body on-foot read");Check(r.body[0].vehicle==0xffff&&r.body[1].actualId==1,"native IDs and empty target exported");
 for(unsigned phase=0;phase<8;++phase){auto x=Fixture();Phase(x,0,phase,0,0x13,0x6409,0x6409);Check(Run(x,s,r)&&r.body[0].phase==phase&&r.body[0].entryOrSeat==3&&r.body[0].packedEntryOrSeat==0x13,"phase factory callback accepts only typed phase");}
 for(unsigned body=0;body<2;++body)for(unsigned phase=0;phase<8;++phase){auto x=Fixture();Phase(x,body,phase,0,0,0xffff,0xffff);x.Put<std::uint32_t>(Record(body)+0x4c,0x1234abcd);Check(Run(x,s,r)&&r.ready,"unspecified nonzero callback padding accepted for both bodies and all phases");}
 for(unsigned body=0;body<2;++body){auto x=Fixture();x.missing=Record(body)+0x4c;Check(Run(x,s,r)&&r.ready,"unreadable callback padding is never read");x=Fixture();x.Put<std::int32_t>(Record(body)+0x48,-1);Check(!Run(x,s,r)&&!r.ready,"negative signed32 callback adjustment still refuses");x=Fixture();x.Put<std::int32_t>(Record(body)+0x48,1);Check(!Run(x,s,r)&&!r.ready,"positive signed32 callback adjustment still refuses");}
 auto paddingRace=Fixture();paddingRace.flipPadding=true;Check(Run(paddingRace,s,r)&&r.ready,"padding-only change between observations is ignored");
 auto both=Fixture();Observation firstRaw{},secondRaw{};Check(ReadOnce(both,s,firstRaw)&&ReadOnce(both,s,secondRaw),"canonical padding comparison fixture initializes");firstRaw.body[0].raw[0x0c]=0x12;secondRaw.body[0].raw[0x0c]=0x34;Check(Same(firstRaw,secondRaw),"Same explicitly excludes callback padding even in manual observations");secondRaw.body[0].raw[8]=1;Check(!Same(firstRaw,secondRaw),"Same still compares the semantic signed32 adjustment");
 auto side=Fixture();Phase(side,0,1,0,1,0x6409,0x6409);Check(Run(side,s,r)&&r.body[0].phase==1&&r.body[0].entryOrSeat==1,"entry side1 remains raw and is not driver seat0");Phase(side,0,3,0,0,0x6409,0x6409);Check(Run(side,s,r)&&r.body[0].phase==3&&r.body[0].entryOrSeat==0,"settled driver field is0 after side entry");
 auto riding=Fixture();Phase(riding,0,2,0,0x13,0x6409,0x6409);Phase(riding,1,3,0,0x10,0x6409,0x6409);riding.Put<std::uint32_t>(0x90000,1);Check(Run(riding,s,r)&&r.body[0].nodeActive&&r.body[0].entryOrSeat==3&&r.body[1].entryOrSeat==0,"passenger raw selection and driver raw selection distinct");
 struct Change{std::uintptr_t p,value;unsigned width;const char* label;};
 for(auto x:{Change{P,0x142295498,8,"wrong player class"},{P+0x204,0x101,4,"online refusal"},{P+0xa8,0x10,8,"native sync refusal"},{P+0x208,4,4,"four bodies refusal"},{P+0x214,1,4,"nonzero local index"},{P+0x218,1,4,"injected selected local id"},{0x31002,0,2,"replaced native body id"},{0x32001,0,1,"shared body pool"},{0x32001,16,1,"out of bounds pool"},{0x33001,2,1,"loading body"},{blocks+8,2,4,"block allocation count"},{blocks+0xc,1,4,"block wrong native body"},{blocks+0x138,P+0x1000,8,"stale block owner"},{blocks+0x128,0,8,"missing controller"},{Primary(0),0x1423cbea0,8,"construction controller class"},{Primary(0)+0x30,0x1423cbf18,8,"wrong update view"},{Primary(0)+8,blocks+0x6a8,8,"wrong controller owner"},{Node(0)+0x20,2,4,"node count wrong"},{Node(0)+0x24,1,4,"node native body wrong"},{Node(0)+0x2c,0x33,4,"carry node is not vehicle"},{Node(0)+0x38,Primary(1)+0x30,8,"node backlink wrong"},{Primary(0)+0x50,0xfffffffffffffff0ull,8,"allocator overflow"},{Primary(0)+0x58,Storage(0)+8,8,"allocator header wrong"},{Primary(0)+0x60,0x240,8,"allocator extent too short"},{Primary(0)+0x68,0,8,"allocator not realized"},{Primary(0)+0x70,8,2,"allocator alignment wrong"},{Primary(0)+0x72,0,2,"allocator not finalized"},{Primary(0)+0x74,0,4,"allocator tag wrong"},{Primary(0)+0x78,Record(1),8,"record belongs to other allocation"},{Storage(0),image+0x645941,8,"header destructor wrong"},{Storage(0)+0x10,Record(1),8,"header records disagree"},{Storage(0)+0x18,0x200,8,"record stride wrong"},{Storage(0)+0x20,2,8,"record count wrong"},{Record(0)+0x54,8,1,"current phase invalid"},{Record(0)+0x55,8,1,"next phase invalid"},{Record(0)+0x48,8,8,"callback member adjustment wrong"},{Record(0)+0x40,image+0x1251200,8,"phase callback wrong"},{Record(0)+0x84,0x3400,2,"wrong latched target type"},{Record(0)+0x86,0x3400,2,"wrong candidate target type"}}){auto fixture=Fixture();if(x.width==8)fixture.Put<std::uintptr_t>(x.p,x.value);else if(x.width==4)fixture.Put<std::uint32_t>(x.p,static_cast<std::uint32_t>(x.value));else if(x.width==2)fixture.Put<std::uint16_t>(x.p,static_cast<std::uint16_t>(x.value));else fixture.Put<std::uint8_t>(x.p,static_cast<std::uint8_t>(x.value));r.ready=true;Check(!Run(fixture,s,r)&&!r.ready,x.label);if(x.p==Storage(0)+0x18)Check(fixture.recordReads==0,"bad allocation refuses before record read");}
 for(auto p:{P,Q,S+0x40,Primary(0)+0x50,Storage(0)+0x20,Record(0)+0x54,Record(1)+0x84}){auto fixture=Fixture();fixture.missing=p;Check(!Run(fixture,s,r)&&!r.ready,"reader failure refuses cleanly");}
 for(unsigned i=0;i<13;++i){auto x=s;switch(i){case 0:x.exactGate=false;break;case 1:x.admitted=false;break;case 2:x.ready=false;break;case 3:x.armed=false;break;case 4:x.thread++;break;case 5:x.ownedEpoch++;break;case 6:x.ownedGeneration++;break;case 7:x.mission=x.ownedMission=10010;break;case 8:x.now=100;break;case 9:x.poll=249;break;case 10:x.batch=true;break;case 11:x.sequenceBefore++;break;case 12:x.sequenceAfter+=2;break;}Check(!Run(m,x,r)&&!r.ready,"invalid natural scope refuses");}
 for(unsigned sampleAt:{2u,3u})for(unsigned kind=0;kind<7;++kind){auto fixture=Fixture();Sample sample;sample.changeAt=sampleAt;switch(kind){case 0:sample.changed.mission=sample.changed.ownedMission=30010;break;case 1:sample.changed.epoch=sample.changed.ownedEpoch=3;break;case 2:sample.changed.generation=sample.changed.ownedGeneration=4;break;case 3:sample.changed.player+=0x1000;break;case 4:sample.changed.thread=sample.changed.ownerThread=10;break;case 5:sample.changed.sequenceBefore=sample.changed.sequenceAfter=10;break;case 6:sample.changed.armed=false;break;}Check(!Observe(fixture,sample,r)&&!std::strcmp(r.reason,"scope_changed")&&!r.ready,"owner input or scene changed during read refuses");}
 auto race=Fixture();race.flip=true;Check(!Run(race,s,r)&&!std::strcmp(r.reason,"state_changed")&&!r.ready,"coherent-scope seat change refuses");
 auto alias=Fixture();alias.Put(Primary(1)+0x50,Storage(0));alias.Put(Primary(1)+0x58,Storage(0));alias.Put(Primary(1)+0x78,Record(0));Check(!Run(alias,s,r),"same records cannot represent both players");
 struct FailureCase{std::uintptr_t p;const char* reason;};
 for(auto c:{FailureCase{P,"player_identity"},{P+0x204,"player_provider_flags"},{P+0x70,"body_metadata"},{0x31002,"body1_metadata"},{blocks+0x128,"body0_block_owner"},{Primary(0),"body0_controller_owner"},{Node(1)+0x24,"body1_node_owner"},{Primary(0)+0x60,"body0_allocation_holder"},{Storage(1)+0x20,"body1_allocation_header"},{0x90000,"body0_active_bitmap"},{Record(1)+0x54,"body1_record_read"}}){auto x=Fixture();x.missing=c.p;Check(!Run(x,s,r)&&!std::strcmp(r.reason,c.reason),"constant first-failure label identifies exact group/body");}
 auto target=Fixture();target.Put<std::uint16_t>(Record(1)+0x86,0x3400);Check(!Run(target,s,r)&&!std::strcmp(r.reason,"body1_target_class"),"wrong target class has distinct first-failure label");
 auto phaseBad=Fixture();phaseBad.Put<std::uint8_t>(Record(0)+0x54,8);Check(!Run(phaseBad,s,r)&&!std::strcmp(r.reason,"body0_phase_callback"),"bad phase does not index callback table");
 auto direct=Fixture();Observation observed{};const char* first="unset";Check(ReadOnce(direct,s,observed,&first)&&first==nullptr,"optional failure cleared on successful direct observation");
 auto fresh=s;fresh.poll=250;Check(Run(m,fresh,r),"scope age exactly750 remains accepted");
 Check(!Range(0xfffffffffffffff0ull,0x20),"overflow refusal");std::printf("%u passed, %u failed\n",good,bad);return bad?1:0;
}
