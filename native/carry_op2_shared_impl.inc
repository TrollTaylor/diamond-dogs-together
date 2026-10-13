// Inside real core after owned op1 implementation. No mod-generated op2.
namespace op2=carry_op2_shared;
struct PoseCarryOp2 {const PoseSharedLease* ticket=nullptr;std::uintptr_t service=0,caller=0,message=0,result=0;std::uint32_t actual=UINT32_MAX;};
thread_local PoseCarryOp2 t_carry_op2;
NativePoseCarryOp2Validator g_pose_op2_sleep_current=nullptr;
NativePoseCarryOp2TreeValidator g_pose_op2_tree_current=nullptr;
bool PoseCarryOp2Active()noexcept{return PoseCarrySharedTicket(t_carry_op2.ticket);}
bool PoseCarryOp2Prefix(carry_owned_handshake::MessagePrefix& packet)noexcept{
 return PoseReader(t_carry_op2.message,&packet,sizeof(packet))&&op2::Source(g_base,t_carry_op2.caller,t_carry_op2.service,t_carry_op2.actual,packet,g_pose_carry_pair.body[0].carrierRecords);
}
bool PoseCarryOp2Query(NativePoseCarryOp2Context& out)noexcept{
 out={};const auto seq=static_cast<std::uint32_t>(g_window_seq);const auto tid=GetCurrentThreadId();carry_owned_handshake::MessagePrefix packet{};
 if(g_pose_bindings.enabled||!PoseWorker()||!PoseRequired()||!g_pose_installed||g_pose_fault||g_shutdown||g_pose_clear||PoseOwned()||t_pose.opened||t_local_carry.opened||
  !seq||(seq&1u)||!PoseCarrySharedTicket(t_carry_op2.ticket)||!g_pose_force_a_confirmed||!g_pose_force_a_submit_nonce||
  g_pose_force_a_serial!=g_pose_owners.carrySerial||g_pose_owners.held!=(holds::sleep|holds::carry)||g_pose_owners.pending||!g_pose_held||
  !PoseCarryOp2Prefix(packet)||!NativePoseCarrySenderHooksLive()||!NativePoseWholePlayerLifetimeLive()||!NativePoseLocalCacheIsolationLive()||
  !PoseParticipantsLive()||!PoseMessageCurrent()||!PoseRouteCurrent()||!PoseQueueCurrent())return false;
 cop::Intent intent{};NativePoseSubmittedCarryContext receipt{};
 if(!TryAcquireSRWLockShared(&g_pose_carry_intent_lock))return false;intent=g_pose_carry_intent;receipt=g_pose_carry_intent_receipt;ReleaseSRWLockShared(&g_pose_carry_intent_lock);
 const auto now=GetTickCount64();
 if(!cop::Current(intent,g_pose_force_a_submit_nonce,g_pose_owners.carrySerial,g_pose_generation,g_pose_epoch,now)||g_pose_deferred_copy.phase!=dcopy::Phase::empty||
  !dcopy::Fresh(PoseDeferredKey(intent),g_pose_force_a_submitted_at,now)||receipt.submitNonce!=intent.submission||receipt.carrySerial!=intent.carry||
  receipt.armGeneration!=g_pose_generation||receipt.armEpoch!=g_pose_epoch||receipt.submittedAt!=g_pose_force_a_submitted_at||receipt.handlerRva!=0x11268b0||
  !receipt.sharedSerial||!receipt.thread||!receipt.sequence||(receipt.sequence&1u)||!receipt.sleepHeld||!receipt.carryHeld)return false;
 cp::Pair pair{},late{};auto reader=PoseReader;pi::Arm arm{},after{};std::uintptr_t vt=0;std::uint32_t real=1,selected=1;
 std::uint16_t target=0xffff,previous=0xffff,flags=0;std::uint8_t actual=0xff,offline=0;
 if(!ReadArm(arm)||!arm.enabled||!arm.native||arm.holdlog||arm.cam!=PartnerInputCam::sandbox||arm.gen!=g_pose_generation||arm.rates_epoch!=g_pose_epoch||arm.P!=g_pose_player||arm.go!=g_pose_go||arm.blk1!=g_pose_block||
  !cp::ReadPair(reader,g_base,g_pose_player,pair)||!cp::Same(pair,g_pose_carry_pair)||!PoseOp0BothBirthsCurrent(pair)||!PoseCarryNativeRest(pair)||
  receipt.player!=pair.player||receipt.go!=pair.go||receipt.data!=pair.data||receipt.block1!=pair.body[1].block||receipt.state1!=pair.body[1].state||
  receipt.modelCS1!=pair.body[1].cs||receipt.modelEntry1!=pair.body[1].entry||receipt.birth1!=g_pose_bound_birth||receipt.carrierBirth0!=g_pose_carry_birth0||
  !PoseRead(pair.player+0x214,real)||real||!PoseRead(pair.player+0x218,selected)||selected||!PoseRead(t_carry_op2.service,vt)||vt!=g_base+cop::serviceVt||
  !PoseRead(pair.body[0].carrierRecords+0x78,target)||target!=1||!PoseRead(pair.body[0].carrierRecords+0x7c,previous)||previous!=1||
  !PoseRead(pair.body[0].carrierRecords+0x6a2,flags)||!(flags&0x1000u)||!cop::ReadCarrierTag(reader,pair.body[0].carrierRecords,actual,offline)||actual||offline!=1||
  !cp::ReadPair(reader,g_base,pair.player,late)||!cp::Same(pair,late)||!cp::CoherentCallbacks(pair.body[0],late.body[0])||!cp::CoherentCallbacks(pair.body[1],late.body[1])||
  !PoseOp0BothBirthsCurrent(late)||!PoseCarryNativeRest(late)||!ReadArm(after)||!after.enabled||!after.native||after.holdlog||after.cam!=arm.cam||after.gen!=arm.gen||after.rates_epoch!=arm.rates_epoch||
  after.P!=arm.P||after.go!=arm.go||after.blk1!=arm.blk1||static_cast<std::uint32_t>(g_window_seq)!=seq||!PoseCarrySharedTicket(t_carry_op2.ticket)||!PoseQueueCurrent())return false;
 cop::Intent end{};if(!TryAcquireSRWLockShared(&g_pose_carry_intent_lock))return false;end=g_pose_carry_intent;ReleaseSRWLockShared(&g_pose_carry_intent_lock);
 if(!end.accepted||end.queued||!dcopy::Same(PoseDeferredKey(intent),PoseDeferredKey(end))||g_pose_fault)return false;
 out.image=g_base;out.player=pair.player;out.go=pair.go;out.data=pair.data;out.carrierRecord=pair.body[0].carrierRecords;out.message=t_carry_op2.message;out.result=t_carry_op2.result;out.service=t_carry_op2.service;
 for(unsigned id=0;id<2;++id){out.block[id]=pair.body[id].block;out.state[id]=pair.body[id].state;out.modelCS[id]=pair.body[id].cs;out.modelEntry[id]=pair.body[id].entry;}
 out.submission=receipt;out.intentNonce=intent.intent;out.intentAt=intent.at;out.birth0=g_pose_carry_birth0;out.birth1=g_pose_bound_birth;out.readSerial=t_carry_op2.ticket->serial;
 out.thread=tid;out.sequence=seq;out.actual=1;out.batch=partner_defer::InBatch();return true;
}
std::int32_t* PoseCarrySharedOp2(void* service,std::int32_t* result,std::uint32_t actual,void* message,std::uintptr_t caller,const PoseSharedLease& read)noexcept{
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;auto* returned=result;const auto saved=t_carry_op2;
 t_carry_op2={&read,reinterpret_cast<std::uintptr_t>(service),caller,reinterpret_cast<std::uintptr_t>(message),reinterpret_cast<std::uintptr_t>(result),actual};
 __try{
  NativePoseCarryOp2Context before{},after{};unsigned char packet[24]{},final[24]{},records[0x6c0]{},recordsAfter[0x6c0]{};std::int32_t output=INT32_MAX;
  if(!g_pose_op2_sleep_current||!g_pose_op2_tree_current||!PoseCarryOp2Query(before)||!g_pose_op2_sleep_current(before.submission.submittedSleep)||!g_pose_op2_tree_current(before)||
   !PoseReader(before.message,packet,sizeof(packet))||!PoseReader(before.carrierRecord,records,sizeof(records)))PoseFail("pose_carry_op2_preflight");
  else{
   SetLastError(incoming);returned=g_pose_carry_dispatch(service,result,actual,message);outgoing=GetLastError();
   carry_owned_handshake::MessagePrefix prefixBefore{},prefixAfter{};std::memcpy(&prefixBefore,packet,24);
   if(!PoseRead(before.result,output)||!PoseReader(before.message,final,sizeof(final))||
    !PoseReader(before.carrierRecord,recordsAfter,sizeof(recordsAfter))||(std::memcpy(&prefixAfter,final,24),!op2::NoMetadata(prefixBefore,prefixAfter,records,recordsAfter,reinterpret_cast<std::uintptr_t>(returned),before.result,output))||!PoseCarryOp2Query(after)||
    before.intentNonce!=after.intentNonce||before.readSerial!=after.readSerial||before.thread!=after.thread||before.sequence!=after.sequence||
    !g_pose_op2_sleep_current(after.submission.submittedSleep)||!g_pose_op2_tree_current(after))PoseFail("pose_carry_op2_post");
  }
 }__finally{t_carry_op2=saved;SetLastError(outgoing);}return returned;
}
bool PoseCarrySharedOp2Recipient(void* phase,std::uint32_t actual,std::int32_t* result,void* message,const PoseSharedLease& read)noexcept{
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;NativePoseCarryOp2Context before{},after{};unsigned char packet[24]{},final[24]{},records[0x60]{},recordsAfter[0x60]{};std::int32_t oldResult=0,newResult=0;
 if(!PoseCarrySharedTicket(&read)||actual!=1||reinterpret_cast<std::uintptr_t>(phase)!=g_pose_carry_pair.body[1].recipient+0x30||
  reinterpret_cast<std::uintptr_t>(result)!=t_carry_op2.result||reinterpret_cast<std::uintptr_t>(message)!=t_carry_op2.message||
  !g_pose_op2_sleep_current||!g_pose_op2_tree_current||!PoseCarryOp2Query(before)||!g_pose_op2_sleep_current(before.submission.submittedSleep)||!g_pose_op2_tree_current(before)||
  !PoseReader(before.message,packet,sizeof(packet))||!PoseRead(before.result,oldResult)||!PoseReader(g_pose_carry_pair.body[1].recipientRecords,records,sizeof(records))){PoseFail("pose_carry_op2_recipient_preflight");SetLastError(incoming);return false;}
 SetLastError(incoming);g_pose_carry_recipient_message(phase,actual,result,message);outgoing=GetLastError();
 const bool same=PoseReader(before.message,final,sizeof(final))&&!std::memcmp(packet,final,sizeof(packet))&&PoseRead(before.result,newResult)&&newResult==oldResult&&
  PoseReader(g_pose_carry_pair.body[1].recipientRecords,recordsAfter,sizeof(recordsAfter))&&!std::memcmp(records,recordsAfter,sizeof(records))&&PoseCarryOp2Query(after)&&
  after.intentNonce==before.intentNonce&&after.thread==before.thread&&after.sequence==before.sequence&&after.readSerial==before.readSerial&&g_pose_op2_sleep_current(after.submission.submittedSleep);
 if(!same)PoseFail("pose_carry_op2_recipient_post");SetLastError(outgoing);return same&&!g_pose_fault;
}
