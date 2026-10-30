-- Realized horse, walker, and occupied-vehicle poses (RIDE_BODY, op 42).
-- Since 0.13.5 a horse or walker pose moves only a dedicated mirror body
-- (R.mirrors, none today), never the viewer's own buddy (see R.mirror).
-- Walker placement is SetPosition with pos and rotY. Horse placement is
-- SetCallHorse. Its consumer writes heading as atan2(goal.x-start.x, goal.z-start.z),
-- so the goal is one metre along the census heading. A vehicle uses TppVehicle2
-- GetPosition and SetPosition. Neither call mounts, dismounts, or arms.
-- The driven vehicle streams RIDE_FAST datagrams at 20 Hz; the other game
-- renders it 100 ms behind the newest sender tick and places it every frame.
-- WS3 A1 (docs/vehicle-ws3.md, partner_ride_tilt, default on): RIDE_TILT carries the
-- driver's vehicle record angles (+0x44/+0x48, mrad) under the same sender
-- tick, only when the native read receipt says the peer asked for it; the
-- viewer interpolates them like the yaw and the native write receipt puts
-- them into its copy's record right before each SetPosition.
do
local W = e.MgsCoopWorld
if type(W) == "table" then
local R = {realized = {}, occupied = {}, poses = {}, seq = 0, nextSend = 0, nextRefresh = 0,
    fast = {send = {next = 0}, tracks = {}, closed = {}, applied = 0, absent = 0}}
W.ridePose = R
local OP, FAST, TILT = 42, 44, 45
W.OP.RIDE_BODY = OP
W.OP.RIDE_FAST = FAST
W.OP.RIDE_TILT = TILT
local floor = math.floor
local SEND_MS, DELAY_MS, EXTRAPOLATE_MS, STALE_MS, CLOSED_MS, ALIVE_MS = 50, 100, 200, 1500, 500, 500
local JUMP_MM, MAX_SAMPLES = 30000, 32
local function integer(n, lo, hi)
    return type(n) == "number" and n == floor(n) and n >= lo and n <= hi
end
local function horse(id) return id == 6656 or id == 6657 end
local function walker(id) return id == 22528 or id == 22529 end
-- RIDE_BODY never moves the viewer's own D-Horse / D-Walker (user decision 2026-10-22,
-- docs/mounts.md "RIDE_BODY and the viewer's own buddy"): a horse or walker pose applies
-- only to a dedicated mirror of the partner's mount, named in R.mirrors (id -> true).
-- Nothing registers one in 0.13.5: the shipped buddy archives realize one buddy per copy,
-- and that body is the viewer's own. Index 0 (6656 / 22528, the copy's declared buddy)
-- is never a mirror (docs/mounts.md section 7, step 0). Vehicles are unchanged.
R.mirrors, R.refused = {}, {}
function R.mirror(id, mirrors)
    return (id == 6657 or id == 22529) and type(mirrors) == "table" and mirrors[id] == true
end
local function ownBody(id) return (horse(id) or walker(id)) and not R.mirror(id, R.mirrors) end
function R.decide(pose, realized, mirrors)
    if type(pose) ~= "table" or not integer(pose.id, 0, 65535) then return {action = "drop"} end
    local body = horse(pose.id) and "SetCallHorse" or walker(pose.id) and "SetPosition" or nil
    if not body and pose.kind ~= 3 then return {action = "drop"} end
    if body and not R.mirror(pose.id, mirrors) then return {action = "refuse", id = pose.id, reason = "not_mirror"} end
    if type(realized) ~= "table" or realized[pose.id] ~= true then return {action = "refuse", id = pose.id} end
    if body then return {action = "apply", id = pose.id, command = body} end
    return {action = "apply", id = pose.id, command = "SetPosition", field = "position"}
end
function R.admit(pose, realized, isVehicle, mirrors)
    local decision = R.decide(pose, realized, mirrors)
    if decision.action == "apply" or type(pose) ~= "table" or pose.kind ~= 3 or isVehicle ~= true then return decision end
    if not integer(pose.id, 0, 65534) or horse(pose.id) or walker(pose.id) then return decision end
    return {action = "apply", id = pose.id, command = "SetPosition", field = "position"}
end
function R.headingGoal(x, y, z, yaw)
    return x + math.sin(yaw), y, z + math.cos(yaw)
