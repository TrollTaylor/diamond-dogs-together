#pragma once
#include <cstddef>
#include <cstdint>
namespace mgscoop::carry_p238_cold_read {
struct Observation {std::uintptr_t pointer=0,entries=0;std::uint32_t vtRva=0,count=0,base=0,reason=0;bool read=false,typed=false,body0=false,body1=false;};
inline bool Range(std::uintptr_t p,std::size_t n)noexcept{return p&&n&&p<=UINTPTR_MAX-n;}
template<class R,class T>bool Read(R& r,std::uintptr_t p,T& x)noexcept{return Range(p,sizeof(x))&&r(p,&x,sizeof(x));}
template<class R>bool Once(R& r,std::uintptr_t image,std::uintptr_t player,std::uintptr_t model0,std::uintptr_t model1,Observation& out)noexcept{
 out={};std::uintptr_t loaded=0,vt=0,entries=0;std::uint32_t count=0,base=0;
 if(!Range(image,0xa01d000)||!Read(r,player+0x238,loaded)){out.reason=1;return false;}
 // Sparse byte-reader fixtures under optimized MSVC must not use a pointer
 // stored directly through the callback into an output aggregate as the next
 // read address. Bind the actual read to an immutable local and publish only
 // after the independent typed reads are complete. Native guards are unchanged.
 const std::uintptr_t pointer=loaded;Observation value{};value.pointer=pointer;value.read=true;
 if(!pointer){value.reason=2;out=value;return true;}value.body0=pointer==model0;value.body1=pointer==model1;
 if(!Read(r,pointer,vt)){value.reason=3;out=value;return true;}if(vt<image||vt-image>=0xa01d000){value.reason=4;out=value;return true;}
 value.vtRva=static_cast<std::uint32_t>(vt-image);if(value.vtRva!=0x22e6070){value.reason=5;out=value;return true;}
 if(!Read(r,pointer+8,count)||!Read(r,pointer+0xc,base)||!Read(r,pointer+0x10,entries)){value.reason=6;out=value;return true;}
 value.count=count;value.base=base;value.entries=entries;
 if(!count||count>2||base>1||count>2-base||!entries||(entries&15u)||!Range(entries,std::size_t(count)*0xc0)){value.reason=7;out=value;return true;}
 value.typed=true;out=value;return true;
}
inline bool Same(const Observation& a,const Observation& b)noexcept{return a.pointer==b.pointer&&a.entries==b.entries&&a.vtRva==b.vtRva&&a.count==b.count&&a.base==b.base&&a.reason==b.reason&&a.read==b.read&&a.typed==b.typed&&a.body0==b.body0&&a.body1==b.body1;}
template<class R>bool Observe(R& r,std::uintptr_t image,std::uintptr_t player,std::uintptr_t model0,std::uintptr_t model1,Observation& out)noexcept{Observation a{},b{};out={};if(!Once(r,image,player,model0,model1,a)||!Once(r,image,player,model0,model1,b)||!Same(a,b)){out.reason=8;return false;}out=b;return true;}
} // namespace mgscoop::carry_p238_cold_read
