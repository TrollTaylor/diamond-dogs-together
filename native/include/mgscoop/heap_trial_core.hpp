#pragma once
#include <cstddef>
#include <cstdint>

namespace mgscoop::heap_trial {
struct Callbacks {
    void* context;
    void* (*allocate)(void*,std::uint64_t,std::uint64_t,std::uint32_t) noexcept;
    bool (*read)(void*,std::uintptr_t,void*,std::size_t) noexcept;
    bool (*release)(void*,void*,std::uint32_t&) noexcept;
};
struct Sample {
    std::uintptr_t address{};
    std::uint32_t header[4]{}; // original-base delta, tag, total bytes, marker
    bool header_read{}, header_valid{}, free_returned{};
    std::uint32_t free_status=0xffffffff;
};
struct Report { Sample samples[2]; unsigned allocated{}, released{}; bool passed{}; };

// This exercises allocator-owned raw storage, never a job or session object.
// Every non-null return is released exactly once through its paired allocator,
// including when header validation fails. No payload or header is written.
inline Report Run(const Callbacks& c) noexcept {
    Report report{};
    if(!c.allocate || !c.read || !c.release) return report;
    constexpr std::uint64_t sizes[]={88,144};
    bool ok=true;
    for(unsigned i=0;i<2;++i) {
        auto& s=report.samples[i];
        s.address=reinterpret_cast<std::uintptr_t>(c.allocate(c.context,sizes[i],8,115));
        if(!s.address) { ok=false; break; }
        ++report.allocated;
        s.header_read=s.address>=0x10010 && c.read(c.context,s.address-16,s.header,sizeof(s.header));
        s.header_valid=s.header_read && s.address%16==0 &&
            s.header[0]==16 && s.header[1]==115 && s.header[2]==sizes[i]+16 && s.header[3]==0xdead;
        if(!s.header_valid) { ok=false; break; }
    }
    // Both allocations remain alive until observation ends. Release in reverse
    // order, even if allocation two was null or a reader rejected a header.
    for(unsigned i=report.allocated;i>0;--i) {
        auto& s=report.samples[i-1];
        s.free_returned=c.release(c.context,reinterpret_cast<void*>(s.address),s.free_status);
        if(s.free_returned && s.free_status==0) ++report.released;
        else ok=false;
    }
    report.passed=ok && report.allocated==2 && report.released==2;
    return report;
}
}
