#ifndef MGS_CARRY_ORIGINAL_VIEW_TEST_HEADER
#define MGS_CARRY_ORIGINAL_VIEW_TEST_HEADER "coop_carry_original_view.hpp"
#endif
#include MGS_CARRY_ORIGINAL_VIEW_TEST_HEADER
#include "coop_carry_original_view_contract.hpp"
#include <map>
#include <cstdio>
namespace v=mgscoop::coop_carry_original_view;
unsigned passed=0,failed=0;
void Check(bool ok,const char* name){if(ok)++passed;else{++failed;std::printf("FAIL %s\n",name);}}
struct Memory {
 std::map<std::uintptr_t,unsigned char> bytes;
 template<class T>void Put(std::uintptr_t at,T value){const auto* p=reinterpret_cast<const unsigned char*>(&value);for(std::size_t j=0;j<sizeof(value);++j)bytes[at+j]=p[j];}
 void PutBytes(std::uintptr_t at,const void* input,std::size_t size){const auto* p=static_cast<const unsigned char*>(input);for(std::size_t j=0;j<size;++j)bytes[at+j]=p[j];}
 bool operator()(std::uintptr_t at,void* output,std::size_t size) noexcept {auto* p=static_cast<unsigned char*>(output);for(std::size_t j=0;j<size;++j){const auto x=bytes.find(at+j);if(x==bytes.end())return false;p[j]=x->second;}return true;}
};
struct Fixture {
 static constexpr std::uintptr_t image=0x140000000;
 Memory memory{};v::Book book{};
 Fixture(){v::Bind(book,image,0xa01d000);}
 bool Detour(unsigned index){const auto& head=mgscoop::coop_carry_original_view_contract::heads[index];
  const auto at=image+head.rva,detour=std::uintptr_t(0x180100000+index*0x1000),trampoline=std::uintptr_t(0x140010000+index*0x1000),relay=trampoline+32;
  memory.PutBytes(at,head.bytes,16);
  if(!v::Capture(memory,book,index,v::Kind::detour,at,detour,head.bytes,5))return false;
  auto& p=book.patches[index];p.created=true;p.trampoline=trampoline;
  unsigned char code[64]{};for(unsigned j=0;j<64;++j)code[j]=static_cast<unsigned char>(j+index);
  code[32]=0xff;code[33]=0x25;code[34]=code[35]=code[36]=code[37]=0;std::memcpy(code+38,&detour,8);
  memory.PutBytes(trampoline,code,64);
  unsigned char jump[5]={0xe9};const auto displacement=static_cast<std::int32_t>(relay-(at+5));std::memcpy(jump+1,&displacement,4);memory.PutBytes(at,jump,5);
  return v::Activated(memory,book,index);
 }
 bool Slot(unsigned index,std::uintptr_t at){const std::uintptr_t original=image+0x112fee0,replacement=0x180200000+index*0x1000;
  memory.Put(at,original);if(!v::Capture(memory,book,index,v::Kind::pointer,at,replacement,&original,8))return false;
  memory.Put(at,replacement);return v::Activated(memory,book,index,&replacement);
 }
 bool Wake(){const auto at=image+0x1127372;unsigned char before[6]={0x0f,0x85,0xad,0x03,0,0},after[6]={0xe9,0xae,0x03,0,0,0x90};
  memory.PutBytes(at,before,6);if(!v::Capture(memory,book,v::autoWake,v::Kind::bytes,at,0,before,6))return false;
  memory.PutBytes(at,after,6);return v::Activated(memory,book,v::autoWake,after);
 }
 bool View(std::uintptr_t at,void* bytes,std::size_t size){return memory(at,bytes,size)&&v::Overlay(memory,book,at,bytes,size);}
};
int main(){
 for(unsigned i=0;i<8;++i){Fixture f;Check(f.Detour(i),"actual pinned prehead plus installed relay/trampoline seal");
  const auto& p=f.book.patches[i];unsigned char out[16]{};
  Check(p.serial>0&&p.active&&p.sealed&&v::Live(f.memory,f.book,p),"positive serial owns live actual entry relay and trampoline");
  Check(f.View(p.site,out,16)&&!std::memcmp(out,p.original,16),"whole pinned head overlays actual captured bytes");
  for(unsigned low=0;low<5;++low)for(unsigned width=1;width<=5-low;++width){unsigned char slice[5]{};
   Check(f.View(p.site+low,slice,width)&&!std::memcmp(slice,p.original+low,width),"all partial head intersections normalize captured ownership");}
  auto raw=f;raw.book.patches[i].active=false;Check(raw.View(p.site,out,16)&&!std::memcmp(out,p.installed,16),"inactive receipt never supplies retail original bytes");
  for(unsigned kind=0;kind<9;++kind){auto broken=f;auto& x=broken.book.patches[i];
   if(kind==0)broken.memory.bytes[x.site]^=1;
   if(kind==1)broken.memory.bytes[x.site+15]^=1;
   if(kind==2)broken.memory.bytes[x.relay+13]^=1;
   if(kind==3)broken.memory.bytes[x.trampoline+63]^=1;
   if(kind==4)x.serial=0;
   if(kind==5)x.serial=broken.book.serial+1;
   if(kind==6)x.trampoline+=64;
   if(kind==7)x.detour+=1;
   if(kind==8)x.original[1]^=1;
   unsigned char actual[16]{},unchanged[16]{};broken.memory(x.site,actual,16);std::memcpy(unchanged,actual,16);
   Check(!v::Overlay(broken.memory,broken.book,x.site,actual,16)&&!std::memcmp(actual,unchanged,16),"foreign entry relay trampoline serial or owner refuses without buffer mutation");
  }
  auto race=f;struct Race {Memory& memory;std::uintptr_t at;unsigned hits=0;bool operator()(std::uintptr_t p,void* out,std::size_t size) noexcept {
    if(p==at&&size==16&&++hits==2)memory.bytes[p+4]^=1;return memory(p,out,size);
   }} reader{race.memory,p.site};race.memory(p.site,out,16);unsigned char keep[16]{};std::memcpy(keep,out,16);
  Check(!v::Overlay(reader,race.book,p.site,out,16)&&!std::memcmp(out,keep,16),"racing foreign writer on second proof leaves input buffer untouched");
  auto restored=f;restored.memory.PutBytes(p.site,p.original,16);Check(v::Restored(restored.memory,restored.book,i)&&!restored.book.patches[i].active&&restored.book.patches[i].trampoline==p.trampoline&&restored.book.patches[i].serial==p.serial,"verified disable retains callback address and receipt while deactivating only restored bytes");
 }
 for(unsigned i=v::provider;i<=v::search;++i){Fixture f;const auto at=Fixture::image+0x23b9000+i*8;Check(f.Slot(i,at),"actual vtable CAS receipt sealed");
  const auto& p=f.book.patches[i];std::uintptr_t original=0;Check(f.View(at,&original,8)&&!std::memcmp(&original,p.original,8),"actual captured original vtable value overlays");
  auto changedOwner=f;changedOwner.book.patches[i].detour++;Check(!changedOwner.View(at,&original,8),"actual installed vtable target must remain the recorded owner");
  auto changedOriginal=f;changedOriginal.book.patches[i].original[7]^=1;Check(!changedOriginal.View(at,&original,8),"captured original vtable data remains bound to its actual prepatch observation");
  f.memory.Put<std::uintptr_t>(at,0x180999999);unsigned char out[8]{},keep[8]{};f.memory(at,out,8);std::memcpy(keep,out,8);
  Check(!v::Overlay(f.memory,f.book,at,out,8)&&!std::memcmp(out,keep,8),"foreign vtable replacement refused without original substitution");
 }
 Fixture wake;Check(wake.Wake(),"exact actual before/after six-byte auto-wake receipt");unsigned char out[16]{};const auto& w=wake.book.patches[v::autoWake];
 Check(wake.View(w.site,out,6)&&!std::memcmp(out,w.original,6),"only owned auto-wake bytes normalize");wake.memory.bytes[w.site+5]^=1;
 Check(!wake.View(w.site,out,6),"auto-wake mutation refuses");
 Fixture partial;Check(partial.Detour(0)&&partial.Slot(v::init0,Fixture::image+0x23b9b50),"partial module install retains each owned successful entry");
 const auto& d=partial.book.patches[0];Check(partial.View(d.site,out,16)&&!std::memcmp(out,d.original,16),"overall installed=false is not used to discard live partial callback receipt");
 auto unqualified=partial;unqualified.book.patches[0].sealed=false;Check(!unqualified.View(d.site,out,16),"successful enable with unqualified seal fails closed");
 auto reused=partial;Check(!v::Capture(reused.memory,reused.book,0,v::Kind::detour,d.site,d.detour,d.original,5),"live receipt cannot be recaptured or replaced");
 Fixture noOwnership;const auto& head=mgscoop::coop_carry_original_view_contract::heads[0];const auto site=Fixture::image+head.rva;
 noOwnership.memory.PutBytes(site,head.bytes,16);noOwnership.memory.bytes[site]=0xe9;
 Check(!v::Capture(noOwnership.memory,noOwnership.book,0,v::Kind::detour,site,0x180100000,head.bytes,5),"arbitrary E9 before creation is not an original-code proof");
 auto pinned=noOwnership;unsigned char keep[16]{};pinned.memory(site,out,16);std::memcpy(keep,out,16);
 Check(v::Overlay(pinned.memory,pinned.book,site,out,16)&&!std::memcmp(out,keep,16),"unowned foreign detour stays visible to exact caller fingerprint");
 Fixture empty;Check(!v::Bind(empty.book,Fixture::image+0x1000,0xa01d000),"partial receipt cannot be rebound to another image");
 Check(!v::Overlay(empty.memory,empty.book,UINTPTR_MAX-2,out,4),"overflow address refused");
 Check(!v::Overlay(empty.memory,empty.book,site,nullptr,4),"null output refused");
 Check(!v::Overlay(empty.memory,empty.book,site,out,0),"empty output refused");
 auto serial=partial;serial.book.serial=UINT64_MAX;Check(!v::Activated(serial.memory,serial.book,v::autoWake),"installation serial saturation never wraps to positive authority");
 // Even an unrelated failed optional hook does not invent bytes at this
 // layer's caller range; its own changed head remains visible when queried.
 auto other=partial;other.book.patches[1].active=true;other.book.patches[1].site=site+0x1000;other.book.patches[1].width=5;
 Check(other.View(d.site,out,16),"nonoverlapping unqualified optional receipt does not mutate an unrelated native span");
 std::printf("%u passed, %u failed\n",passed,failed);return failed?1:0;
}
