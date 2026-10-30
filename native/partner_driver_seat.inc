// The seated vehicle rider (partner_native_ride=seat; docs/anim-round1-seat.md,
// critic.md section 5). Included inside partner_driver.cpp's anonymous
// namespace. While a remote drives or rides a usable seat of a light vehicle or
// truck in 30010/30020 (and, by id only, Mother Base 30050: SeatCapableMission),
// body 1 stays realized and plays a targeted direct
// motion anchored to the receiver's own copy of that vehicle (Lua resolves the
// copy's locator name; partner_ride_seat arms the seat index and the camera
// skips). Phases: release (a dm=map clip is stopped first) -> arm (comparator
// and seat dwords) -> play (RIDE_SEAT reply op) -> seated (confirmed by the DM
// record; re-issued unconfirmed; sanity distance) -> exit (stop or GetOff
// advance) -> settle (dwords reverted after DM End, then the proven re-show
// flow: bits cleared, range_needs_place).
//
// 0.13.6 online ride (the first two-PC session, 2026-10-22, diag-other #1/#2/#9):
// - seated: body 1's slot follows the seated body (kSeatSlotFollowMs/Mm), so
//   the realize policy (which reads the slot) never unrealizes it mid-ride;
// - exit: an unrealized body 1 gets no stop (its DM ended with it; a stop to
//   it is never consumed and left g+0x388 bit 8 set for minutes), and settle
//   clears our stale request bits after such an exit or an exit_timeout;
// - temporary refusals (pending, dm_read, release, host_dm) are retried, not
//   latched, up to kSeatTransientTries in a row; the arm clears a stale
//   request of ours while nothing plays on either body;
// - fallback: a vehicle this copy lacks or has unrealized (by id: local_read,
//   unrealized; a Fultoned jeep) keeps body 1 visible, Warped to the remote's
//   streamed seated pose, instead of hiding it, and re-checks the seat every
//   kSeatFallbackRetryMs. Only there: a vehicle that exists here is never
//   Warped into, nor is the viewer's own vehicle.
// Africa fix (the two-PC test of 2026-10-23, bug 2): an id this copy lacks
// (absent: the by-id read refused live or index) takes the same fallback in
// free roam too (SeatFallbackHere): the host's relief vehicle 25600 existed
// only on his copy, and the named path (Lua no_name -> lua_refused) hid him
// for the whole ride.

namespace prs = mgscoop::partner_ride_seat;

// Refusals that describe a moment, not the vehicle: not latched (the next
// attempt may seat it) until kSeatTransientTries in a row (SeatRefuse).
bool SeatTransient(const char* reason) noexcept {
    return reason && (std::strcmp(reason, "pending") == 0 || std::strcmp(reason, "dm_read") == 0 ||
                      std::strcmp(reason, "release") == 0 || std::strcmp(reason, "host_dm") == 0);
}
// A leave caused by the link itself (the remote's lease or its ride stream went
// stale; the ride hide's reason ride_stale covers both): the receiver's gap is
// real, so the link-quality window is not rebased after it (0.13.5's quality
// hold applies). Any other seat exit or ride hide only skipped the observer.
bool LinkLeaveReason(const char* reason) noexcept {
    return reason && (std::strcmp(reason, "lease") == 0 || std::strcmp(reason, "ride_stale") == 0);
}
// The by-id gate reasons that mean "this copy has no usable vehicle with that
// id right now" (absent: no such vehicle here, it was Fultoned or despawned
// here or never spawned, like the other player's relief vehicle; local_read:
// the read failed elsewhere; unrealized: not realized): body 1 is shown
// without a seat (SeatPhase::fallback) instead of hidden.
bool SeatFallbackReason(const char* reason) noexcept {
    return reason && (std::strcmp(reason, "absent") == 0 || std::strcmp(reason, "local_read") == 0 ||
                      std::strcmp(reason, "unrealized") == 0);
}
// partner_vehicle::ReadVehicle's refusals that mean "no vehicle with that id on
// this copy": live (the index's pool bit *(object+0x28) is clear) and index (the
// index is past the pool count). The engine's own id lookup 0xA272A0, which the
// DM seat anchor uses (0x11987C9..0x11987F3), tests the same pool bit, so no
// seat, named or by id, can anchor on such an id. Every other refusal names a
// link of the read chain (service, vtables, records...), not the vehicle.
bool VehicleAbsentRefusal(const char* refusal) noexcept {
    return refusal && (std::strcmp(refusal, "live") == 0 || std::strcmp(refusal, "index") == 0);
}

void EmitSeat(Mission& m, const Hooks& h, const char* format, ...) noexcept {
    va_list arguments;
    va_start(arguments, format);
    EmitCappedV(m.seat_cap, h, "partner_ride_seat", nullptr, format, arguments);
    va_end(arguments);
}

bool SeatHooks(const Hooks& h) noexcept {
    return h.ride_seat_arm && h.ride_seat_revert && h.dm_targets && h.read_dm && h.dm_patch;
}

bool SeatMission(std::uint32_t mission) noexcept { return mission == 30010 || mission == 30020; }
// Mother Base (30050) has no authored vehicle locator (no veh_lv_/veh_trc_
// name), so only the seat by id (WS3 B1, partner_ride_seat_id=1, default on
// in lab and play) can seat the remote there: the seat flow runs in 30050 only
// with that key and only for an offered id (TickSeat refuses the rest before
// anything is armed). Hidden run bf0388c3: the real save continued into 30050,
// the remote drove the unnamed jeep 25603 (cat 1, realized, same category on
// both copies) and the guest hid it without any seat line (this gate,
// silently); hidden run abb0b7cf then seated it by id (offer=1, wrote=1,
// confirmed, err_mm 0..218 while it moved).
bool SeatByIdOnlyMission(std::uint32_t mission) noexcept { return mission == 30050; }
bool SeatCapableMission(std::uint32_t mission, bool seat_id) noexcept {
    return SeatMission(mission) || (seat_id && SeatByIdOnlyMission(mission));
}
// Where a by-id refusal shows body 1 without a seat (SeatPhase::fallback). Mother
// Base (only the id can seat): every fallback reason. Free roam (30010/30020):
// only an absent vehicle (no named seat can anchor there either: the Africa
// relief vehicle 25600 of 2026-10-23, an authored jeep removed on one copy, hidden
// run 075c1ea4 part B); any other refusal keeps the proven named path.
bool SeatFallbackHere(std::uint32_t mission, const char* reason) noexcept {
    return SeatFallbackReason(reason) && (!SeatMission(mission) || std::strcmp(reason, "absent") == 0);
}

