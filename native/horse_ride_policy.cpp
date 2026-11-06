// Pure parts of the RideHorse ride record (native/include/mgscoop/horse_ride.hpp).
#include "mgscoop/horse_ride.hpp"

namespace mgscoop::horse_ride {

bool GetOffState(std::uint8_t state) noexcept {
    return state == 5 || state == 6 || state == 7 || state == 8 || state == 9 || (state >= 10 && state <= 19) || state == 29;
}

const char* GetOffRefusal(const Rec& r) noexcept {
    if (!r.ok) return "unread";
    if (r.state == 0) return "idle";
    if (!GetOffState(r.state)) return "state";
    if (r.next != 0) return "pending";
    return nullptr;
}

bool Changed(const Rec& was, const Rec& now) noexcept {
    return was.ok != now.ok || was.plugin != now.plugin || was.state != now.state || was.next != now.next || was.again != now.again ||
           was.select != now.select || was.forced != now.forced || was.rider != now.rider || (was.sub == 0) != (now.sub == 0);
}

} // namespace mgscoop::horse_ride
