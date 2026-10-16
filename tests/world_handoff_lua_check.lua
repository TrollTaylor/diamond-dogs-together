-- Join-fix phase 2 track C (docs/join-fix-phase2-spec.md C1-C6): the SAVE report,
-- the guest travel lock, the travel warnings and the title replay hold, with
-- recording Lua APIs. No game, real save or transport.
-- Args: world_handoff.lua, world_follow.lua, world_follow_seed.lua.
local checks = 0
local function check(value, why)
    checks = checks + 1
    if not value then error(tostring(checks) .. ': ' .. why, 2) end
end
local OP = {TRANSITION = 9, SEED = 10, SEEDS_END = 11, REQ_MISSION = 12, FOLLOW_ACK = 13, REJECT = 4}
local HOST = {mission = 30010, level = 3, wait = 9, linked = true, session = 55}
local function copy(t) local r = {} for k, v in pairs(t) do r[k] = v end return r end
-- opts: mission (40050 title helicopter by default), title (default: 4xxxx), seq (str_storySequence),
-- peer (false: none), r12, r13, old (an 11-value CFG), paired, next (title_nextMissionCode;
-- default: the title's own scene, 40050 -> 30050).
local function fixture(role, opts)
    opts = opts or {}
    local mission = opts.mission or 40050
    local c = {calls = {}, tx = {}, logs = {}, notes = {}, saves = {}, time = 1000, inGame = true,
        r12 = opts.r12 or 0, r13 = opts.r13 == nil and 1 or opts.r13}
    if opts.peer ~= false then c.peer = copy(opts.peer or HOST) end
    local env = setmetatable({}, {__index = _G})
    env.vars = {missionCode = mission, locationCode = 50, mbLayoutCode = 0, mbClusterId = 2,
        initialPlayerFlag = 1, initialPlayerPosX = 1, initialPlayerPosY = 2, initialPlayerPosZ = 3, initialPlayerRotY = 4}
    env.mvars = {mis_selectedDeployTime = 0}
    local title = opts.title
    if title == nil then title = math.floor(mission / 10000) == 4 end
    env.gvars = {solface_groupNumber = 111, hosface_groupNumber = 222, heli_missionStartRoute = 5,
        rev_revengeRandomValue = 333, rev_revengeLv = {[0] = 1, [1] = 2}, missionStartClock = 43200,
        missionStartWeather = 0, missionStartWeatherNextTime = 1.5, missionStartExtraWeatherInterval = 200,
        mis_isStartFromHelispace = true, mis_isStartFromFreePlay = false, ply_initialPlayerState = 0,
        ply_useMissionStartPos = false, ply_missionStartPos = {[0] = 0, [1] = 0, [2] = 0},
        ply_missionStartRot = 0, ply_useMissionStartPosForNoOrderBox = false,
        ply_missionStartPosForNoOrderBox = {[0] = 0, [1] = 0, [2] = 0}, ply_missionStartRotForNoOrderBox = 0,
        mis_checkPoint = 0, ini_isTitleMode = title, ini_isReturnToTitle = false, isContinueFromTitle = false,
        str_storySequence = opts.seq or 200, mis_nextMissionCodeForMissionClear = 30020, mis_nextMissionCodeForEmergency = 0,
        title_nextMissionCode = opts.next or (math.floor(mission / 10000) == 4 and mission - 10000 or 0)}
    env.svars = {mis_missionClearType = 3, ply_isUsedPlayerInitialAction = false}
    env.TppDefine = {MISSION_LOAD_TYPE = {MISSION_FINALIZE = 1, MISSION_ABORT = 2, MISSION_RESTART = 3, CONTINUE_FROM_CHECK_POINT = 4},
        STORY_SEQUENCE = {CLEARD_ESCAPE_THE_HOSPITAL = 100, CLEARD_RECUE_MILLER = 110, CLEARD_TO_MATHER_BASE = 120}}
    env.TppRevenge = {REVENGE_TYPE = {MAX = 2}}
    env.TppScriptVars = {IsSavingOrLoading = function() return c.saving == true end}
    env.DemoDaemon = {IsDemoPlaying = function() return false end}
    local function record(name) c.calls[#c.calls + 1] = name end
    env.e = {ReservePlayerLoadingPosition = function() record('reserve'); return 7, nil, 9 end}
    env.TppMission = {
        ReserveMissionClear = function(t) c.clear = t; record('clear'); return true end,
        AbortMission = function(t) c.abort = t; record('abort') end,
        AbortMissionByMenu = function() record('abort_menu') end,
        ContinueFromCheckPoint = function()
            record('continue_menu')
            -- The isNoFade path (and the fade callback) runs the wrapped Execute.
            if c.nestedContinue then env.TppMission.ExecuteContinueFromCheckPoint() end
        end,
        ExecuteContinueFromCheckPoint = function() record('continue'); return 'vanilla' end,
        ExecuteRestartMission = function() record('restart') end,
        SelectNextMissionHeliStartRoute = function() record('route') end,
        OnEndMissionPreparation = function(t, cluster)
            c.deploy = {t, cluster}; record('deploy')
            -- Game-shaped: the vanilla deploy reserves its clear through the wrapped function.
            if c.nested then
                env.TppMission.ReserveMissionClear{missionClearType = 3, nextMissionId = env.gvars.mis_nextMissionCodeForMissionClear}
            end
        end,
        Load = function(m) record('load'); c.loaded = m end,
        IsHelicopterSpace = function(m) return math.floor(m / 10000) == 4 end}
    -- The title menu as the game builds it (title_sequence Seq_Game_TitleMenu, shared
    -- into the helicopter mission's sequence table): TppSequence.Init captures
    -- self.OnSelectContinue in the message table after world_follow's install.
    local menu = {
        OnSelectContinue = function() record('select_continue') end,
        OnSelectRestartHeli = function() record('select_heli') end}
    menu.Messages = function(self) return {Continue = self.OnSelectContinue, RestartHeli = self.OnSelectRestartHeli} end
    env.mvars.seq_sequenceTable = {Seq_Game_TitleMenu = menu}
    env.TppUiCommand = {RebootTitleSelectMenu = function() record('reboot_menu') end}
    env.title_sequence = {StartTitleMenu = function() record('start_menu') end}
    local W = {OP = OP, handlers = {}, startHooks = {}, pairedHooks = {}, unpairedHooks = {}, stopHooks = {},
        level = opts.level or 3, role = role, host = role == 1, active = true, mission = mission, epoch = 7,
        paired = false, eff = 3}
    env.e.MgsCoopWorld = W
    W.get = function(t, k) if t == nil then return nil end return t[k] end
    W.cfg = function()
        local p = c.peer or {mission = 0, level = 0, wait = 3}
        local r = {c.configLevel or W.level, W.role, 1, c.link or 1, 7, 0, 1, c.session or 55, p.mission, p.level, p.wait}
        if not opts.old then r[12], r[13] = c.r12, c.r13 end
        return r
    end
    W.n = function(v)
        if type(v) ~= 'number' or v ~= v or v >= 4294967296 or v < -2147483648 or v ~= math.floor(v) then return nil end
        if v == 0 then return '0' end
        return string.format('%.0f', v)
    end
    W.receipt = function(source)
        local body = source:match("^return 'MGS_COOP_WORLD_SAVE_V1' %-%- (.*)$")
        check(body and body:match('^%d+ %d+ %d+ %d+ %d+ %d+$'), 'strict SAVE_V1 grammar: ' .. source)
        local v = {}
        for n in body:gmatch('%d+') do
            check(n == '0' or n:sub(1, 1) ~= '0', 'SAVE_V1 without leading zeros: ' .. source)
            v[#v + 1] = tonumber(n)
        end
        c.saves[#c.saves + 1] = v
        return true
    end
    W.h = function() return 4294967295 end
    W.now = function() return c.time end
    W.inGame = function() return c.inGame end
    W.nextRid = function() c.rid = (c.rid or 0) + 1; return c.rid end
    W.log = function(s) check(#('event=' .. s) <= 170, 'log line bounded: ' .. s); c.logs[#c.logs + 1] = s end
    W.tx = function(op, ...) c.tx[#c.tx + 1] = {op = op, v = {...}}; return true end
    W.fail = function(s) c.failure = s; W.active = false end
    W.note = function(code, a, b) c.notes[#c.notes + 1] = {code, a, b}; return true end
    W.peer = function() return c.peer end
    for _, path in ipairs({arg[3], arg[2], arg[1]}) do
        local chunk = assert(loadfile(path)); setfenv(chunk, env); chunk()
    end
    W.follow.install({W.level, role})
    c.menu = menu.Messages(menu)  -- TppSequence.Init (OnInitialize, after the install)
    for _, f in ipairs(W.startHooks) do f() end
    if opts.paired then W.paired = true; for _, f in ipairs(W.pairedHooks) do f(false) end end
    c.W, c.env, c.F, c.X, c.M = W, env, W.follow, W.handoff, env.TppMission
    return c
end
local function called(c, name)
    local n = 0
    for _, v in ipairs(c.calls) do if v == name then n = n + 1 end end
    return n
end
local function noted(c, code, a, b)
    local n = 0
    for _, v in ipairs(c.notes) do if v[1] == code and (a == nil or v[2] == a) and (b == nil or v[3] == b) then n = n + 1 end end
    return n
end
local function logged(c, text)
    local n = 0
    for _, s in ipairs(c.logs) do if s:find(text, 1, true) then n = n + 1 end end
    return n
end
local function saved(c, op)
    local out = {}
    for _, v in ipairs(c.saves) do if v[1] == op then out[#out + 1] = v end end
    return out
end
local function same(v, w)
    if #v ~= #w then return false end
    for i = 1, #v do if v[i] ~= w[i] then return false end end
    return true
end
local function at(c, ms) c.time = ms; c.F.tick(c.time) end

-- 1. The chunk: size, LF bytes, the published table and the follow hooks.
do
    local f = assert(io.open(arg[1], 'rb')); local text = f:read('*a'):gsub('\r\n', '\n'); f:close()
    check(#text <= 16000, 'world_handoff.lua is ' .. #text .. ' bytes; the target is 16000')
    check(text:sub(1, 2) == '--' and text:find('\ndo\n', 1, true) and text:sub(-5) == '\nend\n', 'one do ... end chunk')
    local c = fixture(2)
    check(type(c.X) == 'table' and c.F.replayHold == c.X.replayHold and #c.F.installHooks == 1, 'W.handoff, F.replayHold and one install hook')
    local wrapped = c.M.ExecuteContinueFromCheckPoint
    local menu = c.env.mvars.seq_sequenceTable.Seq_Game_TitleMenu
    local select = menu.OnSelectContinue
    check(c.menu.Continue == select and c.menu.RestartHeli == menu.OnSelectRestartHeli, 'the menu message table captures the wrapped selects')
    c.F.install({3, 2})
    check(c.M.ExecuteContinueFromCheckPoint == wrapped and menu.OnSelectContinue == select, 'a second install wraps nothing again')
    local field = fixture(2, {mission = 30010})
    local plain = field.env.mvars.seq_sequenceTable.Seq_Game_TitleMenu
    field.F.install({3, 2})
    plain.OnSelectContinue()
    check(#field.logs == 0 and called(field, 'select_continue') == 1, 'a free-roam mission init leaves a title table alone')
end

-- 2. REPORT (op 1): on a change (at most every 250 ms) and every 1000 ms.
do
    local c = fixture(2)
    at(c, 1000)
    check(#c.saves == 1 and same(c.saves[1], {1, 40050, 1, 2, 0, 0}), 'title helicopter: mission, title, story open, not saving, not in game')
    check(logged(c, 'event=handoff;step=report;title=1;mission=40050;story=2;saving=0;ingame=0') == 1, 'report line on change')
    at(c, 1999)
    check(#c.saves == 1, 'unchanged: nothing before 1000 ms')
    at(c, 2000)
    check(#c.saves == 2 and same(c.saves[2], c.saves[1]) and logged(c, 'step=report') == 1, 'heartbeat at 1000 ms, no new line')
    c.saving = true
    at(c, 2249)
    check(#c.saves == 2, 'a change waits 250 ms after the last report')
    at(c, 2250)
    check(#c.saves == 3 and same(c.saves[3], {1, 40050, 1, 2, 1, 0}) and logged(c, 'saving=1;ingame=0') == 1, 'a change is reported')
    local g = fixture(2, {mission = 30010})
    at(g, 1000)
    check(same(g.saves[1], {1, 30010, 0, 2, 0, 1}), 'free roam: in game')
    g.inGame = false
    at(g, 1250)
    check(same(g.saves[2], {1, 30010, 0, 2, 0, 0}), 'mission state lost: not in game')
    local stopped = fixture(2); stopped.W.active = false
    stopped.F.tick(stopped.time)
    check(same(stopped.saves[1], {1, 40050, 1, 2, 0, 0}), 'F.tick is chained (world_sync ticks it only while active)')
    local bad = fixture(2)
    check(bad.X.save(1, -1, 0, 0, 0, 0) == false and bad.X.save(1, 1.5, 0, 0, 0, 0) == false and
        bad.X.save(1, 0, 0, 0, 0) == false and bad.X.save(1, 0 / 0, 0, 0, 0, 0) == false and #bad.saves == 0,
        'X.save sends nothing for a negative, fractional, missing or NaN value')
    check(bad.X.save(3, 1, 0, 0, 0, 0) == true and same(bad.saves[1], {3, 1, 0, 0, 0, 0}), 'X.save sends six integers')
end

-- 3. story(): 0 unknown, 1 opening, 2 open.
do
    local c = fixture(2)
    local S = c.env.TppDefine.STORY_SEQUENCE
    for _, case in ipairs({{200, 2}, {120, 2}, {119, 1}, {110, 1}, {100, 1}, {50, 1}}) do
        c.env.gvars.str_storySequence = case[1]
        check(c.X.story() == case[2], 'story class of ' .. case[1])
    end
    c.env.gvars.str_storySequence = nil
    check(c.X.story() == 0, 'no str_storySequence: unknown')
    c.env.gvars.str_storySequence = '200'
    check(c.X.story() == 0, 'a string sequence: unknown')
    c.env.gvars.str_storySequence = 0 / 0
    check(c.X.story() == 0, 'NaN sequence: unknown')
    c.env.gvars.str_storySequence = 200
    for _, consts in ipairs({{100, nil, 120}, {100, 130, 120}, {100, 100, 120}, {nil, 110, 120}, {100, 110, 'x'}}) do
        S.CLEARD_ESCAPE_THE_HOSPITAL, S.CLEARD_RECUE_MILLER, S.CLEARD_TO_MATHER_BASE = consts[1], consts[2], consts[3]
        check(c.X.story() == 0, 'missing or unordered constants: unknown')
    end
    c.env.TppDefine.STORY_SEQUENCE = nil
    check(c.X.story() == 0, 'no STORY_SEQUENCE: unknown')
end

-- 4. X.decide: every reason the lock does not apply (a free-roam guest, the host in 30050).
do
    local base = {mission = 30010, peer = {mission = 30050, level = 3, wait = 9, linked = true, session = 55}}
    local function decide(change, kind, target)
        local c = fixture(2, base)
        if change then change(c) end
        return c.X.decide('ReserveMissionClear', kind or 1, target or 10036, c.time)
    end
    local d, code, a, b, m = decide()
    check(d == 'refuse' and code == 5 and a == 30050 and b == 10036 and m == 30050, 'baseline: a story destination is refused with note 5')
    local d6, c6, a6, b6 = decide(nil, 6, 30020)
    check(d6 == 'hold' and c6 == 12 and a6 == 30020 and b6 == 30050, 'a sortie elsewhere is held with note 12')
    local allow = {
        {'no link', function(c) c.link = 0 end},
        {'other link session role', function(c) c.configLevel = 2 end},
        {'r13=0', function(c) c.r13 = 0 end},
        {'peer nil', function(c) c.peer = nil end},
        {'not linked', function(c) c.peer.linked = false end},
        {'partner level 2', function(c) c.peer.level = 2 end},
        {'world level 2', function(c) c.W.level = 2 end},
        {'inactive', function(c) c.W.active = false end},
        {'follow off', function(c) c.F.enabled = false end},
        {'host loading (0)', function(c) c.peer.mission = 0 end},
        {'host at mission 1', function(c) c.peer.mission = 1 end},
        {'host in 10020', function(c) c.peer.mission = 10020 end},
        {'F.current', function(c) c.F.current = {1} end},
        {'queued transition', function(c) c.F.queue[1] = {1} end},
    }
    for _, case in ipairs(allow) do check(decide(case[2]) == 'allow', 'allow: ' .. case[1]) end
    check(decide(nil, 1, 40020) == 'allow', 'allow: an ACC destination')
    check(decide(nil, 1, 30050) == 'allow', 'allow: the host mission')
    check(decide(nil, 1, 30010) == 'allow', 'allow: this mission (restart, reload)')
    check(decide(nil, 1, 0) == 'allow' and fixture(2, base).X.decide('AbortMission', 2, nil, 1000) == 'allow', 'allow: no destination')
    local old = fixture(2, {mission = 30010, old = true, peer = base.peer})
    check(old.X.decide('ReserveMissionClear', 1, 10036, old.time) == 'allow', 'allow: an 11-value CFG (r13 nil)')
    local esc = fixture(2, base)
    esc.X.esc['1:10036'] = esc.time + 2000
    check(esc.X.decide('ReserveMissionClear', 1, 10036, esc.time + 2000) == 'allow' and
        esc.X.decide('ReserveMissionClear', 1, 10036, esc.time + 2001) == 'refuse', 'an escape allows for 2000 ms')
end

-- 5. Title Continue: the title menu's own select (title_sequence Seq_Game_TitleMenu),
-- before any fade, VarRestore or load. The game's title Continue never calls
-- TppMission.ContinueFromCheckPoint/ExecuteContinueFromCheckPoint.
do
    local c = fixture(2)
    check(c.menu.Continue() == nil and called(c, 'select_continue') == 0 and #c.notes == 1 and noted(c, 7, 30010, 1) == 1,
        'location differs (40050 vs 30010): refused at the select with note 7 b=1')
    check(called(c, 'reboot_menu') == 1 and called(c, 'start_menu') == 1, 'the refused select reopens the menu (the vanilla cancel path)')
    check(logged(c, 'event=handoff;step=lock;fn=TitleContinue;kind=4;target=30050;host=30010;decision=refuse') == 1, 'the refusal line')
    c.time = 1500
    c.menu.Continue()
    check(called(c, 'select_continue') == 0 and #c.notes == 1 and logged(c, 'decision=refuse') == 2, '+0.5 s: refused again, no new note')
    c.time = 3000
    c.menu.Continue()
    check(called(c, 'select_continue') == 1 and noted(c, 8, 30050, 1) == 1 and #saved(c, 3) == 0 and logged(c, 'decision=repeat') == 1,
        '+2 s: the vanilla select runs once (travel alone)')
    local late = fixture(2)
    late.menu.Continue()
    late.time = late.time + 11000
    late.menu.Continue()
    check(called(late, 'select_continue') == 0 and noted(late, 7, 30010, 1) == 2, '+11 s: refused again with a new note')
    late.time = late.time + 1000
    late.menu.Continue()
    check(called(late, 'select_continue') == 1, 'the new refusal opens a new repeat window')
    local story = fixture(2, {mission = 40010, seq = 110})
    story.menu.Continue()
    check(called(story, 'select_continue') == 0 and noted(story, 7, 30010, 2) == 1, 'story opening: note 7 b=2')
    local wait = fixture(2, {mission = 40010, r12 = 1, seq = 110})
    wait.menu.Continue()
    check(called(wait, 'select_continue') == 0 and noted(wait, 7, 30010, 3) == 1, 'waiting for the host save first: note 7 b=3')
    for _, h in ipairs({2, 3, 4}) do
        local busy = fixture(2, {mission = 40010, r12 = h})
        busy.menu.Continue()
        check(called(busy, 'select_continue') == 0 and noted(busy, 9, 0, 0) == 1 and #busy.notes == 1, 'handoff busy (' .. h .. '): note 9')
    end
    local ok = fixture(2, {mission = 40010})
    ok.menu.Continue()
    check(called(ok, 'select_continue') == 1 and #ok.notes == 0 and called(ok, 'reboot_menu') == 0, 'same location, story open: allowed')
    for _, h in ipairs({5, 6}) do
        local kept = fixture(2, {mission = 40010, r12 = h})
        kept.menu.Continue()
        check(called(kept, 'select_continue') == 1 and #kept.notes == 0, 'kept or failed handoff (' .. h .. '): allowed')
    end
    local unknown = fixture(2, {mission = 40010, seq = 110})
    unknown.env.TppDefine.STORY_SEQUENCE = nil
    unknown.menu.Continue()
    check(called(unknown, 'select_continue') == 1, 'unknown story at the same location: allowed')
    -- A repeat while the handoff is pending declines it (op 3) and notes 10 before 8.
    local p = fixture(2, {r12 = 3})
    p.menu.Continue()
    p.time = p.time + 2000
    p.menu.Continue()
    local n = #p.notes
    check(called(p, 'select_continue') == 1 and #saved(p, 3) == 1 and same(saved(p, 3)[1], {3, 1, 0, 0, 0, 0}) and
        p.notes[n - 1][1] == 10 and p.notes[n][1] == 8, 'pending repeat: SAVE op 3 decline, note 10 then note 8')
    local w = fixture(2, {r12 = 1})
    w.menu.Continue(); w.time = w.time + 2000; w.menu.Continue()
    check(called(w, 'select_continue') == 1 and #saved(w, 3) == 1, 'waiting repeat: declined too')
    -- At a title the TppMission Continue wrappers pass (the menu owns the title Continue).
    local direct = fixture(2)
    direct.nestedContinue = true
    direct.M.ContinueFromCheckPoint()
    check(called(direct, 'continue_menu') == 1 and called(direct, 'continue') == 1 and #direct.notes == 0,
        'TppMission Continue at a title: passed through')
    -- A game-over Continue inside a mission (not a title) restarts this mission: allowed.
    local field = fixture(2, {mission = 30020})
    field.M.ExecuteContinueFromCheckPoint()
    check(called(field, 'continue') == 1 and #field.notes == 0, 'in-mission Continue: allowed')
    -- RestartHeli (and a Continue into a helicopter) does not travel: only the handoff rules.
    local heli = fixture(2)
    heli.menu.RestartHeli()
    check(called(heli, 'select_heli') == 1 and #heli.notes == 0, 'RestartHeli at another location: allowed')
    local heliNext = fixture(2, {next = 40050})
    heliNext.menu.Continue()
    check(called(heliNext, 'select_continue') == 1 and #heliNext.notes == 0, 'a Continue that restarts in the helicopter: allowed')
    local heliBusy = fixture(2, {r12 = 3})
    heliBusy.menu.RestartHeli()
    check(called(heliBusy, 'select_heli') == 0 and noted(heliBusy, 9, 0, 0) == 1 and
        logged(heliBusy, 'fn=TitleRestartHeli;kind=4;target=30050;host=30010;decision=refuse') == 1, 'RestartHeli while the handoff is pending: note 9')
    heliBusy.time = heliBusy.time + 2000
    heliBusy.menu.RestartHeli()
    check(called(heliBusy, 'select_heli') == 1 and #saved(heliBusy, 3) == 1 and noted(heliBusy, 10) == 1 and noted(heliBusy, 8) == 0,
        'its repeat declines (op 3, note 10) and stays in the helicopter (no note 8)')
    local heliWait = fixture(2, {r12 = 1})
    heliWait.menu.RestartHeli()
    check(called(heliWait, 'select_heli') == 0 and noted(heliWait, 7, 30010, 3) == 1, 'RestartHeli while waiting for the host save: note 7 b=3')
    -- The host and a replay pass; outside title mode the select is the vanilla one.
    local host = fixture(1)
    host.menu.Continue()
    check(called(host, 'select_continue') == 1 and #host.notes == 0, 'the host passes through')
    local notTitle = fixture(2, {title = false})
    notTitle.menu.Continue()
    check(called(notTitle, 'select_continue') == 1 and #notTitle.notes == 0, 'not in title mode: passed through')
end

-- 5b. Review SS-2: the pending-handoff veto does not depend on the travel lock.
do
    local c = fixture(2, {mission = 40010, r12 = 3, r13 = 0})
    c.menu.Continue()
    check(called(c, 'select_continue') == 0 and noted(c, 9, 0, 0) == 1 and #saved(c, 3) == 0, 'lock off, handoff pending: note 9')
    c.time = c.time + 2000
    c.menu.Continue()
    check(called(c, 'select_continue') == 1 and #saved(c, 3) == 1 and same(saved(c, 3)[1], {3, 1, 0, 0, 0, 0}) and noted(c, 10) == 1,
        'lock off: the repeat sends SAVE op 3 and runs the select once')
    -- After the repeat, with r12 still 3, the follow-up Continue calls are not swallowed.
    c.nestedContinue = true
    c.M.ContinueFromCheckPoint()
    check(called(c, 'continue_menu') == 1 and called(c, 'continue') == 1, 'nested Continue calls pass after the repeat')
    for _, case in ipairs({{'peer unlinked', {mission = 0, level = 0, wait = 3, linked = false}}, {'host loading', {mission = 0, level = 3, wait = 9, linked = true, session = 55}},
        {'host in 10020', {mission = 10020, level = 3, wait = 9, linked = true, session = 55}}}) do
        local g = fixture(2, {mission = 40010, r12 = 3, peer = case[2]})
        g.menu.Continue()
        check(called(g, 'select_continue') == 0 and noted(g, 9, 0, 0) == 1, case[1] .. ' while the handoff is pending: still note 9')
    end
    local idle = fixture(2, {mission = 40010, r12 = 1, r13 = 0, seq = 110})
    idle.menu.Continue()
    check(called(idle, 'select_continue') == 1 and #idle.notes == 0, 'r12 1 with the lock off: allowed (wait-host is a lock rule)')
end

-- 6. iDroid ReserveMissionClear and AbortMission.
do
    local function free() return fixture(2, {mission = 30010, peer = {mission = 30050, level = 3, wait = 9, linked = true, session = 55}}) end
    local c = free()
    check(c.M.ReserveMissionClear{nextMissionId = 10036} == false and called(c, 'clear') == 0 and noted(c, 5, 30050, 10036) == 1,
        'ReserveMissionClear{10036}: false (vanilla refusal shape) with note 5')
    c.time = c.time + 5000
    check(c.M.ReserveMissionClear{nextMissionId = 10036} == true and called(c, 'clear') == 1 and noted(c, 8, 10036, 1) == 1 and
        logged(c, 'fn=ReserveMissionClear;kind=1;target=10036;host=30050;decision=repeat') == 1, '+5 s: allowed, travelling alone')
    local late = free()
    late.M.ReserveMissionClear{nextMissionId = 10036}
    late.time = late.time + 31000
    check(late.M.ReserveMissionClear{nextMissionId = 10036} == false and noted(late, 5) == 2, '+31 s: refused again')
    local edge = free()
    edge.M.ReserveMissionClear{nextMissionId = 10036}
    edge.time = edge.time + 30000
    check(edge.M.ReserveMissionClear{nextMissionId = 10036} == true, '+30 s: still a repeat')
    local other = free()
    other.M.ReserveMissionClear{nextMissionId = 10036}
    other.time = other.time + 5000
    check(other.M.ReserveMissionClear{nextMissionId = 10043} == false and noted(other, 5, 30050, 10043) == 1,
        'another destination is a new refusal')
    local a = free()
    check(a.M.AbortMission{nextMissionId = 10036} == nil and called(a, 'abort') == 0 and noted(a, 5, 30050, 10036) == 1,
        'AbortMission to a story mission: refused (nil)')
    a.time = a.time + 1000
    a.M.AbortMission{nextMissionId = 10036}
    check(called(a, 'abort') == 1, 'AbortMission repeat: allowed')
    local t = free()
    t.M.AbortMission{nextMissionId = 10010, isNoSave = true, isTitleMode = true}
    t.M.AbortMissionByMenu()
    t.M.AbortMission{nextMissionId = 40010}
    t.M.ReserveMissionClear{nextMissionId = 40010}
    t.M.ReserveMissionClear{nextMissionId = 30050}
    check(called(t, 'abort') == 2 and called(t, 'abort_menu') == 1 and called(t, 'clear') == 2 and #t.notes == 0,
        'return to title, the menu abort, a helicopter and the host mission are always allowed')
    local token = free()
    token.F.run(function() token.M.ReserveMissionClear{nextMissionId = 10036} end)
    check(called(token, 'clear') == 1 and #token.notes == 0, 'F.token (a replay) passes through')
    local h = fixture(1)
    check(h.M.ExecuteContinueFromCheckPoint() == 'vanilla' and #h.notes == 0, 'the host passes through')
    local paired = fixture(2, {mission = 30010, title = false, paired = true})
    paired.M.ExecuteContinueFromCheckPoint()
    check(called(paired, 'continue') == 0 and #paired.notes == 0 and paired.tx[1].op == OP.REQ_MISSION,
        'an armed guest stays world_follow suppressed (outer layer), never reaching the lock')
end

-- 7. The sortie hold (OnEndMissionPreparation from a helicopter, not a title).
do
    local function heli(opts)
        opts = opts or {}
        opts.title = false
        local c = fixture(2, opts)
        c.nested = true
        return c
    end
    local c = heli()
    check(c.M.OnEndMissionPreparation(2, nil) == nil and called(c, 'deploy') == 0 and c.X.held and c.X.held.n == 2 and
        c.X.held[1] == 2 and c.X.held.target == 30020 and noted(c, 12, 30020, 30010) == 1 and
        logged(c, 'fn=OnEndMissionPreparation;kind=6;target=30020;host=30010;decision=hold') == 1, 'held with its arguments and note 12')
    c.time = c.time + 999
    c.M.OnEndMissionPreparation(5, 6)
    check(called(c, 'deploy') == 0 and c.X.held and c.X.held[1] == 2, 'a second choice within 999 ms changes nothing')
    c.time = c.time + 1
    c.M.OnEndMissionPreparation(5, 6)
    check(called(c, 'deploy') == 1 and c.deploy[1] == 5 and c.deploy[2] == 6 and called(c, 'clear') == 1 and
        c.clear.nextMissionId == 30020 and c.X.held == nil and noted(c, 8, 30020, 1) == 1 and not c.X.pass,
        'chosen again at +1 s: the original runs once with the new arguments, its inner clear passes')
    c.time = c.time + 200000; c.F.tick(c.time)
    check(called(c, 'deploy') == 1, 'nothing more')
    local moved = heli()
    moved.M.OnEndMissionPreparation(2, nil)
    moved.env.gvars.mis_nextMissionCodeForMissionClear = 30010
    moved.time = moved.time + 100
    moved.M.OnEndMissionPreparation(1, 2)
    check(called(moved, 'deploy') == 1 and moved.deploy[1] == 1 and moved.X.held == nil and noted(moved, 8) == 0,
        'an allowed new target (the host mission) runs at once without a note')
    local unlock = heli()
    unlock.M.OnEndMissionPreparation(2, nil)
    unlock.r13 = 0
    at(unlock, unlock.time + 499)
    check(called(unlock, 'deploy') == 0, 'checked every 500 ms')
    at(unlock, unlock.time + 1)
    check(called(unlock, 'deploy') == 1 and unlock.deploy[1] == 2 and unlock.deploy[2] == nil and noted(unlock, 8, 30020, 2) == 1 and
        logged(unlock, 'decision=unlock') == 1 and called(unlock, 'clear') == 1, 'r13 dropping to 0: the stored original, note 8 b=2')
    local slow = heli()
    slow.M.OnEndMissionPreparation(2, nil)
    local t0 = slow.time
    for step = 500, 119500, 500 do at(slow, t0 + step) end
    check(called(slow, 'deploy') == 0 and noted(slow, 12) == 40, 'held up to 120 s, note 12 re-posted every 3 s')
    at(slow, t0 + 120000)
    check(called(slow, 'deploy') == 1 and noted(slow, 8, 30020, 3) == 1 and logged(slow, 'decision=timeout') == 1, '120 s: leaves alone (note 8 b=3)')
    local follow = heli()
    follow.M.OnEndMissionPreparation(2, nil)
    follow.W.paired = true
    at(follow, follow.time + 33)
    check(called(follow, 'deploy') == 0 and follow.X.held == nil and logged(follow, 'decision=follow') == 1,
        'W.paired drops the hold with no original call')
    local queued = heli()
    queued.M.OnEndMissionPreparation(2, nil)
    queued.F.queue[1] = {9, 1, 40050, 30010, since = queued.time}
    at(queued, queued.time + 33)
    check(called(queued, 'deploy') == 0 and queued.X.held == nil, 'a queued follow drops the hold')
    local lost = heli()
    lost.M.OnEndMissionPreparation(2, nil)
    lost.link = 0
    at(lost, lost.time + 500)
    check(called(lost, 'deploy') == 1 and logged(lost, 'decision=allow') == 1 and noted(lost, 8) == 0,
        'the link lost: the lock no longer applies, the sortie leaves')
    local inactive = heli()
    inactive.M.OnEndMissionPreparation(2, nil)
    inactive.W.active = false
    inactive.M.OnEndMissionPreparation(3, nil)
    check(called(inactive, 'deploy') == 1 and inactive.deploy[1] == 3 and inactive.X.held == nil,
        'world sync stopped: choosing again runs at once and drops the hold')
    local stale = heli()
    stale.M.OnEndMissionPreparation(2, nil)
    stale.W.mission = 30020
    for _, f in ipairs(stale.W.startHooks) do f() end
    check(stale.X.held == nil and called(stale, 'deploy') == 0, 'a mission start drops the hold')
    local emergency = heli()
    emergency.env.gvars.mis_nextMissionCodeForEmergency = 50050
    emergency.M.OnEndMissionPreparation(2, nil)
    check(emergency.X.held and emergency.X.held.target == 50050 and noted(emergency, 12, 50050, 30010) == 1, 'the emergency code is the target')
    local host = fixture(1, {title = false})
    host.W.paired = true
    host.M.OnEndMissionPreparation(2, nil)
    check(called(host, 'deploy') == 1 and host.X.held == nil, 'the host sortie never reaches the lock hold')
    -- Review lua-flow-2: the prep screen cancelled (mission 0) or changed since the hold:
    -- the vanilla deploy would re-read the gvars, so the old choice never flies.
    for _, case in ipairs({{'cancelled', 0, 120000}, {'changed', 30050, 500}, {'cancelled, then unlocked', 0, 500}}) do
        local s = heli()
        s.M.OnEndMissionPreparation(2, nil)
        s.env.gvars.mis_nextMissionCodeForMissionClear = case[2]
        if case[1]:find('unlocked', 1, true) then s.r13 = 0 end
        at(s, s.time + case[3])
        at(s, s.time + 1)
        check(called(s, 'deploy') == 0 and s.X.held == nil and logged(s, 'decision=stale') == 1 and noted(s, 8) == 0,
            case[1] .. ': the stale hold is dropped, no deploy')
        at(s, s.time + 200000)
        check(called(s, 'deploy') == 0 and noted(s, 12) == 1, case[1] .. ': nothing later either')
    end
    -- Review lua-flow-3: world sync stopped (W.fail) while held: released once with the
    -- stored arguments (world_sync's inactive update calls X.hold), even when a follow
    -- state is left over; never a follow drop.
    local failed = heli()
    failed.M.OnEndMissionPreparation(2, 7)
    failed.F.current = {1}
    failed.W.paired = true
    failed.W.fail('boom')
    failed.X.hold()
    check(called(failed, 'deploy') == 1 and failed.deploy[1] == 2 and failed.deploy[2] == 7 and failed.X.held == nil and
        logged(failed, 'decision=inactive') == 1 and logged(failed, 'decision=follow') == 0 and noted(failed, 8, 30020, 2) == 1,
        'inactive: the held sortie leaves alone with its stored arguments')
    failed.X.hold()
    check(called(failed, 'deploy') == 1, 'released once')
    local cancelled = heli()
    cancelled.M.OnEndMissionPreparation(2, nil)
    cancelled.env.gvars.mis_nextMissionCodeForMissionClear = 0
    cancelled.W.active = false
    cancelled.X.hold()
    check(called(cancelled, 'deploy') == 0 and cancelled.X.held == nil and logged(cancelled, 'decision=stale') == 1,
        'inactive but cancelled in the prep screen: dropped, not flown')
end

-- 8. The loading choke point: op 2 CONTINUE, the final report and the warnings.
do
    local h = fixture(1, {mission = 30010, title = false})
    h.env.vars.missionCode = 10020
    local a, b, d = h.env.e.ReservePlayerLoadingPosition(1)
    check(a == 7 and b == nil and d == 9 and called(h, 'reserve') == 1, 'original returns preserved')
    check(noted(h, 6, 10020, 30010) == 1 and logged(h, 'event=handoff;step=warn;reason=partner_cannot_join;target=10020') == 1,
        'host loading into 10020 while linked: note 6 and the warn line')
    h.env.e.ReservePlayerLoadingPosition(1)
    check(noted(h, 6) == 1, 'once per target')
    h.env.vars.missionCode = 10036
    h.env.e.ReservePlayerLoadingPosition(2)
    check(noted(h, 6, 10036, 30010) == 1, 'another target warns once')
    h.env.vars.missionCode = 30020
    h.env.e.ReservePlayerLoadingPosition(1)
    check(noted(h, 6) == 2, 'a supported target never warns')
    local alone = fixture(1, {mission = 30010, title = false, peer = {mission = 30010, level = 3, wait = 3, linked = false, session = 55}})
    alone.env.vars.missionCode = 10020
    alone.env.e.ReservePlayerLoadingPosition(1)
    check(#alone.notes == 0, 'never unlinked')
    local hc = fixture(1)
    hc.env.vars.missionCode = 30010
    hc.env.e.ReservePlayerLoadingPosition(4)
    check(#saved(hc, 2) == 1 and same(saved(hc, 2)[1], {2, 30010, 1, 2, 0, 0}), 'host title Continue: op 2 with the restored target')
    check(same(hc.saves[#hc.saves], {1, 40050, 0, 2, 0, 0}), 'then a final report: leaving the title, not in game')
    -- The guest Continued alone into 30050 while the host is in 30010.
    local g = fixture(2)
    g.menu.Continue(); g.time = g.time + 2000; g.menu.Continue()
    -- The vanilla OnEndFadeOutSelectContinue: VarRestore, ClearTitleMode, then the reserve.
    g.env.vars.missionCode, g.env.gvars.ini_isTitleMode = 30050, false
    g.env.e.ReservePlayerLoadingPosition(4)
    check(#saved(g, 2) == 1 and same(saved(g, 2)[1], {2, 30050, 1, 2, 0, 0}) and noted(g, 11, 30050, 30010) == 1 and
        logged(g, 'step=warn;reason=landing_elsewhere;target=30050') == 1, 'guest landing elsewhere: op 2 (title=1 latched) and note 11')
    local meet = fixture(2, {mission = 40010})
    meet.env.vars.missionCode = 30010
    meet.env.e.ReservePlayerLoadingPosition(4)
    check(#saved(meet, 2) == 1 and noted(meet, 11) == 0, 'landing in the host mission: no warning')
    local replay = fixture(2)
    replay.env.vars.missionCode = 30050
    replay.F.run(function() replay.env.e.ReservePlayerLoadingPosition(4) end)
    -- An accepted travel whose seeds world_follow applies before the inner wrapper.
    replay.F.current = {1, 4, 40050, 30050, 0, 0, 0, 2, 0, 0, 8, session = 55}
    replay.F.seeds[1] = {mission = 30050, values = {}, ready = true}
    replay.env.e.ReservePlayerLoadingPosition(4)
    check(called(replay, 'reserve') == 2, 'both loads reached the original')
    check(#saved(replay, 2) == 0 and noted(replay, 11) == 0, 'a replayed Continue binds nothing and warns nothing')
    local off = fixture(2, {r13 = 0})
    off.env.vars.missionCode = 30050
    off.env.e.ReservePlayerLoadingPosition(4)
    check(#saved(off, 2) == 1 and noted(off, 11) == 0, 'lock off: op 2 still sent, no warning')
    local restart = fixture(2, {mission = 30010, title = false})
    restart.env.e.ReservePlayerLoadingPosition(3)
    check(#saved(restart, 2) == 0 and same(restart.saves[1], {1, 30010, 0, 2, 0, 0}), 'restart: only the final report (not in game)')
end

-- 9. The title replay hold (F.replayHold, world_follow's guest replay).
do
    local c = fixture(2, {mission = 40010})
    check(c.X.replayHold() == false, 'r12 0: no hold')
    for _, h in ipairs({1, 2, 3, 4}) do
        c.r12 = h; c.time = c.time + 250
        check(c.X.replayHold() == true, 'r12 ' .. h .. ': held at the title')
    end
    check(logged(c, 'event=handoff;step=replay_hold;code=1') == 1 and logged(c, 'step=replay_hold;code=4') == 1, 'logged once per change')
    c.time = c.time + 100
    check(c.X.replayHold() == true and logged(c, 'step=replay_hold') == 4, 'cached read, no new line')
    c.r12 = 5; c.time = c.time + 250
    check(c.X.replayHold() == false and logged(c, 'step=replay_hold;code=0') == 1, 'r12 5: released')
    c.r12 = 3; c.time = c.time + 250
    c.env.gvars.ini_isTitleMode = false
    check(c.X.replayHold() == false, 'not a title: no hold')
    local h = fixture(1, {mission = 40010, r12 = 3})
    check(h.X.replayHold() == false, 'never the host')
    -- The real follow tick keeps a queued host Continue while held.
    local g = fixture(2, {mission = 40010, r12 = 3, paired = true})
    g.W.handlers[OP.TRANSITION]({1, 4, 40010, 30010, 0, 0, 0, 2, 0, 0, 8}, g.time)
    g.F.seeds[1] = {mission = 30010, values = {}, ready = true}
    at(g, g.time + 33)
    check(#g.F.queue == 1 and not g.F.current and called(g, 'continue') == 0, 'held: the transition stays queued')
    g.r12 = 0
    at(g, g.time + 300)
    check(g.F.current and called(g, 'continue') == 1, 'released: the replay runs (through the lock, F.token)')
end

-- 10. An armed guest's title select stays suppressed (it follows the host's
-- Continue) and still declines a pending handoff when pressed twice.
do
    local c = fixture(2, {mission = 40010, r12 = 3, paired = true})
    c.menu.Continue()
    check(called(c, 'select_continue') == 0 and noted(c, 9, 0, 0) == 1 and #saved(c, 3) == 0 and c.tx[1].op == OP.REQ_MISSION and
        called(c, 'reboot_menu') == 1 and logged(c, 'fn=TitleContinue;kind=4;target=30010;host=0;decision=armed') == 1,
        'first press: suppressed (REQ_MISSION), note 9, the menu reopened')
    c.time = c.time + 500
    c.menu.Continue()
    check(#saved(c, 3) == 0 and noted(c, 9) == 1, '+0.5 s: nothing')
    c.time = c.time + 1500
    c.menu.Continue()
    check(called(c, 'select_continue') == 0 and #saved(c, 3) == 1 and noted(c, 10) == 1 and
        logged(c, 'fn=TitleContinue;kind=4;target=30010;host=0;decision=decline') == 1, '+2 s: SAVE op 3 and note 10, still suppressed')
    local idle = fixture(2, {mission = 40010, r12 = 1, paired = true})
    idle.menu.Continue(); idle.time = idle.time + 2000; idle.menu.Continue()
    check(#idle.notes == 0 and #saved(idle, 3) == 0 and called(idle, 'select_continue') == 0,
        'waiting for the host (r12 1): suppressed, no handoff note')
    local heli = fixture(2, {mission = 40010, paired = true})
    heli.menu.RestartHeli()
    check(called(heli, 'select_heli') == 1 and #heli.tx == 0, 'an armed RestartHeli does not travel: allowed')
end

print('world_handoff_lua_check: ' .. checks .. ' checks passed')
