#include "carry_target_service_contract.hpp"
#include <cstdio>
int main(){namespace c=mgscoop::carry_target_service_contract;unsigned n=0,f=0;auto check=[&](bool v,const char* name){++n;if(!v){++f;std::printf("failed %s\n",name);}};
 const c::Site expect[]={
  {0x9bd7e0,sizeof(c::b0),c::b0},{0x202f890,sizeof(c::b1),c::b1},{0x127dd10,sizeof(c::b2),c::b2},{0x127ced0,sizeof(c::b3),c::b3},
  {0x127d5c0,sizeof(c::b4),c::b4},{0x127ea60,sizeof(c::b5),c::b5},{0x127ff50,sizeof(c::b6),c::b6},{0x127e700,sizeof(c::b7),c::b7},
  {0x127fed0,sizeof(c::b8),c::b8},{0x127fcc0,sizeof(c::b9),c::b9},{0x113bba0,sizeof(c::b10),c::b10},{0x113be8a,sizeof(c::b11),c::b11},
  {0x113c36b,sizeof(c::b12),c::b12},{0x113c5b7,sizeof(c::b13),c::b13},{0x113c73f,sizeof(c::b14),c::b14},{0x127e410,sizeof(c::b15),c::b15},
  {0x127dd70,sizeof(c::b16),c::b16},{0x1280000,sizeof(c::b17),c::b17}};
 check(sizeof(c::sites)/sizeof(c::sites[0])==sizeof(expect)/sizeof(expect[0]),"site count");
 for(unsigned i=0;i<sizeof(expect)/sizeof(expect[0]);++i)check(c::sites[i].rva==expect[i].rva&&c::sites[i].size==expect[i].size&&c::sites[i].bytes==expect[i].bytes,"site span");
 check(c::b9[0]==0x66&&c::b9[1]==0x89&&c::b9[2]==0x51&&c::b9[3]==0x30&&c::b9[4]==0xc3,"word setter bytes");
 const c::Slot slots[]={{0x23ce930,0x127dd10},{0x23ce940,0x127d5c0},{0x23ce948,0x127ea60},{0x23ce950,0x127ff50},{0x23ce958,0x127e410},{0x23ce970,0x127e700},{0x23ce978,0x127fed0},{0x23ce980,0x127fcc0},{0x23ce9c8,0x1280000},{0x23cea90,0x127dd70}};
 check(sizeof(c::slots)/sizeof(c::slots[0])==sizeof(slots)/sizeof(slots[0]),"slot count");
 for(unsigned i=0;i<sizeof(slots)/sizeof(slots[0]);++i)check(c::slots[i].rva==slots[i].rva&&c::slots[i].target==slots[i].target,"slot");
 std::printf("carry_target_service_tests: %u checks, %u failures\n",n,f);return f?1:0;}
