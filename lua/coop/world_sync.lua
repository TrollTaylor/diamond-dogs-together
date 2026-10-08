-- MGS co-op world sync W0-W2 plus W3/W4. MGS_COOP_WORLD_CFG_V1 literals stay inert.
-- W.n formats hashes via %.0f; receipts <256 bytes. Companion chunks extend W.
-- Errors stop sync for this mission while original callbacks keep running.
do
    local OP = {HELLO = 1, READY = 2, RECEIVED = 3, REJECT = 4, RESYNC = 5, RESYNC_END = 6, MISSION_END = 7, PROBE_SYNC = 8,
        OUTCOME = 16, REQ_OUTCOME = 17, PHASE = 18, REQ_PHASE = 19, AREA = 20, DIGEST = 21, GDIGEST = 22, PVEC = 23,
        CLOCK = 32, WEATHER = 33, LINK = 112, PAIRED = 113, UNPAIRED = 114, GOTO = 115, PROBE = 116, PROBE_FIRE = 117,
        RESYNC_NEEDED = 118, LEVEL = 119, GOTO_ACK = 120,
        TRANSITION = 9, SEED = 10, SEEDS_END = 11, REQ_MISSION = 12, FOLLOW_ACK = 13,
        ALIGN_BEGIN = 14, ALIGN_END = 15, ALIGN_RECORD = 24, ALIGN_ACK = 25,
        GAME_OVER = 26, REQ_GAME_OVER = 27, CHECKPOINT = 28}
    local W = {OP = OP, handlers = {}, bindHooks = {}, startHooks = {}, stopHooks = {}, pairedHooks = {}, unpairedHooks = {}, observers = {},
        areaState = {}, areaWrapped = setmetatable({}, {__mode = "k"})}
    e.MgsCoopWorld = W
    W.MISSIONS = {[30010] = {enemies = true, clock = true, weather = "all", areaEnemy = "f30010_enemy"},
        [30020] = {enemies = true, clock = true, weather = "nofog", areaEnemy = "f30020_enemy"},
        [30050] = {clock = true}}
    local fmt, floor, sub, gsub, concat = string.format, math.floor, string.sub, string.gsub, table.concat
    local PRE = "return 'MGS_COOP_WORLD_"
    local function index(t, k) return t[k] end
    function W.get(t, k)
        if t == nil then return nil end
        local ok, v = pcall(index, t, k)
        if ok then return v end
        return nil
    end
    function W.receipt(source)
        local chunk = loadstring(source)
        if type(chunk) ~= "function" then return nil end
        return chunk()
    end
    function W.log(text)
        W.receipt(PRE .. "LOG_V1' -- " .. sub(gsub(tostring(text), "[^%w_=:,.;%-]", "_"), 1, 160))
    end
    function W.now() return W.api.time() * 1000 end
    function W.tick32(now) return floor(now) % 4294967296 end
    -- An integer for a receipt, or nil (the op is then not sent).
    function W.n(v)
        if type(v) ~= "number" or v ~= v or v >= 4294967296 or v < -2147483648 or v ~= floor(v) then return nil end
        if v == 0 then return "0" end
        return fmt("%.0f", v)
    end
    function W.badValue(op, field)
        W.stats.bad_value = W.stats.bad_value + 1
        local now = W.now()
        if not W.badAt or now - W.badAt >= 10000 then
            W.badAt = now
            W.log(fmt("event=bad_value;op=%d;field=%d", op, field))
        end
    end
    -- TX: true, or false and the native reason (-1: a bad value, not sent).
    function W.tx(op, ...)
        local parts = {tostring(op)}
        for i = 1, select("#", ...) do
            local text = W.n((select(i, ...)))
            if not text then W.badValue(op, i); return false, -1 end
            parts[i + 1] = text
        end
        local ok, reason = W.receipt(PRE .. "TX_V1' -- " .. concat(parts, " "))
        if ok == 1 then return true end
        return false, reason
    end
    -- Fox.StrCode32 as an unsigned wire hash.
    function W.h(name)
        local v = W.api.str32(name)
        if v < 0 then v = v + 4294967296 end
        return v
    end
    function W.cfg(mission, phase)
        local text = W.n(mission)
        if not text then return {0} end
        local r = {W.receipt(PRE .. "CFG_V1' -- " .. text .. " " .. phase)}
        if type(r[1]) ~= "number" then return {0} end
        return r
    end
    function W.inGame() return W.api.checkState() == true end
    -- Resolves the bindings once per mission; returns the missing names and
    -- whether the required ones exist. W.bindHooks (world_enemy/env.lua) add
    -- the W1/W2 groups; a missing one switches only that phase off.
    function W.bind()
        local missing, a = {}, {}
        local function want(kind, t, name, key, opt)
            local v = W.get(t, key)
            if type(v) == kind then return v end
            if not opt then missing[#missing + 1] = name .. "." .. key end
            return nil
        end
        local function fn(...) return want("function", ...) end
        local function num(...) return want("number", ...) end
        a.str32 = fn(Fox, "Fox", "StrCode32")
        a.time = fn(Time, "Time", "GetRawElapsedTimeSinceStartUp")
        a.send = fn(GameObject, "GameObject", "SendCommand")
        a.id = fn(GameObject, "GameObject", "GetGameObjectId")
        a.null = num(GameObject, "GameObject", "NULL_ID")
        a.resend = W.get(Mission, "ON_MESSAGE_RESULT_RESEND")
        a.checkState = fn(TppMission, "TppMission", "CheckMissionState")
        a.isStart = fn(TppMission, "TppMission", "IsMissionStart")
        local required = #missing == 0 and vars ~= nil and mvars ~= nil and svars ~= nil and loadstring ~= nil
        if a.resend == nil then missing[#missing + 1] = "Mission.ON_MESSAGE_RESULT_RESEND" end
        W.api = a
        for _, f in ipairs(W.bindHooks) do f(a, fn, num, missing) end
        return concat(missing, ","), required
    end

    -- Maps and roster digest: CPs = sorted string keys of mvars.ene_soldierDefine,
    -- soldiers = the sorted ipairs part of each (lrrpVehicle etc. are skipped).
    function W.buildMaps()
        W.ent, W.byId, W.cp, W.cpById, W.cpOrder, W.entOrder = {}, {}, {}, {}, {}, {}
        W.roster, W.rosterOk = 0, true
        local def, names = W.get(mvars, "ene_soldierDefine"), {}
        if type(def) ~= "table" then return end
        for k, v in pairs(def) do if type(k) == "string" and type(v) == "table" then names[#names + 1] = k end end
        table.sort(names)
        local d, send = 0, W.api.id
        for idx, name in ipairs(names) do
            local h = W.h(name)
            if W.cp[h] then W.rosterOk = false end
            local id = send(name)
            W.cp[h] = {name = name, id = id, idx = idx, localPhase = 0, hostPhase = 0, own = 0, guestOwn = 0, ver = 0}
            if type(id) == "number" then W.cpById[id] = h end
            W.cpOrder[idx] = h
            d = (d * 31 + h) % 4294967296
            local soldiers = {}
            for _, s in ipairs(def[name]) do if type(s) == "string" then soldiers[#soldiers + 1] = s end end
            table.sort(soldiers)
            for _, s in ipairs(soldiers) do
                local sh = W.h(s)
                if W.ent[sh] then W.rosterOk = false end
                local sid = send("TppSoldier2", s)
                W.ent[sh] = {name = s, id = sid, cp = h, committed = 0, ver = 0, known = 0, kver = 0, reached = false}
                if type(sid) == "number" then W.byId[sid] = sh end
                W.entOrder[#W.entOrder + 1] = sh
                d = (d * 31 + sh) % 4294967296
            end
        end
        W.roster = d
        if not W.rosterOk then W.log("event=roster;dup=1") end
    end
    function W.txHello()
        return W.tx(OP.HELLO, W.mission, W.get(vars, "locationCode") or 0, W.roster, #W.entOrder, #W.cpOrder)
    end
    function W.txReady() return W.tx(OP.READY, W.mission, W.roster, #W.entOrder, #W.cpOrder) end
    function W.nextRid()
        W.rid = W.rid % 65535 + 1
        return W.rid
    end
    function W.w1off(reason)
        if not W.w1 then return end
        W.w1 = false
        W.log("event=inactive;reason=" .. reason .. ";scope=w1")
        for _, f in ipairs(W.stopHooks) do f(false) end
    end

    function W.reset()
        W.active, W.observing, W.isStarted, W.failed, W.paired, W.w1 = false, false, false, false, false, false
        W.eff, W.epoch, W.rid, W.flags, W.obsCount, W.badAt = 0, 0, 0, 0, 0, nil
        W.attribOverride, W.pr, W.gt = {}, nil, nil
        W.stats = {commits = 0, reqs = 0, applies = 0, noops = 0, ledger = 0, div = 0, obs = 0, resend = 0, unmapped = 0, bad_value = 0}
    end
    W.reset()
    function W.begin()
        W.reset()
        local mission = vars.missionCode
        local r = W.cfg(mission, 1)
        if r[1] < 1 then return end
        W.level, W.role, W.epoch, W.probeId, W.mode = r[1], r[2], r[5] or 0, r[6] or 0, r[7] or 0
        W.host = W.role == 1
        W.mission, W.feature = mission, W.MISSIONS[mission] or {}
        local missing, required = W.bind()
        if not required then W.log("event=inactive;reason=missing_required;names=" .. missing); return end
        W.isStarted, W.active, W.observing = true, true, W.api.resend ~= nil
        W.t0, W.statsAt = W.now(), W.now()
        if missing ~= "" then W.log("event=missing;names=" .. sub(missing, 1, 140)) end
        -- Raw Fox.StrCode32 values (never W.h) for the message comparison.
        local M = {GameObject = W.api.str32("GameObject"), Weather = W.api.str32("Weather"), ChangeWeather = W.api.str32("ChangeWeather")}
        W.M, W.kind = M, {}
        for _, k in ipairs({"Dead", "Neutralize", "Unconscious", "Dying", "Conscious", "Fulton", "FultonInfo", "FultonFailed",
            "Holdup", "ChangePhase", "Damage"}) do M[k] = W.api.str32(k); W.kind[M[k]] = k end
        W.buildMaps()
        W.w1 = W.level >= 2 and W.feature.enemies == true and W.w1ok and W.rosterOk
        for _, f in ipairs(W.startHooks) do f() end
        if W.w1 and W.api.isStart() ~= true then W.w1off("continue") end
        W.txHello()
        W.txReady()
        if W.probeId > 0 and W.probe then W.probe.schedule(W.probeId, W.t0 + 10000) end
        W.log(fmt("event=started;level=%d;role=%d;mission=%d;soldiers=%d;cps=%d;roster=%.0f;w1=%d", W.level, W.role, mission,
            #W.entOrder, #W.cpOrder, W.roster, W.w1 and 1 or 0))
    end
    function W.fail(err)
        W.active, W.observing = false, false
        if W.failed then return end
        W.failed = true
        pcall(W.log, "event=error;msg=" .. sub(tostring(err), 1, 120))
    end
    -- Stop cleanup: runs at OnTerminate even after W.fail, and on LEVEL 0
    -- (without MISSION_END). Weather forces are released, random weather unpaused.
    function W.stop(final)
        if final and W.isStarted then pcall(W.tx, OP.MISSION_END, W.mission) end
        for _, f in ipairs(W.stopHooks) do pcall(f, true) end
        if W.isStarted then pcall(W.log, final and "event=stop" or "event=inactive;reason=level0") end
        W.active, W.observing = false, false
        if final then W.isStarted = false end
    end

    -- Control dispatch (popped first by RX).
    local H = W.handlers
    H[OP.LINK] = function(v)
        if v[1] == 1 and W.isStarted then W.txHello(); W.txReady() end
    end
    H[OP.PAIRED] = function(v)
        local epochChanged = W.epoch ~= v[1]
        W.paired, W.epoch, W.eff = true, v[1], v[6]
        if v[5] ~= 1 then W.w1off("roster_mismatch") end
        W.log(fmt("event=paired;epoch=%d;eff=%d;match=%d;remote_roster=%.0f;remote_soldiers=%d", v[1], v[6], v[5], v[3], v[4]))
        for _, f in ipairs(W.pairedHooks) do f(epochChanged) end
    end
    H[OP.UNPAIRED] = function(v)
        W.paired, W.eff = false, 0
        W.log("event=unpaired;reason=" .. tostring(v[1]))
        for _, f in ipairs(W.unpairedHooks) do f(v[1]) end
    end
    H[OP.LEVEL] = function() W.stop(false) end
    -- One RX receipt per idle frame: control first, then (paired, in game)
    -- the world queue and the U slots. At most 16 pops per frame.
    function W.drain(now)
        local mask = (W.paired and W.eff >= 2 and W.inGame()) and 3 or 1
        for _ = 1, 16 do
            local r = {W.receipt(PRE .. "RX_V1' -- " .. mask)}
            if type(r[1]) ~= "number" or type(r[2]) ~= "number" then return end
            if r[2] == 0 then W.flags = r[3] or 0; return end
            local v = {}
            for i = 3, #r do v[i - 2] = r[i] end
            local handler = H[r[2]]
            if handler then handler(v, now) end
            if not W.active then return end
        end
    end
    function W.tick()
        local now = W.now()
        W.drain(now)
        if not W.active then return end
        if W.enemy then W.enemy.tick(now) end
        if W.phase then W.phase.tick(now) end
        if W.env then W.env.tick(now) end
        if W.probe then W.probe.tick(now) end
        if W.gt and W.go then W.go.tick(now) end
        if W.follow then W.follow.tick(now) end
        if W.align then W.align.tick(now) end
        if W.place then W.place.tick(now) end
    end

    -- Observer (after the original; records only, never sends).
    function W.observe(class, msg, a0, a1, a2)
        local M = W.M
        if class == M.Weather then
            if msg == M.ChangeWeather then W.weatherDirty = true end
            return
        end
        local kind = class == M.GameObject and W.kind[msg]
        if not kind then return end
        local h, att = W.byId[a0], nil
        if kind == "ChangePhase" then h = W.cpById[a0] end
        if kind == "Dead" or kind == "Neutralize" or kind == "FultonInfo" then att = a1 elseif kind == "Damage" then att = a2 end
        local class2 = W.attClass and W.attClass(att) or "none"
        if not h then W.stats.unmapped = W.stats.unmapped + 1 end
        if kind ~= "Damage" then
            W.stats.obs = W.stats.obs + 1
            W.obsCount = W.obsCount + 1
            if W.obsCount <= 4000 then
                W.log(fmt("event=obs;m=%s;ent=%.0f;id=%s;att=%s", kind, h or 0, tostring(a0), class2))
            elseif W.obsCount == 4001 then
                W.log("event=obs_cap")
            end
        end
        if not h then return end
        if kind == "ChangePhase" then
            if W.phase then W.phase.onLocal(h, a1) end
        elseif W.enemy then
            W.enemy.observe(kind, h, class2, a2)
        end
        for _, f in ipairs(W.observers) do f(kind, h, class2) end
    end

    local initialize, start, update, message, terminate = e.OnInitialize, e.OnMissionGameStart, e.OnUpdate, e.OnMessage, e.OnTerminate
    function W.init(subScripts)
        W.areaState = {}
        local cfg = W.cfg(vars.missionCode, 0)
        local level = cfg[1]
        if level >= 3 and W.follow then W.follow.install(cfg) end
        local feature = W.MISSIONS[vars.missionCode]
        if level >= 2 and feature and feature.areaEnemy and W.area then W.area.install(subScripts, feature.areaEnemy) end
    end
    local function started(...)
        local ok, err = pcall(W.begin)
        if not ok then W.fail(err) end
        return ...
    end
    local function updated(...)
        if W.active then
            local ok, err = pcall(W.tick)
            if not ok then W.fail(err) end
        end
        return ...
    end
    local function after(class, msg, a0, a1, a2, a3, ...)
        if W.observing then
            if select("#", ...) > 0 and (...) == W.api.resend then W.stats.resend = W.stats.resend + 1
            else
                local ok, err = pcall(W.observe, class, msg, a0, a1, a2, a3)
                if not ok then W.fail(err) end
            end
        end
        return ...
    end
    if type(initialize) == "function" then
        -- Install before Init captures menu closures.
        e.OnInitialize = function(subScripts, ...)
            pcall(W.init, subScripts)
            return initialize(subScripts, ...)
        end
    end
    if type(start) == "function" then e.OnMissionGameStart = function(...) return started(start(...)) end end
    if type(update) == "function" then e.OnUpdate = function(...) return updated(update(...)) end end
    if type(message) == "function" then
        e.OnMessage = function(s, class, msg, a0, a1, a2, a3, ...)
            return after(class, msg, a0, a1, a2, a3, message(s, class, msg, a0, a1, a2, a3, ...))
        end
    end
    if type(terminate) == "function" then e.OnTerminate = function(...) pcall(W.stop, true); return terminate(...) end end
end
