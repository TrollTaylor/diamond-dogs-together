#include "owned_flag_map.hpp"
#include <cstdio>
#include <cstring>
#include <limits>
namespace fm=mgscoop::owned_flag_map;
namespace ct=mgscoop::owned_flag_map_contract;
unsigned checks=0,failures=0;
void Check(bool result,const char* name){++checks;if(!result){++failures;std::printf("FAIL %s\n",name);}}
fm::Observation Empty(unsigned cap=8){fm::Observation o{};o.header=0x10000;o.storage=0x20000;o.capacity=static_cast<std::uint16_t>(cap);o.free=0;
 fm::Put(o.head,o.storage);fm::Put(o.head+8,o.capacity);fm::Put(o.head+10,o.used);fm::Put(o.head+12,o.free);
 for(unsigned i=0;i<cap;++i){fm::Put(o.data+i*8+4,std::uint16_t{0xffff});fm::Put(o.data+i*8+6,static_cast<std::uint16_t>(i+1==cap?0xffff:i+1));fm::Put(o.data+cap*8+i*2,std::uint16_t{0xffff});}return o;}
void Add(fm::Observation& o,std::uint32_t key,std::uint16_t val){fm::Observation next{};std::uint32_t result=0;Check(fm::PredictInsert(o,key,val,next,result)&&result==val,"fixture native insertion");o=next;}
struct Reader{fm::Observation o;bool fail=false,drift=false;unsigned headers=0;
 bool operator()(std::uintptr_t address,void* out,std::size_t n){if(fail)return false;
  if(address==o.header&&n==16){std::memcpy(out,o.head,n);if(drift&&++headers==2)fm::Put(static_cast<unsigned char*>(out)+10,std::uint16_t{0});return true;}
  if(address==o.storage&&n==std::size_t(o.capacity)*10){std::memcpy(out,o.data,n);return true;}return false;}};
struct CodeReader{std::uintptr_t image=0x140000000;bool bad=false,fail=false;std::uintptr_t corrupt=0;
 bool operator()(std::uintptr_t address,void* out,std::size_t n){if(fail)return false;for(const auto& s:ct::sites)if(address==image+s.rva&&n==s.size){std::memcpy(out,s.bytes,n);if(bad&&s.rva==corrupt)static_cast<unsigned char*>(out)[n-1]^=1;return true;}return false;}};
