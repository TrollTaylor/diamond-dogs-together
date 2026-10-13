#pragma once
// Pure observation of the current native physical-constraint profile.
#include "horse_player_physical_read.hpp"
#include "horse_physical_profile_contract.hpp"
namespace mgscoop::horse_physical_profile_read {
namespace cr=carry_plugin_read;
namespace hp=horse_player_physical_read;
template<class Reader> bool CodeGate(Reader& r,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch) noexcept {
    mismatch=0;if(!cr::Range(image,span))return false;unsigned char bytes[256]{};
    for(const auto& site:horse_physical_profile_contract::sites){
        if(site.rva>span||site.size>span-site.rva){mismatch=site.rva;return false;}
        for(std::size_t k=0;k<site.size;){const auto n=(site.size-k<sizeof(bytes))?site.size-k:sizeof(bytes);
            if(!r(image+site.rva+k,bytes,n)){mismatch=site.rva;return false;}
            for(std::size_t j=0;j<n;++j)if(bytes[j]!=site.bytes[k+j]){mismatch=site.rva;return false;}
            k+=n;
        }
    }
    for(const auto& slot:horse_physical_profile_contract::slots){std::uintptr_t target=0;
        if(slot.rva>span||sizeof(target)>span-slot.rva||!cr::Get(r,image+slot.rva,target)||target!=image+slot.target){mismatch=slot.rva;return false;}}
    return true;
}
struct Profile {
    std::uintptr_t component=0,profileArray=0,profile=0;
    std::uint16_t profileCount=0;
    std::uint8_t ownerActive[2]{};
    std::uint32_t mode=UINT32_MAX,flags=0;
};
template<class Reader> bool ReadOnce(Reader& r,std::uintptr_t image,const hp::Body& body,Profile& out) noexcept {
    out={};out.component=body.component;std::uintptr_t vt=0,owner=0;
    if(!cr::Aligned(body.component,8)||!cr::Range(body.component,0xf0)||!cr::Get(r,body.component,vt)||vt!=image+0x24c97f8||
       !cr::Get(r,body.component+0x80,vt)||vt!=image+0x24c9830||!cr::Get(r,body.component+0x68,owner)||owner!=body.transform||
       !r(body.component+0x70,out.ownerActive,sizeof(out.ownerActive))||!cr::Get(r,body.component+0xd0,out.flags)||
       !cr::Get(r,body.component+0x40,out.profileCount)||out.profileCount>64||!cr::Get(r,body.component+0x48,out.profileArray))return false;
    if(!out.profileCount)return true;
    if(!cr::Aligned(out.profileArray,8)||!cr::Range(out.profileArray,std::size_t(out.profileCount)*8))return false;
    // Reproduce original1A741E0's highest non-null selection; all entries are
    // bounded before reading. A null profile remains an observed early exit.
    for(std::size_t k=out.profileCount;k;--k){std::uintptr_t candidate=0;
        if(!cr::Get(r,out.profileArray+(k-1)*8,candidate))return false;
        if(candidate){out.profile=candidate;break;}}
    if(!out.profile)return true;
    return cr::Aligned(out.profile,4)&&cr::Range(out.profile,0x1c)&&cr::Get(r,out.profile+0xc,out.mode);
}
inline bool Same(const Profile& a,const Profile& b) noexcept {
    return a.component==b.component&&a.profileArray==b.profileArray&&a.profile==b.profile&&a.profileCount==b.profileCount&&
      a.ownerActive[0]==b.ownerActive[0]&&a.ownerActive[1]==b.ownerActive[1]&&a.mode==b.mode&&a.flags==b.flags;
}
template<class Reader> bool Observe(Reader& r,std::uintptr_t image,const hp::Body& body,Profile& out) noexcept {
    out={};Profile a{},b{};if(!ReadOnce(r,image,body,a)||!ReadOnce(r,image,body,b)||!Same(a,b))return false;out=b;return true;
}
inline bool MatrixCompatible(const Profile& p) noexcept {
    return p.profile&&(p.ownerActive[0]||p.ownerActive[1])&&(p.mode==1||p.mode==2)&&!(p.flags&0x23u);
}
inline bool Line(const Profile& a,const Profile& b,bool ready,char* out,std::size_t capacity) noexcept {
    if(!out||!capacity)return false;
    const int n=std::snprintf(out,capacity,"charProfile;ready=%u;b0=%u,%u,%x,%u;b1=%u,%u,%x,%u",ready?1u:0u,
      a.profileCount,a.mode,a.flags,MatrixCompatible(a)?1u:0u,b.profileCount,b.mode,b.flags,MatrixCompatible(b)?1u:0u);
    return n>=0&&static_cast<std::size_t>(n)<capacity;
}
}
