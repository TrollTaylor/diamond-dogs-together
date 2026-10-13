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
print('world_ride_pose_lua_check: ' .. checks .. ' checks passed')
