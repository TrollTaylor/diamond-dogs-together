#include "carry_request_payload.hpp"
#include <cstdio>
#include <cstring>
#include <utility>
#include <initializer_list>
using namespace mgscoop::carry_request_payload;
unsigned pass=0,fail=0;
void Check(bool b,const char* name){if(b)++pass;else{++fail;std::printf("FAIL %s\n",name);}}
template<class T>bool Zero(const T& v){const unsigned char* p=reinterpret_cast<const unsigned char*>(&v);for(std::size_t n=0;n<sizeof(v);++n)if(p[n])return false;return true;}
int main(){
 const std::uintptr_t image=0x140000000;const std::size_t span=0xa01d000;
 CarrierTargetMessage m{};RecipientRequest r{};
 Check(BuildCarrierTarget(0,1,m)&&m.type==kCarrierMessageHash&&m.secondary==kCarrierMessageSecondary&&m.target==1,"host carrier exact target message");
 Check(Zero(m.reserved),"carrier padding initialized");Check(BuildCarrierTarget(1,0,m)&&m.target==0,"guest carrier maps native target0");
 for(auto ids:{std::pair<unsigned,unsigned>{0,0},{1,1},{2,0},{0,2},{0xffff,1}}){Check(!BuildCarrierTarget(ids.first,ids.second,m)&&Zero(m),"invalid carrier tuple refuses with cleared output");Check(!BuildRecipient(image,span,ids.first,ids.second,3,r)&&Zero(r),"invalid recipient tuple refuses with cleared output");}
 Check(BuildRecipient(image,span,0,1,7,r)&&r.vtable==image+kRecipientVtableRva&&r.kind==15&&r.sequence==8&&r.force==1&&r.carrier==0,"host initial kind15 request exact layout");
 Check(r.substate==0&&r.additional==0&&Zero(r.reserved08)&&r.reserved13==0&&r.reserved16==0&&r.reserved1c==0,"recipient unused fields zero");
 Check(BuildRecipient(image,span,1,0,0xff,r)&&r.sequence==0&&r.carrier==1,"guest initial sequenceFF wraps0");
 bool sequences=true;for(unsigned n=0;n<256;++n){unsigned next=(n+1)&255u;if(next>250)next=0;if(NextSequence(static_cast<std::uint8_t>(n))!=next)sequences=false;}Check(sequences,"all native byte sequence transitions including wrap");
 Check(!BuildRecipient(0,span,0,1,0,r)&&Zero(r),"missing image refuses");Check(!BuildRecipient(image+1,span,0,1,0,r),"unaligned image refuses");Check(!BuildRecipient(image,kRecipientVtableRva+7,0,1,0,r),"truncated vtable image refuses");Check(!BuildRecipient(0xfffffffffffffff8ull,0x1000,0,1,0,r),"image overflow refuses");
 Check(OfflineInitConsumes(true,15,8,9,0),"changed sequence admits offline native init");Check(!OfflineInitConsumes(true,15,8,8,0),"same unforced sequence refuses");Check(OfflineInitConsumes(true,15,8,8,1),"single forced initial request admits sequence equality");Check(!OfflineInitConsumes(false,15,8,9,1),"predicate cannot claim online branch");Check(!OfflineInitConsumes(true,14,8,9,1),"kind14 cannot substitute carried request");
 Check(TargetReceiverAcceptsState(0x83,true,0,0)&&TargetReceiverAcceptsState(0x84,true,0,0),"native asleep actions admit owned carrier handshake");Check(!TargetReceiverAcceptsState(0x1d,true,0,0),"actual prone fallback cannot pass sleep gate");Check(!TargetReceiverAcceptsState(0x2a,true,0,0),"premature target action2A cannot pass owned handshake");Check(!TargetReceiverAcceptsState(0x2b,false,1,0)&&!TargetReceiverAcceptsState(0x2b,false,0,1),"active drop state rejects reentry");Check(TargetReceiverAcceptsState(0x2b,false,0,0),"unowned stock drop-idle gate mirrors exact branch only");
 Check(NativeDropCarrierPresent(1u<<22)&&!NativeDropCarrierPresent(0)&&!NativeDropCarrierPresent(1u<<24),"drop uses actual carrier bit22 not gun bit24");
 std::printf("%u passed, %u failed\n",pass,fail);return fail?1:0;
}
