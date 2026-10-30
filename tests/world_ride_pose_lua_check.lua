-- Pose routing for realized horse and walker bodies. No game process.
local checks = 0
local function check(value, why)
    checks = checks + 1
    assert(value, checks .. ': ' .. why)
end
local env = setmetatable({e = {MgsCoopWorld = {
    OP = {}, handlers = {}, paired = true, eff = 3, mission = 30050, epoch = 7,
    now = function() return 1000 end,
    tx = function() return true end,
    tick = function() end
}}}, {__index = _G})
local chunk = assert(loadfile(arg[1]))
setfenv(chunk, env)
chunk()
local R = env.e.MgsCoopWorld.ridePose
local realized = {[6656] = true, [6657] = true, [22528] = true, [22529] = true}
check(R.decide({}, realized).action == 'drop', 'a pose with no body id is dropped')
check(R.decide({id = nil}, realized).action == 'drop', 'a nil body id is dropped')
check(R.decide({id = 1}, realized).action == 'drop', 'a body id outside the horse and walker ids is dropped')
-- User decision 2026-10-22 (docs/mounts.md "RIDE_BODY and the viewer's own buddy"): the partner's horse / walker
-- pose never moves a body that is not a registered mirror of the partner's mount. Nothing registers one in 0.13.5,
-- and index 0 (6656 / 22528, the copy's own declared buddy) is never a mirror.
check(type(R.mirrors) == 'table' and next(R.mirrors) == nil, 'no mirror body is registered in 0.13.5')
for _, id in ipairs({6656, 6657, 22528, 22529}) do
    local d = R.decide({id = id, kind = id < 22528 and 1 or 2}, realized)
    check(d.action == 'refuse' and d.id == id and d.reason == 'not_mirror',
        'no mirror: the realized horse / walker ' .. id .. ' is refused')
    d = R.decide({id = id}, realized, R.mirrors)
    check(d.action == 'refuse' and d.reason == 'not_mirror', 'the registry as shipped refuses ' .. id)
    local v = R.admit({id = id, kind = 3}, realized, true)
    check(v.action == 'refuse' and v.reason == 'not_mirror', 'no mirror: a vehicle-flagged horse / walker id ' .. id .. ' is refused')
end
local every = {[6656] = true, [6657] = true, [22528] = true, [22529] = true}
check(R.decide({id = 6656}, realized, every).reason == 'not_mirror' and R.decide({id = 22528}, realized, every).reason == 'not_mirror'
    and not R.mirror(6656, every) and not R.mirror(22528, every), "index 0 (the copy's own buddy) is never a mirror, even if named")
local mirrors = {[6657] = true, [22529] = true}
check(R.decide({id = 6657}, {}, mirrors).action == 'refuse' and R.decide({id = 6657}, {}, mirrors).reason == nil,
    'a mirror the census did not realize is refused')
check(R.decide({id = 22529}, {}, mirrors).action == 'refuse', 'a walker mirror the census did not realize is refused')
local walker = R.decide({id = 22529, x = 802100, y = 338300, z = 1642000, yaw = -500}, realized, mirrors)
check(walker.action == 'apply' and walker.id == 22529 and walker.command == 'SetPosition',
    'a realized walker mirror applies only as SetPosition on that body')
local horse = R.decide({id = 6657}, realized, mirrors)
check(horse.action == 'apply' and horse.id == 6657 and horse.command == 'SetCallHorse',
    'a realized horse mirror uses the pinned horse position call on that body')
local calls = {}
local function apply(pose)
    local decision = R.decide(pose, realized, mirrors)
    if decision.action == 'apply' then calls[#calls + 1] = decision end
    return decision.action
end
check(apply({id = 22529}) == 'apply' and #calls == 1 and calls[1].id == 22529 and calls[1].command == 'SetPosition',
    'applying a walker mirror pose commands only 22529')
check(apply({}) == 'drop' and #calls == 1, 'a missing id does not command another body')
check(apply({id = 6656}) == 'refuse' and apply({id = 22528}) == 'refuse' and #calls == 1,
    "the viewer's own horse and walker are never commanded")
check(apply({id = 6657}) == 'apply' and #calls == 2 and calls[2].id == 6657 and calls[2].command == 'SetCallHorse',
    'the horse mirror is commanded separately from the walker')
local gx, gy, gz = R.headingGoal(10, 20, 30, math.pi / 2)
check(math.abs(gx - 11) < 0.000001 and gy == 20 and math.abs(gz - 30) < 0.000001,
    'horse heading is the one-metre goal offset atan2 reads back')
check(R.decide({id = 4000}, realized).action == 'drop', 'a vehicle pose without kind is dropped')
check(R.decide({id = 4000, kind = 3}, realized).action == 'refuse',
    'a vehicle the census did not realize is refused')
local parked = R.admit({id = 4000, kind = 3}, realized, false)
check(parked.action == 'refuse' and parked.id == 4000, 'a body that is not a vehicle is refused')
local jeep = R.admit({id = 4000, kind = 3}, realized, true)
check(jeep.action == 'apply' and jeep.id == 4000 and jeep.command == 'SetPosition' and jeep.field == 'position',
    'a realized vehicle pose applies only as SetPosition on that body')
local horseJeep = R.admit({id = 6657, kind = 3}, realized, true, mirrors)
check(horseJeep.action == 'apply' and horseJeep.command == 'SetCallHorse' and horseJeep.field == nil,
    'a horse mirror keeps the horse call when a vehicle flag is also set')
check(R.admit({id = 6656, kind = 3}, realized, true, every).action == 'refuse',
    "a vehicle flag never turns the viewer's own horse into a vehicle placement")
-- Fast vehicle track: interpolation by sender tick, 100 ms behind.
local function near(a, b, eps) return math.abs(a - b) <= (eps or 0.001) end
local track = {samples = {}}
check(R.trackPush(track, 10000, 5000, 0, 0, 0, 0), 'first fast sample accepted')
check(not R.trackPush(track, 10010, 5000, 9, 9, 9, 9), 'a repeated sender tick is refused')
check(not R.trackPush(track, 10010, 4990, 9, 9, 9, 9), 'an older sender tick is refused')
check(R.trackPush(track, 10050, 5050, 1000, 0, 0, 100), 'next fast sample accepted')
check(R.trackPush(track, 10100, 5100, 2000, 0, 0, 200), 'third fast sample accepted')
check(track.offset == 5000, 'offset is the smallest receive-minus-send time')
local x, y, z, yaw, state = R.trackPose(track, 10125)
check(state == 'interpolate' and near(x, 500) and near(yaw, 50), 'render time 100 ms behind interpolates between samples')
x, y, z, yaw, state = R.trackPose(track, 10175)
check(state == 'interpolate' and near(x, 1500) and near(yaw, 150), 'interpolation follows the sender spacing')
x, y, z, yaw, state = R.trackPose(track, 10250)
check(state == 'extrapolate' and near(x, 3000), 'a late sample is extrapolated along the last segment')
x, y, z, yaw, state = R.trackPose(track, 10450)
check(state == 'hold' and near(x, 2000), 'extrapolation stops after 200 ms and holds the newest pose')
x, y, z, yaw, state = R.trackPose(track, 11800)
check(x == nil and state == 'stale', 'a track without samples for 1.5 s is stale')
check(R.trackPush(track, 10300, 5300, 2100, 0, 0, 200) and track.offset == 5000, 'a delayed sample keeps the smaller offset')
check(R.trackPush(track, 10330, 5310, 40000, 0, 0, 200) and #track.samples == 1, 'a 30 m jump inside 100 ms restarts interpolation')
local wrap = {samples = {}}
check(R.trackPush(wrap, 100, 4294967290, 0, 0, 0, 0) and R.trackPush(wrap, 160, 50, 600, 0, 0, 0), 'the u32 sender tick wraps')
check(wrap.samples[2].t - wrap.samples[1].t == 56, 'unwrapped ticks keep their spacing across the wrap')
check(near(R.angleLerp(3000, -3000, 0.5), 3141.5927, 0.01) or near(R.angleLerp(3000, -3000, 0.5), -3141.5927, 0.01),
    'heading interpolates through pi, not through zero')
check(near(R.angleLerp(-100, 100, 0.5), 0), 'heading interpolates the short way around zero')
-- End to end: receive, apply every frame, ignore late datagrams after the final pose.
local W = env.e.MgsCoopWorld
local clock, sent, placed = 20000, {}, {}
W.now = function() return clock end
W.tick32 = function(n) return math.floor(n) % 4294967296 end
W.tx = function(...) sent[#sent + 1] = {...}; return true end
env.Tpp = {IsVehicle = function(id) return id == 4000 end}
W.api = {
    send = function(id, cmd)
        if cmd.id == 'SetPosition' then placed[#placed + 1] = {id = id, x = cmd.position.x, rotY = cmd.rotY} end
        if cmd.id == 'GetPosition' then
            return {GetX = function() return 10 end, GetY = function() return 1 end, GetZ = function() return 2 end}, 0.5
        end
        return true
    end,
    buddyVector = function(px, py, pz) return {x = px, y = py, z = pz} end
}
R.nextRefresh = math.huge
W.handlers[44]({1, 3, 4000, 0, 0, 0, 0})
check(R.fast.tracks[4000] == nil or #R.fast.tracks[4000].samples == 1, 'a fast pose for a vehicle starts a track')
for i = 1, 6 do
    clock = clock + 50
    W.handlers[44]({1 + i * 50, 3, 4000, i * 1000, 0, 0, 0})
end
W.handlers[44]({400, 3, 1234, 0, 0, 0, 0})
check(R.fast.tracks[1234] == nil, 'a body that is not a vehicle never starts a track')
local before = #placed
W.tick()
check(#placed == before + 1 and placed[#placed].id == 4000 and near(placed[#placed].x, 4), 'each frame places the vehicle at its rendered pose')
clock = clock + 16
W.tick()
check(#placed == before + 2 and placed[#placed].x > 4, 'the next frame advances smoothly')
W.tick()
check(#placed == before + 2, 'an unchanged rendered pose is not placed again')
R.fast.localId = nil
local fakeRiding = true
env.PlayerInfo = {OrCheckStatus = function() return fakeRiding end}
env.PlayerStatus = {ON_VEHICLE = 1}
env.vars = {playerVehicleGameObjectId = 4000}
sent = {}
W.tick()
check(#sent == 1 and sent[1][1] == 44 and sent[1][3] == 3 and sent[1][4] == 4000 and sent[1][5] == 10000 and sent[1][8] == 500,
    'the driver streams its own vehicle on the fast op')
local placedWhileDriving = #placed
clock = clock + 20
W.tick()
check(#sent == 1 and #placed == placedWhileDriving, 'the driver sends at 20 Hz and never places the vehicle it drives')
fakeRiding = false
W.tick()
check(#sent == 2 and sent[2][1] == 42 and sent[2][3] == 3 and sent[2][4] == 4000 and sent[2][9] == 0,
    'leaving the vehicle sends one reliable final pose')
W.tick()
check(#sent == 2, 'the final pose is sent once')
W.handlers[42]({9, 3, 4000, 7000, 0, 0, 0, 0, 65535})
check(R.fast.tracks[4000] == nil and placed[#placed].x == 7, 'the reliable final pose ends the track and places the vehicle')
W.handlers[44]({999, 3, 4000, 1, 0, 0, 0})
check(R.fast.tracks[4000] == nil, 'a late datagram after the final pose is ignored')
clock = clock + 600
W.handlers[44]({1999, 3, 4000, 1, 0, 0, 0})
check(R.fast.tracks[4000] ~= nil, 'a new ride after the final pose starts a new track')
W.paired = false
W.tick()
check(next(R.fast.tracks) == nil, 'unpairing drops every fast track')
-- WS3 A1 (docs/vehicle-ws3.md): RIDE_TILT (op 45) carries the driver's record
-- angles under the same sender tick; the viewer interpolates them and writes
-- them through the native receipt right before each SetPosition.
check(W.OP.RIDE_TILT == 45 and W.handlers[45] ~= nil, 'RIDE_TILT is op 45 with its own handler')
do
    local track = {samples = {}}
    check(not R.tiltPush(track, 5000, 10, 20), 'a tilt before any fast sample is dropped')
    check(R.trackPush(track, 10000, 5000, 0, 0, 0, 0) and R.trackPush(track, 10050, 5050, 1000, 0, 0, 0), 'fast samples')
    check(R.tiltPush(track, 5000, 0, 100) and R.tiltPush(track, 5050, 100, -100), 'tilts under the fast ticks')
    check(not R.tiltPush(track, 5040, 1, 1) and not R.tiltPush(track, 5050, 1, 1), 'an older or repeated tilt tick is dropped')
    check(not R.tiltPush(track, 5100, 1571, 0) and not R.tiltPush(track, 5100, 0, -1571) and not R.tiltPush(track, 5100, 0.5, 0),
        'angles at or beyond pi/2, or fractional mrad, are dropped')
    local a, b = R.tiltAt(track, 5025)
    check(near(a, 50) and near(b, 0), 'the tilt interpolates at render time')
    a, b = R.tiltAt(track, 5200)
    check(a == 100 and b == -100, 'the newest tilt holds for a short while')
    check(R.tiltAt(track, 5400) == nil, 'a stale tilt is not written (the copy keeps its own angles)')
    a, b = R.tiltAt(track, 4000)
    check(a == 0 and b == 100, 'before the first tilt: the first one')
    check(R.tiltAt({samples = {}}, 1) == nil, 'no tilt samples: nil')
    local w = {samples = {}}
    check(R.trackPush(w, 100, 4294967290, 0, 0, 0, 0) and R.tiltPush(w, 4294967290, 5, 5) and R.tiltPush(w, 50, 10, 10) and
        w.tilts[2].t - w.tilts[1].t == 56, 'tilt ticks unwrap with the fast track clock')
end
do
    local receipts, tiltAnswer, placedAtWrite = {}, nil, nil
    W.receipt = function(source)
        receipts[#receipts + 1] = source
        if source:find(' -- read ', 1, true) then
            if tiltAnswer then return tiltAnswer[1], tiltAnswer[2] end
            return nil
        end
        if source:find(' -- write ', 1, true) then placedAtWrite = #placed; return true end
    end
    W.paired = true
    fakeRiding = false
    R.fast.send.id = nil
    clock = clock + 5000
    local t0 = 80000
    W.handlers[44]({t0, 3, 4000, 0, 0, 0, 0})
    W.handlers[45]({t0, 4000, 0, 0})
    for i = 1, 4 do
        clock = clock + 50
        W.handlers[44]({t0 + i * 50, 3, 4000, i * 1000, 0, 0, 0})
        W.handlers[45]({t0 + i * 50, 4000, i * 100, -i * 50})
    end
    W.handlers[45]({t0 + 60, 1234, 5, 5})
    W.handlers[45]({t0 + 60, 4000, 5})
    check(#R.fast.tracks[4000].tilts == 5, 'tilts ride on the live fast track; a short or foreign tilt is dropped')
    local before = #placed
    W.tick()
    check(#placed == before + 1 and near(placed[#placed].x, 2) and placedAtWrite == before and
        receipts[#receipts] == "return 'MGS_COOP_RIDE_TILT_V1' -- write 4000 200 -100",
        'the interpolated tilt is written right before the SetPosition of the same rendered tick')
    local writes = #receipts
    W.tick()
    check(#receipts == writes and #placed == before + 1, 'an unchanged pose and tilt neither write nor place again')
    -- The driver: RIDE_TILT only when the native read answers (enabled and the peer asked for it).
    clock = clock + 3000
    W.tick()
    fakeRiding = true
    tiltAnswer = {120, -30}
    sent = {}
    W.tick()
    check(#sent == 2 and sent[1][1] == 44 and sent[2][1] == 45 and sent[2][2] == sent[1][2] and sent[2][3] == 4000 and
        sent[2][4] == 120 and sent[2][5] == -30, 'the driver adds RIDE_TILT under the same sender tick')
    tiltAnswer = nil
    clock = clock + 60
    W.tick()
    check(#sent == 3 and sent[3][1] == 44, 'no native answer: RIDE_FAST alone')
    tiltAnswer = {1571, 0}
    clock = clock + 60
    W.tick()
    check(#sent == 4 and sent[4][1] == 44, 'an out-of-range native answer is not sent')
    -- The key off ('off' answer): Lua stops asking for the rest of the process.
    tiltAnswer = {nil, 'off'}
    clock = clock + 60
    W.tick()
    local asked = #receipts
    clock = clock + 60
    W.tick()
    check(R.tiltOff == true and #receipts == asked and #sent == 6 and sent[6][1] == 44, 'an off answer ends the reads; RIDE_FAST continues')
    fakeRiding = false
    W.receipt = nil
end
-- F1 vehicles (docs/native-puppet.md "Vehicle boarding"): a stock back-seat passenger (act 0x6E) has no
-- ON_VEHICLE, so R.localVehicle() is nil even with vars.playerVehicleGameObjectId naming the vehicle: the
-- passenger never streams that vehicle and keeps placing the driver's track on its copy (the stock passenger
-- phase attaches the viewer to that copy every frame).
do
    W.paired = true
    R.fast.tracks = {}
    R.fast.send.id = nil
    env.PlayerInfo = {OrCheckStatus = function() return false end}
    env.PlayerStatus = {ON_VEHICLE = 1}
    env.vars = {playerVehicleGameObjectId = 4000}
    clock = clock + 5000
    local t0 = 200000
    W.handlers[44]({t0, 3, 4000, 0, 0, 0, 0})
    for i = 1, 6 do
        clock = clock + 50
        W.handlers[44]({t0 + i * 50, 3, 4000, i * 1000, 0, 0, 0})
    end
    sent = {}
    local before = #placed
    W.tick()
    check(R.localVehicle() == nil and R.fast.localId == nil, 'a passenger without ON_VEHICLE has no local vehicle')
    check(#placed == before + 1 and placed[#placed].id == 4000, 'the passenger keeps placing the driver track on its copy')
    check(#sent == 0, 'the passenger streams nothing for that vehicle')
end
-- Bug 2 of the two-PC test of 2026-10-23 (docs/two-pc-test-2026-10-23.md): the host's relief vehicle 25600 existed
-- only on his copy. The guest's RIDE_FAST track still sent SetPosition (and a refused tilt write) every frame into the
-- missing id, and ride_fast claimed applied=1157. Each track now asks IsAlive at most every 500 ms; while it is not
-- true and GetPosition does not answer either (no instance here) the frame is skipped (no tilt write, no SetPosition)
-- and counted as absent=N on the ride_fast line. A wreck (IsAlive false, still in the pool) is placed as before.
do
    local api, logs, alive, asked, writes = W.api, {}, {[25600] = false, [4000] = true, [4001] = false}, {}, 0
    local pooled, askedPos = {[25600] = false, [4000] = true, [4001] = true}, {}
    local placedAbsent = 0
    W.api = {
        send = function(id, cmd)
            if cmd.id == 'IsAlive' then asked[id] = (asked[id] or 0) + 1; return alive[id] end
            if cmd.id == 'GetPosition' then
                askedPos[id] = (askedPos[id] or 0) + 1
                if pooled[id] then return {x = 1, y = 2, z = 3} end
                return nil
            end
            if cmd.id == 'SetPosition' then
                placed[#placed + 1] = {id = id, x = cmd.position.x, rotY = cmd.rotY}
                if id == 25600 and not alive[id] then placedAbsent = placedAbsent + 1 end
            end
            return true
        end,
        buddyVector = function(px, py, pz) return {x = px, y = py, z = pz} end
    }
    W.receipt = function(source)
        if source:find(' -- write ', 1, true) then writes = writes + 1; return true end
        return nil
    end
    W.log = function(text) logs[#logs + 1] = text end
    env.Tpp = {IsVehicle = function(id) return id == 4000 or id == 4001 or id == 25600 end}
    env.PlayerInfo = {OrCheckStatus = function() return false end}
    W.paired = true
    R.fast.tracks, R.fast.closed, R.fast.send.id, R.fast.localId = {}, {}, nil, nil
    R.fast.applied, R.fast.absent, R.fast.sent, R.fast.logAt = 0, 0, 0, 0
    R.tiltOff = nil
    clock = clock + 5000
    local t0 = 300000
    W.handlers[44]({t0, 3, 25600, 0, 0, 0, 0})
    W.handlers[45]({t0, 25600, 10, 10})
    local before = #placed
    for i = 1, 40 do
        clock = clock + 50
        W.handlers[44]({t0 + i * 50, 3, 25600, i * 1000, 0, 0, i})
        W.handlers[45]({t0 + i * 50, 25600, 10 + i, 10 - i})
        W.tick()
    end
    check(placedAbsent == 0 and #placed == before and writes == 0,
        'an id this copy lacks: no SetPosition and no tilt write while IsAlive is false')
    check(R.fast.applied == 0 and R.fast.absent == 40, 'absent frames are counted, not applied')
    check(asked[25600] >= 4 and asked[25600] <= 5, 'IsAlive is asked at most every 500 ms per track (2 s: ' .. tostring(asked[25600]) .. ')')
    check(askedPos[25600] == asked[25600], 'GetPosition is asked only after an IsAlive that is not true')
    local line
    for _, l in ipairs(logs) do if l:find('event=ride_fast;', 1, true) then line = l end end
    check(line ~= nil and line:find(';applied=0;', 1, true) and line:find(';absent=[1-9][0-9]*$') ~= nil,
        'the ride_fast line reports absent=N next to applied=0: ' .. tostring(line))
    -- The vehicle exists here again (spawned, or the viewer came near): placement resumes after the next check.
    alive[25600] = true
    local resumed = false
    for i = 41, 56 do
        clock = clock + 50
        W.handlers[44]({t0 + i * 50, 3, 25600, i * 1000, 0, 0, i})
        W.tick()
        if #placed > before then resumed = true; break end
    end
    check(resumed and placed[#placed].id == 25600 and R.fast.applied >= 1, 'present again: placed within one check interval')
    check(R.fast.absent == 40, 'no absent frame counted once it is alive (the check was due on that frame)')
    -- A present vehicle is placed every frame as before (IsAlive true), with its own track's check.
    R.fast.tracks = {}
    local t1 = 400000
    W.handlers[44]({t1, 3, 4000, 0, 0, 0, 0})
    for i = 1, 4 do
        clock = clock + 50
        W.handlers[44]({t1 + i * 50, 3, 4000, i * 1000, 0, 0, 0})
    end
    local b4 = #placed
    W.tick()
    check(#placed == b4 + 1 and placed[#placed].id == 4000, 'a vehicle this copy has is placed as before')
    check(askedPos[4000] == nil, 'a live vehicle is never asked GetPosition')
    -- A wreck here (IsAlive false, still in the pool: GetPosition answers) is placed as before, like the native seat
    -- gate's absent (the pool bit only). Hidden run 7561ab63: the guest's copy of the jeep was wrecked in a crash there
    -- only; skipping it left the seated driver behind (refused far, then partner_ride=hide; reason=ride).
    R.fast.tracks = {}
    local absentBefore, t2 = R.fast.absent, 500000
    W.handlers[44]({t2, 3, 4001, 0, 0, 0, 0})
    for i = 1, 4 do
        clock = clock + 50
        W.handlers[44]({t2 + i * 50, 3, 4001, i * 1000, 0, 0, 0})
    end
    local b5 = #placed
    for i = 5, 8 do
        clock = clock + 50
        W.handlers[44]({t2 + i * 50, 3, 4001, i * 1000, 0, 0, 0})
        W.tick()
    end
    local onlyWreck = #placed > b5
    for k = b5 + 1, #placed do if placed[k].id ~= 4001 then onlyWreck = false end end
    check(onlyWreck and R.fast.absent == absentBefore and asked[4001] == 1 and askedPos[4001] == 1,
        'a wreck here (IsAlive false, GetPosition answers) is placed as before, not absent (placed ' ..
        tostring(#placed - b5) .. ', absent ' .. tostring(R.fast.absent - absentBefore) .. ')')
    W.api, W.receipt, W.log = api, nil, nil
end
-- User decision 2026-10-22 (docs/mounts.md "RIDE_BODY and the viewer's own buddy"): the partner's RIDE_BODY stream
-- never places the viewer's own D-Horse / D-Walker. Before 0.13.5 the viewer's idle horse leapt and hung in the air
-- around the partner (hidden runs b264ad58, 251abcef, bfd6fcc4, fbde3dcc).
do
    local sends, logs, api = {}, {}, W.api
    W.api = {
        send = function(id, cmd) sends[#sends + 1] = {id = id, cmd = cmd.id}; return true end,
        buddyVector = function(px, py, pz) return {x = px, y = py, z = pz} end
    }
    W.log = function(text) logs[#logs + 1] = text end
    W.paired = true
    R.realized = {[6656] = true, [6657] = true, [22528] = true, [22529] = true, [4000] = true}
    R.occupied, R.refused, R.fast.tracks, R.fast.closed = {}, {}, {}, {}
    for i = 1, 3 do
        -- seq, kind (1 horse, 2 walker, 3 vehicle), id, x, y, z (mm), yaw (mrad), occupied, rider
        W.handlers[42]({i, 1, 6656, 1000 * i, 0, 2000, 300, 1, 1})
        W.handlers[42]({i, 2, 22528, 1000 * i, 0, 2000, 300, 1, 1})
        W.handlers[42]({i, 1, 6657, 1000 * i, 0, 2000, 300, 0, 65535})
        W.handlers[42]({i, 2, 22529, 1000 * i, 0, 2000, 300, 0, 65535})
        W.handlers[42]({i, 3, 6656, 1000 * i, 0, 2000, 300, 0, 65535})
    end
    check(#sends == 0, "the partner's horse / walker poses never command a horse or walker on the viewer's copy")
    check(R.refused[6656] == 6 and R.refused[22528] == 3 and R.refused[6657] == 3 and R.refused[22529] == 3,
        'every refused pose is counted per body id')
    check(#logs == 4 and logs[1] == 'event=ride_body;action=refuse;reason=not_mirror;id=6656;kind=1' and
        logs[2] == 'event=ride_body;action=refuse;reason=not_mirror;id=22528;kind=2', 'one world_lua line per refused body id')
    check(R.move({id = 6656, x = 0, y = 0, z = 0, yaw = 0}, 'SetCallHorse') == false and
        R.move({id = 22528, x = 0, y = 0, z = 0, yaw = 0}, 'SetPosition') == false and
        R.move({id = 22529, x = 0, y = 0, z = 0, yaw = 0}, 'SetPosition', 'position') == false and
        R.place(6656, 0, 0, 0, 0) == false and R.place(22528, 0, 0, 0, 0) == false and #sends == 0,
        'R.move and R.place refuse a horse or walker that is not a mirror')
    -- A registered mirror (index 1 only) is the one horse / walker a pose may move; index 0 never.
    R.mirrors[6657], R.mirrors[6656] = true, true
    W.handlers[42]({4, 1, 6657, 5000, 0, 2000, 300, 0, 65535})
    W.handlers[42]({4, 1, 6656, 5000, 0, 2000, 300, 0, 65535})
    check(#sends == 1 and sends[1].id == 6657 and sends[1].cmd == 'SetCallHorse', 'only the registered index-1 mirror is moved')
    R.mirrors = {}
    -- Vehicles are unchanged: the reliable final pose still places the vehicle body.
    W.handlers[42]({5, 3, 4000, 8000, 0, 0, 0, 0, 65535})
    check(#sends == 2 and sends[2].id == 4000 and sends[2].cmd == 'SetPosition', 'a vehicle RIDE_BODY pose still places the vehicle')
    W.api, W.log = api, nil
end
-- M6 seat races (docs/vehicles-companions-design.md 4 rule 3, 5 SEAT op 54; review R1/R2): the guest holds the
-- driver's seat of an empty vehicle until the host grants it (its claim goes out when its pre-query offers that
-- seat); the host denies one it wants or drives (ties to the host) and grants the rest, yielding at once; a
-- live grant sticks while the guest keeps claiming; a host that never answers lets the guest go stock.
local function seatEnv(host)
    local c = {tx = {}, notes = {}, logs = {}, cmds = {}, deny = {}, grant = {}, now = 1000, want = 65535, age = -1, on = 1, hits = 0}
    local W = {OP = {}, handlers = {}, paired = true, eff = 3, mission = 30010, epoch = 7, host = host,
        now = function() return c.now end,
        tx = function(op, ...) c.tx[#c.tx + 1] = {op, ...}; return true end,
        tick = function() end,
        log = function(s) c.logs[#c.logs + 1] = s end,
        note = function(code, a, b) c.notes[#c.notes + 1] = {code, a, b}; return true end}
    W.receipt = function(src)
        local cmd = src:match("^return 'MGS_COOP_BOARD_SEAT_V1' %-%- (.+)$")
        assert(cmd, 'the board seat receipt only: ' .. src)
        c.cmds[#c.cmds + 1] = cmd
        local v, ms = cmd:match('^deny (%d+) (%d+)$')
        if v then c.deny[#c.deny + 1] = {tonumber(v), tonumber(ms)}; c.denied = tonumber(v); c.granted = nil end
        local gv, gms = cmd:match('^grant (%d+) (%d+)$')
        if gv then c.grant[#c.grant + 1] = {tonumber(gv), tonumber(gms)}; c.granted = tonumber(gv); if c.denied == c.granted then c.denied = nil end end
        local h = cmd:match('^hold (%d+)$')
        if h then c.hold = tonumber(h) end
        if c.on ~= 1 then return {ok = true, on = 0} end
        return {ok = true, on = 1, want = c.want, age = c.age, deny = c.denied or 65535, left = 0, hits = c.hits,
            hold = (c.hold or 0) > 0 and 1 or 0, grant = c.granted or 65535, holds = c.holds or 0, ref = c.ref or 65535, rage = c.rage or -1}
    end
    local env = setmetatable({e = {MgsCoopWorld = W}}, {__index = _G})
    env.PlayerInfo = {OrCheckStatus = function() return c.driving ~= nil end}
    env.PlayerStatus = {ON_VEHICLE = 4}
    env.vars = {playerVehicleGameObjectId = 65535}
    env.Tpp = {IsVehicle = function(id) return id >= 25600 and id <= 26111 end}
    local f = assert(loadfile(arg[1]))
    setfenv(f, env)
    f()
    c.W, c.R, c.S, c.env = W, W.ridePose, W.ridePose.seat, env
    function c.drive(id) c.driving = id; env.vars.playerVehicleGameObjectId = id or 65535 end
    function c.has(p) for _, l in ipairs(c.logs) do if l:find(p, 1, true) then return true end end return false end
    function c.count(p) local n = 0 for _, l in ipairs(c.cmds) do if l == p then n = n + 1 end end return n end
    return c
end
-- Guest.
local gs = seatEnv(false)
check(gs.W.OP.SEAT == 54 and type(gs.W.handlers[54]) == 'function' and type(gs.S) == 'table', 'SEAT is op 54 with a handler')
gs.S.step(1000)
check(gs.cmds[1] == 'hold 2000' and gs.hold == 2000 and #gs.tx == 0, 'the guest holds every driver seat it has no grant for (a 2 s lease)')
gs.want, gs.age = 25608, 100
gs.S.step(1200)
check(#gs.tx == 1 and gs.tx[1][1] == 54 and gs.tx[1][2] == 1 and gs.tx[1][3] == 25608 and gs.tx[1][4] == 0 and gs.tx[1][5] == 2 and
    gs.tx[1][6] == 1, 'the guest claims the driver seat its pre-query offers: seq 1, id, seat 0, role guest, claim')
check(gs.has('event=seat;action=claim;vehicle=25608;role=guest;age=100'), 'the claim line')
gs.holds = 3
gs.S.step(1300)
gs.S.step(1500)
check(#gs.tx == 1, 'one claim per 500 ms (and one step per 200 ms)')
check(gs.has('event=seat;action=hold;vehicle=25608;holds=3;deny=65535;grant=65535'),
    'the native hold refused the ride icon before any answer: logged with the live deny and grant (none)')
check(gs.count('hold 2000') >= 3, 'the hold lease is refreshed every step')
gs.S.step(1700)
check(#gs.tx == 2 and gs.tx[2][2] == 2, 'the claim repeats while the want lasts')
gs.W.handlers[54]({5, 25608, 0, 1, 2})
check(gs.grant[1] and gs.grant[1][1] == 25608 and gs.grant[1][2] == 3000 and gs.has('event=seat;action=grant;vehicle=25608;role=guest;set=1'),
    "the host's grant: 3 s of native grant for that vehicle (its ride icon)")
gs.W.handlers[54]({6, 25608, 0, 1, 2})
check(#gs.grant == 2, 'each grant refreshes it')
gs.age = 700
gs.S.step(2400)
check(#gs.tx == 2, 'a stale want (over 600 ms) is not claimed')
gs.W.handlers[54]({7, 25608, 0, 1, 3})
check(gs.deny[1][1] == 25608 and gs.deny[1][2] == 3000 and gs.has('event=seat;action=deny;vehicle=25608;role=guest;set=1'),
    "the host's deny: 3 s of native deny on the guest")
check(#gs.notes == 1 and gs.notes[1][1] == 13 and gs.notes[1][2] == 6, 'the guest gets the seat-race toast (NOTE 13 a=6)')
gs.W.handlers[54]({8, 25608, 0, 1, 3})
check(#gs.notes == 1 and #gs.deny == 2, 'the deny is refreshed; one toast per 3 s')
for _, bad in ipairs({{9, 25608, 0, 2, 3}, {9, 25608, 1, 1, 3}, {9, 25599, 0, 1, 3}, {9, 25608, 0, 1}, {9, 25608, 0, 1, 1}}) do
    gs.W.handlers[54](bad)
end
check(#gs.deny == 2 and #gs.grant == 2, "the guest ignores a guest-role op, another seat, a non-vehicle id, a short op, a host claim")
-- A host that never answers (0.14.4, or its key off): the guest goes stock after 1.5 s, until an answer comes.
local sl = seatEnv(false)
sl.want, sl.age = 25608, 50
sl.S.step(1000)
check(#sl.tx == 1 and sl.hold == 2000, 'claimed, held')
sl.S.step(1600)
sl.S.step(2200)
check(not sl.has('state=silent'), 'unanswered for 1.2 s: still held')
sl.S.step(2600)
check(sl.has('event=seat;state=silent;vehicle=25608') and sl.hold == 0, 'unanswered for 1.5 s: hold 0 (the guest boards stock)')
local held = sl.count('hold 2000')
sl.S.step(3000)
sl.S.step(3600)
check(sl.count('hold 2000') == held and sl.cmds[#sl.cmds] == 'poll', 'silent: polled, never held again')
sl.W.handlers[54]({1, 25608, 0, 1, 2})
check(sl.has('event=seat;state=answered') and sl.grant[1][1] == 25608, 'an answer ends the silence')
sl.S.step(4000)
check(sl.cmds[#sl.cmds] == 'hold 2000', 'held again')
-- The board answers off (key off on this copy, or not installed yet).
local off = seatEnv(false)
off.on, off.want, off.age = 0, 25608, 10
off.S.step(1000)
off.S.step(1400)
check(#off.tx == 0 and off.has('event=seat;state=off') and #off.logs == 1, 'key off on this copy: one state=off line, nothing sent')
off.on = 1
off.S.step(2000)
check(#off.tx == 0 and #off.logs == 1, 'off is read again only after 10 s')
off.S.step(11100)
check(off.has('event=seat;state=on') and #off.tx == 1 and off.tx[1][3] == 25608, 'the board installed later: on, and the claim goes out')
-- Unpaired: the hold is dropped at once.
off.W.paired = false
off.W.tick()
check(off.cmds[#off.cmds] == 'hold 0', 'unpaired: hold 0')
-- Host.
local hs = seatEnv(true)
hs.S.step(1000)
check(hs.cmds[1] == 'poll' and hs.hold == nil, 'the host never holds')
hs.want, hs.age = 25608, 300
hs.W.handlers[54]({1, 25608, 0, 2, 1})
check(#hs.tx == 1 and hs.tx[1][3] == 25608 and hs.tx[1][5] == 1 and hs.tx[1][6] == 3 and
    hs.has('event=seat;action=deny;vehicle=25608;role=host;why=host_first'), 'the host wants it too: deny (ties go to the host)')
local hd = seatEnv(true)
hd.drive(25608)
hd.W.handlers[54]({1, 25608, 0, 2, 1})
check(#hd.tx == 1 and hd.tx[1][6] == 3 and hd.has('why=host_drives'), 'the host drives it: deny')
local hg = seatEnv(true)
hg.W.handlers[54]({1, 25608, 0, 2, 1})
check(#hg.tx == 1 and hg.tx[1][6] == 2 and hg.has('event=seat;action=grant;vehicle=25608;role=host') and hg.S.granted[25608] == 1000,
    'nobody else wants it: grant')
check(hg.deny[1] and hg.deny[1][1] == 25608 and hg.deny[1][2] == 3000 and hg.has('event=seat;action=yield;vehicle=25608;role=host'),
    'the host yields the granted vehicle at once (its own native deny)')
hg.want, hg.age, hg.now = 25608, 50, 1400
hg.W.handlers[54]({2, 25608, 0, 2, 1})
check(#hg.tx == 2 and hg.tx[2][6] == 2 and #hg.deny == 2 and hg.S.granted[25608] == 1400,
    'a live grant sticks while the guest keeps claiming, even with the host now at a door (the yield is refreshed)')
hg.hits = 3
hg.S.step(1500)
check(#hg.notes == 1 and hg.notes[1][1] == 13 and hg.notes[1][2] == 7, 'the host tried the yielded vehicle: the yield toast (NOTE 13 a=7)')
hg.hits = 4
hg.S.step(1800)
check(#hg.notes == 1, 'one yield toast per 3 s')
hg.S.step(4500)
check(hg.S.granted[25608] == nil, 'a grant lapses after 3 s without a new claim')
hg.W.handlers[54]({3, 25608, 0, 2, 1})
check(#hg.tx == 3 and hg.tx[3][6] == 3 and hg.has('why=host_first'), 'after the lapse the host at the door wins the next claim')
hg.W.handlers[54]({4, 25608, 0, 1, 1})
hg.W.handlers[54]({5, 25608, 0, 2, 2})
check(#hg.tx == 3, 'the host answers only guest claims')
local hoff = seatEnv(true)
hoff.on = 0
hoff.W.handlers[54]({1, 25608, 0, 2, 1})
check(#hoff.tx == 0, 'a host with the key off never answers (the guest goes stock after its timeout)')

-- Seats on (design 8.21 R6): while the native board refused this copy's ride icon for an empty vehicle (a live deny,
-- or the guest's hold before the host's grant) within GUARD_MS, only that vehicle's stock Fulton prompt is left on E and
-- holding E would Fulton it: the Fulton action is withheld (PlayerDisableAction.FULTON), polled every tick. Only a bit
-- this module added is removed again (world_revive_input.lua's rule), so a mission's own Fulton lock is kept.
local fg = seatEnv(false)
fg.env.vars.playerDisableActionFlag = 4
fg.env.PlayerDisableAction = {FULTON = 8}
fg.S.step(1000)
fg.S.guard(1000)
check(fg.env.vars.playerDisableActionFlag == 4 and not fg.has('action=fulton'), 'no refusal: the Fulton action stays available')
fg.ref, fg.rage = 25608, 0
fg.S.guard(1010)
check(fg.env.vars.playerDisableActionFlag == 12 and fg.has('event=seat;action=fulton;on=1;vehicle=25608'),
    'a refused ride icon withholds the Fulton action (other bits kept), logged')
fg.S.guard(1020)
local fultonLines = 0
for _, l in ipairs(fg.logs) do if l:find('action=fulton', 1, true) then fultonLines = fultonLines + 1 end end
check(fg.env.vars.playerDisableActionFlag == 12 and fultonLines == 1, 'withholding is idempotent and logged once')
fg.rage = 350
fg.S.guard(1360)
check(fg.env.vars.playerDisableActionFlag == 12, 'inside 400 ms of the last refusal: still withheld')
fg.rage = 450
fg.S.guard(1460)
check(fg.env.vars.playerDisableActionFlag == 4 and fg.has('event=seat;action=fulton;on=0;vehicle=25608'),
    'no refusal for 400 ms (the grant came, or the player walked off): the Fulton action is back')
fg.env.vars.playerDisableActionFlag = 8
fg.rage = 0
fg.S.guard(1600)
check(fg.env.vars.playerDisableActionFlag == 8, 'a mission Fulton lock is not doubled')
fg.rage = 900
fg.S.guard(2500)
check(fg.env.vars.playerDisableActionFlag == 8, 'a mission Fulton lock is never removed')
fg.env.vars.playerDisableActionFlag = 0
fg.rage = 0
fg.S.guard(2600)
check(fg.env.vars.playerDisableActionFlag == 8, 'refused again: withheld')
fg.env.vars.playerDisableActionFlag = 0
fg.S.guard(2610)
check(fg.env.vars.playerDisableActionFlag == 8, 'a mission overwrite while refused is withheld again')
local polls = fg.count('poll')
local noop = function() end
fg.R.refresh, fg.R.send, fg.R.fastSend, fg.R.fastApply = noop, noop, noop, noop
fg.W.tick()
check(fg.count('poll') > polls, 'the world tick runs the guard every tick (not only every 200 ms seat step)')
fg.W.paired = false
fg.W.tick()
check(fg.env.vars.playerDisableActionFlag == 0, 'unpaired: the withheld Fulton action comes back at once')
local fo = seatEnv(false)
fo.on = 0
fo.env.vars.playerDisableActionFlag = 4
fo.env.PlayerDisableAction = {FULTON = 8}
fo.ref, fo.rage = 25608, 0
fo.S.guard(1000)
check(fo.env.vars.playerDisableActionFlag == 4, 'the board off (key off): never withheld')
local fh = seatEnv(true)
fh.env.vars.playerDisableActionFlag = 0
fh.env.PlayerDisableAction = {FULTON = 8}
fh.ref, fh.rage = 25608, 10
fh.S.guard(1000)
check(fh.env.vars.playerDisableActionFlag == 8, "the host's own refusal (a yielded vehicle) withholds it too")
local fn = seatEnv(false)
fn.env.vars.playerDisableActionFlag = 0
fn.env.PlayerDisableAction = nil
fn.ref, fn.rage = 25608, 0
fn.S.guard(1000)
check(fn.env.vars.playerDisableActionFlag == 0, 'no PlayerDisableAction table: nothing written')
print('world_ride_pose_lua_check: ' .. checks .. ' checks passed')
