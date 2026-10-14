#include "vehicle_mirror_hook_read.hpp"
#include <cstdio>
#include <map>
#include <vector>
namespace hooks=mgscoop::vehicle_mirror_hook_read;
namespace protect=mgscoop::vehicle_native_protection;
using Word=std::uintptr_t;
unsigned checks=0,failures=0;
void Check(bool yes,const char* message){++checks;if(!yes){++failures;std::printf("FAIL %s\n",message);}}
struct Memory {
 std::map<Word,std::vector<unsigned char>> segments;unsigned calls=0;Word drift=0;unsigned driftAt=0;
 bool operator()(Word at,void* output,std::size_t size){++calls;
  if(driftAt&&calls==driftAt)segments[drift][0]^=1;
  const auto i=segments.upper_bound(at);if(i==segments.begin())return false;const auto& s=*std::prev(i);
  const auto off=at-s.first;if(off>s.second.size()||size>s.second.size()-off)return false;
  std::memcpy(output,s.second.data()+off,size);return true;
 }
};
constexpr Word image=0x140000000ull;
struct Fixture {
 Memory memory;hooks::Hook h{};
 Fixture(){h.rva=0x124e140;h.detour=0x180008000;h.trampoline=0x140600000;
  auto& head=memory.segments[image+h.rva];head.resize(16,0x90);head[0]=0xe9;
  const Word relay=image+h.rva+0x1000;const auto displacement=static_cast<std::int32_t>(relay-(image+h.rva+5));
  std::memcpy(head.data()+1,&displacement,4);auto& bytes=memory.segments[relay];bytes.resize(14,0);
  bytes[0]=0xff;bytes[1]=0x25;std::memcpy(bytes.data()+6,&h.detour,8);
  memory.segments[h.trampoline].resize(64,0xcc);
 }
 bool Seal(){return hooks::Seal(memory,image,h);}
};
int main(){
 Fixture f;Check(f.Seal()&&f.h.enabled&&f.h.relay==image+f.h.rva+0x1000,"actual E9 entry, absolute relay and trampoline are sealed");
 Check(hooks::Live(f.memory,image,f.h),"sealed actual hook remains current");
 protect::UIEvent9Receipt r{},again{};
 Check(hooks::Receipt(f.memory,image,7,f.h,r)&&r.installationSerial==7&&r.entry==image+0x124e140&&r.detour==f.h.detour&&r.relay==f.h.relay,
  "typed event9 receipt binds actual serial/image/entry/detour/relay");
 Check(hooks::Receipt(f.memory,image,7,f.h,again)&&hooks::Same(r,again),"unchanged two-read event9 receipt");
 auto wrong=again;++wrong.installationSerial;Check(!hooks::Same(r,wrong),"installation incarnation change refuses");
 wrong=again;++wrong.image;Check(!hooks::Same(r,wrong),"image change refuses");
 wrong=again;++wrong.entry;Check(!hooks::Same(r,wrong),"entry change refuses");
 wrong=again;++wrong.detour;Check(!hooks::Same(r,wrong),"detour change refuses");
 wrong=again;++wrong.relay;Check(!hooks::Same(r,wrong),"relay change refuses");
 wrong=again;wrong.entryBytes[5]^=1;Check(!hooks::Same(r,wrong),"installed entry tail drift refuses");
 wrong=again;wrong.relayBytes[13]^=1;Check(!hooks::Same(r,wrong),"absolute target drift refuses");
 Check(!hooks::Receipt(f.memory,image,0,f.h,r)&&!r.installationSerial,"zero installation serial cannot issue a receipt");
 auto other=f.h;other.rva=0x12471b0;Check(!hooks::Receipt(f.memory,image,7,other,r),"other hook cannot masquerade as event9");
 other=f.h;other.enabled=false;Check(!hooks::Live(f.memory,image,other),"configuration cannot manufacture installed hook");
 other=f.h;other.trampoline=0;Check(!hooks::Live(f.memory,image,other),"missing original trampoline refuses");
 {
  auto x=f;x.memory.segments[x.h.trampoline][31]^=1;Check(!hooks::Live(x.memory,image,x.h),"retained original trampoline drift refuses");
 }
 {
  auto x=f;x.memory.segments[image+x.h.rva][12]^=1;Check(!hooks::Live(x.memory,image,x.h),"installed entry ownership includes original tail");
 }
 for(unsigned offset=0;offset<14;++offset){auto x=f;x.memory.segments[x.h.relay][offset]^=1;
  Check(!hooks::Live(x.memory,image,x.h),"every relay opcode, displacement and target byte is owned");}
 {
  Fixture x;x.memory.segments[image+x.h.rva][0]=0xeb;Check(!x.Seal()&&!x.h.enabled,"non-MinHook native entry is never sealed");
 }
 {
  Fixture x;x.memory.segments[image+x.h.rva+0x1000][6]^=1;Check(!x.Seal(),"relay pointing to another wrapper refuses installation");
 }
 {
  auto x=f;x.memory.calls=0;x.memory.drift=x.h.trampoline;x.memory.driftAt=5;
  Check(!hooks::Receipt(x.memory,image,7,x.h,r),"drift between receipt validation reads refuses");
 }
 {
  hooks::Hook entries[1]{f.h};unsigned char original[1][16]{};std::memset(original,0x44,sizeof(original));
  unsigned char output[16]{};std::memset(output,0xaa,sizeof(output));
  Check(hooks::Overlay(f.memory,image,entries,original,image+f.h.rva,output,sizeof(output)),"pure overlay accepts real owned entry");
  Check(output[0]==0x44&&output[4]==0x44&&output[5]==0xaa&&output[15]==0xaa,"only installed five JMP bytes are overlaid");
  std::memset(output,0xaa,sizeof(output));
  Check(hooks::Overlay(f.memory,image,entries,original,image+f.h.rva+3,output,sizeof(output))&&output[0]==0x44&&output[1]==0x44&&output[2]==0xaa,
   "partial-overlap overlay preserves every unrelated byte");
  std::memset(output,0xaa,sizeof(output));
  Check(hooks::Overlay(f.memory,image,entries,original,image+f.h.rva-2,output,sizeof(output))&&output[0]==0xaa&&output[1]==0xaa&&output[2]==0x44&&output[6]==0x44&&output[7]==0xaa,
   "leading overlap preserves original actual prefix and tail");
  auto x=f;x.memory.segments[x.h.trampoline][3]^=1;std::memset(output,0xaa,sizeof(output));
  Check(!hooks::Overlay(x.memory,image,entries,original,image+f.h.rva,output,sizeof(output))&&output[0]==0xaa&&output[4]==0xaa,
   "ownership failure leaves code-view output untouched");
  Check(!hooks::Overlay(f.memory,image,entries,original,UINTPTR_MAX-2,output,sizeof(output)),"overflow code-view range refuses");
 }
 std::printf("Vehicle actual hook receipts: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
