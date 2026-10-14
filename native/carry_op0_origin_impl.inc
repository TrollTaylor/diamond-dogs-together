// Include inside the actual core anonymous namespace after PoseOriginalCode,
// with reviewed ForceA invoker ledger present. No new global native override.
namespace cop=carry_op0_origin;
// Reserved-only ledger until the separate fully-qualified invoker is composed.
// No callback, debug command or queue writer in this stage populates it.
std::uint64_t g_pose_force_a_serial=0;bool g_pose_force_a_binding_seen=false;
std::uint64_t g_pose_force_a_submit_nonce=0,g_pose_force_a_submitted_at=0;
sleep_partner_test::CarryLeaseProof g_pose_force_a_sleep{};bool g_pose_force_a_confirmed=false;
using PoseCarryDispatchFn=std::int32_t*(*)(void*,std::int32_t*,std::uint32_t,void*);
using PoseCarryRecipientMessageFn=void(*)(void*,std::uint32_t,std::int32_t*,void*);
PoseCarryDispatchFn g_pose_carry_dispatch=nullptr;
PoseCarryRecipientMessageFn g_pose_carry_recipient_message=nullptr;
struct PoseCarrySource {const PoseSharedLease* ticket=nullptr;std::uintptr_t service=0,caller=0,message=0,result=0;std::uint32_t target=UINT32_MAX;};
struct PoseCarryOp0 {const PoseSharedLease* ticket=nullptr;std::uintptr_t phase=0,message=0,result=0;std::uint32_t actual=UINT32_MAX;};
thread_local PoseCarrySource t_carry_source;
thread_local PoseCarryOp0 t_carry_op0;
using PoseSubmittedSleepCurrentFn=bool(*)(const sleep_partner_test::CarryLeaseProof&) noexcept;
PoseSubmittedSleepCurrentFn g_pose_submitted_sleep_current=nullptr;
SRWLOCK g_pose_carry_intent_lock=SRWLOCK_INIT;
cop::Intent g_pose_carry_intent{};std::uint64_t g_pose_carry_intent_nonce=0;
NativePoseSubmittedCarryContext g_pose_carry_intent_receipt{};
bool PoseCarrySharedTicket(const PoseSharedLease* value)noexcept{
 const auto tid=GetCurrentThreadId();return value&&value->tracked&&value->thread==tid&&value->slot<32&&value->serial&&
 t_readers.serial[value->slot]==value->serial&&g_pose_gate.OwnsRead(tid,t_readers.tickets[value->slot]);
}
bool PoseCarryOriginCode(std::uintptr_t& mismatch)noexcept{
 mismatch=0;unsigned char bytes[8192]{};
 for(const auto& s:carry_op0_origin_contract::sites){if(s.rva>0xa01d000||s.size>0xa01d000-s.rva||s.size>sizeof(bytes)||
 !PoseOriginalCode(g_base+s.rva,bytes,s.size)||std::memcmp(bytes,s.bytes,s.size)){mismatch=s.rva;return false;}}
 for(const auto& s:carry_op0_origin_contract::slots){std::uintptr_t got=0;if(s.rva>0xa01d000||8>0xa01d000-s.rva||!PoseRead(g_base+s.rva,got)||got!=g_base+s.target){mismatch=s.rva;return false;}}
 return true;
}
bool PoseCarryNativeRest(const cp::Pair& pair)noexcept{
 const auto& proof=g_pose_force_a_sleep;const auto& body=pair.body[1];std::uintptr_t pointer=0,vt=0;std::uint16_t action=0;
 std::uint32_t bits=0,life=0;std::uint8_t mode=0,requested=0;float health=0,max=0;
 if(proof.player!=pair.player||proof.go!=pair.go||proof.data!=pair.data||proof.block1!=body.block||proof.state1!=body.state||
 proof.modelCS1!=body.cs||proof.modelEntry1!=body.entry||proof.birth1!=g_pose_bound_birth||proof.carrierBirth0!=g_pose_carry_birth0||
 !PoseRead(pair.data+0x18,pointer)||!pointer||!PoseRead(pointer+2,action)||action!=0x84||
 !PoseRead(pair.data+8,pointer)||!pointer||pointer+0x4c!=proof.life||!PoseRead(proof.life+0x34,life)||life!=2||
 !PoseRead(proof.life+0x24,health)||!std::isfinite(health)||health<=0||!PoseRead(proof.life+0x28,max)||!std::isfinite(max)||max<=0||health>max||
 !PoseRead(proof.plugin,vt)||vt!=g_base+0x23d5c18||!PoseRead(proof.plugin+8,pointer)||pointer!=body.block||
 !PoseRead(proof.plugin+0x38,pointer)||pointer!=proof.node||!PoseRead(proof.plugin+0x78,pointer)||pointer!=proof.records||
 !PoseRead(proof.node,vt)||vt!=g_base+0x2306628||!PoseRead(proof.node+0x38,pointer)||pointer!=proof.plugin+0x30||
 !PoseRead(proof.node+8,pointer)||pointer!=proof.bitmap||!PoseRead(pointer,bits)||!(bits&1u)||
 !PoseRead(proof.records+0x10,mode)||mode!=5||!PoseRead(proof.records+0x11,requested)||requested||
 !PoseRead(proof.records,pointer)||pointer!=g_base+0x12caa10||
 !PoseRead(body.entry+0x90,pointer)||!pointer||!PoseRead(pointer,pointer)||pointer!=proof.graph||
 !PoseRead(pointer,vt)||vt!=g_base+0x250ba50||!PoseRead(pointer+0x10,vt)||vt!=g_base+0x250bc00)return false;
 return true;
}
bool PoseOp0BothBirthsCurrent(const cp::Pair& pair)noexcept{
 birth::Receipt current0{},current1{};AcquireSRWLockShared(&g_pose_birth_lock);current0=PoseObservedBirthLocked(0);current1=PoseObservedBirthLocked(1);ReleaseSRWLockShared(&g_pose_birth_lock);
 return dual_parts::BirthsCurrent(pair,current0,current1,g_pose_carry_birth0,g_pose_bound_birth);
}
bool PoseSubmittedCarryQuery(NativePoseSubmittedCarryContext& out)noexcept{
 out={};const auto tid=GetCurrentThreadId();const auto seq=static_cast<std::uint32_t>(g_window_seq);
 if(g_pose_bindings.enabled||!PoseWorker()||!PoseRequired()||!g_pose_installed||g_pose_fault||g_shutdown||g_pose_clear||
 PoseOwned()||t_pose.opened||t_local_carry.opened||!seq||(seq&1u)||!g_pose_force_a_confirmed||!g_pose_force_a_serial||
 g_pose_force_a_serial!=g_pose_owners.carrySerial||!g_pose_force_a_submit_nonce||!g_pose_force_a_submitted_at||
 g_pose_owners.held!=(holds::sleep|holds::carry)||g_pose_owners.pending||!g_pose_held||
 !PoseCarrySharedTicket(t_carry_source.ticket)||!PoseCarrySharedTicket(t_carry_op0.ticket)||
 t_carry_source.message!=t_carry_op0.message||t_carry_source.result!=t_carry_op0.result||t_carry_op0.actual!=1||
 !NativePoseDispatchInterfaceLifetimeLive()||!NativePoseWholePlayerLifetimeLive()||!NativePoseLocalCacheIsolationLive()||
 !PoseParticipantsLive()||!PoseMessageCurrent()||!PoseRouteCurrent())return false;
 const auto now=GetTickCount64();if(now<g_pose_force_a_submitted_at||now-g_pose_force_a_submitted_at>750)return false;
 pi::Arm arm{},fresh{};if(!ReadArm(arm)||!arm.enabled||!arm.native||arm.holdlog||arm.cam!=PartnerInputCam::sandbox||arm.gen!=g_pose_generation||arm.rates_epoch!=g_pose_epoch||arm.P!=g_pose_player||arm.go!=g_pose_go||arm.blk1!=g_pose_block)return false;
 cp::Pair pair{},after{};auto reader=PoseReader;carry_owned_handshake::MessagePrefix packet{};std::uintptr_t vt=0;
 std::uint32_t real=1,selected=1;std::uint16_t target=0xffff,previous=0xffff,flags=0;std::uint8_t actual=0xff,offline=0;
 if(!cp::ReadPair(reader,g_base,g_pose_player,pair)||!cp::Same(pair,g_pose_carry_pair)||!PoseCarryBirth0Current(pair)||
 !PoseOp0BothBirthsCurrent(pair)||!PoseRead(pair.player+0x214,real)||real||!PoseRead(pair.player+0x218,selected)||selected||
 !PoseReader(t_carry_source.message,&packet,sizeof(packet))||!cop::NaturalSource(g_base,t_carry_source.caller,t_carry_source.service,t_carry_source.target,packet,pair.body[0].carrierRecords)||
 !PoseRead(t_carry_source.service,vt)||vt!=g_base+cop::serviceVt||t_carry_op0.phase!=pair.body[1].recipient+0x30||
 !PoseRead(pair.body[0].carrierRecords+0x78,target)||target!=1||!PoseRead(pair.body[0].carrierRecords+0x7c,previous)||previous!=1||
 !cop::ReadCarrierTag(reader,pair.body[0].carrierRecords,actual,offline)||actual||offline!=1||
 !PoseRead(pair.body[0].carrierRecords+0x6a2,flags)||!(flags&0x1000u)||!PoseCarryNativeRest(pair)||
 !cp::ReadPair(reader,g_base,pair.player,after)||!cp::Same(pair,after)||!cp::CoherentCallbacks(pair.body[0],after.body[0])||
 !cp::CoherentCallbacks(pair.body[1],after.body[1])||!PoseCarryNativeRest(after)||!PoseOp0BothBirthsCurrent(after)||
 !ReadArm(fresh)||!fresh.enabled||!fresh.native||fresh.holdlog||fresh.cam!=PartnerInputCam::sandbox||fresh.gen!=arm.gen||fresh.rates_epoch!=arm.rates_epoch||fresh.P!=arm.P||fresh.go!=arm.go||fresh.blk1!=arm.blk1||
 static_cast<std::uint32_t>(g_window_seq)!=seq||!PoseCarrySharedTicket(t_carry_source.ticket)||!PoseCarrySharedTicket(t_carry_op0.ticket)||g_pose_fault)return false;
 out.image=g_base;out.player=pair.player;out.go=pair.go;out.data=pair.data;out.block1=pair.body[1].block;out.state1=pair.body[1].state;
 out.modelCS1=pair.body[1].cs;out.modelEntry1=pair.body[1].entry;out.armGeneration=g_pose_generation;out.armEpoch=g_pose_epoch;
 out.thread=tid;out.sequence=seq;out.handlerRva=0x11268b0;out.birth1=g_pose_bound_birth;out.carrierBirth0=g_pose_carry_birth0;
 out.carrySerial=g_pose_owners.carrySerial;out.sharedSerial=t_carry_op0.ticket->serial;out.submitNonce=g_pose_force_a_submit_nonce;
 out.submittedAt=g_pose_force_a_submitted_at;out.sleepHeld=true;out.carryHeld=true;out.submittedSleep=g_pose_force_a_sleep;return true;
}
// Insert inside actual core anonymous namespace BEFORE PoseCarryDispatch,
// after existing PoseCarrySharedTicket/ReadArm/participants/birth helpers.
// Observes its REAL shared reader; never substitutes a Lua or writer scope.
std::atomic<const NativeHorseFlowCallbacks*> g_pose_horse_flow_callbacks{nullptr};
NativeHorseFlowCallbacks g_pose_horse_flow_storage{};
SRWLOCK g_pose_horse_flow_registration=SRWLOCK_INIT;
bool PoseHorseFlowOwner(const PoseSharedLease& ticket,NativeHorseFlowOwner& out) noexcept {
    out={};const DWORD incoming=GetLastError();
    const auto finish=[&](bool ok) noexcept {SetLastError(incoming);return ok;};
    const auto sequence=static_cast<std::uint32_t>(g_window_seq);const auto thread=GetCurrentThreadId();
    if(g_pose_bindings.enabled||!PoseWorker()||!g_pose_installed||!g_pose_original_view.load(std::memory_order_acquire)||
       g_pose_fault||g_faulted||g_shutdown||PoseOwned()||(sequence&1u)||partner_defer::InBatch()||
       !PoseCarrySharedTicket(&ticket)||!NativePoseWholePlayerLifetimeLive()||!PoseParticipantsLive()||!PoseRoutePatchesLive())return finish(false);
    pi::Arm arm{},lateArm{};cp::Pair first{},last{};auto reader=PoseReader;
    if(!ReadArm(arm)||!arm.enabled||!arm.native||arm.holdlog||!arm.gen||!arm.rates_epoch||
       !cp::ReadPair(reader,g_base,arm.P,first)||first.go!=arm.go||first.body[1].block!=arm.blk1||first.body[1].state!=arm.ps1)return finish(false);
    birth::Receipt births[2]{},lateBirths[2]{};std::uint64_t lifecycle=0,lateLifecycle=0;
    AcquireSRWLockShared(&g_pose_birth_lock);births[0]=PoseObservedBirthLocked(0);births[1]=PoseObservedBirthLocked(1);lifecycle=g_pose_birth_epoch;ReleaseSRWLockShared(&g_pose_birth_lock);
    if(!lifecycle||!native_pose_birth_census::Current(first,births[0],births[1])||
       !ReadArm(lateArm)||lateArm.enabled!=arm.enabled||lateArm.native!=arm.native||lateArm.holdlog!=arm.holdlog||
       lateArm.gen!=arm.gen||lateArm.rates_epoch!=arm.rates_epoch||lateArm.P!=arm.P||lateArm.go!=arm.go||
       lateArm.blk1!=arm.blk1||lateArm.ps1!=arm.ps1||!cp::ReadPair(reader,g_base,arm.P,last)||!cp::Same(first,last)||
       !cp::CoherentCallbacks(first.body[0],last.body[0])||!cp::CoherentCallbacks(first.body[1],last.body[1]))return finish(false);
    AcquireSRWLockShared(&g_pose_birth_lock);lateBirths[0]=PoseObservedBirthLocked(0);lateBirths[1]=PoseObservedBirthLocked(1);lateLifecycle=g_pose_birth_epoch;ReleaseSRWLockShared(&g_pose_birth_lock);
    if(lifecycle!=lateLifecycle||births[0].serial!=lateBirths[0].serial||births[1].serial!=lateBirths[1].serial||
       !native_pose_birth_census::Current(last,lateBirths[0],lateBirths[1])||sequence!=static_cast<std::uint32_t>(g_window_seq)||
       partner_defer::InBatch()||!PoseCarrySharedTicket(&ticket)||g_pose_fault||g_faulted||g_shutdown)return finish(false);
    auto& b=out.births;b.image=g_base;b.player=first.player;b.data=first.data;b.state=first.scriptState;
    b.lifecycle=lifecycle;b.generation=arm.gen;b.sceneEpoch=arm.rates_epoch;b.thread=thread;b.sequence=sequence;
    for(unsigned k=0;k<2;++k){b.birth[k]=births[k].serial;b.block[k]=first.body[k].block;b.ps[k]=first.body[k].state;
        b.cs[k]=first.body[k].cs;b.entry[k]=first.body[k].entry;out.carrierRecord[k]=first.body[k].carrierRecords;out.recipientRecord[k]=first.body[k].recipientRecords;}
    out.sharedSerial=ticket.serial;out.at=GetTickCount64();return finish(true);
}
bool PoseHorseFlowPeek(std::uintptr_t caller,std::uint32_t target,void* message) noexcept {
    // Cheap filter first: no heavy current-pair sampler for unrelated enemy,
    // scene, ordinary Player or Vehicle messages passing through this entry.
    if((target>>9)!=13||(caller!=g_base+0x1136f57&&caller!=g_base+0x113a814))return false;
    std::uint64_t hash=0;std::uint32_t kind=0;const auto at=reinterpret_cast<std::uintptr_t>(message);
    return PoseRead(at,hash)&&hash==0x5167c60042b5ull&&PoseRead(at+12,kind)&&
        (caller==g_base+0x1136f57?kind==1:kind==2);
}