// The (vehicle, seat) a refusal or a get-off ended stays unseated (it hides)
// until the remote's live ride leaves it.
bool SeatBlocked(const Mission::Seat& s, const partner_ride_wire::Packet& ride) noexcept {
    return s.blocked && ride.kind == partner_ride_wire::Kind::vehicle && ride.vehicle == s.block_vehicle && ride.seat == s.block_seat;
}

// Why seat mode draws a live vehicle ride hidden instead of seated (nullptr:
// seat mode applies, or this is not a seat-mode vehicle ride). The order is
// seat_capable's (partner_driver.cpp), then the block latch.
const char* SeatUnavailable(const Driver& d, const Hooks& h, bool ride_live) noexcept {
    namespace rw = partner_ride_wire;
    const Mission& m = d.mission;
    const rw::Packet p = DrawnRide(d);
    if (d.settings.nride != PartnerNativeRide::seat || !ride_live || p.kind != rw::Kind::vehicle) return nullptr;
    if (!rw::SeatUsable(p.seat, p.vehicle)) return "seat_unknown";
    if (!SeatCapableMission(m.mission_code, d.settings.seat_id)) return "mission";
    if (!SeatHooks(h)) return "hooks";
    if (h.native_pose_suppressed && h.native_pose_suppressed(h.context)) return "native_pose";
    if (SeatBlocked(m.seat, p)) return "blocked";
    return nullptr;
}

// No seat refusal hides silently: each reason is logged once per (vehicle,
// seat) while it holds (a latched refusal logged its episode's exit first).
void SeatLogUnavailable(Driver& d, const Hooks& h, const char* why) noexcept {
    Mission& m = d.mission;
    const partner_ride_wire::Packet p = DrawnRide(d);
    if (!why) {
        m.seat_unavailable = nullptr;
        return;
    }
    if (why == m.seat_unavailable && p.vehicle == m.seat_unavailable_vehicle && p.seat == m.seat_unavailable_seat) return;
    m.seat_unavailable = why;
    m.seat_unavailable_vehicle = p.vehicle;
    m.seat_unavailable_seat = p.seat;
    const bool blocked = std::strcmp(why, "blocked") == 0;
    EmitSeat(m, h, "partner_ride_seat=unavailable; vehicle=%u; seat=%u; reason=%s; mission=%u; byid=%u; block=%s\r\n", unsigned(p.vehicle),
             unsigned(p.seat), why, unsigned(m.mission_code), d.settings.seat_id ? 1u : 0u,
             blocked && m.seat.block_reason ? m.seat.block_reason : "-");
}

// Per tick, before the visibility step: the get-in window (when the live ride
// became this vehicle) and the block latch's release.
void SeatObserveRide(Mission::Seat& s, bool live, const partner_ride_wire::Packet& ride, std::uint64_t now) noexcept {
    namespace rw = partner_ride_wire;
    const bool vehicle = live && ride.kind == rw::Kind::vehicle;
    const bool seat_ok = vehicle && rw::SeatUsable(ride.seat, ride.vehicle);
    if ((vehicle && (!s.vehicle_live || ride.vehicle != s.vehicle_rev)) || (seat_ok && !s.seat_live)) s.vehicle_since_ms = now;
    s.vehicle_live = vehicle;
    s.seat_live = seat_ok;
    s.vehicle_rev = vehicle ? ride.vehicle : rw::kVehicleUnknown;
    if (s.blocked && live && (ride.kind != rw::Kind::vehicle || ride.vehicle != s.block_vehicle || ride.seat != s.block_seat)) {
        s.blocked = false;
        s.block_reason = nullptr;
    }
    // A new ride starts a new count of temporary refusals.
    if (s.transient_refusals && live && (ride.kind != rw::Kind::vehicle || ride.vehicle != s.transient_vehicle || ride.seat != s.transient_seat))
        s.transient_refusals = 0;
}

// The seat flow owns body 1: the native arbiter is blocked with reason seat
// (a network reason: no latch), the deferral and the window are disarmed and
// the native targets withdrawn, exactly as a ride hide does, but the DM
// comparator patch and the realize bits stay.
void SeatOwnBody(Driver& d, const Hooks& h, std::uint64_t now) noexcept {
    Mission& m = d.mission;
    const Settings& set = d.settings;
    pnat::In blocked{};
    blocked.configured = set.drive == PartnerDrive::native;
    blocked.defer_on = true;
    blocked.now_ms = now;
    blocked.visibility_reason = "seat";
    blocked.dm_revert_failed = m.dm_revert_failed;
    m.native_out = pnat::Step(m.native, blocked, pnat::Params{});
    if (m.native_out.req_epoch) ++m.body_epoch;
    if (m.native_out.req_dmmap_rearm) d.dmmap_rearm = true;
    if (h.set_defer && (m.native_defer_armed || set.drive == PartnerDrive::native))
        h.set_defer(h.context, false, EffectivePartnerDefer(set.drive, set.defer));
    m.native_defer_armed = false;
    WithdrawNativeTargets(m, h);
    DisarmInput(d, h, "seat");
    PublishDisarmed(d, h);
    m.tele.configured = set.drive == PartnerDrive::native;
    m.tele.mode = m.native_out.mode;
    CopyText(m.tele.reason, sizeof(m.tele.reason), "seat");
    m.tele.latched = m.native_out.mode == pnat::Mode::latched;
    m.tele.armed = m.tele.input_live = m.tele.body_valid = false;
    m.tele.net.local_drive = set.drive == PartnerDrive::native ? partner_native_net::Drive::clips : partner_native_net::Drive::off;
    CopyText(m.tele.net.local_reason, sizeof(m.tele.net.local_reason), "seat");
    d.settings.fire_live = false;
}

struct SeatTickIn {
    bool capable;          // nride=seat, net source, 30010/30020, hooks, no private pose lease
    bool seated;           // the visibility policy's seated state
    bool ride_live;
    partner_ride_wire::Kind kind;
    std::uint8_t ride_seat;
    std::uint16_t ride_vehicle;
    bool lease_live;
    bool remote_anim_valid;
    std::uint16_t remote_act;
    bool pose_valid;
    partner::Pose pose;
    bool realized, parts_loaded;
    std::uint32_t distance_mm;  // to the local player
    std::uint64_t now;
    bool terminating;
};
struct SeatTickOut {
    bool own;
    std::uint32_t ops;
    std::uint16_t vehicle;
    std::uint8_t seat, action, entry;
};

void SeatGo(Mission::Seat& s, SeatPhase phase, std::uint64_t now) noexcept {
    s.phase = phase;
    s.phase_ms = now;
}

