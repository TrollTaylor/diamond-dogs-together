#include "camera_mode_window.hpp"
#include <map>
#include <cstring>
#include <cstdio>
#include <initializer_list>
using namespace mgscoop::camera_mode_window;
unsigned passed=0,failed=0;
void Check(bool result,const char* label){if(result)++passed;else{++failed;std::printf("FAIL %s\n",label);}}
constexpr std::uintptr_t image=0x140000000ull,P=0x10000,Root=0x20000,I=Root+0x20,Byte=I+0x6d,Active=I+0x6c;
struct Memory {
 std::map<std::uintptr_t,unsigned char> bytes;std::uintptr_t missing=0,flipAddress=0;std::uint8_t flipValue=0;unsigned playerReads=0,flipAt=0,activeReads=0;
 template<class T>void Put(std::uintptr_t p,T value){unsigned char data[sizeof(T)]{};std::memcpy(data,&value,sizeof(T));for(std::size_t k=0;k<sizeof(T);++k)bytes[p+k]=data[k];}
 bool operator()(std::uintptr_t p,void* out,std::size_t n) noexcept {
  if(p==P&&++playerReads==flipAt)Put(flipAddress,flipValue);
  if(p<=Active&&Active-p<n)++activeReads;
  if(missing&&p<=missing&&missing-p<n)return false;
  auto* dst=static_cast<unsigned char*>(out);for(std::size_t k=0;k<n;++k){auto it=bytes.find(p+k);if(it==bytes.end())return false;dst[k]=it->second;}return true;
 }
 std::uint8_t Value(std::uintptr_t p) const {auto it=bytes.find(p);return it==bytes.end()?0:it->second;}
};
Scope Scene(){Scope s{};s.exactGate=s.ownedBody1=s.nativeSandbox=s.serialized=true;s.image=image;s.player=P;s.thread=s.ownerThread=9;s.generation=3;s.sceneEpoch=5;s.windowSequence=11;return s;}
struct Sample {
 Scope base=Scene(),changed=Scene();unsigned calls=0,changeAt=0;
 Scope operator()() noexcept {++calls;return changeAt&&calls>=changeAt?changed:base;}
};
Memory Fixture(){Memory m;m.Put(image+0x2a82c30,P);m.Put(P,image+0x2295490);m.Put<std::uintptr_t>(P+0xa8,0);m.Put<std::uint32_t>(P+0x204,1);m.Put<std::uint32_t>(P+0x208,2);m.Put<std::uint32_t>(P+0x20c,2);m.Put<std::uint32_t>(P+0x214,0);m.Put<std::uint32_t>(P+0x218,1);m.Put(P+0xc0,I);m.Put(Root,image+0x23c74c8);m.Put(I,image+0x23c7500);m.Put(I+8,P);m.Put(image+0x23c7500+0xf8,image+0x11f6520);m.Put<std::uint8_t>(Byte,2);return m;}
struct Writer {
 Memory& m;unsigned calls=0;std::uintptr_t lastAddress=0;bool conflict=false,fault=false,readbackFault=false,postOwner=false,postByte=false;Scope* postScope=nullptr;
 WriteStatus operator()(std::uintptr_t p,std::uint8_t expected,std::uint8_t desired) noexcept {
  ++calls;lastAddress=p;
  if(conflict){m.Put<std::uint8_t>(p,static_cast<std::uint8_t>(expected^0x55u));return WriteStatus::conflict;}
  if(fault)return WriteStatus::fault;
  std::uint8_t current=0;if(!Get(m,p,current))return WriteStatus::fault;if(current!=expected)return WriteStatus::conflict;
  m.Put(p,desired);if(readbackFault)m.missing=P;if(postOwner)m.Put(I+8,P+0x1000);if(postByte)m.Put<std::uint8_t>(p,static_cast<std::uint8_t>(desired^1u));if(postScope)++postScope->generation;return WriteStatus::written;
 }
};
bool CaptureFixture(Memory& m,Sample& sample,Token& token,Reply& reply){const bool result=Capture(m,sample,token,reply);sample.calls=0;m.playerReads=0;return result;}
void ChangeScope(Scope& s,unsigned kind){switch(kind){case 0:s.exactGate=false;break;case 1:s.ownedBody1=false;break;case 2:s.nativeSandbox=false;break;case 3:s.serialized=false;break;case 4:s.batch=true;break;case 5:++s.thread;break;case 6:++s.ownerThread;break;case 7:s.generation=0;break;case 8:s.sceneEpoch=0;break;case 9:s.windowSequence=0;break;case 10:s.windowSequence=12;break;case 11:s.image=0;break;case 12:s.player=0;break;case 13:++s.generation;break;case 14:++s.sceneEpoch;break;case 15:s.windowSequence+=2;break;case 16:s.player+=0x1000;break;case 17:s.image+=0x1000;break;}}
int main(){
 Memory gate;std::uintptr_t mismatch=0;for(const auto& x:mgscoop::camera_mode_window_contract::sites)for(std::size_t k=0;k<x.size;++k)gate.bytes[image+x.rva+k]=x.bytes[k];for(const auto& x:mgscoop::camera_mode_window_contract::slots)gate.Put(image+x.vtable_rva+x.offset,image+x.target_rva);
 Check(CodeGate(gate,image,0xa01d000,mismatch),"exact complete source ranges and slots pass");
 for(const auto& x:mgscoop::camera_mode_window_contract::sites){gate.bytes[image+x.rva]^=1;Check(!CodeGate(gate,image,0xa01d000,mismatch)&&mismatch==x.rva,"each complete code pin mutation refuses");gate.bytes[image+x.rva]^=1;}
 for(const auto& x:mgscoop::camera_mode_window_contract::slots){gate.Put<std::uintptr_t>(image+x.vtable_rva+x.offset,0);Check(!CodeGate(gate,image,0xa01d000,mismatch)&&mismatch==x.vtable_rva+x.offset,"each slot mutation refuses");gate.Put(image+x.vtable_rva+x.offset,image+x.target_rva);}
 Check(!CodeGate(gate,image,0x1000,mismatch),"short image refuses");Check(!CodeGate(gate,0xfffffffffffffff0ull,0x1000,mismatch),"image overflow refuses");
 for(unsigned value=0;value<256;++value){auto m=Fixture();Sample sample;Token token;Reply out;Writer w{m};m.Put<std::uint8_t>(Byte,static_cast<std::uint8_t>(value));m.Put<std::uint8_t>(Active,0xa7);const bool captured=CaptureFixture(m,sample,token,out);m.Put<std::uint8_t>(Byte,static_cast<std::uint8_t>(value^0x5au));Check(captured&&token.Armed()&&Restore(m,w,sample,token,out)&&out.changed&&out.restored&&!token.Armed()&&w.calls==1&&w.lastAddress==Byte&&m.Value(Byte)==value&&m.Value(Active)==0xa7&&m.activeReads==0,"all 256 raw request values restore one byte and preserve active mode");}
 {auto m=Fixture();Sample sample;Token token;Reply out;Writer w{m};Check(CaptureFixture(m,sample,token,out)&&Restore(m,w,sample,token,out)&&out.restored&&!out.changed&&w.calls==0,"unchanged mode requires no write");Check(!Restore(m,w,sample,token,out)&&w.calls==0&&!std::strcmp(out.reason,"no_snapshot"),"consumed token cannot restore again");}
 {auto m=Fixture();m.missing=Active;Sample sample;Token token;Reply out;Writer w{m};Check(CaptureFixture(m,sample,token,out),"unmapped active byte is never captured");m.Put<std::uint8_t>(Byte,5);Check(Restore(m,w,sample,token,out)&&m.activeReads==0,"unmapped active byte is never read or restored");}
 {auto m=Fixture();Sample sample;Token token;Reply out;Check(CaptureFixture(m,sample,token,out),"owned token starts valid");Check(!Capture(m,sample,token,out)&&token.Armed()&&!std::strcmp(out.reason,"snapshot_already_owned"),"duplicate capture cannot discard outstanding owned snapshot");Writer w{m};Check(Restore(m,w,sample,token,out),"original snapshot survives duplicate capture refusal");}
 for(unsigned kind=0;kind<13;++kind){auto m=Fixture();Sample sample;ChangeScope(sample.base,kind);Token token;Reply out;Check(!Capture(m,sample,token,out)&&!token.Armed(),"invalid capture lease refuses without snapshot");}
 for(unsigned kind=0;kind<2;++kind){auto m=Fixture();Sample sample;if(kind)sample.base.stopping=true;else sample.base.faulted=true;Token token;Reply out;Check(!Capture(m,sample,token,out)&&!token.Armed()&&!std::strcmp(out.reason,"capture_disabled"),"new capture after fault or stopping refused");}
 struct Patch{std::uintptr_t p,value;unsigned width;const char* label;};
 const Patch patches[]={
 {image+0x2a82c30,P+8,8,"global current player changed"},{P,image+0x2295498,8,"wrong player type"},{P+0xa8,0x1000,8,"native online sync object refused"},{P+0x204,0,4,"invalid provider refused"},{P+0x204,0x101,4,"online flag refused"},{P+0x204,0x40001,4,"alternate dispatch refused"},{P+0x208,4,4,"wrong body count"},{P+0x20c,1,4,"unrealized partner"},{P+0x214,1,4,"wrong local player"},{P+0xc0,0,8,"missing C0"},{P+0xc0,8,8,"C0 primary subtraction underflow"},{P+0xc0,I+1,8,"C0 unaligned"},{P+0xc0,0xfffffffffffffff8ull,8,"C0 extent overflow"},{Root,image+0x23c74b0,8,"factory is not final primary type"},{I,image+0x23c7508,8,"wrong final interface type"},{I+8,P+0x1000,8,"wrong C0 owner"},{image+0x23c7500+0xf8,image+0x11f6524,8,"replaced request setter slot"}};
 for(const auto& p:patches){auto m=Fixture();if(p.width==8)m.Put<std::uintptr_t>(p.p,p.value);else m.Put<std::uint32_t>(p.p,static_cast<std::uint32_t>(p.value));Sample sample;Token token;Reply out;Check(!Capture(m,sample,token,out)&&!token.Armed(),p.label);}
 for(auto p:{image+0x2a82c30,P,P+0xa8,P+0x204,P+0xc0,Root,I,I+8,image+0x23c7500+0xf8,Byte}){auto m=Fixture();m.missing=p;Sample sample;Token token;Reply out;Check(!Capture(m,sample,token,out)&&!token.Armed(),"capture reader faults refuse without writes");}
 for(unsigned at:{2u,3u}){auto m=Fixture();Sample sample;sample.changeAt=at;sample.changed.generation++;Token token;Reply out;Check(!Capture(m,sample,token,out)&&!token.Armed()&&!std::strcmp(out.reason,"scope_changed"),"fresh capture lease detects middle/final generation changes");}
 {auto m=Fixture();m.flipAt=2;m.flipAddress=Byte;m.flipValue=5;Sample sample;Token token;Reply out;Check(!Capture(m,sample,token,out)&&!token.Armed()&&!std::strcmp(out.reason,"c0_changed"),"capture refuses request byte race");}
 {auto m=Fixture();m.Put<std::uint32_t>(P+0x204,0x10001);Sample sample;Token token;Reply out;Writer w{m};Check(CaptureFixture(m,sample,token,out),"unrelated provider flags accepted");m.Put<std::uint32_t>(P+0x204,0x20001);m.Put<std::uint32_t>(P+0x218,0);m.Put<std::uint8_t>(Byte,5);Check(Restore(m,w,sample,token,out),"unrelated flags and owned local index shadow do not imply stale owner");}
 for(unsigned kind=0;kind<18;++kind){auto m=Fixture();Sample sample;Token token;Reply out;Writer w{m};Check(CaptureFixture(m,sample,token,out),"restore lease fixture initialized");m.Put<std::uint8_t>(Byte,5);ChangeScope(sample.base,kind);Check(!Restore(m,w,sample,token,out)&&!token.Armed()&&w.calls==0&&!std::strcmp(out.reason,"restore_scope_changed"),"changed restore lease consumes token without write");}
 for(const auto& p:patches){auto m=Fixture();Sample sample;Token token;Reply out;Writer w{m};Check(CaptureFixture(m,sample,token,out),"restore typed identity fixture initialized");if(p.width==8)m.Put<std::uintptr_t>(p.p,p.value);else m.Put<std::uint32_t>(p.p,static_cast<std::uint32_t>(p.value));m.Put<std::uint8_t>(Byte,5);Check(!Restore(m,w,sample,token,out)&&!token.Armed()&&w.calls==0,"replacement wrong type/provider owner refuses restore");}
 {auto m=Fixture();Sample sample;Token token;Reply out;Writer w{m};Check(CaptureFixture(m,sample,token,out),"replacement final C0 fixture initialized");constexpr std::uintptr_t replacement=0x30020;m.Put(P+0xc0,replacement);m.Put(replacement-0x20,image+0x23c74c8);m.Put(replacement,image+0x23c7500);m.Put(replacement+8,P);m.Put<std::uint8_t>(replacement+0x6d,5);Check(!Restore(m,w,sample,token,out)&&w.calls==0&&!std::strcmp(out.reason,"restore_owner_changed"),"new C0 of same final type/owner is still not old allocation");}
 for(auto p:{image+0x2a82c30,P,P+0xa8,P+0x204,P+0xc0,Root,I,I+8,image+0x23c7500+0xf8,Byte}){auto m=Fixture();Sample sample;Token token;Reply out;Writer w{m};Check(CaptureFixture(m,sample,token,out),"restore reader fault fixture initialized");m.Put<std::uint8_t>(Byte,5);m.missing=p;Check(!Restore(m,w,sample,token,out)&&!token.Armed()&&w.calls==0,"restore reader fault consumes token without writer");}
 for(unsigned at:{2u,3u}){auto m=Fixture();Sample sample;Token token;Reply out;Writer w{m};Check(CaptureFixture(m,sample,token,out),"restore fresh snapshot fixture initialized");m.Put<std::uint8_t>(Byte,5);sample.changeAt=at;sample.changed.windowSequence+=2;Check(!Restore(m,w,sample,token,out)&&w.calls==0,"middle/before-write window changes refuse restore");}
 {auto m=Fixture();Sample sample;Token token;Reply out;Writer w{m};Check(CaptureFixture(m,sample,token,out),"restore request race fixture initialized");m.Put<std::uint8_t>(Byte,5);m.flipAt=2;m.flipAddress=Byte;m.flipValue=6;Check(!Restore(m,w,sample,token,out)&&w.calls==0&&!std::strcmp(out.reason,"restore_request_changed"),"request changes during pre-write typed reads refuse CAS");}
 for(unsigned kind=0;kind<2;++kind){auto m=Fixture();Sample sample;Token token;Reply out;Writer w{m};Check(CaptureFixture(m,sample,token,out),"cleanup fixture initialized");m.Put<std::uint8_t>(Byte,5);if(kind)sample.base.stopping=true;else sample.base.faulted=true;Check(Restore(m,w,sample,token,out)&&w.calls==1,"same owned lease permits fault/stopping cleanup");}
 for(unsigned kind=0;kind<2;++kind){auto m=Fixture();Sample sample;Token token;Reply out;Writer w{m};Check(CaptureFixture(m,sample,token,out),"changed unwind lifetime fixture initialized");m.Put<std::uint8_t>(Byte,5);sample.base.faulted=true;if(kind)sample.base.sceneEpoch++;else sample.base.ownedBody1=false;Check(!Restore(m,w,sample,token,out)&&w.calls==0,"fault cleanup never overrides changed ownership/lifetime");}
 {auto m=Fixture();Sample sample;Token token;Reply out;Writer w{m};Check(CaptureFixture(m,sample,token,out),"CAS conflict fixture initialized");m.Put<std::uint8_t>(Byte,5);w.conflict=true;Check(!Restore(m,w,sample,token,out)&&out.writeAttempted&&w.calls==1&&m.Value(Byte)==(5^0x55)&&!std::strcmp(out.reason,"restore_conflict"),"foreign request at CAS remains untouched");Check(!Restore(m,w,sample,token,out)&&w.calls==1,"CAS conflict cannot retry stale snapshot");}
 {auto m=Fixture();Sample sample;Token token;Reply out;Writer w{m};Check(CaptureFixture(m,sample,token,out),"CAS fault fixture initialized");m.Put<std::uint8_t>(Byte,5);w.fault=true;Check(!Restore(m,w,sample,token,out)&&out.writeAttempted&&w.calls==1&&!std::strcmp(out.reason,"restore_write_fault"),"write fault explicit and token consumed");}
 for(unsigned kind=0;kind<4;++kind){auto m=Fixture();Sample sample;Token token;Reply out;Writer w{m};Check(CaptureFixture(m,sample,token,out),"post-write validation fixture initialized");m.Put<std::uint8_t>(Byte,5);switch(kind){case 0:w.readbackFault=true;break;case 1:w.postOwner=true;break;case 2:w.postByte=true;break;case 3:w.postScope=&sample.base;break;}Check(!Restore(m,w,sample,token,out)&&out.writeAttempted&&!out.restored&&!token.Armed()&&w.calls==1,"post-CAS read/type/byte/lease failure explicit without retry");}
 Check(!Range(0xfffffffffffffff0ull,0x20),"bounded address overflow rejected");std::printf("%u passed, %u failed\n",passed,failed);return failed?1:0;
}
