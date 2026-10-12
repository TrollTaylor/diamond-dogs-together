#pragma once
#include "sleep_graph_read.hpp"
#include "sleep_partner_stance_contract.hpp"
namespace mgscoop::sleep_parts_plugin {
using namespace sleep_plugin_read;
template<class Reader> bool ReadOnce(Reader& r,const Scope& s,Observation& out,const char** refusal=nullptr) noexcept {
 const auto stage=[&](const char* value) noexcept {if(refusal)*refusal=value;};stage("scope");
 out={};if(ScopeRefusal(s)||!Range(s.image,0xa01d000)||!Range(s.player,0x388)||!Range(s.data,0x1f0)||!Range(s.state,0x48))return false;
 stage("player");out.player=s.player;out.data=s.data;out.state=s.state;
 std::uintptr_t live=0,goVT=0;std::uint32_t selected=UINT32_MAX;
 if(s.player<0x20||!Get(r,s.image+0x2a82c30,live)||live!=s.player||
 !Get(r,s.player-0x20,goVT)||goVT!=s.image+0x22953d0||!Get(r,s.player+0x218,selected)||selected)return false;
 std::uintptr_t vt=0,q=0,st=0,sync=0;std::uint32_t count=0,realized=0,local=0;
 if(!Get(r,s.player,vt)||vt!=s.image+0x2295490||!Get(r,s.player+0x60,q)||q!=s.data||!Get(r,q,vt)||vt!=s.image+0x23ad970||!Get(r,q+0x1e8,st)||st!=s.state||!Get(r,st+0x40,count)||count!=2||!Get(r,s.player+0xa8,sync)||sync||!Get(r,s.player+0x204,out.playerFlags)||!(out.playerFlags&1u)||(out.playerFlags&0x100u)||!Get(r,s.player+0x208,count)||count!=2||!Get(r,s.player+0x20c,realized)||realized!=2||!Get(r,s.player+0x214,local)||local)return false;
 stage("arrays");if(!Get(r,s.player+0x70,out.ids)||!Aligned(out.ids,2)||!Get(r,s.player+0x78,out.pool)||!out.pool||!Get(r,s.player+0x90,out.parts)||!out.parts||!Get(r,s.player+0x378,out.blocks)||!Aligned(out.blocks,8)||!Get(r,s.player+0x50,out.blockTable)||!Aligned(out.blockTable,8)||!Get(r,q+8,out.lifeRecords)||!Aligned(out.lifeRecords,4)||!Get(r,q+0x18,out.qActions)||!Aligned(out.qActions,2))return false;
 if(!Range(out.ids,4)||!Range(out.pool,2)||!Range(out.parts,2)||!Range(out.blocks,16*0x6a8)||!Range(out.blockTable,16*8)||!Range(out.lifeRecords,2*0x4c)||!Range(out.qActions,4))return false;
 std::uint8_t pools[2]{};
 for(unsigned k=1;k<2;++k){stage(k?"instance1":"instance0");auto& b=out.body[k];std::uint16_t id=0;std::uint8_t part=0;
  if(!Get(r,out.ids+k*2,id)||id!=k||!Get(r,out.pool+k,pools[k])||pools[k]>=16||!Get(r,out.parts+k,part)||part!=3)return false;
  b.id=id;b.block=out.blocks+std::uintptr_t(pools[k])*0x6a8;b.lifeRecord=out.lifeRecords+k*0x4c;
  stage(k?"block_ps1":"block_ps0");std::uintptr_t block2=0,owner=0;std::uint32_t base=0,one=0;
  if(!Get(r,out.blockTable+pools[k]*8,block2)||block2!=b.block||!Get(r,b.block,vt)||vt!=s.image+0x2295210||!Get(r,b.block+0x138,owner)||owner!=s.player||!Get(r,b.block+8,one)||one!=1||!Get(r,b.block+0xc,base)||base!=id||!Get(r,b.block+0x78,b.ps)||!Aligned(b.ps,8)||!Range(b.ps,0x510)||!Get(r,b.ps,vt)||(vt!=s.image+0x23b09f0&&vt!=s.image+0x23b0c70)||!Get(r,b.ps+0x408,owner)||owner!=b.block||!Get(r,b.ps+8,base)||base!=id)return false;
  stage(k?"sleep_slot1":"sleep_slot0");if(!Get(r,b.ps+0xd8,b.interface)||!Aligned(b.interface,8)||!Get(r,b.interface,vt))return false;
  stage(k?"sleep_type1":"sleep_type0");const auto adjustment=CompleteOffset(s.image,vt);
  if(adjustment==std::numeric_limits<std::uintptr_t>::max()||b.interface<adjustment)return false;
  b.primary=b.interface-adjustment;
  stage(k?"sleep_primary1":"sleep_primary0");if(!Aligned(b.primary,8)||!Range(b.primary,0x80)||!Get(r,b.primary,vt)||vt!=s.image+0x23d5c18||!Get(r,b.primary+0x10,vt)||vt!=s.image+0x23d5c40||!Get(r,b.primary+0x30,vt)||vt!=s.image+0x23d5c70||!Get(r,b.primary+0x48,vt)||vt!=s.image+0x23d5cb0||!Get(r,b.primary+8,owner)||owner!=b.block)return false;
  stage(k?"sleep_node1":"sleep_node0");if(!Get(r,b.ps+0x500,b.node)||!Aligned(b.node,8)||!Range(b.node,0x70)||!Get(r,b.node,vt)||vt!=s.image+0x2306628||!Get(r,b.node+0x20,one)||one!=1||!Get(r,b.node+0x24,base)||base!=id||!Get(r,b.primary+0x38,owner)||owner!=b.node||!Get(r,b.node+0x38,owner)||owner!=b.primary+0x30||!Get(r,b.node+0x28,b.nodeState)||!Get(r,b.node+0x2c,b.nodeKind))return false;
  stage(k?"sleep_records1":"sleep_records0");if(!Get(r,b.primary+0x78,b.records)||!Aligned(b.records,16)||!Range(b.records,0x40)||!Get(r,b.node+8,b.bitmap)||!Aligned(b.bitmap,4))return false;
  stage(k?"sleep_bitmap1":"sleep_bitmap0");std::uint32_t bits=0;if(!Get(r,b.bitmap,bits))return false;b.nodeActive=(bits&1u)!=0;
  stage(k?"sleep_record1":"sleep_record0");if(!Get(r,b.records,b.callback)||!Get(r,b.records+8,b.adjustment)||b.adjustment||!Get(r,b.records+0x10,b.mode)||b.mode>6||!Get(r,b.records+0x11,b.requested)||b.requested>6||!Get(r,b.records+0x12,b.counter)||!Get(r,b.records+0x14,b.flags)||!Get(r,b.records+0x20,b.subtype)||!Get(r,b.records+0x30,b.recordFlags))return false;
  stage(k?"sleep_callback1":"sleep_callback0");b.callbackInitialized=b.callback!=0;
  if(b.callback!=ExpectedCallback(s.image,b.mode)&&(b.mode||b.callback))return false;
  stage(k?"sleep_action_life1":"sleep_action_life0");if(!Get(r,b.ps+0x2d8,b.actionBytes)||!b.actionBytes||!Get(r,b.actionBytes,b.action)||!Get(r,out.qActions+k*2,b.qAction)||!Get(r,b.lifeRecord+0x34,b.lifeState)||b.lifeState>2)return false;
 }
 stage("ready");return true;
}

}
namespace mgscoop::sleep_parts_graph {
namespace plugin=sleep_parts_plugin;using Scope=plugin::Scope;using Graph=sleep_graph_read::Graph;
struct Observation {sleep_plugin_read::Observation owner;Graph graph[2];};
using sleep_graph_read::kMogCeiling;using sleep_graph_read::Relative;using sleep_graph_read::SleepHashCandidate;
template<class Reader> bool ReadOnce(Reader& r,const Scope& s,Observation& out,const char** refusal=nullptr) noexcept {
 out={};const auto stage=[&](const char* why) noexcept {if(refusal)*refusal=why;};
 if(!plugin::ReadOnce(r,s,out.owner,refusal))return false;
 std::uintptr_t vt=0,owner=0;std::uint32_t base=0,count=0,layer=0;std::uint8_t reload=0;
 for(unsigned k=1;k<2;++k){const auto& b=out.owner.body[k];auto& g=out.graph[k];stage(k?"graph_cs1":"graph_cs0");
  if(!plugin::Get(r,b.ps+0x238,g.cs)||!plugin::Aligned(g.cs,8)||!plugin::Range(g.cs,0x70)||!plugin::Get(r,g.cs,vt)||vt!=s.image+0x22e6070||!plugin::Get(r,b.block+0x10,owner)||owner!=g.cs||!plugin::Get(r,g.cs+8,count)||count!=1||!plugin::Get(r,g.cs+0xc,base)||base!=b.id||!plugin::Get(r,g.cs+0x10,g.entries)||!plugin::Aligned(g.entries,16)||!plugin::Range(g.entries,0xc0))return false;
  g.entry=g.entries;stage(k?"graph_controller1":"graph_controller0");
  if(!plugin::Get(r,g.entry+0x90,g.vector)||!plugin::Aligned(g.vector,8)||!plugin::Get(r,g.vector,g.controller)||!plugin::Aligned(g.controller,8)||!plugin::Range(g.controller,0x240)||!plugin::Get(r,g.controller,vt)||vt!=s.image+0x250ba50||!plugin::Get(r,g.controller+0x10,vt)||vt!=s.image+0x250bc00||!plugin::Get(r,g.controller+0xd4,layer)||layer||!plugin::Get(r,g.controller+0x185,reload)||reload||!plugin::Get(r,g.controller+0xf0,g.lifetime)||!g.lifetime||!plugin::Get(r,g.controller+0x1b8,g.mog)||!plugin::Aligned(g.mog,8)||!plugin::Range(g.mog,kMogCeiling)||!plugin::Get(r,g.controller+0x1a0,g.node))return false;
  stage(k?"graph_header1":"graph_header0");unsigned char magic[15]{};constexpr unsigned char expected[15]={'F','O','X','M','O','T','I','O','N','G','R','A','P','H',0};
  if(!r(g.mog,magic,sizeof(magic)))return false;for(unsigned j=0;j<15;++j)if(magic[j]!=expected[j])return false;
  std::uint32_t version=0;std::int32_t displacement=0;
  if(!plugin::Get(r,g.mog+0x10,version)||version!=6||!plugin::Get(r,g.mog+0x1c,g.layers)||!g.layers||g.layers>32||!plugin::Get(r,g.mog+0x20,displacement)||!Relative(0x1c,displacement,0x38,g.headerOffset)||!plugin::Get(r,g.mog+g.headerOffset+0xc,g.count)||!g.count||g.count>16384||!plugin::Get(r,g.mog+g.headerOffset+0x10,displacement)||!Relative(g.headerOffset+0x10,displacement,std::size_t(g.count)*0x48,g.tableOffset))return false;
  // Null current node is an initialized controller with no current state,
  // never evidence that an onset/rest clip has been selected or is playing.
  if(!g.node)continue;stage(k?"graph_current1":"graph_current0");
  if(g.node<g.mog)return false;const auto offset=g.node-g.mog;
  if(offset<g.tableOffset||offset-g.tableOffset>=std::size_t(g.count)*0x48||(offset-g.tableOffset)%0x48)return false;
  g.index=static_cast<std::uint32_t>((offset-g.tableOffset)/0x48);
  std::uint32_t hashOffset=0;if(!plugin::Get(r,g.node+0x18,displacement)||!Relative(static_cast<std::uint32_t>(offset)+0x18,displacement,8,hashOffset))return false;
  g.hashAt=g.mog+hashOffset;if(!plugin::Get(r,g.hashAt,g.stateHash))return false;
  g.stateHash&=0xffffffffffffull;g.currentNode=true;g.sleepHashCandidate=SleepHashCandidate(g.stateHash);
 }
 
 stage("ready");return true;
}
inline bool Same(const Observation& a,const Observation& b) noexcept {
 if(!plugin::Same(a.owner,b.owner))return false;
 for(unsigned k=1;k<2;++k){const auto& x=a.graph[k];const auto& y=b.graph[k];if(x.cs!=y.cs||x.entries!=y.entries||x.entry!=y.entry||x.vector!=y.vector||x.controller!=y.controller||x.lifetime!=y.lifetime||x.mog!=y.mog||x.node!=y.node||x.hashAt!=y.hashAt||x.count!=y.count||x.layers!=y.layers||x.index!=y.index||x.tableOffset!=y.tableOffset||x.headerOffset!=y.headerOffset||x.stateHash!=y.stateHash||x.currentNode!=y.currentNode||x.sleepHashCandidate!=y.sleepHashCandidate)return false;}return true;
}

struct Sample {Observation observed{};float health=0,maximumHealth=0,stun=0,maximumStun=0;std::uint32_t psPacked=0;std::uint8_t qStance=0;};
template<class R>bool Read(R& r,const Scope& scope,Sample& out,const char** why=nullptr)noexcept {
 out={};if(!ReadOnce(r,scope,out.observed,why))return false;const auto& b=out.observed.owner.body[1];std::uintptr_t ps=0,q=0;
 return plugin::Get(r,b.lifeRecord+0x24,out.health)&&plugin::Get(r,b.lifeRecord+0x28,out.maximumHealth)&&
 plugin::Get(r,b.lifeRecord+0x2c,out.stun)&&plugin::Get(r,b.lifeRecord+0x30,out.maximumStun)&&
 plugin::Get(r,b.ps+sleep_partner_stance_contract::packedOffset,ps)&&plugin::Aligned(ps,8)&&
 plugin::Range(ps,8)&&plugin::Get(r,ps,out.psPacked)&&plugin::Get(r,scope.data+0x30,q)&&q&&plugin::Get(r,q+1,out.qStance)&&
 (out.psPacked&15u)==out.qStance;
}
inline bool Same(const Sample& a,const Sample& b)noexcept {
 return Same(a.observed,b.observed)&&a.health==b.health&&a.maximumHealth==b.maximumHealth&&a.stun==b.stun&&
 a.maximumStun==b.maximumStun&&a.psPacked==b.psPacked&&a.qStance==b.qStance;
}
}
