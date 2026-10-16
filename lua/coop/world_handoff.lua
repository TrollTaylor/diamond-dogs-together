-- Join-fix phase 2 (docs/join-fix-phase2-spec.md C1-C6): the SAVE report for the
-- native save handoff, the guest travel lock (inner wrappers below world_follow's
-- armed-guest suppression), travel warnings and the title replay hold. A title
-- Continue is gated at the title menu itself (title_sequence Seq_Game_TitleMenu):
-- the game's title Continue never calls TppMission's Continue functions.
do
local W = e.MgsCoopWorld
local F = W.follow
local X = {esc = {}, warned = {}}
W.handoff = X
local floor, fmt = math.floor, string.format
local SUPPORTED = {[30010] = 10, [30020] = 20, [30050] = 50, [40010] = 10, [40020] = 20, [40050] = 50}
local KIND = {ReserveMissionClear = 1, AbortMission = 2, AbortMissionByMenu = 2, ContinueFromCheckPoint = 4,
    ExecuteContinueFromCheckPoint = 4, OnEndMissionPreparation = 6}
local DEPLOY = "OnEndMissionPreparation"
local function log(text) W.log("event=handoff;" .. text) end
local function note(code, a, b) if type(W.note) == "function" then pcall(W.note, code, a or 0, b or 0) end end
local function num(v) return type(v) == "number" and v == v and v or nil end
local function peer()
if type(W.peer) ~= "function" then return nil end
local ok, p = pcall(W.peer)
return ok and type(p) == "table" and p or nil
end
local function line(fn, kind, target, host, decision)
log(fmt("step=lock;fn=%s;kind=%d;target=%d;host=%d;decision=%s", fn, kind, target or 0, host or 0, decision))
end
function X.title() return W.get(gvars, "ini_isTitleMode") == true and floor((W.mission or 0) / 10000) == 4 end
-- 0 unknown (unreadable or unordered constants), 1 story opening, 2 open (Mother Base reached).
function X.story()
local d = W.get(TppDefine, "STORY_SEQUENCE")
local q, a, b, c = num(W.get(gvars, "str_storySequence")), num(W.get(d, "CLEARD_ESCAPE_THE_HOSPITAL")),
    num(W.get(d, "CLEARD_RECUE_MILLER")), num(W.get(d, "CLEARD_TO_MATHER_BASE"))
if not (q and a and b and c and a < b and b < c) then return 0 end
return q >= c and 2 or 1
end
function X.saving()
local f = W.get(TppScriptVars, "IsSavingOrLoading")
if type(f) ~= "function" then return false end
local ok, v = pcall(f)
return ok and v == true
end
function X.ingame() return W.active and not X.title() and not X.leaving and W.inGame() == true end
-- CFG r[12] (handoff code) and r[13] (lock); nil reads as 0. maxAge: reuse a read that recent.
function X.code(maxAge)
local now = W.now()
local age = X.codeAt and now - X.codeAt
if not (maxAge and age and age >= 0 and age < maxAge) then
    local r = type(W.cfg) == "function" and W.cfg(W.get(vars, "missionCode"), 0) or {}
    X.r12, X.r13, X.codeAt = num(r[12]) or 0, num(r[13]) or 0, now
end
return X.r12, X.r13
end
-- SAVE_V1: six unsigned integers; nothing is sent when one is invalid.
function X.save(...)
if type(W.n) ~= "function" or type(W.receipt) ~= "function" then return false end
local parts = {}
for i = 1, 6 do
    local v = select(i, ...)
    local s = W.n(v)
    if not s or v < 0 then return false end
    parts[i] = s
end
local ok, r = pcall(W.receipt, "return 'MGS_COOP_WORLD_SAVE_V1' -- " .. table.concat(parts, " "))
return ok and r == true
end
-- op 1 REPORT: on a change (at most every 250 ms) and every 1000 ms.
local function report(now, force)
local t = {W.mission or 0, (X.title() and not X.leaving) and 1 or 0, X.story(), X.saving() and 1 or 0, X.ingame() and 1 or 0}
local key, last = table.concat(t, " "), X.last
if last and not force then
    local age = now - last.at
    if age >= 0 and age < (key == last.key and 1000 or 250) then return end
