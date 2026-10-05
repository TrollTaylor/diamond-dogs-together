// Original MGS Co-op round 3 input lever: pure pad logic (portable).
#include "mgscoop/partner_input.hpp"
#include <cmath>
#include <cstring>

namespace mgscoop::partner_input {
namespace {
constexpr double kPi = 3.14159265358979323846;

constexpr std::size_t CamBytes() noexcept {
    std::size_t total = 0;
    for (const CamRange& range : kCamRanges) total += static_cast<std::size_t>(range.end - range.begin);
    return total;
}
static_assert(CamBytes() == kCamSaveBytes, "camera sandbox ranges");
static_assert(static_cast<int>(Word::net) == kPartnerWordNet && static_cast<int>(Word::zero) == kPartnerWordZero,
              "Word values match the lab.ini word codes");

float Finite(float value) noexcept { return std::isfinite(value) ? value : 0.0f; }
double Clamp(double value, double low, double high) noexcept {
    if (!(value >= low)) return low;   // also NaN
    if (value > high) return high;
    return value;
}
std::int16_t Axis(float value) noexcept {
    return static_cast<std::int16_t>(std::lround(Clamp(Finite(value), -1.0, 1.0) * 32767.0));
}
std::uint8_t Unit8(float value) noexcept {
    return static_cast<std::uint8_t>(std::lround(Clamp(Finite(value), 0.0, 1.0) * 255.0));
}
std::uint16_t Unit16(float value) noexcept {
    return static_cast<std::uint16_t>(std::lround(Clamp(Finite(value), 0.0, 1.0) * 65535.0));
}
bool Bit(std::uint32_t value, unsigned index) noexcept { return ((value >> index) & 1u) != 0; }
// The configured edge word j (press/release/held/net) for this window.
std::uint32_t WordValue(Word word, std::uint32_t press, std::uint32_t release, std::uint32_t held, std::uint32_t net) noexcept {
    switch (word) {
    case Word::press: return press;
    case Word::release: return release;
    case Word::held: return held;
    case Word::net: return net;
    default: return 0;
    }
}
} // namespace

double NormalizeRadians(double radians) noexcept {
    if (!std::isfinite(radians)) return 0.0;
    double r = std::fmod(radians, 2.0 * kPi);  // (-2pi, 2pi)
    if (r <= -kPi) r += 2.0 * kPi;
    else if (r > kPi) r -= 2.0 * kPi;
    return r;
}

PadSnapshot Effective(const PadRaw& raw) noexcept {
    PadSnapshot s{};
    const std::uint32_t buttons = raw.pp_mask[0], sticks = raw.pp_mask[1], triggers = raw.pp_mask[2];
    const auto word = [&](unsigned i) noexcept { return (raw.src_w[i] ? raw.src_w[i] : raw.kb_w[i]) & ~buttons; };
    s.held = word(0);
    s.w4c = word(1);
    s.w50 = word(2);
    s.w54 = word(3);
    const std::uint8_t f = raw.kb_flags;
    s.lx = Bit(sticks, 0) ? 0.0f : ((f & 1) ? raw.kb_f[0] : raw.src_f[0]);
    s.ly = Bit(sticks, 0) ? 0.0f : ((f & 1) ? raw.kb_f[2] : raw.src_f[1]);
    s.rx = Bit(sticks, 1) ? 0.0f : ((f & 2) ? raw.kb_f[1] : raw.src_f[2]);
    s.ry = Bit(sticks, 1) ? 0.0f : ((f & 2) ? raw.kb_f[3] : raw.src_f[3]);
    s.lt = Bit(triggers, 0) ? 0.0f : ((f & 4) ? raw.kb_f[4] : raw.src_f[4]);
    s.rt = Bit(triggers, 1) ? 0.0f : ((f & 8) ? raw.kb_f[5] : raw.src_f[5]);
    s.mag = raw.pp_derived[0];
    s.force = raw.pp_derived[1];
    s.dir = raw.pp_derived[2];
    s.flags = Flag::Valid;
    if (buttons || sticks || triggers) s.flags |= Flag::SenderMasked;
    if ((raw.src_w[0] == 0 && raw.kb_w[0] != 0) || (f & 0x0F)) s.flags |= Flag::SenderKeyboard;
    return s;
}

DirState CheckDir(const PadSnapshot& s, std::uint32_t stick_mask) noexcept {
    if (!(s.mag > 0.01f) || Bit(stick_mask, 0)) return DirState::na;
    if (!std::isfinite(s.lx) || !std::isfinite(s.ly) || !std::isfinite(s.dir)) return DirState::bad;
    const double expected = std::atan2(static_cast<double>(s.lx), static_cast<double>(s.ly));
    const double delta = std::fabs(NormalizeRadians(expected - static_cast<double>(s.dir)));
    return delta < 1e-3 ? DirState::ok : DirState::bad;
}

const char* DirStateName(DirState state) noexcept {
    switch (state) {
    case DirState::ok: return "ok";
    case DirState::bad: return "bad";
    default: return "na";
    }
}

std::int32_t NormalizeCdeg(std::int64_t cdeg) noexcept {
    std::int64_t r = (cdeg + 18000) % 36000;
    if (r < 0) r += 36000;
    return static_cast<std::int32_t>(r - 18000);
}

InputPacket Quantize(const PadSnapshot& s, std::int32_t cam_cdeg) noexcept {
    InputPacket p{};
    p.version = 2;
    p.flags = static_cast<std::uint8_t>((s.flags & Flag::Known) | Flag::Valid);
    p.held = s.held;
    p.w4c = s.w4c;
    // v2: the edge set (w50/w54/tap12) is filled by SenderFill, never from one snapshot.
    p.w50 = 0;
    p.w54 = 0;
    p.lx = Axis(s.lx);
    p.ly = Axis(s.ly);
    p.rx = Axis(s.rx);
    p.ry = Axis(s.ry);
    p.lt = Unit8(s.lt);
    p.rt = Unit8(s.rt);
    p.lmag = Unit16(s.mag);
    p.lforce = Unit16(s.force);
    p.ldir = static_cast<std::int16_t>(std::lround(Clamp(NormalizeRadians(Finite(s.dir)) * 10000.0, -kMaxDir, kMaxDir)));
    p.cam_cdeg = static_cast<std::int16_t>(NormalizeCdeg(cam_cdeg));
    return p;
}

bool ValidPacket(const InputPacket& p) noexcept {
    if ((p.flags & ~Flag::Known) != 0 || p.lx == -32768 || p.ly == -32768 || p.rx == -32768 || p.ry == -32768 ||
        p.ldir < -kMaxDir || p.ldir > kMaxDir || p.cam_cdeg < kCamMin || p.cam_cdeg > kCamMax) return false;
    if (p.version == 1) return p.edge_seq == 0 && p.edge_rep == 0 && p.long12 == 0 && p.tap12 == 0;
    if (p.version != 2 || p.edge_rep > kEdgeRepeats) return false;
    // No edge set: no edge words.
    if (p.edge_seq == 0) return p.edge_rep == 0 && p.w50 == 0 && p.w54 == 0 && p.tap12 == 0;
    return true;
}

double Heading(double cam_rad, float lx, float ly) noexcept {
    const double x = Finite(lx), y = Finite(ly);
    const double dir = (x != 0.0 || y != 0.0) ? std::atan2(x, y) : 0.0;
    return NormalizeRadians((std::isfinite(cam_rad) ? cam_rad : 0.0) + dir - kPi);
}

Injection Compose(const Config& c, const Sample& sample, std::int32_t host_cam_cdeg, Overlay overlay, EdgeState& edge) noexcept {
    Injection j{};
    if (c.mode == PartnerInput::off || c.mode == PartnerInput::pass) return j;
    j.write = true;
    const bool net = c.mode == PartnerInput::net;
    const bool live = net && sample.valid && sample.age_ms <= c.stale_ms;
    // A stall: the newest frame is stale but younger than the grace.
    const bool grace = net && sample.valid && !live && sample.age_ms <= kStaleGraceMs;
    j.stale = net && !live;
    j.live = live;
    std::uint32_t held = overlay.bits;
    if (c.mode == PartnerInput::stick) {
        const double theta = NormalizeRadians(static_cast<double>(c.stick_deg % 360u) * kPi / 180.0);
        const double m = static_cast<double>(c.stick_pct > 100 ? 100 : c.stick_pct) / 100.0;
        j.lx = static_cast<float>(m * std::sin(theta));
        j.ly = static_cast<float>(m * std::cos(theta));
        j.mag = static_cast<float>(m);
    } else if (live) {
        const InputPacket& p = sample.packet;
        held |= p.held;
        const double lx = p.lx / 32767.0, ly = p.ly / 32767.0;
        const double r = Clamp(std::sqrt(lx * lx + ly * ly), 0.0, 1.0);
        double theta = p.lmag > 655 ? p.ldir / 10000.0 : ((lx != 0.0 || ly != 0.0) ? std::atan2(lx, ly) : 0.0);
        const double delta = static_cast<double>(NormalizeCdeg(static_cast<std::int64_t>(p.cam_cdeg) - host_cam_cdeg)) * kPi / 18000.0;
        theta = NormalizeRadians(theta + c.yaw_sign * delta);
        j.lx = static_cast<float>(r * std::sin(theta));
        j.ly = static_cast<float>(r * std::cos(theta));
        j.mag = static_cast<float>(p.lmag / 65535.0);
        j.long12 = p.long12;
        if (c.rstick) {
            const double rx = p.rx / 32767.0, ry = p.ry / 32767.0;
            const double rr = Clamp(std::sqrt(rx * rx + ry * ry), 0.0, 1.0);
            j.rx = static_cast<float>(rx);
            j.ry = static_cast<float>(ry);
            j.rmag = j.rforce = static_cast<float>(rr);
            j.rdir = static_cast<float>((rx != 0.0 || ry != 0.0) ? std::atan2(rx, ry) : 0.0);
        }
        if (c.triggers) {
            j.lt = static_cast<float>(p.lt / 255.0);
            j.rt = static_cast<float>(p.rt / 255.0);
        }
    }
    held &= c.mask;
    const bool resync = edge.resync;
    edge.resync = false;
    j.resynced = resync;
    // No stale level survives a mode change or a disarm.
    if (!net || (resync && !live)) edge.net_held = 0;
    if (net) {
        if (live) {
            edge.net_held = sample.packet.held & c.mask;
        } else if (grace) {
            // Keep the last live level, no new edges, the run does not grow.
            held |= edge.net_held;
            j.hist_freeze = true;
        } else if (edge.net_held) {
            // Beyond the grace (or no sample): release silently.
            edge.prev_held &= ~(edge.net_held & ~held);
            edge.net_held = 0;
            j.hist_reset = true;
        }
    }
    // A tap the window never saw as held (pressed and released between two
    // samples): one window of held, so the press edge is this window and the
    // release edge the next one. Never on a resync (stale edge sets).
    const std::uint32_t taps =
        (live && !resync) ? (((sample.edge_press & sample.edge_release) | sample.edge_tap12) & c.mask & ~held & ~edge.prev_held) : 0u;
    held |= taps;
    j.synth = taps;
    if (resync) {
        // The first window after a disarm: the current level, no edges.
        edge.prev_held = held;
        j.hist_reset = true;
    }
    const std::uint32_t press = held & ~edge.prev_held;
    const std::uint32_t release = edge.prev_held & ~held;
    const bool fresh = live && !resync && sample.seq != edge.last_seq;
    const std::uint32_t net_words[3] = {fresh ? sample.packet.w4c : 0u, fresh ? sample.packet.w50 : 0u, fresh ? sample.packet.w54 : 0u};
    j.held = held;
    for (unsigned i = 0; i < 3; ++i) j.w[i] = WordValue(c.words[i], press, release, held, net_words[i]) & c.mask;
    j.edge_press = press;
    j.edge_release = release;
    j.long12 &= c.mask;
    j.tap12 = (live && !resync) ? (sample.edge_tap12 & c.mask) : 0u;
    // PP+0x18 = atan2f(LX, LY) and PP+0x14 = the world heading, exactly as the
    // PP update derives them from the stick it reads (0x12070CC..0x12070FE).
    j.dir = static_cast<float>((j.lx != 0.0f || j.ly != 0.0f) ? std::atan2(static_cast<double>(j.lx), static_cast<double>(j.ly)) : 0.0);
    j.heading = static_cast<float>(Heading(static_cast<double>(host_cam_cdeg) * kPi / 18000.0, j.lx, j.ly));
    edge.prev_held = held;
    if (live) edge.last_seq = sample.seq;
    return j;
}

Injection LevelOnly(const Injection& injection, const Config& config) noexcept {
    Injection j = injection;
    for (unsigned i = 0; i < 3; ++i) j.w[i] = config.words[i] == Word::held ? (j.held & config.mask) : 0u;
    j.edge_press = j.edge_release = 0;
    j.tap12 = 0;
    j.synth = 0;
    j.hist_freeze = j.hist_reset = false;
    return j;
}

void AccumulateEdges(InputEdges& e, const InputPacket& p) noexcept {
    if (p.version == 1) {
        e.press |= p.w50;
        e.release |= p.w54;
        if (p.w50 | p.w54) ++e.sets;
        return;
    }
    if (p.edge_seq == 0 || p.edge_seq == e.last_seq) return;
    e.last_seq = p.edge_seq;
    e.press |= p.w50;
    e.release |= p.w54;
    e.tap12 |= p.tap12;
    ++e.sets;
}

void HoldPush(HoldHistory& h, std::uint32_t held) noexcept {
    h.head = (h.head + 1) % kHoldHistory;
    h.ring[h.head] = held;
    if (h.count < kHoldHistory) ++h.count;
}

std::uint32_t HoldAt(const HoldHistory& h, std::uint32_t i) noexcept {
    if (i >= h.count || i >= kHoldHistory) return 0;
    return h.ring[(h.head + kHoldHistory - i) % kHoldHistory];
}

std::uint32_t ClassifyHold(const HoldHistory& h, std::uint32_t held_word, std::uint32_t release_word, std::uint32_t btn,
                           std::uint32_t frames) noexcept {
    const std::uint32_t bit = 1u << (btn & 31u);
    const bool released = (release_word & bit) != 0;
    if (!released && !(held_word & bit)) return 0;
    std::uint32_t count = 0;
    bool newest = false;
    for (std::uint32_t i = 0; i < kHoldHistoryUsed; ++i) {
        if (HoldAt(h, i) & bit) {
            ++count;
            if (i == 0) newest = true;
            if (count >= frames) break;
        } else if (count) {
            break;
        }
    }
    if (released && count < frames) return 2;
    if (newest) return count < frames ? 1u : 3u;
    return 0;
}

std::uint32_t NetHoldCode(bool live, std::uint32_t held, std::uint32_t long12, std::uint32_t tap12, std::uint32_t btn,
                          std::uint32_t frames) noexcept {
    if (!live || frames != kHoldFrames) return kHoldUseOwn;
    const std::uint32_t bit = 1u << (btn & 31u);
    if (tap12 & bit) return 2;
    if (held & bit) return (long12 & bit) ? 3u : 1u;
    return 0;
}

int BindingIndex(const std::uint64_t (&handles)[kBindingCount], std::uint64_t handle) noexcept {
    const std::uint64_t h = handle & kHandleMask;
    if (h == kUnboundHandle) return -1;
    for (std::uint32_t i = 0; i < kBindingCount; ++i)
        if ((handles[i] & kHandleMask) == h) return static_cast<int>(i);
    return -1;
}

void SenderObserve(SenderEdges& st, const PadSnapshot& snap) noexcept {
    const std::uint32_t held = snap.held;
    const std::uint32_t prev = st.have_last ? st.last_held : 0u;
    st.press |= snap.w50 | (held & ~prev);
    st.release |= snap.w54 | (prev & ~held);
    std::uint32_t long12 = 0;
    for (unsigned b = 0; b < 32; ++b) {
        const std::uint32_t bit = 1u << b;
        if (held & bit) {
            if (st.run[b] < 255) ++st.run[b];
            if (st.run[b] >= kHoldFrames) long12 |= bit;
        } else {
            if (st.run[b] > 0 && st.run[b] < kHoldFrames) st.tap12 |= bit;
            st.run[b] = 0;
        }
    }
    st.long12 = long12;
    st.last_held = held;
    st.have_last = true;
}

void SenderFill(SenderEdges& st, InputPacket& p) noexcept {
    p.long12 = st.long12;
    if (st.press | st.release | st.tap12) {
        p.edge_seq = static_cast<std::uint8_t>(st.seq == 255 ? 1 : st.seq + 1);
        p.edge_rep = 0;
        p.w50 = st.press;
        p.w54 = st.release;
        p.tap12 = st.tap12;
        st.filled_new = true;
    } else if (st.reps_left && st.seq) {
        p.edge_seq = st.seq;
        p.edge_rep = static_cast<std::uint8_t>(kEdgeRepeats - st.reps_left + 1);
        p.w50 = st.cur_press;
        p.w54 = st.cur_release;
        p.tap12 = st.cur_tap12;
        st.filled_new = false;
    } else {
        p.edge_seq = 0;
        p.edge_rep = 0;
        p.w50 = p.w54 = p.tap12 = 0;
        st.filled_new = false;
    }
}

void SenderSent(SenderEdges& st) noexcept {
    if (st.filled_new) {
        st.seq = static_cast<std::uint8_t>(st.seq == 255 ? 1 : st.seq + 1);
        st.cur_press = st.press;
        st.cur_release = st.release;
        st.cur_tap12 = st.tap12;
        st.press = st.release = st.tap12 = 0;
        st.reps_left = kEdgeRepeats;
        st.filled_new = false;
    } else if (st.reps_left) {
        --st.reps_left;
    }
}

std::uint32_t StepOverlay(TapState& state, std::uint32_t tap_seq, std::uint32_t tap_bits, std::uint32_t press_bits,
                          std::uint64_t press_until_ms, std::uint64_t now_ms) noexcept {
    if (tap_seq != state.last_seq) {
        state.last_seq = tap_seq;
        state.frames_left = tap_seq ? kTapFrames : 0;
        state.bits = tap_bits;
    }
    std::uint32_t bits = 0;
    if (state.frames_left) {
        bits |= state.bits;
        --state.frames_left;
    }
    if (now_ms < press_until_ms) bits |= press_bits;
    return bits;
}

std::int32_t Milli(float value) noexcept {
    const double v = static_cast<double>(value) * 1000.0;
    if (!std::isfinite(v)) return INT32_MIN;
    if (v >= 2147483647.0) return INT32_MAX;
    if (v <= -2147483647.0) return -INT32_MAX;
    return static_cast<std::int32_t>(std::llround(v));
}

bool SameArm(const Arm& a, const Arm& b) noexcept {
    return a.enabled == b.enabled && a.holdlog == b.holdlog && a.p218 == b.p218 && a.cam == b.cam && a.hold == b.hold &&
           a.wrap == b.wrap &&
           a.P == b.P && a.pp == b.pp && a.src == b.src && a.cam_object == b.cam_object && a.root0 == b.root0 &&
           a.root1 == b.root1 && a.blk1 == b.blk1 && a.rec1 == b.rec1 && a.plugin1 == b.plugin1 && a.p_vtable == b.p_vtable &&
           a.pp_vtable == b.pp_vtable && a.cam_vtable == b.cam_vtable &&
           std::memcmp(a.plugin_vt, b.plugin_vt, sizeof(a.plugin_vt)) == 0 && a.pool1 == b.pool1 &&
           a.config.mode == b.config.mode && a.config.mask == b.config.mask &&
           std::memcmp(a.config.words, b.config.words, sizeof(a.config.words)) == 0 && a.config.yaw_sign == b.config.yaw_sign &&
           a.config.rstick == b.config.rstick && a.config.triggers == b.config.triggers &&
           a.config.stick_deg == b.config.stick_deg && a.config.stick_pct == b.config.stick_pct &&
           a.config.stale_ms == b.config.stale_ms;
}

const char* SkipName(Skip skip) noexcept {
    switch (skip) {
    case Skip::player: return "player";
    case Skip::p_vtable: return "p_vtable";
    case Skip::flags: return "flags";
    case Skip::sync: return "sync";
    case Skip::state: return "state";
    case Skip::pp: return "pp";
    case Skip::src: return "src";
    case Skip::cam: return "cam";
    case Skip::ids: return "ids";
    case Skip::root: return "root";
    case Skip::plugin: return "plugin";
    case Skip::rec: return "rec";
    default: return "none";
    }
}

const char* FaultPhaseName(std::uint8_t phase) noexcept {
    switch (phase) {
    case 1: return "pre";
    case 2: return "call";
    case 3: return "post";
    default: return "none";
    }
}

} // namespace mgscoop::partner_input
