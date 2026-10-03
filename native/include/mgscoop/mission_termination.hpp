#pragma once
#include <cstdint>

namespace mgscoop {
// Project-authored scope for the exact TppMain.OnTerminate wrapper. The caller
// serializes receipts and validates their source. A worker may clean up a scene
// without an owned network session; an active session never migrates here.
class MissionTermination {
    std::uint32_t thread_=0;
public:
    bool Active() const noexcept { return thread_!=0; }
    bool OnThread(std::uint32_t thread) const noexcept { return thread && thread==thread_; }
    template<class Transfer>
    bool Enter(bool running,std::uint32_t runtime,std::uint32_t incoming,
               bool session_owned,Transfer transfer) noexcept {
        if(!running || Active() || !runtime || !incoming ||
           (session_owned && incoming!=runtime)) return false;
        if(incoming!=runtime && !transfer(runtime)) return false;
        thread_=incoming;
        return true;
    }
    bool Returned(std::uint32_t incoming) noexcept {
        if(!OnThread(incoming)) return false;
        thread_=0;
        return true;
    }
};
}