end
X.last = {key = key, at = now}
X.save(1, unpack(t))
if not last or key ~= last.key then
    log(fmt("step=report;title=%d;mission=%d;story=%d;saving=%d;ingame=%d", t[2], t[1], t[3], t[4], t[5]))
end
end
-- allow, or refuse/hold with the note (code, a, b) and the host mission.
-- heli: a title RestartHeli (no travel: only the handoff rules apply).
function X.decide(fn, kind, target, now, heli)
if not F.enabled or not W.active or (W.level or 0) < 3 or not F.session() then return "allow" end
local h, lock = X.code()
local esc = X.esc[kind .. ":" .. (target or 0)]
if esc and now <= esc then return "allow" end
-- A pending handoff vetoes a title Continue (note 9, twice declines) with or without the lock.
local title = kind == 4 and X.title()
if title and h >= 2 and h <= 4 then
    local p = peer()
    return "refuse", 9, 0, 0, p and num(p.mission) or 0
end
if lock ~= 1 then return "allow" end
local p = peer()
if not p or not p.linked or (num(p.level) or 0) < 3 then return "allow" end
local m = p.mission
if m == 0 or m == 1 or not SUPPORTED[m] or F.current or #F.queue > 0 then return "allow" end
if title then
    if h == 1 then return "refuse", 7, m, 3, m end
    if heli then return "allow" end
    if X.story() == 1 then return "refuse", 7, m, 2, m end
    if SUPPORTED[W.mission] ~= SUPPORTED[m] then return "refuse", 7, m, 1, m end
    return "allow"
