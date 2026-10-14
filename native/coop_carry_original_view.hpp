#pragma once
// Pure proof/overlay of this module's actually captured installed patches.
// No engine calls, native mutation, hook API, retail substitution or authority.
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace mgscoop::coop_carry_original_view {
enum class Kind : std::uint8_t { detour, pointer, bytes };
enum Index : unsigned {producer,horseFind,horseGeometry,carryPoint,raycast,predicate,loadPredicate,loadFind,
 provider,recipient0,recipient1,dispatch,init0,init1,mode0,mode1,search,autoWake,count};
struct Patch {
 Kind kind=Kind::detour;std::uintptr_t site=0,detour=0,trampoline=0,relay=0;
 std::uint64_t serial=0;unsigned char original[16]{},capturedExpected[16]{},installed[16]{},relayBytes[14]{},trampolineBytes[64]{};
 std::uint8_t width=0;bool captured=false,created=false,active=false,sealed=false;
};
struct Book {std::uintptr_t image=0;std::size_t span=0;std::uint64_t serial=0;Patch patches[count]{};};
inline bool Range(std::uintptr_t at,std::size_t size) noexcept {return at>=0x10000&&size&&at<=UINTPTR_MAX-size;}
inline bool Within(const Book& b,std::uintptr_t at,std::size_t size) noexcept {
 return Range(b.image,b.span)&&at>=b.image&&at-b.image<=b.span&&size<=b.span-(at-b.image)&&Range(at,size);
}
inline bool Bind(Book& b,std::uintptr_t image,std::size_t span) noexcept {
 if(!Range(image,span))return false;
 if(b.image)return b.image==image&&b.span==span;
 b.image=image;b.span=span;return true;
}
template<class Reader>bool Capture(Reader& read,Book& b,unsigned index,Kind kind,std::uintptr_t site,std::uintptr_t replacement,
 const void* expected,std::size_t width) noexcept {
 if(index>=count||b.patches[index].active||b.patches[index].created||!expected||!width||width>16||
    !Within(b,site,width)||((kind==Kind::detour||kind==Kind::pointer)&&!Range(replacement,1)))return false;
 auto& p=b.patches[index];p={};p.kind=kind;p.site=site;p.width=static_cast<std::uint8_t>(width);p.detour=replacement;
 const auto n=kind==Kind::detour?16u:width;
 std::memcpy(p.capturedExpected,expected,n);
 p.captured=Within(b,site,n)&&read(site,p.original,n)&&!std::memcmp(p.original,expected,n);
 return p.captured;
}
template<class Reader>bool Relay(Reader& read,const Patch& p,std::uintptr_t& relay,unsigned char (&bytes)[14]) noexcept {
 unsigned char head[5]{};relay=0;
 if(!Range(p.site,5)||!read(p.site,head,5)||head[0]!=0xe9)return false;
 std::int32_t d=0;std::memcpy(&d,head+1,4);const auto next=p.site+5;
 if(d<0){const auto magnitude=static_cast<std::uint64_t>(-static_cast<std::int64_t>(d));if(next<magnitude)return false;relay=next-static_cast<std::uintptr_t>(magnitude);}
 else {const auto distance=static_cast<std::uint32_t>(d);if(next>UINTPTR_MAX-distance)return false;relay=next+distance;}
 std::uintptr_t target=0;
 return Range(relay,14)&&read(relay,bytes,14)&&bytes[0]==0xff&&bytes[1]==0x25&&!bytes[2]&&!bytes[3]&&!bytes[4]&&!bytes[5]&&
  (std::memcpy(&target,bytes+6,8),target==p.detour);
}
// Called only after this module's real successful MinHook enable or successful
// CAS/memcpy patch. Preserve this record even if the broader install later fails.
template<class Reader>bool Activated(Reader& read,Book& b,unsigned index,const void* replacement=nullptr) noexcept {
 if(index>=count||b.serial==UINT64_MAX)return false;auto& p=b.patches[index];
 if(p.active)return false;p.active=true;p.serial=++b.serial;p.sealed=false;
 if(!p.captured||!p.width||!Within(b,p.site,p.width))return false;
 if(p.kind==Kind::detour){
  if(!p.created||p.width!=5||!Range(p.trampoline,64)||!read(p.trampoline,p.trampolineBytes,64)||!read(p.site,p.installed,16)||
     std::memcmp(p.original+5,p.installed+5,11)||!Relay(read,p,p.relay,p.relayBytes))return false;
 }else if(!replacement||!read(p.site,p.installed,p.width)||std::memcmp(p.installed,replacement,p.width))return false;
 p.sealed=true;return true;
}
template<class Reader>bool Live(Reader& read,const Book& b,const Patch& p) noexcept {
 if(!p.active||!p.sealed||!p.captured||!p.serial||p.serial>b.serial||!p.width||p.width>16||!Within(b,p.site,p.width))return false;
 unsigned char actual[16]{};
 const auto originalWidth=p.kind==Kind::detour?16u:p.width;
 if(std::memcmp(p.original,p.capturedExpected,originalWidth))return false;
 if(p.kind==Kind::detour){unsigned char relay[14]{},trampoline[64]{};std::uintptr_t current=0;
  return p.created&&p.width==5&&Range(p.trampoline,64)&&read(p.trampoline,trampoline,64)&&
   !std::memcmp(trampoline,p.trampolineBytes,64)&&read(p.site,actual,16)&&!std::memcmp(actual,p.installed,16)&&
   Relay(read,p,current,relay)&&current==p.relay&&!std::memcmp(relay,p.relayBytes,14);
 }
 if(p.kind==Kind::pointer){std::uintptr_t installed=0;
  if(p.width!=8)return false;std::memcpy(&installed,p.installed,8);if(installed!=p.detour)return false;
 }else if(p.kind!=Kind::bytes)return false;
 return read(p.site,actual,p.width)&&!std::memcmp(actual,p.installed,p.width);
}
// Actual successful Disable/Remove may retire metadata only after raw entry
// restoration. Callback pointers and all receipt bytes remain process-lifetime.
template<class Reader>bool Restored(Reader& read,Book& b,unsigned index) noexcept {
 if(index>=count)return false;auto& p=b.patches[index];unsigned char current[16]{};
 const auto n=p.kind==Kind::detour?16u:p.width;
 if(!p.captured||!n||!Within(b,p.site,n)||!read(p.site,current,n)||std::memcmp(current,p.original,n))return false;
 p.active=false;return true;
}
inline bool Overlap(std::uintptr_t at,std::size_t size,const Patch& p,std::uintptr_t& low,std::uintptr_t& high) noexcept {
 if(!Range(p.site,p.width))return false;low=at>p.site?at:p.site;high=at+size<p.site+p.width?at+size:p.site+p.width;return low<high;
}
template<class Reader>bool Overlay(Reader& read,const Book& b,std::uintptr_t at,void* output,std::size_t size) noexcept {
 if(!output||!Range(at,size))return false;
 unsigned char copied[count][16]{};std::uintptr_t offsets[count]{};std::uint8_t widths[count]{};
 const auto* current=static_cast<const unsigned char*>(output);
 for(unsigned i=0;i<count;++i){const auto& p=b.patches[i];std::uintptr_t low=0,high=0;
  if(!p.active||!Overlap(at,size,p,low,high))continue;
  if(!Live(read,b,p))return false;
  const auto n=static_cast<std::size_t>(high-low);offsets[i]=low-at;widths[i]=static_cast<std::uint8_t>(n);
  if(std::memcmp(current+low-at,p.installed+low-p.site,n))return false;
  std::memcpy(copied[i],p.original+low-p.site,n);
 }
 // Recheck live owned patches before any caller-buffer changes. A failed
 // observation, including a racing foreign writer, leaves output untouched.
 for(unsigned i=0;i<count;++i)if(widths[i]&&!Live(read,b,b.patches[i]))return false;
 for(unsigned i=0;i<count;++i)if(widths[i])std::memcpy(static_cast<unsigned char*>(output)+offsets[i],copied[i],widths[i]);
 return true;
}
}
