#pragma once
// Bounded original-forward dispatcher diagnostics. A stack/input schema is
// never an actor, retained resource, native job, episode, or selection grant.
#include "qualified_sleep_lookup_observer.hpp"
namespace mgscoop::native_motion_stack_observer {
namespace q=qualified_sleep_lookup_observer;
enum class PayloadClass:unsigned {unreadable,uncovered,covered};
inline PayloadClass Classify(bool readable,std::uint64_t fullId)noexcept {
 return !readable?PayloadClass::unreadable:q::Covered(fullId)?PayloadClass::covered:PayloadClass::uncovered;
}
struct SetupInput {bool observed=false;std::uint64_t fullMotion=0;std::int32_t count=-1;std::uint32_t layer=0;};
template<class Reader>bool SetupStackInput(Reader& read,std::uintptr_t image,const q::Event& event,SetupInput& out)noexcept {
 q::ErrorPreserve error{};out={};
 // Reject foreign callers BEFORE any pointer read, even if they pass a valid
 // sleep-like spec. Preserve the current root negative-callsite optimization.
 if(!q::Range(image,0xa01d000)||event.path!=q::Path::setup||event.returnAddress!=image+0x1a6d88d||
    !q::Aligned(event.spec,8)||!q::Range(event.spec,0x214))return false;
 if(!q::Get(read,event.spec+0x200,out.count)||out.count!=1||!q::Get(read,event.spec+0x210,out.layer)||out.layer||
    !q::Get(read,event.spec,out.fullMotion)||!q::Covered(out.fullMotion))return false;
 out.observed=true;return true;
}
} // namespace mgscoop::native_motion_stack_observer