end
if not target or target == 0 or target == m or floor(target / 10000) == 4 or target == W.mission then return "allow" end
if kind == 6 then return "hold", 12, target, m, m end
return "refuse", 5, m, target, m
end
local function passed(saved, ok, ...)
X.pass = saved
if not ok then error((...), 0) end
return ...
end
-- Runs an original the lock allowed; nested lock wrappers (the deploy's own clear) pass.
function X.call(f, ...)
local saved = X.pass
X.pass = true
return passed(saved, pcall(f, ...))
end
local function target(kind, args)
local t
if kind == 4 then t = W.mission
elseif kind == 6 then
    local em = num(W.get(gvars, "mis_nextMissionCodeForEmergency"))
    t = em and em ~= 0 and em or W.get(gvars, "mis_nextMissionCodeForMissionClear")
elseif type(args) == "table" and args.isTitleMode ~= true then t = args.nextMissionId end
return num(t)
end
-- A refused call: noted at most once per 1 s; true for the repeat 1-10 s (Continue)
-- or 1-30 s (travel) later, which runs once (travelling alone; quiet: no note 8).
local function again(name, kind, t, now, code, a, b, m, quiet)
local key, r = kind .. ":" .. (t or 0), X.refused
local span = r and r.key == key and r.mission == W.mission and now - r.at
if span and span >= 1000 and span <= (kind == 4 and 10000 or 30000) then
    X.refused = nil
    X.esc[key] = now + 2000
    if code == 9 or (code == 7 and b == 3) then X.save(3, 1, 0, 0, 0, 0); note(10, 0, 0) end
    if not quiet then note(8, t, 1) end
    line(name, kind, t, m, "repeat")
    return true
end
if not (span and span >= 0 and span < 1000) then
    X.refused = {key = key, mission = W.mission, at = now}
    note(code, a, b)
end
line(name, kind, t, m, "refuse")
return false
end
local function wrap(name, kind)
local original = W.get(TppMission, name)
if type(original) ~= "function" then return end
TppMission[name] = function(...)
    -- At a title the menu gate below owns Continue (a replay passes F.token anyway).
    if F.token or X.pass or W.host or not W.active or (kind == 4 and X.title()) then
        if kind == 6 then X.held = nil end
        return original(...)
    end
    local now, t = W.now(), target(kind, (...))
    local ok, d, code, a, b, m = pcall(X.decide, name, kind, t, now)
    if not ok then log("step=error;msg=" .. string.sub(tostring(d), 1, 100)) end
    if not ok or d == "allow" then
        if kind == 6 then X.held = nil end
        return original(...)
    end
    if d == "hold" then
        local hd = X.held
        if hd and hd.mission == W.mission then
            -- Chosen again (1 s debounce): fly alone with these arguments.
            if now - hd.at < 1000 then return end
            X.held = nil
            note(8, t, 1)
            line(name, kind, t, m, "repeat")
            return X.call(original, ...)
        end
        X.held = {fn = original, n = select("#", ...), mission = W.mission, target = t, host = m, at = now, checkAt = now, notedAt = now, ...}
        note(code, a, b)
        line(name, kind, t, m, "hold")
        return
    end
    -- Done again: allowed once (both Continue wrappers of one press), travelling alone.
    if again(name, kind, t, now, code, a, b, m) then return original(...) end
    if name == "ReserveMissionClear" then return false end
end
end
local function release(hd, why, b)
X.held = nil
if b then note(8, hd.target, b) end
line(DEPLOY, 6, hd.target, hd.host, why)
X.call(hd.fn, unpack(hd, 1, hd.n))
end
-- The held sortie: dropped on a mission change, a follow, or once the prep screen
-- no longer names it (cancelled or changed: the vanilla deploy re-reads gvars);
-- released when world sync stopped (world_sync calls this while inactive), by the
-- F9 unlock, after 120 s, or once the lock no longer applies; the note every 3 s.
function X.hold(now)
local hd = X.held
if not hd then return end
now = now or W.now()
if hd.mission ~= W.mission or W.host then X.held = nil; return end
if target(6) ~= hd.target then
    X.held = nil
    line(DEPLOY, 6, hd.target, hd.host, "stale")
    return
end
if not W.active then return release(hd, "inactive", 2) end
if W.paired or F.current or #F.queue > 0 then
    X.held = nil
    line(DEPLOY, 6, hd.target, hd.host, "follow")
    return
end
if now - hd.checkAt < 500 then return end
hd.checkAt = now
local _, lock = X.code()
if lock ~= 1 then return release(hd, "unlock", 2) end
if now - hd.at >= 120000 then return release(hd, "timeout", 3) end
local d, _, _, _, m = X.decide(DEPLOY, 6, hd.target, now)
if d == "allow" then return release(hd, "allow") end
hd.host = m or hd.host
if now - hd.notedAt >= 3000 then hd.notedAt = now; note(12, hd.target, hd.host) end
end
function X.tick(now)
report(now)
for k, t in pairs(X.esc) do if now > t then X.esc[k] = nil end end
X.hold(now)
end
local tick = F.tick
F.tick = function(now) X.tick(now); return tick(now) end
-- Title replay hold: the guest keeps a queued host transition while its handoff is pending.
function X.replayHold()
local h = 0
if not W.host and X.title() then h = X.code(250) end
local c = h >= 1 and h <= 4 and h or 0
if c ~= (X.replayCode or 0) then X.replayCode = c; log(fmt("step=replay_hold;code=%d", c)) end
return c ~= 0
end
F.replayHold = X.replayHold
-- An armed guest's title Continue stays suppressed (it follows the host's):
-- pressing it twice while the host's save is coming still declines the handoff.
function X.decline(t)
local h = X.code()
if h < 2 or h > 4 then return end
t = t or W.mission
local now, key, r = W.now(), "4:" .. t, X.refused
local span = r and r.key == key and r.mission == W.mission and now - r.at
if span and span >= 1000 and span <= 10000 then
    X.refused = nil
    X.save(3, 1, 0, 0, 0, 0)
    note(10, 0, 0)
    line("TitleContinue", 4, t, 0, "decline")
elseif not (span and span >= 0 and span < 1000) then
    X.refused = {key = key, mission = W.mission, at = now}
    note(9, 0, 0)
end
end
-- world_follow's guest(): an armed guest follows the host's travel.
local function armed()
return F.enabled and W.active and (W.level or 0) >= 3 and SUPPORTED[W.mission] and (not W.paired or (W.eff or 0) >= 3) and
    F.armed and not F.token
