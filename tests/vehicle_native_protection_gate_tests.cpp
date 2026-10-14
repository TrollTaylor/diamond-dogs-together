#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>
#include "vehicle_native_protection_gate_read.hpp"
namespace c=mgscoop::vehicle_native_protection_contract;
namespace g=mgscoop::vehicle_native_protection_gate_read;
unsigned checks=0;void Check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL %u: %s\n",checks,why);std::exit(1);}}
constexpr std::uintptr_t base=0x140000000ull;
struct Span {std::uintptr_t at;std::vector<unsigned char> bytes;};
struct Memory {
 std::vector<Span> spans;std::uintptr_t fault=0;unsigned calls=0;std::size_t largest=0;
 bool operator()(std::uintptr_t at,void* dst,std::size_t size) noexcept {
  ++calls;if(size>largest)largest=size;if(fault&&at<=fault&&fault-at<size)return false;
  for(const auto& s:spans)if(at>=s.at&&at-s.at<=s.bytes.size()&&size<=s.bytes.size()-(at-s.at)){
   std::memcpy(dst,s.bytes.data()+at-s.at,size);return true;}return false;
 }
};
Memory Code(){Memory m;for(const auto& s:c::sites)m.spans.push_back({base+s.rva,std::vector<unsigned char>(s.bytes,s.bytes+s.size)});return m;}
Memory Slots(){Memory m;for(const auto& s:c::slots){const auto value=base+s.target;Span span{base+s.rva,std::vector<unsigned char>(8)};
 std::memcpy(span.bytes.data(),&value,8);m.spans.push_back(span);}return m;}
int main(){auto code=Code(),slots=Slots();
 Check(g::Gate(code,slots,base,0xa01d000),"all complete code/data pins and raw slots pass");
 const auto spans=sizeof(c::sites)/sizeof(c::sites[0]);const auto rawSlots=sizeof(c::slots)/sizeof(c::slots[0]);
 Check(code.calls==spans&&slots.calls==rawSlots&&code.largest==3563,"one full bounded read per current pinned site; separate raw slot reader");
 std::size_t previousCalls=0;for(const auto& s:c::sites)previousCalls+=(s.size+255)/256;
 std::printf("original-view reads per Gate: old=%zu new=%u, full bytes unchanged\n",previousCalls,code.calls);
 for(std::size_t k=0;k<spans;++k){for(const auto offset:{std::size_t(0),code.spans[k].bytes.size()/2,code.spans[k].bytes.size()-1}){
  auto changed=Code();changed.spans[k].bytes[offset]^=1;auto raw=Slots();
  Check(!g::Gate(changed,raw,base,0xa01d000),"mutation in beginning/middle/end of every full pinned span refuses");}}
 for(std::size_t k=0;k<spans;++k){auto broken=Code();broken.fault=broken.spans[k].at;auto raw=Slots();
  Check(!g::Gate(broken,raw,base,0xa01d000),"unreadable full pinned span refuses");}
 for(std::size_t k=0;k<rawSlots;++k){auto actual=Slots();actual.spans[k].bytes[0]^=1;auto original=Code();
  Check(!g::Gate(original,actual,base,0xa01d000),"raw slot mutation cannot be accepted through original overlay reader");
  actual=Slots();actual.fault=actual.spans[k].at;original=Code();
  Check(!g::Gate(original,actual,base,0xa01d000),"unreadable raw slot refuses");}
 // Each invocation rereads all bytes: no time/scene/serial cache grants a permit.
 code=Code();slots=Slots();Check(g::Gate(code,slots,base,0xa01d000),"first invocation qualifies");
 code.spans.back().bytes.back()^=1;Check(!g::Gate(code,slots,base,0xa01d000),"last pin drift on next invocation refuses without cache reuse");
 code=Code();slots=Slots();Check(!g::Gate(code,slots,base,0x1000),"short image refuses");
 Check(!g::Gate(code,slots,UINTPTR_MAX-10,20),"base extent overflow refuses");
 std::printf("%u complete protection gate checks passed; no game\n",checks);
}
