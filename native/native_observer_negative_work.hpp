#pragma once
#include <cstdint>
namespace mgscoop::native_observer_negative_work {
// Negative observation filters only. False cannot authorize a game call,
// retire/publish a birth or replace fresh ownership/code checks for true.
inline bool ActivationCandidate(bool exactCaller,bool framed,bool sameThread,unsigned body)noexcept {
 return exactCaller&&framed&&sameThread&&body<=1;
}
inline bool ActivationAfter(bool candidate,bool normal,bool alTrue,unsigned factoryCalls)noexcept {
 return candidate&&normal&&alTrue&&factoryCalls!=0;
}
}
