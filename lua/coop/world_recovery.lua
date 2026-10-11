-- Original offline shared death and checkpoint recovery. All game calls stay
-- on the Lua owner; accepted checkpoints are observed at VarSave, not inferred
-- from Update's nil return. Current-position saves carry the host's exact pose.
do
    local W, floor, fmt = e.MgsCoopWorld, math.floor, string.format
    local R, A, OP, H = {version = 0, seen = 0}, W.align, W.OP, W.handlers
    W.recovery = R
    local function log(text) W.log("event=recovery;" .. text) end
    local function valid(n, lo, hi) return type(n) == "number" and n == floor(n) and n >= lo and n <= hi end
    local function hash(n)
        if not valid(n, -2147483648, 4294967295) then return nil end
        return n < 0 and n + 4294967296 or n
    end
    local function on()
        local m = W.mission
        return R.enabled and W.active and W.paired and W.eff >= 3 and
            (m == 30010 or m == 30020 or m == 30050 or m == 40010 or m == 40020 or m == 40050)
    end
    local function death(kind)
        local t = W.get(TppDefine, "GAME_OVER_TYPE") or {}
        return kind == t.PLAYER_DEAD or kind == t.PLAYER_FALL_DEAD
    end
    local function ready() return not (W.feature and W.feature.enemies) or W.w1 end
    local function markDead(kind, radio, cause)
        if R.dead then return end
        R.dead, A.dead, W.w1 = true, true, false
        A.tx, A.rx, R.pending, R.out = nil, nil, nil, nil
        R.game = {kind, radio, cause}
        log(fmt("step=shared_game_over;type=%d;cause=%d", kind, cause))
    end
    function R.failBothDowned()
        if not W.host or not on() or R.dead or type(R.originalReserve) ~= "function" then return false end
        local t = W.get(TppDefine, "GAME_OVER_TYPE")
        local r = W.get(TppDefine, "GAME_OVER_RADIO")
        local kind, radio = W.get(t, "PLAYER_DEAD"), W.get(r, "PLAYER_DEAD")
        if not valid(kind, 0, 255) or not valid(radio, 0, 255) then return false end
        local ok, result = W.follow.run(R.originalReserve, kind, radio)
        if ok and result == true then markDead(kind, radio, 3); return true end
        return false
    end
    local function mm(value, limit)
        if type(value) ~= "number" or value ~= value then return nil end
        local n = floor(value * 1000 + 0.5)
        return valid(n, -limit, limit) and n or nil
    end
    local function observeSave()
        if not on() or not W.host or R.dead then return end
        local h = hash(W.get(gvars, "mis_checkPoint"))
        if h == nil then return end
        local x, y, z, rot = 0, 0, 0, 0
        if h == 0 then
            x, y, z = mm(W.get(vars, "initialPlayerPosX"), 10000000), mm(W.get(vars, "initialPlayerPosY"), 10000000),
                mm(W.get(vars, "initialPlayerPosZ"), 10000000)
            rot = mm(W.get(vars, "initialPlayerRotY"), 360000)
            if not x or not y or not z or not rot then log("step=checkpoint_refused;reason=pose"); return end
        end
        R.version = R.version % 65535 + 1
        R.out = {h, R.version, x, y, z, rot}
    end
    local function checkpoint(v)
        if W.host or not on() or R.dead or type(R.originalCheckpoint) ~= "function" or
            not valid(v[1], 0, 4294967295) or not valid(v[2], 1, 65535) then return end
        for i = 3, 6 do if not valid(v[i], -(i == 6 and 360000 or 10000000), i == 6 and 360000 or 10000000) then return end end
        local delta = (v[2] - R.seen) % 65535
        if delta == 0 or delta > 32767 then return end
        if R.pending and (v[2] - R.pending[2]) % 65535 > 32767 then return end
        R.pending, R.retryAt, R.why = v, 0, nil
    end
    R.applyCheckpoint = checkpoint
    H[OP.CHECKPOINT] = checkpoint
    local function applyCheckpoint(now)
        local v = R.pending
        if not v or not ready() or now < (R.retryAt or 0) then return end
        local options = {ignoreAlert = true, permitHelicopter = true}
        if v[1] == 0 then
            if type(W.get(TppPlayer, "SetInitialPosition")) ~= "function" then return end
            options.atCurrentPosition = true
        else
            for k, name in pairs(W.get(mvars, "mis_checkPointList") or {}) do
                if type(k) == "string" and W.h(k) == v[1] then options.checkPoint = k; break end
                if type(name) == "string" and W.h(name) == v[1] then options.checkPoint = name; break end
            end
        end
        R.retryAt = now + 500
        if v[1] ~= 0 and not options.checkPoint then
            if R.why ~= "name" then log("step=checkpoint_wait;reason=unknown_name"); R.why = "name" end
            return
        end
        R.applying, R.receipt = v, false
        local ok = W.follow.run(R.originalCheckpoint, options)
        R.applying = nil
        if ok and R.receipt then
            R.seen, R.pending = v[2], nil
            local pose = ""
            if v[1] == 0 then
                local x, y, z = mm(W.get(vars, "initialPlayerPosX"), 10000000), mm(W.get(vars, "initialPlayerPosY"), 10000000),
                    mm(W.get(vars, "initialPlayerPosZ"), 10000000)
                local rot = mm(W.get(vars, "initialPlayerRotY"), 360000)
                pose = x and y and z and rot and fmt(";x_mm=%d;y_mm=%d;z_mm=%d;yaw_mdeg=%d", x, y, z, rot) or ";pose=unreadable"
            end
            log(fmt("step=checkpoint;hash=%.0f;ver=%d;receipt=1", v[1], v[2]) .. pose)
        elseif R.why ~= "save" then log("step=checkpoint_wait;reason=no_save_receipt"); R.why = "save" end
    end
    H[OP.REQ_GAME_OVER] = function(v)
        if not W.host or not on() or R.dead or not death(v[2]) then return end
        local rd = W.get(W.get(TppDefine, "GAME_OVER_RADIO"), "PLAYER_DEAD")
        if not valid(rd, 0, 255) or type(R.originalReserve) ~= "function" then return end
        local ok, result = W.follow.run(R.originalReserve, v[2], rd)
        if ok and result == true then markDead(v[2], rd, 2) end
    end
    local function applyGame(now)
        local v = R.inGame
        if not v or now < (R.gameAt or 0) then return end
        R.gameAt = now + 500
        local ok, result = W.follow.run(R.originalReserve, v[1], v[2])
        if ok and result == true then
            R.dead, A.dead, W.w1, A.tx, A.rx, R.pending, R.req, R.inGame = true, true, false, nil, nil, nil, nil, nil
            log(fmt("step=game_over_applied;type=%d;cause=%d", v[1], v[3]))
        end
    end
    H[OP.GAME_OVER] = function(v, now)
        if W.host or not on() or R.dead or not death(v[1]) or not valid(v[2], 0, 255) then return end
        if type(R.originalReserve) ~= "function" then return end
        if not R.inGame then R.inGame, R.gameAt = v, 0 end
        applyGame(now or W.now())
    end
    function R.install(cfg)
        R.enabled = cfg and cfg[1] >= 3 or false
        if R.installed then return end
        R.installed = true
        local reserve = W.get(TppMission, "ReserveGameOver")
        R.originalReserve = reserve
        if type(reserve) == "function" then
            TppMission.ReserveGameOver = function(kind, radio, aborting)
                if not on() or W.follow.token then return reserve(kind, radio, aborting) end
                if not W.host then
                    if death(kind) and not R.dead and not R.req then
                        local rd = W.get(W.get(TppDefine, "GAME_OVER_RADIO"), "PLAYER_DEAD")
                        if valid(rd, 0, 255) then R.req = {W.nextRid(), kind, rd, 1} end
                    end
                    return false
                end
                if death(kind) and R.dead then return true end
                local result = reserve(kind, radio, aborting)
                if result == true and death(kind) and valid(radio, 0, 255) then markDead(kind, radio, 1) end
                return result
            end
        end
        local update = W.get(TppCheckPoint, "Update")
        R.originalCheckpoint = update
        if type(update) == "function" then
            TppCheckPoint.Update = function(...)
                if on() and not W.host and not W.follow.token then return end
                return update(...)
            end
        end
        local save = W.get(TppMission, "VarSaveOnUpdateCheckPoint")
        if type(save) == "function" then
            local function saved(...)
                if (...) ~= false then
                    if R.applying and hash(W.get(gvars, "mis_checkPoint")) == R.applying[1] then R.receipt = true end
                    observeSave()
                end
                return ...
            end
            TppMission.VarSaveOnUpdateCheckPoint = function(...)
                local v = R.applying
                if v and v[1] == 0 and hash(W.get(gvars, "mis_checkPoint")) == 0 then
                    TppPlayer.SetInitialPosition({v[3] / 1000, v[4] / 1000, v[5] / 1000}, v[6] / 1000)
                end
                return saved(save(...))
            end
        end
        local B = W.probe and W.probe.handlers
        if B then
            local names = W.probe.NAMES
            if names then names[16], names[17], names[18] = "shared_death", "host_continue", "shared_checkpoint" end
            local function probe(pr, permitted, fn, receipt)
                local result = "refused"
                if W.mode == 1 and on() and permitted and type(fn) == "function" then
                    local ok, value = pcall(fn)
                    if ok and (not receipt or receipt(value)) then result = "ok" end
                end
                W.log("event=probe;id=" .. pr.name .. ";step=done;result=" .. result)
            end
            B.shared_death = function(pr)
                probe(pr, not R.dead, function()
                    local t, r = W.get(TppDefine, "GAME_OVER_TYPE"), W.get(TppDefine, "GAME_OVER_RADIO")
                    return TppMission.ReserveGameOver(t.PLAYER_DEAD, r.PLAYER_DEAD)
                end, function(v) return v == true or R.req ~= nil end)
            end
            B.host_continue = function(pr)
                probe(pr, W.host and R.dead, function() return TppMission.ContinueFromCheckPoint({isNoFade = true}) end)
            end
            B.shared_checkpoint = function(pr)
                local before = R.version
                probe(pr, W.host and ready() and not R.dead, function()
                    return TppMission.UpdateCheckPoint({atCurrentPosition = true, ignoreAlert = true, permitHelicopter = true})
                end, function() return R.version ~= before end)
            end
        end
    end
    W.startHooks[#W.startHooks + 1] = function()
        R.dead, R.game, R.req, R.out, R.pending, R.applying, R.version, R.seen = false, nil, nil, nil, nil, nil, 0, 0
        R.inGame, R.reqAt, R.gameAt = nil, 0, 0
    end
    W.unpairedHooks[#W.unpairedHooks + 1] = function() R.game, R.req, R.out, R.pending, R.inGame = nil, nil, nil, nil, nil end
    function R.tick(now)
        if not on() then return end
        if R.req and now >= (R.reqAt or 0) and W.tx(OP.REQ_GAME_OVER, unpack(R.req)) then R.reqAt = now + 500 end
        if R.game and W.tx(OP.GAME_OVER, unpack(R.game)) then R.game = nil end
        if R.inGame then applyGame(now) end
        if R.out and ready() and W.tx(OP.CHECKPOINT, unpack(R.out)) then
            local v = R.out
            log(fmt("step=checkpoint_tx;hash=%.0f;ver=%d;x_mm=%d;y_mm=%d;z_mm=%d;yaw_mdeg=%d", unpack(v)))
            R.out = nil
        end
        if not R.dead then applyCheckpoint(now) end
    end
    W.follow.installHooks[#W.follow.installHooks + 1] = R.install
end
