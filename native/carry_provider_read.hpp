#pragma once
#include "carry_plugin_read.hpp"
#include "carry_provider_contract.hpp"
namespace mgscoop::carry_provider_read {
using carry_plugin_read::Scope;
using carry_plugin_read::ScopeRefusal;
using carry_plugin_read::Get;
using carry_plugin_read::Range;
using carry_plugin_read::Aligned;
struct Observation {std::uintptr_t context=0,locator=0,services=0,provider=0,vtable=0;std::uint32_t flags=0;};
struct Reply {bool ready=false,present=false;std::uint32_t vtableRva=0;const char* reason="disabled";};
template<class Reader> bool CodeGate(Reader& r,std::uintptr_t base,std::size_t span,std::uintptr_t& mismatch) noexcept {
 mismatch=0;if(!Range(base,span))return false;unsigned char buffer[8192]{};
 for(const auto& site:carry_provider_contract::sites){if(site.rva>span||site.size>span-site.rva||site.size>sizeof(buffer)||!r(base+site.rva,buffer,site.size)){mismatch=site.rva;return false;}for(std::size_t j=0;j<site.size;++j)if(buffer[j]!=site.bytes[j]){mismatch=site.rva;return false;}}
 for(const auto& slot:carry_provider_contract::slots){auto off=slot.vtable_rva+slot.offset;std::uintptr_t vt=0;if(off>span||sizeof(vt)>span-off||!Get(r,base+off,vt)||vt!=base+slot.target_rva){mismatch=off;return false;}}return true;
}
// Diagnostic only: follows initialized pointers. It never invokes the lazy
// locator getter or any provider method, and never exports native pointers.
template<class Reader> bool ReadOnce(Reader& r,const Scope& s,std::size_t span,Observation& o,const char*& why) noexcept {
 o={};why="provider_scope";if(ScopeRefusal(s)||!Range(s.player,0x388)||!Range(s.data,0x1f0)||!Range(s.state,0x48)||!Range(s.image,span)||span<0x2c3a8a8||span>0x40000000)return false;
 std::uintptr_t vt=0,q=0,st=0,sync=0;std::uint32_t count=0,local=0;
 why="provider_player";if(!Get(r,s.player,vt)||vt!=s.image+0x2295490||!Get(r,s.player+0x60,q)||q!=s.data||!Get(r,q,vt)||vt!=s.image+0x23ad970||!Get(r,q+0x1e8,st)||st!=s.state||!Get(r,st+0x40,count)||count!=2||!Get(r,s.player+0x204,o.flags)||!(o.flags&1u)||(o.flags&0x100u)||!Get(r,s.player+0xa8,sync)||sync||!Get(r,s.player+0x208,count)||count!=2||!Get(r,s.player+0x20c,count)||count!=2||!Get(r,s.player+0x214,local)||local)return false;
 why="provider_context";if(!Get(r,s.image+0x2c3a8a0,o.context)||!Aligned(o.context,8)||!Range(o.context,0x10)||!Get(r,o.context,vt)||vt!=s.image+0x2308648)return false;
 why="provider_locator";if(!Get(r,o.context+8,o.locator)||!Aligned(o.locator,8)||!Range(o.locator,0xa0))return false;
 why="provider_services";if(!Get(r,o.locator+0x98,o.services)||!Aligned(o.services,8)||!Get(r,o.services,vt)||vt!=s.image+0x2294c30||!Range(o.services,0x100))return false;
 why="provider_pointer";if(!Get(r,o.services+0xf8,o.provider))return false;if(!o.provider){why="missing_provider";return true;}
 why="provider_vtable";if(!Aligned(o.provider,8)||!Get(r,o.provider,o.vtable)||!Aligned(o.vtable,8)||o.vtable<s.image||o.vtable-s.image>span-sizeof(std::uintptr_t))return false;
 why="ready";return true;
}
inline bool Same(const Observation& a,const Observation& b) noexcept {return a.context==b.context&&a.locator==b.locator&&a.services==b.services&&a.provider==b.provider&&a.vtable==b.vtable&&a.flags==b.flags;}
template<class Reader> bool ReadPair(Reader& r,const Scope& s,std::size_t span,Reply& out) noexcept {
 out={};if(const char* why=ScopeRefusal(s)){out.reason=why;return false;}Observation a{},b{};const char* why=nullptr;
 if(!ReadOnce(r,s,span,a,why)||!ReadOnce(r,s,span,b,why)){out.reason=why;return false;}if(!Same(a,b)){out.reason="provider_changed";return false;}
 out.ready=true;out.present=a.provider!=0;out.reason=out.present?"ready":"missing_provider";if(out.present)out.vtableRva=static_cast<std::uint32_t>(a.vtable-s.image);return true;
}
} // namespace mgscoop::carry_provider_read
