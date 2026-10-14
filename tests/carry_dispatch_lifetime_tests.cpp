#include <cstdio>
#include <cstring>
#include <map>
#include "carry_dispatch_lifetime.hpp"
namespace d=mgscoop::carry_dispatch_lifetime;
constexpr std::uintptr_t image=0x140000000ull,player=0x100020,go=0x100000,registry=0x200000,node=0x300000,meta=0x400000,iface=0x500000,head=0x600000;
struct Reader {
 std::map<std::uintptr_t,unsigned char> memory;bool drift=false;
 template<class T>void put(std::uintptr_t p,T value){const auto* data=reinterpret_cast<const unsigned char*>(&value);for(std::size_t n=0;n<sizeof(value);++n)memory[p+n]=data[n];}
 bool operator()(std::uintptr_t p,void* out,std::size_t n)noexcept{if(drift&&p==iface+0x30)memory[p]^=1;auto* dest=static_cast<unsigned char*>(out);for(std::size_t i=0;i<n;++i){auto f=memory.find(p+i);if(f==memory.end())return false;dest[i]=f->second;}return true;}
};
Reader fixture(){Reader r;r.put(image+0x2a82c30,player);r.put(player,image+0x2295490);r.put(go,image+0x22953d0);r.put(player+0x204,std::uint32_t{1});r.put(player+0x208,std::uint32_t{2});r.put(player+0x20c,std::uint32_t{2});r.put(player+0x214,std::uint32_t{});r.put(image+0x2c389a0,registry);r.put(registry,image+0x23073c0);r.put(registry+0x30,node);r.put(node+8,meta);r.put(meta+0x20,go);r.put(go+8,meta);r.put(meta+0x28,iface);r.put(iface,image+0x23063f8);r.put(iface+0x20,go);r.put(iface+0xc,std::uint32_t{2});r.put(iface+0x30,std::uint32_t{});r.put(meta+0x38,std::uint32_t{0x20});r.put(node+0x10,head);r.put(head,head);r.put(head+8,head);r.put(node+0x18,std::uint64_t{});return r;}
int main(){unsigned count=0;auto check=[&](bool ok,const char* label){++count;if(!ok){std::printf("failed %s\n",label);return false;}return true;};auto r=fixture();d::Observation held{},o{};
 if(!check(d::ReadPair(r,image,player,held)&&d::NoClassWork(held),"complete current interface/registry no-work"))return 1;
 auto bad=[&](Reader x,const char* label){d::Observation out{};return check(!d::ReadPair(x,image,player,out)&&!out.interface,label);};
 r=fixture();r.put(registry,image+0x23073c8);if(!bad(r,"final registry type"))return 1;
 r=fixture();r.put(go+8,meta+0x80);if(!bad(r,"current metadata alias"))return 1;
 r=fixture();r.put(meta+0x20,go+0x800);if(!bad(r,"metadata owner"))return 1;
 r=fixture();r.put(iface+0x20,go+0x800);if(!bad(r,"interface owner"))return 1;
 r=fixture();r.put(iface,image+0x20e9b38);if(!bad(r,"destructor base type"))return 1;
 r=fixture();r.put(iface+0xc,std::uint32_t{1});if(!bad(r,"two actual actor slots"))return 1;
 r=fixture();r.put(iface+0x30,std::uint32_t{2});if(!bad(r,"native dispatch unavailable mode"))return 1;
 r=fixture();r.put(player+0x204,std::uint32_t{0x101});if(!bad(r,"offline guard"))return 1;
 r=fixture();r.drift=true;if(!bad(r,"fresh double sample"))return 1;
 r=fixture();r.put(meta+0x38,std::uint32_t{0x3e});if(!check(d::ReadPair(r,image,player,o)&&d::NoClassWork(o),"actual healthy3E skips both native branches"))return 1;
 r=fixture();r.put(meta+0x38,std::uint32_t{0x3f});if(!check(d::ReadPair(r,image,player,o)&&!d::NoClassWork(o),"bit1 addition must refuse removal branch"))return 1;
 r=fixture();r.put(meta+0x38,std::uint32_t{0x36});if(!check(d::ReadPair(r,image,player,o)&&!d::NoClassWork(o),"bit8 removal must refuse native synchronization"))return 1;
 for(unsigned value=0;value<16;++value){r=fixture();r.put(meta+0x38,value);const bool first=(value&4u)&&!(value&8u),second=(value&4u)&&(value&1u);
  if(!check(d::ReadPair(r,image,player,o)&&d::NoClassWork(o)==(!first&&!second),"all low flag combinations match two native predicates"))return 1;}
 r=fixture();r.put(meta+0x38,std::uint32_t{4});if(!check(d::ReadPair(r,image,player,o)&&!d::NoClassWork(o),"retirement flags must refuse native work"))return 1;
 r=fixture();r.put(node+0x18,std::uint64_t{1});if(!check(d::ReadPair(r,image,player,o)&&!d::NoClassWork(o),"pending replacement count"))return 1;
 r=fixture();r.put(head,head+0x20);if(!check(d::ReadPair(r,image,player,o)&&!d::NoClassWork(o),"pending sentinel next"))return 1;
 r=fixture();r.put(head+8,head+0x20);if(!check(d::ReadPair(r,image,player,o)&&!d::NoClassWork(o),"pending sentinel previous"))return 1;
 if(!check(d::Refuse(held,d::Entry::interface_ctor,iface),"same allocation constructor"))return 1;
 if(!check(d::Refuse(held,d::Entry::interface_ctor,iface+0x80,go),"new interface would replace heldGO metadata"))return 1;
 if(!check(!d::Refuse(held,d::Entry::interface_ctor,iface+0x80,go+0x800),"foreign constructor forwards"))return 1;
 if(!check(d::Refuse(held,d::Entry::interface_delete,iface),"held interface delete"))return 1;
 if(!check(!d::Refuse(held,d::Entry::interface_delete,iface+0x80),"foreign interface delete"))return 1;
 if(!check(d::Refuse(held,d::Entry::registry_ctor,registry)&&d::Refuse(held,d::Entry::registry_destruct,registry)&&d::Refuse(held,d::Entry::registry_delete,registry),"whole registry parent retention"))return 1;
 if(!check(d::Refuse(held,d::Entry::registry_replace,0),"global registry replacement blocked"))return 1;
 if(!check(d::Refuse(held,d::Entry::class_update,node)&&d::Refuse(held,d::Entry::class_register,node),"current type0 class lifecycle"))return 1;
 if(!check(!d::Refuse(held,d::Entry::class_register,node+0x80),"foreign class unaffected"))return 1;
 auto changed=held;changed.interface+=0x80;if(!check(!d::Identity(held,changed),"interface rebind identity"))return 1;
 struct Hook {std::uintptr_t rva=0;void* wrapper=nullptr;};
 struct Patch {std::uintptr_t rva=0,wrapper=0;unsigned char original[5]{};};
 constexpr std::size_t hookCount=13+mgscoop::carry_dispatch_lifetime_contract::hookCount;
 Hook hooks[hookCount]{};Patch patches[hookCount]{};
 for(std::size_t n=0;n<hookCount;++n){hooks[n].rva=n<13?0x1000+n*0x10:mgscoop::carry_dispatch_lifetime_contract::hookRvas[n-13];hooks[n].wrapper=reinterpret_cast<void*>(0x700000+n*0x10);}
 const auto prefix=[](std::uintptr_t rva,unsigned char (&bytes)[5])noexcept{for(unsigned n=0;n<5;++n)bytes[n]=static_cast<unsigned char>((rva>>n)&0xffu);return true;};
 const auto read=[&](std::uintptr_t rva,void* bytes,std::size_t n)noexcept{unsigned char expected[5]{};if(n!=5||!prefix(rva,expected))return false;std::memcpy(bytes,expected,sizeof(expected));return true;};
 if(!check(d::InitializeHookReceipts(hooks,patches,prefix,read)&&patches[hookCount-1].rva==0xbf91b0&&patches[hookCount-1].wrapper==reinterpret_cast<std::uintptr_t>(hooks[hookCount-1].wrapper),"actual last hook receives ownership metadata"))return 1;
 const auto lateFailure=[&](std::uintptr_t rva,void* bytes,std::size_t n)noexcept{return rva!=hooks[hookCount-1].rva&&read(rva,bytes,n);};
 if(!check(!d::InitializeHookReceipts(hooks,patches,prefix,lateFailure),"late hook prefix read cannot silently pass installation"))return 1;
 const auto lateDrift=[&](std::uintptr_t rva,void* bytes,std::size_t n)noexcept{if(!read(rva,bytes,n))return false;if(rva==hooks[hookCount-1].rva)static_cast<unsigned char*>(bytes)[4]^=1;return true;};
 if(!check(!d::InitializeHookReceipts(hooks,patches,prefix,lateDrift),"last hook byte drift refuses all installation"))return 1;
 std::printf("%u dispatch lifetime source checks\n",count);return 0;}
