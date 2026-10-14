#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace mgscoop::coop_passenger {
using Log = void (*)(const char*) noexcept;
// Internal native observer and code-view layer only. Neither opts into a
// passenger, acquires an owner or changes native geometry/admission.
using NativePoseObserver=void(*)(std::uintptr_t primary,unsigned body,std::uintptr_t record,std::uintptr_t exactCaller) noexcept;
bool RegisterNativePoseObserver(NativePoseObserver) noexcept; // immutable before install
bool OverlayOriginal(std::uintptr_t at,void* bytes,std::size_t size) noexcept; // pure acyclic layer
// Private worker prototype. Only natural local-body0 entry is adapted. The
// stock selector, entry animation, physical admission, attachment and exit run
// unchanged. No node, occupancy, transform or global-online field is written.
bool Install(const void* image, std::size_t span, bool exactRetailAccepted, Log log) noexcept;
// The natural coordinator supplies fresh paired gameplay admission every tick.
void Tick(std::uint32_t mission, std::uint16_t epoch, bool pairedAdmission) noexcept;
bool SetEnabled(bool on) noexcept; // private worker only; refuses live disable
bool Enabled() noexcept;
bool StatusLine(char* out, std::size_t capacity) noexcept;
// Read-only current solo-driver census. Compare it with the game's GetRiderId
// before enabling the occupied-driver adapter for the first hidden run.
bool SeatCensusLine(char* out,std::size_t capacity) noexcept;
// Observe world+60 registry and untouched P120 bindings only. A successful
// read is not a qualification to invoke the registry's currently unknown ABI.
bool EngineCensusLine(char* out,std::size_t capacity) noexcept;
bool LifetimeCensusLine(char* out,std::size_t capacity) noexcept; // read-only private current census
bool ResourceCensusLine(char* out,std::size_t capacity) noexcept; // read-only private current census
bool CameraCensusLine(char* out,std::size_t capacity) noexcept; // read-only actual resource
bool LookupCensusLine(char* out,std::size_t capacity) noexcept;
// A lifecycle loss does not invoke an old unit's exit. False means a live
// native passenger still needs the normal exit before coordinator shutdown.
bool Shutdown() noexcept;

