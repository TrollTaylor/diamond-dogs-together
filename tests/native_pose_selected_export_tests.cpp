#include "native_pose_selected_export.hpp"
#include <iostream>
namespace sx=mgscoop::native_pose_selected_export;
struct Reader {std::uintptr_t image=0x140000000ull,corrupt=0;bool wrongSlot=false;
 bool operator()(std::uintptr_t p,void* out,std::size_t n)noexcept {const auto off=p-image;
  for(const auto& s:mgscoop::native_pose_selected_export_contract::sites)if(off>=s.rva&&off-s.rva<=s.size&&n<=s.size-(off-s.rva)){std::memcpy(out,s.bytes+off-s.rva,n);if(corrupt>=off&&corrupt-off<n)static_cast<unsigned char*>(out)[corrupt-off]^=1;return true;}
  for(const auto& s:mgscoop::native_pose_selected_export_contract::slots)if(off==s.rva&&n==8){auto v=image+s.target+(wrongSlot?1:0);std::memcpy(out,&v,n);return true;}return false;}
};
int main(){unsigned n=0,f=0;auto C=[&](bool v,const char* why){++n;if(!v){++f;std::cerr<<why<<"\n";}};
 sx::Scope s{};s.codeGate=s.worker=s.writer=s.birthCurrent=s.modelPair=s.selectedOwned=true;
 s.generation=s.currentGeneration=3;s.sceneEpoch=s.currentEpoch=4;s.thread=s.ownerThread=5;s.sequence=s.contextSequence=7;
 s.birthSerial=s.currentBirth=9;s.image=0x140000000ull;s.caller=s.image+0xff2208;
 s.player=s.currentPlayer=0x1000;s.block=s.currentBlock=0x2000;s.state=s.currentState=s.argumentState=0x3000;s.cs=s.currentCS=0x4000;s.entry=s.currentEntry=0x5000;
 s.actualBody=1;s.realLocal=0;s.selected=1;C(sx::Suppress(s),"exact owned body1 local0 export suppressed");
 for(auto member:{&sx::Scope::codeGate,&sx::Scope::worker,&sx::Scope::writer,&sx::Scope::birthCurrent,&sx::Scope::modelPair,&sx::Scope::selectedOwned}){auto x=s;x.*member=false;C(!sx::Suppress(x),"missing actual window proof refuses");}
 for(auto member:{&sx::Scope::currentGeneration,&sx::Scope::currentEpoch,&sx::Scope::ownerThread,&sx::Scope::contextSequence,&sx::Scope::actualBody,&sx::Scope::realLocal,&sx::Scope::selected}){auto x=s;++(x.*member);C(!sx::Suppress(x),"different scope/local body refuses");}
 for(auto member:{&sx::Scope::caller,&sx::Scope::currentPlayer,&sx::Scope::currentBlock,&sx::Scope::currentState,&sx::Scope::currentCS,&sx::Scope::currentEntry,&sx::Scope::argumentState}){auto x=s;++(x.*member);C(!sx::Suppress(x),"caller/typed tuple replacement refuses");}
 {auto x=s;x.sequence=x.contextSequence=8;C(!sx::Suppress(x),"even window cannot suppress stock path");}
 {auto x=s;x.currentBirth=10;C(!sx::Suppress(x),"new constructor birth cannot inherit skip");}
 {auto x=s;x.actualBody=0;C(!sx::Suppress(x),"real local body0 retains stock update");}
 Reader r;std::uintptr_t mismatch=0;C(sx::CodeGate(r,r.image,0xa01d000,mismatch),"full native bytes and getter slots");
 for(const auto& site:mgscoop::native_pose_selected_export_contract::sites){r.corrupt=site.rva+site.size-1;C(!sx::CodeGate(r,r.image,0xa01d000,mismatch)&&mismatch==site.rva,"late function byte changes refuse installation");}r.corrupt=0;
 r.wrongSlot=true;C(!sx::CodeGate(r,r.image,0xa01d000,mismatch),"getter slot replacement refuses installation");
 std::cout<<n<<" checks; "<<f<<" failures\n";return f?1:0;}
