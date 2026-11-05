-- Horse plan M3, ride behind your partner (lua/coop/world_horse_pillion.lua on world_horse_share.lua and world_ride_pose.lua).
-- No game process. Usage: lua51 world_horse_pillion_lua_check.lua <world_ride_pose.lua> <world_horse_share.lua> <world_horse_pillion.lua>
local checks = 0
local function check(value, why)
    checks = checks + 1
    if not value then error(checks .. ': ' .. why, 2) end
end
local HORSE, OP = 6656, 80
local function fixture(o)
    o = o or {}
    local f = {now = 1000, tx = {}, logs = {}, places = {}, masks = {}, warps = {}, holds = {}, seats = {}, stances = {}, rides = false,
        key = o.key ~= false, pillion = o.pillion ~= false, gen = 5, e = false, padOk = true, seatOk = true, placeOk = true, masked = false, mag = 0,
        horse = {x = 10000, y = 2000, z = 30000, yaw = 0, occupied = false}}
    local W = {OP = {}, handlers = {}, stopHooks = {}, pairedHooks = {}, startHooks = {}, unpairedHooks = {}, paired = true, eff = 3,
        mission = 30010, epoch = 7, M = {GameObject = 1}, kind = {[701] = "Unconscious", [702] = "Damage", [703] = "Conscious"},
        host = o.host ~= false, role = o.host == false and 2 or 1, active = true, downed = {records = {}}}
    W.get = function(t, k) if t == nil then return nil end return t[k] end
    W.now = function() return f.now end
    W.tick32 = function(t) return math.floor(t) % 4294967296 end
    W.tx = function(...) f.tx[#f.tx + 1] = {...}; return true end
    W.log = function(s) f.logs[#f.logs + 1] = s end
    W.tick = function() end
    W.travel = {blocked = function() return nil end}
    W.observe = function() f.observed = (f.observed or 0) + 1 end
    W.receipt = function(text)
        local verb = text:match("MGS_COOP_HORSE_DRIVE_V1' %-%- (.*)$")
        if not verb then return nil end
        if verb == "cfg" then return {on = f.key and 1 or 0, cargo = 0, pillion = f.pillion and 1 or 0} end
        local h = f.horse
        if verb:find("^read ") then
            return {ready = true, id = HORSE, realized = true, occupied = h.occupied, rider = 0, pending = 0, x = h.x, y = h.y, z = h.z, yaw = h.yaw, gen = f.gen,
                life = 1000, maxlife = 1000}
        end
        if verb:find("^pad ") then
            if not f.padOk then return {ready = false, reason = "off"} end
            return {ready = true, e = f.e and 1 or 0, masked = f.masked and 1 or 0, held = f.e and 0x20 or 0, bm = f.masked and 0x20 or 0,
                mag = f.mag}
        end
        local sb, su, sp = verb:match("^seat1 30010 7 (%d+) (%-?%d+) (%d)$")
        if sb then
            f.seats[#f.seats + 1] = {tonumber(sb), tonumber(su), tonumber(sp)}
            if not f.seatOk then return {ready = false, reason = "not_riding"} end
            return {ready = true, seat = 1}
        end
        local hx, hy, hz, hyaw, hr = verb:match("^hold0 30010 7 (%-?%d+) (%-?%d+) (%-?%d+) (%-?%d+) (%d+)$")
        if hx then
            f.holds[#f.holds + 1] = {tonumber(hx), tonumber(hy), tonumber(hz), tonumber(hyaw), tonumber(hr)}
            return {ready = true, held = true}
        end
        local m, e, x, y, z, yaw, a = verb:match("^place (%d+) (%d+) (%-?%d+) (%-?%d+) (%-?%d+) (%-?%d+) (%d)$")
        if m then
            if not f.placeOk then return {ready = false, reason = "pending"} end
            f.places[#f.places + 1] = {x = tonumber(x), y = tonumber(y), z = tonumber(z), yaw = tonumber(yaw)}
            return {ready = true, placed = true, anchored = tonumber(a), bx = h.x, by = h.y, bz = h.z, byaw = h.yaw, x = tonumber(x),
                y = tonumber(y), z = tonumber(z), gen = f.gen}
        end
    end
    local env = setmetatable({e = {MgsCoopWorld = W}, vars = {playerPosX = 11, playerPosY = 1.1, playerPosZ = 30},
        PlayerInfo = {OrCheckStatus = function(s) return s[1] == 25 and f.rides end}, PlayerStatus = {ON_HORSE = 25},
        PlayerPad = {ACTION = 0x20, STICK_L = 1}, PlayerStance = {SQUAT = 2, STAND = 0},
        Player = {SetPadMask = function(q) f.masks[#f.masks + 1] = {"set", q}; return true end,
            ResetPadMask = function(q) f.masks[#f.masks + 1] = {"reset", q}; return true end,
            RequestToSetTargetStance = function(s) f.stances[#f.stances + 1] = s end},
        GameObject = {SendCommand = function(who, c) if c.id == "Warp" then f.warps[#f.warps + 1] = {who.index, c.pos, c.rotY} end end}},
        {__index = _G})
    for i = 1, 3 do
        local chunk = assert(loadfile(assert(arg[i])))
        setfenv(chunk, env)
        chunk()
    end
    f.W, f.env, f.S, f.P = W, env, W.horseShare, W.horsePillion
    function f.tick(dt) f.now = f.now + (dt or 33); W.tick() end
    function f.fast(t, x, y, z, yaw) W.handlers[44]({t, 3, HORSE, x, y, z, yaw}) end
    function f.final(x, y, z, yaw) W.handlers[42]({9, 1, HORSE, x, y, z, yaw, 0, 65534}) end
    function f.logged(s) for _, l in ipairs(f.logs) do if l:find(s, 1, true) then return true end end return false end
    function f.count(s) local n = 0; for _, l in ipairs(f.logs) do if l:find(s, 1, true) then n = n + 1 end end return n end
    -- The partner rides here: a live track placing this copy's horse every frame.
    f.t = 5000
    function f.ride(n)
        for _ = 1, n or 1 do f.t = f.t + 50; f.fast(f.t, f.horse.x, f.horse.y, f.horse.z, f.horse.yaw); f.tick(50) end
    end
    -- The HORSE_SEAT messages this copy sent since index i.
    function f.seatTx(i)
        local out = {}
        for k = (i or 1), #f.tx do if f.tx[k][1] == OP then out[#out + 1] = f.tx[k] end end
        return out
    end
    return f
end
-- Deliver a sent HORSE_SEAT message (W.tx(OP, seq, seat, role, action, cause, token, x, y, z, yaw)) to the other copy.
local function deliver(to, t) to.W.handlers[OP]({t[2], t[3], t[4], t[5], t[6], t[7], t[8], t[9], t[10], t[11]}) end
local function lastSeat(f) local s = f.seatTx(); return s[#s] end

-- Binds after world_horse_share.lua; HORSE_SEAT seat 1 on op 0x50.
local g = fixture({host = false})
check(type(g.P) == "table" and g.W.OP.HORSE_SEAT == OP, "W.horsePillion on HORSE_SEAT 0x50")
check(g.P.keyOn() and g.P.on(), "both keys on, paired, free roam: on")
local off = fixture({host = false, pillion = false})
check(not off.P.on(), "world_horse_pillion off: off")

-- (a) The host drives, the guest claims the rear seat with E beside the ridden horse (the raw pad read before the masks).
local h = fixture()
g = fixture({host = false})
g.e = true
g.ride(4)
check(g.S.track ~= nil and #g.places > 0, "the guest's copy places the host's ride")
g.ride(2)
check(#g.seatTx() == 0, "an E held on entering the zone is no press")
g.e = false
g.ride(1)
g.e = true
g.ride(1)
local claim = lastSeat(g)
check(claim and claim[3] == 1 and claim[4] == 2 and claim[5] == 1 and claim[7] == 1, "E edge within 2.5 m: CLAIM seat 1, role 2, token 1")
check(g.P.state == "claim" and g.logged("event=horse_pillion;step=claim;seat=1;"), "step=claim")
-- Off: no claim; far: no claim.
off = fixture({host = false, pillion = false})
off.ride(2); off.e = true; off.ride(1); off.e = false; off.ride(1); off.e = true; off.ride(1)
check(#off.seatTx() == 0, "key off: E claims nothing")
local far = fixture({host = false})
far.env.vars.playerPosX = 13.1
far.ride(2); far.e = false; far.ride(1); far.e = true; far.ride(1)
check(#far.seatTx() == 0 and far.P.state == "off", "3 m from the ridden horse: E is not a claim (it acts normally)")
-- M3c: the E there is read from the raw pad verb (not the downed poll, which is not polled with nobody downed).
check(far.logged("event=horse_pillion;step=e_free;dist_cm=310;hold=0;masked=0"), "an unmasked E 3.1 m from the ridden horse: step=e_free")
far.e = false; far.ride(1); far.masked = true; far.e = true; far.ride(1)
check(far.logged("step=e_free;dist_cm=310;hold=0;masked=1"), "the pad's ACTION mask bit is logged with it")
local far6 = fixture({host = false})
far6.env.vars.playerPosX = 16.5
far6.ride(2); far6.e = false; far6.ride(1); far6.e = true; far6.ride(1)
check(not far6.logged("step=e_free"), "past 6 m: no e_free line")
-- The driver's ride while a passenger sits behind: the ridden path every 2 s (runs 306525db / c4428efa: a horse that does not move).
h2 = fixture()
h2.rides, h2.horse.occupied = true, true
h2.W.handlers[OP]({1, 1, 2, 1, 0, 4, 0, 0, 0, 0})
h2.mag = 600
for _ = 1, 50 do h2.horse.x = h2.horse.x + 100; h2.tick(50) end
check(h2.logged("event=horse_pillion;step=drive;moved_cm="), "step=drive: the driver's ridden path every 2 s")
-- M3c (run 7c0b9e71: the 'stall' was the scenario's first ride key 17 s after the stray claim, then a cliff): the census
-- rider index and the pad words with every drive line, the time from the first ride key to 0.5 m, a stall line.
check(h2.logged(";rider=0;occ=1;") and h2.logged(";mag=600"), "step=drive carries the census rider / occupied and the pad words")
check(h2.logged("event=horse_pillion;step=first_move;ms="), "step=first_move: the first ride key to 0.5 m of path")
check(not h2.logged("step=stall"), "a moving horse: no stall line")
local h4 = fixture()
h4.rides, h4.horse.occupied = true, true
h4.W.handlers[OP]({1, 1, 2, 1, 0, 4, 0, 0, 0, 0})
h4.mag = 600
for _ = 1, 30 do h4.tick(50) end
check(not h4.logged("step=stall"), "1.5 s of ride keys without moving: not yet a stall")
for _ = 1, 20 do h4.tick(50) end
check(h4.logged("event=horse_pillion;step=stall;ms=") and h4.logged(";rider=0;occ=1;"), "ride keys 2 s and the horse under 0.5 m: step=stall")
local h5 = fixture()
h5.rides, h5.horse.occupied = true, true
h5.W.handlers[OP]({1, 1, 2, 1, 0, 4, 0, 0, 0, 0})
for _ = 1, 60 do h5.tick(50) end
check(not h5.logged("step=stall") and not h5.logged("step=first_move"), "no ride keys: neither line")
-- The host grants while it rides the shared horse and drives with the guest behind.
h.rides, h.horse.occupied = true, true
deliver(h, claim)
local grant = lastSeat(h)
check(grant and grant[5] == 2 and grant[6] == 0 and grant[7] == 1 and h.P.state == "driver", "the host grants (cause 0) and drives")
check(h.logged("event=horse_pillion;step=grant;seat=1;token=1") and h.logged("step=driver;seat=1;how=grant"), "step=grant, step=driver")
h.tick(33)
-- Run ba6b4bb5 (lab): body 1 at the driver's own root height was drawn on the ground beside the horse (pushed out of the
-- horse) and the host's horse stood still under the ride keys until the passenger left: 0.3 m over the rider's root.
-- Runs 306525db / c4428efa (attempt 2): with body 1 0.3 m over the driver's root the driver's horse still stood still
-- (census static 12-17 s under the ride keys) and the passenger's copy's horse fell unconscious: the bodies go clear of the
-- horse (body 1 0.7 m over the driver's root, body 0 1.2 m over the placed root) to test the collision.
-- Runs bdd49a57 / a295d660 (attempt 3): clear of the horse the driver's horse still stood still (host driving) and the
-- passenger's copy's horse still fell: not the collision; back to 0.3 m / 0.8 m (the better look).
check(#h.seats >= 1 and h.seats[#h.seats][1] == 450 and h.seats[#h.seats][2] == 300 and h.seats[#h.seats][3] == 1,
    "the driver leases seat1 450 300 1 every tick (the overlay seat on)")
check(h.logged("step=seat1;ok=1;back_mm=450;up_mm=300;pose=1"), "step=seat1 ok")
deliver(g, grant)
check(g.P.state == "seated" and g.logged("step=seated;seat=1;how=grant;token=1"), "the guest is seated")
local mask = g.masks[#g.masks]
check(mask[1] == "set" and mask[2].settingName == "MgsCoopPillion" and mask[2].buttons == 0xFFFFFFA7 and mask[2].sticks == 1,
    "the pillion pad mask: everything but aim, fire and reload; the move stick; the camera free")
check(g.stances[#g.stances] == 2, "the passenger squats")
g.ride(1)
local hold = g.holds[#g.holds]
local pl = g.places[#g.places]
-- Run ba6b4bb5: body 0 0.6 m over the root stood on the ground 1-1.8 m under its target (seat_pin dy_cm 99-176): 0.8 m.
-- M3c: BODY0's mechanism by default (run 7c0b9e71: seat_pin lag 0 / dy 0, squatting behind the driver on their own screen).
check(hold and hold[1] == pl.x and hold[2] == pl.y + 800 and hold[3] == pl.z - 350 and hold[5] == 14,
    "body 0 every frame on the rear seat of the placed horse (0.35 m behind the root, 0.8 m up), hold0 + C0 + watch + pin (14)")
-- A refused placement (no P.placed call) still holds body 0 at its last target: no drop onto the horse.
g.placeOk = false
local nk = #g.holds
g.ride(4)
check(#g.holds >= nk + 2 and g.holds[#g.holds][1] == hold[1] and g.holds[#g.holds][2] == hold[2] and g.holds[#g.holds][5] == 14,
    "placements refused: the last seat target held again (keepalive)")
check(g.logged("step=hold_keep;"), "step=hold_keep once")
g.placeOk = true
check(g.P.busy() and g.S.busy(), "seated: busy (Travel / Bring wait)")
-- KEEP both ways at 1 Hz.
local n0 = #g.tx
g.ride(25)
local keeps = 0
for _, t in ipairs(g.seatTx(n0 + 1)) do if t[5] == 6 then keeps = keeps + 1 end end
check(keeps >= 1, "the passenger sends KEEP at 1 Hz")
for _ = 1, 25 do h.tick(50) end
for _, t in ipairs(h.seatTx()) do if t[5] == 6 then deliver(g, t) end end
for _, t in ipairs(g.seatTx()) do if t[5] == 6 then deliver(h, t) end end
check(h.P.state == "driver" and g.P.state == "seated", "KEEP holds both sides")
-- The lab lever's seat flag (pillion_pose ... 0): the passenger's KEEP asks the driver's copy for no overlay seat.
check(g.P.setPose(350, 800, 2, 450, 300, 0), "setPose with the seat flag")
g.ride(25)
for _, t in ipairs(g.seatTx()) do if t[5] == 6 then deliver(h, t) end end
h.tick(33)
check(h.seats[#h.seats][3] == 0 and h.logged("pose=0"), "the driver's lease follows the passenger's seat flag")
g.P.setPose(350, 800, 2, 450, 300, 1)
g.ride(25)
for _, t in ipairs(g.seatTx()) do if t[5] == 6 then deliver(h, t) end end
h.tick(33)
check(h.seats[#h.seats][3] == 1, "and back")
-- The claim carries the flag too: the first lease already follows it.
local hc, gc = fixture(), fixture({host = false})
hc.rides, hc.horse.occupied = true, true
gc.P.setPose(350, 800, 2, 450, 300, 0)
gc.ride(3); gc.e = false; gc.ride(1); gc.e = true; gc.ride(1)
deliver(hc, lastSeat(gc))
hc.tick(33)
check(hc.P.state == "driver" and hc.seats[1][3] == 0 and hc.logged("pose=0"), "a claim without the overlay seat: the first lease has pose 0")
g.ride(10)
check(g.logged("step=seat_pin;"), "step=seat_pin every second")
check(g.logged(";occ=0;life=1000;max=1000"), "seat_pin carries this copy's horse census (occupied, life)")
-- The passenger's copy's horse hit / knocked down: one line with the message, its arguments and the horse's life.
g.W.observe(1, 701, HORSE, 0, 0, 0)
check(g.logged("event=horse_pillion;step=horse_hit;m=Unconscious;") and g.logged("state=seated;") and g.observed == 1,
    "W.observe wrapped: a 6656 Unconscious is logged with the seat state (and the original still runs)")
g.W.observe(1, 702, 1234, 0, 0, 0)
check(g.count("step=horse_hit") == 1, "other ids are not")
check(g.logged("event=horse_pillion;step=pad;e=1;"), "the raw E's changes are logged (capped)")
-- Hop off: E held 0.3 s again, after a release (run ba6b4bb5: a stray raw E for a frame ended a ride at a gallop).
g.e = false
g.ride(1)
g.e = true
g.ride(1)
check(g.P.state == "seated", "a one-frame E does not hop off")
g.e = false
g.ride(1)
g.e = true
g.ride(8)
check(g.P.state == "off" and g.logged("step=unseated;seat=1;why=passenger;side=passenger;placed=1"), "E held 0.3 s again: hop off")
local w = g.warps[#g.warps]
check(w and w[1] == 0 and math.abs(w[2][1] * 1000 - (pl.x - 1300)) < 2, "laid on the horse's left (yaw 0: -x), body 0 warped once")
check(g.masks[#g.masks][1] == "reset" and g.masks[#g.masks][2].settingName == "MgsCoopPillion" and g.stances[#g.stances] == 0,
    "the pad mask released, standing")
local un = lastSeat(g)
check(un[5] == 5 and un[6] == 1, "UNSEATED cause passenger")
deliver(h, un)
check(h.P.state == "off" and h.logged("step=unseated;seat=1;why=passenger;side=driver;"), "the driver's side ends")
local leases = #h.seats
h.tick(33)
check(#h.seats == leases, "no seat1 after the end")

-- (b) Both off when the driver gets off.
local function seatBoth(hh, gg)
    gg.ride(1); gg.e = false; gg.ride(1); gg.e = true; gg.ride(1)
    deliver(hh, lastSeat(gg))
    deliver(gg, lastSeat(hh))
    gg.ride(1)
end
h, g = fixture(), fixture({host = false})
h.rides, h.horse.occupied = true, true
g.ride(3)
seatBoth(h, g)
check(h.P.state == "driver" and g.P.state == "seated", "seated again")
h.rides = false
h.tick(33)
check(h.P.state == "off" and h.logged("step=unseated;seat=1;why=driver_off;side=driver"), "the driver gets off: driver_off")
deliver(g, lastSeat(h))
check(g.P.state == "off" and g.logged("step=unseated;seat=1;why=driver_off;side=passenger;placed=1;"), "the passenger is laid off too")
-- The ride's end on the passenger's copy (the final pose) lays them off as well.
h, g = fixture(), fixture({host = false})
h.rides, h.horse.occupied = true, true
g.ride(3)
seatBoth(h, g)
g.final(g.horse.x, g.horse.y, g.horse.z, 0)
g.tick(33)
check(g.P.state == "off" and g.logged("why=track_end;side=passenger;placed=1"), "the track's end: laid off")
-- Link drop: no KEEP for 3.5 s on either side.
h, g = fixture(), fixture({host = false})
h.rides, h.horse.occupied = true, true
g.ride(3)
seatBoth(h, g)
g.ride(75)
check(g.P.state == "off" and g.logged("step=unseated;seat=1;why=stream_lost;side=passenger;placed=1"), "no KEEP 3.5 s: the passenger is laid off")
for _ = 1, 75 do h.tick(50) end
check(h.P.state == "off" and h.logged("why=stream_lost;side=driver"), "no KEEP 3.5 s: the driver ends too")
-- Downed while seated: off.
h, g = fixture(), fixture({host = false})
h.rides, h.horse.occupied = true, true
g.ride(3)
seatBoth(h, g)
g.W.downed.records[2] = {down = true}
g.ride(1)
check(g.P.state == "off" and g.logged("why=passenger_downed"), "a downed passenger is laid off")
-- Context change (mission end): off.
h, g = fixture(), fixture({host = false})
h.rides, h.horse.occupied = true, true
g.ride(3)
seatBoth(h, g)
g.W.epoch = 8
g.tick(33)
check(g.P.state == "off" and g.logged("why=context") and g.masks[#g.masks][1] == "reset", "a context change lays them off, the mask released")
-- Unpaired: off.
h, g = fixture(), fixture({host = false})
h.rides, h.horse.occupied = true, true
g.ride(3)
seatBoth(h, g)
g.W.paired = false
g.tick(33)
check(g.P.state == "off" and g.masks[#g.masks][1] == "reset", "unpaired: off, the mask released")

-- The host denies a claim while it does not ride.
h, g = fixture(), fixture({host = false})
g.ride(2); g.e = false; g.ride(1); g.e = true; g.ride(1)
deliver(h, lastSeat(g))
local deny = lastSeat(h)
check(deny[5] == 3 and deny[6] == 7 and h.P.state == "off" and h.logged("step=deny;seat=1;why=not_riding"), "not riding: DENY not_riding")
deliver(g, deny)
check(g.P.state == "off" and g.logged("step=denied;seat=1;why=not_riding"), "the guest's claim ends")
-- A claim without an answer times out.
g = fixture({host = false})
g.ride(2); g.e = false; g.ride(1); g.e = true; g.ride(1)
for _ = 1, 50 do g.ride(1) end
check(g.P.state == "off" and g.logged("why=claim_timeout"), "claim_timeout")

-- (c) The guest drives, the host's own press commits (GRANT cause passenger) under the press lock.
h, g = fixture(), fixture({host = false})
g.rides, g.horse.occupied = true, true
h.ride(2); h.e = false; h.ride(1); h.e = true; h.ride(1)
local hg = lastSeat(h)
check(hg[5] == 2 and hg[6] == 1 and h.P.state == "seated" and h.logged("step=seated;seat=1;how=host_press"), "the host sits behind at once")
deliver(g, hg)
check(g.P.state == "driver" and g.logged("step=driver;seat=1;how=host_press"), "the guest drives with the host behind")
g.tick(33)
check(#g.seats >= 1, "the guest's copy leases seat1")
-- The press lock: a guest claim within 3 s of the host's own press is refused.
local h2 = fixture()
h2.rides, h2.horse.occupied = true, true
h2.P.lockUntil = h2.now + 3000
h2.W.handlers[OP]({1, 1, 2, 1, 0, 9, 0, 0, 0, 0})
check(lastSeat(h2)[5] == 3 and lastSeat(h2)[6] == 11, "a guest claim inside the host's press lock: DENY locked")
-- The guest is not riding when the host's press arrives: the host is laid off.
h, g = fixture(), fixture({host = false})
h.ride(2); h.e = false; h.ride(1); h.e = true; h.ride(1)
deliver(g, lastSeat(h))
local nr = lastSeat(g)
check(nr[5] == 5 and nr[6] == 7 and g.P.state == "off", "the guest not riding: UNSEATED not_riding")
deliver(h, nr)
check(h.P.state == "off" and h.logged("why=not_riding;side=passenger;placed=1;"), "the host is laid off")

-- M3c tie (both press E at an unridden horse at once): each copy rides its own horse and the partner's RIDE_FAST arrives
-- while this copy's player rides (no track opens): step=tie once per ride on both copies, the host drives.
local th, tg = fixture(), fixture({host = false})
th.rides, th.horse.occupied, tg.rides, tg.horse.occupied = true, true, true, true
for _ = 1, 3 do th.t = th.t + 50; th.fast(th.t, 1, 2, 3, 0); th.tick(50); tg.t = tg.t + 50; tg.fast(tg.t, 1, 2, 3, 0); tg.tick(50) end
check(th.count("event=horse_pillion;step=tie;role=host;verdict=drive") == 1, "tie on the host's copy: the host drives (once)")
check(tg.count("event=horse_pillion;step=tie;role=guest;verdict=yield;getoff=none") == 1,
    "tie on the guest's copy: the guest yields (no native get-off yet: logged)")
check(th.S.track == nil and tg.S.track == nil, "no track opens while both ride")
-- The pad read refused: no claim, one line.
g = fixture({host = false})
g.padOk = false
g.ride(3); g.e = true; g.ride(1)
check(#g.seatTx() == 0 and g.count("step=pad_refused;reason=off") == 1, "pad refused: no claim, logged once")
-- Seat 2 still reaches world_horse_cargo's handler.
g = fixture({host = false})
local got
g.W.handlers[OP] = nil
local base = function(v) got = v end
g.W.handlers[OP] = base
local chunk = assert(loadfile(arg[3]))
setfenv(chunk, g.env)
chunk()
g.W.handlers[OP]({1, 2, 1, 4, 0, 5, 1, 2, 3, 0})
check(got and got[2] == 2, "seat 2 goes on to world_horse_cargo")
-- The lab lever's pose bounds.
check(g.W.horsePillion.setPose(350, 600, 4, 450, 0) and g.W.horsePillion.setPose(350, 800, 5, 450, 300) and
    g.W.horsePillion.setPose(350, 600, 3, 450, 0) and not g.W.horsePillion.setPose(350, 600, 1, 450, 0) and
    not g.W.horsePillion.setPose(350, 600, 2, 1600, 0), "setPose: mode 0 / 2 / 3 / 4 / 5, bounds")
-- BODY0 round: mode 3, hold0 with the C0 reset alone (reset 2), as world_horse_cargo's body 0.
local m3h, m3g = fixture(), fixture({host = false})
m3h.rides, m3h.horse.occupied = true, true
m3g.ride(3); m3g.e = false; m3g.ride(1); m3g.e = true; m3g.ride(1)
deliver(m3h, lastSeat(m3g)); deliver(m3g, lastSeat(m3h))
m3g.P.setPose(350, 800, 3, 450, 300)
local n3 = #m3g.holds
m3g.ride(1)
check(#m3g.holds == n3 + 1 and m3g.holds[#m3g.holds][5] == 2, "mode 3: hold0 with the C0 reset (2)")
m3g.P.setPose(350, 800, 6, 450, 300)
m3g.ride(1)
check(m3g.holds[#m3g.holds][5] == 14, "mode 6: hold0 with the C0 reset, the write watch and the pin (14)")
-- Mode 5: hold0 with the A0 reset and the Lua Warp in the same frame.
local m5h, m5g = fixture(), fixture({host = false})
m5h.rides, m5h.horse.occupied = true, true
m5g.ride(3); m5g.e = false; m5g.ride(1); m5g.e = true; m5g.ride(1)
deliver(m5h, lastSeat(m5g)); deliver(m5g, lastSeat(m5h))
m5g.P.setPose(350, 800, 5, 450, 300)
local nh, nw = #m5g.holds, #m5g.warps
m5g.ride(1)
check(#m5g.holds == nh + 1 and #m5g.warps == nw + 1, "mode 5: hold0 and the Warp each frame")
check(g.W.horsePillion.status():find("pillion;state=off;key=1;", 1, true) ~= nil, "status line")
print("world_horse_pillion_lua_check: " .. checks .. " checks passed")