namespace detail {
inline bool Range(std::uintptr_t p, std::size_t n) noexcept {
 return p && n && p <= std::numeric_limits<std::uintptr_t>::max()-n;
}
template<class Reader,class T> bool Get(Reader& r,std::uintptr_t p,T& out) noexcept {
 return Range(p,sizeof(T)) && r(p,&out,sizeof(T));
}
struct EngineRead {
 std::uintptr_t root=0,world=0,registry=0,vtable=0,query=0,map=0,bound[2]{};
};
inline bool InImage(std::uintptr_t p,std::uintptr_t image,std::size_t span,std::size_t size) noexcept {
 return Range(image,span)&&p>=image&&p-image<=span&&size<=span-(p-image);
}
template<class Reader> bool ReadEngine(Reader& r,std::uintptr_t image,std::size_t span,
 std::uintptr_t player,EngineRead& out) noexcept {
 out={};
 return InImage(image+0x2c3a8a0,image,span,8)&&Range(player,0x128)&&
 Get(r,image+0x2c3a8a0,out.root)&&Range(out.root,16)&&Get(r,out.root+8,out.world)&&
 Range(out.world,0x68)&&Get(r,out.world+0x60,out.registry)&&Range(out.registry,8)&&
 Get(r,out.registry,out.vtable)&&InImage(out.vtable,image,span,0x40)&&
 Get(r,out.vtable+0x38,out.query)&&InImage(out.query,image,span,1)&&
 Get(r,player+0x120,out.map)&&Range(out.map,16)&&Get(r,out.map,out.bound[0])&&
 Get(r,out.map+8,out.bound[1]);
}
inline bool Same(const EngineRead& a,const EngineRead& b) noexcept {
 return a.root==b.root&&a.world==b.world&&a.registry==b.registry&&a.vtable==b.vtable&&
 a.query==b.query&&a.map==b.map&&a.bound[0]==b.bound[0]&&a.bound[1]==b.bound[1];
}
struct Unit {
 std::uintptr_t service=0,object=0,records=0,bitmap=0,unit=0,metadata=0,variantTable=0,variant=0;
 std::uint16_t vehicle=0xffff,allocated=0,rider[4]{0xffff,0xffff,0xffff,0xffff};
 std::uint8_t type=0,baseCount=0,extraCount=0,count=0;
 std::uint32_t liveWord=0;
};
inline bool Same(const Unit& a,const Unit& b) noexcept {
 return a.service==b.service&&a.object==b.object&&a.records==b.records&&a.bitmap==b.bitmap&&
 a.unit==b.unit&&a.metadata==b.metadata&&a.variantTable==b.variantTable&&a.variant==b.variant&&
 a.vehicle==b.vehicle&&a.allocated==b.allocated&&a.type==b.type&&a.baseCount==b.baseCount&&
 a.extraCount==b.extraCount&&a.count==b.count&&a.liveWord==b.liveWord&&
 !std::memcmp(a.rider,b.rider,sizeof(a.rider));
}
// A272A0 and B2AC60/A28690: service B0 is the final Vehicle GameObject+20
// interface22ED9F0; its count getter B2DD30 reads u16+1C0. Native lookup
// requires a live bitmap bit and returns records+index*80. A27CC0 derives
// capacity from service B8[type>>4].1B7 plus variant. B78D10's authored door
// remapping and the original raycast are retained by the caller.
template<class Reader> bool ReadUnit(Reader& r,std::uintptr_t image,std::uintptr_t service,
 std::uint16_t vehicle,std::uintptr_t expectedUnit,Unit& out) noexcept {
 out={}; const unsigned index=vehicle&0x1ffu;
 if(!Range(image,0xa01d000)||(vehicle&0xfe00u)!=0x6400u||!Range(service,0x160))return false;
 std::uintptr_t vt=0,slot=0;out.service=service;out.vehicle=vehicle;
 if(!Get(r,service,vt)||vt!=image+0x22ac8e0||!Get(r,vt+0x18,slot)||slot!=image+0xa272a0||
 !Get(r,vt+0x168,slot)||slot!=image+0xa27260||!Get(r,vt+0x170,slot)||slot!=image+0xa27220||
 !Get(r,service+0xb0,out.object)||!Range(out.object,0x1c8)||
 !Get(r,out.object,vt)||vt!=image+0x22ed9f0||!Get(r,vt,slot)||slot!=image+0xb2dd30||
 !Get(r,out.object+0x1c0,out.allocated)||!out.allocated||out.allocated>512||index>=out.allocated||
 !Get(r,out.object+0x10,out.records)||!Range(out.records,std::size_t(out.allocated)*0x80)||
 !Get(r,out.object+0x28,out.bitmap)||!Range(out.bitmap,((out.allocated+31u)/32u)*4u)||
 !Get(r,out.bitmap+(index>>5)*4u,out.liveWord)||!(out.liveWord&(1u<<(index&31u))))return false;
 out.unit=out.records+std::uintptr_t(index)*0x80;
 if((expectedUnit&&out.unit!=expectedUnit)||!Get(r,out.unit+0x6c,out.type))return false;
 const unsigned category=out.type>>4,variant=out.type&15u;
 if(!Get(r,service+0xb8+category*8u,out.metadata)||!Range(out.metadata,0x1d8)||
 !Get(r,out.metadata+0x1b7,out.baseCount)||!Get(r,out.metadata+0x1d0,out.variantTable)||
 !Range(out.variantTable,16*8)||!Get(r,out.variantTable+variant*8u,out.variant))return false;
 if(out.variant&&(!Range(out.variant,0xb6)||!Get(r,out.variant+0xb5,out.extraCount)))return false;
 const unsigned capacity=unsigned(out.baseCount)+unsigned(out.extraCount);
 // The census can also qualify a smaller vehicle. EmptyPassenger separately
 // refuses it; sentinel4 is never a valid Player2 raw seat.
 if(!capacity||capacity>4||!r(out.unit+0x30,out.rider,capacity*2))return false;
 out.count=static_cast<std::uint8_t>(capacity);return true;
}
inline std::uint8_t EmptyPassenger(const Unit& u) noexcept {
 if(u.count<3||u.count>4||u.rider[0]==0xffff||u.rider[0]==0)return 4;
 for(unsigned seat=2;seat<u.count;++seat)if(u.rider[seat]==0xffff)return static_cast<std::uint8_t>(seat);
 return 4;
}
struct Lease {
 bool valid=false,committed=false;
 std::uint64_t issued=0,serial=0;
 std::uint32_t mission=0,generation=0,thread=0;
 std::uint16_t epoch=0;
 std::uintptr_t player=0,primary=0,record=0;
 std::uint8_t seat=4;
 Unit vehicle{};
};
inline bool Fresh(std::uint64_t now,std::uint64_t at,std::uint64_t limit) noexcept {
 return at&&now>=at&&now-at<=limit;
}
inline bool SameIdentity(const Lease& a,const Lease& b) noexcept {
 return a.valid&&b.valid&&a.serial&&a.serial==b.serial&&a.mission==b.mission&&a.generation==b.generation&&
 a.thread==b.thread&&a.epoch==b.epoch&&a.player==b.player&&a.primary==b.primary&&a.record==b.record&&
 a.seat==b.seat&&a.issued==b.issued&&Same(a.vehicle,b.vehicle);
}
inline bool OverrideRange(const Lease& l,const Unit& current,std::uintptr_t primary,
 std::uint32_t body,std::uint64_t now,bool naturalPrequery,bool naturalPhase1,
 std::uint8_t& first,std::uint8_t& last) noexcept {
 if(!l.valid||!l.serial||body||primary!=l.primary||(!naturalPrequery&&!naturalPhase1)||
 !Fresh(now,l.issued,1500)||l.seat<2||l.seat>=current.count||l.seat>3||
 current.rider[l.seat]!=0xffff||current.rider[0]==0xffff||current.rider[0]==0||!Same(l.vehicle,current))return false;
 first=last=l.seat;return true;
}
// Gateway entered only on the original occupied-driver JNE. All flags,
// volatile GPRs and XMM0..5 are restored before either original continuation.
// CanExec's audited seven pushes +320-byte frame leave RSP16-aligned here.
// Helper arguments: original RAX unit, RSI primary, R15D actualID, RDI target.
inline std::size_t BuildGateway(unsigned char* out,std::size_t cap,std::uintptr_t helper,
 std::uintptr_t proceed,std::uintptr_t refuse) noexcept {
 if(!out||cap<320||!helper||!proceed||!refuse)return 0;
 std::size_t n=0;const auto byte=[&](unsigned x) noexcept {out[n++]=static_cast<unsigned char>(x);};
 const auto bytes=[&](const unsigned char* p,std::size_t size) noexcept {std::memcpy(out+n,p,size);n+=size;};
 const auto qword=[&](std::uintptr_t x) noexcept {std::memcpy(out+n,&x,8);n+=8;};
 const unsigned char push[]={0x9c,0x50,0x51,0x52,0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53,
 0x48,0x81,0xec,0x80,0,0,0};bytes(push,sizeof(push));
 for(unsigned i=0;i<6;++i){byte(0xf3);byte(0x0f);byte(0x7f);byte(0x44+(i<<3));byte(0x24);byte(0x20+i*16);}
 const unsigned char args[]={0x48,0x8b,0x8c,0x24,0xb0,0,0,0,0x48,0x8b,0xd6,0x45,0x8b,0xc7,0x4c,0x8b,0xcf,0x48,0xb8};
 bytes(args,sizeof(args));qword(helper);byte(0xff);byte(0xd0);byte(0x84);byte(0xc0);byte(0x0f);byte(0x84);
 const auto branch=n;n+=4;
 const auto epilogue=[&](std::uintptr_t target) noexcept {
  for(unsigned i=0;i<6;++i){byte(0xf3);byte(0x0f);byte(0x6f);byte(0x44+(i<<3));byte(0x24);byte(0x20+i*16);}
  const unsigned char pop[]={0x48,0x81,0xc4,0x80,0,0,0,0x41,0x5b,0x41,0x5a,0x41,0x59,0x41,0x58,0x5a,0x59,0x58,0x9d,0xff,0x25,0,0,0,0};
  bytes(pop,sizeof(pop));qword(target);
 };
 epilogue(proceed);const auto fail=n;epilogue(refuse);
 const auto disp=static_cast<std::int32_t>(fail-(branch+4));std::memcpy(out+branch,&disp,4);return n;
}
} // detail
} // mgscoop::coop_passenger