// Leaves through exit (stop or GetOff, then settle). latch blocks this
// (vehicle, seat) until the remote's ride changes (refusals, get-off).
void SeatExit(Mission& m, const Hooks& h, const char* reason, bool latch, std::uint64_t now) noexcept {
    auto& s = m.seat;
    if (s.phase == SeatPhase::exit || s.phase == SeatPhase::settle || s.phase == SeatPhase::idle) return;
    // The fallback never played or armed anything on body 1: nothing to stop.
    s.exit_quiet = s.phase == SeatPhase::fallback;
    s.exit_link = LinkLeaveReason(reason);
    s.exit_reason = reason;
    s.latch_exit = latch;
    s.issue_ms = 0;
    s.stops = 0;
    s.next_sent = false;
    SeatGo(s, SeatPhase::exit, now);
    (void)h;
}

void SeatRefuse(Mission& m, const Hooks& h, const char* reason, std::uint64_t now) noexcept {
    auto& s = m.seat;
    EmitSeat(m, h, "partner_ride_seat=refused; vehicle=%u; seat=%u; name=-; state=-; reason=%s; manual=%u\r\n", unsigned(s.vehicle),
             unsigned(s.seat), reason, s.manual ? 1u : 0u);
    bool latch = !s.manual && !SeatTransient(reason);
    if (!s.manual && SeatTransient(reason)) {
        // Retried, not latched, until the same (vehicle, seat) was refused
        // kSeatTransientTries times in a row (a dm_read failure for the whole
        // ride would otherwise re-show and drop body 1 every 3-4 s).
        if (s.transient_vehicle != s.vehicle || s.transient_seat != s.seat) {
            s.transient_vehicle = s.vehicle;
            s.transient_seat = s.seat;
            s.transient_refusals = 0;
        }
        latch = ++s.transient_refusals >= kSeatTransientTries;
        if (latch)
            EmitSeat(m, h, "partner_ride_seat=retries; vehicle=%u; seat=%u; reason=%s; tries=%u; latch=1\r\n", unsigned(s.vehicle),
                     unsigned(s.seat), reason, s.transient_refusals);
    }
    SeatExit(m, h, reason, latch, now);
}

// Body 1 stays shown without a seat (SeatPhase::fallback): nothing is armed or
// playing on it; TickSeat Warps it to the remote's streamed pose every tick.
void SeatEnterFallback(Mission& m, const Hooks& h, const char* reason, std::uint64_t now) noexcept {
    auto& s = m.seat;
    s.fallback_want = nullptr;
    s.fallback_reason = reason;
    s.fallback_ms = s.fallback_check_ms = now;
    SeatGo(s, SeatPhase::fallback, now);
    EmitSeat(m, h, "partner_ride_seat=fallback; vehicle=%u; seat=%u; reason=%s; show=warp; byid=%u\r\n", unsigned(s.vehicle),
             unsigned(s.seat), reason ? reason : "unknown", s.byid ? 1u : 0u);
}

// The truthful realize state while the seat owns body 1 (the deferred reply
// clears the published readiness; the nametag follows the seated body).
void SeatReportRealized(Mission& m, const Hooks& h, bool realized, bool parts_loaded, std::uint64_t now) noexcept {
    char line[160]{};
    if (realized && !m.realized) {
        m.realized_ms = now;
        ++m.realize_gen;
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "partner_realized=1; R=1; via=seat\r\n"))) LogTransition(m, h, line);
    } else if (!realized && m.realized) {
        ++m.body_epoch;
        ++m.realize_gen;
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "partner_realized=0; R=1; via=seat\r\n"))) LogTransition(m, h, line);
    }
    m.realized = realized;
    m.parts_loaded = realized && parts_loaded;
}

void SeatDmProbe(Mission& m, const Hooks& h, bool dm_ok, const pp::DmState& dm, const char* when) noexcept {
    if (!dm_ok) {
        EmitSeat(m, h, "partner_ride_seat=dm; when=%s; read=0\r\n", when);
        return;
    }
    EmitSeat(m, h,
             "partner_ride_seat=dm; when=%s; q18=0x%x,0x%x; g388=0x%02x; req0=%u; rec18=0x%llx; rec1a0=%u; rec248=0x%02x; cam1f0=0x%llx; "
             "cam200=%u; cam_ok=%u\r\n",
             when, unsigned(dm.q18[0]), unsigned(dm.q18[1]), unsigned(dm.g388), unsigned(dm.req0), static_cast<unsigned long long>(dm.rec18),
             unsigned(dm.rec1a0), unsigned(dm.rec248), static_cast<unsigned long long>(dm.cam1f0), dm.cam200, dm.cam_ok ? 1u : 0u);
}

// Settle: the dwords go once DM End ran (or the exit gave up), then the body
// is handed to the proven re-show flow (bits cleared, placed, realized).
void SeatSettle(Driver& d, const Hooks& h, std::uint64_t now, bool terminating) noexcept {
    Mission& m = d.mission;
    auto& s = m.seat;
    const char* reason = s.exit_reason ? s.exit_reason : "unknown";
    if (s.armed && h.ride_seat_revert) h.ride_seat_revert(h.context, reason);
    s.armed = false;
    EmitSeat(m, h,
             "partner_ride_seat=exit; vehicle=%u; seat=%u; reason=%s; via=%s; confirmed=%u; plays=%u; stops=%u; ms=%u; manual=%u; "
             "follows=%u\r\n",
             unsigned(s.vehicle), unsigned(s.seat), reason,
             s.body_gone ? "gone" : s.exit_quiet ? "fallback" : s.next_sent ? "getoff" : "stop", s.confirmed ? 1u : 0u, s.plays, s.stops,
             Clamp32(now - s.phase_ms), s.manual ? 1u : 0u, s.slot_follows);
    // The exit gave up, or body 1 vanished under the DM: no DM End will clear
    // what this episode requested (2026-10-22: bit 8 of three stops sent to an
    // unrealized body stayed set ~3 min and refused every later seat as
    // pending). Ours only (partner_player::ClearDmBits checks request byte 0
    // and the viewer's own DM).
    if ((s.body_gone || std::strcmp(reason, "exit_timeout") == 0) && h.dm_clear_stale) {
        const std::uint8_t cleared = h.dm_clear_stale(h.context, static_cast<std::uint8_t>(pp::kDmBitPlay | s.req_bits));
        if (cleared)
            EmitSeat(m, h, "partner_ride_seat=stale_cleared; vehicle=%u; seat=%u; bits=0x%02x; when=settle; reason=%s\r\n",
                     unsigned(s.vehicle), unsigned(s.seat), unsigned(cleared), reason);
    }
    // The native arbiter did not observe the link while the seat owned body 1:
    // a new window, unless the link itself ended the episode (lease,
    // ride_stale: that gap is real and keeps the quality hold).
    m.net_quality_rebase = !s.exit_link;
    if (s.latch_exit) {
        s.blocked = true;
        s.block_vehicle = s.vehicle;
        s.block_seat = s.seat;
        s.block_reason = reason;
    }
    if (m.bits_set) ClearBits(d, h, "seat_exit", terminating);
    m.range_needs_place = true;
    m.pose_applied = m.have_cmd = m.hybrid_placed = false;
    m.visibility_place_try_ms = 0;
    partner_dmmap::SpeedReset(m.dmmap_speed);
    partner_dmmap::CancelFiniteEvents(m.dmmap);
    m.dmmap_turn = {};
    m.dmmap_source_turns = {};
    d.dmmap_rearm = true;
    ++m.body_epoch;
    const bool blocked = s.blocked;
    const std::uint16_t bv = s.block_vehicle;
    const std::uint8_t bs = s.block_seat;
    const char* br = s.block_reason;
    const std::uint64_t since = s.vehicle_since_ms;
    const std::uint16_t rev = s.vehicle_rev;
    const bool live = s.vehicle_live;
    const bool slive = s.seat_live;
    const unsigned tries = s.transient_refusals;
    const std::uint16_t tv = s.transient_vehicle;
    const std::uint8_t ts = s.transient_seat;
    s = Mission::Seat{};
    s.blocked = blocked; s.block_vehicle = bv; s.block_seat = bs; s.block_reason = br;
    s.vehicle_since_ms = since; s.vehicle_rev = rev; s.vehicle_live = live; s.seat_live = slive;
    s.transient_refusals = tries; s.transient_vehicle = tv; s.transient_seat = ts;
    m.ride_seated = false;
}