std::int32_t* PoseCarryDispatch(void* service,std::int32_t* result,std::uint32_t actual,void* message)noexcept{
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;auto* returned=result;
 const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
 if(PoseOwned()){
  if(g_pose_owners.held&holds::carry)return PoseCarryOwnedOp1(service,result,actual,message,caller);
  // No Carry-held dispatch is admitted from this selected1 writer yet.
  // Reciprocal op1 requires a separate exact queued-request receipt.
  if(cop::DispatchRoute(cop::Domain::owned,(g_pose_owners.held&holds::carry)!=0,false,
     caller==g_base+cop::senderReturn,actual)==cop::Route::refuse){
   PoseFail("pose_carry_sender_owned_unsupported");SetLastError(incoming);return result;
  }
  SetLastError(incoming);return g_pose_carry_dispatch(service,result,actual,message);
 }
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return result;}
 const auto saved=t_carry_source;
 __try{
  // Submission/hold state is sampled only after the actual reader is acquired.
  // Uninstalled cold forwarding does not inspect those mutable ledgers.
  const bool carryHeld=read.tracked&&(g_pose_owners.held&holds::carry)!=0;
  if(carryHeld&&caller==g_base+0x113c10e){SetLastError(incoming);returned=PoseCarrySharedOp2(service,result,actual,message,caller,read);outgoing=GetLastError();}
  else {
  const auto route=cop::DispatchRoute(read.tracked?cop::Domain::shared:cop::Domain::none,
   carryHeld,read.tracked&&g_pose_force_a_confirmed,caller==g_base+cop::senderReturn,actual);
  if(route==cop::Route::refuse)PoseFail("pose_carry_foreign_dispatch");
  else{
   if(route==cop::Route::origin)t_carry_source={&read,reinterpret_cast<std::uintptr_t>(service),caller,
    reinterpret_cast<std::uintptr_t>(message),reinterpret_cast<std::uintptr_t>(result),actual};
   const auto* horseFlow=g_pose_horse_flow_callbacks.load(std::memory_order_acquire);
   NativeHorseFlowOwner horseBefore{};const NativeHorseFlowCall horseCall{caller,reinterpret_cast<std::uintptr_t>(service),
    reinterpret_cast<std::uintptr_t>(message),reinterpret_cast<std::uintptr_t>(result),0,actual};
   const bool horseCandidate=horseFlow&&horseFlow->before&&horseFlow->after&&horseFlow->refused&&PoseHorseFlowPeek(caller,actual,message);
   const bool horseObserved=horseCandidate&&PoseHorseFlowOwner(read,horseBefore);
   if(horseObserved)horseFlow->before(horseBefore,horseCall);
   else if(horseCandidate)horseFlow->refused(horseCall);
   SetLastError(incoming);returned=g_pose_carry_dispatch(service,result,actual,message);outgoing=GetLastError();
   if(horseObserved){NativeHorseFlowOwner horseAfter{};auto completed=horseCall;completed.returned=reinterpret_cast<std::uintptr_t>(returned);
    const bool horseCurrent=PoseHorseFlowOwner(read,horseAfter);horseFlow->after(horseCurrent?&horseAfter:nullptr,completed);SetLastError(outgoing);}

   if(route==cop::Route::origin&&returned!=result)PoseFail("pose_carry_dispatch_result");
  }
  }
 }__finally{t_carry_source=saved;PoseExitShared(read);SetLastError(outgoing);}return returned;
}
void PoseCarryRecipientMessage(void* phase,std::uint32_t actual,std::int32_t* result,void* message)noexcept{
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;
 if(PoseOwned()){
  if(g_pose_owners.held&holds::carry){PoseCarryOwnedRecipientNoop(phase,actual,result,message);return;}
  if(cop::RecipientRoute(cop::Domain::owned,(g_pose_owners.held&holds::carry)!=0,false,actual)==cop::Route::refuse){
   PoseFail("pose_carry_recipient_owned_unsupported");SetLastError(incoming);return;
  }
  SetLastError(incoming);g_pose_carry_recipient_message(phase,actual,result,message);return;
 }
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 const auto saved=t_carry_op0;
 __try{
  const bool carryHeld=read.tracked&&(g_pose_owners.held&holds::carry)!=0;
  if(carryHeld&&PoseCarryOp2Active()){SetLastError(incoming);PoseCarrySharedOp2Recipient(phase,actual,result,message,read);outgoing=GetLastError();}
  else {
  const auto route=cop::RecipientRoute(read.tracked?cop::Domain::shared:cop::Domain::none,
   carryHeld,PoseCarrySharedTicket(t_carry_source.ticket),actual);
  if(route==cop::Route::stock){
   SetLastError(incoming);g_pose_carry_recipient_message(phase,actual,result,message);outgoing=GetLastError();
  }else if(route==cop::Route::refuse)PoseFail("pose_carry_foreign_recipient");
  else{
  t_carry_op0={&read,reinterpret_cast<std::uintptr_t>(phase),reinterpret_cast<std::uintptr_t>(message),reinterpret_cast<std::uintptr_t>(result),actual};
  NativePoseSubmittedCarryContext before{},after{};cp::Pair pair{};auto reader=PoseReader;std::uintptr_t model=0,transformSlot=0;
  unsigned char initial[0x6c0]{},final[0x6c0]{},matrix[64]{};std::uintptr_t mismatch=0;std::int32_t output=INT32_MAX;
  const bool ready=g_pose_submitted_sleep_current&&PoseCarryOriginCode(mismatch)&&PoseSubmittedCarryQuery(before)&&
   g_pose_submitted_sleep_current(before.submittedSleep)&&cp::ReadPair(reader,g_base,before.player,pair)&&
   PoseRead(g_base+0x22e6070+0xb0,transformSlot)&&transformSlot==g_base+0xaf1aa0&&
   PoseRead(pair.body[1].entry+0x68,model)&&model&&!(model&15u)&&cp::Range(model,0x80)&&
   PoseReader(model+0x40,matrix,sizeof(matrix))&&PoseReader(pair.body[0].carrierRecords,initial,sizeof(initial))&&
   PoseLocalFlagMapWritable(reinterpret_cast<std::uintptr_t>(result),sizeof(*result))&&
   PoseLocalFlagMapWritable(pair.body[0].carrierRecords+0x1d0,64);
  if(!ready){PoseFail("pose_carry_op0_preflight");}
  else{
   SetLastError(incoming);g_pose_carry_recipient_message(phase,actual,result,message);outgoing=GetLastError();
   const bool accepted=PoseRead(reinterpret_cast<std::uintptr_t>(result),output)&&
    PoseReader(pair.body[0].carrierRecords,final,sizeof(final))&&cop::CopyOnly(initial,final,matrix,output)&&
    PoseSubmittedCarryQuery(after)&&after.submitNonce==before.submitNonce&&after.sharedSerial==before.sharedSerial&&
    after.thread==before.thread&&after.sequence==before.sequence&&g_pose_submitted_sleep_current(before.submittedSleep);
   if(!accepted)PoseFail("pose_carry_op0_post");
   else if(!TryAcquireSRWLockExclusive(&g_pose_carry_intent_lock))PoseFail("pose_carry_intent_busy");
   else{
    ++g_pose_carry_intent_nonce;if(!g_pose_carry_intent_nonce)++g_pose_carry_intent_nonce;
    std::uint8_t prior=0;
    const auto now=GetTickCount64();
    const bool repeated=cop::SameCycle(g_pose_carry_intent,before.submitNonce,before.carrySerial,before.armGeneration,before.armEpoch);
    const bool created=!g_pose_carry_intent.accepted&&PoseRead(pair.body[1].recipientRecords+0x12,prior)&&
     cop::Reserve(g_pose_carry_intent,before.submitNonce,g_pose_carry_intent_nonce,now,before.carrySerial,before.armGeneration,before.armEpoch,prior);
    const bool sealed=repeated||created;
    if(created)g_pose_carry_intent_receipt=before;ReleaseSRWLockExclusive(&g_pose_carry_intent_lock);
    if(!sealed)PoseFail("pose_carry_intent_duplicate");
   }
  }
  }
  }
 }__finally{t_carry_op0=saved;PoseExitShared(read);SetLastError(outgoing);}
}
