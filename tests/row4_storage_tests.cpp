#include "row4_storage_read.hpp"
#include <map>
#include <vector>
#include <cstring>
#include <iostream>
namespace st=mgscoop::row4_storage;
struct Memory {
 static constexpr std::uintptr_t image=0x140000000ULL,player=0x100000;
 std::map<std::uintptr_t,std::vector<unsigned char>> regions;unsigned reads=0;bool tear=false,outerExtentTear=false;unsigned extentReads=0;std::uintptr_t denied=0;
 static constexpr std::uintptr_t block(unsigned id){return 0x110000+id*0x10000;}
 static constexpr std::uintptr_t state(unsigned id){return block(id)+0x1000;}
 static constexpr std::uintptr_t node(unsigned id){return block(id)+0x2000;}
 static constexpr std::uintptr_t primary(unsigned id){return block(id)+0x3000;}
 static constexpr std::uintptr_t cell(unsigned id){return block(id)+0x4000;}
 static constexpr std::uintptr_t client(unsigned id){return block(id)+0x5000;}
 static constexpr std::uintptr_t slab(unsigned id){return block(id)+0x6000;}
 template<class T>void set(std::uintptr_t p,T x){auto i=regions.upper_bound(p);--i;std::memcpy(i->second.data()+p-i->first,&x,sizeof(x));}
 bool operator()(std::uintptr_t p,void* d,std::size_t n)noexcept{
  if(p==image+0x2a82c30&&++reads==2&&tear)set<std::uint32_t>(slab(1)+0x108,4);
  if(p==client(1)+0x38&&++extentReads==2&&outerExtentTear){const std::uint64_t shortExtent=0x108;std::memcpy(d,&shortExtent,n);return true;}
  if(p==denied)return false;auto i=regions.upper_bound(p);if(i==regions.begin())return false;--i;
  if(p-i->first>i->second.size()||n>i->second.size()-(p-i->first))return false;std::memcpy(d,i->second.data()+p-i->first,n);return true;
 }
 Memory(){regions[image+0x2a82c30].resize(8);regions[player].resize(0x388);set<std::uintptr_t>(image+0x2a82c30,player);set<std::uintptr_t>(player,image+0x2295490);set<std::uint32_t>(player+0x204,1);set<std::uint32_t>(player+0x208,2);set<std::uint32_t>(player+0x20c,2);
  for(unsigned id=0;id<2;++id){regions[block(id)].resize(0x6a8);regions[state(id)].resize(0x410);regions[node(id)].resize(0x70);regions[primary(id)].resize(0x60);regions[cell(id)].resize(0x18);regions[client(id)].resize(0x68);regions[slab(id)].resize(0x110);
   set<std::uintptr_t>(block(id),image+0x2295210);set<std::uint32_t>(block(id)+8,1);set<std::uint32_t>(block(id)+0xc,id);set<std::uintptr_t>(block(id)+0x138,player);set<std::uintptr_t>(block(id)+0x78,state(id));set<std::uintptr_t>(block(id)+0x210,node(id));set<std::uintptr_t>(block(id)+0x3d8,primary(id)+0x30);
   set<std::uintptr_t>(state(id),image+0x23b09f0);set<std::uint32_t>(state(id)+8,id);set<std::uintptr_t>(state(id)+0x408,block(id));set<std::uintptr_t>(node(id),image+0x2306628);set<std::uint32_t>(node(id)+0x20,1);set<std::uint32_t>(node(id)+0x24,id);set<std::uint32_t>(node(id)+0x2c,4);set<std::uintptr_t>(node(id)+0x38,primary(id)+0x30);
   set<std::uintptr_t>(primary(id),image+0x23cd798);set<std::uintptr_t>(primary(id)+0x10,image+0x23cd7c0);set<std::uintptr_t>(primary(id)+0x30,image+0x23cd7f0);set<std::uintptr_t>(primary(id)+8,block(id));set<std::uintptr_t>(primary(id)+0x38,node(id));set<std::uintptr_t>(primary(id)+0x40,cell(id));set<std::uintptr_t>(primary(id)+0x48,client(id)+0x20);
   set<std::uintptr_t>(cell(id)+0x10,client(id));set<std::uintptr_t>(client(id),image+0x2347e50);set<std::uintptr_t>(client(id)+8,0x3dfd3f482579ULL);set<std::uintptr_t>(client(id)+0x10,cell(id));set<std::uintptr_t>(client(id)+0x20,image+0x2347e80);set<std::uintptr_t>(client(id)+0x28,slab(id));set<std::uintptr_t>(client(id)+0x30,slab(id));set<std::uint64_t>(client(id)+0x38,0x110);set<std::uint64_t>(client(id)+0x40,0x110);set<std::uint16_t>(client(id)+0x4a,1);set<std::uintptr_t>(client(id)+0x50,slab(id)+0x30);set<std::uint32_t>(client(id)+0x58,id);
   set<std::uintptr_t>(slab(id),image+0x645940);set<std::uintptr_t>(slab(id)+0x10,slab(id)+0x30);set<std::uint64_t>(slab(id)+0x18,0xe0);set<std::uint64_t>(slab(id)+0x20,1);
  }
 }
};
int main(){unsigned checks=0,failed=0;const auto check=[&](bool ok,const char* n){++checks;if(!ok){++failed;std::cerr<<n<<'\n';}};st::Pair q{};const char* why="unset";
 const auto read=[&](Memory& m){return st::ReadPair(m,Memory::image,Memory::player,Memory::block(0),Memory::block(1),q,why);};
 {Memory m;check(read(m)&&q.body[0].actualId==0&&q.body[1].actualId==1,"both_actual_participants");}
 {Memory m;m.tear=true;check(!read(m)&&q.player==0,"cross_pair_flag_tear");}
 {Memory m;m.set<std::uintptr_t>(Memory::image+0x2a82c30,0);check(!read(m),"global_current_owner");}
 {Memory m;m.set<std::uintptr_t>(Memory::player+0xa8,1);check(!read(m),"offline_actual_provider");}
 {Memory m;m.set<std::uint32_t>(Memory::player+0x214,1);check(!read(m),"real_local0");}
 {Memory m;m.set<std::uint32_t>(Memory::player+0x208,1);check(!read(m),"roster_count");}
 {Memory m;m.set<std::uint32_t>(Memory::player+0x20c,1);check(!read(m),"realized_count");}
 {Memory m;m.set<std::uint32_t>(Memory::player+0x204,0x101);check(!read(m),"invalid_player_flags");}
 {Memory m;m.set<std::uintptr_t>(Memory::client(1)+0x10,Memory::cell(0));check(!read(m),"wrong_native_cell_backlink");}
 {Memory m;m.set<std::uint32_t>(Memory::client(1)+0x58,0);check(!read(m),"relative_body1_id");}
 {Memory m;m.set<std::uint64_t>(Memory::client(1)+0x38,0x107);check(!read(m),"full_E0_record_extent");}
 {Memory m;m.set<std::uint32_t>(Memory::slab(0)+0x108,8);check(!read(m),"local0_pending_transition");}
 {Memory m;m.set<std::uint32_t>(Memory::slab(1)+0x108,8);check(!read(m),"partner_pending_transition");}
 {Memory m;m.denied=Memory::client(0)+0x10;check(!read(m),"missing_retained_association");}
 {Memory m;m.set<std::uintptr_t>(Memory::state(1)+0x408,0);check(!read(m),"actual_PS_block_backlink");}
 {Memory m;check(read(m),"protect_fixture_read");check(st::Protected(q,Memory::client(0),0x68),"local0_client");check(st::Protected(q,Memory::client(1)+0x67,2),"suffix_client_overlap");check(st::Protected(q,Memory::cell(0)+0x17,2),"suffix_plaincell_overlap");check(st::Protected(q,Memory::primary(1)+0x5f,2),"suffix_primary_overlap");check(st::Protected(q,Memory::slab(0)+0x10f,2),"suffix_slab_overlap");check(!st::Protected(q,Memory::slab(0)+0x110,8),"adjacent_slab_nonoverlap");check(!st::Protected(q,0x900000,8),"disjoint_is_footprint_only");check(st::Protected(q,0,8),"invalid_null_range_refuses");check(st::Protected(q,UINTPTR_MAX-2,8),"overflow_refuses");check(st::Protected(q,Memory::client(0),0),"zero_extent_refuses");}
 {Memory m;check(read(m),"identity_fixture_read");auto z=q;z.body[1].flags=4;check(st::Identity(q,z)&&!st::Same(q,z),"dynamic_flags_vs_paired_coherence");z=q;++z.bytes[1];check(!st::Identity(q,z),"allocation_extent_identity");z=q;++z.body[0].client;check(!st::Identity(q,z),"client_identity");z=q;++z.body[1].cell;check(!st::Identity(q,z),"plaincell_identity");z=q;z.body[0].flags=8;check(!st::Valid(z)&&st::Protected(z,0x900000,8),"invalid_held_tuple_refuses");}
 {Memory m;m.outerExtentTear=true;check(!read(m)&&q.player==0,"extent_shrinks_only_at_outer_reread");}
 {Memory m;check(read(m),"extent_forgery_fixture_read");auto z=q;z.bytes[1]=0x108;check(!st::Valid(z)&&st::Protected(z,Memory::slab(1)+0x10f,1),"captured_record_tail_containment");z=q;z.body[0].descriptor=Memory::slab(0)+0x100;check(!st::Valid(z),"captured_descriptor_extent");z=q;z.body[1].records=Memory::slab(1)+0x40;check(!st::Valid(z),"captured_record_extent");z=q;z.body[0].descriptor=Memory::slab(0)+0x30;check(!st::Valid(z),"captured_descriptor_record_alias");}
 std::cout<<checks<<" checks, "<<failed<<" failures\n";return failed?1:0;
}
