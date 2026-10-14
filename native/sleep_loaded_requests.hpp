#pragma once
#include "sleep_graph_read.hpp"
namespace mgscoop::sleep_loaded_requests {
// Bounded read of node-request names in the current typed graph0 cache.
// These names prove graph node availability, not resource/clip readiness.
inline constexpr std::uint64_t hashes[]={0x531872a59848ull,0x565a6ead28c6ull,0x89349f0ff8a2ull,0x9e3bd6cce5f5ull,0xb1c6aee936a4ull,0xb62a05a085a4ull,0xb6c742e88a85ull,0xbd5546f99202ull,0xc31867300b57ull,0xc37aca2100e9ull,0xd7654fb0a3f2ull,0xe66c3c2d192aull,0xe6d3ed730317ull,0xe6d6abd46423ull,0xeceee0f51e77ull,0xf2364261c9d4ull};
struct Reply {bool ready=false;std::uint16_t found=0;std::uint32_t examined=0;const char* reason="sleep_requests_refused";};
template<class Reader> bool Read(Reader& reader,const sleep_graph_read::Graph& g,Reply& out) noexcept {
 out={};using namespace sleep_graph_read;
 if(!plugin::Range(g.mog,kMogCeiling)||!plugin::Aligned(g.mog,8)||!g.currentNode||!g.lifetime||!g.count||g.count>16384||!MogRange(g.tableOffset,std::size_t(g.count)*0x48))return false;
 for(std::uint32_t index=0;index<g.count;++index){
  const auto field=g.tableOffset+index*0x48+0x18;std::int32_t relative=0;std::uint32_t offset=0;std::uint64_t hash=0;
  if(!plugin::Get(reader,g.mog+field,relative)||!Relative(field,relative,8,offset)||!plugin::Get(reader,g.mog+offset,hash)){out.reason="sleep_requests_node_read";return false;}
  hash&=0xffffffffffffull;for(unsigned k=0;k<16;++k)if(hash==hashes[k])out.found|=std::uint16_t(1u<<k);++out.examined;
 }
 out.ready=true;out.reason=out.found==0xffff?"sleep_requests_present":"sleep_requests_missing";return true;
}
// Caller must place the entire scan between matching typed graph/controller,
// natural owner, WindowSeq and admission observations. Never use resident files
// or cache pointer alone to set the experimental requestsPresent prerequisite.
} // namespace mgscoop::sleep_loaded_requests
