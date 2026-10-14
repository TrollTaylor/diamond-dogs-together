// Included after actual origin+Q+deferred implementations. No standalone call
// enters this path: the game must invoke exact owned natural11285D0 op1.
namespace op1=carry_op1_owned;
bool PoseQueuedCode(std::uintptr_t& mismatch)noexcept{
 mismatch=0;unsigned char bytes[8192]{};
 for(const auto& p:carry_op1_owned_contract::sites){if(p.rva>0xa01d000||p.size>0xa01d000-p.rva||p.size>sizeof(bytes)||!PoseOriginalCode(g_base+p.rva,bytes,p.size)||std::memcmp(bytes,p.bytes,p.size)){mismatch=p.rva;return false;}}
 for(const auto& p:carry_op1_owned_contract::slots){std::uintptr_t got=0;if(!PoseRead(g_base+p.rva,got)||got!=g_base+p.target){mismatch=p.rva;return false;}}return true;
}
struct PoseCarryOp1 {std::uintptr_t service=0,caller=0,message=0,result=0;std::uint32_t target=UINT32_MAX;bool active=false,reply=false;};
thread_local PoseCarryOp1 t_carry_op1;
NativePoseQueuedCarryValidator g_pose_queued_sleep_current=nullptr;
NativePoseCarryTreeValidator g_pose_carry_tree_current=nullptr;
std::uint64_t g_pose_carry_ack_intent=0;
bool PoseQueuedNative(const cp::Pair& pair,std::uint8_t& current,std::uint8_t& next,std::uint16_t& action)noexcept{
 const auto& proof=g_pose_force_a_sleep;const auto& b=pair.body[1];std::uintptr_t pointer=0,vt=0;std::uint32_t life=0,bits=0;std::uint8_t mode=0,requested=0;
 float health=0,max=0;std::uint8_t committed=0;unsigned char queued[0x80]{};
 if(proof.player!=pair.player||proof.go!=pair.go||proof.data!=pair.data||proof.block1!=b.block||proof.state1!=b.state||proof.modelCS1!=b.cs||proof.modelEntry1!=b.entry||
  proof.birth1!=g_pose_bound_birth||proof.carrierBirth0!=g_pose_carry_birth0||!PoseRead(pair.data+8,pointer)||pointer+0x4c!=proof.life||
  !PoseRead(proof.life+0x34,life)||life!=2||!PoseRead(proof.life+0x24,health)||!std::isfinite(health)||health<=0||
  !PoseRead(proof.life+0x28,max)||!std::isfinite(max)||max<=0||health>max||!PoseRead(proof.plugin,vt)||vt!=g_base+0x23d5c18||
  !PoseRead(proof.plugin+8,pointer)||pointer!=b.block||!PoseRead(proof.plugin+0x38,pointer)||pointer!=proof.node||!PoseRead(proof.plugin+0x78,pointer)||pointer!=proof.records||
  !PoseRead(proof.node,vt)||vt!=g_base+0x2306628||!PoseRead(proof.node+0x38,pointer)||pointer!=proof.plugin+0x30||
  !PoseRead(proof.node+8,pointer)||!PoseRead(pointer,bits)||!(bits&1u)||!PoseRead(proof.records+0x10,mode)||mode!=5||
  !PoseRead(proof.records+0x11,requested)||requested||!PoseRead(b.entry+0x90,pointer)||!pointer||!PoseRead(pointer,pointer)||pointer!=proof.graph||
  !PoseRead(pointer,vt)||vt!=g_base+0x250ba50||!PoseRead(pointer+0x10,vt)||vt!=g_base+0x250bc00||
  !PoseRead(b.recipientNode+8,pointer)||!PoseRead(pointer,bits)||!(bits&1u)||!PoseRead(b.recipientRecords+0x10,current)||!PoseRead(b.recipientRecords+0x11,next)||
  !PoseRead(b.recipientRecords+0x12,committed)||!PoseRead(pair.data+0x18,pointer)||!PoseRead(pointer+2,action)||
  !PoseReader(g_pose_queue_owner.queued,queued,sizeof(queued))||!dcopy::IsAfter(g_pose_deferred_copy,queued)||
  !op1::NativePhase(current,next,action,g_pose_deferred_copy.key.prior,committed,g_pose_deferred_copy.payload.sequence))return false;
 return true;
}
bool PoseQueuedQuery(NativePoseQueuedCarryContext& out)noexcept{
 out={};std::uintptr_t mismatch=0;NativePoseJobContext job{},end{};NativePoseCarryQueueContext queue{},late{};carry_owned_handshake::MessagePrefix packet{};
 if(!PoseOwned()||!t_pose.job||!t_carry_op1.active||g_pose_fault||g_shutdown||!g_pose_force_a_confirmed||!g_pose_carry_tree_current||
  !PoseDeferredPatchesLive()||!PoseQueuedCode(mismatch)||!ReadNativePoseOwnedJobContext(job)||!ReadNativePoseCarryQueueContext(queue)||!PoseDeferredSameJob(job,queue.job)||
  job.held||job.press||job.release||!std::isfinite(job.magnitude)||job.magnitude!=0||
  !PoseReader(t_carry_op1.message,&packet,sizeof(packet))||packet.hash!=carry_request_payload::kHandshakeHash||packet.carrier!=1||packet.operation!=1||
  (packet.carrierRecord&&(!t_carry_op1.reply||packet.carrierRecord!=g_pose_carry_pair.body[0].carrierRecords))||
  t_carry_op1.caller!=g_base+op1::sourceReturn||t_carry_op1.service!=g_base+cop::serviceRva||t_carry_op1.target)return false;
 cop::Intent intent{};NativePoseSubmittedCarryContext receipt{};
 if(!TryAcquireSRWLockShared(&g_pose_carry_intent_lock))return false;intent=g_pose_carry_intent;receipt=g_pose_carry_intent_receipt;ReleaseSRWLockShared(&g_pose_carry_intent_lock);
 if(!intent.accepted||!intent.queued||g_pose_deferred_copy.phase!=dcopy::Phase::queued||!dcopy::Same(PoseDeferredKey(intent),g_pose_deferred_copy.key)||
  intent.carry!=g_pose_owners.carrySerial||intent.generation!=job.generation||intent.epoch!=job.sceneEpoch||
  intent.submission!=g_pose_force_a_submit_nonce||receipt.submitNonce!=intent.submission||receipt.carrySerial!=intent.carry||
  receipt.armGeneration!=job.generation||receipt.armEpoch!=job.sceneEpoch||receipt.player!=job.player||receipt.go!=job.go||receipt.data!=g_pose_queue_owner.data||
  receipt.block1!=job.block||receipt.state1!=job.state||receipt.modelCS1!=job.modelCS||receipt.modelEntry1!=job.modelEntry||
  receipt.birth1!=job.birthSerial||receipt.carrierBirth0!=g_pose_carry_birth0)return false;
 cp::Pair pair{},after{};auto read=PoseReader;std::uintptr_t candidate=0,vt=0,binding=1;std::uint8_t current=0,next=0,endCurrent=0,endNext=0;
 std::uint16_t action=0,endAction=0,target=0xffff,carrier=0xffff,flags=0;
 if(!cp::ReadPair(read,g_base,job.player,pair)||!cp::Same(pair,g_pose_carry_pair)||!PoseOp0BothBirthsCurrent(pair)||!PoseQueueCurrent()||
  !PoseRead(t_carry_op1.service,vt)||vt!=g_base+cop::serviceVt||!PoseRead(pair.player+0x238,candidate)||candidate!=pair.body[0].cs||
  !PoseRead(pair.body[0].carrierRecords+0x78,target)||target!=1||!PoseRead(pair.body[0].carrierRecords+0x7c,target)||target!=1||
  !PoseRead(pair.body[0].carrierRecords+0x6a2,flags)||!(flags&0x1000u)||!PoseRead(pair.body[1].recipientRecords+0x30,carrier)||carrier||
  !PoseRead(pair.body[1].recipientRecords+0x28,binding)||binding||!PoseQueuedNative(pair,current,next,action)||
  !cp::ReadPair(read,g_base,pair.player,after)||!cp::Same(pair,after)||!cp::CoherentCallbacks(pair.body[0],after.body[0])||!cp::CoherentCallbacks(pair.body[1],after.body[1])||
  !PoseOp0BothBirthsCurrent(after)||!PoseQueuedNative(after,endCurrent,endNext,endAction)||current!=endCurrent||next!=endNext||action!=endAction||
  !ReadNativePoseOwnedJobContext(end)||!PoseDeferredSameJob(job,end)||!ReadNativePoseCarryQueueContext(late)||!PoseDeferredSameJob(job,late.job)||
  late.root!=queue.root||late.slab!=queue.slab||late.queued!=queue.queued||late.snapshot!=queue.snapshot||late.bytes!=queue.bytes||late.carrySerial!=queue.carrySerial)return false;
 out.image=g_base;out.carrierRecord=pair.body[0].carrierRecords;out.recipientRecord=pair.body[1].recipientRecords;out.message=t_carry_op1.message;
 out.result=t_carry_op1.result;out.service=t_carry_op1.service;out.job=job;out.queue=queue;out.submission=receipt;out.intentNonce=intent.intent;out.intentAt=intent.at;
 out.birth0=g_pose_carry_birth0;out.birth1=job.birthSerial;out.priorSequence=intent.prior;out.current=current;out.next=next;out.nativeAction=action;
 out.acknowledged=g_pose_carry_ack_intent==intent.intent;return true;
}
bool PoseCarryOwnedOp1Reply(void* phase,std::uint32_t actual,void* output,const void* message,std::uintptr_t caller)noexcept{
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;NativePoseQueuedCarryContext before{},after{};
 carry_owned_handshake::MessagePrefix initial{},final{};unsigned char old[0x6c0]{},now[0x6c0]{};std::int32_t result=INT32_MAX;
 const bool ready=actual==0&&caller==g_base+op1::walkerReturn&&t_carry_op1.active&&!t_carry_op1.reply&&
  reinterpret_cast<std::uintptr_t>(phase)==g_pose_carry_pair.body[0].carrier+0x30&&
  reinterpret_cast<std::uintptr_t>(output)==t_carry_op1.result&&reinterpret_cast<std::uintptr_t>(message)==t_carry_op1.message&&
  g_pose_queued_sleep_current&&PoseQueuedQuery(before)&&g_pose_carry_tree_current(before)&&g_pose_queued_sleep_current(before.submission.submittedSleep)&&
  PoseReader(before.message,&initial,sizeof(initial))&&!initial.carrierRecord&&PoseReader(before.carrierRecord,old,sizeof(old))&&
  PoseLocalFlagMapWritable(before.message+0x10,8)&&PoseLocalFlagMapWritable(before.result,4)&&
  PoseLocalFlagMapWritable(before.carrierRecord+0x60,8)&&PoseLocalFlagMapWritable(before.carrierRecord+0x80,1)&&PoseLocalFlagMapWritable(before.carrierRecord+0x85,1);
 if(!ready){PoseFail("pose_carry_op1_reply_preflight");SetLastError(incoming);return false;}
 SetLastError(incoming);g_pose_vars_message(phase,actual,output,message);outgoing=GetLastError();t_carry_op1.reply=true;
 const bool accepted=PoseReader(before.message,&final,sizeof(final))&&PoseReader(before.carrierRecord,now,sizeof(now))&&PoseRead(before.result,result)&&
  op1::ReplyOnly(old,now,g_pose_carry_pair.body[0].cs,initial,final,before.carrierRecord,result)&&PoseQueuedQuery(after)&&
  PoseDeferredSameJob(before.job,after.job)&&after.intentNonce==before.intentNonce&&g_pose_carry_tree_current(after)&&
  g_pose_queued_sleep_current(after.submission.submittedSleep);
 if(!accepted)PoseFail("pose_carry_op1_reply_post");else g_pose_carry_ack_intent=before.intentNonce;
 SetLastError(outgoing);return accepted&&!g_pose_fault;
}
// Invoked from exact owned Carry branch of the BF3A80 wrapper. Other owned
// Carry senders remain refused. Original BF3A80 performs normal game delivery.
std::int32_t* PoseCarryOwnedOp1(void* service,std::int32_t* result,std::uint32_t actual,void* message,std::uintptr_t caller)noexcept{
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;carry_owned_handshake::MessagePrefix packet{};auto* returned=result;
 if(!PoseOwned()||!PoseReader(reinterpret_cast<std::uintptr_t>(message),&packet,sizeof(packet))||
  !op1::Source(g_base,caller,reinterpret_cast<std::uintptr_t>(service),actual,packet)||t_carry_op1.active){PoseFail("pose_carry_owned_op1_source");SetLastError(incoming);return result;}
 const auto saved=t_carry_op1;t_carry_op1={reinterpret_cast<std::uintptr_t>(service),caller,reinterpret_cast<std::uintptr_t>(message),reinterpret_cast<std::uintptr_t>(result),actual,true,false};
 __try{
  NativePoseQueuedCarryContext before{},after{};std::int32_t output=INT32_MAX;
  if(!g_pose_queued_sleep_current||!PoseQueuedQuery(before)||!g_pose_carry_tree_current(before)||!g_pose_queued_sleep_current(before.submission.submittedSleep))PoseFail("pose_carry_owned_op1_preflight");
  else{
   SetLastError(incoming);returned=g_pose_carry_dispatch(service,result,actual,message);outgoing=GetLastError();
   if(returned!=result||!t_carry_op1.reply||!PoseRead(reinterpret_cast<std::uintptr_t>(result),output)||output||!PoseQueuedQuery(after)||
    after.intentNonce!=before.intentNonce||!PoseDeferredSameJob(before.job,after.job)||!g_pose_carry_tree_current(after)||!g_pose_queued_sleep_current(after.submission.submittedSleep))PoseFail("pose_carry_owned_op1_post");
  }
 }__finally{t_carry_op1=saved;SetLastError(outgoing);}return returned;
}

