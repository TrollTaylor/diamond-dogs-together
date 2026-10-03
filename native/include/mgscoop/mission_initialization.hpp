#pragma once
#include <cstdint>

namespace mgscoop {
// OnInitialize can run on a loading worker. Its matching return permits the
// later OnMissionGameStart to establish gameplay ownership; entry alone cannot.
class MissionInitialization {
    std::uint32_t thread_=0;
    bool complete_=false;
public:
    void Reset() noexcept { thread_=0; complete_=false; }
    bool Enter(std::uint32_t incoming) noexcept {
        if(!incoming || thread_ || complete_) return false;
        thread_=incoming; return true;
    }
    bool Returned(std::uint32_t incoming) noexcept {
        if(!incoming || incoming!=thread_ || complete_) return false;
        thread_=0; complete_=true; return true;
    }
    bool Complete() const noexcept { return complete_; }
};
}
