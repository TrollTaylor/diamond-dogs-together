-- W4 alignment against the real outcome/phase apply rules, recording APIs.
-- Usage: lua51 this.lua world_align.lua world_enemy.lua world_outcome.lua world_phase.lua world_recovery.lua world_probe.lua
--        [world_place.lua world_place_guest.lua] (spawn sync, docs/debug-menu.md)
local checks = 0
local function check(ok, why) checks = checks + 1; if not ok then error(checks .. ': ' .. why, 2) end end
local OP = {RESYNC = 5, RESYNC_END = 6, OUTCOME = 16, REQ_OUTCOME = 17, PHASE = 18, REQ_PHASE = 19,
    AREA = 20, PVEC = 23, ALIGN_BEGIN = 14, ALIGN_END = 15, ALIGN_RECORD = 24, ALIGN_ACK = 25, REJECT = 4, RECEIVED = 3,
    GAME_OVER = 26, REQ_GAME_OVER = 27, CHECKPOINT = 28, PROBE = 116, PROBE_FIRE = 117, PROBE_SYNC = 8,
    PAIRED = 113, UNPAIRED = 114}
local function fixture(host, level)
    local c = {now = 1000, tx = {}, logs = {}, commands = {}, life = {}, action = {}, phase = {cp_a = 10, cp_b = 10},
        positions = {}, receipts = {}, warp = {}, enemyCalls = {}, setOut = {}}
    local env = setmetatable({e = {}, vars = {playerPosX = 12.25, playerPosY = 30.1, playerPosZ = -8.5, playerRotY = -45.125},
        mvars = {mis_checkPointList = {cp_start = 1}}, svars = {}, gvars = {mis_checkPoint = 444},
        TppDefine = {GAME_OVER_TYPE = {PLAYER_DEAD = 1, PLAYER_FALL_DEAD = 2}, GAME_OVER_RADIO = {PLAYER_DEAD = 1}},
        TppMission = {}, TppCheckPoint = {}, TppPlayer = {}}, {__index = _G})
    local W = {OP = OP, handlers = {}, startHooks = {}, pairedHooks = {}, unpairedHooks = {}, stopHooks = {}, bindHooks = {},
        active = true, level = level or 3, host = host, role = host and 1 or 2, feature = {enemies = true}, epoch = 7,
        paired = true, eff = level or 3, mode = 1, mission = 30010, roster = 4000000000, rosterOk = true, w1ok = true, w1 = true,
        ent = {}, entOrder = {}, cp = {}, cpOrder = {100, 200}, activeCp = {[100] = true, [200] = not host},
        areaState = {}, attribOverride = {}, observers = {}, flags = 0, isStarted = true, t0 = 0,
        lifeWire = {[1] = 5, [2] = 4, [3] = 2, [4] = 3}, lifeConst = {[5] = 1, [4] = 2, [2] = 3, [3] = 4},
        phaseWire = {[10] = 0, [11] = 1, [12] = 2, [13] = 3}, phaseConst = {[0] = 10, [1] = 11, [2] = 12, [3] = 13},
        stats = {commits = 0, reqs = 0, applies = 0, noops = 0, ledger = 0, div = 0, obs = 0}}
    for i = 1, 5 do
        local h = 3000000000 + i
        W.entOrder[i] = h
        W.ent[h] = {name = 'sol_' .. i, id = i, cp = i == 5 and 200 or 100, committed = 0, ver = 0, known = 0, kver = 0, reached = false}
        c.life[i] = 0
    end
    for i, h in ipairs(W.cpOrder) do
        W.cp[h] = {name = i == 1 and 'cp_a' or 'cp_b', id = 10 + i, idx = i, localPhase = 0, hostPhase = 0,
            own = 0, guestOwn = 0, ver = 0}
    end
    W.api = {null = 65535, fultonRecovered = 10, isStart = function() return c.continued ~= true end, life = function(id) return c.life[id] end,
        recovered = function() return false end, action = function(id) return c.action[id] end,
        getPhase = function(name) return c.phase[name] end,
        setOut = function(list, out) c.setOut[#c.setOut + 1] = {list[1], out} end,
        send = function(id, command)
            if command.id == 'GetPosition' then
                local p = c.positions[id] or {0, 0, 0}
                return {GetX = function() return p[1] end, GetY = function() return p[2] end, GetZ = function() return p[3] end}
            end
            c.commands[#c.commands + 1] = {id, command.id, command.state or command.phase, command.forceRealize, command.route}
            if command.id == 'ChangeLifeState' then c.life[id] = command.state
            elseif command.id == 'RequestForceFulton' then c.action[id] = 10
            elseif command.id == 'SetPhase' then c.phase[id == 11 and 'cp_a' or 'cp_b'] = command.phase end
        end}
    W.get = function(t, k) return t and t[k] end
    W.now = function() return c.now end
    W.nextRid = function() c.rid = (c.rid or 0) + 1; return c.rid end
    W.log = function(s) c.logs[#c.logs + 1] = s end
    W.tx = function(op, ...)
        if c.failOp == op then c.failOp = nil; c.failures = (c.failures or 0) + 1; return false, 6 end
        c.tx[#c.tx + 1] = {op = op, v = {...}}
        return true
    end
    W.w1off = function() W.w1 = false end
    W.inGame = function() return c.inGame ~= false end
    -- Spawn sync receipts: WORLD_WARP_V1 answers from c.warp (FIFO, the last one repeats), REMOTE_V1 rows.
    W.receipt = function(source)
        c.receipts[#c.receipts + 1] = source
        if source:find("'MGS_COOP_WORLD_WARP_V1'", 1, true) then
            local r = #c.warp > 1 and table.remove(c.warp, 1) or c.warp[1]
            if r == 'literal' then return source:match("^return '([^']+)'") end
            return r
        end
        if source:find("'MGS_COOP_DEBUG_REMOTE_V1'", 1, true) then return true end
        return nil
    end
    env.TppEnemy = {}
    for _, k in ipairs({'SetEnableRestrictNotice', 'SetDisableRestrictNotice', 'SetDisable', 'SetEnable'}) do
        env.TppEnemy[k] = function(id) c.enemyCalls[#c.enemyCalls + 1] = k .. ':' .. tostring(id) end
    end
    env.e.MgsCoopDebug = {enemy = {used = {}, ignored = {}, marked = {}}}
    W.h = function(name) return name == 'cp_start' and 444 or 555 end
    env.TppMission.ReserveGameOver = function(kind, radio)
        c.reserved = (c.reserved or 0) + 1
        c.gameType, c.gameRadio = kind, radio
        return c.gameRefuse ~= true
    end
    env.TppPlayer.SetInitialPosition = function(pos, rot)
        env.vars.initialPlayerPosX, env.vars.initialPlayerPosY, env.vars.initialPlayerPosZ, env.vars.initialPlayerRotY = pos[1], pos[2], pos[3], rot
        c.setInitial = (c.setInitial or 0) + 1
    end
    env.TppMission.VarSaveOnUpdateCheckPoint = function()
        c.saves = (c.saves or 0) + 1
        c.savePose = {env.vars.initialPlayerPosX, env.vars.initialPlayerPosY, env.vars.initialPlayerPosZ, env.vars.initialPlayerRotY}
        if c.saveReadbackX ~= nil then env.vars.initialPlayerPosX = c.saveReadbackX end
        if c.saveFalse then return false end
        if c.saveNil then return end
        return 7, nil, 9
    end
    env.TppCheckPoint.Update = function(options)
        c.checkpointCalls = (c.checkpointCalls or 0) + 1
        c.checkpointOptions = options
        if c.checkpointRefuse then return c.refusalReturn end
        if options.atCurrentPosition then
            env.gvars.mis_checkPoint = 0
            env.TppPlayer.SetInitialPosition({env.vars.playerPosX, env.vars.playerPosY + 0.5, env.vars.playerPosZ}, env.vars.playerRotY)
        else env.gvars.mis_checkPoint = W.h(options.checkPoint) end
        if c.wrongHash then env.gvars.mis_checkPoint = 999 end
        return env.TppMission.VarSaveOnUpdateCheckPoint()
    end
    env.TppMission.UpdateCheckPoint = function(options) return env.TppCheckPoint.Update(options) end
    env.TppMission.ContinueFromCheckPoint = function(options) c.continueCalls = (c.continueCalls or 0) + 1; c.continueOptions = options end
    env.e.MgsCoopWorld, W.follow = W, {installHooks = {}, token = false}
    W.follow.run = function(fn, ...)
        W.follow.token = true
        local result = {pcall(fn, ...)}
        W.follow.token = false
        return unpack(result)
    end
    for _, path in ipairs({arg[2], arg[3], arg[4], arg[6], arg[1], arg[5], arg[7], arg[8]}) do
        local chunk = assert(loadfile(path)); setfenv(chunk, env); chunk()
    end
    W.align.install({level or 3})
    if W.recovery then W.recovery.install({level or 3}) end
    for _, f in ipairs(W.startHooks) do f() end
    W.activeCp = {[100] = true, [200] = not host}
    for _, f in ipairs(W.pairedHooks) do f(false) end
    c.W, c.A, c.R, c.env = W, W.align, W.recovery, env
    return c
end
local function send(c, p)
    local v = {}
    for i, n in ipairs(p.v) do v[i] = n end
    if p.op == OP.ALIGN_BEGIN or p.op == OP.ALIGN_END then v[#v + 1] = c.W.epoch end
    local fn = c.W.handlers[p.op]
    if fn then fn(v, c.now) end
end
local function capture(h)
    for _ = 1, 20 do h.now = h.now + 33; h.A.tick(h.now) end
end
local function transfer(h, g, filter)
    for _, p in ipairs(h.tx) do if not filter or filter(p) then send(g, p) end end
end
local function commands(c, name)
    local n = 0
    for _, a in ipairs(c.commands) do if a[2] == name then n = n + 1 end end
    return n
end
local function logged(c, text)
    for _, line in ipairs(c.logs) do if line:find(text, 1, true) then return true end end
    return false
end
local function logCount(c, text)
    local n = 0
    for _, line in ipairs(c.logs) do if line:find(text, 1, true) then n = n + 1; check(#line < 256, 'recovery receipt stays bounded') end end
    return n
end
local h, g = fixture(true), fixture(false)
h.life[1], h.life[2], h.action[3] = 1, 3, 10
h.phase.cp_a = 13
g.life[4] = 1
check(not h.W.w1 and not g.W.w1, 'both hold W1 until host checkpoint truth is sent')
capture(h)
transfer(h, g, function(p) return p.op ~= OP.ALIGN_END end)
check(not g.W.w1 and #g.commands == 0, 'partial snapshot never enables W1 or applies game commands')
local endPacket
for _, p in ipairs(h.tx) do if p.op == OP.ALIGN_END then endPacket = p end end
check(endPacket ~= nil and h.A.tx.ended, 'host ends only after complete bounded scan and records')
send(g, endPacket)
check(g.W.w1 and logged(g, 'unknown=1') and logged(g, 'action=no_revive'), 'complete snapshot enables guest and names unknown/divergent entities')
check(g.life[4] == 1 and g.phase.cp_a == 13, 'guest terminal soldier stays dead; phase floor follows host')
g.W.enemy.tick(g.now)
g.now = g.now + 300; g.W.enemy.tick(g.now)
check(g.life[1] == 1 and g.life[2] == 3 and g.action[3] == 10, 'real apply rules kill, sleep and Fulton initial checkpoint soldiers')
check(commands(g, 'RequestForceFulton') == 1, 'Fulton issued once through guarded apply/readback')
g.A.tick(g.now)
transfer(g, h, function(p) return p.op == OP.ALIGN_ACK end)
check(h.W.w1 and h.A.tx == nil and g.W.enemy.s.seen == h.W.enemy.s.seq, 'validated ACK resumes host and aligns sequence digest')
h.W.activeCp[200], h.life[5] = true, 1
h.A.tick(h.now)
h.A.tick(h.now)
check(h.W.ent[h.W.entOrder[5]].committed == 5 and logged(h, 'step=discovered'), 'unknown out-of-area soldier is queried after activation')
transfer(h, g, function(p) return p.op == OP.OUTCOME end)
g.W.enemy.tick(g.now)
g.now = g.now + 300; g.W.enemy.tick(g.now)
check(g.life[5] == 1 and commands(g, 'RequestForceFulton') == 1, 'later discovery applies forward without repeated Fulton')
local missing = fixture(false)
transfer(h, missing, function(p) return not (p.op == OP.ALIGN_RECORD and p.v[2] == 1 and p.v[3] == 2) end)
check(not missing.W.w1 and logged(missing, 'reason=incomplete_snapshot') and #missing.commands == 0, 'missing record refuses alignment and every side effect')
check(logCount(missing, 'soldiers=4/5;cps=2/2;readable=3;invalid=0;digest_match=0') == 1,
    'truncated snapshot reports bounded received/expected counts and digest failure')
local wrong = fixture(false)
transfer(h, wrong, function(p) return p.op ~= OP.ALIGN_END end)
wrong.W.epoch = 8
send(wrong, endPacket)
check(not wrong.W.w1 and #wrong.commands == 0, 'stale epoch cannot enable W1')
local corrupt = fixture(false)
transfer(h, corrupt, function(p) return p.op ~= OP.ALIGN_END end)
local bad = {op = OP.ALIGN_RECORD, v = {h.A.serial, 1, 1, 3000000001, 1, 0, 1}}
send(corrupt, bad); send(corrupt, endPacket)
check(not corrupt.W.w1 and #corrupt.commands == 0, 'conflicting duplicate record invalidates snapshot')
local pressure = fixture(true)
pressure.failOp = OP.ALIGN_BEGIN
pressure.A.tick(pressure.now)
check(not pressure.A.tx.begun, 'queue pressure retries BEGIN')
pressure.A.tick(pressure.now)
while not pressure.A.tx.captured do pressure.A.tick(pressure.now) end
pressure.failOp = OP.ALIGN_RECORD
pressure.A.tick(pressure.now)
check(pressure.A.tx.sent == 1 and not pressure.A.tx.ended, 'refused record does not advance stream')
pressure.failOp = OP.ALIGN_END
pressure.A.tick(pressure.now)
check(not pressure.A.tx.ended, 'refused END is not reported complete')
pressure.A.tick(pressure.now)
check(pressure.A.tx.ended and pressure.failures == 3, 'stream retries refused END successfully')
local off = fixture(false, 0)
check(off.W.w1 and off.A.rx == nil, 'level zero does not alter base W1 state')
off.W.handlers[OP.ALIGN_BEGIN]({1, 30010, off.W.roster, 5, 2, 7}, off.now)
check(off.A.rx == nil and off.W.w1 and #off.commands == 0, 'level-zero align messages are inert')
local reconnect = fixture(false)
transfer(h, reconnect, function(p) return p.op ~= OP.ALIGN_END end)
for _, f in ipairs(reconnect.W.unpairedHooks) do f(1) end
check(reconnect.A.rx == nil and not reconnect.W.w1, 'unpair discards partial alignment')
send(reconnect, endPacket)
check(not reconnect.W.w1, 'orphan END after unpair cannot enable W1')
local mixed = fixture(true)
mixed.W.w1, mixed.W.eff = true, 2
mixed.A.reset(); mixed.A.start()
check(mixed.W.w1, 'level3/2 fresh start keeps the established level2 world sync')
mixed.continued, mixed.W.w1 = true, true
mixed.A.reset(); mixed.A.start()
check(not mixed.W.w1, 'level3/2 Continue retains the level2 checkpoint gate')
mixed.continued, mixed.W.w1, mixed.W.eff = false, true, 1
mixed.A.reset(); mixed.A.start()
check(not mixed.W.w1, 'roster mismatch effective level1 never restores world sync')
local empty = fixture(true)
empty.W.entOrder, empty.W.cpOrder, empty.W.w1 = {}, {}, true
empty.A.reset(); empty.A.start(); capture(empty)
check(not empty.W.w1 and empty.A.tx == nil and #empty.tx == 0, 'empty roster holds W1 without empty acceptance stream')
local unknown, untouched = fixture(true), fixture(false)
unknown.W.activeCp = nil
capture(unknown); transfer(unknown, untouched)
check(not untouched.W.w1 and #untouched.commands == 0 and logged(untouched, 'reason=no_readable_state'),
    'unknown active area cannot prove readiness by numeric NORMAL fallback')
check(logCount(untouched, 'soldiers=5/5;cps=2/2;readable=0;invalid=0;digest_match=1') == 1,
    'complete all-unknown snapshot is distinguished from lost or malformed records')
untouched.A.tick(untouched.now)
transfer(untouched, unknown, function(p) return p.op == OP.ALIGN_ACK end)
unknown.tx, untouched.tx = {}, {}
unknown.W.activeCp, unknown.life[1] = {[100] = true}, 1
capture(unknown); transfer(unknown, untouched)
untouched.W.enemy.tick(untouched.now)
untouched.now = untouched.now + 300; untouched.W.enemy.tick(untouched.now)
check(untouched.W.w1 and untouched.life[1] == 1 and logged(untouched, 'state=5;ver=1;src=resync;result=ok;readback=5'),
    'delayed area readiness permits a later complete snapshot and verifies restored host death through real apply readback')
for _, raw in ipairs({255, -1, 99}) do
    local sentinel = fixture(true)
    sentinel.life[1], sentinel.life[2], sentinel.life[3], sentinel.life[4] = raw, raw, raw, raw
    capture(sentinel)
    local unreadable = 0
    for _, p in ipairs(sentinel.tx) do
        if p.op == OP.ALIGN_RECORD and p.v[2] == 1 and p.v[3] <= 4 and p.v[5] == 0 then unreadable = unreadable + 1 end
    end
    check(unreadable == 4 and sentinel.W.ent[3000000001].ver == 0,
        'raw sentinel ' .. raw .. ' remains unreadable and never commits NORMAL')
    local unreadable = fixture(false)
    unreadable.W.w1, unreadable.life[1] = true, raw
    local soldier = unreadable.W.entOrder[1]
    unreadable.W.enemy.apply(soldier, 5, 1, 'resync')
    unreadable.W.enemy.tick(unreadable.now)
    check(#unreadable.commands == 0 and unreadable.W.enemy.s.ledger[soldier].reason == 'unreadable',
        'real apply defers raw sentinel ' .. raw .. ' without changing game state')
    unreadable.life[1], unreadable.now = 0, unreadable.now + 500
    unreadable.W.enemy.tick(unreadable.now)
    unreadable.now = unreadable.now + 300; unreadable.W.enemy.tick(unreadable.now)
    check(unreadable.life[1] == 1 and commands(unreadable, 'ChangeLifeState') == 1 and not unreadable.W.enemy.s.ledger[soldier],
        'deferred sentinel ' .. raw .. ' applies once only after verified readable NORMAL and readback')
    local hostUnknown = fixture(true)
    local ent = hostUnknown.W.ent[hostUnknown.W.entOrder[1]]
    ent.committed, ent.ver, ent.reached, hostUnknown.life[1] = 2, 1, true, raw
    check(hostUnknown.W.enemy.check(hostUnknown.W.entOrder[1], 9, true) == false and ent.committed == 2 and ent.ver == 1,
        'host raw sentinel ' .. raw .. ' cannot create a false wake-up commit')
end
local function pairReady(host, guest)
    capture(host); transfer(host, guest)
    guest.A.tick(guest.now)
    transfer(guest, host, function(p) return p.op == OP.ALIGN_ACK end)
    host.tx, guest.tx = {}, {}
end
local deadHost, deadGuest = fixture(true), fixture(false)
pairReady(deadHost, deadGuest)
check(deadGuest.env.TppMission.ReserveGameOver(2, 1) == false and not deadGuest.reserved and deadGuest.R.req,
    'guest fall-death reserves a host request and suppresses local game over')
deadGuest.failOp = OP.REQ_GAME_OVER; deadGuest.A.tick(deadGuest.now)
check(deadGuest.R.req ~= nil, 'shared death request retains payload on TX refusal')
deadGuest.A.tick(deadGuest.now)
transfer(deadGuest, deadHost, function(p) return p.op == OP.REQ_GAME_OVER end)
check(deadHost.reserved == 1 and deadHost.gameType == 2 and deadHost.R.dead and not deadHost.W.w1,
    'host accepts guest death with exact vanilla ReserveGameOver and stops W1')
transfer(deadGuest, deadHost, function(p) return p.op == OP.REQ_GAME_OVER end)
check(deadHost.reserved == 1, 'duplicate guest death cannot reserve game over twice')
deadHost.failOp = OP.GAME_OVER; deadHost.A.tick(deadHost.now)
check(deadHost.R.game ~= nil, 'host game-over broadcast retains payload on TX refusal')
deadHost.A.tick(deadHost.now); transfer(deadHost, deadGuest, function(p) return p.op == OP.GAME_OVER end)
check(deadGuest.reserved == 1 and deadGuest.R.dead and deadGuest.A.dead and not deadGuest.W.w1,
    'authorized broadcast reserves guest game over and cancels alignment')
transfer(deadHost, deadGuest, function(p) return p.op == OP.GAME_OVER end)
check(deadGuest.reserved == 1, 'duplicate host broadcast is idempotent')
check(logCount(deadGuest, 'step=game_over_applied;type=2;cause=2') == 1,
    'accepted guest game-over receipt records type/cause once across duplicate broadcasts')
local refuseDeath = fixture(true)
refuseDeath.gameRefuse = true
check(refuseDeath.env.TppMission.ReserveGameOver(1, 1) == false and not refuseDeath.R.dead and not refuseDeath.R.game,
    'vanilla refused host death does not broadcast fabricated success')
refuseDeath.W.handlers[OP.REQ_GAME_OVER]({1, 99, 1, 1})
check(refuseDeath.reserved == 1, 'guest request cannot invoke non-death game-over types')
local solo = fixture(false)
solo.W.paired = false
check(solo.env.TppMission.ReserveGameOver(1, 1) == true and solo.reserved == 1 and not solo.R.req,
    'unpaired guest preserves vanilla death behavior')
local retryHost, retryGuest = fixture(true), fixture(false)
pairReady(retryHost, retryGuest)
retryHost.gameRefuse = true
retryGuest.env.TppMission.ReserveGameOver(1, 1)
retryGuest.A.tick(retryGuest.now)
send(retryHost, retryGuest.tx[#retryGuest.tx])
check(retryHost.reserved == 1 and not retryHost.R.dead and retryGuest.R.req,
    'transient host refusal keeps guest death request pending after successful native TX')
local reqs = #retryGuest.tx
retryGuest.now = retryGuest.now + 499; retryGuest.A.tick(retryGuest.now)
check(#retryGuest.tx == reqs, 'death request retry is bounded to 500ms')
retryHost.gameRefuse = false
retryGuest.now = retryGuest.now + 1; retryGuest.A.tick(retryGuest.now)
send(retryHost, retryGuest.tx[#retryGuest.tx])
retryHost.A.tick(retryHost.now)
retryGuest.gameRefuse = true
send(retryGuest, retryHost.tx[#retryHost.tx])
check(retryHost.R.dead and retryGuest.reserved == 1 and not retryGuest.R.dead and retryGuest.R.inGame,
    'transient guest vanilla refusal retains authorized game-over broadcast')
check(not logged(retryGuest, 'step=game_over_applied;'), 'refused guest game over cannot log an accepted receipt')
retryGuest.now = retryGuest.now + 499; retryGuest.A.tick(retryGuest.now)
check(retryGuest.reserved == 1, 'guest game-over retry is bounded to 500ms')
retryGuest.gameRefuse = false
retryGuest.now = retryGuest.now + 1; retryGuest.A.tick(retryGuest.now)
check(retryGuest.reserved == 2 and retryGuest.R.dead and not retryGuest.R.inGame and not retryGuest.R.req,
    'both reach shared game over after transient refusals and guest request stops')
local stopped = #retryGuest.tx
retryGuest.now = retryGuest.now + 1000; retryGuest.A.tick(retryGuest.now)
send(retryGuest, retryHost.tx[#retryHost.tx])
check(#retryGuest.tx == stopped and retryGuest.reserved == 2,
    'accepted game over stops retries and duplicate broadcast never reserves again')
check(logCount(retryGuest, 'step=game_over_applied;type=1;cause=2') == 1,
    'game-over receipt appears once only after the retry returns actual true')
local inert = fixture(true, 0)
inert.W.handlers[OP.REQ_GAME_OVER]({1, 1, 1, 1})
inert.W.handlers[OP.GAME_OVER]({1, 1, 1})
inert.W.handlers[OP.CHECKPOINT]({0, 1, 1, 1, 1, 1}); inert.A.tick(inert.now)
check(not inert.reserved and not inert.saves and #inert.tx == 0, 'inert recovery wire handlers never call original game APIs')
local cpHost, cpGuest = fixture(true), fixture(false)
pairReady(cpHost, cpGuest)
local a, b, c = cpHost.env.TppMission.VarSaveOnUpdateCheckPoint()
check(a == 7 and b == nil and c == 9 and cpHost.R.out[1] == 444,
    'accepted host checkpoint preserves exact original multiple return values')
cpHost.A.tick(cpHost.now); transfer(cpHost, cpGuest, function(p) return p.op == OP.CHECKPOINT end)
cpGuest.checkpointRefuse = true
cpGuest.A.tick(cpGuest.now)
check(cpGuest.R.seen == 0 and cpGuest.R.pending and not cpGuest.saves,
    'nil checkpoint refusal without actual VarSave receipt stays pending')
cpGuest.refusalReturn = false; cpGuest.now = cpGuest.now + 500; cpGuest.A.tick(cpGuest.now)
check(cpGuest.R.seen == 0 and cpGuest.R.pending and cpGuest.checkpointCalls == 2,
    'false checkpoint refusal also retries without advancing version')
cpGuest.checkpointRefuse, cpGuest.wrongHash = false, true
cpGuest.now = cpGuest.now + 500; cpGuest.A.tick(cpGuest.now)
check(cpGuest.R.seen == 0 and cpGuest.R.pending, 'save with a different checkpoint hash is not accepted')
cpGuest.wrongHash, cpGuest.saveFalse = false, true
cpGuest.now = cpGuest.now + 500; cpGuest.A.tick(cpGuest.now)
check(cpGuest.R.seen == 0 and cpGuest.R.pending, 'explicit false from save receipt cannot acknowledge checkpoint')
check(not logged(cpGuest, 'receipt=1'), 'refused or mismatched checkpoints cannot log accepted save receipts')
cpGuest.saveFalse, cpGuest.saveNil = false, true
cpGuest.now = cpGuest.now + 500; cpGuest.A.tick(cpGuest.now)
check(cpGuest.R.seen == 1 and not cpGuest.R.pending and cpGuest.checkpointOptions.checkPoint == 'cp_start',
    'real accepted VarSave nil return counts only after actual save hook and matching hash')
local calls = cpGuest.checkpointCalls
transfer(cpHost, cpGuest, function(p) return p.op == OP.CHECKPOINT end)
cpGuest.A.tick(cpGuest.now)
check(cpGuest.checkpointCalls == calls, 'duplicate checkpoint version never saves twice')
cpGuest.env.TppCheckPoint.Update({atCurrentPosition = true})
check(cpGuest.checkpointCalls == calls, 'paired guest local checkpoint trap is suppressed')
cpHost.tx, cpGuest.tx = {}, {}
cpHost.env.TppMission.UpdateCheckPoint({atCurrentPosition = true})
cpHost.failOp = OP.CHECKPOINT; cpHost.A.tick(cpHost.now)
check(cpHost.R.out and cpHost.R.out[3] == 12250 and cpHost.R.out[4] == 30600 and cpHost.R.out[6] == -45125,
    'current checkpoint captures accepted host initial pose including vanilla height offset and retries TX')
check(not logged(cpHost, 'step=checkpoint_tx;hash=0;'), 'refused native TX cannot log an accepted checkpoint send')
cpHost.A.tick(cpHost.now); transfer(cpHost, cpGuest, function(p) return p.op == OP.CHECKPOINT end)
cpGuest.env.vars.playerPosX, cpGuest.env.vars.playerPosY, cpGuest.env.vars.playerPosZ = 99, 88, 77
cpGuest.A.tick(cpGuest.now)
check(cpGuest.R.seen == 2 and cpGuest.savePose[1] == 12.25 and cpGuest.savePose[2] == 30.6 and
    cpGuest.savePose[3] == -8.5 and cpGuest.savePose[4] == -45.125,
    'hash0 checkpoint stores host position and yaw before VarSave despite different guest current pose')
check(logCount(cpHost, 'step=checkpoint_tx;hash=0;ver=2;x_mm=12250;y_mm=30600;z_mm=-8500;yaw_mdeg=-45125') == 1 and
    logCount(cpGuest, 'step=checkpoint;hash=0;ver=2;receipt=1;x_mm=12250;y_mm=30600;z_mm=-8500;yaw_mdeg=-45125') == 1,
    'accepted live receipts expose matching host payload and guest initial-pose readback')
local readback = fixture(false)
readback.W.w1 = true
readback.saveReadbackX = 9.875
readback.W.handlers[OP.CHECKPOINT]({0, 1, 1100, 2200, 3300, 4400}); readback.A.tick(readback.now)
check(readback.R.seen == 1 and logged(readback, 'receipt=1;x_mm=9875;y_mm=2200;z_mm=3300;yaw_mdeg=4400'),
    'guest receipt reads actual post-save initial pose rather than echoing host wire payload')
readback.saveReadbackX = false
readback.W.handlers[OP.CHECKPOINT]({0, 2, 1100, 2200, 3300, 4400}); readback.A.tick(readback.now)
check(readback.R.seen == 2 and logged(readback, 'ver=2;receipt=1;pose=unreadable'),
    'accepted save with unreadable pose is labelled explicitly without invented coordinates')
local before = cpGuest.checkpointCalls
cpGuest.W.handlers[OP.CHECKPOINT]({0, 3, 10000001, 0, 0, 0}); cpGuest.A.tick(cpGuest.now)
check(cpGuest.checkpointCalls == before and cpGuest.R.seen == 2, 'out-of-bounds checkpoint pose is inert')
cpGuest.W.handlers[OP.CHECKPOINT]({555, 3, 0, 0, 0, 0}); cpGuest.A.tick(cpGuest.now)
check(cpGuest.R.pending and cpGuest.R.seen == 2 and cpGuest.checkpointCalls == before,
    'unknown named checkpoint waits instead of saving an invented point')
cpGuest.env.mvars.mis_checkPointList.cp_later = 2
cpGuest.now = cpGuest.now + 500; cpGuest.A.tick(cpGuest.now)
check(cpGuest.R.seen == 3 and cpGuest.checkpointOptions.checkPoint == 'cp_later',
    'named checkpoint retries after its exact engine name becomes available')
cpGuest.W.handlers[OP.CHECKPOINT]({0, 4, 1000, 2000, 3000, 4000})
for _, f in ipairs(cpGuest.W.unpairedHooks) do f(1) end
check(not cpGuest.R.pending and not cpGuest.R.req and not cpGuest.R.inGame,
    'unpair discards checkpoint and recovery requests from the old session')
local probeHost, probeGuest = fixture(true), fixture(false)
pairReady(probeHost, probeGuest)
local function probe(c, id) c.W.probe.schedule(id, c.now); c.W.probe.tick(c.now) end
probeHost.W.mode = 0; probe(probeHost, 16, 'shared_death')
check(not probeHost.reserved and logged(probeHost, 'result=refused'), 'play mode cannot run lab death probe')
probeHost.W.mode = 1; probe(probeGuest, 18, 'shared_checkpoint'); probe(probeGuest, 17, 'host_continue')
check(not probeGuest.checkpointCalls and not probeGuest.continueCalls, 'guest cannot execute host checkpoint or Continue probes')
probe(probeHost, 17, 'host_continue')
check(not probeHost.continueCalls, 'Continue probe requires shared game over first')
probe(probeHost, 18, 'shared_checkpoint')
check(probeHost.R.out and logged(probeHost, 'id=shared_checkpoint;step=done;result=ok'),
    'host lab checkpoint probe uses vanilla API and accepted save receipt')
probe(probeHost, 16, 'shared_death'); probe(probeHost, 17, 'host_continue')
check(probeHost.R.dead and probeHost.continueCalls == 1 and probeHost.continueOptions.isNoFade == true,
    'host lab death then Continue use exact vanilla entrypoints')
-- Spawn sync (lua/coop/world_place.lua, world_place_guest.lua; docs/debug-menu.md).
if arg[7] and arg[8] then
    local HASH = {cp_a = 100, cp_b = 200, quest_cp = 300, rt_a = 7001, rt_b = 7002}
    local function placeFixture(host)
        local c = fixture(host)
        c.W.h = function(name) return HASH[name] or #name * 1000 + name:byte(-1) end
        c.P, c.E = c.W.place, c.W.enemy
        c.W.handlers[OP.PAIRED]({7, 30010, c.W.roster, 5, 1, 3})
        return c
    end
    local function placeTx(c, op)
        local out = {}
        for _, p in ipairs(c.tx) do if p.op == op then out[#out + 1] = p end end
        return out
    end
    local function deliver(from, to, op)
        for _, p in ipairs(placeTx(from, op)) do
            local v = {}
            for i, n in ipairs(p.v) do v[i] = n end
            to.W.handlers[op](v, to.now)
        end
        local kept = {}
        for _, p in ipairs(from.tx) do if p.op ~= op then kept[#kept + 1] = p end end
        from.tx = kept
    end
    local function run(c, frames)
        for _ = 1, frames do c.now = c.now + 33; c.P.tick(c.now) end
    end
    local function receipt(c, text)
        for _, r in ipairs(c.receipts) do if r:find(text, 1, true) then return r end end
    end
    local function receiptCount(c, text)
        local n = 0
        for _, r in ipairs(c.receipts) do if r:find(text, 1, true) then n = n + 1 end end
        return n
    end
    local function sent(c, id, name, extra)
        local n = 0
        for _, a in ipairs(c.commands) do
            if a[1] == id and a[2] == name and (extra == nil or a[4] == extra) then n = n + 1 end
        end
        return n
    end
    local ENT1 = 3000000001
    -- Host: pairing and a matching roster are required, W1 is not.
    local h, g = placeFixture(true), placeFixture(false)
    check(h.P and g.P and h.P.share and g.P.guestTick, 'spawn sync chunks loaded')
    h.W.w1 = false
    h.W.handlers[OP.PAIRED]({7, 30010, h.W.roster, 5, 0, 3})
    check(h.P.share(1, ENT1, 5000, 1200, 6000, -3141, 2, nil, 900) == 'sync=none;why=roster' and #placeTx(h, 34) == 0,
        'a roster mismatch never announces')
    h.W.handlers[OP.PAIRED]({7, 30010, h.W.roster, 5, 1, 3})
    h.W.paired = false
    check(h.P.share(1, ENT1, 5000, 1200, 6000, -3141, 2) == 'sync=none;why=not_paired', 'unpaired host announces nothing')
    h.W.paired = true
    check(g.P.share(1, ENT1, 0, 0, 0, 0, 2) == 'sync=none;why=guest' and #placeTx(g, 34) == 0, 'the guest never announces')
    check(h.P.share(1, 999, 0, 0, 0, 0, 2) == 'sync=none;why=unknown_entity', 'unknown soldier refused')
    h.W.activeCp = {[200] = true}
    check(not h.E.active(100, ENT1), 'out-of-area soldier before placement')
    check(h.P.share(1, ENT1, 5000, 1200, 6000, -3141, 2, nil, 900) == 'sync=world;seq=1', 'warp announced while W1 is off')
    local warpTx = placeTx(h, 34)[1]
    check(warpTx and table.concat(warpTx.v, ' ') == '1 3000000001 1 5000 1200 6000 -3141 2 0 1',
        'ENEMY_DEBUG carries seq, hash, kind, the exact request, flags and the host GameObjectId')
    check(h.W.pinned[ENT1] and h.E.active(100, ENT1) and h.setOut[1][1] == 'sol_1' and h.setOut[1][2] == false,
        'the placed guard is kept in area on the host')
    local outs = #h.setOut
    h.W.area.update({cpList = {[1] = {cp_b = true}}}, 1, true, nil)
    check(#h.setOut == outs + 1 and h.setOut[#h.setOut][1] == 'sol_1', 'every area update re-pins the placed guard')
    -- Guest: unrealized first (force-realize once), then landed; readback, release, ACK, history row.
    g.warp = {{r = -1, h = 0}, {r = -1, h = 0}, {r = 0, h = 1, leaf = 'pinned'}}
    g.positions[1] = {5.0, 1.3, 6.0}
    deliver(h, g, 34)
    check(#g.P.q == 1 and g.P.cur == nil, 'the entry waits in the FIFO')
    run(g, 1)
    check(receipt(g, "return 'MGS_COOP_WORLD_WARP_V1' -- 1 1") and sent(g, 1, 'SetForceRealize') == 1,
        'WARP_V1 names only seq and GameObjectId; an unrealized guard is force-realized once')
    check(g.enemyCalls[1] == 'SetEnableRestrictNotice:1' and g.env.e.MgsCoopDebug.enemy.ignored.sol_1 and g.W.pinned[ENT1],
        'restrict notice (I flag) and area pin on the guest copy')
    run(g, 2)
    check(#placeTx(g, 35) == 0 and receiptCount(g, 'WORLD_WARP_V1') == 3, 'no ACK before the 2-frame readback; no send after landing')
    run(g, 2)
    local ack = placeTx(g, 35)[1]
    check(ack and ack.v[1] == 1 and ack.v[2] == ENT1 and ack.v[3] == 0 and ack.v[4] == 0 and ack.v[5] == 0,
        'ACK: applied, horizontal error 0 cm')
    check(sent(g, 1, 'SetForceRealize', false) == 1 and g.P.cur == nil and receiptCount(g, 'WORLD_WARP_V1') == 3,
        'force-realize released after the readback')
    check(receipt(g, "-- 1 remote_spawn;seq=1;ent=3000000001;state=landed;mode=warp;come=0;err_m=0.00"), 'guest menu history row')
    local menuEnemy = g.env.e.MgsCoopDebug.enemy
    check(menuEnemy.used.sol_1 and menuEnemy.last and menuEnemy.last.position[1] == 5 and menuEnemy.last.mission == 30010,
        'P flag and goto_spawn on the guest')
    g.env.vars.playerPosX, g.env.vars.playerPosY, g.env.vars.playerPosZ = 5, 1.3, 9
    local ok, text = g.P.report('spawn_sync', {partnerPose = {5000, 1300, 1000, 40}})
    check(ok and text == 'remote_spawn;seq=1;ent=3000000001;state=landed;mode=warp;come=0;err_m=0.00;partner_m=5.00;player_m=3.00;goid_match=1',
        'spawn_sync: landing, distance to the partner body and to the guest player: ' .. tostring(text))
    ok, text = g.P.report('spawn_sync', {})
    check(ok and text:find(';partner_m=unknown;player_m=3.00;', 1, true), 'no partner pose: unknown')
    deliver(g, h, 35)
    check(h.P.n.acked == 1 and h.P.n.refused == 0 and
        receipt(h, "-- 1 guest_spawn;seq=1;ent=3000000001;kind=warp;result=applied;why=none;err_m=0.00"),
        'host counts the applied ACK and adds the history row')
    ok, text = h.P.report('world_status')
    check(ok and text:find('^world;paired=1;eff=3;w1=0;roster=match;level=3;') and
        text:find(';sent=1;acked=1;corrected=0;refused=0;', 1, true), 'world_status on the host: ' .. text)
    ok, text = h.P.report('spawn_sync')
    check(ok and text:find('^guest_spawn;seq=1;'), 'spawn_sync on the host reports the last guest answer')
    -- Settle window: moved >= 1 m -> a correction; the guest re-warps only after two samples over 6 m.
    h.positions[1] = {5.0, 1.3, 6.0}
    h.now = h.now + 1000; h.P.tick(h.now)
    check(#placeTx(h, 34) == 0, 'no correction while the guard stays put (under 5 s)')
    h.positions[1] = {7.0, 1.3, 6.0}
    h.now = h.now + 1000; h.P.tick(h.now)
    local corr = placeTx(h, 34)[1]
    check(corr and corr.v[3] == 3 and corr.v[4] == 7000 and corr.v[5] == 400 and corr.v[6] == 6000 and corr.v[7] == 1571,
        'correction: current position, y minus the body offset, heading as yaw')
    deliver(h, g, 34)
    check(#g.P.q == 0 and g.logs[#g.logs]:find('event=place_div;seq=2;ent=3000000001;div_m=2.0;n=0;fix=0', 1, true),
        'divergence under 6 m is only logged')
    for i = 1, 2 do
        h.positions[1] = {7.0 + 8 * i, 1.3, 6.0}
        h.now = h.now + 1000; h.P.tick(h.now)
        deliver(h, g, 34)
    end
    check(#g.P.q == 1 and g.P.q[1].kind == 3 and g.logs[#g.logs]:find(';n=2;fix=1', 1, true), 'two samples over 6 m queue one correction')
    g.warp = {{r = 0, h = 1, leaf = 'engine'}}
    g.positions[1] = {23.0, 1.3, 6.0}
    run(g, 4)
    ack = placeTx(g, 35)[1]
    check(ack and ack.v[3] == 0 and #g.P.q == 0 and not receipt(g, 'remote_spawn;seq=4'), 'correction applied without a history row')
    deliver(g, h, 35)
    check(h.P.n.corrected == 1 and h.P.n.acked == 1, 'corrections are counted apart from placements')
    -- A same-epoch re-pair re-sends the tracked guard at once.
    local before = #placeTx(h, 34)
    for _, f in ipairs(h.W.pairedHooks) do f(false) end
    h.now = h.now + 10; h.P.tick(h.now)
    check(#placeTx(h, 34) == before + 1, 're-pair re-announces the tracked guard as a correction')
    h.life[1] = 4
    h.now = h.now + 5000; h.P.tick(h.now)
    check(next(h.P.live) == nil, 'tracking ends when the guard is no longer NORMAL')
    -- Route mode (kind 2): the hash resolves through the guest's route sets; retail sequence; readback error.
    h.tx, g.tx = {}, {}
    g.env.mvars.ene_routeSetsDefine = {cp_a = {sneak_day = {{'rt_x', 'rt_a'}}, caution = {{'rt_b'}}}, broken = 7}
    local seq = h.P.seq + 1
    check(h.P.share(2, 3000000002, 40000, 2000, 50000, 0, 1, 'rt_a') == 'sync=world;seq=' .. seq, 'route announce')
    check(placeTx(h, 34)[1].v[9] == 7001, 'route hash on the wire')
    g.positions[2] = {40.5, 2, 50}
    deliver(h, g, 34)
    run(g, 4)
    check(sent(g, 2, 'SetSneakRoute') == 1 and sent(g, 2, 'SetCautionRoute') == 1 and sent(g, 2, 'SetAlertRoute') == 1 and
        g.enemyCalls[#g.enemyCalls] == 'SetEnable:2' and sent(g, 2, 'SetForceRealize') == 1, 'retail route sequence on the guest copy')
    ack = placeTx(g, 35)[1]
    check(ack and ack.v[3] == 0 and ack.v[5] == 50 and sent(g, 2, 'SetDiscoveryPlayer') == 1 and g.P.come[3000000002],
        'route landing: 0.5 m from the host readback; come applied')
    check(receipt(g, 'remote_spawn;seq=' .. seq .. ';ent=3000000002;state=landed;mode=route;come=1;err_m=0.50'), 'route history row')
    g.tx, h.tx = {}, {}
    check(h.P.share(2, 3000000002, 0, 0, 0, 0, 0, 'rt_unknown') == 'sync=world;seq=' .. (seq + 1), 'unknown route announced')
    deliver(h, g, 34); run(g, 2)
    ack = placeTx(g, 35)[1]
    check(ack and ack.v[3] == 1 and g.P.WHY[ack.v[4]] == 'route_unknown', 'unknown route refused')
    -- Per-soldier actions (kinds 4-8): one call each on the guest copy.
    g.tx, h.tx = {}, {}
    g.enemyCalls = {}
    for _, k in ipairs({4, 5, 6, 7, 8}) do h.P.share(k, 3000000003) end
    deliver(h, g, 34); run(g, 5)
    check(sent(g, 3, 'SetDiscoveryPlayer') == 1 and table.concat(g.enemyCalls, ',') ==
        'SetEnableRestrictNotice:3,SetDisableRestrictNotice:3,SetDisable:3,SetEnable:3', 'come, ignore, notice, disable, enable')
    check(#placeTx(g, 35) == 5 and receipt(g, ';ent=3000000003;state=landed;mode=enable;'), 'each action answered')
    -- Refusals: terminal copy, native refusals, an old loader, realize timeout, unknown soldier, busy FIFO.
    local function refusal(setup, expectWhy, frames)
        local hh, gg = placeFixture(true), placeFixture(false)
        if setup then setup(gg) end
        hh.P.share(1, ENT1, 1000, 0, 1000, 0, 2)
        deliver(hh, gg, 34); run(gg, frames or 3)
        local a = placeTx(gg, 35)[1]
        return a and a.v[3] == 1 and gg.P.WHY[a.v[4]] == expectWhy, gg
    end
    check(refusal(function(gg) gg.life[1] = 1 end, 'terminal_local'), 'a dead copy is never revived or moved')
    check(refusal(function(gg) gg.warp = {{refused = 'contract'}} end, 'native_off'), 'contract refusal')
    check(refusal(function(gg) gg.warp = {{refused = 'grant'}} end, 'grant'), 'no grant')
    check(refusal(function(gg) gg.warp = {{refused = 'range'}} end, 'range'), 'range')
    check(refusal(function(gg) gg.warp = {{refused = 'anomaly'}} end, 'native_other'), 'other native refusals')
    check(refusal(function(gg) gg.warp = {'literal'} end, 'native_off'), 'an old loader leaves the receipt literal')
    local okTimeout, timed = refusal(function(gg) gg.warp = {{r = -1, h = 0}} end, 'realize_timeout', 160)
    check(okTimeout and receiptCount(timed, 'WORLD_WARP_V1') == 150 and sent(timed, 1, 'SetForceRealize', false) == 1,
        'realize timeout after 150 updates releases force-realize')
    check(receipt(timed, '-- 0 remote_spawn;seq=1;ent=3000000001;state=refused;mode=warp;come=0;why=realize_timeout;native=none'),
        'refusal history row')
    check(refusal(function(gg) gg.W.ent[ENT1].id = 65535 end, 'unmapped'), 'NULL GameObjectId refused')
    check(refusal(function(gg) gg.W.rosterMatch = false end, 'roster'), 'guest roster mismatch refused')
    check(refusal(function(gg) gg.inGame = false end, 'not_ready'), 'not in game refused')
    local hb, gb = placeFixture(true), placeFixture(false)
    for _ = 1, 9 do hb.P.share(5, ENT1) end
    deliver(hb, gb, 34)
    local busy = placeTx(gb, 35)[1]
    check(#gb.P.q == 8 and busy and busy.v[1] == 9 and gb.P.WHY[busy.v[4]] == 'busy', 'a full FIFO refuses busy')
    -- Unpair: queue dropped, force-realize released, nothing answered.
    local hu, gu = placeFixture(true), placeFixture(false)
    gu.warp = {{r = -1, h = 0}}
    hu.P.share(1, ENT1, 1000, 0, 1000, 0, 2); hu.P.share(5, ENT1)
    deliver(hu, gu, 34); run(gu, 2)
    for _, f in ipairs(gu.W.unpairedHooks) do f(1) end
    check(gu.P.cur == nil and #gu.P.q == 0 and sent(gu, 1, 'SetForceRealize', false) == 1 and #placeTx(gu, 35) == 0 and
        not gu.W.rosterMatch, 'unpair drops the queue and releases force-realize')
    -- Host area inference (W4): no trap yet -> the area of the nearest readable soldier's CP.
    local ih, ig = placeFixture(true), placeFixture(false)
    ih.W.activeCp, ih.A.tx, ih.A.pairEpoch, ih.tx, ih.W.w1 = nil, nil, nil, {}, false
    ih.A.areaEnemy = {cpList = {{cp_b = true}, {cp_a = true}}}
    ih.env.vars.playerPosX, ih.env.vars.playerPosZ = 0, 0
    for i = 1, 4 do ih.positions[i] = {3 + i, 0, 0} end
    ih.positions[5] = {400, 0, 0}
    ih.life[1] = 1
    ih.A.start()
    check(ih.W.inferredArea == 2 and ih.W.inferredCp[100] and ih.W.inferredCp[300] and not ih.W.inferredCp[200] and
        logged(ih, 'event=area;inferred=2;nearest_m=4'), 'inferred area from the nearest soldier (bookkeeping only)')
    check(#ih.setOut == 0 and ih.W.activeCp == nil and ih.E.active(200, 3000000005),
        'inference never calls SetOutOfArea and leaves outcome application permissive')
    capture(ih); transfer(ih, ig)
    check(ig.W.w1 and logged(ig, 'step=accepted') and logged(ig, 'unknown=1'), 'the inferred area makes the snapshot acceptable')
    ok, text = ih.P.report('world_status')
    check(text:find(';area=inferred2;', 1, true), 'world_status names the inferred area')
    local none = placeFixture(true)
    none.W.activeCp, none.A.tx, none.A.pairEpoch = nil, nil, nil
    none.A.areaEnemy = {cpList = {{cp_z = true}}}
    none.A.start()
    check(none.W.inferredCp == nil and none.W.inferredArea == nil and logged(none, 'step=area;inferred=none'),
        'no soldier of a listed CP: nothing inferred, the NORMAL fallback stays unproven')
    local trapped = placeFixture(true)
    trapped.W.activeCp, trapped.A.tx, trapped.A.pairEpoch = nil, nil, nil
    trapped.W.areaState.localArea = 3
    trapped.A.areaEnemy = {cpList = {{cp_a = true}}}
    trapped.A.start()
    check(trapped.W.inferredArea == nil and trapped.W.inferredCp == nil, 'a real trap area is never overridden')
end
print(checks .. ' W4 alignment/recovery and spawn sync checks passed; real apply rules, no game')