void SeatStatus(Mission& m, const Hooks& h, const pp::State& st, const SeatTickIn& in, bool dm_ok, const pp::DmState& dm,
                std::uint32_t err_mm) noexcept {
    auto& s = m.seat;
    if (s.status_ms && in.now - s.status_ms < kSeatStatusMs) return;
    s.status_ms = in.now;
    if (m.seat_status_lines >= kSeatStatusCap) {
        if (!m.seat_status_capped) {
            m.seat_status_capped = true;
            EmitFormat(h, "partner_ride_seat=capped; limit=%u; kind=status\r\n", kSeatStatusCap);
        }
        return;
    }
    ++m.seat_status_lines;
    (void)st;
    EmitFormat(h,
               "partner_ride_seat=status; phase=%s; vehicle=%u; seat=%u; confirmed=%u; q18=0x%x,0x%x; rec18=0x%llx; rec1a0=%u; "
               "rec248=0x%02x; cam1f0=0x%llx; cam200=%u; err_mm=%d; remote_act=%u; manual=%u; byid=%u\r\n",
               SeatPhaseName(s.phase), unsigned(s.vehicle), unsigned(s.seat), s.confirmed ? 1u : 0u, dm_ok ? unsigned(dm.q18[0]) : 0u,
               dm_ok ? unsigned(dm.q18[1]) : 0u, dm_ok ? static_cast<unsigned long long>(dm.rec18) : 0ull, dm_ok ? unsigned(dm.rec1a0) : 0u,
               dm_ok ? unsigned(dm.rec248) : 0u, dm_ok ? static_cast<unsigned long long>(dm.cam1f0) : 0ull, dm_ok ? dm.cam200 : 0u,
               err_mm == UINT32_MAX ? -1 : static_cast<int>(err_mm), unsigned(in.remote_act), s.manual ? 1u : 0u,
               s.target_wrote ? 1u : 0u);
}

// Seat by id (WS3 B1, docs/vehicle-ws3.md; partner_ride_seat_id=1): the id
// path is offered (seat_entry::by_id) when the remote's live ride addendum
// names its vehicle category for this exact ride, this copy's same-id vehicle
// is realized (rec+0x6E != 0xFF) and of the same category, the viewer does not
// ride that id itself, and both hooks exist. Lua then plays an unnamed
// vehicle's chain untargeted (locator ""); lv/trc only (GetVehicleType).
// nullptr: offer; else the static reason (the 3 m sanity check still applies).
// absent (the Africa fix): this copy has no vehicle with that id (VehicleAbsentRefusal);
// read before the addendum checks, which cannot change that (the remote's
// category is still logged when its addendum is live).
const char* SeatByIdGate(Driver& d, const Hooks& h, std::uint64_t now) noexcept {
    namespace rw = partner_ride_wire;
    auto& s = d.mission.seat;
    s.remote_category = s.local_category = rw::kCategoryUnknown;
    if (!h.vehicle_read || !h.seat_target) return "unavailable";
    const std::uint32_t epoch = h.link_session ? h.link_session(h.context) : 0;
    if (d.ride_rx.packet.kind != rw::Kind::vehicle || d.ride_rx.packet.vehicle != s.vehicle || DrawnRide(d).seat != s.seat)
        return "vehicle_change";
    partner_vehicle::Read r{};
    h.vehicle_read(h.context, s.vehicle, r);
    const bool absent = VehicleAbsentRefusal(r.refusal);
    if (!rw::ExtLive(d.ride_ext, epoch, now)) return absent ? "absent" : "no_ext";
    s.remote_category = rw::RemoteCategory(d.ride_ext, d.ride_rx.packet, epoch, now);
    if (absent) return "absent";
    if (s.remote_category == rw::kCategoryUnknown) return "remote_category";
    // F1 vehicles (partner_ride_board=passenger): the peer in a back seat of the vehicle the
    // viewer drives is seated there (never rider[]); any other seat of it stays refused.
    if (s.vehicle == d.local_vehicle &&
        !partner_board::LocalVehicleSeatAllowed(BoardMode(d.settings.ride_board), s.seat, d.settings.ride_front || d.settings.ride_cab))
        return "local_vehicle";
    // The viewer's own back seat (Lua's shared_seat guard needs ON_VEHICLE and cannot see a passenger).
    if (partner_board::SharedWithLocal(d.local_passenger, s.vehicle, s.seat)) return "shared_seat";
    if (r.refusal || r.unit.id != s.vehicle) return "local_read";
    s.local_category = partner_vehicle::Category(r.unit.type);
    if (r.unit.realized == partner_vehicle::kNotRealized) return "unrealized";
    if (s.local_category != s.remote_category) return "category";
    return nullptr;
}

