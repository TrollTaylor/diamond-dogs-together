#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
#include <limits>
#include "sleep_body0_transition_contract.hpp"
#include "sleep_body0_transition_read.hpp"
namespace gate=mgscoop::sleep_body0_transition_contract;
namespace read=mgscoop::sleep_body0_transition_read;
struct Memory {
 std::map<std::uintptr_t,std::vector<unsigned char>> cells;
 std::uintptr_t denied=0;
 void Add(std::uintptr_t p,std::size_t n){cells[p]=std::vector<unsigned char>(n);}
 bool operator()(std::uintptr_t p,void* dst,std::size_t n) noexcept {if(denied>=p&&denied-p<n)return false;auto i=cells.upper_bound(p);if(i==cells.begin())return false;--i;const auto offset=p-i->first;if(offset>i->second.size()||n>i->second.size()-offset)return false;std::memcpy(dst,i->second.data()+offset,n);return true;}
 void Bytes(std::uintptr_t p,const void* src,std::size_t n){auto i=cells.upper_bound(p);if(i==cells.begin())std::abort();--i;const auto off=p-i->first;if(off>i->second.size()||n>i->second.size()-off)std::abort();std::memcpy(i->second.data()+off,src,n);}
 template<class T>void Put(std::uintptr_t p,T v){Bytes(p,&v,sizeof(v));}
};
unsigned checks=0;
void Check(bool v,const char* label){++checks;if(!v){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
constexpr std::uintptr_t image=0x140000000ull,P=0x100000,entry=0x200000,U=0x300020,CAM=0x400020,PP=0x500000,SRC=0x600000,SEL=0x700000,REQ=0x800000,REC=0x900000;
Memory Fixture(){Memory r;r.Add(P,0xe0);r.Add(entry,0xc0);r.Add(U-0x20,0x500);r.Add(CAM-0x20,0x500);r.Add(PP,0x500);r.Add(SRC,0x80);r.Add(SEL,0x3ea0);r.Add(REQ,0x18);r.Add(REC,0x1810*32);
 r.Put(P+0xd8,U);r.Put(U,image+0x23af700);r.Put(U-0x20,image+0x23af6d0);r.Put(U+0x28,P);
 r.Put(P+0xb8,CAM);r.Put(CAM,image+0x23b8250);r.Put(CAM-0x20,image+0x23b8218);r.Put(CAM+0x348,P);
 r.Put(P+0xb0,PP);r.Put(PP,image+0x23c8d40);r.Put(PP+0x280,P);r.Put(PP+8,SRC);r.Put(PP+0x28,0xffffffffu);r.Put(SRC+0x48,0xffffffffu);
 r.Put(P+0xd0,SEL);r.Put(SEL,image+0x22e8f00);r.Put(SEL+0x10,P);
 r.Put(entry+0x98,REQ);r.Put(entry+0xb0,1u);r.Put(REQ+8,std::uint8_t(0));r.Put(REQ+0xc,0x28u);r.Put(REQ+0x10,REC);return r;}
bool Observe(Memory& r,read::Observation& o,const char** why=nullptr,bool locked=true){return read::ReadLocalOwnedOnce(r,image,P,entry,o,why,locked);}
int main(){
 Memory code;constexpr std::size_t span=0x3000000;for(const auto& s:gate::sites){code.Add(image+s.rva,s.size);code.Bytes(image+s.rva,s.bytes,s.size);}for(const auto& s:gate::slots){code.Add(image+s.vtable_rva+s.offset,8);code.Put(image+s.vtable_rva+s.offset,image+s.target_rva);}std::uintptr_t mismatch=0;
 Check(gate::CodeGate(code,image,span,mismatch)&&!mismatch,"exact retail code/slots");
 for(const auto& s:gate::sites){for(const std::size_t off:{std::size_t(0),s.size/2,s.size-1}){const auto addr=image+s.rva+off;const auto save=s.bytes[off];code.Put(addr,static_cast<unsigned char>(save^1u));Check(!gate::CodeGate(code,image,span,mismatch)&&mismatch==s.rva,"changed fullbody byte refused");code.Put(addr,save);}code.denied=image+s.rva+s.size-1;Check(!gate::CodeGate(code,image,span,mismatch)&&mismatch==s.rva,"unreadable body refused");code.denied=0;}
 for(const auto& s:gate::slots){const auto addr=image+s.vtable_rva+s.offset;code.Put(addr,image+s.target_rva+1);Check(!gate::CodeGate(code,image,span,mismatch)&&mismatch==s.vtable_rva+s.offset,"changed typed slot refused");code.Put(addr,image+s.target_rva);code.denied=addr+7;Check(!gate::CodeGate(code,image,span,mismatch)&&mismatch==s.vtable_rva+s.offset,"unreadable slot refused");code.denied=0;}
 Check(!gate::CodeGate(code,image,0x1000,mismatch),"image truncated");Check(!gate::CodeGate(code,(std::numeric_limits<std::uintptr_t>::max)()-1,span,mismatch),"image wrap refused");
 read::Observation a{},b{};auto r=Fixture();Check(Observe(r,a),"valid typed local0 transition");Check(Observe(r,b)&&read::Same(a,b),"stable typed pair");
 for(const auto addr:{P+0xd8,U,U-0x20,U+0x28,P+0xb8,CAM,CAM-0x20,CAM+0x348,P+0xb0,PP,PP+0x280,PP+8,P+0xd0,SEL,SEL+0x10,entry+0x98,REQ+0x10}){r=Fixture();r.Put(addr,std::uintptr_t(0));Check(!Observe(r,b),"null or wrong typed owner refused");}
 for(const auto addr:{U+0x3c0,U+0x3c4,U+0x3f0,U+0x3f3,CAM+0x32c,PP+0x28,SRC+0x48,PP+0x460,entry+0xb0,REQ+8,REQ+0xc,REQ+0x10,REC+0x27}){r=Fixture();r.denied=addr;Check(!Observe(r,b),"unreadable required typed field refused");}
 r=Fixture();r.Put(PP+0x28,0xfffffffeu);const char* why="";Check(!Observe(r,b,&why)&&std::strcmp(why,"transition_pad")==0,"named-only lock insufficient aggregate");Check(Observe(r,b,nullptr,false),"post-release read can omit aggregate lock without granting controls");
 for(const auto count:{0u,1u,255u}){r=Fixture();r.Put(REQ+8,static_cast<std::uint8_t>(count));r.Put(REQ+0xc,0x28u+24u*count);Check(Observe(r,b),"constructor parameter stride admitted");r.denied=REC+0x28+24*count-1;Check(!Observe(r,b),"last constructor record byte unreadable");}
 for(const auto layers:{0u,33u,0xffffffffu}){r=Fixture();r.Put(entry+0xb0,layers);Check(!Observe(r,b),"request layer count refused");}
 r=Fixture();r.Put(entry+0xb0,32u);Check(Observe(r,b),"bounded request layers");r=Fixture();r.Put(REQ+0xc,0x30u);Check(!Observe(r,b),"false request schema refused");r=Fixture();r.Put(REQ+0x10,(std::numeric_limits<std::uintptr_t>::max)()-7);Check(!Observe(r,b),"record wrap refused");
 r=Fixture();Check(Observe(r,b),"fixture reset");b.ui+=8;Check(!read::Same(a,b),"replaced UI receipt refused");b=a;b.cam+=8;Check(!read::Same(a,b),"replaced camera receipt refused");b=a;b.request+=8;Check(!read::Same(a,b),"replaced request header refused");b=a;b.records+=8;Check(!read::Same(a,b),"replaced record allocation refused");b=a;++b.parameterCount;Check(!read::Same(a,b),"request schema drift refused");b=a;b.aggregateButtons=0;Check(!read::Same(a,b),"mask drift refused");
 std::printf("PASS %u exact-code and typed transition checks\n",checks);return 0;
}
