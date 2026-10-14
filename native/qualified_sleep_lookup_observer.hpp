#pragma once
// Staged read-only experiment. No hook installation, selection, original game
// method discovery, bank retain/release, epoch conversion or writer authority.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace mgscoop::qualified_sleep_lookup_observer {
inline constexpr unsigned kSpecs=32,kBanks=64;
inline constexpr std::uint64_t coveredIds[]={0xfc53714f24c47b07ull,0xfc537fe811d39a38ull,0xfc53f0f44e7c00fdull,0xfc53f6ab0bc9caafull};
enum class Path:unsigned {raw,setup};
struct Gate {bool exactPe=false,fullCode=false,privateCurrent=false,copiedSaveLab=false,standingCycleProfile=false,stopping=false;std::uintptr_t image=0;};
struct Event {Path path=Path::raw;std::uintptr_t engine=0,destination=0,spec=0,arg3=0,arg4=0,context=0,returnAddress=0;std::uint64_t id=0;};
struct Phase {
 bool observed=false,nodeActive=false,graphObserved=false;
 std::uintptr_t ps=0,primary=0,records=0,node=0,callback=0,controller=0,mog=0,graphNode=0;
 std::uint64_t rememberedGraph=0,currentGraph=0;
 std::uint32_t recordFlags=0,life=0;
 std::uint16_t flags=0,qAction=0;
 std::uint8_t mode=0,request=0,counter=0,action=0;
};
struct Owner {
 bool known=false;unsigned body=UINT32_MAX;
 std::uintptr_t player=0,data=0,record=0,cs=0,rows=0,engine=0,engineData=0,banks=0;
 std::uint32_t capacity=0,currentLocal=UINT32_MAX;Phase phase{};
};
struct Selected {
 bool observed=false,bankBound=false;unsigned slot=UINT32_MAX,index=UINT32_MAX;
 std::uint64_t id=0,fileIdentity=0;
 std::uintptr_t gani=0,descriptor=0,bank=0,blob=0,master=0,main=0,end=0;
 std::uint32_t bankCount=0;
};
struct Sample {
 bool ownerObserved=false,inputObserved=false,outputObserved=false;
 Owner owner{};std::int32_t specCount=-1,outputIndex=-1;std::uint32_t specLayer=0;
 std::uintptr_t contextOwner=0,contextBanks=0;std::uint32_t contextCapacity=0;
 std::uint64_t ids[kSpecs]{};bool covered[kSpecs]{};Selected selected[kSpecs]{};
 const char* reason="disabled";
};
struct Receipt {Event event{};Sample before{},after{};std::uintptr_t rawResult=0;bool setupResult=false,identityStable=false,afterOnly=false;};
struct ErrorPreserve {
 #if defined(_WIN32)
 DWORD error=GetLastError();~ErrorPreserve(){SetLastError(error);}
 #endif
};
inline thread_local bool inObserver=false;
struct ObserverLease {bool acquired=false;ObserverLease() noexcept {if(!inObserver){inObserver=true;acquired=true;}}~ObserverLease(){if(acquired)inObserver=false;}};
inline bool Range(std::uintptr_t p,std::size_t n) noexcept{return p&&n&&p<=std::numeric_limits<std::uintptr_t>::max()-n;}
inline bool Aligned(std::uintptr_t p,unsigned n) noexcept{return p&&!(p&(n-1));}
template<class R,class T> bool Get(R& r,std::uintptr_t at,T& value) noexcept {return Range(at,sizeof(T))&&r(at,&value,sizeof(T));}
inline bool Covered(std::uint64_t id) noexcept {for(auto x:coveredIds)if(x==id)return true;return false;}
inline bool Admitted(const Gate& g) noexcept {return g.exactPe&&g.fullCode&&g.privateCurrent&&g.copiedSaveLab&&g.standingCycleProfile&&!g.stopping&&Range(g.image,0xa01d000);}
inline bool NaturalRaw(std::uintptr_t image,std::uintptr_t caller) noexcept {
 constexpr std::uintptr_t rows[]={0x5d5e97,0x5d61b3,0x1a98c1f,0x1c6115d,0x1c75c59,0x1e0112f};
 for(auto row:rows)if(caller==image+row)return true;return false;
}
inline bool Natural(const Gate& g,const Event& e) noexcept{return e.path==Path::raw?NaturalRaw(g.image,e.returnAddress):e.returnAddress==g.image+0x1a6d88d;}
inline bool SameOwner(const Owner& a,const Owner& b) noexcept {
 return a.known&&b.known&&a.body==b.body&&a.player==b.player&&a.data==b.data&&a.record==b.record&&a.cs==b.cs&&a.rows==b.rows&&a.engine==b.engine&&a.engineData==b.engineData&&a.banks==b.banks&&a.capacity==b.capacity&&a.currentLocal==b.currentLocal;
}
// Recover only actual cold diagnostic identity, matching the original input
// engine or the ORIGINAL caller's bank array/capacity. No gameplay scope is
// manufactured from this mapping and it does not authorize selection.
template<class R> bool OwnerOnce(R& r,const Gate& g,const Event& e,Owner& out) noexcept {
 out={};std::uintptr_t p=0,q=0,vt=0,records=0,pools=0,table=0,ids=0,parts=0,sync=0;std::uint32_t n=0,flags=0,local=0;
 if(!Get(r,g.image+0x2a82c30,p)||!Aligned(p,8)||!Get(r,p,vt)||vt!=g.image+0x2295490||
  !Get(r,p+0x204,flags)||!(flags&1u)||(flags&0x100u)||!Get(r,p+0x208,n)||n!=2||!Get(r,p+0x20c,n)||n!=2||
  !Get(r,p+0x214,local)||local>1||!Get(r,p+0xa8,sync)||sync||!Get(r,p+0x60,q)||!Aligned(q,8)||!Get(r,q,vt)||vt!=g.image+0x23ad970||
  !Get(r,p+0x378,records)||!Aligned(records,8)||!Get(r,p+0x78,pools)||!pools||!Get(r,p+0x50,table)||!Aligned(table,8)||
  !Get(r,p+0x70,ids)||!Aligned(ids,2)||!Get(r,p+0x90,parts)||!parts)return false;
 std::uintptr_t array=0;std::uint32_t capacity=0;
 if(e.path==Path::setup&&(!Aligned(e.context,8)||!Get(r,e.context+0x18,array)||!Aligned(array,8)||!Get(r,e.context+0x34,capacity)||!capacity||capacity>kBanks))return false;
 bool found=false;
 for(unsigned body=0;body<2;++body){Owner candidate{};candidate.player=p;candidate.data=q;candidate.body=body;candidate.currentLocal=local;
  std::uint8_t pool=255,part=255;std::uint16_t id=255;std::uintptr_t mapped=0,parent=0,target=0;std::uint32_t count=0,base=0;
  if(!Get(r,pools+body,pool)||pool>=16||!Get(r,parts+body,part)||part!=3||!Get(r,ids+2u*body,id)||id!=body)continue;
  candidate.record=records+std::uintptr_t(pool)*0x6a8;
  if(!Get(r,table+std::uintptr_t(pool)*8,mapped)||mapped!=candidate.record||!Get(r,candidate.record,vt)||vt!=g.image+0x2295210||
   !Get(r,candidate.record+8,count)||count!=1||!Get(r,candidate.record+0xc,base)||base!=body||!Get(r,candidate.record+0x138,parent)||parent!=p||
   !Get(r,candidate.record+0x10,candidate.cs)||!Aligned(candidate.cs,8)||!Get(r,candidate.cs,vt)||vt!=g.image+0x22e6070||
   !Get(r,vt+0x400,target)||target!=g.image+0xaf0a00||!Get(r,candidate.cs+8,count)||count!=1||!Get(r,candidate.cs+0xc,base)||base!=body||
   !Get(r,candidate.cs+0x10,candidate.rows)||!Aligned(candidate.rows,16)||!Get(r,candidate.rows+0x70,candidate.engine)||!Aligned(candidate.engine,8)||
   !Get(r,candidate.engine,vt)||vt!=g.image+0x24c1988||!Get(r,candidate.engine+0x10,candidate.engineData)||!Aligned(candidate.engineData,8)||
   !Get(r,candidate.engine+0x98,candidate.banks)||!Aligned(candidate.banks,8)||!Get(r,candidate.engine+0x1c,candidate.capacity)||!candidate.capacity||candidate.capacity>kBanks)continue;
  if(e.path==Path::raw?candidate.engine!=e.engine:candidate.banks!=array||candidate.capacity!=capacity)continue;
  if(found)return false;candidate.known=true;out=candidate;found=true;
 }return found;
}
// Root may attach its separately pinned current Sleep/graph reader here.
// Its result is diagnostic: there is deliberately no epoch, generation,
// admission, eligibility or owner-thread field to derive from this callback.
template<class R,class PhaseReader> bool Input(R& r,PhaseReader& phase,const Gate& g,const Event& e,Sample& s) noexcept {
 s={};if(!Admitted(g)||!Natural(g,e)){s.reason="profile_or_callsite";return false;}
 Owner first{},second{};if(!OwnerOnce(r,g,e,first)||!OwnerOnce(r,g,e,second)||!SameOwner(first,second)){s.reason="cold_owner_unresolved";return false;}
 s.owner=second;s.ownerObserved=true;phase(r,g.image,s.owner,s.owner.phase);
 if(e.path==Path::raw){s.specCount=1;s.ids[0]=e.id;s.covered[0]=Covered(e.id);s.inputObserved=true;s.reason="raw_input";return true;}
 if(!Aligned(e.destination,8)||!Aligned(e.spec,8)||!Get(r,e.spec+0x200,s.specCount)||s.specCount<0||s.specCount>int(kSpecs)||
  !Get(r,e.spec+0x210,s.specLayer)||!Get(r,e.destination+0x48,s.outputIndex)||(s.outputIndex!=0&&s.outputIndex!=1)||
  !Get(r,e.context,s.contextOwner)||!Get(r,e.context+0x18,s.contextBanks)||s.contextBanks!=s.owner.banks||
  !Get(r,e.context+0x34,s.contextCapacity)||s.contextCapacity!=s.owner.capacity){s.reason="setup_input_unresolved";return false;}
 for(unsigned i=0;i<unsigned(s.specCount);++i){if(!Get(r,e.spec+std::uintptr_t(i)*0x20,s.ids[i])){s.reason="spec_unreadable";return false;}s.covered[i]=Covered(s.ids[i]);}
 s.inputObserved=true;s.reason="setup_input";return true;
}
template<class R> bool Source(R& r,std::uintptr_t image,const Owner& owner,std::uint64_t id,std::uintptr_t descriptor,Selected& s) noexcept {
 s={};s.id=id;s.descriptor=descriptor;std::uint64_t fullId=0,tableId=0;std::uint16_t bankFlags=0;std::uint8_t kind=0,loadedKind=0;std::uintptr_t vt=0,sources=0,loaded=0;std::uint32_t count=0,magic=0;
 if(!Aligned(descriptor,8)||!Range(descriptor,0x28)||!Get(r,descriptor,fullId)||fullId!=id||!Get(r,descriptor+8,s.bank)||!Aligned(s.bank,8)||!Range(s.bank,0xa0)||
  !Get(r,descriptor+0x20,kind)||(kind&15u)!=3||!Get(r,descriptor+0x24,s.index)||!Get(r,s.bank,vt)||vt!=image+0x24c2890||
  !Get(r,s.bank+0x30,s.fileIdentity)||!Get(r,s.bank+0x40,loadedKind)||loadedKind!=2||!Get(r,s.bank+0x84,s.bankCount)||!s.bankCount||s.bankCount>4096||
  !Get(r,s.bank+0x88,sources)||!Aligned(sources,8)||!Range(sources,std::size_t(s.bankCount)*0x28)||s.index>=s.bankCount||descriptor!=sources+std::uintptr_t(s.index)*0x28||
  !Get(r,s.bank+0x98,s.blob)||!Aligned(s.blob,16)||!Range(s.blob,0x20+std::size_t(s.bankCount)*0x20)||!Get(r,s.bank+0x60,loaded)||loaded!=s.blob||!Get(r,s.blob,magic)||magic!=0x0c012b72||
  !Get(r,s.blob+4,count)||count!=s.bankCount||!Get(r,s.blob+0x12,bankFlags)||!(bankFlags&0x1000u)||
  !Get(r,s.blob+0x20+std::uintptr_t(s.index)*0x20,tableId)||tableId!=id)return false;
 unsigned matches=0;for(unsigned slot=0;slot<owner.capacity;++slot){std::uintptr_t bank=0;if(!Get(r,owner.banks+std::uintptr_t(slot)*0x30+0x18,bank))return false;if(bank==s.bank){++matches;s.slot=slot;}}
 s.bankBound=matches==1;s.observed=s.bankBound;return s.observed;
}
// A post-only receipt can read an actual return on a newly changed source;
// it must be labeled afterOnly and never manufacture pre-call identity.
template<class R> bool CurrentResult(R& r,const Gate& g,const Event& e,std::uintptr_t rawResult,bool setupResult,Sample& after) noexcept {
 if(!after.inputObserved)return false;
 if(e.path==Path::raw){if(!rawResult)return false;after.outputObserved=Source(r,g.image,after.owner,e.id,rawResult,after.selected[0]);return after.outputObserved;}
 if(!setupResult)return false;
 std::uintptr_t view=0;std::uint16_t count=0;
 if(!Get(r,e.destination+std::uintptr_t(after.outputIndex)*0x20,view)||!Aligned(view,8)||!Get(r,view+0xc,count)||count<unsigned(after.specCount))return false;
 for(unsigned i=0;i<unsigned(after.specCount);++i){std::int32_t offset=0;std::uintptr_t descriptor=0;Selected selected{};
  if(!Get(r,view+0x30+std::uintptr_t(i)*4,offset)||offset<=0||offset>0x400000||!Range(view,unsigned(offset)+0x90)||
   !Get(r,view+unsigned(offset)+0x80,descriptor)||!Source(r,g.image,after.owner,after.ids[i],descriptor,selected))return false;
  selected.gani=view+unsigned(offset);
  if(!Get(r,selected.gani+0x50,selected.master)||!Get(r,selected.gani+0x58,selected.main)||!Get(r,selected.gani+0x88,selected.end))return false;
  after.selected[i]=selected;
 }after.outputObserved=true;return true;
}
template<class R> bool Result(R& r,const Gate& g,const Event& e,const Sample& before,std::uintptr_t rawResult,bool setupResult,Sample& after) noexcept {
 if(!before.inputObserved||!after.inputObserved||!SameOwner(before.owner,after.owner)||before.specCount!=after.specCount||
  (e.path==Path::setup&&before.outputIndex!=after.outputIndex))return false;
 for(unsigned i=0;i<unsigned(before.specCount);++i)if(before.ids[i]!=after.ids[i])return false;
 return CurrentResult(r,g,e,rawResult,setupResult,after);
}
// These adapters are callable only after root installs/admits exact private
// detours. All arguments and the ORIGINAL result are preserved; every path,
// including observer refusal, forwards exactly once. Detached tests use fake
// originals only. Observation preserves LastError; nested intercepted calls
// forward unchanged without recursive diagnostic reads or reports.
template<class Raw,class R,class PhaseReader,class Report> std::uintptr_t RawForward(Raw original,R& read,PhaseReader& phase,Report& report,const Gate& gate,const Event& event) {
 ObserverLease lease{};if(!lease.acquired)return original(event.engine,event.id);
 Receipt receipt{};receipt.event=event;{ErrorPreserve error{};Input(read,phase,gate,event,receipt.before);}
 receipt.rawResult=original(event.engine,event.id);
 {ErrorPreserve error{};if(receipt.before.inputObserved){Input(read,phase,gate,event,receipt.after);receipt.identityStable=SameOwner(receipt.before.owner,receipt.after.owner);Result(read,gate,event,receipt.before,receipt.rawResult,false,receipt.after);}report(receipt);}
 return receipt.rawResult;
}
template<class Setup,class R,class PhaseReader,class Report> bool SetupForward(Setup original,R& read,PhaseReader& phase,Report& report,const Gate& gate,const Event& event) {
 ObserverLease lease{};if(!lease.acquired)return original(event.destination,event.spec,event.arg3,event.arg4,event.context);
 Receipt receipt{};receipt.event=event;{ErrorPreserve error{};Input(read,phase,gate,event,receipt.before);}
 receipt.setupResult=original(event.destination,event.spec,event.arg3,event.arg4,event.context);
 {ErrorPreserve error{};if(receipt.before.inputObserved){Input(read,phase,gate,event,receipt.after);receipt.identityStable=SameOwner(receipt.before.owner,receipt.after.owner);Result(read,gate,event,receipt.before,0,receipt.setupResult,receipt.after);}report(receipt);}
 return receipt.setupResult;
}
} // namespace mgscoop::qualified_sleep_lookup_observer
