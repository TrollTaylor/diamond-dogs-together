#include "carry_op2_shared.hpp"
#include <cstdio>
namespace p=mgscoop::carry_op2_shared;namespace h=mgscoop::carry_owned_handshake;
int main(){unsigned n=0;bool all=true;auto check=[&](bool v,const char* label){++n;if(!v){all=false;std::printf("failed %s\n",label);}};
 const std::uintptr_t image=0x140000000,record=0x100000,result=0x200000;h::MessagePrefix source{mgscoop::carry_request_payload::kHandshakeHash,0,0xbeef,2,record};
 check(p::Source(image,image+p::sourceReturn,image+0x2c38008,1,source,record),"genuine initialized source0/op2/target1 with unchanged padding");
 auto changed=source;changed.operation=0;check(!p::Source(image,image+p::sourceReturn,image+0x2c38008,1,changed,record),"op0 receipt never supplies op2 scope");
 changed=source;changed.operation=1;check(!p::Source(image,image+p::sourceReturn,image+0x2c38008,1,changed,record),"owned op1 receipt never supplies shared op2 scope");
 changed=source;changed.carrier=1;check(!p::Source(image,image+p::sourceReturn,image+0x2c38008,1,changed,record),"wrong native source body");
 changed=source;changed.carrierRecord++;check(!p::Source(image,image+p::sourceReturn,image+0x2c38008,1,changed,record),"raw record must match actual retained carrier0");
 check(!p::Source(image,image+0x113c06e,image+0x2c38008,1,source,record),"original op0 sender return excluded");
 check(!p::Source(image,image+p::sourceReturn,image+0x2c38008,0,source,record),"current real target1 only");
 check(!p::Source(image,image+p::sourceReturn,image+0x2c38000,1,source,record),"typed service interface exact");
 unsigned char before[0x6c0]{},after[0x6c0]{};for(unsigned i=0;i<sizeof(before);++i)before[i]=static_cast<unsigned char>(i*7);std::memcpy(after,before,sizeof(after));
 check(p::NoMetadata(source,source,before,after,result,result,-1),"stock optional negative metadata branch keeps all owned game state");
 check(!p::NoMetadata(source,source,before,after,result+4,result,-1),"actual result pointer preserved");
 check(!p::NoMetadata(source,source,before,after,result,result,0),"unqualified successful metadata response refuses");
 changed=source;changed.carrierRecord=0;check(!p::NoMetadata(source,changed,before,after,result,result,-1),"no fabricated metadata pointer response");
 changed=source;changed.reserved=0;check(!p::NoMetadata(source,changed,before,after,result,result,-1),"unused native padding preserves existing bytes");
 after[0x560]^=1;check(!p::NoMetadata(source,source,before,after,result,result,-1),"native metadata cannot silently mutate carrierrecord");
 check(!p::NoMetadata(source,source,nullptr,after,result,result,-1),"incomplete fullrecord read refuses");
 std::printf("%u natural shared op2 source checks\n",n);return all?0:1;}
