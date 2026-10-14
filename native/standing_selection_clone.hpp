#pragma once
// Pure byte builder. It does not allocate executable memory, call an engine
// method, patch a detour, change an argument, or authorize an episode.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include "standing_selection_clone_contract.hpp"
namespace mgscoop::standing_selection_clone {
namespace c=standing_selection_clone_contract;
inline constexpr unsigned kBytes=0x2000,kRaw=0,kSetup=0x200,kRawStub=0x700,kSetupStub=0x780,kIslands=0x900,kUnwinds=0xc00,kTable=0x1100;
struct Function {std::uint32_t begin=0,end=0,unwind=0;};
static_assert(sizeof(Function)==12,"Windows x64 RUNTIME_FUNCTION layout");
struct Image {std::array<unsigned char,kBytes> bytes{};unsigned functions=0,islands=0,rawStubEnd=0,setupStubEnd=0;bool ready=false;const char* why="empty";};
struct Addresses {std::uintptr_t game=0,clone=0,probe=0;};
inline bool Add(std::uintptr_t base,std::uintptr_t offset,std::uintptr_t& value) noexcept {
 if(base>std::numeric_limits<std::uintptr_t>::max()-offset)return false;value=base+offset;return true;
}
inline bool Relative(std::uintptr_t from,std::uintptr_t to,std::int32_t& value) noexcept {
 if(to>=from){const auto n=to-from;if(n>0x7fffffffull)return false;value=static_cast<std::int32_t>(n);}
 else{const auto n=from-to;if(n>0x80000000ull)return false;value=n==0x80000000ull?std::numeric_limits<std::int32_t>::min():-static_cast<std::int32_t>(n);}return true;
}
template<class T> inline void Store(Image& out,unsigned at,const T& value) noexcept {std::memcpy(out.bytes.data()+at,&value,sizeof(value));}
inline bool Disp(Image& out,const Addresses& a,unsigned field,unsigned next,std::uintptr_t target) noexcept {
 std::uintptr_t from=0;std::int32_t value=0;if(!Add(a.clone,next,from)||!Relative(from,target,value)){out.why="rel32_range";return false;}Store(out,field,value);return true;
}
struct Writer {
 Image& out;const Addresses& addresses;unsigned at;bool good=true;
 void Byte(unsigned char b) noexcept {if(at>=kIslands){good=false;return;}out.bytes[at++]=b;}
 void Bytes(const unsigned char* b,unsigned n) noexcept {for(unsigned i=0;i<n;++i)Byte(b[i]);}
 template<std::size_t N>void Bytes(const unsigned char(&b)[N]) noexcept {Bytes(b,static_cast<unsigned>(N));}
 void U32(std::uint32_t n) noexcept {for(unsigned i=0;i<4;++i)Byte(static_cast<unsigned char>(n>>(i*8)));}
 unsigned Jump(unsigned char op,unsigned target) noexcept {const unsigned site=at;Byte(op);U32(0);std::uintptr_t actual=0;good=good&&Add(addresses.clone,target,actual)&&Disp(out,addresses,site+1,site+5,actual);return site;}
 unsigned Conditional(unsigned char op,unsigned target=0) noexcept {const unsigned site=at;Byte(0x0f);Byte(op);U32(0);if(target)FixConditional(site,target);return site;}
 void FixConditional(unsigned site,unsigned target) noexcept {std::uintptr_t actual=0;good=good&&Add(addresses.clone,target,actual)&&Disp(out,addresses,site+2,site+6,actual);}
};
inline unsigned Island(Image& out,std::uintptr_t target) noexcept {
 // CALL rel32 -> FF25 absolute tail jump preserves all argument registers and
 // the original CALL return address. Islands do not establish a stack frame.
 const unsigned at=kIslands+out.islands*16;if(at+16>kUnwinds)return UINT32_MAX;
 out.bytes[at]=0xff;out.bytes[at+1]=0x25;Store(out,at+6,target);++out.islands;return at;
}
inline bool Call(Writer& w,std::uintptr_t target) noexcept {const auto at=Island(w.out,target);if(at==UINT32_MAX)return false;w.Jump(0xe8,at);return w.good;}
template<std::size_t N> bool Relocate(Image& out,const Addresses& a,unsigned start,const c::Reloc(&rs)[N]) noexcept {
 for(const auto& r:rs){if(r.displacementSize!=4)return false;std::uintptr_t target=0;if(!Add(a.game,r.target,target))return false;
  if(!r.rip){const auto island=Island(out,target);if(island==UINT32_MAX||!Add(a.clone,island,target))return false;}
  if(!Disp(out,a,start+r.offset+r.displacementOffset,start+r.offset+r.size,target))return false;
 }return true;
}
inline void Entry(Image& out,unsigned begin,unsigned end,unsigned unwind) noexcept {
 const Function fn{begin,end,unwind};Store(out,kTable+out.functions*sizeof(Function),fn);++out.functions;
}
inline unsigned Align4(unsigned n) noexcept {return(n+3)&~3u;}
template<std::size_t N>bool Unwind(Image& out,const Addresses& a,unsigned base,const c::Unwind(&us)[N],unsigned& next,unsigned& first) noexcept {
 first=next;
 for(const auto& u:us){const unsigned at=next;if(u.size>kTable-at)return false;std::memcpy(out.bytes.data()+at,u.data,u.size);
  const unsigned flags=u.data[0]>>3,codes=u.data[2],trailer=4+((codes+1)&~1u)*2;
  if(flags&4){if(u.size!=trailer+12)return false;
   // All native target chains refer to the first setup RUNTIME_FUNCTION.
   const Function chained{base+us[0].begin,base+us[0].end,first};Store(out,at+trailer,chained);
  }else if(flags&3){if(u.size!=trailer+8)return false;std::uint32_t handler=0;std::memcpy(&handler,u.data+trailer,4);if(handler!=0x1a05f0c)return false;
   std::uintptr_t actual=0;if(!Add(a.game,handler,actual))return false;const auto island=Island(out,actual);if(island==UINT32_MAX)return false;Store(out,at+trailer,island);
   // Complete original four-byte GS offset follows; remains 0x490.
  }else if(u.size!=trailer)return false;
  Entry(out,base+u.begin,base+u.end,at);next=Align4(at+u.size);
 }return true;
}
inline bool ChainStub(Image& out,unsigned begin,unsigned end,unsigned rootBegin,unsigned rootEnd,unsigned rootUnwind,unsigned& next) noexcept {
 if(next+16>kTable)return false;out.bytes[next]=0x21; // version1 + UNW_FLAG_CHAININFO
 const Function root{rootBegin,rootEnd,rootUnwind};Store(out,next+4,root);Entry(out,begin,end,next);next+=16;return true;
}
inline bool Build(const Addresses& a,Image& out) noexcept {
 out={};out.why="addresses";if(!a.game||!a.clone||!a.probe||(a.clone&0xfu))return false;
 std::uintptr_t limit=0;if(!Add(a.clone,kBytes,limit)||!Add(a.game,0xa01d000,limit))return false;
 std::memcpy(out.bytes.data()+kRaw,c::rawBytes,sizeof(c::rawBytes));std::memcpy(out.bytes.data()+kSetup,c::setupBytes,sizeof(c::setupBytes));
 if(!Relocate(out,a,kRaw,c::rawRelocs)||!Relocate(out,a,kSetup,c::setupRelocs))return false;
 // Raw patch replaces exactly XOR EBX,EBX; MOV RDI,RCX after its unchanged
 // native prologue. No original branch targets the interior of these 5B.
 Writer rawPatch{out,a,kRaw+0xf};rawPatch.Jump(0xe9,kRawStub);
 Writer r{out,a,kRawStub};
 const unsigned char rawInit[]={0x33,0xdb,0x48,0x8b,0xf9,0x48,0x8b,0xcf,0x48,0x8b,0x54,0x24,0x38,0x45,0x33,0xc0};r.Bytes(rawInit);
 if(!Call(r,a.probe))return false;r.Bytes({0x48,0x85,0xc0});const auto rawMiss1=r.Conditional(0x84);
 r.Bytes({0x48,0x8b,0xc8,0x48,0x8b,0x54,0x24,0x38});std::uintptr_t find=0;if(!Add(a.game,0x1a754c0,find)||!Call(r,find))return false;
 r.Bytes({0x48,0x85,0xc0});r.Conditional(0x85,kRaw+0x61);const auto rawFallback=r.at;r.FixConditional(rawMiss1,rawFallback);
 r.Bytes({0x48,0x8b,0xcf,0x48,0x8b,0x54,0x24,0x38,0x33,0xdb});r.Jump(0xe9,kRaw+0x14);out.rawStubEnd=r.at;
 // Setup replaces exactly MOV EDI,ESI; MOV [RSP+88],RAX. Original global
 // fallback RAX is saved BEFORE either added call. ESI/native args stay
 // unchanged. A miss recomputes the original capacity flags and resumes the
 // original JBE/search0; a hit takes the ORIGINAL collected-source path.
 Writer setupPatch{out,a,kSetup+0xdf};setupPatch.Jump(0xe9,kSetupStub);for(unsigned i=0;i<5;++i)setupPatch.Byte(0x90);
 Writer s{out,a,kSetupStub};s.Bytes({0x8b,0xfe,0x48,0x89,0x84,0x24,0x88,0,0,0,0x49,0x8b,0xce,0x48,0x8b,0xd3,0x41,0xb8,1,0,0,0});
 if(!Call(s,a.probe))return false;s.Bytes({0x48,0x85,0xc0});const auto setupMiss=s.Conditional(0x84);
 s.Bytes({0x48,0x8b,0xc8,0x48,0x8b,0xd3});if(!Call(s,find))return false;s.Bytes({0x48,0x85,0xc0});s.Conditional(0x85,kSetup+0x144);
 const auto setupFallback=s.at;s.FixConditional(setupMiss,setupFallback);s.Bytes({0x48,0x8b,0x84,0x24,0x88,0,0,0,0x8b,0xfe,0x41,0x83,0x7e,0x34,0});s.Jump(0xe9,kSetup+0xe9);out.setupStubEnd=s.at;
 if(!r.good||!s.good||!rawPatch.good||!setupPatch.good||r.at>kSetupStub||s.at>kIslands){out.why="stub_extent";return false;}
 unsigned next=kUnwinds,rawUnwind=0,setupUnwind=0;
 if(!Unwind(out,a,kRaw,c::rawUnwinds,next,rawUnwind)||!Unwind(out,a,kSetup,c::setupUnwinds,next,setupUnwind)||
  !ChainStub(out,kRawStub,r.at,kRaw,kRaw+sizeof(c::rawBytes),rawUnwind,next)||
  !ChainStub(out,kSetupStub,s.at,kSetup,kSetup+c::setupUnwinds[0].end,setupUnwind,next)){out.why="unwind_extent";return false;}
 out.why="constructed_not_executable_not_admitted";out.ready=true;return true;
}
} // namespace mgscoop::standing_selection_clone