bool PoseCarryOwnedRecipientNoop(void* phase,std::uint32_t actual,std::int32_t* output,void* message)noexcept{
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;NativePoseQueuedCarryContext before{},after{};
 carry_owned_handshake::MessagePrefix old{},now{};std::int32_t oldResult=0,newResult=0;unsigned char oldRecord[0x60]{},newRecord[0x60]{};
 if(actual||!t_carry_op1.active||reinterpret_cast<std::uintptr_t>(phase)!=g_pose_carry_pair.body[0].recipient+0x30||
  reinterpret_cast<std::uintptr_t>(output)!=t_carry_op1.result||reinterpret_cast<std::uintptr_t>(message)!=t_carry_op1.message||
  !g_pose_queued_sleep_current||!PoseQueuedQuery(before)||!g_pose_carry_tree_current(before)||!g_pose_queued_sleep_current(before.submission.submittedSleep)||
  !PoseReader(before.message,&old,sizeof(old))||!PoseRead(before.result,oldResult)||!PoseReader(g_pose_carry_pair.body[0].recipientRecords,oldRecord,sizeof(oldRecord))){
  PoseFail("pose_carry_op1_recipient_noop_preflight");SetLastError(incoming);return false;}
 SetLastError(incoming);g_pose_carry_recipient_message(phase,actual,output,message);outgoing=GetLastError();
 const bool same=PoseReader(before.message,&now,sizeof(now))&&!std::memcmp(&old,&now,sizeof(old))&&PoseRead(before.result,newResult)&&oldResult==newResult&&
  PoseReader(g_pose_carry_pair.body[0].recipientRecords,newRecord,sizeof(newRecord))&&!std::memcmp(oldRecord,newRecord,sizeof(oldRecord))&&
  PoseQueuedQuery(after)&&before.intentNonce==after.intentNonce&&PoseDeferredSameJob(before.job,after.job)&&g_pose_queued_sleep_current(after.submission.submittedSleep);
 if(!same)PoseFail("pose_carry_op1_recipient_noop_post");SetLastError(outgoing);return same&&!g_pose_fault;
}
