#pragma once
#include "sleep_plugin_read.hpp"
#include "sleep_graph_contract.hpp"
namespace mgscoop::sleep_graph_read {
namespace plugin=sleep_plugin_read;
using Scope=plugin::Scope;
struct Graph {
 std::uintptr_t cs=0,entries=0,entry=0,vector=0,controller=0,lifetime=0,mog=0,node=0,hashAt=0;
 std::uint32_t count=0,index=0,tableOffset=0,headerOffset=0,layers=0;
 std::uint64_t stateHash=0;
 bool currentNode=false,sleepHashCandidate=false;
};
struct Observation {plugin::Observation owner;Graph graph[2];};
struct GraphReply {
 std::uint32_t count=0,index=0;
 std::uint64_t stateHash=0;
 bool currentNode=false,sleepHashCandidate=false;
};
struct Reply {bool ready=false;GraphReply bodies[2];const char* reason="disabled";};
// All displacement-derived MOG reads are limited to four MiB from the live
// cache. This bound is a refusal ceiling, not a claim of resource size/identity.
inline constexpr std::uint32_t kMogCeiling=0x400000;
inline bool MogRange(std::uint32_t offset,std::size_t count) noexcept {return offset<=kMogCeiling&&count<=kMogCeiling-offset;}
inline bool Relative(std::uint32_t field,std::int32_t displacement,std::size_t bytes,std::uint32_t& offset) noexcept {
 const auto value=std::int64_t(field)+displacement;if(value<0||value>kMogCeiling)return false;offset=static_cast<std::uint32_t>(value);return MogRange(offset,bytes);
}
inline bool SleepHashCandidate(std::uint64_t hash) noexcept {
 constexpr std::uint64_t values[]={0x531872a59848ull,0x565a6ead28c6ull,0x89349f0ff8a2ull,0x9e3bd6cce5f5ull,0xb1c6aee936a4ull,0xb62a05a085a4ull,0xb6c742e88a85ull,0xbd5546f99202ull,0xc31867300b57ull,0xc37aca2100e9ull,0xd7654fb0a3f2ull,0xe66c3c2d192aull,0xe6d3ed730317ull,0xe6d6abd46423ull,0xeceee0f51e77ull,0xf2364261c9d4ull};for(auto value:values)if(hash==value)return true;return false;
}
template<class Reader> bool CodeGate(Reader& r,std::uintptr_t base,std::size_t span,std::uintptr_t& mismatch) noexcept {
 mismatch=0;if(!plugin::Range(base,span))return false;unsigned char buffer[8192]{};
 for(const auto& site:sleep_graph_contract::sites){if(site.rva>span||site.size>span-site.rva||site.size>sizeof(buffer)||!r(base+site.rva,buffer,site.size)){mismatch=site.rva;return false;}for(std::size_t j=0;j<site.size;++j)if(buffer[j]!=site.bytes[j]){mismatch=site.rva;return false;}}
 for(const auto& slot:sleep_graph_contract::slots){const auto offset=slot.vtable_rva+slot.offset;std::uintptr_t value=0;if(offset>span||sizeof(value)>span-offset||!plugin::Get(r,base+offset,value)||value!=base+slot.target_rva){mismatch=offset;return false;}}return true;
}
// Scope requires both this graph gate and the existing full sleep/plugin/PE
// gate before exactGate=true. This does not call the getter; it reads the
// complete pinned getter's existing fields under the natural owner lease.
template<class Reader> bool ReadOnce(Reader& r,const Scope& s,Observation& out,const char** refusal=nullptr) noexcept {
 out={};const auto stage=[&](const char* why) noexcept {if(refusal)*refusal=why;};
 if(!plugin::ReadOnce(r,s,out.owner,refusal))return false;
 std::uintptr_t vt=0,owner=0;std::uint32_t base=0,count=0,layer=0;std::uint8_t reload=0;
 for(unsigned k=0;k<2;++k){const auto& b=out.owner.body[k];auto& g=out.graph[k];stage(k?"graph_cs1":"graph_cs0");
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
 stage("graph_aliases");if(out.graph[0].cs==out.graph[1].cs||out.graph[0].entries==out.graph[1].entries||out.graph[0].vector==out.graph[1].vector||out.graph[0].controller==out.graph[1].controller)return false;
 stage("ready");return true;
}
inline bool Same(const Observation& a,const Observation& b) noexcept {
 if(!plugin::Same(a.owner,b.owner))return false;
 for(unsigned k=0;k<2;++k){const auto& x=a.graph[k];const auto& y=b.graph[k];if(x.cs!=y.cs||x.entries!=y.entries||x.entry!=y.entry||x.vector!=y.vector||x.controller!=y.controller||x.lifetime!=y.lifetime||x.mog!=y.mog||x.node!=y.node||x.hashAt!=y.hashAt||x.count!=y.count||x.layers!=y.layers||x.index!=y.index||x.tableOffset!=y.tableOffset||x.headerOffset!=y.headerOffset||x.stateHash!=y.stateHash||x.currentNode!=y.currentNode||x.sleepHashCandidate!=y.sleepHashCandidate)return false;}return true;
}
template<class Reader> bool ReadPair(Reader& r,const Scope& s,Reply& out) noexcept {
 out={};if(const char* why=plugin::ScopeRefusal(s)){out.reason=why;return false;}
 Observation a{},b{};const char* why="graph_chain";if(!ReadOnce(r,s,a,&why)||!ReadOnce(r,s,b,&why)){out.reason=why;return false;}if(!Same(a,b)){out.reason="graph_changed";return false;}
 for(unsigned k=0;k<2;++k){const auto& g=a.graph[k];auto& reply=out.bodies[k];reply.count=g.count;reply.index=g.index;reply.stateHash=g.stateHash;reply.currentNode=g.currentNode;reply.sleepHashCandidate=g.sleepHashCandidate;}
 out.ready=true;out.reason="graph_observed";return true;
}
} // namespace mgscoop::sleep_graph_read