end

-- Fast vehicle track (pure): sender ticks are u32 ms; the receiver keeps
-- the smallest observed receive-minus-send offset, rising slowly with drift.
function R.angleLerp(a, b, f)
    local d = (b - a) % 6283.1853
    if d > 3141.5927 then d = d - 6283.1853 end
    local v = a + d * f
    if v > 3141.5927 then v = v - 6283.1853 elseif v < -3141.5927 then v = v + 6283.1853 end
    return v
end
function R.trackPush(track, now, t, x, y, z, yaw)
    if type(track) ~= "table" or not integer(t, 0, 4294967295) then return false end
    local unwrapped
    if track.raw == nil then
        unwrapped = t
    else
        local delta = (t - track.raw) % 4294967296
        if delta == 0 or delta >= 2147483648 then return false end
        unwrapped = track.time + delta
    end
    local samples = track.samples
    local last = samples[#samples]
    if last and (unwrapped - last.t <= 100 and (math.abs(x - last.x) > JUMP_MM or math.abs(z - last.z) > JUMP_MM)) then
        samples = {}
        track.samples = samples
    end
    track.raw, track.time = t, unwrapped
    samples[#samples + 1] = {t = unwrapped, x = x, y = y, z = z, yaw = yaw}
    if #samples > MAX_SAMPLES then table.remove(samples, 1) end
    local offset = now - unwrapped
    if track.offset == nil or offset < track.offset then track.offset = offset
    else track.offset = track.offset + (offset - track.offset) * 0.02 end
    track.received = now
    return true
end
-- Tilt samples (pure): a RIDE_TILT tick is unwrapped against its FAST track's
-- sender clock, kept in tick order (newest 32); a tilt older than the newest
-- is dropped. tiltAt interpolates at render time rt and holds the newest for
-- at most TILT_HOLD_MS (then nil: no write, the copy keeps its own angles).
local TILT_HOLD_MS, TILT_MAX = 300, 1570
function R.tiltPush(track, t, a44, a48)
    if type(track) ~= "table" or track.raw == nil or track.time == nil or not integer(t, 0, 4294967295) then return false end
    if not integer(a44, -TILT_MAX, TILT_MAX) or not integer(a48, -TILT_MAX, TILT_MAX) then return false end
    local d = (t - track.raw) % 4294967296
    if d >= 2147483648 then d = d - 4294967296 end
    local ut = track.time + d
    local tilts = track.tilts
    if not tilts then tilts = {}; track.tilts = tilts end
    local last = tilts[#tilts]
    if last and ut <= last.t then return false end
    tilts[#tilts + 1] = {t = ut, a = a44, b = a48}
    if #tilts > MAX_SAMPLES then table.remove(tilts, 1) end
    return true
end
function R.tiltAt(track, rt)
    local tilts = type(track) == "table" and track.tilts
    if not tilts or #tilts == 0 or type(rt) ~= "number" then return nil end
    local first, last = tilts[1], tilts[#tilts]
    if rt <= first.t then return first.a, first.b end
    if rt >= last.t then
        if rt - last.t > TILT_HOLD_MS then return nil end
        return last.a, last.b
    end
    for i = #tilts - 1, 1, -1 do
        local a = tilts[i]
        if a.t <= rt then
            local b = tilts[i + 1]
            local f = (rt - a.t) / (b.t - a.t)
            return R.angleLerp(a.a, b.a, f), R.angleLerp(a.b, b.b, f)
        end
    end
    return first.a, first.b
end
function R.trackPose(track, now)
    local samples = type(track) == "table" and track.samples
    if not samples or #samples == 0 or track.offset == nil then return nil, nil, nil, nil, "empty" end
    local rt = now - track.offset - DELAY_MS
    local first, last = samples[1], samples[#samples]
    if rt <= first.t then return first.x, first.y, first.z, first.yaw, "hold" end
    if rt >= last.t then
        local late = rt - last.t
        if late > STALE_MS then return nil, nil, nil, nil, "stale" end
        local prev = samples[#samples - 1]
        if not prev or late > EXTRAPOLATE_MS or last.t <= prev.t then return last.x, last.y, last.z, last.yaw, "hold" end
        local f = late / (last.t - prev.t)
        return last.x + (last.x - prev.x) * f, last.y + (last.y - prev.y) * f, last.z + (last.z - prev.z) * f,
            R.angleLerp(last.yaw, last.yaw + (last.yaw - prev.yaw), f), "extrapolate"
    end
    for i = #samples - 1, 1, -1 do
        local a = samples[i]
        if a.t <= rt then
            local b = samples[i + 1]
            local f = (rt - a.t) / (b.t - a.t)
            return a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f, R.angleLerp(a.yaw, b.yaw, f), "interpolate"
        end
    end
    return first.x, first.y, first.z, first.yaw, "hold"
end

local function census(kind)
    if type(W.receipt) ~= "function" or not integer(W.mission, 30010, 30050) or not integer(W.epoch, 1, 65535) then return end
    local ok, result = pcall(W.receipt, string.format("return 'MGS_COOP_RIDE_POSE_V1' -- %d %d %d", W.mission, W.epoch, kind))
    if ok and type(result) == "table" and result.ready == true and type(result.actors) == "table" then return result end
end
function R.refresh()
    R.realized, R.occupied, R.poses = {}, {}, {}
    if not W.paired or not W.eff or W.eff < 3 then return end
    for kind = 1, 2 do
        local result = census(kind)
        if result then
            for i = 1, #result.actors do
                local actor = result.actors[i]
                if type(actor) == "table" and actor.realized == true and integer(actor.id, 0, 65535)
                    and integer(actor.x, -1000000000, 1000000000) and integer(actor.y, -1000000000, 1000000000)
                    and integer(actor.z, -1000000000, 1000000000) and integer(actor.yaw, -31416, 31416) then
                    R.realized[actor.id] = true
                    R.occupied[actor.id] = actor.occupied == true
                    R.poses[actor.id] = {id = actor.id, kind = kind, x = actor.x, y = actor.y, z = actor.z,
                        yaw = actor.yaw, occupied = actor.occupied == true and 1 or 0,
                        rider = integer(actor.rider, 0, 65535) and actor.rider or 65535}
                end
            end
        end
    end
    local id = R.fast.localId
    if id then R.realized[id], R.occupied[id] = true, true end
end
local function milli(value)
    if type(value) ~= "number" or value ~= value then return nil end
    local n = floor(value * 1000 + (value >= 0 and 0.5 or -0.5))
    if n < -1000000000 or n > 1000000000 then return nil end
    return n
end
-- The vehicle the local player currently rides, or nil.
function R.localVehicle()
    if type(PlayerInfo) ~= "table" or type(PlayerInfo.OrCheckStatus) ~= "function" then return end
    if type(PlayerStatus) ~= "table" or type(vars) ~= "table" or type(Tpp) ~= "table" or type(Tpp.IsVehicle) ~= "function" then return end
    local riding, id = false, nil
    pcall(function()
        riding = PlayerInfo.OrCheckStatus({PlayerStatus.ON_VEHICLE}) == true
        id = vars.playerVehicleGameObjectId
    end)
    if not riding or not integer(id, 0, 65534) or horse(id) or walker(id) then return end
    local typed = false
    pcall(function() typed = Tpp.IsVehicle(id) == true end)
    if typed then return id end
end
-- Vehicle pose in mm / mrad from TppVehicle2 GetPosition.
function R.vehiclePose(id)
    local api = W.api
    if type(api) ~= "table" or type(api.send) ~= "function" then return end
    local ok, pos, rot = pcall(api.send, id, {id = "GetPosition"})
    if not ok or type(pos) ~= "table" and type(pos) ~= "userdata" then return end
    local read, x, y, z = pcall(function() return pos:GetX(), pos:GetY(), pos:GetZ() end)
    local xm, ym, zm = read and milli(x), read and milli(y), read and milli(z)
    if not xm or not ym or not zm then return end
    local yaw = type(rot) == "number" and milli(rot) or 0
    if not yaw or yaw < -31416 or yaw > 31416 then yaw = 0 end
    return xm, ym, zm, yaw
end
-- Native tilt receipts (inert without the native lab key and a capable peer).
function R.tiltRead(id)
    if R.tiltOff or type(W.receipt) ~= "function" or not integer(id, 0, 65534) then return nil end
    local ok, a, b = pcall(W.receipt, string.format("return 'MGS_COOP_RIDE_TILT_V1' -- read %d", id))
    if ok and integer(a, -TILT_MAX, TILT_MAX) and integer(b, -TILT_MAX, TILT_MAX) then return a, b end
    -- partner_ride_tilt is fixed for the process: an 'off' answer ends the asking.
    if ok and a == nil and b == "off" then R.tiltOff = true end
end
function R.tiltWrite(id, a, b)
    if type(W.receipt) ~= "function" or not integer(id, 0, 65534) or type(a) ~= "number" or type(b) ~= "number" then return false end
    local ra, rb = floor(a + 0.5), floor(b + 0.5)
    if not integer(ra, -TILT_MAX, TILT_MAX) or not integer(rb, -TILT_MAX, TILT_MAX) then return false end
    local ok, result = pcall(W.receipt, string.format("return 'MGS_COOP_RIDE_TILT_V1' -- write %d %d %d", id, ra, rb))
    return ok and result == true
end
function R.place(id, x, y, z, yaw)
    local api = W.api
    if ownBody(id) then return false end
    if type(api) ~= "table" or type(api.send) ~= "function" or type(api.buddyVector) ~= "function" then return false end
    local made, point = pcall(api.buddyVector, x / 1000, y / 1000, z / 1000)
    if not made or not point then return false end
    local ok, result = pcall(api.send, id, {id = "SetPosition", position = point, rotY = yaw / 1000})
    return ok and result ~= false
end
function R.move(pose, command, field)
    local api = W.api
    if type(pose) ~= "table" or ownBody(pose.id) then return false end
    if type(api) ~= "table" or type(api.send) ~= "function" then return false end
    if R.occupied[pose.id] then return false end
    local x, y, z, yaw = pose.x / 1000, pose.y / 1000, pose.z / 1000, pose.yaw / 1000
    if command == "SetPosition" and field ~= "position" then
        local ok, result = pcall(api.send, pose.id, {id = "SetPosition", pos = {x, y, z}, rotY = yaw})
        return ok and result ~= false
    end
    if command == "SetPosition" then return R.place(pose.id, pose.x, pose.y, pose.z, pose.yaw) end
    if type(api.buddyVector) ~= "function" then return false end
    local gx, gy, gz = R.headingGoal(x, y, z, yaw)
    local made, start = pcall(api.buddyVector, x, y, z)
    local madeGoal, goal = pcall(api.buddyVector, gx, gy, gz)
    if not made or not start or not madeGoal or not goal then return false end
    local ok, result = pcall(api.send, pose.id, {id = "SetCallHorse", startPosition = start, goalPosition = goal})
    return ok and result ~= false
end
function R.send(now)
    if not W.paired or not W.eff or W.eff < 3 or type(W.tx) ~= "function" or now < R.nextSend then return end
    R.nextSend = now + 250
    for _, pose in pairs(R.poses) do
        local seq = R.seq % 65535 + 1
        if W.tx(OP, seq, pose.kind, pose.id, pose.x, pose.y, pose.z, pose.yaw, pose.occupied, pose.rider) then R.seq = seq end
    end
end
-- Driver side: stream the ridden vehicle; one reliable final pose after exit.
function R.fastSend(now)
    local s = R.fast.send
    local id = R.localVehicle()
    R.fast.localId = id
    if not W.paired or not W.eff or W.eff < 3 or type(W.tx) ~= "function" then s.id = nil; return end
    if not id then
        if s.id then
            local x, y, z, yaw = R.vehiclePose(s.id)
            if x then
                local seq = R.seq % 65535 + 1
                if W.tx(OP, seq, 3, s.id, x, y, z, yaw, 0, 65535) then R.seq = seq end
            end
            s.id = nil
        end
        return
    end
    if s.id ~= id then s.id, s.next = id, 0 end
    if now < s.next then return end
    s.next = now + SEND_MS
    local x, y, z, yaw = R.vehiclePose(id)
    if x and W.tx(FAST, W.tick32(now), 3, id, x, y, z, yaw) then
        R.fast.sent = (R.fast.sent or 0) + 1
        -- The record angles under the same sender tick (nil unless enabled and asked for).
        local a, b = R.tiltRead(id)
        if a and W.tx(TILT, W.tick32(now), id, a, b) then R.fast.tiltSent = (R.fast.tiltSent or 0) + 1 end
    end
end
-- One line every 2 s while a track or a stream is live (rate evidence). applied counts SetPosition
-- calls that returned non-false (a call into an id with no instance also does); absent counts the
-- frames skipped because this copy has no live vehicle with that id (R.present).
local function logFast(now)
    local f = R.fast
    if type(W.log) ~= "function" or now < (f.logAt or 0) then return end
    f.logAt = now + 2000
    local tracks, state, samples = 0, "none", 0
    for _, t in pairs(f.tracks) do tracks, state, samples = tracks + 1, t.state or state, #t.samples end
    if tracks == 0 and (f.sent or 0) == (f.loggedSent or 0) then return end
    f.loggedSent = f.sent or 0
    W.log(string.format("event=ride_fast;tracks=%d;applied=%d;sent=%d;state=%s;samples=%d;tilt_sent=%d;tilt_written=%d;absent=%d", tracks,
        f.applied, f.sent or 0, state, samples, f.tiltSent or 0, f.tiltWritten or 0, f.absent or 0))
end
-- Whether this copy has a vehicle with this id (the Africa fix, the two-PC test of 2026-10-23 bug 2: the
-- host's relief vehicle 25600 existed only on his copy, and every frame sent SetPosition and a refused
-- tilt write into nothing). IsAlive (StrCode32 0xED0E1BD6) goes to the TppVehicle2 class dispatcher
-- 0xB305B0, which answers false when the index's pool bit is clear and nothing past the pool
-- (world_vehicle.lua V.alive). A wreck also answers false but stays in the pool and still answers
-- GetPosition (world_vehicle.lua decideId): it is placed as before, like the native seat gate, whose
-- absent is the pool bit only (hidden run 7561ab63: the guest's copy of the jeep was wrecked in a crash
-- there only; skipping it left the seated driver behind, refused far, then hidden). Asked at most every
-- ALIVE_MS per track; only an answered IsAlive that is not true with no GetPosition answer marks it absent
-- (no binding or an error keeps placing it).
function R.present(track, id, now)
    if track.aliveAt == nil or now - track.aliveAt >= ALIVE_MS or now < track.aliveAt then
        track.aliveAt = now
        local api, absent = W.api, false
        if type(api) == "table" and type(api.send) == "function" then
            local ok, alive = pcall(api.send, id, {id = "IsAlive"})
            if ok and alive ~= true then
                local okPos, pos = pcall(api.send, id, {id = "GetPosition"})
                absent = okPos and pos == nil
            end
        end
        track.absent = absent
    end
    return not track.absent
end
-- Passenger/observer side: place each live track every frame.
function R.fastApply(now)
    for id, track in pairs(R.fast.tracks) do
        local x, y, z, yaw, state = R.trackPose(track, now)
        if not x then
            R.fast.tracks[id] = nil
        elseif id ~= R.fast.localId and not R.present(track, id, now) then
            -- No copy here: neither tilt nor SetPosition; once a check finds it, that frame places it.
            track.applied = nil
            track.state = "absent"
            R.fast.absent = (R.fast.absent or 0) + 1
        elseif id ~= R.fast.localId then
            local a = track.applied
            local ta, tb = nil, nil
            if track.offset ~= nil then ta, tb = R.tiltAt(track, now - track.offset - DELAY_MS) end
            local tilted = ta ~= nil and (not a or a[5] == nil or math.abs(a[5] - ta) >= 1 or math.abs(a[6] - tb) >= 1)
            if not a or tilted or math.abs(a[1] - x) >= 1 or math.abs(a[2] - y) >= 1 or math.abs(a[3] - z) >= 1 or math.abs(a[4] - yaw) >= 1 then
                -- The tilt goes into the copy's record right before its SetPosition (0xBA9710's small move reads it).
                if ta ~= nil and R.tiltWrite(id, ta, tb) then R.fast.tiltWritten = (R.fast.tiltWritten or 0) + 1 end
                if R.place(id, x, y, z, yaw) then
                    track.applied = {x, y, z, yaw, ta, tb}
                    track.state = state
                    R.fast.applied = R.fast.applied + 1
                end
            end
        end
    end
end
local function isVehicle(id)
    if type(Tpp) ~= "table" or type(Tpp.IsVehicle) ~= "function" then return false end
    local ok, yes = pcall(Tpp.IsVehicle, id)
    return ok and yes == true
end
W.handlers[OP] = function(v)
    if not W.paired or not W.eff or W.eff < 3 or type(v) ~= "table" or #v ~= 9 then return end
    local pose = {id = v[3], kind = v[2], x = v[4], y = v[5], z = v[6], yaw = v[7], occupied = v[8], rider = v[9]}
    local vehicle = pose.kind == 3 and isVehicle(pose.id)
    if vehicle then
        R.fast.tracks[pose.id] = nil
        R.fast.closed[pose.id] = W.now and W.now() or 0
    end
    local decision = R.admit(pose, R.realized, vehicle, R.mirrors)
    if decision.reason == "not_mirror" then
        -- The partner's horse / walker pose names a body that is not a mirror (the viewer's
        -- own buddy): never moved; one world_lua line per body id.
        local n = (R.refused[decision.id] or 0) + 1
        R.refused[decision.id] = n
        if n == 1 and type(W.log) == "function" then
            W.log(string.format("event=ride_body;action=refuse;reason=not_mirror;id=%d;kind=%s", decision.id, tostring(pose.kind)))
        end
    end
    if decision.action ~= "apply" then return end
    R.move(pose, decision.command, decision.field)
end
W.handlers[TILT] = function(v)
    if not W.paired or not W.eff or W.eff < 3 or type(v) ~= "table" or #v ~= 4 then return end
    local id = v[2]
    if not integer(id, 0, 65534) or horse(id) or walker(id) then return end
    -- A tilt rides on a live RIDE_FAST track of that vehicle only.
    local track = R.fast.tracks[id]
    if track then R.tiltPush(track, v[1], v[3], v[4]) end
end
W.handlers[FAST] = function(v)
    if not W.paired or not W.eff or W.eff < 3 or type(v) ~= "table" or #v ~= 7 or v[2] ~= 3 then return end
    local id, now = v[3], W.now and W.now() or 0
    if not integer(id, 0, 65534) or horse(id) or walker(id) then return end
    local closed = R.fast.closed[id]
    if closed and now - closed < CLOSED_MS then return end
    local track = R.fast.tracks[id]
    if not track then
        if not isVehicle(id) then return end
        track = {samples = {}}
        R.fast.tracks[id] = track
    end
    R.trackPush(track, now, v[1], v[4], v[5], v[6], v[7])
end
-- M6 seat races (design 4 rule 3, 5 SEAT; protocol 21; partner_ride_race, on with the gun profile): SEAT (op 54:
-- seq, TppVehicle2 id, seat 0, role 1 host / 2 guest, action 1 claim / 2 grant / 3 deny) keeps two players from both
-- driving one empty vehicle. Native site C records the want (the pre-query's final answer) and refuses the ride icon
-- for a denied vehicle or, on the guest, one without a live grant (the hold). The guest holds (HOLD_MS lease) and
-- claims its want; the host denies one it drives or wants (ties to the host) unless granted within GRANT_MS, else
-- grants and yields it. NOTE 13 a=6: the denied guest; a=7: the host tried a yielded one. A claim unanswered for
-- ANSWER_MS (0.14.4, key off): the guest goes stock. Once the winner drives, the peer-driver policy decides.
local SEAT = 54
W.OP.SEAT = SEAT
local S = {seq = 0, next = 0, claimAt = {}, granted = {}, gotGrant = {}, toastAt = 0, off = nil,
    n = {claim = 0, grant = 0, deny = 0, yield = 0}}
R.seat = S
local SEAT_TICK, CLAIM_MS, WANT_MS, HOST_WANT_MS, GRANT_MS, DENY_MS, TOAST_MS, OFF_MS = 200, 500, 600, 1000, 3000, 3000, 3000, 10000
local HOLD_MS, ANSWER_MS = 2000, 1500
S.CLAIM_MS, S.HOST_WANT_MS, S.GRANT_MS, S.DENY_MS, S.HOLD_MS, S.ANSWER_MS = CLAIM_MS, HOST_WANT_MS, GRANT_MS, DENY_MS, HOLD_MS, ANSWER_MS
local CLAIM, GRANT, DENY = 1, 2, 3
local function seatLog(text) if type(W.log) == "function" then pcall(W.log, "event=seat;" .. text) end end
function S.board(cmd)
    if type(W.receipt) ~= "function" then return nil end
    local ok, r = pcall(W.receipt, "return 'MGS_COOP_BOARD_SEAT_V1' -- " .. cmd)
    if ok and type(r) == "table" and r.ok == true then return r end
end
local function seatVehicle(id) return integer(id, 25600, 26111) end
local function toast(a, now)
    if now < S.toastAt then return end
    S.toastAt = now + TOAST_MS
    if type(W.note) == "function" then pcall(W.note, 13, a, 0) end
end
-- One SEAT op; the receiver checks the rest.
function S.tx(vehicle, role, action)
    S.seq = S.seq % 65535 + 1
    return W.tx(SEAT, S.seq, vehicle, 0, role, action) == true
end
-- A fresh want of this copy (the native answer r).
local function wants(r, vehicle, ms)
    return r and r.on == 1 and r.want == vehicle and integer(r.age, 0, ms)
end
-- Seats on (8.21 R6): while site C refuses the ride icon, E offers only the vehicle's Fulton: FULTON is withheld while
-- it refused within GUARD_MS (receipt ref / rage, every tick); only a bit added here is removed (revive's rule).
local GUARD_MS = 400
local function fultonFlag(on)
    local ok, f, b = pcall(function() return vars.playerDisableActionFlag, PlayerDisableAction.FULTON end)
    if not ok or not integer(f, 0, 2147483647) or not integer(b, 1, 1073741824) then S.fAdded = nil; return end
    local set = math.floor(f / b) % 2 == 1
    local v = on and not set and f + b or not on and S.fAdded and set and f - b
    if not on then S.fAdded = nil end
    if v and pcall(function() vars.playerDisableActionFlag = v end) and on then S.fAdded = true end
end
function S.guard()
    local r = not S.off and S.board("poll")
    local id = r and r.on == 1 and seatVehicle(r.ref) and integer(r.rage, 0, GUARD_MS) and r.ref or nil
    fultonFlag(id ~= nil)
    if id == S.fulton then return end
    S.n.fLog = (S.n.fLog or 0) + 1
    if S.n.fLog <= 20 then seatLog(string.format("action=fulton;on=%d;vehicle=%d", id and 1 or 0, id or S.fulton)) end
    S.fulton = id
end
-- Unpaired, or the world link below level 3: no hold survives (it would keep the guest from any driver's seat).
function S.reset()
    if S.held then S.board("hold 0") end
    if S.fulton or S.fAdded then S.off = true; S.guard() end
    S.off, S.granted, S.gotGrant, S.next, S.held, S.ask, S.silent, S.hitsSeen = nil, {}, {}, 0, nil, nil, nil, nil
end
-- Off (the key, or the board not installed yet) is read again every OFF_MS; logged once per pairing.
function S.step(now)
    if now < S.next then return end
    S.next = now + SEAT_TICK
    local guest = not W.host
    local hold = guest and not S.silent
    local r = S.board(hold and string.format("hold %d", HOLD_MS) or "poll")
    if not r or r.on ~= 1 then
        S.next = now + OFF_MS
        if not S.off then S.off = true; seatLog("state=off") end
        return
    end
    if S.off then S.off = nil; seatLog("state=on") end
    S.on = true
    if hold then S.held = true end
    if W.host then
        for id, at in pairs(S.granted) do
            if now - at > GRANT_MS then S.granted[id] = nil end
        end
        -- The host tried a vehicle it yielded (its own deny refused it): NOTE 13 a=7.
        local hits = tonumber(r.hits) or 0
        if S.hitsSeen and hits > S.hitsSeen and S.granted[r.deny] then toast(7, now) end
        S.hitsSeen = hits
        return
    end
    -- Guest: the native hold refused the ride icon (site C / B): logged with the live deny and grant, so a run
    -- shows a refusal before any answer existed (at most once a second, 20 lines per pairing).
    local holds = tonumber(r.holds) or 0
    if S.holdsSeen and holds > S.holdsSeen and now - (S.holdLogAt or -1000) >= 1000 and (S.n.holdLog or 0) < 20 then
        S.holdLogAt, S.n.holdLog = now, (S.n.holdLog or 0) + 1
        seatLog(string.format("action=hold;vehicle=%d;holds=%d;deny=%d;grant=%d", tonumber(r.want) or 65535, holds,
            tonumber(r.deny) or 65535, tonumber(r.grant) or 65535))
    end
    S.holdsSeen = holds
    -- Guest: a claim the host left unanswered for ANSWER_MS: stock until an answer comes.
    if not S.silent and S.ask and now - S.ask.at >= ANSWER_MS then
        S.silent = true
        seatLog(string.format("state=silent;vehicle=%d", S.ask.id))
        S.board("hold 0")
        S.ask = nil
    end
    -- Claim the vehicle its pre-query offers as the driver's.
    local id = r.want
    if seatVehicle(id) and integer(r.age, 0, WANT_MS) and now - (S.claimAt[id] or -CLAIM_MS) >= CLAIM_MS and S.tx(id, 2, CLAIM) then
        S.claimAt[id] = now
        if not S.ask then S.ask = {id = id, at = now} end
        S.n.claim = S.n.claim + 1
        if S.n.claim <= 20 then seatLog(string.format("action=claim;vehicle=%d;role=guest;age=%d", id, r.age)) end
    end
end
W.handlers[SEAT] = function(v)
    if not W.paired or not W.eff or W.eff < 3 or type(v) ~= "table" or #v ~= 5 then return end
    local id, role, action = v[2], v[4], v[5]
    if not seatVehicle(id) or v[3] ~= 0 then return end
    local now = W.now and W.now() or 0
    if W.host then
        if role ~= 2 or action ~= CLAIM then return end
        local r = S.board("poll")
        if not r or r.on ~= 1 then return end
        if not S.hitsSeen then S.hitsSeen = tonumber(r.hits) or 0 end
        local live = S.granted[id] and now - S.granted[id] <= GRANT_MS
        local why = R.localVehicle() == id and "host_drives" or not live and wants(r, id, HOST_WANT_MS) and "host_first" or nil
        if why then
            S.tx(id, 1, DENY)
            S.granted[id] = nil
            S.n.deny = S.n.deny + 1
            seatLog(string.format("action=deny;vehicle=%d;role=host;why=%s", id, why))
        else
            -- Grant, and yield at once: this copy's own deny for the vehicle, refreshed by every claim.
            S.tx(id, 1, GRANT)
            S.board(string.format("deny %d %d", id, GRANT_MS))
            if not live then
                seatLog(string.format("action=grant;vehicle=%d;role=host", id))
                seatLog(string.format("action=yield;vehicle=%d;role=host", id))
                S.n.yield = S.n.yield + 1
            end
            S.granted[id] = now
            S.n.grant = S.n.grant + 1
        end
        return
    end
    if role ~= 1 or (action ~= DENY and action ~= GRANT) then return end
    -- Any answer: the host speaks SEAT; the guest holds again.
    S.ask = nil
    if S.silent then S.silent = nil; seatLog("state=answered") end
    if action == DENY then
        local r = S.board(string.format("deny %d %d", id, DENY_MS))
        S.gotGrant[id] = nil
        S.n.deny = S.n.deny + 1
        seatLog(string.format("action=deny;vehicle=%d;role=guest;set=%d", id, r and r.deny == id and 1 or 0))
        toast(6, now)
    else
        local r = S.board(string.format("grant %d %d", id, GRANT_MS))
        S.n.grant = S.n.grant + 1
        if not S.gotGrant[id] or now - S.gotGrant[id] > GRANT_MS then
            seatLog(string.format("action=grant;vehicle=%d;role=guest;set=%d", id, r and r.grant == id and 1 or 0))
        end
        S.gotGrant[id] = now
    end
end
local tick = W.tick
function W.tick(...)
    local now = W.now and W.now() or 0
    if now >= R.nextRefresh then
        R.refresh()
        R.nextRefresh = now + 250
    end
    if tick then tick(...) end
    R.send(now)
    if W.paired and W.eff and W.eff >= 3 then S.step(now); S.guard(now) else S.reset() end
    if W.paired and W.eff and W.eff >= 3 then
        R.fastSend(now)
        R.fastApply(now)
        logFast(now)
    else
        R.fast.tracks, R.fast.send.id = {}, nil
    end
end
end
end