// Once the untargeted seat DM is live on body 1 (q18[1] == 0x8B, a seated
// state of the seat), on this (the player-update) thread: every chained
// entry's target id first, then flag 2 (partner_player::WriteSeatTarget;
// critic #17). A named locator's chain is already targeted (wrote=0).
const char* SeatTarget(Driver& d, const Hooks& h, std::uint64_t now) noexcept {
    Mission& m = d.mission;
    auto& s = m.seat;
    if (const char* why = SeatByIdGate(d, h, now)) {
        EmitSeat(m, h, "partner_ride_seat=target; vehicle=%u; seat=%u; wrote=0; reason=%s; remote_cat=%u; local_cat=%u\r\n",
                 unsigned(s.vehicle), unsigned(s.seat), why, unsigned(s.remote_category), unsigned(s.local_category));
        return why;
    }
    const pp::SeatTargetRequest request{s.vehicle, s.seat, prs::ChainEntries(s.entry)};
    pp::SeatTargetResult r{};
    const char* why = h.seat_target(h.context, request, r);
    EmitSeat(m, h,
             "partner_ride_seat=target; vehicle=%u; seat=%u; wrote=%u; entries=%u; cur=%u; ids=%u,%u,%u; flags=0x%02x,0x%02x,0x%02x; "
             "reason=%s; ms=%u\r\n",
             unsigned(s.vehicle), unsigned(s.seat), r.wrote ? 1u : 0u, unsigned(request.entries), unsigned(r.cur), unsigned(r.ids[0]),
             unsigned(r.ids[1]), unsigned(r.ids[2]), unsigned(r.flags[0]), unsigned(r.flags[1]), unsigned(r.flags[2]), why ? why : "none",
             Clamp32(now - s.issue_ms));
    if (why) return why;
    s.target_done = true;
    s.target_wrote = r.wrote;
    return nullptr;
}