end
-- The vanilla Continue select restarts in the helicopter for these (no travel).
local function heliNext(m)
local f = W.get(TppMission, "IsHelicopterSpace")
local ok, v = false, nil
if type(f) == "function" then ok, v = pcall(f, m) end
return (ok and v == true) or m == 10115 or m == 50050
end
-- A swallowed select: the menu takes input again, as the vanilla cancel path does.
function X.reopen()
local reboot, start = W.get(TppUiCommand, "RebootTitleSelectMenu"), W.get(title_sequence, "StartTitleMenu")
if type(reboot) == "function" then pcall(reboot) end
if type(start) == "function" then pcall(start) end
end
-- The title menu's Continue / RestartHeli select, before any fade, VarRestore or load.
function X.select(original, restart, ...)
if not X.title() then return original(...) end
local nxt = num(W.get(gvars, "title_nextMissionCode")) or 0
local heli = restart or heliNext(nxt)
X.fromTitle = not heli
if X.pass or F.token or W.host or not W.active then return original(...) end
local now, t = W.now(), nxt ~= 0 and nxt or W.mission
local name = heli and "TitleRestartHeli" or "TitleContinue"
if not heli and armed() then
    pcall(X.decline, t)
    F.request(4)
    line(name, 4, t, 0, "armed")
    return X.reopen()
end
local ok, d, code, a, b, m = pcall(X.decide, name, 4, t, now, heli)
if not ok then log("step=error;msg=" .. string.sub(tostring(d), 1, 100)) end
if not ok or d == "allow" or again(name, 4, t, now, code, a, b, m, heli) then return original(...) end
X.reopen()
end
local WRAPPED = setmetatable({}, {__mode = "k"})
-- Each title mission init, before TppSequence.Init builds the menu's message
-- table (it captures self.OnSelectContinue): the shared Seq_Game_TitleMenu.
function X.wrapTitle()
if floor((num(W.get(vars, "missionCode")) or 0) / 10000) ~= 4 then return end
local menu = W.get(W.get(mvars, "seq_sequenceTable"), "Seq_Game_TitleMenu")
if type(menu) ~= "table" then return end
for name, restart in pairs({OnSelectContinue = false, OnSelectRestartHeli = true}) do
    local original = W.get(menu, name)
    if type(original) == "function" and not WRAPPED[original] then
        local f = function(...) return X.select(original, restart, ...) end
        WRAPPED[f] = true
        menu[name] = f
    end
end
end
-- Loading choke point: op 2 CONTINUE (never for a replay), the final REPORT and the warnings.
function X.loading(kind)
local L = W.get(TppDefine, "MISSION_LOAD_TYPE") or {}
local k = kind == L.MISSION_FINALIZE and 1 or kind == L.MISSION_ABORT and 2 or
    kind == L.MISSION_RESTART and 3 or kind == L.CONTINUE_FROM_CHECK_POINT and 4 or 0
if k == 0 then return end
local t, replay = num(W.get(vars, "missionCode")), F.token or F.current
if k == 4 and t and not replay then X.save(2, t, (X.title() or X.fromTitle) and 1 or 0, X.story(), 0, 0) end
if not W.active then return end
X.leaving = true
local now, p = W.now(), peer()
report(now, true)
if not (t and p and p.linked) then return end
if W.host then
    local key = tostring(W.mission) .. ":" .. t
    if not SUPPORTED[t] and not X.warned[key] then
        X.warned[key] = true
        note(6, t, p.mission)
        log(fmt("step=warn;reason=partner_cannot_join;target=%d", t))
    end
elseif k == 4 and not replay then
    local d, _, _, _, m = X.decide("ReservePlayerLoadingPosition", 1, t, now)
    if d ~= "allow" then
        note(11, t, m)
        log(fmt("step=warn;reason=landing_elsewhere;target=%d", t))
    end
end
end
F.installHooks[#F.installHooks + 1] = function()
pcall(X.wrapTitle)
if X.installed then return end
X.installed = true
for name, kind in pairs(KIND) do wrap(name, kind) end
local reserve = W.get(e, "ReservePlayerLoadingPosition")
if type(reserve) == "function" then
    e.ReservePlayerLoadingPosition = function(kind, ...)
        local ok, err = pcall(X.loading, kind)
        if not ok then pcall(log, "step=error;msg=" .. string.sub(tostring(err), 1, 100)) end
        return reserve(kind, ...)
    end
end
end
W.startHooks[#W.startHooks + 1] = function()
X.held, X.refused, X.leaving, X.last, X.replayCode, X.fromTitle = nil, nil, false, nil, nil, false
X.esc, X.warned = {}, {}
end
end
