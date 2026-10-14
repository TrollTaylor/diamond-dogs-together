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
local realized = {[6656] = true, [22529] = true}
check(R.decide({}, realized).action == 'drop', 'a pose with no body id is dropped')
check(R.decide({id = nil}, realized).action == 'drop', 'a nil body id is dropped')
check(R.decide({id = 1}, realized).action == 'drop', 'a body id outside the horse and walker ids is dropped')
check(R.decide({id = 6657}, realized).action == 'refuse' and R.decide({id = 6657}, realized).id == 6657,
    'a horse the census did not realize is refused')
check(R.decide({id = 22528}, realized).action == 'refuse', 'a walker the census did not realize is refused')
local walker = R.decide({id = 22529, x = 802100, y = 338300, z = 1642000, yaw = -500}, realized)
check(walker.action == 'apply' and walker.id == 22529 and walker.command == 'SetPosition',
    'a realized walker pose applies only as SetPosition on that body')
local horse = R.decide({id = 6656}, realized)
check(horse.action == 'apply' and horse.id == 6656 and horse.command == 'SetCallHorse',
    'a realized horse pose uses the pinned horse position call on that body')
local calls = {}
local function apply(pose)
    local decision = R.decide(pose, realized)
    if decision.action == 'apply' then calls[#calls + 1] = decision end
    return decision.action
end
check(apply({id = 22529}) == 'apply' and #calls == 1 and calls[1].id == 22529 and calls[1].command == 'SetPosition',
    'applying a walker pose commands only 22529')
check(apply({}) == 'drop' and #calls == 1, 'a missing id does not command another body')
check(apply({id = 6657}) == 'refuse' and #calls == 1, 'an unrealized body does not command the realized horse')
check(apply({id = 6656}) == 'apply' and #calls == 2 and calls[2].id == 6656 and calls[2].command == 'SetCallHorse',
    'the realized horse is commanded separately from the walker')
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
local horseJeep = R.admit({id = 6656, kind = 3}, realized, true)
check(horseJeep.action == 'apply' and horseJeep.command == 'SetCallHorse' and horseJeep.field == nil,
    'a horse id keeps the horse call when a vehicle flag is also set')
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
print('world_ride_pose_lua_check: ' .. checks .. ' checks passed')
