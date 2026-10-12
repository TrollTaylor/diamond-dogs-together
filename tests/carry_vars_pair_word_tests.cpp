#include "carry_vars_pair_word_window.hpp"
#include <map>
#include <cstring>
#include <iostream>
using namespace mgscoop;
namespace {bool owned=true,closedWriters=true,cleanupOwned=true;unsigned expectedPool=7;std::uintptr_t expectedGo=0x5000000;unsigned checks=0,failures=0;
void Check(bool a,const char* s){++checks;if(!a){++failures;std::cerr<<s<<"\n";}}
struct Mem {
 std::map<std::uintptr_t,unsigned char> m;unsigned failReads=0;
 template<class T>void Put(std::uintptr_t p,T v){const auto* b=reinterpret_cast<const unsigned char*>(&v);for(unsigned j=0;j<sizeof(v);++j)m[p+j]=b[j];}
 bool operator()(std::uintptr_t p,void* out,std::size_t n){if(failReads){--failReads;return false;}auto* b=static_cast<unsigned char*>(out);for(std::size_t j=0;j<n;++j){auto i=m.find(p+j);if(i==m.end())return false;b[j]=i->second;}return true;}
};
struct Cas {Mem& m;unsigned writes=0;bool fail=false,failVerify=false,conflict=false;bool operator()(std::uintptr_t p,std::uint16_t expected,std::uint16_t wanted,std::uint16_t& old){if(fail||!m(p,&old,2))return false;if(conflict){old=std::uint16_t(expected+1);return true;}if(old==expected){m.Put(p,wanted);++writes;if(failVerify){m.failReads=1;failVerify=false;}}return true;}};
constexpr auto B=std::uintptr_t{0x140000000};
partner_input_hook::NativePoseJobContext Make(Mem& m){
 partner_input_hook::NativePoseJobContext c{};c.go=expectedGo;c.player=c.go+0x20;c.block=0x5500000+expectedPool*0x6a8;c.state=0x5200000;c.modelCS=0x5300000;c.modelEntry=0x5310000;c.generation=3;c.sceneEpoch=9;c.thread=5;c.sequence=11;c.modelReady=true;c.selectedBodyOwned=true;c.selectedBody=1;c.pp=0x5400000;c.source=0x5700000;c.windowPPVT=0x9000000;
 m.Put(B+0x2a82c30,c.player);m.Put(c.player,B+0x2295490);m.Put(c.player+0x204,std::uint32_t{1});m.Put(c.player+0xa8,std::uintptr_t{0});m.Put(c.player+0x208,std::uint32_t{2});m.Put(c.player+0x20c,std::uint32_t{2});m.Put(c.player+0x214,std::uint32_t{0});m.Put(c.player+0x218,std::uint32_t{1});m.Put(c.player+0x78,std::uintptr_t{0x5100000});m.Put(std::uintptr_t{0x5100001},std::uint8_t{7});m.Put(c.player+0x378,std::uintptr_t{0x5500000});
 m.Put(B+0x2c16920,B+0x2c01360);m.Put(c.player+0x140,B+0x2c01360);m.Put(c.block,B+0x2295210);m.Put(c.block+8,std::uint32_t{1});m.Put(c.block+0xc,std::uint32_t{1});m.Put(c.block+0x138,c.player);m.Put(c.block+0x140,B+0x2c01360);m.Put(c.block+0x78,c.state);m.Put(c.state,B+0x23b0c70);m.Put(c.state+0x408,c.block);m.Put(c.state+8,std::uint32_t{1});m.Put(c.state+0x238,c.modelCS);m.Put(c.block+0x10,c.modelCS);m.Put(c.modelCS,B+0x22e6070);m.Put(c.modelCS+8,std::uint32_t{1});m.Put(c.modelCS+0xc,std::uint32_t{1});m.Put(c.modelCS+0x10,c.modelEntry);m.Put(c.player+0xb0,c.pp);m.Put(c.pp,c.windowPPVT);m.Put(c.pp+0x8,c.source);m.Put(c.pp+0x388,std::uintptr_t{0xdead388});m.Put(B+0x2c01360+0x10e,std::uint16_t{1234});return c;
}
}
namespace mgscoop::partner_input_hook {
bool NativePoseOwnedWindow()noexcept{return owned;}
bool NativePoseVarsPairWordOwnedWindow()noexcept{return closedWriters;}
bool NativePoseVarsPairWordCleanupWindow(const NativePoseJobContext& c)noexcept{return owned&&cleanupOwned&&c.go==expectedGo;}
bool RequireNativePoseOwnedJob(void* go,std::uint32_t pool)noexcept{return owned&&reinterpret_cast<std::uintptr_t>(go)==expectedGo&&pool==expectedPool;}
}
int main(){namespace w=carry_vars_pair_word_window;
 Mem m;auto c=Make(m);Cas cas{m};w::Shadow shadow{};w::Token token{};std::uint16_t word=0;
 Check(w::Before(m,cas,c,B,shadow,token),"first actual window");m(B+0x2c01360+0x10e,&word,2);Check(word==0xffff&&cas.writes==1,"installs empty body1 shadow");
 m.Put(B+0x2c01360+0x10e,std::uint16_t{42});Check(w::After(m,cas,c,B,shadow,token),"native change preserved and local restored");m(B+0x2c01360+0x10e,&word,2);Check(word==1234&&shadow.word==42&&cas.writes==2,"persistent shadow stores ACTUAL result");
 auto cc=c;cc.sequence++;Check(w::CanRetireShadow(cc,true,true,shadow,token),"successful Closed receipt");Check(!w::CanRetireShadow(cc,false,true,shadow,token),"failed cleanup retains");Check(!w::CanRetireShadow(cc,true,false,shadow,token),"active native retains");cc.sequence+=2;Check(!w::CanRetireShadow(cc,true,true,shadow,token),"foreign Closed sequence rejected");
 w::Token t2;c.sequence+=2;Check(w::Before(m,cas,c,B,shadow,t2),"later window same lease");m(B+0x2c01360+0x10e,&word,2);Check(word==42,"prior shadow installed");Check(w::After(m,cas,c,B,shadow,t2),"later cleanup");Check(!w::Before(m,cas,c,B,shadow,t2),"consumed token not reused");
 const auto reject=[&](auto mutation,const char* name){Mem mm;auto x=Make(mm);mutation(mm,x);Cas cs{mm};w::Token tt;w::Shadow ss;Check(!w::Before(mm,cs,x,B,ss,tt)&&cs.writes==0,name);};
 reject([](auto&,auto& x){x.selectedBodyOwned=false;},"unowned selection");reject([](auto&,auto& x){x.selectedBody=0;},"wrong selected body");reject([](auto&,auto& x){x.sequence=12;},"outside window sequence");reject([](auto&,auto& x){x.generation=0;},"missing generation");reject([](auto&,auto& x){x.sceneEpoch=0;},"missing scene epoch");reject([](auto&,auto& x){x.thread=0;},"missing thread");reject([](auto& mm,auto& x){mm.Put(x.player+0x140,std::uintptr_t{0xdead});},"wrong vars owner");reject([](auto& mm,auto& x){mm.Put(x.block+0x140,std::uintptr_t{0xdead});},"wrong block vars");reject([](auto& mm,auto& x){mm.Put(x.state+0x408,std::uintptr_t{0xdead});},"state replacement");reject([](auto& mm,auto& x){mm.Put(x.modelCS+0x10,std::uintptr_t{0xdead});},"entry replacement");reject([](auto& mm,auto&){mm.Put(std::uintptr_t{0x5100001},std::uint8_t{16});},"invalid pool index");reject([](auto& mm,auto& x){mm.Put(x.player+0x218,std::uint32_t{0});},"selected ID changed");reject([](auto& mm,auto& x){mm.Put(x.player+0x204,std::uint32_t{0x101});},"online synchronizer flags");
 // Exact stock PP source is+8. Unrelated+388 must neither admit a wrong
 // source nor reject the genuine source or affect its restoration.
 reject([](auto& mm,auto& x){mm.Put(x.pp+0x8,std::uintptr_t{0xdead});mm.Put(x.pp+0x388,x.source);},"correct competing+388 cannot replace wrong actual+8");
 reject([](auto& mm,auto& x){for(unsigned n=0;n<8;++n)mm.m.erase(x.pp+0x8+n);mm.Put(x.pp+0x388,x.source);},"missing actual+8 source refuses beforeCAS despite matching+388");
 Mem source;auto pc=Make(source);Cas pcas{source};w::Shadow ps;w::Token pt;
 Check(w::Before(source,pcas,pc,B,ps,pt)&&pcas.writes==1,"real+8 source with competing unrelated+388 admits");
 source.Put(pc.pp+0x388,std::uintptr_t{0x12345678});
 Check(w::After(source,pcas,pc,B,ps,pt)&&pcas.writes==2,"unrelated+388 drift does not block exact same-owner cleanup");
 owned=false;reject([](auto&,auto&){},"missing actual writer");owned=true;closedWriters=false;reject([](auto&,auto&){},"ungated global writers");closedWriters=true;
 Mem n;auto cx=Make(n);Cas cs{n};w::Token tx;w::Shadow sx;Check(w::Before(n,cs,cx,B,sx,tx),"prepare replacement refusal");n.Put(cx.player+0x140,std::uintptr_t{0xdead});auto count=cs.writes;Check(!w::After(n,cs,cx,B,sx,tx)&&cs.writes==count&&tx.active(),"never restore old replacement owner");
 Mem q;auto cq=Make(q);Cas qc{q};w::Token qt;w::Shadow qs;Check(w::Before(q,qc,cq,B,qs,qt),"prepare cleanup-only refusal");cleanupOwned=false;Check(!w::After(q,qc,cq,B,qs,qt)&&qt.active(),"cleanup receipt required");cleanupOwned=true;Check(w::After(q,qc,cq,B,qs,qt),"same owner cleanup retriable");

 // Post-CAS read faults preserve the actual remote result and retry without a
 // second write. This is the faulted same-owner cleanup path, not admission.
 Mem late;auto lc=Make(late);Cas lcas{late};w::Shadow ls;w::Token lt;
 Check(w::Before(late,lcas,lc,B,ls,lt),"prepare post-write read refusal");
 late.Put(B+0x2c01360+0x10e,std::uint16_t{71});lcas.failVerify=true;
 Check(!w::After(late,lcas,lc,B,ls,lt)&&lt.active()&&!ls.bound,"post-write read fault retains token");
 auto written=lcas.writes;
 Check(!w::Renew(lt)&&lt.active(),"renew cannot erase outstanding cleanup");
 Check(w::After(late,lcas,lc,B,ls,lt)&&ls.word==71&&lcas.writes==written,"retry proves local restore and preserves remote result without write");
 Check(w::Renew(lt)&&!lt.restored(),"retired token renews explicitly");
 Mem conflict;auto kc=Make(conflict);Cas kcas{conflict};w::Shadow ks;w::Token kt;
 kcas.conflict=true;Check(!w::Before(conflict,kcas,kc,B,ks,kt)&&kcas.writes==0&&!kt.active(),"CAS conflict cannot capture or grant token");
 kcas.conflict=false;Check(w::Before(conflict,kcas,kc,B,ks,kt),"prepare restore conflict");
 conflict.Put(B+0x2c01360+0x10e,std::uint16_t{99});kcas.conflict=true;written=kcas.writes;
 Check(!w::After(conflict,kcas,kc,B,ks,kt)&&kt.active()&&kcas.writes==written&&!ks.bound,"restore conflict retains hold and remote result");
 kcas.conflict=false;Check(w::After(conflict,kcas,kc,B,ks,kt)&&ks.word==99,"same-owner restore after conflict");
 Mem generation;auto gc=Make(generation);Cas gcas{generation};w::Shadow gs;w::Token gt;
 Check(w::Before(generation,gcas,gc,B,gs,gt),"prepare lease generation drift");
 ++gc.generation;written=gcas.writes;
 Check(!w::After(generation,gcas,gc,B,gs,gt)&&gt.active()&&gcas.writes==written,"generation replacement never restores using new actor lease");
 --gc.generation;Check(w::After(generation,gcas,gc,B,gs,gt),"unchanged original generation cleanup");

 // Verify the complete long initializer pin, including its last byte. A prefix
 // only check would pass this corruption and is explicitly rejected.
 Mem code;for(const auto& site:carry_vars_pair_word_contract::sites)
  for(std::size_t i=0;i<site.size;++i)code.Put(B+site.rva+i,site.bytes[i]);
 for(std::size_t i=0;i<sizeof(carry_vars_pair_word_contract::field_name);++i)
  code.Put(B+0x228b558+i,carry_vars_pair_word_contract::field_name[i]);
 std::uintptr_t mismatch=0;
 Check(w::CodeGate(code,B,0xa01d000,mismatch),"complete byte gate admits exact image");
 const auto last=B+0x996b70+80363-1;code.m[last]^=1;
 Check(!w::CodeGate(code,B,0xa01d000,mismatch)&&mismatch==last-B,"long initializer late corruption refused");code.m[last]^=1;
 Check(!w::CodeGate(code,B,0x996b70+80362,mismatch),"truncated long initializer refused");
 code.m.erase(B+0x9aaaaf+46);
 Check(!w::CodeGate(code,B,0xa01d000,mismatch),"generic U16 setter tail read refusal");
 std::cout<<checks<<" checks; "<<failures<<" failures\n";return failures?1:0;
}
