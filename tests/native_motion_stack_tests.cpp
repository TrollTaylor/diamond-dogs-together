#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include "native_motion_stack_observer.hpp"
namespace s=mgscoop::native_motion_stack_observer;
namespace q=mgscoop::qualified_sleep_lookup_observer;
unsigned checks=0;
void Check(bool yes,const char* label){++checks;if(!yes){std::fprintf(stderr,"FAIL %u: %s\n",checks,label);std::exit(1);}}
struct Reader {
 std::unordered_map<std::uintptr_t,unsigned char> memory;unsigned reads=0;
 template<class T>void put(std::uintptr_t at,T value){const auto* bytes=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(T);++i)memory[at+i]=bytes[i];}
 bool operator()(std::uintptr_t at,void* out,std::size_t size)noexcept {++reads;SetLastError(0xBAD);auto* bytes=static_cast<unsigned char*>(out);for(std::size_t i=0;i<size;++i){const auto it=memory.find(at+i);if(it==memory.end())return false;bytes[i]=it->second;}return true;}
};
int main(){
 const std::uintptr_t image=0x140000000ull,spec=0x100000;
 q::Event event{};event.path=q::Path::setup;event.returnAddress=image+0x1a6d88d;event.spec=spec;
 Reader read{};read.put(spec+0x200,std::int32_t(1));read.put(spec+0x210,std::uint32_t(0));s::SetupInput result{};
 for(const auto id:q::coveredIds){read.put(spec,id);SetLastError(73);Check(s::SetupStackInput(read,image,event,result),"All four covered full64 phases can capture existing native setup stack");Check(result.observed&&result.fullMotion==id&&result.count==1&&result.layer==0,"Preserve actual full64 ID and input metadata");Check(GetLastError()==73,"Reader/error effects do not alter original LastError");}
 const auto reads=read.reads;
 auto foreign=event;foreign.returnAddress=image+0xaf3037;Check(!s::SetupStackInput(read,image,foreign,result),"AF3037 is nested updater return, not setup lookup caller");Check(read.reads==reads,"Foreign callback return causes zero pointer reads");
 foreign.returnAddress=event.returnAddress+1;Check(!s::SetupStackInput(read,image,foreign,result),"Adjacent setup return refused");Check(read.reads==reads,"Negative caller checked before valid spec scan");
 foreign=event;foreign.path=q::Path::raw;Check(!s::SetupStackInput(read,image,foreign,result),"Raw lookup cannot be mislabeled setup stack");Check(read.reads==reads,"Wrong lookup path causes zero reads");
 foreign=event;foreign.spec=spec+1;Check(!s::SetupStackInput(read,image,foreign,result),"Unaligned spec refused");Check(read.reads==reads,"Unaligned spec causes zero reads");
 foreign=event;foreign.spec=~std::uintptr_t(7);Check(!s::SetupStackInput(read,image,foreign,result),"Overflowing aligned spec refused");Check(read.reads==reads,"Overflowing spec causes zero reads");
 Check(!s::SetupStackInput(read,0,event,result),"Zero image refused");Check(!s::SetupStackInput(read,~std::uintptr_t(0)-0x100,event,result),"Overflow image refused");Check(read.reads==reads,"Invalid image causes zero reads");
 for(const auto count:{-1,0,2,16,17}){read.put(spec+0x200,std::int32_t(count));Check(!s::SetupStackInput(read,image,event,result),"Only exactly one original native spec allowed");Check(!result.observed,"Mixed or invalid native specs grant no stack eligibility");}
 read.put(spec+0x200,std::int32_t(1));
 for(const auto layer:{1u,2u,31u,0xffffffffu}){read.put(spec+0x210,layer);Check(!s::SetupStackInput(read,image,event,result),"Only original layer0 input permitted");}
 read.put(spec+0x210,std::uint32_t(0));
 for(const auto id:q::coveredIds){read.put(spec,id^0x8000000000000000ull);Check(!s::SetupStackInput(read,image,event,result),"High-bit-alias motion IDs are never truncated into coverage");}
 Reader unreadable{};Check(!s::SetupStackInput(unreadable,image,event,result),"Unreadable actual spec safely refused");Check(!result.observed,"Unreadable input grants no observation");
 for(const auto id:q::coveredIds){Check(s::Classify(true,id)==s::PayloadClass::covered,"Every covered payload classified separately");Check(s::Classify(false,id)==s::PayloadClass::unreadable,"Unreadable payload cannot become covered from stale copied value");Check(s::Classify(true,id^0x8000000000000000ull)==s::PayloadClass::uncovered,"Uncovered full64 payload remains distinct from unreadable");}
 read.put(spec,q::coveredIds[0]);event.context=0;Check(s::SetupStackInput(read,image,event,result),"Input/stack schema can be observed with unknown owner; never a native lifetime grant");
 std::printf("Native motion stack observer fixtures passed: %u\n",checks);return 0;
}
