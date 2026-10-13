#pragma once
#include "coop_passenger.hpp"
namespace mgscoop::vehicle_registry_read {
namespace pd=coop_passenger::detail;
struct Lookup {
 std::uintptr_t provider=0,holder=0,descriptor=0,primary=0,primaryVT=0,primaryCountSlot=0,query=0,queryVT=0,querySlot=0;
 std::uintptr_t queryOwner=0,innerQuerySlot=0,queryRecords=0,queryBitsHeader=0,queryBits=0;
 std::uint16_t count=0,queryCount=0;std::uint32_t queryMode=0,queryLive=0;
};
template<class Reader> bool Observe(Reader& r,std::uintptr_t image,std::size_t span,
 const pd::EngineRead& engine,const pd::Unit& unit,Lookup& out) noexcept {
 out={};std::uintptr_t vt=0,slot=0,descriptorAgain=0;
 if(engine.registry!=image+0x2c38008||engine.vtable!=image+0x2305e70||engine.query!=image+0xbf3a80||
 !pd::Get(r,image+0x2c389a0,out.provider)||!pd::Range(out.provider,0x470)||
 !pd::Get(r,out.provider,vt)||vt!=image+0x23073c0||!pd::Get(r,vt+0x20,slot)||slot!=image+0xbf99e0||
 !pd::Get(r,out.provider+0x30+std::uintptr_t(unit.vehicle>>9)*8,out.holder)||!pd::Range(out.holder,16)||
 !pd::Get(r,out.holder+8,out.descriptor)||!pd::Range(out.descriptor,0x30)||
 !pd::Get(r,out.descriptor+0x20,out.primary)||out.primary!=unit.object-0x20||
 !pd::Get(r,out.primary,out.primaryVT)||!pd::InImage(out.primaryVT,image,span,0x48)||
 !pd::Get(r,out.primaryVT+0x40,out.primaryCountSlot)||!pd::InImage(out.primaryCountSlot,image,span,1)||
 !pd::Get(r,out.primary+8,descriptorAgain)||descriptorAgain!=out.descriptor||
 !pd::Get(r,out.primary+0x1e0,out.count)||out.count!=unit.allocated||
 !pd::Get(r,out.descriptor+0x28,out.query)||!pd::Range(out.query,0x38)||
 !pd::Get(r,out.query,out.queryVT)||!pd::InImage(out.queryVT,image,span,0x18)||
 !pd::Get(r,out.queryVT+0x10,out.querySlot)||!pd::InImage(out.querySlot,image,span,1))return false;
 // Preserve unknown proxy identities as read-only evidence. BF4690 installs
 // this exact proxy; BF5450 uses its primary-owner field and rejects mode2.
 if(out.queryVT==image+0x23063f8&&out.querySlot==image+0xbf5450&&
  (!pd::Get(r,out.query+0x20,out.queryOwner)||!pd::Get(r,out.query+0x30,out.queryMode)||
   !pd::Get(r,out.primaryVT+0x70,out.innerQuerySlot)))return false;
 return true;
}
template<class Reader> bool Callable(Reader& r,std::uintptr_t image,const pd::Unit& unit,Lookup& out) noexcept {
 if(out.primaryVT!=image+0x22ed930||out.primaryCountSlot!=image+0xb2dbe0||out.queryVT!=image+0x23063f8||out.querySlot!=image+0xbf5450||
 out.queryOwner!=out.primary||out.queryMode==2||out.innerQuerySlot!=image+0xb33420||
 !pd::Get(r,out.primary+0x1e0,out.queryCount)||out.queryCount!=unit.allocated||
 !pd::Get(r,out.primary+0x30,out.queryRecords)||out.queryRecords!=unit.records||
 !pd::Get(r,out.primary+0x1c8,out.queryBitsHeader)||!pd::Range(out.queryBitsHeader,16)||
 !pd::Get(r,out.queryBitsHeader+8,out.queryBits)||out.queryBits!=unit.bitmap||
 !pd::Get(r,out.queryBits+((unit.vehicle&0x1ffu)>>5)*4,out.queryLive)||out.queryLive!=unit.liveWord)return false;
 return true;
}
inline bool Same(const Lookup& a,const Lookup& b) noexcept {
 return a.provider==b.provider&&a.holder==b.holder&&a.descriptor==b.descriptor&&a.primary==b.primary&&a.primaryVT==b.primaryVT&&a.primaryCountSlot==b.primaryCountSlot&&
 a.query==b.query&&a.queryVT==b.queryVT&&a.querySlot==b.querySlot&&a.queryOwner==b.queryOwner&&a.queryMode==b.queryMode&&a.innerQuerySlot==b.innerQuerySlot&&
 a.queryRecords==b.queryRecords&&a.queryBitsHeader==b.queryBitsHeader&&
 a.queryBits==b.queryBits&&a.count==b.count&&a.queryCount==b.queryCount&&a.queryLive==b.queryLive;
}
}
