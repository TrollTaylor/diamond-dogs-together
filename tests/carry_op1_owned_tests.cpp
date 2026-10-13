#include <cstdio>
#include <cstring>
#include "carry_op1_owned.hpp"
namespace p=mgscoop::carry_op1_owned;namespace c=mgscoop::carry_owned_handshake;
int main(){unsigned count=0;bool all=true;auto check=[&](bool v,const char* name){++count;if(!v){all=false;std::printf("failed %s\n",name);}};
 check(p::walkerReturn==0xbf6839&&p::walkerReturn!=0xbf64e3,"carrier descendant callback excludes root-level return");
 const std::uintptr_t image=0x140000000,record=0x100000,cs=0x200000;
 c::MessagePrefix message{mgscoop::carry_request_payload::kHandshakeHash,1,0xbeef,1,0};
 check(p::Source(image,image+p::sourceReturn,image+0x2c38008,0,message),"natural initialized source1/op1/target0");
 auto invalid=message;invalid.carrier=0;check(!p::Source(image,image+p::sourceReturn,image+0x2c38008,0,invalid),"op0 carrier0 not reciprocal1");
 invalid=message;invalid.operation=2;check(!p::Source(image,image+p::sourceReturn,image+0x2c38008,0,invalid),"op2 not owned reciprocal1");
 invalid=message;invalid.carrierRecord=record;check(!p::Source(image,image+p::sourceReturn,image+0x2c38008,0,invalid),"old native response cannot become fresh source");
 check(!p::Source(image,image+p::sourceReturn+1,image+0x2c38008,0,message),"exact natural caller required");
 check(!p::Source(image,image+p::sourceReturn,image+0x2c38008,1,message),"remote target cannot receive local0 reply");
 check(!p::Source(image,image+p::sourceReturn,image+0x2c38010,0,message),"distinct dispatcher interface refuses");
 check(p::NativePhase(1,1,0x84,0xff,0xff,0),"native initial phase1 remainsSleep84 before first bind");
 check(p::NativePhase(2,2,0x2a,0xff,0,0),"native phase2 committed changedsequence2A");
 check(p::NativePhase(3,3,0x2a,7,8,8),"native carried phase3 preserves committed request");
 check(!p::NativePhase(1,1,0x1d,0xff,0xff,0),"mod prone fallback cannot masquerade as consumed sleeping recipient");
 check(!p::NativePhase(2,2,0x84,0xff,0,0),"phase2 must have stock-carried action2A");
 check(!p::NativePhase(3,4,0x2a,7,8,8),"active transition toward putdown cannot new-ack");
 check(!p::NativePhase(4,4,0x2b,7,8,8),"putdown phase excluded from pickup acknowledgement");
 check(!p::NativePhase(1,1,0x84,7,8,8),"phase1 preserves prior committed sequence");
 check(!p::NativePhase(2,2,0x2a,7,7,8),"phase2 native sequencecommit must match new request");
 unsigned char before[0x6c0]{},after[0x6c0]{};for(unsigned n=0;n<sizeof(before);++n)before[n]=static_cast<unsigned char>(n*13);
 std::memcpy(after,before,sizeof(after));std::memcpy(after+0x60,&cs,8);after[0x80]=0;after[0x85]=1;auto reply=message;reply.carrierRecord=record;
 check(p::ReplyOnly(before,after,cs,message,reply,record,0),"native exact pointer/byte reply changes only qualified footprints");
 check(!p::ReplyOnly(before,after,cs,message,reply,record,-1),"native rejected output does not sealack");
 auto wrong=reply;wrong.hash++;check(!p::ReplyOnly(before,after,cs,message,wrong,record,0),"hash unchanged");
 wrong=reply;wrong.reserved++;check(!p::ReplyOnly(before,after,cs,message,wrong,record,0),"unconsumed nativepadding preserved without admissionzero requirement");
 wrong=reply;wrong.carrierRecord++;check(!p::ReplyOnly(before,after,cs,message,wrong,record,0),"response points exact current carrierrecord");
 after[0x81]^=1;check(!p::ReplyOnly(before,after,cs,message,reply,record,0),"neighbor progress byte81 not part of byte80 ID write");after[0x81]^=1;
 after[0x86]^=1;check(!p::ReplyOnly(before,after,cs,message,reply,record,0),"neighbor86 not part of offline byte85");after[0x86]^=1;
 after[0x70]^=1;check(!p::ReplyOnly(before,after,cs,message,reply,record,0),"unknown carrier mutation refuses");after[0x70]^=1;
 check(!p::ReplyOnly(before,after,cs+8,message,reply,record,0),"actual held CS exactpointer required");
 check(!p::ReplyOnly(before,after,0,message,reply,record,0),"no readable/default CS substitute");
 std::printf("%u natural op1 source checks\n",count);return all?0:1;}
