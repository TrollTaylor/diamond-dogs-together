#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
namespace mgscoop::standing_selection_policy {
inline constexpr std::uint64_t kInitial=0xfc537fe811d39a38ull,kBridge=0xfc53714f24c47b07ull,kRest=0xfc53f6ab0bc9caafull,kWake=0xfc53f0f44e7c00fdull;
inline constexpr std::uint64_t kFileIdentity=0x670368045bc73396ull;
inline constexpr unsigned kCommonSlot=17;
enum class Phase:unsigned {none,initial,bridge,rest,wake};
inline Phase ForId(std::uint64_t id) noexcept {return id==kInitial?Phase::initial:id==kBridge?Phase::bridge:id==kRest?Phase::rest:id==kWake?Phase::wake:Phase::none;}
inline bool Graph(Phase p,std::uint64_t hash) noexcept {
 switch(p){case Phase::initial:return hash==0xeceee0f51e77ull||hash==0xf2364261c9d4ull;
 case Phase::bridge:return hash==0xb6c742e88a85ull||hash==0xb1c6aee936a4ull;
 case Phase::rest:return hash==0xd7654fb0a3f2ull;case Phase::wake:return hash==0x40d8fda5ca06ull;default:return false;}
}
// These fields are receipts required from ROOT'S still-unbound provider. The
// current cold observer deliberately cannot populate them. Calling a policy
// function, observing mode4, or loading the bank does not create a receipt.
struct Owner {
 std::uintptr_t image=0,player=0,data=0,state=0,record=0,ps=0,primary=0,records=0,node=0,cs=0,rows=0,engine=0,engineData=0,banks=0,controller=0,mog=0;
 std::uint64_t birth=0,lifecycle=0,registration=0;std::uint32_t generation=0,sceneEpoch=0,body=UINT32_MAX,thread=0,sequence=0,capacity=0;
};
inline bool Same(const Owner& a,const Owner& b) noexcept {
 return a.image==b.image&&a.player==b.player&&a.data==b.data&&a.state==b.state&&a.record==b.record&&a.ps==b.ps&&a.primary==b.primary&&a.records==b.records&&a.node==b.node&&a.cs==b.cs&&a.rows==b.rows&&
 a.engine==b.engine&&a.engineData==b.engineData&&a.banks==b.banks&&a.controller==b.controller&&a.mog==b.mog&&a.birth==b.birth&&a.lifecycle==b.lifecycle&&a.registration==b.registration&&a.generation==b.generation&&a.sceneEpoch==b.sceneEpoch&&a.body==b.body&&a.thread==b.thread&&a.sequence==b.sequence&&a.capacity==b.capacity;
}
inline bool Valid(const Owner& o) noexcept {
 return o.image&&o.player&&o.data&&o.state&&o.record&&o.ps&&o.primary&&o.records&&o.node&&o.cs&&o.rows&&o.engine&&o.engineData&&o.banks&&o.controller&&o.mog&&o.birth&&o.lifecycle&&o.registration&&o.generation&&o.sceneEpoch&&o.body<2&&o.thread&&!(o.sequence&1u)&&o.capacity&&o.capacity<=64;
}
inline bool SameLifetime(Owner a,Owner b) noexcept {
 // A later genuine job has a fresh even sequence. The episode retains model
 // birth/lifecycle/generation, never the first job's writer/window permission.
 a.sequence=0;b.sequence=0;a.thread=0;b.thread=0;return Same(a,b);
}
struct Bank {
 std::uintptr_t bank=0,blob=0,sources=0,master=0,commonEntry=0;std::uint64_t fileIdentity=0;unsigned slot=UINT32_MAX,count=0,commonSlot=UINT32_MAX;
 // Native count4 WHOLE hash, 56-channel original master and each main/end
 // must be verified by the exact current standing asset reader, not booleans
 // copied from archive metadata. Exactly4 native descriptors use indices0..3.
 unsigned verifiedEntries=0;std::uintptr_t descriptors[4]{};
};
inline bool Same(const Bank& a,const Bank& b) noexcept {
 return a.bank==b.bank&&a.blob==b.blob&&a.sources==b.sources&&a.master==b.master&&a.commonEntry==b.commonEntry&&a.commonSlot==b.commonSlot&&a.fileIdentity==b.fileIdentity&&a.slot==b.slot&&a.count==b.count&&a.verifiedEntries==b.verifiedEntries&&
 a.descriptors[0]==b.descriptors[0]&&a.descriptors[1]==b.descriptors[1]&&a.descriptors[2]==b.descriptors[2]&&a.descriptors[3]==b.descriptors[3];
}
inline bool Valid(const Bank& b,const Owner& o) noexcept {
 if(!b.bank||!b.blob||!b.sources||!b.master||b.fileIdentity!=kFileIdentity||b.slot>=o.capacity||b.count!=4||b.verifiedEntries!=0xf||
  b.commonSlot!=kCommonSlot||b.commonEntry!=o.image+0x2a7f388+std::uintptr_t(b.commonSlot)*0x28)return false;
 for(unsigned i=0;i<4;++i)if(b.descriptors[i]!=b.sources+std::uintptr_t(i)*40)return false;return true;
}
enum class FrameKind:unsigned {unknown,rootOwnedPartnerJob,rootQualifiedLocalNativeJob,rootQualifiedNativeMotionJob};
struct Invocation {
 FrameKind kind=FrameKind::unknown;std::uint64_t frameSerial=0,jobSerial=0,activationSerial=0;
 std::uint32_t currentThread=0;std::uintptr_t returnAddress=0,destination=0,spec=0,context=0;
 std::int32_t specCount=0;std::uint64_t id=0;
 bool exactCodeCurrent=false,privateCopiedLabCurrent=false,wholePlayerLifetimeCurrent=false,bodyOwnsCurrentNativeFrame=false;
};
struct NativePhase {
 std::uintptr_t callback=0;std::uint64_t remembered=0,currentGraph=0;
 std::uint32_t life=0;std::uint16_t qAction=0;std::uint8_t mode=0,request=0,counter=0,action=0;
 bool nodeActive=false,carrying=false,carried=false;
};
struct Snapshot {Owner owner{};Bank bank{};Invocation call{};NativePhase phase{};};
struct OnsetWitness {
 // Natural CS+170 call from complete12CB2B0 mode4, return12CB48C,
 // exact first20 graph and BEFORE pose classification from native1002350.
 // The snapshot's currentgraph may still be the previous graph. This witness
 // must come from the actual immutable rooted native hook, not Lua/telemetry.
 Owner owner{};Bank bank{};std::uint64_t episodeSerial=0,nativeRequestSerial=0,graph=0;
 std::uintptr_t requestCaller=0,requestOwner=0;std::uint32_t actor=UINT32_MAX,poseClass=0;
};
struct Episode {Owner owner{};Bank bank{};std::uint64_t serial=0,requestSerial=0,lastFrame=0;Phase phase=Phase::none;bool live=false;};
inline const char* InvocationRefusal(const Snapshot& s) noexcept {
 const auto& v=s.call;const auto& o=s.owner;
 if(!v.exactCodeCurrent||!v.privateCopiedLabCurrent||!v.wholePlayerLifetimeCurrent)return "current_activation_or_lifetime";
 if(!Valid(o)||!Valid(s.bank,o))return "current_typed_owner_or_bank";
 if(v.kind!=FrameKind::rootQualifiedNativeMotionJob||!v.bodyOwnsCurrentNativeFrame||!v.frameSerial||!v.jobSerial||!v.activationSerial||v.activationSerial!=o.registration||v.currentThread!=o.thread)return "native_frame_unqualified";
 if(v.returnAddress!=o.image+0x1a6d88d||!v.destination||!v.spec||!v.context||v.specCount!=1)return "single_spec_native_caller";
 if(ForId(v.id)==Phase::none)return "uncovered_full64_id";
 if(s.phase.life==1||s.phase.life>2)return "wound_or_unknown_life";
 if(!s.phase.nodeActive||s.phase.action!=0x19||s.phase.carrying||s.phase.carried||s.phase.qAction==0x2a||s.phase.qAction==0x2b||s.phase.qAction==0x2c)return "sleep_body_interrupted";
 return nullptr;
}
inline bool Begin(Episode& e,const OnsetWitness& w,const Snapshot& current) noexcept {
 e={};if(InvocationRefusal(current)||!SameLifetime(w.owner,current.owner)||!Same(w.bank,current.bank)||!w.episodeSerial||!w.nativeRequestSerial||
  w.requestCaller!=w.owner.image+0x12cb48c||w.requestOwner!=w.owner.cs||w.actor!=w.owner.body||w.poseClass!=1||!Graph(Phase::initial,w.graph)||
  current.call.id!=kInitial||current.phase.callback!=current.owner.image+0x12cb2b0||current.phase.mode!=4||current.phase.life!=2||
  current.phase.qAction!=0x83||current.phase.counter!=2||current.phase.remembered!=w.graph||current.phase.request!=0)return false;
 e.owner=w.owner;e.bank=w.bank;e.serial=w.episodeSerial;e.requestSerial=w.nativeRequestSerial;e.phase=Phase::initial;e.live=true;return true;
}
inline const char* Qualify(const Episode& e,const Snapshot& a,const Snapshot& b) noexcept {
 if(!e.live||!e.serial||!e.requestSerial)return "no_native_standing_episode";
 if(const auto why=InvocationRefusal(a))return why;if(const auto why=InvocationRefusal(b))return why;
 if(!Same(a.owner,b.owner)||!SameLifetime(a.owner,e.owner)||!Same(a.bank,b.bank)||!Same(a.bank,e.bank))return "owner_bank_generation_changed";
 if(a.call.id!=b.call.id||a.call.destination!=b.call.destination||a.call.spec!=b.call.spec||a.call.context!=b.call.context||a.call.frameSerial!=b.call.frameSerial||a.call.jobSerial!=b.call.jobSerial)return "invocation_changed";
 if(a.phase.callback!=b.phase.callback||a.phase.mode!=b.phase.mode||a.phase.request!=b.phase.request||a.phase.counter!=b.phase.counter||a.phase.remembered!=b.phase.remembered||a.phase.currentGraph!=b.phase.currentGraph||a.phase.life!=b.phase.life||a.phase.qAction!=b.phase.qAction)return "native_phase_changed";
 const auto target=ForId(a.call.id);if(a.call.frameSerial<e.lastFrame)return "frame_reversed";
 const auto& p=a.phase;
 if(target==Phase::initial){if(e.phase!=Phase::initial||p.mode!=4||p.request!=0||p.counter!=2||p.life!=2||p.qAction!=0x83||p.callback!=a.owner.image+0x12cb2b0||!Graph(target,p.remembered))return "initial20_phase";}
 else if(target==Phase::bridge){if((e.phase!=Phase::initial&&e.phase!=Phase::bridge)||p.mode!=4||p.request!=0||p.life!=2||p.qAction!=0x83||p.callback!=a.owner.image+0x12cb2b0||!Graph(target,p.remembered))return "bridge100_phase";}
 // Mode5/6 do not write the mode4 remembered field. Their current resolved
 // graph is checked ONLY after this episode's actual preceding selections.
 // No current graph can create the missing onset or generation authority.
 else if(target==Phase::rest){if((e.phase!=Phase::bridge&&e.phase!=Phase::rest)||p.mode!=5||p.request!=0||p.life!=2||p.qAction!=0x84||p.callback!=a.owner.image+0x12caa10||!Graph(target,p.currentGraph))return "rest420_phase";}
 else if(target==Phase::wake){if((e.phase!=Phase::rest&&e.phase!=Phase::wake)||p.mode!=6||p.request!=0||p.qAction!=0x85||p.callback!=a.owner.image+0x12caf50||!Graph(target,p.currentGraph))return "wake140_phase";}
 else return "uncovered_phase";
 return nullptr;
}
inline bool Selected(Episode& e,const Snapshot& a,const Snapshot& b,std::uintptr_t actualDescriptor) noexcept {
 if(Qualify(e,a,b))return false;unsigned index=UINT32_MAX;
 const auto p=ForId(a.call.id);if(p==Phase::bridge)index=0;else if(p==Phase::initial)index=1;else if(p==Phase::wake)index=2;else if(p==Phase::rest)index=3;
 if(index>=4||actualDescriptor!=a.bank.descriptors[index])return false;e.phase=p;e.lastFrame=a.call.frameSerial;return true;
}
} // namespace mgscoop::standing_selection_policy
