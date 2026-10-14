#include "npc_pickup_entry_read.hpp"
#include <map>
#include <cstdio>
namespace np=mgscoop::npc_pickup_entry_read;constexpr std::uintptr_t image=0x140000000ull,self=0x100000,owner=0x200000,state=0x300000,cs=0x400000,entry=0x500000,matrix=0x600000,testPlayer=0x700000,testData=0x800000,testStance=0x900000,pos=0xa00000;
int n=0,f=0;void Check(bool value,const char* label){++n;if(!value){++f;std::printf("FAIL %s\n",label);}}
struct Memory{std::map<std::uintptr_t,unsigned char> bytes;bool operator()(std::uintptr_t at,void* out,std::size_t count)const noexcept{auto p=static_cast<unsigned char*>(out);for(std::size_t k=0;k<count;++k){auto i=bytes.find(at+k);if(i==bytes.end())return false;p[k]=i->second;}return true;}
 void Raw(std::uintptr_t at,const void* raw,std::size_t count){const auto p=static_cast<const unsigned char*>(raw);for(std::size_t k=0;k<count;++k)bytes[at+k]=p[k];}template<class T>void Put(std::uintptr_t at,T value){Raw(at,&value,sizeof(value));}};
int main(){Memory m{};m.Put(self,image+0x23b9ac0);m.Put(self+8,owner);m.Put(owner,image+0x2295210);m.Put(owner+8,1u);m.Put(owner+0xc,0u);m.Put(owner+0xb8,self);m.Put(owner+0x78,state);m.Put(owner+0x10,cs);m.Put(owner+0x138,testPlayer);
 m.Put(state,image+0x23b09f0);m.Put(state+8,0u);m.Put(state+0x408,owner);m.Put(state+0x238,cs);
 m.Put(cs,image+0x22e6070);m.Put(image+0x22e6070+0xb0,image+0xaf1aa0);m.Raw(image+0xaf1aa0,np::matrixGetterBytes,sizeof(np::matrixGetterBytes));m.Put(cs+8,1u);m.Put(cs+0xc,0u);m.Put(cs+0x10,entry);m.Put(entry+0x68,matrix);
 m.Put(testPlayer,image+0x2295490);m.Put(testPlayer+0x60,testData);m.Put(testData,image+0x23ad970);m.Put(testData+0x30,testStance);m.Put(testStance,std::uint8_t(0));
 float source[16]{};source[0]=source[5]=source[10]=source[15]=1;source[12]=10;source[13]=5;source[14]=20;m.Raw(matrix+0x40,source,sizeof(source));float target[3]{10,5,21.6f};m.Raw(pos,target,sizeof(target));
 np::Entry e{};Check(np::Read(m,image,self,0,1116,pos,e),"exact typed native source");Check(e.d2>2.55f&&e.d2<2.57f&&e.dot>0&&e.dy==0&&e.rangeSquared==3.0625f,"front1.6m metrics");
 target[2]=18.4f;m.Raw(pos,target,sizeof(target));Check(np::Read(m,image,self,0,1116,pos,e)&&e.dot<0&&e.d2<3.0625f,"near but behind is distinct from range");target[2]=22;m.Raw(pos,target,sizeof(target));Check(np::Read(m,image,self,0,1116,pos,e)&&e.dot>0&&e.d2>e.rangeSquared,"front but outside range");
 target[2]=21.6f;target[1]=6.6f;m.Raw(pos,target,sizeof(target));Check(np::Read(m,image,self,0,1116,pos,e)&&e.dy>1.59f,"raw Y delta preserved not fabricated height gate");
 m.Put(testStance,std::uint8_t(1));Check(np::Read(m,image,self,0,1116,pos,e)&&e.rangeSquared>3.42f,"actual testStance selects stock distance constant");
 for(auto at:{self,self+8,owner,owner+8,owner+0xc,owner+0xb8,owner+0x78,owner+0x10,state,state+8,state+0x408,state+0x238,cs,cs+8,cs+0xc,cs+0x10,entry+0x68,testPlayer,testPlayer+0x60,testData}){auto bad=m;bad.bytes[at]^=8;Check(!np::Read(bad,image,self,0,1116,pos,e),"native type/backlink/index mutation refuses");}
 for(auto at:{image+0xaf1aa0,image+0xaf1ad7,image+0x22e6070+0xb0}){auto bad=m;bad.bytes[at]^=1;Check(!np::Read(bad,image,self,0,1116,pos,e),"exact getter full tail/slot mutation refuses");}
 Check(!np::Read(m,image,self,1,1116,pos,e),"replica native actor refused");Check(!np::Read(m,image,self,0,1,pos,e),"testPlayer target not NPC diagnostic");auto missing=m;missing.bytes.erase(matrix+0x7f);Check(!np::Read(missing,image,self,0,1116,pos,e),"full source matrix required");
 Check(np::Read(m,image,self,0,1116,pos,e),"typed current source restored");np::Snapshot s{};s.before=e;s.after=e;s.raw=true;s.at=100;s.callerRva=0x112ccbb;s.al=false;
 char line[160]{};Check(np::Line(s,101,line,sizeof(line))&&std::strstr(line,"raw=1;pair=0")&&std::strstr(line,"al=0")&&std::strstr(line,"rawDY="),"actual AL and bounded raw preliminary metrics");
 Check(np::Line(s,1601,line,sizeof(line))&&std::strstr(line,"raw=0;pair=0"),"raw source witness expires");Check(!np::Line(s,101,line,20),"truncated diagnostic refused");s.after.matrix[12]+=1;Check(!np::SameSource(s.before,s.after),"matrix changed during original invalidates closure");
 std::printf("npc_pickup_entry_check: %d passed,%d failed; no native/game calls\n",n-f,f);return f?1:0;}
