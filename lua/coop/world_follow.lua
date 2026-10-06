-- Original offline W3 travel; Lua-thread APIs only. W4 alignment is separate.
do
local W = e.MgsCoopWorld
local F, OP, H = {queue = {}, seeds = {}, serial = 0, last = 0, token = false, installHooks = {}}, W.OP, W.handlers
W.follow = F
local floor, fmt = math.floor, string.format
local SUPPORTED = {[30010] = true, [30020] = true, [30050] = true, [40010] = true, [40020] = true, [40050] = true}
local KEYS = {{"solface_groupNumber"}, {"hosface_groupNumber"}, {"rev_revengeRandomValue"},
    {"rev_revengeLv", "revenge"}, {"missionStartClock"}, {"missionStartWeather"},
    {"missionStartWeatherNextTime", nil, 1000}, {"missionStartExtraWeatherInterval", nil, 1000},
    {"heli_missionStartRoute"}, {"mis_isStartFromHelispace", nil, "bool"}, {"mis_isStartFromFreePlay", nil, "bool"},
    {"ply_initialPlayerState"}, {"ply_useMissionStartPos", nil, "bool"}, {"ply_missionStartPos", 3, 1000},
    {"ply_missionStartRot", nil, 1}, {"ply_useMissionStartPosForNoOrderBox", nil, "bool"},
    {"ply_missionStartPosForNoOrderBox", 3, 1000}, {"ply_missionStartRotForNoOrderBox", nil, 1}}
local function log(text) W.log("event=follow;" .. text) end
local function on()
    return F.enabled and W.active and W.level >= 3 and SUPPORTED[W.mission] and (not W.paired or W.eff >= 3)
end
local function guest() return on() and not W.host and F.armed and not F.token end
local function u32(n)
    if type(n) ~= "number" or n ~= n or n ~= floor(n) or n < -2147483648 or n > 4294967295 then return nil end
    return n < 0 and n + 4294967296 or n
end
local function hash(v) return type(v) == "string" and W.h(v) or u32(v or 0) end
local function run(f, ...)
    local args, n = {...}, select("#", ...)
    local token = F.token
    F.token = true
    local r = {pcall(f, unpack(args, 1, n))}
    F.token = token
    if not r[1] then W.fail(r[2]); return false end
    return true, unpack(r, 2)
end
F.run = run
function F.request(kind, arg)
    local now = W.now()
    if not F.reqAt or now - F.reqAt >= 2000 then
        F.reqAt = now
        W.tx(OP.REQ_MISSION, W.nextRid(), kind, W.mission, hash(arg) or 0)
        log(fmt("step=request;kind=%d;mission=%d", kind, W.mission))
    end
end
local function dims(key)
    if key[2] ~= "revenge" then return key[2] end
    local typ = W.get(TppRevenge, "REVENGE_TYPE")
    local n = W.get(typ, "MAX")
    return type(n) == "number" and n >= 1 and n <= 128 and n or nil
end
local function encoded(key, value)
    if key[3] == "bool" then return type(value) == "boolean" and (value and 1 or 0) or nil end
    if type(value) ~= "number" or value ~= value then return nil end
    return u32(floor(value * (key[3] or 1) + 0.5))
end
local function decoded(key, value)
    if key[3] == "bool" then return value == 1 end
    if key[3] and value >= 2147483648 then value = value - 4294967296 end
    return value / (key[3] or 1)
