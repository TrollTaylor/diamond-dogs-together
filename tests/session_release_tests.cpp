#include "mgscoop/session_release.hpp"
#include <cstdio>
#include <cstdlib>
#include <map>

namespace {
unsigned checks=0;
void Check(bool value) { ++checks; if(!value) { std::fprintf(stderr,"failed check %u\n",checks); std::exit(1); } }
constexpr std::uintptr_t base=0x140000000,fixture_root=0x20000000,fixture_session=0x30000000,fixture_manager=0x40000000;
constexpr std::size_t span=0xa01d000;
}
int main() {
    // Detached synthetic address space: the former fixture_session is deliberately
    // unreadable, matching the live game's already-destroyed fixture_session case.
    std::map<std::uintptr_t,std::uintptr_t> memory{{base+0x2bef840,fixture_root},
        {fixture_root+0xcc8,fixture_session},{fixture_root+0xd00,fixture_manager},{base+0x2b9dfb0,fixture_session}};
    auto read=[&](std::uintptr_t address,std::uintptr_t& value) {
        const auto found=memory.find(address);
        Check(found!=memory.end()); value=found->second; return true;
    };
    auto released=[&] {return mgscoop::SessionSlotsReleased(base,span,fixture_root,read);};
    Check(!released());
    memory[fixture_root+0xcc8]=0; Check(!released()); // fixture_manager still alive
    memory[fixture_root+0xd00]=0; Check(!released()); // main still alive
    memory[base+0x2b9dfb0]=0; Check(released());
    memory[base+0x2bef840]=fixture_root+0x10000; Check(!released()); // unrelated fixture_root
    memory[base+0x2bef840]=fixture_root;
    Check(!mgscoop::SessionSlotsReleased(base,span,0,read));
    Check(!mgscoop::SessionSlotsReleased(base,0,fixture_root,read));
    // Each failed read must withhold release, even if an output remained zero.
    for(unsigned fail=1;fail<=10;++fail) {
        unsigned calls=0;
        Check(!mgscoop::SessionSlotsReleased(base,span,fixture_root,[&](auto address,auto& value) {
            return ++calls!=fail && read(address,value);
        }));
    }
    for(unsigned change=1;change<=10;++change) {
        unsigned calls=0;
        Check(!mgscoop::SessionSlotsReleased(base,span,fixture_root,[&](auto address,auto& value) {
            const bool ok=read(address,value);
            if(++calls==change) value=value ? value+0x10000 : fixture_session;
            return ok;
        }));
    }
    std::printf("%u fixture_session-release checks passed; synthetic memory, no game calls\n",checks);
}
