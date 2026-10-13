#include "prologue_provider_read.hpp"
#include <map>
#include <cstring>
#include <cstdio>
namespace pr=mgscoop::prologue_provider_read;
unsigned good=0,bad=0;
void Check(bool b,const char* label){if(b)++good;else{++bad;std::printf("FAIL %s\n",label);}}
struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes;
    std::uintptr_t flipAt=0,flipValue=0;unsigned reads=0;
    template<class T>void Put(std::uintptr_t p,T value){unsigned char raw[sizeof(T)]{};std::memcpy(raw,&value,sizeof(T));
        for(std::size_t k=0;k<sizeof(T);++k)bytes[p+k]=raw[k];}
    bool operator()(std::uintptr_t p,void* out,std::size_t n)noexcept{
        if(p==0x140000000+pr::contract::manager&&++reads==2&&flipAt)Put(flipAt,flipValue);
        auto* raw=static_cast<unsigned char*>(out);for(std::size_t k=0;k<n;++k){const auto it=bytes.find(p+k);if(it==bytes.end())return false;raw[k]=it->second;}return true;}
};
pr::Scope Scope(){pr::Scope s{};s.image=0x140000000;s.imageSpan=pr::contract::imageSpan;s.exactGate=s.currentPair=s.fresh=true;return s;}
Memory Fixture(){Memory m;const auto s=Scope();m.Put(s.image+pr::contract::manager,s.image+pr::contract::managerVtable);
    m.Put<std::uint32_t>(s.image+pr::contract::selector,2);
    for(unsigned k=0;k<3;++k){const auto ptr=std::uintptr_t(0x100000+k*0x1000),vt=s.image+0x2297000+k*0x10;
        m.Put(s.image+pr::contract::manager+0xcc0+k*8,ptr);m.Put(ptr,vt);
        m.Put(vt,s.image+0x9d0000+k*0x100);m.Put(vt+8,s.image+0x9d0010+k*0x100);}
    return m;
}
Memory Pins(){Memory m;const auto s=Scope();for(const auto& p:pr::contract::sites)for(std::size_t k=0;k<p.size;++k)m.bytes[s.image+p.rva+k]=p.bytes[k];
    for(const auto& p:pr::contract::slots)m.Put(s.image+p.rva,s.image+p.target);return m;}
int main(){const auto s=Scope();const char* why="unknown";pr::Observation a{};
    {auto m=Fixture();Check(pr::Observe(m,s,a,why)&&a.selector==2&&a.selected==0x102000&&a.provider[0].vtableRva==0x2297000&&
        a.provider[2].slot8Rva==0x9d0210,"three providers selected2");Check(a.manager+0x158==s.image+pr::contract::selector,"selector is requested field alias");}
    for(unsigned mode:{0u,1u,2u,3u,0xffffffffu}){auto m=Fixture();m.Put(s.image+pr::contract::selector,mode);
        Check(pr::Observe(m,s,a,why)&&a.selector==mode&&a.selected==(mode<3?0x100000+mode*0x1000:0),"getter unsigned bounds preserved");}
    {auto m=Fixture();for(unsigned k=0;k<3;++k)m.Put<std::uintptr_t>(s.image+pr::contract::manager+0xcc0+k*8,0);
        Check(pr::Observe(m,s,a,why)&&!a.selected&&!std::strcmp(why,"seq_no_selected"),"null retained providers observed");}
    for(unsigned f=0;f<5;++f){auto sc=s;switch(f){case 0:sc.exactGate=false;break;case 1:sc.currentPair=false;break;
        case 2:sc.fresh=false;break;case 3:sc.imageSpan--;break;default:sc.image=UINTPTR_MAX-128;break;}
        auto m=Fixture();Check(!pr::Observe(m,sc,a,why),"scope and image refuse");}
    {auto m=Fixture();m.Put(s.image+pr::contract::manager,s.image+pr::contract::managerVtable+8);Check(!pr::Observe(m,s,a,why)&&!std::strcmp(why,"seq_manager"),"wrong manager class");}
    {auto m=Fixture();m.bytes.erase(s.image+pr::contract::selector);Check(!pr::Observe(m,s,a,why),"missing selector");}
    for(unsigned k=0;k<3;++k){const auto ptr=std::uintptr_t(0x100000+k*0x1000),vt=s.image+0x2297000+k*0x10;
        for(unsigned kind=0;kind<6;++kind){auto m=Fixture();switch(kind){
            case 0:m.Put(s.image+pr::contract::manager+0xcc0+k*8,ptr+1);break;
            case 1:m.Put(ptr,s.image+0x1000);break;
            case 2:m.Put(ptr,s.image+pr::contract::tableEnd-8);break;
            case 3:m.Put(vt,s.image+pr::contract::tableBegin);break;
            case 4:m.Put(vt+8,s.image+pr::contract::imageSpan);break;
            default:m.bytes.erase(vt+8);break;}
            Check(!pr::Observe(m,s,a,why),"invalid provider vtable/function ranges");}
    }
    // Coherent but unclassified executable provider slots can be packed native.
    {auto m=Fixture();m.Put<std::uintptr_t>(s.image+0x2297000,s.image+0x7766fa0);Check(pr::Observe(m,s,a,why)&&a.provider[0].slot0Rva==0x7766fa0,"packed executable is observation only");}
    for(unsigned kind=0;kind<5;++kind){auto m=Fixture();switch(kind){
        case 0:m.flipAt=s.image+pr::contract::manager;m.flipValue=s.image+pr::contract::managerVtable+8;break;
        case 1:m.flipAt=s.image+pr::contract::selector;m.flipValue=1;break;
        case 2:m.flipAt=s.image+pr::contract::manager+0xcc8;m.flipValue=0;break;
        case 3:m.flipAt=0x100000;m.flipValue=s.image+0x2297010;break;
        default:m.flipAt=s.image+0x2297008;m.flipValue=s.image+0x9d0020;break;}
        Check(!pr::Observe(m,s,a,why),"paired mutation rejected");}
    {auto m=Fixture();Check(pr::Observe(m,s,a,why),"line input");char line[161]{};
        Check(pr::Line(a,true,why,line,sizeof(line))&&std::strlen(line)<=160,"diagnostic fits160");
        Check(!pr::Line(a,true,why,line,16),"small diagnostic refuses truncation");
        a.selector=0xffffffffu;for(auto& p:a.provider)p.vtableRva=p.slot0Rva=p.slot8Rva=0xffffffffu;
        Check(pr::Line(a,true,"seq_no_selected",line,sizeof(line))&&std::strlen(line)<=160,"max diagnostic fits160");}
    std::uintptr_t mismatch=0;{auto m=Pins();Check(pr::CodeGate(m,s.image,s.imageSpan,mismatch),"full original pins");}
    for(const auto& p:pr::contract::sites)for(std::size_t k=0;k<p.size;++k){auto m=Pins();m.bytes[s.image+p.rva+k]^=1;
        Check(!pr::CodeGate(m,s.image,s.imageSpan,mismatch)&&mismatch==p.rva,"every byte pin including function tails");}
    {auto m=Pins();m.Put<std::uintptr_t>(s.image+pr::contract::slots[0].rva,s.image+pr::contract::slots[0].target+1);
        Check(!pr::CodeGate(m,s.image,s.imageSpan,mismatch),"getter slot drift rejected");}
    {auto m=Pins();Check(!pr::CodeGate(m,s.image,s.imageSpan-1,mismatch),"wrong PE span");}
    std::printf("prologue_provider: %u passed, %u failed\n",good,bad);return bad?1:0;
}