end
local function checksum(d, k, i, v) return (d * 31 + k * 1024 + i + v) % 4294967296 end
function F.pump()
    local t = F.outgoing
    if not t or t.sent then return end
    if not t.announced then
        if not W.tx(OP.TRANSITION, unpack(t)) then return end
        t.announced = true
        log(fmt("step=tx;id=%.0f;src=%d;next=%d;kind=%d", t[1], t[3], t[4], t[2]))
    end
    if not t.values then return end
    for _ = 1, 32 do
        local v = t.values[t.i]
        if not v then
            if not W.tx(OP.SEEDS_END, t[1], t[4], #t.values, t.digest) then return end
            t.sent = true
            log(fmt("step=seeds_tx;id=%.0f;mission=%d;n=%d", t[1], t[4], #t.values))
            return
        end
        if not W.tx(OP.SEED, t[1], t[4], v[1], v[2], v[3]) then return end
        t.i = t.i + 1
    end
end
function F.sendSeeds(nextMission)
    local t = F.outgoing
    if not t or t[4] ~= nextMission or t.sent then return end
    if t.values then F.pump(); return end
    local values, digest = {}, 0
    for k, key in ipairs(KEYS) do
        local n = dims(key)
        local data = W.get(gvars, key[1])
        if not key[2] then
            local value = encoded(key, data)
            if value ~= nil then values[#values + 1] = {k, 0, value} end
        elseif n then
            for i = 0, n - 1 do
                local value = encoded(key, W.get(data, i))
                if value ~= nil then values[#values + 1] = {k, i, value} end
            end
        end
    end
    for _, v in ipairs(values) do
        digest = checksum(digest, v[1], v[2], v[3])
    end
    t.values, t.digest, t.i = values, digest, 1
    F.pump()
end
H[OP.TRANSITION] = function(v, now)
    if W.host or not F.enabled then return end
    if not SUPPORTED[v[3]] or not SUPPORTED[v[4]] or (v[2] ~= 1 and v[2] ~= 2 and v[2] ~= 3 and v[2] ~= 4 and v[2] ~= 6) then
        W.tx(OP.FOLLOW_ACK, v[1], W.mission, 0)
        log("step=refused;reason=unsupported_kind"); return
    end
    if v[1] <= F.last then return end
    for _, t in ipairs(F.queue) do if t[1] == v[1] then return end end
    if #F.queue >= 8 then W.fail("follow_queue_full"); return end
    v.since = now
    F.queue[#F.queue + 1] = v
    log(fmt("step=queued;id=%.0f;src=%d;next=%d;kind=%d", v[1], v[3], v[4], v[2]))
end
H[OP.SEED] = function(v)
    if W.host or not F.enabled or v[1] <= F.last then return end
    local key = KEYS[v[3]]
    if not key or (not key[2] and v[4] ~= 0) or (key[2] and (not dims(key) or v[4] >= dims(key))) or
        (key[3] == "bool" and v[5] > 1) then W.fail("follow_bad_seed"); return end
    local s = F.seeds[v[1]]
    if not s then s = {mission = v[2], values = {}, digest = 0}; F.seeds[v[1]] = s end
    if s.mission ~= v[2] or #s.values >= 256 then W.fail("follow_seed_overflow"); return end
    s.values[#s.values + 1] = {v[3], v[4], v[5]}
    s.digest = checksum(s.digest, v[3], v[4], v[5])
end
H[OP.SEEDS_END] = function(v)
    if W.host or not F.enabled or v[1] <= F.last then return end
    local s = F.seeds[v[1]]
    if not s then s = {mission = v[2], values = {}, digest = 0}; F.seeds[v[1]] = s end
    if s.mission ~= v[2] or #s.values ~= v[3] or s.digest ~= v[4] then W.fail("follow_seed_checksum"); return end
    s.ready = true
    log(fmt("step=seeds_ready;id=%.0f;mission=%d;n=%d", v[1], v[2], v[3]))
end
H[OP.REQ_MISSION] = function(v)
    if not W.host then return end
    log(fmt("step=request_rx;rid=%d;kind=%d;mission=%d;decision=host_only", v[1], v[2], v[3]))
    W.tx(OP.REJECT, v[1], OP.REQ_MISSION, 11, 0, 0, 0)
end
H[OP.FOLLOW_ACK] = function(v)
    if W.host then log(fmt("step=ack;id=%.0f;mission=%d;status=%d", v[1], v[2], v[3])) end
end
function F.publish(kind)
    if not on() or not W.host or not W.paired or W.eff < 3 then return end
    local target = vars.missionCode
    if not SUPPORTED[target] then log("step=refused;reason=unsupported_mission"); return end
    F.serial = F.serial % 4294967295 + 1
    local t = {F.serial, F.deploying and 6 or kind, W.mission, target,
        W.get(svars, "mis_missionClearType") or 0, hash(W.get(gvars, "heli_missionStartRoute")) or 0,
        hash(W.get(vars, "mbLayoutCode")) or 0, W.get(vars, "mbClusterId") or 0,
        W.get(mvars, "mis_selectedDeployTime") or 0,
        (F.abortNoSave or (kind == 2 and W.get(mvars, "mis_abortWithSave") == false)) and 1 or 0, W.epoch % 65535 + 1}
    F.outgoing = t
    F.pump()
    F.deploying, F.abortNoSave = false, false
end
function F.applySeeds(t)
    local s = F.seeds[t[1]]
    if not s or not s.ready or s.mission ~= t[4] then return false end
    for _, v in ipairs(s.values) do
        local key, value = KEYS[v[1]], decoded(KEYS[v[1]], v[3])
        if key[2] then
            local data = W.get(gvars, key[1])
            if data ~= nil then data[v[2]] = value end
        else gvars[key[1]] = value end
    end
    vars.mbLayoutCode, vars.mbClusterId = t[7], t[8]
    log(fmt("step=seeds_apply;id=%.0f;mission=%d;n=%d", t[1], t[4], #s.values))
    return true
end
function F.tick(now)
    if F.enabled and W.active and F.hostDeploy and W.host and W.paired then
        local args = F.hostDeploy; F.hostDeploy = nil
        F.deploying = W.eff >= 3
        run(F.originalDeploy, unpack(args, 1, args.n))
    end
    if not on() then return end
    if W.host then F.pump(); return end
    local t = F.queue[1]
    if not t or F.current then return end
    if now - t.since > 120000 then
        table.remove(F.queue, 1); F.last = t[1]; F.seeds[t[1]] = nil
        W.tx(OP.FOLLOW_ACK, t[1], W.mission, 0); log("step=refused;reason=timeout"); return
    end
    if W.mission ~= t[3] or ((t[2] ~= 2 and t[2] ~= 3 and t[2] ~= 4) and not W.inGame()) or (t.retryAt and now < t.retryAt) then return end
    local demo = W.get(DemoDaemon, "IsDemoPlaying")
    if type(demo) == "function" and demo() == true then return end
    local seeds = F.seeds[t[1]]
    if not seeds or not seeds.ready then return end
    F.current = t
    local ok, result
    if t[2] == 3 then
        ok = run(TppMission.ExecuteRestartMission)
    elseif t[2] == 4 then
        ok = run(TppMission.ExecuteContinueFromCheckPoint)
    elseif t[2] == 2 then
        ok = run(TppMission.AbortMission, {nextMissionId = t[4], isNoSave = t[10] == 1, isNoFade = true,
            isAlreadyGameOver = W.get(svars, "mis_isDefiniteGameOver") == true or W.get(mvars, "mis_isReserveGameOver") == true})
    elseif t[2] == 6 then
        ok = run(function()
            TppMission.SelectNextMissionHeliStartRoute(t[4], t[6], false)
            vars.mbLayoutCode = t[7]
            TppMission.OnEndMissionPreparation(t[9], t[8])
        end)
    else
        ok, result = run(TppMission.ReserveMissionClear, {missionClearType = t[5], nextMissionId = t[4],
            nextHeliRoute = t[6], nextLayoutCode = t[7], nextClusterId = t[8]})
    end
    if not ok or result == false then F.current = nil; t.retryAt = now + 1000; log("step=refused;reason=vanilla"); return end
    W.tx(OP.FOLLOW_ACK, t[1], W.mission, 1)
    log(fmt("step=replay;id=%.0f;src=%d;next=%d;kind=%d;route=%.0f;deploy=%d", t[1], t[3], t[4], t[2], t[6], t[9]))
end
W.startHooks[#W.startHooks + 1] = function()
    F.enabled = W.level >= 3 and SUPPORTED[W.mission] == true
    F.reqAt = nil
    local t = F.current
    if t and W.mission == t[4] then
        F.last, F.current = t[1], nil
        F.seeds[t[1]] = nil
        if F.queue[1] and F.queue[1][1] == t[1] then table.remove(F.queue, 1) end
        W.tx(OP.FOLLOW_ACK, t[1], W.mission, 2)
        log(fmt("step=arrived;id=%.0f;mission=%d;epoch=%d", t[1], W.mission, W.epoch))
    elseif t then
        F.current = nil; F.queue = {}; F.seeds = {}; F.armed = false
        log("step=refused;reason=wrong_destination")
    end
end
W.pairedHooks[#W.pairedHooks + 1] = function() F.armed = F.enabled and W.eff >= 3 end
W.unpairedHooks[#W.unpairedHooks + 1] = function(reason)
    if reason == 1 or reason == 5 then
        F.queue, F.seeds, F.current, F.armed, F.last = {}, {}, nil, false, 0
        F.outgoing, F.hostDeploy = nil, nil
    end
end
W.stopHooks[#W.stopHooks + 1] = function(final)
    if final and not W.active then F.armed = false end
end
function F.install(cfg)
    F.enabled = cfg[1] >= 3 and SUPPORTED[vars.missionCode] == true
    for _, f in ipairs(F.installHooks) do f(cfg) end
    if F.installed then return end
    F.installed = true
    local suppressed = {ReserveMissionClear = {6, true}, ReserveMissionClearOnRideOnHelicopter = {5},
        ReserveMissionClearOnRideOnFultonContainer = {5}, ReserveMissionClearOnOutOfHotZone = {1},
        _ReserveMissionClearOnOutOfHotZone = {1}, AbortForOutOfMissionArea = {1}, AbortForRideOnHelicopter = {5},
        AbortMission = {2}, AbortMissionByMenu = {2}, RestartMission = {3}, ExecuteRestartMission = {3},
        ContinueFromCheckPoint = {4}, ExecuteContinueFromCheckPoint = {4}, ReturnToMission = {4},
        Reload = {7}, ExecuteReload = {7}, GameOverAbortMission = {2}, GameOverReturnToTitle = {8}}
    for name, spec in pairs(suppressed) do
        local original = W.get(TppMission, name)
        if type(original) == "function" then
            TppMission[name] = function(...)
                if guest() then F.request(spec[1]); if spec[2] then return false end; return end
                if on() and W.host and (name == "Reload" or name == "ExecuteReload" or name == "ReturnToMission") then
                    log("step=refused;reason=unsupported_reload"); return
                end
                if on() and W.host and (name == "ReserveMissionClear" or name == "AbortMission") then
                    local args = (...)
                    if type(args) == "table" and args.nextMissionId and not SUPPORTED[args.nextMissionId] then
                        log("step=refused;reason=unsupported_mission")
                        if spec[2] then return false end; return
                    end
                end
                if on() and W.host and name == "AbortMission" then
                    local args = (...); F.abortNoSave = type(args) == "table" and args.isNoSave == true
                end
                return original(...)
            end
        end
    end
    F.originalDeploy = W.get(TppMission, "OnEndMissionPreparation")
    if type(F.originalDeploy) == "function" then
        TppMission.OnEndMissionPreparation = function(...)
            if guest() then F.request(6); return end
            if on() and W.host then
                local target, emergency = W.get(gvars, "mis_nextMissionCodeForMissionClear"), W.get(gvars, "mis_nextMissionCodeForEmergency")
                if (target and not SUPPORTED[target]) or (emergency and emergency ~= 0) then
                    log("step=refused;reason=unsupported_mission"); return
                end
                if not W.paired or W.eff < 3 then
                    F.hostDeploy = {...}; F.hostDeploy.n = select("#", ...)
                    log("step=deploy_wait;reason=ready"); return
                end
                F.deploying = true
            end
            return F.originalDeploy(...)
        end
    end
    local reserve = W.get(e, "ReservePlayerLoadingPosition")
    if type(reserve) == "function" then
        e.ReservePlayerLoadingPosition = function(kind, ...)
            local loads = W.get(TppDefine, "MISSION_LOAD_TYPE") or {}
            local k = kind == loads.MISSION_FINALIZE and 1 or kind == loads.MISSION_ABORT and 2 or
                kind == loads.MISSION_RESTART and 3 or kind == loads.CONTINUE_FROM_CHECK_POINT and 4 or 0
            if k > 0 then local ok, err = pcall(F.publish, k); if not ok then W.fail(err) end end
            return reserve(kind, ...)
        end
    end
    local load, loadCheck = W.get(TppMission, "Load"), W.get(TppMission, "LoadWithChunkCheck")
    if type(load) == "function" then
        TppMission.Load = function(nextMission, ...)
            if on() and W.host then local ok, err = pcall(F.sendSeeds, nextMission); if not ok then W.fail(err) end end
            if on() and not W.host and F.current then
                local ok, applied = pcall(F.applySeeds, F.current)
                if not ok then W.fail(applied); return end
                if not applied then log("step=refused;reason=missing_seeds"); return end
            end
            return load(nextMission, ...)
        end
    end
    if type(loadCheck) == "function" then
        TppMission.LoadWithChunkCheck = function(...)
            if on() and not W.host and F.current then
                local s = F.seeds[F.current[1]]
                if not s or not s.ready then return end
            end
            return loadCheck(...)
        end
    end
    local allocate = W.get(e, "OnAllocate")
    if type(allocate) == "function" then
        e.OnAllocate = function(...)
            local t = cfg[2] == 1 and F.outgoing or F.current
            if F.enabled and t and vars.missionCode == t[4] then math.randomseed((t[1] + t[6]) % 2147483647) end
            return allocate(...)
        end
    end
end
end
