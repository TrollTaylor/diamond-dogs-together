#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "vehicle_native_protection.hpp"
namespace mgscoop::vehicle_mirror_hook_read {
struct Hook {
 std::uintptr_t rva=0,detour=0,trampoline=0,relay=0;
 unsigned char installed[16]{},relayBytes[14]{},trampolineBytes[64]{};
 bool enabled=false;
};
inline bool Range(std::uintptr_t p,std::size_t n) noexcept {
 return p>=0x10000&&n&&p<=UINTPTR_MAX-n;
}
template<class Reader> bool Relay(Reader& read,std::uintptr_t entry,std::uintptr_t detour,
 std::uintptr_t& relay,unsigned char (&bytes)[14]) noexcept {
 relay=0;unsigned char head[5]{};if(!Range(entry,5)||!detour||!read(entry,head,5)||head[0]!=0xe9)return false;
 std::int32_t displacement=0;std::memcpy(&displacement,head+1,4);
 const auto next=entry+5;std::uintptr_t candidate=0;
 if(displacement<0){const auto magnitude=static_cast<std::uint64_t>(-static_cast<std::int64_t>(displacement));
  if(next<magnitude)return false;candidate=next-static_cast<std::uintptr_t>(magnitude);
 }else{const auto distance=static_cast<std::uint32_t>(displacement);if(next>UINTPTR_MAX-distance)return false;candidate=next+distance;}
 if(!Range(candidate,14)||!read(candidate,bytes,14)||bytes[0]!=0xff||bytes[1]!=0x25||
  bytes[2]||bytes[3]||bytes[4]||bytes[5])return false;
 std::uintptr_t target=0;std::memcpy(&target,bytes+6,8);if(target!=detour)return false;
 relay=candidate;return true;
}
template<class Reader> bool Seal(Reader& read,std::uintptr_t image,Hook& h) noexcept {
 if(h.enabled||!h.detour||!Range(h.trampoline,64)||!Range(image+h.rva,16)||
  !read(h.trampoline,h.trampolineBytes,64)||!read(image+h.rva,h.installed,16)||
  !Relay(read,image+h.rva,h.detour,h.relay,h.relayBytes))return false;
 h.enabled=true;return true;
}
template<class Reader> bool Live(Reader& read,std::uintptr_t image,const Hook& h) noexcept {
 unsigned char head[16]{},relayBytes[14]{},trampoline[64]{};std::uintptr_t relay=0;
 return h.enabled&&h.trampoline&&read(h.trampoline,trampoline,64)&&
  !std::memcmp(trampoline,h.trampolineBytes,64)&&read(image+h.rva,head,16)&&
  !std::memcmp(head,h.installed,16)&&Relay(read,image+h.rva,h.detour,relay,relayBytes)&&
  relay==h.relay&&!std::memcmp(relayBytes,h.relayBytes,14);
}
inline bool Same(const vehicle_native_protection::UIEvent9Receipt& a,
 const vehicle_native_protection::UIEvent9Receipt& b) noexcept {
 return a.installationSerial==b.installationSerial&&a.image==b.image&&a.entry==b.entry&&
  a.detour==b.detour&&a.relay==b.relay&&!std::memcmp(a.entryBytes,b.entryBytes,16)&&
  !std::memcmp(a.relayBytes,b.relayBytes,14);
}
template<class Reader> bool Receipt(Reader& read,std::uintptr_t image,std::uint64_t serial,
 const Hook& h,vehicle_native_protection::UIEvent9Receipt& out) noexcept {
 out={};if(!serial||h.rva!=0x124e140||!Live(read,image,h))return false;
 out.installationSerial=serial;out.image=image;out.entry=image+h.rva;out.detour=h.detour;out.relay=h.relay;
 std::memcpy(out.entryBytes,h.installed,16);std::memcpy(out.relayBytes,h.relayBytes,14);
 return Live(read,image,h);
}
template<class Reader,std::size_t N> bool Overlay(Reader& read,std::uintptr_t image,
 const Hook (&hooks)[N],const unsigned char (&original)[N][16],std::uintptr_t at,void* output,std::size_t size) noexcept {
 if(!output||!Range(at,size))return false;
 // Validate all ownership before mutating the caller's already-read bytes.
 // This layer invokes no code-view reader and cannot recurse into itself.
 for(const auto& h:hooks)if(!Live(read,image,h))return false;
 auto* bytes=static_cast<unsigned char*>(output);
 for(std::size_t i=0;i<N;++i){const auto begin=image+hooks[i].rva,end=begin+5;
  const auto low=at>begin?at:begin,high=at+size<end?at+size:end;
  if(low<high)std::memcpy(bytes+low-at,original[i]+low-begin,high-low);
 }return true;
}
} // namespace mgscoop::vehicle_mirror_hook_read
