#include "coop_carry_wire.hpp"
#include <cstdio>
namespace w=mgscoop::coop_carry_wire;
int main(){unsigned count=0,fail=0;auto check=[&](bool b){++count;if(!b)++fail;};
 w::Frame sender{30010,0x10000007,0x20000007,3,100,true,true},receiver{30010,0x20000007,0x10000007,3,100,true,false};
 std::uint8_t bytes[w::size]{};w::Message m{};w::State s{};s.Update(receiver);bool fresh=false;
 check(w::Encode(sender,100,w::readiness,65535,bytes)&&w::Parse(bytes,sizeof(bytes),m));
 check(s.Receive(m,100,fresh)&&fresh&&s.PeerReady(100));check(s.Receive(m,101,fresh)&&!fresh);
 check(!s.PeerReady(851));check(w::Encode(sender,100,w::start,0,bytes)&&w::Parse(bytes,sizeof(bytes),m)&&s.Receive(m,102,fresh)&&fresh);
 auto old=m;old.sequence=65535;check(!s.Receive(old,103,fresh));
 old=m;old.sequence=1;old.sender=7;check(!s.Receive(old,103,fresh));old=m;old.sequence=1;old.victim=7;check(!s.Receive(old,103,fresh));
 old=m;old.sequence=1;old.epoch=4;check(!s.Receive(old,103,fresh));old=m;old.sequence=1;old.mission=30020;check(!s.Receive(old,103,fresh));
 sender.ready=false;check(w::Encode(sender,100,w::readiness,1,bytes)&&w::Parse(bytes,sizeof(bytes),m)&&s.Receive(m,104,fresh)&&!s.PeerReady(104));
 sender.ready=true;check(w::Encode(sender,100,w::readiness,2,bytes)&&w::Parse(bytes,sizeof(bytes),m)&&s.Receive(m,105,fresh)&&s.PeerReady(105));
 receiver.localGeneration++;s.Update(receiver);check(!s.PeerReady(105)&&!s.Receive(m,105,fresh));
 bytes[1]=1;check(!w::Parse(bytes,sizeof(bytes),m));bytes[1]=2;bytes[3]=2;check(!w::Parse(bytes,sizeof(bytes),m));
 bytes[3]=1;bytes[2]=w::start;check(!w::Parse(bytes,sizeof(bytes),m));bytes[3]=0;bytes[2]=9;check(!w::Parse(bytes,sizeof(bytes),m));
 check(!w::Parse(bytes,8,m));check(!w::Parse(nullptr,sizeof(bytes),m));sender.valid=false;check(!w::Encode(sender,100,w::start,3,bytes));
 sender.valid=true;check(!w::Encode(sender,99,w::start,3,bytes));check(!w::Encode(sender,851,w::start,3,bytes));
 sender.localGeneration=0;check(!w::Encode(sender,100,w::start,3,bytes));
 std::printf("carry wire: %u checks, %u failures\n",count,fail);return fail?1:0;}
