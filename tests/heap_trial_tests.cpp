#include "mgscoop/heap_trial_core.hpp"
#include <cstring>
#include <iostream>

using namespace mgscoop::heap_trial;
namespace {
unsigned checks{}, failures{};
void Check(bool v) { ++checks; if(!v) ++failures; }
struct Fixture {
    alignas(16) unsigned char blocks[2][160]{};
    unsigned allocations{}, frees{};
    unsigned fail_at=3, bad_header_at=3, read_fail_at=3, free_fail_at=3;
    unsigned corrupt_field=3;
    bool pointer_mismatch=false;
};
void* Allocate(void* ctx,std::uint64_t size,std::uint64_t align,std::uint32_t tag) noexcept {
    auto& f=*static_cast<Fixture*>(ctx); const auto n=f.allocations++;
    Check(n<2 && size==(n==0 ? 88u : 144u) && align==8 && tag==115);
    if(n==f.fail_at) return nullptr;
    std::uint32_t header[]={16,115,static_cast<std::uint32_t>(size+16),0xdead};
    if(n==f.bad_header_at) header[f.corrupt_field]^=1;
    std::memcpy(f.blocks[n],header,sizeof(header));
    return f.blocks[n]+16;
}
bool Read(void* ctx,std::uintptr_t address,void* output,std::size_t size) noexcept {
    auto& f=*static_cast<Fixture*>(ctx);
    const unsigned n=address==reinterpret_cast<std::uintptr_t>(f.blocks[0]) ? 0u : 1u;
    Check(address==reinterpret_cast<std::uintptr_t>(f.blocks[n]) && size==16);
    if(n==f.read_fail_at) return false;
    std::memcpy(output,f.blocks[n],size); return true;
}
bool Release(void* ctx,void* p,std::uint32_t& status) noexcept {
    auto& f=*static_cast<Fixture*>(ctx);
    unsigned allocated=f.allocations;
    if(f.fail_at<f.allocations) --allocated;
    const unsigned n=allocated-1-f.frees++;
    Check(p==f.blocks[n]+16);
    status=n==f.free_fail_at ? 0x80000001 : 0;
    return !f.pointer_mismatch;
}
Report RunFixture(Fixture& f) { return Run({&f,Allocate,Read,Release}); }
}
int main() {
    { Fixture f; auto r=RunFixture(f); Check(r.passed && r.allocated==2 && r.released==2 && f.frees==2); }
    for(unsigned n=0;n<2;++n) {
        { Fixture f; f.fail_at=n; auto r=RunFixture(f); Check(!r.passed && r.allocated==n && r.released==n && f.frees==n); }
        { Fixture f; f.read_fail_at=n; auto r=RunFixture(f); Check(!r.passed && r.allocated==n+1 && r.released==n+1); }
        { Fixture f; f.free_fail_at=n; auto r=RunFixture(f); Check(!r.passed && r.released==1 && f.frees==2); }
        for(unsigned field=0;field<4;++field) {
            Fixture f; f.bad_header_at=n; f.corrupt_field=field;
            auto r=RunFixture(f); Check(!r.passed && r.allocated==n+1 && r.released==n+1);
        }
    }
    { Fixture f; f.pointer_mismatch=true; auto r=RunFixture(f); Check(!r.passed && r.released==0 && f.frees==2); }
    Check(!Run({}).passed);
    std::cout<<checks<<" heap trial checks, "<<failures<<" failures; no game calls\n";
    return failures ? 1 : 0;
}
