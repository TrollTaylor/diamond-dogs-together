-- Realized horse, walker, and occupied-vehicle poses.
-- Walker placement is SetPosition with pos and rotY. Horse placement is
-- SetCallHorse. Its consumer writes heading as atan2(goal.x-start.x, goal.z-start.z),
-- so the goal is one metre along the census heading. A vehicle uses TppVehicle2
-- GetPosition and SetPosition. Neither call mounts, dismounts, or arms.
do
local W = e.MgsCoopWorld
if type(W) == "table" then
local R = {realized = {}, occupied = {}, poses = {}, seq = 0, nextSend = 0, nextRefresh = 0}
W.ridePose = R
local OP = 42
W.OP.RIDE_BODY = OP
local floor = math.floor
local function integer(n, lo, hi)
    return type(n) == "number" and n == floor(n) and n >= lo and n <= hi
end
local function horse(id) return id == 6656 or id == 6657 end
local function walker(id) return id == 22528 or id == 22529 end
function R.decide(pose, realized)
    if type(pose) ~= "table" or not integer(pose.id, 0, 65535) then return {action = "drop"} end
    local body = horse(pose.id) and "SetCallHorse" or walker(pose.id) and "SetPosition" or nil
    if not body and pose.kind ~= 3 then return {action = "drop"} end
    if type(realized) ~= "table" or realized[pose.id] ~= true then return {action = "refuse", id = pose.id} end
    if body then return {action = "apply", id = pose.id, command = body} end
    return {action = "apply", id = pose.id, command = "SetPosition", field = "position"}
end
function R.admit(pose, realized, isVehicle)
    local decision = R.decide(pose, realized)
    if decision.action == "apply" or type(pose) ~= "table" or pose.kind ~= 3 or isVehicle ~= true then return decision end
    if not integer(pose.id, 0, 65534) or horse(pose.id) or walker(pose.id) then return decision end
    return {action = "apply", id = pose.id, command = "SetPosition", field = "position"}
end
function R.headingGoal(x, y, z, yaw)
    return x + math.sin(yaw), y, z + math.cos(yaw)
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
    R.readVehicle()
end
local function milli(value)
    if type(value) ~= "number" then return nil end
    local n = floor(value * 1000 + (value >= 0 and 0.5 or -0.5))
    if n < -1000000000 or n > 1000000000 then return nil end
    return n
end
function R.readVehicle()
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
    if not typed then return end
    local api = W.api
    if type(api) ~= "table" or type(api.send) ~= "function" then return end
    local ok, pos, rot = pcall(api.send, id, {id = "GetPosition"})
    if not ok or type(pos) ~= "table" and type(pos) ~= "userdata" or type(pos.GetX) ~= "function" then return end
    local read, x, y, z = pcall(function() return pos:GetX(), pos:GetY(), pos:GetZ() end)
    local xm, ym, zm = read and milli(x), read and milli(y), read and milli(z)
    if not xm or not ym or not zm then return end
    local yaw = type(rot) == "number" and milli(rot) or 0
    if not yaw or yaw < -31416 or yaw > 31416 then yaw = 0 end
    R.realized[id] = true
    R.occupied[id] = true
    R.poses[id] = {id = id, kind = 3, x = xm, y = ym, z = zm, yaw = yaw, occupied = 1, rider = 0}
end
function R.move(pose, command, field)
    local api = W.api
    if type(api) ~= "table" or type(api.send) ~= "function" then return false end
    if R.occupied[pose.id] then return false end
    local x, y, z, yaw = pose.x / 1000, pose.y / 1000, pose.z / 1000, pose.yaw / 1000
    if command == "SetPosition" and field ~= "position" then
        local ok, result = pcall(api.send, pose.id, {id = "SetPosition", pos = {x, y, z}, rotY = yaw})
        return ok and result ~= false
    end
    if type(api.buddyVector) ~= "function" then return false end
    if command == "SetPosition" then
        local made, point = pcall(api.buddyVector, x, y, z)
        if not made or not point then return false end
        local ok, result = pcall(api.send, pose.id, {id = "SetPosition", position = point, rotY = yaw})
        return ok and result ~= false
    end
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
W.handlers[OP] = function(v)
    if not W.paired or not W.eff or W.eff < 3 or type(v) ~= "table" or #v ~= 9 then return end
    local pose = {id = v[3], kind = v[2], x = v[4], y = v[5], z = v[6], yaw = v[7], occupied = v[8], rider = v[9]}
    local isVehicle = false
    if pose.kind == 3 and type(Tpp) == "table" and type(Tpp.IsVehicle) == "function" then
        local ok, yes = pcall(Tpp.IsVehicle, pose.id)
        isVehicle = ok and yes == true
    end
    local decision = R.admit(pose, R.realized, isVehicle)
    if decision.action ~= "apply" then return end
    R.move(pose, decision.command, decision.field)
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
end
end
end