int main(){
 Check(fm::Source(0x140000000,0x14085c562,false)==fm::Origin::set&&fm::Source(0x140000000,0x14085c579,true)==fm::Origin::set,"exact SET leaf return addresses");
 Check(fm::Source(0x140000000,0x14085c6b2,false)==fm::Origin::clear&&fm::Source(0x140000000,0x14085c6c6,true)==fm::Origin::clear,"original CLEAR source preserved");
 Check(fm::Source(0x140000000,0x14085c562,true)==fm::Origin::none&&fm::Source(0x140000000,0x14085c578,true)==fm::Origin::none,"wrong operation or nearby caller refuses");
 Check(fm::SetOperand(0x1234,0x1234)&&fm::SetOperand(0x12b4,0x1234)&&fm::SetOperand(0xffff,0x1234),"OR zero mask set bit and full mask are legal supersets");
 Check(!fm::SetOperand(0x1034,0x1234)&&!fm::SetOperand(0x11234,0x1234)&&!fm::SetOperand(0xffff,0xffff),"cleared bit high operand or undeletable sentinel refuses");
 const fm::Protected protect{0x40000,0x50000,0x60000};auto o=Empty();fm::Observation after{},read{};std::uint32_t result=0;
 Check(fm::Topology(o),"complete empty free topology");Reader reader{o};Check(fm::ReadPair(reader,o.header,protect,read)&&fm::Same(o,read),"two coherent actual graph reads");
 Add(o,1,0xfff0);Add(o,9,0x1234);Add(o,17,0x5678);Check(fm::Topology(o)&&o.used==3,"three collided native keys");
 Check(fm::PredictOrigin(o,9,0,false,after,result)&&result==0x1234&&after.used==2&&after.free==1,"middle delete reclaims exact entry");
 Check(fm::Value<std::uint16_t>(after.data+6)==2&&fm::Value<std::uint32_t>(after.data+8)==0&&fm::Value<std::uint16_t>(after.data+12)==0xffff,"middle delete bypass key value sentinel");
 Check(fm::Value<std::uint16_t>(after.data+14)==3&&fm::Value<std::uint16_t>(after.head+10)==2&&fm::Value<std::uint16_t>(after.head+12)==1,"delete free link and native header words");
 auto setDeleted=after;
 Check(fm::PredictSet(setDeleted,9,0x12b4,true,true,0x1234,after,result)&&result==0x12b4&&after.used==3,"SET continuation restores original bits plus native mask");
 Check(!fm::PredictSet(setDeleted,9,0x1034,true,true,0x1234,after,result),"SET continuation cannot clear a previous bit");
 Check(fm::PredictSet(setDeleted,9,0xffff,true,true,0x1234,after,result)&&result==0xffff&&after.used==3,"SET full mask creates native sentinel value");
 Check(!fm::PredictSet(o,9,0x1234,true,false,0xffff,after,result),"SET non-sentinel duplicate requires preceding exact delete");
 auto setMissing=Empty();Check(fm::PredictSet(setMissing,2,0x8000,true,false,0xffff,after,result)&&result==0x8000&&after.used==1,"SET missing lookup inserts zero-extended native mask");
 Check(!fm::PredictSet(setMissing,2,0x18000,true,false,0xffff,after,result),"SET raw missing operand must be zero extended16");
 Check(fm::PredictSet(setMissing,2,0xffff,true,false,0xffff,after,result)&&result==0xffff&&after.used==1,"SET missing full mask retains legitimate sentinel semantics");
 auto setSentinel=after;Check(fm::PredictSet(setSentinel,2,1,true,false,0xffff,after,result)&&result==0xffff&&fm::Same(setSentinel,after),"SET existing sentinel skips deletion and native duplicate keeps it");
 Check(!fm::PredictSet(setSentinel,2,0,false,false,0xffff,after,result),"SET sentinel never qualifies Delete source");
 auto setFull=Empty(1);Add(setFull,7,1);Check(fm::PredictSet(setFull,8,2,true,false,0xffff,after,result)&&result==0xffff&&fm::Same(setFull,after),"SET missing key at full capacity is native unchanged failure");
 Check(!fm::PredictSet(setFull,7,3,true,false,0xffff,after,result),"full non-sentinel duplicate still requires actual delete");
 after=setDeleted;
 auto deleted=after;Check(fm::PredictOrigin(deleted,9,0x1034,true,after,result)&&result==0x1034&&after.used==3&&after.free==3,"masked insert reuses deleted free node");
 Check(fm::Value<std::uint16_t>(after.data+22)==1&&fm::Value<std::uint16_t>(after.data+14)==0xffff,"reinsert appended native collision chain");
 Check(fm::PredictOrigin(o,1,0,false,after,result)&&fm::Value<std::uint16_t>(after.data+8*8+2)==1,"delete bucket head");
 Check(fm::PredictOrigin(o,17,0,false,after,result)&&fm::Value<std::uint16_t>(after.data+14)==0xffff,"delete collision tail");
 Check(fm::PredictDelete(o,404,after,result)&&result==0xffff&&fm::Same(o,after),"generic missing delete unchanged");
 Check(!fm::PredictOrigin(o,404,0,false,after,result),"exact caller refuses missing delete");
 Check(fm::PredictInsert(o,9,0x9876,after,result)&&result==0x1234&&fm::Same(o,after),"native duplicate insertion no rewrite");
 Check(!fm::PredictOrigin(o,9,0x9876,true,after,result),"exact reinsert refuses existing key");
 Check(fm::PredictInsert(o,2,0xabcdef12,after,result)&&result==0xef12,"generic leaf writes only low16 input");
 Check(!fm::PredictOrigin(o,2,0xabcdef12,true,after,result),"exact caller input zero extended16 only");
 Check(!fm::PredictOrigin(o,2,0xffff,true,after,result),"exact caller masked non-sentinel result");
 auto full=Empty(2);Add(full,1,7);Add(full,3,8);Check(fm::PredictInsert(full,1,9,after,result)&&result==0xffff&&fm::Same(full,after),"native full-before-duplicate order");
 Check(!fm::PredictOrigin(full,1,9,true,after,result),"exact reinsert requires genuine free node");
 auto bad=o;bad.capacity=0;Check(!fm::Topology(bad),"division by zero refused");bad=o;bad.capacity=4097;Check(!fm::Topology(bad),"bounded capacity refused");
 bad=o;bad.used=4;Check(!fm::Topology(bad),"used count must match active graph");bad=o;bad.free=8;Check(!fm::Topology(bad),"free head index bounded");
 bad=o;fm::Put(bad.data+22,std::uint16_t{0});Check(!fm::Topology(bad),"active cycle refused");
 bad=o;fm::Put(bad.data+22,std::uint16_t{8});Check(!fm::Topology(bad),"active next index bounded");
 bad=o;fm::Put(bad.data+8,std::uint32_t{1});Check(!fm::Topology(bad),"duplicate key refused");
 bad=o;fm::Put(bad.data+8,std::uint32_t{2});Check(!fm::Topology(bad),"key wrong modulo bucket refused");
 bad=o;fm::Put(bad.data+30,std::uint16_t{3});Check(!fm::Topology(bad),"free cycle refused");
 bad=o;fm::Put(bad.data+24,std::uint32_t{2});Check(fm::Topology(bad),"unused free key need not be initialized");
 bad=o;fm::Put(bad.data+28,std::uint16_t{0});Check(fm::Topology(bad),"unused free value need not be initialized");
 bad=o;fm::Put(bad.data+30,std::uint16_t{0xffff});Check(!fm::Topology(bad),"unreachable node refused");
 bad=o;bad.free=1;Check(!fm::Topology(bad),"active and free alias refused");
 auto randomFree=o;for(unsigned index=3;index<8;++index){fm::Put(randomFree.data+index*8,0xdeadc000u+index);fm::Put(randomFree.data+index*8+4,static_cast<std::uint16_t>(0xabcdef00u+index));}
 Check(fm::Topology(randomFree),"random unused payload preserves full free graph");
 Check(fm::PredictOrigin(randomFree,2,0x5678,true,after,result)&&result==0x5678,"native overwrites selected free payload only");
 Check(!std::memcmp(randomFree.data+4*8,after.data+4*8,4*8),"remaining random free payload unchanged");
 Check(fm::PredictOrigin(randomFree,9,0,false,after,result)&&!std::memcmp(randomFree.data+3*8,after.data+3*8,5*8),"delete leaves all previously free payload unchanged");
 Check(fm::Disjoint(o,protect),"distinct complete slabs allowed");
 for(unsigned region=0;region<3;++region){const auto address=region==0?protect.provider:region==1?protect.slab:protect.word;
  bad=o;bad.header=address;Check(!fm::Disjoint(bad,protect),"map header retained overlap refused");
  bad=o;bad.storage=address-8;Check(!fm::Disjoint(bad,protect),"storage crossing retained start refused");
  bad=o;bad.storage=address+(region==0?0x2370:region==1?0x500:2)-8;Check(!fm::Disjoint(bad,protect),"storage crossing retained end refused");}
 auto invalid=protect;invalid.slab=0;Check(!fm::Disjoint(o,invalid),"missing current provider slab refuses");
 invalid=protect;invalid.provider=std::numeric_limits<std::uintptr_t>::max()-8;Check(!fm::Disjoint(o,invalid),"overflow retained range refuses");
 reader={o};reader.o.storage=reader.o.header+8;fm::Put(reader.o.head,reader.o.storage);Check(!fm::ReadPair(reader,o.header,protect,read),"header storage self alias refuses");
 reader={o};reader.o.storage|=1;fm::Put(reader.o.head,reader.o.storage);Check(!fm::ReadPair(reader,o.header,protect,read),"native storage alignment enforced");
 reader={o};reader.drift=true;Check(!fm::ReadPair(reader,o.header,protect,read),"second header mutation refuses");
 reader={o};reader.fail=true;Check(!fm::ReadPair(reader,o.header,protect,read),"inaccessible node graph refuses");
 reader={o};Check(!fm::ReadPair(reader,o.header+1,protect,read),"unaligned header refuses");
 bad=o;bad.data[79]^=1;Check(!fm::Same(o,bad),"late unrelated storage byte mutation detected");bad=o;bad.head[15]^=1;Check(!fm::Same(o,bad),"unwritten header padding mutation detected");
 CodeReader code{};std::uintptr_t mismatch=1;Check(fm::CodeGate(code,code.image,0xa01d000,mismatch)&&mismatch==0,"seven complete retail spans admit");
 for(const auto& site:ct::sites){code.bad=true;code.corrupt=site.rva;Check(!fm::CodeGate(code,code.image,0xa01d000,mismatch)&&mismatch==site.rva,"late byte every native span refuses");}
 code.bad=false;Check(!fm::CodeGate(code,code.image,0x85c620+192,mismatch),"full caller truncated span refuses");
 Check(!fm::CodeGate(code,code.image,0x85c4d0+195,mismatch),"full SET source truncated span refuses");
 code.fail=true;Check(!fm::CodeGate(code,code.image,0xa01d000,mismatch),"inaccessible code refuses");
 std::printf("owned_flag_map: %u checks, %u failures; pure fixtures only\n",checks,failures);return failures?1:0;
}
