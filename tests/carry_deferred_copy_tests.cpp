#include <cstdio>
#include <cstring>
#include "carry_deferred_copy.hpp"
namespace d=mgscoop::carry_deferred_copy;
int main(){unsigned n=0;bool good=true;auto check=[&](bool v,const char* label){++n;if(!v){std::printf("failed %s\n",label);good=false;}};
 const std::uintptr_t image=0x140000000,queue=0x100000;d::Key key{10,20,30,1000,4,5,0xff};
 unsigned char bytes[0x80]{};for(unsigned i=0;i<sizeof(bytes);++i)bytes[i]=static_cast<unsigned char>(i+17);
 check(d::Fresh(key,900,1750),"completed op0 and deferred chain inclusive deadline");
 check(!d::Fresh(key,900,1751),"deferred handoff expires from actual intent");
 auto drift=key;drift.at=1651;check(!d::Fresh(drift,900,1651),"op0 itself expires from actual submission");
 check(!d::Fresh(key,1001,1001),"intent cannot precede original submission");
 check(!d::Fresh(key,900,999),"monotonic time rollback refuses");
 check(d::Fresh(key,900,1700),"successful op0 does not incorrectly reuse original750 deadline");
 for(unsigned field=0;field<5;++field){auto k=key;if(field==0)k.submit=0;if(field==1)k.intent=0;if(field==2)k.carry=0;if(field==3)k.generation=0;if(field==4)k.epoch=0;check(!d::Fresh(k,900,1000),"empty cycle field refuses");}
 d::Ledger ledger{};check(d::Reserve(ledger,key,900,1000,image,bytes),"single reservation captures full queued80");
 check(ledger.phase==d::Phase::reserved&&ledger.payload.kind==15&&ledger.payload.force==0&&ledger.payload.carrier==0&&ledger.payload.sequence==0,"fresh forced-off recipient kind15 from initialFF");
 check(!d::Reserve(ledger,key,900,1000,image,bytes),"reservation cannot retry/restart native entry");
 check(d::CurrentReserved(ledger,key,true,bytes),"exact TLS reservation sees unchanged current buffer");
 check(!d::CurrentReserved(ledger,key,false,bytes),"cached reservation supplies no execution receipt");
 drift=key;drift.intent++;check(!d::CurrentReserved(ledger,drift,true,bytes),"different op0 intent cannot reuse reserved request");
 unsigned char after[0x80]{};std::memcpy(after,bytes,sizeof(after));std::memcpy(after+0x40,&ledger.payload,32);
 check(d::CurrentReserved(ledger,key,true,after),"reserved postread may see exact copied prefix");
 auto partial=ledger;unsigned char damaged[0x80]{};std::memcpy(damaged,after,sizeof(damaged));damaged[0x41]^=1;
 check(!d::Commit(partial,key,queue+0x40,queue,damaged)&&partial.phase==d::Phase::reserved,"partial native copy keeps reservation held");
 std::memcpy(damaged,after,sizeof(damaged));damaged[0]^=1;check(!d::Commit(partial,key,queue+0x40,queue,damaged),"local0 slot must remain unchanged");
 std::memcpy(damaged,after,sizeof(damaged));damaged[0x60]^=1;check(!d::Commit(partial,key,queue+0x40,queue,damaged),"untouched recipient tail must remain unchanged");
 check(!d::Commit(partial,key,queue,queue,after),"native return must be exact target destination");
 check(!d::Commit(partial,key,queue+0x40,UINTPTR_MAX-0x20,after),"wrapped queue extent refuses");
 check(!d::Commit(partial,drift,queue+0x40,queue,after),"late intent drift cannot commit");
 check(d::Commit(ledger,key,queue+0x40,queue,after)&&ledger.phase==d::Phase::queued,"one exact copy commits queued state");
 check(!d::Commit(ledger,key,queue+0x40,queue,after),"committed request cannot be submitted twice");
 check(!d::CanClear(ledger,true,true,false)&&!d::CanClear(ledger,true,false,true)&&!d::CanClear(ledger,false,true,true),"partial retirement or release cannot discard retained request");
 check(d::CanClear(ledger,true,true,true),"actual retirement neutral request clean publication all required");
 mgscoop::carry_request_payload::RecipientRequest payload{};
 check(d::Build(image,0xfa,payload)&&payload.sequence==0&&!payload.force,"native sequenceFA rollover without force");
 check(d::Build(image,7,payload)&&payload.sequence==8&&!payload.force,"ordinary changed sequence without force");
 check(!d::Build(UINTPTR_MAX-0x100,7,payload),"payload image overflow refuses");
 std::printf("%u deferred single-copy source checks\n",n);return good?0:1;}
