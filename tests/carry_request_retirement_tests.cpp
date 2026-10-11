#include "carry_request_retirement.hpp"
#include <cstdio>
#include <limits>
#include <initializer_list>
using namespace mgscoop::carry_request_retirement;
unsigned good=0,bad=0;void Check(bool v,const char* s){if(v)++good;else{++bad;std::printf("FAIL %s\n",s);}}
constexpr std::uintptr_t Image=0x140000000;constexpr std::size_t Span=0xa01d000;
Scope Fixture(unsigned carrier=0){Scope s{};
 auto& l=s.expected.lease;l.mission=30050;l.generation=3;l.thread=9;l.transaction=5;l.revision=7;l.epoch=2;l.carrier=static_cast<std::uint8_t>(carrier);l.recipient=static_cast<std::uint8_t>(1-carrier);l.cause=1;l.player=0x10000;l.carrierRecord=0x70000+carrier*0x1000;l.recipientPhase=0xd0030+(1-carrier)*0x1000;
 s.expected.queue=0x20000;s.expected.array=0x40000;s.expected.slot=s.expected.array+l.recipient*0x40;s.current=s.expected;s.exactGate=s.serializedRecipientWindow=s.typedAllocation=s.coherentNativeStates=true;
 mgscoop::carry_request_payload::BuildRecipient(Image,Span,carrier,1-carrier,0x10,s.ours);s.ours.force=0;s.observed=s.ours;return s;
}
int main(){NeutralRequest n{};auto s=Fixture();
 Check(Plan(Image,Span,s,n)==Reason::plan&&n.vtable==Image+kNeutralVtableRva&&n.kind==0,"retired exact force0 request neutral plan");for(auto b:n.reserved)Check(b==0,"neutral reserved bytes zero");
 s=Fixture(1);Check(Plan(Image,Span,s,n)==Reason::plan,"reverse body role plan");s=Fixture();s.expected.lease.cause=s.current.lease.cause=2;Check(Plan(Image,Span,s,n)==Reason::plan,"sleep lease retirement");
 for(auto member:{&Scope::exactGate,&Scope::serializedRecipientWindow,&Scope::typedAllocation,&Scope::coherentNativeStates}){s=Fixture();s.*member=false;Check(Plan(Image,Span,s,n)==Reason::scope&&!n.vtable,"missing current proof refuses without payload");}
 for(unsigned k=0;k<10;++k){s=Fixture();switch(k){case 0:s.current.lease.generation++;break;case 1:s.current.lease.epoch++;break;case 2:s.current.lease.revision++;break;case 3:s.current.lease.thread++;break;case 4:s.current.queue+=8;break;case 5:s.current.array+=0x80;break;case 6:s.current.slot+=0x40;break;case 7:s.expected.lease.transaction=s.current.lease.transaction=0;break;case 8:s.expected.lease.mission=s.current.lease.mission=10010;break;default:s.expected.array=s.current.array=UINTPTR_MAX-15;s.expected.slot=s.current.slot=s.expected.array;break;}Check(Plan(Image,Span,s,n)==Reason::identity&&!n.vtable,"replaced or invalid lease refuses");}
 for(unsigned k=0;k<9;++k){s=Fixture();switch(k){case 0:s.observed.sequence++;break;case 1:s.observed.kind=0;break;case 2:s.observed.carrier=1;break;case 3:s.observed.force=1;break;case 4:s.ours.force=s.observed.force=1;break;case 5:s.ours.vtable=s.observed.vtable=Image+0x23ab570;break;case 6:s.ours.reserved08[1]=s.observed.reserved08[1]=1;break;case 7:s.ours.substate=s.observed.substate=4;break;default:s.ours.sequence=s.observed.sequence=0xff;break;}Check(Plan(Image,Span,s,n)==Reason::payload&&!n.vtable,"foreign or noncanonical request not overwritten");}
 for(unsigned k=0;k<7;++k){s=Fixture();switch(k){case 0:s.recipientActive=true;break;case 1:s.recipientEndPending=true;break;case 2:s.carrierActive=true;break;case 3:s.carrierEndPending=true;break;case 4:s.recipientCarrierRecord=0x70000;break;case 5:s.recipientCarrier=0;break;default:s.carrierOwner=0;break;}Check(Plan(Image,Span,s,n)==Reason::active&&!n.vtable,"native phase must finish before request retirement");}
 s=Fixture();s.ours.additional=s.observed.additional=-0.0f;Check(Plan(Image,Span,s,n)==Reason::payload,"noncanonical negative-zero additional bytes refused");
 s=Fixture();Check(Plan(Image,0x1000,s,n)==Reason::payload,"short image refuses");Check(Plan(UINTPTR_MAX-7,Span,s,n)==Reason::payload,"overflow image refuses");
 s=Fixture();Check(Plan(Image,Span,s,n)==Reason::plan,"first fresh plan succeeds");s.observed.sequence++;Check(Plan(Image,Span,s,n)==Reason::payload,"fresh recheck catches intervening action");
 std::printf("%u passed, %u failed\n",good,bad);return bad?1:0;
}
