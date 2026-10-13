#pragma once
#include <cstddef>
#include <cstdint>
namespace mgscoop::native_pose_hook_groups {
inline constexpr std::size_t coreCount=13,varsCount=7,providerCount=8,routeCount=8,originCount=2,queueCount=8,deferredCount=1;
inline constexpr std::size_t varsFirst=coreCount,providerFirst=varsFirst+varsCount,routeFirst=providerFirst+providerCount,originFirst=routeFirst+routeCount,queueFirst=originFirst+originCount,deferredFirst=queueFirst+queueCount,total=deferredFirst+deferredCount;
inline constexpr std::uintptr_t expected[]={0x11f6760,0xfe8770,0x9bb260,0xaf0140,0xb6cd60,0x9bfc90,0x9cc850,0x9cd030,0x9c3cd0,0x9bb7a0,0x9c2f70,0xff5510,0x111cba0,0x11301b0,0x1129d70,0x12ce870,0x9dc5f0,0x9aaa90,0x996b70,0x9ab9d0,0x17fea0,0x1802a0,0x180640,0x2dd300,0x180760,0x180a20,0x180c10,0x3a1db30,0xbf4690,0xbf4a80,0xbf8a20,0xbf8c10,0xbf8f20,0xbf9060,0xbf9620,0xbf91b0,0xbf3a80,0x11268b0,0xfdea70,0xfdeb40,0xfdeba0,0xfdef60,0xfdf010,0xfe0260,0xfdffc0,0x5c4910,0xfe00d0};
static_assert(sizeof(expected)/sizeof(expected[0])==total);
template<class R>bool ValidateOrder(R r)noexcept{for(std::size_t n=0;n<total;++n)if(r(n)!=expected[n])return false;return true;}
} // namespace mgscoop::native_pose_hook_groups
