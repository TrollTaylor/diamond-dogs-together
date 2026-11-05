-- Horse plan M1, the shared D-Horse (lua/coop/world_horse_share.lua on world_ride_pose.lua). No game process.
-- Usage: lua51 world_horse_share_lua_check.lua <world_ride_pose.lua> <world_horse_share.lua>
local checks = 0
local function check(value, why)
    checks = checks + 1
    if not value then error(checks .. ': ' .. why, 2) end
end
local HORSE = 6656
local function fixture(o)
    o = o or {}
    local f = {now = 1000, tx = {}, logs = {}, places = {}, masks = {}, rides = false, key = o.key ~= false, gen = 5,
        horse = {x = 10000, y = 2000, z = 30000, yaw = 100, occupied = false}, placeOk = true, travel = 0}
    local W = {OP = {}, handlers = {}, stopHooks = {}, pairedHooks = {}, startHooks = {}, unpairedHooks = {}, paired = true, eff = 3,
        mission = o.mission or 30010, epoch = 7, host = o.host ~= false, active = true}
    W.get = function(t, k) if t == nil then return nil end return t[k] end
    W.now = function() return f.now end
    W.tick32 = function(t) return math.floor(t) % 4294967296 end
    W.tx = function(...) f.tx[#f.tx + 1] = {...}; return true end
    W.log = function(s) f.logs[#f.logs + 1] = s end
    W.tick = function() end
    W.travel = {blocked = function() return nil end}
    W.receipt = function(text)
        local verb = text:match("MGS_COOP_HORSE_DRIVE_V1' %-%- (.*)$")
        if not verb then return nil end
        if verb == "cfg" then return {on = f.key and 1 or 0} end
        local h = f.horse
        if verb:find("^read ") then
            return {ready = true, id = HORSE, realized = true, occupied = h.occupied, rider = 0, pending = 0, x = h.x, y = h.y, z = h.z, yaw = h.yaw, gen = f.gen}
        end
        local m, e, x, y, z, yaw, a = verb:match("^place (%d+) (%d+) (%-?%d+) (%-?%d+) (%-?%d+) (%-?%d+) (%d)$")
        if m then
            f.places[#f.places + 1] = {x = tonumber(x), y = tonumber(y), z = tonumber(z), yaw = tonumber(yaw), anchor = tonumber(a)}
            if not f.placeOk then return {ready = false, reason = "pins"} end
            local r = {ready = true, placed = true, anchored = tonumber(a), bx = h.x, by = h.y, bz = h.z, byaw = h.yaw,
                x = tonumber(x), y = tonumber(y), z = tonumber(z), gen = f.gen}
            h.x, h.y, h.z, h.yaw = r.x, r.y + (f.up or 0), r.z, tonumber(yaw)
            return r
        end
    end
    local env = setmetatable({e = {MgsCoopWorld = W}, vars = {playerPosX = 100, playerPosY = 2, playerPosZ = 300},
        PlayerInfo = {OrCheckStatus = function(s) return s[1] == 25 and f.rides end}, PlayerStatus = {ON_HORSE = 25, ON_VEHICLE = 26},
        PlayerPad = {ACTION = 0x20},
        Player = {SetPadMask = function(q) f.masks[#f.masks + 1] = {"set", q}; return true end,
            ResetPadMask = function(q) f.masks[#f.masks + 1] = {"reset", q}; return true end},
        Tpp = {IsVehicle = function() return false end}}, {__index = _G})
    for i = 1, 2 do
        local chunk = assert(loadfile(assert(arg[i])))
        setfenv(chunk, env)
        chunk()
    end
    f.W, f.env, f.S = W, env, W.horseShare
    function f.tick(dt) f.now = f.now + (dt or 33); W.tick() end
    function f.fast(t, x, y, z, yaw) W.handlers[44]({t, 3, HORSE, x, y, z, yaw}) end
    function f.final(x, y, z, yaw) W.handlers[42]({9, 1, HORSE, x, y, z, yaw, 0, 65534}) end
    function f.logged(s) for _, l in ipairs(f.logs) do if l:find(s, 1, true) then return true end end return false end
    function f.count(s) local n = 0; for _, l in ipairs(f.logs) do if l:find(s, 1, true) then n = n + 1 end end return n end
    return f
end

-- The module binds next to world_ride_pose.lua.
local f = fixture()
check(type(f.S) == "table" and f.W.OP.RIDE_FAST == 44 and f.W.OP.RIDE_BODY == 42, "W.horseShare on RIDE_FAST 44 / RIDE_BODY 42")
check(f.S.keyOn() and f.S.on(), "key on, paired, free roam: on")

-- Key off: today's refusals (RIDE_FAST drops a horse id, RIDE_BODY refuses not_mirror) and nothing placed.
local off = fixture({key = false})
for i = 1, 10 do off.fast(1000 + i * 50, 10000 + i * 100, 2000, 30000, 0); off.tick(50) end
off.W.handlers[42]({1, 1, HORSE, 1, 2, 3, 0, 0, 65535})
check(not off.S.track and #off.places == 0, "key off: no track, no placement")
check(off.logged("event=ride_body;action=refuse;reason=not_mirror;id=6656"), "key off: the old not_mirror refusal still appears")
off.rides, off.horse.occupied = true, true
off.tick(60)
check(#off.tx == 0, "key off: the rider streams nothing")
local mb = fixture({mission = 30050})
check(not mb.S.on(), "Mother Base has no horse: off")

-- Viewer: the partner's stream places this copy's 6656 every frame, anchored under the shown rider.
f = fixture()
for i = 0, 12 do f.fast(5000 + i * 50, 10000 + i * 200, 2000, 30000, 100); f.tick(50) end
check(f.S.track ~= nil and f.logged("event=horse_share;step=track;from=partner"), "the partner's RIDE_FAST 6656 opens a track")
check(#f.places > 0 and f.places[#f.places].anchor == 1 and f.logged("event=horse_share;step=placed;"), "placed every frame with anchor 1")
check(f.S.partnerRides() and f.S.busy(), "the partner rides it: busy")
check(f.W.travel.blocked() == 3, "Travel and Bring partner here wait while the partner rides it (on_foot)")
-- The ride hold: within 4 m of the placed horse.
local p = f.places[#f.places]
f.env.vars.playerPosX, f.env.vars.playerPosZ = (p.x + 2000) / 1000, p.z / 1000
f.fast(5000 + 13 * 50, p.x, 2000, p.z, 100)
f.tick(50)
check(f.S.hold and f.masks[#f.masks][1] == "set" and f.masks[#f.masks][2].settingName == "MgsCoopHorseHold" and f.masks[#f.masks][2].buttons == 0x20,
    "within 4 m: the ACTION-only ride hold")
check(f.logged("event=horse_hold;step=action_masked;dist_cm="), "horse_hold step=action_masked")
f.env.vars.playerPosX = (p.x + 6000) / 1000
f.fast(5000 + 14 * 50, p.x, 2000, p.z, 100)
f.tick(50)
check(not f.S.hold and f.masks[#f.masks][1] == "reset" and f.logged("event=horse_hold;step=released;why=far"), "past 4.5 m: released")
f.env.vars.playerPosX = (p.x + 1000) / 1000
f.fast(5000 + 15 * 50, p.x, 2000, p.z, 100)
f.tick(50)
check(f.S.hold, "back within 4 m: held again")
-- The dismount: the final reliable pose leaves the horse where it was left; the hold goes; nothing moves it after.
local n = #f.places
f.final(55000, 2100, 31000, 200)
check(#f.places == n + 1 and f.places[n + 1].x == 55000 and f.places[n + 1].z == 31000 and f.places[n + 1].anchor == 0,
    "the final pose is placed once, unanchored")
check(f.logged("event=horse_share;step=final;placed=1;reason=ok") and f.logged("step=end;why=final"), "step=final, the track ends")
check(not f.S.hold and f.masks[#f.masks][1] == "reset" and not f.S.busy(), "the hold is released: the first E mounts")
check(f.W.travel.blocked() == nil, "Travel free again")
-- BODY0 round (runs 911ce293 / 144d99bb): the downed player held on the horse as cargo never gets the ride hold: its release
-- at the dismount (ResetPadMask) came with the victim's body going from lying to crouched on their own screen.
do
 local g = fixture()
 g.W.horseCargo = {state = "cargo", busy = function() return true end}
 for i = 0, 6 do g.fast(5000 + i * 50, 10000, 2000, 30000, 100); g.tick(50) end
 local q = g.places[#g.places]
 g.env.vars.playerPosX, g.env.vars.playerPosZ = q.x / 1000, q.z / 1000
 g.fast(5400, q.x, 2000, q.z, 100)
 g.tick(50)
 check(not g.S.hold and #g.masks == 0 and not g.logged("event=horse_hold;step=action_masked;"), "cargo on the horse: no ride hold")
end
f.fast(6000, 60000, 2000, 30000, 0)
f.tick(50)
check(not f.S.track and #f.places == n + 1, "a late datagram after the final pose is ignored")
f.tick(800)
check(#f.places == n + 1, "nothing places the unridden horse")
check(f.logged("horse_place=stats;placed="), "horse_place=stats")

-- Run 05232637: the census reads back a steady offset above each write (about 0.3-0.45 m, the unridden census over the
-- ridden one, probe P1). That is the offset (err_cm / up_cm), not a leap; a leap is the readback jumping up between
-- two frames.
f = fixture()
f.up = 450
for i = 0, 70 do f.fast(1000 + i * 50, 10000, 2000, 30000, 0); f.tick(33) end
local line
for _, l in ipairs(f.logs) do if l:find("horse_place=stats;", 1, true) then line = l end end
check(line and line:find("up_cm=45;", 1, true) and line:find(";leap=0;", 1, true), "a steady 45 cm readback offset: up_cm=45, leap=0")
f.horse.y = f.horse.y + 900
f.fast(1000 + 71 * 50, 10000, 2000, 30000, 0); f.tick(33)
f.fast(1000 + 72 * 50, 10000, 2000, 30000, 0); f.tick(2100)
line = nil
for _, l in ipairs(f.logs) do if l:find("horse_place=stats;", 1, true) then line = l end end
check(line and line:find(";leap=1;", 1, true), "a readback 0.9 m above the previous one: one leap")

-- Stream lost: the horse stays at its last placement, the ride ends.
f = fixture()
for i = 0, 5 do f.fast(1000 + i * 50, 10000, 2000, 30000, 0); f.tick(50) end
for _ = 1, 60 do f.tick(50) end
check(not f.S.track and f.logged("step=end;why=stream_lost"), "a stale stream ends the ride (why=stream_lost)")
-- A census generation change ends it.
f = fixture()
for i = 0, 3 do f.fast(1000 + i * 50, 10000, 2000, 30000, 0); f.tick(50) end
f.gen = 6
f.fast(1300, 10000, 2000, 30000, 0); f.tick(50)
check(not f.S.track and f.logged("step=end;why=generation"), "a horse generation change ends every seat")
-- Two drivers (a same-moment E race): this copy's player rides too. Nothing is placed and the partner's ride ends
-- (why=two_drivers); no new track opens while this copy's player rides, and the partner's final pose is ignored.
f = fixture()
for i = 0, 3 do f.fast(1000 + i * 50, 10000, 2000, 30000, 0); f.tick(50) end
local before = #f.places
f.rides = true
f.fast(1300, 10000, 2000, 30000, 0); f.tick(50)
check(#f.places == before and f.logged("event=horse_share;step=two_drivers"), "two drivers: no placement, logged")
check(not f.S.track and f.logged("step=end;why=two_drivers"), "two drivers: the partner's ride ends (why=two_drivers)")
for i = 1, 30 do f.fast(1300 + i * 50, 10000, 2000, 30000, 0); f.tick(50) end
check(not f.S.track and #f.places == before and f.count("step=two_drivers") == 1,
    "two drivers: no new track while this copy's player rides, logged once")
f.final(10000, 2000, 30000, 0)
check(#f.places == before and f.logged("step=final_ignored;why=local_rides"), "two drivers: the partner's final pose is ignored")
-- This copy's player gets off while the partner still rides: the partner's stream opens a new track and places again.
f.rides = false
for i = 1, 6 do f.fast(3000 + i * 50, 10000, 2000, 30000, 0); f.tick(50) end
check(f.S.track ~= nil and #f.places > before, "two drivers over: the partner's ride places again")
-- A refused placement is counted and named once.
f = fixture()
f.placeOk = false
for i = 0, 5 do f.fast(1000 + i * 50, 10000, 2000, 30000, 0); f.tick(50) end
check(f.count("step=refuse;reason=pins") == 1, "a refusal is logged once per reason")
-- A context change ends it.
f = fixture()
for i = 0, 3 do f.fast(1000 + i * 50, 10000, 2000, 30000, 0); f.tick(50) end
f.W.epoch = 8
f.tick(50)
check(not f.S.track and f.logged("step=end;why=context"), "an epoch change ends the ride")

-- Rider: 20 Hz RIDE_FAST while this copy's player rides record 0, one final RIDE_BODY at the dismount.
f = fixture()
f.rides, f.horse.occupied = true, true
for _ = 1, 20 do f.tick(25) end
local fast = 0
for _, t in ipairs(f.tx) do if t[1] == 44 then fast = fast + 1; check(t[3] == 3 and t[4] == HORSE and t[5] == 10000, "RIDE_FAST kind 3, id 6656, the census pose") end end
check(fast >= 9 and fast <= 11, "20 Hz while riding")
check(f.S.busy() and f.W.travel.blocked() == 3, "riding: busy")
-- M2 round 2 (the M1 regression part): the final_sent line carries the ride's path (the census pose read while riding).
for _ = 1, 10 do f.horse.x = f.horse.x + 100; f.tick(25) end
f.rides, f.horse.occupied = false, false
f.horse.x = 12345
f.tick(25)
local last = f.tx[#f.tx]
check(last[1] == 42 and last[3] == 1 and last[4] == HORSE and last[5] == 12345 and last[9] == 0 and last[10] == 65534,
    "the dismount: one final reliable RIDE_BODY kind 1 with the unridden pose")
check(f.logged("step=final_sent;ok=1;") and f.logged(";path_cm=100;"), "final_sent: the ridden path, 1 m (10 reads 0.1 m apart)")
local count = #f.tx
for _ = 1, 10 do f.tick(25) end
check(#f.tx == count and not f.S.busy(), "once")
-- The M1 seeds: with the module on the companion hold shows the host's horse.
f = fixture()
f.W.companion = {HOLD = {DOG = true}}
f.tick(33)
check(f.W.companion.HOLD.HORSE == true, "world_companion's hold gains HORSE")
-- M2: a downed partner on the shared horse (world_horse_cargo.lua) keeps Travel, Bring and the guest's checkpoint waiting.
f = fixture()
f.W.horseCargo = {busy = function() return true end}
check(f.S.busy() and f.W.travel.blocked() == 3, "M2: a body on the horse: busy")
f.W.horseCargo.busy = function() return false end
check(not f.S.busy() and f.W.travel.blocked() == nil, "M2: unloaded: not busy")
-- M2: each placement tells world_horse_cargo.lua the pose just written (a body loaded on this horse follows it the same frame).
f = fixture()
local heard = {}
f.W.horseCargo = {placed = function(x, y, z, yaw) heard[#heard + 1] = {x, y, z, yaw} end, busy = function() return false end}
for i = 0, 8 do f.fast(5000 + i * 50, 10000 + i * 200, 2000, 30000, 100); f.tick(50) end
check(#heard >= 1 and heard[#heard][4] == 100 and heard[#heard][1] == f.places[#f.places].x,
    "M2: world_horse_cargo hears each placement with the pose written")
-- M3: with world_horse_pillion the ride hold shrinks to where E would claim the rear seat or the stock mount prompt shows
-- (2.5 m, released past 2.8 m): 3 m from the partner-ridden horse E reaches the game. Without it the M1 hold stays (4 m /
-- 4.5 m): the shrink is unproven until a pickup or revive at 3 m is seen (M3 round 1: e_free never logged).
check(f.S.HOLD_MM == 4000 and f.S.FREE_MM == 4500 and f.S.PILLION_HOLD_MM == 2500 and f.S.PILLION_FREE_MM == 2800,
    "M3: 4 m / 4.5 m, and 2.5 m / 2.8 m with the pillion key")
f = fixture()
for i = 0, 6 do f.fast(5000 + i * 50, 10000, 2000, 30000, 100); f.tick(50) end
p = f.places[#f.places]
f.env.vars.playerPosX, f.env.vars.playerPosZ = (p.x + 3000) / 1000, p.z / 1000
f.fast(5400, p.x, 2000, p.z, 100)
f.tick(50)
check(f.S.hold, "M3: without the pillion key 3 m is still held (M1)")
f = fixture()
f.W.horsePillion = {keyOn = function() return true end}
for i = 0, 6 do f.fast(5000 + i * 50, 10000, 2000, 30000, 100); f.tick(50) end
p = f.places[#f.places]
f.env.vars.playerPosX, f.env.vars.playerPosZ = (p.x + 3000) / 1000, p.z / 1000
f.fast(5400, p.x, 2000, p.z, 100)
f.tick(50)
check(not f.S.hold and not f.logged("event=horse_hold;step=action_masked;"), "M3: 3 m from the ridden horse: no ride hold, E acts normally")
f.env.vars.playerPosX = (p.x + 2000) / 1000
f.fast(5450, p.x, 2000, p.z, 100)
f.tick(50)
check(f.S.hold, "M3: 2 m: held (the rear-seat claim zone)")
-- M3: the pillion (world_horse_pillion.lua) hears each placement and keeps Travel / Bring waiting while seated.
local seen = {}
f.W.horsePillion = {placed = function(x, y, z, yaw) seen[#seen + 1] = {x, z, yaw} end, busy = function() return true end}
f.fast(5500, p.x, 2000, p.z, 100)
f.tick(50)
check(#seen == 1 and seen[1][3] == 100, "M3: world_horse_pillion hears the placement")
check(f.S.busy(), "M3: a pillion seated or driving: busy")
print("world_horse_share_lua_check: " .. checks .. " checks passed")
