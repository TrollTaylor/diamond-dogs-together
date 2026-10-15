-- Original offline W3 travel; Lua-thread APIs only. W4 alignment is separate.
do
local W = e.MgsCoopWorld
local F, OP, H = {queue = {}, seeds = {}, serial = 0, last = 0, token = false, installHooks = {}}, W.OP, W.handlers
W.follow = F
local floor, fmt = math.floor, string.format
local SUPPORTED = {[30010] = 10, [30020] = 20, [30050] = 50, [40010] = 10, [40020] = 20, [40050] = 50}
local S = W.followSeeds
local KEYS = S.keys
local function log(text) W.log("event=follow;" .. text) end
-- On-screen notes and the partner's scene (W.note/W.peer); both absent on an older loader.
local function note(code, a, b) if type(W.note) == "function" then pcall(W.note, code, a or 0, b or 0) end end
local function peer()
if type(W.peer) ~= "function" then return nil end
local ok, p = pcall(W.peer)
return ok and type(p) == "table" and p or nil
end
local function reject(why) F.enabled = false; F.clear(); W.fail(why) end
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
if not t or t[4] ~= nextMission or t.sent then return true end
if t.values then F.pump(); return true end
local values, digest = S.snapshot()
if not values then
    if t.missing ~= digest then log("step=refused;reason=unreadable_seed;key=" .. digest); t.missing = digest end
    return false
