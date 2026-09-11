-- Adapted from Dynamite GetNearestPlayer, c90f252f8200765f66c005d6bf4357fda0c0d98e.
-- GPLv3 + Commons Clause 1.0; third_party/dynamite/LICENSE.
-- Changes: host mission-thread queries, actual roster indices, squared distance,
-- sparse soldier enumeration, disabled/dead exclusion and native expiry.
do
    local ready, failed, frames = false, false, 0
    local update, start, terminate = e.OnUpdate, e.OnMissionGameStart, e.OnTerminate
    local function clear()
        local call = loadstring("return 'MGS_COOP_TARGET_CLEAR_V1'")
        if call then call() end
    end
    local function finite(value)
        return type(value) == "number" and value == value and math.abs(value) < 1000000
    end
    local function choose()
        if not ready or failed or vars.missionCode ~= 30050 or type(Tpp.MgsCoop) ~= "table"
            or Tpp.MgsCoop.hostSessionCreated ~= true then return end
        -- Native time gate caps scans at twice per second. This frame gate also
        -- avoids parsing a receipt every frame; it is not the scan clock.
        frames = frames + 1
        if frames % 15 ~= 0 then return end
        local query = loadstring("return 'MGS_COOP_TARGET_SNAPSHOT_V1'")
        local players = query and query()
        if type(players) ~= "table" then return end
        local getId, send = GameObject.GetGameObjectIdByIndex, GameObject.SendCommand
        if type(getId) ~= "function" or type(send) ~= "function" then clear(); return end
        if getId("TppSoldier2", 0) == GameObject.NULL_ID then clear(); return end
        local count = send({type="TppSoldier2"}, {id="GetMaxInstanceCount"})
        if type(count) ~= "number" or count < 1 or count > 512 or count % 1 ~= 0 then clear(); return end
        local best, distance = nil, 1000 * 1000
        for i = 0, count - 1 do
            local id = getId("TppSoldier2", i)
            if id ~= nil and id ~= GameObject.NULL_ID then
                local life = send(id, {id="GetLifeStatus"})
                local status = send(id, {id="GetStatus"})
                if life == TppEnemy.LIFE_STATUS.NORMAL and status == TppGameObject.NPC_STATE_NORMAL then
                    local position = send(id, {id="GetPosition"})
                    if position then
                        local x, y, z = position:GetX(), position:GetY(), position:GetZ()
                        if finite(x) and finite(y) and finite(z) then
                            for _, player in ipairs(players) do
                                if player.alive and finite(player.x) and finite(player.y) and finite(player.z) then
                                    local dx, dy, dz = x-player.x, y-player.y, z-player.z
                                    local squared = dx*dx + dy*dy + dz*dz
                                    if squared < distance then best, distance = player.index, squared end
                                end
                            end
                        end
                    end
                end
            end
        end
        local source
        if best == 0 then source = "return 'MGS_COOP_TARGET_COMMIT_0_V1'"
        elseif best == 1 then source = "return 'MGS_COOP_TARGET_COMMIT_1_V1'"
        else clear(); return end
        local commit = loadstring(source)
        if commit then commit() end
    end
    local function updated(...)
        if not pcall(choose) then
            failed = true
            clear()
            if type(Tpp.MgsCoop) == "table" then Tpp.MgsCoop.targetingError = true end
        end
        return ...
    end
    if type(update) == "function" then e.OnUpdate = function(...) return updated(update(...)) end end
    local function started(...)
        ready, failed, frames = true, false, 0
        return ...
    end
    if type(start) == "function" then e.OnMissionGameStart = function(...) return started(start(...)) end end
    if type(terminate) == "function" then
        e.OnTerminate = function(...)
            ready = false
            clear()
            return terminate(...)
        end
    end
end
