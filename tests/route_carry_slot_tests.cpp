#include "carry_dispatch_lifetime.hpp"
#include "coop_carry_original_view.hpp"
#include <map>
#include <cstdio>
namespace route=mgscoop::carry_dispatch_lifetime;
namespace contract=mgscoop::carry_dispatch_lifetime_contract;
namespace view=mgscoop::coop_carry_original_view;
constexpr std::uintptr_t image=0x140000000,wrapper=0x180002000;
constexpr contract::Slot recipient{0x23b8df8,0x11268b0};
unsigned passed=0,failed=0;
void Check(bool value,const char* name){if(value)++passed;else{++failed;std::printf("FAIL %s\n",name);}}
struct Memory {
 std::map<std::uintptr_t,unsigned char> bytes;
 template<class T>void Put(std::uintptr_t at,T value){PutBytes(at,&value,sizeof(value));}
 void PutBytes(std::uintptr_t at,const void* data,std::size_t size){const auto* p=static_cast<const unsigned char*>(data);for(std::size_t n=0;n<size;++n)bytes[at+n]=p[n];}
 bool operator()(std::uintptr_t at,void* out,std::size_t size)noexcept{auto* p=static_cast<unsigned char*>(out);for(std::size_t n=0;n<size;++n){const auto i=bytes.find(at+n);if(i==bytes.end())return false;p[n]=i->second;}return true;}
};
struct Fixture {
 Memory memory;view::Book book{};unsigned overlays=0;
 Fixture(){view::Bind(book,image,0xa01d000);for(const auto& s:contract::sites)memory.PutBytes(image+s.rva,s.bytes,s.size);
  for(const auto& s:contract::slots)memory.Put(image+s.rva,image+s.target);}
 bool Install(){const auto at=image+recipient.rva,original=image+recipient.target;
  if(!view::Capture(memory,book,view::recipient0,view::Kind::pointer,at,wrapper,&original,8))return false;
  memory.Put(at,wrapper);return view::Activated(memory,book,view::recipient0,&wrapper);}
 bool Overlay(std::uintptr_t at,void* out,std::size_t size)noexcept{++overlays;return view::Overlay(memory,book,at,out,size);}
 bool Live(){auto overlay=[&](std::uintptr_t at,void* out,std::size_t size)noexcept{return Overlay(at,out,size);};return route::SlotLive(memory,overlay,image,recipient);}
 bool All(){auto overlay=[&](std::uintptr_t at,void* out,std::size_t size)noexcept{return Overlay(at,out,size);};for(const auto& s:contract::slots)if(!route::SlotLive(memory,overlay,image,s))return false;return true;}
};
int main(){
 Fixture native;Check(native.Live()&&native.overlays==0,"native recipient needs no invented carry ownership");Check(native.All()&&native.overlays==0,"nine original slots remain accepted without carry");
 Fixture carried;Check(carried.Install()&&carried.book.patches[view::recipient0].serial>0,"actual successful captured pointer install produces positive receipt");
 Check(carried.All()&&carried.overlays==1,"only exact owned recipient composes among all nine slots");
 std::uintptr_t mismatch=0;
 Check(!route::CodeGate(carried.memory,image,0xa01d000,mismatch)&&mismatch==recipient.rva,"raw code gate still sees actual pointer wrapper");
 auto original=[&](std::uintptr_t at,void* out,std::size_t size)noexcept{return carried.memory(at,out,size)&&carried.Overlay(at,out,size);};
 Check(route::CodeGate(original,image,0xa01d000,mismatch),"existing composed Book reader qualifies whole route CodeGate");
 for(unsigned fault=0;fault<12;++fault){auto x=carried;auto& p=x.book.patches[view::recipient0];
  if(fault==0)p.active=false;if(fault==1)p.sealed=false;if(fault==2)p.captured=false;
  if(fault==3)p.serial=0;if(fault==4)p.serial=x.book.serial+1;if(fault==5)x.book.serial=0;
  if(fault==6)p.detour++;if(fault==7)p.width=7;if(fault==8)p.original[0]^=1;
  if(fault==9)p.capturedExpected[1]^=1;if(fault==10)p.installed[7]^=1;if(fault==11)p.site+=8;
  Check(!x.Live(),"partial retired malformed owner or serial receipt cannot bless wrapper");}
 auto unknown=carried;unknown.memory.Put(image+recipient.rva,wrapper+8);Check(!unknown.Live(),"foreign replacement is not carry-owned");
 auto absent=Fixture{};absent.memory.Put(image+recipient.rva,wrapper);Check(!absent.Live(),"same wrapper address without real receipt is refused");
 auto fail=carried;auto refuses=[](std::uintptr_t,void*,std::size_t)noexcept{return false;};
 Check(!route::SlotLive(fail.memory,refuses,image,recipient),"failed trusted leaf cannot admit wrapper");
 auto drift=carried;auto changedOverlay=[&](std::uintptr_t at,void* out,std::size_t size)noexcept{
  if(!drift.Overlay(at,out,size))return false;drift.memory.Put(at,wrapper+0x100);return true;};
 Check(!route::SlotLive(drift.memory,changedOverlay,image,recipient),"raw pointer replacement after locked Book overlay is refused");
 auto retired=carried;auto retireOverlay=[&](std::uintptr_t at,void* out,std::size_t size)noexcept{
  if(!retired.Overlay(at,out,size))return false;retired.memory.Put(at,image+recipient.target);view::Restored(retired.memory,retired.book,view::recipient0);return true;};
 Check(!route::SlotLive(retired.memory,retireOverlay,image,recipient),"native restoration during current caller proof refuses this snapshot");
 Check(retired.Live()&&!retired.book.patches[view::recipient0].active,"next stable native snapshot accepts correctly restored inactive receipt");
 auto badReads=carried;unsigned readCount=0;auto failsLate=[&](std::uintptr_t at,void* out,std::size_t size)noexcept{
  if(at==image+recipient.rva&&++readCount==2)return false;return badReads.memory(at,out,size);};
 auto leaf=[&](std::uintptr_t at,void* out,std::size_t size)noexcept{return badReads.Overlay(at,out,size);};
 Check(!route::SlotLive(failsLate,leaf,image,recipient),"second raw observation must actually succeed");
 auto nativeDrift=Fixture{};readCount=0;auto changesNative=[&](std::uintptr_t at,void* out,std::size_t size)noexcept{
  if(at==image+recipient.rva&&++readCount==2)nativeDrift.memory.Put(at,wrapper);return nativeDrift.memory(at,out,size);};
 Check(!route::SlotLive(changesNative,refuses,image,recipient),"native first sample changing to wrapper is refused");
 for(const auto& slot:contract::slots){if(slot.rva==recipient.rva)continue;auto x=carried;x.memory.Put(image+slot.rva,wrapper);
  unsigned calls=0;auto forged=[](std::uintptr_t,void*,std::size_t)noexcept{return true;};
  auto counted=[&](std::uintptr_t at,void* out,std::size_t size)noexcept{++calls;return forged(at,out,size);};
  Check(!route::SlotLive(x.memory,counted,image,slot)&&calls==0,"all eight other route slots reject replacement without any overlay call");}
 auto x=carried;const contract::Slot wrong{recipient.rva,recipient.target+1};
 Check(!route::SlotLive(x.memory,refuses,image,wrong),"exact recipient target is mandatory");
 Check(!route::SlotLive(x.memory,refuses,UINTPTR_MAX-8,recipient),"image overflow refused before address arithmetic");
 const contract::Slot outside{0xa01d000-4,recipient.target};Check(!route::SlotLive(x.memory,refuses,image,outside),"truncated slot outside pinned image refused");
 const contract::Slot targetOutside{recipient.rva,0xa01d000};Check(!route::SlotLive(x.memory,refuses,image,targetOutside),"target outside pinned image refused");
 for(unsigned corruption=0;corruption<2;++corruption){auto code=carried;const auto& site=contract::sites[0];code.memory.bytes[image+site.rva+(corruption?site.size-1:0)]^=1;
  auto read=[&](std::uintptr_t at,void* out,std::size_t size)noexcept{return code.memory(at,out,size)&&code.Overlay(at,out,size);};
  Check(!route::CodeGate(read,image,0xa01d000,mismatch)&&mismatch==site.rva,"carry slot composition never hides route native code corruption");}
 std::printf("%u passed, %u failed\n",passed,failed);return failed?1:0;
}
