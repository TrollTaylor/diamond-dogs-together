#include "partner_reliable_origin.hpp"
#include <cstdio>
namespace net=mgscoop::partner_net;
namespace origin=net::reliable_origin;
namespace link=mgscoop::link;
unsigned checks=0,failures=0;
void Check(bool yes,const char* why){++checks;if(!yes){++failures;std::printf("FAIL %s\n",why);}}
net::ReliableOrigin Current(bool guest=false){net::ReliableOrigin o{};o.incarnation=17;o.session=1;
 o.peer.address=0x0100007f;o.peer.port=23000;o.localRole=guest?1u:0u;o.peerRole=guest?0u:1u;return o;}
int main(){
 const auto committed=Current();origin::Slot slot{};net::ReliableOrigin receipt{};
 Check(origin::Record(committed,committed,0,7000000,slot),"actual authenticated same-session insertion records native tick clock");
 Check(origin::Observe(committed,0,slot,receipt)&&receipt.incarnation==17&&receipt.acceptedTick==7000000&&receipt.message==0,
  "actual message0 front and original insertion age preserved");
 auto newer=committed;newer.incarnation=18;newer.session=2;
 Check(!origin::Record(newer,committed,0,7001000,slot)&&slot.incarnation==17,
  "restart between authenticated parse and rel acceptance refuses old payload into new queue");
 Check(!origin::Observe(newer,0,slot,receipt)&&!receipt.incarnation,"new session cannot dequeue stale origin slot");
 auto restarted=committed;restarted.incarnation=18; // Session::Start may reset its session_number back to1.
 Check(!origin::SameSession(committed,restarted),"monotonic queue incarnation distinguishes restarted session_number1");
 Check(!origin::Record(restarted,committed,0,7001000,slot),"reused local session counter never resurrects old packet");
 auto changed=committed;++changed.peer.address;Check(!origin::SameSession(committed,changed),"different authenticated commit address refuses");
 changed=committed;++changed.peer.port;Check(!origin::SameSession(committed,changed),"different commit endpoint port refuses");
 changed=committed;changed.localRole=1;changed.peerRole=0;Check(!origin::SameSession(committed,changed),"role swap cannot inherit old queued source");
 Check(!origin::Record(committed,committed,0,0,slot),"missing native clock is not inferred from network QPC clock");
 Check(!origin::Observe(committed,1,slot,receipt),"next reliable ID cannot inherit consumed slot provenance");
 slot={};Check(!origin::Observe(committed,0,slot,receipt),"cleared queue slot has no authenticated receipt");
 Check(origin::Record(newer,newer,0,7002000,slot)&&origin::Observe(newer,0,slot,receipt)&&receipt.incarnation==18,
  "actual new-session message receives new provenance");
 Check(origin::Record(Current(true),Current(true),65535,7002500,slot)&&origin::Observe(Current(true),65535,slot,receipt)&&receipt.localRole==1&&receipt.peerRole==0,
  "guest source carries exact role and maximum native reliable ID");
 Check(!origin::Observe(Current(true),0,slot,receipt),"wrapped ID0 cannot match prior65535 slot");
 {
  link::ReliableReceiver receiver;std::uint16_t id=9;std::uint8_t payload[]={1,2,3},out[4]{};std::size_t size=0;
  Check(!receiver.FrontId(id)&&id==0,"empty reliable queue has no front identity");
  Check(receiver.Accept(1,payload,sizeof(payload),101)&&!receiver.FrontId(id),"out-of-order ID1 has no in-order front identity");
  Check(receiver.Accept(0,payload,sizeof(payload),102)&&receiver.FrontId(id)&&id==0,"actual stored ID0 exposed without dequeue");
  Check(!receiver.Drain(out,2,size,103)&&receiver.FrontId(id)&&id==0,"capacity refusal leaves same origin eligible for later drain");
  Check(receiver.Drain(out,sizeof(out),size,104)&&size==3&&out[0]==1&&receiver.FrontId(id)&&id==1,
   "native drain advances exact in-order origin identity");
  Check(receiver.Drain(out,sizeof(out),size,105)&&!receiver.FrontId(id),"all real payloads drained unchanged");
  receiver.Reset();Check(!receiver.FrontId(id),"receiver reset removes old head identity");
 }
 {
  link::ReliableReceiver receiver;origin::Slot messages[link::kReliableWindow]{};std::uint8_t bytes[]={4,5},out[3]{};
  auto queue=committed;const auto oldPacket=queue;receiver.Accept(0,bytes,sizeof(bytes),1);
  origin::Record(queue,oldPacket,0,7000001,messages[0]);
  // World callback may restart the Session before the transport reacquires
  // tx -> rx. Receiver + all metadata are reset under that session lock.
  queue=newer;receiver.Reset();for(auto& message:messages)message={};
  const bool canAccept=origin::SameSession(queue,oldPacket);
  if(canAccept)receiver.Accept(0,bytes,sizeof(bytes),2);
  std::uint16_t head=0;Check(!canAccept&&!receiver.FrontId(head),"stale parse/reset/accept interleaving never creates a new queue entry");
  const auto freshPacket=queue;Check(origin::SameSession(queue,freshPacket)&&receiver.Accept(0,bytes,sizeof(bytes),3)&&
   origin::Record(queue,freshPacket,0,7002003,messages[0]),"fresh post-reset packet can be accepted under its own origin");
  Check(receiver.FrontId(head)&&origin::Observe(queue,head,messages[head%link::kReliableWindow],receipt)&&receipt.acceptedTick==7002003,
   "payload origin and native insertion age copied before exact dequeue");
  std::size_t size=0;Check(receiver.Drain(out,sizeof(out),size,4)&&size==2&&out[0]==4&&out[1]==5,
   "origin API preserves unrelated reliable payload bytes and order");
 }
 {
  link::ReliableSender sender;std::uint8_t bytes[]={9,8};const auto nativeProducerOrigin=committed;
  auto current=restarted;const bool sendIntoNew=origin::SameSession(current,nativeProducerOrigin);
  if(sendIntoNew)sender.Queue(bytes,sizeof(bytes),link::ReliableMode::append);
  Check(!sendIntoNew&&sender.queued()==0,"session replacement before native send cannot enqueue old mount into new channel");
  current=committed;Check(origin::SameSession(current,nativeProducerOrigin)&&
   sender.Queue(bytes,sizeof(bytes),link::ReliableMode::append)==link::ReliableResult::queued&&sender.queued()==1,
   "actual matching native producer can enqueue ordinary reliable payload unchanged");
 }
 std::printf("Authenticated reliable origins: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
