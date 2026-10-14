#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "vehicle_native_protection_contract.hpp"
namespace mgscoop::vehicle_native_protection_gate_read {
// No cache, waiver or time-based permit. Every invocation compares every
// pinned byte; virtual slots still use the raw reader, never an overlay.
template<class Original,class Raw>bool Gate(Original& original,Raw& raw,std::uintptr_t image,std::size_t span) noexcept {
 namespace c=vehicle_native_protection_contract;
 if(!image||!span||image>UINTPTR_MAX-span)return false;
 unsigned char bytes[4096]{};
 for(const auto& s:c::sites){if(s.rva>span||!s.size||s.size>span-s.rva)return false;
  for(std::size_t off=0;off<s.size;){const auto n=s.size-off<sizeof(bytes)?s.size-off:sizeof(bytes);
   if(!original(image+s.rva+off,bytes,n)||std::memcmp(bytes,s.bytes+off,n))return false;off+=n;}}
 for(const auto& slot:c::slots){std::uintptr_t target=0;
  if(slot.rva>span||sizeof(target)>span-slot.rva||slot.target>=span||
   !raw(image+slot.rva,&target,sizeof(target))||target!=image+slot.target)return false;}
 return true;
}
}
