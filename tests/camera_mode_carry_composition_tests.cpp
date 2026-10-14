// Reuse the exact native-original camera fixture and its negative cases.
#include "coop_carry_presentation_setter.hpp"
#define main native_camera_fixture_cases
#include "camera_mode_window_tests.cpp"
#undef main
namespace carrytest {
namespace view=mgscoop::coop_carry_original_view;
namespace mode=mgscoop::camera_mode_setter;
namespace proof=mgscoop::coop_carry_presentation_setter;
constexpr std::uintptr_t slot=image+0x23c75f8,otherSlot=image+0x23c7498,native=image+0x11f6520,wrapper=0x50000;
Memory* memory=nullptr;view::Book* book=nullptr;
bool mutateAfterProof=false;
bool Provider(std::uintptr_t base,std::uintptr_t at,std::uintptr_t actual,mode::Receipt& out) noexcept {
 if(!memory||!book)return false;
 const bool ok=proof::Read(*memory,*book,base,at,actual,wrapper,native,out);
 if(ok&&mutateAfterProof)memory->Put(slot,wrapper+8);return ok;
}
struct Fixture {
 Memory mem=::Fixture();view::Book receipts{};
 Fixture(){view::Bind(receipts,image,0xa01d000);mem.Put(native,std::uint32_t(0xc36d5188));mem.Put(otherSlot,native);memory=&mem;book=&receipts;mutateAfterProof=false;}
 bool Install(unsigned which=view::mode0){const auto at=which==view::mode0?slot:otherSlot;
  const bool captured=view::Capture(mem,receipts,which,view::Kind::pointer,at,wrapper,&native,8);
  if(!captured)return false;mem.Put(at,wrapper);return view::Activated(mem,receipts,which,&wrapper);
 }
 Sample Scope(){Sample s;s.base.presentationSetter=&Provider;s.changed=s.base;return s;}
};
void Run(){
 const auto start=passed;
 {Fixture f;for(const auto& site:mgscoop::camera_mode_window_contract::sites)
    for(std::size_t k=0;k<site.size;++k)f.mem.bytes[image+site.rva+k]=site.bytes[k];
  for(const auto& s:mgscoop::camera_mode_window_contract::slots)f.mem.Put(image+s.vtable_rva+s.offset,image+s.target_rva);
  std::uintptr_t mismatch=0;Check(CodeGate(f.mem,image,0xa01d000,mismatch),"native original install gate preserved");
  Check(f.Install(),"carry already installed before passive camera gate fixture");
  Check(!CodeGate(f.mem,image,0xa01d000,mismatch)&&mismatch==0x23c75f8,"camera install gate without provider cannot borrow wrapped slot");
  Check(CodeGate(f.mem,image,0xa01d000,mismatch,&Provider),"late passive core installation accepts same typed owned carry slot");
  f.mem.Put(image+0x23c7500+8,wrapper);Check(!CodeGate(f.mem,image,0xa01d000,mismatch,&Provider)&&mismatch==0x23c7508,
    "carry provider never relaxes another virtual target");
 }
 for(unsigned which:{view::mode0,view::mode1}){
  Fixture f;Check(f.Install(which),"actual owned mode slot fixture installation");mode::Receipt r{};
  const auto at=which==view::mode0?slot:otherSlot;
  Check(proof::Read(f.mem,f.receipts,image,at,wrapper,wrapper,native,r)&&r.serial&&r.actual==wrapper&&r.original==native&&
   r.kind==mode::Kind::ownedCarry,"both exact installed presentation slots qualify native ABI without calling it");
 }
 {Fixture f;Check(f.Install(),"wrapped C0 installation");auto s=f.Scope();Token t;Reply out;Writer w{f.mem};
  Check(CaptureFixture(f.mem,s,t,out)&&t.Armed(),"exact owned carry wrapper accepted by snapshot");
  f.mem.Put<std::uint8_t>(Byte,5);Check(Restore(f.mem,w,s,t,out)&&w.calls==1&&f.mem.Value(Byte)==2&&f.mem.activeReads==0,
    "same owned pointer/serial restores byte by CAS without bypassing or calling wrapper");}
 {Fixture f;Check(f.Install(),"no provider negative installation");Sample s;Token t;Reply out;
  Check(!Capture(f.mem,s,t,out)&&!t.Armed()&&!std::strcmp(out.reason,"c0_setter_slot"),"configuration/native type alone cannot accept wrapped slot");}
 {Fixture f;auto s=f.Scope();Token t;Reply out;Writer w{f.mem};
  Check(CaptureFixture(f.mem,s,t,out),"native original remains accepted without any installed carry receipt");
  f.mem.Put<std::uint8_t>(Byte,5);Check(Restore(f.mem,w,s,t,out),"native camera-only cleanup remains accepted");}
 for(unsigned mutation=0;mutation<12;++mutation){Fixture f;Check(f.Install(),"owned receipt negative fixture");
  auto& p=f.receipts.patches[view::mode0];
  switch(mutation){case 0:p.active=false;break;case 1:p.sealed=false;break;case 2:p.captured=false;break;
   case 3:p.serial=0;break;case 4:p.serial=f.receipts.serial+1;break;case 5:p.kind=view::Kind::detour;break;
   case 6:p.width=5;break;case 7:p.site=otherSlot;break;case 8:p.detour=wrapper+8;break;
   case 9:p.original[0]^=1;break;case 10:p.capturedExpected[0]^=1;break;case 11:f.mem.Put(slot,wrapper+8);break;}
  mode::Receipt r{};Check(!proof::Read(f.mem,f.receipts,image,slot,wrapper,wrapper,native,r)&&!r.serial,
   "unowned/partial/changed/foreign presentation receipt refuses");
 }
 for(unsigned k=0;k<4;++k){Fixture f;Check(f.Install(),"native instruction negative fixture");f.mem.bytes[native+k]^=1;
  auto s=f.Scope();Token t;Reply out;Check(!Capture(f.mem,s,t,out)&&!t.Armed(),"every native target instruction byte is independently rechecked");}
 {Fixture f;Check(f.Install(),"unknown wrapper negative fixture");mode::Receipt r{};
  Check(!proof::Read(f.mem,f.receipts,image,slot,wrapper,wrapper+8,native,r),"another compiled wrapper cannot borrow carry slot");
  Check(!proof::Read(f.mem,f.receipts,image,slot,wrapper,wrapper,native+4,r),"another original call target cannot borrow carry slot");
  Check(!proof::Read(f.mem,f.receipts,image,slot+8,wrapper,wrapper,native,r),"unrelated slot cannot borrow carry slot");}
 {Fixture f;Check(f.Install(),"provider return race fixture");auto s=f.Scope();Token t;Reply out;mutateAfterProof=true;
  Check(!Capture(f.mem,s,t,out)&&!t.Armed()&&!std::strcmp(out.reason,"c0_setter_changed"),"raw slot race after provider result refuses capture");}
 {Fixture f;Check(f.Install(),"serial replacement fixture");auto s=f.Scope();Token t;Reply out;Writer w{f.mem};
  Check(CaptureFixture(f.mem,s,t,out),"serial replacement snapshot captured");f.mem.Put<std::uint8_t>(Byte,5);
  f.receipts.patches[view::mode0].serial=++f.receipts.serial;
  Check(!Restore(f.mem,w,s,t,out)&&!t.Armed()&&w.calls==0&&!std::strcmp(out.reason,"restore_owner_changed"),
    "same pointer with different installed serial cannot restore older snapshot");}
 {Fixture f;Check(f.Install(),"retired wrapper fixture");auto s=f.Scope();Token t;Reply out;Writer w{f.mem};
  Check(CaptureFixture(f.mem,s,t,out),"retirement snapshot captured");f.mem.Put<std::uint8_t>(Byte,5);f.mem.Put(slot,native);
  Check(view::Restored(f.mem,f.receipts,view::mode0),"actual native slot restoration retires receipt");
  Check(!Restore(f.mem,w,s,t,out)&&w.calls==0&&!t.Armed(),"retired carry wrapper cannot become a forced-original cleanup permission");}
 {Fixture f;auto s=f.Scope();Token t;Reply out;Writer w{f.mem};Check(CaptureFixture(f.mem,s,t,out),"pre-install snapshot captured");
  Check(f.Install(),"new wrapper installed after native snapshot");f.mem.Put<std::uint8_t>(Byte,5);
  Check(!Restore(f.mem,w,s,t,out)&&w.calls==0,"native to wrapped slot transition refuses stale camera snapshot");}
 {Fixture f;Check(f.Install(),"provider lifetime fixture");auto s=f.Scope();Token t;Reply out;Writer w{f.mem};
  Check(CaptureFixture(f.mem,s,t,out),"provider bound snapshot captured");s.base.presentationSetter=nullptr;f.mem.Put<std::uint8_t>(Byte,5);
  Check(!Restore(f.mem,w,s,t,out)&&w.calls==0&&!std::strcmp(out.reason,"restore_scope_changed"),"provider replacement cannot reopen old scope");}
 for(unsigned cleanup=0;cleanup<2;++cleanup){Fixture f;Check(f.Install(),"owned unwind fixture");auto s=f.Scope();Token t;Reply out;Writer w{f.mem};
  Check(CaptureFixture(f.mem,s,t,out),"owned unwind snapshot captured");f.mem.Put<std::uint8_t>(Byte,5);
  if(cleanup)s.base.stopping=true;else s.base.faulted=true;
  Check(Restore(f.mem,w,s,t,out)&&w.calls==1,"same owned wrapped setter permits exact fault/stopping cleanup");}
 {Fixture f;Check(f.Install(),"raw proof race fixture");unsigned calls=0;
  auto racing=[&](std::uintptr_t at,void* dst,std::size_t n) noexcept {
   const bool ok=f.mem(at,dst,n);if(at==native&&++calls==1)f.mem.Put(slot,wrapper+8);return ok;
  };mode::Receipt r{};Check(!proof::Read(racing,f.receipts,image,slot,wrapper,wrapper,native,r)&&!r.serial,
   "slot race between native instruction/owned receipt reads never publishes typed result");}
 std::printf("%u additional C0/carry composition checks\n",passed-start);
}
}
int main(){native_camera_fixture_cases();carrytest::Run();std::printf("combined %u passed, %u failed; no game\n",passed,failed);return failed?1:0;}