// One step of the seat flow. own: this tick's reply is the seat's (deferred
// pose, the seat ops only); the caller then skips every movement lever.
SeatTickOut TickSeat(Driver& d, const Hooks& h, const pp::State& st, const SeatTickIn& in) noexcept {
    namespace rw = partner_ride_wire;
    Mission& m = d.mission;
    auto& s = m.seat;
    const Settings& set = d.settings;
    SeatTickOut out{};
    const std::uint64_t now = in.now;
    if (s.phase == SeatPhase::idle) {
        const bool start = in.capable && in.seated && in.ride_live && in.kind == rw::Kind::vehicle &&
                           rw::SeatUsable(in.ride_seat, in.ride_vehicle) && m.bits_set && in.realized && in.parts_loaded &&
                           !m.range_hidden && !m.range_needs_place;
        if (!start) return out;
        s.manual = false;
        s.vehicle = in.ride_vehicle;
        s.seat = in.ride_seat;
        s.entry = 0;
        s.loop_entry = 0;
        if (set.seat_enter == PartnerRideSeatEnter::getin && s.vehicle_live && now - s.vehicle_since_ms <= kSeatGetInMs) {
            s.entry |= partner::seat_entry::get_on;
            s.loop_entry = 1;
        }
        if (set.seat_exit == PartnerRideSeatExit::getoff) s.entry |= partner::seat_entry::get_off;
        // Seat by id (WS3 B1): offered per episode, re-checked at the write.
        s.byid = s.target_done = s.target_wrote = false;
        s.byid_reason = nullptr;
        if (set.seat_id) {
            s.byid_reason = SeatByIdGate(d, h, now);
            s.byid = !s.byid_reason;
            if (s.byid) s.entry |= partner::seat_entry::by_id;
            EmitSeat(m, h, "partner_ride_seat=byid; vehicle=%u; seat=%u; offer=%u; reason=%s; remote_cat=%u; local_cat=%u\r\n",
                     unsigned(s.vehicle), unsigned(s.seat), s.byid ? 1u : 0u, s.byid_reason ? s.byid_reason : "none",
                     unsigned(s.remote_category), unsigned(s.local_category));
        }
        s.plays = s.stops = 0;
        s.confirmed = s.loop_confirmed = s.getoff_confirmed = s.next_sent = s.latch_exit = s.lua_refused = false;
        s.exit_reason = nullptr;
        s.far_since_ms = s.status_ms = s.issue_ms = s.confirm_ms = 0;
        s.fallback_want = s.fallback_reason = nullptr;
        s.fallback_ms = s.fallback_check_ms = 0;
        s.fallback_warps = 0;
        s.exit_quiet = s.body_gone = false;
        s.req_bits = 0;
        s.slot_check_ms = 0;
        s.slot_follows = 0;
        s.stale_check_ms = 0;
        SeatGo(s, SeatPhase::release, now);
        m.visibility_logged = nullptr;  // the ride hide's restore path never runs over the seat
        EmitCapped(m.ride_vis_cap, h, "partner_ride", "partner_ride=seat_show; vehicle=%u; seat=%u\r\n", unsigned(s.vehicle),
                   unsigned(s.seat));
        EmitSeat(m, h, "partner_ride_seat=start; vehicle=%u; seat=%u; entry=%u; enter=%s; exit=%s; remote_act=%u\r\n", unsigned(s.vehicle),
                 unsigned(s.seat), unsigned(s.entry), PartnerRideSeatEnterName(set.seat_enter), PartnerRideSeatExitName(set.seat_exit),
                 unsigned(in.remote_act));
        // Mother Base has no authored locator: without the id offer Lua could
        // only refuse it (no_name), so it is refused here, before the release
        // or the arm touches anything (latched: it hides until the ride changes).
        // 0.13.6: a vehicle this copy lacks (local_read: Fultoned here, the
        // 2026-10-22 session) or has unrealized keeps body 1 visible without a
        // seat once the release ran (SeatPhase::fallback), not hidden.
        // The Africa fix: in free roam too for an absent vehicle (SeatFallbackHere: the
        // 2026-10-23 Africa relief vehicle); there any other refusal keeps the
        // named path (Lua plays the locator name).
        if (!s.byid && SeatFallbackHere(m.mission_code, s.byid_reason)) s.fallback_want = s.byid_reason;
        else if (!SeatMission(m.mission_code) && !s.byid) SeatRefuse(m, h, s.byid_reason ? s.byid_reason : "byid", now);
        // F1 vehicles: the remote in the viewer's own back seat (a named vehicle never reaches
        // SeatByIdGate): refused before anything is armed (latched, it hides).
        else if (partner_board::SharedWithLocal(d.local_passenger, s.vehicle, s.seat)) SeatRefuse(m, h, "shared_seat", now);
    }
    out.own = true;
    m.ride_seated = true;
    SeatOwnBody(d, h, now);
    pp::DmTargets targets{};
    pp::DmState dm{};
    bool dm_ok = false;
    if (h.dm_targets && h.read_dm) {
        h.dm_targets(h.context, st, targets);
        dm_ok = h.read_dm(h.context, st, targets, dm);
    }
    const bool body_dm = dm_ok && dm.q18[1] == c::q18_dm_act;
    const bool host_dm = dm_ok && dm.q18[0] == c::q18_dm_act;
    // A Lua refusal of the current request (LuaLog) ends the episode.
    if (s.lua_refused) {
        s.lua_refused = false;
        if (s.phase != SeatPhase::exit && s.phase != SeatPhase::settle) SeatRefuse(m, h, "lua_refused", now);
    }
    // Exit triggers (the manual lever ends only on its stop, the DM's end or an unrealized body).
    const char* why = nullptr;
    bool latch = false;
    if (s.phase != SeatPhase::exit && s.phase != SeatPhase::settle) {
        if (!s.manual) {
            const bool heli = in.remote_anim_valid && in.remote_act >= rw::kHeliActFirst && in.remote_act <= rw::kHeliActLast;
            if (!in.capable) why = "config";
            else if (!in.lease_live) why = "lease";
            else if (!in.ride_live) why = "ride_stale";
            else if (in.kind != rw::Kind::vehicle) why = in.kind == rw::Kind::foot ? "foot" : "ride";
            else if (in.ride_vehicle != s.vehicle || in.ride_seat != s.seat) why = "vehicle_change";
            else if (heli) why = "heli";
            else if (in.remote_anim_valid && in.remote_act == 112) { why = "getoff_act"; latch = true; }
            else if (!in.seated) why = "unseated";
            else if (in.distance_mm >= 200000) why = "range";
        }
        if (!why && (!in.realized || !in.parts_loaded)) why = "unrealized";
        if (why) SeatExit(m, h, why, latch, now);
    }
    switch (s.phase) {
    case SeatPhase::fallback: {
        // Shown without a seat: Warped to the remote's streamed (seated) pose
        // every tick (the proven per-tick Warp of the input fallback; Warp also
        // keeps the slot, which the realize policy reads, with the body). Never
        // while a DM plays on body 1 (a Warp ends it; its End would write the
        // viewer's camera connect, the seat dwords are not armed here), and
        // never into a vehicle the viewer occupies on this copy (it drives it or
        // rides it as a passenger; the fallback is for a vehicle missing here).
        const bool viewer_in = s.vehicle == d.local_vehicle || (d.local_passenger.valid && d.local_passenger.vehicle == s.vehicle);
        if (in.pose_valid && in.realized && in.parts_loaded && !body_dm && !viewer_in) {
            out.ops |= partner::op::warp;
            ++s.fallback_warps;
        }
        if (now - s.fallback_check_ms < kSeatFallbackRetryMs) break;
        s.fallback_check_ms = now;
        // The vehicle may exist / realize here now (the viewer came near it).
        if (const char* gate_why = SeatByIdGate(d, h, now)) {
            if (!SeatFallbackReason(gate_why)) SeatRefuse(m, h, gate_why, now);  // as at the start: latched (it hides)
            else s.fallback_reason = gate_why;
            break;
        }
        // It exists here: the fallback ends at once (never a Warp into a vehicle
        // this copy has); a DM on either body or a pending request is the
        // release's and the arm's (stopped / cleared when ours / retried).
        s.byid = true;
        s.byid_reason = nullptr;
        s.entry |= partner::seat_entry::by_id;
        EmitSeat(m, h, "partner_ride_seat=fallback_end; vehicle=%u; seat=%u; reason=%s; next=release; ms=%u; warps=%u; byid=%u\r\n",
                 unsigned(s.vehicle), unsigned(s.seat), s.fallback_reason ? s.fallback_reason : "unknown", Clamp32(now - s.fallback_ms),
                 s.fallback_warps, s.byid ? 1u : 0u);
        s.fallback_reason = nullptr;
        out.ops = 0;  // the release / arm / play of this tick sends its own ops
        s.stops = 0;
        s.issue_ms = 0;
        SeatGo(s, SeatPhase::release, now);
        [[fallthrough]];
    }
    case SeatPhase::release: {
        if (!dm_ok) {
            if (now - s.phase_ms >= kSeatReleaseMs * 2) SeatRefuse(m, h, "dm_read", now);
            break;
        }
        if (pdo::Active(m.overlay)) {
            // A DM overlay stops its own clip (OverlayBlocked: seat) before the seat stops anything.
            if (now - s.phase_ms >= kSeatExitMs) SeatRefuse(m, h, "overlay", now);
            break;
        }
        if (body_dm) {
            // A dm=map clip (or any DM) on body 1 is stopped first; Stop is
            // global, so never while the viewer's own DM runs.
            if (host_dm) {
                if (now - s.phase_ms >= kSeatExitMs) SeatRefuse(m, h, "host_dm", now);
                break;
            }
            if (!s.issue_ms || now - s.issue_ms >= kSeatStopMs) {
                if (++s.stops > 3) { SeatRefuse(m, h, "release", now); break; }
                out.ops |= partner::op::dm_stop;
                s.req_bits |= pp::kDmBitStop;
                s.issue_ms = now;
                EmitSeat(m, h, "partner_ride_seat=release; vehicle=%u; seat=%u; rec18=0x%llx; attempt=%u\r\n", unsigned(s.vehicle),
                         unsigned(s.seat), static_cast<unsigned long long>(dm.rec18), s.stops);
            }
            break;
        }
        if (m.dmmap.owned || m.dmmap.phase != pdm::Phase::idle) pdm::Forget(m.dmmap);
        m.dm_play_pending = false;
        s.stops = 0;
        s.issue_ms = 0;
        // No usable vehicle here (by id): shown without a seat, nothing armed.
        if (s.fallback_want) {
            SeatEnterFallback(m, h, s.fallback_want, now);
            break;
        }
        SeatGo(s, SeatPhase::arm, now);
        [[fallthrough]];
    }
    case SeatPhase::arm: {
        if (!dm_ok) {
            if (now - s.phase_ms >= kSeatReleaseMs * 2) SeatRefuse(m, h, "dm_read", now);
            break;
        }
        // No DM on either body and nothing pending (the dwords are global
        // while armed; DirectMotion Stop/GoNext are global too).
        if (host_dm || body_dm || (dm.g388 & 0x3C)) {
            // 0.13.6: nothing plays on either body, so no DM End will clear a request
            // still pending (a Stop or GoNext would hit the next DM that starts, this
            // seat's play included): when it is ours (request byte 0 == 1, the last
            // Play named body 1) it is cleared, at most once per kSeatStopMs.
            if (!host_dm && !body_dm && h.dm_clear_stale && now - s.phase_ms >= kSeatStopMs &&
                (!s.stale_check_ms || now - s.stale_check_ms >= kSeatStopMs)) {
                s.stale_check_ms = now ? now : 1;
                const std::uint8_t cleared =
                    h.dm_clear_stale(h.context, static_cast<std::uint8_t>(pp::kDmBitPlay | pp::kDmBitStop | pp::kDmBitNext));
                if (cleared)
                    EmitSeat(m, h, "partner_ride_seat=stale_cleared; vehicle=%u; seat=%u; bits=0x%02x; when=arm; reason=pending\r\n",
                             unsigned(s.vehicle), unsigned(s.seat), unsigned(cleared));
            }
            // A foreign request or the viewer's own DM is a moment, not the vehicle:
            // refused without a latch (retried, SeatRefuse). No Warp here: this vehicle
            // exists on this copy (it may be the one the viewer drives or rides).
            if (now - s.phase_ms >= kSeatExitMs) SeatRefuse(m, h, host_dm ? "host_dm" : body_dm ? "body_dm" : "pending", now);
            break;
        }
        if (!m.dm_patched) {
            m.dm_patched = h.dm_patch(h.context, true, "seat");
            if (!m.dm_patched) {
                m.dm_patch_failed = true;
                SeatRefuse(m, h, "dm_patch", now);
                break;
            }
        }
        if (!s.armed) {
            if (!h.ride_seat_arm(h.context, s.seat, 0xFFFF)) {
                SeatRefuse(m, h, "seat_patch", now);
                break;
            }
            s.armed = true;
            EmitSeat(m, h, "partner_ride_seat=armed; vehicle=%u; seat=%u; name=-; state=-; reason=none; cam1f0=0x%llx; cam200=%u\r\n",
                     unsigned(s.vehicle), unsigned(s.seat), static_cast<unsigned long long>(dm.cam1f0), dm.cam200);
        }
        SeatGo(s, SeatPhase::play, now);
        [[fallthrough]];
    }
    case SeatPhase::play: {
        out.ops |= partner::op::ride_seat;
        out.vehicle = s.vehicle;
        out.seat = s.seat;
        out.action = partner::seat_action::play;
        out.entry = s.entry;
        s.issue_ms = now;
        ++s.plays;
        s.confirmed = s.loop_confirmed = s.getoff_confirmed = false;
        s.target_done = s.target_wrote = false;
        EmitSeat(m, h, "partner_ride_seat=play; vehicle=%u; seat=%u; name=lua; state=lua; entry=%u; attempt=%u; reason=none\r\n",
                 unsigned(s.vehicle), unsigned(s.seat), unsigned(s.entry), s.plays);
        SeatGo(s, SeatPhase::seated, now);
        break;
    }
    case SeatPhase::seated: {
        std::uint32_t err_mm = UINT32_MAX;
        const bool playing = body_dm && dm.rec_ok && (dm.rec248 & 0x10);
        // Seat by id: once the DM is live on body 1 with a seated state of this
        // seat (q18[1] == 0x8B, rec+0x18; critic #17), the record is targeted
        // before the seat can be confirmed (the confirmation waits for it).
        if (s.byid && !s.manual && !s.target_done && body_dm && dm.rec_ok && prs::IsSeatState(dm.rec18, s.seat)) {
            if (const char* target_why = SeatTarget(d, h, now)) {
                SeatRefuse(m, h, target_why, now);
                // 0.13.6: where only the id can seat (Mother Base), a copy that vanished or
                // unrealized since the offer is not latched: the next episode shows body 1
                // without a seat (fallback) instead of hiding it. The Africa fix: in free roam too
                // for a copy that vanished (absent; SeatFallbackHere).
                if (SeatFallbackHere(m.mission_code, target_why)) s.latch_exit = false;
                break;
            }
        }
        if (playing && (s.manual || prs::IsSeatState(dm.rec18, s.seat)) && (!s.byid || s.manual || s.target_done)) {
            if (!s.confirmed) {
                s.confirmed = true;
                s.confirm_ms = now;
                s.transient_refusals = 0;  // the temporary refusals before it were temporary
                s.loop_confirmed = dm.rec1a0 == s.loop_entry;
                EmitSeat(m, h, "partner_ride_seat=confirmed; vehicle=%u; seat=%u; state=0x%llx; entry=%u; ms=%u; plays=%u; manual=%u\r\n",
                         unsigned(s.vehicle), unsigned(s.seat), static_cast<unsigned long long>(dm.rec18 & 0xFFFFFFFFFFFFull),
                         unsigned(dm.rec1a0), Clamp32(now - s.issue_ms), s.plays, s.manual ? 1u : 0u);
                SeatDmProbe(m, h, dm_ok, dm, "confirmed");
            } else if (!s.loop_confirmed && (s.entry & partner::seat_entry::get_on) && dm.rec1a0 == s.loop_entry) {
                s.loop_confirmed = true;
                SeatDmProbe(m, h, dm_ok, dm, "loop");
            }
        } else if (playing && now - s.issue_ms >= kSeatConfirmMs) {
            SeatDmProbe(m, h, dm_ok, dm, "foreign");
            SeatRefuse(m, h, "foreign_dm", now);
            break;
        } else if (s.confirmed && !body_dm) {
            // The seat DM ended under us: re-issued (the dwords stay armed).
            EmitSeat(m, h, "partner_ride_seat=lost; vehicle=%u; seat=%u; played_ms=%u; plays=%u\r\n", unsigned(s.vehicle), unsigned(s.seat),
                     Clamp32(now - s.confirm_ms), s.plays);
            s.confirmed = s.loop_confirmed = false;
            s.entry = static_cast<std::uint8_t>(s.entry & ~partner::seat_entry::get_on);
            s.loop_entry = 0;
            if (s.manual) { SeatExit(m, h, "manual_ended", false, now); break; }
            if (s.plays >= kSeatPlays) { SeatRefuse(m, h, "lost", now); break; }
            SeatGo(s, SeatPhase::arm, now);
            break;
        } else if (!s.confirmed && now - s.issue_ms >= kSeatConfirmMs) {
            SeatDmProbe(m, h, dm_ok, dm, "unconfirmed");
            if (dm_ok && (dm.g388 & 4) && h.dm_clear_pending && h.dm_clear_pending(h.context))
                EmitSeat(m, h, "partner_ride_seat=unconsumed; vehicle=%u; seat=%u; ms=%u\r\n", unsigned(s.vehicle), unsigned(s.seat),
                         Clamp32(now - s.issue_ms));
            if (s.manual) { SeatExit(m, h, "manual_unconfirmed", false, now); break; }
            if (s.plays >= kSeatPlays || body_dm) { SeatRefuse(m, h, "unconfirmed", now); break; }
            SeatGo(s, SeatPhase::arm, now);
            break;
        }
        // Sanity: the anchored body far from the remote's streamed seated pose.
        float body_pos[4]{}, body_quat[4]{};
        if (s.confirmed && in.pose_valid && h.read_body && h.read_body(h.context, st, body_pos, body_quat)) {
            err_mm = partner::DistanceMm(in.pose.x_mm, in.pose.y_mm, in.pose.z_mm, MetresToMm(body_pos[0]), MetresToMm(body_pos[1]),
                                         MetresToMm(body_pos[2]));
            // F1 vehicles (passenger mode): no far latch for the vehicle the viewer occupies (it
            // drives it, or rides that copy as a passenger): the peer's stream lags the real vehicle.
            const bool far_exempt =
                partner_board::FarExempt(BoardMode(d.settings.ride_board), s.vehicle, d.local_vehicle, d.local_passenger);
            if (!s.manual && !far_exempt && err_mm > kSeatFarMm) {
                if (!s.far_since_ms) s.far_since_ms = now ? now : 1;
                if (now - s.far_since_ms >= kSeatFarMs) {
                    EmitCapped(m.ride_vis_cap, h, "partner_ride", "partner_ride=seat_far; vehicle=%u; seat=%u; err_mm=%u; ms=%u\r\n",
                               unsigned(s.vehicle), unsigned(s.seat), err_mm, Clamp32(now - s.far_since_ms));
                    SeatRefuse(m, h, "far", now);
                    break;
                }
            } else {
                s.far_since_ms = 0;
            }
        }
        // Prevention (0.13.6): the realize policy 0x121B5E0 reads body 1's slot
        // (*(S+0x20))[1] x/z (0x14121B8B7..0x14121B8CC) for its loaded-area test
        // (0x14121B92C), and the seated DM moves only the body: the slot follows
        // the seated body at most once a second (a slot write does not move the
        // body: anim-appearance-findings-2026-10-04 apply=slot).
        if (!s.manual && h.write_slot && h.read_body && st.slots_ok &&
            (!s.slot_check_ms || now - s.slot_check_ms >= kSeatSlotFollowMs)) {
            s.slot_check_ms = now ? now : 1;
            float pos[4]{}, quat[4]{};
            if (h.read_body(h.context, st, pos, quat) && std::isfinite(pos[0]) && std::isfinite(pos[1]) && std::isfinite(pos[2])) {
                const std::uint32_t drift = partner::DistanceMm(MetresToMm(st.slot[1][0]), MetresToMm(st.slot[1][1]), MetresToMm(st.slot[1][2]),
                                                                MetresToMm(pos[0]), MetresToMm(pos[1]), MetresToMm(pos[2]));
                if (drift > kSeatSlotFollowMm) {
                    const float xyzw[4] = {pos[0], pos[1], pos[2], partner::QuatToYaw(quat)};
                    if (const char* refused = h.write_slot(h.context, in.terminating, xyzw)) {
                        LogWriteRefusal(m, h, "seat_slot", refused);
                    } else if (++s.slot_follows == 1) {
                        EmitSeat(m, h, "partner_ride_seat=slot_follow; vehicle=%u; seat=%u; drift_mm=%u; body_mm=%d,%d,%d\r\n",
                                 unsigned(s.vehicle), unsigned(s.seat), drift, MetresToMm(pos[0]), MetresToMm(pos[1]), MetresToMm(pos[2]));
                    }
                }
            }
        }
        SeatStatus(m, h, st, in, dm_ok, dm, err_mm);
        break;
    }
    default:
        break;
    }
    if (s.phase == SeatPhase::exit && (!in.realized || s.exit_quiet)) {
        // 0.13.6: body 1 is gone (the engine unrealized it; its DM ended with it,
        // q18[1] only still reads 0x8B) or the fallback played nothing on it: no
        // stop. Three stops sent to an unrealized body were never consumed and
        // left g+0x388 bit 8 set for ~3 min (2026-10-22), refusing every later
        // seat as pending. Settle reverts at once and re-shows the body.
        SeatDmProbe(m, h, dm_ok, dm, s.exit_quiet ? "exit_quiet" : "exit_gone");
        s.body_gone = !in.realized;
        SeatGo(s, SeatPhase::settle, now);
    }
    if (s.phase == SeatPhase::exit) {
        if (!s.issue_ms) SeatDmProbe(m, h, dm_ok, dm, "exit");
        if (s.next_sent && !s.getoff_confirmed && body_dm && dm.rec_ok && dm.rec1a0 == s.loop_entry + 1) {
            s.getoff_confirmed = true;
            SeatDmProbe(m, h, dm_ok, dm, "getoff");
        }
        if (!body_dm) {
            // Nothing (more) plays on body 1: a request still pending is ours.
            if (dm_ok && (dm.g388 & 4) && h.dm_clear_pending) h.dm_clear_pending(h.context);
            SeatGo(s, SeatPhase::settle, now);
        } else if (s.manual && !s.issue_ms) {
            s.issue_ms = now;  // Lua stops the manual DM itself; native backs it up after kSeatStopMs.
        } else if (!host_dm && !s.issue_ms) {
            const bool getoff = set.seat_exit == PartnerRideSeatExit::getoff && (s.entry & partner::seat_entry::get_off) && s.confirmed &&
                                dm.rec1a0 == s.loop_entry && s.exit_reason &&
                                (std::strcmp(s.exit_reason, "getoff_act") == 0 || std::strcmp(s.exit_reason, "foot") == 0);
            if (getoff) {
                out.ops |= partner::op::ride_seat;
                out.vehicle = s.vehicle;
                out.seat = s.seat;
                out.action = partner::seat_action::next;
                out.entry = s.entry;
                s.next_sent = true;
                s.req_bits |= pp::kDmBitNext;
            } else {
                out.ops |= partner::op::dm_stop;
                ++s.stops;
                s.req_bits |= pp::kDmBitStop;
            }
            s.issue_ms = now;
            EmitSeat(m, h, "partner_ride_seat=exit_request; vehicle=%u; seat=%u; reason=%s; via=%s\r\n", unsigned(s.vehicle),
                     unsigned(s.seat), s.exit_reason ? s.exit_reason : "unknown", getoff ? "getoff" : "stop");
        } else if (!host_dm && s.issue_ms && now - s.issue_ms >= (s.next_sent ? kSeatNextMs : kSeatStopMs) && s.stops < 3) {
            // The GetOff advance or a stop did not end it: stop (again).
            out.ops |= partner::op::dm_stop;
            ++s.stops;
            s.req_bits |= pp::kDmBitStop;
            s.next_sent = false;
            s.issue_ms = now;
        }
        if (s.phase == SeatPhase::exit && now - s.phase_ms >= kSeatExitMs + kSeatNextMs) {
            SeatDmProbe(m, h, dm_ok, dm, "exit_timeout");
            s.exit_reason = "exit_timeout";
            SeatGo(s, SeatPhase::settle, now);
        }
    }
    if (s.phase == SeatPhase::settle) {
        // One reply never carries both 256 and 512 (the settle tick sends nothing).
        out.ops = 0;
        SeatSettle(d, h, now, in.terminating);
    }
    return out;
}
