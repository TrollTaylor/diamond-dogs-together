#include "standing_selection_clone.hpp"
#include "standing_selection_dispatch.hpp"
#include "standing_selection_clone_win32.hpp"
#include <cstdio>
#include <vector>
#include <algorithm>
namespace b=mgscoop::standing_selection_clone;
namespace p=mgscoop::standing_selection_policy;
namespace d=mgscoop::standing_selection_dispatch;
unsigned checks=0,failures=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
template<class T>T Load(const b::Image& i,unsigned at){T result{};std::memcpy(&result,i.bytes.data()+at,sizeof(T));return result;}
p::Snapshot Good(){p::Snapshot s{};auto& o=s.owner;
 o.image=0x140000000;o.player=0x500000;o.data=0x501000;o.state=0x502000;o.record=0x503000;o.ps=0x504000;o.primary=0x505000;o.records=0x506000;o.node=0x507000;o.cs=0x508000;o.rows=0x509000;o.engine=0x50a000;o.engineData=0x50b000;o.banks=0x50c000;o.controller=0x50d000;o.mog=0x50e000;o.birth=29;o.lifecycle=8;o.registration=5;o.generation=3;o.sceneEpoch=7;o.body=1;o.thread=9;o.sequence=20;o.capacity=32;
 auto& v=s.call;v.kind=p::FrameKind::rootQualifiedNativeMotionJob;v.frameSerial=10;v.jobSerial=19;v.activationSerial=5;v.currentThread=9;v.returnAddress=o.image+0x1a6d88d;v.destination=0x600000;v.spec=0x601000;v.context=0x602000;v.specCount=1;v.id=p::kInitial;v.exactCodeCurrent=true;v.privateCopiedLabCurrent=true;v.wholePlayerLifetimeCurrent=true;v.bodyOwnsCurrentNativeFrame=true;
 auto& k=s.bank;k.bank=0x700000;k.blob=0x701000;k.sources=0x702000;k.master=0x703000;k.fileIdentity=p::kFileIdentity;k.slot=18;k.count=4;k.commonSlot=17;k.commonEntry=o.image+0x2a7f388+17*0x28;k.verifiedEntries=15;for(unsigned z=0;z<4;++z)k.descriptors[z]=k.sources+z*40;
 s.phase={o.image+0x12cb2b0,0xeceee0f51e77,0x999999999999,2,0x83,4,0,2,0x19,true,false,false};return s;
}
p::OnsetWitness Witness(const p::Snapshot& s){return {s.owner,s.bank,88,31,s.phase.remembered,s.owner.image+0x12cb48c,s.owner.cs,s.owner.body,1};}
void Policy(){auto s=Good();p::Episode e{};const auto witness=Witness(s);Check(p::Begin(e,witness,s),"genuine first20 witness while current graph is OLD");Check(!p::Qualify(e,s,s),"initial20 exact qualification");
 auto bad=s;bad.call.specCount=2;Check(p::Qualify(e,bad,bad)!=nullptr,"mixed specs original");bad=s;bad.call.id=p::kInitial^0x1000000000000ull;Check(p::Qualify(e,bad,bad)!=nullptr,"full64 ID high bits");
 bad=s;bad.call.kind=p::FrameKind::unknown;Check(p::Qualify(e,bad,bad)!=nullptr,"cold diagnostics cannot own frame");bad=s;bad.owner.generation++;Check(p::Qualify(e,bad,bad)!=nullptr,"generation changed");
 bad=s;bad.owner.birth++;Check(p::Qualify(e,bad,bad)!=nullptr,"reborn same addresses");bad=s;bad.owner.lifecycle++;Check(p::Qualify(e,bad,bad)!=nullptr,"lifetime changed");bad=s;bad.call.jobSerial=0;Check(p::Qualify(e,bad,bad)!=nullptr,"window required");
 bad=s;bad.phase.carried=true;Check(p::Qualify(e,bad,bad)!=nullptr,"carried refuse");bad=s;bad.phase.carrying=true;Check(p::Qualify(e,bad,bad)!=nullptr,"carrier refuse");bad=s;bad.phase.life=1;Check(p::Qualify(e,bad,bad)!=nullptr,"wound distinct");bad=s;bad.phase.qAction=0x2b;Check(p::Qualify(e,bad,bad)!=nullptr,"native carry Q refuse");
 auto wrongWitness=witness;wrongWitness.poseClass=2;p::Episode denied{};Check(!p::Begin(denied,wrongWitness,s),"crouch cannot create standing episode");wrongWitness=witness;wrongWitness.graph=0x40d8fda5ca06;Check(!p::Begin(denied,wrongWitness,s),"wake140 cannot create episode");
 p::Episode cold{};s.phase.currentGraph=0xeceee0f51e77;Check(p::Qualify(cold,s,s)!=nullptr,"current graph alone cannot create episode");
 Check(!p::Selected(e,s,s,s.bank.descriptors[0]),"wrong native descriptor refused");Check(p::Selected(e,s,s,s.bank.descriptors[1]),"initial actual selected index1");
 bad=s;bad.call.id=p::kRest;bad.phase.mode=5;bad.phase.qAction=0x84;bad.phase.callback=s.owner.image+0x12caa10;bad.phase.currentGraph=0xd7654fb0a3f2;Check(p::Qualify(e,bad,bad)!=nullptr,"cannot skip bridge100");
 s.call.id=p::kBridge;s.phase.remembered=0xb6c742e88a85;s.call.frameSerial=11;s.call.jobSerial=20;s.owner.sequence=22;s.owner.thread=s.call.currentThread=12;Check(p::Selected(e,s,s,s.bank.descriptors[0]),"new genuine async job thread/sequence bridge100");
 s.call.id=p::kRest;s.phase.mode=5;s.phase.qAction=0x84;s.phase.callback=s.owner.image+0x12caa10;s.phase.currentGraph=0xd7654fb0a3f2;s.call.frameSerial=12;Check(p::Selected(e,s,s,s.bank.descriptors[3]),"rest420 with unchanged mode4 remembered bridge");
 bad=s;bad.phase.currentGraph=0xc31867300b57;Check(p::Qualify(e,bad,bad)!=nullptr,"prone/crouch rest uncovered");
 s.call.id=p::kWake;s.phase.mode=6;s.phase.qAction=0x85;s.phase.life=0;s.phase.callback=s.owner.image+0x12caf50;s.phase.currentGraph=0x40d8fda5ca06;s.call.frameSerial=13;Check(p::Selected(e,s,s,s.bank.descriptors[2]),"wake140 native cleared cause allowed");
 bad=s;bad.bank.slot++;Check(p::Qualify(e,bad,bad)!=nullptr,"same bank moved slot refused");bad=s;bad.bank.descriptors[2]+=40;Check(p::Qualify(e,bad,bad)!=nullptr,"source array/index alias refused");bad=s;bad.call.frameSerial=1;Check(p::Qualify(e,bad,bad)!=nullptr,"reversed frame refused");
}
void Builder(){b::Image image{};const b::Addresses a{0x140000000,0x150000000,0x7ff700000000};Check(b::Build(a,image),"build near original with far helper islands");Check(image.functions==8,"raw/setup5 and stub2 unwind entries");
 Check(image.bytes[b::kRaw+0xf]==0xe9&&image.bytes[b::kSetup+0xdf]==0xe9,"only safe probe patches");Check(image.bytes[b::kSetup+0xe4]==0x90&&image.bytes[b::kSetup+0xe8]==0x90,"setup overwritten10 bytes exactly");
 Check(image.rawStubEnd<=b::kSetupStub&&image.setupStubEnd<=b::kIslands,"stub bounds");
 bool ordered=true;for(unsigned i=1;i<image.functions;++i){const auto x=Load<b::Function>(image,b::kTable+(i-1)*12),y=Load<b::Function>(image,b::kTable+i*12);ordered=ordered&&x.end<=y.begin;}Check(ordered,"unwind table sorted nonoverlap");
 const auto setup=Load<b::Function>(image,b::kTable+12);const auto trailer=4+((image.bytes[setup.unwind+2]+1)&~1u)*2;Check(Load<std::uint32_t>(image,setup.unwind+trailer+4)==0x490,"GS offset preserved");
 const auto chained=Load<b::Function>(image,b::kTable+24);const unsigned chainTrailer=4+((image.bytes[chained.unwind+2]+1)&~1u)*2;const auto target=Load<b::Function>(image,chained.unwind+chainTrailer);Check(target.begin==setup.begin&&target.end==setup.end&&target.unwind==setup.unwind,"native chain fully relocated");
 const auto rs=Load<b::Function>(image,b::kTable+6*12),ss=Load<b::Function>(image,b::kTable+7*12);Check(image.bytes[rs.unwind]==0x21&&image.bytes[ss.unwind]==0x21,"stub CHAININFO root ownership");
 for(unsigned z=0;z<image.islands;++z)Check(image.bytes[b::kIslands+z*16]==0xff&&image.bytes[b::kIslands+z*16+1]==0x25,"absolute island preserves RAX/args");
 b::Image farImage{};Check(!b::Build({0x140000000,0x7ff000000000,0x7ff700000000},farImage),"far original RIP refuses");Check(!b::Build({0x140000000,0x150000003,1},farImage),"unaligned allocation refuses");
 FILE* file=nullptr;if(!fopen_s(&file,"clone-fixture.bin","wb")&&file){std::fwrite(image.bytes.data(),1,image.bytes.size(),file);std::fclose(file);}
}
struct ProviderFixture {p::Snapshot sample=Good();bool present=true,current=true,after=true;unsigned beforeCalls=0,afterCalls=0;std::uintptr_t selected=0;std::uint64_t onset=31;};
bool Before(void* v,std::uintptr_t dest,std::uintptr_t spec,std::uintptr_t,std::uintptr_t,std::uintptr_t ctx,p::Snapshot& a,p::Snapshot& z,p::OnsetWitness& w) noexcept {
 auto& f=*static_cast<ProviderFixture*>(v);++f.beforeCalls;a=z=f.sample;w=Witness(a);w.nativeRequestSerial=f.onset++;return f.present&&dest==a.call.destination&&spec==a.call.spec&&ctx==a.call.context;
}
bool Current(void* v,const p::Snapshot&,p::Snapshot& a,p::Snapshot& z) noexcept {auto& f=*static_cast<ProviderFixture*>(v);a=z=f.sample;return f.current;}
bool After(void* v,const p::Snapshot&,p::Snapshot& a,p::Snapshot& z,std::uintptr_t& desc) noexcept {auto& f=*static_cast<ProviderFixture*>(v);++f.afterCalls;a=z=f.sample;desc=f.selected;return f.after;}
bool ThrowNative(std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t) {RaiseException(0xe0112233,0,0,nullptr);return false;}
bool CatchDispatch(d::Dispatcher& dispatcher,std::uintptr_t destination,std::uintptr_t spec,std::uintptr_t context) noexcept {
 __try {d::SetupDispatch(dispatcher,&ThrowNative,&ThrowNative,destination,spec,1,2,context);}
 __except(GetExceptionCode()==0xe0112233?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}return false;
}
bool CatchInvoke(mgscoop::standing_selection_clone_win32::CloneSet& set,d::Dispatcher& dispatcher) noexcept {
 __try {mgscoop::standing_selection_clone_win32::InvokeSetup(set,dispatcher,&ThrowNative,1,2,3,4,5);}
 __except(GetExceptionCode()==0xe0112233?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}return false;
}
void Dispatcher(){ProviderFixture f{};d::Dispatcher dispatcher{};unsigned originalCalls=0,cloneCalls=0;std::uintptr_t args[5]{};
 auto original=[&](std::uintptr_t a,std::uintptr_t z,std::uintptr_t c,std::uintptr_t e,std::uintptr_t k){++originalCalls;args[0]=a;args[1]=z;args[2]=c;args[3]=e;args[4]=k;SetLastError(307);return true;};
 auto clone=[&](std::uintptr_t a,std::uintptr_t z,std::uintptr_t c,std::uintptr_t e,std::uintptr_t k){++cloneCalls;args[0]=a;args[1]=z;args[2]=c;args[3]=e;args[4]=k;const auto bank=d::Probe(k,f.sample.call.id,1);f.selected=bank?f.sample.bank.descriptors[1]:0;SetLastError(811);return true;};
 const auto& v=f.sample.call;Check(d::SetupDispatch(dispatcher,original,clone,v.destination,v.spec,0x7f11,0x8f22,v.context)&&originalCalls==1&&!cloneCalls,"default OFF/unbound forwards once");
 dispatcher.activationCurrent=true;dispatcher.provider={&f,&Before,&Current,&After};SetLastError(99);const auto result=d::SetupDispatch(dispatcher,original,clone,v.destination,v.spec,0x7f11,0x8f22,v.context);
 if(d::Compiled()){Check(result&&originalCalls==1&&cloneCalls==1,"qualified one clone invocation");Check(GetLastError()==811,"clone LastError preserved through after validation");Check(args[0]==v.destination&&args[1]==v.spec&&args[2]==0x7f11&&args[3]==0x8f22&&args[4]==v.context,"all five exact opaque arguments retained");
  f.sample.phase.carried=true;Check(d::SetupDispatch(dispatcher,original,clone,v.destination,v.spec,1,2,v.context)&&originalCalls==2&&cloneCalls==1&&!dispatcher.episodes[1].live,"carry invalidates episode forwards stock");
  f.sample=Good();f.current=false;Check(d::SetupDispatch(dispatcher,original,clone,v.destination,v.spec,1,2,v.context)&&cloneCalls==2&&!dispatcher.episodes[1].live,"probe miss stock result does not claim candidate");
  f.sample=Good();f.current=true;auto failure=[&](std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t){++cloneCalls;SetLastError(912);return false;};const auto count=originalCalls;
  Check(!d::SetupDispatch(dispatcher,original,failure,v.destination,v.spec,1,2,v.context)&&originalCalls==count&&GetLastError()==912,"native setup failure never rerun");
  const auto old=cloneCalls;f.onset=dispatcher.lastOnsetSerial[1];Check(d::SetupDispatch(dispatcher,original,clone,v.destination,v.spec,1,2,v.context)&&cloneCalls==old,"cached consumed native onset witness cannot reopen episode");
  f.onset=100;Check(CatchDispatch(dispatcher,v.destination,v.spec,v.context)&&!d::active&&!dispatcher.episodes[1].live,"native SEH propagates with TLS/episode retirement");
  Check(!dispatcher.busy[1].test_and_set(std::memory_order_acquire),"native SEH releases episode mutex exactly once");dispatcher.busy[1].clear(std::memory_order_release);
 }else Check(result&&originalCalls==2&&!cloneCalls,"compiled OFF refuses provider activation");
 unsigned raw=0;const auto id=0xffff000000000009ull;Check(d::RawForward([&](std::uintptr_t e,std::uint64_t x){++raw;return e==0x88&&x==id?0x99u:0u;},0x88,id)==0x99&&raw==1,"raw exact2 args always original");Check(!d::active,"TLS plan retired after dispatch");
}
void Unwind(){b::Image built{};auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,b::kBytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));Check(memory!=nullptr,"fixture allocation");if(!memory)return;
 const auto base=reinterpret_cast<std::uintptr_t>(memory);Check(b::Build({base,base,reinterpret_cast<std::uintptr_t>(&d::Probe)},built),"fixture metadata image");std::memcpy(memory,built.bytes.data(),b::kBytes);
 auto* functions=reinterpret_cast<PRUNTIME_FUNCTION>(memory+b::kTable);Check(RtlAddFunctionTable(functions,built.functions,base)!=FALSE,"register fixture function table");
 std::vector<std::uint64_t> stack(512);const auto incoming=(reinterpret_cast<std::uintptr_t>(stack.data()+300)&~std::uintptr_t(15))+8;
 const std::uint64_t returnPc=0x1234567890abcdef,rbx=0x1111222233334444,rdi=0x9999aaaabbbbcccc;std::memcpy(reinterpret_cast<void*>(incoming),&returnPc,8);
 for(unsigned fi:{0u,6u,1u,7u}){const auto fn=functions[fi];const bool raw=fi==0||fi==6;const auto sp=incoming-(raw?0x28u:0x4f8u);
  if(raw){std::memcpy(reinterpret_cast<void*>(sp+0x30),&rbx,8);std::memcpy(reinterpret_cast<void*>(sp+0x20),&rdi,8);}
  else{std::memcpy(reinterpret_cast<void*>(sp+0x4c0),&rbx,8);std::memcpy(reinterpret_cast<void*>(sp+0x4b8),&rdi,8);for(unsigned z=1;z<=6;++z){const auto val=std::uint64_t(z+100);std::memcpy(reinterpret_cast<void*>(incoming-z*8),&val,8);}}
  CONTEXT context{};context.ContextFlags=CONTEXT_FULL;context.Rsp=sp;context.Rip=base+(fi==0?0x20:fi==1?b::kSetup+0xf0:fn.BeginAddress+(raw?21u:27u));context.Rbx=0;context.Rdi=0;
  void* data=nullptr;DWORD64 frame=0;RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,context.Rip,&functions[fi],&context,&data,&frame,nullptr);
  Check(context.Rip==returnPc&&context.Rsp==incoming+8&&context.Rbx==rbx&&context.Rdi==rdi,raw?"raw/root-stub actual OS unwind":"setup/root-stub actual OS unwind");
 }
 CONTEXT context{};context.ContextFlags=CONTEXT_FULL;context.Rsp=incoming-0x4f8;context.Rip=base+b::kSetupStub+27;void* data=nullptr;DWORD64 frame=0;
 const auto handler=RtlVirtualUnwind(UNW_FLAG_EHANDLER,base,context.Rip,&functions[7],&context,&data,&frame,nullptr);
 std::uint32_t gs=0;if(data)std::memcpy(&gs,data,4);Check(handler&&gs==0x490,"stub chain resolves original GS data without invoking handler");
 Check(RtlDeleteFunctionTable(functions)!=FALSE,"unregister after fixture quiescence");VirtualFree(memory,0,MEM_RELEASE);
}
struct PinsReader {std::uintptr_t game=0;bool damage=false;
 bool operator()(std::uintptr_t at,void* out,std::size_t n) noexcept {
  const auto rva=at-game;using namespace mgscoop::standing_selection_clone_contract;
  for(const auto& pin:mgscoop::qualified_sleep_lookup_contract::sites){if(pin.rva==rva&&pin.size==n){std::memcpy(out,pin.bytes,n);if(damage&&rva==0x29084c0)static_cast<unsigned char*>(out)[0]^=1;return true;}}
  for(const auto& pin:mgscoop::sleep_unique_bind_contract::sites){if(pin.rva==rva&&pin.size==n){std::memcpy(out,pin.bytes,n);return true;}}
  for(const auto& pin:extraPins){if(pin.rva==rva&&pin.size==n){std::memcpy(out,pin.data,n);return true;}}return false;
 }
};
void Allocation(){namespace w=mgscoop::standing_selection_clone_win32;w::CloneSet set{};const auto game=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));PinsReader reader{game,false};
 Check(!w::Prepare(set,game,reader,false),"missing full retail receipt refuses allocation");reader.damage=true;Check(!w::Prepare(set,game,reader,true),"changed original GS unwind refuses");reader.damage=false;
 const auto prepared=w::Prepare(set,game,reader,true);
 if(!d::Compiled()){Check(!prepared&&!set.allocation,"OFF allocator produces no executable clone");return;}
 Check(prepared&&set.ready&&set.registered&&!set.accepting.load(),"ON fixture prepares registered clone with admission CLOSED");if(!prepared)return;
 MEMORY_BASIC_INFORMATION region{};VirtualQuery(set.allocation,&region,sizeof(region));Check(region.Protect==PAGE_EXECUTE_READ,"RX final image has no writable executable pages");
 {w::Lease rejected{set};Check(!rejected.set&&!set.active.load(),"closed clone rejects invocation lease");}
 set.accepting.store(true);{w::Lease held{set};Check(held.set==&set&&set.active.load()==1,"fixture actual invocation reference");Check(!w::CloseQuiescent(set)&&set.allocation&&set.registered&&!set.accepting.load(),"active invocation keeps table/RX image resident while closing");}
 set.accepting.store(true);d::Dispatcher dispatcher{};Check(CatchInvoke(set,dispatcher)&&!set.active.load()&&!d::active,"native exception retires clone reference without swallowing exception");
 Check(w::CloseQuiescent(set)&&!set.allocation&&!set.registered&&!set.ready,"quiescent close unregisters before free");
}
struct RawState {std::uintptr_t engine=0,candidate=0x4444,stock=0x5555;std::uint64_t id=0xfc537fe811d39a38ull;bool present=true,hit=true,stockHit=true;unsigned providers=0,finds=0,fallbacks=0;bool arguments=true;} rawState;
std::uintptr_t FakeProbe(std::uintptr_t engine,std::uint64_t id,unsigned path) noexcept {d::ErrorPreserve error{};++rawState.providers;rawState.arguments=rawState.arguments&&engine==rawState.engine&&id==rawState.id&&!path;SetLastError(901);return rawState.present?rawState.candidate:0;}
std::uintptr_t FakeFind(std::uintptr_t bank,std::uint64_t id) noexcept {++rawState.finds;rawState.arguments=rawState.arguments&&id==rawState.id&&(bank==rawState.candidate||bank==rawState.stock);return(bank==rawState.candidate?rawState.hit:rawState.stockHit)?bank+0x10000000:0;}
std::uintptr_t FakeFallback(std::uintptr_t global,std::uint64_t id,std::uintptr_t opaque) noexcept {++rawState.fallbacks;rawState.arguments=rawState.arguments&&global==0x7777777777777777&&id==rawState.id&&opaque==0x8888888888888888;return 0x9999999999999999;}
void FakeRawExecution(){
 // Execute only scratch-generated raw bytes whose ALL external calls point
 // to the above fixture functions. No retail code/image is loaded or invoked.
 auto* game=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x3000000,MEM_RESERVE,PAGE_NOACCESS));Check(game!=nullptr,"fake source address space reserved");if(!game)return;
 auto* globalPage=static_cast<unsigned char*>(VirtualAlloc(game+0x2c71000,0x1000,MEM_COMMIT,PAGE_READWRITE));Check(globalPage!=nullptr,"fixture RIP global committed");if(!globalPage){VirtualFree(game,0,MEM_RELEASE);return;}
 const std::uintptr_t global=0x7777777777777777;std::memcpy(game+0x2c71960,&global,8);
 auto* memory=static_cast<unsigned char*>(mgscoop::standing_selection_clone_win32::AllocateNear(reinterpret_cast<std::uintptr_t>(game)));Check(memory!=nullptr,"fixture near allocation");if(!memory){VirtualFree(game,0,MEM_RELEASE);return;}
 b::Image image{};const auto base=reinterpret_cast<std::uintptr_t>(memory);Check(b::Build({reinterpret_cast<std::uintptr_t>(game),base,reinterpret_cast<std::uintptr_t>(&FakeProbe)},image),"fake raw clone generated");
 for(unsigned z=0;z<image.islands;++z){const auto at=b::kIslands+z*16;auto target=Load<std::uintptr_t>(image,at+6);
  if(target==reinterpret_cast<std::uintptr_t>(game)+0x1a754c0)target=reinterpret_cast<std::uintptr_t>(&FakeFind);
  else if(target==reinterpret_cast<std::uintptr_t>(game)+0x1a94710)target=reinterpret_cast<std::uintptr_t>(&FakeFallback);
  b::Store(image,at+6,target);}
 std::memcpy(memory,image.bytes.data(),b::kBytes);DWORD old=0;Check(VirtualProtect(memory,b::kBytes,PAGE_EXECUTE_READ,&old)!=FALSE,"fake raw RX");FlushInstructionCache(GetCurrentProcess(),memory,b::kBytes);
 auto* functions=reinterpret_cast<PRUNTIME_FUNCTION>(memory+b::kTable);Check(RtlAddFunctionTable(functions,image.functions,base)!=FALSE,"fake raw unwind registered");
 alignas(16)unsigned char engine[0xb0]{},banks[0x60]{};rawState.engine=reinterpret_cast<std::uintptr_t>(engine);const auto slots=reinterpret_cast<std::uintptr_t>(banks);const std::uint32_t capacity=1;const std::uintptr_t opaque=0x8888888888888888;
 std::memcpy(engine+0x1c,&capacity,4);std::memcpy(engine+0x98,&slots,8);std::memcpy(engine+0xa0,&opaque,8);std::memcpy(banks+0x18,&rawState.stock,8);
 using Raw=std::uintptr_t(*)(std::uintptr_t,std::uint64_t);const auto fn=reinterpret_cast<Raw>(memory);
 for(unsigned scenario=0;scenario<4;++scenario){rawState.providers=rawState.finds=rawState.fallbacks=0;rawState.arguments=true;rawState.present=scenario!=1;rawState.hit=scenario==0;rawState.stockHit=scenario!=3;SetLastError(651);
  const auto result=fn(rawState.engine,rawState.id);const auto expected=scenario==0?rawState.candidate+0x10000000:scenario==3?0x9999999999999999:rawState.stock+0x10000000;
  Check(result==expected&&rawState.arguments&&rawState.providers==1,"actual fake raw hit/miss/fallback ABI");Check(GetLastError()==651,"fake helper LastError preserved by actual bytecode");Check(rawState.fallbacks==(scenario==3?1u:0u),"original fallback invoked once only after complete stock miss");}
 RtlDeleteFunctionTable(functions);VirtualFree(memory,0,MEM_RELEASE);VirtualFree(game,0,MEM_RELEASE);
}
int main(){Policy();Builder();Dispatcher();Unwind();Allocation();FakeRawExecution();std::printf("standing-selection fixtures=%u failed=%u compiled=%u\n",checks,failures,d::Compiled()?1u:0u);return failures?1:0;}
