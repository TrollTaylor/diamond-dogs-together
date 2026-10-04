// Original MGS Co-op snapshot interpolation (pure, portable, deterministic).
#include "mgscoop/pose_interp.hpp"
#include <cmath>

namespace mgscoop::pose {
namespace {
constexpr double kPi = 3.14159265358979323846;

double Distance(const Snapshot& a, const Snapshot& b) noexcept {
    const double dx = static_cast<double>(b.x) - a.x, dy = static_cast<double>(b.y) - a.y,
                 dz = static_cast<double>(b.z) - a.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
double WrapDouble(double radians) noexcept {
    if (!std::isfinite(radians)) return 0.0;
    double wrapped = std::fmod(radians + kPi, 2.0 * kPi);
    if (wrapped < 0) wrapped += 2.0 * kPi;
    return wrapped - kPi;
}
void CopyAnim(const Snapshot& from, Sample& to) noexcept {
    to.anim_valid = from.anim_valid;
    to.act = from.act;
    to.anim_stance = from.anim_stance;
    to.anim_speed = from.anim_speed;
}
void Copy(const Snapshot& from, Sample& to) noexcept {
    to.x = from.x; to.y = from.y; to.z = from.z;
    to.yaw = static_cast<float>(WrapDouble(from.yaw));
    to.stance = from.stance;
    to.flags = from.flags;
    CopyAnim(from, to);
}
} // namespace

const char* StatusName(Status status) noexcept {
    switch (status) {
    case Status::live: return "live";
    case Status::stale: return "stale";
    case Status::lost: return "lost";
    default: return "none";
    }
}

Status StatusForAge(std::uint64_t age_ms) noexcept {
    if (age_ms <= stale_ms) return Status::live;
    if (age_ms <= lost_ms) return Status::stale;
    return Status::lost;
}

float WrapAngle(float radians) noexcept { return static_cast<float>(WrapDouble(radians)); }

bool SnapshotRing::Push(const Snapshot& snapshot, std::uint64_t receive_ms) noexcept {
    std::int64_t sender_time = snapshot.sender_ms;
    if (count_) {
        const Entry& newest = at(count_ - 1);
        const auto delta = static_cast<std::int32_t>(snapshot.sender_ms - newest.snapshot.sender_ms);
        if (delta <= 0) return false;
        sender_time = newest.sender_time + delta;
    }
    const Entry entry{snapshot, sender_time, receive_ms};
    if (count_ == capacity) {
        entries_[head_] = entry;
        head_ = (head_ + 1) % capacity;
    } else {
        entries_[(head_ + count_) % capacity] = entry;
        ++count_;
    }
    return true;
}

void SnapshotRing::Reset() noexcept {
    for (auto& entry : entries_) entry = Entry{};
    head_ = 0;
    count_ = 0;
}

std::uint64_t SnapshotRing::newest_receive_ms() const noexcept { return count_ ? at(count_ - 1).receive_ms : 0; }

Sample SnapshotRing::SampleAt(std::uint64_t now_ms, std::uint32_t delay_ms) const noexcept {
    Sample out{};
    out.status = Status::none;
    if (!count_) return out;
    const Entry& newest = at(count_ - 1);
    const std::uint64_t age = now_ms >= newest.receive_ms ? now_ms - newest.receive_ms : 0;
    out.valid = true;
    out.status = StatusForAge(age);
    out.age_ms = age > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(age);
    if (delay_ms == 0) {
        Copy(newest.snapshot, out);
        return out;
    }
    std::int64_t offset = static_cast<std::int64_t>(at(0).receive_ms) - at(0).sender_time;
    for (std::size_t i = 1; i < count_; ++i) {
        const std::int64_t candidate = static_cast<std::int64_t>(at(i).receive_ms) - at(i).sender_time;
        if (candidate < offset) offset = candidate;
    }
    const std::int64_t render = static_cast<std::int64_t>(now_ms) - offset - static_cast<std::int64_t>(delay_ms);
    if (render >= newest.sender_time) {
        Copy(newest.snapshot, out);
        const std::int64_t ahead = render - newest.sender_time;
        if (ahead == 0) return out;
        if (count_ < 2) { out.held = true; return out; }
        const Entry& previous = at(count_ - 2);
        const double span = static_cast<double>(newest.sender_time - previous.sender_time);
        if (Distance(previous.snapshot, newest.snapshot) > snap_distance_m || span <= 0) {
            out.held = true;
            return out;
        }
        const double step = static_cast<double>(ahead < extrapolate_cap_ms ? ahead : extrapolate_cap_ms) / span;
        out.x = static_cast<float>(newest.snapshot.x + (static_cast<double>(newest.snapshot.x) - previous.snapshot.x) * step);
        out.y = static_cast<float>(newest.snapshot.y + (static_cast<double>(newest.snapshot.y) - previous.snapshot.y) * step);
        out.z = static_cast<float>(newest.snapshot.z + (static_cast<double>(newest.snapshot.z) - previous.snapshot.z) * step);
        out.yaw = static_cast<float>(WrapDouble(newest.snapshot.yaw +
                                                WrapDouble(static_cast<double>(newest.snapshot.yaw) - previous.snapshot.yaw) * step));
        out.extrapolated = true;
        out.held = ahead > extrapolate_cap_ms;
        return out;
    }
    const Entry& oldest = at(0);
    if (render <= oldest.sender_time) {
        Copy(oldest.snapshot, out);
        out.held = true;
        return out;
    }
    std::size_t index = 0;
    while (index + 1 < count_ && at(index + 1).sender_time <= render) ++index;
    const Entry& a = at(index);
    const Entry& b = at(index + 1);
    if (Distance(a.snapshot, b.snapshot) > snap_distance_m) {
        Copy(a.snapshot, out);
        out.snapped = true;
        return out;
    }
    const double f = static_cast<double>(render - a.sender_time) / static_cast<double>(b.sender_time - a.sender_time);
    out.x = static_cast<float>(a.snapshot.x + (static_cast<double>(b.snapshot.x) - a.snapshot.x) * f);
    out.y = static_cast<float>(a.snapshot.y + (static_cast<double>(b.snapshot.y) - a.snapshot.y) * f);
    out.z = static_cast<float>(a.snapshot.z + (static_cast<double>(b.snapshot.z) - a.snapshot.z) * f);
    out.yaw = static_cast<float>(WrapDouble(a.snapshot.yaw + WrapDouble(static_cast<double>(b.snapshot.yaw) - a.snapshot.yaw) * f));
    out.stance = b.snapshot.stance;
    out.flags = b.snapshot.flags;
    CopyAnim(b.snapshot, out);
    out.interpolated = true;
    return out;
}

} // namespace mgscoop::pose
