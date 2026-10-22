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

namespace prs = mgscoop::partner_ride_seat;

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
    const rw::Packet& p = d.ride_rx.packet;
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
    const partner_ride_wire::Packet& p = d.ride_rx.packet;
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
    SeatExit(m, h, reason, !s.manual, now);
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
    EmitSeat(m, h, "partner_ride_seat=exit; vehicle=%u; seat=%u; reason=%s; via=%s; confirmed=%u; plays=%u; stops=%u; ms=%u; manual=%u\r\n",
             unsigned(s.vehicle), unsigned(s.seat), reason, s.next_sent ? "getoff" : "stop", s.confirmed ? 1u : 0u, s.plays, s.stops,
             Clamp32(now - s.phase_ms), s.manual ? 1u : 0u);
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
    s = Mission::Seat{};
    s.blocked = blocked; s.block_vehicle = bv; s.block_seat = bs; s.block_reason = br;
    s.vehicle_since_ms = since; s.vehicle_rev = rev; s.vehicle_live = live; s.seat_live = slive;
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
const char* SeatByIdGate(Driver& d, const Hooks& h, std::uint64_t now) noexcept {
    namespace rw = partner_ride_wire;
    auto& s = d.mission.seat;
    s.remote_category = s.local_category = rw::kCategoryUnknown;
    if (!h.vehicle_read || !h.seat_target) return "unavailable";
    const std::uint32_t epoch = h.link_session ? h.link_session(h.context) : 0;
    if (d.ride_rx.packet.kind != rw::Kind::vehicle || d.ride_rx.packet.vehicle != s.vehicle || d.ride_rx.packet.seat != s.seat)
        return "vehicle_change";
    if (!rw::ExtLive(d.ride_ext, epoch, now)) return "no_ext";
    s.remote_category = rw::RemoteCategory(d.ride_ext, d.ride_rx.packet, epoch, now);
    if (s.remote_category == rw::kCategoryUnknown) return "remote_category";
    // F1 vehicles (partner_ride_board=passenger): the peer in a back seat of the vehicle the
    // viewer drives is seated there (never rider[]); any other seat of it stays refused.
    if (s.vehicle == d.local_vehicle && !partner_board::LocalVehicleSeatAllowed(BoardMode(d.settings.ride_board), s.seat))
        return "local_vehicle";
    // The viewer's own back seat (Lua's shared_seat guard needs ON_VEHICLE and cannot see a passenger).
    if (partner_board::SharedWithLocal(d.local_passenger, s.vehicle, s.seat)) return "shared_seat";
    partner_vehicle::Read r{};
    h.vehicle_read(h.context, s.vehicle, r);
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
        if (!SeatMission(m.mission_code) && !s.byid) SeatRefuse(m, h, s.byid_reason ? s.byid_reason : "byid", now);
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
            if (now - s.phase_ms >= kSeatExitMs)
                SeatRefuse(m, h, host_dm ? "host_dm" : body_dm ? "body_dm" : "pending", now);
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
            if (!h.ride_seat_arm(h.context, s.seat)) {
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
                break;
            }
        }
        if (playing && (s.manual || prs::IsSeatState(dm.rec18, s.seat)) && (!s.byid || s.manual || s.target_done)) {
            if (!s.confirmed) {
                s.confirmed = true;
                s.confirm_ms = now;
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
        SeatStatus(m, h, st, in, dm_ok, dm, err_mm);
        break;
    }
    default:
        break;
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
            } else {
                out.ops |= partner::op::dm_stop;
                ++s.stops;
            }
            s.issue_ms = now;
            EmitSeat(m, h, "partner_ride_seat=exit_request; vehicle=%u; seat=%u; reason=%s; via=%s\r\n", unsigned(s.vehicle),
                     unsigned(s.seat), s.exit_reason ? s.exit_reason : "unknown", getoff ? "getoff" : "stop");
        } else if (!host_dm && s.issue_ms && now - s.issue_ms >= (s.next_sent ? kSeatNextMs : kSeatStopMs) && s.stops < 3) {
            // The GetOff advance or a stop did not end it: stop (again).
            out.ops |= partner::op::dm_stop;
            ++s.stops;
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