end
t.values, t.digest, t.i = values, digest, 1
F.pump()
return true
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
v.session = F.session()
F.queue[#F.queue + 1] = v
log(fmt("step=queued;id=%.0f;src=%d;next=%d;kind=%d", v[1], v[3], v[4], v[2]))
end
H[OP.SEED] = function(v)
if W.host or not F.enabled or v[1] <= F.last then return end
local key = KEYS[v[3]]
if not key then reject("follow_bad_seed"); return end
local s = F.seeds[v[1]]
if not s then s = {mission = v[2], values = {}, digest = 0}; F.seeds[v[1]] = s end
if s.mission ~= v[2] or #s.values >= 256 then reject("follow_seed_overflow"); return end
if s.ready or not S.accept(s, v[3], v[4], v[5]) then reject("follow_bad_seed"); return end
end
H[OP.SEEDS_END] = function(v)
if W.host or not F.enabled or v[1] <= F.last then return end
local s = F.seeds[v[1]]
if not s then s = {mission = v[2], values = {}, digest = 0}; F.seeds[v[1]] = s end
if s.mission ~= v[2] or #s.values ~= v[3] or s.digest ~= v[4] or not S.complete(s) then reject("follow_seed_checksum"); return end
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
if W.host and F.outgoing and v[1] == F.outgoing[1] then F.outgoing.ack = v[3] end
end
function F.publish(kind)
if not on() or not W.host or not W.paired or W.eff < 3 then return end
local session = F.session()
if not session then log("step=refused;reason=link_session"); return end
local target = vars.missionCode
if not SUPPORTED[target] then log("step=refused;reason=unsupported_mission"); return end
F.serial = F.serial % 4294967295 + 1
local t = {F.serial, F.deploying and 6 or kind, W.mission, target,
    W.get(svars, "mis_missionClearType") or 0, hash(W.get(gvars, "heli_missionStartRoute")) or 0,
    hash(W.get(vars, "mbLayoutCode")) or 0, W.get(vars, "mbClusterId") or 0,
    W.get(mvars, "mis_selectedDeployTime") or 0,
    (F.abortNoSave or (kind == 2 and W.get(mvars, "mis_abortWithSave") == false)) and 1 or 0, W.epoch % 65535 + 1}
F.outgoing = t
t.session = session
F.pump()
F.deploying, F.abortNoSave = false, false
end
function F.applySeeds(t)
local s = F.seeds[t[1]]
if not s or not s.ready or s.mission ~= t[4] then return false end
for _, v in ipairs(s.values) do
    local key, value = KEYS[v[1]], S.decode(KEYS[v[1]], v[3])
    if not key[5] then
    local data = S.data(key)
    if key[2] then
        local array = W.get(data, key[1])
        if array ~= nil then array[v[2]] = value end
    else data[key[1]] = value end
    end
end
vars.missionCode, vars.locationCode = t[4], SUPPORTED[t[4]]
vars.mbLayoutCode, vars.mbClusterId = t[7], t[8]
log(fmt("step=seeds_apply;id=%.0f;mission=%d;n=%d", t[1], t[4], #s.values))
return true
end
-- F.hostDeploy is not cleared here: F.tick owns the held sortie.
function F.clear()
F.queue, F.seeds, F.current, F.armed, F.last = {}, {}, nil, false, 0
F.outgoing = nil
end
function F.session()
if type(W.cfg) ~= "function" then return nil end
local r = W.cfg(vars.missionCode, 0)
if r[1] >= 3 and r[2] == W.role and r[4] == 1 and type(r[8]) == "number" and r[8] > 0 then return r[8] end
end
-- The guest is still replaying the host's last transition into this mission
-- (no FOLLOW_ACK refused/arrived yet, same session, partner in its source or loading).
function F.following(p)
local t = F.outgoing
return t ~= nil and t[4] == W.mission and t.ack ~= 0 and t.ack ~= 2 and t.session ~= nil and t.session == p.session and
    (p.mission == 0 or p.mission == t[3])
end
-- nil: a partner can come (or an older loader: the legacy hold); 1: nobody can come;
-- 2: the partner is in another mission. The peer view is the second result.
function F.alone()
if not F.session() then return 1 end
local p = peer()
if p == nil then return nil end
if not p.linked or p.level < 3 then return 1, p end
if p.mission ~= 0 and p.mission ~= W.mission and not F.following(p) then return 2, p end
return nil, p
end
local ALONE = {"no_partner", "partner_elsewhere", "repeat", "timeout"}
-- A sortie that leaves without the partner: log, note, and no kind-6 follow.
local function solo(why, target)
F.deploying = false
log(fmt("step=deploy_alone;reason=%s;target=%s", ALONE[why] or "disabled", tostring(target or 0)))
note(3, target or 0, why)
end
-- An unsupported or emergency destination: refused while a partner could
-- come, allowed when chosen again 1-30 s later or when nobody can come.
-- True: refused (line logged; the note is debounced for 1 s).
local function refuse(target, line)
local now, r = W.now(), F.refused
local why = not W.paired and F.alone() or nil
local rep = r ~= nil and r.target == target and r.mission == W.mission and now - r.at >= 1000 and now - r.at <= 30000
if why == nil and not rep then
    log(line)
    if not (r and r.target == target and now - r.at < 1000) then
        F.refused = {target = target, mission = W.mission, at = now}
        note(4, target or 0, 0)
    end
    return true
end
F.refused = nil
solo(rep and 3 or why, target)
return false
end
function F.configured()
if not F.enabled or W.level < 3 or W.failed or (W.paired and W.eff < 3) then return false end
if type(W.cfg) == "function" then
    local r = W.cfg(vars.missionCode, 0)
    if r[1] < 3 or r[2] ~= W.role then F.enabled = false; F.clear(); return false end
    local t = W.host and F.outgoing or F.current
    if t and (r[4] ~= 1 or not r[8] or r[8] == 0 or r[8] ~= t.session) then F.clear(); return false end
end
return true
end
function F.prepare()
if W.host or not F.current or not F.configured() then return true end
local ok, ready = pcall(F.applySeeds, F.current)
if not ok then W.fail(ready) end
return ok and ready
end
-- The held host sortie: released when paired, when nobody can come, after 120 s,
-- or when world sync stops (LEVEL 0, W.fail: world_sync calls this while inactive).
function F.hold(now)
local hd = F.hostDeploy
if not hd then return end
now = now or W.now()
if hd.mission ~= W.mission or not W.host then
    F.hostDeploy = nil
    log(fmt("step=deploy_held;reason=stale;mission=%s", tostring(hd.mission)))
elseif W.paired or not F.enabled or not W.active then
    F.hostDeploy = nil
    local live = W.active and F.enabled
    F.deploying = live and W.paired and W.eff >= 3 or false
    if not live then log(fmt("step=deploy_alone;reason=disabled;target=%s", tostring(hd.target))) end
    run(F.originalDeploy, unpack(hd, 1, hd.n))
elseif now - hd.checkAt >= 500 then
    hd.checkAt = now
    local why = F.alone() or (now - hd.at >= 120000 and 4 or nil)
    if why then
        F.hostDeploy = nil
        solo(why, hd.target)
        run(F.originalDeploy, unpack(hd, 1, hd.n))
    elseif now - hd.notedAt >= 3000 then
        hd.notedAt = now; note(2, hd.target, 0)
    end
end
end
function F.tick(now)
-- Title helicopter with a story-start save: repeat the note every 15 s.
local T = F.title
if T and T.story and now - T.at >= 15000 then
    if W.get(gvars, "ini_isTitleMode") ~= true then F.title = nil else T.at = now; note(1, T.mission, 1) end
end
F.hold(now)
if not on() then return end
if W.host then F.pump(); return end
-- Sticky arm: an idle armed guest disarms after 120 s unpaired, or at once
-- when the partner's mission is unsupported (checked once a second).
if F.armed and not W.paired and #F.queue == 0 and not F.current and now - (F.disarmAt or -1e12) >= 1000 then
    F.disarmAt = now
    local p = peer()
    local gone = p and p.mission ~= 0 and not SUPPORTED[p.mission]
    if gone or now - (F.unpairedAt or now) >= 120000 then
        F.armed = false
        log(fmt("step=disarm;reason=%s;partner=%s", gone and "partner_mission" or "unpaired_timeout", p and tostring(p.mission) or "nil"))
    end
end
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
local session = F.session()
if not session then return end
if t.session and t.session ~= session then F.clear(); log("step=refused;reason=link_session"); return end
t.session = session
F.current = t
S.context(seeds, t[1])
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
-- Title progress: a title-mode helicopter whose save is at the story start
-- (CLEARD_ESCAPE_THE_HOSPITAL: Continue goes to a story mission). nil: unknown.
local function storyStart()
local ok, s = pcall(function()
    local hospital = TppDefine.STORY_SEQUENCE.CLEARD_ESCAPE_THE_HOSPITAL
    if type(hospital) ~= "number" then return nil end
    return TppStory.GetCurrentStorySequence() == hospital
end)
if ok then return s end
end
W.startHooks[#W.startHooks + 1] = function()
F.enabled = W.level >= 3 and SUPPORTED[W.mission] ~= nil
F.reqAt = nil
F.hostDeploy, F.refused, F.disarmAt = nil, nil, nil
if F.armed then F.unpairedAt = W.now() end
F.title = nil
if floor(W.mission / 10000) == 4 and W.get(gvars, "ini_isTitleMode") == true then
    local s = storyStart()
    if s ~= nil then
        F.title = {mission = W.mission, story = s, at = W.now()}
        note(1, W.mission, s and 1 or 0)
        log(fmt("step=progress;title=%d;story_start=%d", W.mission, s and 1 or 0))
    end
end
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
W.pairedHooks[#W.pairedHooks + 1] = function() F.armed = F.enabled and W.eff >= 3; F.unpairedAt = nil end
W.unpairedHooks[#W.unpairedHooks + 1] = function(reason)
F.unpairedAt = W.now()
if reason == 1 or reason == 5 then
    F.clear()
    if reason == 5 then F.enabled = false end
end
end
W.stopHooks[#W.stopHooks + 1] = function(final)
if final and not W.active then F.armed = false end
end
function F.install(cfg)
F.enabled = cfg[1] >= 3 and SUPPORTED[vars.missionCode] ~= nil
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
            if on() and W.host and not F.pass and (name == "ReserveMissionClear" or name == "AbortMission") then
                local args = (...)
                if type(args) == "table" and args.nextMissionId and not SUPPORTED[args.nextMissionId] and
                    refuse(args.nextMissionId, "step=refused;reason=unsupported_mission") then
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
local vanillaDeploy = W.get(TppMission, "OnEndMissionPreparation")
if type(vanillaDeploy) == "function" then
    -- Every path that runs the vanilla deploy has already allowed this sortie:
    -- its own ReserveMissionClear (GoToEmergencyMission's too) passes the
    -- unsupported-destination refusal (F.pass, restored afterwards).
    local function passed(saved, ok, ...)
        F.pass = saved
        if not ok then error((...), 0) end
        return ...
    end
    F.originalDeploy = function(...)
        local saved = F.pass
        F.pass = true
        return passed(saved, pcall(vanillaDeploy, ...))
    end
    TppMission.OnEndMissionPreparation = function(...)
        if guest() then F.request(6); return end
        if on() and W.host then
            local now, hd = W.now(), F.hostDeploy
            local target, emergency = W.get(gvars, "mis_nextMissionCodeForMissionClear"), W.get(gvars, "mis_nextMissionCodeForEmergency")
            -- Chosen again while held (1 s debounce): fly alone with these arguments.
            if hd and hd.mission == W.mission then
                if now - hd.at < 1000 then return end
                F.hostDeploy = nil
                solo(3, target)
                return F.originalDeploy(...)
            end
            if (target and not SUPPORTED[target]) or (emergency and emergency ~= 0) then
                if refuse(target, fmt("step=refused;reason=unsupported_mission;target=%s;emergency=%s;fn=OnEndMissionPreparation",
                    tostring(target), tostring(emergency))) then return end
                return F.originalDeploy(...)
            end
            local why, p
            if not W.paired then
                why, p = F.alone()
                if why then solo(why, target); return F.originalDeploy(...) end
            end
            if not W.paired or W.eff < 3 then
                F.hostDeploy = {n = select("#", ...), mission = W.mission, target = target or 0, at = now, checkAt = now, notedAt = now, ...}
                log(fmt("step=deploy_wait;reason=ready;target=%s;partner=%s", tostring(target or 0), p and tostring(p.mission) or "nil"))
                if not W.paired then note(2, target or 0, 0) end
                return
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
        if k > 0 and not F.prepare() then return end
        if k > 0 then local ok, err = pcall(F.publish, k); if not ok then W.fail(err) end end
        return reserve(kind, ...)
    end
end
local function pack(...) return {n = select("#", ...), ...} end
for _, name in ipairs({"VarRestoreOnContinueFromCheckPoint", "VarRestoreOnMissionStart"}) do
    local restore = W.get(TppSave, name)
    if type(restore) == "function" then
        TppSave[name] = function(...)
            local r = pack(restore(...))
            F.prepare()
            return unpack(r, 1, r.n)
        end
    end
end
local stage = W.get(e, "StageBlockCurrentPosition")
if type(stage) == "function" then
    e.StageBlockCurrentPosition = function(...)
        if not F.prepare() then return end
        return stage(...)
    end
end
local request = W.get(TppMission, "RequestLoad")
if type(request) == "function" then
    TppMission.RequestLoad = function(nextMission, ...)
        if not F.prepare() then return end
        if on() and not W.host and F.current then nextMission = F.current[4] end
        return request(nextMission, ...)
    end
end
local load, loadCheck = W.get(TppMission, "Load"), W.get(TppMission, "LoadWithChunkCheck")
if type(load) == "function" then
    TppMission.Load = function(nextMission, ...)
        if on() and W.host then
            local ok, ready = pcall(F.sendSeeds, nextMission)
            if not ok then W.fail(ready); return end
            if not ready then return end
        end
        if on() and not W.host and F.current then
            local ok, applied = pcall(F.applySeeds, F.current)
            if not ok then W.fail(applied); return end
            if not applied then log("step=refused;reason=missing_seeds"); return end
            nextMission = F.current[4]
        end
        return load(nextMission, ...)
    end
end
if type(loadCheck) == "function" then
    TppMission.LoadWithChunkCheck = function(...)
        if on() and W.host then
            local req = W.get(mvars, "mis_loadRequest")
            if req then
                local ok, ready = pcall(F.sendSeeds, req.nextMission)
                if not ok then W.fail(ready); return end
                if not ready then return end
            end
        end
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
        local t = W.host and F.outgoing or F.current
        if t and F.configured() and vars.missionCode == t[4] then math.randomseed((t[1] + t[6]) % 2147483647) end
        return allocate(...)
    end
end
end
end
